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

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

//
// If-then-else
//

TEST_CASE("If-then-else", "[gen][if-else]") {
    auto jit = gen_jit(R"SRC(
        module __if__;
        min(a: int, b: int) : int {
            if(a<b)
                return a;
            else
                return b;
        }
        max(a: int, b: int) : int {
            if(a>b) {
                return a;
            } else {
                return b;
            }
        }
        fibo(i: unsigned short) : unsigned long {
            if(i==0) return 1;
            else if(i==1) return 1;
            return fibo(i-1) + fibo(i-2);
        }
        )SRC");
    REQUIRE(jit);

    SECTION("if-then-else simple return statement") {
        auto min = jit->lookup_symbol<int(*)(int,int)>("min");
        REQUIRE(min != nullptr);
        REQUIRE(min(4,2) == 2);
        REQUIRE(min(2,4) == 2);
    }

    SECTION("if-then-else with blocks") {
        auto max = jit->lookup_symbol<int(*)(int,int)>("max");
        REQUIRE(max != nullptr);
        REQUIRE(max(4,2) == 4);
        REQUIRE(max(2,4) == 4);
    }

    SECTION("if-then-else with nested if and no else") {
        auto fibo = jit->lookup_symbol<uint64_t(*)(unsigned short)>("fibo");
        REQUIRE(fibo != nullptr);
        REQUIRE(fibo(0) == 1);
        REQUIRE(fibo(1) == 1);
        REQUIRE(fibo(2) == 2);
        REQUIRE(fibo(3) == 3);
        REQUIRE(fibo(4) == 5);
        REQUIRE(fibo(5) == 8);
    }
}

//
// While
//

TEST_CASE("While", "[gen][while]") {
    auto jit = gen_jit(R"SRC(
        module __while__;
        cumul(i : int) : int {
            r : int;
            r = 0;
            while(i>0) {
                r += i;
                --i;
            }
            return r;
        }
        )SRC");
    REQUIRE(jit);

    SECTION("while simple statement") {
        auto cumul = jit->lookup_symbol<int(*)(int)>("cumul");
        REQUIRE(cumul != nullptr);
        REQUIRE(cumul(0) == 0);
        REQUIRE(cumul(1) == 1);
        REQUIRE(cumul(2) == 3);
        REQUIRE(cumul(3) == 6);
        REQUIRE(cumul(4) == 10);
        REQUIRE(cumul(5) == 15);
    }
}

//
// For
//

TEST_CASE("For", "[gen][for]") {
    auto jit = gen_jit(R"SRC(
        module __for__;
        sum(i : short) : int {
            r : int;
            r = 0;
            for (n: short = 0; n<i; ++n) {
                r += n;
            }
            return r;
        }
        )SRC");
    REQUIRE(jit);

    SECTION("for simple statement") {
        auto sum = jit->lookup_symbol<int(*)(short)>("sum");
        REQUIRE(sum != nullptr);
        REQUIRE(sum(0) == 0);
        REQUIRE(sum(1) == 0);
        REQUIRE(sum(2) == 1);
        REQUIRE(sum(3) == 3);
        REQUIRE(sum(4) == 6);
        REQUIRE(sum(5) == 10);
    }
}

//
// Break
//

TEST_CASE("Break in while loop", "[gen][break]") {
    auto jit = gen_jit(R"SRC(
        module __break_while__;
        find_first_ge(limit : int) : int {
            i : int = 0;
            while(i < 100) {
                if(i >= limit) {
                    break;
                }
                ++i;
            }
            return i;
        }
        sum_until(limit : int) : int {
            r : int = 0;
            i : int = 0;
            while(i < 100) {
                if(i >= limit) {
                    break;
                }
                r += i;
                ++i;
            }
            return r;
        }
        )SRC");
    REQUIRE(jit);

    SECTION("break exits while loop early") {
        auto find_first_ge = jit->lookup_symbol<int(*)(int)>("find_first_ge");
        REQUIRE(find_first_ge != nullptr);
        REQUIRE(find_first_ge(0) == 0);
        REQUIRE(find_first_ge(5) == 5);
        REQUIRE(find_first_ge(10) == 10);
        REQUIRE(find_first_ge(200) == 100);
    }

    SECTION("break in while loop with accumulator") {
        auto sum_until = jit->lookup_symbol<int(*)(int)>("sum_until");
        REQUIRE(sum_until != nullptr);
        REQUIRE(sum_until(0) == 0);
        REQUIRE(sum_until(1) == 0);
        REQUIRE(sum_until(3) == 3);
        REQUIRE(sum_until(5) == 10);
    }
}

