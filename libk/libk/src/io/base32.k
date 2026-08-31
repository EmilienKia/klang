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
 * Base32 encoder codec.
 *
 * Supports standard RFC 4648 Base32 and extended Base32Hex alphabet.
 */
public class Base32Encoder : public BaseNCodec {
private:
    _useHex : bool = false;

public:
    static encodeChar(val: unsigned long, useHex: bool) : byte {
        idx : unsigned int = (unsigned int) (val & BaseNCodec::MASK_5BITS);
        if (useHex) {
            if (idx < 10u) {
                return (byte) (((unsigned int) '0') + idx);
            } else {
                return (byte) (((unsigned int) 'A') + (idx - 10u));
            }
        } else {
            if (idx < 26u) {
                return (byte) (((unsigned int) 'A') + idx);
            } else {
                return (byte) (((unsigned int) '2') + (idx - 26u));
            }
        }
    }

    Base32Encoder(useHex: bool, lineLength: unsigned int, separator: const byte[]*, pad: byte)
        : BaseNCodec(lineLength, separator, pad), _useHex(useHex) {}

    Base32Encoder(useHex: bool, lineLength: unsigned int, separator: const byte[]*)
        : BaseNCodec(lineLength, separator, BaseNCodec::PAD_DEFAULT), _useHex(useHex) {}

    Base32Encoder(useHex: bool)
        : BaseNCodec(0u, null, BaseNCodec::PAD_DEFAULT), _useHex(useHex) {}

    Base32Encoder()
        : BaseNCodec(0u, null, BaseNCodec::PAD_DEFAULT), _useHex(false) {}

