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
 * Character reader that decodes a stream of UTF-8 encoded bytes into Unicode code points.
 */
public class Utf8Reader : public Reader {
    _byteInOwner   : InputStream<byte>!;
    _validator     : Utf8Validator!;
    _customIn      : ValidUtf8Input*;
    _customInOwner : ValidUtf8Input!;

public:
    /**
     * Construct a Utf8Reader borrowing the given byte stream.
     */
    Utf8Reader(input: InputStream<byte>*, action: CodingErrorAction)
        : _byteInOwner(null), _validator(null), _customIn(null), _customInOwner(null) {
        _validator = new Utf8Validator(input, action);
    }

    Utf8Reader(input: InputStream<byte>*)
        : _byteInOwner(null), _validator(null), _customIn(null), _customInOwner(null) {
        _validator = new Utf8Validator(input, CodingErrorAction::Report);
    }

    /**
     * Construct a Utf8Reader taking ownership of the given byte stream.
     */
    Utf8Reader(input: InputStream<byte>!, action: CodingErrorAction)
        : _byteInOwner(input), _validator(null), _customIn(null), _customInOwner(null) {
        _validator = new Utf8Validator(_byteInOwner, action);
    }

    Utf8Reader(input: InputStream<byte>!)
        : _byteInOwner(input), _validator(null), _customIn(null), _customInOwner(null) {
        _validator = new Utf8Validator(_byteInOwner, CodingErrorAction::Report);
    }

    /**
     * Construct a Utf8Reader wrapping a pre-validated UTF-8 input stream.
     */
    Utf8Reader(input: ValidUtf8Input*)
        : _byteInOwner(null), _validator(null), _customIn(input), _customInOwner(null) {}

    /**
     * Construct a Utf8Reader taking ownership of a pre-validated UTF-8 input stream.
     */
    Utf8Reader(input: ValidUtf8Input!)
        : _byteInOwner(null), _validator(null), _customIn(null), _customInOwner(input) {
        _customIn = _customInOwner;
    }

    ~Utf8Reader() {
        close();
    }

    override read() : ::k::Optional<char> {
        firstByte : ::k::Optional<unsigned byte> = _readUnit();
        if (!firstByte.hasValue()) {
            return ::k::Optional<char>();
        }

        b0 : unsigned int = (unsigned int) firstByte.get();
        if (b0 < 0x80u) {
            return ::k::Optional<char>((char) b0);
        } else if (b0 < 0xE0u) {
            b1 : unsigned int = (unsigned int) _readUnit().get();
            cp : char = (char) (((b0 & 0x1Fu) << 6) | (b1 & 0x3Fu));
            return ::k::Optional<char>(cp);
        } else if (b0 < 0xF0u) {
            b1 : unsigned int = (unsigned int) _readUnit().get();
            b2 : unsigned int = (unsigned int) _readUnit().get();
            cp : char = (char) (((b0 & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu));
            return ::k::Optional<char>(cp);
        } else {
            b1 : unsigned int = (unsigned int) _readUnit().get();
            b2 : unsigned int = (unsigned int) _readUnit().get();
            b3 : unsigned int = (unsigned int) _readUnit().get();
            cp : char = (char) (((b0 & 0x07u) << 18) | ((b1 & 0x3Fu) << 12) | ((b2 & 0x3Fu) << 6) | (b3 & 0x3Fu));
            return ::k::Optional<char>(cp);
        }
    }

    override read(buf: char[], off: unsigned int, len: unsigned int) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        if (len == 0u) {
            zero : unsigned int = 0u;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(zero);
        }
        count : unsigned int = 0u;
        while (count < len) {
            next : ::k::Optional<char> = read();
            if (!next.hasValue()) {
                break;
            }
            buf[off + count] = next.get();
            ++count;
        }
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(count);
    }

    override read(buf: char[]) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return read(buf, 0u, (unsigned int) buf.size);
    }

    override skip(n: unsigned long) : unsigned long {
        skipped : unsigned long = 0uL;
        while (skipped < n) {
            next : ::k::Optional<char> = read();
            if (!next.hasValue()) {
                break;
            }
            ++skipped;
        }
        return skipped;
    }

    override available() : Expected<unsigned int, ::k::io::StreamOutOfData> {
        if (_validator != null) {
            return _validator->available();
        }
        if (_customIn != null) {
            return _customIn->available();
        }
        err : ::k::io::StreamOutOfData = ::k::io::StreamOutOfData::Closed;
        return Expected<unsigned int, ::k::io::StreamOutOfData>::error(err);
    }

    override close() {
        if (_validator != null) {
            _validator->close();
            delete _validator;
            _validator = null;
        }
        if (_customIn != null) {
            _customIn->close();
        }
        if (_customInOwner != null) {
            delete _customInOwner;
            _customInOwner = null;
            _customIn = null;
        }
        if (_byteInOwner != null) {
            delete _byteInOwner;
            _byteInOwner = null;
        }
    }

