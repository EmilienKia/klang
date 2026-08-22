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
 * Tests for complex multi-diamond inheritance:
 *
 *   Diamant 1 — Shape/Draw/Color
 *   ─────────────────────────────
 *   interface Shape      { draw()  : int; }
 *   abstract Drawable  : Shape   { draw()  = 1  }
 *   interface Colorable          { color() = 2  }
 *   class ColoredShape : Drawable, Colorable { draw()=3, color()=5 }
 *
 *   Diamant 2 — Node/Value
 *   ────────────────────────
 *   interface Node               { value() : int; }
 *   class IntNode    : Node      { value() = 10  }
 *   abstract NamedNode : Node    { value() = 20  }  (abstract)
 *   class IntNamedNode : IntNode, NamedNode { value() = 30 }
 *
 *   Fusion — Everything
 *   ─────────────────────
 *   class Everything : ColoredShape, IntNamedNode
 *       { draw()=100, color()=101, value()=102 }
 *
 * Les deux premiers groupes vérifient chaque diamant isolément.
 * Le troisième groupe fusionne les deux en une seule classe et vérifie
 * le dispatch via toutes les références de la hiérarchie.
 */

#include "helpers.hpp"
#include <catch2/catch_test_macros.hpp>

// ════════════════════════════════════════════════════════════════════════════
//  Diamant 1 : Shape → Drawable + Colorable → ColoredShape
//
//  Shape (interface)
//    └─ Drawable (abstract class) : draw() = 1
//    └─ Colorable (interface)     : color() = 2
//         └─ ColoredShape : Drawable, Colorable
//                draw()  = 3
//                color() = 5
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[MD1] Diamond 1: ColoredShape dispatch via Shape&, Drawable&, Colorable&",
          "[multi-diamond][diamond][dispatch]") {

    auto jit = gen_jit(R"SRC(
module gen_multi_diamond_01;
interface Shape    { draw()  : int; }
interface Colorable { color() : int; }
abstract class Drawable : public Shape {
    draw() : int { return 1; }
}
class ColoredShape : public Drawable, public Colorable {
    ColoredShape() {}
    draw()  : int { return 3; }
    color() : int { return 5; }
}
via_shape(s:     Shape&)    : int { return s.draw();  }
via_drawable(d:  Drawable&) : int { return d.draw();  }
via_colorable(c: Colorable&): int { return c.color(); }
test_shape()     : int { cs: ColoredShape; return via_shape(cs);     }
test_drawable()  : int { cs: ColoredShape; return via_drawable(cs);  }
test_colorable() : int { cs: ColoredShape; return via_colorable(cs); }
test_direct_draw()  : int { cs: ColoredShape; return cs.draw();  }
test_direct_color() : int { cs: ColoredShape; return cs.color(); }
)SRC");
    REQUIRE(jit);

    auto f_shape    = jit->lookup_symbol<int(*)()>("test_shape");
    auto f_drawable = jit->lookup_symbol<int(*)()>("test_drawable");
    auto f_colorable= jit->lookup_symbol<int(*)()>("test_colorable");
    auto f_draw     = jit->lookup_symbol<int(*)()>("test_direct_draw");
    auto f_color    = jit->lookup_symbol<int(*)()>("test_direct_color");
    REQUIRE(f_shape); REQUIRE(f_drawable); REQUIRE(f_colorable);
    REQUIRE(f_draw);  REQUIRE(f_color);

    CHECK(f_draw()     == 3);   // direct call
    CHECK(f_color()    == 5);   // direct call
    CHECK(f_shape()    == 3);   // via Shape& (primary vtable)
    CHECK(f_drawable() == 3);   // via Drawable& (transitive)
    CHECK(f_colorable()== 5);   // via Colorable& (secondary)
}

