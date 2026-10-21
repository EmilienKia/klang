/*
 * Direct compiler seam for synchronous borrowed-temporary cleanup.
 * Copyright 2026 Emilien Kia
 * SPDX-License-Identifier: Apache-2.0
 */
#include <catch2/catch_all.hpp>
#include "helpers.hpp"
#include "gen/gen_arguments.hpp"
#include "gen/gen_helpers.hpp"
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <new>
#include <string_view>
#include <utility>

namespace {

int environments_created = 0;
int environments_dropped = 0;

void drop_environment(void* environment) {
    ++environments_dropped;
    std::free(environment);
}

void* make_environment() {
    auto* environment = std::malloc(sizeof(void (*)(void*)));
    if (!environment) throw std::bad_alloc();
    *static_cast<void (**)(void*)>(environment) = drop_environment;
    ++environments_created;
    return environment;
}

int invoke_environment(void*) { return 7; }

enum class scoped_case {
    normal, unwind, array_normal, array_unwind, array_cleanup_throw,
    take_owner, pending_owner, callable, callable_unwind, return_callable, pending_callable,
    borrowed_owner, borrowed_owner_unwind, union_normal, union_unwind,
    union_cleanup_throw, union_array_normal, union_array_unwind,
    union_array_cleanup_throw, return_union, pending_union, return_array, pending_array
};

class argument_scope_generator : public k::model::gen::implementation_generator {
    using type = k::model::type;
    using function = k::model::function;
    using struct_type = k::model::struct_type;
    using sized_array_type = k::model::sized_array_type;

    std::shared_ptr<function> model_function(const char* name) {
        const auto functions = _unit.get_root_namespace()->get_functions(name);
        REQUIRE(functions.size() == 1);
        return functions.front();
    }

    llvm::PointerType* pointer_type() {
        return llvm::PointerType::get(_context->llvm_context(), 0);
    }

    llvm::AllocaInst* allocate(llvm::Type* type, const char* name) {
        auto* function = _builder->GetInsertBlock()->getParent();
        llvm::IRBuilder<> entry(&function->getEntryBlock(), function->getEntryBlock().begin());
        return entry.CreateAlloca(type, nullptr, name);
    }

    llvm::Value* call(const char* name, const std::vector<llvm::Value*>& arguments = {}) {
        auto* callee = ensure_function_declared(*model_function(name));
        auto* instruction = llvm::cast<llvm::CallBase>(
            create_call_or_invoke(callee->getFunctionType(), callee, arguments));
        instruction->setCallingConv(callee->getCallingConv());
        instruction->setAttributes(callee->getAttributes());
        return instruction;
    }

    template<typename Pointer>
    llvm::Constant* address(Pointer pointer) {
        return llvm::ConstantExpr::getIntToPtr(
            _builder->getInt64(reinterpret_cast<uintptr_t>(pointer)), pointer_type());
    }

    llvm::Value* callable() {
        auto* factory_type = llvm::FunctionType::get(pointer_type(), false);
        auto* environment = create_call_or_invoke(factory_type, address(make_environment), {});
        llvm::Value* value = llvm::UndefValue::get(_context->get_or_create_callable_llvm_type());
        value = _builder->CreateInsertValue(value, address(invoke_environment), 0);
        return _builder->CreateInsertValue(value, environment, 1);
    }

public:
    using implementation_generator::implementation_generator;