private:
    _readUnit() : ::k::Optional<unsigned byte> {
        if (_validator != null) {
            return _validator->read();
        }
        if (_customIn != null) {
            return _customIn->read();
        }
        return ::k::Optional<unsigned byte>();
    }
}

/**
 * Character writer that encodes Unicode code points into a UTF-8 byte stream.
 */
public class Utf8Writer : public Writer {
    _out      : OutputStream<byte>*;
    _outOwner : OutputStream<byte>!;
    _action   : CodingErrorAction;
    _offset   : unsigned long;

public:
    /**
     * Construct a Utf8Writer borrowing the given byte output stream.
     */
    Utf8Writer(output: OutputStream<byte>*, action: CodingErrorAction)
        : _out(output), _outOwner(null), _action(action), _offset(0uL) {}

    Utf8Writer(output: OutputStream<byte>*)
        : _out(output), _outOwner(null), _action(CodingErrorAction::Report), _offset(0uL) {}

    /**
     * Construct a Utf8Writer taking ownership of the given byte output stream.
     */
    Utf8Writer(output: OutputStream<byte>!, action: CodingErrorAction)
        : _out(null), _outOwner(output), _action(action), _offset(0uL) {
        _out = _outOwner;
    }

    Utf8Writer(output: OutputStream<byte>!)
        : _out(null), _outOwner(output), _action(CodingErrorAction::Report), _offset(0uL) {
        _out = _outOwner;
    }

    ~Utf8Writer() {
        close();
    }

    override write(c: char) : OutputStream<char>& {
        if (_out == null) return this;

        if (c < 0x80u) {
            _out->write((byte) c);
            ++_offset;
        } else if (c < 0x800u) {
            b0 : byte = (byte) (0xC0u | (c >> 6));
            b1 : byte = (byte) (0x80u | (c & 0x3Fu));
            _out->write(b0);
            _out->write(b1);
            _offset += 2uL;
        } else if (c < 0x10000u) {
            if (c >= 0xD800u && c <= 0xDFFFu) {
                _handleUnmappable(c);
            } else {
                b0 : byte = (byte) (0xE0u | (c >> 12));
                b1 : byte = (byte) (0x80u | ((c >> 6) & 0x3Fu));
                b2 : byte = (byte) (0x80u | (c & 0x3Fu));
                _out->write(b0);
                _out->write(b1);
                _out->write(b2);
                _offset += 3uL;
            }
        } else if (c <= 0x10FFFFu) {
            b0 : byte = (byte) (0xF0u | (c >> 18));
            b1 : byte = (byte) (0x80u | ((c >> 12) & 0x3Fu));
            b2 : byte = (byte) (0x80u | ((c >> 6) & 0x3Fu));
            b3 : byte = (byte) (0x80u | (c & 0x3Fu));
            _out->write(b0);
            _out->write(b1);
            _out->write(b2);
            _out->write(b3);
            _offset += 4uL;
        } else {
            _handleUnmappable(c);
        }
        return this;
    }

    override write(buf: const char[], off: unsigned int, len: unsigned int) : OutputStream<char>& {
        i : unsigned int = 0u;
        while (i < len) {
            write(buf[off + i]);
            ++i;
        }
        return this;
    }

    override write(buf: const char[]) : OutputStream<char>& {
        return write(buf, 0u, (unsigned int) buf.size);
    }

