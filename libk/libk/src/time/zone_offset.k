/*
 * K Language standard library — ZoneOffset
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
 * ZoneOffset — an immutable signed UTC displacement.
 *
 * Models a fixed numeric offset from UTC in seconds.
 * Range: -18:00 to +18:00 (-64800 to +64800 seconds, inclusive).
 */
public final struct ZoneOffset {
private:
    _totalSeconds : int;

public:
    ZoneOffset() {
        _totalSeconds = 0;
    }

    ZoneOffset(totalSeconds: int) {
        _totalSeconds = totalSeconds;
    }

    // ── Factories ────────────────────────────────────────────────────────────

    static utc() : ZoneOffset {
        return ZoneOffset(0);
    }

    static ofTotalSeconds(seconds: int) : ZoneOffset
        throws(InvalidTemporalValueException) {
        if (seconds < -64800 || seconds > 64800) {
            throw InvalidTemporalValueException();
        }
        return ZoneOffset(seconds);
    }

    static parseIso(text: const String&) : ZoneOffset
        throws(TemporalParseException, InvalidTemporalValueException) {
        return IsoReader::parseZoneOffset(text);
    }

    static parseIso(text: const char[]) : ZoneOffset
        throws(TemporalParseException, InvalidTemporalValueException) {
        return parseIso(String(text));
    }

    // ── Field accessors and properties ───────────────────────────────────────

    const totalSeconds() : int {
        return _totalSeconds;
    }

    const isUtc() : bool {
        return _totalSeconds == 0;
    }

    const toIsoString() : String {
        return IsoWriter::formatZoneOffset(this);
    }

    // ── Comparison ───────────────────────────────────────────────────────────

    const compareTo(other: const ZoneOffset&) : int {
        if (_totalSeconds < other._totalSeconds) {
            return -1;
        }
        if (_totalSeconds > other._totalSeconds) {
            return 1;
        }
        return 0;
    }

    const operator==(other: const ZoneOffset&) : bool {
        return _totalSeconds == other._totalSeconds;
    }

    const operator!=(other: const ZoneOffset&) : bool {
        return _totalSeconds != other._totalSeconds;
    }

    const operator<(other: const ZoneOffset&) : bool {
        return _totalSeconds < other._totalSeconds;
    }

    const operator<=(other: const ZoneOffset&) : bool {
        return _totalSeconds <= other._totalSeconds;
    }

    const operator>(other: const ZoneOffset&) : bool {
        return _totalSeconds > other._totalSeconds;
    }

    const operator>=(other: const ZoneOffset&) : bool {
        return _totalSeconds >= other._totalSeconds;
    }
}

} // namespace time
