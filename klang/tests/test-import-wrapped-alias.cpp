#include <catch2/catch_all.hpp>

#include "helpers.hpp"

#include <string_view>

namespace {

struct WrappedAliasArtifacts {
    std::filesystem::path path;

    WrappedAliasArtifacts() {
        static std::atomic<unsigned> sequence{0};
        while (path.empty()) {
            auto candidate = std::filesystem::current_path() /
                (".klang-wrapped-alias-" + std::to_string(++sequence));
            if (!std::filesystem::exists(candidate)
                && std::filesystem::create_directory(candidate)) {
                path = std::move(candidate);
            }
        }
    }

    ~WrappedAliasArtifacts() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

constexpr auto wrapped_alias_source = R"K(
    module imported_wrapped_aliases;
    namespace c {
        public alias Int : int;
        public alias Frozen : const int;
        public alias Values : Int[];
    }
)K";

} // namespace

TEST_CASE("import using namespace aliases resolve inside owned unsized arrays",
          "[import][alias][array][owner][regression]") {
    const bool optimize = GENERATE(false, true);
    WrappedAliasArtifacts artifacts;
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    resolver->add_search_dir(artifacts.path.string());

    auto producer = k::compiler::create();
    producer->set_file_resolver(resolver);
    producer->parse_source("wrapped-alias-producer.k", wrapped_alias_source, false, false);
    REQUIRE_FALSE(producer->has_compilation_error());
    REQUIRE(producer->gen_kdi((artifacts.path / "imported_wrapped_aliases.so").string()));

    auto consumer = k::compiler::create();
    consumer->set_file_resolver(resolver);
    consumer->parse_source("wrapped-alias-consumer.k", R"K(
        module wrapped_alias_consumer;
        import imported_wrapped_aliases;
        using namespace imported_wrapped_aliases::c;

        namespace local {
            alias Int : long;
            using namespace imported_wrapped_aliases::c;
            probe() : long {
                values : Int[]! = new Int[1u]{42};
                return values[0];
            }
        }

        run() : int {
            empty : Int[]! = new Int[0u];
            nonempty : Int[]! = new Int[2u]{20, 22};
            nested : Values! = new Int[0u];
            frozen : Frozen[]! = new Frozen[2u]{20, 22};
            emptyStrings : String[]! = new String[0u];
            constValues : const Int[2] {7, 9};
            frozenSized : Frozen[2] {20, 22};
            if (empty->size != 0u || nested->size != 0u
                || emptyStrings->size != 0u
                || nonempty->size != 2u || frozen->size != 2u) return -1;
            if (local::probe() != 42) return -2;
            if (constValues[0] + constValues[1] != 16
                || frozenSized[0] + frozenSized[1] != 42) return -3;
            return nonempty[0] + frozen[1];
        }
    )K", optimize, false);
    REQUIRE_FALSE(consumer->has_compilation_error());
    REQUIRE(consumer->verify_gen_code());
    auto root = consumer->get_unit()->get_root_namespace();
    auto run_function = root->get_functions("run").front();
    auto frozen = run_function->get_block()->get_variable("frozen");
    REQUIRE(frozen);
    auto frozen_array = frozen->get_type()->get_subtype();
    REQUIRE(frozen_array);
    CHECK(k::model::type::is_const(
        k::model::type::canonical(frozen_array->get_subtype())));
    auto local_namespace = root->get_child_namespace("local");
    REQUIRE(local_namespace);
    auto probe = local_namespace->get_functions("probe").front();
    auto shadowed = probe->get_block()->get_variable("values");
    REQUIRE(shadowed);
    auto shadowed_array = shadowed->get_type()->get_subtype();
    REQUIRE(shadowed_array);
    auto element = std::dynamic_pointer_cast<k::model::primitive_type>(
        k::model::type::canonical(shadowed_array->get_subtype()));
    REQUIRE(element);
    CHECK(element->get_type() == k::model::primitive_type::LONG);
    REQUIRE(ensure_libk_loaded());
    auto jit = consumer->to_jit();
    REQUIRE(jit);
    auto run = jit->lookup_symbol<int (*)()>("run");
    REQUIRE(run);
    CHECK(run() == 42);
}

TEST_CASE("import wrapped alias resolution preserves const and unresolved names",
          "[import][alias][array][owner][const][regression]") {
    WrappedAliasArtifacts artifacts;
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    resolver->add_search_dir(artifacts.path.string());
    auto producer = k::compiler::create();
    producer->set_file_resolver(resolver);
    producer->parse_source("wrapped-alias-producer.k", wrapped_alias_source, false, false);
    REQUIRE_FALSE(producer->has_compilation_error());
    REQUIRE(producer->gen_kdi((artifacts.path / "imported_wrapped_aliases.so").string()));

    std::string_view source;
    SECTION("const alias elements cannot be mutated") {
        source = R"K(
            module wrapped_alias_const;
            import imported_wrapped_aliases;
            using namespace imported_wrapped_aliases::c;
            run() {
                values : Frozen[]! = new Frozen[1u]{42};
                values[0] = 1;
            }
        )K";
    }
    SECTION("a missing alias still diagnoses the owner type") {
        source = R"K(
            module wrapped_alias_unresolved;
            import imported_wrapped_aliases;
            using namespace imported_wrapped_aliases::c;
            run() {
                values : Missing[]! = new Int[0u];
            }
        )K";
    }
    SECTION("const sized alias arrays cannot be mutated") {
        source = R"K(
            module wrapped_alias_sized_const;
            import imported_wrapped_aliases;
            using namespace imported_wrapped_aliases::c;
            run() {
                values : const Int[2] {7, 9};
                values[0] = 1;
            }
        )K";
    }
    REQUIRE_FALSE(source.empty());
    auto denied = k::compiler::create();
    denied->set_file_resolver(resolver);
    REQUIRE_THROWS_AS(
        denied->parse_source("wrapped-alias-denied.k", source, false, false),
        k::log::compiler_error);
}
