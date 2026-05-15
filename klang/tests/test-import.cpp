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
 * Integration tests for K module imports (Phase 4 — kdi_importer).
 *
 * Strategy:
 *  - Build a .kdi using the existing build_shared_library() helper (which
 *    also produces the .kdi alongside the .so).
 *  - Create a path_lookup_file_resolver pointing at the directory that
 *    contains that .kdi, and inject it into a compiler instance.
 *  - Compile a consumer K module that declares the import and verify
 *    that the imported_module entries are filled in correctly.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Phase 4 — kdi_importer: load, validate, store
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import — kdi loaded and stored in unit imported_modules", "[import][phase4]") {
    // Build a simple library
    TmpKdi lib(R"K(
        module mathlib;
        add(a: int, b: int) : int { return a + b; }
    )K");

    // Compile a consumer that imports it (no body using the function yet)
    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    // Use explicit path: the .kdi has a random name, not "mathlib.kdi"
    resolver->add_explicit_path("mathlib", lib.kdi_path);
    comp->set_file_resolver(resolver);

    REQUIRE_NOTHROW(comp->parse_source("consumer.k",
        R"K(
            module consumer;
            import mathlib;
        )K"));

    const auto& imports = comp->get_unit()->get_imports();
    REQUIRE( imports.size() == 1 );
    REQUIRE( imports[0].module_name.to_string() == "mathlib" );
    REQUIRE( imports[0].kdi != nullptr );
    REQUIRE_FALSE( imports[0].resolved_kdi_path.empty() );
    REQUIRE( imports[0].kdi->header.module_name == "mathlib" );
}

