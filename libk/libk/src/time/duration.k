/*
 * K Language standard library — Duration
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
 * Duration — an immutable chronological elapsed amount of time.
 *
 * Canonical representation: signed 64-bit seconds and 32-bit nanosecond adjustment.
 * Invariant: 0 <= nanoAdjustment < 1_000_000_000.
 */
public final struct Duration {
private:
    _seconds : long;
    _nanos   : int;

public:
    Duration() {
        _seconds = 0L;
        _nanos = 0;
    }

    Duration(seconds: long, nanos: int) {
        _seconds = seconds;
        _nanos = nanos;
    }

    // ── Factories ────────────────────────────────────────────────────────────

    static zero() : Duration {
        return Duration(0L, 0);
    }

    static ofNanos(nanos: long) : Duration {
        sec : long = floorDiv(nanos, NANOS_PER_SECOND);
        rem : long = floorMod(nanos, NANOS_PER_SECOND);
        return Duration(sec, (int) rem);
    }

    static ofMicros(micros: long) : Duration throws(TemporalArithmeticException) {
        sec : long = floorDiv(micros, 1000000L);
        rem : long = floorMod(micros, 1000000L);
        return Duration(sec, (int) (rem * NANOS_PER_MICRO));
    }

    static ofMillis(millis: long) : Duration throws(TemporalArithmeticException) {
        sec : long = floorDiv(millis, MILLIS_PER_SECOND);
        rem : long = floorMod(millis, MILLIS_PER_SECOND);
        return Duration(sec, (int) (rem * NANOS_PER_MILLI));
    }

    static ofSeconds(seconds: long) : Duration {
        return Duration(seconds, 0);
    }

    static ofSeconds(seconds: long, nanos: int) : Duration throws(TemporalArithmeticException) {
        norm : NormalizedSecondNanos = NormalizedSecondNanos(seconds, nanos);
        return Duration(norm.seconds, norm.nanos);
    }

    static ofMinutes(minutes: long) : Duration throws(TemporalArithmeticException) {
        sec : long = multiplyChecked(minutes, SECONDS_PER_MINUTE);
        return Duration(sec, 0);
    }

    static ofHours(hours: long) : Duration throws(TemporalArithmeticException) {
        sec : long = multiplyChecked(hours, SECONDS_PER_HOUR);
        return Duration(sec, 0);
    }

    // ── Field accessors and properties ───────────────────────────────────────

    const secondsPart() : long {
        return _seconds;
    }

    const nanoAdjustment() : int {
        return _nanos;
    }

    const isZero() : bool {
        return _seconds == 0L && _nanos == 0;
    }

    const isNegative() : bool {
        return _seconds < 0L;
    }

    // ── Unit conversions ─────────────────────────────────────────────────────

    const toNanosExact() : long throws(TemporalArithmeticException) {
        secNanos : long = multiplyChecked(_seconds, NANOS_PER_SECOND);
        return addChecked(secNanos, (long) _nanos);
    }

    const toMicros() : long throws(TemporalArithmeticException) {
        secMicros : long = multiplyChecked(_seconds, 1000000L);
        return addChecked(secMicros, (long) (_nanos / 1000));
    }

    const toMillis() : long throws(TemporalArithmeticException) {
        secMillis : long = multiplyChecked(_seconds, MILLIS_PER_SECOND);
        return addChecked(secMillis, (long) (_nanos / 1000000));
    }

    const toSeconds() : long {
        return _seconds;
    }

    // ── Arithmetic ───────────────────────────────────────────────────────────

    const plus(other: const Duration&) : Duration throws(TemporalArithmeticException) {
        sec : long = addChecked(_seconds, other._seconds);
        nanosSum : int = _nanos + other._nanos;
        if (nanosSum >= 1000000000) {
            sec = addChecked(sec, 1L);
            nanosSum = nanosSum - 1000000000;
        }
        return Duration(sec, nanosSum);
    }

    const minus(other: const Duration&) : Duration throws(TemporalArithmeticException) {
        sec : long = subtractChecked(_seconds, other._seconds);
        nanosDiff : int = _nanos - other._nanos;
        if (nanosDiff < 0) {
            sec = subtractChecked(sec, 1L);
            nanosDiff = nanosDiff + 1000000000;
        }
        return Duration(sec, nanosDiff);
    }

    const negated() : Duration throws(TemporalArithmeticException) {
        if (_nanos == 0) {
            return Duration(negateChecked(_seconds), 0);
        }
        sec : long = subtractChecked(negateChecked(_seconds), 1L);
        return Duration(sec, 1000000000 - _nanos);
    }

