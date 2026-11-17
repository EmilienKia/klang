/*
 * Exact semantic/native RTTI regression tests.
 * Copyright 2026 Emilien Kia
 * SPDX-License-Identifier: Apache-2.0
 */
#include <catch2/catch_all.hpp>
#include "helpers.hpp"
#include "gen/gen_rtti.hpp"
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/TargetParser/Triple.h>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#if defined(__linux__) && defined(__x86_64__) && defined(__ELF__)
#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace k::model {

struct rtti_signature_test_access {
    static const auto& signatures(const context& ctx) { return ctx._rtti_signatures; }
    static const auto& types(const context& ctx) { return ctx._rtti_types; }
    static auto& functions(context& ctx) { return ctx._functions; }
    static auto& argument_sources(context& ctx) { return ctx._native_argument_sources; }
    static auto create_unresolved(context& ctx, const name& type_id) {
        return ctx.create_unresolved(type_id);
    }
};

} // namespace k::model

namespace {

using rtti_context = k::model::rtti_signature_test_access;

llvm::Constant* record(llvm::Constant* pointer) {
    auto* global = llvm::dyn_cast<llvm::GlobalVariable>(pointer->stripPointerCastsAndAliases());
    REQUIRE(global != nullptr);
    REQUIRE(global->hasInitializer());
    auto* value = global->getInitializer();
    REQUIRE(value->getType()->isStructTy());
    return value;
}

llvm::Constant* field(llvm::Constant* value, unsigned position) {
    auto* type = llvm::dyn_cast<llvm::StructType>(value->getType());
    REQUIRE(type != nullptr);
    REQUIRE(position < type->getNumElements());
    auto* element = value->getAggregateElement(position);
    REQUIRE(element != nullptr);
    return element;
}

llvm::Value* actual_entry(llvm::Constant* signature) {
    return field(signature, 8)->stripPointerCastsAndAliases();
}

uint64_t number(llvm::Constant* value) {
    auto* integer = llvm::dyn_cast<llvm::ConstantInt>(value);
    REQUIRE(integer != nullptr);
    return integer->getZExtValue();
}

int64_t signed_number(llvm::Constant* value) {
    auto* integer = llvm::dyn_cast<llvm::ConstantInt>(value);
    REQUIRE(integer != nullptr);
    return integer->getSExtValue();
}

std::vector<llvm::Constant*> array(llvm::Constant* pointer) {
    if (pointer->isNullValue()) return {};
    auto* value = record(pointer);
    std::vector<llvm::Constant*> result;
    auto* data = field(value, 1);
    const auto count = number(field(value, 0));
    for (unsigned n = 0; n < count; ++n) {
        auto* element = data->getAggregateElement(n);
        REQUIRE(element != nullptr);
        result.push_back(element);
    }
    return result;
}

std::string text(llvm::Constant* pointer) {
    auto* value = record(pointer);
    auto* data = field(value, 1);
    auto* type = llvm::dyn_cast<llvm::ArrayType>(data->getType());
    REQUIRE(type != nullptr);
    std::string result;
    for (unsigned n = 0; n + 1 < type->getNumElements(); ++n)
        result.push_back(static_cast<char>(number(data->getAggregateElement(n))));
    return result;
}

llvm::Constant* signature(const std::shared_ptr<k::compiler>& compiler,
                          const std::shared_ptr<k::model::function>& function) {
    REQUIRE(function != nullptr);
    const auto& signatures = rtti_context::signatures(*compiler->get_context_for_test());
    auto found = signatures.find(function.get());
    REQUIRE(found != signatures.end());
    return record(found->second);
}

llvm::Constant* signature(const std::shared_ptr<k::compiler>& compiler, const char* name) {
    return signature(compiler, compiler->get_unit()->get_root_namespace()->get_function(name));
}

uint64_t direct_eligibility(const llvm::Module& module) {
    const llvm::Triple triple(module.getTargetTriple());
    return triple.isOSLinux() && triple.getArch() == llvm::Triple::x86_64 ? 0 : 7;
}

llvm::Constant* referent(llvm::Constant* pointer) {
    auto* value = record(pointer);
    while (number(field(value, 0)) >= 2 && number(field(value, 0)) <= 8)
        value = record(field(value, 6));
    return value;
}

std::shared_ptr<k::model::aggregate> aggregate_for_descriptor(
    const k::model::context& context, llvm::Constant* descriptor) {
    for (const auto& [semantic, pointer] : rtti_context::types(context)) {
        if (pointer != descriptor) continue;
        auto structure = std::dynamic_pointer_cast<k::model::struct_type>(semantic);
        REQUIRE(structure != nullptr);
        auto aggregate = structure->get_struct();
        REQUIRE(aggregate != nullptr);
        return aggregate;
    }
    FAIL("Virtual receiver has no authoritative semantic aggregate");
    return nullptr;
}

std::shared_ptr<k::model::function> function_for_signature(
    const k::model::context& context, llvm::Constant* descriptor) {
    for (const auto& [function, pointer] : rtti_context::signatures(context))
        if (pointer == descriptor)
            return std::const_pointer_cast<k::model::function>(
                function->shared_as<const k::model::function>());
    FAIL("Virtual slot has no authoritative semantic function");
    return nullptr;
}

bool check_projection(llvm::Constant* pointer,
                      const k::model::gen::rtti_receiver_projection& expected) {
    auto* recipe = record(pointer);
    CHECK(signed_number(field(recipe, 0)) == expected.delta);
    CHECK(number(field(recipe, 2)) == (expected.indirect ? 1 : 0));
    auto steps = array(field(recipe, 1));
    REQUIRE(steps.size() == expected.steps.size());
    for (unsigned index = 0; index < steps.size(); ++index) {
        auto* step = record(steps[index]);
        CHECK(number(field(step, 0)) == expected.steps[index].offset);
        CHECK(number(field(step, 1)) == (expected.steps[index].dereference ? 1 : 0));
    }
    return expected.delta != 0 || !expected.steps.empty();
}

#if defined(__linux__) && defined(__x86_64__) && defined(__ELF__)
void native_require(bool condition, const char* description);

// Use the producer's LLVM layout, not a parallel C++ definition of RTTI records.
struct native_record_view {
    const void* address;
    llvm::Constant* value;
    const llvm::DataLayout& layout;

    template<typename T>
    T read(unsigned position) const {
        native_require(address != nullptr, "a required native metadata record is null");
        auto* structure = llvm::cast<llvm::StructType>(value->getType());
        const auto offset = layout.getStructLayout(structure)->getElementOffset(position);
        T result;
        std::memcpy(&result, static_cast<const char*>(address) + offset, sizeof(T));
        return result;
    }

    native_record_view follow(unsigned position) const {
        return {read<const void*>(position), record(field(value, position)), layout};
    }

    native_record_view element(unsigned position) const {
        native_require(address != nullptr, "a required native metadata array is null");
        auto* structure = llvm::cast<llvm::StructType>(value->getType());
        auto* elements = field(value, 1);
        auto* type = llvm::cast<llvm::ArrayType>(elements->getType());
        const auto offset = layout.getStructLayout(structure)->getElementOffset(1)
            + position * layout.getTypeAllocSize(type->getElementType()).getFixedValue();
        const void* result;
        std::memcpy(&result, static_cast<const char*>(address) + offset, sizeof(result));
        return {result, record(elements->getAggregateElement(position)), layout};
    }
};

void native_require(bool condition, const char* description) {
    if (condition) return;
    std::cerr << "ELF RTTI producer regression: " << description << '\n';
    _exit(1);
}

void verify_native_producer_images(const std::shared_ptr<k::compiler>& compiler, bool reverse) {
    const auto context = compiler->get_context_for_test();
    const auto root = compiler->get_unit()->get_root_namespace();
    const auto function = root->get_function("f");
    const auto ordinary = root->get_function("ordinary");
    const auto klass = find_klass(compiler, "C");
    const auto method = klass->get_function("value");
    auto* unit_ir = context->module().getNamedGlobal(
        k::model::mangler::mangle_rtti_unit(compiler->get_unit()->get_unit_name()));
    const auto& layout = context->module().getDataLayout();
    const std::string directory = LIBK_RTTI_PRODUCER_FIXTURES_DIR;
    const std::string paths[] = {
        directory + "/first/librtti_image_local.so",
        directory + "/second/librtti_image_local.so"
    };
    const int order[] = {reverse ? 1 : 0, reverse ? 0 : 1};
    void* handles[2]{};
    for (int image : order) {
        handles[image] = dlopen(paths[image].c_str(), RTLD_NOW | RTLD_GLOBAL);
        if (!handles[image]) std::cerr << dlerror() << '\n';
        native_require(handles[image] != nullptr, "both real shared images must load");
    }
    void* producers[2]{};
    void* entries[2]{};
    const int constants[] = {101, 202};
    struct P { int32_t value; };
    using typed_entry = int(*)(P&);
    P argument{7};
    auto* first_public = dlsym(handles[order[0]], function->get_mangled_name().c_str());
    native_require(dlsym(RTLD_DEFAULT, function->get_mangled_name().c_str()) == first_public,
        "ordinary global symbol resolution must still select the first image");
    for (unsigned image = 0; image < 2; ++image) {
        auto* unit_address = dlsym(handles[image], unit_ir->getName().str().c_str());
        native_require(unit_address != nullptr, "each image must expose its own Unit definition");
        native_record_view unit{unit_address, unit_ir->getInitializer(), layout};
        auto functions = unit.follow(4);
        const auto functions_ir = array(field(unit.value, 4));
        native_require(functions.read<uint32_t>(0) == functions_ir.size(),
            "native Unit function inventory must match its producer layout");
        bool found = false;
        for (unsigned n = 0; n < functions_ir.size(); ++n) {
            if (field(record(functions_ir[n]), 8)
                != rtti_context::signatures(*context).at(function.get())) continue;
            auto signature = functions.element(n).follow(8);
            entries[image] = signature.read<void*>(8);
            native_require(signature.read<uint32_t>(6) == 0,
                "the concrete reference prototype must remain DIRECT");
            native_require(entries[image] != nullptr, "a DIRECT signature must expose its existing body");
            native_require(entries[image] == dlsym(handles[image], function->get_mangled_name().c_str()),
                "reflection must name the existing producer body, not a thunk or PLT entry");
            native_require(reinterpret_cast<typed_entry>(entries[image])(argument)
                == constants[image] + argument.value,
                "int(P&) entry must execute its own image's actual body");
            auto parameter = signature.follow(1).element(0);
            native_require(parameter.read<uint32_t>(0) == 3, "P& must retain reference semantics");
            auto identity = parameter.follow(6).follow(23);
            auto producer = identity.follow(1);
            producers[image] = const_cast<void*>(producer.address);
            native_require(identity.read<const void*>(0) == producer.address,
                "the locally declared P must retain its own descriptor producer");
            native_require(producer.read<const void*>(0) == unit_address,
                "the private producer anchor must identify its own Unit definition");
            Dl_info entry_info{}, producer_info{}, unit_info{};
            native_require(dladdr(entries[image], &entry_info)
                && dladdr(producer.address, &producer_info) && dladdr(unit_address, &unit_info),
                "entry, producer and Unit must belong to native ELF images");
            native_require(entry_info.dli_fbase == producer_info.dli_fbase
                && entry_info.dli_fbase == unit_info.dli_fbase,
                "prototype provenance and entry must identify the same actual image");
            native_require(std::filesystem::equivalent(paths[image], entry_info.dli_fname),
                "dladdr must identify the expected producer file");
            found = true;
        }
        native_require(found, "the public f descriptor must be present");
        auto ordinary_entry = reinterpret_cast<typed_entry>(
            dlsym(handles[image], ordinary->get_mangled_name().c_str()));
        native_require(ordinary_entry != nullptr, "the ordinary call probe must be present");
        native_require(ordinary_entry(argument) == constants[order[0]] + argument.value + 3,
            "ordinary K calls must retain public ELF interposition");

        auto* rtti_ir = klass->get_vtable()->llvm_rtti_global;
        auto* rtti_address = dlsym(handles[image], rtti_ir->getName().str().c_str());
        native_require(rtti_address != nullptr, "the class must expose its producer RTTI");
        native_record_view nominal{rtti_address, record(rtti_ir), layout};
        auto bodies = nominal.follow(14).follow(15);
        const auto bodies_ir = array(field(nominal.follow(14).value, 15));
        auto* vtable_ir = klass->get_vtable()->llvm_global;
        auto* vtable_address = dlsym(handles[image], vtable_ir->getName().str().c_str());
        native_require(vtable_address != nullptr, "the existing vtable must be published");
        native_record_view vtable{vtable_address, record(vtable_ir), layout};
        bool method_found = false;
        for (unsigned n = 0; n < bodies_ir.size(); ++n) {
            if (field(record(bodies_ir[n]), 2)
                != rtti_context::signatures(*context).at(method.get())) continue;
            auto body = bodies.element(n);
            native_require(body.read<const void*>(4) == vtable_address,
                "the body map's vtable relocation must bind to its own image");
            auto* entry = body.follow(2).read<void*>(8);
            native_require(entry != nullptr, "the concrete virtual implementation must expose its body");
            native_require(entry == dlsym(handles[image], method->get_mangled_name().c_str()),
                "virtual body metadata must point at the existing producer implementation");
            const auto size = layout.getTypeAllocSize(klass->get_struct_type()->get_llvm_type());
            std::vector<uint64_t> receiver((size.getFixedValue() + 7) / 8);
            native_require(reinterpret_cast<int(*)(void*)>(entry)(receiver.data()) == constants[image],
                "the reflected virtual implementation must execute its own producer body");
            native_require(vtable.read<void*>(body.read<uint32_t>(1) + 1)
                == dlsym(handles[order[0]], method->get_mangled_name().c_str()),
                "ordinary vtable slots must retain public ELF interposition");
            method_found = true;
        }
        native_require(method_found, "the concrete producer method body map must be present");
    }
    native_require(producers[0] != producers[1], "same-named images must have distinct private producers");
    native_require(entries[0] != entries[1], "same-named images must retain distinct actual bodies");
}
#endif

} // namespace