// ════════════════════════════════════════════════════════════════════════════
//  Diamant 2 : Node → IntNode + NamedNode → IntNamedNode
//
//  Node (interface)    : value() abstract
//    └─ IntNode  : Node        { value() = 10 }
//    └─ NamedNode (abstract) : Node { value() = 20 }
//         └─ IntNamedNode : IntNode, NamedNode
//                value() = 30
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[MD2] Diamond 2: IntNamedNode dispatch via Node&, IntNode&, NamedNode&",
          "[multi-diamond][diamond][dispatch]") {

    auto jit = gen_jit(R"SRC(
module gen_multi_diamond_02;
interface Node { value() : int; }
class IntNode : public Node {
    IntNode() {}
    value() : int { return 10; }
}
abstract class NamedNode : public Node {
    value() : int { return 20; }
}
class IntNamedNode : public IntNode, public NamedNode {
    IntNamedNode() {}
    value() : int { return 30; }
}
via_node(n:      Node&)     : int { return n.value(); }
via_intnode(i:   IntNode&)  : int { return i.value(); }
via_namednode(n: NamedNode&): int { return n.value(); }
test_node()      : int { inn: IntNamedNode; return via_node(inn);      }
test_intnode()   : int { inn: IntNamedNode; return via_intnode(inn);   }
test_namednode() : int { inn: IntNamedNode; return via_namednode(inn); }
test_direct()    : int { inn: IntNamedNode; return inn.value(); }
)SRC");
    REQUIRE(jit);

    auto f_node      = jit->lookup_symbol<int(*)()>("test_node");
    auto f_intnode   = jit->lookup_symbol<int(*)()>("test_intnode");
    auto f_namednode = jit->lookup_symbol<int(*)()>("test_namednode");
    auto f_direct    = jit->lookup_symbol<int(*)()>("test_direct");
    REQUIRE(f_node); REQUIRE(f_intnode); REQUIRE(f_namednode); REQUIRE(f_direct);

    CHECK(f_direct()    == 30);  // direct call
    CHECK(f_node()      == 30);  // via Node& (transitive)
    CHECK(f_intnode()   == 30);  // via IntNode& (primary)
    CHECK(f_namednode() == 30);  // via NamedNode& (secondary)
}

// ════════════════════════════════════════════════════════════════════════════
//  Diamant 2 variante : vérifie que chaque classe retourne la SIENNE
//  quand instanciée directement (pas de confusion par la fusion)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[MD3] Diamond 2: each class dispatches its own value",
          "[multi-diamond][diamond][dispatch]") {

    auto jit = gen_jit(R"SRC(
module gen_multi_diamond_03;
interface Node { value() : int; }
class IntNode : public Node {
    IntNode() {}
    value() : int { return 10; }
}
abstract class NamedNode : public Node {
    value() : int { return 20; }
}
class IntNamedNode : public IntNode, public NamedNode {
    IntNamedNode() {}
    value() : int { return 30; }
}
via_node(n: Node&)    : int { return n.value(); }
via_intnode(i: IntNode&): int { return i.value(); }
test_intnode_via_node()    : int { n: IntNode;      return via_node(n);    }
test_intnode_via_self()    : int { n: IntNode;      return via_intnode(n); }
test_inn_via_node()        : int { n: IntNamedNode; return via_node(n);    }
test_inn_via_intnode()     : int { n: IntNamedNode; return via_intnode(n); }
)SRC");
    REQUIRE(jit);

    auto f_in_node  = jit->lookup_symbol<int(*)()>("test_intnode_via_node");
    auto f_in_self  = jit->lookup_symbol<int(*)()>("test_intnode_via_self");
    auto f_inn_node = jit->lookup_symbol<int(*)()>("test_inn_via_node");
    auto f_inn_in   = jit->lookup_symbol<int(*)()>("test_inn_via_intnode");
    REQUIRE(f_in_node); REQUIRE(f_in_self); REQUIRE(f_inn_node); REQUIRE(f_inn_in);

    CHECK(f_in_node()  == 10);  // IntNode via Node& → 10
    CHECK(f_in_self()  == 10);  // IntNode via IntNode& → 10
    CHECK(f_inn_node() == 30);  // IntNamedNode via Node& → 30 (override)
    CHECK(f_inn_in()   == 30);  // IntNamedNode via IntNode& → 30 (override)
}

