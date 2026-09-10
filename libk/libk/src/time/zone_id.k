/*
 * K Language standard library — ZoneId
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

namespace time {

/**
 * ZoneId — immutable validated IANA-style time zone identifier.
 *
 * Identifies a geographical or logical time-zone location (e.g. "Europe/Paris",
 * "America/New_York", "UTC", "Etc/GMT+1").
 *
 * A ZoneId represents an identity, not a set of rules or an offset.
 * Equality compares identifier text, not current or historical offsets.
 */
public final class ZoneId {
private:
    _name : String;

    ZoneId(name: const String&) {
        _name = name;
    }

public:
    ZoneId() {
        _name = String();
    }

    // ── Factories ────────────────────────────────────────────────────────────

    /**
     * Create a validated ZoneId from the provided identifier string.
     *
     * Validates that:
     * - name is non-empty.
     * - name does not start with '/' and does not end with '/'.
     * - name does not contain consecutive '//' or '..'.
     * - characters are restricted to ASCII letters, digits, '/', '_', '-', and '+'.
     *
     * Throws InvalidTemporalValueException(501) on violation.
     */
    static of(name: const String&) : ZoneId throws(InvalidTemporalValueException) {
        if (name.empty()) {
            throw InvalidTemporalValueException(501);
        }

        len : unsigned int = name.size();
        if (name[0u] == '/') {
            throw InvalidTemporalValueException(501);
        }
        if (name[len - 1u] == '/') {
            throw InvalidTemporalValueException(501);
        }

        prevSlash : bool = false;
        i : unsigned int = 0u;
        while (i < len) {
            c : char = name[i];
            if (c == '/') {
                if (prevSlash) {
                    throw InvalidTemporalValueException(501);
                }
                prevSlash = true;
            } else {
                prevSlash = false;
            }

            validChar : bool = false;
            if (c >= 'a' && c <= 'z') validChar = true;
            else if (c >= 'A' && c <= 'Z') validChar = true;
            else if (c >= '0' && c <= '9') validChar = true;
            else if (c == '/' || c == '_' || c == '-' || c == '+') validChar = true;

            if (!validChar) {
                throw InvalidTemporalValueException(501);
            }
            ++i;
        }

        return ZoneId(name);
    }

    // ── Accessors ────────────────────────────────────────────────────────────

    const name() : String {
        return _name;
    }

    // ── Equality ─────────────────────────────────────────────────────────────

    const operator==(other: const ZoneId&) : bool {
        return _name == other._name;
    }

    const operator!=(other: const ZoneId&) : bool {
        return _name != other._name;
    }
}

} // namespace time