TEST_CASE("Break in for loop", "[gen][break]") {
    auto jit = gen_jit(R"SRC(
        module __break_for__;
        sum_until_for(limit : int) : int {
            r : int = 0;
            for (i : int = 0; i < 100; ++i) {
                if(i >= limit) {
                    break;
                }
                r += i;
            }
            return r;
        }
        )SRC");
    REQUIRE(jit);

    SECTION("break exits for loop early") {
        auto sum_until_for = jit->lookup_symbol<int(*)(int)>("sum_until_for");
        REQUIRE(sum_until_for != nullptr);
        REQUIRE(sum_until_for(0) == 0);
        REQUIRE(sum_until_for(1) == 0);
        REQUIRE(sum_until_for(3) == 3);
        REQUIRE(sum_until_for(5) == 10);
    }
}

TEST_CASE("Break in nested loops", "[gen][break]") {
    auto jit = gen_jit(R"SRC(
        module __break_nested__;
        nested_break(n : int) : int {
            total : int = 0;
            i : int = 0;
            while(i < n) {
                j : int = 0;
                while(j < n) {
                    if(j >= 3) {
                        break;
                    }
                    ++total;
                    ++j;
                }
                ++i;
            }
            return total;
        }
        )SRC");
    REQUIRE(jit);

    SECTION("break only exits innermost loop") {
        auto nested_break = jit->lookup_symbol<int(*)(int)>("nested_break");
        REQUIRE(nested_break != nullptr);
        // n=1: outer runs 1 time, inner runs min(1,3)=1 -> total=1
        REQUIRE(nested_break(1) == 1);
        // n=2: outer runs 2 times, inner runs min(2,3)=2 each -> total=4
        REQUIRE(nested_break(2) == 4);
        // n=3: outer runs 3 times, inner runs min(3,3)=3 each -> total=9
        REQUIRE(nested_break(3) == 9);
        // n=5: outer runs 5 times, inner breaks at 3 each -> total=15
        REQUIRE(nested_break(5) == 15);
        // n=10: outer runs 10 times, inner breaks at 3 each -> total=30
        REQUIRE(nested_break(10) == 30);
    }
}

