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

/**
 * Runtime helper: convert float values to raw integer bit patterns.
 */
@ffi::Extern("C") __k_io_float_to_bits(f: float) : int;
/**
 * Runtime helper: convert raw integer bit patterns to float values.
 */
@ffi::Extern("C") __k_io_bits_to_float(bits: int) : float;
/**
 * Runtime helper: convert double values to raw integer bit patterns.
 */
@ffi::Extern("C") __k_io_double_to_bits(d: double) : long;
/**
 * Runtime helper: convert raw integer bit patterns to double values.
 */
@ffi::Extern("C") __k_io_bits_to_double(bits: long) : double;

@ffi::Extern("C") __k_io_native_bytes_to_short(buf: const byte*) : int;
@ffi::Extern("C") __k_io_native_bytes_to_int(buf: const byte*) : int;
@ffi::Extern("C") __k_io_native_bytes_to_long(buf: const byte*) : long;

@ffi::Extern("C") __k_io_short_to_native_bytes(v: int, buf: byte*);
@ffi::Extern("C") __k_io_int_to_native_bytes(v: int, buf: byte*);
@ffi::Extern("C") __k_io_long_to_native_bytes(v: long, buf: byte*);

namespace io {

/**
 * Read primitive values from a byte stream.
 *
 * Multi-byte values are interpreted using native platform byte order.
 */
public interface DataInput {
    /** Read a single signed byte. */
    readByte() : Expected<byte, int>;

    /** Read a single char. */
    readChar() : Expected<char, int>;

    /** Read a signed 16-bit short (native byte order). */
    readShort() : Expected<short, int>;

    /** Read an unsigned 16-bit short (native byte order). */
    readUnsignedShort() : Expected<unsigned short, int>;

    /** Read a signed 32-bit int (native byte order). */
    readInt() : Expected<int, int>;

    /** Read an unsigned 32-bit int (native byte order). */
    readUnsignedInt() : Expected<unsigned int, int>;

    /** Read a signed 64-bit long (native byte order). */
    readLong() : Expected<long, int>;

    /** Read an unsigned 64-bit long (native byte order). */
    readUnsignedLong() : Expected<unsigned long, int>;

    /** Read a 32-bit IEEE 754 float (native byte order). */
    readFloat() : float;

    /** Read a 64-bit IEEE 754 double (native byte order). */
    readDouble() : double;

    /** Read a boolean (1 byte, != 0 means true). */
    readBool() : bool;

    /**
     * Read exactly `len` bytes into `buf` starting at `off`.
     * Returns the number of bytes read, or an error when the stream is broken.
     */
    readFully(buf: byte[], off: unsigned int, len: unsigned int) : Expected<unsigned int, int>;

    /**
     * Read exactly `buf.size` bytes into `buf`.
     * Returns the number of bytes read, or an error when the stream is broken.
     */
    readFully(buf: byte[]) : Expected<unsigned int, int>;
}


/**
 * Write primitive values to a byte stream.
 *
 * Multi-byte values are encoded using native platform byte order.
 */
public interface DataOutput {
    /** Write a single byte. */
    writeByte(v: byte) : DataOutput&;

    /** Write a single char. */
    writeChar(v: char) : DataOutput&;

    /** Write a signed 16-bit short (native byte order). */
    writeShort(v: short) : DataOutput&;

    /** Write an unsigned 16-bit short (native byte order). */
    writeUnsignedShort(v: unsigned short) : DataOutput&;

    /** Write a signed 32-bit int (native byte order). */
    writeInt(v: int) : DataOutput&;

    /** Write an unsigned 32-bit int (native byte order). */
    writeUnsignedInt(v: unsigned int) : DataOutput&;

    /** Write a signed 64-bit long (native byte order). */
    writeLong(v: long) : DataOutput&;

    /** Write an unsigned 64-bit long (native byte order). */
    writeUnsignedLong(v: unsigned long) : DataOutput&;

    /** Write a 32-bit IEEE 754 float (native byte order). */
    writeFloat(v: float) : DataOutput&;

    /** Write a 64-bit IEEE 754 double (native byte order). */
    writeDouble(v: double) : DataOutput&;

