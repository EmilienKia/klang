#include <catch2/catch_all.hpp>

#include "helpers.hpp"
#include "../src/errors_gen.hpp"
#include "../src/gen/resolvers_scope_lookup.hpp"

#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/DynamicLibrary.h>
#include <functional>
#include <sstream>
#include <string_view>
#include <type_traits>

namespace {

struct ImportedConstructorLibrary {
    std::filesystem::path path;
    std::shared_ptr<k::path_lookup_file_resolver> resolver;
    std::shared_ptr<k::compiler> producer;

    ImportedConstructorLibrary() {
        static std::atomic<unsigned> sequence{0};
        while (path.empty()) {
            auto candidate = std::filesystem::current_path() /
                (".klang-imported-constructor-" + std::to_string(++sequence));
            if (!std::filesystem::exists(candidate)
                && std::filesystem::create_directory(candidate)) path = std::move(candidate);
        }
        resolver = std::make_shared<k::path_lookup_file_resolver>();
        resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
        resolver->add_search_dir(path.string());
    }

    void build() {
        producer = k::compiler::create(make_pic_target_machine());
        producer->set_file_resolver(resolver);
        producer->parse_source("imported-constructor-producer.k", R"K(
            module imported_shared_recipe;
            public class Base : public Object {
                public Base() : Object() {}
                public const value() : int { return 42; }
            }
            public class Derived : public Base {
                public Derived() : Base() {}
            }
            public class Sibling : public Base {
                public Sibling() : Base() {}
            }
            public class Other {}
            public objectHandle() : Shared<Object> { return Shared<Object>(); }
            public baseHandle() : Shared<Base> { return Shared<Base>(); }
            public weakObjectHandle() : Weak<Object> { return Weak<Object>(); }
            public convertedBase(value : const Shared<Derived>&) : Shared<Base> {
                return Shared<Base>(value);
            }
            template<typename T>
            public struct Box {
            private:
                value : T;
                normalize(input : T) : T { return input + 1; }
            public:
                stamp : int;
                Box() : value(0), stamp(7) {}
                template<typename U>
                Box(input : U) : value((T) input), stamp(7) {
                    value = normalize(value);
                }
                const get() : T { return value; }
            }
            public boxedHandle() : Box<int> { return Box<int>(); }
            template<typename T>
            public struct Peer {
            private:
                value : T;
            public:
                Peer(input : T) : value(input) {}
                const get() : T { return value; }
            }
            template<typename T>
            public struct UsesPeer {
            private:
                peer : Peer<T>;
            public:
                UsesPeer(input : T) : peer(input) {}
                const get() : T { return peer.get(); }
            }
            public peerHandle() : UsesPeer<int> { return UsesPeer<int>(42); }
        )K", false, false);
        REQUIRE_FALSE(producer->has_compilation_error());
        REQUIRE(producer->gen_shared_library(library().string()));
        auto kdi = library();
        kdi.replace_extension(".kdi");
        resolver->add_explicit_path("imported_shared_recipe", kdi);
    }

    ~ImportedConstructorLibrary() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    std::filesystem::path library() const { return path / "libimported_shared_recipe.so"; }

    void load() const {
        REQUIRE(ensure_libk_loaded());
        std::string error;
        auto failed = llvm::sys::DynamicLibrary::LoadLibraryPermanently(library().c_str(), &error);
        INFO(error);
        REQUIRE_FALSE(failed);
    }
};

} // namespace

