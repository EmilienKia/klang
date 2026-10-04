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

TEST_CASE("Lambda: void owner and member callbacks do not inherit an enclosing owner return",
          "[gen][lambda][owner][return][func-ref][void-callable-return][regression]") {
    auto jit = gen_jit(R"SRC(
        module gen_void_callable_owner_return;
        observed : int = 0;
        drops : int = 0;
        struct Result {
            value : int;
            Result(v : int) : value(v) {}
            ~Result() { ++drops; }
        }
        struct Sink {
            emit(value : int) { observed += value; }
        }
        invoke(callback : !(int)) : Result! {
            callback(42);
            return new Result(73);
        }
        invokeMember(callback : Sink::*(int), sink : Sink&) : Result! {
            (sink.*callback)(5);
            return new Result(74);
        }
        test() : int {
            observed = 0; drops = 0;
            {
                callback : !(int) = [](value : int) { observed = value; };
                first : Result! = invoke(callback);
                if (callback != null || observed != 42 || first->value != 73) return -1;
                sink : Sink;
                member : Sink::*(int) = Sink::emit;
                second : Result! = invokeMember(member, sink);
                if (observed != 47 || second->value != 74 || drops != 0) return -2;
                callback = [](value : int) { observed += value; };
                callback(1);
                if (observed != 48) return -3;
            }
            return drops == 2 ? 1 : -4;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(test);
    int result = 0;
    REQUIRE_NOTHROW(result = test());
    CHECK(result == 1);
}

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

TEST_CASE("Lambda: capture-free targets use their declaration owner in every lexical scope",
          "[gen][lambda][owner][aggregate][capture-free-owner][regression]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module capture_free_declaration_owner;
        observed : int = 0;
        globalCallback : *(int):int = [](value : int):int { return value; };
        make() : !(int):int {
            return [](value : int):int { return value; };
        }
        class Factory {
            static memberCallback : *(int):int = [](value : int):int { return value; };
            Factory() {
                {
                    callback : !(int):int = [](value : int):int { return value; };
                    observed = callback(42);
                }
            }
            make() : !(int):int {
                {
                    {
                        return [](value : int):int { return value; };
                    }
                }
            }
        }
        run() : int {
            factory : Factory;
            first : !(int):int = make();
            second : !(int):int = factory.make();
            return observed == 42 && globalCallback(42) == 42
                && Factory::memberCallback(42) == 42 && first(42) == 42
                && second(42) == 42 ? 42 : -1;
        }
    )SRC");
    REQUIRE(comp);
    auto check_targets = [](const auto& scope) {
        size_t count = 0;
        for (const auto& function : scope->functions()) {
            if (!function->is_lambda()) continue;
            ++count;
            REQUIRE(function->get_this_parameter() == nullptr);
            CHECK(function->is_static() == (function->get_owner() != nullptr));
        }
        return count;
    };
    CHECK(check_targets(comp->get_unit()->get_root_namespace()) == 2);
    auto factory = find_aggregate(comp, "Factory");
    REQUIRE(factory);
    CHECK(check_targets(factory) == 3);
    auto jit = comp->to_jit();
    REQUIRE(jit);
    auto run = jit->lookup_symbol<int (*)()>("run");
    REQUIRE(run);
    CHECK(run() == 42);
}

TEST_CASE("Lambda: nested owned callbacks preserve aggregate arguments after the outer closure drops",
          "[gen][lambda][owner][capture][lifecycle][exceptions][capture-free-owner][regression]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module nested_capture_free_callback;
        calls : int = 0;
        drops : int = 0;
        struct Probe {
            value : int = 42;
            ~Probe() { ++drops; }
        }
        struct Done {}
        struct Pair {
            first : int = 17;
            second : int = 25;
        }
        alias Result : Expected<Done, int>;
        completion : !(Result, Pair, bool):int = null;
        inspect(result : Result&, pair : Pair&, fail : bool):int {
            ++calls;
            if (!result.hasResult() || result.hasError()) return -1;
            if (pair.first != 17 || pair.second != 25) return -2;
            if (fail) throw Exception(73);
            return pair.first + pair.second;
        }
        execute(task : !():int):int { return task(); }
        deliver(callback : !(Result, Pair, bool):int,
                result : Result&, pair : Pair&, fail : bool):int {
            return callback(result, pair, fail);
        }
        run(captured : bool, fail : bool):int {
            calls = 0; drops = 0;
            probe : Probe! = new Probe();
            task : !():int = [probe, captured]():int {
                if (captured) {
                    resource : Probe! = new Probe();
                    completion = [resource](result : Result, pair : Pair, fail : bool):int {
                        if (resource->value != 42) return -3;
                        return inspect(result, pair, fail);
                    };
                    if (resource != null) return -4;
                } else {
                    completion = [](result : Result, pair : Pair, fail : bool):int {
                        return inspect(result, pair, fail);
                    };
                }
                return probe->value;
            };
            if (probe != null) return -5;
            if (execute(task) != 42 || task != null || drops != 1) return -6;
            result : Result;
            done : Done;
            result.setResult(done);
            pair : Pair;
            value : int = 0;
            caught : int = 0;
            try { value = deliver(completion, result, pair, fail); }
            catch (e : Throwable&) { caught = e.getCode(); }
            if (completion != null || calls != 1 || drops != (captured ? 2 : 1)) return -7;
            if (caught != (fail ? 73 : 0) || (!fail && value != 42)) return -8;
            return 42;
        }
    )SRC");
    REQUIRE(comp);
    size_t free_targets = 0;
    size_t captured_targets = 0;
    auto check_targets = [&](const auto& self, const auto& scope) -> void {
        for (const auto& function : scope->functions()) {
            if (!function->is_lambda()) continue;
            if (function->get_short_name() == "__invoke") {
                ++captured_targets;
                REQUIRE_FALSE(function->is_static());
                REQUIRE(function->get_this_parameter() != nullptr);
            } else {
                ++free_targets;
                REQUIRE(function->is_static());
                REQUIRE(function->get_this_parameter() == nullptr);
            }
        }
        for (const auto& child : scope->get_children()) {
            if (auto aggregate = std::dynamic_pointer_cast<k::model::aggregate>(child)) {
                self(self, aggregate);
            }
        }
    };
    check_targets(check_targets, comp->get_unit()->get_root_namespace());
    REQUIRE(free_targets == 1);
    REQUIRE(captured_targets == 2);
    auto jit = comp->to_jit();
    REQUIRE(jit);
    auto run = jit->lookup_symbol<int (*)(bool, bool)>("run");
    REQUIRE(run);
    for (bool captured : {false, true}) {
        for (bool fail : {false, true}) {
            CAPTURE(captured, fail);
            CHECK(run(captured, fail) == 42);
        }
    }
}

