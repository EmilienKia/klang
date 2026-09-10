/*
 * K Language standard library — LocalDate
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
 * LocalDate — an immutable validated proleptic Gregorian civil date.
 *
 * Fields:
 * - year: long (proleptic Gregorian astronomical year, supports negative and zero)
 * - month: int (1..12)
 * - day: int (1..lengthOfMonth(year, month))
 */
public final struct LocalDate {
private:
    _year  : long;
    _month : int;
    _day   : int;

public:
    LocalDate() {
        _year = 1970L;
        _month = 1;
        _day = 1;
    }

    LocalDate(year: long, month: int, day: int) {
        _year = year;
        _month = month;
        _day = day;
    }

    // ── Factories ────────────────────────────────────────────────────────────

    static of(year: long, month: int, day: int) : LocalDate
        throws(InvalidTemporalValueException) {
        if (month < 1 || month > 12) {
            throw InvalidTemporalValueException();
        }
        maxDay : int = GregorianMath::lengthOfMonth(year, month);
        if (day < 1 || day > maxDay) {
            throw InvalidTemporalValueException();
        }
        return LocalDate(year, month, day);
    }

    static fromEpochDay(day: const EpochDay&) : LocalDate
        throws(TemporalArithmeticException) {
        ymd : YearMonthDay = day.toDate();
        return LocalDate(ymd.getYear(), ymd.getMonth(), ymd.getDay());
    }

    // ── Field accessors and properties ───────────────────────────────────────

    const year() : long {
        return _year;
    }

    const month() : int {
        return _month;
    }

    const day() : int {
        return _day;
    }

    const dayOfYear() : int {
        return GregorianMath::dayOfYear(_year, _month, _day);
    }

    const toEpochDay() : EpochDay throws(TemporalArithmeticException) {
        return EpochDay::fromDate(_year, _month, _day);
    }

    const isLeapYear() : bool {
        return GregorianMath::isLeapYear(_year);
    }

    const lengthOfMonth() : int {
        return GregorianMath::lengthOfMonth(_year, _month);
    }

    // ── Arithmetic ───────────────────────────────────────────────────────────

    const plus(period: const Period&) : LocalDate
        throws(TemporalArithmeticException) {
        // Step 1: Add years
        newYear : long = addChecked(_year, period.yearsPart());

        // Step 2: Add months with floorDiv / floorMod
        m0 : long = (long) (_month - 1);
        totalMonths : long = addChecked(m0, period.monthsPart());
        yearAdj : long = floorDiv(totalMonths, 12L);
        monthRem : long = floorMod(totalMonths, 12L);
        newYear = addChecked(newYear, yearAdj);
        newMonth : int = (int) (monthRem + 1L);

        // Step 3: Clamping to last valid day of month
        maxDay : int = GregorianMath::lengthOfMonth(newYear, newMonth);
        clampedDay : int = _day > maxDay ? maxDay : _day;

        // Step 4: Add weeks and days via EpochDay
        weeksDays : long = multiplyChecked(period.weeksPart(), 7L);
        totalDaysToAdd : long = addChecked(weeksDays, period.daysPart());

        if (totalDaysToAdd == 0L) {
            return LocalDate(newYear, newMonth, clampedDay);
        }

        ed : EpochDay = EpochDay::fromDate(newYear, newMonth, clampedDay);
        ed = ed.plusDays(totalDaysToAdd);
        return LocalDate::fromEpochDay(ed);
    }

    const minus(period: const Period&) : LocalDate
        throws(TemporalArithmeticException) {
        return plus(period.negated());
    }

    // ── Composition ──────────────────────────────────────────────────────────

    const atTime(time: const LocalTime&) : LocalDateTime {
        return LocalDateTime::of(this, time);
    }

    // ── Comparisons ──────────────────────────────────────────────────────────

    const compareTo(other: const LocalDate&) : int {
        if (_year < other._year) return -1;
        if (_year > other._year) return 1;
        if (_month < other._month) return -1;
        if (_month > other._month) return 1;
        if (_day < other._day) return -1;
        if (_day > other._day) return 1;
        return 0;
    }

    const operator==(other: const LocalDate&) : bool {
        return _year == other._year && _month == other._month && _day == other._day;
    }

    const operator!=(other: const LocalDate&) : bool {
        return _year != other._year || _month != other._month || _day != other._day;
    }

    const operator<(other: const LocalDate&) : bool {
        if (_year != other._year) return _year < other._year;
        if (_month != other._month) return _month < other._month;
        return _day < other._day;
    }

    const operator<=(other: const LocalDate&) : bool {
        if (_year != other._year) return _year < other._year;
        if (_month != other._month) return _month < other._month;
        return _day <= other._day;
    }

    const operator>(other: const LocalDate&) : bool {
        if (_year != other._year) return _year > other._year;
        if (_month != other._month) return _month > other._month;
        return _day > other._day;
    }

    const operator>=(other: const LocalDate&) : bool {
        if (_year != other._year) return _year > other._year;
        if (_month != other._month) return _month > other._month;
        return _day >= other._day;
    }

    // ── Operator overloads ───────────────────────────────────────────────────

    const operator+(period: const Period&) : LocalDate
        throws(TemporalArithmeticException) {
        return plus(period);
    }

    const operator-(period: const Period&) : LocalDate
        throws(TemporalArithmeticException) {
        return minus(period);
    }
}

} // namespace time
