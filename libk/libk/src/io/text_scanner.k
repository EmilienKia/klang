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

@ffi::Extern("C") private __k_io_parse_double(str: const byte*, outVal: double*) : int;

/**
 * Scanner for reading structured values and tokens from a character stream.
 */
public class TextScanner : public FilterReader {
    _br : BufferedReader!;

public:
    TextScanner(input: Reader*) : FilterReader(input), _br(null) {
        _br = new BufferedReader(input);
    }

    ~TextScanner() {
        close();
    }

    /**
     * Check whether there is another token in the input stream.
     */
    hasNext() : bool {
        _skipWhitespace();
        if (_br == null) return false;
        p : ::k::Optional<char> = _br->peek();
        return p.hasValue();
    }

    /**
     * Read the next whitespace-delimited token.
     */
    nextToken() : ::k::Optional<String> {
        _skipWhitespace();
        if (_br == null) return ::k::Optional<String>();
        first : ::k::Optional<char> = _br->read();
        if (!first.hasValue()) {
            return ::k::Optional<String>();
        }

        sb : StringBuilder;
        c : char = first.get();
        while (true) {
            sb.appendChar(c);
            p : ::k::Optional<char> = _br->peek();
            if (!p.hasValue() || CharHelpers::isWhitespace(p.get())) {
                break;
            }
            c = _br->read().get();
        }
        return ::k::Optional<String>(sb.toString());
    }

    /**
     * Read the remainder of the current line.
     */
    nextLine() : ::k::Optional<String> {
        if (_br == null) return ::k::Optional<String>();
        return _br->readLine();
    }

    /**
     * Read a boolean value ("true" or "false").
     */
    readBool() : bool {
        tokOpt : ::k::Optional<String> = nextToken();
        if (!tokOpt.hasValue()) {
            throw EndOfStreamException();
        }
        tok : String = tokOpt.get();
        if (tok == String("true")) {
            return true;
        } else if (tok == String("false")) {
            return false;
        }
        throw NumberFormatException(tok);
    }

    /**
     * Read a signed 32-bit integer in decimal.
     */
    readInt() : int {
        return readInt(10u);
    }

    /**
     * Read a signed 32-bit integer with the given radix.
     */
    readInt(radix: unsigned int) : int {
        v : long = readLong(radix);
        if (v < -2147483648L || v > 2147483647L) {
            throw NumberFormatException();
        }
        return (int) v;
    }

    /**
     * Read a signed 64-bit integer in decimal.
     */
    readLong() : long {
        return readLong(10u);
    }

    /**
     * Read a signed 64-bit integer with the given radix.
     */
    readLong(radix: unsigned int) : long {
        tokOpt : ::k::Optional<String> = nextToken();
        if (!tokOpt.hasValue()) {
            throw EndOfStreamException();
        }
        tok : String = tokOpt.get();
        if (radix < 2u || radix > 36u) {
            throw IllegalArgumentException();
        }

        u32 : char[]! = tok.toUtf32();
        n : unsigned int = tok.size();
        if (n == 0u) {
            throw NumberFormatException(tok);
        }

        neg : bool = false;
        start : unsigned int = 0u;
        if (u32[0] == '-') {
            neg = true;
            start = 1u;
        } else if (u32[0] == '+') {
            start = 1u;
        }

        if (start >= n) {
            throw NumberFormatException(tok);
        }

        result : long = 0L;
        rad : long = (long) radix;
        i : unsigned int = start;
        while (i < n) {
            cp : char = u32[i];
            d : int = -1;
            if (cp >= '0' && cp <= '9') {
                d = cp - '0';
            } else if (cp >= 'a' && cp <= 'z') {
                d = cp - 'a' + 10;
            } else if (cp >= 'A' && cp <= 'Z') {
                d = cp - 'A' + 10;
            }

            if (d < 0 || (unsigned int) d >= radix) {
                throw NumberFormatException(tok);
            }

            result = result * rad + (long) d;
            ++i;
        }

        if (neg) {
            result = -result;
        }
        return result;
    }

    /**
     * Read an unsigned 32-bit integer in decimal.
     */
    readUnsignedInt() : unsigned int {
        return readUnsignedInt(10u);
    }

    /**
     * Read an unsigned 32-bit integer with the given radix.
     */
    readUnsignedInt(radix: unsigned int) : unsigned int {
        v : unsigned long = readUnsignedLong(radix);
        if (v > 4294967295uL) {
            throw NumberFormatException();
        }
        return (unsigned int) v;
    }

    /**
     * Read an unsigned 64-bit integer in decimal.
     */
    readUnsignedLong() : unsigned long {
        return readUnsignedLong(10u);
    }

    /**
     * Read an unsigned 64-bit integer with the given radix.
     */
    readUnsignedLong(radix: unsigned int) : unsigned long {
        tokOpt : ::k::Optional<String> = nextToken();
        if (!tokOpt.hasValue()) {
            throw EndOfStreamException();
        }
        tok : String = tokOpt.get();
        if (radix < 2u || radix > 36u) {
            throw IllegalArgumentException();
        }

        u32 : char[]! = tok.toUtf32();
        n : unsigned int = tok.size();
        if (n == 0u) {
            throw NumberFormatException(tok);
        }

        start : unsigned int = 0u;
        if (u32[0] == '+') {
            start = 1u;
        }

        if (start >= n) {
            throw NumberFormatException(tok);
        }

        result : unsigned long = 0uL;
        rad : unsigned long = (unsigned long) radix;
        i : unsigned int = start;
        while (i < n) {
            cp : char = u32[i];
            d : int = -1;
            if (cp >= '0' && cp <= '9') {
                d = cp - '0';
            } else if (cp >= 'a' && cp <= 'z') {
                d = cp - 'a' + 10;
            } else if (cp >= 'A' && cp <= 'Z') {
                d = cp - 'A' + 10;
            }

            if (d < 0 || (unsigned int) d >= radix) {
                throw NumberFormatException(tok);
            }

            result = result * rad + (unsigned long) d;
            ++i;
        }
        return result;
    }

    /**
     * Read a 32-bit floating point value.
     */
    readFloat() : float {
        d : double = readDouble();
        return (float) d;
    }

    /**
     * Read a 64-bit floating point value.
     */
    readDouble() : double {
        tokOpt : ::k::Optional<String> = nextToken();
        if (!tokOpt.hasValue()) {
            throw EndOfStreamException();
        }
        tok : String = tokOpt.get();
        u8 : unsigned byte[]! = tok.toUtf8();
        outVal : double = 0.0;
        ok : int = __k_io_parse_double((byte*) &u8[0], &outVal);
        if (ok == 0) {
            throw NumberFormatException(tok);
        }
        return outVal;
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

private:
    _skipWhitespace() {
        if (_br == null) return;
        while (true) {
            p : ::k::Optional<char> = _br->peek();
            if (!p.hasValue()) break;
            if (CharHelpers::isWhitespace(p.get())) {
                _br->read();
            } else {
                break;
            }
        }
    }
}

} // namespace io
