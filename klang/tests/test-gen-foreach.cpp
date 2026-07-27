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
 * Tests for the ARRAY variant of the `foreach` statement:
 *   for ( [specifiers] name : type = source_expr ) nested_stmt
 *
 * Where source_expr is an array (or reference to an array), sized or unsized,
 * including a primitive-type array temporary literal built directly in the
 * init expression (e.g. `int[]{1, 2, 3}`). The loop variable is local to the
 * foreach statement: constructed then destroyed at every iteration.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Basic iteration: copy semantics
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Foreach array — sum elements via copy", "[gen][foreach][array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int[5]{1, 2, 3, 4, 5};
            sum : int = 0;
            for(x : int = arr) {
                sum = sum + x;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 15);
}

TEST_CASE("Foreach array — copy does not mutate source array", "[gen][foreach][array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int[3]{1, 2, 3};
            for(x : int = arr) {
                x = x * 100;
            }
            return arr[0] + arr[1] + arr[2];
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 6); // unchanged: 1 + 2 + 3
}

// ─────────────────────────────────────────────────────────────────────────────
// Reference semantics: mutation in place
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Foreach array — mutate elements via reference", "[gen][foreach][array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int[4]{1, 2, 3, 4};
            for(x : int& = arr) {
                x = x * 10;
            }
            return arr[0] + arr[1] + arr[2] + arr[3];
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 100); // (1+2+3+4)*10
}

TEST_CASE("Foreach array — const array requires const reference", "[gen][foreach][array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            const arr : int[3]{5, 6, 7};
            sum : int = 0;
            for(x : const int& = arr) {
                sum = sum + x;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 18);
}

// ─────────────────────────────────────────────────────────────────────────────
// Primitive-type array temporary literal as the source expression
//
// `int[]{...}` (and other primitive-type keywords: bool, byte, char, short,
// long, float, double) is recognised as a temporary array literal directly in
// the foreach init expression, exactly like a struct/class type name already
// was (e.g. `Point[]{...}`). Only single-keyword (non-compound) primitive
// types are supported this way: 'unsigned int' / 'long long' still require an
// identifier-driven form and are unaffected by this feature.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Foreach array — iterates over a primitive array temporary literal by copy",
          "[gen][foreach][array][temporary]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            sum : int = 0;
            for(x : int = int[]{1, 2, 30}) {
                sum = sum + x;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 33);
}

TEST_CASE("Foreach array — reference loop variable can bind to a primitive array temporary literal",
          "[gen][foreach][array][temporary]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            sum : int = 0;
            for(x : int& = int[]{1, 2, 30}) {
                sum = sum + x;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 33);
}

