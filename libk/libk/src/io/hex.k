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
 * Base16 (Hexadecimal) encoder codec.
 */
public class HexEncoder : public BaseNCodec {
private:
    _uppercase : bool = true;

public:
    static encodeNibble(nibble: unsigned int, uppercase: bool) : byte {
        n : unsigned int = nibble & 0x0Fu;
        if (n < 10u) {
            return (byte) (((unsigned int) '0') + n);
        } else {
            return (byte) ((uppercase ? ((unsigned int) 'A') : ((unsigned int) 'a')) + (n - 10u));
        }
    }

    HexEncoder(uppercase: bool, lineLength: unsigned int, separator: const byte[]*)
        : BaseNCodec(lineLength, separator, (byte) 0), _uppercase(uppercase) {}

    HexEncoder(uppercase: bool)
        : BaseNCodec(0u, null, (byte) 0), _uppercase(uppercase) {}

    HexEncoder()
        : BaseNCodec(0u, null, (byte) 0), _uppercase(true) {}

    override process(buff: const byte[], off: unsigned int, len: unsigned int) {
        pos : unsigned int = 0u;
        while (pos < len) {
            ub : unsigned int = ((unsigned int) buff[off + pos]) & 0xFFu;
            _buf.push(encodeNibble(ub >> 4, _uppercase));
            _buf.push(encodeNibble(ub, _uppercase));
            _currentLinePos += 2u;
            if (_lineLength > 0u && _lineLength <= _currentLinePos) {
                _addSeparator();
            }
            ++pos;
        }
    }

    override finalize() {
        if (_eof) return;
        _eof = true;
        if (_lineLength > 0u && _currentLinePos > 0u) {
            _addSeparator();
        }
    }
}


/**
 * Base16 (Hexadecimal) decoder codec.
 */
public class HexDecoder : public BaseNCodec {
public:
    static decodeNibble(b: byte) : int {
        ub : unsigned int = ((unsigned int) b) & 0xFFu;
        if (ub >= (unsigned int) '0' && ub <= (unsigned int) '9') {
            return (int) (ub - ((unsigned int) '0'));
        }
        if (ub >= (unsigned int) 'A' && ub <= (unsigned int) 'F') {
            return (int) (ub - ((unsigned int) 'A') + 10u);
        }
        if (ub >= (unsigned int) 'a' && ub <= (unsigned int) 'f') {
            return (int) (ub - ((unsigned int) 'a') + 10u);
        }
        return -1;
    }

    HexDecoder() : BaseNCodec(0u, null, (byte) 0) {}

    override process(buff: const byte[], off: unsigned int, len: unsigned int) {
        if (_eof) return;
        pos : unsigned int = 0u;
        while (pos < len) {
            b : byte = buff[off + pos];
            if (BaseNCodec::isWhitespace(b)) {
                ++pos;
                continue;
            }
            val : int = decodeNibble(b);
            if (val >= 0) {
                _work = (_work << 4) | ((unsigned long) val);
                _state = (_state + 1u) % 2u;
                if (_state == 0u) {
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
        _state = 0u;
        _work = 0uL;
    }
}


/**
 * Stream decorator for reading Hex-encoded or Hex-decoded data.
 */
public class HexInputStream : public CodecInputStream {
protected:
    _ownCodec : Codec!;

public:
    /** Construct a Hex decoding input stream borrowing input. */
    HexInputStream(input: InputStream<byte>*)
        : CodecInputStream(input, null, 4096u), _ownCodec(null) {
        _ownCodec = new HexDecoder();
        _codec = _ownCodec;
    }

    /** Construct a Hex encoding/decoding input stream borrowing input. */
    HexInputStream(input: InputStream<byte>*, encode: bool, uppercase: bool, lineLength: unsigned int, separator: const byte[]*)
        : CodecInputStream(input, null, 4096u), _ownCodec(null) {
        if (encode) {
            _ownCodec = new HexEncoder(uppercase, lineLength, separator);
        } else {
            _ownCodec = new HexDecoder();
        }
        _codec = _ownCodec;
    }

    /** Construct a Hex encoding/decoding input stream borrowing input. */
    HexInputStream(input: InputStream<byte>*, encode: bool)
        : CodecInputStream(input, null, 4096u), _ownCodec(null) {
        if (encode) {
            _ownCodec = new HexEncoder(true);
        } else {
            _ownCodec = new HexDecoder();
        }
        _codec = _ownCodec;
    }

    ~HexInputStream() {
        close();
        if (_ownCodec != null) {
            delete _ownCodec;
            _ownCodec = null;
            _codec = null;
        }
    }
}


/**
 * Stream decorator for writing Hex-encoded or Hex-decoded data.
 */
public class HexOutputStream : public CodecOutputStream {
protected:
    _ownCodec : Codec!;

public:
    /** Construct a Hex encoding output stream borrowing output. */
    HexOutputStream(output: OutputStream<byte>*)
        : CodecOutputStream(output, null), _ownCodec(null) {
        _ownCodec = new HexEncoder(true);
        _codec = _ownCodec;
    }

    /** Construct a Hex encoding/decoding output stream borrowing output. */
    HexOutputStream(output: OutputStream<byte>*, encode: bool, uppercase: bool, lineLength: unsigned int, separator: const byte[]*)
        : CodecOutputStream(output, null), _ownCodec(null) {
        if (encode) {
            _ownCodec = new HexEncoder(uppercase, lineLength, separator);
        } else {
            _ownCodec = new HexDecoder();
        }
        _codec = _ownCodec;
    }

    /** Construct a Hex encoding/decoding output stream borrowing output. */
    HexOutputStream(output: OutputStream<byte>*, encode: bool)
        : CodecOutputStream(output, null), _ownCodec(null) {
        if (encode) {
            _ownCodec = new HexEncoder(true);
        } else {
            _ownCodec = new HexDecoder();
        }
        _codec = _ownCodec;
    }

    ~HexOutputStream() {
        close();
        if (_ownCodec != null) {
            delete _ownCodec;
            _ownCodec = null;
            _codec = null;
        }
    }
}

} // namespace io
