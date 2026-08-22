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
 * Exhaustive coverage matrix for virtual dispatch in K.
 *
 * This file closes the gaps left by other test files and verifies that
 * virtual dispatch works correctly in ALL combinations of:
 *   - Interface / abstract class / concrete class
 *   - Single inheritance / multiple inheritance / diamond
 *   - Primary base ref / secondary base ref / virtual base ref
 *   - Dispatch via interface / dispatch via class / dispatch via abstract class
 *
 * ════════════════════════════════════════════════════════════════════════════
 *  GAP MATRIX (cross-checked against existing tests)
 * ════════════════════════════════════════════════════════════════════════════
 *
 * [V1]  Two interfaces, dispatch via SECONDARY interface ref (Right&)   [NEW]
 * [V2]  Two interfaces, subclass overrides; dispatch via each ref       [NEW]
 * [V3]  Interface + class in diamond: dispatch via I& on D              [NEW]
 * [V4]  Two interfaces both overriding: two separate vtable slots       [NEW]
 * [V5]  Abstract class with interface: dispatch via A& and via I&       [NEW]
 * [V6]  Interface inherited transitively through diamond: D:B,C, B:A:I [NEW]
 * [V7]  Three-level interface chain: I←A←B←C, dispatch via I&          [NEW]
 * [V8]  Class diamond with two different interface branches             [NEW]
 * [V9]  Sibling override: B overrides A::f(), D:B,C, via C&            [NEW]
 * [V10] Non-diamond multi-inheritance: class+interface combined         [NEW]
 */

#include "../tests/helpers.hpp"
#include <catch2/catch_test_macros.hpp>

// ════════════════════════════════════════════════════════════════════════════
//  [V1] Dispatch via SECONDARY interface reference (Right&)
//  Gap in [J]/[X3]: those tests only verified primary (Left/Readable) dispatch.
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[V1] Two interfaces: dispatch via secondary interface ref Right& works",
          "[interface][dispatch][secondary][coverage]") {
    auto jit = gen_jit(R"SRC(
module gen_virtuality_coverage_01;
interface Left  { left()  : int; }
interface Right { right() : int; }
class Both : public Left, public Right {
    Both() {}
    left()  : int { return 10; }
    right() : int { return 20; }
}
via_left(l:  Left&)  : int { return l.left();  }
via_right(r: Right&) : int { return r.right(); }
test_via_left()  : int { b: Both; return via_left(b);  }
test_via_right() : int { b: Both; return via_right(b); }
)SRC");
    REQUIRE(jit);
    auto fl = jit->lookup_symbol<int(*)()>("test_via_left");
    auto fr = jit->lookup_symbol<int(*)()>("test_via_right");
    REQUIRE(fl); REQUIRE(fr);
    CHECK(fl() == 10);
    CHECK(fr() == 20);
}

// ════════════════════════════════════════════════════════════════════════════
//  [V2] Two interfaces, subclass overrides; dispatch via each ref
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[V2] Two interfaces: subclass overrides both; dispatch via each ref reaches override",
          "[interface][dispatch][secondary][coverage]") {
    auto jit = gen_jit(R"SRC(
module gen_virtuality_coverage_02;
interface Ping { ping() : int; }
interface Pong { pong() : int; }
abstract class Base : public Ping, public Pong {
    ping() : int { return 1; }
    pong() : int { return 2; }
}
class Child : public Base {
    Child() {}
    ping() : int { return 11; }
    pong() : int { return 22; }
}
do_ping(p: Ping&) : int { return p.ping(); }
do_pong(p: Pong&) : int { return p.pong(); }
test_ping() : int { c: Child; return do_ping(c); }
test_pong() : int { c: Child; return do_pong(c); }
)SRC");
    REQUIRE(jit);
    auto fp = jit->lookup_symbol<int(*)()>("test_ping");
    auto fq = jit->lookup_symbol<int(*)()>("test_pong");
    REQUIRE(fp); REQUIRE(fq);
    CHECK(fp() == 11);   // Child::ping via Ping&
    CHECK(fq() == 22);   // Child::pong via Pong&
}

