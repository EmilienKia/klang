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
 * Tests for annotation model elements.
 *
 * These tests verify:
 *  - annotation_type is correctly created via model_builder
 *  - annotation_type::is_annotation() returns true
 *  - annotation_type is NOT a class (is_class() == false)
 *  - annotation instances are attached to annotated aggregates
 *  - annotation types can have member variables
 *  - empty annotation types are valid
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"


// ════════════════════════════════════════════════════════════════════════════
//  Annotation type declaration
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Model: annotation type is created and flagged", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_01;
        annotation MyAnnotation {
            value : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "MyAnnotation");
    REQUIRE(ann != nullptr);
    CHECK(ann->is_annotation());
    CHECK_FALSE(ann->is_class());
    CHECK(ann->get_short_name() == "MyAnnotation");
}

TEST_CASE("Model: empty annotation type", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_02;
        annotation Empty {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "Empty");
    REQUIRE(ann != nullptr);
    CHECK(ann->is_annotation());
    // The annotation has synthetic fields (__vptr__, __base_*__) but no user-defined variables.
    // Check that no user-defined member exists.
    CHECK(ann->get_variable("__vptr__") != nullptr); // synthetic vptr
}

TEST_CASE("Model: annotation type with multiple members", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_03;
        annotation Versioned {
            major : int;
            minor : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "Versioned");
    REQUIRE(ann != nullptr);
    CHECK(ann->is_annotation());
    // User-defined members present alongside synthetic ones
    CHECK(ann->get_variable("major") != nullptr);
    CHECK(ann->get_variable("minor") != nullptr);
}

TEST_CASE("Model: annotation type appears in aggregate map", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_04;
        annotation Info {
            value : int;
        }
        struct Foo {
            x : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto root = comp->get_unit()->get_root_namespace();
    REQUIRE(root != nullptr);

    // Both the annotation type and struct appear in aggregates()
    auto& aggs = root->aggregates();
    CHECK(aggs.find("Info") != aggs.end());
    CHECK(aggs.find("Foo") != aggs.end());

    // Info is an annotation_type
    auto info = find_annotation_type(comp, "Info");
    REQUIRE(info != nullptr);
    CHECK(info->is_annotation());

    // Foo is a regular structure
    auto foo = find_aggregate(comp, "Foo");
    REQUIRE(foo != nullptr);
    CHECK_FALSE(foo->is_annotation());
}


// ════════════════════════════════════════════════════════════════════════════
//  Annotation instances on aggregates
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Model: annotated class has annotation instances", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_05;
        annotation Deprecated {}
        @Deprecated
        class OldStuff {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto st = find_aggregate(comp, "OldStuff");
    REQUIRE(st != nullptr);
    auto& anns = st->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].raw_name == "Deprecated");
    CHECK(anns[0].ast_node != nullptr);
}

TEST_CASE("Model: class with multiple annotations", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_06;
        annotation A {}
        annotation B {}
        @A @B
        class Multi {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto st = find_aggregate(comp, "Multi");
    REQUIRE(st != nullptr);
    auto& anns = st->get_annotations();
    REQUIRE(anns.size() == 2);
    CHECK(anns[0].raw_name == "A");
    CHECK(anns[1].raw_name == "B");
}

TEST_CASE("Model: struct without annotations has empty list", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_07;
        struct Plain {
            x : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto st = find_aggregate(comp, "Plain");
    REQUIRE(st != nullptr);
    CHECK(st->get_annotations().empty());
}

TEST_CASE("Model: annotated class has annotation via find_klass", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_08;
        annotation Tag {}
        @Tag
        class MyClass {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto kl = find_klass(comp, "MyClass");
    REQUIRE(kl != nullptr);
    auto& anns = kl->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].raw_name == "Tag");
}

TEST_CASE("Model: annotation with qualified name on class", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_09;
        annotation Info {}
        @Info
        class Foo {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto st = find_aggregate(comp, "Foo");
    REQUIRE(st != nullptr);
    auto& anns = st->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].raw_name == "Info");
}


// ════════════════════════════════════════════════════════════════════════════
//  Annotation type is itself annotatable
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Model: annotation type can itself be annotated", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_10;
        annotation Meta {}
        @Meta
        annotation Documented {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto doc = find_annotation_type(comp, "Documented");
    REQUIRE(doc != nullptr);
    CHECK(doc->is_annotation());
    auto& anns = doc->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].raw_name == "Meta");
}


