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
 * Abstract interface for reading a sequence of Unicode characters (code points / UTF-32).
 */
public interface Reader : public InputStream<char> {
}

/**
 * Abstract interface for writing a sequence of Unicode characters.
 */
public interface Writer : public OutputStream<char> {
    /**
     * Write a String.
     * @param str The string to write.
     * @return This writer.
     */
    write(str: const String&) : Writer&;

    /**
     * Write a character array.
     * @param str Character array.
     * @return This writer.
     */
    write(str: const char[]) : Writer&;

    /**
     * Write a newline.
     * @return This writer.
     */
    writeLine() : Writer&;

    /**
     * Write a String followed by a newline.
     * @param str The string to write.
     * @return This writer.
     */
    writeLine(str: const String&) : Writer&;

    /**
     * Write a character array followed by a newline.
     * @param str Character array.
     * @return This writer.
     */
    writeLine(str: const char[]) : Writer&;
}

/**
 * FilterReader delegates reader operations to an underlying Reader.
 */
public class FilterReader : public Reader {
protected:
    _in : Reader*;

public:
    /**
     * Construct a FilterReader borrowing the given reader.
     * The stream is held by pointer — the caller retains ownership.
     */
    FilterReader(in: Reader*) : _in(in) {}

    /** Default constructor. */
    FilterReader() : _in(null) {}

    override read() : ::k::Optional<char> {
        if (_in == null) return ::k::Optional<char>();
        return _in->read();
    }

    override read(buf: char[], off: unsigned int, len: unsigned int) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        if (_in == null) {
            err : ::k::io::StreamOutOfData = ::k::io::StreamOutOfData::Closed;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::error(err);
        }
        return _in->read(buf, off, len);
    }

    override read(buf: char[]) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return read(buf, 0u, (unsigned int) buf.size);
    }

    override skip(n: unsigned long) : unsigned long {
        if (_in == null) return 0uL;
        return _in->skip(n);
    }

    override available() : Expected<unsigned int, ::k::io::StreamOutOfData> {
        if (_in == null) {
            err : ::k::io::StreamOutOfData = ::k::io::StreamOutOfData::Closed;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::error(err);
        }
        return _in->available();
    }

    override close() {
        if (_in != null) {
            _in->close();
        }
    }
}

/**
 * FilterWriter delegates writer operations to an underlying Writer.
 */
public class FilterWriter : public Writer {
protected:
    _out : Writer*;

public:
    /**
     * Construct a FilterWriter borrowing the given writer.
     * The stream is held by pointer — the caller retains ownership.
     */
    FilterWriter(out: Writer*) : _out(out) {}

    /** Default constructor. */
    FilterWriter() : _out(null) {}

    override write(c: char) : OutputStream<char>& {
        if (_out != null) _out->write(c);
        return this;
    }

    override write(buf: const char[], off: unsigned int, len: unsigned int) : OutputStream<char>& {
        if (_out != null) _out->write(buf, off, len);
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
        if (_out != null) {
            _out->write(str);
        }
        return this;
    }

    override write(str: const char[]) : Writer& {
        len : unsigned int = str.size;
        if (len > 0u && str[len - 1u] == (char) 0) {
            --len;
        }
        if (len > 0u) {
            write(str, 0u, len);
        }
        return this;
    }

    override writeLine() : Writer& {
        write((char) 10);
        return this;
    }

    override writeLine(str: const String&) : Writer& {
        write(str);
        write((char) 10);
        return this;
    }

    override writeLine(str: const char[]) : Writer& {
        write(str);
        write((char) 10);
        return this;
    }

    override flush() : OutputStream<char>& {
        if (_out != null) _out->flush();
        return this;
    }

    override close() : OutputStream<char>& {
        if (_out != null) {
            _out->close();
        }
        return this;
    }
}

/**
 * In-memory character stream reader backed by a String or character buffer.
 */
public class StringReader : public Reader {
    _buf   : char[]!;
    _pos   : unsigned int;
    _count : unsigned int;

public:
    StringReader(str: const String&) : _buf(null), _pos(0u), _count(0u) {
        _count = str.size();
        _buf = str.toUtf32();
    }

    StringReader(src: const char[]) : _buf(null), _pos(0u), _count(0u) {
        len : unsigned int = src.size;
        if (len > 0u && src[len - 1u] == (char) 0) {
            --len;
        }
        _count = len;
        _buf = new char[len + 1u];
        i : unsigned int = 0u;
        while (i < len) {
            _buf[i] = src[i];
            ++i;
        }
        _buf[len] = (char) 0;
    }

    override read() : ::k::Optional<char> {
        if (_pos >= _count) {
            return ::k::Optional<char>();
        }
        c : char = _buf[_pos];
        ++_pos;
        return ::k::Optional<char>(c);
    }

    override read(buf: char[], off: unsigned int, len: unsigned int) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        if (_pos >= _count) {
            zero : unsigned int = 0u;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(zero);
        }
        avail : unsigned int = _count - _pos;
        actual : unsigned int = len;
        if (actual > avail) actual = avail;
        if (actual == 0u) {
            zero : unsigned int = 0u;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(zero);
        }
        i : unsigned int = 0u;
        while (i < actual) {
            buf[off + i] = _buf[_pos + i];
            ++i;
        }
        _pos += actual;
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(actual);
    }

    override read(buf: char[]) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return read(buf, 0u, (unsigned int) buf.size);
    }

    override skip(n: unsigned long) : unsigned long {
        avail : unsigned long = (unsigned long)(_count - _pos);
        actual : unsigned long = n;
        if (actual > avail) actual = avail;
        _pos += (unsigned int) actual;
        return actual;
    }

    override available() : Expected<unsigned int, ::k::io::StreamOutOfData> {
        avail : unsigned int = _count - _pos;
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(avail);
    }

    override close() {
        // In-memory stream
    }
}

/**
 * In-memory character stream writer backed by a StringBuilder.
 */
public class StringWriter : public Writer {
    _buf : StringBuilder;

public:
    StringWriter() {}

    override write(c: char) : OutputStream<char>& {
        _buf.appendChar(c);
        return this;
    }

    override write(buf: const char[], off: unsigned int, len: unsigned int) : OutputStream<char>& {
        i : unsigned int = 0u;
        while (i < len) {
            _buf.appendChar(buf[off + i]);
            ++i;
        }
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
        _buf.append(str);
        return this;
    }

    override write(str: const char[]) : Writer& {
        _buf.append(str);
        return this;
    }

    override writeLine() : Writer& {
        _buf.appendChar((char) 10);
        return this;
    }

    override writeLine(str: const String&) : Writer& {
        _buf.append(str);
        _buf.appendChar((char) 10);
        return this;
    }

    override writeLine(str: const char[]) : Writer& {
        _buf.append(str);
        _buf.appendChar((char) 10);
        return this;
    }

    override flush() : OutputStream<char>& {
        return this;
    }

    override close() : OutputStream<char>& {
        return this;
    }

    const toString() : String {
        return _buf.toString();
    }

    const size() : unsigned int {
        return _buf.size();
    }

    reset() {
        _buf.clear();
    }
}

} // namespace io
