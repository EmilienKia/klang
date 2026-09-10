/*
 * K Language standard library — LocalDateTime
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
 * LocalDateTime — an immutable composition of a LocalDate and LocalTime.
 *
 * Contains no time zone, offset, or instant interpretation.
 */
public final struct LocalDateTime {
private:
    _date : LocalDate;
    _time : LocalTime;

public:
    LocalDateTime() {
        _date = LocalDate();
        _time = LocalTime();
    }

    LocalDateTime(date: const LocalDate&, time: const LocalTime&) {
        _date = date;
        _time = time;
    }

    // ── Factories ────────────────────────────────────────────────────────────

    static of(date: const LocalDate&, time: const LocalTime&) : LocalDateTime {
        return LocalDateTime(date, time);
    }

    static of(year: long, month: int, day: int, hour: int, minute: int) : LocalDateTime
        throws(InvalidTemporalValueException) {
        return of(year, month, day, hour, minute, 0, 0);
    }

    static of(year: long, month: int, day: int, hour: int, minute: int,
              second: int) : LocalDateTime
        throws(InvalidTemporalValueException) {
        return of(year, month, day, hour, minute, second, 0);
    }

    static of(year: long, month: int, day: int, hour: int, minute: int,
              second: int, nano: int) : LocalDateTime
        throws(InvalidTemporalValueException) {
        d : LocalDate = LocalDate::of(year, month, day);
        t : LocalTime = LocalTime::of(hour, minute, second, nano);
        return LocalDateTime(d, t);
    }

    // ── Field accessors ──────────────────────────────────────────────────────

    const date() : LocalDate {
        return _date;
    }

    const time() : LocalTime {
        return _time;
    }

    const year() : long {
        return _date.year();
    }

    const month() : int {
        return _date.month();
    }

    const day() : int {
        return _date.day();
    }

    const hour() : int {
        return _time.hour();
    }

    const minute() : int {
        return _time.minute();
    }

    const second() : int {
        return _time.second();
    }

    const nano() : int {
        return _time.nano();
    }

    // ── Arithmetic ───────────────────────────────────────────────────────────

    const plus(period: const Period&) : LocalDateTime
        throws(TemporalArithmeticException) {
        // Step 1: Apply date fields of period to date
        datePeriod : Period = Period(period.yearsPart(), period.monthsPart(),
                                     period.weeksPart(), period.daysPart(),
                                     0L, 0L, 0L);
        d : LocalDate = _date.plus(datePeriod);

        // Step 2: Apply time fields with day carry/borrow
        timeSec : long = multiplyChecked(period.hoursPart(), 3600L);
        timeSec = addChecked(timeSec, multiplyChecked(period.minutesPart(), 60L));
        timeSec = addChecked(timeSec, period.secondsPart());

        if (timeSec == 0L) {
            return LocalDateTime(d, _time);
        }

        curSec : long = ((long) _time.hour()) * 3600L +
                        ((long) _time.minute()) * 60L +
                        ((long) _time.second());
        totSec : long = addChecked(curSec, timeSec);

        dayCarry : long = floorDiv(totSec, SECONDS_PER_DAY);
        secOfDay : long = floorMod(totSec, SECONDS_PER_DAY);

        h : int = (int) (secOfDay / 3600L);
        m : int = (int) ((secOfDay % 3600L) / 60L);
        s : int = (int) (secOfDay % 60L);
        newTime : LocalTime = LocalTime(h, m, s, _time.nano());

        if (dayCarry != 0L) {
            ed : EpochDay = d.toEpochDay();
            ed = ed.plusDays(dayCarry);
            d = LocalDate::fromEpochDay(ed);
        }

        return LocalDateTime(d, newTime);
    }

    const minus(period: const Period&) : LocalDateTime
        throws(TemporalArithmeticException) {
        return plus(period.negated());
    }