    override write(str: const String&) : Writer& {
        if (_out == null) return this;
        // Fast path: if String is UTF-8 encoded
        if (str.rawEncoding() == Encoding::UTF8) {
            u8 : unsigned byte[]! = str.toUtf8();
            n : unsigned int = u8.size;
            if (n > 0u && u8[n - 1u] == (unsigned byte) 0) {
                --n;
            }
            if (n > 0u) {
                i : unsigned int = 0u;
                while (i < n) {
                    _out->write((byte) u8[i]);
                    ++i;
                }
                _offset += (unsigned long) n;
            }
            return this;
        }

        // Generic path
        u32 : char[]! = str.toUtf32();
        n : unsigned int = str.size();
        write(u32, 0u, n);
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
        if (_outOwner != null) {
            delete _outOwner;
            _outOwner = null;
            _out = null;
        }
        return this;
    }

private:
    _handleUnmappable(c: char) {
        if (_action == CodingErrorAction::Report) {
            throw UnmappableCharacterException(c, _offset);
        } else if (_action == CodingErrorAction::Replace) {
            // Write U+FFFD (0xEF, 0xBF, 0xBD)
            _out->write((byte) 0xEFu);
            _out->write((byte) 0xBFu);
            _out->write((byte) 0xBDu);
            _offset += 3uL;
        }
    }
}

/**
 * Character reader that decodes a stream of UTF-16 code units into Unicode code points.
 */
public class Utf16Reader : public Reader {
    _in      : InputStream<byte>*;
    _inOwner : InputStream<byte>!;
    _order   : ByteOrder;
    _action  : CodingErrorAction;
    _offset  : unsigned long;

public:
    Utf16Reader(input: InputStream<byte>*, order: ByteOrder, action: CodingErrorAction)
        : _in(input), _inOwner(null), _order(order), _action(action), _offset(0uL) {}

    Utf16Reader(input: InputStream<byte>*, order: ByteOrder)
        : _in(input), _inOwner(null), _order(order), _action(CodingErrorAction::Report), _offset(0uL) {}

    Utf16Reader(input: InputStream<byte>*)
        : _in(input), _inOwner(null), _order(ByteOrder::BigEndian), _action(CodingErrorAction::Report), _offset(0uL) {}

    Utf16Reader(input: InputStream<byte>!, order: ByteOrder, action: CodingErrorAction)
        : _in(null), _inOwner(input), _order(order), _action(action), _offset(0uL) {
        _in = _inOwner;
    }

    Utf16Reader(input: InputStream<byte>!, order: ByteOrder)
        : _in(null), _inOwner(input), _order(order), _action(CodingErrorAction::Report), _offset(0uL) {
        _in = _inOwner;
    }

    Utf16Reader(input: InputStream<byte>!)
        : _in(null), _inOwner(input), _order(ByteOrder::BigEndian), _action(CodingErrorAction::Report), _offset(0uL) {
        _in = _inOwner;
    }

    ~Utf16Reader() {
        close();
    }

    override read() : ::k::Optional<char> {
        firstUnit : ::k::Optional<unsigned short> = _readUnit();
        if (!firstUnit.hasValue()) {
            return ::k::Optional<char>();
        }

        u1 : unsigned int = (unsigned int) firstUnit.get();
        if (u1 >= 0xD800u && u1 <= 0xDBFFu) {
            // High surrogate -> read low surrogate
            secondUnit : ::k::Optional<unsigned short> = _readUnit();
            if (!secondUnit.hasValue()) {
                _handleMalformed(2u);
                return ::k::Optional<char>((char) 0xFFFDu);
            }
            u2 : unsigned int = (unsigned int) secondUnit.get();
            if (u2 < 0xDC00u || u2 > 0xDFFFu) {
                _handleMalformed(4u);
                return ::k::Optional<char>((char) 0xFFFDu);
            }
            cp : char = (char) (0x10000u + (((u1 - 0xD800u) << 10) | (u2 - 0xDC00u)));
            return ::k::Optional<char>(cp);
        } else if (u1 >= 0xDC00u && u1 <= 0xDFFFu) {
            // Unpaired low surrogate
            _handleMalformed(2u);
            return ::k::Optional<char>((char) 0xFFFDu);
        }

        return ::k::Optional<char>((char) u1);
    }

    override read(buf: char[], off: unsigned int, len: unsigned int) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        if (len == 0u) {
            zero : unsigned int = 0u;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(zero);
        }
        count : unsigned int = 0u;
        while (count < len) {
            next : ::k::Optional<char> = read();
            if (!next.hasValue()) {
                break;
            }
            buf[off + count] = next.get();
            ++count;
        }
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(count);
    }

    override read(buf: char[]) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return read(buf, 0u, (unsigned int) buf.size);
    }

    override skip(n: unsigned long) : unsigned long {
        skipped : unsigned long = 0uL;
        while (skipped < n) {
            next : ::k::Optional<char> = read();
            if (!next.hasValue()) {
                break;
            }
            ++skipped;
        }
        return skipped;
    }

    override available() : Expected<unsigned int, ::k::io::StreamOutOfData> {
        if (_in == null) {
            err : ::k::io::StreamOutOfData = ::k::io::StreamOutOfData::Closed;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::error(err);
        }
        underlying : Expected<unsigned int, ::k::io::StreamOutOfData> = _in->available();
        if (underlying.hasError()) return underlying;
        units : unsigned int = underlying.getResult() / 2u;
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(units);
    }

    override close() {
        if (_in != null) {
            _in->close();
        }
        if (_inOwner != null) {
            delete _inOwner;
            _inOwner = null;
            _in = null;
        }
    }

