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
 * Tests for Milestone 10: Template constraint validation.
 *
 * These tests verify that:
 *  [A] validate_template_arg_constraints: typename accepts any type (primitive).
 *  [B] validate_template_arg_constraints: struct kind rejects non-aggregate type.
 *  [C] validate_template_arg_constraints: struct kind accepts a struct type.
 *  [D] validate_template_arg_constraints: class kind rejects a struct type.
 *  [E] validate_template_arg_constraints: class kind accepts a class type.
 *  [F] validate_template_arg_constraints: interface kind accepts an interface type.
 *  [G] validate_template_arg_constraints: base-type constraint accepts derived type.
 *  [H] validate_template_arg_constraints: base-type constraint rejects unrelated type.
 *  [I] validate_template_arg_constraints: value param is skipped.
 *  [J] Integration: struct-constrained template param rejects int at resolver level.
 *  [K] Integration: typename-constrained template accepts struct at resolver level.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"
#include "../src/model/template.hpp"
#include "../src/model/template_instantiator.hpp"
#include "../src/errors.hpp"

using namespace k::model;

namespace {

class constraint_test_compiler final : public k::compiler {
public:
    std::vector<k::log::diagnostic> errors;

    constraint_test_compiler() : k::compiler(make_pic_target_machine()) {}

    void report(const k::log::diagnostic& diagnostic) override {
        if (diagnostic.level >= k::log::diagnostic::severity::error)
            errors.push_back(diagnostic);
        k::compiler::report(diagnostic);
    }
};

void check_ctad_guide_scope(const std::string& source_text, bool has_default,
                            bool expect_success) {
    test_logger logger;
    k::source source(source_text);
    k::parse::parser parser(logger);
    parser.parse(source);
    auto ast = parser.parse_unit();
    REQUIRE(ast);
    auto context = k::model::context::create();
    auto unit = k::model::unit::create(context);
    k::model::model_builder::visit(logger, context, *ast, *unit);
    k::model::gen::symbol_resolver symbols(logger, context, *unit);
    symbols.resolve();
    context->resolve_types();
    k::model::gen::aggregate_type_resolver aggregates(logger, context, *unit);
    aggregates.resolve();
    context->resolve_types();
    k::model::gen::model_materializer materializer(logger, context, *unit);
    materializer.materialize();
    REQUIRE_FALSE(logger.has_error());

    auto root = unit->get_root_namespace();
    REQUIRE(root);
    auto definition = root->get_child_namespace("definition");
    auto caller = root->get_child_namespace("caller");
    REQUIRE(definition);
    REQUIRE(caller);
    auto holder = definition->get_aggregate("Holder");
    auto expected = definition->get_aggregate(expect_success ? "Good" : "Wrong");
    auto wrong_bound = caller->get_aggregate("Bound");
    REQUIRE(holder);
    REQUIRE(expected);
    REQUIRE(wrong_bound);
    REQUIRE(expected->get_struct_type());
    REQUIRE(wrong_bound->get_struct_type());
    auto info = holder->get_tpl_info();
    REQUIRE(info);
    REQUIRE(info->params.size() == 1);
    REQUIRE(info->params[0].constraint_source_type);
    info->params[0].constraint_type = wrong_bound->get_struct_type();
    if (has_default) {
        info->params[0].default_type = expected->get_struct_type();
        auto foreign = caller->get_aggregate(expect_success ? "Good" : "Wrong");
        REQUIRE(foreign);
        REQUIRE(foreign->get_struct_type());
        using k::model::gen::scope_lookup;
        CHECK(scope_lookup::has_proven_local_ctad_default(
            *holder, *info, {template_argument::make_type(expected->get_struct_type())}));
        CHECK_FALSE(scope_lookup::has_proven_local_ctad_default(
            *holder, *info, {template_argument::make_type(foreign->get_struct_type())}));
        auto symbolic = unresolved_type::make_source_binder(
            k::name::from("T"), {info->binder_scope, 0});
        CHECK_FALSE(scope_lookup::has_proven_local_ctad_default(
            *holder, *info, {template_argument::make_type(symbolic)}));
    }
    size_t error_index;
    std::string error_kind;
    CHECK(validate_template_arg_constraints(
        info->params, {template_argument::make_type(expected->get_struct_type())},
        error_index, error_kind) == !expect_success);

    k::model::gen::type_reference_resolver resolver(logger, context, *unit);
    std::optional<k::log::diagnostic> error;
    try {
        resolver.resolve();
    } catch (const k::log::compiler_error& exception) {
        error = exception.get_diagnostic();
    }
    if (!expect_success) {
        REQUIRE(error);
        const auto message = k::log::format_diagnostic_message(*error);
        CHECK(message.find(has_default ? "No viable constructor"
                                       : "No matching constructor or guide") != std::string::npos);
        return;
    }
    REQUIRE_FALSE(error);
    CHECK_FALSE(logger.has_error());
    auto function = caller->get_function("test");
    REQUIRE(function);
    REQUIRE(function->get_existing_block());
    auto variable = function->get_existing_block()->get_variable("holder");
    REQUIRE(variable);
    auto concrete = std::dynamic_pointer_cast<struct_type>(variable->get_type());
    REQUIRE(concrete);
    auto instance = concrete->get_struct();
    REQUIRE(instance);
    CHECK(instance->get_tpl_definition() == holder);
    REQUIRE(instance->get_tpl_args().size() == 1);
    CHECK(type::are_equal(instance->get_tpl_args()[0].type_arg, expected->get_struct_type()));
}

} // namespace

TEST_CASE("CTAD uses declaration-scope evidence before legacy constraint filtering",
          "[template][concrete-ctad-constraint-scope]") {
    const bool missing = GENERATE(false, true);
    const std::string constructors = missing
        ? "Holder(value : T&) {}"
        : "Holder(value : T&, other : Wrong&) {} Holder(value : Good&, other : T&) {}";
    const std::string arguments = missing ? "value" : "value, other";
    const std::string source_text = R"SRC(
        module ctad_constraint_scope;
        namespace caller { class Bound {} }
        namespace definition {
            class Bound {}
            class Good : Bound {}
            class Wrong : caller::Bound {}
    )SRC" + std::string("template<class T : ") + (missing ? "MissingBase" : "Bound") + R"SRC(>
            struct Holder {
                public:
    )SRC" + constructors + R"SRC(
            }
        }
        namespace caller {
            test() : int {
                value : definition::Good;
                other : definition::Wrong;
    )SRC" + "holder : definition::Holder(" + arguments + R"SRC();
                return 0;
            }
        }
    )SRC";
    test_logger logger;
    k::source source(source_text);
    k::parse::parser parser(logger);
    parser.parse(source);
    auto ast = parser.parse_unit();
    REQUIRE(ast);
    auto context = k::model::context::create();
    auto unit = k::model::unit::create(context);
    k::model::model_builder::visit(logger, context, *ast, *unit);
    k::model::gen::symbol_resolver symbols(logger, context, *unit);
    symbols.resolve();
    context->resolve_types();
    k::model::gen::aggregate_type_resolver aggregates(logger, context, *unit);
    aggregates.resolve();
    context->resolve_types();
    k::model::gen::model_materializer materializer(logger, context, *unit);
    materializer.materialize();
    REQUIRE_FALSE(logger.has_error());

    auto root = unit->get_root_namespace();
    REQUIRE(root);
    auto definition = root->get_child_namespace("definition");
    auto caller = root->get_child_namespace("caller");
    REQUIRE(definition);
    REQUIRE(caller);
    auto holder = definition->get_aggregate("Holder");
    auto good = definition->get_aggregate("Good");
    auto wrong = definition->get_aggregate("Wrong");
    auto wrong_bound = caller->get_aggregate("Bound");
    REQUIRE(holder);
    REQUIRE(good);
    REQUIRE(wrong);
    REQUIRE(wrong_bound);
    REQUIRE(good->get_struct_type());
    REQUIRE(wrong->get_struct_type());
    REQUIRE(wrong_bound->get_struct_type());
    auto info = holder->get_tpl_info();
    REQUIRE(info);
    REQUIRE(info->params.size() == 1);
    REQUIRE(info->params[0].constraint_source_type);

    // Simulate an earlier name-only resolution caching a same-spelled, wrong-origin type.
    info->params[0].constraint_type = wrong_bound->get_struct_type();
    size_t error_index;
    std::string error_kind;
    CHECK_FALSE(validate_template_arg_constraints(
        info->params, {template_argument::make_type(good->get_struct_type())},
        error_index, error_kind));
    CHECK(validate_template_arg_constraints(
        info->params, {template_argument::make_type(wrong->get_struct_type())},
        error_index, error_kind));

    k::model::gen::type_reference_resolver resolver(logger, context, *unit);
    std::optional<k::log::diagnostic> error;
    try {
        resolver.resolve();
    } catch (const k::log::compiler_error& exception) {
        error = exception.get_diagnostic();
    }
    if (missing) {
        REQUIRE(error);
        CHECK(k::log::format_diagnostic_message(*error).find("MissingBase") != std::string::npos);
    } else {
        REQUIRE_FALSE(error);
        CHECK_FALSE(logger.has_error());
        auto function = caller->get_function("test");
        REQUIRE(function);
        REQUIRE(function->get_existing_block());
        auto variable = function->get_existing_block()->get_variable("holder");
        REQUIRE(variable);
        auto concrete = std::dynamic_pointer_cast<struct_type>(variable->get_type());
        REQUIRE(concrete);
        REQUIRE(concrete->get_struct());
        REQUIRE(concrete->get_struct()->get_tpl_args().size() == 1);
        CHECK(type::are_equal(concrete->get_struct()->get_tpl_args()[0].type_arg,
                              good->get_struct_type()));
    }
}

TEST_CASE("CTAD copy guide uses declaration-scope bound evidence",
          "[template][concrete-ctad-constraint-scope]") {
    check_ctad_guide_scope(R"SRC(
        module ctad_copy_constraint_scope;
        namespace caller { class Bound {} }
        namespace definition {
            class Bound {}
            class Good : Bound {}
            template<class T : Bound>
            struct Holder { public: Holder(value : T&) {} }
        }
        namespace caller {
            test() : int {
                value : definition::Good;
                holder : definition::Holder = definition::Holder(value);
                return 0;
            }
        }
    )SRC", false, true);
}

TEST_CASE("CTAD copy proof requires the same template definition",
          "[template][concrete-ctad-constraint-scope]") {
    check_ctad_guide_scope(R"SRC(
        module ctad_foreign_copy_constraint_scope;
        namespace caller { class Bound {} }
        namespace definition {
            class Bound {}
            class Good : Bound {}
            class Wrong : caller::Bound {}
            template<class T : Bound> struct Holder {}
        }
        namespace other {
            template<typename T>
            struct Holder { public: Holder(value : T&) {} }
        }
        namespace caller {
            test() : int {
                value : definition::Good;
                holder : definition::Holder = other::Holder(value);
                return 0;
            }
        }
    )SRC", false, false);
}

TEST_CASE("CTAD default guide uses proven declaration-scope default",
          "[template][concrete-ctad-constraint-scope]") {
    const bool valid = GENERATE(false, true);
    check_ctad_guide_scope(R"SRC(
        module ctad_default_constraint_scope;
        namespace caller { class Bound {} class Good {} class Wrong {} }
        namespace definition {
            class Bound {}
            class Good : Bound {}
            class Wrong : caller::Bound {}
    )SRC" + std::string("template<class T : Bound = ") + (valid ? "Good" : "Wrong") + R"SRC(>
            struct Holder {}
        }
        namespace caller {
            test() : int {
                holder : definition::Holder();
                return 0;
            }
        }
    )SRC", true, valid);
}

