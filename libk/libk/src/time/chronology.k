/*
 * K Language standard library — Chronology, Era, CalendarMonth, CalendarFields
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

// ── Private FFI declarations for ICU bridge runtime ──────────────────────────
@ffi::Extern("C") private __k_icu_is_available() : int;
@ffi::Extern("C") private __k_icu_calendar_from_epoch_day(cal_name: const byte*, epoch_day: long) : int;
@ffi::Extern("C") private __k_icu_calendar_to_epoch_day(cal_name: const byte*, era: int, year: long, month: int, day: int, is_leap: int) : int;
@ffi::Extern("C") private __k_icu_calendar_days_in_month(cal_name: const byte*, era: int, year: long, month: int, is_leap: int) : int;
@ffi::Extern("C") private __k_icu_calendar_months_in_year(cal_name: const byte*, era: int, year: long) : int;
@ffi::Extern("C") private __k_icu_calendar_is_leap_year(cal_name: const byte*, era: int, year: long) : int;

@ffi::Extern("C") private __k_icu_res_status() : int;
@ffi::Extern("C") private __k_icu_res_era() : int;
@ffi::Extern("C") private __k_icu_res_year() : long;
@ffi::Extern("C") private __k_icu_res_month() : int;
@ffi::Extern("C") private __k_icu_res_day() : int;
@ffi::Extern("C") private __k_icu_res_is_leap() : int;
@ffi::Extern("C") private __k_icu_res_epoch_day() : long;
@ffi::Extern("C") private __k_icu_res_days_in_month() : int;
@ffi::Extern("C") private __k_icu_res_months_in_year() : int;
@ffi::Extern("C") private __k_icu_res_is_leap_year() : int;
@ffi::Extern("C") private __k_icu_res_epoch_sec() : long;
@ffi::Extern("C") private __k_icu_res_nanos() : int;

@ffi::Extern("C") private __k_icu_get_era_display_name(cal_name: const byte*, locale_tag: const byte*, era_id: const byte*) : unsigned byte[]!;
@ffi::Extern("C") private __k_icu_is_locale_valid(locale_tag: const byte*) : int;
@ffi::Extern("C") private __k_icu_get_system_locale() : unsigned byte[]!;
@ffi::Extern("C") private __k_icu_format_pattern(locale_tag: const byte*, cal_name: const byte*, tz_id: const byte*, pattern: const byte*, epoch_sec: long, nanos: int) : unsigned byte[]!;
@ffi::Extern("C") private __k_icu_parse_pattern(locale_tag: const byte*, cal_name: const byte*, tz_id: const byte*, pattern: const byte*, text: const byte*) : int;

/**
 * ChronologyId — stable identifier for a civil calendar system.
 * Standard IDs: "ISO", "Gregorian", "Julian", "Japanese", "Buddhist", "Hebrew", "Islamic".
 */
public final class ChronologyId {
private:
    _value : String;

public:
    ChronologyId() {
        _value = String("ISO");
    }

    ChronologyId(value: const String&) {
        _value = value;
    }

    static of(value: const String&) : ChronologyId throws(InvalidTemporalValueException) {
        if (value.empty()) {
            throw InvalidTemporalValueException(501);
        }
        i : unsigned int = 0u;
        sz : unsigned int = value.size();
        while (i < sz) {
            c : char = value.at(i);
            isAlpha : bool = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            isDigit : bool = (c >= '0' && c <= '9');
            if (!isAlpha && !isDigit && c != '-' && c != '_') {
                throw InvalidTemporalValueException(501);
            }
            ++i;
        }
        return ChronologyId(value);
    }

    static iso() : ChronologyId {
        return ChronologyId(String("ISO"));
    }

    static gregorian() : ChronologyId {
        return ChronologyId(String("Gregorian"));
    }

    static julian() : ChronologyId {
        return ChronologyId(String("Julian"));
    }

    static japanese() : ChronologyId {
        return ChronologyId(String("Japanese"));
    }

    static buddhist() : ChronologyId {
        return ChronologyId(String("Buddhist"));
    }

    static hebrew() : ChronologyId {
        return ChronologyId(String("Hebrew"));
    }

    static islamic() : ChronologyId {
        return ChronologyId(String("Islamic"));
    }

    const value() : String {
        return _value;
    }

    const operator==(other: const ChronologyId&) : bool {
        return _value == other._value;
    }

    const operator!=(other: const ChronologyId&) : bool {
        return _value != other._value;
    }
}

