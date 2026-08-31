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
 * Base64 encoder codec.
 *
 * Supports standard RFC 4648 Base64, URL-safe Base64, and line chunking (MIME/PEM).
 */
public class Base64Encoder : public BaseNCodec {
private:
    _urlSafe : bool = false;

public:
    static encodeChar(val: unsigned long, urlSafe: bool) : byte {
        idx : unsigned int = (unsigned int) (val & BaseNCodec::MASK_6BITS);
        if (idx < 26u) {
            return (byte) (((unsigned int) 'A') + idx);
        } else if (idx < 52u) {
            return (byte) (((unsigned int) 'a') + (idx - 26u));
        } else if (idx < 62u) {
            return (byte) (((unsigned int) '0') + (idx - 52u));
        } else if (idx == 62u) {
            return urlSafe ? (byte) '-' : (byte) '+';
        } else {
            return urlSafe ? (byte) '_' : (byte) '/';
        }
    }

    Base64Encoder(urlSafe: bool, lineLength: unsigned int, separator: const byte[]*)
        : BaseNCodec(lineLength, separator, urlSafe ? (byte) 0 : BaseNCodec::PAD_DEFAULT),
          _urlSafe(urlSafe) {}

    Base64Encoder(urlSafe: bool)
        : BaseNCodec(0u, null, urlSafe ? (byte) 0 : BaseNCodec::PAD_DEFAULT),
          _urlSafe(urlSafe) {}

    Base64Encoder(lineLength: unsigned int, separator: const byte[]*)
        : BaseNCodec(lineLength, separator, BaseNCodec::PAD_DEFAULT),
          _urlSafe(false) {}

    Base64Encoder()
        : BaseNCodec(0u, null, BaseNCodec::PAD_DEFAULT),
          _urlSafe(false) {}

    override process(buff: const byte[], off: unsigned int, len: unsigned int) {
        pos : unsigned int = 0u;
        while (pos < len) {
            ub : unsigned long = ((unsigned long) buff[off + pos]) & BaseNCodec::MASK_8BITS;
            _state = (_state + 1u) % 3u;
            _work = (_work << 8) | ub;
            if (_state == 0u) {
                _buf.push(encodeChar(_work >> 18, _urlSafe));
                _buf.push(encodeChar(_work >> 12, _urlSafe));
                _buf.push(encodeChar(_work >> 6, _urlSafe));
                _buf.push(encodeChar(_work, _urlSafe));
                _work = 0uL;
                _currentLinePos += 4u;
                if (_lineLength > 0u && _lineLength <= _currentLinePos) {
                    _addSeparator();
                }
            }
            ++pos;
        }
    }

    override finalize() {
        if (_eof) return;
        _eof = true;

        if (_state == 0u && _lineLength == 0u) {
            return;
        }

        savedCount : unsigned int = _buf.size();
        if (_state == 1u) {
            // 1 byte (8 bits) -> 6 bits + 2 bits
            _buf.push(encodeChar(_work >> 2, _urlSafe));
            _buf.push(encodeChar(_work << 4, _urlSafe));
            if (_pad != (byte) 0) {
                _buf.push(_pad);
                _buf.push(_pad);
            }
        } else if (_state == 2u) {
            // 2 bytes (16 bits) -> 6 + 6 + 4 bits
            _buf.push(encodeChar(_work >> 10, _urlSafe));
            _buf.push(encodeChar(_work >> 4, _urlSafe));
            _buf.push(encodeChar(_work << 2, _urlSafe));
            if (_pad != (byte) 0) {
                _buf.push(_pad);
            }
        }

        added : unsigned int = _buf.size() - savedCount;
        _currentLinePos += added;
        if (_lineLength > 0u && _currentLinePos > 0u) {
            _addSeparator();
        }
    }
}


/**
 * Base64 decoder codec.
 *
 * Decodes both standard and URL-safe Base64 streams, skipping whitespace and padding.
 */
public class Base64Decoder : public BaseNCodec {
private:
    static decodeChar(b: byte) : int {
        ub : unsigned int = ((unsigned int) b) & 0xFFu;
        if (ub >= (unsigned int) 'A' && ub <= (unsigned int) 'Z') {
            return (int) (ub - ((unsigned int) 'A'));
        }
        if (ub >= (unsigned int) 'a' && ub <= (unsigned int) 'z') {
            return (int) (ub - ((unsigned int) 'a') + 26u);
        }
        if (ub >= (unsigned int) '0' && ub <= (unsigned int) '9') {
            return (int) (ub - ((unsigned int) '0') + 52u);
        }
        if (b == (byte) '+' || b == (byte) '-') {
            return 62;
        }
        if (b == (byte) '/' || b == (byte) '_') {
            return 63;
        }
        return -1;
    }

public:
    Base64Decoder(pad: byte) : BaseNCodec(0u, null, pad) {}
    Base64Decoder() : BaseNCodec(0u, null, BaseNCodec::PAD_DEFAULT) {}

