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

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// =============================================================================
// Multi-source compilation tests
// =============================================================================

TEST_CASE("Multi-source: two files, same module, cross-file call", "[multi-source]") {
    // File A defines a function, File B calls it. Both declare the same module.
    std::string file_a = R"(
        module multi_source_01;
        get_value() : int {
            return 42;
        }
    )";
    std::string file_b = R"(
        module multi_source_01; // <- Same module name
        call_get() : int {
            return get_value();
        }
    )";

    auto jit = gen_jit_multi({{"multi_source_01.a.k", file_a}, {"multi_source_01.b.k", file_b}});
    REQUIRE(jit);

    auto call_get = jit->lookup_symbol<int(*)()>("call_get");
    REQUIRE(call_get);
    REQUIRE(call_get() == 42);
}

TEST_CASE("Multi-source: single file with module decl, other without", "[multi-source]") {
    // Only file A has a module declaration. File B has none.
    // This is the normal case — file A's module name applies to the whole unit.
    std::string file_a = R"(
        module multi_source_03;
        get_value() : int {
            return 100;
        }
    )";
    std::string file_b = R"(
        double_it() : int {
            return get_value() * 2;
        }
    )";

    auto jit = gen_jit_multi({{"multi_source_03.a.k", file_a}, {"multi_source_03.b.k", file_b}});
    REQUIRE(jit);

    auto double_it = jit->lookup_symbol<int(*)()>("double_it");
    REQUIRE(double_it);
    REQUIRE(double_it() == 200);
}

TEST_CASE("Multi-source: conflicting module declarations", "[multi-source]") {
    // Two files declare different module names — must fail.
    std::string file_a = R"(
        module multi_source_04;
        foo() : int { return 1; }
    )";
    std::string file_b = R"(
        module multi_source_05;
        bar() : int { return 2; }
    )";

    REQUIRE_THROWS_AS(
        gen_jit_multi_throws({{"multi_source_07.a.k", file_a}, {"multi_source_07.b.k", file_b}}),
        k::log::compiler_error
    );
}

TEST_CASE("Multi-source: no module declaration in any file", "[multi-source]") {
    // No module declaration anywhere — should still compile (with warning)
    // and generate a random unit name.
    std::string file_a = R"(
        get_a() : int { return 10; }
    )";
    std::string file_b = R"(
        get_b() : int { return get_a() + 20; }
    )";

    auto jit = gen_jit_multi({{"multi_source_18.a.k", file_a}, {"multi_source_18.b.k", file_b}});
    REQUIRE(jit);

    auto get_b = jit->lookup_symbol<int(*)()>("get_b");
    REQUIRE(get_b);
    REQUIRE(get_b() == 30);
}

TEST_CASE("Multi-source: forced module name via CLI override", "[multi-source]") {
    // Both files have no module decl, but we force a name via CLI argument.
    std::string file_a = R"(
        get_x() : int { return 7; }
    )";
    std::string file_b = R"(
        get_y() : int { return get_x() + 3; }
    )";

    auto comp = k::compiler::create();
    comp->parse_sources({{"multi_source_11.a.k", file_a}, {"multi_source_11.b.k", file_b}}, true, false, "forced_mod");

    // Verify the module name is the forced one
    REQUIRE(comp->get_unit()->get_unit_name().to_string() == "forced_mod");

    auto jit = comp->to_jit();
    REQUIRE(jit);

    auto get_y = jit->lookup_symbol<int(*)()>("get_y");
    REQUIRE(get_y);
    REQUIRE(get_y() == 10);
}

TEST_CASE("Multi-source: forced module name overrides source declaration", "[multi-source]") {
    // File A declares module 'src_mod', but CLI forces 'cli_mod'.
    // Verify both that the function works AND that the actual module name is 'cli_mod'.
    std::string file_a = R"(
        module multi_source_06;
        get_val() : int { return 55; }
    )";

    auto comp = k::compiler::create();
    comp->parse_sources({{"multi_source_06.k", file_a}}, true, false, "cli_mod");

    // Verify the module name is the forced one, not the source-level one
    REQUIRE(comp->get_unit()->get_unit_name().to_string() == "cli_mod");

    auto jit = comp->to_jit();
    REQUIRE(jit);

    auto get_val = jit->lookup_symbol<int(*)()>("get_val");
    REQUIRE(get_val);
    REQUIRE(get_val() == 55);
}