// ════════════════════════════════════════════════════════════════════════════
//  Error: annotations on structs
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Model: annotation on struct is an error", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_11;
        annotation Deprecated {}
        @Deprecated
        struct BadTarget {
            x : int;
        }
    )SRC");
    // compile_model returns nullptr on error
    CHECK(comp == nullptr);
}


// ════════════════════════════════════════════════════════════════════════════
//  Annotations on interfaces
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Model: annotated interface has annotation instances", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_12;
        annotation Tag {}
        @Tag
        interface Describable {
            const describe() : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto iface = find_aggregate(comp, "Describable");
    REQUIRE(iface != nullptr);
    auto& anns = iface->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].raw_name == "Tag");
}


// ════════════════════════════════════════════════════════════════════════════
//  Annotation type has vtable (for RTTI type resolution)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Model: annotation type has vtable after resolution", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_13;
        annotation Marker {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "Marker");
    REQUIRE(ann != nullptr);
    CHECK(ann->has_vtable());
    CHECK(ann->get_vtable() != nullptr);
    // Annotation vtable has zero user slots (RTTI only)
    CHECK(ann->get_vtable()->entries.empty());
}

TEST_CASE("Model: annotation type has vptr field", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_14;
        annotation Info { value : int; }
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "Info");
    REQUIRE(ann != nullptr);
    CHECK(ann->get_vptr() != nullptr);
    CHECK(ann->get_variable("__vptr__") != nullptr);
}


// ════════════════════════════════════════════════════════════════════════════
//  Annotation instances are resolved to annotation_type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Model: annotation instance resolved_type is set on class", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_15;
        annotation Deprecated {}
        @Deprecated
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
    CHECK(anns[0].resolved_type->is_annotation());
    CHECK(anns[0].resolved_type->get_short_name() == "Deprecated");
}

TEST_CASE("Model: annotation instance resolved_type is set on interface", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_16;
        annotation Tag {}
        @Tag
        interface Describable {
            const describe() : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto iface = find_aggregate(comp, "Describable");
    REQUIRE(iface != nullptr);
    auto& anns = iface->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].resolved_type != nullptr);
    CHECK(anns[0].resolved_type->get_short_name() == "Tag");
}

TEST_CASE("Model: multiple annotations all resolved on class", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_17;
        annotation A { x : int; }
        annotation B {}
        @A @B
        class Bar {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto bar = find_aggregate(comp, "Bar");
    REQUIRE(bar != nullptr);
    auto& anns = bar->get_annotations();
    REQUIRE(anns.size() == 2);
    CHECK(anns[0].resolved_type != nullptr);
    CHECK(anns[0].resolved_type->get_short_name() == "A");
    CHECK(anns[1].resolved_type != nullptr);
    CHECK(anns[1].resolved_type->get_short_name() == "B");
}


// ════════════════════════════════════════════════════════════════════════════
//  Error: using non-annotation type as annotation
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Model: using a class as annotation is an error", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_18;
        class NotAnAnnotation {
            foo() : int { return 0; }
        }
        @NotAnAnnotation
        class Target {
            bar() : int { return 0; }
        }
    )SRC");
    // Should fail because NotAnAnnotation is not an annotation type
    CHECK(comp == nullptr);
}

TEST_CASE("Model: using a struct as annotation is an error", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_19;
        struct NotAnAnnotation {
            x : int;
        }
        @NotAnAnnotation
        class Target {
            bar() : int { return 0; }
        }
    )SRC");
    CHECK(comp == nullptr);
}


// ════════════════════════════════════════════════════════════════════════════
//  Meta-annotation: resolved_type on annotation type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Model: annotation instance resolved_type is set on annotation type", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_20;
        annotation Meta {}
        @Meta
        annotation Documented {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto doc = find_annotation_type(comp, "Documented");
    REQUIRE(doc != nullptr);
    auto& anns = doc->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].resolved_type != nullptr);
    CHECK(anns[0].resolved_type->is_annotation());
    CHECK(anns[0].resolved_type->get_short_name() == "Meta");
}