// ════════════════════════════════════════════════════════════════════════════
//  [V3] Interface + class diamond: dispatch via I& on diamond leaf D
//  D:B,C with B:A:I, C:A — A is shared virtual base implementing I
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[V3] Interface+diamond: dispatch via I& on D reaches D::method",
          "[interface][diamond][dispatch][coverage]") {
    auto jit = gen_jit(R"SRC(
module gen_virtuality_coverage_03;
interface I { method() : int; }
abstract class A : public I { method() : int { return 0; } }
abstract class B : public A {}
abstract class C : public A {}
class D : public B, public C {
    D() {}
    method() : int { return 42; }
}
via_i(x: I&) : int { return x.method(); }
test() : int { d: D; return via_i(d); }
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [V4] Two interfaces both providing separate virtual slots
//  Each method must be dispatched to the correct vtable slot
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[V4] Two interfaces: each method dispatches to its own vtable slot independently",
          "[interface][dispatch][multi][coverage]") {
    auto jit = gen_jit(R"SRC(
module gen_virtuality_coverage_04;
interface Shape  { area()      : int; }
interface Greet  { greeting()  : int; }
class Circle : public Shape, public Greet {
    Circle() {}
    area()     : int { return 314; }
    greeting() : int { return 7;   }
}
class Square : public Shape, public Greet {
    Square() {}
    area()     : int { return 100; }
    greeting() : int { return 3;   }
}
get_area(s:  Shape&) : int { return s.area(); }
get_greet(g: Greet&) : int { return g.greeting(); }
test_circle_area()  : int { c: Circle; return get_area(c);  }
test_circle_greet() : int { c: Circle; return get_greet(c); }
test_square_area()  : int { s: Square; return get_area(s);  }
test_square_greet() : int { s: Square; return get_greet(s); }
)SRC");
    REQUIRE(jit);
    auto fca = jit->lookup_symbol<int(*)()>("test_circle_area");
    auto fcg = jit->lookup_symbol<int(*)()>("test_circle_greet");
    auto fsa = jit->lookup_symbol<int(*)()>("test_square_area");
    auto fsg = jit->lookup_symbol<int(*)()>("test_square_greet");
    REQUIRE(fca); REQUIRE(fcg); REQUIRE(fsa); REQUIRE(fsg);
    CHECK(fca() == 314);
    CHECK(fcg() == 7);
    CHECK(fsa() == 100);
    CHECK(fsg() == 3);
}

// ════════════════════════════════════════════════════════════════════════════
//  [V5] Abstract class with interface: dispatch via abstract class ref AND
//       via interface ref both reach the concrete override
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[V5] Abstract class implementing interface: dispatch via A& and via I& both correct",
          "[interface][abstract][dispatch][coverage]") {
    auto jit = gen_jit(R"SRC(
module gen_virtuality_coverage_05;
interface Computable { compute() : int; }
abstract class Engine : public Computable {
    compute() : int { return 0; }
}
class FastEngine : public Engine {
    FastEngine() {}
    compute() : int { return 99; }
}
class SlowEngine : public Engine {
    SlowEngine() {}
    compute() : int { return 1; }
}
run_iface(c:  Computable&) : int { return c.compute(); }
run_engine(e: Engine&)     : int { return e.compute(); }
test_fast_iface()  : int { f: FastEngine; return run_iface(f);  }
test_fast_engine() : int { f: FastEngine; return run_engine(f); }
test_slow_iface()  : int { s: SlowEngine; return run_iface(s);  }
test_slow_engine() : int { s: SlowEngine; return run_engine(s); }
)SRC");
    REQUIRE(jit);
    auto ffi = jit->lookup_symbol<int(*)()>("test_fast_iface");
    auto ffe = jit->lookup_symbol<int(*)()>("test_fast_engine");
    auto fsi = jit->lookup_symbol<int(*)()>("test_slow_iface");
    auto fse = jit->lookup_symbol<int(*)()>("test_slow_engine");
    REQUIRE(ffi); REQUIRE(ffe); REQUIRE(fsi); REQUIRE(fse);
    CHECK(ffi() == 99);   // FastEngine::compute via Computable&
    CHECK(ffe() == 99);   // FastEngine::compute via Engine&
    CHECK(fsi() == 1);    // SlowEngine::compute via Computable&
    CHECK(fse() == 1);    // SlowEngine::compute via Engine&
}

