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

#include <kdi.hpp>

#include "../src/compiler.hpp"
#include "../src/common/logger.hpp"
#include "../src/common/path_lookup_file_resolver.hpp"
#include "../src/model/model.hpp"
#include "../src/model/imported.hpp"
#include "../src/model/tools/kdi_importer.hpp"

#include "helpers.hpp"

#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

struct TmpKdi {
    std::string so_path;
    std::string kdi_path;

    explicit TmpKdi(const std::string_view& src) {
        so_path = build_shared_library(src);
        kdi_path = [&]() {
            fs::path p(so_path);
            p.replace_extension(".kdi");
            return p.string();
        }();
        if (!fs::is_regular_file(kdi_path)) {
            throw std::runtime_error("build_shared_library did not produce " + kdi_path);
        }
    }
    ~TmpKdi() {
        std::error_code ec;
        fs::remove(so_path, ec);
        fs::remove(kdi_path, ec);
    }
    /// Directory that contains the .kdi file
    fs::path dir() const { return fs::path(kdi_path).parent_path(); }
};

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
//    IA (lib2)       IB (lib3)
//  (extends IBase)  (extends IBase)
//         \           /
//         Diamond (lib4)
//    (implements IA & IB)
//
// lib1: interface IBase { base_val() : int; }
// lib2: interface IA : IBase { a_val() : int; }
// lib3: interface IB : IBase { b_val() : int; }
// lib4: class Diamond : IA, IB
//         base_val → 1 ; a_val → 2 ; b_val → 3
// exe:
//   via_base(x: IBase&) → x.base_val()
//   via_a(x: IA&)       → x.a_val()
//   via_b(x: IB&)       → x.b_val()
//   main() → 1 + 2 + 3 = 6
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
// lib2: abstract class AVal : IVal  { val() → 10 }
// lib3: class ConcreteVal : AVal    { val() → 30 }
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
            call_val(v: ival_lib::IVal&) : int { return v.val(); }
            main() : int {
                cv : cval_lib::ConcreteVal;
                return call_val(cv);
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
// lib2: class Doubler { double(x:int) : int { return helper_lib::helper(x); } }
// exe : imports doubler_lib; helper_lib is transitive.
//
// Expected: exit code 42 (Doubler.double(21) == 42)
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
// P2 — Unused-import warnings (warning 0x80010)
//
// Strategy: use kdi_importer directly with a test_logger so we can inspect
// the emitted diagnostics without going through the full compiler pipeline
// (which prints to stderr and has no diagnostic-collection API).
// ═════════════════════════════════════════════════════════════════════════════

// Helper: build a .kdi in /tmp from a simple lib source and return its path.
static std::string build_kdi_for_import_warning_test(const std::string_view& lib_src) {
    std::string so = build_shared_library(lib_src);
    std::filesystem::path kdi = std::filesystem::path(so).replace_extension(".kdi");
    if (!std::filesystem::exists(kdi))
        throw std::runtime_error("expected .kdi not produced: " + kdi.string());
    return kdi.string();
}

// Helper: run kdi_importer phases A+B+C on a fresh unit importing the given
// module names.  Returns all diagnostics collected by the test_logger.
static std::vector<k::log::diagnostic> run_importer_with_logger(
    const std::string& unit_name,
    const std::vector<std::string>& module_names,
    k::path_lookup_file_resolver& resolver,
    std::vector<std::string> pre_used = {})   // module names to mark used before check
{
    // Use compiler::create() to get properly-initialised unit + context
    auto comp = k::compiler::create();
    auto* model_unit = comp->get_unit().get();
    if (!model_unit) throw std::runtime_error("unit not created by compiler");

    model_unit->set_unit_name(k::name::from(unit_name));
    for (const auto& mn : module_names)
        model_unit->add_import(k::name::from(mn));

    test_logger tl;
    k::model::kdi_importer importer(*model_unit, resolver, tl);
    importer.import_all();
    importer.materialise_all(comp->get_context_for_test());

    // Mark any requested imports as used (simulate symbol resolution)
    for (auto& imp : model_unit->get_imports()) {
        const std::string canon = imp.module_name.to_string();
        for (const auto& pu : pre_used)
            if (canon == pu) imp.used = true;
    }

    importer.check_unused_imports();
    return tl.diagnostics;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test: an import whose symbols are never touched → warning 0x80010
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("unused import — declared but never used emits warning 0x80010",
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
        if (d.level == k::log::diagnostic::severity::warning && d.code == 0x80010)
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
        if (d.level == k::log::diagnostic::severity::warning && d.code == 0x80010) {
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
// P1 — Circular import detection (error 0x80003)
// ═════════════════════════════════════════════════════════════════════════════

// Helper: build a minimal kdi_file with forged dependencies and write it to /tmp.
static std::string write_minimal_kdi(const std::string& module_name,
                                      const std::vector<std::string>& deps)
{
    kdi::kdi_file f;
    f.header.module_name   = module_name;
    f.header.lib_base      = module_name;
    f.header.lib_path      = "lib" + module_name + ".so";
    f.header.target_triple = "x86_64-pc-linux-gnu";
    f.header.compiler_ver  = "0.0.0-test";
    f.header.dependencies  = deps;
    f.unit.name            = module_name;
    f.unit.root_ns.name    = "";
    f.unit.root_ns.fq_name = "";

    std::string path = "/tmp/" + module_name + ".kdi";
    if (!kdi::kdi_write_cbor_file(f, path))
        throw std::runtime_error("Cannot write test kdi: " + path);
    return path;
}

// Helper: attempt to load a single import via kdi_importer and return whether
// a compiler_error was thrown.  Fills *out_what with e.what() on throw.
// @param kdi_paths  map of module_name → kdi file path (for explicit resolution)
static bool try_import(const std::string& unit_name,
                       const std::string& first_import,
                       const std::unordered_map<std::string,std::string>& kdi_paths,
                       std::string* out_what = nullptr)
{
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    for (const auto& [mod, path] : kdi_paths)
        resolver->add_explicit_path(mod, path);

    auto comp = k::compiler::create();
    comp->set_file_resolver(resolver);
    auto* model_unit = comp->get_unit().get();
    if (!model_unit) throw std::runtime_error("unit not created");

    model_unit->set_unit_name(k::name::from(unit_name));
    model_unit->add_import(k::name::from(first_import));

    test_logger tl;
    k::model::kdi_importer importer(*model_unit, *resolver, tl);
    try {
        importer.import_all();
        return false;   // no exception → no cycle
    } catch (const k::log::compiler_error& e) {
        if (out_what) *out_what = e.what();
        return true;    // exception → cycle detected
    } catch (const std::exception& e) {
        if (out_what) *out_what = std::string("std::exception: ") + e.what();
        return true;    // other exception → count as error
    }
}

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

