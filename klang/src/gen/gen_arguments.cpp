/*
 * Inline borrowed binding capture and checked typed access. No target invokers.
 * Copyright 2026 Emilien Kia
 * SPDX-License-Identifier: Apache-2.0
 */
#include "gen_arguments.hpp"
#include "gen_intrinsics.hpp"
#include "generators.hpp"
#include "gen_rtti.hpp"
#include "../model/statements.hpp"
#include "../errors.hpp"
#include <llvm/ADT/ScopeExit.h>
#include <iterator>
#include <stdexcept>

namespace k::model::gen {

namespace {

std::string function_name(const function& fn) {
    return fn.has_tpl_args() ? fn.get_tpl_base_name() : fn.get_short_name();
}

bool named_type(std::shared_ptr<type> t, const std::string& name) {
    t = type::remove_const(t);
    if (auto u = std::dynamic_pointer_cast<unresolved_type>(t)) {
        if (!u->is_resolved()) {
            auto id = u->type_id().to_string();
            return id == name || "::" + id == name || "::k::" + id == name;
        }
        t = u->get_resolved();
    }
    auto st = std::dynamic_pointer_cast<struct_type>(t);
    if (!st || !st->get_struct()) return false;
    const auto id = st->get_struct()->get_fq_name();
    return id == name || "::" + id == name;
}

std::shared_ptr<type> unwrap_alias(std::shared_ptr<type> t) {
    for (unsigned depth = 0; t && depth < 64; ++depth) {
        if (auto alias = std::dynamic_pointer_cast<alias_type>(t)) {
            t = alias->get_underlying();
        } else if (auto unresolved = std::dynamic_pointer_cast<unresolved_type>(t);
            unresolved && unresolved->is_resolved()) {
            t = unresolved->get_resolved();
        } else {
            return t;
        }
    }
    return t;
}

bool readonly_value(const std::shared_ptr<type>& t) {
    return type::is_const(unwrap_alias(t));
}

bool stable_lvalue(const std::shared_ptr<expression>& expr) {
    if (!expr) return false;
    if (auto symbol = std::dynamic_pointer_cast<symbol_expression>(expr))
        return symbol->is_variable_def();
    if (auto member = std::dynamic_pointer_cast<member_of_object_expression>(expr))
        return member->symbol().is_variable_def() && stable_lvalue(member->sub_expr());
    if (auto member = std::dynamic_pointer_cast<member_of_pointer_expression>(expr))
        return member->symbol().is_variable_def() && stable_lvalue(member->sub_expr());
    if (auto subscript = std::dynamic_pointer_cast<subscript_expression>(expr))
        return !subscript->has_operator_overload() && stable_lvalue(subscript->left());
    if (auto deref = std::dynamic_pointer_cast<dereference_expression>(expr))
        return stable_lvalue(deref->sub_expr());
    if (auto drain = std::dynamic_pointer_cast<drain_expression>(expr))
        return stable_lvalue(drain->sub_expr());
    if (auto cast = std::dynamic_pointer_cast<cast_expression>(expr))
        return type::is_reference(expr->get_type()) && stable_lvalue(cast->sub_expr());
    // A returned reference has the ordinary reference lifetime contract. A
    // pointer/owner/value return is not a source slot and must not be boxed here.
    return std::dynamic_pointer_cast<function_invocation_expression>(expr)
        && type::is_reference(expr->get_type());
}

} // namespace

bool is_canonical_argument_intrinsic(const function& fn, std::string_view intrinsic) {
    auto owner = fn.get_owner();
    if (!owner || (owner->get_fq_name() != "k::Argument"
                   && owner->get_fq_name() != "::k::Argument")) return false;
    const auto name = function_name(fn);
    if (intrinsic == "argument.capture.v1" || intrinsic == "argument.borrow.v1") {
        return name == (intrinsic == "argument.capture.v1" ? "capture" : "borrow")
            && fn.is_static() && fn.parameters().size() == 1
            && type::is_reference(fn.parameters()[0]->get_type())
            && named_type(std::const_pointer_cast<type>(fn.get_return_type()), "::k::Argument");
    }
    if (fn.is_static() || !fn.is_const_member() || !fn.parameters().empty()) return false;
    if (intrinsic == "argument.is_null.v1")
        return name == "_isNull" && fn.get_return_type()
            && fn.get_return_type()->to_string() == "bool";
    auto result = type::remove_const(std::const_pointer_cast<type>(fn.get_return_type()));
    if (intrinsic == "argument.read.v1")
        return name == "read" && type::is_reference(result) && type::is_const(result->get_subtype());
    if (intrinsic == "argument.get.v1") return name == "get" && type::is_pointer(result);
    if (intrinsic == "argument.take_owner.v1") return name == "takeOwner" && type::is_owner(result);
    if (intrinsic == "argument.take.v1") {
        if (name != "take") return false;
        return result && (fn.is_template() || fn.has_tpl_args() || fn.is_instantiation());
    }
    if (intrinsic == "argument.drain.v1") return name == "drain" && type::is_drain(result);
    return false;
}

bool has_argument_intrinsic_contract(const function& fn) {
    const auto op = get_argument_intrinsic(fn);
    if (op == argument_intrinsic::none || op == argument_intrinsic::invalid) return false;
    if (fn.get_existing_block() || fn.has_varargs() || fn.is_deleted()
        || fn.is_abstract_func() || fn.is_extern() || fn.get_annotations().size() != 1) return false;
    if (op != argument_intrinsic::is_null) {
        if (fn.is_template() || !fn.has_tpl_args() || fn.get_tpl_args().size() != 1
            || !fn.get_tpl_args()[0].is_type() || !fn.get_tpl_args()[0].type_arg) return false;
        auto supplied = fn.get_tpl_args()[0].type_arg;
        auto result = std::const_pointer_cast<type>(fn.get_return_type());
        if (op == argument_intrinsic::capture || op == argument_intrinsic::borrow) {
            if (fn.parameters()[0]->has_default_expr()
                || !type::are_equal(fn.parameters()[0]->get_type(), supplied->get_reference())
                || !type::are_equal(result, fn.get_owner()->get_struct_type())) return false;
        } else {
            std::shared_ptr<type> expected;
            switch (op) {
                case argument_intrinsic::read: expected = supplied->get_const()->get_reference(); break;
                case argument_intrinsic::get: expected = supplied->get_pointer(); break;
                case argument_intrinsic::take_owner: expected = supplied->get_owner(); break;
                case argument_intrinsic::take: expected = supplied; break;
                case argument_intrinsic::drain: expected = supplied->get_drain(); break;
                default: return false;
            }
            if (!type::are_equal(result, expected)) return false;
        }
    } else if (fn.is_template() || fn.has_tpl_args()) {
        return false;
    }
    bool canonical_annotation = false;
    for (const auto& annotation : fn.get_annotations()) {
        if (!annotation.resolved_type) continue;
        const auto name = annotation.resolved_type->get_fq_name();
        if (name == "k::annotations::Intrinsic" || name == "::k::annotations::Intrinsic")
            canonical_annotation = true;
    }
    if (!canonical_annotation) return false;
    auto owner = fn.get_owner();
    if (!owner->is_struct()) return false;
    auto model = owner->get_struct_type();
    auto* storage = model
        ? llvm::dyn_cast_or_null<llvm::StructType>(model->get_llvm_type()) : nullptr;
    if (!storage || storage->isOpaque() || storage->isPacked()
        || storage->getNumElements() != argument_field_count) return false;
    for (unsigned index = 0; index < 4; ++index) {
        auto* pointer = llvm::dyn_cast<llvm::PointerType>(storage->getElementType(index));
        if (!pointer || pointer->getAddressSpace() != 0) return false;
    }
    if (!storage->getElementType(4)->isIntegerTy(32)
        || !storage->getElementType(5)->isIntegerTy(32)
        || !storage->getElementType(6)->isIntegerTy(1)) return false;
    const char* fields[] = {"_slot", "_logical", "_binding", "_referent",
                            "_category", "_capabilities", "_indirect"};
    auto descriptor = [](std::shared_ptr<type> t) {
        return t && type::is_resolved(t) && type::is_view(t) && type::is_const(t->get_subtype())
            && named_type(t->get_subtype(), "::k::rtti::Type");
    };
    for (const auto* getter : {"getLogicalType", "getBindingType", "getReferentType"}) {
        auto method = owner->get_function(getter);
        if (!method || method->is_static() || !method->is_const_member()
            || !method->parameters().empty()
            || !descriptor(std::const_pointer_cast<type>(method->get_return_type()))) return false;
    }
    if (owner->is_imported()) {
        // KDI intentionally omits private fields. The canonical producer's local
        // gate validates their semantic contract; imports retain its exact native
        // layout and the public descriptor signatures, never a nominal type guess.
        return true;
    }
    if (owner->variables().size() != argument_field_count || model->fields_size() != argument_field_count)
        return false;
    for (unsigned index = 0; index < argument_field_count; ++index) {
        const auto member = model->get_member(fields[index]);
        if (!member || member->index != index) return false;
        auto variable = std::dynamic_pointer_cast<member_variable_definition>(owner->get_variable(fields[index]));
        if (!variable || variable->get_visibility() != PRIVATE) return false;
        auto t = member->field_type.lock();
        if (!t || t->get_llvm_type() != storage->getElementType(index)) return false;
        if (index == argument_slot) {
            auto primitive = type::is_pointer(t)
                ? std::dynamic_pointer_cast<primitive_type>(t->get_subtype()) : nullptr;
            if (!primitive || primitive->get_type() != primitive_type::BYTE) return false;
        } else if (index <= argument_referent) {
            if (!descriptor(t)) return false;
        } else {
            auto primitive = std::dynamic_pointer_cast<primitive_type>(t);
            if (!primitive || primitive->get_type() !=
                (index == argument_indirect ? primitive_type::BOOL : primitive_type::UNSIGNED_INT)) return false;
        }
    }
    return true;
}

argument_intrinsic get_argument_intrinsic(const function& fn) {
    auto intrinsic = get_intrinsic_name(fn);
    if (!intrinsic || !intrinsic->starts_with("argument.")) return argument_intrinsic::none;
    if (*intrinsic == "argument.capture.v1") return argument_intrinsic::capture;
    if (*intrinsic == "argument.borrow.v1") return argument_intrinsic::borrow;
    if (*intrinsic == "argument.read.v1") return argument_intrinsic::read;
    if (*intrinsic == "argument.get.v1") return argument_intrinsic::get;
    if (*intrinsic == "argument.take_owner.v1") return argument_intrinsic::take_owner;
    if (*intrinsic == "argument.take.v1") return argument_intrinsic::take;
    if (*intrinsic == "argument.drain.v1") return argument_intrinsic::drain;
    if (*intrinsic == "argument.is_null.v1") return argument_intrinsic::is_null;
    return argument_intrinsic::invalid;
}

argument_source describe_argument_source(const std::shared_ptr<expression>& expr) {
    if (!expr || !stable_lvalue(expr))
        throw std::invalid_argument("Argument capture requires an addressable, stable caller binding; "
                                    "explicit capture of a temporary is forbidden");
    argument_source source;
    auto expr_type = expr->get_type();
    if (!expr_type) throw std::invalid_argument("Argument capture source has no resolved type");
    bool readonly_slot = false;
    if (auto symbol = std::dynamic_pointer_cast<symbol_expression>(expr);
        symbol && symbol->is_variable_def()) {
        auto var = symbol->get_variable_def();
        // Symbol typing already applies declaration const to the correct layer.
        // Keep reference/drain bindings, but remove the lvalue transport wrapper
        // on ordinary values and writable addresser slots.
        if (type::is_reference(var->get_type()) || type::is_drain(var->get_type())) {
            source.logical = expr_type;
        } else {
            if (!type::is_reference(expr_type))
                throw std::invalid_argument("Argument capture requires an addressable caller slot");
            source.logical = expr_type->get_subtype();
        }
        source.variable_slot = true;
    } else if (std::dynamic_pointer_cast<drain_expression>(expr)) {
        source.logical = expr_type;
        source.binding = expr_type;
        source.referent = expr_type->get_subtype();
        source.category = 3;
    } else if (std::dynamic_pointer_cast<function_invocation_expression>(expr)) {
        source.logical = expr_type;
        source.binding = expr_type;
        source.referent = expr_type->get_subtype();
        source.category = 4;
    } else {
        if (!type::is_reference(expr_type))
            throw std::invalid_argument("Argument capture requires an addressable caller slot");
        source.logical = expr_type->get_subtype();
    }
    if (!source.logical) throw std::invalid_argument("Argument capture source type is unresolved");
    if (!source.binding) {
        source.binding = source.logical;
        auto binding = source.binding;
        readonly_slot |= type::is_const(source.binding);
        for (unsigned depth = 0; binding && depth < 64; ++depth) {
            binding = unwrap_alias(binding);
            if (!type::is_const(binding)) break;
            readonly_slot = true;
            binding = binding->get_subtype();
        }
        if (type::is_any_indirection(binding)) {
            source.indirect = true;
            source.category = 2;
            source.referent = binding->get_subtype();
            source.capabilities |= 4;
            const bool writable = !readonly_slot && (type::is_pointer(binding)
                || type::is_link(binding) || type::is_owner(binding));
            if (writable) source.capabilities |= 32;
            if (writable && type::is_owner(binding)) source.capabilities |= 8;
            if (type::is_drain(binding) && !readonly_value(source.referent))
                source.capabilities |= 16;
        } else {
            source.category = 1;
            source.referent = readonly_slot && !readonly_value(source.logical)
                ? source.logical->get_const() : source.logical;
            source.logical = source.referent;
            source.binding = source.referent;
        }
    }
    if (!source.referent) throw std::invalid_argument("Argument source has no resolved referent");
    if (readonly_slot && !readonly_value(source.binding))
        source.binding = source.binding->get_const();
    if (source.category == 2) source.logical = source.binding;
    source.capabilities |= 1;
    if (!readonly_value(source.referent)) source.capabilities |= 2;
    if (source.category == 3 && !readonly_value(source.referent)) source.capabilities |= 16;
    return source;
}

std::shared_ptr<type> argument_access_type(const function& fn) {
    auto result = type::remove_const(std::const_pointer_cast<type>(fn.get_return_type()));
    auto representation = [](std::shared_ptr<type> t) {
        for (unsigned depth = 0; t && depth < 64; ++depth) {
            t = unwrap_alias(t);
            if (!type::is_const(t)) return t;
            t = t->get_subtype();
        }
        return std::shared_ptr<type>{};
    };
    if (get_argument_intrinsic(fn) == argument_intrinsic::take) {
        if (!type::is_owner(representation(result)))
            throw std::invalid_argument("Argument take requires an exact logical owner type");
        return result;
    }
    auto value = result ? result->get_subtype() : nullptr;
    auto canonical = representation(value);
    if (!value || !canonical || type::is_any_indirection(canonical)
        || type::is_callable(canonical) || type::is_null(canonical))
        throw std::invalid_argument("Argument typed access requires a value referent type, "
                                    "not an owner, addresser or callable binding");
    return value;
}

llvm::Constant* implementation_generator::describe_argument_type(const std::shared_ptr<type>& t) {
    // Canonicalize only by exact semantic equality within this compilation.
    // In particular, never erase strong aliases through type::canonical().
    for (const auto& [existing, value] : _context->_rtti_types)
        if (type::are_equal(existing, t)) return value;
    if (type::is_const(t)) {
        auto* inner = describe_argument_type(t->get_subtype());
        for (const auto& [existing, value] : _context->_rtti_types)
            if (value == inner)
                return rtti_emitter(_context, _unit).describe(existing->get_const());
    }
    return rtti_emitter(_context, _unit).describe(t);
}

llvm::Value* implementation_generator::emit_borrowed_argument(const argument_source& source,
    llvm::Value* slot, const std::shared_ptr<struct_type>& holder, llvm::Value* destination,
    bool readonly) {
    auto* holder_type = _context->get_llvm_type(holder);
    if (!destination) {
        auto* function = _builder->GetInsertBlock()->getParent();
        llvm::IRBuilder<> entry(&function->getEntryBlock(), function->getEntryBlock().begin());
        destination = entry.CreateAlloca(holder_type, nullptr, "borrowed_argument");
    }
    auto store = [&](argument_field index, llvm::Value* value) {
        _builder->CreateStore(value, _builder->CreateStructGEP(holder_type, destination, index));
    };
    const auto capabilities = readonly ? source.capabilities & (1u | 4u) : source.capabilities;
    store(argument_slot, slot);
    store(argument_logical, describe_argument_type(source.logical));
    store(argument_binding, describe_argument_type(source.binding));
    store(argument_referent, describe_argument_type(source.referent));
    store(argument_category_field, _builder->getInt32(source.category));
    store(argument_capabilities, _builder->getInt32(capabilities));
    store(argument_indirect, _builder->getInt1(source.indirect));
    return destination;
}

bool implementation_generator::emit_argument_intrinsic(function_invocation_expression& expr) {
    auto symbol = std::dynamic_pointer_cast<symbol_expression>(expr.callee_expr());
    auto member = std::dynamic_pointer_cast<member_of_object_expression>(expr.callee_expr());
    auto fn = symbol ? symbol->get_function() : member ? member->symbol().get_function() : nullptr;
    if (!fn) return false;
    const auto intrinsic = get_argument_intrinsic(*fn);
    if (intrinsic == argument_intrinsic::none) return false;
    if (intrinsic == argument_intrinsic::invalid || !has_argument_intrinsic_contract(*fn))
        throw_error(static_cast<unsigned>(k::diag::type_diag::ERR_INVOKE_METHOD_ARG_MISMATCH),
            expr.first_lexeme(), "Invalid or non-canonical Argument intrinsic declaration");
    auto owner = fn->get_owner();
    auto* holder_type = _context->get_llvm_type(owner->get_struct_type());
    auto* ptr_type = llvm::PointerType::get(**_context, 0);
    auto* nil = llvm::ConstantPointerNull::get(ptr_type);
    auto field = [&](llvm::Value* holder, const char* name) {
        const std::string_view fields[] = {"_slot", "_logical", "_binding", "_referent",
                                          "_category", "_capabilities", "_indirect"};
        for (unsigned index = 0; index < std::size(fields); ++index)
            if (fields[index] == name)
                return _builder->CreateStructGEP(holder_type, holder, index, name);
        throw std::runtime_error("Missing canonical Argument field");
    };
    if (intrinsic == argument_intrinsic::capture || intrinsic == argument_intrinsic::borrow) {
        const auto source = describe_argument_source(expr.arguments().at(0));
        auto* destination = _sret_destination;
        _sret_destination = nullptr;
        auto* saved_slot_symbol = _argument_slot_symbol;
        if (source.variable_slot)
            _argument_slot_symbol = std::dynamic_pointer_cast<symbol_expression>(expr.arguments()[0]).get();
        auto restore_slot_symbol = llvm::make_scope_exit([&]() { _argument_slot_symbol = saved_slot_symbol; });
        expr.arguments()[0]->accept(*this);
        _value = emit_borrowed_argument(source, _value, owner->get_struct_type(), destination,
            intrinsic == argument_intrinsic::borrow);
        return true;
    }
    if (!member)
        throw_error(static_cast<unsigned>(k::diag::type_diag::ERR_INVOKE_METHOD_ARG_MISMATCH),
            expr.first_lexeme(), "Argument access requires its holder receiver");
    member->sub_expr()->accept(*this);
    auto* holder = _value;
    auto receiver = type::remove_const(member->sub_expr()->get_type());
    if (type::is_reference(receiver) && type::is_owner(type::remove_const(receiver->get_subtype())))
        holder = _builder->CreateLoad(ptr_type, holder, "argument_receiver");
    auto call_helper = [&](const char* name, std::vector<llvm::Value*> args) {
        auto helper = owner->get_function(name);
        if (helper) {
            auto* callee = ensure_function_declared(*helper);
            return create_call_or_invoke(callee->getFunctionType(), callee, args);
        }
        // Private methods deliberately do not enter KDI's public API. Their
        // compiler/runtime ABI is fixed here, just like the private holder layout.
        // Construct only a mangling prototype, not a model declaration or body.
        const bool address_check = std::string_view(name) == "_checkAddress";
        auto prototype = function::make_shared(owner, name, address_check);
        prototype->assign_name(owner->get_name().with_back(name));
        prototype->set_const_member(!address_check);
        std::vector<llvm::Type*> parameters;
        llvm::Type* result = _builder->getVoidTy();
        if (address_check) {
            auto byte_pointer = _context->from_type(primitive_type::BYTE)->get_pointer();
            prototype->append_parameter("address", byte_pointer);
            prototype->set_return_type(byte_pointer);
            parameters.push_back(ptr_type);
            result = ptr_type;
        } else {
            parameters.push_back(ptr_type);
            if (std::string_view(name) == "_check" || std::string_view(name) == "_checkTake") {
                auto getter = owner->get_function("getReferentType");
                if (!getter) throw std::runtime_error("Missing canonical Argument descriptor accessor");
                prototype->append_parameter("expected", getter->get_return_type());
                parameters.push_back(ptr_type);
                if (std::string_view(name) == "_check") {
                    prototype->append_parameter("access", _context->from_type(primitive_type::UNSIGNED_INT));
                    parameters.push_back(_builder->getInt32Ty());
                }
            } else if (std::string_view(name) != "_checkBound") {
                throw std::runtime_error("Unknown canonical Argument access check");
            }
        }
        auto native = get_module().getOrInsertFunction(mangler(_context).mangle_function(*prototype),
            llvm::FunctionType::get(result, parameters, false));
        return create_call_or_invoke(native.getFunctionType(), native.getCallee(), args);
    };
    if (intrinsic == argument_intrinsic::is_null) {
        call_helper("_checkBound", {holder});
    } else if (intrinsic == argument_intrinsic::take) {
        call_helper("_checkTake", {holder, describe_argument_type(argument_access_type(*fn))});
    } else {
        auto expected = argument_access_type(*fn);
        unsigned access = 0;
        if (intrinsic == argument_intrinsic::get && !readonly_value(expected)) access = 1;
        if (intrinsic == argument_intrinsic::take_owner) access = 2;
        if (intrinsic == argument_intrinsic::drain) access = 3;
        call_helper("_check", {holder, describe_argument_type(expected), _builder->getInt32(access)});
    }
    auto* slot = _builder->CreateLoad(ptr_type, field(holder, "_slot"), "argument_slot");
    auto* indirect = _builder->CreateLoad(_builder->getInt1Ty(), field(holder, "_indirect"));
    auto* function = _builder->GetInsertBlock()->getParent();
    auto* direct_bb = _builder->GetInsertBlock();
    auto* load_bb = llvm::BasicBlock::Create(**_context, "argument_binding_load", function);
    auto* done_bb = llvm::BasicBlock::Create(**_context, "argument_binding_ready", function);
    _builder->CreateCondBr(indirect, load_bb, done_bb);
    _builder->SetInsertPoint(load_bb);
    auto* current = _builder->CreateLoad(ptr_type, slot, "argument_current_binding");
    _builder->CreateBr(done_bb);
    _builder->SetInsertPoint(done_bb);
    auto* address = _builder->CreatePHI(ptr_type, 2, "argument_referent");
    address->addIncoming(slot, direct_bb);
    address->addIncoming(current, load_bb);
    if (intrinsic == argument_intrinsic::is_null) {
        _value = _builder->CreateICmpEQ(address, nil);
    } else if (intrinsic == argument_intrinsic::take_owner || intrinsic == argument_intrinsic::take) {
        // _check grants this only for an original writable owner SLOT.
        _builder->CreateStore(nil, slot);
        _value = address;
    } else if (intrinsic == argument_intrinsic::read || intrinsic == argument_intrinsic::drain) {
        _value = call_helper("_checkAddress", {address});
    } else {
        _value = address;
    }
    return true;
}

} // namespace k::model::gen
