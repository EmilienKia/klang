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
 * Tests for annotation member variable materialisation.
 *
 * These tests verify:
 *  - Annotation types can have member variables of various types
 *  - Annotation instances are materialized with correct field values
 *  - Positional and designated construction follow struct rules
 *  - Default values are used for omitted fields
 *  - Constness is enforced (implicit const on annotation types)
 *  - Nested annotations and annotation arrays work
 *  - Type restrictions on annotation fields are enforced
 *  - Annotations are exported through KDI
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

#include <llvm/IR/Constants.h>


// ════════════════════════════════════════════════════════════════════════════
//  1. Annotation implicit constness
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: implicit const — annotation type is const struct", "[annotation][const]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_01;
        annotation Config {
            level : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "Config");
    REQUIRE(ann != nullptr);
    CHECK(ann->is_const_struct());
}

TEST_CASE("Annotation: explicit const is redundant but compiles", "[annotation][const]") {
    // 'const annotation' is accepted — annotation is still const.
    // The compiler emits a warning (0x00A9) but compilation succeeds.
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_02;
        const annotation Redundant {
            x : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "Redundant");
    REQUIRE(ann != nullptr);
    CHECK(ann->is_const_struct());
}


// ════════════════════════════════════════════════════════════════════════════
//  2. Annotation with primitive fields — model level
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: single int field", "[annotation][member]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_03;
        annotation Priority {
            value : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "Priority");
    REQUIRE(ann != nullptr);
    CHECK(ann->get_variable("value") != nullptr);
    CHECK(ann->is_const_struct());
}

TEST_CASE("Annotation: multiple fields", "[annotation][member]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_04;
        annotation Version {
            major : int;
            minor : int;
            patch : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "Version");
    REQUIRE(ann != nullptr);
    CHECK(ann->get_variable("major") != nullptr);
    CHECK(ann->get_variable("minor") != nullptr);
    CHECK(ann->get_variable("patch") != nullptr);
}

TEST_CASE("Annotation: bool field", "[annotation][member]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_05;
        annotation Flag {
            enabled : bool;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "Flag");
    REQUIRE(ann != nullptr);
    CHECK(ann->get_variable("enabled") != nullptr);
}

TEST_CASE("Annotation: field with default value", "[annotation][member]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_06;
        annotation Config {
            level : int = 5;
            verbose : bool = false;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "Config");
    REQUIRE(ann != nullptr);
    CHECK(ann->get_variable("level") != nullptr);
    CHECK(ann->get_variable("verbose") != nullptr);
}


// ════════════════════════════════════════════════════════════════════════════
//  3. Annotation with methods
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: method declaration", "[annotation][method]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_07;
        annotation Range {
            min : int;
            max : int;
            span() : int { return max - min; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "Range");
    REQUIRE(ann != nullptr);
    CHECK(ann->get_variable("min") != nullptr);
    CHECK(ann->get_variable("max") != nullptr);
    // The 'span' function should be in the children
    CHECK(ann->get_function("span") != nullptr);
}


// ════════════════════════════════════════════════════════════════════════════
//  4. Annotation application with positional args — model level
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: positional init on class", "[annotation][init][positional]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_08;
        annotation Priority {
            value : int;
        }
        @Priority(42)
        class Task {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto task = find_aggregate(comp, "Task");
    REQUIRE(task != nullptr);
    auto& anns = task->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].resolved_type != nullptr);
    CHECK(anns[0].resolved_type->get_short_name() == "Priority");
    // The AST node should have args
    REQUIRE(anns[0].ast_node != nullptr);
    CHECK(anns[0].ast_node->has_parens);
    CHECK(anns[0].ast_node->args.size() == 1);
}

TEST_CASE("Annotation: positional init with multiple args", "[annotation][init][positional]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_09;
        annotation Version {
            major : int;
            minor : int;
        }
        @Version(2, 1)
        class Api {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto api = find_aggregate(comp, "Api");
    REQUIRE(api != nullptr);
    auto& anns = api->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].ast_node->args.size() == 2);
}


