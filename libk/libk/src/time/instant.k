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

    static fromPosixTimestamp(value: const PosixTimestamp&) : Instant {
        contSec : long = LeapSecondTable::posixToContinuous(value.seconds());
        return Instant(contSec, value.nanoAdjustment());
    }

    // ── Field accessors ──────────────────────────────────────────────────────

    const epochSeconds() : long {
        return _seconds;
    }

    const nanoAdjustment() : int {
        return _nanos;
    }

    // ── Projections & Conversions ────────────────────────────────────────────

    /**
     * Convert this Instant to a POSIX timestamp under the specified leap-second policy.
     * Throws LeapSecondException(504) if policy is reject and this instant is a leap second.
     */
    const toPosixTimestamp(policy: const PosixLeapSecondPolicy&) : PosixTimestamp throws(LeapSecondException) {
        posixSec : long = LeapSecondTable::continuousToPosix(_seconds, policy.mode());
        return PosixTimestamp(posixSec, _nanos);
    }

    /**
     * Convert this Instant to a POSIX timestamp using PosixLeapSecondPolicy::reject().
     */
    const toPosixTimestamp() : PosixTimestamp throws(LeapSecondException) {
        return toPosixTimestamp(PosixLeapSecondPolicy::reject());
    }

    /**
     * Convert this Instant to POSIX epoch seconds, folding inserted leap seconds.
     * Never throws.
     */
    const toPosixSeconds() : long {
        return LeapSecondTable::continuousToPosixFolded(_seconds);
    }

    /**
     * Format this Instant as a UTC calendar LocalDateTime.
     * Accurately reflects second 60 for inserted leap seconds.
     */
    const toUtcDateTime() : LocalDateTime {
        y : long = 0L;
        m : int = 0;
        d : int = 0;
        try {
            if (LeapSecondTable::getLeapDate(_seconds, &y, &m, &d)) {
                date : LocalDate = LocalDate::of(y, m, d);
                time : LocalTime = LocalTime::of(23, 59, 60, _nanos);
                return LocalDateTime::of(date, time);
            }
            posixSec : long = toPosixSeconds();
            days : long = floorDiv(posixSec, 86400L);
            sod : long = floorMod(posixSec, 86400L);
            date : LocalDate = LocalDate::fromEpochDay(EpochDay(days));
            h : int = (int) (sod / 3600L);
            minute : int = (int) ((sod % 3600L) / 60L);
            sec : int = (int) (sod % 60L);
            time : LocalTime = LocalTime::of(h, minute, sec, _nanos);
            return LocalDateTime::of(date, time);
        } catch (e: TemporalArithmeticException&) {
            return LocalDateTime();
        } catch (e2: InvalidTemporalValueException&) {
            return LocalDateTime();
        }
    }

    /**
     * Project this Instant into the given TimeZone to create a ZonedDateTime.
     */
    const atZone(zone: const TimeZone&) : ZonedDateTime {
        return ZonedDateTime::fromInstant(this, zone);
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
