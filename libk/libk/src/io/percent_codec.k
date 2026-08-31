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
 * Percent-encoding (URI / URL percent-encoding) encoder codec.
 *
 * Encodes characters outside the RFC 3986 unreserved set into '%XX' uppercase hex sequences.
 */
public class PercentEncoder : public Codec {
public:
    static isUnreserved(b: byte) : bool {
        ub : unsigned int = ((unsigned int) b) & 0xFFu;
        if (ub >= (unsigned int) 'A' && ub <= (unsigned int) 'Z') return true;
        if (ub >= (unsigned int) 'a' && ub <= (unsigned int) 'z') return true;
        if (ub >= (unsigned int) '0' && ub <= (unsigned int) '9') return true;
        if (b == (byte) '-' || b == (byte) '_' || b == (byte) '.' || b == (byte) '~') return true;
        return false;
    }

    PercentEncoder() : Codec() {}

    override process(buff: const byte[], off: unsigned int, len: unsigned int) {
        pos : unsigned int = 0u;
        while (pos < len) {
            b : byte = buff[off + pos];
            if (isUnreserved(b)) {
                _buf.push(b);
            } else {
                ub : unsigned int = ((unsigned int) b) & 0xFFu;
                _buf.push((byte) '%');
                _buf.push(HexEncoder::encodeNibble(ub >> 4, true));
                _buf.push(HexEncoder::encodeNibble(ub, true));
            }
            ++pos;
        }
    }
}


/**
 * Percent-encoding decoder codec.
 *
 * Decodes '%XX' hexadecimal sequences into their corresponding byte values.
 */
public class PercentDecoder : public Codec {
public:
    PercentDecoder() : Codec() {}

    override process(buff: const byte[], off: unsigned int, len: unsigned int) {
        pos : unsigned int = 0u;
        while (pos < len) {
            b : byte = buff[off + pos];
            if (_state == 0u) {
                if (b == (byte) '%') {
                    _state = 1u;
                } else {
                    _buf.push(b);
                }
            } else if (_state == 1u) {
                n : int = HexDecoder::decodeNibble(b);
                if (n >= 0) {
                    _work = (unsigned long) n;
                    _state = 2u;
                } else {
                    _buf.push((byte) '%');
                    _buf.push(b);
                    _state = 0u;
                    _work = 0uL;
                }
            } else if (_state == 2u) {
                n : int = HexDecoder::decodeNibble(b);
                if (n >= 0) {
                    _work = (_work << 4) | ((unsigned long) n);
                    _buf.push((byte) (_work & 0xFFuL));
                } else {
                    _buf.push((byte) '%');
                    _buf.push(HexEncoder::encodeNibble((unsigned int) _work, true));
                    _buf.push(b);
                }
                _state = 0u;
                _work = 0uL;
            }
            ++pos;
        }
    }

    override finalize() {
        if (_eof) return;
        _eof = true;
        if (_state == 1u) {
            _buf.push((byte) '%');
        } else if (_state == 2u) {
            _buf.push((byte) '%');
            _buf.push(HexEncoder::encodeNibble((unsigned int) _work, true));
        }
        _state = 0u;
        _work = 0uL;
    }
}


/**
 * Form-URL (application/x-www-form-urlencoded) encoder codec.
 *
 * Encodes spaces as '+' and non-unreserved characters as '%XX'.
 */
public class FormUrlEncoder : public Codec {
public:
    FormUrlEncoder() : Codec() {}

    override process(buff: const byte[], off: unsigned int, len: unsigned int) {
        pos : unsigned int = 0u;
        while (pos < len) {
            b : byte = buff[off + pos];
            if (b == (byte) ' ') {
                _buf.push((byte) '+');
            } else if (PercentEncoder::isUnreserved(b)) {
                _buf.push(b);
            } else {
                ub : unsigned int = ((unsigned int) b) & 0xFFu;
                _buf.push((byte) '%');
                _buf.push(HexEncoder::encodeNibble(ub >> 4, true));
                _buf.push(HexEncoder::encodeNibble(ub, true));
            }
            ++pos;
        }
    }
}


/**
 * Form-URL (application/x-www-form-urlencoded) decoder codec.
 *
 * Decodes '+' as space and '%XX' sequences into their byte values.
 */
public class FormUrlDecoder : public Codec {
public:
    FormUrlDecoder() : Codec() {}

    override process(buff: const byte[], off: unsigned int, len: unsigned int) {
        pos : unsigned int = 0u;
        while (pos < len) {
            b : byte = buff[off + pos];
            if (_state == 0u) {
                if (b == (byte) '%') {
                    _state = 1u;
                } else if (b == (byte) '+') {
                    _buf.push((byte) ' ');
                } else {
                    _buf.push(b);
                }
            } else if (_state == 1u) {
                n : int = HexDecoder::decodeNibble(b);
                if (n >= 0) {
                    _work = (unsigned long) n;
                    _state = 2u;
                } else {
                    _buf.push((byte) '%');
                    _buf.push(b);
                    _state = 0u;
                    _work = 0uL;
                }
            } else if (_state == 2u) {
                n : int = HexDecoder::decodeNibble(b);
                if (n >= 0) {
                    _work = (_work << 4) | ((unsigned long) n);
                    _buf.push((byte) (_work & 0xFFuL));
                } else {
                    _buf.push((byte) '%');
                    _buf.push(HexEncoder::encodeNibble((unsigned int) _work, true));
                    _buf.push(b);
                }
                _state = 0u;
                _work = 0uL;
            }
            ++pos;
        }
    }

