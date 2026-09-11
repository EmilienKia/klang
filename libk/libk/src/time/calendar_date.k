/*
 * K Language standard library — CalendarDate and CalendarDateTime
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
 * CalendarDate — an immutable civil date interpreted according to an explicit Chronology.
 *
 * Encapsulates a reference to its Chronology, its validated CalendarFields,
 * and its cached EpochDay.
 * Cross-chronology conversion proceeds exclusively through toEpochDay().
 */
public final class CalendarDate {
private:
    _chronology : Chronology*;
    _fields     : CalendarFields;
    _epochDay   : EpochDay;

public:
    CalendarDate() {
        _chronology = &GregorianChronology::instance();
        _fields = CalendarFields();
        _epochDay = EpochDay(0L);
    }

    CalendarDate(chronology: Chronology&, fields: const CalendarFields&, epochDay: const EpochDay&) {
        _chronology = &chronology;
        _fields = fields;
        _epochDay = epochDay;
    }

    CalendarDate(other: const CalendarDate&) {
        _chronology = other._chronology;
        _fields = other._fields;
        _epochDay = other._epochDay;
    }

    // ── Factories ────────────────────────────────────────────────────────────

    static of(chronology: Chronology&, fields: const CalendarFields&) : CalendarDate
        throws(InvalidTemporalValueException) {
        return chronology.date(fields);
    }

    static of(chronology: Chronology&, year: long, month: int, day: int) : CalendarDate
        throws(InvalidTemporalValueException) {
        m : CalendarMonth = CalendarMonth::of(month, false);
        f : CalendarFields = CalendarFields::of(null, year, m, day);
        return chronology.date(f);
    }

    static fromEpochDay(chronology: Chronology&, day: const EpochDay&) : CalendarDate
        throws(InvalidTemporalValueException) {
        return chronology.fromEpochDay(day);
    }

    // ── Accessors ────────────────────────────────────────────────────────────

    const chronology() : Chronology& {
        return *_chronology;
    }

    const fields() : CalendarFields {
        return _fields;
    }

    const toEpochDay() : EpochDay {
        return _epochDay;
    }

    // ── Conversions ──────────────────────────────────────────────────────────

    /**
     * Convert this calendar date to a different chronology via EpochDay.
     */
    const toChronology(target: const Chronology&) : CalendarDate throws(InvalidTemporalValueException) {
        targetChronology : Chronology& = (Chronology&) target;
        ed : EpochDay = _epochDay;
        return targetChronology.fromEpochDay(ed);
    }

    /**
     * Convert this calendar date to a proleptic Gregorian LocalDate.
     */
    const toLocalDate() : LocalDate {
        ed : EpochDay = _epochDay;
        try {
            return LocalDate::fromEpochDay(ed);
        } catch (e: TemporalArithmeticException&) {
            return LocalDate();
        }
    }

    /**
     * Combine this date with a local time.
     */
    const atTime(time: const LocalTime&) : CalendarDateTime {
        return CalendarDateTime::of(this, time);
    }

    // ── Arithmetic ───────────────────────────────────────────────────────────

    const plus(period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        return _chronology->plus(this, period);
    }

    const minus(period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        return _chronology->minus(this, period);
    }

    // ── Comparisons ──────────────────────────────────────────────────────────

    /**
     * Compare two dates. Only valid if both belong to the exact same chronology.
     */
    const compareTo(other: const CalendarDate&) : int throws(InvalidTemporalValueException) {
        if (_chronology->id() != other.chronology().id()) {
            throw InvalidTemporalValueException(501);
        }
        return _epochDay.compareTo(other._epochDay);
    }

    /**
     * Equality requires both same chronology identity and equal fields.
     */
    const operator==(other: const CalendarDate&) : bool {
        return _chronology->id() == other.chronology().id() && _fields == other._fields;
    }

    const operator!=(other: const CalendarDate&) : bool {
        return !(this == other);
    }

    const operator+(period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        return plus(period);
    }

    const operator-(period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        return minus(period);
    }
}

/**
 * CalendarDateTime — an immutable pairing of a CalendarDate and a LocalTime.
 */
public final class CalendarDateTime {
private:
    _date : CalendarDate;
    _time : LocalTime;

public:
    CalendarDateTime() {
        _date = CalendarDate();
        _time = LocalTime();
    }

    CalendarDateTime(date: const CalendarDate&, time: const LocalTime&) {
        _date = date;
        _time = time;
    }

    CalendarDateTime(other: const CalendarDateTime&) {
        _date = other._date;
        _time = other._time;
    }

    // ── Factory ──────────────────────────────────────────────────────────────

    static of(date: const CalendarDate&, time: const LocalTime&) : CalendarDateTime {
        return CalendarDateTime(date, time);
    }

    // ── Accessors ────────────────────────────────────────────────────────────

    const chronology() : Chronology& {
        return _date.chronology();
    }

    const date() : CalendarDate {
        return _date;
    }

    const time() : LocalTime {
        return _time;
    }

    // ── Arithmetic ───────────────────────────────────────────────────────────

    const plus(period: const Period&) : CalendarDateTime
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        newDate : CalendarDate = _date.plus(period);
        t : LocalTime = _time;
        return CalendarDateTime::of(newDate, t);
    }

    const minus(period: const Period&) : CalendarDateTime
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        newDate : CalendarDate = _date.minus(period);
        t : LocalTime = _time;
        return CalendarDateTime::of(newDate, t);
    }

    // ── Projections & Zone Resolution ────────────────────────────────────────

    /**
     * Project this CalendarDateTime into a TimeZone using an explicit resolver.
     */
    const atZone(zone: const TimeZone&, resolver: const LocalDateTimeResolver&) : ZonedDateTime
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException) {
        ld : LocalDate = _date.toLocalDate();
        t : LocalTime = _time;
        ldt : LocalDateTime = LocalDateTime::of(ld, t);
        return ZonedDateTime::of(ldt, zone, resolver);
    }

    /**
     * Project this CalendarDateTime into a TimeZone using the StrictResolver.
     */
    const atZone(zone: const TimeZone&) : ZonedDateTime
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException) {
        return atZone(zone, StrictResolver::instance());
    }

    /**
     * Convert this CalendarDateTime to an absolute Instant in the given TimeZone using an explicit resolver.
     */
    const toInstant(zone: const TimeZone&, resolver: const LocalDateTimeResolver&) : time::Instant
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException) {
        return atZone(zone, resolver).instant();
    }

    /**
     * Convert this CalendarDateTime to an absolute Instant in the given TimeZone using StrictResolver.
     */
    const toInstant(zone: const TimeZone&) : time::Instant
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException) {
        return atZone(zone).instant();
    }

    // ── Comparisons ──────────────────────────────────────────────────────────

    const operator==(other: const CalendarDateTime&) : bool {
        return _date == other._date && _time == other._time;
    }

    const operator!=(other: const CalendarDateTime&) : bool {
        return !(this == other);
    }

    const operator+(period: const Period&) : CalendarDateTime
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        return plus(period);
    }

    const operator-(period: const Period&) : CalendarDateTime
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        return minus(period);
    }
}

} // namespace time