// ════════════════════════════════════════════════════════════════════════════
//  Fusion : Everything : ColoredShape + IntNamedNode
//
//  Everything hérite de DEUX hiérarchies indépendantes.
//  Vérifie le dispatch via tous les types ancêtres :
//    Shape&, Drawable&, Colorable&, Node&, IntNode&, NamedNode&
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[MD4] Everything: dispatch via all ancestor refs",
          "[multi-diamond][dispatch][secondary]") {

    auto jit = gen_jit(R"SRC(
module gen_multi_diamond_04;

interface Shape     { draw()  : int; }
interface Colorable { color() : int; }
abstract class Drawable : public Shape {
    draw() : int { return 1; }
}
class ColoredShape : public Drawable, public Colorable {
    ColoredShape() {}
    draw()  : int { return 3; }
    color() : int { return 5; }
}

interface Node { value() : int; }
class IntNode : public Node {
    IntNode() {}
    value() : int { return 10; }
}
abstract class NamedNode : public Node {
    value() : int { return 20; }
}
class IntNamedNode : public IntNode, public NamedNode {
    IntNamedNode() {}
    value() : int { return 30; }
}

class Everything : public ColoredShape, public IntNamedNode {
    Everything() {}
    draw()  : int { return 100; }
    color() : int { return 101; }
    value() : int { return 102; }
}

via_shape(s:     Shape&)    : int { return s.draw();  }
via_drawable(d:  Drawable&) : int { return d.draw();  }
via_colorable(c: Colorable&): int { return c.color(); }
via_node(n:      Node&)     : int { return n.value(); }
via_intnode(i:   IntNode&)  : int { return i.value(); }
via_namednode(n: NamedNode&): int { return n.value(); }

test_shape(    ) : int { e: Everything; return via_shape(e);     }
test_drawable( ) : int { e: Everything; return via_drawable(e);  }
test_colorable() : int { e: Everything; return via_colorable(e); }
test_node(     ) : int { e: Everything; return via_node(e);      }
test_intnode(  ) : int { e: Everything; return via_intnode(e);   }
test_namednode() : int { e: Everything; return via_namednode(e); }
test_direct_draw()  : int { e: Everything; return e.draw();  }
test_direct_color() : int { e: Everything; return e.color(); }
test_direct_value() : int { e: Everything; return e.value(); }
)SRC");
    REQUIRE(jit);

    auto f_draw   = jit->lookup_symbol<int(*)()>("test_direct_draw");
    auto f_color  = jit->lookup_symbol<int(*)()>("test_direct_color");
    auto f_value  = jit->lookup_symbol<int(*)()>("test_direct_value");
    auto f_shape  = jit->lookup_symbol<int(*)()>("test_shape");
    auto f_drwbl  = jit->lookup_symbol<int(*)()>("test_drawable");
    auto f_clrbl  = jit->lookup_symbol<int(*)()>("test_colorable");
    auto f_node   = jit->lookup_symbol<int(*)()>("test_node");
    auto f_intnd  = jit->lookup_symbol<int(*)()>("test_intnode");
    auto f_namednd= jit->lookup_symbol<int(*)()>("test_namednode");

    REQUIRE(f_draw); REQUIRE(f_color); REQUIRE(f_value);
    REQUIRE(f_shape); REQUIRE(f_drwbl); REQUIRE(f_clrbl);
    REQUIRE(f_node); REQUIRE(f_intnd); REQUIRE(f_namednd);

    // Appels directs
    CHECK(f_draw()  == 100);
    CHECK(f_color() == 101);
    CHECK(f_value() == 102);

    // Dispatch via la hiérarchie Shape/Draw/Color
    CHECK(f_shape() == 100);   // via Shape&  (transitive: Everything→ColoredShape→Drawable→Shape)
    CHECK(f_drwbl() == 100);   // via Drawable& (transitive: Everything→ColoredShape→Drawable)
    CHECK(f_clrbl() == 101);   // via Colorable& (secondary de ColoredShape, transitive de Everything)

    // Dispatch via la hiérarchie Node
    CHECK(f_node()   == 102);  // via Node& (transitive: Everything→IntNamedNode→IntNode→Node)
    CHECK(f_intnd()  == 102);  // via IntNode& (transitive: Everything→IntNamedNode→IntNode)
    CHECK(f_namednd()== 102);  // via NamedNode& (secondary de IntNamedNode, transitive de Everything)
}