    void generate(const char* name, scoped_case mode) {
        auto model = model_function(name);
        auto* native = ensure_function_declared(*model);
        native->deleteBody();
        _builder->SetInsertPoint(llvm::BasicBlock::Create(_context->llvm_context(), "entry", native));
        _exc_ptr_slot = nullptr;
        _exc_sel_slot = nullptr;
        _value = nullptr;
        REQUIRE(_cleanup_lpad_stack.empty());
        REQUIRE(_expression_temporaries.empty());
        auto resource = _unit.get_root_namespace()->get_aggregate("Resource");
        auto resource_type = resource->get_struct_type();
        auto* resource_native = _context->get_llvm_type(resource_type);
        auto* destructor = ensure_function_declared(*resource->get_destructor());
        const std::map<std::shared_ptr<function>, llvm::Function*> cleanup_functions{
            {resource->get_destructor(), destructor}};
        auto result_type = mode == scoped_case::return_array
            ? model_function("makeOwnerArray")->get_return_type() : model->get_return_type();
        llvm::Value* callable_storage = nullptr;
        {
            call_argument_cleanup_scope frame(*this, true, result_type);
            const bool array = mode == scoped_case::array_normal || mode == scoped_case::array_unwind
                || mode == scoped_case::array_cleanup_throw;
            const bool union_array = mode == scoped_case::union_array_normal
                || mode == scoped_case::union_array_unwind
                || mode == scoped_case::union_array_cleanup_throw;
            const bool union_value = mode == scoped_case::union_normal || mode == scoped_case::union_unwind
                || mode == scoped_case::union_cleanup_throw;
            if (union_value || union_array) {
                auto bundle_type = std::dynamic_pointer_cast<struct_type>(model_function("makeBundle")->get_return_type());
                REQUIRE(bundle_type);
                if (union_array) {
                    auto array_type = bundle_type->get_array(2);
                    auto* storage = allocate(_context->get_llvm_type(array_type), "original_union_array");
                    auto* count = _builder->CreateStructGEP(array_type->get_llvm_struct_type(), storage,
                        sized_array_type::FIELD_SIZE);
                    _builder->CreateStore(_builder->getInt32(0), count);
                    CHECK(frame.materialize_borrowed(array_type, storage) == storage);
                    auto* data = _builder->CreateStructGEP(array_type->get_llvm_struct_type(), storage,
                        sized_array_type::FIELD_DATA);
                    for (unsigned index = 0; index < 2; ++index) {
                        auto* element = _builder->CreateConstInBoundsGEP2_32(
                            array_type->get_llvm_data_array_type(), data, 0, index);
                        call("makeBundle", {element, _builder->getInt32(index * 2 + 1)});
                        _builder->CreateStore(_builder->getInt32(index + 1), count);
                    }
                    if (mode == scoped_case::union_array_cleanup_throw) call("arm", {_builder->getInt32(4)});
                } else {
                    auto* storage = allocate(_context->get_llvm_type(bundle_type), "original_union");
                    call("makeBundle", {storage, _builder->getInt32(1)});
                    auto* cleanup = k::model::gen::get_or_create_union_cleanup_function(
                        get_module(), cleanup_functions, *bundle_type->get_union());
                    _expression_temporaries.push_back({storage, cleanup, nullptr});
                    CHECK(frame.materialize_borrowed(bundle_type, storage) == storage);
                    CHECK_FALSE(is_expression_temporary(storage));
                    if (mode == scoped_case::union_cleanup_throw) call("arm", {_builder->getInt32(2)});
                }
            } else if (mode == scoped_case::borrowed_owner || mode == scoped_case::borrowed_owner_unwind) {
                auto owner_type = model_function("makeOwner")->get_return_type();
                auto* value = call("makeOwner", {_builder->getInt32(1)});
                frame.materialize_borrowed(owner_type, value);
            } else if (array) {
                auto array_type = resource_type->get_array(2);
                auto* storage = allocate(_context->get_llvm_type(array_type), "original_array");
                auto* count = _builder->CreateStructGEP(array_type->get_llvm_struct_type(), storage,
                    sized_array_type::FIELD_SIZE);
                _builder->CreateStore(_builder->getInt32(0), count);
                auto* data = _builder->CreateStructGEP(array_type->get_llvm_struct_type(), storage,
                    sized_array_type::FIELD_DATA);
                for (unsigned index = 0; index < 2; ++index) {
                    auto* element = _builder->CreateConstInBoundsGEP2_32(
                        array_type->get_llvm_data_array_type(), data, 0, index);
                    call("makeValue", {element, _builder->getInt32(index + 1)});
                    _builder->CreateStore(_builder->getInt32(index + 1), count);
                }
                _expression_temporaries.push_back({storage, nullptr, array_type});
                CHECK(frame.materialize_borrowed(array_type, storage) == storage);
                CHECK_FALSE(is_expression_temporary(storage));
                if (mode == scoped_case::array_cleanup_throw) call("arm", {_builder->getInt32(2)});
            } else if (mode == scoped_case::callable || mode == scoped_case::callable_unwind) {
                auto callable_type = model_function("returnCallableProbe")->get_return_type();
                auto* storage = allocate(_context->get_llvm_type(callable_type), "original_callable");
                callable_storage = storage;
                _builder->CreateStore(callable(), storage);
                CHECK(frame.materialize_borrowed(callable_type, storage) == storage);
            } else {
                auto* storage = allocate(resource_native, "original_resource");
                call("makeValue", {storage, _builder->getInt32(1)});
                _expression_temporaries.push_back({storage, destructor, nullptr});
                CHECK(frame.materialize_borrowed(resource_type, storage) == storage);
                CHECK_FALSE(is_expression_temporary(storage));
                if (mode == scoped_case::unwind) {
                    auto* second = allocate(resource_native, "second_resource");
                    call("makeValue", {second, _builder->getInt32(2)});
                    _expression_temporaries.push_back({second, destructor, nullptr});
                    CHECK(frame.materialize_borrowed(resource_type, second) == second);
                }
            }
            if (mode == scoped_case::unwind) call("fail");
            if (mode == scoped_case::unwind || mode == scoped_case::array_unwind
                || mode == scoped_case::union_unwind || mode == scoped_case::union_array_unwind
                || mode == scoped_case::borrowed_owner_unwind || mode == scoped_case::callable_unwind) {
                if (mode != scoped_case::unwind) {
                    frame.transfer();
                    call("fail");
                }
                _value = _builder->getInt32(0);
            } else if (mode == scoped_case::return_union || mode == scoped_case::pending_union
                || mode == scoped_case::return_array || mode == scoped_case::pending_array) {
                auto* wrapper = llvm::StructType::get(_context->llvm_context(),
                    {_builder->getInt32Ty(), _context->get_llvm_type(result_type)});
                auto* enclosing = allocate(wrapper, "pending_result_container");
                auto* storage = _builder->CreateStructGEP(wrapper, enclosing, 1, "pending_result");
                if (mode == scoped_case::pending_array || mode == scoped_case::return_array)
                    call("makeOwnerArray", {storage, _builder->getInt32(4)});
                else
                    call("makeBundle", {storage, _builder->getInt32(4)});
                frame.transfer();
                _value = storage;
                if (mode == scoped_case::pending_union || mode == scoped_case::pending_array)
                    call("arm", {_builder->getInt32(1)});
            } else if (mode == scoped_case::take_owner || mode == scoped_case::pending_owner) {
                auto owner_type = model_function("makeOwner")->get_return_type();
                auto* owner_value = call("makeOwner", {_builder->getInt32(4)});
                auto* slot = frame.materialize_borrowed(owner_type, owner_value);
                auto holder_type = std::dynamic_pointer_cast<struct_type>(type::remove_const(
                    model_function("takeRetained")->parameters().front()->get_type()->get_subtype()));
                k::model::gen::argument_source source;
                source.logical = owner_type;
                source.binding = owner_type;
                source.referent = resource_type;
                source.category = 5;
                source.capabilities = 1u | 2u | 4u | 8u | 32u;
                source.indirect = true;
                auto* holder = emit_borrowed_argument(source, slot, holder_type, nullptr, false);
                frame.transfer();
                _value = call("takeRetained", {holder});
                auto* current = _builder->CreateLoad(pointer_type(), slot);
                call("recordClear", {_builder->CreateICmpEQ(current,
                    llvm::ConstantPointerNull::get(pointer_type()))});
                if (mode == scoped_case::pending_owner) call("arm", {_builder->getInt32(1)});
            } else if (mode == scoped_case::return_callable || mode == scoped_case::pending_callable) {
                frame.transfer();
                _value = callable();
                if (mode == scoped_case::pending_callable) call("arm", {_builder->getInt32(1)});
            } else {
                frame.transfer();
                call("observeLive");
                _value = _builder->getInt32(0);
            }
        }
        if (mode == scoped_case::return_array) {
            call("observeArray", {_value});
            k::model::gen::emit_value_cleanup(_builder.get(), get_module(), cleanup_functions,
                _value, result_type, make_virtual_call_emitter());
            _value = _builder->getInt32(0);
        }
        if (callable_storage) {
            auto* slot = _builder->CreateStructGEP(_context->get_or_create_callable_llvm_type(),
                callable_storage, 1);
            auto* environment = _builder->CreateLoad(pointer_type(), slot);
            call("recordClear", {_builder->CreateICmpEQ(environment,
                llvm::ConstantPointerNull::get(pointer_type()))});
        }
        emit_expression_temporaries_cleanup();
        if (k::model::gen::needs_sret_return(model->get_return_type())) {
            auto* result = _builder->CreateLoad(_context->get_llvm_type(result_type), _value);
            _builder->CreateStore(result, native->getArg(0));
            k::model::gen::emit_transferred_value_reset(_builder.get(), _value, result_type);
            _builder->CreateRetVoid();
        } else {
            _builder->CreateRet(_value);
        }
        REQUIRE_FALSE(llvm::verifyFunction(*native, &llvm::errs()));
    }
};

} // namespace