// ════════════════════════════════════════════════════════════════════════════
//  [V6] Interface inherited transitively through diamond
//  A:I, B:A, C:A, D:B,C — D overrides I::method; dispatch via I& on D
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[V6] Interface inherited transitively through class diamond: dispatch via I& reaches D",
          "[interface][diamond][dispatch][transitive][coverage]") {
    auto jit = gen_jit(R"SRC(
module gen_virtuality_coverage_06;
interface I { method() : int; }
abstract class A : public I {
    method() : int { return 0; }
}
abstract class B : public A {}
abstract class C : public A {}
class D : public B, public C {
    D() {}
    method() : int { return 77; }
}
via_i(x: I&) : int { return x.method(); }
via_a(x: A&) : int { return x.method(); }
via_b(x: B&) : int { return x.method(); }
via_c(x: C&) : int { return x.method(); }
test_via_i() : int { d: D; return via_i(d); }
test_via_a() : int { d: D; return via_a(d); }
test_via_b() : int { d: D; return via_b(d); }
test_via_c() : int { d: D; return via_c(d); }
)SRC");
    REQUIRE(jit);
    auto fi = jit->lookup_symbol<int(*)()>("test_via_i");
    auto fa = jit->lookup_symbol<int(*)()>("test_via_a");
    auto fb = jit->lookup_symbol<int(*)()>("test_via_b");
    auto fc = jit->lookup_symbol<int(*)()>("test_via_c");
    REQUIRE(fi); REQUIRE(fa); REQUIRE(fb); REQUIRE(fc);
    CHECK(fi() == 77);   // D::method via I&
    CHECK(fa() == 77);   // D::method via A& (virtual base)
    CHECK(fb() == 77);   // D::method via B& (primary base)
    CHECK(fc() == 77);   // D::method via C& (secondary base, thunk)
}

// ════════════════════════════════════════════════════════════════════════════
//  [V7] Three-level interface chain: I <- A <- B <- C
//  Each level overrides; dispatch via I& on C reaches C's override
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[V7] Three-level interface chain: dispatch via I& always reaches leaf override",
          "[interface][dispatch][multi-level][coverage]") {
    auto jit = gen_jit(R"SRC(
module gen_virtuality_coverage_07;
interface I { val() : int; }
abstract class A : public I { val() : int { return 1; } }
class B : public A {
    B() {}
    val() : int { return 2; }
}
class C : public B {
    C() {}
    val() : int { return 3; }
}
via_i(x: I&) : int { return x.val(); }
via_a(x: A&) : int { return x.val(); }
via_b(x: B&) : int { return x.val(); }
test_b_via_i() : int { b: B; return via_i(b); }
test_b_via_a() : int { b: B; return via_a(b); }
test_b_via_b() : int { b: B; return via_b(b); }
test_c_via_i() : int { c: C; return via_i(c); }
test_c_via_a() : int { c: C; return via_a(c); }
test_c_via_b() : int { c: C; return via_b(c); }
)SRC");
    REQUIRE(jit);
    auto fbi = jit->lookup_symbol<int(*)()>("test_b_via_i");
    auto fba = jit->lookup_symbol<int(*)()>("test_b_via_a");
    auto fbb = jit->lookup_symbol<int(*)()>("test_b_via_b");
    auto fci = jit->lookup_symbol<int(*)()>("test_c_via_i");
    auto fca = jit->lookup_symbol<int(*)()>("test_c_via_a");
    auto fcb = jit->lookup_symbol<int(*)()>("test_c_via_b");
    REQUIRE(fbi); REQUIRE(fba); REQUIRE(fbb);
    REQUIRE(fci); REQUIRE(fca); REQUIRE(fcb);
    CHECK(fbi() == 2);   // B::val via I&
    CHECK(fba() == 2);   // B::val via A&
    CHECK(fbb() == 2);   // B::val via B&
    CHECK(fci() == 3);   // C::val via I&
    CHECK(fca() == 3);   // C::val via A&
    CHECK(fcb() == 3);   // C::val via B&
}

// ════════════════════════════════════════════════════════════════════════════
//  [V8] Diamond with two different interfaces on each branch
//  B:IA, C:IB, D:B,C — D implements both; dispatch via IA& and IB&
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[V8] Diamond with separate interface on each branch: dispatch via each interface ref",
          "[interface][diamond][dispatch][coverage]") {
    auto jit = gen_jit(R"SRC(
module gen_virtuality_coverage_08;
interface IA { fa() : int; }
interface IB { fb() : int; }
abstract class B : public IA { fa() : int { return 1; } }
abstract class C : public IB { fb() : int { return 2; } }
class D : public B, public C {
    D() {}
    fa() : int { return 10; }
    fb() : int { return 20; }
}
via_ia(x: IA&) : int { return x.fa(); }
via_ib(x: IB&) : int { return x.fb(); }
test_via_ia() : int { d: D; return via_ia(d); }
test_via_ib() : int { d: D; return via_ib(d); }
)SRC");
    REQUIRE(jit);
    auto fia = jit->lookup_symbol<int(*)()>("test_via_ia");
    auto fib = jit->lookup_symbol<int(*)()>("test_via_ib");
    REQUIRE(fia); REQUIRE(fib);
    CHECK(fia() == 10);   // D::fa via IA&
    CHECK(fib() == 20);   // D::fb via IB&
}

