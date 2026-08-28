/*
 * K Language compiler
 *
 * Copyright 2023-2026 Emilien Kia
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

/*
 * Callable types behind an alias, a typedef or a parameterised alias.
 *
 * A callable is itself an indirection, so an addresser written after an alias
 * name ('F&') re-addresses the renamed callable in place instead of wrapping
 * it. A soft 'alias' stays fully transparent; a 'typedef' keeps a nominal
 * identity and needs an explicit cast to be fed from the underlying type.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// =============================================================================
// Soft alias
// =============================================================================

TEST_CASE("callable alias — bare prototype re-addressed at the use site",
          "[gen][callable][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_callable_alias_01;
        alias F : (int):bool;
        isPos(x: int) : bool { return x > 0; }
        apply(f: F&, v: int) : bool { return f(v); }
        run() : int {
            f : F& = isPos;
            if (f(3) && apply(f, 7) && !f(-1)) return 42;
            return 1;
        }
    )SRC");
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

TEST_CASE("callable alias — every addresser can be applied to a bare prototype",
          "[gen][callable][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_callable_alias_02;
        alias F : (int):int;
        twice(x: int) : int { return x * 2; }
        run() : int {
            a : F& = twice;
            b : F* = twice;
            c : F+ = twice;
            d : F? = twice;
            e : F! = twice;
            return a(1) + b(2) + c(3) + d(10) + e(5);
        }
    )SRC");
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

TEST_CASE("callable alias — alias of an already addressed callable",
          "[gen][callable][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_callable_alias_03;
        alias P : *(int):int;
        thrice(x: int) : int { return x * 3; }
        apply(p: P, v: int) : int { return p(v); }
        run() : int {
            p : P = thrice;
            return apply(p, 14);
        }
    )SRC");
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

TEST_CASE("callable alias — const alias of a callable", "[gen][callable][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_callable_alias_04;
        alias F : (int):int;
        twice(x: int) : int { return x * 2; }
        run() : int {
            f : const F& = twice;
            return f(21);
        }
    )SRC");
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

TEST_CASE("callable alias — alias over a callable returning a callable",
          "[gen][callable][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_callable_alias_05;
        alias IntFn : *(int):int;
        alias Factory : *():IntFn;
        twice(x: int) : int { return x * 2; }
        make() : IntFn { return twice; }
        run() : int {
            f : Factory = make;
            g : IntFn = f();
            return g(21);
        }
    )SRC");
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

// =============================================================================
// Strong alias (typedef) — nominality
// =============================================================================

TEST_CASE("callable typedef — usable like the renamed callable",
          "[gen][callable][alias][typedef]") {
    auto jit = gen_jit(R"SRC(
        module gen_callable_alias_06;
        typedef G : *(int):bool;
        isPos(x: int) : bool { return x > 0; }
        takes(g: G) : bool { return g(3); }
        run() : int {
            g : G = isPos;
            if (takes(g) && g(1)) return 42;
            return 1;
        }
    )SRC");
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

TEST_CASE("callable typedef — explicit cast from the underlying callable",
          "[gen][callable][alias][typedef]") {
    auto jit = gen_jit(R"SRC(
        module gen_callable_alias_07;
        typedef G : *(int):bool;
        isPos(x: int) : bool { return x > 0; }
        takes(g: G) : bool { return g(3); }
        run() : int {
            f : *(int):bool = isPos;
            return takes((G) f) ? 42 : 1;
        }
    )SRC");
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

TEST_CASE("callable typedef — a soft alias still flows into a typedef parameter",
          "[gen][callable][alias][typedef]") {
    // A soft alias is transparent, so the argument is representationally the
    // underlying callable: it is accepted (with a nominality warning) where the
    // typedef is expected, unlike an unrelated struct type.
    auto jit = gen_jit(R"SRC(
        module gen_callable_alias_08;
        alias F : *(int):bool;
        typedef G : *(int):bool;
        isPos(x: int) : bool { return x > 0; }
        takes(g: G) : bool { return g(3); }
        run() : int {
            f : F = isPos;
            return takes(f) ? 42 : 1;
        }
    )SRC");
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

TEST_CASE("callable typedef — a bare prototype cannot be re-addressed",
          "[gen][callable][alias][typedef]") {
    // A typedef interns exactly one nominal type per declaration, so it cannot
    // gain an addresser at the use site: the declaration itself must carry it.
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_callable_alias_09;
        typedef G : (int):bool;
        isPos(x: int) : bool { return x > 0; }
        run() : int {
            g : G& = isPos;
            return g(1) ? 42 : 1;
        }
    )SRC"), k::log::compiler_error);
}

// =============================================================================
// Parameterised aliases
// =============================================================================

TEST_CASE("callable template alias — Fn<T,R> renames a callable family",
          "[gen][callable][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_callable_alias_10;
        template<typename T, typename R> alias Fn : (T):R;
        isPos(x: int) : bool { return x > 0; }
        apply(f: Fn<int,bool>&, v: int) : bool { return f(v); }
        run() : int {
            f : Fn<int,bool>& = isPos;
            if (f(5) && apply(f, 1)) return 42;
            return 1;
        }
    )SRC");
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

TEST_CASE("callable template alias — chaining Predicate<T> = Fn<T,bool>",
          "[gen][callable][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_callable_alias_11;
        template<typename T, typename R> alias Fn : (T):R;
        template<typename T> alias Predicate : Fn<T,bool>;
        isPos(x: int) : bool { return x > 0; }
        apply(p: Predicate<int>&, v: int) : bool { return p(v); }
        run() : int {
            p : Predicate<int>& = isPos;
            if (p(5) && apply(p, 1) && !p(-3)) return 42;
            return 1;
        }
    )SRC");
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

TEST_CASE("callable template alias — substituted parameter inside a nested type",
          "[gen][callable][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_callable_alias_12;
        template<typename T> alias Consumer : *(T);
        acc : int = 0;
        add(x: int) { acc += x; }
        run() : int {
            c : Consumer<int> = add;
            c(40);
            c(2);
            return acc;
        }
    )SRC");
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

TEST_CASE("callable template alias — addressed target keeps its addresser",
          "[gen][callable][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_callable_alias_13;
        template<typename T, typename R> alias Fn : *(T):R;
        twice(x: int) : int { return x * 2; }
        run() : int {
            f : Fn<int,int> = twice;
            g : Fn<int,int> = null;
            if (g != null) return 1;
            return f(21);
        }
    )SRC");
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

TEST_CASE("callable template alias — a member method behind a template alias",
          "[gen][callable][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_callable_alias_14;
        template<typename T, typename R> alias Fn : *(T):R;
        struct Adder {
            base : int = 40;
            add(x: int) : int { return base + x; }
        }
        run() : int {
            a : Adder;
            f : Fn<int,int> = a.add;
            return f(2);
        }
    )SRC");
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

// =============================================================================
// Mangling stability
// =============================================================================

TEST_CASE("callable alias — mangling is driven by the canonical callable",
          "[gen][callable][alias][mangling]") {
    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
        module gen_callable_alias_15;
        alias F : *(int):bool;
        typedef G : *(int):bool;
        viaAlias(f: F) : bool { return f(1); }
        viaTypedef(g: G) : bool { return g(1); }
        viaRaw(r: *(int):bool) : bool { return r(1); }
    )SRC");

    auto mangled = [&](const std::string& name) {
        auto elems = comp->find_elements(name);
        REQUIRE(elems.size() == 1);
        auto fn = std::dynamic_pointer_cast<k::model::function>(elems.front());
        REQUIRE(fn != nullptr);
        return fn->get_mangled_name();
    };

    // The mangled name embeds the function name, so only the parameter encoding
    // (everything after the name terminator 'E') can be compared across functions.
    auto param_part = [](const std::string& m) {
        auto pos = m.find('E');
        return pos == std::string::npos ? m : m.substr(pos);
    };

    // An alias is a pure renaming: it never reaches the mangled name.
    CHECK(param_part(mangled("viaAlias")) == param_part(mangled("viaRaw")));
    CHECK(param_part(mangled("viaTypedef")) == param_part(mangled("viaRaw")));
    CHECK(mangled("viaRaw") == "_KFN21gen_callable_alias_156viaRawEPFiYbE");
}

TEST_CASE("callable mangling — addresser, return type and throws are all encoded",
          "[gen][callable][mangling]") {
    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
        module gen_callable_alias_16;
        class Err { }
        byPtr(f: *(int):int) : void { }
        byRef(f: &(int):int) : void { }
        byLink(f: +(int):int) : void { }
        byView(f: ?(int):int) : void { }
        otherRet(f: *(int):bool) : void { }
        withThrows(f: *(int):int throws Err) : void { }
    )SRC");

    auto mangled = [&](const std::string& name) {
        auto elems = comp->find_elements(name);
        REQUIRE(elems.size() == 1);
        auto fn = std::dynamic_pointer_cast<k::model::function>(elems.front());
        REQUIRE(fn != nullptr);
        return fn->get_mangled_name();
    };

    std::set<std::string> names{
        mangled("byPtr"), mangled("byRef"), mangled("byLink"), mangled("byView"),
        mangled("otherRet"), mangled("withThrows")};
    // Six distinct signatures must produce six distinct symbols; the function
    // name itself already differs, so compare the parameter encoding instead.
    auto param_part = [](const std::string& m) {
        auto pos = m.find('E');
        return pos == std::string::npos ? m : m.substr(pos);
    };
    std::set<std::string> params{
        param_part(mangled("byPtr")), param_part(mangled("byRef")),
        param_part(mangled("byLink")), param_part(mangled("byView")),
        param_part(mangled("otherRet")), param_part(mangled("withThrows"))};
    CHECK(params.size() == 6u);
}

// =============================================================================
// Known limitation (see TODO.md, "Callable throws clause in a parameter list")
// =============================================================================

// The `throws` list of a callable type specification is parsed greedily, so the
// comma that separates two parameters is swallowed by the exception list and the
// declaration below fails to parse. Until the grammar disambiguates the two
// commas, a callable parameter carrying a `throws` clause must come last.
TEST_CASE("callable throws clause in a parameter list is greedy",
          "[.][gen][callable][throws][parser-limitation]") {
    auto jit = gen_jit(R"SRC(
        module gen_callable_alias_17;
        class Boom { }
        twice(x: int) : int { return x * 2; }
        call(f: *(int):int throws Boom, v: int) : int throws Boom { return f(v); }
        run() : int throws Boom { return call(twice, 21); }
    )SRC");
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}