TEST_CASE("imported Shared specializations retain converting member constructors and ABI",
          "[import][template][member-constructor][shared][regression]") {
    const bool optimize = GENERATE(false, true);
    ImportedConstructorLibrary fixture;
    fixture.build();
    auto consumer = k::compiler::create();
    consumer->set_file_resolver(fixture.resolver);
    consumer->parse_source("imported-constructor-consumer.k", R"K(
        module imported_shared_consumer;
        import imported_shared_recipe;
        using namespace imported_shared_recipe;

        class LocalPayload : public Object {
        public:
            LocalPayload() {}
            const value() : int { return 42; }
        }

        fromConst(baseSource : const Shared<Base>&) : int {
            first : Shared<Object>(baseSource);
            second : Shared<Object>(baseSource);
            return first.get() != null && second.get() != null
                && first.useCount() == 4 ? 42 : -1;
        }

        run() : int {
            source : Shared<Derived>(new Derived());
            base : Shared<Base>(source);
            if (base.get() == null || source.useCount() != 2) return -2;
            {
                fromBase : Shared<Object>(base);
                fromDerived : Shared<Object>(source);
                repeated : Shared<Object>(base);
                if (fromBase.get() == null || fromDerived.get() == null
                    || repeated.get() == null || source.useCount() != 5) return -3;
            }
            if (source.useCount() != 2 || base.get()->value() != 42) return -4;
            return fromConst(base);
        }

        emptyAndFailed() : int {
            empty : Shared<Derived>;
            emptyBase : Shared<Base>(empty);
            emptyObject : Shared<Object>(emptyBase);
            if (emptyBase.get() != null || emptyObject.get() != null
                || emptyObject.useCount() != 0) return -5;
            source : Shared<Derived>(new Derived());
            base : Shared<Base>(source);
            failed : Shared<Sibling>(base);
            return failed.get() == null && failed.useCount() == 0
                && source.useCount() == 2 ? 42 : -6;
        }

        weakConversions() : int {
            source : Shared<Derived>(new Derived());
            base : Weak<Base>(source);
            object : Weak<Object>;
            object = base;
            repeated : Weak<Object>(object);
            view : Weak<Base>;
            view = repeated;
            constSource : const Weak<Object>& = object;
            fromConst : Weak<Base>;
            fromConst = constSource;
            locked : Shared<Base> = fromConst.lock();
            return !base.expired() && !repeated.expired() && !view.expired()
                && locked.get() != null && locked.get()->value() == 42
                && source.useCount() == 2 ? 42 : -7;
        }

        localPayload() : int {
            source : Shared<LocalPayload>(new LocalPayload());
            erased : Shared<Object>(source);
            observer : Weak<LocalPayload>(source);
            observed : Shared<LocalPayload> = observer.lock();
            return erased.get() != null && observed.get() != null
                && observed.get()->value() == 42
                && source.useCount() == 3 ? 42 : -8;
        }
    )K", optimize, false);
    REQUIRE_FALSE(consumer->has_compilation_error());
    REQUIRE(consumer->verify_gen_code());

    auto run_function = consumer->get_unit()->get_root_namespace()->get_functions("run").front();
    auto base = run_function->get_block()->get_variable("base");
    REQUIRE(base);
    auto type = std::dynamic_pointer_cast<k::model::struct_type>(
        k::model::type::canonical(base->get_type()));
    REQUIRE(type);
    auto host = std::dynamic_pointer_cast<k::model::imported_aggregate>(type->get_struct());
    REQUIRE(host);
    REQUIRE(host->get_kdi_aggregate());
    REQUIRE(host->get_tpl_definition());
    auto block = std::dynamic_pointer_cast<k::model::member_variable_definition>(
        host->get_variable("_block"));
    REQUIRE(block);
    CHECK(block->get_visibility() == k::model::PRIVATE);
    auto producer_type = fixture.producer->get_unit()->get_root_namespace()
        ->get_functions("baseHandle").front()->get_return_type();
    auto producer_struct = std::dynamic_pointer_cast<k::model::struct_type>(producer_type);
    REQUIRE(producer_struct);
    // KDI aggregate mangled_name is the LLVM layout name, not the language symbol.
    CHECK(host->get_mangled_name() == producer_struct->name());
    CHECK(host->get_kdi_aggregate()->mangled_name == producer_struct->name());
    const auto& consumer_layout = consumer->get_context_for_test()->module().getDataLayout();
    const auto& producer_layout = fixture.producer->get_context_for_test()->module().getDataLayout();
    auto consumer_llvm_type = llvm::dyn_cast<llvm::StructType>(type->get_llvm_type());
    auto producer_llvm_type = llvm::dyn_cast<llvm::StructType>(producer_struct->get_llvm_type());
    REQUIRE(consumer_llvm_type);
    REQUIRE(producer_llvm_type);
    REQUIRE_FALSE(consumer_llvm_type->isOpaque());
    REQUIRE_FALSE(producer_llvm_type->isOpaque());
    CHECK(consumer_llvm_type->isPacked() == producer_llvm_type->isPacked());
    REQUIRE(consumer_llvm_type->getNumElements() == producer_llvm_type->getNumElements());
    CHECK(consumer_layout.getTypeAllocSize(consumer_llvm_type)
        == producer_layout.getTypeAllocSize(producer_llvm_type));
    CHECK(consumer_layout.getABITypeAlign(consumer_llvm_type)
        == producer_layout.getABITypeAlign(producer_llvm_type));
    auto consumer_offsets = consumer_layout.getStructLayout(consumer_llvm_type);
    auto producer_offsets = producer_layout.getStructLayout(producer_llvm_type);
    for (unsigned index = 0; index < consumer_llvm_type->getNumElements(); ++index) {
        CHECK(consumer_offsets->getElementOffset(index) == producer_offsets->getElementOffset(index));
        auto consumer_field = llvm::dyn_cast<llvm::PointerType>(consumer_llvm_type->getElementType(index));
        auto producer_field = llvm::dyn_cast<llvm::PointerType>(producer_llvm_type->getElementType(index));
        REQUIRE(consumer_field);
        REQUIRE(producer_field);
        CHECK(consumer_field->getAddressSpace() == producer_field->getAddressSpace());
    }
    unsigned exported_fields = 0;
    for (const auto& entry : host->get_kdi_aggregate()->layout) {
        std::visit([&](const auto& field) {
            REQUIRE(field.llvm_field_index == exported_fields);
            if constexpr (std::is_same_v<std::decay_t<decltype(field)>, kdi::kdi_layout_opaque_block>) {
                REQUIRE(field.field_count > 0);
                REQUIRE(exported_fields + field.field_count <= consumer_llvm_type->getNumElements());
                const auto last = exported_fields + field.field_count - 1;
                const auto span = consumer_offsets->getElementOffset(last)
                    + consumer_layout.getTypeAllocSize(consumer_llvm_type->getElementType(last)).getFixedValue()
                    - consumer_offsets->getElementOffset(exported_fields);
                CHECK(span * 8 == field.size_bits);
                exported_fields += field.field_count;
            } else {
                REQUIRE(exported_fields < consumer_llvm_type->getNumElements());
                ++exported_fields;
            }
        }, entry);
    }
    CHECK(exported_fields == consumer_llvm_type->getNumElements());
    auto init = std::dynamic_pointer_cast<k::model::constructor_invocation_expression>(
        base->get_init_expr());
    REQUIRE(init);
    auto imported_ctor = std::dynamic_pointer_cast<k::model::imported_constructor>(
        init->get_constructor());
    REQUIRE(imported_ctor);
    REQUIRE(imported_ctor->get_kdi_constructor());
    auto conversion = fixture.producer->get_unit()->get_root_namespace()
        ->get_functions("convertedBase").front();
    REQUIRE(imported_ctor->get_parameter_size() == 1);
    REQUIRE(conversion->get_parameter_size() == 1);
    auto source = run_function->get_block()->get_variable("source");
    REQUIRE(source);
    auto consumer_parameter = k::model::type::canonical(
        std::const_pointer_cast<k::model::type>(imported_ctor->get_parameter(0)->get_type()));
    INFO("consumer constructor parameter: " << consumer_parameter->to_string());
    INFO("consumer source type: " << source->get_type()->to_string());
    INFO("producer convertedBase parameter: " << conversion->get_parameter(0)->get_type()->to_string());
    REQUIRE(k::model::gen::scope_lookup::same_signature_type(
        consumer_parameter, source->get_type()->get_const()->get_reference()));
    REQUIRE(init->arguments().size() == 1);
    std::shared_ptr<k::model::constructor> producer_ctor;
    size_t matching_constructors = 0;
    std::ostringstream candidates;
    for (const auto& candidate : producer_struct->get_struct()->constructors()) {
        candidates << candidate->get_mangled_name() << "(";
        for (const auto& parameter : candidate->parameters()) {
            candidates << parameter->get_type()->to_string() << ";";
        }
        candidates << ")\n";
        if (candidate->get_parameter_size() == 1
            && k::model::gen::scope_lookup::same_signature_type(
                std::const_pointer_cast<k::model::type>(candidate->get_parameter(0)->get_type()),
                std::const_pointer_cast<k::model::type>(conversion->get_parameter(0)->get_type()))) {
            producer_ctor = candidate;
            ++matching_constructors;
        }
    }
    INFO("producer constructor candidates:\n" << candidates.str());
    REQUIRE(matching_constructors == 1);
    REQUIRE(producer_ctor);
    size_t exported_constructors = 0;
    const kdi::kdi_constructor* exported_ctor = nullptr;
    for (const auto& constructor : host->get_kdi_aggregate()->constructors) {
        if (constructor.mangled_name == producer_ctor->get_mangled_name()) {
            exported_ctor = &constructor;
            ++exported_constructors;
        }
    }
    REQUIRE(exported_constructors == 1);
    REQUIRE(exported_ctor);
    CHECK(imported_ctor->get_mangled_name() == producer_ctor->get_mangled_name());
    CHECK(imported_ctor->get_kdi_constructor()->mangled_name == producer_ctor->get_mangled_name());
    CHECK(imported_ctor->get_kdi_constructor()->llvm_def == exported_ctor->llvm_def);
    CHECK(imported_ctor->get_c2_mangled_name() == exported_ctor->mangled_name_c2);
    auto producer_function = fixture.producer->get_context_for_test()->module().getFunction(
        producer_ctor->get_mangled_name());
    auto consumer_function = consumer->get_context_for_test()->module().getFunction(
        producer_ctor->get_mangled_name());
    REQUIRE(producer_function);
    REQUIRE(consumer_function);
    CHECK(consumer_function->getCallingConv() == producer_function->getCallingConv());
    auto producer_signature = producer_function->getFunctionType();
    auto consumer_signature = consumer_function->getFunctionType();
    CHECK(consumer_signature->isVarArg() == producer_signature->isVarArg());
    REQUIRE(producer_signature->getReturnType()->isVoidTy());
    REQUIRE(consumer_signature->getReturnType()->isVoidTy());
    REQUIRE(producer_signature->getNumParams() == 2);
    REQUIRE(consumer_signature->getNumParams() == producer_signature->getNumParams());
    for (unsigned index = 0; index < producer_signature->getNumParams(); ++index) {
        auto producer_parameter = llvm::dyn_cast<llvm::PointerType>(producer_signature->getParamType(index));
        auto consumer_parameter = llvm::dyn_cast<llvm::PointerType>(consumer_signature->getParamType(index));
        REQUIRE(producer_parameter);
        REQUIRE(consumer_parameter);
        CHECK(consumer_parameter->getAddressSpace() == producer_parameter->getAddressSpace());
    }

    fixture.load();
    auto jit = consumer->to_jit();
    REQUIRE(jit);
    for (const auto* name : {"run", "emptyAndFailed", "weakConversions", "localPayload"}) {
        INFO(name);
        auto invoke = jit->lookup_symbol<int (*)()>(name);
        REQUIRE(invoke);
        CHECK(invoke() == 42);
        CHECK(invoke() == 42);
    }
}