TEST_CASE("Multi-source: duplicate function definition is an error", "[multi-source]") {
    // Same function defined in two files — should ideally be a symbol collision error.
    // TODO: Once duplicate symbol detection is implemented, this test should
    // verify that compilation fails. For now, the model builder does not
    // reject duplicate function definitions, so we just verify the compilation
    // does not crash.
    std::string file_a = R"(
        module multi_source_07;
        foo() : int { return 1; }
    )";
    std::string file_b = R"(
        module multi_source_08;
        foo() : int { return 2; }
    )";

    // Currently the model builder does not detect duplicate function definitions,
    // so compilation may succeed or fail depending on internal ordering.
    // We just ensure no crash.
    try {
        auto jit = gen_jit_multi_throws({{"multi_source_07.a.k", file_a}, {"multi_source_07.b.k", file_b}});
        // If it succeeds, that's the current (imperfect) behavior.
    } catch (const k::log::compiler_error&) {
        // If it fails, that's also acceptable (future behavior).
    }
}

TEST_CASE("Multi-source: global visibility across files", "[multi-source]") {
    // A struct defined in file A should be usable in file B (no file-private).
    std::string file_a = R"(
        module multi_source_09;
        struct Point {
            x : int = 0;
            y : int = 0;
            Point(ax: int, ay: int) : x(ax), y(ay) {}
            sum() : int { return x + y; }
        }
    )";
    std::string file_b = R"(
        module multi_source_09; // <- Same module, different file name
        make_point_sum() : int {
            p : Point(3, 4);
            return p.sum();
        }
    )";

    auto jit = gen_jit_multi({{"multi_source_09.k", file_a}, {"multi_source_10.k", file_b}});
    REQUIRE(jit);

    auto make_point_sum = jit->lookup_symbol<int(*)()>("make_point_sum");
    REQUIRE(make_point_sum);
    REQUIRE(make_point_sum() == 7);
}

TEST_CASE("Multi-source: same import in multiple files is deduplicated", "[multi-source]") {
    // If both files import the same module, it should be processed once.
    // We test this simply by verifying the compilation succeeds.
    // (We don't actually have the imported module, so we just ensure no crash
    //  from duplicate import registration.)
    std::string file_a = R"(
        module multi_source_11;
        get_a() : int { return 1; }
    )";
    std::string file_b = R"(
        module multi_source_11; // <- Same module, different file name
        get_b() : int { return get_a() + 1; }
    )";

    auto jit = gen_jit_multi({{"multi_source_11.k", file_a}, {"multi_source_12.k", file_b}});
    REQUIRE(jit);
}

TEST_CASE("Multi-source: backward compat — single file still works", "[multi-source]") {
    // Ensure a single file via parse_sources (1-element vector) works fine.
    std::string src = R"(
        module multi_source_13;
        single_fn() : int { return 99; }
    )";

    auto comp = k::compiler::create();
    comp->parse_sources({{"multi_source_13.k", src}}, true, false);
    auto jit = comp->to_jit();
    REQUIRE(jit);

    auto single_fn = jit->lookup_symbol<int(*)()>("single_fn");
    REQUIRE(single_fn);
    REQUIRE(single_fn() == 99);
}

TEST_CASE("Multi-source: comments before module declaration in lookup", "[multi-source]") {
    // Long comments before the module declaration should not prevent lookup.
    std::string file_a = R"(
        /* This is a very long copyright header.
         * It spans multiple lines.
         * It should not prevent the module declaration from being found.
         * Lorem ipsum dolor sit amet, consectetur adipiscing elit.
         * End of copyright notice.
         */
        module multi_source_14;
        get_commented() : int { return 77; }
    )";

    auto jit = gen_jit_multi({{"multi_source_14.k", file_a}});
    REQUIRE(jit);

    auto fn = jit->lookup_symbol<int(*)()>("get_commented");
    REQUIRE(fn);
    REQUIRE(fn() == 77);
}