TEST_CASE("Lambda: explicit delete destroys a locally owned struct exactly once",
          "[gen][lambda][owner][lifecycle][nested-owner-delete][regression]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module nested_lambda_owner_delete;
        drops : int = 0;
        cleared : bool = false;
        struct Probe {
            ~Probe() { ++drops; }
        }
        execute(task : !()) { task(); }
        run() : int {
            drops = 0; cleared = false;
            expectedDrops : int = 1;
            task : !() = [expectedDrops]() {
                probe : Probe! = new Probe();
                delete probe;
                if (probe != null || drops != expectedDrops) return;
                delete probe;
                cleared = probe == null && drops == expectedDrops;
            };
            execute(task);
            return task == null && cleared && drops == 1 ? 1 : -1;
        }
    )SRC");
    REQUIRE(comp);
    auto jit = comp->to_jit();
    REQUIRE(jit);
    auto run = jit->lookup_symbol<int (*)()>("run");
    REQUIRE(run);
    int value = 0;
    REQUIRE_NOTHROW(value = run());
    CHECK(value == 1);
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

TEST_CASE("Lambda: member closures contain only explicit captures",
          "[gen][lambda][owner][capture][lifecycle][aggregate]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module member_lambda_captures;
        destructions: int = 0;
        struct Resource {
            value: int;
            Resource(v: int) : value(v) {}
            ~Resource() { destructions += 1; }
        }
        class Factory {
            first(v: int) : !():int {
                resource: Resource! = new Resource(v);
                return [resource]() : int { return resource.value; };
            }
            second(v: int) : !():int {
                resource: Resource! = new Resource(v);
                return [resource]() : int { return resource.value; };
            }
        }
        run() : int {
            destructions = 0;
            firstTask: !():int = null;
            secondTask: !():int = null;
            {
                factory: Factory;
                firstTask = factory.first(20);
                secondTask = factory.second(22);
            }
            if (firstTask() + secondTask() != 42) return -1;
            firstTask = null;
            if (destructions != 1) return -2;
            secondTask = null;
            return destructions;
        }
    )SRC");
    REQUIRE(comp != nullptr);
    auto factory = find_aggregate(comp, "Factory");
    REQUIRE(factory != nullptr);
    size_t closures = 0;
    for (const auto& child : factory->get_children()) {
        auto closure = std::dynamic_pointer_cast<k::model::aggregate>(child);
        if (!closure) continue;
        ++closures;
        CHECK(closure->is_static_nested());
        CHECK_FALSE(closure->is_inner());
        CHECK(closure->get_parent_field() == nullptr);
        CHECK(closure->get_variable("__parent__") == nullptr);
        REQUIRE(closure->constructors().size() == 1);
        auto ctor = closure->constructors().front();
        CHECK(ctor->get_parameter_size() == 1);
        CHECK(ctor->get_parameter("__parent__") == nullptr);
        REQUIRE(ctor->get_existing_block() != nullptr);
        CHECK(ctor->get_existing_block()->get_statements().size() == 1);
    }
    CHECK(closures == 2);
    auto jit = comp->to_jit();
    REQUIRE(jit != nullptr);
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 2);
}