/**
 * Era — calendar era (e.g. AD/BC in Gregorian, Reiwa in Japanese).
 */
public final class Era {
private:
    _id      : String;
    _calName : String;

public:
    Era() {
        _id = String();
        _calName = String();
    }

    Era(id: const String&) {
        _id = id;
        _calName = String();
    }

    Era(id: const String&, calName: const String&) {
        _id = id;
        _calName = calName;
    }

    static of(id: const String&) : Era throws(InvalidTemporalValueException) {
        if (id.empty()) {
            throw InvalidTemporalValueException(501);
        }
        return Era(id);
    }

    static of(id: const String&, calName: const String&) : Era throws(InvalidTemporalValueException) {
        if (id.empty()) {
            throw InvalidTemporalValueException(501);
        }
        return Era(id, calName);
    }

    const id() : String {
        return _id;
    }

    const displayName(locale: const Locale&) : String throws(TimeDataUnavailableException) {
        if (__k_icu_is_available() == 0) {
            throw TimeDataUnavailableException(510);
        }
        u8cal : unsigned byte[]! = _calName.empty() ? String("gregorian").toUtf8() : _calName.toUtf8();
        u8loc : unsigned byte[]! = locale.languageTag().toUtf8();
        u8id : unsigned byte[]! = _id.toUtf8();
        bytes : unsigned byte[]! = __k_icu_get_era_display_name((const byte*) &u8cal[0],
                                                                (const byte*) &u8loc[0],
                                                                (const byte*) &u8id[0]);
        if (bytes == null || bytes.size <= 1u) {
            throw TimeDataUnavailableException(510);
        }
        return String(bytes);
    }

    const operator==(other: const Era&) : bool {
        return _id == other._id;
    }

    const operator!=(other: const Era&) : bool {
        return _id != other._id;
    }
}

/**
 * CalendarMonth — month representation within a calendar system, supporting leap months.
 */
public final struct CalendarMonth {
private:
    _number      : int;
    _isLeapMonth : bool;

public:
    CalendarMonth() {
        _number = 1;
        _isLeapMonth = false;
    }

    CalendarMonth(number: int, isLeapMonth: bool) {
        _number = number;
        _isLeapMonth = isLeapMonth;
    }

    static of(number: int, isLeapMonth: bool) : CalendarMonth throws(InvalidTemporalValueException) {
        if (number < 1 || number > 14) {
            throw InvalidTemporalValueException(501);
        }
        return CalendarMonth(number, isLeapMonth);
    }

    const number() : int {
        return _number;
    }

    const isLeapMonth() : bool {
        return _isLeapMonth;
    }

    const operator==(other: const CalendarMonth&) : bool {
        return _number == other._number && _isLeapMonth == other._isLeapMonth;
    }

    const operator!=(other: const CalendarMonth&) : bool {
        return !(this == other);
    }
}

/**
 * CalendarFields — validated field representation for a civil date in a specific chronology.
 */
public final class CalendarFields {
private:
    _hasEra    : bool;
    _era       : Era;
    _yearOfEra : long;
    _month     : CalendarMonth;
    _day       : int;

    CalendarFields(hasEra: bool, era: const Era&, yearOfEra: long, month: const CalendarMonth&, day: int) {
        _hasEra = hasEra;
        _era = era;
        _yearOfEra = yearOfEra;
        _month = month;
        _day = day;
    }

public:
    CalendarFields() {
        _hasEra = false;
        _era = Era();
        _yearOfEra = 1970L;
        _month = CalendarMonth();
        _day = 1;
    }

    CalendarFields(other: const CalendarFields&) {
        _hasEra = other._hasEra;
        _era = other._era;
        _yearOfEra = other._yearOfEra;
        _month = other._month;
        _day = other._day;
    }

    static of(era: const Era?, yearOfEra: long, month: const CalendarMonth&, day: int) : CalendarFields
        throws(InvalidTemporalValueException) {
        if (day < 1 || day > 32) {
            throw InvalidTemporalValueException(501);
        }
        if (era != null) {
            return CalendarFields(true, *era, yearOfEra, month, day);
        }
        return CalendarFields(false, Era(), yearOfEra, month, day);
    }

    static of(yearOfEra: long, month: const CalendarMonth&, day: int) : CalendarFields
        throws(InvalidTemporalValueException) {
        return of(null, yearOfEra, month, day);
    }

    const hasEra() : bool {
        return _hasEra;
    }

    const era() : Era? {
        if (!_hasEra) {
            return null;
        }
        return &_era;
    }

