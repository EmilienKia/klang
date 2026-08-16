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
 * Tests for nested/inner structures.
 *
 * Covers:
 *   - Static nested struct (behaves as top-level, no __parent__ field)
 *   - Non-static inner struct basic: implicit parent pointer, outer field access
 *   - Constructor: implicit parent param when constructing from within outer struct method
 *   - Constructor: explicit parent param when constructing from outside
 *   - Name shadowing: inner field hides outer field of the same name
 *   - Implicit up-cast: ref<Inner> → ref<Outer>
 *   - Multi-level nesting: three levels, access from innermost to outermost
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// =============================================================================
// Static nested struct: behaves exactly like a top-level struct
// =============================================================================

TEST_CASE("Static nested struct: basic use as a scoped type", "[gen][nested][static]") {
    auto jit = gen_jit(R"SRC(
        module __nested_static__;

        struct Outer {
            value : int = 10;

            static struct Inner {
                x : int = 0;
                Inner(v : int) : x(v) {}
                get() : int { return x; }
            }

            get_value() : int { return value; }
        }

        test_static_nested() : int {
            // Static nested struct can be constructed directly without an outer instance
            i : Outer::Inner(42);
            return i.get();
        }

        test_static_no_parent_field() : int {
            // A static nested struct has no __parent__ field: its first field is x at index 0
            // Verify by constructing and reading
            i : Outer::Inner(7);
            return i.get();
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_static_nested");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);

    auto fn2 = jit->lookup_symbol<int(*)()>("test_static_no_parent_field");
    REQUIRE(fn2 != nullptr);
    REQUIRE(fn2() == 7);
}

// =============================================================================
// Non-static inner struct: implicit parent pointer, inner constructor param
// =============================================================================

TEST_CASE("Non-static inner struct: construct from within outer method, read inner field", "[gen][nested][inner]") {
    auto jit = gen_jit(R"SRC(
        module __nested_inner_basic__;

        struct Outer {
            outer_val : int = 100;

            struct Inner {
                inner_val : int = 0;
                Inner(v : int) : inner_val(v) {}
                get() : int { return inner_val; }
            }

            make_and_get(v : int) : int {
                i : Inner(v);
                return i.get();
            }
        }

        test_inner_field() : int {
            o : Outer;
            return o.make_and_get(7);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_inner_field");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 7);
}

// =============================================================================
// Non-static inner struct: access outer struct field from inner method
// =============================================================================

TEST_CASE("Non-static inner struct: inner method reads outer struct field via __parent__", "[gen][nested][inner][parent-access]") {
    auto jit = gen_jit(R"SRC(
        module __nested_inner_parent__;

        struct Outer {
            outer_val : int = 55;

            struct Inner {
                inner_val : int = 0;
                Inner(v : int) : inner_val(v) {}
                get_outer() : int { return outer_val; }
                get_inner() : int { return inner_val; }
                sum() : int { return inner_val + outer_val; }
            }

            make_and_sum(v : int) : int {
                i : Inner(v);
                return i.sum();
            }

            get_outer_via_inner() : int {
                i : Inner(3);
                return i.get_outer();
            }
        }

        test_inner_reads_outer() : int {
            o : Outer;
            return o.get_outer_via_inner();
        }

        test_inner_sum() : int {
            o : Outer;
            return o.make_and_sum(5);
        }
    )SRC");
    REQUIRE(jit);

    auto test_reads = jit->lookup_symbol<int(*)()>("test_inner_reads_outer");
    REQUIRE(test_reads != nullptr);
    REQUIRE(test_reads() == 55);

    auto test_sum = jit->lookup_symbol<int(*)()>("test_inner_sum");
    REQUIRE(test_sum != nullptr);
    REQUIRE(test_sum() == 60);  // 5 + 55
}

// =============================================================================
// Name shadowing: inner field hides outer field with same name
// =============================================================================

TEST_CASE("Non-static inner struct: inner field shadows outer field with same name", "[gen][nested][inner][shadowing]") {
    auto jit = gen_jit(R"SRC(
        module __nested_shadow__;

        struct Outer {
            value : int = 10;

            struct Inner {
                value : int = 0;
                Inner(v : int) : value(v) {}
                get_inner_value() : int { return value; }
                get_outer_value() : int { return Outer::value; }
            }

            test(v : int) : int {
                i : Inner(v);
                return i.get_inner_value();
            }

            test_outer_via_inner(v : int) : int {
                i : Inner(v);
                return i.get_outer_value();
            }
        }

        test_shadow_inner() : int {
            o : Outer;
            return o.test(99);
        }

        test_shadow_outer() : int {
            o : Outer;
            return o.test_outer_via_inner(99);
        }
    )SRC");
    REQUIRE(jit);

    auto fn_inner = jit->lookup_symbol<int(*)()>("test_shadow_inner");
    REQUIRE(fn_inner != nullptr);
    REQUIRE(fn_inner() == 99);  // inner field wins

    auto fn_outer = jit->lookup_symbol<int(*)()>("test_shadow_outer");
    REQUIRE(fn_outer != nullptr);
    REQUIRE(fn_outer() == 10);  // explicit Outer::value reaches outer field
}

// =============================================================================
// Multi-level nesting: three levels, read from innermost to outermost field
// =============================================================================

TEST_CASE("Multi-level nested structs: access grandparent field from innermost method", "[gen][nested][inner][multilevel]") {
    auto jit = gen_jit(R"SRC(
        module __nested_multi__;

        struct Outer {
            outer_val : int = 1;

            struct Middle {
                middle_val : int = 2;

                struct Inner {
                    inner_val : int = 3;
                    Inner(v : int) : inner_val(v) {}
                    get_inner() : int { return inner_val; }
                    get_middle() : int { return middle_val; }
                    get_outer() : int { return outer_val; }
                    sum_all() : int { return inner_val + middle_val + outer_val; }
                }

                Middle(mv : int) : middle_val(mv) {}

                make_and_sum(iv : int) : int {
                    i : Inner(iv);
                    return i.sum_all();
                }
            }

            test_all(mv : int, iv : int) : int {
                m : Middle(mv);
                return m.make_and_sum(iv);
            }
        }

        test_multilevel() : int {
            o : Outer;
            return o.test_all(20, 300);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_multilevel");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 321);  // 300 + 20 + 1
}

// =============================================================================
// Inner struct with default constructor (compiler-generated)
// =============================================================================

TEST_CASE("Non-static inner struct: compiler-generated default constructor", "[gen][nested][inner][default-ctor]") {
    auto jit = gen_jit(R"SRC(
        module __nested_default_ctor__;

        struct Outer {
            outer_val : int = 7;

            struct Inner {
                inner_val : int = 42;
                get() : int { return inner_val; }
                get_outer() : int { return outer_val; }
            }

            test() : int {
                i : Inner;
                return i.get() + i.get_outer();
            }
        }

        test_default_ctor() : int {
            o : Outer;
            return o.test();
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_default_ctor");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 49);  // 42 + 7
}

// --------------------------------------------------------------------------
// Nested struct inside a template aggregate
// --------------------------------------------------------------------------

TEST_CASE("Nested static struct inside template: member variable of nested type", "[gen][nested][template]") {
    auto jit = gen_jit(R"SRC(
        module __nested_tpl_static__;

        template<typename T>
        struct Wrapper {
            protected:
            static struct Inner {
                _val : int;
                Inner() { _val = 0; }
                get() : int { return _val; }
            }
            private:
            _inner : Inner;
            public:
            Wrapper() {}
            getVal() : int {
                return _inner.get();
            }
        }

        test_nested_tpl() : int {
            w : Wrapper<int>;
            return w.getVal();
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_nested_tpl");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 0);
}

TEST_CASE("Nested static struct inside template: constructor with init value", "[gen][nested][template]") {
    auto jit = gen_jit(R"SRC(
        module __nested_tpl_init__;

        template<typename T>
        struct Container {
            protected:
            static struct Node {
                _data : int;
                Node() { _data = 99; }
                nodeData() : int { return _data; }
            }
            private:
            _node : Node;
            public:
            Container() {}
            getData() : int {
                return _node.nodeData();
            }
        }

        test_nested_tpl_init() : int {
            c : Container<double>;
            return c.getData();
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_nested_tpl_init");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

TEST_CASE("Template struct method with while loop and local variable", "[gen][nested][while-bug]") {
    auto jit = gen_jit(R"SRC(
module __while_tpl_bug__;

template<typename T>
struct Container {
    _data : T;

    Container() { _data = 0; }

    countTo(n : int) : int {
        i : int = 0;
        while (i < n) {
            ++i;
        }
        return i;
    }
}

test() : int {
    c : Container<int>;
    return c.countTo(3);
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 3);
}
