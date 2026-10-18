/*
 * BigInteger public K API regressions. Copyright 2026 Emilien Kia.
 * Licensed under the Apache License, Version 2.0.
 */
#include <catch2/catch_all.hpp>
#include "helpers.hpp"

#include <array>
#include <string>
#include <string_view>

namespace {
void check_big_integer(std::string_view source) {
    auto jit = gen_jit_with_stdlib(source, LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}
}

TEST_CASE("BigInteger public arithmetic is exact and uses truncating signed division",
          "[libk][big-integer][arithmetic]") {
    check_big_integer(R"SRC(
        module __big_integer_arithmetic__;
        test() : int {
            maximum : ::k::BigInteger(18446744073709551615uL);
            square : BigInteger = maximum * maximum;
            text : String = square.toString();
            if (text != String("340282366920938463426481119284349108225")) return 1;
            if (square / maximum != maximum) return 2;
            if (square % maximum != 0) return 3;
            minimum : BigInteger(-9223372036854775807L - 1L);
            mixed : BigInteger = minimum + 18446744073709551615uL;
            if ((long) mixed != 9223372036854775807L) return 4;
            mixed = maximum + (-9223372036854775807L - 1L);
            if ((long) mixed != 9223372036854775807L) return 5;
            negative : BigInteger(-17);
            positive : BigInteger(17);
            if (negative / 5 != -3 || negative % 5 != -2) return 6;
            if (negative / -5 != 3 || negative % -5 != -2) return 7;
            if (positive / -5 != -3 || positive % -5 != 2) return 8;
            if (positive / 5 != 3 || positive % 5 != 2) return 9;
            if (negative.abs() != positive || -negative != positive) return 10;
            unaryCopy : BigInteger = +negative;
            unaryCopy += 1;
            if (negative != -17 || unaryCopy != -16) return 11;
            zero : BigInteger;
            zeroText : String = zero.toString();
            if (zeroText != String("0")) return 12;
            negativeText : String = negative.toString();
            if (negativeText != String("-17")) return 13;
            return 0;
        }
    )SRC");
}

TEST_CASE("BigInteger deep copies drains and self assignments preserve ownership",
          "[libk][big-integer][lifetime]") {
    check_big_integer(R"SRC(
        module __big_integer_lifetime__;
        identity(value: BigInteger) : BigInteger { return value; }
        test() : int {
            original : BigInteger(18446744073709551615uL);
            original *= original;
            saved : BigInteger(original);
            copied : BigInteger(original);
            copied += 1;
            if (original != saved || copied == original) return 1;
            assigned : BigInteger;
            assigned = original;
            assigned -= 1;
            if (assigned == original || original != saved) return 2;
            original = original;
            if (original != saved) return 3;
            original = #original;
            if (original != saved) return 4;
            moved : BigInteger(#copied);
            if (!copied.isZero() || moved != saved + 1) return 5;
            copied = -42;
            if (moved != saved + 1 || copied != -42) return 6;
            assigned = #moved;
            if (!moved.isZero() || assigned != saved + 1) return 7;
            moved += 3;
            if (moved != 3 || assigned != saved + 1) return 8;
            returned : BigInteger = identity(original);
            returned *= 2;
            if (original != saved || returned != saved * 2) return 9;
            drainedZero : BigInteger(#copied);
            if (!copied.isZero() || drainedZero != -42) return 10;
            return 0;
        }
    )SRC");
}

TEST_CASE("BigInteger bool spaceship synthesis and increment operators agree with its value",
          "[libk][big-integer][operators]") {
    check_big_integer(R"SRC(
        module __big_integer_operators__;
        test() : int {
            zero : BigInteger;
            negative : BigInteger(-4);
            positive : BigInteger(4);
            if ((bool) zero || !(!zero) || !zero.isZero()) return 1;
            if (!(bool) negative || !negative || negative.isZero()) return 2;
            if (!(bool) positive || !positive) return 3;
            if (negative.signum() != -1 || zero.signum() != 0 || positive.signum() != 1) return 4;
            if ((negative <=> positive) != -1 || (positive <=> negative) != 1) return 5;
            if ((positive <=> positive) != 0) return 6;
            if (!(negative < positive) || !(negative <= positive)) return 7;
            if (!(positive > negative) || !(positive >= negative)) return 8;
            if (!(positive == positive) || !(negative != positive)) return 9;
            if (!(positive <= positive) || !(positive >= positive)) return 10;
            if (!(negative < 0) || !(negative <= -4) || !(positive > 0)) return 11;
            if (!(positive >= 4) || !(positive == 4) || !(positive != -4)) return 12;
            previous : BigInteger = positive++;
            if (previous != 4 || positive != 5) return 13;
            previous = positive--;
            if (previous != 5 || positive != 4) return 14;
            if (++positive != 5 || --positive != 4) return 15;
            negative += 4;
            if ((bool) negative || !negative.isZero()) return 16;
            huge : BigInteger(18446744073709551615uL);
            huge *= huge;
            if (!(bool) huge || !huge) return 17;
            return 0;
        }
    )SRC");
}

TEST_CASE("BigInteger accepts all primitive constructors assignments and right operands",
          "[libk][big-integer][primitives]") {
    const std::array<std::string_view, 8> types{
        "byte", "short", "int", "long",
        "unsigned byte", "unsigned short", "unsigned int", "unsigned long"
    };
    for (size_t i = 0; i < types.size(); ++i) {
        const std::string type(types[i]);
        DYNAMIC_SECTION(type) {
            const std::string value = i < 4 ? "-7" : "7";
            std::string source = "module __big_integer_primitives__;\ntest() : int {\n";
            source += "primitive : " + type + " = (" + type + ") (" + value + ");\n";
            source += R"SRC(
                number : BigInteger(primitive);
                if (number != primitive || (number <=> primitive) != 0) return 1;
                if (number + primitive != number * 2) return 2;
                if (number - primitive != 0 || number * primitive != 49) return 3;
                if (number / primitive != 1 || number % primitive != 0) return 4;
                number += primitive;
                number -= primitive;
                number *= primitive;
                number /= primitive;
                if (number != primitive) return 5;
                number %= primitive;
                if (!number.isZero()) return 6;
                number = primitive;
            )SRC";
            source += "if (!(number <= primitive) || !(number >= primitive)) return 8;\n";
            source += "return 0; }\n";
            check_big_integer(source);
        }
    }
}

TEST_CASE("BigInteger checked casts accept boundaries and throw typed arithmetic errors",
          "[libk][big-integer][casts][exceptions]") {
    struct Bounds {
        std::string_view type;
        std::string_view minimum;
        std::string_view maximum;
    };
    const std::array<Bounds, 8> bounds{{
        {"byte", "-128", "127"},
        {"short", "-32768", "32767"},
        {"int", "-2147483647 - 1", "2147483647"},
        {"long", "-9223372036854775807L - 1L", "9223372036854775807L"},
        {"unsigned byte", "0", "255"},
        {"unsigned short", "0", "65535"},
        {"unsigned int", "0", "4294967295u"},
        {"unsigned long", "0", "18446744073709551615uL"}
    }};
    for (size_t i = 0; i < bounds.size(); ++i) {
        const auto& bound = bounds[i];
        const std::string type(bound.type);
        DYNAMIC_SECTION(type) {
            std::string source = "module __big_integer_casts__;\ntest() : int {\n";
            source += "low : " + type + " = (" + type + ") (" + std::string(bound.minimum) + ");\n";
            source += "high : " + type + " = (" + type + ") (" + std::string(bound.maximum) + ");\n";
            source += "minimum : BigInteger(low); maximum : BigInteger(high);\n";
            source += "if ((" + type + ") minimum != low || (" + type + ") maximum != high) return 1;\n";
            source += "maximum += 1; caught : bool = false;\n";
            source += "try { bad : " + type + " = (" + type + ") maximum; }\n";
            source += "catch (error: NumericOverflowError&) { caught = true; }\n";
            source += "if (!caught) return 2;\n";
            source += "minimum -= 1; caught = false;\n";
            source += "try { bad : " + type + " = (" + type + ") minimum; }\n";
            source += i < 4
                ? "catch (error: NumericOverflowError&) { caught = true; }\n"
                : "catch (error: InvalidSignedToUnsignedError&) { caught = true; }\n";
            source += "if (!caught) return 3;\n";
            source += "positive : BigInteger(73); if ((" + type + ") positive != (" + type + ") 73) return 4;\n";
            source += "small : " + type + " = (" + type + ") (" + (i < 4 ? "-7" : "7") + ");\n";
            source += "assigned : BigInteger; assigned = small;\n";
            source += "if ((" + type + ") assigned != small) return 5;\n";
            source += "return 0; }\n";
            check_big_integer(source);
        }
    }
    SECTION("negative unsigned byte cast is catchable as NumericOverflowError") {
        check_big_integer(R"SRC(
            module __big_integer_casts__;
            test() : int {
                negative : BigInteger(-1);
                caught : bool = false;
                try { bad : unsigned byte = (unsigned byte) negative; }
                catch (error: NumericOverflowError&) { caught = true; }
                if (!caught) return 9;
                return 0;
            }
        )SRC");
    }
    SECTION("huge signed cast is catchable as ArithmeticError") {
        check_big_integer(R"SRC(
            module __big_integer_casts__;
            test() : int {
                huge : BigInteger(18446744073709551615uL);
                huge *= huge;
                caught : bool = false;
                try { bad : long = (long) huge; }
                catch (error: ArithmeticError&) { caught = true; }
                if (!caught) return 10;
                return 0;
            }
        )SRC");
    }
}

TEST_CASE("BigInteger division errors are catchable and compound receivers are unchanged",
          "[libk][big-integer][division][exceptions]") {
    check_big_integer(R"SRC(
        module __big_integer_division_errors__;
        test() : int {
            number : BigInteger(18446744073709551615uL);
            number *= number;
            saved : BigInteger(number);
            zero : BigInteger;
            caught : bool = false;
            try { bad : BigInteger = number / zero; }
            catch (error: DivisionByZeroError&) { caught = true; }
            if (!caught || number != saved) return 1;
            caught = false;
            try { bad : BigInteger = number % 0; }
            catch (error: ArithmeticError&) { caught = true; }
            if (!caught || number != saved) return 2;
            caught = false;
            try { number /= zero; }
            catch (error: DivisionByZeroError&) { caught = true; }
            if (!caught || number != saved) return 3;
            caught = false;
            try { number %= 0uL; }
            catch (error: DivisionByZeroError&) { caught = true; }
            if (!caught || number != saved) return 4;
            caught = false;
            try { zero /= zero; }
            catch (error: FatalError&) { caught = true; }
            if (!caught || !zero.isZero()) return 5;
            return 0;
        }
    )SRC");
}

TEST_CASE("BigInteger parsing factories accept decimal prefixes and large independent values",
          "[libk][big-integer][parsing]") {
    check_big_integer(R"SRC(
        module __big_integer_parse_defaults__;
        test() : int {
            decimal : ::k::BigInteger = ::k::BigInteger::fromString("010");
            if (decimal != 10 || BigInteger::fromString("09") != 9) return 1;
            if (BigInteger::fromString("+00042") != 42) return 2;
            if (BigInteger::fromString("-000").signum() != 0) return 3;
            if (BigInteger::fromString("0b101") != 5 || BigInteger::fromString("-0B101") != -5) return 4;
            if (BigInteger::fromString("0o17") != 15 || BigInteger::fromString("+0O17") != 15) return 5;
            if (BigInteger::fromString("0xaF") != 175 || BigInteger::fromString("-0XfF") != -255) return 6;
            utf8 : String(u8"+010");
            utf16 : String(u"-0o17");
            if (BigInteger::fromString(utf8) != 10 || BigInteger::fromString(utf16) != -15) return 7;
            if (utf8 != String("+010") || utf16 != String("-0o17")) return 8;
            digits : char[]! = new char[2];
            digits[0] = '4'; digits[1] = '2';
            parsed : BigInteger = BigInteger::fromString(digits);
            if (parsed != 42 || digits[0] != '4' || digits[1] != '2') return 9;
            digits[0] = '7';
            if (parsed != 42) return 10;
            huge : BigInteger = BigInteger::fromString("0x100000000000000000000000000000000");
            text : String = huge.toString();
            if (text != String("340282366920938463463374607431768211456")) return 11;
            negativeText : String("-12345678901234567890123456789012345678901234567890");
            negative : BigInteger = BigInteger::fromString(negativeText);
            roundTrip : String = negative.toString();
            if (roundTrip != negativeText) return 12;
            copied : BigInteger(huge);
            copied += 1;
            if (copied - huge != 1) return 13;
            return 0;
        }
    )SRC");
}

TEST_CASE("BigInteger parsing factories respect explicit radix and case conventions",
          "[libk][big-integer][parsing]") {
    check_big_integer(R"SRC(
        module __big_integer_parse_bases__;
        test() : int {
            if (BigInteger::fromString("+0B101", 2) != 5) return 1;
            if (BigInteger::fromString("-0o17", 8) != -15) return 2;
            if (BigInteger::fromString("0XAf", 16) != 175) return 3;
            if (BigInteger::fromString("010", 10) != 10) return 4;
            if (BigInteger::fromString("0b10", 16) != 2832) return 5;
            if (BigInteger::fromString("0x", 36) != 33) return 6;
            if (BigInteger::fromString("zZ", 36) != 1295) return 7;
            if (BigInteger::fromString("Aa", 37) != 406 || BigInteger::fromString("aA", 37) != 1342) return 8;
            if (BigInteger::fromString("Zz", 62) != 2231 || BigInteger::fromString("zZ", 62) != 3817) return 9;
            utf8 : String(u8"-0xFF");
            utf16 : String(u"zZ");
            if (BigInteger::fromString(utf8, 16) != -255) return 10;
            if (BigInteger::fromString(utf16, 62) != 3817) return 11;
            digits : char[]! = new char[2];
            digits[0] = 'f'; digits[1] = 'F';
            if (BigInteger::fromString(digits, 16) != 255) return 12;
            if (digits[0] != 'f' || digits[1] != 'F') return 13;
            if (BigInteger::fromString("-0", 62).signum() != 0) return 14;
            return 0;
        }
    )SRC");
}

TEST_CASE("BigInteger parsing rejects malformed text with NumberFormatError",
          "[libk][big-integer][parsing][exceptions]") {
    check_big_integer(R"SRC(
        module __big_integer_parse_errors__;
        rejectsString(text: const String&) : bool {
            try { value : BigInteger = BigInteger::fromString(text); }
            catch (error: NumberFormatError&) { return error.getCode() == 16; }
            return false;
        }
        rejects(text: const char[]) : bool {
            caught : bool = false;
            try { value : BigInteger = BigInteger::fromString(text); }
            catch (error: NumberFormatError&) { caught = error.getCode() == 16; }
            return caught && rejectsString(String(text));
        }
        test() : int {
            if (!rejects("") || !rejects("+") || !rejects("-")) return 1;
            if (!rejects("0b") || !rejects("+0O") || !rejects("-0x")) return 2;
            if (!rejects("++1") || !rejects("+-1") || !rejects("--1")) return 3;
            if (!rejects(" 12") || !rejects("12 ") || !rejects("1 2")) return 4;
            if (!rejects("\t12") || !rejects("12\n")) return 5;
            if (!rejects("1_2") || !rejects("1,2") || !rejects("12!")) return 6;
            if (!rejects("0b2") || !rejects("0o8") || !rejects("0xg")) return 7;
            if (!rejects("12x") || !rejects("\u0661") || !rejects("\uFF11")) return 8;
            utf8 : String(u8"\u0661");
            utf16 : String(u"\uFF11");
            if (!rejectsString(utf8) || !rejectsString(utf16)) return 9;
            caught : bool = false;
            try { value : BigInteger = BigInteger::fromString("-"); }
            catch (error: ArithmeticError&) { caught = error.getCode() == 16; }
            if (!caught) return 10;
            return 0;
        }
    )SRC");
}

TEST_CASE("BigInteger parsing distinguishes array terminators from String NUL content",
          "[libk][big-integer][parsing][exceptions]") {
    check_big_integer(R"SRC(
        module __big_integer_parse_counted__;
        rejectsArray(text: const char[]) : bool {
            try { value : BigInteger = BigInteger::fromString(text); }
            catch (error: NumberFormatError&) { return true; }
            return false;
        }
        rejectsString(text: const String&) : bool {
            try { value : BigInteger = BigInteger::fromString(text); }
            catch (error: NumberFormatError&) { return true; }
            return false;
        }
        test() : int {
            empty : char[]! = new char[0];
            emptyString : String;
            if (!rejectsArray(empty) || !rejectsString(emptyString)) return 1;
            zero : char[]! = new char[1];
            zero[0] = (char) 0;
            if (!rejectsArray(zero)) return 2;
            zeros : char[]! = new char[2];
            zeros[0] = (char) 0; zeros[1] = (char) 0;
            if (!rejectsArray(zeros) || !rejectsString(String(zeros))) return 9;
            terminated : char[]! = new char[3];
            terminated[0] = '1'; terminated[1] = '2'; terminated[2] = (char) 0;
            if (BigInteger::fromString(terminated) != 12) return 3;
            embedded : char[]! = new char[4];
            embedded[0] = '1'; embedded[1] = (char) 0;
            embedded[2] = '2'; embedded[3] = (char) 0;
            if (!rejectsArray(embedded) || !rejectsString(String(embedded))) return 4;
            doubled : char[]! = new char[4];
            doubled[0] = '1'; doubled[1] = '2';
            doubled[2] = (char) 0; doubled[3] = (char) 0;
            if (!rejectsArray(doubled)) return 5;
            trailingNul : String(doubled);
            if (trailingNul.size() != 3u || !rejectsString(trailingNul)) return 6;
            utf8 : String(doubled, Encoding::UTF8);
            utf16 : String(doubled, Encoding::UTF16);
            if (!rejectsString(utf8) || !rejectsString(utf16)) return 7;
            if (embedded[1] != (char) 0 || embedded[2] != '2') return 8;
            return 0;
        }
    )SRC");
}

TEST_CASE("BigInteger parsing rejects invalid public bases and explicit-base digits",
          "[libk][big-integer][parsing][exceptions]") {
    check_big_integer(R"SRC(
        module __big_integer_parse_radix_errors__;
        rejects(text: const char[], base: int) : bool {
            caught : bool = false;
            try { value : BigInteger = BigInteger::fromString(text, base); }
            catch (error: NumberFormatError&) { caught = error.getCode() == 16; }
            if (!caught) return false;
            string : String(text);
            try { value : BigInteger = BigInteger::fromString(string, base); }
            catch (error: NumberFormatError&) { return error.getCode() == 16; }
            return false;
        }
        test() : int {
            if (!rejects("10", 0) || !rejects("10", 1) || !rejects("10", 63)) return 1;
            if (!rejects("10", -2) || !rejects("10", 2147483647)) return 2;
            if (!rejects("2", 2) || !rejects("8", 8) || !rejects("g", 16)) return 3;
            if (!rejects("b", 37) || !rejects("0x10", 10)) return 4;
            if (!rejects("0b", 2) || !rejects("0o", 8) || !rejects("0x", 16)) return 5;
            if (!rejects("", 10) || !rejects("+", 62) || !rejects("1 ", 36)) return 6;
            if (!rejects("\u0661", 62) || !rejects("1_0", 16)) return 7;
            nul : char[]! = new char[4];
            nul[0] = '1'; nul[1] = '2'; nul[2] = (char) 0; nul[3] = (char) 0;
            if (!rejects(nul, 16)) return 8;
            empty : char[]! = new char[0];
            caught : bool = false;
            try { value : BigInteger = BigInteger::fromString(empty, 10); }
            catch (error: NumberFormatError&) { caught = true; }
            if (!caught) return 9;
            return 0;
        }
    )SRC");
}