// ════════════════════════════════════════════════════════════════════════════
//  [MD5] Vérification de non-contamination : ColoredShape et IntNamedNode
//  instanciés séparément dans le même module que Everything
//  → leurs dispatches ne sont pas affectés par la présence d'Everything
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[MD5] Non-contamination: ColoredShape and IntNamedNode unaffected by Everything",
          "[multi-diamond][dispatch]") {

    auto jit = gen_jit(R"SRC(
module gen_multi_diamond_05;

interface Shape     { draw()  : int; }
interface Colorable { color() : int; }
abstract class Drawable : public Shape {
    draw() : int { return 1; }
}
class ColoredShape : public Drawable, public Colorable {
    ColoredShape() {}
    draw()  : int { return 3; }
    color() : int { return 5; }
}

interface Node { value() : int; }
class IntNode : public Node {
    IntNode() {}
    value() : int { return 10; }
}
abstract class NamedNode : public Node {
    value() : int { return 20; }
}
class IntNamedNode : public IntNode, public NamedNode {
    IntNamedNode() {}
    value() : int { return 30; }
}

class Everything : public ColoredShape, public IntNamedNode {
    Everything() {}
    draw()  : int { return 100; }
    color() : int { return 101; }
    value() : int { return 102; }
}

via_shape(s:     Shape&)    : int { return s.draw();  }
via_drawable(d:  Drawable&) : int { return d.draw();  }
via_colorable(c: Colorable&): int { return c.color(); }
via_node(n:      Node&)     : int { return n.value(); }
via_intnode(i:   IntNode&)  : int { return i.value(); }
via_namednode(n: NamedNode&): int { return n.value(); }

test_cs_shape()    : int { cs: ColoredShape; return via_shape(cs);     }
test_cs_drawable() : int { cs: ColoredShape; return via_drawable(cs);  }
test_cs_colorable(): int { cs: ColoredShape; return via_colorable(cs); }
test_inn_node()    : int { n: IntNamedNode;  return via_node(n);       }
test_inn_intnode() : int { n: IntNamedNode;  return via_intnode(n);    }
test_inn_named()   : int { n: IntNamedNode;  return via_namednode(n);  }
)SRC");
    REQUIRE(jit);

    auto f_cs_sh  = jit->lookup_symbol<int(*)()>("test_cs_shape");
    auto f_cs_dr  = jit->lookup_symbol<int(*)()>("test_cs_drawable");
    auto f_cs_cl  = jit->lookup_symbol<int(*)()>("test_cs_colorable");
    auto f_inn_nd = jit->lookup_symbol<int(*)()>("test_inn_node");
    auto f_inn_in = jit->lookup_symbol<int(*)()>("test_inn_intnode");
    auto f_inn_nm = jit->lookup_symbol<int(*)()>("test_inn_named");

    REQUIRE(f_cs_sh); REQUIRE(f_cs_dr); REQUIRE(f_cs_cl);
    REQUIRE(f_inn_nd); REQUIRE(f_inn_in); REQUIRE(f_inn_nm);

    // ColoredShape non-affecté par Everything
    CHECK(f_cs_sh()  == 3);   // ColoredShape via Shape&
    CHECK(f_cs_dr()  == 3);   // ColoredShape via Drawable&
    CHECK(f_cs_cl()  == 5);   // ColoredShape via Colorable&

    // IntNamedNode non-affecté par Everything
    CHECK(f_inn_nd() == 30);  // IntNamedNode via Node&
    CHECK(f_inn_in() == 30);  // IntNamedNode via IntNode&
    CHECK(f_inn_nm() == 30);  // IntNamedNode via NamedNode&
}