private:
    _readUnit() : ::k::Optional<unsigned short> {
        if (_in == null) return ::k::Optional<unsigned short>();
        b0Opt : ::k::Optional<byte> = _in->read();
        if (!b0Opt.hasValue()) return ::k::Optional<unsigned short>();
        b1Opt : ::k::Optional<byte> = _in->read();
        if (!b1Opt.hasValue()) {
            _handleMalformed(1u);
            return ::k::Optional<unsigned short>();
        }
        _offset += 2uL;

        b0 : unsigned int = (unsigned int) (unsigned byte) b0Opt.get();
        b1 : unsigned int = (unsigned int) (unsigned byte) b1Opt.get();

        if (_order == ByteOrder::LittleEndian) {
            val : unsigned short = (unsigned short) (b0 | (b1 << 8));
            return ::k::Optional<unsigned short>(val);
        } else {
            // BigEndian or default
            val : unsigned short = (unsigned short) ((b0 << 8) | b1);
            return ::k::Optional<unsigned short>(val);
        }
    }

    _handleMalformed(len: unsigned int) {
        if (_action == CodingErrorAction::Report) {
            throw MalformedInputException(_offset - (unsigned long) len, len);
        }
    }
}

/**
 * Character writer that encodes Unicode code points into a UTF-16 byte stream.
 */
public class Utf16Writer : public Writer {
    _out      : OutputStream<byte>*;
    _outOwner : OutputStream<byte>!;
    _order    : ByteOrder;
    _action   : CodingErrorAction;
    _offset   : unsigned long;

public:
    Utf16Writer(output: OutputStream<byte>*, order: ByteOrder, action: CodingErrorAction)
        : _out(output), _outOwner(null), _order(order), _action(action), _offset(0uL) {}

    Utf16Writer(output: OutputStream<byte>*, order: ByteOrder)
        : _out(output), _outOwner(null), _order(order), _action(CodingErrorAction::Report), _offset(0uL) {}

    Utf16Writer(output: OutputStream<byte>*)
        : _out(output), _outOwner(null), _order(ByteOrder::BigEndian), _action(CodingErrorAction::Report), _offset(0uL) {}

    Utf16Writer(output: OutputStream<byte>!, order: ByteOrder, action: CodingErrorAction)
        : _out(null), _outOwner(output), _order(order), _action(action), _offset(0uL) {
        _out = _outOwner;
    }

    Utf16Writer(output: OutputStream<byte>!, order: ByteOrder)
        : _out(null), _outOwner(output), _order(order), _action(CodingErrorAction::Report), _offset(0uL) {
        _out = _outOwner;
    }

    Utf16Writer(output: OutputStream<byte>!)
        : _out(null), _outOwner(output), _order(ByteOrder::BigEndian), _action(CodingErrorAction::Report), _offset(0uL) {
        _out = _outOwner;
    }

    ~Utf16Writer() {
        close();
    }

    override write(c: char) : OutputStream<char>& {
        if (_out == null) return this;

        if (c < 0x10000u) {
            if (c >= 0xD800u && c <= 0xDFFFu) {
                _handleUnmappable(c);
            } else {
                _writeUnit((unsigned short) c);
            }
        } else if (c <= 0x10FFFFu) {
            v : unsigned int = (unsigned int) c - 0x10000u;
            high : unsigned short = (unsigned short) (0xD800u + (v >> 10));
            low  : unsigned short = (unsigned short) (0xDC00u + (v & 0x3FFu));
            _writeUnit(high);
            _writeUnit(low);
        } else {
            _handleUnmappable(c);
        }
        return this;
    }

    override write(buf: const char[], off: unsigned int, len: unsigned int) : OutputStream<char>& {
        i : unsigned int = 0u;
        while (i < len) {
            write(buf[off + i]);
            ++i;
        }
        return this;
    }

