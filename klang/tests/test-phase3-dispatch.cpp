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
 * Tests for Phase 3: virtual_dispatch_info annotation in function_invocation_expression.
 *
 * type_reference_resolver now annotates every function_invocation_expression with a
 * virtual_dispatch_info descriptor that records:
 *  - dispatch_kind::DIRECT  — non-virtual call, static call, or qualified bypass
 *  - dispatch_kind::VTABLE  — vtable dispatch, with slot_index and dispatch_class
 *
 * Tests inspect the AST model directly (no JIT) by traversing the unit after a full
 * compile_model() run.
 *
 * Test catalogue:
 *  [A] Free (non-member) function call → DIRECT
 *  [B] Non-virtual struct method call   → DIRECT
 *  [C] Virtual class method call (single inheritance, base ref) → VTABLE, slot_index=0
 *  [D] Qualified call Base::method(d) → DIRECT (bypasses vtable)
 *  [E] Abstract method call via abstract class ref → VTABLE, slot_index correct
 *  [F] Virtual method call, slot_index > 0 (second virtual method in vtable)
 *  [G] Virtual call via secondary base reference (multiple inheritance) → VTABLE
 *  [H] Runtime: virtual dispatch via dispatch_info still produces correct results (JIT)
 */

#include <catch2/catch_all.hpp>

#include "../src/common/logger.hpp"
#include "../src/model/model.hpp"
#include "../src/model/expressions.hpp"
#include "../src/model/statements.hpp"
#include "../src/gen/resolvers.hpp"
#include "../src/gen/generators.hpp"
#include "../src/compiler.hpp"

#include "helpers.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Compile helper
// ─────────────────────────────────────────────────────────────────────────────
static std::shared_ptr<k::compiler> compile_model(std::string_view src) {
    auto comp = k::compiler::create();
    try {
        comp->parse_source("", src, /*optimize=*/false, /*dump=*/false);
        return comp;
    } catch (const k::log::compiler_error&) {
        return nullptr;
    } catch (const std::exception& ex) {
        std::cerr << "Unexpected error: " << ex.what() << std::endl;
        return nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Recursive AST traversal helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace {

// Forward declarations
void collect_in_expr(k::model::expression* expr,
                     std::vector<k::model::function_invocation_expression*>& out);
void collect_in_stmt(k::model::statement* stmt,
                     std::vector<k::model::function_invocation_expression*>& out);

void collect_in_expr(k::model::expression* expr,
                     std::vector<k::model::function_invocation_expression*>& out)
{
    if (!expr) return;
    using namespace k::model;

    if (auto* inv = dynamic_cast<function_invocation_expression*>(expr)) {
        out.push_back(inv);
        // Recurse into callee and arguments
        if (inv->callee_expr()) collect_in_expr(inv->callee_expr().get(), out);
        for (auto& arg : inv->arguments()) collect_in_expr(arg.get(), out);
        return;
    }
    // binary / unary / member / cast expressions — recurse via children
    if (auto* bin = dynamic_cast<binary_expression*>(expr)) {
        collect_in_expr(bin->left().get(), out);
        collect_in_expr(bin->right().get(), out);
        return;
    }
    if (auto* un = dynamic_cast<unary_expression*>(expr)) {
        collect_in_expr(un->sub_expr().get(), out);
        return;
    }
    if (auto* mem = dynamic_cast<member_of_object_expression*>(expr)) {
        collect_in_expr(mem->sub_expr().get(), out);
        return;
    }
    // cast_expression is a unary_expression — handled above already
    // symbol_expression, value_expression — leaf nodes, nothing to recurse into
}

void collect_in_stmt(k::model::statement* stmt,
                     std::vector<k::model::function_invocation_expression*>& out)
{
    if (!stmt) return;
    using namespace k::model;

    if (auto* blk = dynamic_cast<block*>(stmt)) {
        for (auto& s : blk->get_statements()) collect_in_stmt(s.get(), out);
        return;
    }
    if (auto* ret = dynamic_cast<return_statement*>(stmt)) {
        if (ret->get_expression()) collect_in_expr(ret->get_expression().get(), out);
        return;
    }
    if (auto* es = dynamic_cast<expression_statement*>(stmt)) {
        if (es->get_expression()) collect_in_expr(es->get_expression().get(), out);
        return;
    }
    if (auto* vs = dynamic_cast<variable_statement*>(stmt)) {
        // variable_statement inherits variable_definition — check for init expression (constructor call)
        if (auto ctor = std::dynamic_pointer_cast<constructor_invocation_expression>(vs->get_init_expr())) {
            for (auto& arg : ctor->arguments()) collect_in_expr(arg.get(), out);
        }
        return;
    }
    if (auto* ifs = dynamic_cast<if_else_statement*>(stmt)) {
        if (ifs->get_test_expr()) collect_in_expr(ifs->get_test_expr().get(), out);
        if (ifs->get_then_stmt()) collect_in_stmt(ifs->get_then_stmt().get(), out);
        if (ifs->get_else_stmt()) collect_in_stmt(ifs->get_else_stmt().get(), out);
        return;
    }
    if (auto* ws = dynamic_cast<while_statement*>(stmt)) {
        if (ws->get_test_expr()) collect_in_expr(ws->get_test_expr().get(), out);
        if (ws->get_nested_stmt()) collect_in_stmt(ws->get_nested_stmt().get(), out);
        return;
    }
}

} // anonymous namespace

/**
 * Find all function invocations inside a named function within the root namespace.
 */
static std::vector<k::model::function_invocation_expression*>
collect_invocations_in(const std::shared_ptr<k::compiler>& comp, const std::string& func_name) {
    if (!comp || !comp->get_unit()) return {};
    auto root = comp->get_unit()->get_root_namespace();
    if (!root) return {};

    // Find the function by short name
    std::shared_ptr<k::model::function> target_func;
    for (auto& fn : root->functions()) {
        if (fn && fn->get_short_name() == func_name) {
            target_func = fn;
            break;
        }
    }
    if (!target_func || !target_func->get_block()) return {};

    std::vector<k::model::function_invocation_expression*> result;
    collect_in_stmt(target_func->get_block().get(), result);
    return result;
}

using dispatch_kind = k::model::virtual_dispatch_info::dispatch_kind;


// ════════════════════════════════════════════════════════════════════════════
//  [A] Free function call → DIRECT
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[A] Phase3: free function call is annotated DIRECT", "[phase3][dispatch][direct]") {
    auto comp = compile_model(R"SRC(
        module __p3_a__;
        helper() : int { return 42; }
        caller() : int { return helper(); }
    )SRC");
    REQUIRE(comp != nullptr);

    auto invocations = collect_invocations_in(comp, "caller");
    REQUIRE(!invocations.empty());

    // The call to helper() is a free function — must be DIRECT
    bool found = false;
    for (auto* inv : invocations) {
        REQUIRE(inv->has_dispatch_info());
        CHECK(inv->get_dispatch_info().kind == dispatch_kind::DIRECT);
        found = true;
    }
    CHECK(found);
}


// ════════════════════════════════════════════════════════════════════════════
//  [B] Non-virtual struct method call → DIRECT
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[B] Phase3: non-virtual struct method call is annotated DIRECT", "[phase3][dispatch][direct]") {
    auto comp = compile_model(R"SRC(
        module __p3_b__;
        struct Point {
            public x : int;
            get_x() : int { return this.x; }
        }
        test(p : Point&) : int { return p.get_x(); }
    )SRC");
    REQUIRE(comp != nullptr);

    auto invocations = collect_invocations_in(comp, "test");
    REQUIRE(!invocations.empty());

    bool found = false;
    for (auto* inv : invocations) {
        REQUIRE(inv->has_dispatch_info());
        CHECK(inv->get_dispatch_info().kind == dispatch_kind::DIRECT);
        found = true;
    }
    CHECK(found);
}


