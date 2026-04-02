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
 * Tests for K language owner ('!') / new / delete dynamic allocation.
 *
 * Tests covered:
 *  - Parser: owner type specifier '!' recognized
 *  - new of a primitive type, owner variable, explicit delete
 *  - new of a primitive type, no delete: scope auto-cleanup
 *  - new twice + delete null owner: no crash
 *  - new struct with ctor/dtor: ctor called on new, dtor+free on delete
 *  - owner auto-destroy at scope exit calls dtor
 *  - owner assigned to observer pointer
 *  - owner move semantics: return, variable init, assignment, null, parameter, chain
 *  - owner → indirection observer (*, ~, ^, &) for primitives and structs
 *  - new/delete of class type (vtable + virtual-dispatch methods)
 *  - owner in nested block scope: dtor at inner scope exit, not outer
 *  - multiple owners at same scope: cleanup in reverse declaration order
 *
 *  Warning 0x5010 — result of 'new' immediately discarded:
 *  - bare new struct expression statement: ctor+dtor both called
 *  - bare new primitive expression statement: no leak
 *  - function returning owner, result discarded: ctor+dtor called
 *  - bare new array expression statement: elements allocated and freed
 *
 *  Error tests:
 *  - Error 0x0117: delete applied to non-owner type
 *  - Error 0x0114: new on abstract class
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Parser tests: owner type specifier
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Parse owner type specifier '!'", "[parser][owner]") {
    SECTION("Variable declaration with owner type and explicit delete") {
        test_logger logger;
        k::parse::parser parser(logger);
        k::source src(R"SRC(
            module __owner_parse__;
            foo() : void {
                x : int! = new int(42);
                delete x;
            }
        )SRC");
        parser.parse(src);
        auto unit = parser.parse_unit();
        REQUIRE(unit);
        REQUIRE_FALSE(logger.has_error());
    }

    SECTION("Owner variable without explicit delete (auto-cleanup)") {
        test_logger logger;
        k::parse::parser parser(logger);
        k::source src(R"SRC(
            module __owner_parse2__;
            foo() : void {
                x : int! = new int(0);
            }
        )SRC");
        parser.parse(src);
        auto unit = parser.parse_unit();
        REQUIRE(unit);
        REQUIRE_FALSE(logger.has_error());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// JIT execution tests
// Symbol name convention: _KFN<namelen><funcname><modulelen><module>Ev
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("new/delete primitive: explicit delete returns value", "[gen][owner][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __own_prim__;
        test_new_delete() : int {
            p : int! = new int(99);
            v : int = *p;
            delete p;
            return v;
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);
    // _KFN12__own_prim__15test_new_deleteEv
    auto fn = jit->lookup_symbol<int(*)()>("_KFN12__own_prim__15test_new_deleteEv");
    REQUIRE(fn);
    REQUIRE(fn() == 99);
}

TEST_CASE("new/delete: scope auto-cleanup (no explicit delete)", "[gen][owner][jit]") {
    auto jit = gen_jit(R"SRC(
        module __own_scope__;
        test_scope_cleanup() : int {
            p : int! = new int(42);
            return *p;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN13__own_scope__18test_scope_cleanupEv");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("delete null owner: no-op, no crash", "[gen][owner][jit]") {
    auto jit = gen_jit(R"SRC(
        module __own_null__;
        test_delete_null() : int {
            p : int! = new int(7);
            delete p;
            delete p;
            return 1;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN12__own_null__16test_delete_nullEv");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("new struct with ctor/dtor: ctor called, dtor+free on delete", "[gen][owner][jit]") {
    auto jit = gen_jit(R"SRC(
        module __own_struct__;

        g_count : int = 0;

        struct Box {
            value : int = 0;
            Box(v: int) {
                value = v;
                g_count = g_count + 1;
            }
            ~Box() {
                g_count = g_count - 1;
            }
        }

        test_box() : int {
            b : Box! = new Box(10);
            sum : int = g_count * 100 + (*b).value;
            delete b;
            return sum * 10 + g_count;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__own_struct__8test_boxEv");
    REQUIRE(fn);
    // g_count=1 when inside, value=10 → sum=110, after delete g_count=0 → result=1100
    REQUIRE(fn() == 1100);
}

TEST_CASE("owner auto-destroy at scope exit calls dtor", "[gen][owner][jit]") {
    auto jit = gen_jit(R"SRC(
        module __own_dtor__;

        g_dtor : int = 0;

        struct Widget {
            Widget() {}
            ~Widget() {
                g_dtor = g_dtor + 1;
            }
        }

        test_auto_dtor() : int {
            w : Widget! = new Widget();
            return 0;
        }

        run() : int {
            test_auto_dtor();
            return g_dtor;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN12__own_dtor__3runEv");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("owner assigned to observer pointer", "[gen][owner][jit]") {
    auto jit = gen_jit(R"SRC(
        module __own_obs__;
        test_observer() : int {
            p : int! = new int(55);
            obs : int* = p;
            return *obs;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN11__own_obs__13test_observerEv");
    REQUIRE(fn);
    REQUIRE(fn() == 55);
}

// =============================================================================
// Phase 6: Owner move semantics — return, variable init, assignment, null
// =============================================================================

// -----------------------------------------------------------------------------
// Owner as function return value: transfers ownership to the caller.
// The callee's local alloca is nulled out; scope cleanup is a no-op.
// -----------------------------------------------------------------------------
TEST_CASE("Owner return value: ownership transferred to caller", "[gen][owner][move][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __own_ret__;

        g_dtors : int = 0;

        struct Box {
            v : int = 0;
            Box(x: int) { v = x; }
            ~Box() { g_dtors = g_dtors + 1; }
        }

        make_box(x: int) : Box! {
            b : Box! = new Box(x);
            return b;       // <-- ownership transfer: b nulled out, no cleanup in callee
        }

        test_ret() : int {
            b : Box! = make_box(42);
            result : int = b->v * 100 + g_dtors;
            delete b;
            return result * 10 + g_dtors;
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN11__own_ret__8test_retEv");
    REQUIRE(fn);
    // Inside test_ret: b->v=42, g_dtors=0 (callee dtor NOT called) → result=4200
    // After delete b: g_dtors=1 → result*10+1 = 42001
    REQUIRE(fn() == 42001);
}

// -----------------------------------------------------------------------------
// Owner variable initialisation from another owner: move semantics.
// Source becomes null after init; scope cleanup of source is a no-op.
// -----------------------------------------------------------------------------
TEST_CASE("Owner variable init from another owner (move)", "[gen][owner][move][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __own_init_move__;

        g_count : int = 0;

        struct Cnt {
            Cnt() { g_count = g_count + 1; }
            ~Cnt() { g_count = g_count - 1; }
        }

        test_init_move() : int {
            a : Cnt! = new Cnt();   // g_count = 1
            b : Cnt! = a;           // move: a becomes null, b owns; g_count still 1
            delete b;               // dtor: g_count = 0
            return g_count;         // a's cleanup: noop (null), so g_count stays 0
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN17__own_init_move__14test_init_moveEv");
    REQUIRE(fn);
    REQUIRE(fn() == 0);
}

// -----------------------------------------------------------------------------
// Owner assignment (a = b): delete old object, transfer ownership from b to a.
// b becomes null; old object of a is destroyed.
// -----------------------------------------------------------------------------
TEST_CASE("Owner assignment: old object deleted, new ownership transferred", "[gen][owner][move][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __own_asgn__;

        g_id : int = 0;
        g_last_dtor : int = 0;

        struct Tagged {
            id : int = 0;
            Tagged(i: int) { id = i; g_id = g_id + 1; }
            ~Tagged() { g_last_dtor = id; g_id = g_id - 1; }
        }

        test_assign() : int {
            a : Tagged! = new Tagged(1);    // a.id=1, g_id=1
            b : Tagged! = new Tagged(2);    // b.id=2, g_id=2
            a = b;                          // delete a(1): g_last_dtor=1, g_id=1; a=b's ptr; b=null
            result : int = a->id * 1000 + g_last_dtor * 10 + g_id;
            delete a;
            return result;
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN12__own_asgn__11test_assignEv");
    REQUIRE(fn);
    // a->id=2, g_last_dtor=1, g_id=1 → result=2000+10+1=2011
    REQUIRE(fn() == 2011);
}

// -----------------------------------------------------------------------------
// Null assignment to owner: delete the current object and set to null.
// -----------------------------------------------------------------------------
TEST_CASE("Null assignment to owner: deletes object", "[gen][owner][move][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __own_null_asgn__;

        g_dtors : int = 0;

        struct Widget {
            Widget() {}
            ~Widget() { g_dtors = g_dtors + 1; }
        }

        test_null_assign() : int {
            w : Widget! = new Widget();
            before : int = g_dtors;     // 0
            w = null;                   // delete w: g_dtors=1; w=null
            after : int = g_dtors;
            return before * 10 + after;
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN17__own_null_asgn__16test_null_assignEv");
    REQUIRE(fn);
    // before=0, after=1 → 0*10+1 = 1
    REQUIRE(fn() == 1);
}

// -----------------------------------------------------------------------------
// Owner as function parameter: ownership transferred into function.
// Callee's scope cleanup runs the dtor; caller's alloca is null after call.
// -----------------------------------------------------------------------------
TEST_CASE("Owner as function parameter: ownership transferred", "[gen][owner][move][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __own_param__;

        g_dtors : int = 0;

        struct Obj {
            Obj() {}
            ~Obj() { g_dtors = g_dtors + 1; }
        }

        consume(o : Obj!) {
            // o is owned here; goes out of scope → dtor called
        }

        test_param() : int {
            o : Obj! = new Obj();
            consume(o);         // ownership transferred; o becomes null in caller
            // no double-free: o is null here, scope cleanup is no-op
            return g_dtors;     // dtor called exactly once (inside consume)
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN13__own_param__10test_paramEv");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

// -----------------------------------------------------------------------------
// Chain: make → pass → consume: dtor called exactly once.
// -----------------------------------------------------------------------------
TEST_CASE("Owner chain: make → pass → consume, dtor called once", "[gen][owner][move][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __own_chain__;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Token {
            Token() { g_ctors = g_ctors + 1; }
            ~Token() { g_dtors = g_dtors + 1; }
        }

        make_token() : Token! {
            return new Token();
        }

        consume_token(t : Token!) {
            // t goes out of scope here
        }

        test_chain() : int {
            consume_token(make_token());
            return g_ctors * 10 + g_dtors;  // 1 ctor, 1 dtor → 11
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN13__own_chain__10test_chainEv");
    REQUIRE(fn);
    REQUIRE(fn() == 11);
}

// -----------------------------------------------------------------------------
// Pointer (*): nullable observer — already the baseline test above.
// This dedicated test exercises read AND write through the pointer.
// -----------------------------------------------------------------------------
TEST_CASE("Owner to pointer observer: read and write", "[gen][owner][indirection][jit]") {
    auto jit = gen_jit(R"SRC(
        module __own_obs_ptr__;
        test_ptr() : int {
            p : int! = new int(10);
            obs : int* = p;
            *obs = 42;
            result : int = *p;
            delete p;
            return result;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN15__own_obs_ptr__8test_ptrEv");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

// -----------------------------------------------------------------------------
// Link (~): mutable, non-null observer.
// Assigning owner to a link transmits the address; write through link
// modifies the owned object.
// -----------------------------------------------------------------------------
TEST_CASE("Owner to link observer: read and write", "[gen][owner][indirection][jit]") {
    auto jit = gen_jit(R"SRC(
        module __own_obs_link__;
        test_link() : int {
            p : int! = new int(7);
            lnk : int+ = p;
            *lnk = 99;
            result : int = *p;
            delete p;
            return result;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN16__own_obs_link__9test_linkEv");
    REQUIRE(fn);
    REQUIRE(fn() == 99);
}

// -----------------------------------------------------------------------------
// Pin (^): immutable, nullable observer.
// Assigning owner to a pin transmits the address; pin is read-only.
// -----------------------------------------------------------------------------
TEST_CASE("Owner to pin observer: read", "[gen][owner][indirection][jit]") {
    auto jit = gen_jit(R"SRC(
        module __own_obs_pin__;
        test_pin() : int {
            p : int! = new int(33);
            pin : int? = p;
            result : int = *pin;
            delete p;
            return result;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN15__own_obs_pin__8test_pinEv");
    REQUIRE(fn);
    REQUIRE(fn() == 33);
}

// -----------------------------------------------------------------------------
// Reference (&): non-null, transparent observer.
// Dereferencing a owner gives a reference; passing that reference to a
// sub-function checks that the address is correctly forwarded.
// -----------------------------------------------------------------------------
TEST_CASE("Owner to reference observer: read", "[gen][owner][indirection][jit]") {
    auto jit = gen_jit(R"SRC(
        module __own_obs_ref__;

        read_ref(r : int&) : int {
            return r;
        }

        test_ref() : int {
            p : int! = new int(77);
            result : int = read_ref(*p);
            delete p;
            return result;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN15__own_obs_ref__8test_refEv");
    REQUIRE(fn);
    REQUIRE(fn() == 77);
}

// =============================================================================
// Owner → indirection observer tests on struct types
// =============================================================================

TEST_CASE("Owner<struct> to pointer observer: read field", "[gen][owner][indirection][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __own_obs_struct_ptr__;

        struct Point {
            x : int = 0;
            y : int = 0;
            Point(ax: int, ay: int) { x = ax; y = ay; }
        }

        test_ptr_struct() : int {
            p : Point! = new Point(3, 4);
            obs : Point* = p;
            result : int = obs->x * 10 + obs->y;
            delete p;
            return result;
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);
    auto fn_ptr = jit->lookup_symbol<int(*)()>("_KFN22__own_obs_struct_ptr__15test_ptr_structEv");
    REQUIRE(fn_ptr);
    CHECK(fn_ptr() == 34);
}

TEST_CASE("Owner<struct> to link observer: read and write field", "[gen][owner][indirection][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __own_obs_struct_lnk__;

        struct Point {
            x : int = 0;
            y : int = 0;
            Point(ax: int, ay: int) { x = ax; y = ay; }
        }

        test_link_struct() : int {
            p : Point! = new Point(5, 6);
            lnk : Point+ = p;
            lnk->x = 9;
            result : int = p->x * 10 + p->y;
            delete p;
            return result;
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);
    auto fn_link = jit->lookup_symbol<int(*)()>("_KFN22__own_obs_struct_lnk__16test_link_structEv");
    REQUIRE(fn_link);
    CHECK(fn_link() == 96);
}

TEST_CASE("Owner<struct> to pin observer: read field", "[gen][owner][indirection][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __own_obs_struct_pin__;

        struct Point {
            x : int = 0;
            y : int = 0;
            Point(ax: int, ay: int) { x = ax; y = ay; }
        }

        test_pin_struct() : int {
            p : Point! = new Point(7, 8);
            pin : Point? = p;
            result : int = pin->x * 10 + pin->y;
            delete p;
            return result;
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);
    auto fn_pin = jit->lookup_symbol<int(*)()>("_KFN22__own_obs_struct_pin__15test_pin_structEv");
    REQUIRE(fn_pin);
    CHECK(fn_pin() == 78);
}

// -----------------------------------------------------------------------------
// Owner keeps ownership after observer assignment:
// Deleting the owner after reading through an observer must not crash.
// -----------------------------------------------------------------------------
TEST_CASE("Owner keeps ownership after observer assignment", "[gen][owner][indirection][jit]") {
    auto jit = gen_jit(R"SRC(
        module __own_keeps_own__;

        g_dtors : int = 0;

        struct Tracked {
            Tracked() {}
            ~Tracked() { g_dtors = g_dtors + 1; }
        }

        test() : int {
            owner : Tracked! = new Tracked();
            obs_ptr  : Tracked* = owner;
            obs_pin  : Tracked? = owner;
            delete owner;
            return g_dtors;
        }
    )SRC");
    REQUIRE(jit);
    std::string mod = "__own_keeps_own__";
    std::string fn  = "test";
    auto sym = "_KFN" + std::to_string(mod.size()) + mod
               + std::to_string(fn.size()) + fn + "Ev";
    auto fn_ptr = jit->lookup_symbol<int(*)()>(sym);
    REQUIRE(fn_ptr);
    // dtor called exactly once (by explicit delete, not by observers)
    REQUIRE(fn_ptr() == 1);
}

// =============================================================================
// Class type with vtable: new/delete
// =============================================================================

// -----------------------------------------------------------------------------
// 'new' of a class type — verifies that the vtable pointer is properly
// initialised by the constructor and that virtual dispatch through '->'
// works correctly on an owned object.  The destructor is also tested.
// -----------------------------------------------------------------------------
TEST_CASE("new/delete of class type: ctor, virtual method, dtor", "[gen][owner][class][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __own_class__;

        g_dtors : int = 0;

        class Counter {
            count : int;
            Counter(n : int) : count(n) {}
            ~Counter() { g_dtors = g_dtors + 1; }
            get() : int { return count; }
        }

        test_class() : int {
            c : Counter! = new Counter(42);
            v : int = c->get();
            delete c;
            return v * 10 + g_dtors;
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);
    // _KFN13__own_class__10test_classEv
    auto fn = jit->lookup_symbol<int(*)()>("_KFN13__own_class__10test_classEv");
    REQUIRE(fn);
    // c->get() == 42, after explicit delete g_dtors == 1 → 42*10+1 = 421
    REQUIRE(fn() == 421);
}

// =============================================================================
// Scope and ordering
// =============================================================================

// -----------------------------------------------------------------------------
// Owner declared inside an inner block '{ ... }' must be destroyed when that
// inner block exits, not when the enclosing function returns.
// -----------------------------------------------------------------------------
TEST_CASE("Owner in nested block: dtor called at inner scope exit", "[gen][owner][scope][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __own_nested_block__;

        g_dtors : int = 0;

        struct Item {
            Item()  {}
            ~Item() { g_dtors = g_dtors + 1; }
        }

        test_nested() : int {
            before : int = g_dtors;   // 0
            {
                w : Item! = new Item();
                // w goes out of scope here → dtor called
            }
            after : int = g_dtors;    // 1
            return before * 10 + after;
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);
    // _KFN20__own_nested_block__11test_nestedEv
    auto fn = jit->lookup_symbol<int(*)()>("_KFN20__own_nested_block__11test_nestedEv");
    REQUIRE(fn);
    // before=0, after=1 → 0*10+1 = 1
    REQUIRE(fn() == 1);
}

// -----------------------------------------------------------------------------
// When multiple owners coexist in the same scope, they must be destroyed in
// reverse declaration order (last declared is first destroyed).
// -----------------------------------------------------------------------------
TEST_CASE("Multiple owners at scope exit: cleanup in reverse declaration order", "[gen][owner][scope][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __own_multi_order__;

        g_order : int = 0;

        struct First  { First()  {} ~First()  { g_order = g_order * 10 + 1; } }
        struct Second { Second() {} ~Second() { g_order = g_order * 10 + 2; } }

        test_order() : int {
            a : First!  = new First();
            b : Second! = new Second();
            return 0;
            // scope exit: b destroyed first -> g_order = 0*10+2 = 2
            //             a destroyed next  -> g_order = 2*10+1 = 21
        }

        run() : int {
            test_order();
            return g_order;   // 21
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);
    // _KFN19__own_multi_order__3runEv
    auto fn = jit->lookup_symbol<int(*)()>("_KFN19__own_multi_order__3runEv");
    REQUIRE(fn);
    // Second destroyed first: g_order = 2; First destroyed next: g_order = 21
    REQUIRE(fn() == 21);
}

// =============================================================================
// Warning 0x5010: result of 'new' immediately discarded
// =============================================================================

// When 'new Foo()' is used as a bare expression statement, the compiler emits
// Warning 0x5010 and immediately destroys the object after construction.
// This test verifies the semantics: ctor is called, then dtor immediately.

TEST_CASE("Bare new expression statement: ctor+dtor both called (Warning 0x5010 semantics)", "[gen][owner][w5010][jit]") {
    auto jit = gen_jit(R"SRC(
        module __own_bare_new__;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Ephemeral {
            Ephemeral() { g_ctors = g_ctors + 1; }
            ~Ephemeral() { g_dtors = g_dtors + 1; }
        }

        test() : int {
            new Ephemeral();   // Warning 0x5010: immediately discarded
            return g_ctors * 10 + g_dtors;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN16__own_bare_new__4testEv");
    REQUIRE(fn);
    // ctor called once, dtor called once (immediately after construction)
    REQUIRE(fn() == 11);
}

TEST_CASE("Bare new primitive expression statement: no leak (Warning 0x5010 semantics)", "[gen][owner][w5010][jit]") {
    auto jit = gen_jit(R"SRC(
        module __own_bare_prim__;
        test() : int {
            new int(42);       // Warning 0x5010: allocated then freed immediately
            return 1;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN17__own_bare_prim__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("Function returning owner, result discarded: ctor+dtor (Warning 0x5010 semantics)", "[gen][owner][w5010][jit]") {
    auto jit = gen_jit(R"SRC(
        module __own_discard_ret__;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Temp {
            Temp() { g_ctors = g_ctors + 1; }
            ~Temp() { g_dtors = g_dtors + 1; }
        }

        make() : Temp! {
            return new Temp();
        }

        test() : int {
            make();            // Warning 0x5010: owner return value discarded
            return g_ctors * 10 + g_dtors;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN19__own_discard_ret__4testEv");
    REQUIRE(fn);
    // ctor called once (in make), dtor called once (at expression statement end)
    REQUIRE(fn() == 11);
}

TEST_CASE("Bare new array expression statement: elements allocated and freed (Warning 0x5010 semantics)", "[gen][owner][w5010][jit]") {
    auto jit = gen_jit(R"SRC(
        module __own_bare_arr__;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Item {
            Item() { g_ctors = g_ctors + 1; }
            ~Item() { g_dtors = g_dtors + 1; }
        }

        test() : int {
            new Item[3]{};     // Warning 0x5010: 3 elements constructed, then all destructed
            return g_ctors * 10 + g_dtors;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN16__own_bare_arr__4testEv");
    REQUIRE(fn);
    // 3 ctors, 3 dtors (all immediate)
    REQUIRE(fn() == 33);
}

// =============================================================================
// Error tests: delete on non-owner, new abstract class
// =============================================================================

TEST_CASE("delete on non-owner type — error 0x0117", "[gen][owner][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __own_err_del_nonowner__;
        test() : int {
            x : int = 42;
            p : int* = &x;
            delete p;
            return 0;
        }
    )SRC"));
}

TEST_CASE("new abstract class — error 0x0114", "[gen][owner][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __own_err_new_abstract__;

        abstract class Shape {
            Shape() {}
            abstract area() : int;
        }

        test() : int {
            s : Shape! = new Shape();
            return s->area();
        }
    )SRC"));
}

