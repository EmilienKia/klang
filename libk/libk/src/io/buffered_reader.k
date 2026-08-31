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
 * Buffer character reads from an underlying Reader for efficiency and lookahead.
 */
public class BufferedReader : public FilterReader {
    _cbuf   : char[]!;
    _cpos   : int;
    _ccount : int;
    _csize  : int;

public:
    /**
     * Construct a BufferedReader borrowing the given reader with default buffer size (4096 chars).
     */
    BufferedReader(input: Reader*, size: int)
        : FilterReader(input), _cbuf(null), _cpos(0), _ccount(0), _csize(size) {
        if (_csize <= 0) _csize = 4096;
        _cbuf = new char[_csize];
    }

    BufferedReader(input: Reader*)
        : FilterReader(input), _cbuf(null), _cpos(0), _ccount(0), _csize(4096) {
        _cbuf = new char[4096];
    }

    BufferedReader() : FilterReader(), _cbuf(null), _cpos(0), _ccount(0), _csize(4096) {
        _cbuf = new char[4096];
    }

    ~BufferedReader() {
        close();
    }

    /**
     * Look at the next character without consuming it.
     */
    peek() : ::k::Optional<char> {
        if (_cpos >= _ccount) {
            _fill();
            if (_cpos >= _ccount) {
                return ::k::Optional<char>();
            }
        }
        val : char = _cbuf[_cpos];
        return ::k::Optional<char>(val);
    }

    /**
     * Read a line of text. A line is considered to be terminated by any one of
     * a line feed ('\n'), a carriage return ('\r'), or a carriage return followed
     * immediately by a line feed ("\r\n").
     *
     * @return A String containing the line contents without termination, or empty Optional at EOF.
     */
    readLine() : ::k::Optional<String> {
        firstChar : ::k::Optional<char> = read();
        if (!firstChar.hasValue()) {
            return ::k::Optional<String>();
        }

        sb : StringBuilder;
        c : char = firstChar.get();
        while (true) {
            if (c == (char) 10) { // '\n'
                break;
            } else if (c == (char) 13) { // '\r'
                // Check if followed by '\n'
                p : ::k::Optional<char> = peek();
                if (p.hasValue() && p.get() == (char) 10) {
                    read(); // Consume '\n'
                }
                break;
            }

            sb.appendChar(c);
            nextChar : ::k::Optional<char> = read();
            if (!nextChar.hasValue()) {
                break;
            }
            c = nextChar.get();
        }

        return ::k::Optional<String>(sb.toString());
    }

    override read() : ::k::Optional<char> {
        if (_cpos >= _ccount) {
            _fill();
            if (_cpos >= _ccount) {
                return ::k::Optional<char>();
            }
        }
        val : char = _cbuf[_cpos];
        ++_cpos;
        return ::k::Optional<char>(val);
    }

    override read(buf: char[], off: unsigned int, len: unsigned int) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        if (len == 0u) {
            zero : unsigned int = 0u;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(zero);
        }
        totalRead : unsigned int = 0u;
        remaining : unsigned int = len;
        dstOff : unsigned int = off;

        while (remaining > 0u) {
            if (_cpos >= _ccount) {
                if (remaining >= (unsigned int) _csize) {
                    if (_in == null) {
                        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(totalRead);
                    }
                    nResult : Expected<unsigned int, ::k::io::StreamOutOfData> = _in->read(buf, dstOff, remaining);
                    if (nResult.hasError()) {
                        if (totalRead > 0u) {
                            return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(totalRead);
                        }
                        return nResult;
                    }
                    n : unsigned int = nResult.getResult();
                    if (n == 0u) {
                        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(totalRead);
                    }
                    totalRead += n;
                    return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(totalRead);
                }
                _fill();
                if (_cpos >= _ccount) {
                    return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(totalRead);
                }
            }

            avail : unsigned int = (unsigned int) (_ccount - _cpos);
            toCopy : unsigned int = remaining;
            if (toCopy > avail) toCopy = avail;
            i : unsigned int = 0u;
            while (i < toCopy) {
                buf[dstOff + i] = _cbuf[(unsigned int) _cpos + i];
                ++i;
            }
            _cpos += (int) toCopy;
            dstOff += toCopy;
            remaining -= toCopy;
            totalRead += toCopy;
        }
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(totalRead);
    }

    override read(buf: char[]) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return read(buf, 0u, (unsigned int) buf.size);
    }

    override available() : Expected<unsigned int, ::k::io::StreamOutOfData> {
        buffered : unsigned int = (unsigned int) (_ccount - _cpos);
        if (_in == null) {
            return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(buffered);
        }
        underlying : Expected<unsigned int, ::k::io::StreamOutOfData> = _in->available();
        if (underlying.hasError()) {
            if (buffered > 0u) return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(buffered);
            return underlying;
        }
        total : unsigned int = buffered + underlying.getResult();
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(total);
    }

    override skip(n: unsigned long) : unsigned long {
        buffered : unsigned long = (unsigned long) (_ccount - _cpos);
        if (buffered >= n) {
            _cpos += (int) n;
            return n;
        }
        _cpos = _ccount;
        remaining : unsigned long = n - buffered;
        if (_in == null) return buffered;
        skipped : unsigned long = _in->skip(remaining);
        return buffered + skipped;
    }

    override close() {
        if (_cbuf != null) {
            delete _cbuf;
            _cbuf = null;
        }
        FilterReader::close();
    }

