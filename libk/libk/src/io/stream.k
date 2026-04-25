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

// ═════════════════════════════════════════════════════════════════════════════
// InputStream — Abstract interface for reading bytes.
//
// read() returns the next byte as an int (0–255), or -1 on end of stream.
// read(buf, off, len) reads up to len bytes into buf starting at off;
// returns the number of bytes actually read, or -1 on end of stream.
//
// Inspired by java.io.InputStream (without mark/reset).
// ═════════════════════════════════════════════════════════════════════════════
public interface InputStream {
    /**
     * Read a single byte.
     * Returns the byte value as an int in the range 0–255,
     * or -1 if the end of the stream has been reached.
     */
    read() : int;

    /**
     * Read up to `len` bytes into `buf` starting at offset `off`.
     * Returns the number of bytes actually read, or -1 if the end
     * of the stream has been reached before any byte was read.
     */
    read(buf: byte[], off: unsigned int, len: unsigned int) : int;

    /**
     * Read up to `buf.size` bytes into `buf`.
     * Returns the number of bytes actually read, or -1 if the end
     * of the stream has been reached before any byte was read.
     */
    read(buf: byte[]) : int;

    /**
     * Skip over and discard `n` bytes of data from this stream.
     * Returns the actual number of bytes skipped.
     */
    skip(n: unsigned long) : unsigned long;

    /**
     * Return the number of bytes that can be read without blocking.
     * Default semantic: returns 0 (no guarantee).
     */
    available() : int;

    /**
     * Close the stream and release any associated resources.
     */
    close();
}


// ═════════════════════════════════════════════════════════════════════════════
// OutputStream — Abstract interface for writing bytes.
//
// write(b) writes the low 8 bits of b.
// write(buf, off, len) writes len bytes from buf starting at off.
//
// Inspired by java.io.OutputStream (without mark/reset).
// ═════════════════════════════════════════════════════════════════════════════
public interface OutputStream {
    /**
     * Write a single byte. The byte to write is the low 8 bits of `b`.
     * Returns this stream for fluent chaining.
     */
    write(b: int) : OutputStream&;

    /**
     * Write `len` bytes from `buf` starting at offset `off`.
     * Returns this stream for fluent chaining.
     */
    write(buf: const byte[], off: unsigned int, len: unsigned int) : OutputStream&;

    /**
     * Write all bytes from `buf` (buf.size bytes).
     * Returns this stream for fluent chaining.
     */
    write(buf: const byte[]) : OutputStream&;

    /**
     * Flush any buffered output to the underlying destination.
     * Returns this stream for fluent chaining.
     */
    flush() : OutputStream&;

    /**
     * Flush and close the stream, releasing any associated resources.
     * Returns this stream for fluent chaining.
     */
    close() : OutputStream&;
}


// ═════════════════════════════════════════════════════════════════════════════
// ByteArrayInputStream — Reads bytes from an in-memory byte array.
//
// The stream owns a copy of the source data. Reads advance through the
// buffer sequentially; read() returns -1 when all bytes are consumed.
// ═════════════════════════════════════════════════════════════════════════════
public class ByteArrayInputStream : public InputStream {
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

    // ── InputStream implementation ───────────────────────────────────────

    read() : int {
        if (_pos >= _count) return -1;
        val : int = _buf[_pos] & 0xFF;
        _pos = _pos + 1;
        return val;
    }

    read(buf: byte[], off: unsigned int, len: unsigned int) : int {
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

    read(buf: byte[]) : int {
        return read(buf, 0, (unsigned int) buf.size);
    }

    skip(n: unsigned long) : unsigned long {
        avail : unsigned long = (unsigned long)(_count - _pos);
        actual : unsigned long = n;
        if (actual > avail) actual = avail;
        _pos = _pos + (int) actual;
        return actual;
    }

    available() : int {
        return _count - _pos;
    }

    close() {
        // Nothing to release for an in-memory stream.
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// ByteArrayOutputStream — Writes bytes to a growable in-memory byte array.
//
// Provides toByteArray() to retrieve a copy of the written data,
// size() to query the current byte count, and reset() to start over.
// ═════════════════════════════════════════════════════════════════════════════
public class ByteArrayOutputStream : public OutputStream {
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

    // ── OutputStream implementation ──────────────────────────────────────

    write(b: int) : OutputStream& {
        _ensureCap(1);
        _buf[_count] = (byte) b;
        _count = _count + 1;
        return this;
    }

    write(buf: const byte[], off: unsigned int, len: unsigned int) : OutputStream& {
        if (len == 0) return this;
        _ensureCap((int) len);
        i : unsigned int = 0;
        while (i < len) {
            _buf[_count + (int) i] = buf[off + i];
            i = i + 1;
        }
        _count = _count + (int) len;
        return this;
    }

    write(buf: const byte[]) : OutputStream& {
        return write(buf, 0, (unsigned int) buf.size);
    }

    flush() : OutputStream& {
        // No-op for in-memory stream.
        return this;
    }

    close() : OutputStream& {
        // No-op for in-memory stream.
        return this;
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
    writeTo(out: OutputStream&) {
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