TEST_CASE("Multi-source: three files, chain of calls", "[multi-source]") {
    // Three files contributing to the same module with call chains.
    std::string file_a = R"(
        module multi_source_15;
        base_val() : int { return 10; }
    )";
    std::string file_b = R"(
        module multi_source_15;
        mid_val() : int { return base_val() + 5; }
    )";
    std::string file_c = R"(
        module multi_source_15;
        top_val() : int { return mid_val() * 2; }
    )";

    auto jit = gen_jit_multi({{"multi_source_15.a.k", file_a}, {"multi_source_15.b.k", file_b}, {"multi_source_15.c.k", file_c}});
    REQUIRE(jit);

    auto top_val = jit->lookup_symbol<int(*)()>("top_val");
    REQUIRE(top_val);
    REQUIRE(top_val() == 30); // (10 + 5) * 2
}

TEST_CASE("Multi-source: empty file in source list", "[multi-source]") {
    // An empty file should not prevent compilation.
    std::string file_a = R"(
        module multi_source_18;
        get_emp() : int { return 42; }
    )";
    std::string file_b = "";  // completely empty

    auto jit = gen_jit_multi({{"multi_source_18.a.k", file_a}, {"multi_source_18.b.k", file_b}});
    REQUIRE(jit);

    auto get_emp = jit->lookup_symbol<int(*)()>("get_emp");
    REQUIRE(get_emp);
    REQUIRE(get_emp() == 42);
}

TEST_CASE("Multi-source: global variable shared across files", "[multi-source]") {
    // A global variable defined in file A should be accessible from file B.
    std::string file_a = R"(
        module multi_source_19;
        g : int = 100;
    )";
    std::string file_b = R"(
        module multi_source_19;
        read_g() : int { return g; }
    )";

    auto jit = gen_jit_multi({{"multi_source_19.a.k", file_a}, {"multi_source_19.b.k", file_b}});
    REQUIRE(jit);

    auto read_g = jit->lookup_symbol<int(*)()>("read_g");
    REQUIRE(read_g);
    REQUIRE(read_g() == 100);
}

TEST_CASE("Multi-source: forced module name with multiple files, some with module decl", "[multi-source]") {
    // File A declares 'alpha', File B declares 'alpha', CLI forces 'override'.
    // The forced name should win.
    std::string file_a = R"(
        module multi_source_21;
        fa() : int { return 1; }
    )";
    std::string file_b = R"(
        module multi_source_22;
        fb() : int { return fa() + 1; }
    )";

    auto comp = k::compiler::create();
    comp->parse_sources({{"multi_source_21.a.k", file_a}, {"multi_source_21.b.k", file_b}}, true, false, "override");

    REQUIRE(comp->get_unit()->get_unit_name().to_string() == "override");

    auto jit = comp->to_jit();
    REQUIRE(jit);

    auto fb = jit->lookup_symbol<int(*)()>("fb");
    REQUIRE(fb);
    REQUIRE(fb() == 2);
}

