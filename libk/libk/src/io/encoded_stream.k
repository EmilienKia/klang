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
 * Byte order (endianness) for multi-byte encodings.
 */
public enum ByteOrder {
    BigEndian;
    LittleEndian;
    Native;
}

/**
 * Action to take when an encoding or decoding error occurs.
 */
public enum CodingErrorAction {
    Report;
    Replace;
    Ignore;
}

/**
 * Interface for streams of UTF-8 code units (unsigned bytes).
 */
public interface Utf8Input : public InputStream<unsigned byte> {
    default const encoding() : Encoding {
        return Encoding::UTF8;
    }
}

/**
 * Marker interface for streams of validated UTF-8 code units.
 */
public interface ValidUtf8Input : public Utf8Input {
}

/**
 * Interface for output streams of UTF-8 code units.
 */
public interface Utf8Output : public OutputStream<unsigned byte> {
    default const encoding() : Encoding {
        return Encoding::UTF8;
    }
}

/**
 * Marker interface for output streams guaranteed to receive valid UTF-8.
 */
public interface ValidUtf8Output : public Utf8Output {
}

/**
 * Interface for streams of UTF-16 code units (unsigned shorts).
 */
public interface Utf16Input : public InputStream<unsigned short> {
    default const encoding() : Encoding {
        return Encoding::UTF16;
    }
}

/**
 * Interface for output streams of UTF-16 code units.
 */
public interface Utf16Output : public OutputStream<unsigned short> {
    default const encoding() : Encoding {
        return Encoding::UTF16;
    }
}

/**
 * Filter that validates UTF-8 byte streams on the fly, transforming any byte input stream
 * into a ValidUtf8Input stream.
 */
public class Utf8Validator : public ValidUtf8Input {
    _in      : InputStream<byte>*;
    _inOwner : InputStream<byte>!;
    _action  : CodingErrorAction;
    _offset  : unsigned long;

    _pending    : unsigned byte[4];
    _pendingLen : unsigned int;
    _expectedLen: unsigned int;

    _outBuf   : unsigned byte[8];
    _outHead  : unsigned int;
    _outTail  : unsigned int;
    _outCount : unsigned int;

public:
    Utf8Validator(input: InputStream<byte>*, action: CodingErrorAction)
        : _in(input), _inOwner(null), _action(action), _offset(0uL), _pendingLen(0u), _expectedLen(0u),
          _outHead(0u), _outTail(0u), _outCount(0u) {}

    Utf8Validator(input: InputStream<byte>*)
        : _in(input), _inOwner(null), _action(CodingErrorAction::Report), _offset(0uL), _pendingLen(0u), _expectedLen(0u),
          _outHead(0u), _outTail(0u), _outCount(0u) {}

    Utf8Validator() : _in(null), _inOwner(null), _action(CodingErrorAction::Report), _offset(0uL), _pendingLen(0u), _expectedLen(0u),
                      _outHead(0u), _outTail(0u), _outCount(0u) {}

    ~Utf8Validator() {
        close();
    }

    override const encoding() : Encoding {
        return Encoding::UTF8;
    }

    override read() : ::k::Optional<unsigned byte> {
        while (_outCount == 0u) {
            if (_in == null) {
                return ::k::Optional<unsigned byte>();
            }
            raw : ::k::Optional<byte> = _in->read();
            if (!raw.hasValue()) {
                if (_pendingLen > 0u) {
                    _handleError(_pendingLen);
                    _pendingLen = 0u;
                    _expectedLen = 0u;
                    if (_outCount > 0u) {
                        break;
                    }
                }
                return ::k::Optional<unsigned byte>();
            }

            b : unsigned byte = (unsigned byte) raw.get();
            _processByte(b);
        }

        if (_outCount > 0u) {
            val : unsigned byte = _outBuf[_outHead];
            _outHead = (_outHead + 1u) % 8u;
            --_outCount;
            return ::k::Optional<unsigned byte>(val);
        }
        return ::k::Optional<unsigned byte>();
    }

