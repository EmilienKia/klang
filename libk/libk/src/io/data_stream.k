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
    readByte() : byte;

    /** Read a single char. */
    readChar() : char;

    /** Read a signed 16-bit short (native byte order). */
    readShort() : short;

    /** Read an unsigned 16-bit short (native byte order). */
    readUnsignedShort() : unsigned short;

    /** Read a signed 32-bit int (native byte order). */
    readInt() : int;

    /** Read an unsigned 32-bit int (native byte order). */
    readUnsignedInt() : unsigned int;

    /** Read a signed 64-bit long (native byte order). */
    readLong() : long;

    /** Read an unsigned 64-bit long (native byte order). */
    readUnsignedLong() : unsigned long;

    /** Read a 32-bit IEEE 754 float (native byte order). */
    readFloat() : float;

    /** Read a 64-bit IEEE 754 double (native byte order). */
    readDouble() : double;

    /** Read a boolean (1 byte, != 0 means true). */
    readBool() : bool;

    /**
     * Read exactly `len` bytes into `buf` starting at `off`.
     * Blocks until all bytes are read, end of stream is reached, or
     * an error occurs.
     */
    readFully(buf: byte[], off: unsigned int, len: unsigned int);

    /**
     * Read exactly `buf.size` bytes into `buf`.
     * Blocks until all bytes are read, end of stream is reached, or
     * an error occurs.
     */
    readFully(buf: byte[]);
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

    readByte() : byte {
        buf : byte[1];
        if (_in->read(buf, 0, 1) <= 0) {
            return (byte) 0;
        }
        return buf[0];
    }

    readChar() : char {
        buf : byte[1];
        if (_in->read(buf, 0, 1) <= 0) {
            return (char) 0;
        }
        return (char) buf[0];
    }

    readShort() : short {
        buf : byte[2];
        readFully(buf);
        return (short) __k_io_native_bytes_to_short(&buf[0]);
    }

    readUnsignedShort() : unsigned short {
        buf : byte[2];
        readFully(buf);
        return (unsigned short) __k_io_native_bytes_to_short(&buf[0]);
    }

    readInt() : int {
        buf : byte[4];
        readFully(buf);
        return __k_io_native_bytes_to_int(&buf[0]);
    }

    readUnsignedInt() : unsigned int {
        buf : byte[4];
        readFully(buf);
        return (unsigned int) __k_io_native_bytes_to_int(&buf[0]);
    }

    readLong() : long {
        buf : byte[8];
        readFully(buf);
        return __k_io_native_bytes_to_long(&buf[0]);
    }

    readUnsignedLong() : unsigned long {
        val : long = readLong();
        return (unsigned long) val;
    }

    readFloat() : float {
        bits : int = readInt();
        return __k_io_bits_to_float(bits);
    }

    readDouble() : double {
        bits : long = readLong();
        return __k_io_bits_to_double(bits);
    }

    readBool() : bool {
        buf : byte[1];
        if (_in->read(buf, 0, 1) <= 0) {
            return false;
        }
        return buf[0] != (byte) 0;
    }

    readFully(buf: byte[], off: unsigned int, len: unsigned int) {
        remaining : unsigned int = len;
        pos : unsigned int = off;
        while (remaining > 0) {
            n : int = _in->read(buf, pos, remaining);
            if (n <= 0) return;
            pos = pos + (unsigned int) n;
            remaining = remaining - (unsigned int) n;
        }
    }

    readFully(buf: byte[]) {
        readFully(buf, 0, (unsigned int) buf.size);
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







