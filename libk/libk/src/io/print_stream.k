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

/**
 * Write textual representations of primitive values to an output stream.
 *
 * `PrintStream` provides overloaded `print` and `println` helpers for primitive
 * and string-like values and supports fluent chaining.
 */
public class PrintStream : public FilterOutputStream<byte> {
private:
    @ffi::Extern("C") private static __k_io_int_to_str(v: int, buf: byte*, bufLen: int) : int;
    @ffi::Extern("C") private static __k_io_long_to_str(v: long, buf: byte*, bufLen: int) : int;
    @ffi::Extern("C") private static __k_io_uint_to_str(v: unsigned int, buf: byte*, bufLen: int) : int;
    @ffi::Extern("C") private static __k_io_ulong_to_str(v: unsigned long, buf: byte*, bufLen: int) : int;
    @ffi::Extern("C") private static __k_io_float_to_str(v: float, buf: byte*, bufLen: int) : int;
    @ffi::Extern("C") private static __k_io_double_to_str(v: double, buf: byte*, bufLen: int) : int;
    @ffi::Extern("C") private static __k_io_chars_to_bytes(src: const char*, dst: byte*, len: int) : int;

public:
    /**
     * Construct a PrintStream wrapping the given output stream.
     * The stream is held by pointer — the caller retains ownership.
     *
     * @param output Wrapped output stream.
     */
    PrintStream(output: OutputStream<byte>*) : FilterOutputStream(output) {}

    // ── print methods (one per primitive type) ───────────────────────────

    /** Print a boolean as "true" or "false". */
    print(v: bool) : PrintStream& {
        static const TRUE : byte[4] { 't', 'r', 'u', 'e' };
        static const FALSE : byte[5] { 'f', 'a', 'l', 's', 'e' };
        if (v) {
            _out->write(TRUE, 0, 4);
        } else {
            _out->write(FALSE, 0, 5);
        }
        return this;
    }

    /** Print a single character. */
    print(v: char) : PrintStream& {
        _out->write((byte) v);
        return this;
    }

    /** Print a byte as a decimal integer. */
    print(v: byte) : PrintStream& {
        buf : byte[12];
        n : int = __k_io_int_to_str((int) v, &buf[0], 12);
        _out->write(buf, 0, (unsigned int) n);
        return this;
    }

    /** Print a short as a decimal integer. */
    print(v: short) : PrintStream& {
        buf : byte[12];
        n : int = __k_io_int_to_str((int) v, &buf[0], 12);
        _out->write(buf, 0, (unsigned int) n);
        return this;
    }

    /** Print a signed 32-bit integer. */
    print(v: int) : PrintStream& {
        buf : byte[12];
        n : int = __k_io_int_to_str(v, &buf[0], 12);
        _out->write(buf, 0, (unsigned int) n);
        return this;
    }

    /** Print a signed 64-bit long. */
    print(v: long) : PrintStream& {
        buf : byte[24];
        n : int = __k_io_long_to_str(v, &buf[0], 24);
        _out->write(buf, 0, (unsigned int) n);
        return this;
    }

    /** Print an unsigned short as a decimal integer. */
    print(v: unsigned short) : PrintStream& {
        buf : byte[12];
        n : int = __k_io_uint_to_str((unsigned int) v, &buf[0], 12);
        _out->write(buf, 0, (unsigned int) n);
        return this;
    }

    /** Print an unsigned 32-bit integer. */
    print(v: unsigned int) : PrintStream& {
        buf : byte[12];
        n : int = __k_io_uint_to_str(v, &buf[0], 12);
        _out->write(buf, 0, (unsigned int) n);
        return this;
    }

    /** Print an unsigned 64-bit long. */
    print(v: unsigned long) : PrintStream& {
        buf : byte[24];
        n : int = __k_io_ulong_to_str(v, &buf[0], 24);
        _out->write(buf, 0, (unsigned int) n);
        return this;
    }

    /** Print a 32-bit float. */
    print(v: float) : PrintStream& {
        buf : byte[24];
        n : int = __k_io_float_to_str(v, &buf[0], 24);
        _out->write(buf, 0, (unsigned int) n);
        return this;
    }

    /** Print a 64-bit double. */
    print(v: double) : PrintStream& {
        buf : byte[24];
        n : int = __k_io_double_to_str(v, &buf[0], 24);
        _out->write(buf, 0, (unsigned int) n);
        return this;
    }

    /** Print a character array / string literal (writes all bytes, stripping
     *  a trailing null if present so that only the readable content is
     *  written — consistent with the String(const char[]) convention). */
    print(v: const char[]) : PrintStream& {
        len : unsigned int = v.size;
        // K string literals always include a null terminator in the array
        // size; strip it so that only the readable content is written.
        if (len > 0) {
            len = len - 1;
        }
        if (len > 0) {
            buf : byte[]! = new byte[(int) len];
            __k_io_chars_to_bytes(&v[0], &buf[0], (int) len);
            _out->write(buf, 0, len);
            delete buf;
        }
        return this;
    }

    // ── println methods ──────────────────────────────────────────────────

    /** Print a newline. */
    println() : PrintStream& {
        _out->write((byte) 10);
        return this;
    }

    /** Print a boolean followed by a newline. */
    println(v: bool) : PrintStream& {
        print(v);
        _out->write((byte) 10);
        return this;
    }

    /** Print a character followed by a newline. */
    println(v: char) : PrintStream& {
        print(v);
        _out->write((byte) 10);
        return this;
    }

    /** Print a byte followed by a newline. */
    println(v: byte) : PrintStream& {
        print(v);
        _out->write((byte) 10);
        return this;
    }

    /** Print a short followed by a newline. */
    println(v: short) : PrintStream& {
        print(v);
        _out->write((byte) 10);
        return this;
    }

    /** Print an int followed by a newline. */
    println(v: int) : PrintStream& {
        print(v);
        _out->write((byte) 10);
        return this;
    }

    /** Print a long followed by a newline. */
    println(v: long) : PrintStream& {
        print(v);
        _out->write((byte) 10);
        return this;
    }

    /** Print an unsigned short followed by a newline. */
    println(v: unsigned short) : PrintStream& {
        print(v);
        _out->write((byte) 10);
        return this;
    }

    /** Print an unsigned int followed by a newline. */
    println(v: unsigned int) : PrintStream& {
        print(v);
        _out->write((byte) 10);
        return this;
    }

    /** Print an unsigned long followed by a newline. */
    println(v: unsigned long) : PrintStream& {
        print(v);
        _out->write((byte) 10);
        return this;
    }

    /** Print a float followed by a newline. */
    println(v: float) : PrintStream& {
        print(v);
        _out->write((byte) 10);
        return this;
    }

    /** Print a double followed by a newline. */
    println(v: double) : PrintStream& {
        print(v);
        _out->write((byte) 10);
        return this;
    }

    /** Print a character array / string literal followed by a newline. */
    println(v: const char[]) : PrintStream& {
        print(v);
        _out->write(10);
        return this;
    }

    // ── flush / close ────────────────────────────────────────────────────

    flush() : OutputStream<byte>& {
        _out->flush();
        return this;
    }

    close() : OutputStream<byte>& {
        flush();
        _out->close();
        return this;
    }
}

} // namespace io


