TEST_CASE("Break outside loop is an error", "[gen][break]") {
    SECTION("break in function body (not in loop)") {
        REQUIRE_THROWS(gen_jit_throws(R"SRC(
            module __break_error__;
            bad() : int {
                break;
                return 0;
            }
        )SRC"));
    }

    SECTION("break in if (not in loop)") {
        REQUIRE_THROWS(gen_jit_throws(R"SRC(
            module __break_error2__;
            bad(x : int) : int {
                if(x > 0) {
                    break;
                }
                return 0;
            }
        )SRC"));
    }
}

//
// Continue
//

TEST_CASE("Continue in while loop", "[gen][continue]") {
    auto jit = gen_jit(R"SRC(
        module __continue_while__;
        sum_odd(n : int) : int {
            r : int = 0;
            i : int = 0;
            while(i < n) {
                ++i;
                if(i % 2 == 0) {
                    continue;
                }
                r += i;
            }
            return r;
        }
        )SRC");
    REQUIRE(jit);

    SECTION("continue skips even numbers in while loop") {
        auto sum_odd = jit->lookup_symbol<int(*)(int)>("sum_odd");
        REQUIRE(sum_odd != nullptr);
        REQUIRE(sum_odd(0) == 0);
        REQUIRE(sum_odd(1) == 1);
        REQUIRE(sum_odd(2) == 1);
        REQUIRE(sum_odd(3) == 4);
        REQUIRE(sum_odd(4) == 4);
        REQUIRE(sum_odd(5) == 9);
        REQUIRE(sum_odd(10) == 25);
    }
}

TEST_CASE("Continue in for loop", "[gen][continue]") {
    auto jit = gen_jit(R"SRC(
        module __continue_for__;
        sum_skip_multiples(n : int, skip : int) : int {
            r : int = 0;
            for (i : int = 0; i < n; ++i) {
                if(i % skip == 0) {
                    continue;
                }
                r += i;
            }
            return r;
        }
        )SRC");
    REQUIRE(jit);

    SECTION("continue skips multiples in for loop") {
        auto sum_skip = jit->lookup_symbol<int(*)(int,int)>("sum_skip_multiples");
        REQUIRE(sum_skip != nullptr);
        // skip multiples of 3 in 0..5: skip 0,3 -> sum 1+2+4 = 7
        REQUIRE(sum_skip(5, 3) == 7);
        // skip multiples of 2 in 0..6: skip 0,2,4 -> sum 1+3+5 = 9
        REQUIRE(sum_skip(6, 2) == 9);
        // skip multiples of 1 (all): sum = 0
        REQUIRE(sum_skip(5, 1) == 0);
    }
}

TEST_CASE("Continue in for loop preserves step", "[gen][continue]") {
    auto jit = gen_jit(R"SRC(
        module __continue_for_step__;
        count_non_multiples(n : int, skip : int) : int {
            count : int = 0;
            for (i : int = 1; i <= n; ++i) {
                if(i % skip == 0) {
                    continue;
                }
                ++count;
            }
            return count;
        }
        )SRC");
    REQUIRE(jit);

    SECTION("continue executes step expression before next iteration") {
        auto count = jit->lookup_symbol<int(*)(int,int)>("count_non_multiples");
        REQUIRE(count != nullptr);
        // 1..10, skip multiples of 3 (3,6,9) -> 7 non-multiples
        REQUIRE(count(10, 3) == 7);
        // 1..10, skip multiples of 2 (2,4,6,8,10) -> 5 non-multiples
        REQUIRE(count(10, 2) == 5);
        // 1..1, skip multiples of 1 (1) -> 0
        REQUIRE(count(1, 1) == 0);
    }
}

TEST_CASE("Continue in nested loops", "[gen][continue]") {
    auto jit = gen_jit(R"SRC(
        module __continue_nested__;
        nested_continue(n : int) : int {
            total : int = 0;
            i : int = 0;
            while(i < n) {
                j : int = 0;
                while(j < n) {
                    ++j;
                    if(j % 2 == 0) {
                        continue;
                    }
                    ++total;
                }
                ++i;
            }
            return total;
        }
        )SRC");
    REQUIRE(jit);

    SECTION("continue only affects innermost loop") {
        auto nested = jit->lookup_symbol<int(*)(int)>("nested_continue");
        REQUIRE(nested != nullptr);
        // n=1: outer 1 iter, inner j=1(odd,+1) -> total=1
        REQUIRE(nested(1) == 1);
        // n=2: outer 2 iters, inner j=1(odd,+1),j=2(even,skip) -> 1 per outer -> total=2
        REQUIRE(nested(2) == 2);
        // n=4: outer 4 iters, inner j=1(+1),j=2(skip),j=3(+1),j=4(skip) -> 2 per outer -> total=8
        REQUIRE(nested(4) == 8);
    }
}

TEST_CASE("Continue outside loop is an error", "[gen][continue]") {
    SECTION("continue in function body (not in loop)") {
        REQUIRE_THROWS(gen_jit_throws(R"SRC(
            module __continue_error__;
            bad() : int {
                continue;
                return 0;
            }
        )SRC"));
    }

    SECTION("continue in if (not in loop)") {
        REQUIRE_THROWS(gen_jit_throws(R"SRC(
            module __continue_error2__;
            bad(x : int) : int {
                if(x > 0) {
                    continue;
                }
                return 0;
            }
        )SRC"));
    }
}