TEST_CASE("Foreach array — owner loop variable is still forbidden with a temporary array literal source",
          "[gen][foreach][array][temporary][errors]") {
    REQUIRE(compile_should_fail(R"SRC(
        module __foreach_temp_owner_forbidden__;

        test() : void {
            for(x : int! = int[]{1, 2, 3}) {
            }
        }
    )SRC", nullptr));
}

TEST_CASE("Foreach array — array literal source expression is evaluated exactly once, "
          "not once per iteration/condition-check",
          "[gen][foreach][array][temporary][regression]") {
    // Regression test for the bug where the ARRAY-variant `foreach` re-cloned its
    // `source_expr` twice per codegen site (once for the `.size` loop-condition
    // check, once for the per-iteration subscript), causing a non-idempotent source
    // expression (here, an array literal whose elements call a side-effecting
    // counting function) to be reconstructed on every iteration instead of exactly
    // once. Each element-call increments a shared counter; if the source array were
    // rebuilt more than once, `get_call_count()` would read a value far above 3 and
    // `sum` would reflect stale/inconsistent element values across iterations.
    auto jit = gen_jit(R"SRC(
        module test;

        struct Counter {
            static call_count : int = 0;
        }

        next() : int {
            Counter::call_count = Counter::call_count + 1;
            return Counter::call_count;
        }

        test() : int {
            sum : int = 0;
            for(x : int = int[]{next(), next(), next()}) {
                sum = sum + x;
            }
            return sum;
        }

        get_call_count() : int {
            return Counter::call_count;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    auto get_call_count = jit->lookup_symbol<int(*)()>("get_call_count");
    REQUIRE(test != nullptr);
    REQUIRE(get_call_count != nullptr);
    REQUIRE(test() == 6);          // 1 + 2 + 3
    REQUIRE(get_call_count() == 3); // `next()` called exactly once per element, exactly once overall
}

// ─────────────────────────────────────────────────────────────────────────────
// Sized vs unsized array parameters
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Foreach over an unsized array reference parameter (sized→unsized widening)",
          "[gen][foreach][array]") {
    // Previously a known limitation: passing a sized array `int[4]` to an
    // `int[]&` (or `+`/`*`/`?`) parameter relies on the sized→unsized array
    // implicit widening conversion. Fixed by:
    //  - context::from_type_specifier: explicit `T[]&` no longer double-wraps
    //    the reference (it used to produce ref<ref<array<T>>> instead of the
    //    single-level ref<array<T>> that bare `T[]` and overload resolution
    //    expect).
    //  - check_and_insert_inheritance_cast / adapt_from_{pointer,link,view,owner}:
    //    now accept a sized-array source when the target is the matching
    //    unsized array, for all indirection kinds.
    // See `klang/tests/test-gen-array-unsized-conv.cpp` for focused coverage
    // of every indirection kind; this test keeps the original foreach repro.
    auto jit = gen_jit(R"SRC(
        module test;

        sumArr(arr : int[]&) : int {
            sum : int = 0;
            for(x : int = arr) {
                sum = sum + x;
            }
            return sum;
        }

        test() : int {
            a : int[4]{1, 2, 3, 4};
            return sumArr(a);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 10);
}

// ─────────────────────────────────────────────────────────────────────────────
// break / continue
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Foreach array — break stops iteration early", "[gen][foreach][array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int[5]{1, 2, 3, 4, 5};
            sum : int = 0;
            for(x : int = arr) {
                if(x == 4) {
                    break;
                }
                sum = sum + x;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 6); // 1 + 2 + 3
}

TEST_CASE("Foreach array — continue skips one element", "[gen][foreach][array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int[5]{1, 2, 3, 4, 5};
            sum : int = 0;
            for(x : int = arr) {
                if(x == 3) {
                    continue;
                }
                sum = sum + x;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 12); // 1 + 2 + 4 + 5
}

// ─────────────────────────────────────────────────────────────────────────────
// Nullable addresser content
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Foreach array — nullable pointer elements can be null", "[gen][foreach][array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            a : int = 1;
            b : int = 2;
            arr : int*[3]{ &a, null, &b };
            count : int = 0;
            for(p : int* = arr) {
                if(p == null) {
                    count = count + 100;
                } else {
                    count = count + *p;
                }
            }
            return count;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 103); // 1 + 100 + 2
}

// ─────────────────────────────────────────────────────────────────────────────
// Regression: classic for-loop must still parse and work (non-regression)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Foreach array — classic for loop is unaffected", "[gen][foreach][array][regression]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            sum : int = 0;
            for(i : int = 0; i < 5; i++) {
                sum = sum + i;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 10); // 0+1+2+3+4
}

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction per iteration
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Foreach array — loop variable constructed/destructed each iteration", "[gen][foreach][array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        struct Counter {
            static ctor_count : int = 0;
            static dtor_count : int = 0;
            val : int;
            Counter(v : int) {
                val = v;
                ctor_count = ctor_count + 1;
            }
            ~Counter() {
                dtor_count = dtor_count + 1;
            }
        }

        run_loop() : void {
            arr : int[3]{1, 2, 3};
            for(x : Counter = arr) {
            }
        }

        get_ctor_count() : int {
            return Counter::ctor_count;
        }

        get_dtor_count() : int {
            return Counter::dtor_count;
        }
    )SRC");
    REQUIRE(jit);
    auto run_loop = jit->lookup_symbol<void(*)()>("run_loop");
    auto get_ctor_count = jit->lookup_symbol<int(*)()>("get_ctor_count");
    auto get_dtor_count = jit->lookup_symbol<int(*)()>("get_dtor_count");
    REQUIRE(run_loop != nullptr);
    REQUIRE(get_ctor_count != nullptr);
    REQUIRE(get_dtor_count != nullptr);
    // Both counters must be 0 before the loop runs (each construct/destruct
    // happens strictly once per iteration when the loop actually executes).
    REQUIRE(get_ctor_count() == 0);
    REQUIRE(get_dtor_count() == 0);
    run_loop();
    REQUIRE(get_ctor_count() == 3);
    REQUIRE(get_dtor_count() == 3);
}