TEST_CASE("CTAD constructor guide accepts a proven default-compatible argument",
          "[template][concrete-ctad-constraint-scope]") {
    check_ctad_guide_scope(R"SRC(
        module ctad_constructor_default_constraint_scope;
        namespace caller { class Bound {} class Good {} }
        namespace definition {
            class Bound {}
            class Good : Bound {}
            template<class T : Bound = Good>
            struct Holder { public: Holder(value : T&) {} }
        }
        namespace caller {
            test() : int {
                value : definition::Good;
                holder : definition::Holder(value);
                return 0;
            }
        }
    )SRC", true, true);
}

TEST_CASE("Classic template forwarding remains usable for valid concrete bounds",
          "[template][concrete-ctad-constraint-scope]") {
    auto compiler = std::make_shared<constraint_test_compiler>();
    try {
        compiler->parse_source("classic-constrained-forwarding.k", R"SRC(
            module classic_constrained_forwarding;
            class Bound {}
            class First : Bound {}
            class Second : Bound {}
            template<class T : Bound>
            sink(value : T&) : int { return 1; }
            template<class U>
            forward(value : U&) : int { return sink(value); }
            test() : int {
                first : First;
                second : Second;
                return forward(first) + forward(second);
            }
        )SRC", false, false);
    } catch (const k::log::compiler_error&) {
    }
    CHECK_FALSE(compiler->has_compilation_error());
}

TEST_CASE("Classic callable soft-alias bounds use their declaration scope",
          "[template][concrete-alias-bound-constraints]") {
    const bool valid = GENERATE(false, true);
    const bool explicit_args = GENERATE(false, true);
    const bool declaration_first = GENERATE(false, true);
    const std::string argument = valid ? "definition::Good" : "caller::Wrong";
    const std::string template_decl = R"SRC(
        template<class T : LocalConstraintAlias>
        check(value : T&) : int { return 1; }
    )SRC";
    const std::string definitions = R"SRC(
        class Base {}
        class Good : Base {}
        alias LocalConstraintAlias : Base;
    )SRC";
    const std::string invocation = explicit_args
        ? "definition::check<" + argument + ">(value)"
        : "definition::check(value)";
    const std::string source = R"SRC(
        module concrete_alias_bound;
        namespace definition {
    )SRC" + (declaration_first ? template_decl + definitions : definitions + template_decl) + R"SRC(
        }
        namespace caller {
            class Base {}
            class Wrong : Base {}
            alias LocalConstraintAlias : Base;
            test() : int {
    )SRC" + "value : " + argument + ";\nreturn " + invocation + R"SRC(;
            }
        }
    )SRC";
    INFO("valid: " << valid << ", explicit: " << explicit_args
                   << ", declaration first: " << declaration_first);
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    auto compiler = std::make_shared<constraint_test_compiler>();
    compiler->set_file_resolver(resolver);
    try {
        compiler->parse_source("concrete-alias-bound.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }
    auto imp = compiler->get_unit()->find_import(k::name("k"));
    REQUIRE(imp);
    REQUIRE(imp->implicit);
    REQUIRE(imp->kdi);
    CHECK(compiler->has_compilation_error() == !valid);
    if (!valid) {
        CHECK(std::any_of(compiler->errors.begin(), compiler->errors.end(), [](const auto& error) {
            return error.code == static_cast<unsigned int>(
                       k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED)
                || error.code == static_cast<unsigned int>(k::diag::symbol_diag::ERR_OVERLOAD_NO_MATCH)
                || error.code == static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_NO_MATCHING_OVERLOAD);
        }));
    }
}