    const yearOfEra() : long {
        return _yearOfEra;
    }

    const month() : CalendarMonth {
        return _month;
    }

    const day() : int {
        return _day;
    }

    const operator==(other: const CalendarFields&) : bool {
        if (_hasEra != other._hasEra) return false;
        if (_hasEra && _era != other._era) return false;
        return _yearOfEra == other._yearOfEra && _month == other._month && _day == other._day;
    }

    const operator!=(other: const CalendarFields&) : bool {
        return !(this == other);
    }
}

/**
 * Chronology — contract for civil calendar systems.
 */
public interface Chronology {
    id() : ChronologyId;
    isValid(fields: const CalendarFields&) : bool;
    date(fields: const CalendarFields&) : CalendarDate throws(InvalidTemporalValueException);
    daysInMonth(yearOfEra: long, month: const CalendarMonth&) : int throws(InvalidTemporalValueException);
    monthsInYear(yearOfEra: long) : int throws(InvalidTemporalValueException);
    isLeapYear(yearOfEra: long) : bool throws(InvalidTemporalValueException);
    plus(date: const CalendarDate&, period: const Period&) : CalendarDate throws(TemporalArithmeticException, InvalidTemporalValueException);
    minus(date: const CalendarDate&, period: const Period&) : CalendarDate throws(TemporalArithmeticException, InvalidTemporalValueException);
    toEpochDay(date: const CalendarDate&) : EpochDay;
    fromEpochDay(day: const EpochDay&) : CalendarDate throws(InvalidTemporalValueException);
}

// ═════════════════════════════════════════════════════════════════════════════
// GregorianChronology — proleptic Gregorian calendar system
// ═════════════════════════════════════════════════════════════════════════════

public final class GregorianChronology : public Chronology {
public:
    GregorianChronology() {}

    static instance() : GregorianChronology& {
        return _gregorianChronologyInstance;
    }

    override id() : ChronologyId {
        return ChronologyId::gregorian();
    }

    override isValid(fields: const CalendarFields&) : bool {
        if (fields.month().isLeapMonth()) return false;
        m : int = fields.month().number();
        if (m < 1 || m > 12) return false;
        maxDay : int = GregorianMath::lengthOfMonth(fields.yearOfEra(), m);
        if (fields.day() < 1 || fields.day() > maxDay) return false;
        return true;
    }

    override date(fields: const CalendarFields&) : CalendarDate throws(InvalidTemporalValueException) {
        if (!isValid(fields)) {
            throw InvalidTemporalValueException(501);
        }
        try {
            ed : EpochDay = EpochDay::fromDate(fields.yearOfEra(), fields.month().number(), fields.day());
            return CalendarDate(this, fields, ed);
        } catch (e: TemporalArithmeticException&) {
            throw InvalidTemporalValueException(501);
        }
    }

    override daysInMonth(yearOfEra: long, month: const CalendarMonth&) : int throws(InvalidTemporalValueException) {
        if (month.isLeapMonth() || month.number() < 1 || month.number() > 12) {
            throw InvalidTemporalValueException(501);
        }
        return GregorianMath::lengthOfMonth(yearOfEra, month.number());
    }

    override monthsInYear(yearOfEra: long) : int throws(InvalidTemporalValueException) {
        return 12;
    }

    override isLeapYear(yearOfEra: long) : bool throws(InvalidTemporalValueException) {
        return GregorianMath::isLeapYear(yearOfEra);
    }

    override plus(date: const CalendarDate&, period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        ld : LocalDate = date.toLocalDate();
        ldNew : LocalDate = ld.plus(period);
        m : CalendarMonth = CalendarMonth::of(ldNew.month(), false);
        f : CalendarFields = CalendarFields::of(date.fields().era(), ldNew.year(), m, ldNew.day());
        return this.date(f);
    }

    override minus(date: const CalendarDate&, period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        return plus(date, period.negated());
    }

    override toEpochDay(date: const CalendarDate&) : EpochDay {
        return date.toEpochDay();
    }

    override fromEpochDay(day: const EpochDay&) : CalendarDate throws(InvalidTemporalValueException) {
        try {
            ymd : YearMonthDay = day.toDate();
            m : CalendarMonth = CalendarMonth::of(ymd.getMonth(), false);
            f : CalendarFields = CalendarFields::of(null, ymd.getYear(), m, ymd.getDay());
            return CalendarDate(this, f, day);
        } catch (e: TemporalArithmeticException&) {
            throw InvalidTemporalValueException(501);
        }
    }
}