// ─────────────────────────────────────────────────────────────────────────────
// Error cases
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Foreach array — owner loop variable is forbidden", "[gen][foreach][array][errors]") {
    REQUIRE(compile_should_fail(R"SRC(
        module __foreach_owner_forbidden__;
        struct Widget {
            v : int;
            Widget(x : int) { v = x; }
        }
        test() : int {
            arr : Widget[2]{ Widget(1), Widget(2) };
            for(w : Widget! = arr) {
            }
            return 0;
        }
    )SRC", nullptr));
}

TEST_CASE("Foreach array — drain loop variable is forbidden", "[gen][foreach][array][errors]") {
    REQUIRE(compile_should_fail(R"SRC(
        module __foreach_drain_forbidden__;
        struct Widget {
            v : int;
            Widget(x : int) { v = x; }
        }
        test() : int {
            arr : Widget[2]{ Widget(1), Widget(2) };
            for(w : Widget# = arr) {
            }
            return 0;
        }
    )SRC", nullptr));
}

TEST_CASE("Foreach array — non-iterable source is rejected", "[gen][foreach][array][errors]") {
    REQUIRE(compile_should_fail(R"SRC(
        module __foreach_not_iterable__;
        test() : int {
            v : int = 42;
            for(x : int = v) {
            }
            return 0;
        }
    )SRC", nullptr));
}


// ─────────────────────────────────────────────────────────────────────────────
// Iterator / sequence foreach tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Foreach sequence — sum Vector<int> via copy", "[gen][foreach][sequence]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            vec : Vector<int>;
            vec.append(1);
            vec.append(2);
            vec.append(3);
            sum : int = 0;
            for(x : int = vec) {
                sum = sum + x;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 6);
}

TEST_CASE("Foreach iterator — direct Iterator<T> object", "[gen][foreach][iterator]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            vec : Vector<int>;
            vec.append(10);
            vec.append(20);
            vec.append(30);
            it : Iterator<int>! = vec.iterator();
            sum : int = 0;
            for(x : int = it) {
                sum = sum + x;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 60);
}

TEST_CASE("Foreach sequence — mutable in-place via reference", "[gen][foreach][sequence]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            vec : Vector<int>;
            vec.append(1);
            vec.append(2);
            vec.append(3);
            for(x : int& = vec) {
                x = x * 10;
            }
            sum : int = 0;
            for(x : int = vec) {
                sum = sum + x;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 60);
}

TEST_CASE("Foreach sequence — break and continue", "[gen][foreach][sequence]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            vec : Vector<int>;
            vec.append(1);
            vec.append(2);
            vec.append(3);
            vec.append(4);
            vec.append(5);
            sum : int = 0;
            for(x : int = vec) {
                if (x == 2) { continue; }
                if (x == 5) { break; }
                sum = sum + x;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 1 + 3 + 4);
}

TEST_CASE("Foreach sequence — empty sequence never enters body", "[gen][foreach][sequence]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            vec : Vector<int>;
            sum : int = 0;
            for(x : int = vec) {
                sum = sum + 1;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 0);
}

namespace {
std::shared_ptr<k::path_lookup_file_resolver> stdlib_search_resolver() {
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    return resolver;
}
}

TEST_CASE("Foreach iterator — const source: mutable ref forbidden", "[gen][foreach][iterator][errors]") {
    REQUIRE(compile_should_fail(R"SRC(
        module test;

        test() : int {
            vec : Vector<int>;
            vec.append(1);
            cv : const Vector<int>& = vec;
            for(x : int& = cv) {
                x = 1;
            }
            return 0;
        }
    )SRC", stdlib_search_resolver()));
}

TEST_CASE("Foreach iterator — owner loop var forbidden on sequence", "[gen][foreach][sequence][errors]") {
    REQUIRE(compile_should_fail(R"SRC(
        module test;

        test() : int {
            vec : Vector<int>;
            vec.append(1);
            for(x : int! = vec) {
            }
            return 0;
        }
    )SRC", stdlib_search_resolver()));
}

TEST_CASE("Foreach iterator — const source sums via copy (constIterator)", "[gen][foreach][iterator]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            vec : Vector<int>;
            vec.append(1);
            vec.append(2);
            vec.append(3);
            cv : const Vector<int>& = vec;
            sum : int = 0;
            for(x : int = cv) {
                sum = sum + x;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 6);
}

TEST_CASE("Foreach sequence — iterator() called exactly once, destroyed once (full loop)", "[gen][foreach][sequence]") {
    auto jit = gen_jit(R"SRC(
        module test;

        class CountingIter : public ConstIterator<int> {
            static dtor_count : int = 0;
            idx : int = 0;
            limit : int;
            CountingIter(l : int) { limit = l; }
            ~CountingIter() { CountingIter::dtor_count = CountingIter::dtor_count + 1; }
            next() : OptionalConstRef<int> {
                if (idx < limit) {
                    idx = idx + 1;
                    return OptionalConstRef<int>(idx);
                }
                return OptionalConstRef<int>();
            }
        }

        class CountingSeq : public Sequence<int> {
            static call_count : int = 0;
            const constIterator() : ConstIterator<int>! {
                CountingSeq::call_count = CountingSeq::call_count + 1;
                return new CountingIter(5);
            }
        }

        run_loop_full() : int {
            seq : CountingSeq;
            sum : int = 0;
            for(x : int = seq) {
                sum = sum + x;
            }
            return sum;
        }

        get_call_count() : int {
            return CountingSeq::call_count;
        }

        get_dtor_count() : int {
            return CountingIter::dtor_count;
        }
    )SRC");
    REQUIRE(jit);
    auto run_loop_full = jit->lookup_symbol<int(*)()>("run_loop_full");
    auto get_call_count = jit->lookup_symbol<int(*)()>("get_call_count");
    auto get_dtor_count = jit->lookup_symbol<int(*)()>("get_dtor_count");
    REQUIRE(run_loop_full != nullptr);
    REQUIRE(get_call_count != nullptr);
    REQUIRE(get_dtor_count != nullptr);
    REQUIRE(get_call_count() == 0);
    REQUIRE(get_dtor_count() == 0);
    REQUIRE(run_loop_full() == 1 + 2 + 3 + 4 + 5);
    // constIterator() must be called exactly once (not once per iteration),
    // and the hidden owned iterator must be destroyed exactly once at loop end.
    REQUIRE(get_call_count() == 1);
    REQUIRE(get_dtor_count() == 1);
}

TEST_CASE("Foreach sequence — hidden iterator destroyed once on break", "[gen][foreach][sequence]") {
    auto jit = gen_jit(R"SRC(
        module test;

        class CountingIter : public ConstIterator<int> {
            static dtor_count : int = 0;
            idx : int = 0;
            limit : int;
            CountingIter(l : int) { limit = l; }
            ~CountingIter() { CountingIter::dtor_count = CountingIter::dtor_count + 1; }
            next() : OptionalConstRef<int> {
                if (idx < limit) {
                    idx = idx + 1;
                    return OptionalConstRef<int>(idx);
                }
                return OptionalConstRef<int>();
            }
        }

        class CountingSeq : public Sequence<int> {
            static call_count : int = 0;
            const constIterator() : ConstIterator<int>! {
                CountingSeq::call_count = CountingSeq::call_count + 1;
                return new CountingIter(5);
            }
        }

        run_loop_break() : int {
            seq : CountingSeq;
            sum : int = 0;
            for(x : int = seq) {
                if (x == 3) { break; }
                sum = sum + x;
            }
            return sum;
        }

        get_call_count() : int {
            return CountingSeq::call_count;
        }

        get_dtor_count() : int {
            return CountingIter::dtor_count;
        }
    )SRC");
    REQUIRE(jit);
    auto run_loop_break = jit->lookup_symbol<int(*)()>("run_loop_break");
    auto get_call_count = jit->lookup_symbol<int(*)()>("get_call_count");
    auto get_dtor_count = jit->lookup_symbol<int(*)()>("get_dtor_count");
    REQUIRE(run_loop_break != nullptr);
    REQUIRE(get_call_count != nullptr);
    REQUIRE(get_dtor_count != nullptr);
    REQUIRE(run_loop_break() == 1 + 2);
    REQUIRE(get_call_count() == 1);
    REQUIRE(get_dtor_count() == 1);
}