TEST_CASE("RTTI native ELF entries and vtable identities bind to their descriptor producer",
          "[libk][rtti][signature][native][producer][elf]") {
#if defined(__linux__) && defined(__x86_64__) && defined(__ELF__)
    const bool reverse = GENERATE(false, true);
    std::ifstream input(LIBK_RTTI_PRODUCER_SOURCE);
    REQUIRE(input.good());
    std::string source((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    auto compiler = compile_model_with_stdlib(source);
    REQUIRE(compiler != nullptr);
    auto context = compiler->get_context_for_test();
    auto function = compiler->get_unit()->get_root_namespace()->get_function("f");
    REQUIRE(function != nullptr);
    auto klass = find_klass(compiler, "C");
    REQUIRE(klass != nullptr);
    REQUIRE(klass->get_function("value") != nullptr);
    auto* body = context->lookup_llvm_function(function);
    REQUIRE(body != nullptr);
    auto* local = llvm::dyn_cast<llvm::GlobalAlias>(field(signature(compiler, function), 8));
    REQUIRE(local != nullptr);
    CHECK(local->hasPrivateLinkage());
    CHECK(local->isDSOLocal());
    CHECK(local->getAliasee() == body);
    CHECK_FALSE(local->hasGlobalUnnamedAddr());
    CHECK(body->hasExternalLinkage());
    CHECK(body->getVisibility() == llvm::GlobalValue::DefaultVisibility);
    CHECK_FALSE(body->isDSOLocal());
    for (const auto& candidate : context->module())
        CHECK_FALSE(candidate.getName().starts_with("__k_rtti_local_"));
    const auto child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        verify_native_producer_images(compiler, reverse);
        _exit(0);
    }
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
    const std::string target = context->module().getTargetTriple();
    context->module().setTargetTriple("x86_64-pc-windows-msvc");
    k::model::gen::rtti_emitter emitter(context, *compiler->get_unit());
    auto* unsupported = record(emitter.signature(function, true));
    CHECK(number(field(unsupported, 6)) == 7);
    CHECK(field(unsupported, 8)->isNullValue());
    context->module().setTargetTriple(target);
    CHECK(actual_entry(record(emitter.signature(function, true))) == body);
#else
    SKIP("Producer-local native relocation proof is scoped to Linux x86-64 ELF");
#endif
}

TEST_CASE("RTTI nominal identity is producer anchored, template exact and unresolved for imports",
          "[libk][rtti][signature][type][template][producer]") {
    constexpr auto source = R"SRC(
        module rtti_nominal_producer;
        class Item { public Item() {} }
        struct Plain { value : int; }
        enum Mode { ONE = 1; }
        union Choice { value : int; }
        typedef Handle : int;
        template<typename T> struct Box { value : T; }
        template<typename T> typedef Identity : int;
        plain(value : Plain&) {}
        item(value : Item&) {}
        mode(value : Mode) : Mode { return value; }
        choice(value : Choice&) {}
        handle(value : Handle) : Handle { return value; }
        templates(a : Box<int>&, b : Box<long>&, c : Identity<int>, d : Identity<long>) {}
        imported(a : const k::String&, b : const k::String&) {}
        producerAddress() : const k::rtti::Producer? {
            value : Item;
            return value.getClass().getSemanticType()->getNominalIdentity()->getProducer();
        }
    )SRC";
    auto first = compile_model_with_stdlib(source);
    auto second = compile_model_with_stdlib(source);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    llvm::Constant* first_producer = nullptr;
    for (const auto* name : {"plain", "item", "mode", "choice", "handle"}) {
        auto* left_type = referent(array(field(signature(first, name), 1))[0]);
        auto* right_type = referent(array(field(signature(second, name), 1))[0]);
        auto* left = record(field(left_type, 23));
        auto* right = record(field(right_type, 23));
        CHECK(number(field(left_type, 3)) == number(field(right_type, 3)));
        CHECK(text(field(left_type, 11)) == text(field(right_type, 11)));
        CHECK(text(field(left, 2)) == text(field(right, 2)));
        CHECK_FALSE(field(left, 1)->isNullValue());
        CHECK_FALSE(field(right, 1)->isNullValue());
        CHECK(field(left, 0) == field(left, 1));
        CHECK(field(right, 0) == field(right, 1));
        CHECK(field(left, 1) != field(right, 1)); // identical names/layout are not producer identity
        if (!first_producer) first_producer = field(left, 1);
        CHECK(field(left, 1) == first_producer);
        CHECK(field(left, 3) == field(left_type, 13));
    }
    auto* anchor = llvm::dyn_cast<llvm::GlobalVariable>(first_producer);
    REQUIRE(anchor != nullptr);
    CHECK(anchor->hasPrivateLinkage());
    CHECK(anchor->isConstant());
    CHECK_FALSE(anchor->hasGlobalUnnamedAddr());
    auto* producer = record(anchor);
    CHECK(number(field(producer, 4)) == 3);
    CHECK(text(field(producer, 1)) == first->get_unit()->get_unit_name().to_string());
    CHECK(text(field(producer, 2)) == first->get_context_for_test()->module().getTargetTriple());
    CHECK(text(field(producer, 3)) == first->get_context_for_test()->module().getDataLayoutStr());
    auto* unit = first->get_context_for_test()->module().getNamedGlobal(
        k::model::mangler::mangle_rtti_unit(first->get_unit()->get_unit_name()));
    REQUIRE(unit != nullptr);
    if (direct_eligibility(first->get_context_for_test()->module()) == 0) {
        CHECK(field(producer, 0)->stripPointerCastsAndAliases() == unit);
        auto* unit_alias = llvm::dyn_cast<llvm::GlobalAlias>(field(producer, 0));
        REQUIRE(unit_alias != nullptr);
        CHECK(unit_alias->hasPrivateLinkage());
        CHECK(unit_alias->isDSOLocal());
    } else {
        CHECK(field(producer, 0)->isNullValue());
    }
    CHECK_FALSE(unit->isDSOLocal());

    const auto templates = array(field(signature(first, "templates"), 1));
    const auto other_templates = array(field(signature(second, "templates"), 1));
    REQUIRE(templates.size() == 4);
    REQUIRE(other_templates.size() == 4);
    for (unsigned pair = 0; pair < 2; ++pair) {
        auto* left_type = referent(templates[pair * 2]);
        auto* right_type = referent(templates[pair * 2 + 1]);
        auto* left = record(field(left_type, 23));
        auto* right = record(field(right_type, 23));
        CHECK(field(left, 1) == first_producer);
        CHECK(field(right, 1) == first_producer);
        CHECK(text(field(left, 2)) == text(field(right, 2)));
        CHECK(text(field(record(field(left, 3)), 0))
            == text(field(record(field(right, 3)), 0)));
        if (pair == 0) {
            auto primary = find_aggregate(first, "Box");
            REQUIRE(primary != nullptr);
            CHECK(primary->is_template());
            CHECK(primary->parent<k::model::ns>() == first->get_unit()->get_root_namespace());
            const auto primary_name = first->get_unit()->get_unit_name().with_root_prefix()
                .with_back(primary->get_short_name());
            CHECK(text(field(left, 2)) == std::to_string(number(field(left_type, 0))) + ":"
                + k::model::mangler::mangle_fq_name(primary_name, true));
            CHECK(text(field(record(field(left, 3)), 0)) == primary_name.to_string());
        }
        CHECK(field(left, 3) == field(left_type, 13));
        CHECK(field(right, 3) == field(right_type, 13));
        auto left_args = array(field(record(field(left, 3)), 1));
        auto right_args = array(field(record(field(right, 3)), 1));
        REQUIRE(left_args.size() == 1);
        REQUIRE(right_args.size() == 1);
        CHECK(number(field(record(field(record(left_args[0]), 1)), 1)) == 6); // int
        CHECK(number(field(record(field(record(right_args[0]), 1)), 1)) == 8); // long
        auto* other = record(field(referent(other_templates[pair * 2]), 23));
        CHECK(text(field(left, 2)) == text(field(other, 2)));
        CHECK(field(left, 1) != field(other, 1));
        auto other_args = array(field(record(field(other, 3)), 1));
        REQUIRE(other_args.size() == 1);
        CHECK(number(field(record(field(record(other_args[0]), 1)), 1)) == 6);
    }
    const auto imports = array(field(signature(first, "imported"), 1));
    REQUIRE(imports.size() == 2);
    auto* imported = referent(imports[0]);
    CHECK(imported == referent(imports[1]));
    auto* imported_identity = record(field(imported, 23));
    CHECK(field(imported_identity, 0) == first_producer);
    CHECK(field(imported_identity, 1)->isNullValue());
    CHECK_FALSE(text(field(imported_identity, 2)).empty());
    CHECK(field(referent(array(field(signature(first, "plain"), 1))[0]), 10)->isNullValue());

    auto first_jit = first->to_jit();
    auto second_jit = second->to_jit();
    REQUIRE(first_jit != nullptr);
    REQUIRE(second_jit != nullptr);
    auto left = first_jit->lookup_symbol<const void*(*)()>("producerAddress");
    auto right = second_jit->lookup_symbol<const void*(*)()>("producerAddress");
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);
    CHECK(left() != nullptr);
    CHECK(right() != nullptr);
    CHECK(left() != right());
}