    override read(buf: unsigned byte[], off: unsigned int, len: unsigned int) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        if (len == 0u) {
            zero : unsigned int = 0u;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(zero);
        }
        count : unsigned int = 0u;
        while (count < len) {
            next : ::k::Optional<unsigned byte> = read();
            if (!next.hasValue()) {
                break;
            }
            buf[off + count] = next.get();
            ++count;
        }
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(count);
    }

    override read(buf: unsigned byte[]) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return read(buf, 0u, (unsigned int) buf.size);
    }

    override skip(n: unsigned long) : unsigned long {
        skipped : unsigned long = 0uL;
        while (skipped < n) {
            next : ::k::Optional<unsigned byte> = read();
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
        if (underlying.hasError()) {
            if (_outCount > 0u) return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(_outCount);
            return underlying;
        }
        total : unsigned int = _outCount + underlying.getResult();
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(total);
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
    _queuePush(b: unsigned byte) {
        _outBuf[_outTail] = b;
        _outTail = (_outTail + 1u) % 8u;
        ++_outCount;
    }

    _processByte(b: unsigned byte) {
        ++_offset;
        ub : unsigned int = (unsigned int) b;
        if (_pendingLen == 0u) {
            if (ub < 0x80u) {
                _queuePush(b);
            } else if (ub >= 0xC2u && ub <= 0xDFu) {
                _pending[0] = b;
                _pendingLen = 1u;
                _expectedLen = 2u;
            } else if (ub >= 0xE0u && ub <= 0xEFu) {
                _pending[0] = b;
                _pendingLen = 1u;
                _expectedLen = 3u;
            } else if (ub >= 0xF0u && ub <= 0xF4u) {
                _pending[0] = b;
                _pendingLen = 1u;
                _expectedLen = 4u;
            } else {
                _handleError(1u);
            }
        } else {
            // Continuation byte validation
            _pending[_pendingLen] = b;
            ++_pendingLen;

            // Intermediate checks
            if (_pendingLen == 2u) {
                b0 : unsigned int = (unsigned int) _pending[0];
                b1 : unsigned int = ub;
                if (b0 == 0xE0u && (b1 < 0xA0u || b1 > 0xBFu)) {
                    _handleError(1u);
                    _resetTo(b);
                    return;
                }
                if (b0 == 0xEDu && (b1 < 0x80u || b1 > 0x9Fu)) {
                    _handleError(1u);
                    _resetTo(b);
                    return;
                }
                if (b0 == 0xF0u && (b1 < 0x90u || b1 > 0xBFu)) {
                    _handleError(1u);
                    _resetTo(b);
                    return;
                }
                if (b0 == 0xF4u && (b1 < 0x80u || b1 > 0x8Fu)) {
                    _handleError(1u);
                    _resetTo(b);
                    return;
                }
                if (b1 < 0x80u || b1 > 0xBFu) {
                    _handleError(1u);
                    _resetTo(b);
                    return;
                }
            } else if (ub < 0x80u || ub > 0xBFu) {
                _handleError(_pendingLen - 1u);
                _resetTo(b);
                return;
            }

            if (_pendingLen == _expectedLen) {
                i : unsigned int = 0u;
                while (i < _expectedLen) {
                    _queuePush(_pending[i]);
                    ++i;
                }
                _pendingLen = 0u;
                _expectedLen = 0u;
            }
        }
    }

    _resetTo(b: unsigned byte) {
        _pendingLen = 0u;
        _expectedLen = 0u;
        _processByte(b);
    }

    _handleError(len: unsigned int) {
        if (_action == CodingErrorAction::Report) {
            throw MalformedInputException(_offset - (unsigned long) len, len);
        } else if (_action == CodingErrorAction::Replace) {
            // U+FFFD in UTF-8: 0xEF, 0xBF, 0xBD
            _queuePush((unsigned byte) 0xEFu);
            _queuePush((unsigned byte) 0xBFu);
            _queuePush((unsigned byte) 0xBDu);
        }
        // If Ignore, do nothing
    }
}

} // namespace io