TEST_CASE("imported Shared recipe hydration keeps private access and const restrictions",
          "[import][template][member-constructor][shared][const][regression]") {
    ImportedConstructorLibrary fixture;
    fixture.build();
    std::string_view source;
    SECTION("hydrated private fields are not a public API") {
        source = R"K(
            module imported_shared_private;
            import imported_shared_recipe;
            inspect(value : const Shared<imported_shared_recipe::Base>&) : int {
                return value._block == null ? 0 : 1;
            }
        )K";
    }
    SECTION("converting constructors cannot discard pointee const") {
        source = R"K(
            module imported_shared_const;
            import imported_shared_recipe;
            run() {
                source : Shared<imported_shared_recipe::Base>(
                    new imported_shared_recipe::Base());
                frozen : Shared<const imported_shared_recipe::Base>(source);
                stripped : Shared<imported_shared_recipe::Base>(frozen);
            }
        )K";
    }
    SECTION("converting constructors reject unrelated pointee types") {
        source = R"K(
            module imported_shared_unrelated;
            import imported_shared_recipe;
            using namespace imported_shared_recipe;
            run() {
                source : Shared<Derived>(new Derived());
                base : Shared<Base>(source);
                denied : Shared<Other>(base);
            }
        )K";
    }
    REQUIRE_FALSE(source.empty());
    auto denied = k::compiler::create();
    denied->set_file_resolver(fixture.resolver);
    REQUIRE_THROWS_AS(
        denied->parse_source("imported-constructor-denied.k", source, false, false),
        k::log::compiler_error);
}