TEST_CASE("RTTI redirects describe actual providers and remap source receiver and parameter roles",
          "[libk][rtti][signature][native][redirect][negative]") {
    auto compiler = compile_model_with_stdlib(R"SRC(
        module rtti_redirect_contract;
        class Base {
            value : int;
            public Base() { value = 17; }
            protected read(extra : int) : int { return value + extra; }
            public viaBaseFree(extra : int) : int -> baseFree;
        }
        class Leading {
            padding : long;
            public Leading() { padding = 41L; }
        }
        class Derived : public Leading, public Base {
            public Derived() {}
            public override read(extra : int) : int -> Base::read;
            public override viaBaseFree(extra : int) : int -> Base::viaBaseFree;
        }
        baseFree(self : Base&, value : int) : int { return value; }
        memberChain(self : Derived&, value : int) : int -> Derived::read;
        freeChain(self : Derived&, value : int) : int -> Derived::viaBaseFree;
        freeProvider(self : Bridge&, value : int, taken : int!) : long { return (long) value; }
        class Bridge {
            public Bridge() {}
            public take(value : int, taken : int!) : long { return (long) value; }
            public viaFree(value : int, taken : int!) : long -> freeProvider;
        }
        freeAlias(self : Bridge&, value : int, taken : int!) : long -> Bridge::take;
        objectClass(value : const k::Object&) : const k::Class& { return value.getClass(); }
        importedAlias(value : const k::Object&) : const k::Class& -> objectClass;
        arrayProvider(values : int[]) : int { return (int) values.size; }
        packedProvider(values... : int) : int { return (int) values.size; }
        packedAlias(values... : int) : int -> arrayProvider;
        arrayAlias(values : int[]) : int -> packedProvider;
    )SRC");
    REQUIRE(compiler != nullptr);
    auto context = compiler->get_context_for_test();
    auto root = compiler->get_unit()->get_root_namespace();
    auto base = find_klass(compiler, "Base");
    auto derived = find_klass(compiler, "Derived");
    auto bridge = find_klass(compiler, "Bridge");
    REQUIRE(base != nullptr);
    REQUIRE(derived != nullptr);
    REQUIRE(bridge != nullptr);
    for (const auto* name : {"packedAlias", "arrayAlias"}) {
        auto alias = root->get_function(name);
        REQUIRE(alias != nullptr);
        auto* sig = signature(compiler, alias);
        CHECK(number(field(sig, 6)) == direct_eligibility(context->module()));
        CHECK(number(field(sig, 2)) == 1);
        CHECK(actual_entry(sig) == context->lookup_llvm_function(alias->get_redirect_target()));
        CHECK_FALSE(context->lookup_llvm_function(alias)->isVarArg());
        CHECK(alias->parameters()[0]->is_varargs() == (std::string(name) == "packedAlias"));
    }
    auto read = derived->get_function("read");
    auto via_free = bridge->get_function("viaFree");
    auto free_alias = root->get_function("freeAlias");
    auto free_provider = root->get_function("freeProvider");
    auto take = bridge->get_function("take");
    auto* read_signature = signature(compiler, read);
    CHECK(actual_entry(read_signature) == context->lookup_llvm_function(base->get_function("read")));
    CHECK(number(field(read_signature, 6)) == direct_eligibility(context->module()));
    auto* native = record(field(read_signature, 3));
    auto arguments = array(field(native, 5));
    REQUIRE(arguments.size() == 2);
    CHECK((number(field(record(arguments[0]), 6)) & 128) != 0);
    CHECK(signed_number(field(native, 11)) == 0);
    auto* actual_receiver = referent(field(record(arguments[0]), 4));
    CHECK(actual_receiver == record(field(record(base->get_vtable()->llvm_rtti_global), 14)));
    CHECK(field(record(field(read_signature, 4)), 0)
        == field(record(derived->get_vtable()->llvm_rtti_global), 14));
    const auto via_map = rtti_context::argument_sources(*context).at(via_free.get());
    const auto free_map = rtti_context::argument_sources(*context).at(free_alias.get());
    CHECK((via_map == std::vector<int>{-2, 0, 1}));
    CHECK((free_map == std::vector<int>{0, 1, 2}));
    auto* via_signature = signature(compiler, via_free);
    auto* free_signature = signature(compiler, free_alias);
    CHECK(actual_entry(via_signature) == context->lookup_llvm_function(free_provider));
    CHECK(actual_entry(free_signature) == context->lookup_llvm_function(take));
    CHECK(number(field(via_signature, 2)) == 2);
    CHECK(number(field(free_signature, 2)) == 3);
    CHECK(signed_number(field(record(field(via_signature, 3)), 7)) == 0);
    CHECK(signed_number(field(record(field(free_signature, 3)), 7)) == -1);
    CHECK(signed_number(field(record(field(via_signature, 3)), 11)) == -1);
    CHECK(signed_number(field(record(field(free_signature, 3)), 11)) == 0);
    CHECK(field(free_signature, 4)->isNullValue());
    for (auto* sig : {via_signature, free_signature}) {
        CHECK(number(field(sig, 6)) == direct_eligibility(context->module()));
        auto args = array(field(record(field(sig, 3)), 5));
        REQUIRE(args.size() == 3);
        CHECK((number(field(record(args[2]), 6)) & 64) != 0);
        CHECK(number(field(record(field(record(args[2]), 4)), 0)) == 7);
    }
    bool base_projection = false, adjusted_base_projection = false, free_body = false;
    for (auto klass : {derived, bridge}) {
        auto* type = record(field(record(klass->get_vtable()->llvm_rtti_global), 14));
        for (auto* entry : array(field(type, 15))) {
            auto* body = record(entry);
            if (field(body, 2) == rtti_context::signatures(*context).at(read.get())) {
                CHECK(field(body, 3) == field(record(base->get_vtable()->llvm_rtti_global), 14));
                auto subobject = aggregate_for_descriptor(*context, field(body, 0));
                auto expected = k::model::gen::describe_rtti_receiver_projection(
                    *derived, *subobject, *base, context->module().getDataLayout());
                adjusted_base_projection |= check_projection(field(body, 5), expected);
                CHECK(actual_entry(record(field(body, 2)))
                    == context->lookup_llvm_function(base->get_function("read")));
                base_projection = true;
            }
            if (field(body, 2) == rtti_context::signatures(*context).at(via_free.get())) {
                CHECK(actual_entry(record(field(body, 2))) == context->lookup_llvm_function(free_provider));
                CHECK(field(body, 3) == field(record(bridge->get_vtable()->llvm_rtti_global), 14));
                free_body = true;
            }
        }
    }
    CHECK(base_projection);
    CHECK(adjusted_base_projection);
    CHECK(free_body);
    for (const auto* name : {"memberChain", "freeChain"}) {
        auto* chain = signature(compiler, name);
        CHECK(number(field(chain, 6)) == direct_eligibility(context->module()));
        CHECK(number(field(chain, 2)) == 2);
        CHECK(field(chain, 4)->isNullValue());
        auto* shape = record(field(chain, 3));
        CHECK(signed_number(field(shape, 7)) == -1);
        CHECK(signed_number(field(shape, 11)) == (std::string(name) == "memberChain" ? 0 : -1));
        auto first_arg = array(field(shape, 5)).front();
        CHECK(signed_number(field(record(first_arg), 5)) == 0);
        CHECK((number(field(record(first_arg), 6)) & 128) != 0);
        CHECK(referent(field(record(first_arg), 4)) == actual_receiver);
        CHECK(actual_entry(chain) == context->lookup_llvm_function(std::string(name) == "memberChain"
            ? base->get_function("read") : root->get_function("baseFree")));
    }

    // Defend the metadata boundary even if a caller supplies an invalid model.
    k::model::gen::rtti_emitter emitter(context, *compiler->get_unit());
    auto owner = via_free->parameters()[1]->get_type();
    auto pointer = context->from_type(k::model::primitive_type::INT)->get_pointer();
    CHECK(owner->get_llvm_type() == pointer->get_llvm_type());
    via_free->parameters()[1]->set_type(pointer);
    auto* rejected = record(emitter.signature(via_free, true));
    CHECK(number(field(rejected, 6)) == 10);
    CHECK(field(rejected, 8)->isNullValue());
    auto actual_args = array(field(record(field(rejected, 3)), 5));
    CHECK((number(field(record(actual_args[2]), 6)) & 64) != 0);
    CHECK(number(field(record(field(record(actual_args[2]), 4)), 0)) == 7);
    via_free->parameters()[1]->set_type(owner);
    emitter.signature(via_free, true);

    auto imported_alias = root->get_function("importedAlias");
    std::shared_ptr<k::model::function> imported_provider;
    for (const auto& [function, body] : rtti_context::functions(*context))
        if (function->is_external() && function->get_short_name() == "getClass"
            && function->get_owner() && function->get_owner()->get_short_name() == "Object")
            imported_provider = function;
    REQUIRE(imported_provider != nullptr);
    auto original_target = imported_alias->get_redirect_target();
    auto* original_body = rtti_context::functions(*context).at(imported_alias);
    auto original_mapping = rtti_context::argument_sources(*context).at(imported_alias.get());
    imported_alias->set_redirect_target(imported_provider);
    rtti_context::functions(*context)[imported_alias] = rtti_context::functions(*context).at(imported_provider);
    rtti_context::argument_sources(*context)[imported_alias.get()] = {0};
    rejected = record(emitter.signature(imported_alias, true));
    CHECK(number(field(rejected, 6)) == 9);
    CHECK(field(rejected, 8)->isNullValue());
    CHECK((number(field(record(field(rejected, 3)), 10)) & 4) != 0);
    imported_alias->set_redirect_target(original_target);
    rtti_context::functions(*context)[imported_alias] = original_body;
    rtti_context::argument_sources(*context)[imported_alias.get()] = std::move(original_mapping);
    emitter.signature(imported_alias, true);
}

TEST_CASE("RTTI finalization refreshes late native entries without losing semantic ineligibility",
          "[libk][rtti][signature][native][negative]") {
    auto compiler = compile_model_with_stdlib(R"SRC(
        module rtti_signature_late_native;
        struct Payload { value : int; }
        private scalar(value : int) : int { return value; }
        private aggregate(value : Payload) : int { return value.value; }
    )SRC");
    REQUIRE(compiler != nullptr);
    auto context = compiler->get_context_for_test();
    auto unit = compiler->get_unit();
    auto scalar = unit->get_root_namespace()->get_function("scalar");
    auto aggregate = unit->get_root_namespace()->get_function("aggregate");
    REQUIRE(scalar != nullptr);
    REQUIRE(aggregate != nullptr);
    REQUIRE_FALSE(rtti_context::signatures(*context).contains(scalar.get()));
    REQUIRE_FALSE(rtti_context::signatures(*context).contains(aggregate.get()));
    k::model::gen::rtti_emitter emitter(context, *unit);

    // Reproduce descriptors captured before their actual native declarations.
    auto scalar_entry = rtti_context::functions(*context).extract(scalar);
    auto aggregate_entry = rtti_context::functions(*context).extract(aggregate);
    REQUIRE_FALSE(scalar_entry.empty());
    REQUIRE_FALSE(aggregate_entry.empty());
    auto* scalar_body = scalar_entry.mapped();
    auto* aggregate_body = aggregate_entry.mapped();
    const auto scalar_symbol = scalar_body->getName().str();
    const auto aggregate_symbol = aggregate_body->getName().str();
    scalar_body->setName(scalar_symbol + "_not_yet_declared");
    aggregate_body->setName(aggregate_symbol + "_not_yet_declared");
    REQUIRE(context->lookup_llvm_function(scalar) == nullptr);
    REQUIRE(context->lookup_llvm_function(aggregate) == nullptr);
    auto* scalar_descriptor = emitter.signature(scalar);
    auto* aggregate_descriptor = emitter.signature(aggregate);
    CHECK(number(field(record(scalar_descriptor), 6)) == 4);
    CHECK(number(field(record(aggregate_descriptor), 6)) == 5);
    CHECK(field(record(scalar_descriptor), 8)->isNullValue());
    CHECK(field(record(aggregate_descriptor), 8)->isNullValue());
    scalar_body->setName(scalar_symbol);
    aggregate_body->setName(aggregate_symbol);
    rtti_context::functions(*context).insert(std::move(scalar_entry));
    rtti_context::functions(*context).insert(std::move(aggregate_entry));
    CHECK(context->lookup_llvm_function(scalar) == scalar_body);
    CHECK(context->lookup_llvm_function(aggregate) == aggregate_body);

    // A missing body must not replace the aggregate-input reason with NO_ENTRY.
    aggregate_body->deleteBody();
    emitter.finalize_virtual_bodies();
    CHECK(rtti_context::signatures(*context).at(scalar.get()) == scalar_descriptor);
    CHECK(rtti_context::signatures(*context).at(aggregate.get()) == aggregate_descriptor);
    CHECK(number(field(record(scalar_descriptor), 6)) == direct_eligibility(context->module()));
    CHECK(actual_entry(record(scalar_descriptor)) == scalar_body);
    CHECK_FALSE(field(record(scalar_descriptor), 3)->isNullValue());
    CHECK(number(field(record(aggregate_descriptor), 6)) == 5);
    CHECK(field(record(aggregate_descriptor), 8)->isNullValue());
    CHECK_FALSE(field(record(aggregate_descriptor), 3)->isNullValue());
}

TEST_CASE("RTTI signatures preserve getters, defaults, exact arity and actual entry addresses",
          "[libk][rtti][signature][native]") {
    auto compiler = compile_model_with_stdlib(R"SRC(
        module rtti_signature_getters;
        class Service {
            public Service() {}
            public compute(first : int, second : long = 9L) : double {
                return (double) first + (double) second;
            }
        }
        inspect() : int {
            service : Service;
            functions : const k::Function?[]? = service.getClass().getFunctions();
            if (functions == null || functions->size != 1u) return 1;
            fn : const k::Function? = functions[0];
            if (fn == null || !fn->isMember() || fn->isStatic()) return 2;
            if (k::String(fn->getName()) != k::String("compute")) return 3;
            if (fn->getAnnotations() != null || fn->getArity() != 2u) return 4;
            params : const k::Parameter?[]? = fn->getParameters();
            if (params == null || params->size != 2u) return 5;
            if (params[0]->getPosition() != 0u || params[0]->hasSourceDefault()) return 6;
            if (params[1]->getPosition() != 1u || !params[1]->hasSourceDefault()) return 7;
            if (params[1]->isVararg() || params[1]->getAnnotations() != null) return 8;
            if (params[0]->getType()->getPrimitiveKind() != k::rtti::PrimitiveKind::INT) return 9;
            if (params[1]->getType()->getPrimitiveKind() != k::rtti::PrimitiveKind::LONG) return 10;
            result : const k::rtti::Type? = fn->getReturnType();
            if (result == null || !result->hasLayout() || !result->isResolved()) return 11;
            if (result->getKind() != k::rtti::TypeKind::PRIMITIVE) return 12;
            if (result->getPrimitiveKind() != k::rtti::PrimitiveKind::DOUBLE) return 13;
            if (result->getSize() != (unsigned long) 8 || result->getCanonical() != result) return 14;
            sig : const k::rtti::Signature? = fn->getSignature();
            if (sig == null || sig->getReceiver() == null) return 15;
            if (sig->getNative()->getReceiverIndex() != 0) return 16;
            if (sig->getNative()->getResultIndex() != -1) return 17;
            if (sig->getNative()->getArguments()->size != 3u) return 18;
            if (sig->getTemplateOrigin() != null || sig->getEnclosingTemplateOrigin() != null) return 19;
            if (sig->getThrowsTypes() != null) return 20;
            return 42;
        }
        reflectedEntry() : const byte* {
            service : Service;
            functions : const k::Function?[]? = service.getClass().getFunctions();
            if (functions == null || functions->size == 0u) return null;
            fn : const k::Function? = functions[0];
            if (fn == null) return null;
            return fn->getSignature()->getEntry();
        }
    )SRC");
    REQUIRE(compiler != nullptr);
    auto service = find_klass(compiler, "Service");
    REQUIRE(service != nullptr);
    auto function = service->get_function("compute");
    REQUIRE(function != nullptr);
    auto* body = compiler->get_context_for_test()->lookup_llvm_function(function);
    REQUIRE(body != nullptr);
    auto* descriptor = signature(compiler, function);
    CHECK(actual_entry(descriptor) == body);
    CHECK(number(field(descriptor, 2)) == function->get_parameter_size());
    CHECK(number(field(descriptor, 6)) == direct_eligibility(compiler->get_context_for_test()->module()));
    CHECK(text(field(descriptor, 7)) == body->getName().str());
    for (auto& candidate : compiler->get_context_for_test()->module()) {
        for (auto& block : candidate) for (auto& instruction : block) {
            if (auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction))
                CHECK(call->getCalledFunction() != body);
        }
    }
    auto jit = compiler->to_jit();
    REQUIRE(jit != nullptr);
    auto inspect = jit->lookup_symbol<int(*)()>("inspect");
    auto reflected = jit->lookup_symbol<const void*(*)()>("reflectedEntry");
    auto actual = jit->lookup_symbol<void(*)()>(function->get_mangled_name());
    REQUIRE(inspect != nullptr);
    REQUIRE(reflected != nullptr);
    REQUIRE(actual != nullptr);
    CHECK(inspect() == 42);
    CHECK(reflected() == reinterpret_cast<const void*>(actual));
}