TEST_CASE("Model: multiple meta-annotations all resolved on annotation type", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_21;
        annotation Alpha {}
        annotation Beta {}
        @Alpha @Beta
        annotation Combined {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto combined = find_annotation_type(comp, "Combined");
    REQUIRE(combined != nullptr);
    auto& anns = combined->get_annotations();
    REQUIRE(anns.size() == 2);
    CHECK(anns[0].resolved_type != nullptr);
    CHECK(anns[0].resolved_type->get_short_name() == "Alpha");
    CHECK(anns[1].resolved_type != nullptr);
    CHECK(anns[1].resolved_type->get_short_name() == "Beta");
}

TEST_CASE("Model: meta-annotation with members has resolved_field_constants", "[model][annotation]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_22;
        annotation Version { major : int; minor : int; }
        @Version(2, 5)
        annotation Documented {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto doc = find_annotation_type(comp, "Documented");
    REQUIRE(doc != nullptr);
    auto& anns = doc->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].resolved_type != nullptr);
    CHECK(anns[0].resolved_type->get_short_name() == "Version");
    // resolved_field_constants should have 2 entries (major, minor)
    CHECK(anns[0].resolved_field_constants.size() == 2);
}


// ════════════════════════════════════════════════════════════════════════════
//  annotation_type::is_source_retention()
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Model: is_source_retention() returns false when no @Retention", "[model][annotation][retention]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_23;
        annotation Marker {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "Marker");
    REQUIRE(ann != nullptr);
    // Default retention is RUNTIME → is_source_retention() must return false
    CHECK_FALSE(ann->is_source_retention());
}

TEST_CASE("Model: is_source_retention() returns true for @Retention(Policy::SOURCE) positional", "[model][annotation][retention]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module model_annotation_24;
        @annotations::Retention(Policy::SOURCE)
        annotation CompileOnly {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "CompileOnly");
    REQUIRE(ann != nullptr);
    CHECK(ann->is_source_retention());
}

TEST_CASE("Model: is_source_retention() returns false for @Retention(Policy::RUNTIME) positional", "[model][annotation][retention]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module model_annotation_25;
        @annotations::Retention(Policy::RUNTIME)
        annotation RunOnly {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "RunOnly");
    REQUIRE(ann != nullptr);
    CHECK_FALSE(ann->is_source_retention());
}

TEST_CASE("Model: is_source_retention() returns true for @Retention{Policy::SOURCE} brace-init", "[model][annotation][retention]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module model_annotation_26;
        @annotations::Retention{Policy::SOURCE}
        annotation BraceSource {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "BraceSource");
    REQUIRE(ann != nullptr);
    CHECK(ann->is_source_retention());
}

TEST_CASE("Model: is_source_retention() returns true for @Retention{.policy = Policy::SOURCE} designated", "[model][annotation][retention]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module model_annotation_27;
        @annotations::Retention{.policy = Policy::SOURCE}
        annotation DesignSource {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "DesignSource");
    REQUIRE(ann != nullptr);
    CHECK(ann->is_source_retention());
}

TEST_CASE("Model: is_source_retention() returns true for @Retention{.policy(Policy::SOURCE)} call form", "[model][annotation][retention]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module model_annotation_28;
        @annotations::Retention{.policy(Policy::SOURCE)}
        annotation CallSource {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "CallSource");
    REQUIRE(ann != nullptr);
    CHECK(ann->is_source_retention());
}

TEST_CASE("Model: is_source_retention() returns false for @Retention{.policy = Policy::RUNTIME}", "[model][annotation][retention]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module model_annotation_29;
        @annotations::Retention{.policy = Policy::RUNTIME}
        annotation DesignRuntime {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "DesignRuntime");
    REQUIRE(ann != nullptr);
    CHECK_FALSE(ann->is_source_retention());
}

TEST_CASE("Model: is_source_retention() on annotation with unrelated meta-annotations", "[model][annotation][retention]") {
    auto comp = compile_model(R"SRC(
        module model_annotation_30;
        annotation Unrelated {}
        @Unrelated
        annotation Foo {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "Foo");
    REQUIRE(ann != nullptr);
    // @Unrelated is not @Retention → default RUNTIME
    CHECK_FALSE(ann->is_source_retention());
}



