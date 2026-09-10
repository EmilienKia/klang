/*
 * K Language standard library — Period
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
 * Period — an immutable civil amount containing distinct years, months,
 * weeks, days, hours, minutes, and seconds.
 *
 * Preserves individual civil fields rather than normalizing months into years
 * or any field into Duration.
 */
public final struct Period {
private:
    _years   : long;
    _months  : long;
    _weeks   : long;
    _days    : long;
    _hours   : long;
    _minutes : long;
    _seconds : long;

public:
    Period() {
        _years = 0L;
        _months = 0L;
        _weeks = 0L;
        _days = 0L;
        _hours = 0L;
        _minutes = 0L;
        _seconds = 0L;
    }

    Period(years: long, months: long, weeks: long, days: long,
           hours: long, minutes: long, seconds: long) {
        _years = years;
        _months = months;
        _weeks = weeks;
        _days = days;
        _hours = hours;
        _minutes = minutes;
        _seconds = seconds;
    }

    // ── Factories ────────────────────────────────────────────────────────────

    static zero() : Period {
        return Period(0L, 0L, 0L, 0L, 0L, 0L, 0L);
    }

    static of(years: long, months: long, weeks: long, days: long,
              hours: long, minutes: long, seconds: long) : Period {
        return Period(years, months, weeks, days, hours, minutes, seconds);
    }

    static years(value: long) : Period {
        return Period(value, 0L, 0L, 0L, 0L, 0L, 0L);
    }

    static months(value: long) : Period {
        return Period(0L, value, 0L, 0L, 0L, 0L, 0L);
    }

    static weeks(value: long) : Period {
        return Period(0L, 0L, value, 0L, 0L, 0L, 0L);
    }

    static days(value: long) : Period {
        return Period(0L, 0L, 0L, value, 0L, 0L, 0L);
    }

    static hours(value: long) : Period {
        return Period(0L, 0L, 0L, 0L, value, 0L, 0L);
    }

    static minutes(value: long) : Period {
        return Period(0L, 0L, 0L, 0L, 0L, value, 0L);
    }

    static seconds(value: long) : Period {
        return Period(0L, 0L, 0L, 0L, 0L, 0L, value);
    }

    // ── Field accessors ──────────────────────────────────────────────────────

    const yearsPart() : long {
        return _years;
    }

    const monthsPart() : long {
        return _months;
    }

    const weeksPart() : long {
        return _weeks;
    }

    const daysPart() : long {
        return _days;
    }

    const hoursPart() : long {
        return _hours;
    }

    const minutesPart() : long {
        return _minutes;
    }

    const secondsPart() : long {
        return _seconds;
    }

    // ── Arithmetic ───────────────────────────────────────────────────────────

    const plus(other: const Period&) : Period throws(TemporalArithmeticException) {
        return Period(
            addChecked(_years, other._years),
            addChecked(_months, other._months),
            addChecked(_weeks, other._weeks),
            addChecked(_days, other._days),
            addChecked(_hours, other._hours),
            addChecked(_minutes, other._minutes),
            addChecked(_seconds, other._seconds)
        );
    }

    const minus(other: const Period&) : Period throws(TemporalArithmeticException) {
        return Period(
            subtractChecked(_years, other._years),
            subtractChecked(_months, other._months),
            subtractChecked(_weeks, other._weeks),
            subtractChecked(_days, other._days),
            subtractChecked(_hours, other._hours),
            subtractChecked(_minutes, other._minutes),
            subtractChecked(_seconds, other._seconds)
        );
    }

    const negated() : Period throws(TemporalArithmeticException) {
        return Period(
            negateChecked(_years),
            negateChecked(_months),
            negateChecked(_weeks),
            negateChecked(_days),
            negateChecked(_hours),
            negateChecked(_minutes),
            negateChecked(_seconds)
        );
    }

    const isZero() : bool {
        return _years == 0L && _months == 0L && _weeks == 0L && _days == 0L &&
               _hours == 0L && _minutes == 0L && _seconds == 0L;
    }

    // ── Equality ─────────────────────────────────────────────────────────────

    const operator==(other: const Period&) : bool {
        return _years == other._years && _months == other._months &&
               _weeks == other._weeks && _days == other._days &&
               _hours == other._hours && _minutes == other._minutes &&
               _seconds == other._seconds;
    }

    const operator!=(other: const Period&) : bool {
        return _years != other._years || _months != other._months ||
               _weeks != other._weeks || _days != other._days ||
               _hours != other._hours || _minutes != other._minutes ||
               _seconds != other._seconds;
    }

    // ── Operator overloads ───────────────────────────────────────────────────

    const operator+(other: const Period&) : Period throws(TemporalArithmeticException) {
        return plus(other);
    }

    const operator-(other: const Period&) : Period throws(TemporalArithmeticException) {
        return minus(other);
    }

    const operator-() : Period throws(TemporalArithmeticException) {
        return negated();
    }
}

} // namespace time
