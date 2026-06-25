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
 * Define the base contract for byte-oriented input streams.
 *
 * `read()` returns the next byte as an `Optional<byte>`, set when a byte was
 * successfully read, or empty at end of stream. Bulk reads fill
 * caller-provided buffers and report the number of bytes actually read.
 */
template<typename T>
public interface InputStream {
    /**
     * Read a single byte.
     *
     * @return An `Optional<byte>` containing the next byte if one is available,
     *         or an empty Optional at end of stream.
     */
    read() : Optional<T>;

    /**
     * Read up to `len` bytes into `buf` starting at `off`.
     *
     * @param buf Destination byte buffer.
     * @param off Destination start offset.
     * @param len Maximum number of bytes to read.
     * @return Number of bytes read, or `-1` if end of stream is reached first.
     */
    read(buf: T[], off: unsigned int, len: unsigned int) : int;

    /**
     * Read up to `buf.size` bytes into `buf`.
     *
     * @param buf Destination byte buffer.
     * @return Number of bytes read, or `-1` if end of stream is reached first.
     */
    read(buf: T[]) : int;

    /**
     * Skip and discard up to `n` bytes.
     *
     * @param n Requested number of bytes to skip.
     * @return Actual number of bytes skipped.
     */
    skip(n: unsigned long) : unsigned long;

    /**
     * Return a non-blocking availability estimate.
     *
     * @return Number of bytes that can be read without blocking.
     */
    available() : int;

    /**
     * Close the stream and release any associated resources.
     */
    close();
}


/**
 * Define the base contract for byte-oriented output streams.
 *
 * Single-byte writes use the low 8 bits of the provided integer value. Bulk
 * writes consume caller-provided buffers.
 */
template<typename T>
public interface OutputStream {
    /**
     * Write a single byte.
     *
     * @param b Source value; low 8 bits are written.
     * @return This stream.
     */
    write(b: T) : OutputStream<T>&;

    /**
     * Write `len` bytes from `buf` starting at `off`.
     *
     * @param buf Source byte buffer.
     * @param off Source start offset.
     * @param len Number of bytes to write.
     * @return This stream.
     */
    write(buf: const T[], off: unsigned int, len: unsigned int) : OutputStream<T>&;

    /**
     * Write all bytes from `buf`.
     *
     * @param buf Source byte buffer.
     * @return This stream.
     */
    write(buf: const T[]) : OutputStream<T>&;

    /**
     * Flush any buffered output to the underlying destination.
     * Returns this stream for fluent chaining.
     */
    flush() : OutputStream<T>&;

    /**
     * Flush and close the stream, releasing any associated resources.
     * Returns this stream for fluent chaining.
     */
    close() : OutputStream<T>&;
}

private self_output_stream(output: OutputStream<byte>&) : OutputStream<byte>& {
    return output;
}


/**
 * Read bytes from an in-memory array.
 *
 * The stream stores an internal copy of the source bytes and advances a read
 * cursor as data is consumed.
 */
public class ByteArrayInputStream : public InputStream<byte> {
    _buf   : byte[]!;
    _pos   : int;
    _count : int;

public:
    /**
     * Construct a ByteArrayInputStream from a byte array pointer.
     * The content is copied so the stream keeps independent storage.
     * `size` is the number of valid bytes to read from `buf`.
     */
    ByteArrayInputStream(buf: byte[]*, size: int) : _buf(null), _pos(0), _count(0) {
        if (size <= 0 || buf == null) {
            _buf = new byte[0];
            return;
        }

        _buf = new byte[size];
        i : int = 0;
        while (i < size) {
            _buf[i] = buf[i];
            i = i + 1;
        }
        _count = size;
    }

    /** Copy constructor — preserves the buffer reference and cursor state. */
    ByteArrayInputStream(other : const ByteArrayInputStream&) : _buf(other._buf), _pos(other._pos), _count(other._count) {}

    // ── InputStream implementation ───────────────────────────────────────

    override read() : Optional<byte> {
        if (_pos >= _count) {
            return Optional<byte>();
        }
        val : byte = _buf[_pos];
        _pos = _pos + 1;
        return Optional<byte>(val);
    }

