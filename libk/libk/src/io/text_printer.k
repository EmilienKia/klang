/*
 * K Language standard library — base module
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

module k;

namespace io {

@ffi::Extern("C") private __k_io_format_double(v: double, notation: int, precision: int, buf: byte*, bufLen: int) : int;

/**
 * Format specification for integer printing.
 */
public struct IntegerFormat {
    _radix      : unsigned int;
    _uppercase  : bool;
    _showPrefix : bool;
    _minDigits  : unsigned int;

public:
    IntegerFormat() : _radix(10u), _uppercase(false), _showPrefix(false), _minDigits(0u) {}
    IntegerFormat(radix: unsigned int, uppercase: bool = false, showPrefix: bool = false, minDigits: unsigned int = 0u)
        : _radix(radix), _uppercase(uppercase), _showPrefix(showPrefix), _minDigits(minDigits) {}

    const radix() : unsigned int { return _radix; }
    const uppercase() : bool { return _uppercase; }
    const showPrefix() : bool { return _showPrefix; }
    const minDigits() : unsigned int { return _minDigits; }

    static decimal() : IntegerFormat {
        f : IntegerFormat(10u, false, false, 0u);
        return f;
    }

    static hex(uppercase: bool = false, showPrefix: bool = false) : IntegerFormat {
        f : IntegerFormat(16u, uppercase, showPrefix, 0u);
        return f;
    }

    static octal(showPrefix: bool = false) : IntegerFormat {
        f : IntegerFormat(8u, false, showPrefix, 0u);
        return f;
    }

    static binary(showPrefix: bool = false) : IntegerFormat {
        f : IntegerFormat(2u, false, showPrefix, 0u);
        return f;
    }
}

/**
 * Floating point notation styles.
 */
public enum FloatNotation {
    Standard;
    Fixed;
    Scientific;
}

/**
 * Format specification for floating point printing.
 */
public struct FloatFormat {
    _notation  : FloatNotation;
    _precision : int;

public:
    FloatFormat() : _notation(FloatNotation::Standard), _precision(-1) {}
    FloatFormat(notation: FloatNotation, precision: int = -1) : _notation(notation), _precision(precision) {}

    const notation() : FloatNotation { return _notation; }
    const precision() : int { return _precision; }

    static standard() : FloatFormat {
        f : FloatFormat(FloatNotation::Standard, -1);
        return f;
    }

    static fixed(precision: int) : FloatFormat {
        f : FloatFormat(FloatNotation::Fixed, precision);
        return f;
    }

    static scientific(precision: int) : FloatFormat {
        f : FloatFormat(FloatNotation::Scientific, precision);
        return f;
    }
}

/**
 * Print formatted representations of values to a Writer.
 */
public class TextPrinter : public FilterWriter {
public:
    TextPrinter(out: Writer*) : FilterWriter(out) {}
    TextPrinter() : FilterWriter() {}

    /** Print a boolean ("true" or "false"). */
    print(v: bool) : TextPrinter& {
        if (v) {
            write("true");
        } else {
            write("false");
        }
        return this;
    }

    /** Print a single character. */
    print(v: char) : TextPrinter& {
        write(v);
        return this;
    }

    /** Print a byte as an integer. */
    print(v: byte) : TextPrinter& {
        fmt : IntegerFormat = IntegerFormat::decimal();
        return print((long) v, fmt);
    }

    /** Print a short as an integer. */
    print(v: short) : TextPrinter& {
        fmt : IntegerFormat = IntegerFormat::decimal();
        return print((long) v, fmt);
    }

    /** Print an int as an integer. */
    print(v: int) : TextPrinter& {
        fmt : IntegerFormat = IntegerFormat::decimal();
        return print((long) v, fmt);
    }

    /** Print a long with the given IntegerFormat. */
    print(v: long) : TextPrinter& {
        fmt : IntegerFormat = IntegerFormat::decimal();
        return print(v, fmt);
    }

    print(v: long, fmt: const IntegerFormat&) : TextPrinter& {
        if (v < 0L && fmt.radix() == 10u) {
            write('-');
            uv : unsigned long = (unsigned long) (-v);
            _printUnsigned(uv, fmt, false);
        } else {
            _printUnsigned((unsigned long) v, fmt, v < 0L);
        }
        return this;
    }

    /** Print an unsigned short as an integer. */
    print(v: unsigned short) : TextPrinter& {
        fmt : IntegerFormat = IntegerFormat::decimal();
        return print((unsigned long) v, fmt);
    }

