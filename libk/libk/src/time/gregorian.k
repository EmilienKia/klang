/*
 * K Language standard library — Gregorian calendar math and EpochDay
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
 * Helper struct representing a civil date decomposed into year, month, and day.
 */
@lang::NoArgConstructor
@lang::AllArgsConstructor
public struct YearMonthDay {
public:
    year  : long = 0L;
    month : int = 0;
    day   : int = 0;

    const getYear() : long {
        return year;
    }

    const getMonth() : int {
        return month;
    }

    const getDay() : int {
        return day;
    }

    const operator==(other: const YearMonthDay&) : bool {
        return year == other.year && month == other.month && day == other.day;
    }

    const operator!=(other: const YearMonthDay&) : bool {
        return year != other.year || month != other.month || day != other.day;
    }
}

/**
 * Pure proleptic Gregorian calendar mathematics.
 */
public final class GregorianMath {
public:
    /**
     * Check if a Gregorian year is a leap year.
     * Supports negative and zero astronomical years.
     */
    static isLeapYear(year: long) : bool {
        return (year % 4L == 0L && year % 100L != 0L) || (year % 400L == 0L);
    }

    /**
     * Return the length of the given month (1..12) in the specified year.
     */
    static lengthOfMonth(year: long, month: int) : int {
        if (month == 2) {
            return isLeapYear(year) ? 29 : 28;
        }
        if (month == 4 || month == 6 || month == 9 || month == 11) {
            return 30;
        }
        return 31;
    }

    /**
     * Return the length of the specified year (365 or 366 days).
     */
    static lengthOfYear(year: long) : int {
        return isLeapYear(year) ? 366 : 365;
    }

    /**
     * Return the 1-based day of year for the given date.
     */
    static dayOfYear(year: long, month: int, day: int) : int {
        doy : int = day;
        if (month > 1) { doy += 31; }
        if (month > 2) { doy += isLeapYear(year) ? 29 : 28; }
        if (month > 3) { doy += 31; }
        if (month > 4) { doy += 30; }
        if (month > 5) { doy += 31; }
        if (month > 6) { doy += 30; }
        if (month > 7) { doy += 31; }
        if (month > 8) { doy += 31; }
        if (month > 9) { doy += 30; }
        if (month > 10) { doy += 31; }
        if (month > 11) { doy += 30; }
        return doy;
    }

    /**
     * Convert a proleptic Gregorian civil date to epoch day (days since 1970-01-01).
     * Checked against overflow.
     */
    static dateToEpochDay(year: long, month: int, day: int) : long throws(TemporalArithmeticException) {
        y : long = year;
        m : long = (long) month;
        total : long = multiplyChecked(365L, y);
        if (y >= 0L) {
            leapDays : long = (y + 3L) / 4L - (y + 99L) / 100L + (y + 399L) / 400L;
            total = addChecked(total, leapDays);
        } else {
            leapDays : long = y / -4L - y / -100L + y / -400L;
            total = subtractChecked(total, leapDays);
        }
        monthDays : long = (367L * m - 362L) / 12L;
        total = addChecked(total, monthDays);
        total = addChecked(total, (long)(day - 1));
        if (m > 2L) {
            total = subtractChecked(total, 1L);
            if (!isLeapYear(year)) {
                total = subtractChecked(total, 1L);
            }
        }
        return subtractChecked(total, 719528L);
    }

    /**
     * Convert an epoch day (days since 1970-01-01) to a proleptic Gregorian date.
     * Checked against overflow.
     */
    static epochDayToDate(epochDay: long) : YearMonthDay throws(TemporalArithmeticException) {
        z : long = addChecked(epochDay, 719468L);
        era : long = floorDiv(z, 146097L);
        doe : long = floorMod(z, 146097L);
        yoe : long = (doe - doe / 1460L + doe / 36524L - doe / 146096L) / 365L;
        y : long = addChecked(multiplyChecked(era, 400L), yoe);
        doy : long = doe - (365L * yoe + yoe / 4L - yoe / 100L);
        mp : long = (5L * doy + 2L) / 153L;
        d : int = (int) (doy - (153L * mp + 2L) / 5L + 1L);
        m : int = (int) (mp < 10L ? mp + 3L : mp - 9L);
        if (m <= 2) {
            y = addChecked(y, 1L);
        }
        return YearMonthDay(y, m, d);
    }
}

/**
 * EpochDay — a chronology-neutral integral count of civil days relative to
 * the 1970-01-01 epoch.
 */
@lang::NoArgConstructor
@lang::AllArgsConstructor
public final struct EpochDay {
private:
    _days : long = 0L;

public:
    static ofDaysSinceEpoch(days: long) : EpochDay {
        return EpochDay(days);
    }

    static fromDate(year: long, month: int, day: int) : EpochDay throws(TemporalArithmeticException) {
        return EpochDay(GregorianMath::dateToEpochDay(year, month, day));
    }

    const daysSinceEpoch() : long {
        return _days;
    }

    const toDate() : YearMonthDay throws(TemporalArithmeticException) {
        return GregorianMath::epochDayToDate(_days);
    }

    const plusDays(days: long) : EpochDay throws(TemporalArithmeticException) {
        return EpochDay(addChecked(_days, days));
    }

    const minusDays(days: long) : EpochDay throws(TemporalArithmeticException) {
        return EpochDay(subtractChecked(_days, days));
    }

    const compareTo(other: const EpochDay&) : int {
        if (_days < other._days) return -1;
        if (_days > other._days) return 1;
        return 0;
    }

    const operator==(other: const EpochDay&) : bool {
        return _days == other._days;
    }

    const operator!=(other: const EpochDay&) : bool {
        return _days != other._days;
    }

    const operator<(other: const EpochDay&) : bool {
        return _days < other._days;
    }

    const operator<=(other: const EpochDay&) : bool {
        return _days <= other._days;
    }

    const operator>(other: const EpochDay&) : bool {
        return _days > other._days;
    }

    const operator>=(other: const EpochDay&) : bool {
        return _days >= other._days;
    }

    const operator+(days: long) : EpochDay throws(TemporalArithmeticException) {
        return plusDays(days);
    }

    const operator-(days: long) : EpochDay throws(TemporalArithmeticException) {
        return minusDays(days);
    }

    const operator-(other: const EpochDay&) : long throws(TemporalArithmeticException) {
        return subtractChecked(_days, other._days);
    }
}

} // namespace time