    /** Write a boolean (1 byte: 1 for true, 0 for false). */
    writeBool(v: bool) : DataOutput&;
}


/**
 * Read primitive values from an underlying input stream.
 *
 * `DataInputStream` combines `InputStream` delegation with typed primitive
 * reading helpers.
 */
public class DataInputStream : public FilterInputStream<byte>, public DataInput {

public:
    /**
     * Construct a data input stream wrapper.
     *
     * @param input Wrapped input stream.
     */
    DataInputStream(input: InputStream<byte>+) : FilterInputStream(input) {}

    // ── DataInput implementation ─────────────────────────────────────────

    readByte() : Expected<byte, int> {
        buf : byte[1];
        n : Expected<unsigned int, int> = readFully(buf, 0, 1);
        if (n.hasError()) { return Expected<byte, int>::error(n.getError()); }
        if (n.getResult() == 0) { return Expected<byte, int>::error(2); }
        v : byte = buf[0];
        return Expected<byte, int>::expected(v);
    }

    readChar() : Expected<char, int> {
        buf : byte[1];
        n : Expected<unsigned int, int> = readFully(buf, 0, 1);
        if (n.hasError()) { return Expected<char, int>::error(n.getError()); }
        if (n.getResult() == 0) { return Expected<char, int>::error(2); }
        v : char = (char) buf[0];
        return Expected<char, int>::expected(v);
    }

    readShort() : Expected<short, int> {
        buf : byte[2];
        n : Expected<unsigned int, int> = readFully(buf);
        if (n.hasError()) { return Expected<short, int>::error(n.getError()); }
        if (n.getResult() == 0) { return Expected<short, int>::error(2); }
        v : short = (short) __k_io_native_bytes_to_short(&buf[0]);
        return Expected<short, int>::expected(v);
    }

    readUnsignedShort() : Expected<unsigned short, int> {
        buf : byte[2];
        n : Expected<unsigned int, int> = readFully(buf);
        if (n.hasError()) { return Expected<unsigned short, int>::error(n.getError()); }
        if (n.getResult() == 0) { return Expected<unsigned short, int>::error(2); }
        v : unsigned short = (unsigned short) __k_io_native_bytes_to_short(&buf[0]);
        return Expected<unsigned short, int>::expected(v);
    }

    readInt() : Expected<int, int> {
        buf : byte[4];
        n : Expected<unsigned int, int> = readFully(buf);
        if (n.hasError()) { return Expected<int, int>::error(n.getError()); }
        if (n.getResult() == 0) { return Expected<int, int>::error(2); }
        v : int = __k_io_native_bytes_to_int(&buf[0]);
        return Expected<int, int>::expected(v);
    }

    readUnsignedInt() : Expected<unsigned int, int> {
        buf : byte[4];
        n : Expected<unsigned int, int> = readFully(buf);
        if (n.hasError()) { return Expected<unsigned int, int>::error(n.getError()); }
        if (n.getResult() == 0) { return Expected<unsigned int, int>::error(2); }
        v : unsigned int = (unsigned int) __k_io_native_bytes_to_int(&buf[0]);
        return Expected<unsigned int, int>::expected(v);
    }

    readLong() : Expected<long, int> {
        buf : byte[8];
        n : Expected<unsigned int, int> = readFully(buf);
        if (n.hasError()) { return Expected<long, int>::error(n.getError()); }
        if (n.getResult() == 0) { return Expected<long, int>::error(2); }
        v : long = __k_io_native_bytes_to_long(&buf[0]);
        return Expected<long, int>::expected(v);
    }

    readUnsignedLong() : Expected<unsigned long, int> {
        buf : byte[8];
        n : Expected<unsigned int, int> = readFully(buf);
        if (n.hasError()) { return Expected<unsigned long, int>::error(n.getError()); }
        if (n.getResult() == 0) { return Expected<unsigned long, int>::error(2); }
        v : unsigned long = (unsigned long) __k_io_native_bytes_to_long(&buf[0]);
        return Expected<unsigned long, int>::expected(v);
    }