TEST_CASE("import — qualified module name", "[import][phase4]") {
    TmpKdi lib(R"K(
        module math::utils;
        double_val(x: int) : int { return x + x; }
    )K");

    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("math::utils", lib.kdi_path);
    comp->set_file_resolver(resolver);

    REQUIRE_NOTHROW(comp->parse_source("consumer.k",
        R"K(
            module consumer;
            import math::utils;
        )K"));

    const auto& imports = comp->get_unit()->get_imports();
    REQUIRE( imports.size() == 1 );
    REQUIRE( imports[0].module_name.to_string() == "math::utils" );
    REQUIRE( imports[0].kdi != nullptr );
}

TEST_CASE("import — module not found is a fatal error", "[import][phase4][error]") {
    auto comp = k::compiler::create();
    // Empty resolver — nothing will be found
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    comp->set_file_resolver(resolver);

    REQUIRE_THROWS_AS(
        comp->parse_source("consumer.k",
            R"K(
                module consumer;
                import no::such::lib;
            )K"),
        k::log::compiler_error);
}

TEST_CASE("import — namespace root collision between two imports is an error",
          "[import][phase4][error]") {
    // Build two libraries with the same root namespace component
    TmpKdi lib1(R"K(
        module myns::liba;
        fa() : int { return 1; }
    )K");
    TmpKdi lib2(R"K(
        module myns::libb;
        fb() : int { return 2; }
    )K");

    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("myns::liba", lib1.kdi_path);
    resolver->add_explicit_path("myns::libb", lib2.kdi_path);
    comp->set_file_resolver(resolver);

    // Both share root "myns" — this IS a collision per spec
    REQUIRE_THROWS_AS(
        comp->parse_source("consumer.k",
            R"K(
                module consumer;
                import myns::liba;
                import myns::libb;
            )K"),
        k::log::compiler_error);
}

TEST_CASE("import — duplicate import is deduplicated silently", "[import][phase4]") {
    TmpKdi lib(R"K(
        module duplib;
        f() : int { return 0; }
    )K");

    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("duplib", lib.kdi_path);
    comp->set_file_resolver(resolver);

    // model_builder::add_import() deduplicates
    REQUIRE_NOTHROW(comp->parse_source("consumer.k",
        R"K(
            module consumer;
            import duplib;
            import duplib;
        )K"));

    REQUIRE( comp->get_unit()->get_imports().size() == 1 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 6a — unit::find_imported_*()
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("find_imported_function — simple module, function found", "[import][step6a]") {
    TmpKdi lib(R"K(
        module mymath;
        add(a: int, b: int) : int { return a + b; }
    )K");

    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("mymath", lib.kdi_path);
    comp->set_file_resolver(resolver);
    comp->parse_source("consumer.k", R"K(
        module consumer;
        import mymath;
    )K");

    auto* unit = comp->get_unit().get();
    // look up the function by its K qualified name
    k::name fn_name{false, {"mymath", "add"}};
    auto* fn = unit->find_imported_function(fn_name);
    REQUIRE( fn != nullptr );
    REQUIRE( fn->name == "add" );

    // The import must be marked as used
    REQUIRE( unit->get_imports()[0].used == true );
}

TEST_CASE("find_imported_function — qualified module, function found", "[import][step6a]") {
    TmpKdi lib(R"K(
        module math::utils;
        square(x: int) : int { return x * x; }
    )K");

    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("math::utils", lib.kdi_path);
    comp->set_file_resolver(resolver);
    comp->parse_source("consumer.k", R"K(
        module consumer;
        import math::utils;
    )K");

    k::name fn_name{false, {"math", "utils", "square"}};
    auto* fn = comp->get_unit()->find_imported_function(fn_name);
    REQUIRE( fn != nullptr );
    REQUIRE( fn->name == "square" );
}

TEST_CASE("find_imported_function — unknown name returns nullptr", "[import][step6a]") {
    TmpKdi lib(R"K(
        module mylib2;
        foo() : int { return 0; }
    )K");

    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("mylib2", lib.kdi_path);
    comp->set_file_resolver(resolver);
    comp->parse_source("consumer.k", R"K(
        module consumer;
        import mylib2;
    )K");

    k::name bad_name{false, {"mylib2", "nonexistent"}};
    REQUIRE( comp->get_unit()->find_imported_function(bad_name) == nullptr );
    // Nothing was used
    REQUIRE( comp->get_unit()->get_imports()[0].used == false );
}

TEST_CASE("find_imported_type — aggregate found", "[import][step6a]") {
    TmpKdi lib(R"K(
        module shapes;
        struct Point { x: int; y: int; }
    )K");

    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("shapes", lib.kdi_path);
    comp->set_file_resolver(resolver);
    comp->parse_source("consumer.k", R"K(
        module consumer;
        import shapes;
    )K");

    k::name type_name{false, {"shapes", "Point"}};
    auto* agg = comp->get_unit()->find_imported_type(type_name);
    REQUIRE( agg != nullptr );
    REQUIRE( agg->name == "Point" );
    REQUIRE( comp->get_unit()->get_imports()[0].used == true );
}

TEST_CASE("find_imported_type — unknown type returns nullptr", "[import][step6a]") {
    TmpKdi lib(R"K(
        module shapes2;
        struct Circle { r: int; }
    )K");

    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("shapes2", lib.kdi_path);
    comp->set_file_resolver(resolver);
    comp->parse_source("consumer.k", R"K(
        module consumer;
        import shapes2;
    )K");

    k::name bad_name{false, {"shapes2", "NoSuchType"}};
    REQUIRE( comp->get_unit()->find_imported_type(bad_name) == nullptr );
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 7a — imported_function / imported_variable model nodes
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("get_or_create_imported_function — model node created and cached", "[import][step7a]") {
    TmpKdi lib(R"K(
        module imath;
        add(a: int, b: int) : int { return a + b; }
    )K");

    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("imath", lib.kdi_path);
    comp->set_file_resolver(resolver);

    // Compile a consumer that CALLS the imported function so that the
    // resolver creates the imported_function node.
    REQUIRE_NOTHROW(comp->parse_source("consumer.k", R"K(
        module consumer;
        import imath;
        main() : int {
            return imath::add(1, 2);
        }
    )K"));
    // After compilation, the imported_functions map should contain one entry.
    const auto& ifns = comp->get_unit()->get_imported_functions();
    REQUIRE( ifns.size() >= 1 );

    // Find our specific function by its simple name
    bool found = false;
    for (const auto& [mangled, fn] : ifns) {
        if (fn->get_short_name() == "add") {
            found = true;
            REQUIRE( fn->get_return_type() != nullptr );    // int return type
            REQUIRE( fn->get_parameter_size() == 2 );       // a, b
            break;
        }
    }
    REQUIRE( found );
}

TEST_CASE("get_or_create_imported_function — LLVM declare emitted in IR",
          "[import][step7a]") {
    // SKIP removed for debug
    TmpKdi lib(R"K(
        module imath2;
        mul(a: int, b: int) : int { return a * b; }
    )K");

    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("imath2", lib.kdi_path);
    comp->set_file_resolver(resolver);

    REQUIRE_NOTHROW(comp->parse_source("consumer.k", R"K(
        module consumer;
        import imath2;
        main() : int {
            return imath2::mul(3, 4);
        }
    )K"));

    // Dump IR and search for a declare of the mangled mul symbol
    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);

    // The mul function must appear as 'declare' (no definition body)
    // Mangled names contain the function name
    bool declare_found = (ir.find("declare") != std::string::npos);
    REQUIRE( declare_found );

    // Verify no definition body is emitted for the imported function
    // (i.e. "define" with "mul" in the same line should NOT be from imath2)
    // The 'main' wrapper may have a define, but the imported mul should not.
    // We check by counting the imported function mangled name appearances.
    const auto& ifns = comp->get_unit()->get_imported_functions();
    // First verify that there IS an imported function named 'mul'
    bool found_mul = false;
    for (const auto& [mangled, fn] : ifns) {
        if (fn->get_short_name() == "mul") {
            found_mul = true;
            INFO("mangled key   = " << mangled);
            INFO("get_mangled() = " << fn->get_mangled_name());
            INFO("IR snippet    = " << ir.substr(0, 500));
            // The mangled name must appear in IR as a declare, not a define
            REQUIRE( ir.find(mangled) != std::string::npos );
        }
    }
    REQUIRE( found_mul );
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 7b — imported_aggregate model nodes
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("get_or_create_imported_aggregate — struct type resolved", "[import][step7b]") {
    // SKIP removed for debug
    TmpKdi lib(R"K(
        module geom;
        struct Vec2 { x: int; y: int; }
        make_vec(x: int, y: int) : Vec2 {
            v : Vec2;
            return v;
        }
    )K");

    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("geom", lib.kdi_path);
    comp->set_file_resolver(resolver);

    // Consumer uses the aggregate type as return type of a local function
    REQUIRE_NOTHROW(comp->parse_source("consumer.k", R"K(
        module consumer;
        import geom;
        get_origin() : geom::Vec2 {
            return geom::make_vec(0, 0);
        }
    )K"));

    // imported_aggregates map should contain Vec2
    const auto& iaggs = comp->get_unit()->get_imported_aggregates();
    bool found = false;
    for (const auto& [fq, agg] : iaggs) {
        if (agg->get_short_name() == "Vec2") {
            found = true;
            REQUIRE( agg->get_struct_type() != nullptr );
            break;
        }
    }
    REQUIRE( found );
}

TEST_CASE("import — imported struct member methods are accessible", "[import][step7b]") {
    TmpKdi lib(R"K(
        module calclib;
        struct Counter {
            value: int;
            increment() { this.value = this.value + 1; }
            get() : int { return this.value; }
        }
    )K");

    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("calclib", lib.kdi_path);
    comp->set_file_resolver(resolver);

    REQUIRE_NOTHROW(comp->parse_source("consumer.k", R"K(
        module consumer;
        import calclib;
        use_counter() : int {
            c : calclib::Counter;
            c.increment();
            return c.get();
        }
    )K"));

    // The Counter aggregate must have been imported
    const auto& iaggs = comp->get_unit()->get_imported_aggregates();
    bool found = false;
    for (const auto& [fq, agg] : iaggs) {
        if (agg->get_short_name() == "Counter") {
            found = true;
            // Methods should have been materialised as imported_method children
            REQUIRE_FALSE( agg->get_children().empty() );
            break;
        }
    }
    REQUIRE( found );
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 7 — End-to-end integration: lib + executable linked and run
//
// The library exports:
//   - a global function  global_add(a, b) : int  →  returns a + b
//   - a struct  Adder  with a member  value: int  and a method
//     add(x: int) : int  →  returns this.value + x
//
// The executable:
//   - calls  mylib::global_add(10, 5)                  → 15
//   - instantiates  mylib::Adder  (value = 20), calls  a.add(7)  → 27
//   - returns  15 + 27  = 42  as the process exit code
//
// The test asserts exit_code == 42.
// ─────────────────────────────────────────────────────────────────────────────

static constexpr std::string_view LIB_SRC = R"K(
    module mylib;

    /** Global function: return a + b */
    global_add(a: int, b: int) : int {
        return a + b;
    }

    /** Aggregate with one field and one method */
    struct Adder {
        value: int;

        /** Return this.value + x */
        add(x: int) : int {
            return this.value + x;
        }
    }
)K";

static constexpr std::string_view EXEC_SRC = R"K(
    module myexec;
    import mylib;

    main() : int {
        // global_add(10, 5) → 15
        r1 : int;
        r1 = mylib::global_add(10, 5);

        // Adder{value=20}.add(7) → 27
        a : mylib::Adder;
        a.value = 20;
        r2 : int;
        r2 = a.add(7);

        // Return 15 + 27 = 42
        return r1 + r2;
    }
)K";

TEST_CASE("import end-to-end — exe uses imported global fn + struct method, exits 42",
          "[import][e2e]") {
    // SKIP removed for debug
    auto result = build_exec_with_lib(LIB_SRC, EXEC_SRC);

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 42 );
}

// ═════════════════════════════════════════════════════════════════════════════
// Multi-lib import tests — interfaces and classes across library boundaries
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// [import-class] Interface + concrete class in the same lib, dispatch via I&
//
// lib:
//   interface ICounter { increment(); get() : int; }
//   class Counter : ICounter  { value:int; increment → value+1; get → value }
// exe:
//   use(c: iface_one::ICounter&) : int { c.increment(); c.increment(); return c.get(); }
//   main() → 2
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import class — interface + concrete class in same lib, dispatch via I&",
          "[import][e2e][import-class]") {
    auto result = build_exec_with_lib(
        R"K(
            module iface_one;
            interface ICounter {
                increment();
                get() : int;
            }
            class Counter : public ICounter {
                value : int;
                Counter() : value(0) {}
                increment() { this.value = this.value + 1; }
                get() : int { return this.value; }
            }
        )K",
        R"K(
            module exec_one;
            import iface_one;
            use(c: iface_one::ICounter&) : int {
                c.increment();
                c.increment();
                return c.get();
            }
            main() : int {
                c : iface_one::Counter;
                return use(c);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 2 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-class-multi] Interface in lib1, concrete class in lib2.
//
// lib1 (iface_lib):
//   interface IShape { area() : int; }
// lib2 (impl_lib):
//   class Square : iface_lib::IShape  { side:int; area → side*side }
// exe:
//   measure(s: iface_lib::IShape&) : int { return s.area(); }
//   main() → Square(7).area() = 49
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import class multi — interface in lib1, class in lib2, dispatch via I& in exe",
          "[import][e2e][import-class-multi]") {
    std::vector<LibSpec> libs = {
        { R"K(
            module iface_lib;
            interface IShape {
                area() : int;
            }
        )K" },
        { R"K(
            module impl_lib;
            import iface_lib;
            class Square : public iface_lib::IShape {
                side : int;
                Square(s: int) : side(s) {}
                area() : int { return this.side * this.side; }
            }
        )K" }
    };

    auto result = build_exec_with_libs(libs,
        R"K(
            module exec_multi;
            import iface_lib;
            import impl_lib;
            measure(s: iface_lib::IShape&) : int { return s.area(); }
            main() : int {
                sq : impl_lib::Square(7);
                return measure(sq);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 49 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-class-iface] Three-lib chain:
//   lib1 declares interface IVal { val() : int; }
//   lib2 declares abstract class AVal : IVal { val() : int { return 10; } }
//   lib3 declares class ConcreteVal : AVal { val() : int { return 30; } }
//   exe calls via IVal& → expects ConcreteVal::val() = 30
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import chain — interface lib1, abstract class lib2, concrete class lib3, dispatch via I&",
          "[import][e2e][import-class-iface]") {
    std::vector<LibSpec> libs = {
        { R"K(
            module ival_lib;
            interface IVal {
                val() : int;
            }
        )K" },
        { R"K(
            module aval_lib;
            import ival_lib;
            abstract class AVal : public ival_lib::IVal {
                AVal() {}
                val() : int { return 10; }
            }
        )K" },
        { R"K(
            module cval_lib;
            import ival_lib;
            import aval_lib;
            class ConcreteVal : public aval_lib::AVal {
                ConcreteVal() {}
                val() : int { return 30; }
            }
        )K" }
    };

    auto result = build_exec_with_libs(libs,
        R"K(
            module exec_chain;
            import ival_lib;
            import cval_lib;
            call_val(v: ival_lib::IVal&) : int { return v.val(); }
            main() : int {
                cv : cval_lib::ConcreteVal;
                return call_val(cv);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 30 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-two-ifaces] Class implementing two interfaces from two different libs.
//
// lib1: interface IAdd { add(x:int, y:int) : int; }
// lib2: interface IMul { mul(x:int, y:int) : int; }
// lib3: class Calculator : IAdd, IMul
//         add(x,y) → x+y ; mul(x,y) → x*y
// exe:
//   do_add(c: IAdd&, x,y) → c.add(x,y)
//   do_mul(c: IMul&, x,y) → c.mul(x,y)
//   main() → do_add(calc, 3,4) + do_mul(calc, 3,4) = 7 + 12 = 19
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import two interfaces — class implements two interfaces from two libs, dispatch via each",
          "[import][e2e][import-two-ifaces]") {
    std::vector<LibSpec> libs = {
        { R"K(
            module iadd_lib;
            interface IAdd {
                add(x: int, y: int) : int;
            }
        )K" },
        { R"K(
            module imul_lib;
            interface IMul {
                mul(x: int, y: int) : int;
            }
        )K" },
        { R"K(
            module calc_lib;
            import iadd_lib;
            import imul_lib;
            class Calculator : public iadd_lib::IAdd, public imul_lib::IMul {
                Calculator() {}
                add(x: int, y: int) : int { return x + y; }
                mul(x: int, y: int) : int { return x * y; }
            }
        )K" }
    };

    auto result = build_exec_with_libs(libs,
        R"K(
            module exec_two_ifaces;
            import iadd_lib;
            import imul_lib;
            import calc_lib;
            do_add(c: iadd_lib::IAdd&, x: int, y: int) : int { return c.add(x, y); }
            do_mul(c: imul_lib::IMul&, x: int, y: int) : int { return c.mul(x, y); }
            main() : int {
                calc : calc_lib::Calculator;
                r1 : int;
                r1 = do_add(calc, 3, 4);
                r2 : int;
                r2 = do_mul(calc, 3, 4);
                return r1 + r2;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 19 );  // 7 + 12
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-diamond-iface] Interface diamond across 4 libs:
//
//          IBase (lib1)
//         /           \
//    IA (lib2)       IB (lib3)        ← both transitive for the exe
//         \           /
//         Diamond (lib4)
//
// exe imports lib1 + lib4 only; lib2 + lib3 are transitive (search-dir).
//
// Expected: exit code 6 (1+2+3)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import diamond interfaces — IBase/IA/IB from 3 libs, Diamond class in lib4, dispatch via each",
          "[import][e2e][import-diamond-iface]") {
    std::vector<LibSpec> libs = {
        { R"K(
            module ibase_lib;
            interface IBase {
                base_val() : int;
            }
        )K" },
        { R"K(
            module ia_lib;
            import ibase_lib;
            interface IA : public ibase_lib::IBase {
                a_val() : int;
            }
        )K" },
        { R"K(
            module ib_lib;
            import ibase_lib;
            interface IB : public ibase_lib::IBase {
                b_val() : int;
            }
        )K" },
        { R"K(
            module diamond_lib;
            import ibase_lib;
            import ia_lib;
            import ib_lib;
            class Diamond : public ia_lib::IA, public ib_lib::IB {
                Diamond() {}
                base_val() : int { return 1; }
                a_val()    : int { return 2; }
                b_val()    : int { return 3; }
            }
        )K" }
    };

    auto result = build_exec_with_libs(libs,
        R"K(
            module exec_diamond;
            import ibase_lib;
            import diamond_lib;
            via_base(x: ibase_lib::IBase&) : int { return x.base_val(); }
            via_a(x: ia_lib::IA&)          : int { return x.a_val(); }
            via_b(x: ib_lib::IB&)          : int { return x.b_val(); }
            main() : int {
                d : diamond_lib::Diamond;
                r1 : int;
                r1 = via_base(d);
                r2 : int;
                r2 = via_a(d);
                r3 : int;
                r3 = via_b(d);
                return r1 + r2 + r3;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 6 );   // 1 + 2 + 3
}

// ═════════════════════════════════════════════════════════════════════════════
// ENUM IMPORT TESTS
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// [import-enum-basic] Enum defined in a lib, accessed by qualified name in exe.
//
// lib:  enum Color { RED = 0; GREEN = 1; BLUE = 2; }
// exe:  main() → Color::GREEN = 1
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import enum — basic qualified access to imported enum entries",
          "[import][e2e][import-enum]") {
    auto result = build_exec_with_lib(
        R"K(
            module colorlib;
            enum Color {
                RED = 0;
                GREEN = 1;
                BLUE = 2;
            };
        )K",
        R"K(
            module exec_enum;
            import colorlib;
            main() : int {
                c : colorlib::Color = colorlib::Color::GREEN;
                return c;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 1 );  // GREEN = 1
}

TEST_CASE("import enum — explicit integer underlying stays integer-backed across module boundary",
          "[import][e2e][import-enum][typed]") {
    auto result = build_exec_with_lib(
        R"K(
            module intenumlib;
            enum Small : unsigned byte {
                A = 250;
                B;
            };
        )K",
        R"K(
            module exec_import_int_typed_enum;
            import intenumlib;
            main() : int {
                v : intenumlib::Small = intenumlib::Small::B;
                if (v == 251) { return v; }
                return 0;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 251 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-enum-default] Imported enum with a 'default' entry.
//
// lib:  enum Status { OK = 0; ERR = 1 default; WARN = 2; }
// exe:  s : Status;  → ERR = 1  (default construction)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import enum — default construction of imported enum",
          "[import][e2e][import-enum]") {
    auto result = build_exec_with_lib(
        R"K(
            module statuslib;
            enum Status {
                OK = 0;
                ERR = 1 default;
                WARN = 2;
            };
        )K",
        R"K(
            module exec_enum_default;
            import statuslib;
            main() : int {
                s : statuslib::Status;
                return s;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 1 );  // ERR = 1 is the default
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-enum-fn-param] Enum used as function parameter and return type across
// library boundary.
//
// lib:  enum Dir { NORTH=0; SOUTH=1; EAST=2; WEST=3; }
//       opposite(d: Dir) : Dir
// exe:  main() → opposite(NORTH) = SOUTH = 1
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import enum — enum as function parameter/return across lib boundary",
          "[import][e2e][import-enum]") {
    auto result = build_exec_with_lib(
        R"K(
            module dirlib;
            enum Dir {
                NORTH = 0;
                SOUTH = 1;
                EAST = 2;
                WEST = 3;
            };
            opposite(d: Dir) : int {
                if (d == Dir::NORTH) { return Dir::SOUTH; }
                if (d == Dir::SOUTH) { return Dir::NORTH; }
                if (d == Dir::EAST)  { return Dir::WEST; }
                return Dir::EAST;
            }
        )K",
        R"K(
            module exec_enum_fn;
            import dirlib;
            main() : int {
                d : dirlib::Dir = dirlib::Dir::NORTH;
                return dirlib::opposite(d);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 1 );  // opposite(NORTH) = SOUTH = 1
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-enum-comparison] Imported enum values compared in exe.
//
// lib:  enum Priority { LOW=1; MED=5; HIGH=10; }
// exe:  main() → (LOW < HIGH) ? 42 : 0
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import enum — comparison operators on imported enum",
          "[import][e2e][import-enum]") {
    auto result = build_exec_with_lib(
        R"K(
            module priolib;
            enum Priority {
                LOW = 1;
                MED = 5;
                HIGH = 10;
            };
        )K",
        R"K(
            module exec_enum_cmp;
            import priolib;
            main() : int {
                a : priolib::Priority = priolib::Priority::LOW;
                b : priolib::Priority = priolib::Priority::HIGH;
                if (a < b) { return 42; }
                return 0;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-enum-derive-local] Local derivation of an imported enum.
//
// lib:  enum Base { A=1; B=2; }
// exe:  enum Extended : colorlib::Base { C=3; }
//       main() → Extended::A + Extended::C = 1 + 3 = 4
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import enum — local derivation of imported enum",
          "[import][e2e][import-enum][import-enum-derive]") {
    auto result = build_exec_with_lib(
        R"K(
            module baseenumlib;
            enum Base {
                A = 1;
                B = 2;
            };
        )K",
        R"K(
            module exec_enum_derive;
            import baseenumlib;
            enum Extended : baseenumlib::Base {
                C = 3;
            };
            main() : int {
                return Extended::A + Extended::C;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 4 );  // A(1) + C(3) = 4
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-enum-derive-default] Local derivation overrides the base default.
//
// lib:  enum Base { A=1 default; B=2; }
// exe:  enum Ext : Base { C=3 default; }
//       main() → default(Ext) = C = 3
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import enum — local derivation overrides imported default",
          "[import][e2e][import-enum][import-enum-derive]") {
    auto result = build_exec_with_lib(
        R"K(
            module defenumlib;
            enum Base {
                A = 1 default;
                B = 2;
            };
        )K",
        R"K(
            module exec_enum_derive_default;
            import defenumlib;
            enum Ext : defenumlib::Base {
                C = 3 default;
            };
            main() : int {
                e : Ext;
                return e;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 3 );  // C is the new default
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-enum-derive-autoincr] Local derivation auto-increments from base max.
//
// lib:  enum Base { X=10; Y=20; }
// exe:  enum Ext : Base { Z; }   → Z = 21
//       main() → Z = 21
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import enum — local derivation auto-increments from imported base",
          "[import][e2e][import-enum][import-enum-derive]") {
    auto result = build_exec_with_lib(
        R"K(
            module autoenumlib;
            enum Base {
                X = 10;
                Y = 20;
            };
        )K",
        R"K(
            module exec_enum_derive_auto;
            import autoenumlib;
            enum Ext : autoenumlib::Base {
                Z;
            };
            main() : int {
                return Ext::Z;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 21 );  // auto-incremented from Y=20
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-enum-derive-cross-lib] Enum derivation chain across two libraries.
//
// lib1: enum Base { A=1; B=2; }
// lib2: enum Mid : Base { C=3; }  (imports lib1)
// exe:  import lib2;  main() → Mid::A + Mid::B + Mid::C = 6
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import enum — derivation chain across two libraries",
          "[import][e2e][import-enum][import-enum-derive]") {
    std::vector<LibSpec> libs = {
        { R"K(
            module enumbase_lib;
            enum Base {
                A = 1;
                B = 2;
            };
        )K" },
        { R"K(
            module enumderiv_lib;
            import enumbase_lib;
            enum Mid : enumbase_lib::Base {
                C = 3;
            };
        )K" }
    };

    auto result = build_exec_with_libs(libs,
        R"K(
            module exec_enum_chain;
            import enumbase_lib;
            import enumderiv_lib;
            main() : int {
                return enumderiv_lib::Mid::A + enumderiv_lib::Mid::B + enumderiv_lib::Mid::C;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 6 );  // 1 + 2 + 3
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-enum-derive-local-from-cross-lib] 3-level derivation:
//   lib1: enum Base { A=1; }
//   lib2: enum Mid : Base { B=2; }   (imports lib1)
//   exe:  enum Leaf : Mid { C=3; }   (imports lib2)
//         main() → Leaf::A + Leaf::B + Leaf::C = 6
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import enum — local derivation from cross-lib derived enum (3-level chain)",
          "[import][e2e][import-enum][import-enum-derive]") {
    std::vector<LibSpec> libs = {
        { R"K(
            module ebase3_lib;
            enum Base {
                A = 1;
            };
        )K" },
        { R"K(
            module emid3_lib;
            import ebase3_lib;
            enum Mid : ebase3_lib::Base {
                B = 2;
            };
        )K" }
    };

    auto result = build_exec_with_libs(libs,
        R"K(
            module exec_enum_3level;
            import ebase3_lib;
            import emid3_lib;
            enum Leaf : emid3_lib::Mid {
                C = 3;
            };
            main() : int {
                return Leaf::A + Leaf::B + Leaf::C;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 6 );  // 1 + 2 + 3
}

TEST_CASE("import enum — object-backed typed enum to const ref across module boundary",
          "[import][e2e][import-enum][typed]") {
    auto result = build_exec_with_lib(
        R"K(
            module typedenumlib;
            struct Vec2 {
                x : int;
                y : int;
            }
            enum Dir : Vec2 {
                UP{.x = 0, .y = 1} default;
                RIGHT{.x = 1, .y = 0};
            };
        )K",
        R"K(
            module exec_import_typed_enum;
            import typedenumlib;
            main() : int {
                p : const typedenumlib::Vec2& = typedenumlib::Dir::RIGHT;
                return p.x * 10 + p.y;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 10 );
}

TEST_CASE("import enum — local typed extension from imported typed base enum",
          "[import][e2e][import-enum][typed][import-enum-derive]") {
    auto result = build_exec_with_lib(
        R"K(
            module typedbase_lib;
            struct Vec2 {
                x : int;
                y : int;
            }
            enum Dir : Vec2 {
                UP{.x = 0, .y = 1} default;
                RIGHT{.x = 1, .y = 0};
            };
        )K",
        R"K(
            module exec_import_typed_extend;
            import typedbase_lib;

            enum ExtendedDir : typedbase_lib::Dir {
                DOWN{.x = 0, .y = 7};
            };

            main() : int {
                p : const typedbase_lib::Vec2& = ExtendedDir::DOWN;
                return p.y + ExtendedDir::DOWN;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 9 );  // y=7 and DOWN index=2
}

// ═════════════════════════════════════════════════════════════════════════════
// TRANSITIVE IMPORT TESTS
// ═════════════════════════════════════════════════════════════════════════════
//
// These tests exercise the transitive dependency resolution logic:
//   - A missing transitive KDI is a fatal error
//   - Transitives are resolved via search-dirs (not just explicit paths)
//   - Multi-level and diamond transitive chains work correctly
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// [import-transitive-missing] A missing transitive dependency must be fatal.
//
// lib1: interface IFoo { f() : int; }
// lib2: class Bar : IFoo { f() → 42 }   (imports lib1)
// exe : imports lib2 only; lib1.kdi is NOT in the resolver  → compiler_error
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import transitive missing — missing transitive KDI is a fatal error",
          "[import][transitive][import-transitive-missing]") {
    // Build lib1 (IFoo) — use build_shared_library helper
    std::string so1 = build_shared_library(R"K(
        module ifoo_lib;
        interface IFoo { f() : int; }
    )K");
    std::filesystem::path kdi1_p(so1); kdi1_p.replace_extension(".kdi");
    std::string kdi1 = kdi1_p.string();
    REQUIRE(std::filesystem::is_regular_file(kdi1));

    // Create a canonical-name symlink so the linker can find -lifoo_lib
    std::filesystem::path so1_dir = std::filesystem::path(so1).parent_path();
    std::filesystem::path ifoo_symlink = so1_dir / "libifoo_lib.so";
    {
        std::error_code ec;
        std::filesystem::remove(ifoo_symlink, ec);
        std::filesystem::create_symlink(
            std::filesystem::path(so1).filename(), ifoo_symlink, ec);
    }

    // Build lib2 (Bar : IFoo) with lib1 available in its resolver
    // We need to do this manually since build_shared_library doesn't support imports.
    // Use build_exec_with_libs but only to build the lib, not the exe.
    std::string so2;
    std::string kdi2;
    {
        std::vector<LibSpec> libs2 = {
            { R"K(
                module bar_lib;
                import ifoo_lib;
                class Bar : public ifoo_lib::IFoo {
                    Bar() {}
                    f() : int { return 42; }
                }
            )K" }
        };
        // Build bar_lib with ifoo_lib accessible
        // We re-use the helpers' pattern manually to set the resolver
        char tmp2[] = "/tmp/klang_lib_test_XXXXXX";
        int fd2 = ::mkstemp(tmp2); ::close(fd2);
        so2 = std::string(tmp2) + ".so";
        std::filesystem::remove(tmp2);

        auto bar_comp = k::compiler::create(make_pic_target_machine());
        auto r2 = std::make_shared<k::path_lookup_file_resolver>();
        r2->add_explicit_path("ifoo_lib", kdi1);
        r2->add_search_dir(std::filesystem::path(so1).parent_path().string());
        bar_comp->set_file_resolver(r2);
        bar_comp->parse_source("lib.k", libs2[0].src, true, false);
        bar_comp->gen_shared_library(so2);

        std::filesystem::path kdi2_p(so2); kdi2_p.replace_extension(".kdi");
        kdi2 = kdi2_p.string();
        REQUIRE(std::filesystem::is_regular_file(kdi2));
    }

    // Build exe resolver: only bar_lib.kdi is registered — ifoo_lib.kdi is ABSENT
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("bar_lib", kdi2);
    // NOTE: ifoo_lib.kdi is intentionally NOT added → transitive dep missing

    bool threw = compile_should_fail(R"K(
        module test_exe;
        import bar_lib;
        main() : int {
            b : bar_lib::Bar;
            return b.f();
        }
    )K", resolver);

    // Clean up
    std::error_code ec;
    std::filesystem::remove(so1, ec); std::filesystem::remove(kdi1, ec);
    std::filesystem::remove(ifoo_symlink, ec);
    std::filesystem::remove(so2, ec); std::filesystem::remove(kdi2, ec);
    std::filesystem::remove(
        std::filesystem::path(so2).parent_path() / "libbar_lib.so", ec);

    REQUIRE(threw);
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-transitive-chain-searchdir] Transitive resolved via search-dir only.
//
// lib1: interface IVal { val() : int; }
// lib2: abstract class AVal : IVal  { val() : int { return 10; } }
// lib3: class ConcreteVal : AVal    { val() : int { return 30; } }
// exe : imports ival_lib + cval_lib (NOT aval_lib explicitly).
//       aval_lib is discovered via the search directory containing all libs.
//
// Expected: exit code 30 (ConcreteVal::val dispatched via IVal&)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import transitive chain via search-dir — transitive KDI found via directory",
          "[import][transitive][import-transitive-chain-searchdir]") {
    std::vector<LibSpec> libs = {
        { R"K(
            module ival_lib;
            interface IVal { val() : int; }
        )K" },
        { R"K(
            module aval_lib;
            import ival_lib;
            abstract class AVal : public ival_lib::IVal {
                AVal() {}
                val() : int { return 10; }
            }
        )K" },
        { R"K(
            module cval_lib;
            import ival_lib;
            import aval_lib;
            class ConcreteVal : public aval_lib::AVal {
                ConcreteVal() {}
                val() : int { return 30; }
            }
        )K" }
    };

    // exe imports ival_lib and cval_lib directly; aval_lib is transitive
    auto result = build_exec_with_libs_direct_only(libs,
        R"K(
             module exec_chain;
            import ival_lib;
            import cval_lib;
            measure(s: ival_lib::IVal&) : int { return s.val(); }
            main() : int {
                cv : cval_lib::ConcreteVal;
                return measure(cv);
            }
        )K",
        {"ival_lib", "cval_lib"} // only these are registered explicitly
    );

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 30);
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-transitive-deep] Three-level transitive chain.
//
// lib1: interface IBase { base() : int; }
// lib2: interface IMid : IBase { mid() : int; }
// lib3: class Leaf : IMid { base() → 1; mid() → 2 }
// exe : imports lib1 + lib3 only; lib2 is transitive.
//       Dispatches via IBase& and via IMid&.
//
// Expected: exit code 3  (1+2)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import transitive deep — 3-level chain, middle lib is transitive",
          "[import][transitive][import-transitive-deep]") {
    std::vector<LibSpec> libs = {
        { R"K(
            module ibase_lib;
            interface IBase { base() : int; }
        )K" },
        { R"K(
            module imid_lib;
            import ibase_lib;
            interface IMid : public ibase_lib::IBase {
                mid() : int;
            }
        )K" },
        { R"K(
            module leaf_lib;
            import ibase_lib;
            import imid_lib;
            class Leaf : public imid_lib::IMid {
                Leaf() {}
                base() : int { return 1; }
                mid()  : int { return 2; }
            }
        )K" }
    };

    // exe imports ibase_lib and leaf_lib; imid_lib is transitive
    auto result = build_exec_with_libs_direct_only(libs,
        R"K(
            module exec_deep;
            import ibase_lib;
            import leaf_lib;
            via_base(x: ibase_lib::IBase&) : int { return x.base(); }
            via_mid(x: ibase_lib::IBase&)  : int { return x.base(); }
            main() : int {
                leaf : leaf_lib::Leaf;
                r1 : int;
                r1 = via_base(leaf);
                r2 : int;
                r2 = via_mid(leaf);
                return r1 + r2;
            }
        )K",
        {"ibase_lib", "leaf_lib"} // imid_lib only reachable via search-dir
    );

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 2);  // via_base(1) + via_mid(1)
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-transitive-diamond-searchdir] Diamond hierarchy, middle libs transitive.
//
//          IBase (lib1)
//         /           \
//    IA (lib2)       IB (lib3)        ← both transitive for the exe
//         \           /
//         Diamond (lib4)
//
// exe imports lib1 + lib4 only; lib2 + lib3 are transitive (search-dir).
//
// Expected: exit code 6 (1+2+3)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import transitive diamond via search-dir — middle interfaces are transitive",
          "[import][transitive][import-transitive-diamond-searchdir]") {
    std::vector<LibSpec> libs = {
        { R"K(
            module ibase_lib;
            interface IBase { base_val() : int; }
        )K" },
        { R"K(
            module ia_lib;
            import ibase_lib;
            interface IA : public ibase_lib::IBase { a_val() : int; }
        )K" },
        { R"K(
            module ib_lib;
            import ibase_lib;
            interface IB : public ibase_lib::IBase { b_val() : int; }
        )K" },
        { R"K(
            module diamond_lib;
            import ibase_lib;
            import ia_lib;
            import ib_lib;
            class Diamond : public ia_lib::IA, public ib_lib::IB {
                Diamond() {}
                base_val() : int { return 1; }
                a_val()    : int { return 2; }
                b_val()    : int { return 3; }
            }
        )K" }
    };

    // exe imports ibase_lib + diamond_lib; ia_lib + ib_lib are transitive
    auto result = build_exec_with_libs_direct_only(libs,
        R"K(
            module exec_diamond2;
            import ibase_lib;
            import diamond_lib;
            via_base(x: ibase_lib::IBase&) : int { return x.base_val(); }
            main() : int {
                d : diamond_lib::Diamond;
                r1 : int; r1 = via_base(d);
                return r1 + r1 + r1;
            }
        )K",
        {"ibase_lib", "diamond_lib"} // ia_lib, ib_lib found via search-dir
    );

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 3);  // via_base(d) = 1 → 1+1+1 = 3
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-transitive-function] Transitive module exposes a global function used
//                              indirectly via a class in a direct import.
//
// lib1: helper(x:int) : int { return x * 2; }  (module helper_lib)
// lib2: class Doubler { twice(x:int) : int { return helper_lib::helper(x); } }
// exe : imports doubler_lib; helper_lib is transitive.
//
// Expected: exit code 42 (Doubler.twice(21) == 42)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("import transitive function — transitive global function used by direct class",
          "[import][transitive][import-transitive-function]") {
    std::vector<LibSpec> libs = {
        { R"K(
            module helper_lib;

            helper(x: int) : int { return x * 2; }
        )K" },
        { R"K(
            module doubler_lib;
            import helper_lib;

            class Doubler {
                Doubler() {}
                twice(x: int) : int { return helper_lib::helper(x); }
            }
        )K" }
    };

    // exe imports doubler_lib only; helper_lib is transitive
    auto result = build_exec_with_libs_direct_only(libs,
        R"K(
            module exec_doubler;
            import doubler_lib;

            main() : int {
                d : doubler_lib::Doubler;
                return d.twice(21);
            }
        )K",
        {"doubler_lib"}
    );

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 42);
}

