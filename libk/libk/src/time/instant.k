/*
 * K Language standard library — Instant
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
 * Instant — an immutable point on K's continuous global timeline.
 *
 * Canonical representation: signed 64-bit epoch-seconds and 32-bit nanosecond
 * adjustment from 1970-01-01T00:00:00Z.
 * Invariant: 0 <= nanoAdjustment < 1_000_000_000.
 */
public final struct Instant {
private:
    _seconds : long;
    _nanos   : int;

public:
    Instant() {
        _seconds = 0L;
        _nanos = 0;
    }

    Instant(seconds: long, nanos: int) {
        _seconds = seconds;
        _nanos = nanos;
    }

    // ── Factories ────────────────────────────────────────────────────────────

    static epoch() : Instant {
        return Instant(0L, 0);
    }

    static ofEpochSecond(seconds: long) : Instant {
        return Instant(seconds, 0);
    }

    static ofEpochSecond(seconds: long, nanos: int) : Instant throws(TemporalArithmeticException) {
        norm : NormalizedSecondNanos = NormalizedSecondNanos(seconds, nanos);
        return Instant(norm.seconds, norm.nanos);
    }

    // ── Field accessors ──────────────────────────────────────────────────────

    const epochSeconds() : long {
        return _seconds;
    }

    const nanoAdjustment() : int {
        return _nanos;
    }

    // ── Timeline arithmetic ──────────────────────────────────────────────────

    const plus(duration: const Duration&) : Instant throws(TemporalArithmeticException) {
        sec : long = addChecked(_seconds, duration.secondsPart());
        nanosSum : int = _nanos + duration.nanoAdjustment();
        if (nanosSum >= 1000000000) {
            sec = addChecked(sec, 1L);
            nanosSum = nanosSum - 1000000000;
        }
        return Instant(sec, nanosSum);
    }

    const minus(duration: const Duration&) : Instant throws(TemporalArithmeticException) {
        sec : long = subtractChecked(_seconds, duration.secondsPart());
        nanosDiff : int = _nanos - duration.nanoAdjustment();
        if (nanosDiff < 0) {
            sec = subtractChecked(sec, 1L);
            nanosDiff = nanosDiff + 1000000000;
        }
        return Instant(sec, nanosDiff);
    }

    const minus(other: const Instant&) : Duration throws(TemporalArithmeticException) {
        sec : long = subtractChecked(_seconds, other._seconds);
        nanosDiff : int = _nanos - other._nanos;
        if (nanosDiff < 0) {
            sec = subtractChecked(sec, 1L);
            nanosDiff = nanosDiff + 1000000000;
        }
        return Duration(sec, nanosDiff);
    }

    const until(other: const Instant&) : Duration throws(TemporalArithmeticException) {
        return other.minus(Instant(_seconds, _nanos));
    }

    // ── Comparisons ──────────────────────────────────────────────────────────

    const compareTo(other: const Instant&) : int {
        if (_seconds < other._seconds) return -1;
        if (_seconds > other._seconds) return 1;
        if (_nanos < other._nanos) return -1;
        if (_nanos > other._nanos) return 1;
        return 0;
    }

    const operator==(other: const Instant&) : bool {
        return _seconds == other._seconds && _nanos == other._nanos;
    }

    const operator!=(other: const Instant&) : bool {
        return _seconds != other._seconds || _nanos != other._nanos;
    }

    const operator<(other: const Instant&) : bool {
        if (_seconds != other._seconds) return _seconds < other._seconds;
        return _nanos < other._nanos;
    }

    const operator<=(other: const Instant&) : bool {
        if (_seconds != other._seconds) return _seconds < other._seconds;
        return _nanos <= other._nanos;
    }

    const operator>(other: const Instant&) : bool {
        if (_seconds != other._seconds) return _seconds > other._seconds;
        return _nanos > other._nanos;
    }

    const operator>=(other: const Instant&) : bool {
        if (_seconds != other._seconds) return _seconds > other._seconds;
        return _nanos >= other._nanos;
    }

    // ── Operator overloads ───────────────────────────────────────────────────

    const operator +(duration: const Duration&) : Instant throws(TemporalArithmeticException) {
        return plus(duration);
    }

    const operator -(duration: const Duration&) : Instant throws(TemporalArithmeticException) {
        return minus(duration);
    }

    const operator -(other: const Instant&) : Duration throws(TemporalArithmeticException) {
        return minus(other);
    }
}

} // namespace time
