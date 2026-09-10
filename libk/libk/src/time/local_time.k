/*
 * K Language standard library — LocalTime
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
 * LocalTime — an immutable local clock label with hour, minute, second,
 * and nanosecond accessors.
 *
 * Fields:
 * - hour: 0..23 (rejects 24:00)
 * - minute: 0..59
 * - second: 0..59, or 60 ONLY if hour == 23 && minute == 59 (structural leap second)
 * - nano: 0..999_999_999
 */
public final struct LocalTime {
private:
    _hour   : int;
    _minute : int;
    _second : int;
    _nano   : int;

public:
    LocalTime() {
        _hour = 0;
        _minute = 0;
        _second = 0;
        _nano = 0;
    }

    LocalTime(hour: int, minute: int, second: int, nano: int) {
        _hour = hour;
        _minute = minute;
        _second = second;
        _nano = nano;
    }

    // ── Factories ────────────────────────────────────────────────────────────

    static midnight() : LocalTime {
        return LocalTime(0, 0, 0, 0);
    }

    static of(hour: int, minute: int) : LocalTime throws(InvalidTemporalValueException) {
        return of(hour, minute, 0, 0);
    }

    static of(hour: int, minute: int, second: int) : LocalTime throws(InvalidTemporalValueException) {
        return of(hour, minute, second, 0);
    }

    static of(hour: int, minute: int, second: int, nano: int) : LocalTime throws(InvalidTemporalValueException) {
        if (hour < 0 || hour > 23) {
            throw InvalidTemporalValueException();
        }
        if (minute < 0 || minute > 59) {
            throw InvalidTemporalValueException();
        }
        if (nano < 0 || nano >= 1000000000) {
            throw InvalidTemporalValueException();
        }
        if (second < 0 || second > 60) {
            throw InvalidTemporalValueException();
        }
        if (second == 60 && (hour != 23 || minute != 59)) {
            throw InvalidTemporalValueException();
        }
        return LocalTime(hour, minute, second, nano);
    }

    // ── Field accessors ──────────────────────────────────────────────────────

    const hour() : int {
        return _hour;
    }

    const minute() : int {
        return _minute;
    }

    const second() : int {
        return _second;
    }

    const nano() : int {
        return _nano;
    }

    const isStructuralLeapSecond() : bool {
        return _second == 60;
    }

    // ── Unit conversions ─────────────────────────────────────────────────────

    const toNanoOfDay() : long {
        sec : long = ((long) _hour) * 3600L + ((long) _minute) * 60L + ((long) _second);
        return sec * NANOS_PER_SECOND + (long) _nano;
    }

    // ── Arithmetic ───────────────────────────────────────────────────────────

    const plus(duration: const Duration&) : LocalTime
        throws(InvalidTemporalValueException, TemporalArithmeticException) {
        if (isStructuralLeapSecond()) {
            throw InvalidTemporalValueException();
        }
        durSecMod : long = floorMod(duration.secondsPart(), SECONDS_PER_DAY);
        newNanos : int = _nano + duration.nanoAdjustment();
        carrySec : long = 0L;
        if (newNanos >= 1000000000) {
            newNanos = newNanos - 1000000000;
            carrySec = 1L;
        }
        curSec : long = ((long) _hour) * 3600L + ((long) _minute) * 60L + ((long) _second);
        newSec : long = (curSec + durSecMod + carrySec) % SECONDS_PER_DAY;
        h : int = (int) (newSec / 3600L);
        m : int = (int) ((newSec % 3600L) / 60L);
        s : int = (int) (newSec % 60L);
        return LocalTime(h, m, s, newNanos);
    }

    const minus(duration: const Duration&) : LocalTime
        throws(InvalidTemporalValueException, TemporalArithmeticException) {
        if (isStructuralLeapSecond()) {
            throw InvalidTemporalValueException();
        }
        durSecMod : long = floorMod(duration.secondsPart(), SECONDS_PER_DAY);
        newNanos : int = _nano - duration.nanoAdjustment();
        borrowSec : long = 0L;
        if (newNanos < 0) {
            newNanos = newNanos + 1000000000;
            borrowSec = 1L;
        }
        curSec : long = ((long) _hour) * 3600L + ((long) _minute) * 60L + ((long) _second);
        newSec : long = floorMod(curSec - durSecMod - borrowSec, SECONDS_PER_DAY);
        h : int = (int) (newSec / 3600L);
        m : int = (int) ((newSec % 3600L) / 60L);
        s : int = (int) (newSec % 60L);
        return LocalTime(h, m, s, newNanos);
    }

    // ── Comparisons ──────────────────────────────────────────────────────────

    const compareTo(other: const LocalTime&) : int {
        if (_hour < other._hour) return -1;
        if (_hour > other._hour) return 1;
        if (_minute < other._minute) return -1;
        if (_minute > other._minute) return 1;
        if (_second < other._second) return -1;
        if (_second > other._second) return 1;
        if (_nano < other._nano) return -1;
        if (_nano > other._nano) return 1;
        return 0;
    }

    const operator==(other: const LocalTime&) : bool {
        return _hour == other._hour && _minute == other._minute &&
               _second == other._second && _nano == other._nano;
    }

    const operator!=(other: const LocalTime&) : bool {
        return _hour != other._hour || _minute != other._minute ||
               _second != other._second || _nano != other._nano;
    }

    const operator<(other: const LocalTime&) : bool {
        if (_hour != other._hour) return _hour < other._hour;
        if (_minute != other._minute) return _minute < other._minute;
        if (_second != other._second) return _second < other._second;
        return _nano < other._nano;
    }

    const operator<=(other: const LocalTime&) : bool {
        if (_hour != other._hour) return _hour < other._hour;
        if (_minute != other._minute) return _minute < other._minute;
        if (_second != other._second) return _second < other._second;
        return _nano <= other._nano;
    }

    const operator>(other: const LocalTime&) : bool {
        if (_hour != other._hour) return _hour > other._hour;
        if (_minute != other._minute) return _minute > other._minute;
        if (_second != other._second) return _second > other._second;
        return _nano > other._nano;
    }

    const operator>=(other: const LocalTime&) : bool {
        if (_hour != other._hour) return _hour > other._hour;
        if (_minute != other._minute) return _minute > other._minute;
        if (_second != other._second) return _second > other._second;
        return _nano >= other._nano;
    }

    // ── Operator overloads ───────────────────────────────────────────────────

    const operator+(duration: const Duration&) : LocalTime
        throws(InvalidTemporalValueException, TemporalArithmeticException) {
        return plus(duration);
    }

    const operator-(duration: const Duration&) : LocalTime
        throws(InvalidTemporalValueException, TemporalArithmeticException) {
        return minus(duration);
    }
}

} // namespace time
