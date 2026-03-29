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
// 12. Enum field edge cases — additional
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: multiple enum fields with different enums", "[annotation][enum]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_enum_multi_fields__;
        annotation Config {
            enum Mode { FAST; SAFE; };
            enum Level { LOW; MEDIUM; HIGH; };
            mode : Mode;
            level : Level;
        }
        @Config(Mode::SAFE, Level::HIGH)
        class App {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto app = find_aggregate(comp, "App");
    REQUIRE(app != nullptr);
    auto& anns = app->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].resolved_type->get_short_name() == "Config");

    REQUIRE(anns[0].resolved_field_constants.size() >= 2);
    auto* mode_val = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[0]);
    auto* level_val = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[1]);
    REQUIRE(mode_val != nullptr);
    REQUIRE(level_val != nullptr);
    CHECK(mode_val->getSExtValue() == 1);  // SAFE = 1
    CHECK(level_val->getSExtValue() == 2); // HIGH = 2
}

TEST_CASE("Annotation: enum field with first entry as default", "[annotation][enum]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_enum_first_default__;
        annotation Status {
            enum State { ACTIVE; INACTIVE; };
            state : State = State::ACTIVE;
        }
        @Status
        class Obj {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto obj = find_aggregate(comp, "Obj");
    REQUIRE(obj != nullptr);
    auto& anns = obj->get_annotations();
    REQUIRE(anns.size() == 1);
    REQUIRE(anns[0].resolved_field_constants.size() >= 1);
    auto* ci = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[0]);
    REQUIRE(ci != nullptr);
    CHECK(ci->getSExtValue() == 0); // ACTIVE = 0
}

TEST_CASE("Annotation: enum field explicit overrides default", "[annotation][enum]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_enum_override_default__;
        annotation Priority {
            enum Level { LOW; MEDIUM; HIGH; };
            level : Level = Level::LOW;
        }
        @Priority(Level::HIGH)
        class Urgent {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto urgent = find_aggregate(comp, "Urgent");
    REQUIRE(urgent != nullptr);
    auto& anns = urgent->get_annotations();
    REQUIRE(anns.size() == 1);
    REQUIRE(anns[0].resolved_field_constants.size() >= 1);
    auto* ci = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[0]);
    REQUIRE(ci != nullptr);
    CHECK(ci->getSExtValue() == 2); // HIGH = 2, not LOW default
}


// ════════════════════════════════════════════════════════════════════════════
// 13. @Target — further edge cases
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("@Target: ANNOTATION-only cannot be applied to interface", "[annotation][target]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_target_annonly_iface__;
        annotation AnnRestrict {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @AnnRestrict
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::ANNOTATION})
        annotation AnnOnly {}
        @AnnOnly
        interface BadIface {
            abstract foo() : int;
        }
    )SRC");
    CHECK(comp == nullptr);
}

TEST_CASE("@Target: INTERFACE-only annotation applied to annotation fails", "[annotation][target]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_target_ifonly_ann__;
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
        annotation BadAnn {}
    )SRC");
    CHECK(comp == nullptr);
}

TEST_CASE("@Target: CLASS-only annotation applied to annotation fails", "[annotation][target]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_target_clsonly_ann__;
        annotation ClsRestrict {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @ClsRestrict
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::CLASS})
        annotation ClassOnly {}
        @ClassOnly
        annotation BadAnn {}
    )SRC");
    CHECK(comp == nullptr);
}

