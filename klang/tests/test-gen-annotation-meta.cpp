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
 * Tests for meta-annotation semantics:
 *   - @Target: restricts which element types an annotation may be applied to.
 *   - @Inherited: propagates annotations from base classes to derived classes.
 *   - @Retention: controls whether annotation instances are emitted into binary RTTI.
 *   - Enum fields in annotations: annotations can have enum-valued members.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/Support/raw_ostream.h>

#include <set>


// ════════════════════════════════════════════════════════════════════════════
//  1. Enum fields in annotations
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: enum field with default value", "[annotation][enum]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_enum_1__;
        annotation Severity {
            enum Level { LOW; MEDIUM; HIGH; };
            level : Level = Level::MEDIUM;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "Severity");
    REQUIRE(ann != nullptr);
    CHECK(ann->get_variable("level") != nullptr);
    auto en = ann->get_enum("Level");
    REQUIRE(en != nullptr);
    CHECK(en->entries().size() == 3);
}

TEST_CASE("Annotation: enum field with positional init on class", "[annotation][enum]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_enum_2__;
        annotation Severity {
            enum Level { LOW; MEDIUM; HIGH; };
            level : Level;
        }
        @Severity(Level::HIGH)
        class Critical {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto task = find_aggregate(comp, "Critical");
    REQUIRE(task != nullptr);
    auto& anns = task->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].resolved_type != nullptr);
    CHECK(anns[0].resolved_type->get_short_name() == "Severity");

    // Check that the field constant was materialized to the enum value of HIGH (=2)
    REQUIRE(anns[0].resolved_field_constants.size() >= 1);
    auto* ci = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[0]);
    REQUIRE(ci != nullptr);
    CHECK(ci->getSExtValue() == 2); // HIGH = 2 (0-based: LOW=0, MEDIUM=1, HIGH=2)
}

TEST_CASE("Annotation: enum array field with brace-init", "[annotation][enum]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_enum_arr_1__;
        annotation Filter {
            enum Kind { A; B; C; };
            kinds : Kind[];
        }
        @Filter({Kind::A, Kind::C})
        class Filtered {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto task = find_aggregate(comp, "Filtered");
    REQUIRE(task != nullptr);
    auto& anns = task->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].resolved_type->get_short_name() == "Filter");
    // The field constant should be a GlobalVariable (the K-array constant)
    REQUIRE(anns[0].resolved_field_constants.size() >= 1);
    CHECK(anns[0].resolved_field_constants[0] != nullptr);
}


// ════════════════════════════════════════════════════════════════════════════
//  2. @Target semantics
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("@Target: annotation restricted to CLASS can be applied to class", "[annotation][target]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_target_1__;
        annotation ClassOnly {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @ClassOnly
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::CLASS})
        annotation MyMarker {}
        @MyMarker
        class Foo {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);
    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    CHECK(foo->get_annotations().size() == 1);
}

TEST_CASE("@Target: annotation restricted to CLASS cannot be applied to interface", "[annotation][target]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_target_2__;
        annotation ClassOnly {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @ClassOnly
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::CLASS})
        annotation MyMarker {}
        @MyMarker
        interface BadIface {
            abstract foo() : int;
        }
    )SRC");
    // @Target restricts MyMarker to CLASS, so applying it to an interface should fail
    CHECK(comp == nullptr);
}

TEST_CASE("@Target: annotation restricted to ANNOTATION can be applied to annotation", "[annotation][target]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_target_3__;
        annotation AnnOnly {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @AnnOnly
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::ANNOTATION})
        annotation MetaMarker {}
        @MetaMarker
        annotation SomeAnn {}
    )SRC");
    REQUIRE(comp != nullptr);
    auto some = find_annotation_type(comp, "SomeAnn");
    REQUIRE(some != nullptr);
    CHECK(some->get_annotations().size() == 1);
}

TEST_CASE("@Target: annotation restricted to ANNOTATION cannot be applied to class", "[annotation][target]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_target_4__;
        annotation AnnOnly {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @AnnOnly
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::ANNOTATION})
        annotation MetaOnly {}
        @MetaOnly
        class BadClass {
            foo() : int { return 0; }
        }
    )SRC");
    // @Target restricts MetaOnly to ANNOTATION, so applying it to a class should fail
    CHECK(comp == nullptr);
}

TEST_CASE("@Target: no @Target means annotation can be applied anywhere", "[annotation][target]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_target_5__;
        annotation Unrestricted {}
        @Unrestricted
        class C1 {
            foo() : int { return 0; }
        }
        @Unrestricted
        interface I1 {
            abstract foo() : int;
        }
        @Unrestricted
        annotation A1 {}
    )SRC");
    REQUIRE(comp != nullptr);
    CHECK(find_aggregate(comp, "C1")->get_annotations().size() == 1);
    CHECK(find_aggregate(comp, "I1")->get_annotations().size() == 1);
    CHECK(find_annotation_type(comp, "A1")->get_annotations().size() == 1);
}

TEST_CASE("@Target: multiple element types allowed", "[annotation][target]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_target_6__;
        annotation Multi {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @Multi
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::CLASS, ElementType::INTERFACE})
        annotation ClassOrIface {}
        @ClassOrIface
        class Good1 {
            foo() : int { return 0; }
        }
        @ClassOrIface
        interface Good2 {
            abstract foo() : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);
    CHECK(find_aggregate(comp, "Good1")->get_annotations().size() == 1);
    CHECK(find_aggregate(comp, "Good2")->get_annotations().size() == 1);
}