    const plus(duration: const Duration&) : LocalDateTime
        throws(InvalidTemporalValueException, TemporalArithmeticException) {
        if (_time.isStructuralLeapSecond()) {
            throw InvalidTemporalValueException();
        }

        newNano : int = _time.nano() + duration.nanoAdjustment();
        carrySec : long = 0L;
        if (newNano >= 1000000000) {
            newNano = newNano - 1000000000;
            carrySec = 1L;
        }

        totDurSec : long = addChecked(duration.secondsPart(), carrySec);
        dayFromSec : long = floorDiv(totDurSec, SECONDS_PER_DAY);
        remSec : long = floorMod(totDurSec, SECONDS_PER_DAY);

        curSec : long = ((long) _time.hour()) * 3600L +
                        ((long) _time.minute()) * 60L +
                        ((long) _time.second());
        secSum : long = curSec + remSec;
        additionalDay : long = secSum / SECONDS_PER_DAY;
        finalSecOfDay : long = secSum % SECONDS_PER_DAY;

        totalDayCarry : long = addChecked(dayFromSec, additionalDay);

        h : int = (int) (finalSecOfDay / 3600L);
        m : int = (int) ((finalSecOfDay % 3600L) / 60L);
        s : int = (int) (finalSecOfDay % 60L);
        newTime : LocalTime = LocalTime(h, m, s, newNano);

        d : LocalDate = _date;
        if (totalDayCarry != 0L) {
            ed : EpochDay = _date.toEpochDay();
            ed = ed.plusDays(totalDayCarry);
            d = LocalDate::fromEpochDay(ed);
        }

        return LocalDateTime(d, newTime);
    }

    const minus(duration: const Duration&) : LocalDateTime
        throws(InvalidTemporalValueException, TemporalArithmeticException) {
        if (_time.isStructuralLeapSecond()) {
            throw InvalidTemporalValueException();
        }
        return plus(duration.negated());
    }

    // ── Comparisons ──────────────────────────────────────────────────────────

    const compareTo(other: const LocalDateTime&) : int {
        cmp : int = _date.compareTo(other._date);
        if (cmp != 0) return cmp;
        return _time.compareTo(other._time);
    }

    const operator==(other: const LocalDateTime&) : bool {
        return _date == other._date && _time == other._time;
    }

    const operator!=(other: const LocalDateTime&) : bool {
        return _date != other._date || _time != other._time;
    }

    const operator<(other: const LocalDateTime&) : bool {
        cmp : int = _date.compareTo(other._date);
        if (cmp != 0) return cmp < 0;
        return _time < other._time;
    }

    const operator<=(other: const LocalDateTime&) : bool {
        cmp : int = _date.compareTo(other._date);
        if (cmp != 0) return cmp < 0;
        return _time <= other._time;
    }

    const operator>(other: const LocalDateTime&) : bool {
        cmp : int = _date.compareTo(other._date);
        if (cmp != 0) return cmp > 0;
        return _time > other._time;
    }

    const operator>=(other: const LocalDateTime&) : bool {
        cmp : int = _date.compareTo(other._date);
        if (cmp != 0) return cmp > 0;
        return _time >= other._time;
    }

    // ── Operator overloads ───────────────────────────────────────────────────

    const operator+(period: const Period&) : LocalDateTime
        throws(TemporalArithmeticException) {
        return plus(period);
    }

    const operator-(period: const Period&) : LocalDateTime
        throws(TemporalArithmeticException) {
        return minus(period);
    }

    const operator+(duration: const Duration&) : LocalDateTime
        throws(InvalidTemporalValueException, TemporalArithmeticException) {
        return plus(duration);
    }

    const operator-(duration: const Duration&) : LocalDateTime
        throws(InvalidTemporalValueException, TemporalArithmeticException) {
        return minus(duration);
    }

    // ── Zone Projections ─────────────────────────────────────────────────────

    /**
     * Create a ZonedDateTime in the specified TimeZone using StrictResolver.
     */
    const atZone(zone: const TimeZone&) : ZonedDateTime
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException) {
        return ZonedDateTime::of(this, zone);
    }

    /**
     * Create a ZonedDateTime in the specified TimeZone using an explicit resolver.
     */
    const atZone(zone: const TimeZone&, resolver: const LocalDateTimeResolver&) : ZonedDateTime
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException) {
        return ZonedDateTime::of(this, zone, resolver);
    }
}

} // namespace time