TEST_CASE("Classic alias constraint proof handles implicit imported Object conservatively",
          "[template][concrete-alias-bound-constraints]") {
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    auto compiler = std::make_shared<constraint_test_compiler>();
    compiler->set_file_resolver(resolver);
    REQUIRE_NOTHROW(compiler->parse_source("alias-bound-evidence.k", R"SRC(
        module alias_bound_evidence;
        namespace definition {
            class Base {}
            class Good : Base {}
            alias LocalConstraintAlias : Base;
            template<class T : LocalConstraintAlias>
            check(value : T&) : int { return 1; }
        }
        namespace caller {
            class Base {}
            class Wrong : Base {}
        }
    )SRC", false, false));
    REQUIRE_FALSE(compiler->has_compilation_error());
    auto unit = compiler->get_unit();
    REQUIRE(unit);
    auto imp = unit->find_import(k::name("k"));
    REQUIRE(imp);
    REQUIRE(imp->implicit);
    REQUIRE(imp->kdi);
    auto root = unit->get_root_namespace();
    auto definition = root->get_child_namespace("definition");
    auto caller = root->get_child_namespace("caller");
    REQUIRE(definition);
    REQUIRE(caller);
    auto bound = definition->get_aggregate("Base");
    auto good = definition->get_aggregate("Good");
    auto other_base = caller->get_aggregate("Base");
    auto wrong = caller->get_aggregate("Wrong");
    auto declaration = definition->get_function("check");
    REQUIRE(bound);
    REQUIRE(good);
    REQUIRE(other_base);
    REQUIRE(wrong);
    REQUIRE(declaration);
    REQUIRE(bound->get_struct_type());
    REQUIRE(good->get_struct_type());
    REQUIRE(wrong->get_struct_type());
    auto info = declaration->get_tpl_info();
    REQUIRE(info);
    REQUIRE(info->params.size() == 1);
    auto argument = template_argument::make_type(wrong->get_struct_type());
    using k::model::gen::scope_lookup;
    auto lookup = scope_lookup::inspect_local_constraint_bound(
        *unit, *declaration, *info, 0, {argument});
    REQUIRE(lookup.state == scope_lookup::local_bound_state::aggregate);
    CHECK(lookup.bound == bound);

    tpl_info one;
    one.binder_scope = info->binder_scope;
    one.params.push_back(info->params[0]);
    one.params[0].constraint_type = bound->get_struct_type();
    one.params[0].constraint_source_type = generic_source_type::capture(bound->get_struct_type(), {});
    auto evidence = analyze_template_arg_constraints(
        one, {argument}, {generic_source_type::capture(argument.type_arg, {})});
    REQUIRE(evidence.state == constraint_analysis_state::deferred);
    REQUIRE(evidence.checks.size() == 1);
    CHECK(evidence.checks[0].reason == constraint_analysis_reason::incomplete_hierarchy);
    CHECK(scope_lookup::is_proven_unrelated_with_implicit_object(*unit, argument, bound));
    CHECK_FALSE(scope_lookup::is_proven_unrelated_with_implicit_object(
        *unit, template_argument::make_type(good->get_struct_type()), bound));
    auto symbolic = unresolved_type::make_source_binder(
        k::name::from("T"), {info->binder_scope, 0});
    CHECK_FALSE(scope_lookup::is_proven_unrelated_with_implicit_object(
        *unit, template_argument::make_type(symbolic), bound));

    REQUIRE(other_base->get_bases().size() == 1);
    SECTION("An unresolved base remains deferred") {
        auto saved = other_base->get_bases()[0].base;
        other_base->get_bases_mutable()[0].base.reset();
        CHECK_FALSE(scope_lookup::is_proven_unrelated_with_implicit_object(*unit, argument, bound));
        other_base->get_bases_mutable()[0].base = saved;
    }
    SECTION("Unexpected unresolved base remains deferred") {
        other_base->add_base("Missing");
        CHECK_FALSE(scope_lookup::is_proven_unrelated_with_implicit_object(*unit, argument, bound));
    }
    SECTION("A cyclic hierarchy remains deferred") {
        auto saved = other_base->get_bases()[0].base;
        other_base->get_bases_mutable()[0].base = other_base;
        CHECK_FALSE(scope_lookup::is_proven_unrelated_with_implicit_object(*unit, argument, bound));
        other_base->get_bases_mutable()[0].base = saved;
    }
    SECTION("A model path reaching the bound is not rejected") {
        auto saved = other_base->get_bases()[0].base;
        other_base->get_bases_mutable()[0].base = bound;
        CHECK_FALSE(scope_lookup::is_proven_unrelated_with_implicit_object(*unit, argument, bound));
        other_base->get_bases_mutable()[0].base = saved;
    }
}

TEST_CASE("Rejected classic alias-bound candidate leaves a viable overload",
          "[template][concrete-alias-bound-constraints]") {
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    auto compiler = std::make_shared<constraint_test_compiler>();
    compiler->set_file_resolver(resolver);
    try {
        compiler->parse_source("alias-bound-overload.k", R"SRC(
            module alias_bound_overload;
            class Base {}
            class Wrong {}
            alias LocalConstraintAlias : Base;
            template<class T : LocalConstraintAlias>
            check(value : T&) : int { return value.no_such_member; }
            check(value : Wrong&) : int { return 7; }
            test() : int {
                value : Wrong;
                return check(value);
            }
        )SRC", false, false);
    } catch (const k::log::compiler_error&) {
    }
    auto imp = compiler->get_unit()->find_import(k::name("k"));
    REQUIRE(imp);
    REQUIRE(imp->implicit);
    REQUIRE(imp->kdi);
    CHECK_FALSE(compiler->has_compilation_error());
}

TEST_CASE("Classic callable bounds can name a fully qualified imported class",
          "[template][concrete-imported-bound-constraints]") {
    const bool valid = GENERATE(false, true);
    const bool explicit_args = GENERATE(false, true);
    const std::string argument = valid ? "Good" : "Wrong";
    const std::string invocation = explicit_args
        ? "definition::check<" + argument + ">(value)"
        : "definition::check(value)";
    const std::string source = std::string(R"SRC(
        module imported_bound_constraint;
        namespace definition {
            class Exception {}
            template<class T : ::k::Exception>
            check(value : T&) : int { return 1; }
        }
        namespace caller {
            class Exception {}
            class Good : public k::Exception {}
            class Wrong {}
    )SRC") + "test(value : " + argument + "&) : int { return " + invocation + R"SRC(; }
        }
    )SRC";
    INFO("valid: " << valid << ", explicit: " << explicit_args);
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    auto compiler = std::make_shared<constraint_test_compiler>();
    compiler->set_file_resolver(resolver);
    try {
        compiler->parse_source("imported-bound-constraint.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }
    auto unit = compiler->get_unit();
    REQUIRE(unit);
    auto imp = unit->find_import(k::name("k"));
    REQUIRE(imp);
    REQUIRE(imp->implicit);
    REQUIRE(imp->kdi);
    CHECK(compiler->has_compilation_error() == !valid);
    if (!valid) {
        CHECK(std::any_of(compiler->errors.begin(), compiler->errors.end(), [](const auto& error) {
            return error.code == static_cast<unsigned int>(
                       k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED)
                || error.code == static_cast<unsigned int>(k::diag::symbol_diag::ERR_OVERLOAD_NO_MATCH)
                || error.code == static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_NO_MATCHING_OVERLOAD);
        }));
    } else if (!compiler->has_compilation_error()) {
        auto caller = unit->get_root_namespace()->get_child_namespace("caller");
        REQUIRE(caller);
        auto good = caller->get_aggregate("Good");
        REQUIRE(good);
        REQUIRE(good->get_bases().size() == 1);
        REQUIRE(good->get_bases()[0].base);
        CHECK(good->get_bases()[0].base->get_fq_name() == "::k::Exception");
    }
}

TEST_CASE("An imported-bound candidate leaves a viable concrete overload",
          "[template][concrete-imported-bound-constraints]") {
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    auto compiler = std::make_shared<constraint_test_compiler>();
    compiler->set_file_resolver(resolver);
    try {
        compiler->parse_source("imported-bound-overload.k", R"SRC(
            module imported_bound_overload;
            namespace caller { class Wrong {} }
            namespace definition {
                template<class T : ::k::Exception>
                check(value : T&) : int { return value.no_such_member; }
                check(value : caller::Wrong&) : int { return 7; }
            }
            namespace caller {
                test(value : Wrong&) : int { return definition::check(value); }
            }
        )SRC", false, false);
    } catch (const k::log::compiler_error&) {
    }
    auto unit = compiler->get_unit();
    REQUIRE(unit);
    auto imp = unit->find_import(k::name("k"));
    REQUIRE(imp);
    REQUIRE(imp->implicit);
    REQUIRE(imp->kdi);
    CHECK_FALSE(compiler->has_compilation_error());
}

TEST_CASE("Unresolved or imported bounds never accept an unrelated argument",
          "[template][fail-closed-bound-constraints]") {
    const bool explicit_args = GENERATE(false, true);
    const bool unresolved = GENERATE(false, true);
    const bool aggregate = GENERATE(false, true);
    const bool rooted = GENERATE(false, true);
    if (rooted && unresolved) return;
    const std::string bound = unresolved ? "NoSuchBase" : (rooted ? "::k::Exception" : "k::Exception");
    const std::string declaration = aggregate
        ? "template<class T : " + bound + "> struct Holder { public: Holder() {} }"
        : "template<class T : " + bound + "> check(value : T&) : int { return 1; }";
    const std::string invocation = aggregate
        ? "holder : definition::Holder<Wrong>;"
        : (explicit_args ? "definition::check<Wrong>(value);" : "definition::check(value);");
    const std::string source = "module fail_closed_bound;\nclass Wrong {}\nnamespace definition {\n"
        + declaration + "\n}\ntest(value : Wrong&) { " + invocation + " }\n";
    INFO("explicit: " << explicit_args << ", unresolved: " << unresolved
         << ", aggregate: " << aggregate);
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    auto compiler = std::make_shared<constraint_test_compiler>();
    compiler->set_file_resolver(resolver);
    std::optional<k::log::diagnostic> thrown;
    try {
        compiler->parse_source("fail-closed-bound.k", source, false, false);
    } catch (const k::log::compiler_error& error) {
        thrown = error.get_diagnostic();
    }
    const auto expected = unresolved
        ? static_cast<unsigned int>(k::diag::type_diag::ERR_UNRESOLVED_TYPE_EXPR)
        : static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED);
    const bool reported = (thrown && thrown->code == expected)
        || std::any_of(compiler->errors.begin(), compiler->errors.end(),
                       [&](const auto& error) { return error.code == expected; });
    CHECK(reported);
}

TEST_CASE("A rooted imported base class resolves and satisfies a rooted bound",
          "[template][fail-closed-bound-constraints][rooted-base]") {
    const bool derived = GENERATE(false, true);
    const std::string source = std::string(R"SRC(
        module rooted_imported_base;
        class Good : public ::k::Exception {}
        class Wrong {}
        template<class T : ::k::Exception>
        check(value : T&) : int { return 1; }
    )SRC") + "test(value : " + (derived ? "Good" : "Wrong") + "&) : int { return check(value); }\n";
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    auto compiler = std::make_shared<constraint_test_compiler>();
    compiler->set_file_resolver(resolver);
    std::optional<k::log::diagnostic> thrown;
    try {
        compiler->parse_source("rooted-imported-base.k", source, false, false);
    } catch (const k::log::compiler_error& error) {
        thrown = error.get_diagnostic();
    }
    const auto violated = static_cast<unsigned int>(
        k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED);
    const bool reported = (thrown && thrown->code == violated)
        || std::any_of(compiler->errors.begin(), compiler->errors.end(),
                       [&](const auto& error) { return error.code == violated; });
    CHECK(reported == !derived);
    if (derived) {
        CHECK_FALSE(thrown.has_value());
        CHECK_FALSE(compiler->has_compilation_error());
    }
}

TEST_CASE("An imported hierarchy proven complete by the KDI filters function-template overloads",
          "[template][fail-closed-bound-constraints][imported-bases]") {
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    auto compiler = std::make_shared<constraint_test_compiler>();
    compiler->set_file_resolver(resolver);
    REQUIRE_NOTHROW(compiler->parse_source("imported-bases-overload.k", R"SRC(
        module imported_bases_overload;
        class Foo {}
        class Mine : public ::k::IllegalArgumentException {
            public: Mine() : IllegalArgumentException(1) {}
        }
        template<class T : Foo>
        pick(value : T&) : int { return 1; }
        template<class T : ::k::Exception>
        pick(value : T&) : int { return 42; }
        test(value : Mine&) : int { return pick(value); }
    )SRC", false, false));
    CHECK_FALSE(compiler->has_compilation_error());
}

TEST_CASE("A bound deferred in a template body is checked when the body is instantiated",
          "[template][fail-closed-bound-constraints]") {
    const bool valid = GENERATE(false, true);
    const bool aggregate = GENERATE(false, true);
    const std::string argument = valid ? "Good" : "Wrong";
    const std::string declarations = aggregate
        ? "template<class T : Base> struct Box { public: v : T*; }\n"
          "template<class U> struct Wrap { public: b : Box<U>*; }\n"
        : "template<class T : Base> check(value : T&) : int { return 1; }\n"
          "template<class U> forward(value : U&) : int { return check(value); }\n";
    const std::string use = aggregate
        ? "test() { x : Wrap<" + argument + ">*; }\n"
        : "test(value : " + argument + "&) : int { return forward(value); }\n";
    const std::string source = "module deferred_bound;\nclass Base {}\nclass Good : Base {}\n"
        "class Wrong {}\n" + declarations + use;
    INFO("valid: " << valid << ", aggregate: " << aggregate);
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    auto compiler = std::make_shared<constraint_test_compiler>();
    compiler->set_file_resolver(resolver);
    std::optional<k::log::diagnostic> thrown;
    try {
        compiler->parse_source("deferred-bound.k", source, false, false);
    } catch (const k::log::compiler_error& error) {
        thrown = error.get_diagnostic();
    }
    const auto expected = static_cast<unsigned int>(
        k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED);
    const bool reported = (thrown && thrown->code == expected)
        || std::any_of(compiler->errors.begin(), compiler->errors.end(),
                       [&](const auto& error) { return error.code == expected; });
    CHECK(reported == !valid);
    if (valid) CHECK_FALSE(thrown.has_value());
}

TEST_CASE("A same-list dependent bound is decided at instantiation",
          "[template][fail-closed-bound-constraints][concrete-dependent-constraints]") {
    const bool valid = GENERATE(false, true);
    const bool aggregate = GENERATE(false, true);
    const std::string argument = valid ? "Good" : "Wrong";
    const std::string declaration = aggregate
        ? "template<class B, class T : B> struct S { public: S() {} }\n"
        : "template<class B, class T : B> check(value : T&) : int { return 1; }\n";
    const std::string use = aggregate
        ? "main() : int { x : S<Base, " + argument + ">; return 0; }\n"
        : "main() : int { v : " + argument + "; return check<Base, " + argument + ">(v); }\n";
    const std::string source = "module dependent_instantiation_bound;\nclass Base {}\n"
        "class Good : Base {}\nclass Wrong {}\n" + declaration + use;
    INFO("valid: " << valid << ", aggregate: " << aggregate);
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    auto compiler = std::make_shared<constraint_test_compiler>();
    compiler->set_file_resolver(resolver);
    std::optional<k::log::diagnostic> thrown;
    try {
        compiler->parse_source("dependent-instantiation-bound.k", source, false, false);
    } catch (const k::log::compiler_error& error) {
        thrown = error.get_diagnostic();
    }
    const auto expected = static_cast<unsigned int>(
        k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED);
    const bool reported = (thrown && thrown->code == expected)
        || std::any_of(compiler->errors.begin(), compiler->errors.end(),
                       [&](const auto& error) { return error.code == expected; });
    CHECK(reported == !valid);
    if (valid) CHECK_FALSE(compiler->has_compilation_error());
}

TEST_CASE("Qualified classic bounds retain their declaration scope",
          "[template][concrete-qualified-bound-constraints]") {
    const bool valid = GENERATE(false, true);
    const bool rooted = GENERATE(false, true);
    const std::string bound = rooted
        ? "::qualified_constraint_scope::constraint_scope_probe::nested::Bound"
        : "constraint_scope_probe::nested::Bound";
    const std::string argument = valid
        ? "::qualified_constraint_scope::constraint_scope_probe::nested::Good"
        : "Wrong";
    std::string declaration;
    std::string invocation;
    SECTION("free explicit") {
        declaration = "template<class T : " + bound + "> check(value : T&) : int { return 1; }";
        invocation = "definition::check<" + argument + ">(value);";
    }
    SECTION("free deduction") {
        declaration = "template<class T : " + bound + "> check(value : T&) : int { return 1; }";
        invocation = "definition::check(value);";
    }
    SECTION("member explicit") {
        declaration = "struct Service { public: template<class T : " + bound
            + "> check(value : T&) : int { return 1; } }";
        invocation = "service : definition::Service; service.check<" + argument + ">(value);";
    }
    SECTION("member deduction") {
        declaration = "struct Service { public: template<class T : " + bound
            + "> check(value : T&) : int { return 1; } }";
        invocation = "service : definition::Service; service.check(value);";
    }
    SECTION("aggregate explicit") {
        declaration = "template<class T : " + bound
            + "> struct Holder { public: Holder(value : T&) {} }";
        invocation = "holder : definition::Holder<" + argument + ">(value);";
    }
    SECTION("aggregate CTAD") {
        declaration = "template<class T : " + bound
            + "> struct Holder { public: Holder(value : T&) {} }";
        invocation = "holder : definition::Holder = definition::Holder(value);";
    }
    const std::string source = R"SRC(
        module qualified_constraint_scope;
        namespace constraint_scope_probe::nested {
            class Bound {}
            class Good : Bound {}
        }
        namespace definition {
    )SRC" + declaration + R"SRC(
        }
        namespace caller {
            namespace constraint_scope_probe::nested { class Bound {} }
            class Bound {}
            class Wrong : Bound {}
            test() : int {
    )SRC" + "value : " + argument + ";\n" + invocation + R"SRC(
                return 0;
            }
        }
    )SRC";
    INFO("valid: " << valid << ", rooted bound: " << rooted);
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    auto compiler = std::make_shared<constraint_test_compiler>();
    compiler->set_file_resolver(resolver);
    try {
        compiler->parse_source("qualified-constraint.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }
    auto imp = compiler->get_unit()->find_import(k::name("k"));
    REQUIRE(imp);
    REQUIRE(imp->implicit);
    REQUIRE(imp->kdi);
    CHECK(compiler->has_compilation_error() == !valid);
    if (!valid) {
        CHECK(std::any_of(compiler->errors.begin(), compiler->errors.end(), [](const auto& error) {
            return error.code == static_cast<unsigned int>(
                       k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED)
                || error.code == static_cast<unsigned int>(k::diag::template_diag::ERR_CTAD_NO_MATCH)
                || error.code == static_cast<unsigned int>(k::diag::symbol_diag::ERR_OVERLOAD_NO_MATCH)
                || error.code == static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_NO_MATCHING_OVERLOAD)
                || error.code == static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_MEMBER_NO_MATCH)
                || error.code == static_cast<unsigned int>(k::diag::type_diag::ERR_MEMBER_FUNC_NO_MATCH);
        }));
    }
}

TEST_CASE("Qualified bounds select the nearest declaration namespace",
          "[template][concrete-qualified-bound-constraints]") {
    const bool valid = GENERATE(false, true);
    const std::string argument = valid ? "definition::constraint_scope_probe::Good" : "Wrong";
    const std::string source = std::string(R"SRC(
        module qualified_shadowed_bound;
        namespace constraint_scope_probe { class Bound {} }
        namespace definition {
            namespace constraint_scope_probe { class Bound {} class Good : Bound {} }
            template<class T : constraint_scope_probe::Bound>
            check(value : T&) : int { return 1; }
        }
        namespace caller {
            class Wrong : constraint_scope_probe::Bound {}
            test() : int {
    )SRC") + "value : " + argument + R"SRC(;
                return definition::check(value);
            }
        }
    )SRC";
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    auto compiler = std::make_shared<constraint_test_compiler>();
    compiler->set_file_resolver(resolver);
    try {
        compiler->parse_source("qualified-shadowed-bound.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }
    CHECK(compiler->has_compilation_error() == !valid);
    if (!valid) CHECK_FALSE(compiler->errors.empty());
}

TEST_CASE("Rejected qualified bound leaves a viable callable alternative",
          "[template][concrete-qualified-bound-constraints]") {
    const bool explicit_args = GENERATE(false, true);
    const std::string call = explicit_args
        ? "definition::check<Wrong>(value)"
        : "definition::check(value)";
    const std::string source = R"SRC(
        module qualified_bound_overload;
        namespace constraint_scope_probe { class Bound {} }
        namespace definition {
            template<class T : constraint_scope_probe::Bound>
            check(value : T&) : int { return value.no_such_member; }
            template<typename T>
            check(value : T&, extra : int = 0) : int { return extra; }
        }
        namespace caller {
            class Wrong {}
            test() : int {
                value : Wrong;
                return )SRC" + call + R"SRC(;
            }
        }
    )SRC";
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    auto compiler = std::make_shared<constraint_test_compiler>();
    compiler->set_file_resolver(resolver);
    try {
        compiler->parse_source("qualified-bound-overload.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }
    CHECK_FALSE(compiler->has_compilation_error());
}

TEST_CASE("Qualified bound evidence defers uncertain paths",
          "[template][concrete-qualified-bound-constraints]") {
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    auto compiler = std::make_shared<constraint_test_compiler>();
    compiler->set_file_resolver(resolver);
    REQUIRE_NOTHROW(compiler->parse_source("qualified-bound-evidence.k", R"SRC(
        module qualified_bound_evidence;
        namespace constraint_scope_probe {
            class Bound {}
            alias Alias : Bound;
        }
        namespace extras {
            namespace constraint_scope_probe { class Bound {} }
        }
        namespace definition {
            template<class T : ::constraint_scope_probe::Bound>
            rooted(value : T&) : int { return 0; }
            template<class T : constraint_scope_missing::Bound>
            missing(value : T&) : int { return 0; }
            template<class T : constraint_scope_probe::Alias>
            aliased(value : T&) : int { return 0; }
            template<class T : k::Object>
            imported(value : T&) : int { return 0; }
            template<class T, class U : T::Bound>
            dependent(value : U&) : int { return 0; }
        }
        namespace using_scope {
            using namespace extras;
            template<class T : constraint_scope_probe::Bound>
            ambiguous(value : T&) : int { return 0; }
        }
    )SRC", false, false));
    REQUIRE_FALSE(compiler->has_compilation_error());
    auto unit = compiler->get_unit();
    REQUIRE(unit);
    auto imp = unit->find_import(k::name("k"));
    REQUIRE(imp);
    REQUIRE(imp->implicit);
    REQUIRE(imp->kdi);
    auto root = unit->get_root_namespace();
    auto source_scope = root->get_child_namespace("constraint_scope_probe");
    auto definition = root->get_child_namespace("definition");
    auto using_scope = root->get_child_namespace("using_scope");
    REQUIRE(source_scope);
    REQUIRE(definition);
    REQUIRE(using_scope);
    auto bound = source_scope->get_aggregate("Bound");
    REQUIRE(bound);
    using k::model::gen::scope_lookup;
    auto inspect = [&](const std::shared_ptr<function>& fn) {
        REQUIRE(fn);
        auto info = fn->get_tpl_info();
        REQUIRE(info);
        return scope_lookup::inspect_local_constraint_bound(*unit, *fn, *info, 0, {});
    };
    auto rooted = inspect(definition->get_function("rooted"));
    CHECK(rooted.state == scope_lookup::local_bound_state::aggregate);
    CHECK(rooted.bound == bound);
    auto extras = root->get_child_namespace("extras");
    REQUIRE(extras);
    auto foreign_scope = extras->get_child_namespace("constraint_scope_probe");
    REQUIRE(foreign_scope);
    auto foreign = foreign_scope->get_aggregate("Bound");
    REQUIRE(foreign);
    REQUIRE(foreign->get_struct_type());
    auto rooted_fn = definition->get_function("rooted");
    REQUIRE(rooted_fn);
    rooted_fn->get_tpl_info()->params[0].constraint_type = foreign->get_struct_type();
    CHECK(inspect(rooted_fn).bound == bound);
    for (const auto& name : {"missing", "aliased", "imported"}) {
        INFO(name);
        CHECK(inspect(definition->get_function(name)).state == scope_lookup::local_bound_state::deferred);
    }
    auto dependent = definition->get_function("dependent");
    REQUIRE(dependent);
    REQUIRE(dependent->get_tpl_info());
    CHECK(scope_lookup::inspect_local_constraint_bound(
        *unit, *dependent, *dependent->get_tpl_info(), 1, {}).state
        == scope_lookup::local_bound_state::deferred);
    CHECK(inspect(using_scope->get_function("ambiguous")).state == scope_lookup::local_bound_state::deferred);
}

TEST_CASE("Concrete template constraints use their declaration scope",
          "[template][concrete-template-constraints]") {
    // Each route is compiled independently, with the same valid, invalid and
    // missing-bound inputs. No generated function is executed.
    const int input = GENERATE(0, 1, 2);
    const std::string bound = input == 2 ? "MissingBase" : "Bound";
    const std::string argument = input == 1 ? "Wrong" : "definition::Good";
    std::string declaration;
    std::string invocation;

    SECTION("free explicit") {
        declaration = "template<class T : " + bound + "> check(value : T&) : int { return 1; }";
        invocation = "definition::check<" + argument + ">(value);";
    }
    SECTION("free deduction") {
        declaration = "template<class T : " + bound + "> check(value : T&) : int { return 1; }";
        invocation = "definition::check(value);";
    }
    SECTION("member explicit") {
        declaration = "struct Service { public: template<class T : " + bound
            + "> check(value : T&) : int { return 1; } }";
        invocation = "service : definition::Service; service.check<" + argument + ">(value);";
    }
    SECTION("member deduction") {
        declaration = "struct Service { public: template<class T : " + bound
            + "> check(value : T&) : int { return 1; } }";
        invocation = "service : definition::Service; service.check(value);";
    }
    SECTION("aggregate explicit") {
        declaration = "template<class T : " + bound
            + "> struct Holder { public: Holder(value : T&) {} }";
        invocation = "holder : definition::Holder<" + argument + ">(value);";
    }
    SECTION("aggregate CTAD") {
        declaration = "template<class T : " + bound
            + "> struct Holder { public: Holder(value : T&) {} }";
        invocation = "holder : definition::Holder = definition::Holder(value);";
    }

    const std::string source = R"SRC(
        module concrete_template_constraints;
        namespace definition {
            class Bound {}
            class Good : Bound {}
    )SRC" + declaration + R"SRC(
        }
        namespace caller {
            class Bound {}
            class Wrong : Bound {}
            test() : int {
    )SRC" + "value : " + argument + ";\n" + invocation + R"SRC(
                return 0;
            }
        }
    )SRC";
    INFO("input: " << (input == 0 ? "valid" : input == 1 ? "unrelated" : "missing bound"));
    INFO("source:\n" << source);
    auto compiler = std::make_shared<constraint_test_compiler>();
    try {
        compiler->parse_source("concrete-template-constraints.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }

    if (input == 0) {
        CHECK_FALSE(compiler->has_compilation_error());
    } else {
        REQUIRE(compiler->has_compilation_error());
        REQUIRE_FALSE(compiler->errors.empty());
        if (input == 1) {
            CHECK(std::any_of(compiler->errors.begin(), compiler->errors.end(), [](const auto& error) {
                return error.code == static_cast<unsigned int>(
                           k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED)
                    || error.code == static_cast<unsigned int>(k::diag::template_diag::ERR_CTAD_NO_MATCH)
                    || error.code == static_cast<unsigned int>(k::diag::symbol_diag::ERR_OVERLOAD_NO_MATCH)
                    || error.code == static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_NO_MATCHING_OVERLOAD)
                    || error.code == static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_MEMBER_NO_MATCH)
                    || error.code == static_cast<unsigned int>(k::diag::type_diag::ERR_MEMBER_FUNC_NO_MATCH);
            }));
        } else {
            CHECK(std::any_of(compiler->errors.begin(), compiler->errors.end(), [](const auto& error) {
                return k::log::format_diagnostic_message(error).find("MissingBase") != std::string::npos;
            }));
            CHECK(std::none_of(compiler->errors.begin(), compiler->errors.end(), [](const auto& error) {
                return error.code == static_cast<unsigned int>(
                    k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED);
            }));
        }
    }
}

TEST_CASE("Concrete callable constraints retain declaration scope across declaration order",
          "[template][concrete-callable-constraint-scope]") {
    const bool bound_first = GENERATE(true, false);
    const bool valid = GENERATE(true, false);
    const std::string bound = "class Bound {}";
    const std::string declaration =
        "template<class T : Bound> check(value : T&) : int { return 1; }";
    const std::string argument = valid ? "definition::Good" : "Wrong";
    std::string invocation;
    SECTION("explicit free call") {
        invocation = "definition::check<" + argument + ">(value);";
    }
    SECTION("deduced free call") {
        invocation = "definition::check(value);";
    }
    const std::string source = R"SRC(
        module callable_constraint_order;
        namespace definition {
    )SRC" + (bound_first ? bound + declaration : declaration + bound) + R"SRC(
            class Good : Bound {}
        }
        namespace caller {
            class Bound {}
            class Wrong : Bound {}
            test() : int {
    )SRC" + "value : " + argument + ";\n" + invocation + R"SRC(
                return 0;
            }
        }
    )SRC";
    INFO("bound declared first: " << bound_first << ", valid: " << valid);
    INFO("source:\n" << source);
    auto compiler = std::make_shared<constraint_test_compiler>();
    try {
        compiler->parse_source("callable-constraint-order.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }
    CHECK(compiler->has_compilation_error() == !valid);
    if (!valid) CHECK_FALSE(compiler->errors.empty());
}

TEST_CASE("Concrete aggregate constraints retain declaration scope across declaration order",
          "[template][concrete-aggregate-constraint-scope]") {
    const bool bound_first = GENERATE(true, false);
    const bool valid = GENERATE(true, false);
    const std::string bound = "class Bound {}";
    const std::string declaration =
        "template<class T : Bound> struct Holder { public: Holder(value : T&) {} }";
    const std::string argument = valid ? "definition::Good" : "Wrong";
    std::string invocation;
    SECTION("explicit aggregate") {
        invocation = "holder : definition::Holder<" + argument + ">(value);";
    }
    SECTION("aggregate deduction") {
        invocation = "holder : definition::Holder = definition::Holder(value);";
    }
    const std::string source = R"SRC(
        module aggregate_constraint_order;
        namespace definition {
    )SRC" + (bound_first ? bound + declaration : declaration + bound) + R"SRC(
            class Good : Bound {}
        }
        namespace caller {
            class Bound {}
            class Wrong : Bound {}
            test() : int {
    )SRC" + "value : " + argument + ";\n" + invocation + R"SRC(
                return 0;
            }
        }
    )SRC";
    INFO("bound declared first: " << bound_first << ", valid: " << valid);
    INFO("source:\n" << source);
    auto compiler = std::make_shared<constraint_test_compiler>();
    try {
        compiler->parse_source("aggregate-constraint-order.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }
    CHECK(compiler->has_compilation_error() == !valid);
    if (!valid) CHECK_FALSE(compiler->errors.empty());
}

TEST_CASE("Concrete constructor and operator candidates enforce lexical bounds",
          "[template][concrete-callable-constraint-scope]") {
    const bool valid = GENERATE(true, false);
    const std::string argument = valid ? "definition::Good" : "Wrong";
    std::string declaration;
    std::string invocation;
    SECTION("constructor deduction") {
        declaration = R"SRC(
            struct Service {
                public: template<class T : Bound> Service(value : T&) {}
            }
        )SRC";
        invocation = "service : definition::Service(value);";
    }
    SECTION("member operator deduction") {
        declaration = R"SRC(
            struct Service {
                public: template<class T : Bound> operator +(value : T&) : int { return 1; }
            }
        )SRC";
        invocation = "service : definition::Service; service + value;";
    }
    const std::string source = R"SRC(
        module callable_constraint_selection;
        namespace definition {
            class Bound {}
            class Good : Bound {}
    )SRC" + declaration + R"SRC(
        }
        namespace caller {
            class Bound {}
            class Wrong : Bound {}
            test() : int {
    )SRC" + "value : " + argument + ";\n" + invocation + R"SRC(
                return 0;
            }
        }
    )SRC";
    INFO("valid: " << valid);
    INFO("source:\n" << source);
    auto compiler = std::make_shared<constraint_test_compiler>();
    try {
        compiler->parse_source("callable-constraint-selection.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }
    CHECK(compiler->has_compilation_error() == !valid);
    if (!valid) CHECK_FALSE(compiler->errors.empty());
}

TEST_CASE("Rejected constrained function does not prevent an explicit overload",
          "[template][concrete-callable-constraint-scope]") {
    auto compiler = std::make_shared<constraint_test_compiler>();
    try {
        compiler->parse_source("callable-constraint-overload.k", R"SRC(
            module callable_constraint_overload;
            namespace definition {
                class Bound { public flag : int; }
                class Wrong {}
                template<class T : Bound>
                check(value : T&) : int { return value.flag; }
                template<typename T>
                check(value : T&, extra : int = 0) : int { return extra; }
            }
            namespace caller {
                class Bound {}
                test() : int {
                    value : definition::Wrong;
                    return definition::check<definition::Wrong>(value);
                }
            }
        )SRC", false, false);
    } catch (const k::log::compiler_error&) {
    }
    CHECK_FALSE(compiler->has_compilation_error());
}

TEST_CASE("A missing bound does not poison viable callable alternatives",
          "[template][concrete-callable-constraint-scope][concrete-missing-bound]") {
    std::string declaration;
    std::string invocation;
    SECTION("free explicit") {
        declaration = R"SRC(
            template<class T : MissingBase>
            check(value : T&) : int { return value.no_such_member; }
            template<typename T>
            check(value : T&, extra : int = 0) : int { return extra; }
        )SRC";
        invocation = "definition::check<definition::Good>(value);";
    }
    SECTION("free deduction") {
        declaration = R"SRC(
            template<class T : MissingBase>
            check(value : T&) : int { return value.no_such_member; }
            template<typename T>
            check(value : T&, extra : int = 0) : int { return extra; }
        )SRC";
        invocation = "definition::check(value);";
    }
    SECTION("member explicit") {
        declaration = R"SRC(
            struct Service {
                public:
                template<class T : MissingBase>
                check(value : T&) : int { return value.no_such_member; }
                template<typename T>
                check(value : T&, extra : int = 0) : int { return extra; }
            }
        )SRC";
        invocation = "service : definition::Service; service.check<definition::Good>(value);";
    }
    SECTION("member deduction") {
        declaration = R"SRC(
            struct Service {
                public:
                template<class T : MissingBase>
                check(value : T&) : int { return value.no_such_member; }
                template<typename T>
                check(value : T&, extra : int = 0) : int { return extra; }
            }
        )SRC";
        invocation = "service : definition::Service; service.check(value);";
    }
    const std::string source = R"SRC(
        module missing_bound_overload;
        namespace definition {
            class Good {}
    )SRC" + declaration + R"SRC(
        }
        namespace caller {
            class MissingBase {}
            test() : int {
                value : definition::Good;
    )SRC" + invocation + R"SRC(
                return 0;
            }
        }
    )SRC";
    INFO("source:\n" << source);
    auto compiler = std::make_shared<constraint_test_compiler>();
    try {
        compiler->parse_source("missing-bound-overload.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }
    CHECK_FALSE(compiler->has_compilation_error());
}

TEST_CASE("A caller homonym does not supply a missing callable bound",
          "[template][concrete-callable-constraint-scope][concrete-missing-bound]") {
    std::string declaration;
    std::string invocation;
    SECTION("free explicit") {
        declaration = "template<class T : MissingBase> check(value : T&) : int { return 1; }";
        invocation = "definition::check<definition::Good>(value);";
    }
    SECTION("free deduction") {
        declaration = "template<class T : MissingBase> check(value : T&) : int { return 1; }";
        invocation = "definition::check(value);";
    }
    SECTION("member explicit") {
        declaration = "struct Service { public: template<class T : MissingBase>"
            " check(value : T&) : int { return 1; } }";
        invocation = "service : definition::Service; service.check<definition::Good>(value);";
    }
    SECTION("member deduction") {
        declaration = "struct Service { public: template<class T : MissingBase>"
            " check(value : T&) : int { return 1; } }";
        invocation = "service : definition::Service; service.check(value);";
    }
    const std::string source = R"SRC(
        module missing_bound_homonym;
        namespace definition {
            class Good {}
    )SRC" + declaration + R"SRC(
        }
        namespace caller {
            class MissingBase {}
            test() : int {
                value : definition::Good;
    )SRC" + invocation + R"SRC(
                return 0;
            }
        }
    )SRC";
    INFO("source:\n" << source);
    auto compiler = std::make_shared<constraint_test_compiler>();
    try {
        compiler->parse_source("missing-bound-homonym.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }
    REQUIRE(compiler->has_compilation_error());
    CHECK(std::any_of(compiler->errors.begin(), compiler->errors.end(), [](const auto& error) {
        return k::log::format_diagnostic_message(error).find("MissingBase") != std::string::npos;
    }));
    CHECK(std::none_of(compiler->errors.begin(), compiler->errors.end(), [](const auto& error) {
        return error.code == static_cast<unsigned int>(
            k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED);
    }));
}

TEST_CASE("Missing bounds stay lazy and dependent bounds stay deferred",
          "[template][concrete-callable-constraint-scope][concrete-missing-bound]") {
    std::string source;
    SECTION("unused missing bound") {
        source = R"SRC(
            module unused_missing_bound;
            template<class T : MissingBase> check(value : T&) : int { return 1; }
            test() : int { return 0; }
        )SRC";
    }
    SECTION("bound names an earlier template parameter") {
        source = R"SRC(
            module dependent_template_bound;
            class Bound {}
            class Good : Bound {}
            template<class Base, class T : Base>
            check(value : T&) : int { return 1; }
            test() : int {
                value : Good;
                return check<Bound, Good>(value);
            }
        )SRC";
    }
    auto compiler = std::make_shared<constraint_test_compiler>();
    try {
        compiler->parse_source("deferred-template-bound.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }
    CHECK_FALSE(compiler->has_compilation_error());
}

TEST_CASE("Implicit standard library imports do not hide missing local bounds",
          "[template][concrete-constraint-implicit-k]") {
    std::string declaration;
    std::string invocation;
    const std::string bound = "MissingConstraintForImplicitK";
    SECTION("free explicit") {
        declaration = "template<class T : " + bound + "> check(value : T&) : int { return 1; }";
        invocation = "definition::check<definition::Good>(value);";
    }
    SECTION("free deduction") {
        declaration = "template<class T : " + bound + "> check(value : T&) : int { return 1; }";
        invocation = "definition::check(value);";
    }
    SECTION("member explicit") {
        declaration = "struct Service { public: template<class T : " + bound
            + "> check(value : T&) : int { return 1; } }";
        invocation = "service : definition::Service; service.check<definition::Good>(value);";
    }
    SECTION("member deduction") {
        declaration = "struct Service { public: template<class T : " + bound
            + "> check(value : T&) : int { return 1; } }";
        invocation = "service : definition::Service; service.check(value);";
    }
    SECTION("aggregate explicit") {
        declaration = "template<class T : " + bound
            + "> struct Holder { public: Holder(value : T&) {} }";
        invocation = "holder : definition::Holder<definition::Good>(value);";
    }
    SECTION("aggregate deduction") {
        declaration = "template<class T : " + bound
            + "> struct Holder { public: Holder(value : T&) {} }";
        invocation = "holder : definition::Holder = definition::Holder(value);";
    }
    SECTION("multi-parameter free explicit") {
        declaration = "template<typename U, class T : " + bound
            + "> check(value : T&, other : U&) : int { return 1; }";
        invocation = "definition::check<definition::Other, definition::Good>(value, other);";
    }
    SECTION("multi-parameter aggregate deduction") {
        declaration = "template<typename U, class T : " + bound
            + "> struct Holder { public: Holder(value : T&, other : U&) {} }";
        invocation = "holder : definition::Holder = definition::Holder(value, other);";
    }
    const std::string source = R"SRC(
        module implicit_k_bound;
        namespace definition {
            class Good {}
            class Other {}
    )SRC" + declaration + R"SRC(
        }
        test() : int {
            value : definition::Good;
            other : definition::Other;
    )SRC" + invocation + R"SRC(
            return 0;
        }
    )SRC";
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    auto compiler = std::make_shared<constraint_test_compiler>();
    compiler->set_file_resolver(resolver);
    try {
        compiler->parse_source("implicit-k-bound.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }
    auto imp = compiler->get_unit()->find_import(k::name("k"));
    REQUIRE(imp);
    REQUIRE(imp->implicit);
    REQUIRE(imp->kdi);
    REQUIRE(compiler->has_compilation_error());
    CHECK(std::any_of(compiler->errors.begin(), compiler->errors.end(), [&](const auto& error) {
        return k::log::format_diagnostic_message(error).find(bound) != std::string::npos;
    }));
    CHECK(std::none_of(compiler->errors.begin(), compiler->errors.end(), [](const auto& error) {
        return error.code == static_cast<unsigned int>(
            k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED);
    }));
}

TEST_CASE("Independent multi-parameter bounds use declaration scope",
          "[template][concrete-multi-parameter-constraints]") {
    const bool bound_first = GENERATE(true, false);
    const int input = GENERATE(0, 1, 2);
    const std::string bound = input == 2 ? "MissingMultiParameterBase" : "Bound";
    const std::string argument = input == 1 ? "Wrong" : "definition::Good";
    const std::string parameters = bound_first
        ? "class T : " + bound + ", class U : OtherBound"
        : "class U : OtherBound, class T : " + bound;
    const std::string arguments = bound_first
        ? argument + ", definition::Other" : "definition::Other, " + argument;
    std::string declaration;
    std::string invocation;
    SECTION("free explicit") {
        declaration = "template<" + parameters
            + "> check(value : T&, other : U&) : int { return 1; }";
        invocation = "definition::check<" + arguments + ">(value, other);";
    }
    SECTION("free deduction") {
        declaration = "template<" + parameters
            + "> check(value : T&, other : U&) : int { return 1; }";
        invocation = "definition::check(value, other);";
    }
    SECTION("aggregate explicit") {
        declaration = "template<" + parameters
            + "> struct Holder { public: Holder(value : T&, other : U&) {} }";
        invocation = "holder : definition::Holder<" + arguments + ">(value, other);";
    }
    SECTION("aggregate deduction") {
        declaration = "template<" + parameters
            + "> struct Holder { public: Holder(value : T&, other : U&) {} }";
        invocation = "holder : definition::Holder = definition::Holder(value, other);";
    }
    const std::string source = R"SRC(
        module multi_parameter_bound;
        namespace definition {
            class Bound {}
            class Good : Bound {}
            class OtherBound {}
            class Other : OtherBound {}
    )SRC" + declaration + R"SRC(
        }
        namespace caller {
            class Bound {}
            class Wrong : Bound {}
            test() : int {
                other : definition::Other;
    )SRC" + "value : " + argument + ";\n" + invocation + R"SRC(
                return 0;
            }
        }
    )SRC";
    INFO("bound first: " << bound_first << ", input: " << input);
    INFO("source:\n" << source);
    auto compiler = std::make_shared<constraint_test_compiler>();
    try {
        compiler->parse_source("multi-parameter-bound.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }
    CHECK(compiler->has_compilation_error() == (input != 0));
    if (input == 2) {
        CHECK(std::any_of(compiler->errors.begin(), compiler->errors.end(), [&](const auto& error) {
            return k::log::format_diagnostic_message(error).find(bound) != std::string::npos;
        }));
        CHECK(std::none_of(compiler->errors.begin(), compiler->errors.end(), [](const auto& error) {
            return error.code == static_cast<unsigned int>(
                k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED);
        }));
    }
}

TEST_CASE("Missing independent bounds leave viable overloads and generic forwarding",
          "[template][concrete-multi-parameter-constraints][concrete-constraint-implicit-k]") {
    std::string source;
    SECTION("explicit overload") {
        source = R"SRC(
            module missing_multi_overload;
            namespace definition {
                class Good {}
                class Other {}
                template<class T : MissingMultiParameterBase, typename U>
                check(value : T&, other : U&) : int { return value.no_such_member; }
                template<typename T, typename U>
                check(value : T&, other : U&, extra : int = 0) : int { return extra; }
            }
            test() : int {
                value : definition::Good;
                other : definition::Other;
                return definition::check<definition::Good, definition::Other>(value, other);
            }
        )SRC";
    }
    SECTION("deduced overload") {
        source = R"SRC(
            module missing_multi_deduced_overload;
            namespace definition {
                class Good {}
                class Other {}
                template<class T : MissingMultiParameterBase, typename U>
                check(value : T&, other : U&) : int { return value.no_such_member; }
                template<typename T, typename U>
                check(value : T&, other : U&, extra : int = 0) : int { return extra; }
            }
            test() : int {
                value : definition::Good;
                other : definition::Other;
                return definition::check(value, other);
            }
        )SRC";
    }
    SECTION("dependent forwarding") {
        source = R"SRC(
            module deferred_multi_forwarding;
            class Bound {}
            class Good : Bound {}
            generic<class T : Bound>
            check(value : T*) : int { return 1; }
            generic<class Base, class U : Base>
            forward(value : U*) : int { return check(value); }
            test() : int {
                value : Good;
                return forward<Bound, Good>(&value);
            }
        )SRC";
    }
    SECTION("imported bound name") {
        source = R"SRC(
            module imported_bound_control;
            class Good : public k::Object {}
            template<class T : Object>
            check(value : T&) : int { return 1; }
            test() : int {
                value : Good;
                return check<Good>(value);
            }
        )SRC";
    }
    SECTION("local bound in an exposed imported namespace") {
        source = R"SRC(
            module extended_imported_namespace_bound;
            namespace k {
                class LocalConstraintBase {}
                class LocalConstraintGood : LocalConstraintBase {}
            }
            template<class T : LocalConstraintBase>
            check(value : T&) : int { return 1; }
            test() : int {
                value : k::LocalConstraintGood;
                return check<k::LocalConstraintGood>(value);
            }
        )SRC";
    }
    SECTION("explicit using with implicit k") {
        source = R"SRC(
            module implicit_k_explicit_using_bound;
            namespace definition {
                class UsingConstraintBase {}
                class Good : UsingConstraintBase {}
            }
            namespace caller {
                using namespace definition;
                template<class T : UsingConstraintBase>
                check(value : T&) : int { return 1; }
                test() : int {
                    value : definition::Good;
                    return check<definition::Good>(value);
                }
            }
        )SRC";
    }
    SECTION("unused missing bound") {
        source = R"SRC(
            module unused_imported_bound;
            template<class T : MissingConstraintForImplicitK>
            check(value : T&) : int { return 1; }
            test() : int { return 0; }
        )SRC";
    }
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    auto compiler = std::make_shared<constraint_test_compiler>();
    compiler->set_file_resolver(resolver);
    try {
        compiler->parse_source("multi-bound-controls.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }
    auto imp = compiler->get_unit()->find_import(k::name("k"));
    REQUIRE(imp);
    REQUIRE(imp->implicit);
    REQUIRE(imp->kdi);
    CHECK_FALSE(compiler->has_compilation_error());
}

TEST_CASE("A deferred dependent bound does not hide a different missing bound",
          "[template][concrete-multi-parameter-constraints]") {
    auto compiler = std::make_shared<constraint_test_compiler>();
    try {
        compiler->parse_source("mixed-dependent-bound.k", R"SRC(
            module mixed_dependent_bound;
            class Bound {}
            class Good : Bound {}
            class Other {}
            template<class Base, class T : Base, class U : MissingIndependentBase>
            check(value : T&, other : U&) : int { return 1; }
            test() : int {
                value : Good;
                other : Other;
                return check<Bound, Good, Other>(value, other);
            }
        )SRC", false, false);
    } catch (const k::log::compiler_error&) {
    }
    REQUIRE(compiler->has_compilation_error());
    CHECK(std::any_of(compiler->errors.begin(), compiler->errors.end(), [](const auto& error) {
        return k::log::format_diagnostic_message(error).find("MissingIndependentBase")
            != std::string::npos;
    }));
    CHECK(std::none_of(compiler->errors.begin(), compiler->errors.end(), [](const auto& error) {
        return error.code == static_cast<unsigned int>(
            k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED);
    }));
}

TEST_CASE("Concrete earlier-parameter bounds use the bound argument",
          "[template][concrete-dependent-constraints]") {
    const int input = GENERATE(0, 1, 2);
    const std::string argument = input == 0 ? "definition::Good"
        : input == 1 ? "caller::Wrong" : "definition::Bound";
    std::string declaration;
    std::string invocation;
    bool expect_constraint_diagnostic = false;
    SECTION("free explicit") {
        declaration = "template<class Base, class T : Base>"
            " check(base : Base&, value : T&) : int { return 1; }";
        invocation = "definition::check<definition::Bound, " + argument + ">(base, value);";
    }
    SECTION("free deduction") {
        declaration = "template<class Base, class T : Base>"
            " check(base : Base&, value : T&) : int { return 1; }";
        invocation = "definition::check(base, value);";
    }
    SECTION("member explicit") {
        declaration = "struct Service { public: template<class Base, class T : Base>"
            " check(base : Base&, value : T&) : int { return 1; } }";
        invocation = "service : definition::Service; service.check<definition::Bound, "
            + argument + ">(base, value);";
    }
    SECTION("member deduction") {
        declaration = "struct Service { public: template<class Base, class T : Base>"
            " check(base : Base&, value : T&) : int { return 1; } }";
        invocation = "service : definition::Service; service.check(base, value);";
    }
    SECTION("aggregate explicit") {
        declaration = "template<class Base, class T : Base>"
            " struct Holder { public: Holder(base : Base&, value : T&) {} }";
        invocation = "holder : definition::Holder<definition::Bound, " + argument
            + ">(base, value);";
        expect_constraint_diagnostic = true;
    }
    SECTION("aggregate CTAD") {
        declaration = "template<class Base, class T : Base>"
            " struct Holder { public: Holder(base : Base&, value : T&) {} }";
        invocation = "holder : definition::Holder = definition::Holder(base, value);";
    }
    const std::string source = R"SRC(
        module concrete_dependent_constraints;
        namespace definition {
            class Base {}
            class Bound {}
            class Good : Bound {}
    )SRC" + declaration + R"SRC(
        }
        namespace caller {
            class Base {}
            class Wrong : Base {}
            test() : int {
                base : definition::Bound;
    )SRC" + "value : " + argument + ";\n" + invocation + R"SRC(
                return 0;
            }
        }
    )SRC";
    INFO("input: " << input);
    INFO("source:\n" << source);
    auto compiler = std::make_shared<constraint_test_compiler>();
    try {
        compiler->parse_source("concrete-dependent-constraints.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }
    CHECK(compiler->has_compilation_error() == (input == 1));
    if (input == 1 && expect_constraint_diagnostic) {
        CHECK(std::any_of(compiler->errors.begin(), compiler->errors.end(), [](const auto& error) {
            return error.code == static_cast<unsigned int>(
                k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED);
        }));
    }
}

TEST_CASE("Earlier-parameter bounds retain their own binder identity",
          "[template][concrete-dependent-constraints]") {
    auto compiler = std::make_shared<constraint_test_compiler>();
    try {
        compiler->parse_source("dependent-binder-identity.k", R"SRC(
            module dependent_binder_identity;
            class Base {}
            class First {}
            class FirstChild : First {}
            class Second {}
            class SecondChild : Second {}
            template<class Base, class T : Base>
            check(base : Base&, value : T&) : int { return 1; }
            test() : int {
                first : First;
                firstChild : FirstChild;
                second : Second;
                secondChild : SecondChild;
                check<First, FirstChild>(first, firstChild);
                return check<Second, SecondChild>(second, secondChild);
            }
        )SRC", false, false);
    } catch (const k::log::compiler_error&) {
    }
    REQUIRE_FALSE(compiler->has_compilation_error());
    auto fn = compiler->get_unit()->get_root_namespace()->get_function("check");
    REQUIRE(fn);
    const auto* info = fn->get_tpl_info();
    REQUIRE(info);
    REQUIRE(info->params.size() == 2);
    auto source = info->params[1].constraint_source_type;
    REQUIRE(source);
    CHECK_FALSE(source->has_unknown_bindings());
    auto binder = std::dynamic_pointer_cast<unresolved_type>(source->get_type());
    REQUIRE(binder);
    REQUIRE(binder->source_binder());
    CHECK(binder->source_binder()->scope == info->binder_scope);
    CHECK(binder->source_binder()->ordinal == 0);
}

TEST_CASE("Dependent bound bindings are not cached across calls",
          "[template][concrete-dependent-constraints]") {
    auto compiler = std::make_shared<constraint_test_compiler>();
    try {
        compiler->parse_source("dependent-binder-rebinding.k", R"SRC(
            module dependent_binder_rebinding;
            class First {}
            class FirstChild : First {}
            class Second {}
            template<class Base, class T : Base>
            check(base : Base&, value : T&) : int { return 1; }
            test() : int {
                first : First;
                second : Second;
                child : FirstChild;
                check<First, FirstChild>(first, child);
                return check<Second, FirstChild>(second, child);
            }
        )SRC", false, false);
    } catch (const k::log::compiler_error&) {
    }
    CHECK(compiler->has_compilation_error());
}

TEST_CASE("Dependent-bound candidate rejection preserves viable overloads",
          "[template][concrete-dependent-constraints]") {
    const bool explicit_args = GENERATE(false, true);
    const std::string invocation = explicit_args
        ? "definition::check<definition::Bound, definition::Wrong>(base, value)"
        : "definition::check(base, value)";
    const std::string source = R"SRC(
        module dependent_constraint_overload;
        namespace definition {
            class Bound {}
            class Wrong {}
            template<class Base, class T : Base>
            check(base : Base&, value : T&) : int { return value.no_such_member; }
            template<class Base, class T>
            check(base : Base&, value : T&, extra : int = 0) : int { return extra; }
        }
        test() : int {
            base : definition::Bound;
            value : definition::Wrong;
            return )SRC" + invocation + R"SRC(;
        }
    )SRC";
    auto compiler = std::make_shared<constraint_test_compiler>();
    try {
        compiler->parse_source("dependent-bound-overload.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }
    CHECK_FALSE(compiler->has_compilation_error());
}

TEST_CASE("Alias and using bound lookup remains deferred",
          "[template][concrete-multi-parameter-constraints]") {
    std::string source;
    SECTION("alias") {
        source = R"SRC(
            module alias_bound_control;
            class Bound {}
            class Good : Bound {}
            alias BoundAlias : Bound;
            template<typename U, class T : BoundAlias>
            check(value : T&) : int { return 1; }
            test() : int {
                value : Good;
                return check<int, Good>(value);
            }
        )SRC";
    }
    SECTION("using directive") {
        source = R"SRC(
            module using_bound_control;
            namespace definitions {
                class Bound {}
                class Good : Bound {}
            }
            namespace caller {
                using namespace definitions;
                template<typename U, class T : Bound>
                check(value : T&) : int { return 1; }
                test() : int {
                    value : definitions::Good;
                    return check<int, definitions::Good>(value);
                }
            }
        )SRC";
    }
    auto compiler = std::make_shared<constraint_test_compiler>();
    try {
        compiler->parse_source("alias-using-bound-controls.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }
    CHECK_FALSE(compiler->has_compilation_error());
}

TEST_CASE("Qualified bounds and erased generic forwarding remain usable",
          "[template][concrete-callable-constraint-scope]") {
    std::string source;
    SECTION("qualified bound") {
        source = R"SRC(
            module callable_constraint_qualified;
            namespace definition {
                class Bound {}
                class Good : Bound {}
            }
            namespace caller {
                template<class T : definition::Bound>
                check(value : T&) : int { return 1; }
                test() : int {
                    value : definition::Good;
                    return check(value);
                }
            }
        )SRC";
    }
    SECTION("generic forwarding") {
        source = R"SRC(
            module callable_constraint_forwarding;
            class Bound {}
            class Good : Bound {}
            generic<class T : Bound>
            check(value : T*) : int { return 1; }
            generic<class U : Bound>
            forward(value : U*) : int { return check(value); }
            test() : int {
                value : Good;
                return forward<Good>(&value);
            }
        )SRC";
    }
    auto compiler = std::make_shared<constraint_test_compiler>();
    try {
        compiler->parse_source("callable-constraint-controls.k", source, false, false);
    } catch (const k::log::compiler_error&) {
    }
    INFO("source:\n" << source);
    CHECK_FALSE(compiler->has_compilation_error());
}

// ════════════════════════════════════════════════════════════════════════════
//  [A] typename accepts any type (primitive)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[A] M10: typename param accepts primitive type",
          "[milestone10][template][constraints]") {
    auto ctx = context::create();
    auto int_type = ctx->from_type(primitive_type::INT);

    std::vector<template_param_descriptor> params;
    template_param_descriptor p;
    p.kind = template_param_kind::TYPENAME;
    p.name = "T";
    params.push_back(p);

    std::vector<template_argument> args;
    args.push_back(template_argument::make_type(int_type));

    size_t err_idx;
    std::string err_kind;
    CHECK(validate_template_arg_constraints(params, args, err_idx, err_kind));
}