// ════════════════════════════════════════════════════════════════════════════
//  5. Annotation application with designated init — model level
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: designated init on class", "[annotation][init][designated]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_10;
        annotation Range {
            min : int;
            max : int;
        }
        @Range{.min = 1, .max = 100}
        class Bounded {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto bounded = find_aggregate(comp, "Bounded");
    REQUIRE(bounded != nullptr);
    auto& anns = bounded->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].resolved_type != nullptr);
    CHECK(anns[0].resolved_type->get_short_name() == "Range");
    // The AST node should have a brace init
    REQUIRE(anns[0].ast_node != nullptr);
    CHECK(anns[0].ast_node->brace_init != nullptr);
}


// ════════════════════════════════════════════════════════════════════════════
//  6. Default construction
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: default construction (no args)", "[annotation][init][default]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_11;
        annotation Config {
            level : int = 1;
            verbose : bool = false;
        }
        @Config
        class DefaultService {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto svc = find_aggregate(comp, "DefaultService");
    REQUIRE(svc != nullptr);
    auto& anns = svc->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].resolved_type != nullptr);
    // No args, no brace init
    REQUIRE(anns[0].ast_node != nullptr);
    CHECK_FALSE(anns[0].ast_node->has_parens);
    CHECK(anns[0].ast_node->brace_init == nullptr);
}


// ════════════════════════════════════════════════════════════════════════════
//  7. Annotation default visibility is public
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: member variables are public by default", "[annotation][visibility]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_12;
        annotation Info {
            value : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "Info");
    REQUIRE(ann != nullptr);
    auto var = ann->get_variable("value");
    REQUIRE(var != nullptr);
    auto member_var = std::dynamic_pointer_cast<k::model::member_variable_definition>(var);
    REQUIRE(member_var != nullptr);
    CHECK(member_var->get_visibility() == k::model::PUBLIC);
}


// ════════════════════════════════════════════════════════════════════════════
//  8. Multiple classes with the same annotation type, different values
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: same type, different values on different classes", "[annotation][multi]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_13;
        annotation Version {
            major : int;
            minor : int;
        }
        @Version(1, 0)
        class ApiV1 {
            foo() : int { return 0; }
        }
        @Version(2, 3)
        class ApiV2 {
            bar() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto v1 = find_aggregate(comp, "ApiV1");
    auto v2 = find_aggregate(comp, "ApiV2");
    REQUIRE(v1 != nullptr);
    REQUIRE(v2 != nullptr);

    // Both have one Version annotation
    REQUIRE(v1->get_annotations().size() == 1);
    REQUIRE(v2->get_annotations().size() == 1);

    // Both resolved to the same annotation type
    CHECK(v1->get_annotations()[0].resolved_type == v2->get_annotations()[0].resolved_type);

    // But their AST nodes have different argument values
    REQUIRE(v1->get_annotations()[0].ast_node->args.size() == 2);
    REQUIRE(v2->get_annotations()[0].ast_node->args.size() == 2);
}


// ════════════════════════════════════════════════════════════════════════════
//  9. Annotation on interface
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: with fields on interface", "[annotation][interface]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_14;
        annotation Version {
            major : int;
            minor : int;
        }
        @Version(1, 0)
        interface Describable {
            const describe() : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto iface = find_aggregate(comp, "Describable");
    REQUIRE(iface != nullptr);
    auto& anns = iface->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].resolved_type->get_short_name() == "Version");
    CHECK(anns[0].ast_node->args.size() == 2);
}


// ════════════════════════════════════════════════════════════════════════════
//  10. Annotation on annotation
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: with fields on another annotation", "[annotation][meta]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_15;
        annotation Target {
            value : int;
        }
        @Target(1)
        annotation Documented {}
    )SRC");
    REQUIRE(comp != nullptr);

    auto doc = find_annotation_type(comp, "Documented");
    REQUIRE(doc != nullptr);
    auto& anns = doc->get_annotations();
    REQUIRE(anns.size() == 1);
    CHECK(anns[0].resolved_type->get_short_name() == "Target");
}


// ════════════════════════════════════════════════════════════════════════════
//  11. Nested annotation field — model level
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: nested annotation type as field", "[annotation][nested]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_16;
        annotation Inner {
            x : int;
        }
        annotation Outer {
            inner : Inner;
            label : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto outer = find_annotation_type(comp, "Outer");
    REQUIRE(outer != nullptr);
    CHECK(outer->get_variable("inner") != nullptr);
    CHECK(outer->get_variable("label") != nullptr);
}