TEST_CASE("Lambda: direct literals materialize owned arguments and returns",
          "[gen][lambda][owner][param][return][fresh-lambda][lifecycle]") {
    auto jit = gen_jit(R"SRC(
        module fresh_lambda_ownership;
        drops: int = 0;
        struct Resource {
            value: int;
            Resource(v: int): value(v) {}
            ~Resource() { drops += 1; }
        }
        consume(task: !():int): int { return task(); }
        template<typename T>
        execute(task: !():T): T { return task(); }
        make(): !():int {
            resource: Resource! = new Resource(22);
            return [resource]():int { return resource->value; };
        }
        run(): int {
            drops = 0;
            resource: Resource! = new Resource(20);
            value: int = consume([resource]():int { return resource->value; });
            if (resource != null || drops != 1) return -1;
            if (execute<int>([]() { return 42; }) != 42) return -2;
            {
                returned: !():int = make();
                value += returned();
            }
            if (drops != 2) return -3;
            return value;
        }
    )SRC");
    REQUIRE(jit);
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run);
    CHECK(run() == 42);
}

TEST_CASE("Lambda: stored borrowed values cannot become owners",
          "[gen][lambda][owner][error][fresh-lambda]") {
    SECTION("argument") {
        REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
            module borrowed_lambda_argument;
            consume(task: !():int): int { return task(); }
            run(): int {
                borrowed: &():int = []():int { return 42; };
                return consume(borrowed);
            }
        )SRC"), k::log::compiler_error);
    }
    SECTION("return") {
        REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
            module borrowed_lambda_return;
            make(): !():int {
                borrowed: &():int = []():int { return 42; };
                return borrowed;
            }
        )SRC"), k::log::compiler_error);
    }
    SECTION("owning argument still forbids local reference captures") {
        REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
            module borrowed_capture_argument;
            consume(task: !():int): int { return task(); }
            run(): int {
                value: int = 42;
                return consume([&value]():int { return value; });
            }
        )SRC"), k::log::compiler_error);
    }
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

