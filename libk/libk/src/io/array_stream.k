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
 * Read values from an in-memory array.
 *
 * The stream stores an internal copy of the source values and advances a read
 * cursor as data is consumed.
 */
template<typename T>
public class ArrayInputStream : public InputStream<T> {
    _buf   : T[]!;
    _pos   : int;
    _count : int;

public:
    /** Default constructor (empty stream). */
    ArrayInputStream() : _buf(null), _pos(0), _count(0) {}

    /**
     * Construct an ArrayInputStream from an array pointer.
     * The content is copied so the stream keeps independent storage.
     * `size` is the number of valid entries to read from `buf`.
     */
    ArrayInputStream(buf: T[]*, size: int) : _buf(null), _pos(0), _count(0) {
        if (size <= 0 || buf == null) {
            _buf = new T[0];
            return;
        }

        _buf = new T[size];
        i : int = 0;
        while (i < size) {
            _buf[i] = buf[i];
            i = i + 1;
        }
        _count = size;
    }

    // ── InputStream implementation ───────────────────────────────────────

    read() : Optional<T> {
        if (_pos >= _count) {
            return Optional<T>();
        }
        val : T = _buf[_pos];
        _pos = _pos + 1;
        return Optional<T>(val);
    }

    read(buf: T[], off: unsigned int, len: unsigned int) : Expected<unsigned int, int> {
        if (_pos >= _count) {
            // End-of-stream: return 0 bytes read (not an error — stream is open)
            zero : unsigned int = 0;
            return Expected<unsigned int, int>::expected(zero);
        }
        avail : unsigned int = (unsigned int) (_count - _pos);
        actual : unsigned int = len;
        if (actual > avail) actual = avail;
        if (actual == 0) {
            zero : unsigned int = 0;
            return Expected<unsigned int, int>::expected(zero);
        }
        i : unsigned int = 0;
        while (i < actual) {
            buf[off + i] = _buf[(unsigned int) _pos + i];
            i = i + 1;
        }
        _pos = _pos + (int) actual;
        return Expected<unsigned int, int>::expected(actual);
    }

    read(buf: T[]) : Expected<unsigned int, int> {
        return read(buf, 0, (unsigned int) buf.size);
    }

    skip(n: unsigned long) : unsigned long {
        avail : unsigned long = (unsigned long) (_count - _pos);
        actual : unsigned long = n;
        if (actual > avail) actual = avail;
        _pos = _pos + (int) actual;
        return actual;
    }

    available() : Expected<unsigned int, int> {
        avail : unsigned int = (unsigned int) (_count - _pos);
        return Expected<unsigned int, int>::expected(avail);
    }

    close() {
        // Nothing to release for an in-memory stream.
    }
}


/**
 * Write values to a growable in-memory array.
 *
 * The stream accumulates values and exposes helpers to query size,
 * reset content, and retrieve a copy of the accumulated data.
 */
template<typename T>
public class ArrayOutputStream : public OutputStream<T> {
    _buf   : T[]!;
    _count : int;
    _cap   : int;

public:
    /** Default constructor with initial capacity of 32 entries. */
    ArrayOutputStream() : _buf(null), _count(0), _cap(0) {}

    /** Constructor with specified initial capacity. */
    ArrayOutputStream(capacity: int) : _buf(null), _count(0), _cap(0) {
        if (capacity > 0) {
            _buf = new T[capacity];
            _cap = capacity;
        }
    }

    // ── OutputStream implementation ──────────────────────────────────────

    write(v: T) : OutputStream<T>& {
        _ensureCap(1);
        _buf[_count] = v;
        _count = _count + 1;
        return this;
    }

    write(buf: const T[], off: unsigned int, len: unsigned int) : OutputStream<T>& {
        if (len == 0) {
            return this;
        }
        _ensureCap((int) len);
        i : unsigned int = 0;
        while (i < len) {
            _buf[_count + (int) i] = buf[off + i];
            i = i + 1;
        }
        _count = _count + (int) len;
        return this;
    }

    write(buf: const T[]) : OutputStream<T>& {
        return write(buf, 0, (unsigned int) buf.size);
    }

    flush() : OutputStream<T>& {
        // No-op for in-memory stream.
        return this;
    }

    close() : OutputStream<T>& {
        // No-op for in-memory stream.
        return this;
    }

    // ── Additional methods ───────────────────────────────────────────────

    /** Return the number of entries written so far. */
    const size() : int {
        return _count;
    }

    /**
     * Return a new array containing a copy of the written data.
     * The caller takes ownership of the returned array.
     */
    toArray() : T[]! {
        result : T[]! = new T[_count];
        i : int = 0;
        while (i < _count) {
            result[i] = _buf[i];
            i = i + 1;
        }
        return result;
    }

    /** Reset the entry count to zero. The internal buffer is retained. */
    reset() {
        _count = 0;
    }

    /**
     * Write the entire content of this stream to another output stream.
     */
    writeTo(out: OutputStream<T>&) {
        if (_count > 0) {
            out.write(_buf, 0, (unsigned int) _count);
        }
    }

private:
    /** Ensure the internal buffer has room for `extra` more entries. */
    _ensureCap(extra: int) {
        needed : int = _count + extra;
        if (needed <= _cap) return;
        newCap : int = _cap * 2;
        if (newCap < 32) newCap = 32;
        if (newCap < needed) newCap = needed;
        newBuf : T[]! = new T[newCap];
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