    override finalize() {
        if (_eof) return;
        _eof = true;
        if (_state == 1u) {
            _buf.push((byte) '%');
        } else if (_state == 2u) {
            _buf.push((byte) '%');
            _buf.push(HexEncoder::encodeNibble((unsigned int) _work, true));
        }
        _state = 0u;
        _work = 0uL;
    }
}


/**
 * Stream decorator for reading Percent-encoded or Percent-decoded data.
 */
public class PercentInputStream : public CodecInputStream {
protected:
    _ownCodec : Codec!;

public:
    /** Construct a Percent decoding input stream borrowing input. */
    PercentInputStream(input: InputStream<byte>*)
        : CodecInputStream(input, null, 4096u), _ownCodec(null) {
        _ownCodec = new PercentDecoder();
        _codec = _ownCodec;
    }

    /** Construct a Percent encoding/decoding input stream borrowing input. */
    PercentInputStream(input: InputStream<byte>*, encode: bool)
        : CodecInputStream(input, null, 4096u), _ownCodec(null) {
        if (encode) {
            _ownCodec = new PercentEncoder();
        } else {
            _ownCodec = new PercentDecoder();
        }
        _codec = _ownCodec;
    }

    ~PercentInputStream() {
        close();
        if (_ownCodec != null) {
            delete _ownCodec;
            _ownCodec = null;
            _codec = null;
        }
    }
}


/**
 * Stream decorator for writing Percent-encoded or Percent-decoded data.
 */
public class PercentOutputStream : public CodecOutputStream {
protected:
    _ownCodec : Codec!;

public:
    /** Construct a Percent encoding output stream borrowing output. */
    PercentOutputStream(output: OutputStream<byte>*)
        : CodecOutputStream(output, null), _ownCodec(null) {
        _ownCodec = new PercentEncoder();
        _codec = _ownCodec;
    }

    /** Construct a Percent encoding/decoding output stream borrowing output. */
    PercentOutputStream(output: OutputStream<byte>*, encode: bool)
        : CodecOutputStream(output, null), _ownCodec(null) {
        if (encode) {
            _ownCodec = new PercentEncoder();
        } else {
            _ownCodec = new PercentDecoder();
        }
        _codec = _ownCodec;
    }

    ~PercentOutputStream() {
        close();
        if (_ownCodec != null) {
            delete _ownCodec;
            _ownCodec = null;
            _codec = null;
        }
    }
}


/**
 * Stream decorator for reading Form-URL-encoded or Form-URL-decoded data.
 */
public class FormUrlInputStream : public CodecInputStream {
protected:
    _ownCodec : Codec!;

public:
    /** Construct a Form-URL decoding input stream borrowing input. */
    FormUrlInputStream(input: InputStream<byte>*)
        : CodecInputStream(input, null, 4096u), _ownCodec(null) {
        _ownCodec = new FormUrlDecoder();
        _codec = _ownCodec;
    }

    /** Construct a Form-URL encoding/decoding input stream borrowing input. */
    FormUrlInputStream(input: InputStream<byte>*, encode: bool)
        : CodecInputStream(input, null, 4096u), _ownCodec(null) {
        if (encode) {
            _ownCodec = new FormUrlEncoder();
        } else {
            _ownCodec = new FormUrlDecoder();
        }
        _codec = _ownCodec;
    }

    ~FormUrlInputStream() {
        close();
        if (_ownCodec != null) {
            delete _ownCodec;
            _ownCodec = null;
            _codec = null;
        }
    }
}


/**
 * Stream decorator for writing Form-URL-encoded or Form-URL-decoded data.
 */
public class FormUrlOutputStream : public CodecOutputStream {
protected:
    _ownCodec : Codec!;

public:
    /** Construct a Form-URL encoding output stream borrowing output. */
    FormUrlOutputStream(output: OutputStream<byte>*)
        : CodecOutputStream(output, null), _ownCodec(null) {
        _ownCodec = new FormUrlEncoder();
        _codec = _ownCodec;
    }

    /** Construct a Form-URL encoding/decoding output stream borrowing output. */
    FormUrlOutputStream(output: OutputStream<byte>*, encode: bool)
        : CodecOutputStream(output, null), _ownCodec(null) {
        if (encode) {
            _ownCodec = new FormUrlEncoder();
        } else {
            _ownCodec = new FormUrlDecoder();
        }
        _codec = _ownCodec;
    }

    ~FormUrlOutputStream() {
        close();
        if (_ownCodec != null) {
            delete _ownCodec;
            _ownCodec = null;
            _codec = null;
        }
    }
}

} // namespace io