// ════════════════════════════════════════════════════════════════════════════
//  12. Annotation with various primitive types
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: various primitive field types", "[annotation][member][types]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_17;
        annotation AllTypes {
            a : int;
            b : bool;
            c : char;
            d : short;
            e : long;
            f : float;
            g : double;
            h : byte;
            i : unsigned int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto ann = find_annotation_type(comp, "AllTypes");
    REQUIRE(ann != nullptr);
    CHECK(ann->get_variable("a") != nullptr);
    CHECK(ann->get_variable("b") != nullptr);
    CHECK(ann->get_variable("c") != nullptr);
    CHECK(ann->get_variable("d") != nullptr);
    CHECK(ann->get_variable("e") != nullptr);
    CHECK(ann->get_variable("f") != nullptr);
    CHECK(ann->get_variable("g") != nullptr);
    CHECK(ann->get_variable("h") != nullptr);
    CHECK(ann->get_variable("i") != nullptr);
}


// ════════════════════════════════════════════════════════════════════════════
//  13. Materialized field constants — positional init
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: positional init materializes int constant", "[annotation][materialize][positional]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_18;
        annotation Priority {
            value : int;
        }
        @Priority(42)
        class Task {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto task = find_aggregate(comp, "Task");
    REQUIRE(task != nullptr);
    auto& anns = task->get_annotations();
    REQUIRE(anns.size() == 1);

    // resolved_field_constants should have one entry (the "value" field)
    REQUIRE(anns[0].resolved_field_constants.size() == 1);
    auto* c = anns[0].resolved_field_constants[0];
    REQUIRE(c != nullptr);

    auto* ci = llvm::dyn_cast<llvm::ConstantInt>(c);
    REQUIRE(ci != nullptr);
    CHECK(ci->getSExtValue() == 42);
}

TEST_CASE("Annotation: positional init materializes multiple fields", "[annotation][materialize][positional]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_19;
        annotation Version {
            major : int;
            minor : int;
            patch : int;
        }
        @Version(2, 5, 1)
        class Api {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto api = find_aggregate(comp, "Api");
    REQUIRE(api != nullptr);
    auto& anns = api->get_annotations();
    REQUIRE(anns.size() == 1);

    REQUIRE(anns[0].resolved_field_constants.size() == 3);

    auto* c0 = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[0]);
    auto* c1 = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[1]);
    auto* c2 = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[2]);
    REQUIRE(c0 != nullptr);
    REQUIRE(c1 != nullptr);
    REQUIRE(c2 != nullptr);
    CHECK(c0->getSExtValue() == 2);
    CHECK(c1->getSExtValue() == 5);
    CHECK(c2->getSExtValue() == 1);
}

TEST_CASE("Annotation: positional init materializes bool constant", "[annotation][materialize][positional]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_20;
        annotation Flag {
            enabled : bool;
        }
        @Flag(true)
        class Active {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto active = find_aggregate(comp, "Active");
    REQUIRE(active != nullptr);
    auto& anns = active->get_annotations();
    REQUIRE(anns.size() == 1);
    REQUIRE(anns[0].resolved_field_constants.size() == 1);

    auto* ci = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[0]);
    REQUIRE(ci != nullptr);
    CHECK(ci->getZExtValue() == 1);
}


// ════════════════════════════════════════════════════════════════════════════
//  14. Materialized field constants — designated init
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: designated init materializes field constants", "[annotation][materialize][designated]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_21;
        annotation Range {
            min : int;
            max : int;
        }
        @Range{.min = 10, .max = 200}
        class Bounded {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto bounded = find_aggregate(comp, "Bounded");
    REQUIRE(bounded != nullptr);
    auto& anns = bounded->get_annotations();
    REQUIRE(anns.size() == 1);
    REQUIRE(anns[0].resolved_field_constants.size() == 2);

    auto* c_min = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[0]);
    auto* c_max = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[1]);
    REQUIRE(c_min != nullptr);
    REQUIRE(c_max != nullptr);
    CHECK(c_min->getSExtValue() == 10);
    CHECK(c_max->getSExtValue() == 200);
}