TEST_CASE("imported template member constructors support new and private helpers",
          "[import][template][member-constructor][new][regression]") {
    const bool optimize = GENERATE(false, true);
    ImportedConstructorLibrary fixture;
    fixture.build();
    auto consumer = k::compiler::create();
    consumer->set_file_resolver(fixture.resolver);
    consumer->parse_source("imported-constructor-new.k", R"K(
        module imported_constructor_new;
        import imported_shared_recipe;
        using namespace imported_shared_recipe;
        run() : int {
            input : short = 41;
            owned : Box<int>! = new Box<int>(input);
            repeated : Box<int>(input);
            other : Box<int>((long) 41);
            peer : UsesPeer<short>(input);
            return owned->get() == 42 && owned->stamp == 7
                && repeated.get() == 42 && repeated.stamp == 7
                && other.get() == 42 && other.stamp == 7
                && peer.get() == 41 ? 42 : -1;
        }
    )K", optimize, false);
    REQUIRE_FALSE(consumer->has_compilation_error());
    REQUIRE(consumer->verify_gen_code());
    fixture.load();
    auto jit = consumer->to_jit();
    REQUIRE(jit);
    auto run = jit->lookup_symbol<int (*)()>("run");
    REQUIRE(run);
    CHECK(run() == 42);
}