private _gregorianChronologyInstance : GregorianChronology;

// ═════════════════════════════════════════════════════════════════════════════
// IsoChronology — ISO-8601 calendar system (alias to proleptic Gregorian)
// ═════════════════════════════════════════════════════════════════════════════

public final class IsoChronology : public Chronology {
public:
    IsoChronology() {}

    static instance() : IsoChronology& {
        return _isoChronologyInstance;
    }

    override id() : ChronologyId {
        return ChronologyId::iso();
    }

    override isValid(fields: const CalendarFields&) : bool {
        return GregorianChronology::instance().isValid(fields);
    }

    override date(fields: const CalendarFields&) : CalendarDate throws(InvalidTemporalValueException) {
        cd : CalendarDate = GregorianChronology::instance().date(fields);
        return CalendarDate(this, cd.fields(), cd.toEpochDay());
    }

    override daysInMonth(yearOfEra: long, month: const CalendarMonth&) : int throws(InvalidTemporalValueException) {
        return GregorianChronology::instance().daysInMonth(yearOfEra, month);
    }

    override monthsInYear(yearOfEra: long) : int throws(InvalidTemporalValueException) {
        return 12;
    }

    override isLeapYear(yearOfEra: long) : bool throws(InvalidTemporalValueException) {
        return GregorianChronology::instance().isLeapYear(yearOfEra);
    }

    override plus(date: const CalendarDate&, period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        gregDate : CalendarDate = CalendarDate(GregorianChronology::instance(), date.fields(), date.toEpochDay());
        res : CalendarDate = GregorianChronology::instance().plus(gregDate, period);
        return CalendarDate(this, res.fields(), res.toEpochDay());
    }

    override minus(date: const CalendarDate&, period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        return plus(date, period.negated());
    }

    override toEpochDay(date: const CalendarDate&) : EpochDay {
        return date.toEpochDay();
    }

    override fromEpochDay(day: const EpochDay&) : CalendarDate throws(InvalidTemporalValueException) {
        res : CalendarDate = GregorianChronology::instance().fromEpochDay(day);
        return CalendarDate(this, res.fields(), res.toEpochDay());
    }
}

private _isoChronologyInstance : IsoChronology;

// ═════════════════════════════════════════════════════════════════════════════
// JulianChronology — proleptic Julian calendar system
// ═════════════════════════════════════════════════════════════════════════════

public final class JulianMath {
public:
    static isLeapYear(year: long) : bool {
        return (year % 4L) == 0L;
    }

    static lengthOfMonth(year: long, month: int) : int {
        if (month == 2) {
            return isLeapYear(year) ? 29 : 28;
        }
        if (month == 4 || month == 6 || month == 9 || month == 11) {
            return 30;
        }
        return 31;
    }

    static dateToEpochDay(year: long, month: int, day: int) : long throws(TemporalArithmeticException) {
        m_p : long = (long) ((month + 9) % 12);
        y_p : long = month <= 2 ? subtractChecked(year, 1L) : year;
        term1 : long = multiplyChecked(365L, y_p);
        term2 : long = floorDiv(y_p, 4L);
        term3 : long = (153L * m_p + 2L) / 5L;
        term4 : long = (long) (day - 1);
        total : long = addChecked(term1, term2);
        total = addChecked(total, term3);
        total = addChecked(total, term4);
        return subtractChecked(total, 719470L);
    }

    static epochDayToDate(epochDay: long) : YearMonthDay throws(TemporalArithmeticException) {
        jdn : long = addChecked(epochDay, 2440588L);
        c : long = addChecked(jdn, 32082L);
        d_ : long = floorDiv(multiplyChecked(4L, c) + 3L, 1461L);
        e : long = c - floorDiv(multiplyChecked(1461L, d_), 4L);
        m_ : long = (5L * e + 2L) / 153L;
        day : int = (int) (e - (153L * m_ + 2L) / 5L + 1L);
        month : int = (int) (m_ + 3L - 12L * (m_ / 10L));
        year : long = d_ - 4800L + (m_ / 10L);
        return YearMonthDay(year, month, day);
    }
}

public final class JulianChronology : public Chronology {
public:
    JulianChronology() {}

    static instance() : JulianChronology& {
        return _julianChronologyInstance;
    }

    override id() : ChronologyId {
        return ChronologyId::julian();
    }

    override isValid(fields: const CalendarFields&) : bool {
        if (fields.month().isLeapMonth()) return false;
        m : int = fields.month().number();
        if (m < 1 || m > 12) return false;
        maxDay : int = JulianMath::lengthOfMonth(fields.yearOfEra(), m);
        if (fields.day() < 1 || fields.day() > maxDay) return false;
        return true;
    }

