/*
 * K Language standard library — ZonedDateTime
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
 * ZonedDateTime — an immutable pairing of an Instant on the global timeline and a TimeZone.
 *
 * Local date/time fields and effective UTC offset are derived from (instant, zone).
 * Equality requires equal instants and equal time zones (including rules version).
 */
public final class ZonedDateTime {
private:
    _instant       : time::Instant;
    _zone          : TimeZone;
    _offset        : ZoneOffset;
    _localDateTime : LocalDateTime;

    ZonedDateTime(instant: const time::Instant&, zone: const TimeZone&,
                  offset: const ZoneOffset&, localDateTime: const LocalDateTime&) {
        _instant = instant;
        _zone = zone;
        _offset = offset;
        _localDateTime = localDateTime;
    }

public:
    ZonedDateTime() {
        _instant = time::Instant::epoch();
        _zone = TimeZone();
        _offset = ZoneOffset::utc();
        _localDateTime = LocalDateTime();
    }

    ZonedDateTime(other: const ZonedDateTime&) {
        _instant = other._instant;
        _zone = other._zone;
        _offset = other._offset;
        _localDateTime = other._localDateTime;
    }

    // ── Factories ────────────────────────────────────────────────────────────

    /**
     * Create a ZonedDateTime by projecting an Instant into the given TimeZone.
     * Always succeeds because an Instant corresponds to exactly one offset in any zone.
     */
    static fromInstant(instant: const time::Instant&, zone: const TimeZone&) : ZonedDateTime {
        offset : ZoneOffset = zone.rules().offsetAt(instant);
        posixSec : long = instant.toPosixSeconds();
        secLocal : long = posixSec + (long) offset.totalSeconds();
        days : long = floorDiv(secLocal, 86400L);
        sod : long = floorMod(secLocal, 86400L);
        try {
            d : LocalDate = LocalDate::fromEpochDay(EpochDay(days));
            h : int = (int) (sod / 3600L);
            m : int = (int) ((sod % 3600L) / 60L);
            s : int = (int) (sod % 60L);
            t : LocalTime = LocalTime::of(h, m, s, instant.nanoAdjustment());
            local : LocalDateTime = LocalDateTime::of(d, t);
            return ZonedDateTime(instant, zone, offset, local);
        } catch (e: TemporalArithmeticException&) {
            return ZonedDateTime(instant, zone, offset, LocalDateTime());
        } catch (e2: InvalidTemporalValueException&) {
            return ZonedDateTime(instant, zone, offset, LocalDateTime());
        }
    }

    /**
     * Create a ZonedDateTime from a local date/time and time zone using StrictResolver.
     * Throws LocalTimeGapException(521) on gap, or LocalTimeOverlapException(522) on overlap.
     */
    static of(local: const LocalDateTime&, zone: const TimeZone&) : ZonedDateTime
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException) {
        return of(local, zone, StrictResolver::instance());
    }

    /**
     * Create a ZonedDateTime from a local date/time and time zone using an explicit resolver.
     */
    static of(local: const LocalDateTime&, zone: const TimeZone&, resolver: const LocalDateTimeResolver&) : ZonedDateTime
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException) {
        res : ZoneLocalResolution! = zone.rules().resolveLocal(local);
        inst : time::Instant = resolver.resolve(local, *res);
        return fromInstant(inst, zone);
    }

    // ── Accessors ────────────────────────────────────────────────────────────

    const instant() : time::Instant {
        return _instant;
    }

    const zone() : TimeZone {
        return _zone;
    }

    const offset() : ZoneOffset {
        return _offset;
    }

    const localDate() : LocalDate {
        return _localDateTime.date();
    }

    const localTime() : LocalTime {
        return _localDateTime.time();
    }

    const localDateTime() : LocalDateTime {
        return _localDateTime;
    }

    const toInstant() : time::Instant {
        return _instant;
    }

    // ── Projections ──────────────────────────────────────────────────────────

    /**
     * Return a copy of this ZonedDateTime projected to a new TimeZone, keeping the same Instant.
     */
    const withZone(newZone: const TimeZone&) : ZonedDateTime {
        return fromInstant(_instant, newZone);
    }

    // ── Timeline Arithmetic (Duration) ───────────────────────────────────────

    /**
     * Add a duration on the timeline (exact physical elapsed time).
     */
    const plus(duration: const time::Duration&) : ZonedDateTime throws(TemporalArithmeticException) {
        return fromInstant(_instant.plus(duration), _zone);
    }

    /**
     * Subtract a duration on the timeline (exact physical elapsed time).
     */
    const minus(duration: const time::Duration&) : ZonedDateTime throws(TemporalArithmeticException) {
        return fromInstant(_instant.minus(duration), _zone);
    }

    // ── Civil Arithmetic (Period) ────────────────────────────────────────────

    /**
     * Add a calendar period using an explicit LocalDateTimeResolver.
     */
    const plus(period: const Period&, resolver: const LocalDateTimeResolver&) : ZonedDateTime
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException) {
        newLocal : LocalDateTime = _localDateTime.plus(period);
        return ZonedDateTime::of(newLocal, this._zone, resolver);
    }

    /**
     * Subtract a calendar period using an explicit LocalDateTimeResolver.
     */
    const minus(period: const Period&, resolver: const LocalDateTimeResolver&) : ZonedDateTime
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException) {
        newLocal : LocalDateTime = _localDateTime.minus(period);
        return ZonedDateTime::of(newLocal, this._zone, resolver);
    }

    /**
     * Add a calendar period using StrictResolver.
     */
    const plus(period: const Period&) : ZonedDateTime
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException) {
        return plus(period, StrictResolver::instance());
    }

    /**
     * Subtract a calendar period using StrictResolver.
     */
    const minus(period: const Period&) : ZonedDateTime
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException) {
        return minus(period, StrictResolver::instance());
    }

    // ── Comparisons ──────────────────────────────────────────────────────────

    const isBefore(other: const ZonedDateTime&) : bool {
        return _instant < other._instant;
    }

    const isAfter(other: const ZonedDateTime&) : bool {
        return _instant > other._instant;
    }

    const isSameInstant(other: const ZonedDateTime&) : bool {
        return _instant == other._instant;
    }

    const operator==(other: const ZonedDateTime&) : bool {
        return _instant == other._instant && _zone == other._zone;
    }

    const operator!=(other: const ZonedDateTime&) : bool {
        return !(this == other);
    }
}

} // namespace time