    const multipliedBy(scalar: long) : Duration throws(TemporalArithmeticException) {
        if (scalar == 0L || isZero()) {
            return Duration::zero();
        }
        if (scalar == 1L) {
            return Duration(_seconds, _nanos);
        }
        if (scalar == -1L) {
            return negated();
        }

        s_hi : long = scalar / NANOS_PER_SECOND;
        s_lo : long = scalar % NANOS_PER_SECOND;

        product_lo : long = (long) _nanos * s_lo;
        sec_lo : long = floorDiv(product_lo, NANOS_PER_SECOND);
        rem_nanos : long = floorMod(product_lo, NANOS_PER_SECOND);

        totalSec : long = multiplyChecked(_seconds, scalar);
        totalSec = addChecked(totalSec, multiplyChecked((long) _nanos, s_hi));
        totalSec = addChecked(totalSec, sec_lo);

        return Duration(totalSec, (int) rem_nanos);
    }

    const dividedBy(divisor: long) : Duration throws(TemporalArithmeticException) {
        if (divisor == 0L) {
            throw TemporalArithmeticException();
        }
        if (divisor == 1L) {
            return Duration(_seconds, _nanos);
        }
        if (divisor == -1L) {
            return negated();
        }
        if (isZero()) {
            return Duration::zero();
        }

        if (_seconds > -9223372036L && _seconds < 9223372036L) {
            totalNanos : long = _seconds * NANOS_PER_SECOND + (long) _nanos;
            return Duration::ofNanos(totalNanos / divisor);
        }

        neg : bool = isNegative() != (divisor < 0L);
        posSec : long = 0L;
        posNanos : int = 0;
        if (_seconds < 0L) {
            if (_nanos > 0) {
                posSec = -_seconds - 1L;
                posNanos = 1000000000 - _nanos;
            } else {
                posSec = -_seconds;
                posNanos = 0;
            }
        } else {
            posSec = _seconds;
            posNanos = _nanos;
        }

        absDiv : long = divisor < 0L ? -divisor : divisor;
        secQ : long = posSec / absDiv;
        remSec : long = posSec % absDiv;
        nanoQ : long = 0L;
        if (remSec <= 9223372036L) {
            remNanos : long = remSec * NANOS_PER_SECOND + (long) posNanos;
            nanoQ = remNanos / absDiv;
        } else {
            q1 : long = (remSec * 1000000L) / absDiv;
            r1 : long = (remSec * 1000000L) % absDiv;
            q2 : long = (r1 * 1000L + (long) posNanos) / absDiv;
            nanoQ = q1 * 1000L + q2;
        }

        result : Duration = Duration(secQ, (int) nanoQ);
        if (neg) {
            return result.negated();
        }
        return result;
    }

    // ── Comparisons ──────────────────────────────────────────────────────────

    const compareTo(other: const Duration&) : int {
        if (_seconds < other._seconds) return -1;
        if (_seconds > other._seconds) return 1;
        if (_nanos < other._nanos) return -1;
        if (_nanos > other._nanos) return 1;
        return 0;
    }

    const operator==(other: const Duration&) : bool {
        return _seconds == other._seconds && _nanos == other._nanos;
    }

    const operator!=(other: const Duration&) : bool {
        return _seconds != other._seconds || _nanos != other._nanos;
    }

    const operator<(other: const Duration&) : bool {
        if (_seconds != other._seconds) return _seconds < other._seconds;
        return _nanos < other._nanos;
    }

    const operator<=(other: const Duration&) : bool {
        if (_seconds != other._seconds) return _seconds < other._seconds;
        return _nanos <= other._nanos;
    }

    const operator>(other: const Duration&) : bool {
        if (_seconds != other._seconds) return _seconds > other._seconds;
        return _nanos > other._nanos;
    }

    const operator>=(other: const Duration&) : bool {
        if (_seconds != other._seconds) return _seconds > other._seconds;
        return _nanos >= other._nanos;
    }

    // ── Operator overloads ───────────────────────────────────────────────────

    const operator +(other: const Duration&) : Duration throws(TemporalArithmeticException) {
        return plus(other);
    }

    const operator -(other: const Duration&) : Duration throws(TemporalArithmeticException) {
        return minus(other);
    }

    const operator -() : Duration throws(TemporalArithmeticException) {
        return negated();
    }

    const operator *(scalar: long) : Duration throws(TemporalArithmeticException) {
        return multipliedBy(scalar);
    }

    const operator /(divisor: long) : Duration throws(TemporalArithmeticException) {
        return dividedBy(divisor);
    }
}

} // namespace time
