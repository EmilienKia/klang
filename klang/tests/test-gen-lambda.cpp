/*
 * K Language compiler
 *
 * Copyright 2023-2026 Emilien Kia
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
 * Lambda lowering smoke tests.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"
#include "../src/errors.hpp"
#include "../src/parse/parser.hpp"
#include "../src/model/model_builder.hpp"

TEST_CASE("Lambda: capture-free lambda binds to a callable", "[gen][lambda]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_01;
        apply(f : *(int):int, x : int) : int { return f(x); }
        test() : int {
            return apply([](x : int) { return x + 1; }, 41);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Lambda: capture by value in borrowed callable", "[gen][lambda][capture]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_02;
        apply(f : &(int):int, x : int) : int { return f(x); }
        test() : int {
            base : int = 10;
            return apply([base](x : int) : int { return base + x; }, 32);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Lambda: capture by value in owned callable", "[gen][lambda][owner]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_03;
        test() : int {
            base : int = 40;
            fp : !(int):int = [base](x : int) : int { return base + x; };
            return fp(2);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Lambda: return owned capturing lambda from function", "[gen][lambda][owner][return]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_04;
        makeAdder(base : int) : !(int):int {
            return [base](x : int) : int { return base + x; };
        }
        test() : int {
            adder : !(int):int = makeAdder(40);
            return adder(2);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Lambda: owned lambda rejecting by-reference capture of local variable", "[gen][lambda][owner][error]")
{
    REQUIRE(compile_should_fail(R"SRC(
        module gen_lambda_05;
        test() : int {
            base : int = 40;
            fp : !(int):int = [&base](x : int) : int { return base + x; };
            return fp(2);
        }
    )SRC", nullptr));
}

TEST_CASE("Lambda: move owned lambda nulls source", "[gen][lambda][owner][move]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_06;
        test() : int {
            base : int = 30;
            f1 : !(int):int = [base](x : int) : int { return base + x; };
            f2 : !(int):int = f1; // move
            return f2(12);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Lambda: capture multiple values in owned callable", "[gen][lambda][owner][capture]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_07;
        test() : int {
            a : int = 10;
            b : int = 20;
            c : int = 12;
            fp : !():int = [a, b, c]() : int { return a + b + c; };
            return fp();
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Lambda: explicit generic execute selects owning free and UCS overloads",
          "[gen][lambda][owner][template][ucs][fresh-lambda][regression]") {
    const bool priority_first = GENERATE(false, true);
    INFO("priority overload declared first: " << priority_first);
    std::string source = R"SRC(
        module generic_executor_literals;
        drops: int = 0;
        struct Probe {
            value: int;
            Probe(v: int): value(v) {}
            ~Probe() { ++drops; }
        }
        struct InlineExecutor {
            const execute(task: !()) { task(); }
        }
    )SRC";
    const std::string priority_definition = R"SRC(
        template<typename R>
        execute(executor: const InlineExecutor&, task: !():R, priority: int): R {
            return task();
        }
    )SRC";
    const std::string ordinary_definition = R"SRC(
        template<typename R>
        execute(executor: const InlineExecutor&, task: !():R): R {
            return execute<R>(executor, task, 0);
        }
    )SRC";
    source += priority_first ? priority_definition + ordinary_definition
                             : ordinary_definition + priority_definition;
    source += R"SRC(
        run(): int {
            drops = 0;
            executor: InlineExecutor;
            if (execute<int>(executor, []():int { return 42; }) != 42) return -1;
            if (executor.execute<int>([]():int { return 42; }) != 42) return -2;
            probe: Probe! = new Probe(42);
            result: int = executor.execute<int>([probe]():int { return probe->value; }, 7);
            if (probe != null || drops != 1) return -3;
            task: !():int = []():int { return 42; };
            result += executor.execute<int>(task);
            return task == null ? result : -4;
        }
    )SRC";
    auto jit = gen_jit(source);
    REQUIRE(jit);
    auto run = jit->lookup_symbol<int (*)()>("run");
    REQUIRE(run);
    int result = 0;
    REQUIRE_NOTHROW(result = run());
    CHECK(result == 84);

    try {
        gen_jit_throws(R"SRC(
            module generic_executor_borrowed;
            struct InlineExecutor {}
            template<typename R>
            execute(executor: const InlineExecutor&, task: !():R): R { return task(); }
            run(): int {
                executor: InlineExecutor;
                borrowed: &():int = []():int { return 42; };
                return executor.execute<int>(borrowed);
            }
        )SRC");
        FAIL("Expected borrowed callable ownership rejection");
    } catch (const k::log::compiler_error& error) {
        CHECK(error.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::symbol_diag::ERR_OVERLOAD_NO_MATCH));
    }
}

TEST_CASE("Lambda: capture owner object in owned lambda runs destructor on cleanup", "[gen][lambda][owner][lifecycle]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_08;
        g_dtor_count : int = 0;
        struct Resource {
            val : int;
            Resource(v : int) { val = v; }
            ~Resource() { g_dtor_count = g_dtor_count + 1; }
        }
        test() : int {
            g_dtor_count = 0;
            {
                r : Resource! = new Resource(42);
                fp : !():int = [r]() : int { return r.val; };
                // fp goes out of scope here and drops r
            }
            return g_dtor_count;
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 1);
}

TEST_CASE("Lambda: re-assignment of owned callable cleans up old closure", "[gen][lambda][owner][assign]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_09;
        g_dtor_count : int = 0;
        struct Resource {
            val : int;
            Resource(v : int) { val = v; }
            ~Resource() { g_dtor_count = g_dtor_count + 1; }
        }
        test() : int {
            g_dtor_count = 0;
            r1 : Resource! = new Resource(10);
            fp : !():int = [r1]() : int { return r1.val; };
            
            r2 : Resource! = new Resource(20);
            fp = [r2]() : int { return r2.val; }; // drops r1 closure
            
            return g_dtor_count;
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 1);
}

TEST_CASE("Lambda: contextual return type deduction in variable initialization", "[gen][lambda][deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_10;
        test() : int {
            fp : *(int):int = [](x: int) { return x + 10; };
            return fp(32);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Lambda: contextual return type deduction in capturing lambda", "[gen][lambda][deduction][capture]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_11;
        test() : int {
            base : int = 40;
            fp : !(int):int = [base](x: int) { return base + x; };
            return fp(2);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Lambda: contextual return type deduction in assignment", "[gen][lambda][deduction][assign]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_12;
        test() : int {
            fp : *(int):int = [](x: int) { return x + 1; };
            fp = [](x: int) { return x * 2; };
            return fp(21);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Lambda: contextual return type deduction in return statement", "[gen][lambda][deduction][return]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_13;
        makeMultiplier(factor : int) : !(int):int {
            return [factor](x: int) { return factor * x; };
        }
        test() : int {
            f : !(int):int = makeMultiplier(6);
            return f(7);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Lambda: contextual return type deduction in ternary branches", "[gen][lambda][deduction][ternary]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_14;
        test() : int {
            cond : bool = true;
            fp : *(int):int = cond ? [](x: int) { return x + 2; } : [](x: int) { return x - 2; };
            return fp(40);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Lambda: contextual return type deduction in array literal", "[gen][lambda][deduction][array]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_15;
        alias Fn : *(int):int;
        test() : int {
            arr : Fn[2] { [](x: int) { return x + 10; }, [](x: int) { return x + 20; } };
            return arr[0](12) + arr[1](10);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 52);
}

TEST_CASE("Lambda: immediately invoked lambda with deduced return type", "[gen][lambda][iife][deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_16;
        test() : int {
            return ((x: int) { return x * 3; })(14);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Functions: return type deduction from function body", "[gen][function][deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_fn_deduce_01;
        add(a: int, b: int) {
            return a + b;
        }
        test() : int {
            return add(20, 22);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Functions: return type deduction with if/else branches", "[gen][function][deduction][branch]") {
    auto jit = gen_jit(R"SRC(
        module gen_fn_deduce_02;
        absVal(n: int) {
            if (n < 0) {
                return -n;
            }
            return n;
        }
        test() : int {
            return absVal(-42);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Functions: recursive function with return type deduction", "[gen][function][deduction][recursive]") {
    auto jit = gen_jit(R"SRC(
        module gen_fn_deduce_03;
        fib(n: int) {
            if (n <= 1) {
                return n;
            }
            return fib(n - 1) + fib(n - 2);
        }
        test() : int {
            return fib(7);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 13);
}

TEST_CASE("Functions: void function with deduced return type", "[gen][function][deduction][void]") {
    auto jit = gen_jit(R"SRC(
        module gen_fn_deduce_04;
        increment(n: int&) {
            n = n + 1;
        }
        test() : int {
            val : int = 41;
            increment(val);
            return val;
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Functions: reject mixing void return and non-void return in deduced function", "[gen][function][deduction][error]") {
    REQUIRE(compile_should_fail(R"SRC(
        module gen_fn_deduce_05;
        bad(cond: bool) {
            if (cond) {
                return;
            }
            return 42;
        }
        test() : int {
            return bad(true);
        }
    )SRC", nullptr));
}

TEST_CASE("Functions: reject inconsistent return types in deduced function", "[gen][function][deduction][error]") {
    REQUIRE(compile_should_fail(R"SRC(
        module gen_fn_deduce_06;
        bad(cond: bool) {
            if (cond) {
                return 42;
            }
            return "error";
        }
        test() : int {
            return bad(true);
        }
    )SRC", nullptr));
}

TEST_CASE("Functions: warning emitted when classic function omits return type and returns non-void", "[gen][function][deduction][warning]") {
    test_logger logger;
    auto src = R"SRC(
        module gen_fn_deduce_07;
        compute(x: int) {
            return x * 2;
        }
    )SRC";
    k::parse::parser parser(logger);
    k::source source(src);
    parser.parse(source);
    auto ast_unit = parser.parse_unit();
    REQUIRE(ast_unit);
    auto ctx = k::model::context::create();
    auto unit = k::model::unit::create(ctx);
    k::model::model_builder::visit(logger, ctx, *ast_unit, *unit);
    k::model::gen::symbol_resolver var_resolver(logger, ctx, *unit);
    var_resolver.resolve();
    ctx->resolve_types();
    k::model::gen::aggregate_type_resolver agg_type_resolver(logger, ctx, *unit);
    agg_type_resolver.resolve();
    k::model::gen::model_materializer materializer(logger, ctx, *unit);
    materializer.materialize();
    k::model::gen::type_reference_resolver type_ref_resolver(logger, ctx, *unit);
    type_ref_resolver.resolve();
    bool has_omitted_ret_warning = std::any_of(logger.diagnostics.begin(), logger.diagnostics.end(), [](const auto& d) {
        return d.code == static_cast<unsigned int>(k::diag::function_diag::WARN_FUNC_RETURN_TYPE_OMITTED);
    });
    CHECK(has_omitted_ret_warning);
}

TEST_CASE("Functions: no warning when classic function returns void with omitted return type", "[gen][function][deduction][warning]") {
    test_logger logger;
    auto src = R"SRC(
        module gen_fn_deduce_08;
        proc(x: int&) {
            x = x + 1;
        }
    )SRC";
    k::parse::parser parser(logger);
    k::source source(src);
    parser.parse(source);
    auto ast_unit = parser.parse_unit();
    REQUIRE(ast_unit);
    auto ctx = k::model::context::create();
    auto unit = k::model::unit::create(ctx);
    k::model::model_builder::visit(logger, ctx, *ast_unit, *unit);
    k::model::gen::symbol_resolver var_resolver(logger, ctx, *unit);
    var_resolver.resolve();
    ctx->resolve_types();
    k::model::gen::aggregate_type_resolver agg_type_resolver(logger, ctx, *unit);
    agg_type_resolver.resolve();
    k::model::gen::model_materializer materializer(logger, ctx, *unit);
    materializer.materialize();
    k::model::gen::type_reference_resolver type_ref_resolver(logger, ctx, *unit);
    type_ref_resolver.resolve();
    bool has_omitted_ret_warning = std::any_of(logger.diagnostics.begin(), logger.diagnostics.end(), [](const auto& d) {
        return d.code == static_cast<unsigned int>(k::diag::function_diag::WARN_FUNC_RETURN_TYPE_OMITTED);
    });
    CHECK_FALSE(has_omitted_ret_warning);
}

TEST_CASE("Lambda: no warning when lambda omits return type", "[gen][lambda][deduction][warning]") {
    test_logger logger;
    auto src = R"SRC(
        module gen_fn_deduce_09;
        apply(f: *(int):int, x: int) : int {
            return f(x);
        }
        test() : int {
            fp : *(int):int = [](x: int) { return x * 2; };
            return apply([](n: int) { return n + 1; }, 41);
        }
    )SRC";
    k::parse::parser parser(logger);
    k::source source(src);
    parser.parse(source);
    auto ast_unit = parser.parse_unit();
    REQUIRE(ast_unit);
    auto ctx = k::model::context::create();
    auto unit = k::model::unit::create(ctx);
    k::model::model_builder::visit(logger, ctx, *ast_unit, *unit);
    k::model::gen::symbol_resolver var_resolver(logger, ctx, *unit);
    var_resolver.resolve();
    ctx->resolve_types();
    k::model::gen::aggregate_type_resolver agg_type_resolver(logger, ctx, *unit);
    agg_type_resolver.resolve();
    k::model::gen::model_materializer materializer(logger, ctx, *unit);
    materializer.materialize();
    k::model::gen::type_reference_resolver type_ref_resolver(logger, ctx, *unit);
    type_ref_resolver.resolve();
    bool has_omitted_ret_warning = std::any_of(logger.diagnostics.begin(), logger.diagnostics.end(), [](const auto& d) {
        return d.code == static_cast<unsigned int>(k::diag::function_diag::WARN_FUNC_RETURN_TYPE_OMITTED);
    });
    CHECK_FALSE(has_omitted_ret_warning);
}

