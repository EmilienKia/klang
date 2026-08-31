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
 * Base class for character encoding and decoding exceptions.
 */
public class CharacterCodingException : public IOException {
    _offset : unsigned long;
    _length : unsigned int;

public:
    CharacterCodingException() : IOException(210, 0), _offset(0uL), _length(0u) {}
    CharacterCodingException(offset: unsigned long, len: unsigned int) : IOException(210, 0), _offset(offset), _length(len) {}
    CharacterCodingException(code: int, offset: unsigned long, len: unsigned int) : IOException(code, 0), _offset(offset), _length(len) {}

    const getOffset() : unsigned long {
        return _offset;
    }

    const getLength() : unsigned int {
        return _length;
    }
}

/**
 * Thrown when an input byte or code-unit sequence is not legal for the given charset.
 */
public class MalformedInputException : public CharacterCodingException {
public:
    MalformedInputException() : CharacterCodingException(211, 0uL, 0u) {}
    MalformedInputException(offset: unsigned long, len: unsigned int) : CharacterCodingException(211, offset, len) {}
}

/**
 * Thrown when an input character cannot be mapped to an output byte sequence.
 */
public class UnmappableCharacterException : public CharacterCodingException {
    _codePoint : char;

public:
    UnmappableCharacterException() : CharacterCodingException(212, 0uL, 0u), _codePoint((char) 0) {}
    UnmappableCharacterException(cp: char, offset: unsigned long) : CharacterCodingException(212, offset, 1u), _codePoint(cp) {}

    const getCodePoint() : char {
        return _codePoint;
    }
}

/**
 * Thrown to indicate that the application has attempted to convert a string to one of the numeric types,
 * but that the string does not have the appropriate format.
 */
public class NumberFormatException : public IllegalArgumentException {
    _token : String;

public:
    NumberFormatException() : IllegalArgumentException(220) {}
    NumberFormatException(token: const String&) : IllegalArgumentException(220), _token(token) {}

    const getToken() : const String& {
        return _token;
    }
}

} // namespace io
