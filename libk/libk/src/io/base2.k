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
 * Base2 (Binary / '0' and '1') encoder codec.
 */
public class Base2Encoder : public BaseNCodec {
public:
    Base2Encoder(lineLength: unsigned int, separator: const byte[]*)
        : BaseNCodec(lineLength, separator, (byte) 0) {}

    Base2Encoder()
        : BaseNCodec(0u, null, (byte) 0) {}

    override process(buff: const byte[], off: unsigned int, len: unsigned int) {
        pos : unsigned int = 0u;
        while (pos < len) {
            ub : unsigned int = ((unsigned int) buff[off + pos]) & 0xFFu;
            _buf.push(((ub & 0x80u) != 0u) ? (byte) '1' : (byte) '0');
            _buf.push(((ub & 0x40u) != 0u) ? (byte) '1' : (byte) '0');
            _buf.push(((ub & 0x20u) != 0u) ? (byte) '1' : (byte) '0');
            _buf.push(((ub & 0x10u) != 0u) ? (byte) '1' : (byte) '0');
            _buf.push(((ub & 0x08u) != 0u) ? (byte) '1' : (byte) '0');
            _buf.push(((ub & 0x04u) != 0u) ? (byte) '1' : (byte) '0');
            _buf.push(((ub & 0x02u) != 0u) ? (byte) '1' : (byte) '0');
            _buf.push(((ub & 0x01u) != 0u) ? (byte) '1' : (byte) '0');
            _currentLinePos += 8u;
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
 * Base2 (Binary / '0' and '1') decoder codec.
 */
public class Base2Decoder : public BaseNCodec {
public:
    Base2Decoder() : BaseNCodec(0u, null, (byte) 0) {}

    override process(buff: const byte[], off: unsigned int, len: unsigned int) {
        if (_eof) return;
        pos : unsigned int = 0u;
        while (pos < len) {
            b : byte = buff[off + pos];
            if (BaseNCodec::isWhitespace(b)) {
                ++pos;
                continue;
            }
            if (b == (byte) '0' || b == (byte) '1') {
                bit : unsigned long = (b == (byte) '1') ? 1uL : 0uL;
                _work = (_work << 1) | bit;
                _state = (_state + 1u) % 8u;
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
 * Stream decorator for reading Base2-encoded or Base2-decoded data.
 */
public class Base2InputStream : public CodecInputStream {
protected:
    _ownCodec : Codec!;

public:
    /** Construct a Base2 decoding input stream borrowing input. */
    Base2InputStream(input: InputStream<byte>*)
        : CodecInputStream(input, null, 4096u), _ownCodec(null) {
        _ownCodec = new Base2Decoder();
        _codec = _ownCodec;
    }

    /** Construct a Base2 encoding/decoding input stream borrowing input. */
    Base2InputStream(input: InputStream<byte>*, encode: bool, lineLength: unsigned int, separator: const byte[]*)
        : CodecInputStream(input, null, 4096u), _ownCodec(null) {
        if (encode) {
            _ownCodec = new Base2Encoder(lineLength, separator);
        } else {
            _ownCodec = new Base2Decoder();
        }
        _codec = _ownCodec;
    }

    /** Construct a Base2 encoding/decoding input stream borrowing input. */
    Base2InputStream(input: InputStream<byte>*, encode: bool)
        : CodecInputStream(input, null, 4096u), _ownCodec(null) {
        if (encode) {
            _ownCodec = new Base2Encoder();
        } else {
            _ownCodec = new Base2Decoder();
        }
        _codec = _ownCodec;
    }

    ~Base2InputStream() {
        close();
        if (_ownCodec != null) {
            delete _ownCodec;
            _ownCodec = null;
            _codec = null;
        }
    }
}


/**
 * Stream decorator for writing Base2-encoded or Base2-decoded data.
 */
public class Base2OutputStream : public CodecOutputStream {
protected:
    _ownCodec : Codec!;

public:
    /** Construct a Base2 encoding output stream borrowing output. */
    Base2OutputStream(output: OutputStream<byte>*)
        : CodecOutputStream(output, null), _ownCodec(null) {
        _ownCodec = new Base2Encoder();
        _codec = _ownCodec;
    }

    /** Construct a Base2 encoding/decoding output stream borrowing output. */
    Base2OutputStream(output: OutputStream<byte>*, encode: bool, lineLength: unsigned int, separator: const byte[]*)
        : CodecOutputStream(output, null), _ownCodec(null) {
        if (encode) {
            _ownCodec = new Base2Encoder(lineLength, separator);
        } else {
            _ownCodec = new Base2Decoder();
        }
        _codec = _ownCodec;
    }

    /** Construct a Base2 encoding/decoding output stream borrowing output. */
    Base2OutputStream(output: OutputStream<byte>*, encode: bool)
        : CodecOutputStream(output, null), _ownCodec(null) {
        if (encode) {
            _ownCodec = new Base2Encoder();
        } else {
            _ownCodec = new Base2Decoder();
        }
        _codec = _ownCodec;
    }

    ~Base2OutputStream() {
        close();
        if (_ownCodec != null) {
            delete _ownCodec;
            _ownCodec = null;
            _codec = null;
        }
    }
}

} // namespace io