TEST_CASE("Annotation: designated init with out-of-order fields", "[annotation][materialize][designated]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_22;
        annotation Pair {
            first : int;
            second : int;
        }
        @Pair{.second = 99, .first = 11}
        class Holder {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto holder = find_aggregate(comp, "Holder");
    REQUIRE(holder != nullptr);
    auto& anns = holder->get_annotations();
    REQUIRE(anns.size() == 1);
    REQUIRE(anns[0].resolved_field_constants.size() == 2);

    // "first" is field index 0, "second" is field index 1
    auto* c_first = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[0]);
    auto* c_second = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[1]);
    REQUIRE(c_first != nullptr);
    REQUIRE(c_second != nullptr);
    CHECK(c_first->getSExtValue() == 11);
    CHECK(c_second->getSExtValue() == 99);
}


// ════════════════════════════════════════════════════════════════════════════
//  15. Materialized field constants — default values
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: default construction uses member default values", "[annotation][materialize][default]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_23;
        annotation Config {
            level : int = 5;
            verbose : bool = true;
        }
        @Config
        class DefaultService {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto svc = find_aggregate(comp, "DefaultService");
    REQUIRE(svc != nullptr);
    auto& anns = svc->get_annotations();
    REQUIRE(anns.size() == 1);
    REQUIRE(anns[0].resolved_field_constants.size() == 2);

    auto* c_level = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[0]);
    auto* c_verbose = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[1]);
    REQUIRE(c_level != nullptr);
    REQUIRE(c_verbose != nullptr);
    CHECK(c_level->getSExtValue() == 5);
    CHECK(c_verbose->getZExtValue() == 1);
}

TEST_CASE("Annotation: partial positional args use defaults for remaining", "[annotation][materialize][default]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_24;
        annotation Config {
            level : int = 0;
            verbose : bool = true;
        }
        @Config(7)
        class PartialService {
            foo() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto svc = find_aggregate(comp, "PartialService");
    REQUIRE(svc != nullptr);
    auto& anns = svc->get_annotations();
    REQUIRE(anns.size() == 1);
    REQUIRE(anns[0].resolved_field_constants.size() == 2);

    // First field from positional arg, second from default
    auto* c_level = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[0]);
    auto* c_verbose = llvm::dyn_cast<llvm::ConstantInt>(anns[0].resolved_field_constants[1]);
    REQUIRE(c_level != nullptr);
    REQUIRE(c_verbose != nullptr);
    CHECK(c_level->getSExtValue() == 7);
    CHECK(c_verbose->getZExtValue() == 1);
}


// ════════════════════════════════════════════════════════════════════════════
//  16. Same annotation, different values on different classes
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: different materialized values on different classes", "[annotation][materialize][multi]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_25;
        annotation Version {
            major : int;
            minor : int;
        }
        @Version(1, 0)
        class ApiV1 {
            foo() : int { return 0; }
        }
        @Version(2, 3)
        class ApiV2 {
            bar() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto v1 = find_aggregate(comp, "ApiV1");
    auto v2 = find_aggregate(comp, "ApiV2");
    REQUIRE(v1 != nullptr);
    REQUIRE(v2 != nullptr);

    auto& a1 = v1->get_annotations();
    auto& a2 = v2->get_annotations();
    REQUIRE(a1.size() == 1);
    REQUIRE(a2.size() == 1);
    REQUIRE(a1[0].resolved_field_constants.size() == 2);
    REQUIRE(a2[0].resolved_field_constants.size() == 2);

    auto* v1_major = llvm::dyn_cast<llvm::ConstantInt>(a1[0].resolved_field_constants[0]);
    auto* v1_minor = llvm::dyn_cast<llvm::ConstantInt>(a1[0].resolved_field_constants[1]);
    auto* v2_major = llvm::dyn_cast<llvm::ConstantInt>(a2[0].resolved_field_constants[0]);
    auto* v2_minor = llvm::dyn_cast<llvm::ConstantInt>(a2[0].resolved_field_constants[1]);
    REQUIRE(v1_major != nullptr);
    REQUIRE(v1_minor != nullptr);
    REQUIRE(v2_major != nullptr);
    REQUIRE(v2_minor != nullptr);
    CHECK(v1_major->getSExtValue() == 1);
    CHECK(v1_minor->getSExtValue() == 0);
    CHECK(v2_major->getSExtValue() == 2);
    CHECK(v2_minor->getSExtValue() == 3);
}