// ════════════════════════════════════════════════════════════════════════════
//  [C] Virtual class method call via base reference → VTABLE, slot_index=0
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[C] Phase3: virtual call via base ref → VTABLE with slot_index=0", "[phase3][dispatch][vtable]") {
    auto comp = compile_model(R"SRC(
        module __p3_c__;
        class Animal {
            speak() : int { return 0; }
        }
        class Dog : Animal {
            speak() : int { return 7; }
        }
        dispatch(a : Animal&) : int { return a.speak(); }
    )SRC");
    REQUIRE(comp != nullptr);

    auto invocations = collect_invocations_in(comp, "dispatch");
    REQUIRE(!invocations.empty());

    bool found_vtable = false;
    for (auto* inv : invocations) {
        REQUIRE(inv->has_dispatch_info());
        const auto& di = inv->get_dispatch_info();
        if (di.kind == dispatch_kind::VTABLE) {
            found_vtable = true;
            CHECK(di.slot_index == 0);
            REQUIRE(di.dispatch_class != nullptr);
            CHECK(di.dispatch_class->get_short_name() == "Animal");
        }
    }
    CHECK(found_vtable);
}


// ════════════════════════════════════════════════════════════════════════════
//  [D] Qualified call Base::method(d) → DIRECT (bypasses vtable)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[D] Phase3: qualified call bypasses vtable → DIRECT", "[phase3][dispatch][direct]") {
    auto comp = compile_model(R"SRC(
        module __p3_d__;
        class Base {
            value() : int { return 1; }
        }
        class Derived : Base {
            value() : int { return 2; }
        }
        test(d : Derived&) : int {
            return Base::value(d);
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto invocations = collect_invocations_in(comp, "test");
    REQUIRE(!invocations.empty());

    bool found = false;
    for (auto* inv : invocations) {
        REQUIRE(inv->has_dispatch_info());
        CHECK(inv->get_dispatch_info().kind == dispatch_kind::DIRECT);
        found = true;
    }
    CHECK(found);
}


// ════════════════════════════════════════════════════════════════════════════
//  [E] Abstract method call via abstract class reference → VTABLE
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[E] Phase3: abstract method call via base ref → VTABLE", "[phase3][dispatch][vtable][abstract]") {
    auto comp = compile_model(R"SRC(
        module __p3_e__;
        abstract class Shape {
            abstract area() : int;
        }
        class Circle : Shape {
            area() : int { return 314; }
        }
        measure(s : Shape&) : int { return s.area(); }
    )SRC");
    REQUIRE(comp != nullptr);

    auto invocations = collect_invocations_in(comp, "measure");
    REQUIRE(!invocations.empty());

    bool found_vtable = false;
    for (auto* inv : invocations) {
        REQUIRE(inv->has_dispatch_info());
        const auto& di = inv->get_dispatch_info();
        if (di.kind == dispatch_kind::VTABLE) {
            found_vtable = true;
            CHECK(di.slot_index == 0);
            REQUIRE(di.dispatch_class != nullptr);
            CHECK(di.dispatch_class->get_short_name() == "Shape");
        }
    }
    CHECK(found_vtable);
}


