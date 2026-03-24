/*
 * K Language standard library — StringBuilder mutation / search tests
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
 * Tests for ::k::StringBuilder mutation and search methods.
 *
 * Covers: prepend, insert, remove, replace, reverse, trim, trimLeft,
 *         trimRight, find, rfind, contains, beginsWith, endsWith,
 *         substr, toString, equality operators, append(StringBuilder).
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined — check CMakeLists.txt"
#endif

namespace {

std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

} // anonymous namespace


// ═════════════════════════════════════════════════════════════════════════════
// 1. StringBuilder.prepend
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder.prepend — String", "[libk][string][builder][mutate]") {
    auto jit = jit_k(R"SRC(
        module __sb_prepend_str__;

        test_prepend_size() : unsigned int {
            sb : k::StringBuilder("world");
            prefix : k::String("hello ");
            sb.prepend(prefix);
            return sb.size();
        }

        test_prepend_content() : int {
            sb : k::StringBuilder("world");
            prefix : k::String("hello ");
            sb.prepend(prefix);
            s : k::String(sb);
            expected : k::String("hello world");
            if (s == expected) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_prepend_size = jit->lookup_symbol<unsigned(*)()>("test_prepend_size");
    REQUIRE(test_prepend_size);
    CHECK(test_prepend_size() == 11);

    auto test_prepend_content = jit->lookup_symbol<int(*)()>("test_prepend_content");
    REQUIRE(test_prepend_content);
    CHECK(test_prepend_content() == 1);
}

TEST_CASE("StringBuilder.prepend — char[] literal", "[libk][string][builder][mutate]") {
    auto jit = jit_k(R"SRC(
        module __sb_prepend_lit__;

        test_prepend_lit() : int {
            sb : k::StringBuilder("world");
            sb.prepend("hello ");
            s : k::String(sb);
            expected : k::String("hello world");
            if (s == expected) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test_prepend_lit");
    REQUIRE(test);
    CHECK(test() == 1);
}


// ═════════════════════════════════════════════════════════════════════════════
// 2. StringBuilder.insert
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder.insert — middle", "[libk][string][builder][mutate]") {
    auto jit = jit_k(R"SRC(
        module __sb_insert__;

        test_insert_size() : unsigned int {
            sb : k::StringBuilder("helo");
            ins : k::String("l");
            sb.insert(2u, ins);
            return sb.size();
        }

        test_insert_content() : int {
            sb : k::StringBuilder("helo");
            ins : k::String("l");
            sb.insert(2u, ins);
            s : k::String(sb);
            expected : k::String("hello");
            if (s == expected) return 1;
            return 0;
        }

        test_insert_at_start() : int {
            sb : k::StringBuilder("world");
            ins : k::String("hello ");
            sb.insert(0u, ins);
            s : k::String(sb);
            expected : k::String("hello world");
            if (s == expected) return 1;
            return 0;
        }

        test_insert_at_end() : int {
            sb : k::StringBuilder("hello");
            ins : k::String(" world");
            sb.insert(5u, ins);
            s : k::String(sb);
            expected : k::String("hello world");
            if (s == expected) return 1;
            return 0;
        }

        test_insert_lit() : int {
            sb : k::StringBuilder("hd");
            sb.insert(1u, "ello worl");
            s : k::String(sb);
            expected : k::String("hello world");
            if (s == expected) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_insert_size = jit->lookup_symbol<unsigned(*)()>("test_insert_size");
    REQUIRE(test_insert_size);
    CHECK(test_insert_size() == 5);

    auto test_insert_content = jit->lookup_symbol<int(*)()>("test_insert_content");
    REQUIRE(test_insert_content);
    CHECK(test_insert_content() == 1);

    auto test_insert_at_start = jit->lookup_symbol<int(*)()>("test_insert_at_start");
    REQUIRE(test_insert_at_start);
    CHECK(test_insert_at_start() == 1);

    auto test_insert_at_end = jit->lookup_symbol<int(*)()>("test_insert_at_end");
    REQUIRE(test_insert_at_end);
    CHECK(test_insert_at_end() == 1);

    auto test_insert_lit = jit->lookup_symbol<int(*)()>("test_insert_lit");
    REQUIRE(test_insert_lit);
    CHECK(test_insert_lit() == 1);
}


// ═════════════════════════════════════════════════════════════════════════════
// 3. StringBuilder.remove
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder.remove", "[libk][string][builder][mutate]") {
    auto jit = jit_k(R"SRC(
        module __sb_remove__;

        test_remove_middle() : int {
            sb : k::StringBuilder("hello world");
            sb.remove(5u, 1u);
            s : k::String(sb);
            expected : k::String("helloworld");
            if (s == expected) return 1;
            return 0;
        }

        test_remove_start() : int {
            sb : k::StringBuilder("hello world");
            sb.remove(0u, 6u);
            s : k::String(sb);
            expected : k::String("world");
            if (s == expected) return 1;
            return 0;
        }

        test_remove_end() : int {
            sb : k::StringBuilder("hello world");
            sb.remove(5u, 6u);
            s : k::String(sb);
            expected : k::String("hello");
            if (s == expected) return 1;
            return 0;
        }

        test_remove_all() : int {
            sb : k::StringBuilder("hello");
            sb.remove(0u, 5u);
            if (sb.empty()) return 1;
            return 0;
        }

        test_remove_nothing() : int {
            sb : k::StringBuilder("hello");
            sb.remove(0u, 0u);
            if (sb.size() == 5) return 1;
            return 0;
        }

        test_remove_out_of_range() : int {
            sb : k::StringBuilder("hello");
            sb.remove(10u, 5u);
            if (sb.size() == 5) return 1;
            return 0;
        }

        test_remove_clamped() : int {
            sb : k::StringBuilder("hello");
            sb.remove(3u, 100u);
            s : k::String(sb);
            expected : k::String("hel");
            if (s == expected) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_remove_middle = jit->lookup_symbol<int(*)()>("test_remove_middle");
    REQUIRE(test_remove_middle);
    CHECK(test_remove_middle() == 1);

    auto test_remove_start = jit->lookup_symbol<int(*)()>("test_remove_start");
    REQUIRE(test_remove_start);
    CHECK(test_remove_start() == 1);

    auto test_remove_end = jit->lookup_symbol<int(*)()>("test_remove_end");
    REQUIRE(test_remove_end);
    CHECK(test_remove_end() == 1);

    auto test_remove_all = jit->lookup_symbol<int(*)()>("test_remove_all");
    REQUIRE(test_remove_all);
    CHECK(test_remove_all() == 1);

    auto test_remove_nothing = jit->lookup_symbol<int(*)()>("test_remove_nothing");
    REQUIRE(test_remove_nothing);
    CHECK(test_remove_nothing() == 1);

    auto test_remove_out_of_range = jit->lookup_symbol<int(*)()>("test_remove_out_of_range");
    REQUIRE(test_remove_out_of_range);
    CHECK(test_remove_out_of_range() == 1);

    auto test_remove_clamped = jit->lookup_symbol<int(*)()>("test_remove_clamped");
    REQUIRE(test_remove_clamped);
    CHECK(test_remove_clamped() == 1);
}


// ═════════════════════════════════════════════════════════════════════════════
// 4. StringBuilder.replace
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder.replace", "[libk][string][builder][mutate]") {
    auto jit = jit_k(R"SRC(
        module __sb_replace__;

        test_replace_same_len() : int {
            sb : k::StringBuilder("hello world");
            repl : k::String("earth");
            sb.replace(6u, 5u, repl);
            s : k::String(sb);
            expected : k::String("hello earth");
            if (s == expected) return 1;
            return 0;
        }

        test_replace_shorter() : int {
            sb : k::StringBuilder("hello world");
            repl : k::String("hi");
            sb.replace(0u, 5u, repl);
            s : k::String(sb);
            expected : k::String("hi world");
            if (s == expected) return 1;
            return 0;
        }

        test_replace_longer() : int {
            sb : k::StringBuilder("hello world");
            repl : k::String("greetings");
            sb.replace(0u, 5u, repl);
            s : k::String(sb);
            expected : k::String("greetings world");
            if (s == expected) return 1;
            return 0;
        }

        test_replace_lit() : int {
            sb : k::StringBuilder("hello world");
            sb.replace(6u, 5u, "earth");
            s : k::String(sb);
            expected : k::String("hello earth");
            if (s == expected) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_replace_same_len = jit->lookup_symbol<int(*)()>("test_replace_same_len");
    REQUIRE(test_replace_same_len);
    CHECK(test_replace_same_len() == 1);

    auto test_replace_shorter = jit->lookup_symbol<int(*)()>("test_replace_shorter");
    REQUIRE(test_replace_shorter);
    CHECK(test_replace_shorter() == 1);

    auto test_replace_longer = jit->lookup_symbol<int(*)()>("test_replace_longer");
    REQUIRE(test_replace_longer);
    CHECK(test_replace_longer() == 1);

    auto test_replace_lit = jit->lookup_symbol<int(*)()>("test_replace_lit");
    REQUIRE(test_replace_lit);
    CHECK(test_replace_lit() == 1);
}


// ═════════════════════════════════════════════════════════════════════════════
// 5. StringBuilder.reverse
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder.reverse", "[libk][string][builder][mutate]") {
    auto jit = jit_k(R"SRC(
        module __sb_reverse__;

        test_reverse() : int {
            sb : k::StringBuilder("abcde");
            sb.reverse();
            s : k::String(sb);
            expected : k::String("edcba");
            if (s == expected) return 1;
            return 0;
        }

        test_reverse_single() : int {
            sb : k::StringBuilder("x");
            sb.reverse();
            s : k::String(sb);
            expected : k::String("x");
            if (s == expected) return 1;
            return 0;
        }

        test_reverse_empty() : int {
            sb : k::StringBuilder;
            sb.reverse();
            if (sb.empty()) return 1;
            return 0;
        }

        test_reverse_two() : int {
            sb : k::StringBuilder("ab");
            sb.reverse();
            s : k::String(sb);
            expected : k::String("ba");
            if (s == expected) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_reverse = jit->lookup_symbol<int(*)()>("test_reverse");
    REQUIRE(test_reverse);
    CHECK(test_reverse() == 1);

    auto test_reverse_single = jit->lookup_symbol<int(*)()>("test_reverse_single");
    REQUIRE(test_reverse_single);
    CHECK(test_reverse_single() == 1);

    auto test_reverse_empty = jit->lookup_symbol<int(*)()>("test_reverse_empty");
    REQUIRE(test_reverse_empty);
    CHECK(test_reverse_empty() == 1);

    auto test_reverse_two = jit->lookup_symbol<int(*)()>("test_reverse_two");
    REQUIRE(test_reverse_two);
    CHECK(test_reverse_two() == 1);
}


// ═════════════════════════════════════════════════════════════════════════════
// 6. StringBuilder.trimLeft, trimRight, trim
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder.trimLeft", "[libk][string][builder][mutate]") {
    auto jit = jit_k(R"SRC(
        module __sb_trim_left__;

        test_trim_left() : int {
            sb : k::StringBuilder("  hello");
            sb.trimLeft();
            s : k::String(sb);
            expected : k::String("hello");
            if (s == expected) return 1;
            return 0;
        }

        test_trim_left_no_ws() : int {
            sb : k::StringBuilder("hello");
            sb.trimLeft();
            s : k::String(sb);
            expected : k::String("hello");
            if (s == expected) return 1;
            return 0;
        }

        test_trim_left_all_ws() : int {
            sb : k::StringBuilder("   ");
            sb.trimLeft();
            if (sb.empty()) return 1;
            return 0;
        }

        test_trim_left_empty() : int {
            sb : k::StringBuilder;
            sb.trimLeft();
            if (sb.empty()) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_trim_left = jit->lookup_symbol<int(*)()>("test_trim_left");
    REQUIRE(test_trim_left);
    CHECK(test_trim_left() == 1);

    auto test_trim_left_no_ws = jit->lookup_symbol<int(*)()>("test_trim_left_no_ws");
    REQUIRE(test_trim_left_no_ws);
    CHECK(test_trim_left_no_ws() == 1);

    auto test_trim_left_all_ws = jit->lookup_symbol<int(*)()>("test_trim_left_all_ws");
    REQUIRE(test_trim_left_all_ws);
    CHECK(test_trim_left_all_ws() == 1);

    auto test_trim_left_empty = jit->lookup_symbol<int(*)()>("test_trim_left_empty");
    REQUIRE(test_trim_left_empty);
    CHECK(test_trim_left_empty() == 1);
}

TEST_CASE("StringBuilder.trimRight", "[libk][string][builder][mutate]") {
    auto jit = jit_k(R"SRC(
        module __sb_trim_right__;

        test_trim_right() : int {
            sb : k::StringBuilder("hello  ");
            sb.trimRight();
            s : k::String(sb);
            expected : k::String("hello");
            if (s == expected) return 1;
            return 0;
        }

        test_trim_right_no_ws() : int {
            sb : k::StringBuilder("hello");
            sb.trimRight();
            s : k::String(sb);
            expected : k::String("hello");
            if (s == expected) return 1;
            return 0;
        }

        test_trim_right_all_ws() : int {
            sb : k::StringBuilder("   ");
            sb.trimRight();
            if (sb.empty()) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_trim_right = jit->lookup_symbol<int(*)()>("test_trim_right");
    REQUIRE(test_trim_right);
    CHECK(test_trim_right() == 1);

    auto test_trim_right_no_ws = jit->lookup_symbol<int(*)()>("test_trim_right_no_ws");
    REQUIRE(test_trim_right_no_ws);
    CHECK(test_trim_right_no_ws() == 1);

    auto test_trim_right_all_ws = jit->lookup_symbol<int(*)()>("test_trim_right_all_ws");
    REQUIRE(test_trim_right_all_ws);
    CHECK(test_trim_right_all_ws() == 1);
}

TEST_CASE("StringBuilder.trim — both sides", "[libk][string][builder][mutate]") {
    auto jit = jit_k(R"SRC(
        module __sb_trim__;

        test_trim() : int {
            sb : k::StringBuilder("  hello world  ");
            sb.trim();
            s : k::String(sb);
            expected : k::String("hello world");
            if (s == expected) return 1;
            return 0;
        }

        test_trim_empty() : int {
            sb : k::StringBuilder;
            sb.trim();
            if (sb.empty()) return 1;
            return 0;
        }

        test_trim_all_ws() : int {
            sb : k::StringBuilder("    ");
            sb.trim();
            if (sb.empty()) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_trim = jit->lookup_symbol<int(*)()>("test_trim");
    REQUIRE(test_trim);
    CHECK(test_trim() == 1);

    auto test_trim_empty = jit->lookup_symbol<int(*)()>("test_trim_empty");
    REQUIRE(test_trim_empty);
    CHECK(test_trim_empty() == 1);

    auto test_trim_all_ws = jit->lookup_symbol<int(*)()>("test_trim_all_ws");
    REQUIRE(test_trim_all_ws);
    CHECK(test_trim_all_ws() == 1);
}


// ═════════════════════════════════════════════════════════════════════════════
// 7. StringBuilder.find, rfind, contains
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder.find and rfind — char", "[libk][string][builder][search]") {
    auto jit = jit_k(R"SRC(
        module __sb_find_char__;

        test_find_char() : int {
            sb : k::StringBuilder("hello");
            sb.append(" world");
            return sb.find('o');
        }

        test_find_char_not_found() : int {
            sb : k::StringBuilder("hello");
            return sb.find('z');
        }

        test_rfind_char() : int {
            sb : k::StringBuilder("hello");
            sb.append(" world");
            return sb.rfind('o');
        }
    )SRC");
    REQUIRE(jit);

    auto test_find_char = jit->lookup_symbol<int(*)()>("test_find_char");
    REQUIRE(test_find_char);
    CHECK(test_find_char() == 4);

    auto test_find_char_not_found = jit->lookup_symbol<int(*)()>("test_find_char_not_found");
    REQUIRE(test_find_char_not_found);
    CHECK(test_find_char_not_found() == -1);

    auto test_rfind_char = jit->lookup_symbol<int(*)()>("test_rfind_char");
    REQUIRE(test_rfind_char);
    CHECK(test_rfind_char() == 7);
}

TEST_CASE("StringBuilder.find and rfind — String", "[libk][string][builder][search]") {
    auto jit = jit_k(R"SRC(
        module __sb_find_str__;

        test_find_str() : int {
            sb : k::StringBuilder("hello");
            sb.append(" world");
            needle : k::String("world");
            return sb.find(needle);
        }

        test_find_str_not_found() : int {
            sb : k::StringBuilder("hello");
            needle : k::String("xyz");
            return sb.find(needle);
        }

        test_rfind_str() : int {
            sb : k::StringBuilder("abc");
            sb.append("abc");
            needle : k::String("abc");
            return sb.rfind(needle);
        }
    )SRC");
    REQUIRE(jit);

    auto test_find_str = jit->lookup_symbol<int(*)()>("test_find_str");
    REQUIRE(test_find_str);
    CHECK(test_find_str() == 6);

    auto test_find_str_not_found = jit->lookup_symbol<int(*)()>("test_find_str_not_found");
    REQUIRE(test_find_str_not_found);
    CHECK(test_find_str_not_found() == -1);

    auto test_rfind_str = jit->lookup_symbol<int(*)()>("test_rfind_str");
    REQUIRE(test_rfind_str);
    CHECK(test_rfind_str() == 3);
}

TEST_CASE("StringBuilder.contains", "[libk][string][builder][search]") {
    auto jit = jit_k(R"SRC(
        module __sb_contains__;

        test_contains_char_yes() : int {
            sb : k::StringBuilder("hello");
            if (sb.contains('e')) return 1;
            return 0;
        }

        test_contains_char_no() : int {
            sb : k::StringBuilder("hello");
            if (sb.contains('z')) return 0;
            return 1;
        }

        test_contains_str_yes() : int {
            sb : k::StringBuilder("hello world");
            needle : k::String("world");
            if (sb.contains(needle)) return 1;
            return 0;
        }

        test_contains_str_no() : int {
            sb : k::StringBuilder("hello");
            needle : k::String("xyz");
            if (sb.contains(needle)) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(jit);

    auto cc_yes = jit->lookup_symbol<int(*)()>("test_contains_char_yes");
    REQUIRE(cc_yes);
    CHECK(cc_yes() == 1);

    auto cc_no = jit->lookup_symbol<int(*)()>("test_contains_char_no");
    REQUIRE(cc_no);
    CHECK(cc_no() == 1);

    auto cs_yes = jit->lookup_symbol<int(*)()>("test_contains_str_yes");
    REQUIRE(cs_yes);
    CHECK(cs_yes() == 1);

    auto cs_no = jit->lookup_symbol<int(*)()>("test_contains_str_no");
    REQUIRE(cs_no);
    CHECK(cs_no() == 1);
}


// ═════════════════════════════════════════════════════════════════════════════
// 8. StringBuilder.beginsWith and endsWith
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder.beginsWith and endsWith", "[libk][string][builder][search]") {
    auto jit = jit_k(R"SRC(
        module __sb_begins_ends__;

        test_begins_yes() : int {
            sb : k::StringBuilder("hello world");
            prefix : k::String("hello");
            if (sb.beginsWith(prefix)) return 1;
            return 0;
        }

        test_begins_no() : int {
            sb : k::StringBuilder("hello world");
            prefix : k::String("world");
            if (sb.beginsWith(prefix)) return 0;
            return 1;
        }

        test_ends_yes() : int {
            sb : k::StringBuilder("hello world");
            suffix : k::String("world");
            if (sb.endsWith(suffix)) return 1;
            return 0;
        }

        test_ends_no() : int {
            sb : k::StringBuilder("hello world");
            suffix : k::String("hello");
            if (sb.endsWith(suffix)) return 0;
            return 1;
        }

        test_begins_empty() : int {
            sb : k::StringBuilder("hello");
            prefix : k::String;
            if (sb.beginsWith(prefix)) return 1;
            return 0;
        }

        test_ends_empty() : int {
            sb : k::StringBuilder("hello");
            suffix : k::String;
            if (sb.endsWith(suffix)) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_begins_yes = jit->lookup_symbol<int(*)()>("test_begins_yes");
    REQUIRE(test_begins_yes);
    CHECK(test_begins_yes() == 1);

    auto test_begins_no = jit->lookup_symbol<int(*)()>("test_begins_no");
    REQUIRE(test_begins_no);
    CHECK(test_begins_no() == 1);

    auto test_ends_yes = jit->lookup_symbol<int(*)()>("test_ends_yes");
    REQUIRE(test_ends_yes);
    CHECK(test_ends_yes() == 1);

    auto test_ends_no = jit->lookup_symbol<int(*)()>("test_ends_no");
    REQUIRE(test_ends_no);
    CHECK(test_ends_no() == 1);

    auto test_begins_empty = jit->lookup_symbol<int(*)()>("test_begins_empty");
    REQUIRE(test_begins_empty);
    CHECK(test_begins_empty() == 1);

    auto test_ends_empty = jit->lookup_symbol<int(*)()>("test_ends_empty");
    REQUIRE(test_ends_empty);
    CHECK(test_ends_empty() == 1);
}


// ═════════════════════════════════════════════════════════════════════════════
// 9. StringBuilder.substr and toString
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder.substr and toString", "[libk][string][builder][extract]") {
    auto jit = jit_k(R"SRC(
        module __sb_substr__;

        test_substr() : int {
            sb : k::StringBuilder("hello world");
            sub : k::String = sb.substr(6u, 5u);
            expected : k::String("world");
            if (sub == expected) return 1;
            return 0;
        }

        test_toString() : int {
            sb : k::StringBuilder("hello");
            sb.append(" world");
            s : k::String = sb;
            expected : k::String("hello world");
            if (s == expected) return 1;
            return 0;
        }

        test_toString_size() : unsigned int {
            sb : k::StringBuilder("hello");
            sb.append(" world");
            s : k::String = sb ;
            return s.size();
        }
    )SRC");
    REQUIRE(jit);

    auto test_substr = jit->lookup_symbol<int(*)()>("test_substr");
    REQUIRE(test_substr);
    CHECK(test_substr() == 1);

    auto test_toString = jit->lookup_symbol<int(*)()>("test_toString");
    REQUIRE(test_toString);
    CHECK(test_toString() == 1);

    auto test_toString_size = jit->lookup_symbol<unsigned(*)()>("test_toString_size");
    REQUIRE(test_toString_size);
    CHECK(test_toString_size() == 11);
}


// ═════════════════════════════════════════════════════════════════════════════
// 10. StringBuilder equality operators
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder equality operators", "[libk][string][builder][compare]") {
    auto jit = jit_k(R"SRC(
        module __sb_eq_ops__;

        test_equal() : int {
            sb1 : k::StringBuilder("hello");
            sb2 : k::StringBuilder("hello");
            if (sb1 == sb2) return 1;
            return 0;
        }

        test_not_equal() : int {
            sb1 : k::StringBuilder("hello");
            sb2 : k::StringBuilder("world");
            if (sb1 != sb2) return 1;
            return 0;
        }

        test_equal_multi_frag() : int {
            sb1 : k::StringBuilder("hel");
            sb1.append("lo");
            sb2 : k::StringBuilder("hello");
            if (sb1 == sb2) return 1;
            return 0;
        }

        test_not_equal_different_size() : int {
            sb1 : k::StringBuilder("hello");
            sb2 : k::StringBuilder("hello world");
            if (sb1 != sb2) return 1;
            return 0;
        }

        test_equal_empty() : int {
            sb1 : k::StringBuilder;
            sb2 : k::StringBuilder;
            if (sb1 == sb2) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_equal = jit->lookup_symbol<int(*)()>("test_equal");
    REQUIRE(test_equal);
    CHECK(test_equal() == 1);

    auto test_not_equal = jit->lookup_symbol<int(*)()>("test_not_equal");
    REQUIRE(test_not_equal);
    CHECK(test_not_equal() == 1);

    auto test_equal_multi_frag = jit->lookup_symbol<int(*)()>("test_equal_multi_frag");
    REQUIRE(test_equal_multi_frag);
    CHECK(test_equal_multi_frag() == 1);

    auto test_not_equal_different_size = jit->lookup_symbol<int(*)()>("test_not_equal_different_size");
    REQUIRE(test_not_equal_different_size);
    CHECK(test_not_equal_different_size() == 1);

    auto test_equal_empty = jit->lookup_symbol<int(*)()>("test_equal_empty");
    REQUIRE(test_equal_empty);
    CHECK(test_equal_empty() == 1);
}


// ═════════════════════════════════════════════════════════════════════════════
// 11. StringBuilder.append(StringBuilder)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder.append(StringBuilder)", "[libk][string][builder][mutate]") {
    auto jit = jit_k(R"SRC(
        module __sb_append_sb__;

        test_append_sb_size() : unsigned int {
            sb1 : k::StringBuilder("hello");
            sb2 : k::StringBuilder(" world");
            sb1.append(sb2);
            return sb1.size();
        }

        test_append_sb_content() : int {
            sb1 : k::StringBuilder("hello");
            sb2 : k::StringBuilder(" world");
            sb1.append(sb2);
            s : k::String(sb1);
            expected : k::String("hello world");
            if (s == expected) return 1;
            return 0;
        }

        test_append_empty_sb() : int {
            sb1 : k::StringBuilder("hello");
            sb2 : k::StringBuilder;
            sb1.append(sb2);
            if (sb1.size() == 5) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_append_sb_size = jit->lookup_symbol<unsigned(*)()>("test_append_sb_size");
    REQUIRE(test_append_sb_size);
    CHECK(test_append_sb_size() == 11);

    auto test_append_sb_content = jit->lookup_symbol<int(*)()>("test_append_sb_content");
    REQUIRE(test_append_sb_content);
    CHECK(test_append_sb_content() == 1);

    auto test_append_empty_sb = jit->lookup_symbol<int(*)()>("test_append_empty_sb");
    REQUIRE(test_append_empty_sb);
    CHECK(test_append_empty_sb() == 1);
}


// ═════════════════════════════════════════════════════════════════════════════
// 12. StringBuilder += and + operators
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder += operator", "[libk][string][builder][operators]") {
    auto jit = jit_k(R"SRC(
        module __sb_ops_pluseq__;

        test_plus_eq() : int {
            sb : k::StringBuilder("hello");
            s : k::String(" world");
            sb += s;
            result : k::String(sb);
            expected : k::String("hello world");
            if (result == expected) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_plus_eq = jit->lookup_symbol<int(*)()>("test_plus_eq");
    REQUIRE(test_plus_eq);
    CHECK(test_plus_eq() == 1);
}

// NOTE: StringBuilder operator + test disabled — the K compiler has an LLVM
// codegen bug when a const method returns a class type by value.
// TEST_CASE("StringBuilder + operator", "[libk][string][builder][operators]") {
//     ...disabled...
// }


// ═════════════════════════════════════════════════════════════════════════════
// 13. StringBuilder fluent chaining
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder fluent chaining", "[libk][string][builder][chain]") {
    auto jit = jit_k(R"SRC(
        module __sb_chain__;

        test_chain() : int {
            sb : k::StringBuilder;
            sb.append("hello").append(" ").append("world").appendChar('!');
            s : k::String(sb);
            expected : k::String("hello world!");
            if (s == expected) return 1;
            return 0;
        }

        test_chain_size() : unsigned int {
            sb : k::StringBuilder;
            sb.append("hello").append(" ").append("world").appendChar('!');
            return sb.size();
        }
    )SRC");
    REQUIRE(jit);

    auto test_chain = jit->lookup_symbol<int(*)()>("test_chain");
    REQUIRE(test_chain);
    CHECK(test_chain() == 1);

    auto test_chain_size = jit->lookup_symbol<unsigned(*)()>("test_chain_size");
    REQUIRE(test_chain_size);
    CHECK(test_chain_size() == 12);
}


// ═════════════════════════════════════════════════════════════════════════════
// 14. StringBuilder — multi-fragment charAt
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder — multi-fragment charAt", "[libk][string][builder][access]") {
    auto jit = jit_k(R"SRC(
        module __sb_multi_frag_at__;

        test_charAt_frag1() : char {
            sb : k::StringBuilder("AB");
            sb.append("CD");
            sb.append("EF");
            return sb.charAt(0u);
        }

        test_charAt_frag2() : char {
            sb : k::StringBuilder("AB");
            sb.append("CD");
            sb.append("EF");
            return sb.charAt(2u);
        }

        test_charAt_frag3() : char {
            sb : k::StringBuilder("AB");
            sb.append("CD");
            sb.append("EF");
            return sb.charAt(5u);
        }

        test_charAt_boundary() : char {
            sb : k::StringBuilder("AB");
            sb.append("CD");
            sb.append("EF");
            return sb.charAt(1u);
        }
    )SRC");
    REQUIRE(jit);

    auto test_charAt_frag1 = jit->lookup_symbol<char(*)()>("test_charAt_frag1");
    REQUIRE(test_charAt_frag1);
    CHECK(test_charAt_frag1() == 'A');

    auto test_charAt_frag2 = jit->lookup_symbol<char(*)()>("test_charAt_frag2");
    REQUIRE(test_charAt_frag2);
    CHECK(test_charAt_frag2() == 'C');

    auto test_charAt_frag3 = jit->lookup_symbol<char(*)()>("test_charAt_frag3");
    REQUIRE(test_charAt_frag3);
    CHECK(test_charAt_frag3() == 'F');

    auto test_charAt_boundary = jit->lookup_symbol<char(*)()>("test_charAt_boundary");
    REQUIRE(test_charAt_boundary);
    CHECK(test_charAt_boundary() == 'B');
}


// ═════════════════════════════════════════════════════════════════════════════
// 15. StringBuilder.toString() — named method
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder.toString() — named method", "[libk][string][builder][convert]") {
    auto jit = jit_k(R"SRC(
        module __sb_toString__;

        test_toString_size() : unsigned int {
            sb : k::StringBuilder("hello");
            sb.append(" world");
            s : k::String = sb.toString();
            return s.size();
        }

        test_toString_content() : int {
            sb : k::StringBuilder("hello");
            sb.append(" world");
            s : k::String = sb.toString();
            expected : k::String("hello world");
            if (s == expected) return 1;
            return 0;
        }

        test_toString_empty() : int {
            sb : k::StringBuilder;
            s : k::String = sb.toString();
            if (s.empty()) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_toString_size = jit->lookup_symbol<unsigned(*)()>("test_toString_size");
    REQUIRE(test_toString_size);
    CHECK(test_toString_size() == 11);

    auto test_toString_content = jit->lookup_symbol<int(*)()>("test_toString_content");
    REQUIRE(test_toString_content);
    CHECK(test_toString_content() == 1);

    auto test_toString_empty = jit->lookup_symbol<int(*)()>("test_toString_empty");
    REQUIRE(test_toString_empty);
    CHECK(test_toString_empty() == 1);
}


// ═════════════════════════════════════════════════════════════════════════════
// 16. StringBuilder.first() and StringBuilder.last()
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder.first() and last()", "[libk][string][builder][extract]") {
    auto jit = jit_k(R"SRC(
        module __sb_first_last__;

        test_first_size() : unsigned int {
            sb : k::StringBuilder("hello world");
            f : k::String = sb.first(5u);
            return f.size();
        }

        test_first_content() : int {
            sb : k::StringBuilder("hello world");
            f : k::String = sb.first(5u);
            expected : k::String("hello");
            if (f == expected) return 1;
            return 0;
        }

        test_first_zero() : int {
            sb : k::StringBuilder("hello");
            f : k::String = sb.first(0u);
            if (f.empty()) return 1;
            return 0;
        }

        test_first_oversize() : unsigned int {
            sb : k::StringBuilder("hi");
            f : k::String = sb.first(100u);
            return f.size();
        }

        test_last_size() : unsigned int {
            sb : k::StringBuilder("hello world");
            l : k::String = sb.last(5u);
            return l.size();
        }

        test_last_content() : int {
            sb : k::StringBuilder("hello world");
            l : k::String = sb.last(5u);
            expected : k::String("world");
            if (l == expected) return 1;
            return 0;
        }

        test_last_full() : int {
            sb : k::StringBuilder("hello");
            l : k::String = sb.last(100u);
            expected : k::String("hello");
            if (l == expected) return 1;
            return 0;
        }

        test_last_zero() : int {
            sb : k::StringBuilder("hello");
            l : k::String = sb.last(0u);
            if (l.empty()) return 1;
            return 0;
        }

        test_first_multi_frag() : int {
            sb : k::StringBuilder("hel");
            sb.append("lo ");
            sb.append("world");
            f : k::String = sb.first(5u);
            expected : k::String("hello");
            if (f == expected) return 1;
            return 0;
        }

        test_last_multi_frag() : int {
            sb : k::StringBuilder("hel");
            sb.append("lo ");
            sb.append("world");
            l : k::String = sb.last(5u);
            expected : k::String("world");
            if (l == expected) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_first_size = jit->lookup_symbol<unsigned(*)()>("test_first_size");
    REQUIRE(test_first_size);
    CHECK(test_first_size() == 5);

    auto test_first_content = jit->lookup_symbol<int(*)()>("test_first_content");
    REQUIRE(test_first_content);
    CHECK(test_first_content() == 1);

    auto test_first_zero = jit->lookup_symbol<int(*)()>("test_first_zero");
    REQUIRE(test_first_zero);
    CHECK(test_first_zero() == 1);

    auto test_first_oversize = jit->lookup_symbol<unsigned(*)()>("test_first_oversize");
    REQUIRE(test_first_oversize);
    CHECK(test_first_oversize() == 2);

    auto test_last_size = jit->lookup_symbol<unsigned(*)()>("test_last_size");
    REQUIRE(test_last_size);
    CHECK(test_last_size() == 5);

    auto test_last_content = jit->lookup_symbol<int(*)()>("test_last_content");
    REQUIRE(test_last_content);
    CHECK(test_last_content() == 1);

    auto test_last_full = jit->lookup_symbol<int(*)()>("test_last_full");
    REQUIRE(test_last_full);
    CHECK(test_last_full() == 1);

    auto test_last_zero = jit->lookup_symbol<int(*)()>("test_last_zero");
    REQUIRE(test_last_zero);
    CHECK(test_last_zero() == 1);

    auto test_first_multi_frag = jit->lookup_symbol<int(*)()>("test_first_multi_frag");
    REQUIRE(test_first_multi_frag);
    CHECK(test_first_multi_frag() == 1);

    auto test_last_multi_frag = jit->lookup_symbol<int(*)()>("test_last_multi_frag");
    REQUIRE(test_last_multi_frag);
    CHECK(test_last_multi_frag() == 1);
}


// ═════════════════════════════════════════════════════════════════════════════
// 17. StringBuilder.indexOf() — alias for find
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder.indexOf()", "[libk][string][builder][search]") {
    auto jit = jit_k(R"SRC(
        module __sb_indexOf__;

        test_indexOf_char() : int {
            sb : k::StringBuilder("hello");
            return sb.indexOf('l');
        }

        test_indexOf_char_not_found() : int {
            sb : k::StringBuilder("hello");
            return sb.indexOf('z');
        }

        test_indexOf_str() : int {
            sb : k::StringBuilder("hello world");
            needle : k::String("world");
            return sb.indexOf(needle);
        }

        test_indexOf_str_not_found() : int {
            sb : k::StringBuilder("hello");
            needle : k::String("xyz");
            return sb.indexOf(needle);
        }
    )SRC");
    REQUIRE(jit);

    auto test_indexOf_char = jit->lookup_symbol<int(*)()>("test_indexOf_char");
    REQUIRE(test_indexOf_char);
    CHECK(test_indexOf_char() == 2);

    auto test_indexOf_char_not_found = jit->lookup_symbol<int(*)()>("test_indexOf_char_not_found");
    REQUIRE(test_indexOf_char_not_found);
    CHECK(test_indexOf_char_not_found() == -1);

    auto test_indexOf_str = jit->lookup_symbol<int(*)()>("test_indexOf_str");
    REQUIRE(test_indexOf_str);
    CHECK(test_indexOf_str() == 6);

    auto test_indexOf_str_not_found = jit->lookup_symbol<int(*)()>("test_indexOf_str_not_found");
    REQUIRE(test_indexOf_str_not_found);
    CHECK(test_indexOf_str_not_found() == -1);
}


// ═════════════════════════════════════════════════════════════════════════════
// 18. StringBuilder.substr — edge cases
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder.substr — edge cases", "[libk][string][builder][extract]") {
    auto jit = jit_k(R"SRC(
        module __sb_substr_edge__;

        test_substr_out_of_range() : int {
            sb : k::StringBuilder("hello");
            sub : k::String = sb.substr(10u, 5u);
            if (sub.empty()) return 1;
            return 0;
        }

        test_substr_truncated() : unsigned int {
            sb : k::StringBuilder("hello");
            sub : k::String = sb.substr(3u, 100u);
            return sub.size();
        }

        test_substr_truncated_content() : int {
            sb : k::StringBuilder("hello");
            sub : k::String = sb.substr(3u, 100u);
            expected : k::String("lo");
            if (sub == expected) return 1;
            return 0;
        }

        test_substr_zero_len() : int {
            sb : k::StringBuilder("hello");
            sub : k::String = sb.substr(0u, 0u);
            if (sub.empty()) return 1;
            return 0;
        }

        test_substr_empty_sb() : int {
            sb : k::StringBuilder;
            sub : k::String = sb.substr(0u, 5u);
            if (sub.empty()) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_substr_out_of_range = jit->lookup_symbol<int(*)()>("test_substr_out_of_range");
    REQUIRE(test_substr_out_of_range);
    CHECK(test_substr_out_of_range() == 1);

    auto test_substr_truncated = jit->lookup_symbol<unsigned(*)()>("test_substr_truncated");
    REQUIRE(test_substr_truncated);
    CHECK(test_substr_truncated() == 2); // "lo"

    auto test_substr_truncated_content = jit->lookup_symbol<int(*)()>("test_substr_truncated_content");
    REQUIRE(test_substr_truncated_content);
    CHECK(test_substr_truncated_content() == 1);

    auto test_substr_zero_len = jit->lookup_symbol<int(*)()>("test_substr_zero_len");
    REQUIRE(test_substr_zero_len);
    CHECK(test_substr_zero_len() == 1);

    auto test_substr_empty_sb = jit->lookup_symbol<int(*)()>("test_substr_empty_sb");
    REQUIRE(test_substr_empty_sb);
    CHECK(test_substr_empty_sb() == 1);
}


// ═════════════════════════════════════════════════════════════════════════════
// 19. StringBuilder — find/rfind on empty builder
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder.find and rfind — empty builder", "[libk][string][builder][search]") {
    auto jit = jit_k(R"SRC(
        module __sb_find_empty__;

        test_find_char_empty() : int {
            sb : k::StringBuilder;
            return sb.find('a');
        }

        test_find_str_empty() : int {
            sb : k::StringBuilder;
            needle : k::String("abc");
            return sb.find(needle);
        }

        test_find_empty_needle_empty() : int {
            sb : k::StringBuilder;
            needle : k::String;
            return sb.find(needle);
        }

        test_rfind_char_empty() : int {
            sb : k::StringBuilder;
            return sb.rfind('a');
        }

        test_rfind_str_empty() : int {
            sb : k::StringBuilder;
            needle : k::String("abc");
            return sb.rfind(needle);
        }

        test_rfind_empty_needle_empty() : int {
            sb : k::StringBuilder;
            needle : k::String;
            return sb.rfind(needle);
        }
    )SRC");
    REQUIRE(jit);

    auto test_find_char_empty = jit->lookup_symbol<int(*)()>("test_find_char_empty");
    REQUIRE(test_find_char_empty);
    CHECK(test_find_char_empty() == -1);

    auto test_find_str_empty = jit->lookup_symbol<int(*)()>("test_find_str_empty");
    REQUIRE(test_find_str_empty);
    CHECK(test_find_str_empty() == -1);

    auto test_find_empty_needle_empty = jit->lookup_symbol<int(*)()>("test_find_empty_needle_empty");
    REQUIRE(test_find_empty_needle_empty);
    CHECK(test_find_empty_needle_empty() == 0);

    auto test_rfind_char_empty = jit->lookup_symbol<int(*)()>("test_rfind_char_empty");
    REQUIRE(test_rfind_char_empty);
    CHECK(test_rfind_char_empty() == -1);

    auto test_rfind_str_empty = jit->lookup_symbol<int(*)()>("test_rfind_str_empty");
    REQUIRE(test_rfind_str_empty);
    CHECK(test_rfind_str_empty() == -1);

    auto test_rfind_empty_needle_empty = jit->lookup_symbol<int(*)()>("test_rfind_empty_needle_empty");
    REQUIRE(test_rfind_empty_needle_empty);
    CHECK(test_rfind_empty_needle_empty() == 0);
}