private:
    _fill() {
        _cpos = 0;
        _ccount = 0;
        if (_in == null || _cbuf == null) return;
        nResult : Expected<unsigned int, ::k::io::StreamOutOfData> = _in->read(_cbuf, 0u, (unsigned int) _csize);
        if (nResult.hasResult()) {
            n : unsigned int = nResult.getResult();
            if (n > 0u) {
                _ccount = (int) n;
            }
        }
    }
}

/**
 * Buffer character writes to an underlying Writer.
 */
public class BufferedWriter : public FilterWriter {
    _cbuf   : char[]!;
    _ccount : int;
    _csize  : int;

public:
    BufferedWriter(output: Writer*, size: int)
        : FilterWriter(output), _cbuf(null), _ccount(0), _csize(size) {
        if (_csize <= 0) _csize = 4096;
        _cbuf = new char[_csize];
    }

    BufferedWriter(output: Writer*)
        : FilterWriter(output), _cbuf(null), _ccount(0), _csize(4096) {
        _cbuf = new char[4096];
    }

    BufferedWriter() : FilterWriter(), _cbuf(null), _ccount(0), _csize(4096) {
        _cbuf = new char[4096];
    }

    ~BufferedWriter() {
        close();
    }

    newLine() : BufferedWriter& {
        write((char) 10);
        return this;
    }

    override write(c: char) : OutputStream<char>& {
        if (_ccount >= _csize) {
            _flushBuffer();
        }
        if (_cbuf != null) {
            _cbuf[_ccount] = c;
            ++_ccount;
        }
        return this;
    }

    override write(buf: const char[], off: unsigned int, len: unsigned int) : OutputStream<char>& {
        if (len == 0u) return this;
        if (len >= (unsigned int) _csize) {
            _flushBuffer();
            if (_out != null) {
                _out->write(buf, off, len);
            }
            return this;
        }
        if (_ccount + (int) len > _csize) {
            _flushBuffer();
        }
        i : unsigned int = 0u;
        while (i < len) {
            _cbuf[_ccount + (int) i] = buf[off + i];
            ++i;
        }
        _ccount += (int) len;
        return this;
    }

    override write(buf: const char[]) : OutputStream<char>& {
        len : unsigned int = buf.size;
        if (len > 0u && buf[len - 1u] == (char) 0) {
            --len;
        }
        if (len > 0u) {
            write(buf, 0u, len);
        }
        return this;
    }

    override write(str: const String&) : Writer& {
        u32 : char[]! = str.toUtf32();
        n : unsigned int = str.size();
        write(u32, 0u, n);
        return this;
    }

    override write(str: const char[]) : Writer& {
        write(str);
        return this;
    }

    override flush() : OutputStream<char>& {
        _flushBuffer();
        if (_out != null) {
            _out->flush();
        }
        return this;
    }

    override close() : OutputStream<char>& {
        flush();
        if (_cbuf != null) {
            delete _cbuf;
            _cbuf = null;
        }
        FilterWriter::close();
        return this;
    }

private:
    _flushBuffer() {
        if (_ccount > 0 && _out != null && _cbuf != null) {
            _out->write(_cbuf, 0u, (unsigned int) _ccount);
            _ccount = 0;
        }
    }
}

/**
 * Filter that simplifies line-by-line reading from any Reader.
 */
public class LineReader : public FilterReader {
    _br : BufferedReader!;

public:
    LineReader(input: Reader*) : FilterReader(input), _br(null) {
        _br = new BufferedReader(input);
    }

    ~LineReader() {
        close();
    }

    readLine() : ::k::Optional<String> {
        if (_br == null) return ::k::Optional<String>();
        return _br->readLine();
    }

    override read() : ::k::Optional<char> {
        if (_br == null) return ::k::Optional<char>();
        return _br->read();
    }

    override read(buf: char[], off: unsigned int, len: unsigned int) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        if (_br == null) {
            err : ::k::io::StreamOutOfData = ::k::io::StreamOutOfData::Closed;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::error(err);
        }
        return _br->read(buf, off, len);
    }

    override read(buf: char[]) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return read(buf, 0u, (unsigned int) buf.size);
    }

    override available() : Expected<unsigned int, ::k::io::StreamOutOfData> {
        if (_br == null) {
            err : ::k::io::StreamOutOfData = ::k::io::StreamOutOfData::Closed;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::error(err);
        }
        return _br->available();
    }

    override close() {
        if (_br != null) {
            _br->close();
            delete _br;
            _br = null;
        }
    }
}

} // namespace io