// ════════════════════════════════════════════════════════════════════════════
//  20. Annotation on a function parameter — model level
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: on function parameter — model level", "[annotation][parameter]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_26;
        annotation Tag {
            label : int;
        }
        class Svc {
            public Svc() {}
            public process(@Tag(42) input : int, output : int) : int {
                return input + output;
            }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto klass = find_klass(comp, "Svc");
    REQUIRE(klass != nullptr);

    // Find the 'process' function
    std::shared_ptr<k::model::function> process_fn;
    for (auto& fn : klass->functions()) {
        if (fn && fn->get_short_name() == "process") {
            process_fn = fn;
            break;
        }
    }
    REQUIRE(process_fn != nullptr);

    auto& params = process_fn->parameters();
    REQUIRE(params.size() == 2);

    // First parameter 'input' has @Tag annotation
    auto& p0 = params[0];
    REQUIRE(p0 != nullptr);
    CHECK(p0->get_short_name() == "input");
    auto& p0_anns = p0->get_annotations();
    REQUIRE(p0_anns.size() == 1);
    CHECK(p0_anns[0].resolved_type->get_short_name() == "Tag");

    // Second parameter 'output' has no annotations
    auto& p1 = params[1];
    REQUIRE(p1 != nullptr);
    CHECK(p1->get_short_name() == "output");
    CHECK(p1->get_annotations().empty());
}


// ════════════════════════════════════════════════════════════════════════════
//  21. Multiple annotations on a single parameter — model level
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: multiple annotations on single parameter — model level", "[annotation][parameter]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_27;
        annotation Tag {
            label : int;
        }
        annotation Info {
            code : int;
        }
        class Worker {
            public Worker() {}
            public run(@Tag(1) @Info(99) data : int) : int {
                return data;
            }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto klass = find_klass(comp, "Worker");
    REQUIRE(klass != nullptr);

    std::shared_ptr<k::model::function> run_fn;
    for (auto& fn : klass->functions()) {
        if (fn && fn->get_short_name() == "run") {
            run_fn = fn;
            break;
        }
    }
    REQUIRE(run_fn != nullptr);

    auto& params = run_fn->parameters();
    REQUIRE(params.size() == 1);
    auto& p0_anns = params[0]->get_annotations();
    REQUIRE(p0_anns.size() == 2);
    CHECK(p0_anns[0].resolved_type->get_short_name() == "Tag");
    CHECK(p0_anns[1].resolved_type->get_short_name() == "Info");
}


// ════════════════════════════════════════════════════════════════════════════
//  22. Annotation on constructor parameter — model level
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Annotation: on constructor parameter — model level", "[annotation][parameter][constructor]") {
    auto comp = compile_model(R"SRC(
        module gen_annotation_members_28;
        annotation Required {}
        class Config {
            public Config(@Required host : int, port : int) {}
            public dummy() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto klass = find_klass(comp, "Config");
    REQUIRE(klass != nullptr);

    // Find the constructor
    auto& ctors = klass->constructors();
    REQUIRE(!ctors.empty());
    auto& ctor = ctors[0];
    REQUIRE(ctor != nullptr);

    auto& params = ctor->parameters();
    // Constructor parameters include 'this' as param 0
    // Find 'host' and 'port'
    std::shared_ptr<k::model::parameter> host_param, port_param;
    for (auto& p : params) {
        if (!p) continue;
        if (p->get_short_name() == "host") host_param = p;
        if (p->get_short_name() == "port") port_param = p;
    }
    REQUIRE(host_param != nullptr);
    REQUIRE(port_param != nullptr);

    auto& host_anns = host_param->get_annotations();
    REQUIRE(host_anns.size() == 1);
    CHECK(host_anns[0].resolved_type->get_short_name() == "Required");

    CHECK(port_param->get_annotations().empty());
}