    override write(buf: const char[]) : OutputStream<char>& {
        return write(buf, 0u, (unsigned int) buf.size);
    }

    override write(str: const String&) : Writer& {
        u32 : char[]! = str.toUtf32();
        n : unsigned int = str.size();
        write(u32, 0u, n);
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
        if (_outOwner != null) {
            delete _outOwner;
            _outOwner = null;
            _out = null;
        }
        return this;
    }

private:
    _writeUnit(u: unsigned short) {
        b0 : byte = (byte) ((u >> 8) & 0xFFu);
        b1 : byte = (byte) (u & 0xFFu);
        if (_order == ByteOrder::LittleEndian) {
            _out->write(b1);
            _out->write(b0);
        } else {
            _out->write(b0);
            _out->write(b1);
        }
        _offset += 2uL;
    }

    _handleUnmappable(c: char) {
        if (_action == CodingErrorAction::Report) {
            throw UnmappableCharacterException(c, _offset);
        } else if (_action == CodingErrorAction::Replace) {
            _writeUnit((unsigned short) 0xFFFDu);
        }
    }
}

/**
 * Character reader that reads 32-bit Unicode code points from a byte stream.
 */
public class Utf32Reader : public Reader {
    _in      : InputStream<byte>*;
    _inOwner : InputStream<byte>!;
    _order   : ByteOrder;
    _action  : CodingErrorAction;
    _offset  : unsigned long;

public:
    Utf32Reader(input: InputStream<byte>*, order: ByteOrder, action: CodingErrorAction)
        : _in(input), _inOwner(null), _order(order), _action(action), _offset(0uL) {}

    Utf32Reader(input: InputStream<byte>*, order: ByteOrder)
        : _in(input), _inOwner(null), _order(order), _action(CodingErrorAction::Report), _offset(0uL) {}

    Utf32Reader(input: InputStream<byte>*)
        : _in(input), _inOwner(null), _order(ByteOrder::BigEndian), _action(CodingErrorAction::Report), _offset(0uL) {}

    Utf32Reader(input: InputStream<byte>!, order: ByteOrder, action: CodingErrorAction)
        : _in(null), _inOwner(input), _order(order), _action(action), _offset(0uL) {
        _in = _inOwner;
    }

    Utf32Reader(input: InputStream<byte>!, order: ByteOrder)
        : _in(null), _inOwner(input), _order(order), _action(CodingErrorAction::Report), _offset(0uL) {
        _in = _inOwner;
    }

    Utf32Reader(input: InputStream<byte>!)
        : _in(null), _inOwner(input), _order(ByteOrder::BigEndian), _action(CodingErrorAction::Report), _offset(0uL) {
        _in = _inOwner;
    }

    ~Utf32Reader() {
        close();
    }

    override read() : ::k::Optional<char> {
        if (_in == null) return ::k::Optional<char>();
        b0Opt : ::k::Optional<byte> = _in->read();
        if (!b0Opt.hasValue()) return ::k::Optional<char>();
        b1Opt : ::k::Optional<byte> = _in->read();
        b2Opt : ::k::Optional<byte> = _in->read();
        b3Opt : ::k::Optional<byte> = _in->read();
        if (!b1Opt.hasValue() || !b2Opt.hasValue() || !b3Opt.hasValue()) {
            if (_action == CodingErrorAction::Report) {
                throw MalformedInputException(_offset, 4u);
            }
            return ::k::Optional<char>((char) 0xFFFDu);
        }
        _offset += 4uL;

        b0 : unsigned int = (unsigned int) (unsigned byte) b0Opt.get();
        b1 : unsigned int = (unsigned int) (unsigned byte) b1Opt.get();
        b2 : unsigned int = (unsigned int) (unsigned byte) b2Opt.get();
        b3 : unsigned int = (unsigned int) (unsigned byte) b3Opt.get();

        cp : unsigned int = 0u;
        if (_order == ByteOrder::LittleEndian) {
            cp = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
        } else {
            cp = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
        }

        if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
            if (_action == CodingErrorAction::Report) {
                throw MalformedInputException(_offset - 4uL, 4u);
            }
            return ::k::Optional<char>((char) 0xFFFDu);
        }

