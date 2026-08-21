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
 * Integration tests for aliases, typedefs, callables and cross-module interop.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>

namespace fs = std::filesystem;

namespace {

/// Library exporting a soft alias, two typedefs and a function typed with one
/// of the typedefs.
constexpr const char* ALIAS_LIB_SRC = R"K(
    module aliaslib;

    typedef Identifier : int;
    alias   Num        : int;

    struct Point { x : int; y : int; }
    typedef Coord : Point;

    makeId(v : Identifier) : Identifier { return v; }
)K";

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// An imported soft alias, typedef and typedef-over-struct are all usable as
// types from the importing module.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("import exported aliases and typedefs", "[import][alias][typedef]") {
    auto result = build_exec_with_lib(
        ALIAS_LIB_SRC,
        R"K(
            module app;
            import aliaslib;

            main() : int {
                id : aliaslib::Identifier = 4;
                n  : aliaslib::Num        = 3;
                c  : aliaslib::Coord;
                c.x = 5;
                return aliaslib::makeId(id) + n + c.x;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 12 );
}

// ─────────────────────────────────────────────────────────────────────────────
// An imported typedef keeps its nominal identity: assigning the underlying
// type to it still requires an explicit cast, and the cast is a no-op.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("imported typedef keeps its nominal identity", "[import][typedef]") {
    auto result = build_exec_with_lib(
        ALIAS_LIB_SRC,
        R"K(
            module app;
            import aliaslib;

            main() : int {
                n  : int = 40;
                id : aliaslib::Identifier = (aliaslib::Identifier) n;
                // The reverse direction is implicit.
                back : int = id;
                return back + 2;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// An imported soft alias is fully transparent: it is interchangeable with the
// type it renames, in both directions and without any cast.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("imported soft alias is transparent", "[import][alias]") {
    auto result = build_exec_with_lib(
        ALIAS_LIB_SRC,
        R"K(
            module app;
            import aliaslib;

            twice(v : int) : int { return v * 2; }

            main() : int {
                n : aliaslib::Num = 21;
                m : int = n;
                return twice(m);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Parameterised aliases (template<typename T> alias / typedef) across modules.
//
// A parameterised alias denotes no type by itself, so it is round-tripped
// through KDI as source text and re-parsed by the importing compiler, exactly
// like a template definition. The consumer then substitutes its own arguments.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("cross-module parameterised alias — soft alias of a template aggregate",
          "[import][e2e][alias][template]") {
    auto result = build_exec_with_lib(
        R"K(
            module tpl_alias_lib;

            public:
            template<typename T>
            struct Box {
                v : T;
            }

            template<typename T> alias BoxOf : Box<T>;
            template<typename T> alias Ptr : T*;

            dummy() : int { return 0; }
        )K",
        R"K(
            module tpl_alias_exe;
            import tpl_alias_lib;

            main() : int {
                b : BoxOf<int>;
                b.v = 40;
                p : Ptr<int> = &b.v;
                return *p + 2;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

TEST_CASE("cross-module parameterised alias — strong alias keeps its nominal identity",
          "[import][e2e][typedef][template]") {
    auto result = build_exec_with_lib(
        R"K(
            module tpl_typedef_lib;

            public:
            template<typename T> typedef Id : T;

            dummy() : int { return 0; }
        )K",
        R"K(
            module tpl_typedef_exe;
            import tpl_typedef_lib;

            main() : int {
                a : Id<int> = 40;
                n : int = 2;
                b : Id<int> = (Id<int>)n;
                return (int)a + (int)b;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ═════════════════════════════════════════════════════════════════════════════
// Callable types across module boundaries (KDI export/import)
// ═════════════════════════════════════════════════════════════════════════════

// ─────────────────────────────────────────────────────────────────────────────
// A library exporting a callable-typed global, a callable parameter and a
// callable return type.  All three must round-trip through the .kdi descriptor
// and stay usable (and callable) from the importing executable.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("cross-module callable — global, parameter and return type round-trip",
          "[import][e2e][callable]") {
    auto result = build_exec_with_lib(
        R"K(
            module cbl_lib;

            public isPos(x: int) : bool { return x > 0; }

            public gPred : *(int):bool = isPos;

            public applyPred(p: *(int):bool, v: int) : bool { return p(v); }

            public getPred() : *(int):bool { return isPos; }
        )K",
        R"K(
            module cbl_exe;
            import cbl_lib;

            isNeg(x: int) : bool { return x < 0; }

            main() : int {
                // Callable-typed global imported from the library.
                g : *(int):bool = cbl_lib::gPred;
                if (!g(7)) return 1;
                // Callable return type.
                q : *(int):bool = cbl_lib::getPred();
                if (!q(4)) return 2;
                // Callable parameter, bound to a local function.
                if (!cbl_lib::applyPred(isNeg, -1)) return 3;
                return 42;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// A soft alias over a callable is a pure renaming: the importing module may use
// the alias name or the underlying callable interchangeably.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("cross-module callable — soft alias over a callable is transparent",
          "[import][e2e][callable][alias]") {
    auto result = build_exec_with_lib(
        R"K(
            module cbl_alias_lib;

            public isPos(x: int) : bool { return x > 0; }

            public alias Pred : *(int):bool;

            public applyAlias(p: Pred, v: int) : bool { return p(v); }
        )K",
        R"K(
            module cbl_alias_exe;
            import cbl_alias_lib;

            main() : int {
                p : cbl_alias_lib::Pred = cbl_alias_lib::isPos;
                if (!p(3)) return 1;
                // The alias and the raw callable denote the very same type.
                r : *(int):bool = p;
                if (!r(5)) return 2;
                if (!cbl_alias_lib::applyAlias(cbl_alias_lib::isPos, 9)) return 3;
                return 42;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// A typedef over a callable keeps its nominal identity across the module
// boundary: converting the underlying callable into it requires an explicit cast.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("cross-module callable — typedef over a callable stays nominal",
          "[import][e2e][callable][typedef]") {
    auto result = build_exec_with_lib(
        R"K(
            module cbl_typedef_lib;

            public isPos(x: int) : bool { return x > 0; }

            public typedef StrictPred : *(int):bool;

            public getPred() : *(int):bool { return isPos; }
        )K",
        R"K(
            module cbl_typedef_exe;
            import cbl_typedef_lib;

            main() : int {
                s : cbl_typedef_lib::StrictPred =
                    (cbl_typedef_lib::StrictPred) cbl_typedef_lib::getPred();
                return s(2) ? 42 : 1;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// A parameterised alias over a callable is exported as source text and
// re-materialised on import; instantiating it in the consumer must produce the
// very same callable type as the raw prototype (this is what `k::functional`
// relies on).  Imported parameterised aliases are re-homed under the consumer's
// root namespace, so they are referenced unqualified.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("cross-module callable — parameterised alias over a callable round-trips",
          "[import][e2e][callable][alias][template]") {
    auto result = build_exec_with_lib(
        R"K(
            module cbl_tpl_lib;

            public:
            template<typename T, typename R> alias Fn : *(T):R;
            template<typename T> alias Predicate : Fn<T,bool>;

            isPos(x: int) : bool { return x > 0; }

            applyFn(p: Fn<int,bool>, v: int) : bool { return p(v); }
        )K",
        R"K(
            module cbl_tpl_exe;
            import cbl_tpl_lib;

            isNeg(x: int) : bool { return x < 0; }

            main() : int {
                f : Fn<int,bool> = isNeg;
                if (!f(-5)) return 1;
                // Alias chaining: Predicate<T> renames Fn<T,bool>.
                p : Predicate<int> = cbl_tpl_lib::isPos;
                if (!p(3)) return 2;
                // The instantiated alias is the raw callable type.
                r : *(int):bool = f;
                if (!r(-9)) return 3;
                if (!cbl_tpl_lib::applyFn(cbl_tpl_lib::isPos, 9)) return 4;
                return 42;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

// ─────────────────────────────────────────────────────────────────────────────
// Callable mangling must be stable across the export/import boundary: the
// symbol the consumer emits for a call has to match the one the library
// defined, including the addresser and the declared throws set.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("cross-module callable — mangling stays stable across import",
          "[import][e2e][callable][mangling]") {
    auto result = build_exec_with_lib(
        R"K(
            module cbl_mangle_lib;

            public class Boom { }

            public byPtr(f: *(int):int, v: int) : int { return f(v); }
            public byRef(f: &(int):int, v: int) : int { return f(v); }
            public byLink(f: +(int):int, v: int) : int { return f(v); }
            public byView(f: ?(int):int, v: int) : int { return f(v); }
            // The throws clause of a callable parameter type is greedy, so such a
            // parameter must come last (see TODO.md, "callable throws clause in a
            // parameter list").
            public withThrows(v: int, f: *(int):int throws Boom) : int throws Boom { return f(v); }
        )K",
        R"K(
            module cbl_mangle_exe;
            import cbl_mangle_lib;

            twice(x: int) : int { return x * 2; }

            main() : int {
                acc : int = 0;
                acc += cbl_mangle_lib::byPtr(twice, 1);
                acc += cbl_mangle_lib::byRef(twice, 2);
                acc += cbl_mangle_lib::byLink(twice, 3);
                acc += cbl_mangle_lib::byView(twice, 4);
                try {
                    acc += cbl_mangle_lib::withThrows(11, twice);
                } catch (e : cbl_mangle_lib::Boom&) {
                    return 5;
                }
                return acc;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

TEST_CASE("import cast-operator — explicit cast works on imported class value",
          "[import][e2e][cast][operator]") {
    auto result = build_exec_with_lib(
        R"K(
            module castop_lib;

            public class Wrapper {
                public value : int;
                public Wrapper(v : int) : value(v) {}
                public operator() : int { return value; }
            }

            public make(v : int) : Wrapper {
                return Wrapper(v);
            }
        )K",
        R"K(
            module castop_exe;
            import castop_lib;

            main() : int {
                return (int) castop_lib::make(42);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 42);
}

// ─────────────────────────────────────────────────────────────────────────────
// [import-alias-no-reexport] Regression: Imported aliases from auto-imported `k`
// (e.g. k::functional::Function/Consumer/Predicate) and from explicit dependency
// libraries must not be re-exported into downstream library KDIs, and multiple
// libraries importing them must not produce duplicate alias declarations.
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("import multi-lib — imported aliases do not collide across dependency chain",
          "[import][e2e][alias][regression]") {
    std::vector<LibSpec> libs = {
        { R"K(
            module multi_alias_lib1;
            public template<typename T> alias Pred : *(T):bool;
            public isEven(x: int) : bool { return x % 2 == 0; }
        )K" },
        { R"K(
            module multi_alias_lib2;
            import multi_alias_lib1;
            public checkVal(p: multi_alias_lib1::Pred<int>, v: int) : bool {
                return p(v);
            }
        )K" }
    };

    auto result = build_exec_with_libs(libs,
        R"K(
            module multi_alias_exe;
            import multi_alias_lib1;
            import multi_alias_lib2;

            main() : int {
                if (!multi_alias_lib2::checkVal(multi_alias_lib1::isEven, 42)) return 1;
                if (multi_alias_lib2::checkVal(multi_alias_lib1::isEven, 43)) return 2;
                return 0;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 0);
}