    override date(fields: const CalendarFields&) : CalendarDate throws(InvalidTemporalValueException) {
        if (!isValid(fields)) {
            throw InvalidTemporalValueException(501);
        }
        try {
            ed : long = JulianMath::dateToEpochDay(fields.yearOfEra(), fields.month().number(), fields.day());
            return CalendarDate(this, fields, EpochDay(ed));
        } catch (e: TemporalArithmeticException&) {
            throw InvalidTemporalValueException(501);
        }
    }

    override daysInMonth(yearOfEra: long, month: const CalendarMonth&) : int throws(InvalidTemporalValueException) {
        if (month.isLeapMonth() || month.number() < 1 || month.number() > 12) {
            throw InvalidTemporalValueException(501);
        }
        return JulianMath::lengthOfMonth(yearOfEra, month.number());
    }

    override monthsInYear(yearOfEra: long) : int throws(InvalidTemporalValueException) {
        return 12;
    }

    override isLeapYear(yearOfEra: long) : bool throws(InvalidTemporalValueException) {
        return JulianMath::isLeapYear(yearOfEra);
    }

    override plus(date: const CalendarDate&, period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        y : long = addChecked(date.fields().yearOfEra(), period.yearsPart());
        m0 : long = (long) (date.fields().month().number() - 1);
        totalMonths : long = addChecked(m0, period.monthsPart());
        yearAdj : long = floorDiv(totalMonths, 12L);
        monthRem : long = floorMod(totalMonths, 12L);
        y = addChecked(y, yearAdj);
        newMonth : int = (int) (monthRem + 1L);

        maxDay : int = JulianMath::lengthOfMonth(y, newMonth);
        clampedDay : int = date.fields().day() > maxDay ? maxDay : date.fields().day();

        weeksDays : long = multiplyChecked(period.weeksPart(), 7L);
        totalDaysToAdd : long = addChecked(weeksDays, period.daysPart());

        edVal : long = JulianMath::dateToEpochDay(y, newMonth, clampedDay);
        ed : EpochDay = EpochDay(addChecked(edVal, totalDaysToAdd));
        return fromEpochDay(ed);
    }

    override minus(date: const CalendarDate&, period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        return plus(date, period.negated());
    }

    override toEpochDay(date: const CalendarDate&) : EpochDay {
        return date.toEpochDay();
    }

    override fromEpochDay(day: const EpochDay&) : CalendarDate throws(InvalidTemporalValueException) {
        try {
            ymd : YearMonthDay = JulianMath::epochDayToDate(day.daysSinceEpoch());
            m : CalendarMonth = CalendarMonth::of(ymd.getMonth(), false);
            f : CalendarFields = CalendarFields::of(null, ymd.getYear(), m, ymd.getDay());
            return CalendarDate(this, f, day);
        } catch (e: TemporalArithmeticException&) {
            throw InvalidTemporalValueException(501);
        }
    }
}

private _julianChronologyInstance : JulianChronology;

// ═════════════════════════════════════════════════════════════════════════════
// IcuCalendarMath & ICU Chronology implementations
// ═════════════════════════════════════════════════════════════════════════════

public final class IcuCalendarMath {
public:
    static isValid(calName: const String&, fields: const CalendarFields&) : bool {
        if (__k_icu_is_available() == 0) return false;
        try {
            maxDays : int = daysInMonth(calName, fields.yearOfEra(), fields.month());
            if (fields.day() < 1 || fields.day() > maxDays) return false;
            return true;
        } catch (e: InvalidTemporalValueException&) {
            return false;
        }
    }

    static date(chronology: Chronology&, calName: const String&, fields: const CalendarFields&) : CalendarDate
        throws(InvalidTemporalValueException) {
        if (__k_icu_is_available() == 0) {
            throw InvalidTemporalValueException(501);
        }
        u8cal : unsigned byte[]! = calName.toUtf8();
        eraVal : int = fields.hasEra() ? 0 : -1;
        if (fields.hasEra()) {
            eraObj : Era? = fields.era();
            if (eraObj != null) {
                idStr : String = eraObj->id();
                if (idStr == String("AD") || idStr == String("CE")) eraVal = 1;
                else if (idStr == String("BC") || idStr == String("BCE")) eraVal = 0;
                else if (calName == String("japanese")) eraVal = 236;
            }
        }
        res : int = __k_icu_calendar_to_epoch_day((const byte*) &u8cal[0],
                                                  eraVal,
                                                  fields.yearOfEra(),
                                                  fields.month().number(),
                                                  fields.day(),
                                                  fields.month().isLeapMonth() ? 1 : 0);
        if (res != 0) {
            throw InvalidTemporalValueException(501);
        }
        ed : long = __k_icu_res_epoch_day();
        return CalendarDate(chronology, fields, EpochDay(ed));
    }

