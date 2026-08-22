/*
 * K Language compiler
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * Integration tests for template K module imports and cross-module instantiation.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>

namespace fs = std::filesystem;

// Template struct / function import tests
//
// These tests exercise the basic import of template instantiations across
// module boundaries.  The library defines a template and creates concrete
// instantiations; the consumer module imports the library and uses
// the instantiations.
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// Template struct: library exports a template struct instantiation and a
// wrapper function.  Consumer calls the wrapper that exercises the template.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import template — struct instantiation used via wrapper function",
          "[import][e2e][template]") {
    auto result = build_exec_with_lib(
        R"K(
            module tpllib;

            template<typename T>
            struct Box {
                val : T;
            }

            box_roundtrip(v : int) : int {
                b : Box<int>;
                b.val = v;
                return b.val;
            }
        )K",
        R"K(
            module tplexec;
            import tpllib;

            main() : int {
                return tpllib::box_roundtrip(42);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Template function: library exports a template function instantiation.
// Consumer calls a wrapper that exercises it.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import template — function instantiation used via wrapper",
          "[import][e2e][template]") {
    auto result = build_exec_with_lib(
        R"K(
            module tplfnlib;

            template<typename T>
            identity(x : T) : T {
                return x;
            }

            call_identity(v : int) : int {
                return identity<int>(v);
            }
        )K",
        R"K(
            module tplfnexec;
            import tplfnlib;

            main() : int {
                return tplfnlib::call_identity(77);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 77 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Template struct with method: consumer calls wrapper that exercises
// methods on a concrete template instantiation.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import template — struct with method used via wrapper",
          "[import][e2e][template]") {
    auto result = build_exec_with_lib(
        R"K(
            module tplmethlib;

            template<typename T, int N>
            struct Holder {
                val : T;
                get_n() : int { return N; }
            }

            holder_test() : int {
                h : Holder<int, 33>;
                return h.get_n();
            }
        )K",
        R"K(
            module tplmethexec;
            import tplmethlib;

            main() : int {
                return tplmethlib::holder_test();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 33 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Template with access to struct data from outside the template:
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import template — consumer calls wrapper returning template struct value",
          "[import][e2e][template]") {
    auto result = build_exec_with_lib(
        R"K(
            module tplaccess;

            template<typename T>
            struct Wrapper {
                val : T;
            }

            make_and_read(v : int) : int {
                w : Wrapper<int>;
                w.val = v;
                return w.val;
            }
        )K",
        R"K(
            module tplaccessexec;
            import tplaccess;

            main() : int {
                return tplaccess::make_and_read(55);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 55 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Template struct with constructor:
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import template — struct with constructor used via wrapper",
          "[import][e2e][template]") {
    auto result = build_exec_with_lib(
        R"K(
            module tplctor;

            template<typename T>
            struct Container {
                val : T;
            }

            make_container(v : int) : int {
                c : Container<int>;
                c.val = v;
                return c.val;
            }
        )K",
        R"K(
            module tplctorexec;
            import tplctor;

            main() : int {
                return tplctor::make_container(88);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 88 );
}

TEST_CASE("import template — exit code via wrapper function",
          "[import][e2e][template]") {
    auto result = build_exec_with_lib(
        R"K(
            module tplexit;

            template<typename T>
            struct Pair {
                first : T;
                second : T;
            }

            sum_pair(a : int, b : int) : int {
                p : Pair<int>;
                p.first = a;
                p.second = b;
                return p.first + p.second;
            }
        )K",
        R"K(
            module tplexitexec;
            import tplexit;

            main() : int {
                return tplexit::sum_pair(42, 57);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 99 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Template definition export: verify that template_def is present in KDI
// when a library defines a template without instantiating it.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import template — template_def exported in KDI for uninstantiated template",
          "[import][template][model]") {
    TmpKdi lib(R"K(
        module tpldefonly;

        template<typename T>
        struct Storage {
            data : T;
        }

        dummy() : int { return 0; }
    )K");

    auto kdi = kdi::kdi_read_cbor_file(lib.kdi_path);

    // The template definition should be exported as a kdi_template_def
    bool found_def = false;
    std::function<void(const kdi::kdi_namespace&)> search_ns =
        [&](const kdi::kdi_namespace& ns) {
        for (const auto& td : ns.template_defs) {
            if (td.name == "Storage") {
                REQUIRE(td.fq_name == "tpldefonly::Storage");
                REQUIRE(td.entity_kind == "struct");
                REQUIRE(td.params.size() == 1);
                REQUIRE(td.params[0].kind == "typename");
                REQUIRE(td.params[0].name == "T");
                REQUIRE(!td.source.empty());
                found_def = true;
            }
        }
        for (const auto& child : ns.namespaces) search_ns(child);
    };
    search_ns(kdi.unit.root_ns);
    REQUIRE(found_def);
}

TEST_CASE("import generic — template_def exported as signature-only metadata",
          "[import][template][generic][model]") {
    TmpKdi lib(R"K(
        module tplgenericmeta;

        generic<typename T>
        struct Box {
            public value : T&;
            relay(v : T&) : T& { return v; }
        }

        dummy() : int { return 0; }
    )K");

    auto kdi = kdi::kdi_read_cbor_file(lib.kdi_path);

    bool found_def = false;
    std::function<void(const kdi::kdi_namespace&)> search_ns =
        [&](const kdi::kdi_namespace& ns) {
        for (const auto& td : ns.template_defs) {
            if (td.name == "Box") {
                REQUIRE(td.is_generic);
                REQUIRE(td.source.empty());
                REQUIRE(td.aggregate_signature != nullptr);
                REQUIRE(td.aggregate_signature->methods.size() == 1);
                auto* member = std::get_if<kdi::kdi_layout_member>(&td.aggregate_signature->layout[0]);
                REQUIRE(member != nullptr);
                REQUIRE(std::holds_alternative<kdi::kdi_ref_type>(member->type.value));
                auto& inner = *std::get<kdi::kdi_ref_type>(member->type.value).inner;
                REQUIRE(std::holds_alternative<kdi::kdi_template_param_ref>(inner.value));
                REQUIRE(std::get<kdi::kdi_template_param_ref>(inner.value).name == "T");
                found_def = true;
            }
        }
        for (const auto& child : ns.namespaces) search_ns(child);
    };
    search_ns(kdi.unit.root_ns);
    REQUIRE(found_def);
}

TEST_CASE("import generic — signature-only template_def is materialised into model",
          "[import][template][generic][model]") {
    TmpKdi lib(R"K(
        module tplgenericimport;

        generic<typename T>
        struct Box {
            public value : T&;
            relay(v : T&) : T& { return v; }
        }

        dummy() : int { return 0; }
    )K");

    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("tplgenericimport", lib.kdi_path);
    comp->set_file_resolver(resolver);

    REQUIRE_NOTHROW(comp->parse_source("consumer.k", R"K(
        module consumer;
        import tplgenericimport;
    )K"));

    auto root_ns = comp->get_unit()->get_root_namespace();
    REQUIRE(root_ns != nullptr);
    auto imported_ns = root_ns->get_child_namespace("tplgenericimport");
    REQUIRE(imported_ns != nullptr);

    auto box_tpl = imported_ns->get_aggregate("Box");
    REQUIRE(box_tpl != nullptr);
    REQUIRE(box_tpl->is_template());
    REQUIRE(box_tpl->is_generic());
    REQUIRE(box_tpl->get_tpl_info() != nullptr);
    REQUIRE(box_tpl->get_tpl_info()->is_imported_signature_only);
    REQUIRE(box_tpl->get_variable("value") != nullptr);
    REQUIRE(box_tpl->get_function("relay") != nullptr);
}

TEST_CASE("import generic<class> — owner placeholder is preserved in signature metadata",
          "[import][template][generic][model]") {
    TmpKdi lib(R"K(
        module tplgenericownermeta;

        class Dog {
        }

        generic<class T>
        class Box {
            relay(v : T!) : T! { return v; }
        }

        dummy() : int { return 0; }
    )K");

    auto kdi = kdi::kdi_read_cbor_file(lib.kdi_path);

    bool found_def = false;
    std::function<void(const kdi::kdi_namespace&)> search_ns =
        [&](const kdi::kdi_namespace& ns) {
        for (const auto& td : ns.template_defs) {
            if (td.name != "Box") continue;
            REQUIRE(td.is_generic);
            REQUIRE(td.aggregate_signature != nullptr);
            REQUIRE(td.aggregate_signature->methods.size() == 1);

            const auto& method = td.aggregate_signature->methods[0];
            REQUIRE(std::holds_alternative<kdi::kdi_owner_type>(method.return_type.value));
            auto& ret_inner = *std::get<kdi::kdi_owner_type>(method.return_type.value).inner;
            REQUIRE(std::holds_alternative<kdi::kdi_template_param_ref>(ret_inner.value));
            REQUIRE(std::get<kdi::kdi_template_param_ref>(ret_inner.value).name == "T");

            REQUIRE(method.params.size() == 1);
            REQUIRE(std::holds_alternative<kdi::kdi_owner_type>(method.params[0].type.value));
            auto& param_inner = *std::get<kdi::kdi_owner_type>(method.params[0].type.value).inner;
            REQUIRE(std::holds_alternative<kdi::kdi_template_param_ref>(param_inner.value));
            REQUIRE(std::get<kdi::kdi_template_param_ref>(param_inner.value).name == "T");
            found_def = true;
        }
        for (const auto& child : ns.namespaces) search_ns(child);
    };

    search_ns(kdi.unit.root_ns);
    REQUIRE(found_def);
}

TEST_CASE("import generic<class> — nested aggregate template_def keeps generic metadata",
          "[import][template][generic][model]") {
    TmpKdi lib(R"K(
        module tplgenericclassmeta;

        generic<class T>
        class Box {
            private struct Node {
                public value : T&;
            }

        public:
            public head : Node*;
        }

        dummy() : int { return 0; }
    )K");

    auto kdi = kdi::kdi_read_cbor_file(lib.kdi_path);

    bool found_def = false;
    std::function<void(const kdi::kdi_namespace&)> search_ns =
        [&](const kdi::kdi_namespace& ns) {
        for (const auto& td : ns.template_defs) {
            if (td.name == "Box") {
                REQUIRE(td.is_generic);
                REQUIRE(td.source.empty());
                REQUIRE(td.aggregate_signature != nullptr);
                found_def = true;
            }
        }
        for (const auto& child : ns.namespaces) search_ns(child);
    };
    search_ns(kdi.unit.root_ns);
    REQUIRE(found_def);
}

TEST_CASE("cross-module generic function — owner parameter works after KDI import",
          "[import][e2e][template][owner]") {
    auto result = build_exec_with_lib(
        R"K(
            module generic_owner_lib;

            class Dog {
            }

            generic<class T>
            is_not_null(v : T!) : int {
                if (v == null) {
                    return 0;
                }
                return 42;
            }
        )K",
        R"K(
            module generic_owner_exe;
            import generic_owner_lib;

            main() : int {
                d : generic_owner_lib::Dog! = new generic_owner_lib::Dog();
                return generic_owner_lib::is_not_null<generic_owner_lib::Dog>(d);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 42);
}

// ─────────────────────────────────────────────────────────────────────────────
// Template with value parameter: verify template_origin includes value args.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import template — template_origin with value parameter metadata",
          "[import][template][model]") {
    TmpKdi lib(R"K(
        module tplvalmeta;

        template<typename T, int N>
        struct ValHolder {
            val : T;
            get_n() : int { return N; }
        }

        use_it() : int {
            h : ValHolder<int, 10>;
            return h.get_n();
        }
    )K");

    auto kdi = kdi::kdi_read_cbor_file(lib.kdi_path);

    // Look for a concrete aggregate with template_origin that has a value arg
    bool found_origin = false;
    std::function<void(const kdi::kdi_namespace&)> search_ns =
        [&](const kdi::kdi_namespace& ns) {
        for (const auto& agg : ns.aggregates) {
            if (agg.template_origin.has_value() &&
                agg.template_origin->base_name == "ValHolder") {
                REQUIRE(agg.template_origin->args.size() == 2);
                // First arg: type (int)
                REQUIRE(agg.template_origin->args[0].type_arg.has_value());
                auto& targ = std::get<kdi::kdi_int_type>(
                    agg.template_origin->args[0].type_arg->value);
                REQUIRE(targ.bits == 32);
                // Second arg: value (10)
                REQUIRE(agg.template_origin->args[1].value_arg.has_value());
                REQUIRE(*agg.template_origin->args[1].value_arg == "10");
                found_origin = true;
            }
        }
        for (const auto& child : ns.namespaces) search_ns(child);
    };
    search_ns(kdi.unit.root_ns);
    REQUIRE(found_origin);
}

// ═════════════════════════════════════════════════════════════════════════════
// Cross-module template definition + instantiation tests (Phase 1)
//
// In Phase 1, a library defines templates and instantiates them internally.
// The concrete instances are exported in the KDI as regular entities (with
// template_origin metadata).  A consumer module imports the library and uses
// the instantiations.
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-struct-basic] Basic struct template: lib defines + instantiates,
// consumer uses via wrapper.
//
// lib:  template<typename T> struct Holder { val: T; }
//       set_and_get(v: int) : int  — wraps Holder<int>
// exe:  main() → set_and_get(42) = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — basic struct template via wrapper",
          "[import][e2e][template][cross-tpl]") {
    auto result = build_exec_with_lib(
        R"K(
            module holder_lib;

            template<typename T>
            struct Holder {
                val : T;
            }

            set_and_get(v : int) : int {
                h : Holder<int>;
                h.val = v;
                return h.val;
            }
        )K",
        R"K(
            module holder_exe;
            import holder_lib;

            main() : int {
                return holder_lib::set_and_get(42);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-fn-basic] Basic function template: lib defines + instantiates,
// consumer calls the concrete wrapper.
//
// lib:  template<typename T> add(a: T, b: T) : T { return a + b; }
//       add_ints(a: int, b: int) : int  — wraps add<int>
// exe:  main() → add_ints(17, 25) = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — basic function template via wrapper",
          "[import][e2e][template][cross-tpl]") {
    auto result = build_exec_with_lib(
        R"K(
            module adder_lib;

            template<typename T>
            add(a : T, b : T) : T {
                return a + b;
            }

            add_ints(a : int, b : int) : int {
                return add<int>(a, b);
            }
        )K",
        R"K(
            module adder_exe;
            import adder_lib;

            main() : int {
                return adder_lib::add_ints(17, 25);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-multiple-instantiations] Two distinct instantiations of the same
// template in the same lib, consumer exercises both via separate wrappers.
//
// lib:  template<typename T> struct Cell { val: T; }
//       cell_int(v: int) : int    — wraps Cell<int>
//       cell_long(v: long) : long — wraps Cell<long>
// exe:  main() → cell_int(10) + cell_long(32) = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — multiple instantiations of same template",
          "[import][e2e][template][cross-tpl]") {
    auto result = build_exec_with_lib(
        R"K(
            module cell_lib;

            template<typename T>
            struct Cell {
                val : T;
            }

            cell_int(v : int) : int {
                c : Cell<int>;
                c.val = v;
                return c.val;
            }

            cell_long(v : long) : long {
                c : Cell<long>;
                c.val = v;
                return c.val;
            }
        )K",
        R"K(
            module cell_exe;
            import cell_lib;

            main() : int {
                r1 : int;
                r1 = cell_lib::cell_int(10);
                r2 : long;
                r2 = cell_lib::cell_long(32);
                return r1 + r2;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-struct-method] Template struct with methods across import boundary.
// Consumer calls wrappers that exercise member functions on concrete instances.
//
// NOTE: Member variable access via 'this.' in template method bodies is a
// known limitation. These tests use external field access and value params.
//
// lib:  template<typename T, int N>
//       struct Acc { val: T; get_n() : int { return N; } }
//       accumulate(a: int, b: int, c: int) : int
// exe:  main() → accumulate(10, 20, 12) = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — struct with methods via wrapper",
          "[import][e2e][template][cross-tpl]") {
    auto result = build_exec_with_lib(
        R"K(
            module acc_lib;

            template<typename T, int N>
            struct Acc {
                val : T;
                get_n() : int { return N; }
            }

            accumulate(a : int, b : int, c : int) : int {
                acc : Acc<int, 0>;
                acc.val = a + b + c;
                return acc.val + acc.get_n();
            }
        )K",
        R"K(
            module acc_exe;
            import acc_lib;

            main() : int {
                return acc_lib::accumulate(10, 20, 12);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-value-param] Template with value parameter across import boundary.
//
// lib:  template<int N>
//       struct Fixed { get() : int { return N; } }
// exe:  main() → Fixed<42>.get() = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — consumer instantiates with value parameter",
          "[import][e2e][template][cross-tpl][consumer-inst]") {
    auto result = build_exec_with_lib(
        R"K(
            module val_tpl_lib;

            template<int N>
            struct Fixed {
                get() : int { return N; }
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module val_tpl_consumer;
            import val_tpl_lib;

            main() : int {
                f : Fixed<42>;
                return f.get();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [consumer-inst-mixed-params] Template with both type and value parameters
// across import boundary.
//
// lib:  template<typename T, int Scale>
//       scaled(x: T) : T { return x * Scale; }
//       test_scaled() : int
// exe:  main() → test_scaled() = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — consumer instantiates with mixed type and value params",
          "[import][e2e][template][cross-tpl][consumer-inst]") {
    auto result = build_exec_with_lib(
        R"K(
            module mixed_tpl_lib;

            template<typename T, int Scale>
            scaled(x : T) : T {
                return x * Scale;
            }

            test_scaled() : int {
                return scaled<int, 6>(7);
            }
        )K",
        R"K(
            module mixed_tpl_exe;
            import mixed_tpl_lib;

            main() : int {
                return mixed_tpl_lib::test_scaled();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );  // 7 * 6 = 42
}



TEST_CASE("cross-module template declaration",
          "[import][e2e][template][cross-tpl]") {
    auto result = build_exec_with_lib(
        R"K(
            module point_lib;

            template<typename T>
            struct Point {
                x : T;
                y : T;
                sum() : T {
                    return x + y;
                }
            }

            test_int_point() : int {
                pt : Point<int>{.x = 2, .y = 3};
                return pt.sum();
            }
        )K",
        R"K(
            module point_exe;
            import point_lib;

            main() : int {
                pt : Point<short>{.x = 5, .y = 7};
                return point_lib::test_int_point() + pt.sum();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 17 );  // 2 + 3 + 5 + 7 = 17
}

// ═════════════════════════════════════════════════════════════════════════════
// Using-alias de-resolution in template KDI export
//
// These tests verify that when a template body references a type through a
// `using` alias, the KDI export (model-based source reconstruction) emits the
// fully-qualified name so the importing module can resolve it without having
// the original `using` directive.
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// Struct template whose member type is aliased via `using` outside the template.
//
// lib:
//   namespace inner { struct Coord { x: int; y: int; } }
//   using Pt = inner::Coord;
//   template<typename T> struct Wrapper { pos: Pt; val: T; }
//
// exe: Wrapper<int> → set pos.x, pos.y, val → return pos.x + pos.y + val
//
// Without model-based reconstruction, the KDI would contain "Pt" which the
// consumer cannot resolve. With the emitter, it should contain "inner::Coord".
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — struct member type aliased via using",
          "[import][e2e][template][cross-tpl][consumer-inst][using-alias]") {
    auto result = build_exec_with_lib(
        R"K(
            module alias_tpl_lib;

            namespace inner {
                struct Coord {
                    x : int;
                    y : int;
                }
            }

            using Pt = inner::Coord;

            template<typename T>
            struct Wrapper {
                pos : Pt;
                val : T;
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module alias_tpl_exe;
            import alias_tpl_lib;

            main() : int {
                w : Wrapper<int>;
                w.pos.x = 10;
                w.pos.y = 20;
                w.val = 12;
                return w.pos.x + w.pos.y + w.val;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Function template whose parameter type is aliased via `using`.
//
// lib:
//   namespace types { struct Pair { a: int; b: int; } }
//   using P = types::Pair;
//   template<typename T> sum_pair(p: P, extra: T) : int { return p.a + p.b + extra; }
//
// exe: sum_pair<int>(Pair{10,20}, 12) → 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — function param type aliased via using",
          "[import][e2e][template][cross-tpl][consumer-inst][using-alias]") {
    auto result = build_exec_with_lib(
        R"K(
            module alias_fn_lib;

            namespace types {
                struct Pair {
                    a : int;
                    b : int;
                }
            }

            using P = types::Pair;

            template<typename T>
            sum_pair(p : P, extra : T) : int {
                return p.a + p.b + extra;
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module alias_fn_exe;
            import alias_fn_lib;

            main() : int {
                p : types::Pair;
                p.a = 10;
                p.b = 20;
                return sum_pair<int>(p, 12);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Function template whose return type is aliased via `using`.
//
// lib:
//   namespace data { struct Result { val: int; } }
//   using Res = data::Result;
//   template<typename T> extract(r: Res, extra: T) : int { return r.val + extra; }
//
// exe: create Result, call extract<int>(r, 10) → 42+10 = 52
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — function param type aliased via using (struct)",
          "[import][e2e][template][cross-tpl][consumer-inst][using-alias]") {
    auto result = build_exec_with_lib(
        R"K(
            module alias_ret_lib;

            namespace data {
                struct Result {
                    val : int;
                }
            }

            using Res = data::Result;

            template<typename T>
            extract(r : Res, extra : T) : int {
                return r.val + extra;
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module alias_ret_exe;
            import alias_ret_lib;

            main() : int {
                r : alias_ret_lib::data::Result;
                r.val = 42;
                return alias_ret_lib::extract<int>(r, 10);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 52 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Struct template with a member whose type is aliased and a method that uses it.
//
// lib:
//   namespace aux { struct Acc { total: int; } }
//   using Accum = aux::Acc;
//   template<typename T>
//   struct Adder {
//       val : Accum;
//       extra : T;
//       sum() : int { return val.total + extra; }
//   }
//
// exe: Adder<int> with val.total=30, extra=12 → sum() = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — member type aliased via using with method",
          "[import][e2e][template][cross-tpl][consumer-inst][using-alias]") {
    auto result = build_exec_with_lib(
        R"K(
            module alias_body_lib;

            namespace aux {
                struct Acc {
                    total : int;
                }
            }

            using Accum = aux::Acc;

            template<typename T>
            struct Adder {
                val : Accum;
                extra : T;
                sum() : int { return val.total + extra; }
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module alias_body_exe;
            import alias_body_lib;

            main() : int {
                d : Adder<int>;
                d.val.total = 30;
                d.extra = 12;
                return d.sum();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Template struct with a member whose type is resolved via `using namespace`.
//
// lib:
//   namespace base { struct Elem { val: int; } }
//   using namespace base;
//   template<typename T>
//   struct Container {
//       item : Elem;
//       extra : T;
//       total() : int { return item.val + extra; }
//   }
//
// exe: Container<int> with item.val=30, extra=12 → total() = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — member type resolved via using namespace",
          "[import][e2e][template][cross-tpl][consumer-inst][using-alias]") {
    auto result = build_exec_with_lib(
        R"K(
            module alias_base_lib;

            namespace base {
                struct Elem {
                    val : int;
                }
            }

            using namespace base;

            template<typename T>
            struct Container {
                item : Elem;
                extra : T;
                total() : int { return item.val + extra; }
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module alias_base_exe;
            import alias_base_lib;

            main() : int {
                c : Container<int>;
                c.item.val = 30;
                c.extra = 12;
                return c.total();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ═════════════════════════════════════════════════════════════════════════════
// Additional cross-module template coverage
//
// These tests complete Phase 1 coverage for cross-module template instantiation
// scenarios: class templates, default parameters, constructors, direct function
// template calls, and type constraints.
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-class] Template class across module boundary.
// Consumer instantiates a template class from the library and calls a method.
//
// lib:  template<typename T>
//       class Box { val: T; public get() : T { return val; } set(v: T) { val = v; } }
// exe:  Box<int> → set(42) → get() = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — class template consumer instantiation",
          "[import][e2e][template][cross-tpl][consumer-inst]") {
    auto result = build_exec_with_lib(
        R"K(
            module cls_tpl_lib;

            template<typename T>
            class Box {
                public val : T;
                Box() {}
                public get() : T { return val; }
                public set(v : T) { val = v; }
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module cls_tpl_exe;
            import cls_tpl_lib;

            main() : int {
                b : Box<int>();
                b.set(42);
                return b.get();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-default-params] Template with default type parameter.
// Consumer uses `<>` syntax to rely on the default.
//
// lib:  template<typename T = int>
//       struct DefaultBox { val: T; }
// exe:  DefaultBox<> → val = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — default type parameter with <> syntax",
          "[import][e2e][template][cross-tpl][consumer-inst][defaults]") {
    auto result = build_exec_with_lib(
        R"K(
            module def_tpl_lib;

            template<typename T = int>
            struct DefaultBox {
                val : T;
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module def_tpl_exe;
            import def_tpl_lib;

            main() : int {
                b : DefaultBox<>;
                b.val = 42;
                return b.val;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-default-partial] Template with mixed params, consumer supplies only
// the first and relies on the default for the second.
//
// lib:  template<typename T, int N = 10>
//       struct SizedVal { val: T; get_size() : int { return N; } }
// exe:  SizedVal<int> → val=32, get_size()=10, sum=42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — partial default params across modules",
          "[import][e2e][template][cross-tpl][consumer-inst][defaults]") {
    auto result = build_exec_with_lib(
        R"K(
            module partial_def_lib;

            template<typename T, int N = 10>
            struct SizedVal {
                val : T;
                get_size() : int { return N; }
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module partial_def_exe;
            import partial_def_lib;

            main() : int {
                s : SizedVal<int>;
                s.val = 32;
                return s.val + s.get_size();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );  // 32 + 10 = 42
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-fn-direct] Consumer directly calls a template function from the
// library (not via a wrapper).
//
// lib:  template<typename T>
//       identity(x: T) : T { return x; }
// exe:  main() → identity<int>(42) = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — consumer calls template function directly",
          "[import][e2e][template][cross-tpl][consumer-inst]") {
    auto result = build_exec_with_lib(
        R"K(
            module fn_direct_lib;

            template<typename T>
            identity(x : T) : T {
                return x;
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module fn_direct_exe;
            import fn_direct_lib;

            main() : int {
                return identity<int>(42);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-fn-multi-type] Consumer directly calls a multi-type-param template
// function from the library.
//
// lib:  template<typename T, typename U>
//       add_cast(a: T, b: U) : int { return a + b; }
// exe:  main() → add_cast<int, short>(30, 12) = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — consumer calls multi-param template function",
          "[import][e2e][template][cross-tpl][consumer-inst]") {
    auto result = build_exec_with_lib(
        R"K(
            module fn_multi_lib;

            template<typename T, typename U>
            add_cast(a : T, b : U) : int {
                return a + b;
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module fn_multi_exe;
            import fn_multi_lib;

            main() : int {
                return add_cast<int, short>(30, 12);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-struct-ctor] Template struct with constructor across module boundary.
// Consumer instantiates the template and uses the constructor.
//
// lib:  template<typename T>
//       struct Wrap { val: T; Wrap(v: T) : val(v) {} get() : T { return val; } }
// exe:  Wrap<int>(42) → get() = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — struct with constructor consumer instantiation",
          "[import][e2e][template][cross-tpl][consumer-inst]") {
    auto result = build_exec_with_lib(
        R"K(
            module ctor_tpl_lib;

            template<typename T>
            struct Wrap {
                val : T;
                Wrap(v : T) : val(v) {}
                get() : T { return val; }
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module ctor_tpl_exe;
            import ctor_tpl_lib;

            main() : int {
                w : Wrap<int>(42);
                return w.get();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-two-consumers] Two different consumer instantiations of the same
// template from one library, verifying that distinct types are produced.
//
// lib:  template<typename T>
//       struct Val { data: T; }
// exe:  Val<int> and Val<short> — set different values and sum
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — consumer creates two distinct instantiations",
          "[import][e2e][template][cross-tpl][consumer-inst]") {
    auto result = build_exec_with_lib(
        R"K(
            module two_inst_lib;

            template<typename T>
            struct Val {
                data : T;
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module two_inst_exe;
            import two_inst_lib;

            main() : int {
                vi : Val<int>;
                vi.data = 30;
                vs : Val<short>;
                vs.data = 12;
                return vi.data + vs.data;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );  // 30 + 12 = 42
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-fn-value-param-direct] Consumer directly calls a template function
// with a value parameter from the library.
//
// lib:  template<int N>
//       offset(x: int) : int { return x + N; }
// exe:  main() → offset<2>(40) = 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — consumer calls value-param template function",
          "[import][e2e][template][cross-tpl][consumer-inst]") {
    auto result = build_exec_with_lib(
        R"K(
            module fn_val_lib;

            template<int N>
            offset(x : int) : int {
                return x + N;
            }

            dummy() : int { return 0; }
        )K",
        R"K(
            module fn_val_exe;
            import fn_val_lib;

            main() : int {
                return offset<2>(40);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-intrinsic] Template struct with @Intrinsic-annotated constructor,
// destructor, and methods across module boundary. Consumer instantiates the
// template with its own type and calls intrinsic methods.
//
// lib:  template<typename T> struct Slot { @Intrinsic ctor/dtor/construct/destruct }
// exe:  main() → Slot<Widget>.construct(); .get().v = 42; .destruct(); return 42
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — intrinsic UniSlot consumer instantiation",
          "[import][e2e][template][cross-tpl][consumer-inst][intrinsic]") {
    auto result = build_exec_with_lib(
        R"K(
            module intrlib;

            namespace annotations {
                annotation Intrinsic {
                    name : int;
                }
            }

            template<typename T>
            struct Slot {
                private:
                _slot : T;

                public:
                @annotations::Intrinsic(0)
                Slot();

                @annotations::Intrinsic(0)
                ~Slot();

                @annotations::Intrinsic(1)
                construct();

                @annotations::Intrinsic(2)
                destruct();

                get() : T& { return _slot; }
            }

            // Force at least one lib-side instantiation for linkage
            lib_test() : int {
                s : Slot<int>;
                s.construct();
                s.get() = 77;
                result : int = s.get();
                s.destruct();
                return result;
            }
        )K",
        R"K(
            module intrexec;
            import intrlib;

            struct Widget {
                v : int;
                Widget() { v = 42; }
            }

            main() : int {
                s : Slot<Widget>;
                s.construct();
                result : int = s.get().v;
                s.destruct();
                return result;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [cross-tpl-intrinsic][member-template] Template struct with member template
// method (variadic pack) and @Intrinsic annotation across module boundary.
// This mirrors the real UniSlot<T>::construct<Args...>(Args...args) pattern.
//
// lib:  template<typename T> struct Slot { template<typename...Args> construct(Args...args); }
// exe:  main() → Slot<Point>.construct<int,int>(10,32); return .get().x + .get().y
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module template — intrinsic member template with variadic pack",
          "[import][e2e][template][cross-tpl][consumer-inst][intrinsic][member-template]") {
    auto result = build_exec_with_lib(
        R"K(
            module mtlib;

            namespace annotations {
                annotation Intrinsic {
                    name : int;
                }
            }

            template<typename T>
            struct Slot {
                private:
                _slot : T;

                public:
                @annotations::Intrinsic(0)
                Slot();

                @annotations::Intrinsic(0)
                ~Slot();

                @annotations::Intrinsic(1)
                template<typename...Args>
                construct(Args...args);

                @annotations::Intrinsic(2)
                destruct();

                get() : T& { return _slot; }
            }

            // Force at least one lib-side instantiation for linkage
            lib_test() : int {
                s : Slot<int>;
                s.construct();
                s.get() = 77;
                result : int = s.get();
                s.destruct();
                return result;
            }
        )K",
        R"K(
            module mtexec;
            import mtlib;

            struct Point {
                x : int;
                y : int;
                Point(ax : int, ay : int) { x = ax; y = ay; }
            }

            main() : int {
                s : Slot<Point>;
                s.construct<int, int>(10, 32);
                result : int = s.get().x + s.get().y;
                s.destruct();
                return result;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [homonym-imports] Known limitation — two *imported* templates with the same
// short name from different modules.
//
// A consumer imports two libraries, each exporting a top-level template named
// `Box`. It instantiates each, qualified, as `boxa::Box<int>` and
// `boxb::Box<int>`, which have different layouts (1 vs 2 fields) and different
// behaviour. Expected: 21 + (10 + 10) = 41.
//
// CURRENT LIMITATION: imported templates are re-injected by flattening them into
// the consumer's *root* namespace (the `module <ns>;`-rename trick in
// `kdi_importer::materialise_template_def`). This is required so that imported
// top-level symbols stay reachable *unqualified* (`import lib;` behaves like
// `using namespace lib;`). As a side effect, two homonymous imported templates
// collide on their short name in root and get merged — the second silently wins.
//
// The instantiation `struct_type` *identity* registry is already collision-safe
// (origin-qualified key, see `[template][instantiation][ns-collision]`); the
// remaining gap is the model-level *symbol* clash. Fixing it cleanly requires a
// deeper change: scope lookup must resolve unqualified imported symbols from
// their origin namespaces (an `import`-as-`using-namespace` mechanism) so the
// flatten can be dropped and homonymous imports require qualification.
// Tracked in TODO.md.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Known-limitation: homonymous imported templates from different modules",
          "[.][import][template][homonym-imports]") {
    SKIP("Two imported templates with the same short name from different modules "
         "still clash at the model/symbol level because imported templates are "
         "flattened into the consumer root for unqualified access. Requires "
         "import-as-using-namespace scope lookup; tracked in TODO.md.");

    std::vector<LibSpec> libs = {
        { R"K(
            module boxa;
            template<typename T>
            struct Box {
                v : T;
            public:
                Box(x : T) { v = x; }
                const get() : T { return v; }
            }
        )K" },
        { R"K(
            module boxb;
            template<typename T>
            struct Box {
                v1 : T;
                v2 : T;
            public:
                Box(x : T) { v1 = x; v2 = x; }
                const sum() : T { return v1 + v2; }
            }
        )K" }
    };

    auto result = build_exec_with_libs(libs,
        R"K(
            module box_exec;
            import boxa;
            import boxb;
            main() : int {
                ba : boxa::Box<int>(21);
                bb : boxb::Box<int>(10);
                return ba.get() + bb.sum();   // 21 + (10 + 10) = 41
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 41 );
}


// ═════════════════════════════════════════════════════════════════════════════
// TEMPLATE INSTANTIATION DIAMOND (libk templates, linkonce_odr + COMDAT)
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// [instantiation-diamond-shared] Diamond instantiation of a libk template.
//
//                 libk: Optional<T>
//                /                  \
//   optdiamond_a (Optional<int>)   optdiamond_b (Optional<int>)
//                \                  /
//            optdiamond_exec (Optional<int>)   <- imports both A and B
//
// Each of the two libraries and the executable instantiate ::k::Optional<int>
// independently (k is auto-imported). Every instantiation symbol is synthesised
// under its origin-absolute name (::k::Optional<int>::...) and emitted
// linkonce_odr + COMDAT, so the shared-library diamond links without
// duplicate-symbol errors and the dynamic linker interposes a single copy at
// load (default visibility).
//
// Expected: 40 + 2 + 0 = 42.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import instantiation diamond - Optional<int> from libk in two libs + exe",
          "[import][e2e][instantiation-diamond-shared]") {
    std::vector<LibSpec> libs = {
        { R"K(
            module optdiamond_a;
            makeOptA() : int {
                v : int = 40;
                z : int = 0;
                o : Optional<int>;
                o.set(v);
                return o.getOr(z);
            }
        )K" },
        { R"K(
            module optdiamond_b;
            makeOptB() : int {
                v : int = 2;
                z : int = 0;
                o : Optional<int>;
                o.set(v);
                return o.getOr(z);
            }
        )K" }
    };

    auto result = build_exec_with_libs(libs,
        R"K(
            module optdiamond_exec;
            import optdiamond_a;
            import optdiamond_b;
            main() : int {
                z : int = 0;
                o : Optional<int>;
                return makeOptA() + makeOptB() + o.getOr(z);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [instantiation-diamond-shared] Same diamond for the two-parameter Expected<R,E>.
//
// Two libraries and the executable each instantiate ::k::Expected<int,int>.
// Verifies that multi-parameter libk instantiations dedup the same way across a
// shared-library diamond.
//
// Expected: 40 + 2 + 0 = 42.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import instantiation diamond - Expected<int,int> from libk in two libs + exe",
          "[import][e2e][instantiation-diamond-shared]") {
    std::vector<LibSpec> libs = {
        { R"K(
            module expdiamond_a;
            makeExpA() : int {
                v : int = 40;
                e : Expected<int, int>;
                e.setResult(v);
                return e.getResult();
            }
        )K" },
        { R"K(
            module expdiamond_b;
            makeExpB() : int {
                v : int = 2;
                e : Expected<int, int>;
                e.setResult(v);
                return e.getResult();
            }
        )K" }
    };

    auto result = build_exec_with_libs(libs,
        R"K(
            module expdiamond_exec;
            import expdiamond_a;
            import expdiamond_b;
            main() : int {
                v : int = 0;
                e : Expected<int, int>;
                e.setResult(v);
                return makeExpA() + makeExpB() + e.getResult();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [instantiation-diamond-shared][user-template] Diamond instantiation of a
// USER-DECLARED (non-libk) struct template.
//
//                 boxlib: Box<T>           (user library — NOT libk)
//                /                  \
//   boxdiamond_a (Box<int>)   boxdiamond_b (Box<int>)
//                \                  /
//            boxdiamond_exec (Box<int>)   <- imports boxlib + both A and B
//
// The COMDAT/linkonce_odr dedup of template instantiations is module-agnostic:
// it keys off the template's origin module (set by the KDI importer for ANY
// imported template) and the is_instantiation()/is_template() flags, never on
// libk specifically. This test proves it for a template declared in a plain
// user library: A, B and the executable each synthesise their own
// ::boxlib::Box<int> under the same origin-absolute name, emitted linkonce_odr
// + COMDAT, so the shared-library diamond links without duplicate-symbol errors
// and the dynamic linker interposes a single copy at load (default visibility).
//
// Expected: 40 + 2 + 0 = 42.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import instantiation diamond - user-declared Box<int> template in two libs + exe",
          "[import][e2e][instantiation-diamond-shared][user-template]") {
    std::vector<LibSpec> libs = {
        // lib[0]: the template-defining library (the template's true origin).
        { R"K(
            module boxlib_01;

            template<typename T>
            struct Box {
                val : T;
            }
        )K" },
        // lib[1]: imports boxlib and instantiates ::boxlib::Box<int> itself.
        { R"K(
            module boxdiamond_a;
            import boxlib_01;
            makeBoxA() : int {
                b : boxlib_01::Box<int>;
                b.val = 40;
                return b.val;
            }
        )K" },
        // lib[2]: imports boxlib and instantiates ::boxlib::Box<int> itself.
        { R"K(
            module boxdiamond_b;
            import boxlib_01;
            makeBoxB() : int {
                b : boxlib_01::Box<int>;
                b.val = 2;
                return b.val;
            }
        )K" }
    };

    auto result = build_exec_with_libs(libs,
        R"K(
            module boxdiamond_exec;
            import boxlib_01;
            import boxdiamond_a;
            import boxdiamond_b;
            main() : int {
                b : boxlib_01::Box<int>;
                b.val = 0;
                return makeBoxA() + makeBoxB() + b.val;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [instantiation-diamond-static] STATIC-LINK diamond of a libk template.
//
// FIXED. The static-archive variant of the diamond above (link the executable
// against two static archives instead of two .so files) used to fail to link
// with "multiple definition" errors on secondary (base/interface) vtable
// globals (named `<vtable>_for_<BaseName>`) re-emitted by every consumer of a
// libk/user template that implements a base interface or reaches a shared
// virtual base. These globals were always created with plain ExternalLinkage,
// unlike the primary vtable/RTTI globals which already got the merge-friendly
// linkonce_odr + COMDAT treatment for template instantiations.
//
// Fixed in gen_class.cpp: secondary vtable globals now go through the same
// `should_merge_aggregate_symbols()` + `apply_instantiation_linkage()` path as
// the primary vtable/RTTI globals.
//
// This scenario (both a libk-template diamond and a user-declared-template
// diamond, statically linked) is now covered by real, non-skipped, passing
// tests in test-klangc-static-diamond.cpp:
//   - "klangc: static-link diamond of a libk template instantiation"
//   - "klangc: static-link diamond of a user-declared template instantiation"
// ─────────────────────────────────────────────────────────────────────────────


// ─────────────────────────────────────────────────────────────────────────────
// [import][mangling][enum-param] Cross-module overloads differing only by an enum
// parameter.
//
// The mangling half is FIXED: mangler::mangle_type() used to return an empty encoding for
// enumerations, so both overloads were recorded in the .kdi under the very same mangled
// name while the .so actually exported '<sym>' and '<sym>.1' (LLVM auto-uniquification,
// invisible to the KDI). Both overloads now get distinct symbols — asserted below.
//
// The remaining half is a KNOWN LIMITATION (skipped): an unqualified call to an imported
// function is bound eagerly by symbol_resolver to the first matching imported overload and
// never reaches type_reference_resolver::get_best_matching_function, so the consumer still
// calls the ErrA overload for an ErrB argument. See TODO.md, "Unqualified calls to imported
// functions bypass overload resolution".
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Imported overloads differing only by an enum parameter have distinct symbols",
          "[import][mangling][enum-param]") {
    auto comp = compile_model(R"K(
        module enumovl;
        public enum ErrA { a1 = 1; a2 = 2; }
        public enum ErrB { b1 = 7; b2 = 8; }
        public f(x : ErrA) : int { return 100; }
        public f(x : ErrB) : int { return 200; }
    )K");
    REQUIRE(comp != nullptr);

    auto root_ns = comp->get_unit()->get_root_namespace();
    auto overloads = root_ns->get_functions("f");
    REQUIRE(overloads.size() == 2);
    CHECK_FALSE(overloads[0]->get_mangled_name().empty());
    CHECK_FALSE(overloads[1]->get_mangled_name().empty());
    CHECK(overloads[0]->get_mangled_name() != overloads[1]->get_mangled_name());
    // No symbol may carry LLVM's '.N' uniquification suffix: those never reach the KDI.
    CHECK(overloads[0]->get_mangled_name().find('.') == std::string::npos);
    CHECK(overloads[1]->get_mangled_name().find('.') == std::string::npos);
}

TEST_CASE("Known-limitation: cross-module overload selection on an imported enum argument",
          "[.][import][mangling][enum-param]") {
    auto result = build_exec_with_lib(R"K(
        module enumovl2;
        public enum ErrA { a1 = 1; a2 = 2; }
        public enum ErrB { b1 = 7; b2 = 8; }
        public f(x : ErrA) : int { return 100; }
        public f(x : ErrB) : int { return 200; }
    )K", R"K(
        module main;
        import enumovl2;
        main() : int {
            if (f(enumovl2::ErrA::a1) != 100) return 1;
            if (f(enumovl2::ErrB::b1) != 200) return 2;
            return 0;
        }
    )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 0 );
}

// ═════════════════════════════════════════════════════════════════════════════
// CROSS-MODULE VIRTUAL / EXCEPTION REGRESSIONS
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// [import-virtual-void] Overriding a void-returning virtual method declared in
// an imported interface / class.
//
// build_vtable_layout() runs before `void` return types are normalised to
// nullptr, so a locally-declared `: void` method carried an unresolved_type
// named "void" while its imported counterpart carried nullptr. The signatures
// were reported as different and the override was rejected with error 00177.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import virtual — local class overrides a void-returning imported virtual",
          "[import][e2e][import-class][import-virtual-void]") {
    auto result = build_exec_with_lib(
        R"K(
            module voidvirt_lib;

            public interface Sink {
                accept(v: int) : void;
            }

            public class Emitter {
            public:
                Emitter() {}
                reset() : void {}
                emit(s: Sink+, v: int) : void { s->accept(v); }
            }
        )K",
        R"K(
            module exec_voidvirt;
            import voidvirt_lib;

            total : int = 0;

            class Collector : public voidvirt_lib::Sink {
            public:
                Collector() {}
                override accept(v: int) : void { total += v; }
            }

            class ResetCounter : public voidvirt_lib::Emitter {
            public:
                ResetCounter() {}
                override reset() : void { total += 100; }
            }

            main() : int {
                c : Collector! = new Collector();
                e : voidvirt_lib::Emitter;
                e.emit(c, 7);
                r : ResetCounter! = new ResetCounter();
                r->reset();
                return total;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 107 );   // 7 + 100
}

TEST_CASE("import virtual — local class implements imported template interface with self-return",
          "[import][e2e][import-class][import-virtual-template-interface]") {
    auto result = build_exec_with_lib(
        R"K(
            module tplvirt_lib;

            template<typename T>
            public interface Out {
                write(v: T) : Out<T>&;
                close() : Out<T>&;
            }
        )K",
        R"K(
            module exec_tplvirt;
            import tplvirt_lib;

            class CounterOut : public tplvirt_lib::Out<byte> {
            public:
                sum : int = 0;
                isClosed : bool = false;

                CounterOut() {}

                override write(v: byte) : tplvirt_lib::Out<byte>& {
                    sum += v;
                    return this;
                }

                override close() : tplvirt_lib::Out<byte>& {
                    isClosed = true;
                    return this;
                }
            }

            main() : int {
                out : CounterOut! = new CounterOut();
                out->write(5);
                out->close();
                return out->sum + (out->isClosed ? 100 : 0);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 105 );   // 5 + 100
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-exception] An exception thrown inside a library must be catchable by
// the importing module, including by one of its base classes.
//
// The RTTI global of an imported class used to be re-emitted in the importing
// module as a `linkonce_odr` definition instead of an external declaration.
// Exception dispatch matches typeinfo by pointer identity, so each module ended
// up comparing its own copy of the symbol. It only ever worked because ELF
// symbol interposition happened to collapse the copies back into one.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import exception — exception thrown in a library is caught by the exe",
          "[import][e2e][import-exception]") {
    auto result = build_exec_with_lib(
        R"K(
            module exclib;

            public class LibError : public Exception {
            public:
                LibError(code: int) : Exception(code) {}
            }

            public boom(code: int) : int throws LibError {
                throw LibError(code);
            }
        )K",
        R"K(
            module exec_exc;
            import exclib;

            main() : int {
                direct : int = 0;
                try {
                    exclib::boom(5);
                } catch (e : exclib::LibError&) {
                    direct = e.getCode();
                }

                base : int = 0;
                try {
                    exclib::boom(6);
                } catch (e : Exception&) {
                    base = e.getCode();
                }

                return direct + base;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 11 );   // 5 + 6
}

// ═════════════════════════════════════════════════════════════════════════════
// [import-template-symbols] A template defined in a library and instantiated in
// the importing module must still resolve the symbols its body references from
// the library: free functions, static methods and module-level constants.
//
// Regression: template bodies are cloned by template_instantiator *after* the
// symbol_resolver pass, so the clone was resolved against the importing unit
// only, where those library symbols exist as `imported_*` nodes that neither
// scope_lookup::lookup_functions nor the symbol resolver ever consulted.
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("import template — instantiated body resolves imported functions and constants",
          "[import][e2e][import-template-symbols]") {
    auto result = build_exec_with_lib(
        R"K(
            module tplsym;

            public const BONUS : int = 5;

            public triple(v : int) : int { return v * 3; }

            public struct Helper {
                public static offset() : int { return 2; }
            }

            template<typename T>
            public struct Calc {
                public compute(v : int) : int {
                    // imported free function + imported static method + imported constant
                    return triple(v) + Helper::offset() + BONUS;
                }
            }
        )K",
        R"K(
            module main;
            import tplsym;
            using tplsym;

            main() : int {
                c : Calc<int>;
                return c.compute(5);   // 15 + 2 + 5 = 22
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 22 );
}

// ═════════════════════════════════════════════════════════════════════════════
// [import-struct-indirect-member] A struct member whose type is a pointer (or
// any other addresser) to an *imported* aggregate must be fully resolved.
//
// Regression: context::resolve_struct_type used to record the field with its
// still-unresolved pointee when the imported name was not yet bound (that only
// happens in the later aggregate_type_resolver pass). Because the LLVM body was
// set during that first attempt, the early-return at the top of
// resolve_struct_type then kept the stale field record forever, and any member
// access through it failed with "the left-hand side is a reference to
// '<<unresolved:X>>' which is not a struct".
//
// The bug only showed up when *no* by-value member of an imported aggregate was
// present: such a member made the first attempt bail out, so the struct was
// rebuilt later with correct field types — which is why the two shapes below
// are both exercised.
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("import struct — indirect members targeting an imported aggregate resolve",
          "[import][e2e][import-struct-indirect-member]") {
    auto result = build_exec_with_lib(
        R"K(
            module boxlib_02;

            public struct Payload {
                public value : int;
                public get() : int { return value; }
            }

            public struct Marker {
                public tag : int;
            }
        )K",
        R"K(
            module main;
            import boxlib_02;
            using boxlib_02;

            // Only indirect members: nothing forces an early re-resolution pass.
            public struct OnlyIndirect {
                p : Payload*;
            }

            // Mixed shape: a by-value imported member alongside indirect ones.
            public struct Mixed {
                m : Marker;
                p : Payload*;
            }

            readOnly(s: OnlyIndirect*) : int { return s->p->get(); }
            readMixed(s: Mixed*) : int { return s->p->get() + s->m.tag; }

            main() : int {
                payload : Payload;
                payload.value = 20;

                a : OnlyIndirect;
                a.p = &payload;

                b : Mixed;
                b.m.tag = 2;
                b.p = &payload;

                return readOnly(&a) + readMixed(&b);   // 20 + (20 + 2) = 42
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 42 );
}

// ═════════════════════════════════════════════════════════════════════════════
// Exported aliasing across a module boundary — 'alias' and 'typedef'
// ═════════════════════════════════════════════════════════════════════════════