        return ::k::Optional<char>((char) cp);
    }

    override read(buf: char[], off: unsigned int, len: unsigned int) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        if (len == 0u) {
            zero : unsigned int = 0u;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(zero);
        }
        count : unsigned int = 0u;
        while (count < len) {
            next : ::k::Optional<char> = read();
            if (!next.hasValue()) {
                break;
            }
            buf[off + count] = next.get();
            ++count;
        }
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(count);
    }

    override read(buf: char[]) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return read(buf, 0u, (unsigned int) buf.size);
    }

    override skip(n: unsigned long) : unsigned long {
        skipped : unsigned long = 0uL;
        while (skipped < n) {
            next : ::k::Optional<char> = read();
            if (!next.hasValue()) {
                break;
            }
            ++skipped;
        }
        return skipped;
    }

    override available() : Expected<unsigned int, ::k::io::StreamOutOfData> {
        if (_in == null) {
            err : ::k::io::StreamOutOfData = ::k::io::StreamOutOfData::Closed;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::error(err);
        }
        underlying : Expected<unsigned int, ::k::io::StreamOutOfData> = _in->available();
        if (underlying.hasError()) return underlying;
        units : unsigned int = underlying.getResult() / 4u;
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(units);
    }

    override close() {
        if (_in != null) {
            _in->close();
        }
        if (_inOwner != null) {
            delete _inOwner;
            _inOwner = null;
            _in = null;
        }
    }
}

/**
 * Character writer that writes 32-bit Unicode code points to a byte stream.
 */
public class Utf32Writer : public Writer {
    _out      : OutputStream<byte>*;
    _outOwner : OutputStream<byte>!;
    _order    : ByteOrder;
    _action   : CodingErrorAction;
    _offset   : unsigned long;

public:
    Utf32Writer(output: OutputStream<byte>*, order: ByteOrder, action: CodingErrorAction)
        : _out(output), _outOwner(null), _order(order), _action(action), _offset(0uL) {}

    Utf32Writer(output: OutputStream<byte>*, order: ByteOrder)
        : _out(output), _outOwner(null), _order(order), _action(CodingErrorAction::Report), _offset(0uL) {}

    Utf32Writer(output: OutputStream<byte>*)
        : _out(output), _outOwner(null), _order(ByteOrder::BigEndian), _action(CodingErrorAction::Report), _offset(0uL) {}

    Utf32Writer(output: OutputStream<byte>!, order: ByteOrder, action: CodingErrorAction)
        : _out(null), _outOwner(output), _order(order), _action(action), _offset(0uL) {
        _out = _outOwner;
    }

    Utf32Writer(output: OutputStream<byte>!, order: ByteOrder)
        : _out(null), _outOwner(output), _order(order), _action(CodingErrorAction::Report), _offset(0uL) {
        _out = _outOwner;
    }

    Utf32Writer(output: OutputStream<byte>!)
        : _out(null), _outOwner(output), _order(ByteOrder::BigEndian), _action(CodingErrorAction::Report), _offset(0uL) {
        _out = _outOwner;
    }

    ~Utf32Writer() {
        close();
    }

    override write(c: char) : OutputStream<char>& {
        if (_out == null) return this;

        cp : unsigned int = (unsigned int) c;
        if (cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
            if (_action == CodingErrorAction::Report) {
                throw UnmappableCharacterException(c, _offset);
            } else if (_action == CodingErrorAction::Replace) {
                cp = 0xFFFDu;
            } else {
                return this;
            }
        }

        b0 : byte = (byte) ((cp >> 24) & 0xFFu);
        b1 : byte = (byte) ((cp >> 16) & 0xFFu);
        b2 : byte = (byte) ((cp >> 8) & 0xFFu);
        b3 : byte = (byte) (cp & 0xFFu);

        if (_order == ByteOrder::LittleEndian) {
            _out->write(b3);
            _out->write(b2);
            _out->write(b1);
            _out->write(b0);
        } else {
            _out->write(b0);
            _out->write(b1);
            _out->write(b2);
            _out->write(b3);
        }
        _offset += 4uL;
        return this;
    }

    override write(buf: const char[], off: unsigned int, len: unsigned int) : OutputStream<char>& {
        i : unsigned int = 0u;
        while (i < len) {
            write(buf[off + i]);
            ++i;
        }
        return this;
    }

    override write(buf: const char[]) : OutputStream<char>& {
        return write(buf, 0u, (unsigned int) buf.size);
    }

    override write(str: const String&) : Writer& {
        u32 : char[]! = str.toUtf32();
        n : unsigned int = str.size();
        write(u32, 0u, n);
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
        if (_outOwner != null) {
            delete _outOwner;
            _outOwner = null;
            _out = null;
        }
        return this;
    }
}

} // namespace io