// ════════════════════════════════════════════════════════════════════════════
//  [V9] Sibling override in diamond: B overrides A::f(), C does not
//  D:B,C — dispatch via B& and via C& must both reach D's override (if D overrides)
//  and B's override (if D does not override)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[V9] Diamond sibling override: D overrides, dispatch via B& and C& both reach D",
          "[class][diamond][dispatch][sibling][coverage]") {
    auto jit = gen_jit(R"SRC(
module gen_virtuality_coverage_09;
class A { f() : int { return 0; } }
class B : public A { f() : int { return 1; } }
class C : public A {}
class D : public B, public C {
    D() {}
    f() : int { return 99; }
}
via_b(x: B&) : int { return x.f(); }
via_c(x: C&) : int { return x.f(); }
via_a(x: A&) : int { return x.f(); }
test_via_b() : int { d: D; return via_b(d); }
test_via_c() : int { d: D; return via_c(d); }
test_via_a() : int { d: D; return via_a(d); }
)SRC");
    REQUIRE(jit);
    auto fb = jit->lookup_symbol<int(*)()>("test_via_b");
    auto fc = jit->lookup_symbol<int(*)()>("test_via_c");
    auto fa = jit->lookup_symbol<int(*)()>("test_via_a");
    REQUIRE(fb); REQUIRE(fc); REQUIRE(fa);
    CHECK(fb() == 99);   // D::f via B& (primary base)
    CHECK(fc() == 99);   // D::f via C& (secondary base, thunk)
    CHECK(fa() == 99);   // D::f via A& (virtual base)
}

TEST_CASE("[V9b] Diamond sibling: D does NOT override, B's override wins through all paths",
          "[class][diamond][dispatch][sibling][coverage]") {
    auto jit = gen_jit(R"SRC(
module gen_virtuality_coverage_10;
class A { f() : int { return 0; } }
class B : public A { f() : int { return 1; } }
class C : public A {}
class D : public B, public C { D() {} }
via_b(x: B&) : int { return x.f(); }
via_c(x: C&) : int { return x.f(); }
via_a(x: A&) : int { return x.f(); }
test_via_b() : int { d: D; return via_b(d); }
test_via_c() : int { d: D; return via_c(d); }
test_via_a() : int { d: D; return via_a(d); }
)SRC");
    REQUIRE(jit);
    auto fb = jit->lookup_symbol<int(*)()>("test_via_b");
    auto fc = jit->lookup_symbol<int(*)()>("test_via_c");
    auto fa = jit->lookup_symbol<int(*)()>("test_via_a");
    REQUIRE(fb); REQUIRE(fc); REQUIRE(fa);
    CHECK(fb() == 1);   // B::f via B& (no D override)
    CHECK(fc() == 1);   // B::f via C& (thunk to B::f)
    CHECK(fa() == 1);   // B::f via A& (virtual base)
}

// ════════════════════════════════════════════════════════════════════════════
//  [V10] Combined: class + interface, non-diamond multi-inheritance
//  Concrete class inherits from abstract class AND interface simultaneously
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[V10] Class + interface combined: dispatch via class ref and via interface ref",
          "[class][interface][dispatch][multi][coverage]") {
    auto jit = gen_jit(R"SRC(
module gen_virtuality_coverage_11;
interface Measurable { measure() : int; }
abstract class Named {
    name() : int { return 0; }
}
class Thing : public Named, public Measurable {
    Thing() {}
    name()    : int { return 5; }
    measure() : int { return 7; }
}
class BigThing : public Thing {
    BigThing() {}
    name()    : int { return 50; }
    measure() : int { return 70; }
}
via_named(n:       Named&)      : int { return n.name();    }
via_meas(m:        Measurable&) : int { return m.measure(); }
test_thing_named()   : int { t: Thing;    return via_named(t); }
test_thing_meas()    : int { t: Thing;    return via_meas(t);  }
test_big_named()     : int { b: BigThing; return via_named(b); }
test_big_meas()      : int { b: BigThing; return via_meas(b);  }
)SRC");
    REQUIRE(jit);
    auto ftn = jit->lookup_symbol<int(*)()>("test_thing_named");
    auto ftm = jit->lookup_symbol<int(*)()>("test_thing_meas");
    auto fbn = jit->lookup_symbol<int(*)()>("test_big_named");
    auto fbm = jit->lookup_symbol<int(*)()>("test_big_meas");
    REQUIRE(ftn); REQUIRE(ftm); REQUIRE(fbn); REQUIRE(fbm);
    CHECK(ftn() == 5);    // Thing::name via Named&
    CHECK(ftm() == 7);    // Thing::measure via Measurable&
    CHECK(fbn() == 50);   // BigThing::name via Named&
    CHECK(fbm() == 70);   // BigThing::measure via Measurable&
}