// ════════════════════════════════════════════════════════════════════════════
//  3. @Inherited semantics
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("@Inherited: annotation propagates to derived class", "[annotation][inherited]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_inherit_1__;
        annotation Inh {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @Inh
        annotation Inherited {}
        @Inherited
        annotation Marker { value : int; }
        @Marker(42)
        class Base {
            foo() : int { return 0; }
        }
        class Derived : Base {
            bar() : int { return 1; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto base = find_aggregate(comp, "Base");
    auto derived = find_aggregate(comp, "Derived");
    REQUIRE(base != nullptr);
    REQUIRE(derived != nullptr);

    // Base has @Marker
    REQUIRE(base->get_annotations().size() == 1);
    CHECK(base->get_annotations()[0].resolved_type->get_short_name() == "Marker");

    // Derived should inherit @Marker via @Inherited
    REQUIRE(derived->get_annotations().size() == 1);
    CHECK(derived->get_annotations()[0].resolved_type->get_short_name() == "Marker");
}

TEST_CASE("@Inherited: explicit override on derived replaces inherited", "[annotation][inherited]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_inherit_2__;
        annotation Inh {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @Inh
        annotation Inherited {}
        @Inherited
        annotation Priority { value : int; }
        @Priority(1)
        class Base {
            foo() : int { return 0; }
        }
        @Priority(99)
        class Derived : Base {
            bar() : int { return 1; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto base = find_aggregate(comp, "Base");
    auto derived = find_aggregate(comp, "Derived");
    REQUIRE(base != nullptr);
    REQUIRE(derived != nullptr);

    // Base has @Priority(1)
    REQUIRE(base->get_annotations().size() == 1);
    REQUIRE(base->get_annotations()[0].resolved_field_constants.size() >= 1);
    auto* base_val = llvm::dyn_cast<llvm::ConstantInt>(base->get_annotations()[0].resolved_field_constants[0]);
    REQUIRE(base_val != nullptr);
    CHECK(base_val->getSExtValue() == 1);

    // Derived has its own @Priority(99), not the inherited one
    REQUIRE(derived->get_annotations().size() == 1);
    REQUIRE(derived->get_annotations()[0].resolved_field_constants.size() >= 1);
    auto* derived_val = llvm::dyn_cast<llvm::ConstantInt>(derived->get_annotations()[0].resolved_field_constants[0]);
    REQUIRE(derived_val != nullptr);
    CHECK(derived_val->getSExtValue() == 99);
}

TEST_CASE("@Inherited: non-inherited annotation does NOT propagate", "[annotation][inherited]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_inherit_3__;
        annotation NoInherit { value : int; }
        @NoInherit(42)
        class Base {
            foo() : int { return 0; }
        }
        class Derived : Base {
            bar() : int { return 1; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto base = find_aggregate(comp, "Base");
    auto derived = find_aggregate(comp, "Derived");
    REQUIRE(base != nullptr);
    REQUIRE(derived != nullptr);

    // Base has @NoInherit
    CHECK(base->get_annotations().size() == 1);
    // Derived should NOT have any annotations (not @Inherited)
    CHECK(derived->get_annotations().empty());
}


// ════════════════════════════════════════════════════════════════════════════
//  4. @Retention semantics
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("@Retention(RUNTIME): annotation instances appear in RTTI", "[annotation][retention]") {
    // Default (no @Retention) = RUNTIME → annotation instance constant is emitted
    auto comp = compile_model(R"SRC(
        module __test_ann_ret_1__;
        annotation RuntimeMarker {}
        @RuntimeMarker
        class Foo {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    auto& anns = foo->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].resolved_type != nullptr);
}

TEST_CASE("@Retention(SOURCE): annotation kept in model but not emitted", "[annotation][retention]") {
    // SOURCE retention means the annotation is in the model but NOT in binary RTTI.
    // We verify the annotation is present in the model.
    auto comp = compile_model(R"SRC(
        module __test_ann_ret_2__;
        annotation Src {
            enum Policy { SOURCE; RUNTIME; };
        }
        @Src
        annotation Retention {
            policy : Policy;
        }
        @Retention(Policy::SOURCE)
        annotation CompileOnly {}
        @CompileOnly
        class Bar {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto bar = find_aggregate(comp, "Bar");
    REQUIRE(bar != nullptr);
    // The annotation is in the model
    auto& anns = bar->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].resolved_type->get_short_name() == "CompileOnly");
}


// ════════════════════════════════════════════════════════════════════════════
//  5. Combined meta-annotation scenarios
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Meta-annotations: @Target + @Inherited + @Retention combined", "[annotation][meta]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_meta_combo_1__;
        annotation TargetDef {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        annotation InheritedDef {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @TargetDef
        annotation Target {
            value : ElementType[];
        }
        @InheritedDef
        annotation Inherited {}
        @Target({ElementType::CLASS})
        @Inherited
        annotation Important { reason : int; }
        @Important(42)
        class Base {
            foo() : int { return 0; }
        }
        class Derived : Base {
            bar() : int { return 1; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto derived = find_aggregate(comp, "Derived");
    REQUIRE(derived != nullptr);
    // Derived should inherit @Important from Base
    REQUIRE(derived->get_annotations().size() == 1);
    CHECK(derived->get_annotations()[0].resolved_type->get_short_name() == "Important");
}


// ════════════════════════════════════════════════════════════════════════════
//  6. @Target — additional edge cases
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("@Target: INTERFACE-only annotation applied to interface succeeds", "[annotation][target]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_target_iface_ok__;
        annotation IfaceRestrict {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @IfaceRestrict
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::INTERFACE})
        annotation IfaceOnly {}
        @IfaceOnly
        interface MyIface {
            abstract foo() : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);
    auto iface = find_aggregate(comp, "MyIface");
    REQUIRE(iface != nullptr);
    CHECK(iface->get_annotations().size() == 1);
    CHECK(iface->get_annotations()[0].resolved_type->get_short_name() == "IfaceOnly");
}

TEST_CASE("@Target: INTERFACE-only annotation applied to class fails", "[annotation][target]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_target_iface_cls__;
        annotation IfaceRestrict {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @IfaceRestrict
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::INTERFACE})
        annotation IfaceOnly {}
        @IfaceOnly
        class BadClass {
            foo() : int { return 0; }
        }
    )SRC");
    CHECK(comp == nullptr);
}

TEST_CASE("@Target: {CLASS, ANNOTATION} allows class and annotation but not interface", "[annotation][target]") {
    // Apply to class: OK
    auto comp_cls = compile_model(R"SRC(
        module __test_ann_target_ca_cls__;
        annotation MultiRestrict {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @MultiRestrict
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::CLASS, ElementType::ANNOTATION})
        annotation ClsOrAnn {}
        @ClsOrAnn
        class Good {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp_cls != nullptr);
    CHECK(find_aggregate(comp_cls, "Good")->get_annotations().size() == 1);

    // Apply to annotation: OK
    auto comp_ann = compile_model(R"SRC(
        module __test_ann_target_ca_ann__;
        annotation MultiRestrict2 {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @MultiRestrict2
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::CLASS, ElementType::ANNOTATION})
        annotation ClsOrAnn2 {}
        @ClsOrAnn2
        annotation AlsoGood {}
    )SRC");
    REQUIRE(comp_ann != nullptr);
    CHECK(find_annotation_type(comp_ann, "AlsoGood")->get_annotations().size() == 1);

    // Apply to interface: FAIL
    auto comp_iface = compile_model(R"SRC(
        module __test_ann_target_ca_ifc__;
        annotation MultiRestrict3 {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @MultiRestrict3
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::CLASS, ElementType::ANNOTATION})
        annotation ClsOrAnn3 {}
        @ClsOrAnn3
        interface BadIface {
            abstract foo() : int;
        }
    )SRC");
    CHECK(comp_iface == nullptr);
}

TEST_CASE("@Target: self-referential — @Target on Target itself", "[annotation][target]") {
    // Mimics the stdlib: @Target({ElementType::ANNOTATION}) annotation Target {...}
    auto comp = compile_model(R"SRC(
        module __test_ann_target_self__;
        @Target({ElementType::ANNOTATION})
        annotation Target {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
            value : ElementType[];
        }
        @Target({ElementType::CLASS})
        annotation ClassMarker {}
        @ClassMarker
        class Foo {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);
    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    CHECK(foo->get_annotations().size() == 1);
    CHECK(foo->get_annotations()[0].resolved_type->get_short_name() == "ClassMarker");
}

TEST_CASE("@Target: all three element types allowed", "[annotation][target]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_target_all3__;
        annotation AllTarget {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @AllTarget
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::CLASS, ElementType::INTERFACE, ElementType::ANNOTATION})
        annotation Universal {}
        @Universal
        class C {
            foo() : int { return 0; }
        }
        @Universal
        interface I {
            abstract bar() : int;
        }
        @Universal
        annotation A {}
    )SRC");
    REQUIRE(comp != nullptr);
    CHECK(find_aggregate(comp, "C")->get_annotations().size() == 1);
    CHECK(find_aggregate(comp, "I")->get_annotations().size() == 1);
    CHECK(find_annotation_type(comp, "A")->get_annotations().size() == 1);
}


// ════════════════════════════════════════════════════════════════════════════
//  7. @Inherited — additional edge cases
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("@Inherited: multi-level propagation (A → B → C)", "[annotation][inherited]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_inherit_multi__;
        annotation Inh {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @Inh
        annotation Inherited {}
        @Inherited
        annotation Tag { value : int; }
        @Tag(10)
        class A {
            foo() : int { return 0; }
        }
        class B : A {
            bar() : int { return 1; }
        }
        class C : B {
            baz() : int { return 2; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto a = find_aggregate(comp, "A");
    auto b = find_aggregate(comp, "B");
    auto c = find_aggregate(comp, "C");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);

    // A has @Tag(10)
    REQUIRE(a->get_annotations().size() == 1);
    CHECK(a->get_annotations()[0].resolved_type->get_short_name() == "Tag");

    // B inherits @Tag from A
    REQUIRE(b->get_annotations().size() == 1);
    CHECK(b->get_annotations()[0].resolved_type->get_short_name() == "Tag");

    // C inherits @Tag from B (which inherited it from A)
    REQUIRE(c->get_annotations().size() == 1);
    CHECK(c->get_annotations()[0].resolved_type->get_short_name() == "Tag");
}

TEST_CASE("@Inherited: multiple distinct inherited annotations from same base", "[annotation][inherited]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_inherit_multi_ann__;
        annotation Inh {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @Inh
        annotation Inherited {}
        @Inherited
        annotation Color { value : int; }
        @Inherited
        annotation Size  { value : int; }
        @Color(1) @Size(99)
        class Base {
            foo() : int { return 0; }
        }
        class Derived : Base {
            bar() : int { return 1; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto base = find_aggregate(comp, "Base");
    auto derived = find_aggregate(comp, "Derived");
    REQUIRE(base != nullptr);
    REQUIRE(derived != nullptr);

    // Base has both @Color and @Size
    CHECK(base->get_annotations().size() == 2);

    // Derived should inherit both
    REQUIRE(derived->get_annotations().size() == 2);
    std::set<std::string> ann_names;
    for (auto& ann : derived->get_annotations()) {
        ann_names.insert(ann.resolved_type->get_short_name());
    }
    CHECK(ann_names.count("Color") == 1);
    CHECK(ann_names.count("Size") == 1);
}

TEST_CASE("@Inherited: does NOT propagate through interfaces", "[annotation][inherited]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_inherit_no_iface__;
        annotation Inh {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @Inh
        annotation Inherited {}
        @Inherited
        annotation Marker {}
        @Marker
        interface Iface {
            abstract foo() : int;
        }
        class Impl : Iface {
            foo() : int { return 42; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto iface = find_aggregate(comp, "Iface");
    auto impl = find_aggregate(comp, "Impl");
    REQUIRE(iface != nullptr);
    REQUIRE(impl != nullptr);

    // Interface has @Marker
    CHECK(iface->get_annotations().size() == 1);
    // Implementing class should NOT inherit from an interface (only class inheritance propagates)
    CHECK(impl->get_annotations().empty());
}

TEST_CASE("@Inherited: partial override — only the overridden one is replaced", "[annotation][inherited]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_inherit_partial__;
        annotation Inh {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @Inh
        annotation Inherited {}
        @Inherited
        annotation Color { value : int; }
        @Inherited
        annotation Size  { value : int; }
        @Color(1) @Size(10)
        class Base {
            foo() : int { return 0; }
        }
        @Size(99)
        class Derived : Base {
            bar() : int { return 1; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto derived = find_aggregate(comp, "Derived");
    REQUIRE(derived != nullptr);

    // Derived should have @Color(1) inherited + @Size(99) overridden
    REQUIRE(derived->get_annotations().size() == 2);

    // Find each by name
    const k::model::annotation_instance* color_ann = nullptr;
    const k::model::annotation_instance* size_ann = nullptr;
    for (auto& ann : derived->get_annotations()) {
        if (ann.resolved_type->get_short_name() == "Color") color_ann = &ann;
        if (ann.resolved_type->get_short_name() == "Size")  size_ann  = &ann;
    }
    REQUIRE(color_ann != nullptr);
    REQUIRE(size_ann != nullptr);

    // Color should have inherited value 1
    REQUIRE(color_ann->resolved_field_constants.size() >= 1);
    auto* color_val = llvm::dyn_cast<llvm::ConstantInt>(color_ann->resolved_field_constants[0]);
    REQUIRE(color_val != nullptr);
    CHECK(color_val->getSExtValue() == 1);

    // Size should have overridden value 99
    REQUIRE(size_ann->resolved_field_constants.size() >= 1);
    auto* size_val = llvm::dyn_cast<llvm::ConstantInt>(size_ann->resolved_field_constants[0]);
    REQUIRE(size_val != nullptr);
    CHECK(size_val->getSExtValue() == 99);
}

TEST_CASE("@Inherited: non-inherited annotation mixed with inherited — only inherited propagates", "[annotation][inherited]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_inherit_mixed__;
        annotation Inh {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @Inh
        annotation Inherited {}
        @Inherited
        annotation Propagated { value : int; }
        annotation NotPropagated { value : int; }
        @Propagated(1) @NotPropagated(2)
        class Base {
            foo() : int { return 0; }
        }
        class Derived : Base {
            bar() : int { return 1; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto base = find_aggregate(comp, "Base");
    auto derived = find_aggregate(comp, "Derived");
    REQUIRE(base != nullptr);
    REQUIRE(derived != nullptr);

    // Base has both
    CHECK(base->get_annotations().size() == 2);

    // Derived should only have @Propagated (the @Inherited one)
    REQUIRE(derived->get_annotations().size() == 1);
    CHECK(derived->get_annotations()[0].resolved_type->get_short_name() == "Propagated");
}


// ════════════════════════════════════════════════════════════════════════════
//  8. @Retention — LLVM IR level verification
// ════════════════════════════════════════════════════════════════════════════

/*
 * Helper: compile K source with compile_model() and dump the LLVM IR to a string.
 */
static std::string get_llvm_ir(std::string_view src) {
    auto comp = compile_model(src);
    if (!comp) return {};
    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    return ir;
}

TEST_CASE("@Retention: default (absent) is RUNTIME — annotation instance emitted in IR", "[annotation][retention]") {
    std::string ir = get_llvm_ir(R"SRC(
        module __test_ann_ret_default__;
        annotation RuntimeDefault {}
        @RuntimeDefault
        class Foo {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(!ir.empty());
    // The annotation instance global should contain the annotation name "RuntimeDefault"
    CHECK(ir.find("RuntimeDefault") != std::string::npos);
    // The RTTI annotation array should reference it
    CHECK(ir.find("_ann_RuntimeDefault") != std::string::npos);
    // Check that the default value was correctly materialized
    CHECK(ir.find("i32 0") != std::string::npos); // LOW = 0
}

TEST_CASE("@Retention(RUNTIME): explicit RUNTIME — annotation instance emitted in IR", "[annotation][retention]") {
    std::string ir = get_llvm_ir(R"SRC(
        module __test_ann_ret_explicit_rt__;
        annotation Src {
            enum Policy { SOURCE; RUNTIME; };
        }
        @Src
        annotation Retention {
            policy : Policy;
        }
        @Retention(Policy::RUNTIME)
        annotation RuntimeExplicit {}
        @RuntimeExplicit
        class Bar {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(!ir.empty());
    CHECK(ir.find("_ann_RuntimeExplicit") != std::string::npos);
}

TEST_CASE("@Retention(SOURCE): annotation NOT emitted in IR", "[annotation][retention]") {
    std::string ir = get_llvm_ir(R"SRC(
        module __test_ann_ret_source__;
        annotation Src {
            enum Policy { SOURCE; RUNTIME; };
        }
        @Src
        annotation Retention {
            policy : Policy;
        }
        @Retention(Policy::SOURCE)
        annotation CompileOnly {}
        @CompileOnly
        class Baz {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(!ir.empty());
    // The annotation instance global should NOT be emitted
    CHECK(ir.find("_ann_CompileOnly") == std::string::npos);
}

TEST_CASE("@Retention(SOURCE): annotation present in model but skipped in IR", "[annotation][retention]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_ret_src_model__;
        annotation Src {
            enum Policy { SOURCE; RUNTIME; };
        }
        @Src
        annotation Retention {
            policy : Policy;
        }
        @Retention(Policy::SOURCE)
        annotation SourceAnn {}
        @SourceAnn
        class Marked {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    // The annotation IS in the model
    auto marked = find_aggregate(comp, "Marked");
    REQUIRE(marked != nullptr);
    REQUIRE(marked->get_annotations().size() == 1);
    CHECK(marked->get_annotations()[0].resolved_type->get_short_name() == "SourceAnn");

    // But NOT in the IR
    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    CHECK(ir.find("_ann_SourceAnn") == std::string::npos);
}

TEST_CASE("@Retention: mixed SOURCE and RUNTIME annotations on same class", "[annotation][retention]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_ret_mixed__;
        annotation Src {
            enum Policy { SOURCE; RUNTIME; };
        }
        @Src
        annotation Retention {
            policy : Policy;
        }
        @Retention(Policy::SOURCE)
        annotation SourceOnly {}
        annotation RuntimeDefault {}
        @SourceOnly @RuntimeDefault
        class MixedTarget {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    // Both in model
    auto target = find_aggregate(comp, "MixedTarget");
    REQUIRE(target != nullptr);
    CHECK(target->get_annotations().size() == 2);

    // Only RuntimeDefault in IR, not SourceOnly
    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    CHECK(ir.find("_ann_RuntimeDefault") != std::string::npos);
    CHECK(ir.find("_ann_SourceOnly") == std::string::npos);
}

TEST_CASE("@Retention(SOURCE) on annotation type: meta-annotation not emitted in type RTTI", "[annotation][retention]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_ret_src_on_type__;
        annotation Src {
            enum Policy { SOURCE; RUNTIME; };
        }
        @Src
        annotation Retention {
            policy : Policy;
        }
        @Retention(Policy::SOURCE)
        annotation MetaSource {}
        @MetaSource
        annotation TargetAnn {}
    )SRC");
    REQUIRE(comp != nullptr);

    // MetaSource is present in the model on TargetAnn
    auto tgt = find_annotation_type(comp, "TargetAnn");
    REQUIRE(tgt != nullptr);
    REQUIRE(tgt->get_annotations().size() == 1);
    CHECK(tgt->get_annotations()[0].resolved_type->get_short_name() == "MetaSource");

    // But MetaSource instance should NOT appear in the RTTI IR for TargetAnn
    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    CHECK(ir.find("_ann_MetaSource") == std::string::npos);
}


// ════════════════════════════════════════════════════════════════════════════
//  9. Enum field edge cases
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: enum field default value used when no argument provided", "[annotation][enum]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_enum_default__;
        annotation Priority {
            enum Level { LOW; MEDIUM; HIGH; };
            level : Level = Level::MEDIUM;
        }
        @Priority
        class Foo {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    auto& anns = foo->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].resolved_type->get_short_name() == "Priority");

    // The default value should be MEDIUM = 1
    REQUIRE(anns[0].resolved_field_constants.size() >= 1);
    auto* ci = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[0]);
    REQUIRE(ci != nullptr);
    CHECK(ci->getSExtValue() == 1); // MEDIUM = 1 (LOW=0, MEDIUM=1, HIGH=2)
}


// ════════════════════════════════════════════════════════════════════════════
// 10. Self-referential and cross-referential annotations
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: self-referential annotation (@Foo annotation Foo)", "[annotation][meta]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_selfref__;
        @SelfRef
        annotation SelfRef {}
    )SRC");
    REQUIRE(comp != nullptr);
    auto sr = find_annotation_type(comp, "SelfRef");
    REQUIRE(sr != nullptr);
    REQUIRE(sr->get_annotations().size() == 1);
    CHECK(sr->get_annotations()[0].resolved_type->get_short_name() == "SelfRef");
}

TEST_CASE("Annotation: cross-referential annotations in same module", "[annotation][meta]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_crossref__;
        @B
        annotation A {}
        @A
        annotation B {}
    )SRC");
    REQUIRE(comp != nullptr);
    auto a = find_annotation_type(comp, "A");
    auto b = find_annotation_type(comp, "B");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(a->get_annotations().size() == 1);
    CHECK(a->get_annotations()[0].resolved_type->get_short_name() == "B");
    CHECK(b->get_annotations().size() == 1);
    CHECK(b->get_annotations()[0].resolved_type->get_short_name() == "A");
}


// ════════════════════════════════════════════════════════════════════════════
// 11. Combined / integration scenarios
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Meta-annotations: @Retention(SOURCE) + @Inherited — propagates in model but not in IR", "[annotation][meta]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_src_inh_combo__;
        annotation Src {
            enum Policy { SOURCE; RUNTIME; };
        }
        annotation Inh {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @Src
        annotation Retention {
            policy : Policy;
        }
        @Inh
        annotation Inherited {}
        @Retention(Policy::SOURCE)
        @Inherited
        annotation SourceInherited { value : int; }
        @SourceInherited(42)
        class Base {
            foo() : int { return 0; }
        }
        class Derived : Base {
            bar() : int { return 1; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto base = find_aggregate(comp, "Base");
    auto derived = find_aggregate(comp, "Derived");
    REQUIRE(base != nullptr);
    REQUIRE(derived != nullptr);

    // Both should have the annotation in the model (inherited)
    REQUIRE(base->get_annotations().size() == 1);
    CHECK(base->get_annotations()[0].resolved_type->get_short_name() == "SourceInherited");
    REQUIRE(derived->get_annotations().size() == 1);
    CHECK(derived->get_annotations()[0].resolved_type->get_short_name() == "SourceInherited");

    // Neither should have the annotation in the IR (SOURCE retention)
    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    CHECK(ir.find("_ann_SourceInherited") == std::string::npos);
}

TEST_CASE("Meta-annotations: @Target(CLASS) + @Inherited + @Retention(RUNTIME) all together", "[annotation][meta]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_all3_combo__;
        annotation MetaDef {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        annotation InhDef {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        annotation SrcDef {
            enum Policy { SOURCE; RUNTIME; };
        }
        @MetaDef
        annotation Target {
            value : ElementType[];
        }
        @InhDef
        annotation Inherited {}
        @SrcDef
        annotation Retention {
            policy : Policy;
        }
        @Target({ElementType::CLASS})
        @Inherited
        @Retention(Policy::RUNTIME)
        annotation Important { reason : int; }
        @Important(7)
        class Base {
            foo() : int { return 0; }
        }
        class Derived : Base {
            bar() : int { return 1; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto base = find_aggregate(comp, "Base");
    auto derived = find_aggregate(comp, "Derived");
    REQUIRE(base != nullptr);
    REQUIRE(derived != nullptr);

    // Both have @Important in model
    REQUIRE(base->get_annotations().size() == 1);
    CHECK(base->get_annotations()[0].resolved_type->get_short_name() == "Important");
    REQUIRE(derived->get_annotations().size() == 1);
    CHECK(derived->get_annotations()[0].resolved_type->get_short_name() == "Important");

    // Both should have the annotation instance in IR (RUNTIME retention)
    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    CHECK(ir.find("_ann_Important") != std::string::npos);
}

TEST_CASE("Meta-annotations: @Target(CLASS) + @Inherited — fails when applied to interface", "[annotation][meta]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_target_inh_fail__;
        annotation MetaDef {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        annotation InhDef {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @MetaDef
        annotation Target {
            value : ElementType[];
        }
        @InhDef
        annotation Inherited {}
        @Target({ElementType::CLASS})
        @Inherited
        annotation ClassInherited {}
        @ClassInherited
        interface BadIface {
            abstract foo() : int;
        }
    )SRC");
    // @Target restricts to CLASS, so applying to interface should fail
    CHECK(comp == nullptr);
}

TEST_CASE("Meta-annotations: stdlib-like self-contained meta-annotation system", "[annotation][meta]") {
    // Simulate the full stdlib annotations.k pattern with self-referential and
    // cross-referential meta-annotations, including enum fields.
    auto comp = compile_model(R"SRC(
        module __test_ann_stdlib_like__;

        @Retention(Policy::RUNTIME)
        @Target({ElementType::ANNOTATION})
        annotation Retention {
            enum Policy { SOURCE; RUNTIME; };
            policy : Policy = Policy::RUNTIME;
        }

        @Retention(Policy::RUNTIME)
        @Target({ElementType::ANNOTATION})
        annotation Inherited {}

        @Retention(Policy::RUNTIME)
        @Target({ElementType::ANNOTATION})
        annotation Target {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
            value : ElementType[];
        }

        // User-defined annotation using the meta-annotations
        @Target({ElementType::CLASS})
        @Inherited
        @Retention(Policy::RUNTIME)
        annotation Versioned { version : int; }

        @Versioned(3)
        class Base {
            foo() : int { return 0; }
        }
        class Child : Base {
            bar() : int { return 1; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    // Retention is self-annotated
    auto ret = find_annotation_type(comp, "Retention");
    REQUIRE(ret != nullptr);
    CHECK(ret->get_annotations().size() >= 1);

    // Target is annotated with @Retention and @Target
    auto tgt = find_annotation_type(comp, "Target");
    REQUIRE(tgt != nullptr);
    CHECK(tgt->get_annotations().size() >= 1);

    // Versioned is annotated with all three
    auto ver = find_annotation_type(comp, "Versioned");
    REQUIRE(ver != nullptr);
    CHECK(ver->get_annotations().size() == 3);

    // Child inherits @Versioned from Base
    auto child = find_aggregate(comp, "Child");
    REQUIRE(child != nullptr);
    REQUIRE(child->get_annotations().size() == 1);
    CHECK(child->get_annotations()[0].resolved_type->get_short_name() == "Versioned");

    // RUNTIME retention: Versioned instance emitted in IR
    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    CHECK(ir.find("_ann_Versioned") != std::string::npos);
}


// ════════════════════════════════════════════════════════════════════════════
// 12. Function annotations — model level
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Function annotation: RUNTIME annotation on public function in model", "[annotation][function]") {
    auto comp = compile_model(R"SRC(
        module __test_fn_ann_1__;
        annotation Marker {}
        class Foo {
            @Marker
            bar() : int { return 42; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    auto fn = foo->get_function("bar");
    REQUIRE(fn != nullptr);
    auto& anns = fn->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].resolved_type != nullptr);
    CHECK(anns[0].resolved_type->get_short_name() == "Marker");
}

TEST_CASE("Function annotation: SOURCE retention annotation kept in model", "[annotation][function][retention]") {
    auto comp = compile_model(R"SRC(
        module __test_fn_ann_src_1__;
        annotation Src {
            enum Policy { SOURCE; RUNTIME; };
        }
        @Src
        annotation Retention {
            policy : Policy;
        }
        @Retention(Policy::SOURCE)
        annotation CompileHint {}
        class Foo {
            @CompileHint
            bar() : int { return 1; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    auto fn = foo->get_function("bar");
    REQUIRE(fn != nullptr);
    auto& anns = fn->get_annotations();
    // SOURCE annotations are kept in the model even if not emitted in binary
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].resolved_type != nullptr);
    CHECK(anns[0].resolved_type->get_short_name() == "CompileHint");
    CHECK(anns[0].resolved_type->is_source_retention());
}

TEST_CASE("Function annotation: multiple annotations on same function", "[annotation][function]") {
    auto comp = compile_model(R"SRC(
        module __test_fn_ann_multi__;
        annotation Alpha {}
        annotation Beta {}
        class Foo {
            @Alpha @Beta
            work() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    auto fn = foo->get_function("work");
    REQUIRE(fn != nullptr);
    CHECK(fn->get_annotations().size() == 2);
}

TEST_CASE("Function annotation: annotation on free function (namespace level)", "[annotation][function]") {
    auto comp = compile_model(R"SRC(
        module __test_fn_ann_free__;
        annotation Tag {}
        @Tag
        doStuff() : int { return 7; }
    )SRC");
    REQUIRE(comp != nullptr);

    auto root = comp->get_unit()->get_root_namespace();
    REQUIRE(root != nullptr);
    auto fn = root->get_function("doStuff");
    REQUIRE(fn != nullptr);
    CHECK(fn->get_annotations().size() == 1);
    CHECK(fn->get_annotations()[0].resolved_type->get_short_name() == "Tag");
}

TEST_CASE("Function annotation: @Target(FUNCTION) allows annotation on function", "[annotation][function][target]") {
    auto comp = compile_model(R"SRC(
        module __test_fn_ann_target_1__;
        annotation TargetDef {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; FUNCTION; };
        }
        @TargetDef
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::FUNCTION})
        annotation FuncOnly {}
        class Foo {
            @FuncOnly
            bar() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    auto fn = foo->get_function("bar");
    REQUIRE(fn != nullptr);
    CHECK(fn->get_annotations().size() == 1);
    CHECK(fn->get_annotations()[0].resolved_type->get_short_name() == "FuncOnly");
}

TEST_CASE("Function annotation: @Target(CLASS) rejects annotation on function", "[annotation][function][target]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __test_fn_ann_target_2__;
        annotation TargetDef {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; FUNCTION; };
        }
        @TargetDef
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::CLASS})
        annotation ClassOnly {}
        class Foo {
            @ClassOnly
            bar() : int { return 0; }
        }
    )SRC"), k::model::gen::resolution_error);
}


// ════════════════════════════════════════════════════════════════════════════
//  13. Function annotations — RUNTIME annotation on non-RTTI functions
//      (compilation succeeds with warning; annotation kept in model)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Function annotation: RUNTIME annotation on private function compiles (warning)", "[annotation][function][warning]") {
    // A RUNTIME annotation on a private function is a warning (not an error).
    // The annotation is still in the model.
    auto comp = compile_model(R"SRC(
        module __test_fn_ann_warn_priv__;
        annotation Marker {}
        class Foo {
            @Marker
            private bar() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    auto fn = foo->get_function("bar");
    REQUIRE(fn != nullptr);
    // Annotation is kept in the model despite the warning
    REQUIRE(fn->get_annotations().size() == 1);
    CHECK(fn->get_annotations()[0].resolved_type->get_short_name() == "Marker");
}

TEST_CASE("Function annotation: RUNTIME annotation on protected function compiles (warning)", "[annotation][function][warning]") {
    auto comp = compile_model(R"SRC(
        module __test_fn_ann_warn_prot__;
        annotation Marker {}
        class Foo {
            @Marker
            protected bar() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    auto fn = foo->get_function("bar");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->get_annotations().size() == 1);
    CHECK(fn->get_annotations()[0].resolved_type->get_short_name() == "Marker");
}

TEST_CASE("Function annotation: RUNTIME annotation on public function — no issue", "[annotation][function]") {
    auto comp = compile_model(R"SRC(
        module __test_fn_ann_no_warn_pub__;
        annotation Marker {}
        class Foo {
            @Marker
            public bar() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    auto fn = foo->get_function("bar");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->get_annotations().size() == 1);
    CHECK(fn->get_annotations()[0].resolved_type->get_short_name() == "Marker");
    CHECK_FALSE(fn->get_annotations()[0].resolved_type->is_source_retention());
}

TEST_CASE("Function annotation: SOURCE annotation on private function — no warning", "[annotation][function][retention]") {
    auto comp = compile_model(R"SRC(
        module __test_fn_ann_no_warn_src__;
        annotation Src {
            enum Policy { SOURCE; RUNTIME; };
        }
        @Src
        annotation Retention {
            policy : Policy;
        }
        @Retention(Policy::SOURCE)
        annotation CompileOnly {}
        class Foo {
            @CompileOnly
            private bar() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    auto fn = foo->get_function("bar");
    REQUIRE(fn != nullptr);
    // SOURCE annotation is in the model
    REQUIRE(fn->get_annotations().size() == 1);
    CHECK(fn->get_annotations()[0].resolved_type->get_short_name() == "CompileOnly");
    CHECK(fn->get_annotations()[0].resolved_type->is_source_retention());
}

TEST_CASE("Function annotation: RUNTIME annotation on constructor compiles (warning)", "[annotation][function][warning]") {
    auto comp = compile_model(R"SRC(
        module __test_fn_ann_warn_ctor__;
        annotation Marker {}
        class Foo {
            @Marker
            Foo() {}
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    // Constructor's annotation is in the model
    auto& ctors = foo->constructors();
    REQUIRE(!ctors.empty());
    REQUIRE(ctors[0]->get_annotations().size() == 1);
    CHECK(ctors[0]->get_annotations()[0].resolved_type->get_short_name() == "Marker");
}

TEST_CASE("Function annotation: RUNTIME annotation on destructor compiles (warning)", "[annotation][function][warning]") {
    auto comp = compile_model(R"SRC(
        module __test_fn_ann_warn_dtor__;
        annotation Marker {}
        class Foo {
            @Marker
            ~Foo() {}
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    // Destructor's annotation is in the model
    auto dtor = foo->get_destructor();
    REQUIRE(dtor != nullptr);
    REQUIRE(dtor->get_annotations().size() == 1);
    CHECK(dtor->get_annotations()[0].resolved_type->get_short_name() == "Marker");
}


// ════════════════════════════════════════════════════════════════════════════
//  14. Constructor annotations — @Target with CONSTRUCTOR element type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Constructor annotation: @Target(CONSTRUCTOR) allows annotation on constructor", "[annotation][constructor][target]") {
    auto comp = compile_model(R"SRC(
        module __test_ctor_ann_target_1__;
        annotation TargetDef {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; FUNCTION; CONSTRUCTOR; };
        }
        @TargetDef
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::CONSTRUCTOR})
        annotation CtorOnly {}
        class Foo {
            @CtorOnly
            public Foo() {}
            bar() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    REQUIRE(!foo->constructors().empty());
    auto& ctor = foo->constructors().front();
    CHECK(ctor->get_annotations().size() == 1);
    CHECK(ctor->get_annotations()[0].resolved_type->get_short_name() == "CtorOnly");
}

TEST_CASE("Constructor annotation: @Target(CLASS) rejects annotation on constructor", "[annotation][constructor][target]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __test_ctor_ann_target_2__;
        annotation TargetDef {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; FUNCTION; CONSTRUCTOR; };
        }
        @TargetDef
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::CLASS})
        annotation ClassOnly {}
        class Foo {
            @ClassOnly
            public Foo() {}
            bar() : int { return 0; }
        }
    )SRC"), k::model::gen::resolution_error);
}

TEST_CASE("Constructor annotation: @Target(FUNCTION) rejects annotation on constructor", "[annotation][constructor][target]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __test_ctor_ann_target_3__;
        annotation TargetDef {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; FUNCTION; CONSTRUCTOR; };
        }
        @TargetDef
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::FUNCTION})
        annotation FuncOnly {}
        class Foo {
            @FuncOnly
            public Foo() {}
            bar() : int { return 0; }
        }
    )SRC"), k::model::gen::resolution_error);
}

TEST_CASE("Constructor annotation: @Target(CONSTRUCTOR) rejects annotation on function", "[annotation][constructor][target]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __test_ctor_ann_target_4__;
        annotation TargetDef {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; FUNCTION; CONSTRUCTOR; };
        }
        @TargetDef
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::CONSTRUCTOR})
        annotation CtorOnly {}
        class Foo {
            public Foo() {}
            @CtorOnly
            bar() : int { return 0; }
        }
    )SRC"), k::model::gen::resolution_error);
}

TEST_CASE("Constructor annotation: unrestricted annotation allowed on constructor", "[annotation][constructor][target]") {
    auto comp = compile_model(R"SRC(
        module __test_ctor_ann_target_5__;
        annotation Marker {}
        class Foo {
            @Marker
            public Foo() {}
            bar() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    REQUIRE(!foo->constructors().empty());
    auto& ctor = foo->constructors().front();
    CHECK(ctor->get_annotations().size() == 1);
    CHECK(ctor->get_annotations()[0].resolved_type->get_short_name() == "Marker");
}

TEST_CASE("Constructor annotation: RUNTIME annotation on public constructor — no warning", "[annotation][constructor][warning]") {
    test_logger logger;
    bool ok = compile_collect_diagnostics(R"SRC(
        module __test_ctor_ann_no_warn__;
        annotation Marker {}
        class Foo {
            @Marker
            public Foo() {}
            bar() : int { return 0; }
        }
    )SRC", nullptr, logger);
    CHECK(ok);
    // No 0x003D warning should be emitted for a public constructor with annotations
    bool has_003D = false;
    for (auto& diag : logger.diagnostics) {
        if (diag.code == 0x003D) {
            has_003D = true;
            break;
        }
    }
    CHECK_FALSE(has_003D);
}


// ════════════════════════════════════════════════════════════════════════════
// 15. Real stdlib meta-annotations — import k; uses ::k::annotations::*
// ════════════════════════════════════════════════════════════════════════════
// These tests import the actual K standard library so that the meta-annotations
// @Retention, @Inherited, and @Target resolve to the real types from
// ::k::annotations rather than local stand-ins.  This exercises the FQN
// matching path in the compiler (get_fq_name() == "::k::annotations::*"),
// not just the raw_name fallback.
//
// Note: because the stdlib types live in the k::annotations namespace,
// we must use the fully-qualified form: @k::annotations::Target({...}).
// Enum values inside the annotation type (e.g. ElementType::CLASS, Policy::SOURCE)
// are resolved relative to the annotation type's scope and do not need
// further qualification.

TEST_CASE("Stdlib @Target: annotation restricted to CLASS can be applied to class", "[annotation][stdlib][target]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module __test_stdlib_target_cls_ok__;
        import k;
        @k::annotations::Target({ElementType::CLASS})
        annotation MyMarker {}
        @MyMarker
        class Foo {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);
    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    REQUIRE(foo->get_annotations().size() == 1);
    CHECK(foo->get_annotations()[0].resolved_type->get_short_name() == "MyMarker");

    // Verify the @Target meta-annotation on MyMarker resolved to the real stdlib type
    auto my_marker = find_annotation_type(comp, "MyMarker");
    REQUIRE(my_marker != nullptr);
    bool found_real_target = false;
    for (auto& meta : my_marker->get_annotations()) {
        if (meta.resolved_type && meta.resolved_type->get_fq_name() == "::k::annotations::Target") {
            found_real_target = true;
            break;
        }
    }
    CHECK(found_real_target);
}

TEST_CASE("Stdlib @Target: annotation restricted to CLASS rejected on interface", "[annotation][stdlib][target]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module __test_stdlib_target_cls_iface_fail__;
        import k;
        @k::annotations::Target({ElementType::CLASS})
        annotation MyMarker {}
        @MyMarker
        interface BadIface {
            abstract foo() : int;
        }
    )SRC");
    // @Target restricts MyMarker to CLASS, so applying it to an interface should fail
    CHECK(comp == nullptr);
}

TEST_CASE("Stdlib @Target: FUNCTION-only annotation allowed on function", "[annotation][stdlib][target]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module __test_stdlib_target_func_ok__;
        import k;
        @k::annotations::Target({ElementType::FUNCTION})
        annotation FuncOnly {}
        class Foo {
            @FuncOnly
            bar() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);
    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    auto fn = foo->get_function("bar");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->get_annotations().size() == 1);
    CHECK(fn->get_annotations()[0].resolved_type->get_short_name() == "FuncOnly");
}

TEST_CASE("Stdlib @Target: CONSTRUCTOR-only annotation allowed on constructor", "[annotation][stdlib][target]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module __test_stdlib_target_ctor_ok__;
        import k;
        @k::annotations::Target({ElementType::CONSTRUCTOR})
        annotation CtorOnly {}
        class Foo {
            @CtorOnly
            public Foo() {}
            bar() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);
    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    REQUIRE(!foo->constructors().empty());
    auto& ctor = foo->constructors().front();
    REQUIRE(ctor->get_annotations().size() == 1);
    CHECK(ctor->get_annotations()[0].resolved_type->get_short_name() == "CtorOnly");
}

TEST_CASE("Stdlib @Inherited: annotation propagates from base to derived", "[annotation][stdlib][inherited]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module __test_stdlib_inherited__;
        import k;
        @k::annotations::Inherited
        annotation Tag { value : int; }
        @Tag(42)
        class Base {
            foo() : int { return 0; }
        }
        class Derived : Base {
            bar() : int { return 1; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto base = find_aggregate(comp, "Base");
    auto derived = find_aggregate(comp, "Derived");
    REQUIRE(base != nullptr);
    REQUIRE(derived != nullptr);

    // Base has @Tag
    REQUIRE(base->get_annotations().size() == 1);
    CHECK(base->get_annotations()[0].resolved_type->get_short_name() == "Tag");

    // Derived inherits @Tag via real stdlib @Inherited
    REQUIRE(derived->get_annotations().size() == 1);
    CHECK(derived->get_annotations()[0].resolved_type->get_short_name() == "Tag");

    // Verify @Inherited on Tag is the real stdlib type
    auto tag = find_annotation_type(comp, "Tag");
    REQUIRE(tag != nullptr);
    bool found_real_inherited = false;
    for (auto& meta : tag->get_annotations()) {
        if (meta.resolved_type && meta.resolved_type->get_fq_name() == "::k::annotations::Inherited") {
            found_real_inherited = true;
            break;
        }
    }
    CHECK(found_real_inherited);
}

TEST_CASE("Stdlib @Inherited: non-inherited annotation does NOT propagate", "[annotation][stdlib][inherited]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module __test_stdlib_no_inherit__;
        import k;
        annotation NoInherit { value : int; }
        @NoInherit(42)
        class Base {
            foo() : int { return 0; }
        }
        class Derived : Base {
            bar() : int { return 1; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto base = find_aggregate(comp, "Base");
    auto derived = find_aggregate(comp, "Derived");
    REQUIRE(base != nullptr);
    REQUIRE(derived != nullptr);

    CHECK(base->get_annotations().size() == 1);
    CHECK(derived->get_annotations().empty());
}

TEST_CASE("Stdlib @Retention(SOURCE): is_source_retention() with real stdlib Retention", "[annotation][stdlib][retention]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module __test_stdlib_retention_src__;
        import k;
        @k::annotations::Retention(Policy::SOURCE)
        annotation CompileOnly {}
        @CompileOnly
        class Bar {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "CompileOnly");
    REQUIRE(ann != nullptr);
    CHECK(ann->is_source_retention());

    // Verify @Retention on CompileOnly is the real stdlib type
    bool found_real_retention = false;
    for (auto& meta : ann->get_annotations()) {
        if (meta.resolved_type && meta.resolved_type->get_fq_name() == "::k::annotations::Retention") {
            found_real_retention = true;
            break;
        }
    }
    CHECK(found_real_retention);
}

TEST_CASE("Stdlib @Retention: default (no @Retention) is RUNTIME", "[annotation][stdlib][retention]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module __test_stdlib_retention_default__;
        import k;
        annotation RuntimeDefault {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "RuntimeDefault");
    REQUIRE(ann != nullptr);
    CHECK_FALSE(ann->is_source_retention());
}

TEST_CASE("Stdlib @Retention(SOURCE): annotation NOT emitted in IR", "[annotation][stdlib][retention]") {
    // Compile with compile_model_with_stdlib and dump IR
    auto comp = compile_model_with_stdlib(R"SRC(
        module __test_stdlib_ret_src_ir__;
        import k;
        @k::annotations::Retention(Policy::SOURCE)
        annotation CompileOnly {}
        @CompileOnly
        class Baz {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    // Annotation present in model
    auto baz = find_aggregate(comp, "Baz");
    REQUIRE(baz != nullptr);
    REQUIRE(baz->get_annotations().size() == 1);
    CHECK(baz->get_annotations()[0].resolved_type->get_short_name() == "CompileOnly");

    // But NOT in IR
    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    CHECK(ir.find("_ann_CompileOnly") == std::string::npos);
}

TEST_CASE("Stdlib combined: @Target(CLASS) + @Inherited + @Retention(RUNTIME)", "[annotation][stdlib][meta]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module __test_stdlib_combined__;
        import k;
        @k::annotations::Target({ElementType::CLASS})
        @k::annotations::Inherited
        @k::annotations::Retention(Policy::RUNTIME)
        annotation Important { reason : int; }
        @Important(7)
        class Base {
            foo() : int { return 0; }
        }
        class Derived : Base {
            bar() : int { return 1; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto base = find_aggregate(comp, "Base");
    auto derived = find_aggregate(comp, "Derived");
    REQUIRE(base != nullptr);
    REQUIRE(derived != nullptr);

    // Both have @Important in model (inherited)
    REQUIRE(base->get_annotations().size() == 1);
    CHECK(base->get_annotations()[0].resolved_type->get_short_name() == "Important");
    REQUIRE(derived->get_annotations().size() == 1);
    CHECK(derived->get_annotations()[0].resolved_type->get_short_name() == "Important");

    // RUNTIME retention: annotation emitted in IR
    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    CHECK(ir.find("_ann_Important") != std::string::npos);

    // Verify all three meta-annotations on Important resolve to real stdlib FQNs
    auto imp = find_annotation_type(comp, "Important");
    REQUIRE(imp != nullptr);
    bool found_target = false, found_inherited = false, found_retention = false;
    for (auto& meta : imp->get_annotations()) {
        if (!meta.resolved_type) continue;
        std::string fqn = meta.resolved_type->get_fq_name();
        if (fqn == "::k::annotations::Target")    found_target    = true;
        if (fqn == "::k::annotations::Inherited")  found_inherited = true;
        if (fqn == "::k::annotations::Retention")  found_retention = true;
    }
    CHECK(found_target);
    CHECK(found_inherited);
    CHECK(found_retention);
}

TEST_CASE("Stdlib @Target(CLASS) + @Inherited: rejects when applied to interface", "[annotation][stdlib][meta]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module __test_stdlib_target_inh_fail__;
        import k;
        @k::annotations::Target({ElementType::CLASS})
        @k::annotations::Inherited
        annotation ClassInherited {}
        @ClassInherited
        interface BadIface {
            abstract foo() : int;
        }
    )SRC");
    CHECK(comp == nullptr);
}


// ════════════════════════════════════════════════════════════════════════════
// 16. Namespace collision regression — user-defined "Retention" / "Inherited"
// ════════════════════════════════════════════════════════════════════════════
// Document the current behavior: the compiler's raw_name fallback means
// ANY annotation named "Retention", "Inherited", or "Target" is treated
// as a meta-annotation, even if it's defined in a user namespace.
// These tests document that behavior so a future tightening of the
// fallback will cause them to flip, signaling the change.

TEST_CASE("Namespace collision: user-defined 'Retention' in local module triggers source retention", "[annotation][meta][namespace]") {
    // A user-defined annotation named "Retention" with a "Policy::SOURCE"
    // value is currently recognized as a meta-annotation due to the raw_name
    // fallback.  This test documents that behavior.
    auto comp = compile_model(R"SRC(
        module __test_ns_collision_ret__;
        annotation RetDef {
            enum Policy { SOURCE; RUNTIME; };
        }
        @RetDef
        annotation Retention {
            policy : Policy;
        }
        @Retention(Policy::SOURCE)
        annotation UserAnn {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "UserAnn");
    REQUIRE(ann != nullptr);
    // Due to the raw_name fallback, the compiler treats this as @Retention(SOURCE)
    // even though this is NOT ::k::annotations::Retention.
    // This CHECK documents the current behavior.
    CHECK(ann->is_source_retention());
}

TEST_CASE("Namespace collision: user-defined 'Inherited' in local module triggers inheritance", "[annotation][meta][namespace]") {
    // A user-defined annotation named "Inherited" is currently recognized
    // as a meta-annotation due to the raw_name fallback.
    auto comp = compile_model(R"SRC(
        module __test_ns_collision_inh__;
        annotation InhDef {}
        @InhDef
        annotation Inherited {}
        @Inherited
        annotation Marker { value : int; }
        @Marker(42)
        class Base {
            foo() : int { return 0; }
        }
        class Derived : Base {
            bar() : int { return 1; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto derived = find_aggregate(comp, "Derived");
    REQUIRE(derived != nullptr);
    // Due to the raw_name fallback, the compiler treats the local "Inherited"
    // as the @Inherited meta-annotation.
    REQUIRE(derived->get_annotations().size() == 1);
    CHECK(derived->get_annotations()[0].resolved_type->get_short_name() == "Marker");
}

TEST_CASE("Namespace collision: user-defined 'Target' in local module triggers target restriction", "[annotation][meta][namespace]") {
    // A user-defined annotation named "Target" is currently recognized
    // as a meta-annotation due to the raw_name fallback.
    auto comp = compile_model(R"SRC(
        module __test_ns_collision_target__;
        annotation TargetDef {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @TargetDef
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::CLASS})
        annotation MyMarker {}
        @MyMarker
        interface BadIface {
            abstract foo() : int;
        }
    )SRC");
    // Due to the raw_name fallback, the compiler treats the local "Target"
    // as the @Target meta-annotation and rejects the annotation on an interface.
    CHECK(comp == nullptr);
}