    static daysInMonth(calName: const String&, yearOfEra: long, month: const CalendarMonth&) : int
        throws(InvalidTemporalValueException) {
        if (__k_icu_is_available() == 0) {
            throw InvalidTemporalValueException(501);
        }
        u8cal : unsigned byte[]! = calName.toUtf8();
        eraVal : int = -1;
        if (calName == String("japanese")) eraVal = 236;
        res : int = __k_icu_calendar_days_in_month((const byte*) &u8cal[0],
                                                   eraVal,
                                                   yearOfEra,
                                                   month.number(),
                                                   month.isLeapMonth() ? 1 : 0);
        if (res != 0) {
            throw InvalidTemporalValueException(501);
        }
        return __k_icu_res_days_in_month();
    }

    static monthsInYear(calName: const String&, yearOfEra: long) : int
        throws(InvalidTemporalValueException) {
        if (__k_icu_is_available() == 0) {
            throw InvalidTemporalValueException(501);
        }
        u8cal : unsigned byte[]! = calName.toUtf8();
        eraVal : int = -1;
        if (calName == String("japanese")) eraVal = 236;
        res : int = __k_icu_calendar_months_in_year((const byte*) &u8cal[0], eraVal, yearOfEra);
        if (res != 0) {
            throw InvalidTemporalValueException(501);
        }
        return __k_icu_res_months_in_year();
    }

    static isLeapYear(calName: const String&, yearOfEra: long) : bool
        throws(InvalidTemporalValueException) {
        if (__k_icu_is_available() == 0) {
            throw InvalidTemporalValueException(501);
        }
        u8cal : unsigned byte[]! = calName.toUtf8();
        eraVal : int = -1;
        if (calName == String("japanese")) eraVal = 236;
        res : int = __k_icu_calendar_is_leap_year((const byte*) &u8cal[0], eraVal, yearOfEra);
        if (res != 0) {
            throw InvalidTemporalValueException(501);
        }
        return __k_icu_res_is_leap_year() != 0;
    }

    static plus(chronology: Chronology&, calName: const String&, date: const CalendarDate&, period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        y : long = addChecked(date.fields().yearOfEra(), period.yearsPart());
        m : int = date.fields().month().number() + (int) period.monthsPart();
        maxMonths : int = monthsInYear(calName, y);
        while (m > maxMonths) {
            m -= maxMonths;
            y = addChecked(y, 1L);
            maxMonths = monthsInYear(calName, y);
        }
        while (m < 1) {
            y = subtractChecked(y, 1L);
            maxMonths = monthsInYear(calName, y);
            m += maxMonths;
        }
        calMonth : CalendarMonth = CalendarMonth::of(m, false);
        maxDay : int = daysInMonth(calName, y, calMonth);
        clampedDay : int = date.fields().day() > maxDay ? maxDay : date.fields().day();

        f : CalendarFields = CalendarFields::of(date.fields().era(), y, calMonth, clampedDay);
        baseDate : CalendarDate = IcuCalendarMath::date(chronology, calName, f);

        weeksDays : long = multiplyChecked(period.weeksPart(), 7L);
        totalDays : long = addChecked(weeksDays, period.daysPart());
        if (totalDays == 0L) {
            return baseDate;
        }
        newEd : EpochDay = baseDate.toEpochDay().plusDays(totalDays);
        return fromEpochDay(chronology, calName, newEd);
    }