// ════════════════════════════════════════════════════════════════════════════
//  [V11] Mise a jour du test [X3] existant: dispatch via Right& fonctionne
//  (le commentaire de [X3] disait "not yet implemented")
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[V11] Secondary interface dispatch [X3 updated]: via_right(Both) works",
          "[interface][dispatch][secondary][coverage]") {
    auto jit = gen_jit(R"SRC(
module gen_virtuality_coverage_12;
interface Left  { left()  : int; }
interface Right { right() : int; }
class Both : public Left, public Right {
    Both() {}
    left()  : int { return -1; }
    right() : int { return  1; }
}
class OnlyLeft : public Left {
    OnlyLeft() {}
    left() : int { return -99; }
}
via_left(l:  Left&)  : int { return l.left();  }
via_right(r: Right&) : int { return r.right(); }
test_both_left()    : int { b: Both;     return via_left(b);  }
test_both_right()   : int { b: Both;     return via_right(b); }
test_only_left()    : int { o: OnlyLeft; return via_left(o);  }
)SRC");
    REQUIRE(jit);
    auto fbl = jit->lookup_symbol<int(*)()>("test_both_left");
    auto fbr = jit->lookup_symbol<int(*)()>("test_both_right");
    auto fol = jit->lookup_symbol<int(*)()>("test_only_left");
    REQUIRE(fbl); REQUIRE(fbr); REQUIRE(fol);
    CHECK(fbl() == -1);    // Both::left via Left&
    CHECK(fbr() ==  1);    // Both::right via Right& — secondary vtable thunk
    CHECK(fol() == -99);   // OnlyLeft::left via Left&
}

// ════════════════════════════════════════════════════════════════════════════
//  [V12] Polymorphisme interface complet: plusieurs classes, plusieurs refs
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[V12] Full polymorphism matrix: 3 classes x 2 interfaces",
          "[interface][dispatch][polymorphism][coverage]") {
    auto jit = gen_jit(R"SRC(
module gen_virtuality_coverage_13;
interface Adder  { add(a: int, b: int)  : int; }
interface Multer { mult(a: int, b: int) : int; }
class Plain : public Adder, public Multer {
    Plain() {}
    add(a: int, b: int)  : int { return a + b;     }
    mult(a: int, b: int) : int { return a * b;     }
}
class Shifted : public Adder, public Multer {
    Shifted() {}
    add(a: int, b: int)  : int { return a + b + 1; }
    mult(a: int, b: int) : int { return a * b + 1; }
}
do_add(x:  Adder&,  a: int, b: int) : int { return x.add(a, b);  }
do_mult(x: Multer&, a: int, b: int) : int { return x.mult(a, b); }
test_plain_add()    : int { p: Plain;   return do_add(p,  3, 4); }
test_plain_mult()   : int { p: Plain;   return do_mult(p, 3, 4); }
test_shift_add()    : int { s: Shifted; return do_add(s,  3, 4); }
test_shift_mult()   : int { s: Shifted; return do_mult(s, 3, 4); }
)SRC");
    REQUIRE(jit);
    auto fpa = jit->lookup_symbol<int(*)()>("test_plain_add");
    auto fpm = jit->lookup_symbol<int(*)()>("test_plain_mult");
    auto fsa = jit->lookup_symbol<int(*)()>("test_shift_add");
    auto fsm = jit->lookup_symbol<int(*)()>("test_shift_mult");
    REQUIRE(fpa); REQUIRE(fpm); REQUIRE(fsa); REQUIRE(fsm);
    CHECK(fpa() == 7);    // 3+4
    CHECK(fpm() == 12);   // 3*4
    CHECK(fsa() == 8);    // 3+4+1
    CHECK(fsm() == 13);   // 3*4+1
}