TEST_CASE("Argument: actual scoped materialization retains sources and guards pending results",
          "[libk][argument][lifetime][scoped-materialization]") {
    auto compiler = compile_model_with_stdlib(R"K(
        module argument_scopes;
        made : int = 0;
        copies : int = 0;
        dropped : int = 0;
        order : int = 0;
        failOnDrop : int = 0;
        cleared : bool = false;
        observedLive : bool = false;
        class Stop : FatalError { public Stop() : FatalError(55) {} }
        struct Resource {
            public value : int;
            Resource(n : int) : value(n) { made += 1; }
            Resource(other : const Resource&) : value(other.value) { copies += 1; }
            ~Resource() {
                dropped += 1;
                order = order * 10 + value;
                if (failOnDrop == value) { failOnDrop = 0; throw Stop(); }
            }
        }
        makeValue(n : int) : Resource { return Resource(n); }
        makeOwner(n : int) : Resource! { return new Resource(n); }
        union Bundle { empty : int; owners : Resource![2]; }
        makeBundle(n : int) : Bundle {
            result : Bundle;
            values : Resource![2]{new Resource(n), new Resource(n + 1)};
            result.owners = #values;
            return result;
        }
        makeOwnerArray(n : int) result : Resource![2] {
            result[0] = new Resource(n);
            result[1] = new Resource(n + 1);
        }
        takeRetained(holder : const Argument&) : Resource! { return holder.takeOwner<Resource>(); }
        recordClear(value : bool) { cleared = value; }
        arm(value : int) { failOnDrop = value; }
        fail() { throw Stop(); }
        observeLive() { observedLive = dropped == 0; }
        observeArray(values : Resource![2]&) {
            observedLive = dropped == 1 && values[0].value == 4 && values[1].value == 5;
        }
        reset() { made = 0; copies = 0; dropped = 0; order = 0; failOnDrop = 0; cleared = false; observedLive = false; }
        normalProbe() : int { return 0; }
        unwindProbe() : int { return 0; }
        arrayProbe() : int { return 0; }
        arrayUnwindProbe() : int { return 0; }
        arrayCleanupProbe() : int { return 0; }
        ownerProbe() : Resource! { return null; }
        ownerFailureProbe() : Resource! { return null; }
        callableProbe() : int { return 0; }
        callableUnwindProbe() : int { return 0; }
        returnCallableProbe() : !():int { return null; }
        callableFailureProbe() : !():int { return null; }
        borrowedOwnerProbe() : int { return 0; }
        borrowedOwnerUnwindProbe() : int { return 0; }
        unionProbe() : int { return 0; }
        unionUnwindProbe() : int { return 0; }
        unionCleanupProbe() : int { return 0; }
        unionArrayProbe() : int { return 0; }
        unionArrayUnwindProbe() : int { return 0; }
        unionArrayCleanupProbe() : int { return 0; }
        unionResultProbe() : Bundle { result : Bundle; return result; }
        unionResultFailureProbe() : Bundle { result : Bundle; return result; }
        arrayResultFailureProbe() result : Resource![2] {}
        arrayDeliveryProbe() : int { return 0; }
        normal() : int {
            reset(); normalProbe();
            return made == 1 && copies == 0 && dropped == 1 && observedLive ? 0 : 1;
        }
        unwind() : int {
            reset();
            try { unwindProbe(); } catch (e : Stop&) {
                return made == 2 && copies == 0 && dropped == 2 && order == 21 ? 0 : 1;
            }
            return 2;
        }
        arrayNormal() : int {
            reset(); arrayProbe();
            return made == 2 && copies == 0 && dropped == 2 && order == 21 && observedLive ? 0 : 1;
        }
        arrayUnwind() : int {
            reset();
            try { arrayUnwindProbe(); } catch (e : Stop&) {
                return made == 2 && copies == 0 && dropped == 2 && order == 21 ? 0 : 1;
            }
            return 2;
        }
        arrayCleanupThrows() : int {
            reset();
            try { arrayCleanupProbe(); } catch (e : Stop&) {
                return made == 2 && copies == 0 && dropped == 2 && order == 21 ? 0 : 1;
            }
            return 2;
        }
        owner() : int {
            reset();
            {
                result : Resource! = ownerProbe();
                if (!cleared || result == null || result.value != 4 || dropped != 1) return 1;
            }
            return made == 2 && copies == 0 && dropped == 2 && order == 14 ? 0 : 2;
        }
        ownerFailure() : int {
            reset();
            try { result : Resource! = ownerFailureProbe(); } catch (e : Stop&) {
                return cleared && made == 2 && dropped == 2 && order == 14 && copies == 0 ? 0 : 1;
            }
            return 2;
        }
        callable() : int { reset(); callableProbe(); return observedLive && cleared ? 0 : 1; }
        callableUnwind() : int {
            reset();
            try { callableUnwindProbe(); } catch (e : Stop&) { return 0; }
            return 1;
        }
        returnCallable() : int {
            reset();
            result : !():int = returnCallableProbe();
            return result() == 7 && made == 1 && dropped == 1 && copies == 0 ? 0 : 1;
        }
        callableFailure() : int {
            reset();
            try { result : !():int = callableFailureProbe(); } catch (e : Stop&) {
                return made == 1 && dropped == 1 && copies == 0 ? 0 : 1;
            }
            return 2;
        }
        borrowedOwner() : int {
            reset(); borrowedOwnerProbe();
            return made == 1 && dropped == 1 && copies == 0 && observedLive ? 0 : 1;
        }
        borrowedOwnerUnwind() : int {
            reset();
            try { borrowedOwnerUnwindProbe(); } catch (e : Stop&) {
                return made == 1 && dropped == 1 && copies == 0 ? 0 : 1;
            }
            return 2;
        }
        unionNormal() : int {
            reset(); unionProbe();
            return made == 2 && dropped == 2 && order == 21 && copies == 0 && observedLive ? 0 : 1;
        }
        unionUnwind() : int {
            reset();
            try { unionUnwindProbe(); } catch (e : Stop&) {
                return made == 2 && dropped == 2 && order == 21 && copies == 0 ? 0 : 1;
            }
            return 2;
        }
        unionCleanupThrows() : int {
            reset();
            try { unionCleanupProbe(); } catch (e : Stop&) {
                return made == 2 && dropped == 2 && order == 21 && copies == 0 ? 0 : 1;
            }
            return 2;
        }
        unionArrayNormal() : int {
            reset(); unionArrayProbe();
            return made == 4 && dropped == 4 && order == 4321 && copies == 0 && observedLive ? 0 : 1;
        }
        unionArrayUnwind() : int {
            reset();
            try { unionArrayUnwindProbe(); } catch (e : Stop&) {
                return made == 4 && dropped == 4 && order == 4321 && copies == 0 ? 0 : 1;
            }
            return 2;
        }
        unionArrayCleanupThrows() : int {
            reset();
            try { unionArrayCleanupProbe(); } catch (e : Stop&) {
                return made == 4 && dropped == 4 && order == 4321 && copies == 0 ? 0 : 1;
            }
            return 2;
        }
        unionResult() : int {
            reset();
            {
                result : Bundle = unionResultProbe();
                if (made != 3 || dropped != 1 || result.owners[1].value != 5) return 1;
            }
            return made == 3 && dropped == 3 && order == 154 && copies == 0 ? 0 : 2;
        }
        unionResultFailure() : int {
            reset();
            try { result : Bundle = unionResultFailureProbe(); } catch (e : Stop&) {
                return made == 3 && dropped == 3 && order == 154 && copies == 0 ? 0 : 1;
            }
            return 2;
        }
        arrayResultFailure() : int {
            reset();
            try { arrayResultFailureProbe(); } catch (e : Stop&) {
                return made == 3 && dropped == 3 && order == 154 && copies == 0 ? 0 : 1;
            }
            return 2;
        }
        arrayDelivery() : int {
            reset(); arrayDeliveryProbe();
            return made == 3 && dropped == 3 && order == 154 && copies == 0 && observedLive ? 0 : 1;
        }
    )K");
    REQUIRE(compiler);
    test_logger logger;
    argument_scope_generator generator(logger, *compiler, compiler->get_context_for_test(), *compiler->get_unit());
    for (auto [name, mode] : {
        std::pair{"normalProbe", scoped_case::normal},
        std::pair{"unwindProbe", scoped_case::unwind},
        std::pair{"arrayProbe", scoped_case::array_normal},
        std::pair{"arrayUnwindProbe", scoped_case::array_unwind},
        std::pair{"arrayCleanupProbe", scoped_case::array_cleanup_throw},
        std::pair{"ownerProbe", scoped_case::take_owner},
        std::pair{"ownerFailureProbe", scoped_case::pending_owner},
        std::pair{"callableProbe", scoped_case::callable},
        std::pair{"callableUnwindProbe", scoped_case::callable_unwind},
        std::pair{"returnCallableProbe", scoped_case::return_callable},
        std::pair{"callableFailureProbe", scoped_case::pending_callable},
        std::pair{"borrowedOwnerProbe", scoped_case::borrowed_owner},
        std::pair{"borrowedOwnerUnwindProbe", scoped_case::borrowed_owner_unwind},
        std::pair{"unionProbe", scoped_case::union_normal},
        std::pair{"unionUnwindProbe", scoped_case::union_unwind},
        std::pair{"unionCleanupProbe", scoped_case::union_cleanup_throw},
        std::pair{"unionArrayProbe", scoped_case::union_array_normal},
        std::pair{"unionArrayUnwindProbe", scoped_case::union_array_unwind},
        std::pair{"unionArrayCleanupProbe", scoped_case::union_array_cleanup_throw},
        std::pair{"unionResultProbe", scoped_case::return_union},
        std::pair{"unionResultFailureProbe", scoped_case::pending_union},
        std::pair{"arrayDeliveryProbe", scoped_case::return_array},
        std::pair{"arrayResultFailureProbe", scoped_case::pending_array}}) {
        generator.generate(name, mode);
    }
    REQUIRE_FALSE(llvm::verifyModule(compiler->get_context_for_test()->module(), &llvm::errs()));
    auto jit = compiler->to_jit();
    REQUIRE(jit);
    for (const auto* name : {"normal", "unwind", "arrayNormal", "arrayUnwind", "arrayCleanupThrows",
                             "owner", "ownerFailure", "callable", "callableUnwind", "returnCallable", "callableFailure",
                             "borrowedOwner", "borrowedOwnerUnwind", "unionNormal", "unionUnwind",
                             "unionCleanupThrows", "unionArrayNormal", "unionArrayUnwind",
                             "unionArrayCleanupThrows", "unionResult", "unionResultFailure", "arrayResultFailure",
                             "arrayDelivery"}) {
        CAPTURE(name);
        environments_created = 0;
        environments_dropped = 0;
        auto function = jit->lookup_symbol<int (*)()>(name);
        REQUIRE(function);
        CHECK(function() == 0);
        if (std::string_view(name).find("Callable") != std::string_view::npos
            || std::string_view(name).starts_with("callable")) {
            CHECK(environments_created == 1);
            CHECK(environments_dropped == 1);
        }
    }
}