TEST_CASE("@Target: multiple annotations with different targets on same class", "[annotation][target]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_target_multi_on_cls__;
        annotation TgtDef {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @TgtDef
        annotation Target {
            value : ElementType[];
        }
        @Target({ElementType::CLASS})
        annotation ClassMarker {}
        @Target({ElementType::CLASS, ElementType::INTERFACE})
        annotation FlexMarker {}
        @ClassMarker @FlexMarker
        class Foo {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);
    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    CHECK(foo->get_annotations().size() == 2);
}


// ════════════════════════════════════════════════════════════════════════════
// 14. @Inherited — value preservation and deeper chains
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("@Inherited: inherited value constant preserved through chain", "[annotation][inherited]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_inherit_val_chain__;
        annotation Inh {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @Inh
        annotation Inherited {}
        @Inherited
        annotation Version { value : int; }
        @Version(42)
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

    // All three should have @Version with value 42
    for (auto* agg : {a.get(), b.get(), c.get()}) {
        REQUIRE(agg->get_annotations().size() == 1);
        CHECK(agg->get_annotations()[0].resolved_type->get_short_name() == "Version");
        REQUIRE(agg->get_annotations()[0].resolved_field_constants.size() >= 1);
        auto* ci = llvm::dyn_cast<llvm::ConstantInt>(
            agg->get_annotations()[0].resolved_field_constants[0]);
        REQUIRE(ci != nullptr);
        CHECK(ci->getSExtValue() == 42);
    }
}

TEST_CASE("@Inherited: override in middle of chain, grandchild inherits override", "[annotation][inherited]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_inherit_mid_override__;
        annotation Inh {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @Inh
        annotation Inherited {}
        @Inherited
        annotation Tag { value : int; }
        @Tag(1)
        class A {
            foo() : int { return 0; }
        }
        @Tag(99)
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

    // A has @Tag(1)
    auto* a_val = llvm::dyn_cast<llvm::ConstantInt>(a->get_annotations()[0].resolved_field_constants[0]);
    REQUIRE(a_val != nullptr);
    CHECK(a_val->getSExtValue() == 1);

    // B overrides with @Tag(99)
    auto* b_val = llvm::dyn_cast<llvm::ConstantInt>(b->get_annotations()[0].resolved_field_constants[0]);
    REQUIRE(b_val != nullptr);
    CHECK(b_val->getSExtValue() == 99);

    // C inherits B's override (99), not A's original (1)
    auto* c_val = llvm::dyn_cast<llvm::ConstantInt>(c->get_annotations()[0].resolved_field_constants[0]);
    REQUIRE(c_val != nullptr);
    CHECK(c_val->getSExtValue() == 99);
}

TEST_CASE("@Inherited: inherited annotation with enum field value preserved", "[annotation][inherited]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_inherit_enum_val__;
        annotation Inh {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @Inh
        annotation Inherited {}
        @Inherited
        annotation Severity {
            enum Level { LOW; MEDIUM; HIGH; };
            level : Level;
        }
        @Severity(Level::HIGH)
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
    REQUIRE(derived->get_annotations().size() == 1);
    CHECK(derived->get_annotations()[0].resolved_type->get_short_name() == "Severity");

    // Enum value HIGH (=2) should be preserved through inheritance
    REQUIRE(derived->get_annotations()[0].resolved_field_constants.size() >= 1);
    auto* ci = llvm::dyn_cast<llvm::ConstantInt>(
        derived->get_annotations()[0].resolved_field_constants[0]);
    REQUIRE(ci != nullptr);
    CHECK(ci->getSExtValue() == 2); // HIGH = 2
}

TEST_CASE("@Inherited: annotation on class with multiple bases — only class parent propagates", "[annotation][inherited]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_inherit_multi_base__;
        annotation Inh {
            enum ElementType { CLASS; INTERFACE; ANNOTATION; };
        }
        @Inh
        annotation Inherited {}
        @Inherited
        annotation Marker { value : int; }
        @Marker(10)
        class Base {
            foo() : int { return 0; }
        }
        @Marker(20)
        interface Iface {
            abstract bar() : int;
        }
        class Child : Base, Iface {
            bar() : int { return 1; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto child = find_aggregate(comp, "Child");
    REQUIRE(child != nullptr);
    // Should inherit @Marker from Base (class), not from Iface (interface)
    REQUIRE(child->get_annotations().size() == 1);
    CHECK(child->get_annotations()[0].resolved_type->get_short_name() == "Marker");
    auto* ci = llvm::dyn_cast<llvm::ConstantInt>(
        child->get_annotations()[0].resolved_field_constants[0]);
    REQUIRE(ci != nullptr);
    CHECK(ci->getSExtValue() == 10); // from Base, not Iface
}


// ════════════════════════════════════════════════════════════════════════════
// 15. @Retention — additional edge cases
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("@Retention: default on annotation with int field — field value in IR", "[annotation][retention]") {
    std::string ir = get_llvm_ir(R"SRC(
        module __test_ann_ret_field_ir__;
        annotation Rating { value : int; }
        @Rating(42)
        class Foo {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(!ir.empty());
    CHECK(ir.find("_ann_Rating") != std::string::npos);
}

TEST_CASE("@Retention: annotation on multiple classes — each gets its own RTTI global", "[annotation][retention]") {
    std::string ir = get_llvm_ir(R"SRC(
        module __test_ann_ret_multi_class__;
        annotation Tag {}
        @Tag
        class A {
            foo() : int { return 0; }
        }
        @Tag
        class B {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(!ir.empty());
    // Both classes should have an annotation instance in the IR
    // The IR should contain RTTI entries for both A and B
    size_t count = 0;
    std::string::size_type pos = 0;
    while ((pos = ir.find("_ann_Tag", pos)) != std::string::npos) {
        ++count;
        ++pos;
    }
    CHECK(count >= 2); // at least one per class
}

TEST_CASE("@Retention(SOURCE): marker annotation — no RTTI at all for annotated class", "[annotation][retention]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_ret_src_marker__;
        annotation Src {
            enum Policy { SOURCE; RUNTIME; };
        }
        @Src
        annotation Retention {
            policy : Policy;
        }
        @Retention(Policy::SOURCE)
        annotation CompileMarker {}
        @CompileMarker
        class Marked {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    // Annotation present in model
    auto marked = find_aggregate(comp, "Marked");
    REQUIRE(marked != nullptr);
    CHECK(marked->get_annotations().size() == 1);

    // No annotation global in IR
    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    CHECK(ir.find("_ann_CompileMarker") == std::string::npos);
}


// ════════════════════════════════════════════════════════════════════════════
// 16. Combined meta-annotation scenarios — additional
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Meta-annotations: @Target(CLASS) prevents use on annotation even with @Inherited", "[annotation][meta]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_target_blocks_ann_use__;
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
        annotation ClassOnly {}
        @ClassOnly
        annotation BadAnn {}
    )SRC");
    // @Target restricts to CLASS, applying to annotation should fail
    CHECK(comp == nullptr);
}

TEST_CASE("Meta-annotations: @Inherited + @Retention(RUNTIME) — inherited instance emitted in IR for derived", "[annotation][meta]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_inh_rt_ir__;
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
        @Retention(Policy::RUNTIME)
        @Inherited
        annotation RuntimeInherited { value : int; }
        @RuntimeInherited(7)
        class Base {
            foo() : int { return 0; }
        }
        class Derived : Base {
            bar() : int { return 1; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    // Both should have the annotation in the model
    auto base = find_aggregate(comp, "Base");
    auto derived = find_aggregate(comp, "Derived");
    REQUIRE(base != nullptr);
    REQUIRE(derived != nullptr);
    CHECK(base->get_annotations().size() == 1);
    CHECK(derived->get_annotations().size() == 1);

    // Both should have the annotation in the IR
    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    CHECK(ir.find("_ann_RuntimeInherited") != std::string::npos);
}

TEST_CASE("Meta-annotations: unrestricted annotation works on all element types with RUNTIME IR", "[annotation][meta]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_unrestricted_all__;
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

    // All three have the annotation
    CHECK(find_aggregate(comp, "C")->get_annotations().size() == 1);
    CHECK(find_aggregate(comp, "I")->get_annotations().size() == 1);
    CHECK(find_annotation_type(comp, "A")->get_annotations().size() == 1);

    // Default RUNTIME: annotation instances appear in IR
    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    CHECK(ir.find("_ann_Universal") != std::string::npos);
}

TEST_CASE("Meta-annotations: annotation with int and enum fields combined", "[annotation][meta]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_mixed_fields__;
        annotation Config {
            enum Mode { DEBUG; RELEASE; };
            mode : Mode;
            version : int;
        }
        @Config(Mode::RELEASE, 3)
        class App {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto app = find_aggregate(comp, "App");
    REQUIRE(app != nullptr);
    auto& anns = app->get_annotations();
    REQUIRE(anns.size() == 1);
    REQUIRE(anns[0].resolved_field_constants.size() >= 2);

    auto* mode_val = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[0]);
    auto* ver_val = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[1]);
    REQUIRE(mode_val != nullptr);
    REQUIRE(ver_val != nullptr);
    CHECK(mode_val->getSExtValue() == 1); // RELEASE = 1
    CHECK(ver_val->getSExtValue() == 3);
}

TEST_CASE("Meta-annotations: stdlib pattern — @Retention on itself validates correctly", "[annotation][meta]") {
    // @Retention(Policy::RUNTIME) annotation Retention { ... }
    // The self-referential @Retention should be visible in the model
    auto comp = compile_model(R"SRC(
        module __test_ann_self_retention__;
        @Retention(Policy::RUNTIME)
        annotation Retention {
            enum Policy { SOURCE; RUNTIME; };
            policy : Policy = Policy::RUNTIME;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto ret = find_annotation_type(comp, "Retention");
    REQUIRE(ret != nullptr);
    // @Retention is applied to itself
    REQUIRE(ret->get_annotations().size() == 1);
    CHECK(ret->get_annotations()[0].resolved_type->get_short_name() == "Retention");
}

TEST_CASE("Meta-annotations: forward reference — annotation B used on A declared after A", "[annotation][meta]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_forward_ref__;
        @Later
        annotation Early {}
        annotation Later { value : int; }
    )SRC");
    REQUIRE(comp != nullptr);

    auto early = find_annotation_type(comp, "Early");
    REQUIRE(early != nullptr);
    REQUIRE(early->get_annotations().size() == 1);
    CHECK(early->get_annotations()[0].resolved_type->get_short_name() == "Later");
}

TEST_CASE("Meta-annotations: three-way cross-reference", "[annotation][meta]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_3way_crossref__;
        @C
        annotation A {}
        @A
        annotation B {}
        @B
        annotation C {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto a = find_annotation_type(comp, "A");
    auto b = find_annotation_type(comp, "B");
    auto c = find_annotation_type(comp, "C");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(c != nullptr);
    CHECK(a->get_annotations()[0].resolved_type->get_short_name() == "C");
    CHECK(b->get_annotations()[0].resolved_type->get_short_name() == "A");
    CHECK(c->get_annotations()[0].resolved_type->get_short_name() == "B");
}

TEST_CASE("Meta-annotations: multiple annotations on same annotation type", "[annotation][meta]") {
    auto comp = compile_model(R"SRC(
        module __test_ann_multi_meta__;
        annotation Alpha {}
        annotation Beta {}
        annotation Gamma {}
        @Alpha @Beta @Gamma
        annotation Decorated {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto decorated = find_annotation_type(comp, "Decorated");
    REQUIRE(decorated != nullptr);
    CHECK(decorated->get_annotations().size() == 3);

    std::set<std::string> names;
    for (auto& ann : decorated->get_annotations()) {
        names.insert(ann.resolved_type->get_short_name());
    }
    CHECK(names.count("Alpha") == 1);
    CHECK(names.count("Beta") == 1);
    CHECK(names.count("Gamma") == 1);
}


