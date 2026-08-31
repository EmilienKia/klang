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
 * Thrown to indicate that a string could not be parsed as a URI reference.
 */
public class UriSyntaxException : public IllegalArgumentException {
    _input  : String;
    _reason : String;
    _index  : int;

public:
    UriSyntaxException() : IllegalArgumentException(230), _index(-1) {}

    UriSyntaxException(input: const String&, reason: const String&, index: int)
        : IllegalArgumentException(230), _input(input), _reason(reason), _index(index) {}

    UriSyntaxException(input: const String&, reason: const String&)
        : IllegalArgumentException(230), _input(input), _reason(reason), _index(-1) {}

    const getInput() : const String& {
        return _input;
    }

    const getReason() : const String& {
        return _reason;
    }

    const getIndex() : int {
        return _index;
    }
}

} // namespace io
