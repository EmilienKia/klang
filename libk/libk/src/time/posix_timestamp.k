/*
 * K Language standard library — PosixTimestamp
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
 * PosixTimestamp — immutable normalized POSIX timestamp coordinate.
 *
 * Represents seconds and nanoseconds since the Unix epoch (1970-01-01T00:00:00 UTC)
 * according to POSIX conventions.
 * Invariant: 0 <= nanoAdjustment < 1_000_000_000.
 */
public final struct PosixTimestamp {
private:
    _seconds : long;
    _nanos   : int;

public:
    PosixTimestamp() {
        _seconds = 0L;
        _nanos = 0;
    }

    PosixTimestamp(seconds: long, nanos: int) {
        _seconds = seconds;
        _nanos = nanos;
    }

    // ── Factories ────────────────────────────────────────────────────────────

    static ofSeconds(seconds: long) : PosixTimestamp {
        return PosixTimestamp(seconds, 0);
    }

    static ofSeconds(seconds: long, nanos: int) : PosixTimestamp
        throws(TemporalArithmeticException) {
        norm : NormalizedSecondNanos = NormalizedSecondNanos(seconds, nanos);
        return PosixTimestamp(norm.seconds, norm.nanos);
    }

    static systemNow() : PosixTimestamp throws(TimeDataUnavailableException) {
        status : int = __k_time_clock_read_realtime();
        if (status != 0) {
            code : int = __k_time_clock_map_status(status);
            throw TimeDataUnavailableException(code);
        }
        sec : long = __k_time_clock_get_reading_sec();
        nano : int = __k_time_clock_get_reading_nano();
        return PosixTimestamp(sec, nano);
    }

    // ── Accessors ────────────────────────────────────────────────────────────

    const seconds() : long {
        return _seconds;
    }

    const nanoAdjustment() : int {
        return _nanos;
    }

    // ── Conversion ───────────────────────────────────────────────────────────

    const toInstant() : Instant throws(TimeScaleDataUnavailableException) {
        // Premature conversion in Phase 2 throws TimeScaleDataUnavailableException;
        // Phase 3 supplies the leap table and conversion policy.
        throw TimeScaleDataUnavailableException(513);
    }

    // ── Arithmetic ───────────────────────────────────────────────────────────

    const plus(duration: const Duration&) : PosixTimestamp
        throws(TemporalArithmeticException) {
        sec : long = addChecked(_seconds, duration.secondsPart());
        nanosSum : int = _nanos + duration.nanoAdjustment();
        if (nanosSum >= 1000000000) {
            sec = addChecked(sec, 1L);
            nanosSum = nanosSum - 1000000000;
        }
        return PosixTimestamp(sec, nanosSum);
    }

    const minus(duration: const Duration&) : PosixTimestamp
        throws(TemporalArithmeticException) {
        sec : long = subtractChecked(_seconds, duration.secondsPart());
        nanosDiff : int = _nanos - duration.nanoAdjustment();
        if (nanosDiff < 0) {
            sec = subtractChecked(sec, 1L);
            nanosDiff = nanosDiff + 1000000000;
        }
        return PosixTimestamp(sec, nanosDiff);
    }

    const minus(other: const PosixTimestamp&) : Duration
        throws(TemporalArithmeticException) {
        sec : long = subtractChecked(_seconds, other._seconds);
        nanosDiff : int = _nanos - other._nanos;
        if (nanosDiff < 0) {
            sec = subtractChecked(sec, 1L);
            nanosDiff = nanosDiff + 1000000000;
        }
        return Duration(sec, nanosDiff);
    }

    const until(other: const PosixTimestamp&) : Duration
        throws(TemporalArithmeticException) {
        return other.minus(PosixTimestamp(_seconds, _nanos));
    }

    // ── Comparisons & Relational Operators ────────────────────────────────────

    const compareTo(other: const PosixTimestamp&) : int {
        if (_seconds < other._seconds) return -1;
        if (_seconds > other._seconds) return 1;
        if (_nanos < other._nanos) return -1;
        if (_nanos > other._nanos) return 1;
        return 0;
    }

    const operator==(other: const PosixTimestamp&) : bool {
        return _seconds == other._seconds && _nanos == other._nanos;
    }

    const operator!=(other: const PosixTimestamp&) : bool {
        return _seconds != other._seconds || _nanos != other._nanos;
    }

    const operator<(other: const PosixTimestamp&) : bool {
        if (_seconds != other._seconds) return _seconds < other._seconds;
        return _nanos < other._nanos;
    }

    const operator<=(other: const PosixTimestamp&) : bool {
        if (_seconds != other._seconds) return _seconds < other._seconds;
        return _nanos <= other._nanos;
    }

    const operator>(other: const PosixTimestamp&) : bool {
        if (_seconds != other._seconds) return _seconds > other._seconds;
        return _nanos > other._nanos;
    }

    const operator>=(other: const PosixTimestamp&) : bool {
        if (_seconds != other._seconds) return _seconds > other._seconds;
        return _nanos >= other._nanos;
    }

    const operator+(duration: const Duration&) : PosixTimestamp
        throws(TemporalArithmeticException) {
        return plus(duration);
    }

    const operator-(duration: const Duration&) : PosixTimestamp
        throws(TemporalArithmeticException) {
        return minus(duration);
    }

    const operator-(other: const PosixTimestamp&) : Duration
        throws(TemporalArithmeticException) {
        return minus(other);
    }
}

} // namespace time