    override process(buff: const byte[], off: unsigned int, len: unsigned int) {
        pos : unsigned int = 0u;
        while (pos < len) {
            ub : unsigned long = ((unsigned long) buff[off + pos]) & BaseNCodec::MASK_8BITS;
            _state = (_state + 1u) % 5u;
            _work = (_work << 8) | ub;
            if (_state == 0u) {
                _buf.push(encodeChar(_work >> 35, _useHex));
                _buf.push(encodeChar(_work >> 30, _useHex));
                _buf.push(encodeChar(_work >> 25, _useHex));
                _buf.push(encodeChar(_work >> 20, _useHex));
                _buf.push(encodeChar(_work >> 15, _useHex));
                _buf.push(encodeChar(_work >> 10, _useHex));
                _buf.push(encodeChar(_work >> 5, _useHex));
                _buf.push(encodeChar(_work, _useHex));
                _work = 0uL;
                _currentLinePos += 8u;
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
            // 1 byte (8 bits) -> 5 bits + 3 bits
            _buf.push(encodeChar(_work >> 3, _useHex));
            _buf.push(encodeChar(_work << 2, _useHex));
            if (_pad != (byte) 0) {
                _buf.push(_pad);
                _buf.push(_pad);
                _buf.push(_pad);
                _buf.push(_pad);
                _buf.push(_pad);
                _buf.push(_pad);
            }
        } else if (_state == 2u) {
            // 2 bytes (16 bits) -> 5 + 5 + 5 + 1 bits
            _buf.push(encodeChar(_work >> 11, _useHex));
            _buf.push(encodeChar(_work >> 6, _useHex));
            _buf.push(encodeChar(_work >> 1, _useHex));
            _buf.push(encodeChar(_work << 4, _useHex));
            if (_pad != (byte) 0) {
                _buf.push(_pad);
                _buf.push(_pad);
                _buf.push(_pad);
                _buf.push(_pad);
            }
        } else if (_state == 3u) {
            // 3 bytes (24 bits) -> 5 + 5 + 5 + 5 + 4 bits
            _buf.push(encodeChar(_work >> 19, _useHex));
            _buf.push(encodeChar(_work >> 14, _useHex));
            _buf.push(encodeChar(_work >> 9, _useHex));
            _buf.push(encodeChar(_work >> 4, _useHex));
            _buf.push(encodeChar(_work << 1, _useHex));
            if (_pad != (byte) 0) {
                _buf.push(_pad);
                _buf.push(_pad);
                _buf.push(_pad);
            }
        } else if (_state == 4u) {
            // 4 bytes (32 bits) -> 5 + 5 + 5 + 5 + 5 + 5 + 2 bits
            _buf.push(encodeChar(_work >> 27, _useHex));
            _buf.push(encodeChar(_work >> 22, _useHex));
            _buf.push(encodeChar(_work >> 17, _useHex));
            _buf.push(encodeChar(_work >> 12, _useHex));
            _buf.push(encodeChar(_work >> 7, _useHex));
            _buf.push(encodeChar(_work >> 2, _useHex));
            _buf.push(encodeChar(_work << 3, _useHex));
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
 * Base32 decoder codec.
 */
public class Base32Decoder : public BaseNCodec {
private:
    _useHex : bool = false;

public:
    static decodeChar(b: byte, useHex: bool) : int {
        ub : unsigned int = ((unsigned int) b) & 0xFFu;
        if (useHex) {
            if (ub >= (unsigned int) '0' && ub <= (unsigned int) '9') {
                return (int) (ub - ((unsigned int) '0'));
            }
            if (ub >= (unsigned int) 'A' && ub <= (unsigned int) 'V') {
                return (int) (ub - ((unsigned int) 'A') + 10u);
            }
            if (ub >= (unsigned int) 'a' && ub <= (unsigned int) 'v') {
                return (int) (ub - ((unsigned int) 'a') + 10u);
            }
            return -1;
        } else {
            if (ub >= (unsigned int) 'A' && ub <= (unsigned int) 'Z') {
                return (int) (ub - ((unsigned int) 'A'));
            }
            if (ub >= (unsigned int) 'a' && ub <= (unsigned int) 'z') {
                return (int) (ub - ((unsigned int) 'a'));
            }
            if (ub >= (unsigned int) '2' && ub <= (unsigned int) '7') {
                return (int) (ub - ((unsigned int) '2') + 26u);
            }
            return -1;
        }
    }

    Base32Decoder(useHex: bool, pad: byte) : BaseNCodec(0u, null, pad), _useHex(useHex) {}
    Base32Decoder(useHex: bool) : BaseNCodec(0u, null, BaseNCodec::PAD_DEFAULT), _useHex(useHex) {}
    Base32Decoder() : BaseNCodec(0u, null, BaseNCodec::PAD_DEFAULT), _useHex(false) {}

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
            val : int = decodeChar(b, _useHex);
            if (val >= 0) {
                _state = (_state + 1u) % 8u;
                _work = (_work << 5) | ((unsigned long) val);
                if (_state == 0u) {
                    _buf.push((byte) ((_work >> 32) & BaseNCodec::MASK_8BITS));
                    _buf.push((byte) ((_work >> 24) & BaseNCodec::MASK_8BITS));
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
            _buf.push((byte) ((_work >> 2) & BaseNCodec::MASK_8BITS));
        } else if (_state == 3u) {
            _buf.push((byte) ((_work >> 7) & BaseNCodec::MASK_8BITS));
        } else if (_state == 4u) {
            w : unsigned long = _work >> 4;
            _buf.push((byte) ((w >> 8) & BaseNCodec::MASK_8BITS));
            _buf.push((byte) (w & BaseNCodec::MASK_8BITS));
        } else if (_state == 5u) {
            w : unsigned long = _work >> 1;
            _buf.push((byte) ((w >> 16) & BaseNCodec::MASK_8BITS));
            _buf.push((byte) ((w >> 8) & BaseNCodec::MASK_8BITS));
            _buf.push((byte) (w & BaseNCodec::MASK_8BITS));
        } else if (_state == 6u) {
            w : unsigned long = _work >> 6;
            _buf.push((byte) ((w >> 16) & BaseNCodec::MASK_8BITS));
            _buf.push((byte) ((w >> 8) & BaseNCodec::MASK_8BITS));
            _buf.push((byte) (w & BaseNCodec::MASK_8BITS));
        } else if (_state == 7u) {
            w : unsigned long = _work >> 3;
            _buf.push((byte) ((w >> 24) & BaseNCodec::MASK_8BITS));
            _buf.push((byte) ((w >> 16) & BaseNCodec::MASK_8BITS));
            _buf.push((byte) ((w >> 8) & BaseNCodec::MASK_8BITS));
            _buf.push((byte) (w & BaseNCodec::MASK_8BITS));
        }
        _state = 0u;
        _work = 0uL;
    }
}


/**
 * Stream decorator for reading Base32-encoded or Base32-decoded data.
 */
public class Base32InputStream : public CodecInputStream {
protected:
    _ownCodec : Codec!;

public:
    /** Construct a Base32 decoding input stream borrowing input. */
    Base32InputStream(input: InputStream<byte>*)
        : CodecInputStream(input, null, 4096u), _ownCodec(null) {
        _ownCodec = new Base32Decoder();
        _codec = _ownCodec;
    }

    /** Construct a Base32 encoding/decoding input stream borrowing input. */
    Base32InputStream(input: InputStream<byte>*, encode: bool, useHex: bool, lineLength: unsigned int, separator: const byte[]*, pad: byte)
        : CodecInputStream(input, null, 4096u), _ownCodec(null) {
        if (encode) {
            _ownCodec = new Base32Encoder(useHex, lineLength, separator, pad);
        } else {
            _ownCodec = new Base32Decoder(useHex, pad);
        }
        _codec = _ownCodec;
    }

    /** Construct a Base32 encoding/decoding input stream borrowing input. */
    Base32InputStream(input: InputStream<byte>*, encode: bool, useHex: bool)
        : CodecInputStream(input, null, 4096u), _ownCodec(null) {
        if (encode) {
            _ownCodec = new Base32Encoder(useHex);
        } else {
            _ownCodec = new Base32Decoder(useHex);
        }
        _codec = _ownCodec;
    }

    /** Construct a Base32 encoding/decoding input stream borrowing input. */
    Base32InputStream(input: InputStream<byte>*, encode: bool)
        : CodecInputStream(input, null, 4096u), _ownCodec(null) {
        if (encode) {
            _ownCodec = new Base32Encoder(false);
        } else {
            _ownCodec = new Base32Decoder(false);
        }
        _codec = _ownCodec;
    }

    ~Base32InputStream() {
        close();
        if (_ownCodec != null) {
            delete _ownCodec;
            _ownCodec = null;
            _codec = null;
        }
    }
}


/**
 * Stream decorator for writing Base32-encoded or Base32-decoded data.
 */
public class Base32OutputStream : public CodecOutputStream {
protected:
    _ownCodec : Codec!;

public:
    /** Construct a Base32 encoding output stream borrowing output. */
    Base32OutputStream(output: OutputStream<byte>*)
        : CodecOutputStream(output, null), _ownCodec(null) {
        _ownCodec = new Base32Encoder();
        _codec = _ownCodec;
    }

    /** Construct a Base32 encoding/decoding output stream borrowing output. */
    Base32OutputStream(output: OutputStream<byte>*, encode: bool, useHex: bool, lineLength: unsigned int, separator: const byte[]*, pad: byte)
        : CodecOutputStream(output, null), _ownCodec(null) {
        if (encode) {
            _ownCodec = new Base32Encoder(useHex, lineLength, separator, pad);
        } else {
            _ownCodec = new Base32Decoder(useHex, pad);
        }
        _codec = _ownCodec;
    }

    /** Construct a Base32 encoding/decoding output stream borrowing output. */
    Base32OutputStream(output: OutputStream<byte>*, encode: bool, useHex: bool)
        : CodecOutputStream(output, null), _ownCodec(null) {
        if (encode) {
            _ownCodec = new Base32Encoder(useHex);
        } else {
            _ownCodec = new Base32Decoder(useHex);
        }
        _codec = _ownCodec;
    }

    /** Construct a Base32 encoding/decoding output stream borrowing output. */
    Base32OutputStream(output: OutputStream<byte>*, encode: bool)
        : CodecOutputStream(output, null), _ownCodec(null) {
        if (encode) {
            _ownCodec = new Base32Encoder(false);
        } else {
            _ownCodec = new Base32Decoder(false);
        }
        _codec = _ownCodec;
    }

    ~Base32OutputStream() {
        close();
        if (_ownCodec != null) {
            delete _ownCodec;
            _ownCodec = null;
            _codec = null;
        }
    }
}

} // namespace io