// ════════════════════════════════════════════════════════════════════════════
//  [MD6] Diamant "profond" : 4 niveaux d'héritage avant la fusion
//
//  interface I     { tag() : int; }
//  class A : I     { tag() = 1 }
//  class B : A     { tag() = 2 }     (single-inheritance chain)
//  class C : I     { tag() = 3 }     (autre branche)
//  class D : B, C  { tag() = 4 }     (fusion multi-branche)
//
//  Dispatch via I&, A&, B&, C& tous doivent retourner 4.
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[MD6] Deep 4-level diamond: dispatch via all ancestor refs returns D override",
          "[multi-diamond][diamond][dispatch]") {

    auto jit = gen_jit(R"SRC(
module gen_multi_diamond_06;
interface I { tag() : int; }
class A : public I {
    A() {}
    tag() : int { return 1; }
}
class B : public A {
    B() {}
    tag() : int { return 2; }
}
class C : public I {
    C() {}
    tag() : int { return 3; }
}
class D : public B, public C {
    D() {}
    tag() : int { return 4; }
}
via_i(x: I&) : int { return x.tag(); }
via_a(x: A&) : int { return x.tag(); }
via_b(x: B&) : int { return x.tag(); }
via_c(x: C&) : int { return x.tag(); }
test_a_via_i() : int { a: A; return via_i(a); }
test_b_via_i() : int { b: B; return via_i(b); }
test_b_via_a() : int { b: B; return via_a(b); }
test_d_via_i() : int { d: D; return via_i(d); }
test_d_via_a() : int { d: D; return via_a(d); }
test_d_via_b() : int { d: D; return via_b(d); }
test_d_via_c() : int { d: D; return via_c(d); }
test_d_direct(): int { d: D; return d.tag(); }
)SRC");
    REQUIRE(jit);

    auto fa_i  = jit->lookup_symbol<int(*)()>("test_a_via_i");
    auto fb_i  = jit->lookup_symbol<int(*)()>("test_b_via_i");
    auto fb_a  = jit->lookup_symbol<int(*)()>("test_b_via_a");
    auto fd_i  = jit->lookup_symbol<int(*)()>("test_d_via_i");
    auto fd_a  = jit->lookup_symbol<int(*)()>("test_d_via_a");
    auto fd_b  = jit->lookup_symbol<int(*)()>("test_d_via_b");
    auto fd_c  = jit->lookup_symbol<int(*)()>("test_d_via_c");
    auto fd_dr = jit->lookup_symbol<int(*)()>("test_d_direct");

    REQUIRE(fa_i); REQUIRE(fb_i); REQUIRE(fb_a);
    REQUIRE(fd_i); REQUIRE(fd_a); REQUIRE(fd_b); REQUIRE(fd_c); REQUIRE(fd_dr);

    // Sous-classes sans fusion
    CHECK(fa_i() == 1);   // A via I&
    CHECK(fb_i() == 2);   // B via I& (transitive A→I)
    CHECK(fb_a() == 2);   // B via A& (transitive)

    // D (fusion de B et C)
    CHECK(fd_dr() == 4);  // D direct
    CHECK(fd_i()  == 4);  // D via I& (transitive B→A→I ou C→I)
    CHECK(fd_a()  == 4);  // D via A& (transitive B→A)
    CHECK(fd_b()  == 4);  // D via B& (transitive)
    CHECK(fd_c()  == 4);  // D via C& (secondary + transitive)
}

