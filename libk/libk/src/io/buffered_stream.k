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
 * Buffer reads from an underlying input stream.
 *
 * `BufferedInputStream` reduces backend read calls by maintaining an internal
 * byte buffer.
 */
public class BufferedInputStream : public FilterInputStream<byte> {
    _bbuf   : byte[]!;
    _bpos   : int;
    _bcount : int;
    _bsize  : int;

public:
    /**
     * Construct with default buffer size (8192 bytes).
     *
     * @param input Wrapped input stream.
     */
    BufferedInputStream(input: InputStream<byte>*) : FilterInputStream(input), _bbuf(null), _bpos(0), _bcount(0), _bsize(8192) {
        _bbuf = new byte[8192];
    }

    /**
     * Construct with a caller-defined buffer size.
     *
     * @param input Wrapped input stream.
     * @param size Internal buffer size in bytes.
     */
    BufferedInputStream(input: InputStream<byte>*, size: int) : FilterInputStream(input), _bbuf(null), _bpos(0), _bcount(0), _bsize(size) {
        _bbuf = new byte[size];
    }

    // ── InputStream overrides ────────────────────────────────────────────

    override read() : ::k::Optional<byte> {
        if (_bpos >= _bcount) {
            _fill();
            if (_bpos >= _bcount) {
                return ::k::Optional<byte>();
            }
        }
        val : byte = _bbuf[_bpos];
        ++_bpos;
        result : ::k::Optional<byte>;
        result.set(val);
        return result;
    }

    override read(buf: byte[], off: unsigned int, len: unsigned int) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        if (len == 0) {
            zero : unsigned int = 0;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(zero);
        }
        totalRead : unsigned int = 0;
        remaining : unsigned int = len;
        dstOff : unsigned int = off;

        while (remaining > 0) {
            // If buffer is exhausted, refill
            if (_bpos >= _bcount) {
                // If request is larger than buffer, read directly
                if (remaining >= (unsigned int) _bsize) {
                    nResult : Expected<unsigned int, ::k::io::StreamOutOfData> = _in->read(buf, dstOff, remaining);
                    if (nResult.hasError()) {
                        if (totalRead > 0) {
                            return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(totalRead);
                        }
                        return nResult;
                    }
                    n : unsigned int = nResult.getResult();
                    if (n == 0) {
                        // EOS
                        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(totalRead);
                    }
                    totalRead += n;
                    return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(totalRead);
                }
                _fill();
                if (_bpos >= _bcount) {
                    // EOS after refill
                    return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(totalRead);
                }
            }
            // Copy from internal buffer
            avail : unsigned int = (unsigned int)(_bcount - _bpos);
            toCopy : unsigned int = remaining;
            if (toCopy > avail) toCopy = avail;
            i : unsigned int = 0;
            while (i < toCopy) {
                buf[dstOff + i] = _bbuf[(unsigned int) _bpos + i];
                ++i;
            }
            _bpos += (int) toCopy;
            dstOff += toCopy;
            remaining -= toCopy;
            totalRead += toCopy;
        }
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(totalRead);
    }

    override read(buf: byte[]) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return read(buf, 0, (unsigned int) buf.size);
    }

    override available() : Expected<unsigned int, ::k::io::StreamOutOfData> {
        buffered : unsigned int = (unsigned int) (_bcount - _bpos);
        underlying : Expected<unsigned int, ::k::io::StreamOutOfData> = _in->available();
        if (underlying.hasError()) {
            return underlying;
        }
        total : unsigned int = buffered + underlying.getResult();
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(total);
    }

    override skip(n: unsigned long) : unsigned long {
        buffered : unsigned long = (unsigned long)(_bcount - _bpos);
        if (buffered >= n) {
            _bpos += (int) n;
            return n;
        }
        // Skip what's in buffer, then delegate the rest
        _bpos = _bcount;
        remaining : unsigned long = n - buffered;
        skipped : unsigned long = _in->skip(remaining);
        return buffered + skipped;
    }

    override close() {
        _in->close();
    }

private:
    /** Fill the internal buffer from the underlying stream. */
    _fill() {
        _bpos = 0;
        _bcount = 0;
        nResult : Expected<unsigned int, ::k::io::StreamOutOfData> = _in->read(_bbuf, 0, (unsigned int) _bsize);
        if (nResult.hasResult()) {
            n : unsigned int = nResult.getResult();
            if (n > 0) {
                _bcount = (int) n;
            }
        }
    }
}


/**
 * Buffer writes to an underlying output stream.
 *
 * `BufferedOutputStream` accumulates bytes in memory and flushes them in
 * larger chunks to reduce backend write calls.
 */
public class BufferedOutputStream : public FilterOutputStream<byte> {
    _bbuf   : byte[]!;
    _bcount : int;
    _bsize  : int;

public:
    /**
     * Construct with default buffer size (8192 bytes).
     *
     * @param output Wrapped output stream.
     */
    BufferedOutputStream(output: OutputStream<byte>*) : FilterOutputStream(output), _bbuf(null), _bcount(0), _bsize(8192) {
        _bbuf = new byte[8192];
    }

    /**
     * Construct with a caller-defined buffer size.
     *
     * @param output Wrapped output stream.
     * @param size Internal buffer size in bytes.
     */
    BufferedOutputStream(output: OutputStream<byte>*, size: int) : FilterOutputStream(output), _bbuf(null), _bcount(0), _bsize(size) {
        _bbuf = new byte[size];
    }

    // ── OutputStream overrides ───────────────────────────────────────────

    override write(b: byte) : OutputStream<byte>& {
        if (_bcount >= _bsize) {
            _flushBuffer();
        }
        _bbuf[_bcount] = b;
        ++_bcount;
        return this;
    }

    override write(buf: const byte[], off: unsigned int, len: unsigned int) : OutputStream<byte>& {
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
            ++i;
        }
        _bcount += (int) len;
        return this;
    }

    override write(buf: const byte[]) : OutputStream<byte>& {
        return write(buf, 0, (unsigned int) buf.size);
    }

    override flush() : OutputStream<byte>& {
        _flushBuffer();
        _out->flush();
        return this;
    }

    override close() : OutputStream<byte>& {
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









