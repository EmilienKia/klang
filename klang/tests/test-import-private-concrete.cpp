#include <catch2/catch_all.hpp>

#include "helpers.hpp"

#include <llvm/IR/DataLayout.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/DynamicLibrary.h>

#include <algorithm>
#include <functional>

TEST_CASE("import public aggregate retains private concrete Shared layout without exposing its API",
          "[import][private-dependency][shared][regression]") {
    namespace fs = std::filesystem;
    struct Artifacts {
        fs::path path;
        ~Artifacts() {
            std::error_code error;
            fs::remove_all(path, error);
        }
    } artifacts;
    static std::atomic<unsigned> sequence{0};
    while (artifacts.path.empty()) {
        auto candidate = fs::current_path() /
            (".klang-private-concrete-" + std::to_string(++sequence));
        if (!fs::exists(candidate) && fs::create_directory(candidate))
            artifacts.path = std::move(candidate);
    }

    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    resolver->add_search_dir(artifacts.path.string());
    auto producer = k::compiler::create(make_pic_target_machine());
    producer->set_file_resolver(resolver);
    producer->parse_source("private-concrete-producer.k", R"K(
        module privateconcrete;
        private class Hidden { value: long = 9; }
        private struct Leaf { stamp: long; marker: int; }
        private struct Payload {
            resource: Shared<Hidden>;
            leaf: Leaf;
        }
        public class Visible {}
        public struct Carrier {
        private:
            payload: Payload;
        public:
            Carrier() {}
            const value(): int { return 42; }
        }
        public empty(): Shared<Visible> { return Shared<Visible>(); }
        public emptyWeak(): Weak<Visible> { return Weak<Visible>(); }
    )K", false, false);
    REQUIRE_FALSE(producer->has_compilation_error());
    auto library = artifacts.path / "libprivateconcrete.so";
    REQUIRE(producer->gen_shared_library(library.string()));
    auto kdi_path = library;
    kdi_path.replace_extension(".kdi");
    resolver->add_explicit_path("privateconcrete", kdi_path);
    auto file = kdi::kdi_read_cbor_file(kdi_path.string());
    const kdi::kdi_aggregate* carrier = nullptr;
    bool public_shared = false;
    std::function<void(const kdi::kdi_namespace&)> check_namespace;
    check_namespace = [&](const kdi::kdi_namespace& ns) {
        for (const auto& aggregate : ns.aggregates) {
            CHECK(aggregate.name != "Hidden");
            CHECK(aggregate.name != "Leaf");
            CHECK(aggregate.name != "Payload");
            CHECK(aggregate.name.find("Hidden") == std::string::npos);
            if (aggregate.name == "Carrier") carrier = &aggregate;
            if (aggregate.name.starts_with("Shared__")
                && aggregate.name.find("Visible") != std::string::npos) {
                public_shared = true;
                CHECK_FALSE(aggregate.constructors.empty());
            }
        }
        for (const auto& child : ns.namespaces) check_namespace(child);
    };
    check_namespace(file.unit.root_ns);
    REQUIRE(carrier);
    CHECK(public_shared);
    CHECK(carrier->llvm_def.find("Shared__") != std::string::npos);
    CHECK(carrier->llvm_def.find("Payload") != std::string::npos);
    CHECK(carrier->llvm_def.find("Leaf") != std::string::npos);
    CHECK(std::none_of(carrier->layout.begin(), carrier->layout.end(),
        [](const auto& field) {
            return std::holds_alternative<kdi::kdi_layout_member>(field);
        }));
    const auto producer_carrier =
        producer->get_unit()->get_root_namespace()->get_aggregate("Carrier");
    REQUIRE(producer_carrier);
    const auto producer_size = producer->get_context_for_test()->module().getDataLayout()
        .getTypeAllocSize(producer_carrier->get_struct_type()->get_llvm_type());

    auto consumer = k::compiler::create();
    consumer->set_file_resolver(resolver);
    consumer->parse_source("private-concrete-consumer.k", R"K(
        module privateconcreteconsumer;
        import privateconcrete;
        run(): int {
            carrier: privateconcrete::Carrier;
            shared: Shared<privateconcrete::Visible> = privateconcrete::empty();
            copied: Shared<privateconcrete::Visible>(shared);
            weak: Weak<privateconcrete::Visible> = privateconcrete::emptyWeak();
            return copied.get() == null ? carrier.value() : -1;
        }
    )K", false, false);
    REQUIRE_FALSE(consumer->has_compilation_error());
    REQUIRE(consumer->verify_gen_code());
    auto imported_namespace =
        consumer->get_unit()->get_root_namespace()->get_child_namespace("privateconcrete");
    REQUIRE(imported_namespace);
    CHECK_FALSE(imported_namespace->get_aggregate("Hidden"));
    CHECK_FALSE(imported_namespace->get_aggregate("Leaf"));
    CHECK_FALSE(imported_namespace->get_aggregate("Payload"));
    auto imported_carrier = imported_namespace->get_aggregate("Carrier");
    REQUIRE(imported_carrier);
    CHECK(consumer->get_context_for_test()->module().getDataLayout()
        .getTypeAllocSize(imported_carrier->get_struct_type()->get_llvm_type()) == producer_size);
    REQUIRE(ensure_libk_loaded());
    std::string load_error;
    INFO(load_error);
    REQUIRE_FALSE(llvm::sys::DynamicLibrary::LoadLibraryPermanently(
        library.c_str(), &load_error));
    auto jit = consumer->to_jit();
    REQUIRE(jit);
    auto run = jit->lookup_symbol<int (*)()>("run");
    REQUIRE(run);
    CHECK(run() == 42);

    auto facade = k::compiler::create(make_pic_target_machine());
    facade->set_file_resolver(resolver);
    facade->parse_source("private-concrete-facade.k", R"K(
        module privateconcretefacade;
        import privateconcrete;
        public struct Carrier {
        private:
            original: privateconcrete::Carrier;
        public:
            Carrier() {}
            const value(): int { return original.value(); }
        }
    )K", false, false);
    REQUIRE_FALSE(facade->has_compilation_error());
    auto facade_library = artifacts.path / "libprivateconcretefacade.so";
    REQUIRE(facade->gen_shared_library(facade_library.string()));
    auto facade_kdi = facade_library;
    facade_kdi.replace_extension(".kdi");
    resolver->add_explicit_path("privateconcretefacade", facade_kdi);
    auto downstream = k::compiler::create();
    downstream->set_file_resolver(resolver);
    downstream->parse_source("private-concrete-downstream.k", R"K(
        module privateconcretedownstream;
        import privateconcretefacade;
        run(): int {
            carrier: privateconcretefacade::Carrier;
            return carrier.value();
        }
    )K", false, false);
    REQUIRE_FALSE(downstream->has_compilation_error());
    REQUIRE(downstream->verify_gen_code());
    REQUIRE_FALSE(llvm::sys::DynamicLibrary::LoadLibraryPermanently(
        facade_library.c_str(), &load_error));
    auto downstream_jit = downstream->to_jit();
    REQUIRE(downstream_jit);
    auto downstream_run = downstream_jit->lookup_symbol<int (*)()>("run");
    REQUIRE(downstream_run);
    CHECK(downstream_run() == 42);

    for (const auto* forbidden : {
            "hidden: privateconcrete::Hidden* = null;",
            "shared: Shared<privateconcrete::Hidden>;",
            "carrier: privateconcrete::Carrier; carrier.payload;"}) {
        INFO(forbidden);
        auto denied = k::compiler::create();
        denied->set_file_resolver(resolver);
        try {
            denied->parse_source("private-concrete-denied.k",
                std::string("module privateconcretedenied; import privateconcrete; "
                            "run(): int { ") + forbidden + " return 0; }", false, false);
        } catch (const k::log::compiler_error&) {
        }
        CHECK(denied->has_compilation_error());
    }
}
