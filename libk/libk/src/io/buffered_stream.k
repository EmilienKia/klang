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
// BufferedInputStream — Adds an internal buffer to an underlying InputStream
// to reduce the number of actual read calls.
//
// Default buffer size is 8192 bytes.
//
// Inspired by java.io.BufferedInputStream (without mark/reset).
// ═════════════════════════════════════════════════════════════════════════════
public class BufferedInputStream : public FilterInputStream {
    _bbuf   : byte[]!;
    _bpos   : int;
    _bcount : int;
    _bsize  : int;

public:
    /** Construct with default buffer size (8192). */
    BufferedInputStream(input: InputStream*) : FilterInputStream(input), _bbuf(null), _bpos(0), _bcount(0), _bsize(8192) {
        _bbuf = new byte[8192];
    }

    /** Construct with explicit buffer size. */
    BufferedInputStream(input: InputStream*, size: int) : FilterInputStream(input), _bbuf(null), _bpos(0), _bcount(0), _bsize(size) {
        _bbuf = new byte[size];
    }

    // ── InputStream overrides ────────────────────────────────────────────

    read() : int {
        if (_bpos >= _bcount) {
            _fill();
            if (_bpos >= _bcount) return -1;
        }
        val : int = ((int) _bbuf[_bpos]) & 0xFF;
        _bpos = _bpos + 1;
        return val;
    }

    read(buf: byte[], off: unsigned int, len: unsigned int) : int {
        if (len == 0) return 0;
        totalRead : int = 0;
        remaining : unsigned int = len;
        dstOff : unsigned int = off;

        while (remaining > 0) {
            // If buffer is exhausted, refill
            if (_bpos >= _bcount) {
                // If request is larger than buffer, read directly
                if (remaining >= (unsigned int) _bsize) {
                    n : int = _in->read(buf, dstOff, remaining);
                    if (n <= 0) {
                        if (totalRead == 0) return -1;
                        return totalRead;
                    }
                    totalRead = totalRead + n;
                    return totalRead;
                }
                _fill();
                if (_bpos >= _bcount) {
                    if (totalRead == 0) return -1;
                    return totalRead;
                }
            }
            // Copy from internal buffer
            avail : unsigned int = (unsigned int)(_bcount - _bpos);
            toCopy : unsigned int = remaining;
            if (toCopy > avail) toCopy = avail;
            i : unsigned int = 0;
            while (i < toCopy) {
                buf[dstOff + i] = _bbuf[(unsigned int) _bpos + i];
                i = i + 1;
            }
            _bpos = _bpos + (int) toCopy;
            dstOff = dstOff + toCopy;
            remaining = remaining - toCopy;
            totalRead = totalRead + (int) toCopy;
        }
        return totalRead;
    }

    read(buf: byte[]) : int {
        return read(buf, 0, (unsigned int) buf.size);
    }

    available() : int {
        buffered : int = _bcount - _bpos;
        underlying : int = _in->available();
        return buffered + underlying;
    }

    skip(n: unsigned long) : unsigned long {
        buffered : unsigned long = (unsigned long)(_bcount - _bpos);
        if (buffered >= n) {
            _bpos = _bpos + (int) n;
            return n;
        }
        // Skip what's in buffer, then delegate the rest
        _bpos = _bcount;
        remaining : unsigned long = n - buffered;
        skipped : unsigned long = _in->skip(remaining);
        return buffered + skipped;
    }

    close() {
        _in->close();
    }

private:
    /** Fill the internal buffer from the underlying stream. */
    _fill() {
        _bpos = 0;
        _bcount = 0;
        n : int = _in->read(_bbuf, 0, (unsigned int) _bsize);
        if (n > 0) {
            _bcount = n;
        }
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// BufferedOutputStream — Adds an internal buffer to an underlying OutputStream
// to reduce the number of actual write calls.
//
// Default buffer size is 8192 bytes.
//
// Inspired by java.io.BufferedOutputStream.
// ═════════════════════════════════════════════════════════════════════════════
public class BufferedOutputStream : public FilterOutputStream {
    _bbuf   : byte[]!;
    _bcount : int;
    _bsize  : int;

public:
    /** Construct with default buffer size (8192). */
    BufferedOutputStream(output: OutputStream*) : FilterOutputStream(output), _bbuf(null), _bcount(0), _bsize(8192) {
        _bbuf = new byte[8192];
    }

    /** Construct with explicit buffer size. */
    BufferedOutputStream(output: OutputStream*, size: int) : FilterOutputStream(output), _bbuf(null), _bcount(0), _bsize(size) {
        _bbuf = new byte[size];
    }

    // ── OutputStream overrides ───────────────────────────────────────────

    write(b: int) : OutputStream& {
        if (_bcount >= _bsize) {
            _flushBuffer();
        }
        _bbuf[_bcount] = (byte) b;
        _bcount = _bcount + 1;
        return this;
    }

    write(buf: const byte[], off: unsigned int, len: unsigned int) : OutputStream& {
        if (len == 0) return this;
        // If data is larger than buffer, flush and write directly
        if (len >= (unsigned int) _bsize) {
            _flushBuffer();
            _out->write(buf, off, len);
            return this;
        }
        // If data would overflow buffer, flush first
        if (_bcount + (int) len > _bsize) {
            _flushBuffer();
        }
        // Copy into buffer
        i : unsigned int = 0;
        while (i < len) {
            _bbuf[_bcount + (int) i] = buf[off + i];
            i = i + 1;
        }
        _bcount = _bcount + (int) len;
        return this;
    }

    write(buf: const byte[]) : OutputStream& {
        return write(buf, 0, (unsigned int) buf.size);
    }

    flush() : OutputStream& {
        _flushBuffer();
        _out->flush();
        return this;
    }

    close() : OutputStream& {
        flush();
        _out->close();
        return this;
    }

private:
    /** Write the internal buffer to the underlying stream and reset. */
    _flushBuffer() {
        if (_bcount > 0) {
            _out->write(_bbuf, 0, (unsigned int) _bcount);
            _bcount = 0;
        }
    }
}

} // namespace io