TEST_CASE("Lambda: runnable adapters transfer owned closures with catch handlers",
          "[gen][lambda][owner][capture][aggregate][exceptions]") {
    auto jit = gen_jit(R"SRC(
        module runnable_lambda_adapters;
        failures: int = 0;
        reports: int = 0;
        report(origin: int, code: int) {
            reports += 1;
            failures += origin * code;
        }
        class Command : public Runnable {
            override run() : void throws(Exception) { throw Exception(7); }
        }
        class Lease {
            execute(command: Runnable+) {
                cmd: Runnable* = &(*command);
                firstTask: !() = [cmd]() {
                    try { cmd->run(); }
                    catch (e: Throwable&) { report(1, e.getCode()); }
                };
                execute(firstTask);
                if (firstTask != null) failures = -1000;
                secondTask: !() = [cmd]() {
                    try { cmd->run(); }
                    catch (e: Throwable&) { report(2, e.getCode()); }
                };
                execute(secondTask);
                if (secondTask != null) failures = -1000;
            }
            execute(task: !()) { task(); }
        }
        interface Executor {
            default execute(command: Runnable+) {
                cmd: Runnable* = &(*command);
                task: !() = [cmd]() {
                    try { cmd->run(); }
                    catch (e: Throwable&) { report(3, e.getCode()); }
                };
                execute(task);
                if (task != null) failures = -1000;
            }
            const execute(task: !());
        }
        class InlineExecutor : public Executor {
            override const execute(task: !()) { task(); }
        }
        run() : int {
            failures = 0;
            reports = 0;
            command: Command;
            lease: Lease;
            lease.execute(command);
            executor: InlineExecutor;
            view: Executor+ = executor;
            view->execute(command);
            if (reports != 3) return -1;
            return failures;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

TEST_CASE("Lambda: template member closures use their instantiated scope exactly once",
          "[gen][lambda][owner][capture][template][symbol-order][exceptions]") {
    std::string source = R"SRC(
        module template_lambda_captures;
        total: int = 0;
        reports: int = 0;
        constructions: int = 0;
        destructions: int = 0;
        initializations: int = 0;
        initialize() : int { initializations += 1; return 0; }
        report(code: int) { total += code; reports += 1; }
        class Command {
            run(code: int) : void throws(Exception) { throw Exception(code); }
        }
        template<typename T>
        struct Adapter {
            marker: int;
            Adapter() : marker(initialize()) { constructions += 1; }
            ~Adapter() { destructions += 1; }
            static consume(task: !()) { task(); }
            static execute(command: Command+, value: T) {
                cmd: Command* = &(*command);
                firstTask: !() = [cmd, value]() {
                    try { cmd->run((int) value); }
                    catch (e: Throwable&) { report(e.getCode()); }
                };
                consume(firstTask);
                local: T = value + (T) 1;
                secondTask: !() = [cmd, local]() {
                    try { cmd->run((int) local); }
                    catch (e: Throwable&) { report(e.getCode()); }
                };
                consume(secondTask);
                reportTask: !() = []() { reports += 1; };
                consume(reportTask);
            }
        }
    )SRC";
    SECTION("Eager instantiation through a member declaration") {
        source += R"SRC(
            struct Anchor {
                first: Adapter<int>;
                second: Adapter<long>;
            }
            run() : int {
                total = 0; reports = 0;
                constructions = 0; destructions = 0; initializations = 0;
                command: Command;
                {
                    anchor: Anchor;
                    Adapter<int>::execute(command, 9);
                    Adapter<long>::execute(command, (long) 11);
                }
                if (reports != 6) return -1;
                if (constructions != 2 || destructions != 2 || initializations != 2) return -2;
                return total;
            }
        )SRC";
    }
    SECTION("On-demand instantiation through a static member call") {
        source += R"SRC(
            run() : int {
                total = 0; reports = 0;
                command: Command;
                Adapter<int>::execute(command, 9);
                Adapter<long>::execute(command, (long) 11);
                if (reports != 6) return -1;
                return total;
            }
        )SRC";
    }
    auto comp = compile_model_with_stdlib(source);
    REQUIRE(comp != nullptr);
    size_t adapters = 0;
    for (const auto& child : comp->get_unit()->get_root_namespace()->get_children()) {
        auto adapter = std::dynamic_pointer_cast<k::model::aggregate>(child);
        if (!adapter || !adapter->is_instantiation()
            || adapter->get_short_name().rfind("Adapter__", 0) != 0) continue;
        ++adapters;
        size_t closures = 0;
        for (const auto& nested : adapter->get_children()) {
            auto closure = std::dynamic_pointer_cast<k::model::aggregate>(nested);
            if (!closure) continue;
            ++closures;
            CHECK(closure->is_static_nested());
            CHECK_FALSE(closure->is_inner());
            CHECK(closure->get_variable("__parent__") == nullptr);
            REQUIRE(closure->constructors().size() == 1);
            auto ctor = closure->constructors().front();
            CHECK(ctor->get_parameter_size() == 2);
            REQUIRE(ctor->get_existing_block() != nullptr);
            CHECK(ctor->get_existing_block()->get_statements().size() == 2);
        }
        CHECK(closures == 2);
    }
    CHECK(adapters == 2);
    auto jit = comp->to_jit();
    REQUIRE(jit != nullptr);
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

TEST_CASE("Lambda: unused free template closures retain their lexical template parameters",
          "[gen][lambda][template][owner][capture][free-template-capture]") {
    auto comp = compile_model(R"SRC(
        module unused_template_capture;
        template<typename T>
        struct Runner { run() {} }
        template<typename T>
        makeTask(runner: Runner<T>!) : !() {
            {
                task: !() = [runner]() { runner->run(); };
                return task;
            }
        }
    )SRC");
    REQUIRE(comp != nullptr);
    size_t closures = 0;
    for (const auto& child : comp->get_unit()->get_root_namespace()->get_children()) {
        auto closure = std::dynamic_pointer_cast<k::model::aggregate>(child);
        if (!closure || !closure->get_variable("runner")) continue;
        ++closures;
        REQUIRE(closure->is_template());
        REQUIRE(closure->get_tpl_info()->params.size() == 1);
        CHECK(closure->get_tpl_info()->params[0].name == "T");
        CHECK_FALSE(closure->is_instantiation());
        CHECK(closure->is_static_nested());
        REQUIRE(closure->get_struct_type() != nullptr);
        CHECK_FALSE(closure->get_struct_type()->is_resolved());
        REQUIRE(closure->constructors().size() == 1);
        auto parameter = closure->constructors().front()->get_parameter("runner_p");
        REQUIRE(parameter != nullptr);
        CHECK(k::model::type::contains_unresolved(parameter->get_type()));
        REQUIRE(parameter->get_ast_parameter_spec() != nullptr);
        REQUIRE(parameter->get_ast_parameter_spec()->name.has_value());
        CHECK(parameter->get_ast_parameter_spec()->name->content == "runner");
    }
    CHECK(closures == 1);
}

TEST_CASE("Lambda: free template owner captures use concrete closure constructor types",
          "[gen][lambda][template][owner][capture][lifecycle][free-template-capture]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module free_template_owner_capture;
        destructions: int = 0;
        total: int = 0;
        template<typename R>
        struct TaskPromiseRunner {
            _task: !():R;
            TaskPromiseRunner(task: !():R) { _task = task; }
            ~TaskPromiseRunner() { destructions += 1; }
            run() { total += (int) _task(); }
        }
        template<typename R>
        execute(task: !():R) : !() {
            runner: TaskPromiseRunner<R>! = new TaskPromiseRunner<R>(task);
            taskWrapper: !() = [runner]() { runner->run(); };
            if (runner != null || task != null) return null;
            return taskWrapper;
        }
        template<typename R>
        captureParameter(runner: TaskPromiseRunner<R>!) : !() {
            taskWrapper: !() = [runner]() { runner->run(); };
            if (runner != null) return null;
            return taskWrapper;
        }
        template<typename R>
        constantTask() : !():R {
            return []() : R { return (R) 1; };
        }
        run() : int {
            destructions = 0; total = 0;
            first: !() = execute<int>([]() : int { return 20; });
            second: !() = execute<long>([]() : long { return (long) 22; });
            if (first == null || second == null) return -1;
            first(); second();
            if (total != 42 || destructions != 0) return -2;
            first = null;
            if (destructions != 1) return -3;
            second = null;
            if (destructions != 2) return -4;
            runner: TaskPromiseRunner<int>! = new TaskPromiseRunner<int>(
                []() : int { return 1; });
            third: !() = captureParameter<int>(runner);
            if (runner != null || third == null) return -5;
            third();
            third = null;
            if (destructions != 3 || total != 43) return -6;
            fourth: !():int = constantTask<int>();
            if (fourth() != 1) return -7;
            return 42;
        }
    )SRC");
    REQUIRE(comp != nullptr);
    size_t closures = 0;
    size_t free_targets = 0;
    for (const auto& child : comp->get_unit()->get_root_namespace()->get_children()) {
        if (auto target = std::dynamic_pointer_cast<k::model::function>(child);
            target && target->is_instantiation() && target->is_lambda()) {
            ++free_targets;
            CHECK(target->get_name().has_root_prefix());
            CHECK_FALSE(target->get_mangled_name().empty());
        }
        auto closure = std::dynamic_pointer_cast<k::model::aggregate>(child);
        if (!closure || !closure->is_instantiation() || !closure->get_variable("runner")) continue;
        ++closures;
        CHECK(closure->is_static_nested());
        CHECK_FALSE(closure->is_inner());
        CHECK(closure->get_parent_field() == nullptr);
        REQUIRE(closure->constructors().size() == 1);
        auto ctor = closure->constructors().front();
        REQUIRE(ctor->get_parameter_size() == 1);
        auto parameter = ctor->get_parameter("runner_p");
        REQUIRE(parameter != nullptr);
        auto captured = parameter->get_type();
        REQUIRE(k::model::type::is_owner(captured));
        CHECK_FALSE(k::model::type::contains_unresolved(captured));
        CHECK(k::model::type::are_equal(captured, closure->get_variable("runner")->get_type()));
        REQUIRE(ctor->get_existing_block() != nullptr);
        CHECK(ctor->get_existing_block()->get_statements().size() == 1);
    }
    CHECK(closures == 3);
    CHECK(free_targets == 1);
    auto jit = comp->to_jit();
    REQUIRE(jit != nullptr);
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

TEST_CASE("Lambda: imported free template owner captures construct and invoke concrete closures",
          "[gen][lambda][template][import][owner][lifecycle][free-template-capture]") {
    auto result = build_exec_with_lib(
        R"SRC(
            module free_template_capture_lib;
            drops: int = 0;
            total: int = 0;
            recordDrop() { drops += 1; }
            recordValue(value: int) { total += value; }
            dropCount() : int { return drops; }
            valueTotal() : int { return total; }
            template<typename R>
            struct TaskPromiseRunner {
                _task: !():R;
                TaskPromiseRunner(task: !():R) { _task = task; }
                ~TaskPromiseRunner() { recordDrop(); }
                run() { recordValue((int) _task()); }
            }
            template<typename R>
            execute(task: !():R) : !() {
                runner: TaskPromiseRunner<R>! = new TaskPromiseRunner<R>(task);
                taskWrapper: !() = [runner]() { runner->run(); };
                if (runner != null || task != null) return null;
                return taskWrapper;
            }
        )SRC",
        R"SRC(
            module free_template_capture_consumer;
            import free_template_capture_lib;
            main() : int {
                firstTask: !():int = []() : int { return 20; };
                secondTask: !():long = []() : long { return (long) 22; };
                first: !() = execute<int>(firstTask);
                second: !() = execute<long>(secondTask);
                if (firstTask != null || secondTask != null) return 1;
                if (first == null || second == null) return 2;
                first(); second();
                if (valueTotal() != 42 || dropCount() != 0) return 3;
                first = null;
                if (dropCount() != 1) return 4;
                second = null;
                if (dropCount() != 2) return 5;
                return 42;
            }
        )SRC");
    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    CHECK(result.exit_code == 42);
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

TEST_CASE("Lambda: forwarding owned tasks clears every source and drops each closure once",
          "[gen][lambda][owner][move][param][overload][lifecycle]") {
    auto jit = gen_jit(R"SRC(
        module owned_task_forwarding;
        destructions: int = 0;
        total: int = 0;
        struct Resource {
            value: int;
            Resource(v: int) : value(v) {}
            ~Resource() { destructions += 1; }
        }
        forward(task: !()) : bool {
            consume(task);
            return task == null;
        }
        consume(task: !()) { task(); }
        class Relay {
            forward(task: !()) : bool {
                execute(task);
                return task == null;
            }
            execute(task: !()) { task(); }
        }
        run() : int {
            destructions = 0;
            total = 0;
            {
                resource: Resource! = new Resource(20);
                task: !() = [resource]() { total += resource.value; };
                if (resource != null) return -1;
                if (!forward(task) || task != null || destructions != 1) return -2;
            }
            if (destructions != 1) return -3;
            {
                relay: Relay;
                resource: Resource! = new Resource(22);
                task: !() = [resource]() { total += resource.value; };
                if (!relay.forward(task) || task != null || destructions != 2) return -4;
            }
            if (destructions != 2) return -5;
            return total;
        }
    )SRC");
    REQUIRE(jit);
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run);
    CHECK(run() == 42);
}

TEST_CASE("Lambda: forwarded owned error suppliers retain their owner return type",
          "[gen][lambda][owner][move][param][exceptions]") {
    auto jit = gen_jit(R"SRC(
        module owned_error_supplier_forwarding;
        class Errors {
            forward(supplier: !():Throwable!) : int {
                code: int = consume(supplier);
                return supplier == null ? code : -1;
            }
            consume(supplier: !():Throwable!) : int {
                error: Throwable! = supplier();
                return error.getCode();
            }
        }
        run() : int {
            errors: Errors;
            code: int = 42;
            supplier: !():Throwable! = [code]() : Throwable! {
                return new Exception(code);
            };
            result: int = errors.forward(supplier);
            return supplier == null ? result : -2;
        }
    )SRC");
    REQUIRE(jit);
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run);
    CHECK(run() == 42);
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