TEST_CASE("RTTI native sret and receiver slots agree with the actual LLVM declaration",
          "[libk][rtti][signature][native][sret]") {
    auto compiler = compile_model_with_stdlib(R"SRC(
        module rtti_signature_sret;
        struct Value {
            data : long;
            Value(seed : long) : data(seed) {}
            ~Value() {}
        }
        class Maker {
            public Maker() {}
            public const make(seed : int) : Value {
                result : Value((long) seed);
                return result;
            }
        }
        consume(value : Value) : long { return value.data; }
        importedResult() : k::String {
            result : k::String("metadata");
            return result;
        }
        addresses(a : Value&, b : const Value?, c : Value+, d : Value*,
                  e : Value!, f : Value#, g : const int[3]&) {}
    )SRC");
    REQUIRE(compiler != nullptr);
    auto maker = find_klass(compiler, "Maker");
    REQUIRE(maker != nullptr);
    auto function = maker->get_function("make");
    auto* body = compiler->get_context_for_test()->lookup_llvm_function(function);
    REQUIRE(body != nullptr);
    REQUIRE(body->hasParamAttribute(0, llvm::Attribute::StructRet));
    auto* sig = signature(compiler, function);
    auto* native = record(field(sig, 3));
    CHECK(number(field(native, 1)) == body->getCallingConv());
    CHECK(text(field(native, 2)) == body->getParent()->getTargetTriple());
    CHECK(text(field(native, 3)) == body->getParent()->getDataLayoutStr());
    CHECK(signed_number(field(native, 6)) == 0);
    CHECK(signed_number(field(native, 7)) == 1);
    CHECK(signed_number(field(native, 11)) == 1);
    auto args = array(field(native, 5));
    REQUIRE(args.size() == body->arg_size());
    CHECK(signed_number(field(record(args[0]), 5)) == -1);
    CHECK(signed_number(field(record(args[1]), 5)) == -2);
    CHECK(signed_number(field(record(args[2]), 5)) == 0);
    CHECK((number(field(record(args[0]), 6)) & 1) == 1);
    CHECK(number(field(record(args[2]), 1)) == body->getArg(2)->getType()->getIntegerBitWidth());
    auto* storage = body->getParamAttribute(0, llvm::Attribute::StructRet).getValueAsType();
    const auto& layout = body->getParent()->getDataLayout();
    CHECK(number(field(native, 8)) == layout.getTypeAllocSize(storage).getFixedValue());
    CHECK(number(field(native, 9)) == layout.getABITypeAlign(storage).value());
    auto* result = record(field(sig, 0));
    CHECK(number(field(result, 0)) == 13); // struct, not its hidden pointer transport
    CHECK(number(field(result, 3)) == layout.getTypeAllocSize(storage).getFixedValue());
    auto value = find_aggregate(compiler, "Value");
    REQUIRE(value != nullptr);
    auto* destructor = compiler->get_context_for_test()->lookup_llvm_function(value->get_destructor());
    REQUIRE(destructor != nullptr);
    auto lifecycle = array(field(result, 14));
    REQUIRE_FALSE(lifecycle.empty());
    CHECK(actual_entry(record(field(record(lifecycle[0]), 2))) == destructor);
    auto* receiver = record(field(sig, 4));
    CHECK((number(field(receiver, 2)) & 1) == 1); // const receiver, independently of virtual dispatch
    CHECK(number(field(receiver, 2))
        == ((function->is_const_member() ? 1u : 0u) | (function->is_virtual() ? 6u : 0u)));
    CHECK(signed_number(field(receiver, 1)) == function->get_vtable_slot());
    auto* deferred = signature(compiler, "consume");
    CHECK(number(field(deferred, 6)) == 5);
    CHECK(number(field(record(array(field(deferred, 1))[0]), 0)) == 13);
    auto* addresses = signature(compiler, "addresses");
    CHECK(number(field(addresses, 6)) == direct_eligibility(*body->getParent()));
    auto types = array(field(addresses, 1));
    REQUIRE(types.size() == 7);
    const std::vector<uint64_t> expected{3, 4, 5, 6, 7, 8, 3};
    for (unsigned n = 0; n < expected.size(); ++n)
        CHECK(number(field(record(types[n]), 0)) == expected[n]);
    auto* const_array = record(field(record(types[6]), 6));
    if (number(field(const_array, 0)) == 2) const_array = record(field(const_array, 6));
    CHECK(number(field(const_array, 0)) == 10);
    CHECK(number(field(const_array, 5)) == 3);
    CHECK(number(field(const_array, 16)) > 0);
    auto* imported_result = record(field(signature(compiler, "importedResult"), 0));
    auto imported_lifecycle = array(field(imported_result, 14));
    REQUIRE_FALSE(imported_lifecycle.empty());
    bool producer_required = false;
    for (auto* operation : imported_lifecycle) {
        auto* imported_signature = record(field(record(operation), 2));
        if (number(field(imported_signature, 6)) == 9) producer_required = true;
        if (!field(imported_signature, 3)->isNullValue())
            CHECK((number(field(record(field(imported_signature, 3)), 10)) & 4) != 0);
    }
    CHECK(producer_required);
}

TEST_CASE("RTTI fixed allocation layout follows value qualifiers and aliases, not array headers",
          "[libk][rtti][signature][type][layout]") {
    auto compiler = compile_model_with_stdlib(R"SRC(
        module rtti_signature_layout;
        struct Empty {}
        typedef SourceBuffer : int[];
        arrays(values : int[], frozen : const int[], named : SourceBuffer) {}
    )SRC");
    REQUIRE(compiler != nullptr);
    auto context = compiler->get_context_for_test();
    auto unit = compiler->get_unit();
    auto root = unit->get_root_namespace();
    k::model::gen::rtti_emitter emitter(context, *unit);
    const auto& layout = context->module().getDataLayout();
    auto check_layout = [&](const std::shared_ptr<k::model::type>& value, bool complete) {
        auto* descriptor = record(emitter.describe(value));
        auto flags = number(field(descriptor, 2));
        CHECK((flags & 2) != 0);
        CHECK(((flags & 1) != 0) == complete);
        auto* native = value->get_llvm_type();
        REQUIRE(native != nullptr);
        REQUIRE(native->isSized());
        CHECK(number(field(descriptor, 3)) == layout.getTypeAllocSize(native).getFixedValue());
        CHECK(number(field(descriptor, 4)) == layout.getABITypeAlign(native).value());
    };
    auto make_alias = [&](const std::string& name, const std::shared_ptr<k::model::type>& value) {
        auto declaration = k::model::alias_definition::make_shared(
            root, name, k::model::alias_definition::kind_t::STRONG);
        declaration->set_target_type(value);
        root->add_alias(declaration);
        return context->create_alias_type(declaration, value);
    };

    // Source T[] remains a fixed reference, including const elements and typedefs.
    auto source = array(field(signature(compiler, "arrays"), 1));
    REQUIRE(source.size() == 3);
    for (auto* argument : source)
        CHECK((number(field(record(argument), 2)) & 1) != 0);
    CHECK(number(field(record(source[0]), 0)) == 3);
    CHECK(number(field(record(source[1]), 0)) == 3);
    CHECK(number(field(record(source[2]), 0)) == 19);
    auto* source_array = record(field(record(source[0]), 6));
    CHECK(number(field(source_array, 0)) == 9);
    CHECK((number(field(source_array, 2)) & 1) == 0);

    // Value qualifiers also arise from const bindings and imported exact graphs.
    auto integer = context->from_type(k::model::primitive_type::INT);
    auto dynamic = integer->get_array();
    auto alias = make_alias("DynamicValue", dynamic);
    auto nested = make_alias("FrozenDynamicValue", alias->get_const());
    std::vector<std::shared_ptr<k::model::type>> incomplete{
        dynamic, dynamic->get_const(), alias, alias->get_const(), nested, nested->get_const()
    };
    for (const auto& value : incomplete) {
        check_layout(value, false);
        std::vector<std::shared_ptr<k::model::type>> addressers{
            value->get_reference(), value->get_view(), value->get_link(),
            value->get_pointer(), value->get_owner(), value->get_drain()
        };
        for (const auto& addresser : addressers) {
            check_layout(addresser, true);
            check_layout(addresser->get_const(), true);
        }
    }

    auto fixed = integer->get_array(3);
    auto fixed_alias = make_alias("FixedValue", fixed);
    auto fixed_nested = make_alias("FrozenFixedValue", fixed_alias->get_const());
    auto zero_extent = integer->get_array(0);
    auto empty = find_aggregate(compiler, "Empty");
    REQUIRE(empty != nullptr);
    auto empty_type = empty->get_struct_type();
    REQUIRE(empty_type != nullptr);
    CHECK(layout.getTypeAllocSize(empty_type->get_llvm_type()).getFixedValue() == 0);
    std::vector<std::shared_ptr<k::model::type>> complete{
        fixed, fixed->get_const(), fixed_alias, fixed_alias->get_const(),
        fixed_nested, fixed_nested->get_const(), zero_extent, zero_extent->get_const(),
        empty_type, empty_type->get_const()
    };
    for (const auto& value : complete) check_layout(value, true);
}

TEST_CASE("RTTI exact enum alias union and callable graphs do not collapse to LLVM storage",
          "[libk][rtti][signature][type]") {
    auto compiler = compile_model_with_stdlib(R"SRC(
        module rtti_signature_types;
        enum Mode { ON = 1; OFF = 2; }
        typedef Identifier : int;
        template<typename T> typedef TypedIdentity : T;
        union Choice { number : int; fraction : double; }
        union OtherChoice { flag : bool; }
        enumValue(value : Mode) : Mode { return value; }
        aliasValue(value : Identifier) : Identifier { return value; }
        templateAlias(value : TypedIdentity<int>) : TypedIdentity<int> { return value; }
        templateAliasLong(value : TypedIdentity<long>) : TypedIdentity<long> { return value; }
        choice(value : Choice&) {}
        otherChoice(value : OtherChoice&) {}
        callback(value : *(int):int) {}
        wide(value : long long) : long long { return value; }
        identity(value : int) : int { return value; }
        factory() : *(int):int { return identity; }
    )SRC");
    REQUIRE(compiler != nullptr);
    auto* enumeration = record(field(signature(compiler, "enumValue"), 0));
    CHECK(number(field(enumeration, 0)) == 17);
    CHECK(number(field(record(field(enumeration, 6)), 0)) == 1);
    auto* alias = record(field(signature(compiler, "aliasValue"), 0));
    CHECK(number(field(alias, 0)) == 19);
    CHECK(number(field(record(field(alias, 12)), 0)) == 1);
    CHECK(field(alias, 12) == field(alias, 6));
    auto* template_alias = record(field(signature(compiler, "templateAlias"), 0));
    CHECK(number(field(template_alias, 0)) == 19);
    auto alias_arguments = array(field(record(field(template_alias, 13)), 1));
    REQUIRE(alias_arguments.size() == 1);
    CHECK(number(field(record(field(record(alias_arguments[0]), 1)), 1)) == k::model::primitive_type::INT);
    auto* long_alias = record(field(signature(compiler, "templateAliasLong"), 0));
    CHECK(long_alias != template_alias);
    auto long_alias_arguments = array(field(record(field(long_alias, 13)), 1));
    REQUIRE(long_alias_arguments.size() == 1);
    CHECK(number(field(record(field(record(long_alias_arguments[0]), 1)), 1)) == k::model::primitive_type::LONG);
    auto* union_type = record(field(record(array(field(signature(compiler, "choice"), 1))[0]), 6));
    CHECK(number(field(union_type, 0)) == 18);
    auto cases = array(field(union_type, 19));
    REQUIRE(cases.size() == 2);
    CHECK(number(field(record(cases[0]), 2)) == 0);
    CHECK(number(field(record(cases[1]), 2)) == 1);
    CHECK(number(field(union_type, 21)) > number(field(union_type, 20)));
    auto* other_union = record(field(record(array(field(signature(compiler, "otherChoice"), 1))[0]), 6));
    CHECK(other_union != union_type);
    CHECK(text(field(other_union, 11)) != text(field(union_type, 11)));
    CHECK(array(field(other_union, 19)).size() == 1);
    auto* callable_signature = signature(compiler, "callback");
    CHECK(number(field(callable_signature, 6)) == 5);
    auto* callable = record(array(field(callable_signature, 1))[0]);
    CHECK(number(field(callable, 0)) == 11);
    CHECK(array(field(callable, 8)).size() == 1);
    CHECK(number(field(record(field(callable, 7)), 1)) == k::model::primitive_type::INT);
    auto* wide = signature(compiler, "wide");
    CHECK(number(field(wide, 6)) == 6);
    CHECK(number(field(record(array(field(record(field(wide, 3)), 5))[0]), 1)) == 128);
    auto* factory = signature(compiler, "factory");
    CHECK(number(field(factory, 6)) == 6); // existing direct native aggregate return: explicitly unproved
    auto* physical_result = record(field(record(field(factory, 3)), 4));
    CHECK(number(field(physical_result, 0)) == 4);
    CHECK(array(field(physical_result, 11)).size() == 2);
}

TEST_CASE("RTTI virtual records normalize existing adjustment thunks to bodies and exact projections",
          "[libk][rtti][signature][virtual]") {
    auto compiler = compile_model_with_stdlib(R"SRC(
        module rtti_signature_virtual;
        abstract class Abstract {
            public Abstract() {}
            public implemented(value : int) : long { return (long) value + 7L; }
            public abstract missing(value : int) : long;
        }
        class Concrete : public Abstract {
            public Concrete() {}
            public override missing(value : int) : long { return (long) value; }
        }
        class Base {
            public Base() {}
            public score(value : int) : long { return (long) value; }
        }
        class Other {
            public Other() {}
            public other(value : int) : long { return (long) value; }
        }
        class Derived : public Base, public Other {
            public Derived() {}
            public override score(value : int) : long { return (long) value + 42L; }
            public override other(value : int) : long { return (long) value + 43L; }
        }
    )SRC");
    REQUIRE(compiler != nullptr);
    auto abstract = find_klass(compiler, "Abstract");
    REQUIRE(abstract != nullptr);
    auto* abstract_sig = signature(compiler, abstract->get_function("missing"));
    CHECK(number(field(abstract_sig, 6)) == 1);
    CHECK(field(abstract_sig, 8)->isNullValue());
    REQUIRE_FALSE(field(abstract_sig, 4)->isNullValue());
    auto context = compiler->get_context_for_test();
    auto* abstract_rtti = abstract->get_vtable()->llvm_rtti_global;
    REQUIRE(abstract_rtti != nullptr);
    CHECK(abstract_rtti->isConstant());
    CHECK(abstract->get_vtable()->llvm_global == nullptr);
    auto* abstract_type = record(field(record(abstract_rtti), 14));
    CHECK((number(field(abstract_type, 2)) & 16) != 0);
    CHECK((number(field(abstract_type, 2)) & 2) != 0);
    CHECK((number(field(abstract_type, 2)) & 8) == 0);
    CHECK(field(abstract_type, 15)->isNullValue());
    CHECK_FALSE(field(abstract_type, 10)->isNullValue());
    CHECK_FALSE(field(abstract_type, 14)->isNullValue());
    auto implemented = abstract->get_function("implemented");
    auto* implemented_sig = signature(compiler, implemented);
    auto* implemented_body = context->lookup_llvm_function(implemented);
    REQUIRE(implemented_body != nullptr);
    CHECK(number(field(implemented_sig, 6)) == direct_eligibility(context->module()));
    CHECK(actual_entry(implemented_sig) == implemented_body);
    CHECK_FALSE(implemented_body->isDeclaration());
    auto concrete = find_klass(compiler, "Concrete");
    REQUIRE(concrete != nullptr);
    auto* concrete_type = record(field(record(concrete->get_vtable()->llvm_rtti_global), 14));
    CHECK((number(field(concrete_type, 2)) & 16) == 0);
    bool abstract_body_selected = false;
    for (auto* pointer : array(field(concrete_type, 15))) {
        auto* body = record(pointer);
        if (field(body, 2) == rtti_context::signatures(*context).at(implemented.get())) {
            CHECK(actual_entry(record(field(body, 2))) == implemented_body);
            abstract_body_selected = true;
        }
    }
    CHECK(abstract_body_selected);
    auto derived = find_klass(compiler, "Derived");
    REQUIRE(derived != nullptr);
    auto* rtti = derived->get_vtable()->llvm_rtti_global;
    REQUIRE(rtti != nullptr);
    auto* exact = record(field(record(rtti), 14));
    auto bodies = array(field(exact, 15));
    REQUIRE_FALSE(bodies.empty());
    unsigned adjusted = 0, identities = 0, producer_only = 0, local_entries = 0;
    for (auto* pointer : bodies) {
        auto* entry = record(pointer);
        auto* sig = record(field(entry, 2));
        auto contract = function_for_signature(*context, field(entry, 2));
        const auto& provider = k::model::gen::redirect_provider(*contract);
        auto* body = llvm::dyn_cast<llvm::Function>(actual_entry(sig));
        if (!body) {
            CHECK(field(sig, 8)->isNullValue());
            CHECK((provider.is_external() || provider.is_extern()));
            CHECK(number(field(sig, 6)) == (provider.is_extern() ? 8 : 9));
            REQUIRE_FALSE(field(sig, 3)->isNullValue());
            if (provider.is_external())
                CHECK((number(field(record(field(sig, 3)), 10)) & 4) != 0);
            ++producer_only;
            continue;
        }
        CHECK_FALSE(provider.is_external());
        CHECK_FALSE(provider.is_extern());
        CHECK(actual_entry(sig) == context->lookup_llvm_function(contract));
        CHECK(number(field(sig, 6)) == direct_eligibility(context->module()));
        ++local_entries;
        auto source_subobject = aggregate_for_descriptor(*context, field(entry, 0));
        auto receiver_owner = k::model::gen::implementation_receiver_owner(*contract);
        REQUIRE(receiver_owner != nullptr);
        auto expected = k::model::gen::describe_rtti_receiver_projection(
            *derived, *source_subobject, *receiver_owner, context->module().getDataLayout());
        check_projection(field(entry, 5), expected);
        bool actual = false;
        for (const auto& function : derived->functions()) {
            if (compiler->get_context_for_test()->lookup_llvm_function(function) == body) actual = true;
        }
        // The universal destructor may be inherited; public overrides must map
        // directly to their model declaration, never the vtable's thunk symbol.
        if (body == compiler->get_context_for_test()->lookup_llvm_function(derived->get_function("other"))
            || body == compiler->get_context_for_test()->lookup_llvm_function(derived->get_function("score"))) {
            CHECK(actual);
            CHECK(field(entry, 3) == field(record(field(sig, 4)), 0));
            auto* projection = record(field(entry, 5));
            if (signed_number(field(projection, 0)) != 0) ++adjusted;
            else if (field(projection, 1)->isNullValue() && number(field(projection, 2)) == 0) {
                CHECK(llvm::isa<llvm::ConstantAggregateZero>(projection));
                ++identities;
            }
        }
    }
    CHECK(adjusted > 0);
    CHECK(identities > 0);
    CHECK(producer_only > 0);
    CHECK(local_entries > 0);
    unsigned secondary_slots = 0, destructor_slots = 0;
    for (const auto& [subobject, layout] : derived->get_secondary_vtables()) {
        REQUIRE(layout != nullptr);
        REQUIRE(layout->llvm_global != nullptr);
        auto* initializer = record(layout->llvm_global);
        auto* type = llvm::cast<llvm::StructType>(initializer->getType());
        REQUIRE(layout->entries.size() + 1 == type->getNumElements());
        llvm::Constant* source_type = nullptr;
        for (const auto& [semantic, descriptor] : rtti_context::types(*context))
            if (semantic == subobject->get_struct_type()) source_type = descriptor;
        REQUIRE(source_type != nullptr);
        for (unsigned slot = 0; slot < layout->entries.size(); ++slot) {
            auto* target = field(initializer, slot + 1);
            if (target->isNullValue()) continue;
            const auto& model = layout->entries[slot].func;
            REQUIRE(model != nullptr);
            CHECK(layout->entries[slot].slot_index == slot);
            if (std::dynamic_pointer_cast<k::model::destructor>(model)) ++destructor_slots;
            bool found = false;
            for (auto* pointer : bodies) {
                auto* entry = record(pointer);
                if (field(entry, 0) != source_type || number(field(entry, 1)) != slot) continue;
                CHECK(field(entry, 4)->stripPointerCastsAndAliases() == layout->llvm_global);
                CHECK(field(entry, 2) == rtti_context::signatures(*context).at(model.get()));
                auto* native_entry = field(record(field(entry, 2)), 8);
                if (!native_entry->isNullValue()) {
                    CHECK(native_entry->stripPointerCastsAndAliases() == context->lookup_llvm_function(model));
                } else {
                    const auto& provider = k::model::gen::redirect_provider(*model);
                    CHECK((provider.is_external() || provider.is_extern()));
                    CHECK(number(field(record(field(entry, 2)), 6))
                        == (provider.is_extern() ? 8 : 9));
                }
                found = true;
            }
            CHECK(found);
            ++secondary_slots;
        }
    }
    CHECK(secondary_slots > 0);
    CHECK(destructor_slots > 0); // Includes generated universal-destructor slots, not only user overrides.

    bool contextual_failure = false;
    for (const auto& [subobject, layout] : derived->get_secondary_vtables()) {
        auto* initializer = record(layout->llvm_global);
        for (unsigned slot = 0; slot < layout->entries.size(); ++slot) {
            auto* target = field(initializer, slot + 1);
            if (target->isNullValue()) continue;
            auto model = layout->entries[slot].func;
            layout->entries[slot].func.reset();
            k::model::gen::rtti_emitter emitter(context, *compiler->get_unit());
            CHECK_THROWS_WITH(emitter.finalize_virtual_bodies(),
                Catch::Matchers::ContainsSubstring("Missing semantic RTTI virtual slot contract")
                && Catch::Matchers::ContainsSubstring("aggregate=" + derived->get_fq_name())
                && Catch::Matchers::ContainsSubstring("sourceSubobject=" + subobject->get_fq_name())
                && Catch::Matchers::ContainsSubstring("slot=" + std::to_string(slot))
                && Catch::Matchers::ContainsSubstring("vtable=" + layout->llvm_global->getName().str())
                && Catch::Matchers::ContainsSubstring("target=" + target->getName().str())
                && Catch::Matchers::ContainsSubstring("model=<missing>"));
            layout->entries[slot].func = std::move(model);
            auto contracts = std::move(layout->entries);
            layout->entries.clear();
            CHECK_THROWS_WITH(emitter.finalize_virtual_bodies(),
                Catch::Matchers::ContainsSubstring("RTTI virtual table contract count differs")
                && Catch::Matchers::ContainsSubstring("sourceSubobject=" + subobject->get_fq_name())
                && Catch::Matchers::ContainsSubstring("target=" + target->getName().str())
                && Catch::Matchers::ContainsSubstring("contracts=0"));
            layout->entries = std::move(contracts);
            emitter.finalize_virtual_bodies();
            contextual_failure = true;
            break;
        }
        if (contextual_failure) break;
    }
    CHECK(contextual_failure);

    k::model::gen::rtti_emitter emitter(context, *compiler->get_unit());
    abstract_rtti->setConstant(false);
    CHECK_THROWS_WITH(emitter.finalize_virtual_bodies(),
        Catch::Matchers::ContainsSubstring("Missing finalized RTTI abstract declaration")
        && Catch::Matchers::ContainsSubstring("aggregate=" + abstract->get_fq_name())
        && Catch::Matchers::ContainsSubstring("abstract=true"));
    abstract_rtti->setConstant(true);
    auto* concrete_vtable = concrete->get_vtable()->llvm_global;
    concrete->get_vtable()->llvm_global = nullptr;
    CHECK_THROWS_WITH(emitter.finalize_virtual_bodies(),
        Catch::Matchers::ContainsSubstring("Missing finalized RTTI virtual table")
        && Catch::Matchers::ContainsSubstring("aggregate=" + concrete->get_fq_name())
        && Catch::Matchers::ContainsSubstring("abstract=false"));
    concrete->get_vtable()->llvm_global = concrete_vtable;
    emitter.finalize_virtual_bodies();
}

TEST_CASE("RTTI concrete template origins separate member arguments from their enclosing host",
          "[libk][rtti][signature][template]") {
    auto compiler = compile_model_with_stdlib(R"SRC(
        module rtti_signature_templates;
        struct Point { x : int; y : int; }
        enum Label { FIRST = 1; SECOND = 2; }
        template<Point P>
        stamp() : int { return 1; }
        template<Label L>
        labeled() : int { return 1; }
        template<typename T, int N>
        addN(value : T) : T { return value + N; }
        template<typename... Ts>
        packCount(Ts... values) : int { return 1; }
        template<typename T, int N>
        class Host {
            public:
            Host() {}
            template<typename U>
            convert(value : U) : U { return value; }
        }
        force() : long {
            host : Host<int, 7>;
            return host.convert<long>(42L) + (long) addN<int, 3>(1)
                + (long) packCount<int, long>(1, 2L) + (long) packCount<>()
                + (long) stamp<{ .x = 3, .y = 4 }>() + (long) labeled<Label::FIRST>();
        }
    )SRC");
    REQUIRE(compiler != nullptr);
    std::shared_ptr<k::model::function> concrete;
    std::shared_ptr<k::model::function> recipe;
    for (const auto& fn : compiler->get_unit()->get_root_namespace()->functions()) {
        if (fn->get_tpl_base_name() == "addN") concrete = fn;
        if (fn->get_short_name() == "addN" && fn->is_template()) recipe = fn;
    }
    REQUIRE(concrete != nullptr);
    REQUIRE(recipe != nullptr);
    CHECK(recipe->parent<k::model::ns>() == compiler->get_unit()->get_root_namespace());
    const auto recipe_name = compiler->get_unit()->get_unit_name().with_root_prefix()
        .with_back(recipe->get_short_name());
    auto* concrete_sig = signature(compiler, concrete);
    CHECK(actual_entry(concrete_sig) == compiler->get_context_for_test()->lookup_llvm_function(concrete));
    CHECK(text(field(record(field(concrete_sig, 5)), 0)) == recipe_name.to_string());
    auto args = array(field(record(field(concrete_sig, 5)), 1));
    REQUIRE(args.size() == 2);
    CHECK(number(field(record(args[0]), 0)) == 0);
    CHECK(number(field(record(args[1]), 0)) == 1);
    auto* stored_value = llvm::dyn_cast<llvm::GlobalVariable>(field(record(args[1]), 2));
    REQUIRE(stored_value != nullptr);
    CHECK(number(stored_value->getInitializer()) == 3);
    CHECK(number(field(record(args[1]), 3)) == sizeof(int32_t));
    auto* dependent = signature(compiler, recipe);
    CHECK(number(field(dependent, 6)) == 3);
    CHECK(field(dependent, 8)->isNullValue());
    CHECK(field(dependent, 3)->isNullValue());
    CHECK(text(field(record(field(dependent, 5)), 0)) == recipe_name.to_string());
    bool full_pack = false, empty_pack = false;
    for (const auto& fn : compiler->get_unit()->get_root_namespace()->functions()) {
        if (fn->get_tpl_base_name() != "packCount") continue;
        auto* sig = signature(compiler, fn);
        CHECK(text(field(record(field(sig, 5)), 0))
            == compiler->get_unit()->get_root_namespace()->get_name().with_back("packCount").to_string());
        auto arguments = array(field(record(field(sig, 5)), 1));
        REQUIRE(arguments.size() == 1);
        auto* argument = record(arguments[0]);
        CHECK(number(field(argument, 0)) == 2);
        auto pack = array(field(argument, 5));
        if (pack.empty()) {
            empty_pack = true;
            CHECK(number(field(sig, 2)) == 0);
        } else {
            full_pack = true;
            CHECK(pack.size() == 2);
            CHECK(number(field(sig, 2)) == 2);
        }
    }
    CHECK(full_pack);
    CHECK(empty_pack);
    bool aggregate_value_found = false;
    for (const auto& fn : compiler->get_unit()->get_root_namespace()->functions()) {
        if (fn->get_tpl_base_name() != "stamp") continue;
        auto arguments = array(field(record(field(signature(compiler, fn), 5)), 1));
        REQUIRE(arguments.size() == 1);
        auto* argument = record(arguments[0]);
        CHECK(number(field(argument, 0)) == 1);
        CHECK(number(field(record(field(argument, 1)), 0)) == 13);
        auto* storage = record(field(argument, 2));
        CHECK(number(field(storage, 0)) == 3);
        CHECK(number(field(storage, 1)) == 4);
        const auto& layout = compiler->get_context_for_test()->module().getDataLayout();
        CHECK(number(field(argument, 3)) == layout.getTypeAllocSize(storage->getType()).getFixedValue());
        CHECK(number(field(argument, 4)) == layout.getABITypeAlign(storage->getType()).value());
        aggregate_value_found = true;
    }
    CHECK(aggregate_value_found);
    bool enum_value_found = false;
    for (const auto& fn : compiler->get_unit()->get_root_namespace()->functions()) {
        if (fn->get_tpl_base_name() != "labeled") continue;
        auto arguments = array(field(record(field(signature(compiler, fn), 5)), 1));
        REQUIRE(arguments.size() == 1);
        auto* argument = record(arguments[0]);
        CHECK(number(field(argument, 0)) == 1);
        CHECK(number(field(record(field(argument, 1)), 0)) == 17);
        auto* storage = llvm::dyn_cast<llvm::GlobalVariable>(field(argument, 2));
        REQUIRE(storage != nullptr);
        CHECK(number(storage->getInitializer()) == 1);
        enum_value_found = true;
    }
    CHECK(enum_value_found);
    bool member_found = false;
    for (const auto& child : compiler->get_unit()->get_root_namespace()->get_children()) {
        auto host = std::dynamic_pointer_cast<k::model::aggregate>(child);
        if (!host || host->get_tpl_base_name() != "Host") continue;
        for (const auto& fn : host->functions()) {
            if (fn->get_tpl_base_name() != "convert") continue;
            auto* sig = signature(compiler, fn);
            auto* own = record(field(sig, 5));
            auto* enclosing = record(field(sig, 10));
            auto primary = host->get_tpl_definition();
            REQUIRE(primary != nullptr);
            CHECK(primary->parent<k::model::ns>() == compiler->get_unit()->get_root_namespace());
            const auto primary_name = compiler->get_unit()->get_unit_name().with_root_prefix()
                .with_back(primary->get_short_name());
            CHECK(text(field(enclosing, 0)) == primary_name.to_string());
            CHECK(text(field(own, 0)) == primary_name.with_back("convert").to_string());
            CHECK(array(field(own, 1)).size() == 1);
            CHECK(array(field(enclosing, 1)).size() == 2);
            CHECK(field(own, 2) == field(sig, 10));
            member_found = true;
        }
    }
    CHECK(member_found);
}

TEST_CASE("RTTI qualified enum template constants retain exact nominal types and typed storage",
          "[libk][rtti][signature][template][enum]") {
    auto compiler = compile_model_with_stdlib(R"SRC(
        module rtti_qualified_enum_constants;
        enum ThemeMode : int { Light = 41; Dark = 42; }
        namespace ui {
            namespace theme {
                enum ThemeMode : byte { Light = 1; Dark = 5; }
            }
        }
        namespace other {
            namespace theme {
                enum ThemeMode : unsigned short { Light = 65530; Dark = 65531; }
            }
        }
        typedef FrozenMode : const ui::theme::ThemeMode;
        template<ui::theme::ThemeMode M>
        get_mode() : int { return M; }
        template<other::theme::ThemeMode M>
        get_other_mode() : int { return M; }
        wrapped(value : FrozenMode) {}
        force() : int {
            return get_mode<ui::theme::ThemeMode::Dark>()
                + get_mode<ui::theme::ThemeMode::Light>()
                + get_mode<ui::theme::ThemeMode::Dark>()
                + get_other_mode<other::theme::ThemeMode::Dark>()
                + get_other_mode<other::theme::ThemeMode::Dark>();
        }
    )SRC");
    REQUIRE(compiler != nullptr);
    auto context = compiler->get_context_for_test();
    auto root = compiler->get_unit()->get_root_namespace();
    auto ui = root->get_child_namespace("ui");
    auto other = root->get_child_namespace("other");
    REQUIRE(ui != nullptr);
    REQUIRE(other != nullptr);
    auto theme = ui->get_child_namespace("theme");
    auto other_theme = other->get_child_namespace("theme");
    REQUIRE(theme != nullptr);
    REQUIRE(other_theme != nullptr);
    auto mode = theme->get_enum("ThemeMode");
    auto other_mode = other_theme->get_enum("ThemeMode");
    auto homonym = root->get_enum("ThemeMode");
    REQUIRE(mode != nullptr);
    REQUIRE(other_mode != nullptr);
    REQUIRE(homonym != nullptr);
    k::model::gen::rtti_emitter emitter(context, *compiler->get_unit());
    auto* exact_mode = emitter.describe(mode->get_enum_type());
    auto* exact_other = emitter.describe(other_mode->get_enum_type());
    CHECK(exact_mode != exact_other);
    CHECK(exact_mode != emitter.describe(homonym->get_enum_type()));
    CHECK(number(field(record(field(record(exact_mode), 6)), 1))
        == k::model::primitive_type::BYTE);
    CHECK(number(field(record(field(record(exact_other), 6)), 1))
        == k::model::primitive_type::UNSIGNED_SHORT);
    const auto& layout = context->module().getDataLayout();
    auto check_value = [&](llvm::Constant* argument, llvm::Constant* semantic,
                           llvm::Type* storage_type, int64_t expected, bool is_signed) {
        CHECK(number(field(argument, 0)) == 1);
        CHECK(field(argument, 1) == semantic);
        auto* storage = llvm::dyn_cast<llvm::GlobalVariable>(field(argument, 2));
        REQUIRE(storage != nullptr);
        REQUIRE(storage->hasInitializer());
        CHECK(storage->isConstant());
        CHECK(storage->getValueType() == storage_type);
        auto* value = storage->getInitializer();
        CHECK(value->getType() == storage_type);
        CHECK((is_signed ? signed_number(value) : static_cast<int64_t>(number(value))) == expected);
        CHECK(number(field(argument, 3)) == layout.getTypeAllocSize(storage_type).getFixedValue());
        CHECK(number(field(argument, 4)) == layout.getABITypeAlign(storage_type).value());
    };
    unsigned modes = 0, other_modes = 0;
    bool light = false, dark = false;
    llvm::Constant* first_specialization = nullptr;
    for (const auto& fn : root->functions()) {
        const bool is_mode = fn->get_tpl_base_name() == "get_mode";
        if (!is_mode && fn->get_tpl_base_name() != "get_other_mode") continue;
        auto* sig = signature(compiler, fn);
        CHECK(number(field(sig, 6)) == direct_eligibility(context->module()));
        auto* origin = record(field(sig, 5));
        CHECK(text(field(origin, 0)) == root->get_name()
            .with_back(is_mode ? "get_mode" : "get_other_mode").to_string());
        auto arguments = array(field(origin, 1));
        REQUIRE(arguments.size() == 1);
        auto* argument = record(arguments[0]);
        if (is_mode) {
            ++modes;
            REQUIRE(fn->get_tpl_args().size() == 1);
            const auto value = fn->get_tpl_args()[0].value_as_int64();
            CHECK((value == 1 || value == 5));
            light |= value == 1;
            dark |= value == 5;
            check_value(argument, exact_mode, mode->get_enum_type()->get_llvm_type(), value, true);
            if (first_specialization) CHECK(field(sig, 5) != first_specialization);
            else first_specialization = field(sig, 5);
        } else {
            ++other_modes;
            check_value(argument, exact_other, other_mode->get_enum_type()->get_llvm_type(), 65531, false);
        }
    }
    CHECK(modes == 2);
    CHECK(other_modes == 1);
    CHECK(light);
    CHECK(dark);

    // Model controls cover negative signed storage, qualifiers and strong aliases.
    auto wrapped = root->get_function("wrapped");
    REQUIRE(wrapped != nullptr);
    auto alias = root->get_alias("FrozenMode");
    REQUIRE(alias != nullptr);
    auto alias_type = alias->get_declared_type();
    REQUIRE(alias_type != nullptr);
    auto qualified = rtti_context::create_unresolved(*context, k::name::from("ThemeMode"));
    qualified->set_definition_namespace(theme);
    auto constant_type = qualified->get_const();
    auto alias_constant_type = alias_type->get_const();
    wrapped->set_tpl_instantiation_info("wrapped", {
        k::model::template_argument::make_value(-7, constant_type),
        k::model::template_argument::make_value(5, alias_constant_type)
    });
    auto* wrapped_sig = record(emitter.signature(wrapped, true));
    auto wrapped_args = array(field(record(field(wrapped_sig, 5)), 1));
    REQUIRE(wrapped_args.size() == 2);
    CHECK(number(field(wrapped_sig, 6)) == direct_eligibility(context->module()));
    auto* qualified_mode = field(record(wrapped_args[0]), 1);
    CHECK(number(field(record(qualified_mode), 0)) == 2);
    CHECK(field(record(qualified_mode), 6) == exact_mode);
    check_value(record(wrapped_args[0]), qualified_mode,
        mode->get_enum_type()->get_llvm_type(), -7, true);
    check_value(record(wrapped_args[1]), emitter.describe(alias_constant_type),
        mode->get_enum_type()->get_llvm_type(), 5, true);
    CHECK(field(record(field(record(wrapped_args[1]), 1)), 6) == emitter.describe(alias_type));
    CHECK(number(field(record(emitter.describe(alias_type)), 0)) == 19);
    auto* alias_underlying = record(field(record(emitter.describe(alias_type)), 6));
    CHECK(number(field(alias_underlying, 0)) == 2);
    CHECK(field(alias_underlying, 6) == exact_mode);
    const auto valid_args = wrapped->get_tpl_args();
    wrapped->set_tpl_instantiation_info("wrapped", {
        k::model::template_argument::make_value(5,
            rtti_context::create_unresolved(*context, k::name::from("missing::ThemeMode")))
    });
    CHECK_THROWS_WITH(emitter.signature(wrapped, true),
        "RTTI template value has no exact typed initializer");
    wrapped->set_tpl_instantiation_info("wrapped", valid_args);
    emitter.signature(wrapped, true);
}

TEST_CASE("RTTI primary identities distinguish namespace homonyms and retain nested host arguments",
          "[libk][rtti][signature][template][producer][identity]") {
    auto compiler = compile_model_with_stdlib(R"SRC(
        module rtti_primary_scopes;
        namespace left {
            template<typename T> struct Box { value : T; }
            template<typename T> identity(value : T) : T { return value; }
            template<typename T> class Host {
                public:
                Host() {}
                static struct Nested { value : int; }
                template<typename U> convert(value : U) : U { return value; }
            }
        }
        namespace right {
            template<typename T> struct Box { value : T; }
            template<typename T> identity(value : T) : T { return value; }
            template<typename T> class Host {
                public:
                Host() {}
                static struct Nested { value : int; }
                template<typename U> convert(value : U) : U { return value; }
            }
        }
        observe(a : left::Box<int>&, b : left::Box<long>&, c : right::Box<int>&,
                first : left::Host<int>&, second : left::Host<long>&, third : right::Host<int>&) {}
        force() : long {
            first : left::Host<int>;
            second : left::Host<long>;
            third : right::Host<int>;
            return first.convert<long>(1L) + (long) second.convert<int>(2)
                + third.convert<long>(3L) + (long) left::identity<int>(4)
                + (long) right::identity<int>(5);
        }
    )SRC");
    REQUIRE(compiler != nullptr);
    auto context = compiler->get_context_for_test();
    auto unit = compiler->get_unit();
    auto root = unit->get_root_namespace();
    const auto module = unit->get_unit_name().with_root_prefix();
    k::model::gen::rtti_emitter emitter(context, *unit);
    auto types = array(field(signature(compiler, "observe"), 1));
    REQUIRE(types.size() == 6);
    auto identity = [&](llvm::Constant* type, const k::name& expected) {
        CHECK((number(field(type, 2)) & 3) == 3);
        auto* nominal = record(field(type, 23));
        CHECK(text(field(nominal, 2)) == std::to_string(number(field(type, 0))) + ":"
            + k::model::mangler::mangle_fq_name(expected, true));
        CHECK(field(nominal, 0) == field(nominal, 1));
        CHECK_FALSE(field(nominal, 1)->isNullValue());
        return nominal;
    };
    std::vector<llvm::Constant*> boxes;
    for (unsigned index = 0; index < 3; ++index) {
        auto name = module.with_back(index == 2 ? "right" : "left").with_back("Box");
        auto* type = referent(types[index]);
        boxes.push_back(identity(type, name));
        auto* origin = record(field(type, 13));
        CHECK(text(field(origin, 0)) == name.to_string());
        auto args = array(field(origin, 1));
        REQUIRE(args.size() == 1);
        CHECK(number(field(record(field(record(args[0]), 1)), 1)) == (index == 1 ? 8 : 6));
    }
    CHECK(text(field(boxes[0], 2)) == text(field(boxes[1], 2)));
    CHECK(text(field(boxes[0], 2)) != text(field(boxes[2], 2)));
    CHECK(field(boxes[0], 0) == field(boxes[2], 0));
    CHECK(number(field(referent(types[0]), 3)) == number(field(referent(types[2]), 3)));

    for (const auto* name : {"left", "right"}) {
        auto scope = root->get_child_namespace(name);
        REQUIRE(scope != nullptr);
        std::shared_ptr<k::model::function> primary, concrete;
        for (const auto& function : scope->functions()) {
            if (function->get_short_name() == "identity" && function->is_template()) primary = function;
            if (function->get_tpl_base_name() == "identity") concrete = function;
        }
        REQUIRE(primary != nullptr);
        REQUIRE(concrete != nullptr);
        CHECK(primary->parent<k::model::ns>() == scope);
        const auto expected = module.with_back(name).with_back("identity");
        auto* recipe = record(emitter.signature(primary));
        auto* instance = signature(compiler, concrete);
        CHECK(text(field(record(field(recipe, 5)), 0)) == expected.to_string());
        CHECK(text(field(record(field(instance, 5)), 0)) == expected.to_string());
        CHECK(number(field(recipe, 6)) == 3);
        CHECK(field(recipe, 8)->isNullValue());
        CHECK(actual_entry(instance) == context->lookup_llvm_function(concrete));
    }

    std::vector<llvm::Constant*> hosts, nested;
    for (unsigned index = 0; index < 3; ++index) {
        auto host_pointer = field(record(types[index + 3]), 6);
        auto host = aggregate_for_descriptor(*context, host_pointer);
        auto name = module.with_back(index == 2 ? "right" : "left").with_back("Host");
        auto* host_type = record(host_pointer);
        hosts.push_back(identity(host_type, name));
        auto* host_origin = record(field(host_type, 13));
        CHECK(text(field(host_origin, 0)) == name.to_string());
        auto origin_args = array(field(host_origin, 1));
        REQUIRE(origin_args.size() == 1);
        CHECK(number(field(record(field(record(origin_args[0]), 1)), 1)) == (index == 1 ? 8 : 6));
        auto child = host->get_aggregate("Nested");
        REQUIRE(child != nullptr);
        CHECK(child->parent<k::model::aggregate>() == host);
        auto* child_type = record(emitter.describe(child->get_struct_type()));
        nested.push_back(identity(child_type, name.with_back("Nested")));
        auto* child_origin = record(field(child_type, 13));
        CHECK(text(field(child_origin, 0)) == name.to_string());
        auto child_args = array(field(child_origin, 1));
        REQUIRE(child_args.size() == 1);
        CHECK(number(field(record(field(record(child_args[0]), 1)), 1)) == (index == 1 ? 8 : 6));
        bool member_found = false;
        for (const auto& function : host->functions()) {
            if (function->get_tpl_base_name() != "convert") continue;
            auto* sig = signature(compiler, function);
            auto* own = record(field(sig, 5));
            auto* enclosing = record(field(sig, 10));
            CHECK(text(field(own, 0)) == name.with_back("convert").to_string());
            CHECK(text(field(enclosing, 0)) == name.to_string());
            CHECK(field(own, 2) == field(sig, 10));
            auto own_args = array(field(own, 1));
            auto host_args = array(field(enclosing, 1));
            REQUIRE(own_args.size() == 1);
            REQUIRE(host_args.size() == 1);
            CHECK(number(field(record(field(record(own_args[0]), 1)), 1)) == (index == 1 ? 6 : 8));
            CHECK(number(field(record(field(record(host_args[0]), 1)), 1)) == (index == 1 ? 8 : 6));
            CHECK(actual_entry(sig) == context->lookup_llvm_function(function));
            member_found = true;
        }
        CHECK(member_found);
    }
    CHECK(text(field(hosts[0], 2)) == text(field(hosts[1], 2)));
    CHECK(text(field(hosts[0], 2)) != text(field(hosts[2], 2)));
    CHECK(text(field(nested[0], 2)) == text(field(nested[1], 2)));
    CHECK(text(field(nested[0], 2)) != text(field(nested[2], 2)));
    CHECK(field(nested[0], 0) == field(nested[2], 0));
}

TEST_CASE("RTTI recursive lifecycle graphs, deleted entries and fixed source-vararg arity are explicit",
          "[libk][rtti][signature][negative][parameter]") {
    auto compiler = compile_model_with_stdlib(R"SRC(
        module rtti_signature_markers;
        class Recursive {
            public Recursive(other : const Recursive&) {}
        }
        class PrivateCopy {
            private PrivateCopy(other : const PrivateCopy&) {}
        }
        class DeletedCopy {
            public DeletedCopy(other : const DeletedCopy&) -> delete;
        }
        class SoftCopy {
            public:
            alias Self : SoftCopy;
            SoftCopy(other : const Self&) {}
        }
        class DrainSelf {
            public DrainSelf(other : DrainSelf#) {}
        }
        class StrongReference {
            public:
            typedef StrongSelf : StrongReference;
            StrongReference(other : const StrongSelf&) {}
        }
        class StrongDrain {
            public:
            typedef StrongSelf : StrongDrain;
            StrongDrain(other : StrongSelf#) {}
        }
        class StrongWrapper {
            public:
            typedef StrongSelf : const StrongWrapper&;
            StrongWrapper(other : StrongSelf) {}
        }
        class Markers {
            public Markers() {}
            public static collect(values... : int) : int { return (int) values.size; }
            public main(value : int) : int -> delete;
        }
        observe(value : Recursive&) {}
        observeCopies(hidden : PrivateCopy&, deleted : DeletedCopy&) {}
        observeConversions(reference : StrongReference&, drain : StrongDrain&, wrapper : StrongWrapper&,
                           soft : SoftCopy&, moved : DrainSelf&) {}
    )SRC");
    REQUIRE(compiler != nullptr);
    auto markers = find_klass(compiler, "Markers");
    REQUIRE(markers != nullptr);
    auto collect = markers->get_function("collect");
    auto* sig = signature(compiler, collect);
    CHECK(number(field(sig, 2)) == 1);
    CHECK(field(sig, 4)->isNullValue());
    auto* native = record(field(sig, 3));
    CHECK(signed_number(field(native, 7)) == -1);
    auto* body = compiler->get_context_for_test()->lookup_llvm_function(collect);
    REQUIRE(body != nullptr);
    CHECK_FALSE(body->isVarArg());
    CHECK(body->arg_size() == 1);
    auto functions = array(field(record(markers->get_vtable()->llvm_rtti_global), 12));
    bool parameter_found = false;
    for (auto* pointer : functions) {
        auto* function = record(pointer);
        if (record(field(function, 8)) != sig) continue;
        CHECK((number(field(function, 5)) & 12) == 12); // existing member/static getters
        auto parameters = array(field(function, 7));
        REQUIRE(parameters.size() == 1);
        auto* parameter = record(parameters[0]);
        CHECK(number(field(parameter, 4)) == 0);
        CHECK(number(field(parameter, 5)) == 2);
        CHECK(field(parameter, 3)->isNullValue());
        CHECK(text(field(parameter, 2)) == "values");
        parameter_found = true;
    }
    CHECK(parameter_found);
    auto* deleted = signature(compiler, markers->get_function("main"));
    CHECK(number(field(deleted, 6)) == 2);
    CHECK(field(deleted, 8)->isNullValue());
    CHECK(field(deleted, 3)->isNullValue());
    auto* observer = signature(compiler, "observe");
    auto* recursive_pointer = field(record(array(field(observer, 1))[0]), 6);
    auto* recursive = record(recursive_pointer);
    auto operations = array(field(recursive, 14));
    bool copy_found = false;
    for (auto* pointer : operations) {
        auto* operation = record(pointer);
        if (number(field(operation, 0)) != 1) continue;
        auto* copy = record(field(operation, 2));
        auto params = array(field(copy, 1));
        REQUIRE(params.size() == 1);
        auto* referent = record(field(record(params[0]), 6));
        if (number(field(referent, 0)) == 2) referent = record(field(referent, 6));
        CHECK(referent == recursive);
        copy_found = true;
    }
    CHECK(copy_found);
    auto check_operation = [&](const char* name, k::model::visibility visibility,
                               bool deleted_copy, unsigned operation_kind) {
        auto aggregate = find_klass(compiler, name);
        REQUIRE(aggregate != nullptr);
        std::shared_ptr<k::model::constructor> constructor;
        for (const auto& candidate : aggregate->constructors()) {
            if (candidate->get_parameter_size() != 1) continue;
            REQUIRE(constructor == nullptr);
            constructor = candidate;
        }
        REQUIRE(constructor != nullptr);
        CHECK(constructor->get_visibility() == visibility);
        CHECK(constructor->is_deleted() == deleted_copy);
        auto* type = record(field(record(aggregate->get_vtable()->llvm_rtti_global), 14));
        unsigned copies = 0;
        for (auto* pointer : array(field(type, 14))) {
            auto* operation = record(pointer);
            if (number(field(operation, 0)) != operation_kind) continue;
            ++copies;
            const auto flags = number(field(operation, 1));
            CHECK((flags & 3) == static_cast<uint32_t>(visibility));
            CHECK(((flags & 4) != 0) == deleted_copy);
            CHECK(((flags & 8) != 0) == constructor->is_compiler_generated());
            auto* copy = record(field(operation, 2));
            CHECK(copy == signature(compiler, constructor));
            CHECK(number(field(copy, 2)) == 1);
            CHECK(referent(array(field(copy, 1))[0]) == type);
            if (deleted_copy) {
                CHECK(number(field(copy, 6)) == 2);
                CHECK(field(copy, 8)->isNullValue());
                CHECK(field(copy, 3)->isNullValue());
            } else {
                auto* body = compiler->get_context_for_test()->lookup_llvm_function(constructor);
                REQUIRE(body != nullptr);
                CHECK_FALSE(body->isDeclaration());
                CHECK(actual_entry(copy) == body);
                CHECK(number(field(copy, 6))
                    == direct_eligibility(compiler->get_context_for_test()->module()));
            }
        }
        CHECK(copies == 1);
    };
    check_operation("Recursive", k::model::PUBLIC, false, 1);
    check_operation("PrivateCopy", k::model::PRIVATE, false, 1);
    check_operation("DeletedCopy", k::model::PUBLIC, true, 1);
    check_operation("SoftCopy", k::model::PUBLIC, false, 1);
    check_operation("DrainSelf", k::model::PUBLIC, false, 2);

    auto context = compiler->get_context_for_test();
    k::model::gen::rtti_emitter emitter(context, *compiler->get_unit());
    for (const auto* name : {"StrongReference", "StrongDrain", "StrongWrapper"}) {
        auto aggregate = find_klass(compiler, name);
        REQUIRE(aggregate != nullptr);
        REQUIRE(aggregate->constructors().size() == 1);
        // Model copy selection is canonical; reflected lifecycle classification stays nominal.
        if (std::string(name) == "StrongDrain") {
            CHECK(aggregate->get_copy_constructor() == nullptr);
        } else {
            CHECK(aggregate->get_copy_constructor() == aggregate->constructors()[0]);
        }
        auto* rtti = record(aggregate->get_vtable()->llvm_rtti_global);
        auto* type = record(field(rtti, 14));
        for (auto* pointer : array(field(type, 14))) {
            auto* operation = record(pointer);
            CHECK(number(field(operation, 0)) != 1);
            CHECK(number(field(operation, 0)) != 2);
        }
        auto constructors = array(field(rtti, 13));
        REQUIRE(constructors.size() == 1);
        auto parameters = array(field(record(constructors[0]), 4));
        REQUIRE(parameters.size() == 1);
        auto* exact = field(record(parameters[0]), 6);
        auto* alias = referent(exact);
        CHECK(number(field(alias, 0)) == 19);
        CHECK(referent(field(alias, 12)) == type); // Same representation, distinct semantic type.
        REQUIRE(aggregate->constructors().size() == 1);
        auto constructor = aggregate->constructors()[0];
        auto* sig = record(emitter.signature(constructor));
        auto exact_parameters = array(field(sig, 1));
        REQUIRE(exact_parameters.size() == 1);
        CHECK(exact_parameters[0] == exact);
        CHECK(number(field(sig, 6)) == direct_eligibility(context->module()));
        auto* body = context->lookup_llvm_function(constructor);
        REQUIRE(body != nullptr);
        CHECK_FALSE(body->isDeclaration());
        CHECK(actual_entry(sig) == body);
    }
    for (auto& global : compiler->get_context_for_test()->module().globals())
        if (global.getName().starts_with("__k_rtti_type")) CHECK(global.hasInitializer());
}

TEST_CASE("RTTI erased generic receivers preserve unresolved contracts and exact stand-in kinds",
          "[libk][rtti][signature][generic][erased]") {
    auto compiler = compile_model_with_stdlib(R"SRC(
        module rtti_signature_erased;
        class Item {
            public value : int = 27;
            public Item() {}
        }
        generic<class T> class Box {
            private item : T*;
            public:
            Box() { item = null; }
            isNull() : bool { return item == null; }
            relay(value : T&) : T& { return value; }
        }
        observe(value : Box<Item>&) {}
        observeItem(value : Item&) {}
        force() : int {
            item : Item;
            box : Box<Item>;
            if (box.isNull()) return box.relay(item).value;
            return 0;
        }
    )SRC");
    REQUIRE(compiler != nullptr);
    auto context = compiler->get_context_for_test();
    auto root = compiler->get_unit()->get_root_namespace();
    auto observe = root->get_function("observe");
    REQUIRE(observe != nullptr);
    auto parameter = k::model::type::canonical(observe->parameters().front()->get_type());
    REQUIRE(k::model::type::is_reference(parameter));
    auto structure = std::dynamic_pointer_cast<k::model::struct_type>(parameter->get_subtype());
    REQUIRE(structure != nullptr);
    auto owner = structure->get_struct();
    REQUIRE(owner != nullptr);
    REQUIRE(owner->get_tpl_definition() != nullptr);
    CHECK(owner->get_tpl_definition()->is_generic());
    CHECK(owner->is_instantiation());
    CHECK_FALSE(owner->has_tpl_args());
    REQUIRE(structure->get_llvm_type() != nullptr);
    CHECK(structure->get_llvm_type()->isSized());

    k::model::gen::rtti_emitter emitter(context, *compiler->get_unit());
    auto* owner_pointer = emitter.describe(structure);
    auto* exact = record(owner_pointer);
    CHECK(number(field(exact, 0)) == 14);
    CHECK((number(field(exact, 2)) & 2) == 0);
    CHECK((number(field(exact, 2)) & 1) != 0);
    const auto& layout = context->module().getDataLayout();
    CHECK(number(field(exact, 3)) == layout.getTypeAllocSize(structure->get_llvm_type()).getFixedValue());
    CHECK(number(field(exact, 4)) == layout.getABITypeAlign(structure->get_llvm_type()).value());
    std::vector<std::shared_ptr<k::model::type>> wrappers{
        structure->get_const(), structure->get_reference(), structure->get_view(),
        structure->get_link(), structure->get_pointer(), structure->get_owner(), structure->get_drain()};
    for (const auto& wrapper : wrappers) {
        auto* metadata = record(emitter.describe(wrapper));
        CHECK((number(field(metadata, 2)) & 3) == 1);
        CHECK(number(field(metadata, 3)) == layout.getTypeAllocSize(wrapper->get_llvm_type()).getFixedValue());
        CHECK(number(field(metadata, 4)) == layout.getABITypeAlign(wrapper->get_llvm_type()).value());
        CHECK(field(metadata, 6) == owner_pointer);
    }
    CHECK(field(exact, 14)->isNullValue());
    CHECK(field(exact, 15)->isNullValue());
    CHECK(field(exact, 18)->isNullValue());
    auto* observed = signature(compiler, observe);
    CHECK(number(field(observed, 2)) == 1);
    CHECK(number(field(observed, 6)) == 3);
    CHECK(field(observed, 3)->isNullValue());
    CHECK(field(observed, 8)->isNullValue());
    CHECK(field(record(array(field(observed, 1))[0]), 6) == owner_pointer);
    auto* observed_body = context->lookup_llvm_function(observe);
    REQUIRE(observed_body != nullptr);
    CHECK_FALSE(observed_body->isDeclaration());

    auto relay = owner->get_function("relay");
    REQUIRE(relay != nullptr);
    REQUIRE_FALSE(owner->constructors().empty());
    std::vector<std::shared_ptr<k::model::function>> methods{
        relay, owner->get_function("isNull"), owner->constructors().front()};
    for (const auto& method : methods) {
        REQUIRE(method != nullptr);
        auto* body = context->lookup_llvm_function(method);
        REQUIRE(body != nullptr);
        CHECK_FALSE(body->isDeclaration());
        auto* sig = record(emitter.signature(method));
        CHECK(number(field(sig, 6)) == 3);
        CHECK(field(sig, 3)->isNullValue());
        CHECK(field(sig, 8)->isNullValue());
        CHECK(field(record(field(sig, 4)), 0) == owner_pointer);
        const auto& roles = rtti_context::argument_sources(*context).at(method.get());
        REQUIRE(roles.size() == body->arg_size());
        REQUIRE_FALSE(roles.empty());
        CHECK(roles.front() == -2);
        CHECK(method->get_this_parameter()->get_type()->get_llvm_type()
            == body->getArg(0)->getType());
    }
    auto* relay_signature = signature(compiler, relay);
    auto* result = record(field(relay_signature, 0));
    CHECK(number(field(result, 0)) == 3);
    auto* erased_pointer = field(result, 6);
    auto* erased = record(erased_pointer);
    CHECK(number(field(erased, 0)) == 21);
    CHECK((number(field(erased, 2)) & 2) == 0);
    CHECK(field(relay_signature, 0) == array(field(relay_signature, 1))[0]);

    auto byte = context->from_type(k::model::primitive_type::BYTE);
    auto stand_in = byte->get_erased_param();
    CHECK(emitter.describe(stand_in) == erased_pointer);
    auto* byte_pointer = record(emitter.describe(byte->get_pointer()));
    CHECK(number(field(byte_pointer, 0)) == 6);
    CHECK((number(field(byte_pointer, 2)) & 2) != 0);
    CHECK(erased != byte_pointer);
    CHECK(stand_in->get_llvm_type() == byte->get_pointer()->get_llvm_type());
    auto* qualified = record(emitter.describe(stand_in->get_const()));
    CHECK(number(field(qualified, 0)) == 2);
    CHECK(field(qualified, 6) == erased_pointer);
    CHECK((number(field(qualified, 2)) & 2) == 0);

    auto item = find_klass(compiler, "Item");
    REQUIRE(item != nullptr);
    auto* concrete = record(emitter.describe(item->get_struct_type()));
    CHECK((number(field(concrete, 2)) & 2) != 0);
    auto bases = array(field(concrete, 18));
    REQUIRE(bases.size() == item->get_bases().size());
    REQUIRE_FALSE(bases.empty());
    for (unsigned n = 0; n < bases.size(); ++n) {
        const auto& base = item->get_bases()[n];
        REQUIRE(base.base != nullptr);
        auto* metadata = record(bases[n]);
        CHECK(field(metadata, 0) == emitter.describe(base.base->get_struct_type()));
        check_projection(field(metadata, 1),
            k::model::gen::describe_rtti_receiver_projection(
                *item, *item, *base.base, context->module().getDataLayout()));
    }
    auto* concrete_signature = signature(compiler, "observeItem");
    CHECK(number(field(concrete_signature, 6)) == direct_eligibility(context->module()));
    CHECK_FALSE(field(concrete_signature, 3)->isNullValue());
    CHECK(actual_entry(concrete_signature)
        == context->lookup_llvm_function(root->get_function("observeItem")));
    for (auto& global : context->module().globals())
        if (global.getName().starts_with("__k_rtti_type")) CHECK(global.hasInitializer());
}

TEST_CASE("RTTI late erased aggregate bodies retain storage without inventing source base projections",
          "[libk][rtti][signature][generic][erased][late]") {
    const bool used = GENERATE(false, true);
    std::string source = R"SRC(
        module rtti_signature_late_erased;
        class Item { public value : int = 31; }
        generic<class T> class Box {
            private item : T*;
            public:
            Box() { item = null; }
            isNull() : bool { return item == null; }
            relay(value : T&) : T& { return value; }
            static independent(value : int) : int { return value; }
        }
    )SRC";
    if (used) source += R"SRC(
        force() : int {
            item : Item;
            box : Box<Item>;
            if (box.isNull()) return box.relay(item).value;
            return 0;
        }
    )SRC";
    auto compiler = compile_model_with_stdlib(source);
    REQUIRE(compiler != nullptr);
    auto primary = find_aggregate(compiler, "Box");
    REQUIRE(primary != nullptr);
    REQUIRE(primary->is_generic());
    auto* info = primary->get_tpl_info();
    REQUIRE(info != nullptr);
    std::shared_ptr<k::model::aggregate> owner;
    for (const auto& [key, instance] : info->instantiations) {
        auto concrete = std::get_if<std::shared_ptr<k::model::aggregate>>(&instance);
        REQUIRE(concrete != nullptr);
        REQUIRE(*concrete != nullptr);
        if (owner) CHECK(owner == *concrete); // usage bindings do not create per-type bodies
        owner = *concrete;
    }
    REQUIRE(owner != nullptr);
    CHECK(owner != primary);
    CHECK(owner->get_tpl_definition() == primary);
    CHECK(owner->is_instantiation());
    CHECK_FALSE(owner->has_tpl_args());
    REQUIRE_FALSE(primary->get_bases().empty());
    REQUIRE(owner->get_bases().size() == primary->get_bases().size());
    auto storage = owner->get_struct_type();
    REQUIRE(storage != nullptr);
    REQUIRE(storage->get_llvm_type() != nullptr);
    REQUIRE(storage->get_llvm_type()->isSized());
    bool source_only_base = false;
    for (unsigned n = 0; n < owner->get_bases().size(); ++n) {
        const auto& base = owner->get_bases()[n];
        CHECK(base.raw_name == primary->get_bases()[n].raw_name);
        if (base.base) continue;
        source_only_base = true;
        CHECK_FALSE(storage->has_member("__base_" + base.sanitised_name() + "__"));
        CHECK_FALSE(storage->has_member("__vbptr_" + base.sanitised_name() + "__"));
    }
    CHECK(source_only_base);
    auto context = compiler->get_context_for_test();
    k::model::gen::rtti_emitter emitter(context, *compiler->get_unit());
    auto* exact = record(emitter.describe(storage));
    CHECK((number(field(exact, 2)) & 2) == 0);
    CHECK((number(field(exact, 2)) & 1) != 0);
    CHECK(number(field(exact, 3))
        == context->module().getDataLayout().getTypeAllocSize(storage->get_llvm_type()).getFixedValue());
    CHECK(field(exact, 14)->isNullValue());
    CHECK(field(exact, 15)->isNullValue());
    CHECK(field(exact, 18)->isNullValue());
    REQUIRE_FALSE(owner->constructors().empty());
    std::vector<std::shared_ptr<k::model::function>> methods{
        owner->constructors().front(), owner->get_function("isNull"), owner->get_function("relay")};
    for (const auto& method : methods) {
        REQUIRE(method != nullptr);
        auto* body = context->lookup_llvm_function(method);
        REQUIRE(body != nullptr);
        CHECK_FALSE(body->isDeclaration());
        auto* sig = record(emitter.signature(method));
        CHECK(number(field(sig, 6)) == 3);
        CHECK(field(sig, 3)->isNullValue());
        CHECK(field(sig, 8)->isNullValue());
        CHECK(record(field(record(field(sig, 4)), 0)) == exact);
        const auto& roles = rtti_context::argument_sources(*context).at(method.get());
        REQUIRE(roles.size() == body->arg_size());
        REQUIRE_FALSE(roles.empty());
        CHECK(roles.front() == -2);
        CHECK(body->getArg(0)->getType() == method->get_this_parameter()->get_type()->get_llvm_type());
    }

    // The erased owner does not make an independent static signature dependent.
    auto independent = owner->get_function("independent");
    REQUIRE(independent != nullptr);
    auto* body = context->lookup_llvm_function(independent);
    REQUIRE(body != nullptr);
    CHECK_FALSE(body->isDeclaration());
    auto* sig = record(emitter.signature(independent));
    CHECK(number(field(sig, 6)) == direct_eligibility(context->module()));
    CHECK(actual_entry(sig) == body);
    CHECK(field(sig, 4)->isNullValue());
    auto* native = record(field(sig, 3));
    CHECK(signed_number(field(native, 6)) == -1);
    CHECK(signed_number(field(native, 7)) == -1);
    CHECK(signed_number(field(native, 11)) == -1);
    const auto& roles = rtti_context::argument_sources(*context).at(independent.get());
    REQUIRE(roles == std::vector<int>{0});
    auto arguments = array(field(native, 5));
    REQUIRE(arguments.size() == body->arg_size());
    REQUIRE(arguments.size() == 1);
    CHECK(signed_number(field(record(arguments[0]), 5)) == 0);
    CHECK(field(record(arguments[0]), 4) == array(field(sig, 1))[0]);
    CHECK(body->getArg(0)->getType()->isIntegerTy(32));
    CHECK(text(field(sig, 7)) == body->getName().str());
    CHECK_NOTHROW(emitter.finalize_virtual_bodies());
}

TEST_CASE("RTTI lexical recipes and generic facades never publish a concrete receiver or trusted entry",
          "[libk][rtti][signature][generic][recipe][negative]") {
    auto compiler = compile_model_with_stdlib(R"SRC(
        module rtti_signature_generic_recipes;
        class Item { public value : int = 17; }
        template<typename T> struct Recipe {
            class Nested {
                public scalar(value : int) : int { return value; }
                public static independent(value : int) : int { return value; }
            }
        }
        generic<class T> relay(value : T&) : T& { return value; }
        force() : int {
            item : Item;
            return relay<Item>(item).value;
        }
    )SRC");
    REQUIRE(compiler != nullptr);
    auto context = compiler->get_context_for_test();
    k::model::gen::rtti_emitter emitter(context, *compiler->get_unit());
    auto primary = find_aggregate(compiler, "Recipe");
    REQUIRE(primary != nullptr);
    CHECK(primary->is_template());
    auto nested = primary->get_aggregate("Nested");
    REQUIRE(nested != nullptr);
    CHECK_FALSE(nested->is_template());
    auto nested_type = nested->get_struct_type();
    REQUIRE(nested_type != nullptr);
    auto* exact = record(emitter.describe(nested_type));
    CHECK((number(field(exact, 2)) & 3) == 0);
    CHECK(field(exact, 10)->isNullValue());
    CHECK(field(exact, 14)->isNullValue());
    CHECK(field(exact, 15)->isNullValue());
    CHECK(field(exact, 18)->isNullValue());
    for (const char* name : {"scalar", "independent"}) {
        auto method = nested->get_function(name);
        REQUIRE(method != nullptr);
        auto* sig = record(emitter.signature(method));
        CHECK(number(field(sig, 6)) == 3);
        CHECK(field(sig, 3)->isNullValue());
        CHECK(field(sig, 8)->isNullValue());
    }
    auto item = find_klass(compiler, "Item");
    REQUIRE(item != nullptr);
    bool facade_found = false;
    for (const auto& facade : compiler->get_unit()->get_root_namespace()->functions()) {
        auto target = facade->get_erased_target();
        if (!target) continue;
        facade_found = true;
        auto* body = context->lookup_llvm_function(target);
        REQUIRE(body != nullptr);
        CHECK_FALSE(body->isDeclaration());
        auto* sig = record(emitter.signature(facade));
        CHECK(number(field(sig, 6)) == 3);
        CHECK(field(sig, 3)->isNullValue());
        CHECK(field(sig, 8)->isNullValue());
        CHECK(text(field(sig, 7)) == body->getName().str());
        auto params = array(field(sig, 1));
        REQUIRE(params.size() == 1);
        CHECK(field(sig, 0) == params[0]);
        CHECK(referent(params[0]) == record(emitter.describe(item->get_struct_type())));
        auto* erased = record(emitter.signature(target));
        CHECK(number(field(erased, 6)) == 3);
        CHECK(field(erased, 8)->isNullValue());
        CHECK(number(field(record(field(record(field(erased, 0)), 6)), 0)) == 21);
        const auto& roles = rtti_context::argument_sources(*context).at(target.get());
        CHECK(roles == std::vector<int>{0});
        CHECK(body->arg_size() == roles.size());
    }
    CHECK(facade_found);
    CHECK_NOTHROW(emitter.finalize_virtual_bodies());

    // Concrete receiver corruption must still diagnose, rather than become a recipe.
    REQUIRE_FALSE(item->get_bases().empty());
    auto& base = item->get_bases_mutable().front();
    auto original_base = base.base;
    REQUIRE(original_base != nullptr);
    const auto expected = "Unresolved RTTI receiver base [aggregate=" + item->get_fq_name()
        + ", base=" + base.raw_name + "]";
    base.base.reset();
    auto uncached = std::make_shared<k::model::struct_type>(item->get_fq_name(), item);
    CHECK_THROWS_WITH(emitter.describe(uncached), expected);
    base.base = original_base;

    auto storage = item->get_struct_type();
    auto* native = llvm::dyn_cast<llvm::StructType>(storage->get_llvm_type());
    REQUIRE(native != nullptr);
    std::vector<k::model::struct_type::field> fields(storage->fields_begin(), storage->fields_end());
    auto* opaque = llvm::StructType::create(context->llvm_context(), "__rtti_missing_receiver_layout");
    context->attach_llvm_struct_type(storage, opaque, fields);
    auto unavailable = std::make_shared<k::model::struct_type>(item->get_fq_name(), item);
    CHECK_THROWS_WITH(emitter.describe(unavailable),
        "Missing finalized RTTI receiver layout [aggregate=" + item->get_fq_name() + "]");
    context->attach_llvm_struct_type(storage, native, std::move(fields));
}