    static fromEpochDay(chronology: Chronology&, calName: const String&, day: const EpochDay&) : CalendarDate
        throws(InvalidTemporalValueException) {
        if (__k_icu_is_available() == 0) {
            throw InvalidTemporalValueException(501);
        }
        u8cal : unsigned byte[]! = calName.toUtf8();
        res : int = __k_icu_calendar_from_epoch_day((const byte*) &u8cal[0], day.daysSinceEpoch());
        if (res != 0) {
            throw InvalidTemporalValueException(501);
        }
        eraVal : int = __k_icu_res_era();
        yVal : long = __k_icu_res_year();
        mVal : int = __k_icu_res_month();
        dVal : int = __k_icu_res_day();
        isLeapVal : bool = __k_icu_res_is_leap() != 0;

        eraStr : String;
        sb : StringBuilder;
        IsoWriter::appendDigits(sb, (long) eraVal, 1);
        eraStr = sb.toString();
        eraObj : Era = Era(eraStr, calName);
        eraView : Era? = &eraObj;
        calMonth : CalendarMonth = CalendarMonth::of(mVal, isLeapVal);
        f : CalendarFields = CalendarFields::of(eraView, yVal, calMonth, dVal);
        return CalendarDate(chronology, f, day);
    }
}

// ── JapaneseChronology ───────────────────────────────────────────────────────

public final class JapaneseChronology : public Chronology {
public:
    JapaneseChronology() {}

    static instance() : JapaneseChronology& {
        return _japaneseChronologyInstance;
    }

    override id() : ChronologyId {
        return ChronologyId::japanese();
    }

    override isValid(fields: const CalendarFields&) : bool {
        return IcuCalendarMath::isValid(String("japanese"), fields);
    }

    override date(fields: const CalendarFields&) : CalendarDate throws(InvalidTemporalValueException) {
        return IcuCalendarMath::date(this, String("japanese"), fields);
    }

    override daysInMonth(yearOfEra: long, month: const CalendarMonth&) : int throws(InvalidTemporalValueException) {
        return IcuCalendarMath::daysInMonth(String("japanese"), yearOfEra, month);
    }

    override monthsInYear(yearOfEra: long) : int throws(InvalidTemporalValueException) {
        return IcuCalendarMath::monthsInYear(String("japanese"), yearOfEra);
    }

    override isLeapYear(yearOfEra: long) : bool throws(InvalidTemporalValueException) {
        return IcuCalendarMath::isLeapYear(String("japanese"), yearOfEra);
    }

    override plus(date: const CalendarDate&, period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        return IcuCalendarMath::plus(this, String("japanese"), date, period);
    }

    override minus(date: const CalendarDate&, period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        return IcuCalendarMath::plus(this, String("japanese"), date, period.negated());
    }

    override toEpochDay(date: const CalendarDate&) : EpochDay {
        return date.toEpochDay();
    }

    override fromEpochDay(day: const EpochDay&) : CalendarDate throws(InvalidTemporalValueException) {
        return IcuCalendarMath::fromEpochDay(this, String("japanese"), day);
    }
}

private _japaneseChronologyInstance : JapaneseChronology;

// ── BuddhistChronology ───────────────────────────────────────────────────────

public final class BuddhistChronology : public Chronology {
public:
    BuddhistChronology() {}

    static instance() : BuddhistChronology& {
        return _buddhistChronologyInstance;
    }

    override id() : ChronologyId {
        return ChronologyId::buddhist();
    }

    override isValid(fields: const CalendarFields&) : bool {
        return IcuCalendarMath::isValid(String("buddhist"), fields);
    }

    override date(fields: const CalendarFields&) : CalendarDate throws(InvalidTemporalValueException) {
        return IcuCalendarMath::date(this, String("buddhist"), fields);
    }

    override daysInMonth(yearOfEra: long, month: const CalendarMonth&) : int throws(InvalidTemporalValueException) {
        return IcuCalendarMath::daysInMonth(String("buddhist"), yearOfEra, month);
    }

    override monthsInYear(yearOfEra: long) : int throws(InvalidTemporalValueException) {
        return IcuCalendarMath::monthsInYear(String("buddhist"), yearOfEra);
    }

    override isLeapYear(yearOfEra: long) : bool throws(InvalidTemporalValueException) {
        return IcuCalendarMath::isLeapYear(String("buddhist"), yearOfEra);
    }

    override plus(date: const CalendarDate&, period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        return IcuCalendarMath::plus(this, String("buddhist"), date, period);
    }

    override minus(date: const CalendarDate&, period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        return IcuCalendarMath::plus(this, String("buddhist"), date, period.negated());
    }

    override toEpochDay(date: const CalendarDate&) : EpochDay {
        return date.toEpochDay();
    }

    override fromEpochDay(day: const EpochDay&) : CalendarDate throws(InvalidTemporalValueException) {
        return IcuCalendarMath::fromEpochDay(this, String("buddhist"), day);
    }
}

private _buddhistChronologyInstance : BuddhistChronology;

// ── HebrewChronology ─────────────────────────────────────────────────────────

public final class HebrewChronology : public Chronology {
public:
    HebrewChronology() {}