    /** Print an unsigned int as an integer. */
    print(v: unsigned int) : TextPrinter& {
        fmt : IntegerFormat = IntegerFormat::decimal();
        return print((unsigned long) v, fmt);
    }

    /** Print an unsigned long with the given IntegerFormat. */
    print(v: unsigned long) : TextPrinter& {
        fmt : IntegerFormat = IntegerFormat::decimal();
        return print(v, fmt);
    }

    print(v: unsigned long, fmt: const IntegerFormat&) : TextPrinter& {
        _printUnsigned(v, fmt, false);
        return this;
    }

    /** Print a float with the given FloatFormat. */
    print(v: float) : TextPrinter& {
        fmt : FloatFormat = FloatFormat::standard();
        return print((double) v, fmt);
    }

    /** Print a double with the given FloatFormat. */
    print(v: double) : TextPrinter& {
        fmt : FloatFormat = FloatFormat::standard();
        return print(v, fmt);
    }

    print(v: double, fmt: const FloatFormat&) : TextPrinter& {
        buf : byte[64];
        notCode : int = 0;
        if (fmt.notation() == FloatNotation::Fixed) notCode = 1;
        else if (fmt.notation() == FloatNotation::Scientific) notCode = 2;

        n : int = __k_io_format_double(v, notCode, fmt.precision(), &buf[0], 64);
        if (n > 0) {
            i : int = 0;
            while (i < n) {
                write((char) buf[i]);
                ++i;
            }
        }
        return this;
    }

    /** Print a String. */
    print(str: const String&) : TextPrinter& {
        write(str);
        return this;
    }

    /** Print a string literal / character array. */
    print(str: const char[]) : TextPrinter& {
        write(str);
        return this;
    }

    // ── println methods ──────────────────────────────────────────────────

    println() : TextPrinter& {
        write((char) 10);
        return this;
    }

    println(v: bool) : TextPrinter& {
        print(v);
        return println();
    }

    println(v: char) : TextPrinter& {
        print(v);
        return println();
    }

    println(v: byte) : TextPrinter& {
        print(v);
        return println();
    }

    println(v: short) : TextPrinter& {
        print(v);
        return println();
    }

    println(v: int) : TextPrinter& {
        print(v);
        return println();
    }

    println(v: long) : TextPrinter& {
        print(v);
        return println();
    }

    println(v: unsigned short) : TextPrinter& {
        print(v);
        return println();
    }

    println(v: unsigned int) : TextPrinter& {
        print(v);
        return println();
    }

    println(v: unsigned long) : TextPrinter& {
        print(v);
        return println();
    }

    println(v: float) : TextPrinter& {
        print(v);
        return println();
    }

    println(v: double) : TextPrinter& {
        print(v);
        return println();
    }

    println(str: const String&) : TextPrinter& {
        print(str);
        return println();
    }

    println(str: const char[]) : TextPrinter& {
        print(str);
        return println();
    }

private:
    _printUnsigned(v: unsigned long, fmt: const IntegerFormat&, isNegative: bool) {
        rad : unsigned long = (unsigned long) fmt.radix();
        if (rad < 2uL || rad > 36uL) rad = 10uL;

        if (fmt.showPrefix()) {
            if (rad == 16uL) {
                if (fmt.uppercase()) write("0X");
                else write("0x");
            } else if (rad == 2uL) {
                write("0b");
            } else if (rad == 8uL) {
                write("0o");
            }
        }

        if (v == 0uL && fmt.minDigits() <= 1u) {
            write('0');
            return;
        }

        digits : char[64];
        count : unsigned int = 0u;
        cur : unsigned long = v;
        while (cur > 0uL || count < fmt.minDigits()) {
            rem : unsigned int = (unsigned int) (cur % rad);
            cur = cur / rad;

            c : char = '0';
            if (rem < 10u) {
                c = (char) ('0' + rem);
            } else {
                if (fmt.uppercase()) {
                    c = (char) ('A' + (rem - 10u));
                } else {
                    c = (char) ('a' + (rem - 10u));
                }
            }
            digits[count] = c;
            ++count;
        }

        if (count == 0u) {
            write('0');
            return;
        }

        // Write digits reversed
        i : unsigned int = count;
        while (i > 0u) {
            --i;
            write(digits[i]);
        }
    }
}

} // namespace io