// ════════════════════════════════════════════════════════════════════════════
//  [F] Virtual method with slot_index > 0 (second method in vtable)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] Phase3: second virtual method has slot_index=1", "[phase3][dispatch][vtable]") {
    auto comp = compile_model(R"SRC(
        module __p3_f__;
        class Vehicle {
            start() : int { return 1; }
            stop()  : int { return 0; }
        }
        test_stop(v : Vehicle&) : int { return v.stop(); }
        test_start(v : Vehicle&) : int { return v.start(); }
    )SRC");
    REQUIRE(comp != nullptr);

    {
        auto invocations = collect_invocations_in(comp, "test_stop");
        REQUIRE(!invocations.empty());
        bool found = false;
        for (auto* inv : invocations) {
            REQUIRE(inv->has_dispatch_info());
            const auto& di = inv->get_dispatch_info();
            if (di.kind == dispatch_kind::VTABLE) {
                found = true;
                CHECK(di.slot_index == 1);  // stop() is declared second → slot 1
            }
        }
        CHECK(found);
    }
    {
        auto invocations = collect_invocations_in(comp, "test_start");
        REQUIRE(!invocations.empty());
        bool found = false;
        for (auto* inv : invocations) {
            REQUIRE(inv->has_dispatch_info());
            const auto& di = inv->get_dispatch_info();
            if (di.kind == dispatch_kind::VTABLE) {
                found = true;
                CHECK(di.slot_index == 0);  // start() is declared first → slot 0
            }
        }
        CHECK(found);
    }
}


// ════════════════════════════════════════════════════════════════════════════
//  [G] Virtual call via secondary base reference → VTABLE, dispatch_class = secondary base
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[G] Phase3: virtual call via secondary base ref → VTABLE with correct dispatch_class",
          "[phase3][dispatch][vtable][multiple_inheritance]") {
    auto comp = compile_model(R"SRC(
        module __p3_g__;
        class B {
            b_val() : int { return 10; }
        }
        class C {
            c_val() : int { return 20; }
        }
        class D : B, C {}
        dispatch_c(c : C&) : int { return c.c_val(); }
    )SRC");
    REQUIRE(comp != nullptr);

    auto invocations = collect_invocations_in(comp, "dispatch_c");
    REQUIRE(!invocations.empty());

    bool found_vtable = false;
    for (auto* inv : invocations) {
        REQUIRE(inv->has_dispatch_info());
        const auto& di = inv->get_dispatch_info();
        if (di.kind == dispatch_kind::VTABLE) {
            found_vtable = true;
            CHECK(di.slot_index == 0);
            REQUIRE(di.dispatch_class != nullptr);
            // The call is through C& — dispatch_class must be C
            CHECK(di.dispatch_class->get_short_name() == "C");
        }
    }
    CHECK(found_vtable);
}


// ════════════════════════════════════════════════════════════════════════════
//  [H] Runtime JIT: dispatch_info annotation doesn't break existing dispatch
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[H] Phase3: runtime JIT — virtual dispatch via annotated dispatch_info works",
          "[phase3][dispatch][runtime]") {
    auto jit = gen_jit(R"SRC(
        module __p3_h__;
        abstract class Shape {
            abstract area() : int;
        }
        class Square : Shape {
            public side : int;
            Square(s : int) : side(s) {}
            area() : int { return this.side * this.side; }
        }
        measure(s : Shape&) : int { return s.area(); }
        test() : int {
            sq : Square(5);
            return measure(sq);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 25);  // 5 * 5
}