    readFloat() : float {
        buf : byte[4];
        n : Expected<unsigned int, int> = readFully(buf);
        if (n.hasError() || n.getResult() == 0) { return 0.0f; }
        bits : int = __k_io_native_bytes_to_int(&buf[0]);
        return __k_io_bits_to_float(bits);
    }

    readDouble() : double {
        buf : byte[8];
        n : Expected<unsigned int, int> = readFully(buf);
        if (n.hasError() || n.getResult() == 0) { return 0.0; }
        bits : long = __k_io_native_bytes_to_long(&buf[0]);
        return __k_io_bits_to_double(bits);
    }

    readBool() : bool {
        buf : byte[1];
        n : Expected<unsigned int, int> = readFully(buf, 0, 1);
        if (n.hasError() || n.getResult() == 0) { return false; }
        return buf[0] != (byte) 0;
    }

    readFully(buf: byte[], off: unsigned int, len: unsigned int) : Expected<unsigned int, int> {
        remaining : unsigned int = len;
        pos : unsigned int = off;
        while (remaining > 0) {
            n : Expected<unsigned int, int> = _in->read(buf, pos, remaining);
            if (n.hasError()) { return n; }
            got : unsigned int = n.getResult();
            if (got == 0) {
                // EOS before all bytes were read
                done : unsigned int = len - remaining;
                return Expected<unsigned int, int>::expected(done);
            }
            pos = pos + got;
            remaining = remaining - got;
        }
        return Expected<unsigned int, int>::expected(len);
    }

    readFully(buf: byte[]) : Expected<unsigned int, int> {
        return readFully(buf, 0, (unsigned int) buf.size);
    }
}


/**
 * Write primitive values to an underlying output stream.
 *
 * `DataOutputStream` combines `OutputStream` delegation with typed primitive
 * writing helpers.
 */
public class DataOutputStream : public FilterOutputStream<byte>, public DataOutput {
    _written  : int;

public:
    /**
     * Construct a data output stream wrapper.
     *
     * @param output Wrapped output stream.
     */
    DataOutputStream(output: OutputStream<byte>+) : FilterOutputStream(output), _written(0) {}

    /**
     * Return the total number of bytes written through this stream.
     *
     * @return Cumulative byte count.
     */
    const size() : int {
        return _written;
    }

    // ── DataOutput implementation ────────────────────────────────────────

    writeByte(v: byte) : DataOutput& {
        _out->write(v);
        _written = _written + 1;
        return this;
    }

    writeChar(v: char) : DataOutput& {
        _out->write((byte) v);
        _written = _written + 1;
        return this;
    }

    writeShort(v: short) : DataOutput& {
        buf : byte[2];
        __k_io_short_to_native_bytes((int) v, &buf[0]);
        _out->write(buf);
        _written = _written + 2;
        return this;
    }

    writeUnsignedShort(v: unsigned short) : DataOutput& {
        buf : byte[2];
        __k_io_short_to_native_bytes((int) v, &buf[0]);
        _out->write(buf);
        _written = _written + 2;
        return this;
    }

    writeInt(v: int) : DataOutput& {
        buf : byte[4];
        __k_io_int_to_native_bytes(v, &buf[0]);
        _out->write(buf);
        _written = _written + 4;
        return this;
    }

    writeUnsignedInt(v: unsigned int) : DataOutput& {
        buf : byte[4];
        __k_io_int_to_native_bytes((int) v, &buf[0]);
        _out->write(buf);
        _written = _written + 4;
        return this;
    }

    writeLong(v: long) : DataOutput& {
        buf : byte[8];
        __k_io_long_to_native_bytes(v, &buf[0]);
        _out->write(buf);
        _written = _written + 8;
        return this;
    }

    writeUnsignedLong(v: unsigned long) : DataOutput& {
        writeLong((long) v);
        return this;
    }

    writeFloat(v: float) : DataOutput& {
        writeInt(__k_io_float_to_bits(v));
        return this;
    }

    writeDouble(v: double) : DataOutput& {
        writeLong(__k_io_double_to_bits(v));
        return this;
    }

    writeBool(v: bool) : DataOutput& {
        if (v) {
            _out->write((byte) 1);
        } else {
            _out->write((byte) 0);
        }
        _written = _written + 1;
        return this;
    }

    // ...existing code...
}

} // namespace io