TEST_CASE("imported member recipes reject a conflicting concrete layout",
          "[import][template][member-constructor][layout][regression]") {
    ImportedConstructorLibrary fixture;
    fixture.build();
    auto path = fixture.library();
    path.replace_extension(".kdi");
    auto metadata = kdi::kdi_read_cbor_file(path.string());
    std::function<bool(kdi::kdi_namespace&)> corrupt_recipe =
        [&](kdi::kdi_namespace& scope) {
            for (auto& definition : scope.template_defs) {
                if (definition.name != "Box" || definition.source.empty()) continue;
                auto field = definition.source.find("value");
                REQUIRE(field != std::string::npos);
                auto colon = definition.source.find(':', field);
                auto end = definition.source.find(';', colon);
                REQUIRE(colon != std::string::npos);
                REQUIRE(end != std::string::npos);
                definition.source.replace(colon + 1, end - colon - 1, " long");
                return true;
            }
            for (auto& child : scope.namespaces) {
                if (corrupt_recipe(child)) return true;
            }
            return false;
        };
    REQUIRE(corrupt_recipe(metadata.unit.root_ns));
    REQUIRE(kdi::kdi_write_cbor_file(metadata, path.string()));

    const auto source = R"K(
        module imported_constructor_layout;
        import imported_shared_recipe;
        run() {
            value : imported_shared_recipe::Box<int>;
        }
    )K";
    auto denied = k::compiler::create();
    denied->set_file_resolver(fixture.resolver);
    REQUIRE_THROWS_MATCHES(
        denied->parse_source("imported-constructor-layout.k", source, false, false),
        k::log::compiler_error,
        Catch::Matchers::Predicate<k::log::compiler_error>(
            [](const k::log::compiler_error& error) {
                return error.get_diagnostic().code == static_cast<unsigned int>(
                    k::diag::codegen_diag::ERR_KDI_TYPE_LAYOUT_CONFLICT);
            },
            "the imported recipe conflicts with its authoritative ABI"));
}
