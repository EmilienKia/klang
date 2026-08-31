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
 * Common base class for Base-N codecs (Base64, Base32, Base16/Hex, Base2/Bin).
 */
public abstract class BaseNCodec : public Codec {
public:
    static const PAD_DEFAULT        : byte = (byte) '=';
    static const MIME_CHUNK_SIZE    : unsigned int = 76u;
    static const PEM_CHUNK_SIZE     : unsigned int = 64u;
    static const MASK_5BITS         : unsigned long = 0x1FuL;
    static const MASK_6BITS         : unsigned long = 0x3FuL;
    static const MASK_8BITS         : unsigned long = 0xFFuL;

protected:
    _pad            : byte = (byte) '=';
    _lineLength     : unsigned int = 0u;
    _currentLinePos : unsigned int = 0u;
    _separator      : byte[]!;

    _addSeparator() {
        if (_separator != null && _separator.size > 0) {
            i : unsigned int = 0u;
            while (i < (unsigned int) _separator.size) {
                _buf.push(_separator[i]);
                ++i;
            }
        }
        _currentLinePos = 0u;
    }

public:
    BaseNCodec(lineLength: unsigned int, separator: const byte[]*, pad: byte)
        : Codec(), _pad(pad), _lineLength(lineLength), _currentLinePos(0u), _separator(null) {
        if (separator != null && separator->size > 0 && lineLength > 0u) {
            n : unsigned int = (unsigned int) separator->size;
            _separator = new byte[n];
            i : unsigned int = 0u;
            while (i < n) {
                _separator[i] = (*separator)[i];
                ++i;
            }
        } else {
            _lineLength = 0u;
        }
    }

    BaseNCodec(lineLength: unsigned int, separator: const byte[]*)
        : BaseNCodec(lineLength, separator, PAD_DEFAULT) {}

    BaseNCodec()
        : BaseNCodec(0u, null, PAD_DEFAULT) {}

    ~BaseNCodec() {
        if (_separator != null) {
            delete _separator;
            _separator = null;
        }
    }

    static isWhitespace(c: byte) : bool {
        return c == (byte) ' ' || c == (byte) '\r' || c == (byte) '\n' || c == (byte) '\t';
    }
}

} // namespace io