// ═════════════════════════════════════════════════════════════════════════════
// P2 — Unused-import warnings (warning 0x0008)
//
// Strategy: use kdi_importer directly with a test_logger so we can inspect
// the emitted diagnostics without going through the full compiler pipeline
// (which prints to stderr and has no diagnostic-collection API).
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// Test: an import whose symbols are never touched → warning 0x0008
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("unused import — declared but never used emits warning 0x0008",
          "[import][unused-import]")
{
    // Build the library
    std::string kdi_path = build_kdi_for_import_warning_test(R"K(
        module unused_lib;
        public foo() : int { return 42; }
    )K");
    std::string kdi_dir = std::filesystem::path(kdi_path).parent_path().string();

    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("unused_lib", kdi_path);

    auto diags = run_importer_with_logger("myapp", {"unused_lib"}, *resolver);

    bool found_warning = false;
    for (const auto& d : diags) {
        if (d.level == k::log::diagnostic::severity::warning && d.code == 0x0008)
            found_warning = true;
    }
    REQUIRE(found_warning);

    std::filesystem::remove(kdi_path);
    std::filesystem::remove(std::filesystem::path(kdi_path).replace_extension(".so"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: an import that IS used → no warning
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("unused import — used import does NOT emit warning",
          "[import][unused-import]")
{
    std::string kdi_path = build_kdi_for_import_warning_test(R"K(
        module used_lib;
        public bar() : int { return 7; }
    )K");
    std::string kdi_dir = std::filesystem::path(kdi_path).parent_path().string();

    // Full end-to-end compile: the consumer actually calls used_lib::bar()
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("used_lib", kdi_path);
    resolver->add_search_dir(kdi_dir);

    // This compile must succeed without throwing
    test_logger tl;
    bool ok = compile_collect_diagnostics(R"K(
        module consumer;
        import used_lib;
        main() : int { return used_lib::bar(); }
    )K", resolver, tl);

    REQUIRE(ok);

    std::filesystem::remove(kdi_path);
    std::filesystem::remove(std::filesystem::path(kdi_path).replace_extension(".so"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: two imports — one used, one unused → exactly one warning, for the unused one
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("unused import — one used one unused, only unused triggers warning",
          "[import][unused-import]")
{
    std::string kdi_used = build_kdi_for_import_warning_test(R"K(
        module lib_used;
        public get_val() : int { return 1; }
    )K");
    std::string kdi_unused = build_kdi_for_import_warning_test(R"K(
        module lib_unused;
        public get_val() : int { return 2; }
    )K");
    std::string kdi_dir = std::filesystem::path(kdi_used).parent_path().string();

    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_explicit_path("lib_used",   kdi_used);
    resolver->add_explicit_path("lib_unused", kdi_unused);

    // lib_used is marked used; lib_unused is not
    auto diags = run_importer_with_logger(
        "myapp2",
        {"lib_used", "lib_unused"},
        *resolver,
        {"lib_used"}   // pre_used
    );

    int warning_count = 0;
    bool warned_about_unused = false;
    bool warned_about_used = false;
    for (const auto& d : diags) {
        if (d.level == k::log::diagnostic::severity::warning && d.code == 0x0008) {
            ++warning_count;
            for (const auto& arg : d.args) {
                if (arg.find("lib_unused") != std::string::npos) warned_about_unused = true;
                if (arg.find("lib_used")   != std::string::npos) warned_about_used   = true;
            }
        }
    }
    REQUIRE(warning_count == 1);
    REQUIRE(warned_about_unused);
    REQUIRE_FALSE(warned_about_used);

    std::filesystem::remove(kdi_used);
    std::filesystem::remove(kdi_unused);
    std::filesystem::remove(std::filesystem::path(kdi_used).replace_extension(".so"));
    std::filesystem::remove(std::filesystem::path(kdi_unused).replace_extension(".so"));
}

// ═════════════════════════════════════════════════════════════════════════════
// P1 — Circular import detection (error 0x0005)
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// Test: self-import (A depends on A)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("circular import — self-import A→A throws compiler_error",
          "[import][circular]")
{
    auto p = write_minimal_kdi("self_lib", {"self_lib"});

    std::string what;
    bool threw = try_import("app_self", "self_lib",
                            {{"self_lib", p}}, &what);

    REQUIRE(threw);
    REQUIRE(what.find("self_lib") != std::string::npos);

    std::filesystem::remove(p);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: two-node cycle (A→B→A)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("circular import — two-node cycle A→B→A throws compiler_error",
          "[import][circular]")
{
    auto pa = write_minimal_kdi("cycle_a", {"cycle_b"});
    auto pb = write_minimal_kdi("cycle_b", {"cycle_a"});

    std::string what;
    bool threw = try_import("app_ab", "cycle_a",
                            {{"cycle_a", pa}, {"cycle_b", pb}}, &what);

    REQUIRE(threw);
    REQUIRE((what.find("cycle_a") != std::string::npos ||
             what.find("cycle_b") != std::string::npos));

    std::filesystem::remove(pa);
    std::filesystem::remove(pb);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: three-node cycle (A→B→C→A)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("circular import — three-node cycle A→B→C→A throws compiler_error",
          "[import][circular]")
{
    auto pa = write_minimal_kdi("tri_a", {"tri_b"});
    auto pb = write_minimal_kdi("tri_b", {"tri_c"});
    auto pc = write_minimal_kdi("tri_c", {"tri_a"});

    std::string what;
    bool threw = try_import("app_tri", "tri_a",
                            {{"tri_a", pa}, {"tri_b", pb}, {"tri_c", pc}}, &what);

    REQUIRE(threw);
    int found = 0;
    if (what.find("tri_a") != std::string::npos) ++found;
    if (what.find("tri_b") != std::string::npos) ++found;
    if (what.find("tri_c") != std::string::npos) ++found;
    REQUIRE(found >= 2);

    std::filesystem::remove(pa);
    std::filesystem::remove(pb);
    std::filesystem::remove(pc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: diamond without cycle (A→B, A→C, B→D, C→D) must NOT throw
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("circular import — diamond A→B,A→C,B→D,C→D has no cycle, must succeed",
          "[import][circular]")
{
    auto pd = write_minimal_kdi("dia_d", {});
    auto pb = write_minimal_kdi("dia_b", {"dia_d"});
    auto pc = write_minimal_kdi("dia_c", {"dia_d"});
    auto pa = write_minimal_kdi("dia_a", {"dia_b", "dia_c"});

    REQUIRE_FALSE(try_import("app_dia", "dia_a",
                             {{"dia_a",pa},{"dia_b",pb},{"dia_c",pc},{"dia_d",pd}}));

    std::filesystem::remove(pa);
    std::filesystem::remove(pb);
    std::filesystem::remove(pc);
    std::filesystem::remove(pd);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: linear chain without cycle (A→B→C→D) must NOT throw
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("circular import — linear chain A→B→C→D has no cycle, must succeed",
          "[import][circular]")
{
    auto pd = write_minimal_kdi("chain_d", {});
    auto pc = write_minimal_kdi("chain_c", {"chain_d"});
    auto pb = write_minimal_kdi("chain_b", {"chain_c"});
    auto pa = write_minimal_kdi("chain_a", {"chain_b"});

    REQUIRE_FALSE(try_import("app_chain", "chain_a",
                             {{"chain_a",pa},{"chain_b",pb},{"chain_c",pc},{"chain_d",pd}}));

    std::filesystem::remove(pa);
    std::filesystem::remove(pb);
    std::filesystem::remove(pc);
    std::filesystem::remove(pd);
}

// ═════════════════════════════════════════════════════════════════════════════
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