    override process(buff: const byte[], off: unsigned int, len: unsigned int) {
        if (_eof) return;
        pos : unsigned int = 0u;
        while (pos < len) {
            b : byte = buff[off + pos];
            if (b == _pad) {
                finalize();
                break;
            }
            if (BaseNCodec::isWhitespace(b)) {
                ++pos;
                continue;
            }
            val : int = decodeChar(b);
            if (val >= 0) {
                _state = (_state + 1u) % 4u;
                _work = (_work << 6) | ((unsigned long) val);
                if (_state == 0u) {
                    _buf.push((byte) ((_work >> 16) & BaseNCodec::MASK_8BITS));
                    _buf.push((byte) ((_work >> 8) & BaseNCodec::MASK_8BITS));
                    _buf.push((byte) (_work & BaseNCodec::MASK_8BITS));
                    _work = 0uL;
                }
            }
            ++pos;
        }
    }

    override finalize() {
        if (_eof) return;
        _eof = true;

        if (_state == 2u) {
            // 2 characters (12 bits) -> 8 bits data + 4 bits discard
            _buf.push((byte) ((_work >> 4) & BaseNCodec::MASK_8BITS));
        } else if (_state == 3u) {
            // 3 characters (18 bits) -> 16 bits data + 2 bits discard
            w : unsigned long = _work >> 2;
            _buf.push((byte) ((w >> 8) & BaseNCodec::MASK_8BITS));
            _buf.push((byte) (w & BaseNCodec::MASK_8BITS));
        }
        _state = 0u;
        _work = 0uL;
    }
}


/**
 * Stream decorator for reading Base64-encoded or Base64-decoded data.
 */
public class Base64InputStream : public CodecInputStream {
protected:
    _ownCodec : Codec!;

public:
    /** Construct a Base64 decoding input stream borrowing input. */
    Base64InputStream(input: InputStream<byte>*)
        : CodecInputStream(input, null, 4096u), _ownCodec(null) {
        _ownCodec = new Base64Decoder();
        _codec = _ownCodec;
    }

    /** Construct a Base64 encoding/decoding input stream borrowing input. */
    Base64InputStream(input: InputStream<byte>*, encode: bool, urlSafe: bool, lineLength: unsigned int, separator: const byte[]*)
        : CodecInputStream(input, null, 4096u), _ownCodec(null) {
        if (encode) {
            _ownCodec = new Base64Encoder(urlSafe, lineLength, separator);
        } else {
            _ownCodec = new Base64Decoder();
        }
        _codec = _ownCodec;
    }

    /** Construct a Base64 encoding/decoding input stream borrowing input. */
    Base64InputStream(input: InputStream<byte>*, encode: bool)
        : CodecInputStream(input, null, 4096u), _ownCodec(null) {
        if (encode) {
            _ownCodec = new Base64Encoder();
        } else {
            _ownCodec = new Base64Decoder();
        }
        _codec = _ownCodec;
    }

    ~Base64InputStream() {
        close();
        if (_ownCodec != null) {
            delete _ownCodec;
            _ownCodec = null;
            _codec = null;
        }
    }
}


/**
 * Stream decorator for writing Base64-encoded or Base64-decoded data.
 */
public class Base64OutputStream : public CodecOutputStream {
protected:
    _ownCodec : Codec!;

public:
    /** Construct a Base64 encoding output stream borrowing output. */
    Base64OutputStream(output: OutputStream<byte>*)
        : CodecOutputStream(output, null), _ownCodec(null) {
        _ownCodec = new Base64Encoder();
        _codec = _ownCodec;
    }

    /** Construct a Base64 encoding/decoding output stream borrowing output. */
    Base64OutputStream(output: OutputStream<byte>*, encode: bool, urlSafe: bool, lineLength: unsigned int, separator: const byte[]*)
        : CodecOutputStream(output, null), _ownCodec(null) {
        if (encode) {
            _ownCodec = new Base64Encoder(urlSafe, lineLength, separator);
        } else {
            _ownCodec = new Base64Decoder();
        }
        _codec = _ownCodec;
    }

    /** Construct a Base64 encoding/decoding output stream borrowing output. */
    Base64OutputStream(output: OutputStream<byte>*, encode: bool)
        : CodecOutputStream(output, null), _ownCodec(null) {
        if (encode) {
            _ownCodec = new Base64Encoder();
        } else {
            _ownCodec = new Base64Decoder();
        }
        _codec = _ownCodec;
    }

    ~Base64OutputStream() {
        close();
        if (_ownCodec != null) {
            delete _ownCodec;
            _ownCodec = null;
            _codec = null;
        }
    }
}

} // namespace io