    override read(buf: byte[], off: unsigned int, len: unsigned int) : int {
        if (_pos >= _count) return -1;
        avail : unsigned int = (unsigned int)(_count - _pos);
        actual : unsigned int = len;
        if (actual > avail) actual = avail;
        if (actual == 0) return 0;
        i : unsigned int = 0;
        while (i < actual) {
            buf[off + i] = _buf[(unsigned int) _pos + i];
            i = i + 1;
        }
        _pos = _pos + (int) actual;
        return (int) actual;
    }

    override read(buf: byte[]) : int {
        return read(buf, 0, (unsigned int) buf.size);
    }

    override skip(n: unsigned long) : unsigned long {
        avail : unsigned long = (unsigned long)(_count - _pos);
        actual : unsigned long = n;
        if (actual > avail) actual = avail;
        _pos = _pos + (int) actual;
        return actual;
    }

    override available() : int {
        return _count - _pos;
    }

    override close() {
        // Nothing to release for an in-memory stream.
    }
}


/**
 * Write bytes to a growable in-memory array.
 *
 * The stream accumulates written bytes and exposes helpers to query size,
 * reset content, and retrieve a copy of the accumulated data.
 */
public class ByteArrayOutputStream : public OutputStream<byte> {
    _buf   : byte[]!;
    _count : int;
    _cap   : int;

public:
    /** Default constructor with initial capacity of 32 bytes. */
    ByteArrayOutputStream() : _buf(null), _count(0), _cap(0) {}

    /** Constructor with specified initial capacity. */
    ByteArrayOutputStream(capacity: int) : _buf(null), _count(0), _cap(0) {
        if (capacity > 0) {
            _buf = new byte[capacity];
            _cap = capacity;
        }
    }

    /** Copy constructor — preserves the backing buffer reference and size state. */
    ByteArrayOutputStream(other : const ByteArrayOutputStream&) : _buf(other._buf), _count(other._count), _cap(other._cap) {}

    // ── OutputStream implementation ──────────────────────────────────────

    override write(b: byte) : OutputStream<byte>& {
        _ensureCap(1);
        _buf[_count] = b;
        _count = _count + 1;
        return self_output_stream(this);
    }

    override write(buf: const byte[], off: unsigned int, len: unsigned int) : OutputStream<byte>& {
        if (len == 0) {
            return self_output_stream(this);
        }
        _ensureCap((int) len);
        i : unsigned int = 0;
        while (i < len) {
            _buf[_count + (int) i] = buf[off + i];
            i = i + 1;
        }
        _count = _count + (int) len;
        return self_output_stream(this);
    }

    override write(buf: const byte[]) : OutputStream<byte>& {
        return write(buf, 0, (unsigned int) buf.size);
    }

    override flush() : OutputStream<byte>& {
        // No-op for in-memory stream.
        return self_output_stream(this);
    }

    override close() : OutputStream<byte>& {
        // No-op for in-memory stream.
        return self_output_stream(this);
    }

    // ── Additional methods ───────────────────────────────────────────────

    /** Return the number of bytes written so far. */
    const size() : int {
        return _count;
    }

    /**
     * Return a new byte array containing a copy of the written data.
     * The caller takes ownership of the returned array.
     */
    toByteArray() : byte[]! {
        result : byte[]! = new byte[_count];
        i : int = 0;
        while (i < _count) {
            result[i] = _buf[i];
            i = i + 1;
        }
        return result;
    }

    /** Reset the byte count to zero. The internal buffer is retained. */
    reset() {
        _count = 0;
    }

    /**
     * Write the entire content of this stream to another output stream.
     */
    writeTo(out: OutputStream<byte>&) {
        if (_count > 0) {
            out.write(_buf, 0, (unsigned int) _count);
        }
    }

private:
    /** Ensure the internal buffer has room for `extra` more bytes. */
    _ensureCap(extra: int) {
        needed : int = _count + extra;
        if (needed <= _cap) return;
        newCap : int = _cap * 2;
        if (newCap < 32) newCap = 32;
        if (newCap < needed) newCap = needed;
        newBuf : byte[]! = new byte[newCap];
        if (_buf != null) {
            i : int = 0;
            while (i < _count) {
                newBuf[i] = _buf[i];
                i = i + 1;
            }
            delete _buf;
        }
        _buf = newBuf;
        _cap = newCap;
    }
}

} // namespace io


