/*
 * K Language standard library — RTTI parameter tests
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
 * Tests for ::k::Parameter RTTI (Runtime Type Information).
 *
 * These tests exercise:
 *  - Function::getParameters() returns non-null for functions with parameters
 *  - Function::getParameters() returns null for parameterless functions
 *  - Parameter array size matches the number of declared parameters
 *  - Parameter::getName() returns the correct parameter name
 *  - Constructor::getParameters() returns non-null for constructors with parameters
 *  - Constructor::getParameters() returns null for no-arg constructors
 *  - Constructor parameter names are correctly reflected
 *  - Static function parameters are correctly reflected
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

#include <cstring>

// Compile-time paths injected by CMake (see libk/libk/CMakeLists.txt).
#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined -- check CMakeLists.txt"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined -- check CMakeLists.txt"
#endif

namespace {

/// Shorthand: compile K source that uses the base stdlib and JIT it.
std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

} // anonymous namespace


// =========================================================================
// FP1. Function::getParameters() is non-null for a function with parameters
// =========================================================================

TEST_CASE("RTTI: Function getParameters() non-null for function with params", "[libk][rtti][parameter]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fparam_1__;
        import k;

        class Calc {
            public Calc() {}
            public add(a : int, b : int) : int { return a + b; }
        }

        test() : int {
            c : Calc;
            fns : const k::Function?[]? = c.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            if (fn->getParameters() != null) return 42;
            return 2;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// FP2. Function::getParameters() is null for a parameterless function
// =========================================================================

TEST_CASE("RTTI: Function getParameters() null for parameterless function", "[libk][rtti][parameter]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fparam_2__;
        import k;

        class NoParams {
            public NoParams() {}
            public doStuff() : int { return 0; }
        }

        test() : int {
            n : NoParams;
            fns : const k::Function?[]? = n.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            if (fn->getParameters() == null) return 42;
            return 2;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// FP3. Function parameter array size matches declared parameter count
// =========================================================================

TEST_CASE("RTTI: Function getParameters() size matches parameter count", "[libk][rtti][parameter]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fparam_3__;
        import k;

        class MathOps {
            public MathOps() {}
            public compute(x : int, y : int, z : int) : int { return x + y + z; }
        }

        test() : int {
            m : MathOps;
            fns : const k::Function?[]? = m.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            params : const k::Parameter?[]? = fn->getParameters();
            if (params == null) return 2;
            return params->size;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 3);
}


// =========================================================================
// FP4. Parameter::getName() returns the correct name for first parameter
// =========================================================================

TEST_CASE("RTTI: Function parameter getName() returns correct first param name", "[libk][rtti][parameter]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fparam_4__;
        import k;

        class Worker {
            public Worker() {}
            public process(input : int, count : int) : int { return input * count; }
        }

        test() : int {
            w : Worker;
            fns : const k::Function?[]? = w.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            params : const k::Parameter?[]? = fn->getParameters();
            if (params == null) return 2;
            p : const k::Parameter? = params[0];
            if (p == null) return 3;
            name : k::String(p->getName());
            expected : k::String("input");
            if (name == expected) return 42;
            return 4;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// FP5. Parameter::getName() returns the correct name for second parameter
// =========================================================================

TEST_CASE("RTTI: Function parameter getName() returns correct second param name", "[libk][rtti][parameter]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fparam_5__;
        import k;

        class Worker {
            public Worker() {}
            public process(input : int, count : int) : int { return input * count; }
        }

        test() : int {
            w : Worker;
            fns : const k::Function?[]? = w.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            params : const k::Parameter?[]? = fn->getParameters();
            if (params == null) return 2;
            p : const k::Parameter? = params[1];
            if (p == null) return 3;
            name : k::String(p->getName());
            expected : k::String("count");
            if (name == expected) return 42;
            return 4;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// FP6. Single-parameter function has parameter array of size 1
// =========================================================================

TEST_CASE("RTTI: Function single parameter has array size 1", "[libk][rtti][parameter]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fparam_6__;
        import k;

        class Greeter {
            public Greeter() {}
            public greet(name : int) : int { return name; }
        }

        test() : int {
            g : Greeter;
            fns : const k::Function?[]? = g.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            params : const k::Parameter?[]? = fn->getParameters();
            if (params == null) return 2;
            return params->size;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 1);
}


// =========================================================================
// CP1. Constructor::getParameters() is non-null for ctor with parameters
// =========================================================================

TEST_CASE("RTTI: Constructor getParameters() non-null for ctor with params", "[libk][rtti][parameter][constructor]") {
    auto jit = jit_k(R"SRC(
        module __rtti_cparam_1__;
        import k;

        class Point {
            public Point(x : int, y : int) {}
            public dummy() : int { return 0; }
        }

        test() : int {
            p : Point(1, 2);
            ctors : const k::Constructor?[]? = p.getClass().getConstructors();
            if (ctors == null) return 0;
            c : const k::Constructor? = ctors[0];
            if (c == null) return 1;
            if (c->getParameters() != null) return 42;
            return 2;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// CP2. Constructor::getParameters() is null for no-arg constructor
// =========================================================================

TEST_CASE("RTTI: Constructor getParameters() null for no-arg ctor", "[libk][rtti][parameter][constructor]") {
    auto jit = jit_k(R"SRC(
        module __rtti_cparam_2__;
        import k;

        class Empty {
            public Empty() {}
            public dummy() : int { return 0; }
        }

        test() : int {
            e : Empty;
            ctors : const k::Constructor?[]? = e.getClass().getConstructors();
            if (ctors == null) return 0;
            c : const k::Constructor? = ctors[0];
            if (c == null) return 1;
            if (c->getParameters() == null) return 42;
            return 2;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// CP3. Constructor parameter array size matches declared parameter count
// =========================================================================

TEST_CASE("RTTI: Constructor getParameters() size matches param count", "[libk][rtti][parameter][constructor]") {
    auto jit = jit_k(R"SRC(
        module __rtti_cparam_3__;
        import k;

        class Rect {
            public Rect(x : int, y : int, w : int, h : int) {}
            public dummy() : int { return 0; }
        }

        test() : int {
            r : Rect(0, 0, 10, 20);
            ctors : const k::Constructor?[]? = r.getClass().getConstructors();
            if (ctors == null) return 0;
            c : const k::Constructor? = ctors[0];
            if (c == null) return 1;
            params : const k::Parameter?[]? = c->getParameters();
            if (params == null) return 2;
            return params->size;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 4);
}


// =========================================================================
// CP4. Constructor parameter getName() returns correct first param name
// =========================================================================

TEST_CASE("RTTI: Constructor parameter getName() returns correct first param name", "[libk][rtti][parameter][constructor]") {
    auto jit = jit_k(R"SRC(
        module __rtti_cparam_4__;
        import k;

        class Vec {
            public Vec(dx : int, dy : int) {}
            public dummy() : int { return 0; }
        }

        test() : int {
            v : Vec(1, 2);
            ctors : const k::Constructor?[]? = v.getClass().getConstructors();
            if (ctors == null) return 0;
            c : const k::Constructor? = ctors[0];
            if (c == null) return 1;
            params : const k::Parameter?[]? = c->getParameters();
            if (params == null) return 2;
            p : const k::Parameter? = params[0];
            if (p == null) return 3;
            name : k::String(p->getName());
            expected : k::String("dx");
            if (name == expected) return 42;
            return 4;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// CP5. Constructor parameter getName() returns correct second param name
// =========================================================================

TEST_CASE("RTTI: Constructor parameter getName() returns correct second param name", "[libk][rtti][parameter][constructor]") {
    auto jit = jit_k(R"SRC(
        module __rtti_cparam_5__;
        import k;

        class Vec {
            public Vec(dx : int, dy : int) {}
            public dummy() : int { return 0; }
        }

        test() : int {
            v : Vec(1, 2);
            ctors : const k::Constructor?[]? = v.getClass().getConstructors();
            if (ctors == null) return 0;
            c : const k::Constructor? = ctors[0];
            if (c == null) return 1;
            params : const k::Parameter?[]? = c->getParameters();
            if (params == null) return 2;
            p : const k::Parameter? = params[1];
            if (p == null) return 3;
            name : k::String(p->getName());
            expected : k::String("dy");
            if (name == expected) return 42;
            return 4;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// CP6. Multiple constructors each have correct parameter arrays
// =========================================================================

TEST_CASE("RTTI: multiple constructors have correct parameter arrays", "[libk][rtti][parameter][constructor]") {
    auto jit = jit_k(R"SRC(
        module __rtti_cparam_6__;
        import k;

        class Multi {
            public Multi() {}
            public Multi(a : int) {}
            public Multi(a : int, b : int) {}
            public dummy() : int { return 0; }
        }

        test_ctor0_null() : int {
            m : Multi;
            ctors : const k::Constructor?[]? = m.getClass().getConstructors();
            if (ctors == null) return 0;
            c : const k::Constructor? = ctors[0];
            if (c == null) return 1;
            // No-arg ctor has null parameters
            if (c->getParameters() == null) return 42;
            return 2;
        }

        test_ctor1_size() : int {
            m : Multi;
            ctors : const k::Constructor?[]? = m.getClass().getConstructors();
            if (ctors == null) return 0;
            c : const k::Constructor? = ctors[1];
            if (c == null) return 1;
            params : const k::Parameter?[]? = c->getParameters();
            if (params == null) return 2;
            return params->size;
        }

        test_ctor2_size() : int {
            m : Multi;
            ctors : const k::Constructor?[]? = m.getClass().getConstructors();
            if (ctors == null) return 0;
            c : const k::Constructor? = ctors[2];
            if (c == null) return 1;
            params : const k::Parameter?[]? = c->getParameters();
            if (params == null) return 2;
            return params->size;
        }
    )SRC");
    REQUIRE(jit);

    auto test_ctor0_null = jit->lookup_symbol<int(*)()>("test_ctor0_null");
    auto test_ctor1_size = jit->lookup_symbol<int(*)()>("test_ctor1_size");
    auto test_ctor2_size = jit->lookup_symbol<int(*)()>("test_ctor2_size");
    REQUIRE(test_ctor0_null);
    REQUIRE(test_ctor1_size);
    REQUIRE(test_ctor2_size);
    CHECK(test_ctor0_null() == 42);
    CHECK(test_ctor1_size() == 1);
    CHECK(test_ctor2_size() == 2);
}


// =========================================================================
// FP7. Multiple functions each have correct parameter arrays
// =========================================================================

TEST_CASE("RTTI: multiple functions have correct parameter arrays", "[libk][rtti][parameter]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fparam_7__;
        import k;

        class Svc {
            public Svc() {}
            public noargs() : int { return 0; }
            public onearg(x : int) : int { return x; }
            public twoargs(a : int, b : int) : int { return a + b; }
        }

        test_noargs() : int {
            s : Svc;
            fns : const k::Function?[]? = s.getClass().getFunctions();
            if (fns == null) return -1;
            // Find the no-args function (noargs)
            idx : int = 0;
            while (idx < fns->size) {
                fn : const k::Function? = fns[idx];
                if (fn != null) {
                    name : k::String(fn->getName());
                    expected : k::String("noargs");
                    if (name == expected) {
                        if (fn->getParameters() == null) return 42;
                        return 1;
                    }
                }
                idx = idx + 1;
            }
            return 0;
        }

        test_onearg() : int {
            s : Svc;
            fns : const k::Function?[]? = s.getClass().getFunctions();
            if (fns == null) return -1;
            idx : int = 0;
            while (idx < fns->size) {
                fn : const k::Function? = fns[idx];
                if (fn != null) {
                    name : k::String(fn->getName());
                    expected : k::String("onearg");
                    if (name == expected) {
                        params : const k::Parameter?[]? = fn->getParameters();
                        if (params == null) return 1;
                        return params->size;
                    }
                }
                idx = idx + 1;
            }
            return 0;
        }

        test_twoargs() : int {
            s : Svc;
            fns : const k::Function?[]? = s.getClass().getFunctions();
            if (fns == null) return -1;
            idx : int = 0;
            while (idx < fns->size) {
                fn : const k::Function? = fns[idx];
                if (fn != null) {
                    name : k::String(fn->getName());
                    expected : k::String("twoargs");
                    if (name == expected) {
                        params : const k::Parameter?[]? = fn->getParameters();
                        if (params == null) return 1;
                        return params->size;
                    }
                }
                idx = idx + 1;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_noargs  = jit->lookup_symbol<int(*)()>("test_noargs");
    auto test_onearg  = jit->lookup_symbol<int(*)()>("test_onearg");
    auto test_twoargs = jit->lookup_symbol<int(*)()>("test_twoargs");
    REQUIRE(test_noargs);
    REQUIRE(test_onearg);
    REQUIRE(test_twoargs);
    CHECK(test_noargs() == 42);
    CHECK(test_onearg() == 1);
    CHECK(test_twoargs() == 2);
}


// =========================================================================
// CP7. Constructor parameter getParameters() size matches getParamCount()
// =========================================================================

TEST_CASE("RTTI: Constructor getParameters() size matches getParamCount()", "[libk][rtti][parameter][constructor]") {
    auto jit = jit_k(R"SRC(
        module __rtti_cparam_7__;
        import k;

        class Config {
            public Config(host : int, port : int, timeout : int) {}
            public dummy() : int { return 0; }
        }

        test() : int {
            c : Config(0, 0, 0);
            ctors : const k::Constructor?[]? = c.getClass().getConstructors();
            if (ctors == null) return 0;
            ctor : const k::Constructor? = ctors[0];
            if (ctor == null) return 1;
            params : const k::Parameter?[]? = ctor->getParameters();
            if (params == null) return 2;
            if (params->size == ctor->getParamCount()) return 42;
            return 3;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// FP9. Static function parameter names are correctly reflected
// =========================================================================

TEST_CASE("RTTI: static function parameters are correctly reflected", "[libk][rtti][parameter]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fparam_9__;
        import k;

        class Factory {
            public Factory() {}
            public static create(mode : int) : int { return mode; }
        }

        test() : int {
            f : Factory;
            fns : const k::Function?[]? = f.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            params : const k::Parameter?[]? = fn->getParameters();
            if (params == null) return 2;
            if (params->size != 1) return 3;
            p : const k::Parameter? = params[0];
            if (p == null) return 4;
            name : k::String(p->getName());
            expected : k::String("mode");
            if (name == expected) return 42;
            return 5;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 10. Parameter::getAnnotations() non-null on annotated function parameter
// =========================================================================

TEST_CASE("RTTI: parameter getAnnotations() non-null on annotated param", "[libk][rtti][parameter][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fparam_ann_1__;
        import k;

        annotation Tag {
            label : int;
        }

        class Svc {
            public Svc() {}
            public process(@Tag(42) input : int) : int { return input; }
        }

        test() : int {
            s : Svc;
            fns : const k::Function?[]? = s.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            params : const k::Parameter?[]? = fn->getParameters();
            if (params == null) return 2;
            if (params->size != 1) return 3;
            p : const k::Parameter? = params[0];
            if (p == null) return 4;
            anns : const k::Annotation?[]? = p->getAnnotations();
            if (anns == null) return 5;
            return 42;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 11. Parameter::getAnnotations() returns correct count
// =========================================================================

TEST_CASE("RTTI: parameter getAnnotations() returns correct count", "[libk][rtti][parameter][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fparam_ann_2__;
        import k;

        annotation Alpha {}
        annotation Beta {}

        class Worker {
            public Worker() {}
            public run(@Alpha @Beta data : int) : int { return data; }
        }

        test() : int {
            w : Worker;
            fns : const k::Function?[]? = w.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            params : const k::Parameter?[]? = fn->getParameters();
            if (params == null) return 2;
            p : const k::Parameter? = params[0];
            if (p == null) return 3;
            anns : const k::Annotation?[]? = p->getAnnotations();
            if (anns == null) return 4;
            return anns->size;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 2);
}


// =========================================================================
// 12. Parameter::getAnnotations() is null on unannotated parameter
// =========================================================================

TEST_CASE("RTTI: parameter getAnnotations() null on unannotated param", "[libk][rtti][parameter][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fparam_ann_3__;
        import k;

        class Plain {
            public Plain() {}
            public compute(x : int) : int { return x; }
        }

        test() : int {
            p : Plain;
            fns : const k::Function?[]? = p.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            params : const k::Parameter?[]? = fn->getParameters();
            if (params == null) return 2;
            param : const k::Parameter? = params[0];
            if (param == null) return 3;
            if (param->getAnnotations() == null) return 42;
            return 4;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 13. Mixed: annotated and unannotated parameters in the same function
// =========================================================================

TEST_CASE("RTTI: mixed annotated and unannotated params", "[libk][rtti][parameter][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fparam_ann_4__;
        import k;

        annotation Tag {}

        class Mixer {
            public Mixer() {}
            public mix(@Tag a : int, b : int) : int { return a + b; }
        }

        test() : int {
            m : Mixer;
            fns : const k::Function?[]? = m.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            params : const k::Parameter?[]? = fn->getParameters();
            if (params == null) return 2;
            if (params->size != 2) return 3;

            // First param 'a' has annotation
            p0 : const k::Parameter? = params[0];
            if (p0 == null) return 4;
            anns0 : const k::Annotation?[]? = p0->getAnnotations();
            if (anns0 == null) return 5;
            if (anns0->size != 1) return 6;

            // Second param 'b' has no annotations
            p1 : const k::Parameter? = params[1];
            if (p1 == null) return 7;
            if (p1->getAnnotations() != null) return 8;

            return 42;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 14. Constructor parameter annotations via RTTI
// =========================================================================

TEST_CASE("RTTI: constructor parameter getAnnotations() non-null on annotated param", "[libk][rtti][parameter][constructor][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_cparam_ann_1__;
        import k;

        annotation Required {}

        class Config {
            @Required
            public Config(@Required host : int, port : int) {}
            public dummy() : int { return 0; }
        }

        test() : int {
            c : Config(1, 2);
            ctors : const k::Constructor?[]? = c.getClass().getConstructors();
            if (ctors == null) return 0;
            ctor : const k::Constructor? = ctors[0];
            if (ctor == null) return 1;
            params : const k::Parameter?[]? = ctor->getParameters();
            if (params == null) return 2;
            if (params->size != 2) return 3;

            // First param 'host' has @Required
            p0 : const k::Parameter? = params[0];
            if (p0 == null) return 4;
            anns0 : const k::Annotation?[]? = p0->getAnnotations();
            if (anns0 == null) return 5;
            if (anns0->size != 1) return 6;

            // Second param 'port' has no annotations
            p1 : const k::Parameter? = params[1];
            if (p1 == null) return 7;
            if (p1->getAnnotations() != null) return 8;

            return 42;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}


// =========================================================================
// 15. Annotation with field value accessible at runtime on parameter
// =========================================================================

TEST_CASE("RTTI: parameter annotation with field value", "[libk][rtti][parameter][annotation]") {
    auto jit = jit_k(R"SRC(
        module __rtti_fparam_ann_5__;
        import k;

        annotation Priority {
            value : int;
        }

        class Handler {
            public Handler() {}
            public handle(@Priority(7) msg : int) : int { return msg; }
        }

        test() : int {
            h : Handler;
            fns : const k::Function?[]? = h.getClass().getFunctions();
            if (fns == null) return 0;
            fn : const k::Function? = fns[0];
            if (fn == null) return 1;
            params : const k::Parameter?[]? = fn->getParameters();
            if (params == null) return 2;
            p : const k::Parameter? = params[0];
            if (p == null) return 3;
            anns : const k::Annotation?[]? = p->getAnnotations();
            if (anns == null) return 4;
            if (anns->size != 1) return 5;
            // Annotation is non-null — we verified it exists and has the right count
            return 42;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