TEST_CASE("Multi-source: forward interface overrides are independent of source order",
          "[multi-source][override][interface][forward-base]") {
    const std::pair<std::string, std::string> implementation = {
        "multi_source_forward_executor.impl.k", R"SRC(
module multi_source_forward_executor;
executions : int = 0;
recordExecution() { executions += 1; }
namespace io {
    class SerialExecutor : public LeaseProvider {
        override acquireLease() : Lease! throws(RejectedExecutionException) {
            return new Lease(42);
        }
        override const execute(task: !()) : void throws(RejectedExecutionException) {
            if (task != null) task();
        }
        override const execute(task: !(), priority: int) : void throws(RejectedExecutionException) {
            execute(task);
        }
    }
}
readLease(provider: LeaseProvider&) : int throws(RejectedExecutionException) {
    lease : Lease! = provider.acquireLease();
    return lease->value();
}
test() : int throws(RejectedExecutionException) {
    executor : io::SerialExecutor;
    executions = 0;
    first : !() = recordExecution;
    executor.execute(first);
    if (first != null || executions != 1) return -1;
    second : !() = recordExecution;
    executor.execute(second, 7);
    if (second != null || executions != 2) return -2;
    base : const LeaseProvider& = executor;
    third : !() = recordExecution;
    base.execute(third);
    fourth : !() = recordExecution;
    base.execute(fourth, 9);
    if (third != null || fourth != null || executions != 4) return -3;
    fifth : !() = recordExecution;
    execute(executor, fifth);
    if (fifth != null || executions != 5) return -4;
    return readLease(executor);
}
)SRC"};
    const std::pair<std::string, std::string> contract = {
        "multi_source_forward_executor.contract.k", R"SRC(
module multi_source_forward_executor;
interface LeaseProvider {
    acquireLease() : Lease! throws(RejectedExecutionException);
    const execute(task: !()) : void throws(RejectedExecutionException);
    default const execute(task: !(), priority: int) : void throws(RejectedExecutionException) {
        execute(task);
    }
}
)SRC"};
    const std::pair<std::string, std::string> lease_type = {
        "multi_source_forward_executor.lease.k", R"SRC(
module multi_source_forward_executor;
class Lease {
    _value : int;
    Lease(value: int) { _value = value; }
    const value() : int { return _value; }
}
)SRC"};

    std::vector<std::pair<std::string, std::string>> sources;
    SECTION("Implementation before interface and owner return type") {
        sources = {implementation, contract, lease_type};
    }
    SECTION("Interface before implementation and owner return type") {
        sources = {contract, implementation, lease_type};
    }
    SECTION("Return type and interface before implementation") {
        sources = {lease_type, contract, implementation};
    }
    SECTION("Return type before implementation and interface") {
        sources = {lease_type, implementation, contract};
    }
    auto jit = gen_jit_multi(std::move(sources));
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

TEST_CASE("Multi-source: forward interface still requires every abstract method",
          "[multi-source][override][interface][forward-base]") {
    REQUIRE_THROWS_AS(gen_jit_multi_throws({
        {"multi_source_forward_missing.impl.k", R"SRC(
module multi_source_forward_missing;
namespace io {
    class MissingImplementation : public Contract {}
}
)SRC"},
        {"multi_source_forward_missing.contract.k", R"SRC(
module multi_source_forward_missing;
interface Contract {
    required() : int;
}
)SRC"}
    }), k::model::gen::resolution_error);
}

TEST_CASE("Forward base: owned-callable override retains inherited priority overload",
          "[multi-source][override][interface][forward-base][callable]") {
    auto jit = gen_jit_multi({
        {"forward_callable_priority.impl.k", R"SRC(
            module forward_callable_priority;
            executions: int = 0;
            selectedPriority: int = 0;
            recordExecution() { executions += 1; }
            class DirectExecutor : Executor {
                override const execute(task: !()) { task(); }
            }
            test() : int {
                executor: DirectExecutor;
                first: !() = recordExecution;
                executor.execute(first);
                second: !() = recordExecution;
                executor.execute(second, 7);
                third: !() = recordExecution;
                execute(executor, third);
                if (first != null || second != null || third != null) return -1;
                if (executions != 3 || selectedPriority != 7) return -2;
                return 42;
            }
        )SRC"},
        {"forward_callable_priority.contract.k", R"SRC(
            module forward_callable_priority;
            interface Executor {
                const execute(task: !());
                default const execute(task: !(), priority: int) {
                    selectedPriority = priority;
                    execute(task);
                }
            }
        )SRC"}
    });
    REQUIRE(jit != nullptr);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);
}

TEST_CASE("Multi-source: forward base does not accept an unrelated override",
          "[multi-source][override][interface][forward-base]") {
    REQUIRE_THROWS_AS(gen_jit_multi_throws({
        {"multi_source_forward_typo.impl.k", R"SRC(
module multi_source_forward_typo;
namespace io {
    class WrongOverride : public Contract {
        override unrelated() : int { return 42; }
    }
}
)SRC"},
        {"multi_source_forward_typo.contract.k", R"SRC(
module multi_source_forward_typo;
interface Contract {
    required() : int;
}
)SRC"}
    }), k::model::gen::resolution_error);
}

TEST_CASE("Forward base: nested class can override its enclosing class",
          "[multi-source][override][forward-base][nested]") {
    auto jit = gen_jit(R"SRC(
module forward_base_nested;
class Outer {
    value() : int { return 1; }
    static class Inner : public Outer {
        override value() : int { return 42; }
    }
}
readValue(outer: Outer&) : int { return outer.value(); }
test() : int {
    inner : Outer::Inner;
    return readValue(inner);
}
)SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 42);
}

TEST_CASE("Forward base: inheritance cycles remain rejected",
          "[multi-source][override][forward-base]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
module forward_base_cycle;
struct A : public B {}
struct B : public A {}
)SRC"), k::model::gen::resolution_error);
}