// ════════════════════════════════════════════════════════════════════════════
//  [B] struct kind rejects non-aggregate type (int)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[B] M10: struct kind rejects primitive type",
          "[milestone10][template][constraints]") {
    auto ctx = context::create();
    auto int_type = ctx->from_type(primitive_type::INT);

    std::vector<template_param_descriptor> params;
    template_param_descriptor p;
    p.kind = template_param_kind::STRUCT;
    p.name = "S";
    params.push_back(p);

    std::vector<template_argument> args;
    args.push_back(template_argument::make_type(int_type));

    size_t err_idx;
    std::string err_kind;
    CHECK_FALSE(validate_template_arg_constraints(params, args, err_idx, err_kind));
    CHECK(err_idx == 0);
    CHECK(err_kind == "not_aggregate");
}

// ════════════════════════════════════════════════════════════════════════════
//  [C] struct kind accepts a struct type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[C] M10: struct kind accepts struct type from model",
          "[milestone10][template][constraints]") {
    auto comp = compile_model(R"SRC(
        module gen_template_constraints_01;
        struct Foo {
            public x : int;
        }
        template<struct S>
        struct Holder {
            public val : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();

    // Get the struct Foo's struct_type
    auto foo_agg = root_ns->get_aggregate("Foo");
    REQUIRE(foo_agg != nullptr);
    auto foo_st = foo_agg->get_struct_type();
    REQUIRE(foo_st != nullptr);

    // Get the template Holder's tpl_info
    auto holder = root_ns->get_aggregate("Holder");
    REQUIRE(holder != nullptr);
    REQUIRE(holder->is_template());
    auto* ti = holder->get_tpl_info();
    REQUIRE(ti != nullptr);
    REQUIRE(ti->params.size() == 1);
    CHECK(ti->params[0].kind == template_param_kind::STRUCT);

    // Validate: Foo should be accepted
    std::vector<template_argument> args;
    args.push_back(template_argument::make_type(foo_st));

    size_t err_idx;
    std::string err_kind;
    CHECK(validate_template_arg_constraints(ti->params, args, err_idx, err_kind));
}

// ════════════════════════════════════════════════════════════════════════════
//  [D] class kind rejects a struct type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[D] M10: class kind rejects struct type",
          "[milestone10][template][constraints]") {
    auto comp = compile_model(R"SRC(
        module gen_template_constraints_02;
        struct Foo {
            public x : int;
        }
        template<class C>
        struct Holder {
            public val : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();

    auto foo_agg = root_ns->get_aggregate("Foo");
    REQUIRE(foo_agg != nullptr);
    auto foo_st = foo_agg->get_struct_type();
    REQUIRE(foo_st != nullptr);

    auto holder = root_ns->get_aggregate("Holder");
    REQUIRE(holder != nullptr);
    auto* ti = holder->get_tpl_info();
    REQUIRE(ti != nullptr);

    std::vector<template_argument> args;
    args.push_back(template_argument::make_type(foo_st));

    size_t err_idx;
    std::string err_kind;
    CHECK_FALSE(validate_template_arg_constraints(ti->params, args, err_idx, err_kind));
    CHECK(err_idx == 0);
    CHECK(err_kind == "kind");
}

// ════════════════════════════════════════════════════════════════════════════
//  [E] class kind accepts a class type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[E] M10: class kind accepts class type",
          "[milestone10][template][constraints]") {
    auto comp = compile_model(R"SRC(
        module gen_template_constraints_03;
        class Animal {
            public name : int;
        }
        template<class C>
        struct Holder {
            public val : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();

    auto animal_agg = root_ns->get_aggregate("Animal");
    REQUIRE(animal_agg != nullptr);
    auto animal_st = animal_agg->get_struct_type();
    REQUIRE(animal_st != nullptr);

    auto holder = root_ns->get_aggregate("Holder");
    REQUIRE(holder != nullptr);
    auto* ti = holder->get_tpl_info();
    REQUIRE(ti != nullptr);

    std::vector<template_argument> args;
    args.push_back(template_argument::make_type(animal_st));

    size_t err_idx;
    std::string err_kind;
    CHECK(validate_template_arg_constraints(ti->params, args, err_idx, err_kind));
}

// ════════════════════════════════════════════════════════════════════════════
//  [F] interface kind accepts an interface type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] M10: interface kind accepts interface type",
          "[milestone10][template][constraints]") {
    auto comp = compile_model(R"SRC(
        module gen_template_constraints_04;
        interface Printable {
            print() : int;
        }
        template<interface I>
        struct Holder {
            public val : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();

    auto iface = root_ns->get_aggregate("Printable");
    REQUIRE(iface != nullptr);
    auto iface_st = iface->get_struct_type();
    REQUIRE(iface_st != nullptr);

    auto holder = root_ns->get_aggregate("Holder");
    REQUIRE(holder != nullptr);
    auto* ti = holder->get_tpl_info();
    REQUIRE(ti != nullptr);

    std::vector<template_argument> args;
    args.push_back(template_argument::make_type(iface_st));

    size_t err_idx;
    std::string err_kind;
    CHECK(validate_template_arg_constraints(ti->params, args, err_idx, err_kind));
}

// ════════════════════════════════════════════════════════════════════════════
//  [G] base-type constraint accepts same or derived type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[G] M10: base-type constraint accepts derived type",
          "[milestone10][template][constraints]") {
    auto comp = compile_model(R"SRC(
        module gen_template_constraints_05;
        class Animal {
            public age : int;
        }
        class Dog : Animal {
            public name : int;
        }
        template<class T : Animal>
        struct Holder {
            public val : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();

    auto dog = root_ns->get_aggregate("Dog");
    REQUIRE(dog != nullptr);
    auto dog_st = dog->get_struct_type();
    REQUIRE(dog_st != nullptr);

    auto holder = root_ns->get_aggregate("Holder");
    REQUIRE(holder != nullptr);
    auto* ti = holder->get_tpl_info();
    REQUIRE(ti != nullptr);
    REQUIRE(ti->params.size() == 1);
    CHECK(ti->params[0].kind == template_param_kind::CLASS);

    // Dog derives from Animal — should pass
    std::vector<template_argument> args;
    args.push_back(template_argument::make_type(dog_st));

    size_t err_idx;
    std::string err_kind;
    CHECK(validate_template_arg_constraints(ti->params, args, err_idx, err_kind));

    // Also check: Animal itself should pass
    auto animal = root_ns->get_aggregate("Animal");
    REQUIRE(animal != nullptr);
    auto animal_st = animal->get_struct_type();
    REQUIRE(animal_st != nullptr);

    std::vector<template_argument> args2;
    args2.push_back(template_argument::make_type(animal_st));
    CHECK(validate_template_arg_constraints(ti->params, args2, err_idx, err_kind));
}

// ════════════════════════════════════════════════════════════════════════════
//  [H] base-type constraint rejects unrelated type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[H] M10: base-type constraint rejects unrelated type",
          "[milestone10][template][constraints]") {
    auto comp = compile_model(R"SRC(
        module gen_template_constraints_06;
        class Animal {
            public age : int;
        }
        class Car {
            public speed : int;
        }
        template<class T : Animal>
        struct Holder {
            public val : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();

    auto car = root_ns->get_aggregate("Car");
    REQUIRE(car != nullptr);
    auto car_st = car->get_struct_type();
    REQUIRE(car_st != nullptr);

    auto holder = root_ns->get_aggregate("Holder");
    REQUIRE(holder != nullptr);
    auto* ti = holder->get_tpl_info();
    REQUIRE(ti != nullptr);

    std::vector<template_argument> args;
    args.push_back(template_argument::make_type(car_st));

    size_t err_idx;
    std::string err_kind;
    CHECK_FALSE(validate_template_arg_constraints(ti->params, args, err_idx, err_kind));
    CHECK(err_idx == 0);
    CHECK(err_kind == "constraint");
}

// ════════════════════════════════════════════════════════════════════════════
//  [I] value param is skipped by constraint validation
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[I] M10: value parameter is skipped by constraint validation",
          "[milestone10][template][constraints]") {
    std::vector<template_param_descriptor> params;
    template_param_descriptor p;
    p.kind = template_param_kind::VALUE;
    p.name = "N";
    params.push_back(p);

    std::vector<template_argument> args;
    args.push_back(template_argument::make_value(42));

    size_t err_idx;
    std::string err_kind;
    CHECK(validate_template_arg_constraints(params, args, err_idx, err_kind));
}

// ════════════════════════════════════════════════════════════════════════════
//  [J] Integration: struct-constrained template rejects int at resolver level
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[J] M10: struct-constrained template rejects int in resolver",
          "[milestone10][template][constraints][integration]") {
    // Attempting to instantiate Holder<int> where Holder has `struct S` param
    // should throw a resolution_error with ERR_TPL_ARG_NOT_AGGREGATE.
    try {
        gen_jit_throws(R"SRC(
            module gen_template_constraints_07;
            template<struct S>
            struct Holder {
                public val : int;
            }
            struct User {
                public h : Holder<int>;
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_NOT_AGGREGATE));
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [K] Integration: typename accepts struct at resolver level
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[K] M10: typename-constrained template accepts int in resolver",
          "[milestone10][template][constraints][integration]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_constraints_08;
        template<typename T>
        struct Box {
            public value : T;
        }
        get_value() : int {
            b : Box<int>;
            b.value = 42;
            return b.value;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto get_value = jit->lookup_symbol<int(*)()>("_KFN27gen_template_constraints_089get_valueEv");
    REQUIRE(get_value != nullptr);
    CHECK(get_value() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [L] Integration: class-constrained template rejects struct
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[L] M10: class-constrained template rejects struct in resolver",
          "[milestone10][template][constraints][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module gen_template_constraints_09;
            struct Foo {
                public x : int;
            }
            template<class C>
            struct Wrapper {
                public val : int;
            }
            struct User {
                public w : Wrapper<Foo>;
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_WRONG_KIND));
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [M] Integration: interface-constrained template rejects class
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[M] M10: interface-constrained template rejects class in resolver",
          "[milestone10][template][constraints][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module gen_template_constraints_10;
            class Animal {
                public age : int;
            }
            template<interface I>
            struct Wrapper {
                public val : int;
            }
            struct User {
                public w : Wrapper<Animal>;
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_WRONG_KIND));
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [N] Integration: base-type constraint rejects unrelated class
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[N] M10: base-type constraint rejects unrelated class in resolver",
          "[milestone10][template][constraints][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module gen_template_constraints_11;
            class Animal {
                public age : int;
            }
            class Car {
                public speed : int;
            }
            template<class T : Animal>
            struct Cage {
                public val : int;
            }
            struct User {
                public c : Cage<Car>;
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED));
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [O] Integration: base-type constraint accepts derived class
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[O] M10: base-type constraint accepts derived class in resolver",
          "[milestone10][template][constraints][jit]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_constraints_12;
        class Animal {
            public age : int;
        }
        class Dog : Animal {
            public name : int;
        }
        template<class T : Animal>
        struct Cage {
            public val : int;
        }
        test() : int {
            c : Cage<Dog>;
            c.val = 99;
            return c.val;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN27gen_template_constraints_124testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 99);
}

// ════════════════════════════════════════════════════════════════════════════
//  [P] Integration: struct-constrained template accepts struct
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[P] M10: struct-constrained template accepts struct in resolver",
          "[milestone10][template][constraints][jit]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_constraints_13;
        struct Vec2 {
            public x : int;
            public y : int;
        }
        template<struct S>
        struct Holder {
            public val : S;
        }
        test() : int {
            h : Holder<Vec2>;
            h.val.x = 10;
            h.val.y = 32;
            return h.val.x + h.val.y;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN27gen_template_constraints_134testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [Q] Integration: class-constrained template accepts class
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[Q] M10: class-constrained template accepts class in resolver",
          "[milestone10][template][constraints][jit]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_constraints_14;
        class Widget {
            public id : int;
        }
        template<class C>
        struct Container {
            public data : int;
        }
        test() : int {
            c : Container<Widget>;
            c.data = 55;
            return c.data;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN27gen_template_constraints_144testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 55);
}

// ════════════════════════════════════════════════════════════════════════════
//  [R] Integration: interface-constrained template accepts interface
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[R] M10: interface-constrained template accepts interface in resolver",
          "[milestone10][template][constraints][jit]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_constraints_15;
        interface Countable {
            count() : int;
        }
        template<interface I>
        struct Adapter {
            public tag : int;
        }
        test() : int {
            a : Adapter<Countable>;
            a.tag = 77;
            return a.tag;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN27gen_template_constraints_154testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 77);
}

// ════════════════════════════════════════════════════════════════════════════
//  [S] Integration: function template with struct constraint — rejects int
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[S] M10: function template with struct constraint rejects int",
          "[milestone10][template][constraints][integration]") {
    // For function templates, constraint violations don't throw from the
    // resolver — they prevent instantiation, leading to further errors.
    REQUIRE_THROWS_AS(
        gen_jit_throws(R"SRC(
            module gen_template_constraints_16;
            template<struct S>
            process() : int { return 0; }

            test() : int {
                return process<int>();
            }
        )SRC"),
        k::log::compiler_error
    );
}

// ════════════════════════════════════════════════════════════════════════════
//  [T] Integration: function template with class constraint — accepts class
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[T] M10: function template with class constraint accepts class",
          "[milestone10][template][constraints][jit]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_constraints_17;
        class Widget {
            public id : int;
        }
        template<class C>
        get_zero() : int { return 0; }

        test() : int {
            return get_zero<Widget>();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN27gen_template_constraints_174testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 0);
}

// ════════════════════════════════════════════════════════════════════════════
//  [U] Integration: function template with base-type constraint — rejects
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[U] M10: function template with base-type constraint rejects unrelated",
          "[milestone10][template][constraints][integration]") {
    REQUIRE_THROWS_AS(
        gen_jit_throws(R"SRC(
            module gen_template_constraints_18;
            class Animal {
                public age : int;
            }
            class Car {
                public speed : int;
            }
            template<class T : Animal>
            feed() : int { return 0; }

            test() : int {
                return feed<Car>();
            }
        )SRC"),
        k::log::compiler_error
    );
}

// ════════════════════════════════════════════════════════════════════════════
//  [V] Integration: function template with base-type constraint — accepts derived
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[V] M10: function template with base-type constraint accepts derived",
          "[milestone10][template][constraints][jit]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_constraints_19;
        class Animal {
            public age : int;
        }
        class Dog : Animal {
            public name : int;
        }
        template<class T : Animal>
        feed() : int { return 42; }

        test() : int {
            return feed<Dog>();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN27gen_template_constraints_194testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [W] Error message content: "not_aggregate" includes param name and type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[W] M10: error message for not_aggregate includes param name",
          "[milestone10][template][constraints][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module gen_template_constraints_20;
            template<struct MyParam>
            struct Box { public val : int; }
            struct User { public b : Box<int>; }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_NOT_AGGREGATE));
        // Message should contain param name and template name
        CHECK(e.get_diagnostic().message.find("MyParam") != std::string::npos);
        CHECK(e.get_diagnostic().message.find("Box") != std::string::npos);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [X] Error message content: "kind" includes expected and actual kind
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[X] M10: error message for kind mismatch includes kinds",
          "[milestone10][template][constraints][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module gen_template_constraints_21;
            struct Foo { public x : int; }
            template<class C>
            struct Wrapper { public val : int; }
            struct User { public w : Wrapper<Foo>; }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_WRONG_KIND));
        CHECK(e.get_diagnostic().message.find("class") != std::string::npos);
        CHECK(e.get_diagnostic().message.find("struct") != std::string::npos);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [Y] Error message content: "constraint" includes constraint type name
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[Y] M10: error message for constraint violation includes constraint type",
          "[milestone10][template][constraints][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module gen_template_constraints_22;
            class Animal { public age : int; }
            class Car { public speed : int; }
            template<class T : Animal>
            struct Cage { public val : int; }
            struct User { public c : Cage<Car>; }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED));
        CHECK(e.get_diagnostic().message.find("Animal") != std::string::npos);
        CHECK(e.get_diagnostic().message.find("Car") != std::string::npos);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [Z] Generic validation: local variables must respect addresser rule
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[Z] M10: generic function rejects bare local variable type",
          "[milestone10][template][constraints][generic][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module gen_template_constraints_23;
            generic<typename T>
            fun bad(value: T&) : int {
                local : T;
                return 0;
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::generic_diag::ERR_GENERIC_DIRECT_TYPE_USAGE));
        CHECK(e.get_diagnostic().message.find("local variable 'local'") != std::string::npos);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [AA] Generic validation: owner local variable is not supported
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[AA] M10: generic function rejects owner local of a type parameter",
          "[milestone10][template][constraints][generic][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module gen_template_constraints_24;
            generic<typename T>
            fun bad(value: T&) : int {
                owned : T!;
                return 0;
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::generic_diag::ERR_GENERIC_OWNER_NOT_SUPPORTED));
        CHECK(e.get_diagnostic().message.find("local variable 'owned'") != std::string::npos);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [AB] Generic validation: member generic methods merge nested generic params
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[AB] M10: generic member function validates its own generic locals",
          "[milestone10][template][constraints][generic][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module gen_template_constraints_25;
            generic<typename T>
            struct Box {
                generic<typename U>
                fun bad(value: T&, other: U&) : int {
                    local : U;
                    return 0;
                }
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::generic_diag::ERR_GENERIC_DIRECT_TYPE_USAGE));
        CHECK(e.get_diagnostic().message.find("Generic type parameter 'U'") != std::string::npos);
        CHECK(e.get_diagnostic().message.find("local variable 'local'") != std::string::npos);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [AC] Generic validation: cast target type must respect addresser rule
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[AC] M10: generic function rejects cast target with bare type parameter",
          "[milestone10][template][constraints][generic][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module gen_template_constraints_26;
            generic<typename T>
            fun bad(v: int) : int {
                (T) v;
                return 0;
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::generic_diag::ERR_GENERIC_DIRECT_TYPE_USAGE));
        CHECK(e.get_diagnostic().message.find("cast target type") != std::string::npos);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [AD] Generic validation: owner cast target requires class/interface
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[AD] M10: generic function rejects owner cast target of a type parameter",
          "[milestone10][template][constraints][generic][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module gen_template_constraints_27;
            generic<typename T>
            fun bad(v: int) : int {
                (T!) v;
                return 0;
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::generic_diag::ERR_GENERIC_OWNER_NOT_SUPPORTED));
        CHECK(e.get_diagnostic().message.find("cast target type") != std::string::npos);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [AE] Generic validation: new allocated type must respect addresser rule
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[AE] M10: generic function rejects new allocation of bare type parameter",
          "[milestone10][template][constraints][generic][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module gen_template_constraints_28;
            generic<typename T>
            fun bad() : int {
                new T();
                return 0;
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::generic_diag::ERR_GENERIC_DIRECT_TYPE_USAGE));
        CHECK(e.get_diagnostic().message.find("new allocation type") != std::string::npos);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [AF] Generic validation: const wrapper must not hide direct type usage
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[AF] M10: generic function rejects const-wrapped bare local type",
          "[milestone10][template][constraints][generic][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module gen_template_constraints_29;
            generic<typename T>
            fun bad(value: T&) : int {
                local : const T;
                return 0;
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::generic_diag::ERR_GENERIC_DIRECT_TYPE_USAGE));
        CHECK(e.get_diagnostic().message.find("local variable 'local'") != std::string::npos);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [AG] Generic validation: const wrapper must not hide owner constraint
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[AG] M10: generic function rejects const owner local of a type parameter",
          "[milestone10][template][constraints][generic][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module gen_template_constraints_30;
            generic<typename T>
            fun bad(value: T&) : int {
                owned : const T!;
                return 0;
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::generic_diag::ERR_GENERIC_OWNER_NOT_SUPPORTED));
        CHECK(e.get_diagnostic().message.find("local variable 'owned'") != std::string::npos);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [AH] Generic validation: explicit template arg must respect addresser rule
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[AH] M10: generic function rejects explicit template arg with bare type parameter",
          "[milestone10][template][constraints][generic][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module gen_template_constraints_31;
            template<typename X>
            identity(x: X&) : int { return 0; }

            generic<typename T>
            fun bad(value: T&) : int {
                return identity<T>(value);
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::generic_diag::ERR_GENERIC_DIRECT_TYPE_USAGE));
        CHECK(e.get_diagnostic().message.find("explicit template argument #1") != std::string::npos);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [AI] Generic validation: explicit owner arg of a type parameter is rejected
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[AI] M10: generic function rejects explicit owner template arg of a type parameter",
          "[milestone10][template][constraints][generic][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module gen_template_constraints_32;
            template<typename X>
            consume() : int { return 0; }

            generic<typename T>
            fun bad(value: T&) : int {
                return consume<T!>();
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::generic_diag::ERR_GENERIC_OWNER_NOT_SUPPORTED));
        CHECK(e.get_diagnostic().message.find("explicit template argument #1") != std::string::npos);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [AJ] Generic validation: explicit owner arg is rejected even for class param
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[AJ] M10: generic function rejects explicit owner template arg for class",
          "[milestone10][template][constraints][generic][error]") {
    try {
        gen_jit_throws(R"SRC(
            module gen_template_constraints_33;
            class Boxed {
                public value : int;
                Boxed() : value(7) {}
            }

            template<typename X>
            consume() : int { return 1; }

            generic<class T>
            fun bad(value: T&) : int {
                return consume<T!>();
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::generic_diag::ERR_GENERIC_OWNER_NOT_SUPPORTED));
        CHECK(e.get_diagnostic().message.find("explicit template argument #1") != std::string::npos);
    }
}