    static instance() : HebrewChronology& {
        return _hebrewChronologyInstance;
    }

    override id() : ChronologyId {
        return ChronologyId::hebrew();
    }

    override isValid(fields: const CalendarFields&) : bool {
        return IcuCalendarMath::isValid(String("hebrew"), fields);
    }

    override date(fields: const CalendarFields&) : CalendarDate throws(InvalidTemporalValueException) {
        return IcuCalendarMath::date(this, String("hebrew"), fields);
    }

    override daysInMonth(yearOfEra: long, month: const CalendarMonth&) : int throws(InvalidTemporalValueException) {
        return IcuCalendarMath::daysInMonth(String("hebrew"), yearOfEra, month);
    }

    override monthsInYear(yearOfEra: long) : int throws(InvalidTemporalValueException) {
        return IcuCalendarMath::monthsInYear(String("hebrew"), yearOfEra);
    }

    override isLeapYear(yearOfEra: long) : bool throws(InvalidTemporalValueException) {
        return IcuCalendarMath::isLeapYear(String("hebrew"), yearOfEra);
    }

    override plus(date: const CalendarDate&, period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        return IcuCalendarMath::plus(this, String("hebrew"), date, period);
    }

    override minus(date: const CalendarDate&, period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        return IcuCalendarMath::plus(this, String("hebrew"), date, period.negated());
    }

    override toEpochDay(date: const CalendarDate&) : EpochDay {
        return date.toEpochDay();
    }

    override fromEpochDay(day: const EpochDay&) : CalendarDate throws(InvalidTemporalValueException) {
        return IcuCalendarMath::fromEpochDay(this, String("hebrew"), day);
    }
}

private _hebrewChronologyInstance : HebrewChronology;

// ── IslamicChronology ────────────────────────────────────────────────────────

public final class IslamicChronology : public Chronology {
public:
    IslamicChronology() {}

    static instance() : IslamicChronology& {
        return _islamicChronologyInstance;
    }

    override id() : ChronologyId {
        return ChronologyId::islamic();
    }

    override isValid(fields: const CalendarFields&) : bool {
        return IcuCalendarMath::isValid(String("islamic"), fields);
    }

    override date(fields: const CalendarFields&) : CalendarDate throws(InvalidTemporalValueException) {
        return IcuCalendarMath::date(this, String("islamic"), fields);
    }

    override daysInMonth(yearOfEra: long, month: const CalendarMonth&) : int throws(InvalidTemporalValueException) {
        return IcuCalendarMath::daysInMonth(String("islamic"), yearOfEra, month);
    }

    override monthsInYear(yearOfEra: long) : int throws(InvalidTemporalValueException) {
        return IcuCalendarMath::monthsInYear(String("islamic"), yearOfEra);
    }

    override isLeapYear(yearOfEra: long) : bool throws(InvalidTemporalValueException) {
        return IcuCalendarMath::isLeapYear(String("islamic"), yearOfEra);
    }

    override plus(date: const CalendarDate&, period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        return IcuCalendarMath::plus(this, String("islamic"), date, period);
    }

    override minus(date: const CalendarDate&, period: const Period&) : CalendarDate
        throws(TemporalArithmeticException, InvalidTemporalValueException) {
        return IcuCalendarMath::plus(this, String("islamic"), date, period.negated());
    }

    override toEpochDay(date: const CalendarDate&) : EpochDay {
        return date.toEpochDay();
    }

    override fromEpochDay(day: const EpochDay&) : CalendarDate throws(InvalidTemporalValueException) {
        return IcuCalendarMath::fromEpochDay(this, String("islamic"), day);
    }
}

private _islamicChronologyInstance : IslamicChronology;

/**
 * Chronologies — factory for obtaining Chronology instances by id.
 */
public final class Chronologies {
public:
    static of(id: const ChronologyId&) : Chronology& throws(TimeDataUnavailableException, InvalidTemporalValueException) {
        val : String = id.value();
        if (val == String("Gregorian")) return GregorianChronology::instance();
        if (val == String("ISO")) return IsoChronology::instance();
        if (val == String("Julian")) return JulianChronology::instance();
        if (val == String("Japanese")) return JapaneseChronology::instance();
        if (val == String("Buddhist")) return BuddhistChronology::instance();
        if (val == String("Hebrew")) return HebrewChronology::instance();
        if (val == String("Islamic")) return IslamicChronology::instance();
        throw TimeDataUnavailableException(510);
    }
}

} // namespace time
