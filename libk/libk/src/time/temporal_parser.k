/*
 * K Language standard library — TemporalParser, TemporalParsers
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
 * TemporalParser — contract for parsing temporal text representations.
 */
public interface TemporalParser {
    parseLocalDate(text: const String&) : LocalDate throws(TemporalParseException, InvalidTemporalValueException);
    parseLocalDateTime(text: const String&) : LocalDateTime throws(TemporalParseException, InvalidTemporalValueException);
    parseInstant(text: const String&) : Instant throws(TemporalParseException, LeapSecondException, TimeScaleDataUnavailableException);
    parseZonedDateTime(text: const String&, resolver: const LocalDateTimeResolver&) : ZonedDateTime
        throws(TemporalParseException, LeapSecondException, ZoneRulesUnavailableException, LocalTimeGapException, LocalTimeOverlapException);
    parseCalendarDateTime(text: const String&) : CalendarDateTime
        throws(TemporalParseException, InvalidTemporalValueException, TimeDataUnavailableException);
    withLocale(locale: const Locale&) : TemporalParser!;
    withChronology(chronology: const Chronology&) : TemporalParser!;
    withZone(zone: const TimeZone&) : TemporalParser!;
}

// ═════════════════════════════════════════════════════════════════════════════
// IsoTemporalParser — canonical ISO-8601 parser
// ═════════════════════════════════════════════════════════════════════════════

public final class IsoTemporalParser : public TemporalParser {
private:
    _locale     : Locale;
    _chronology : Chronology*;
    _zone       : TimeZone;

public:
    IsoTemporalParser() {
        _locale = Locale();
        _chronology = &IsoChronology::instance();
        _zone = TimeZone::fixed(ZoneOffset::utc());
    }

    IsoTemporalParser(locale: const Locale&, chronology: Chronology*, zone: const TimeZone&) {
        _locale = locale;
        _chronology = chronology;
        _zone = zone;
    }

    override parseLocalDate(text: const String&) : LocalDate throws(TemporalParseException, InvalidTemporalValueException) {
        return Iso::parseLocalDate(text);
    }

    override parseLocalDateTime(text: const String&) : LocalDateTime throws(TemporalParseException, InvalidTemporalValueException) {
        return Iso::parseLocalDateTime(text);
    }

    override parseInstant(text: const String&) : Instant throws(TemporalParseException, LeapSecondException, TimeScaleDataUnavailableException) {
        try {
            return Iso::parseInstant(text);
        } catch (e: InvalidTemporalValueException&) {
            throw TemporalParseException(503);
        } catch (e2: TemporalArithmeticException&) {
            throw TemporalParseException(503);
        }
    }

    override parseZonedDateTime(text: const String&, resolver: const LocalDateTimeResolver&) : ZonedDateTime
        throws(TemporalParseException, LeapSecondException, ZoneRulesUnavailableException, LocalTimeGapException, LocalTimeOverlapException) {
        try {
            return Iso::parseZonedDateTime(text, resolver);
        } catch (e: InvalidTemporalValueException&) {
            throw TemporalParseException(503);
        } catch (e2: TemporalArithmeticException&) {
            throw TemporalParseException(503);
        }
    }

    override parseCalendarDateTime(text: const String&) : CalendarDateTime
        throws(TemporalParseException, InvalidTemporalValueException, TimeDataUnavailableException) {
        ldt : LocalDateTime = Iso::parseLocalDateTime(text);
        try {
            cd : CalendarDate = _chronology->fromEpochDay(ldt.date().toEpochDay());
            return CalendarDateTime::of(cd, ldt.time());
        } catch (e: TemporalArithmeticException&) {
            throw InvalidTemporalValueException(501);
        }
    }

    override withLocale(locale: const Locale&) : TemporalParser! {
        return new IsoTemporalParser(locale, _chronology, _zone);
    }

    override withChronology(chronology: const Chronology&) : TemporalParser! {
        ChronologyPtr : Chronology* = &((Chronology&) chronology);
        return new IsoTemporalParser(_locale, ChronologyPtr, _zone);
    }

    override withZone(zone: const TimeZone&) : TemporalParser! {
        return new IsoTemporalParser(_locale, _chronology, zone);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// PatternTemporalParser — pattern-based temporal parser
// ═════════════════════════════════════════════════════════════════════════════

public final class PatternTemporalParser : public TemporalParser {
private:
    _pattern    : String;
    _locale     : Locale;
    _chronology : Chronology*;
    _zone       : TimeZone;

    static parsePureK(pattern: const String&, text: const String&,
                      year: long&, month: int&, day: int&,
                      hour: int&, minute: int&, second: int&, nano: int&)
        throws(TemporalParseException) {
        psz : unsigned int = pattern.size();
        tsz : unsigned int = text.size();
        pi : unsigned int = 0u;
        ti : unsigned int = 0u;

        year = 1970L;
        month = 1;
        day = 1;
        hour = 0;
        minute = 0;
        second = 0;
        nano = 0;

        while (pi < psz) {
            pc : char = pattern.at(pi);
            if (pc == '\'') {
                ++pi;
                while (pi < psz && pattern.at(pi) != '\'') {
                    if (ti >= tsz || text.at(ti) != pattern.at(pi)) {
                        throw TemporalParseException(503);
                    }
                    ++pi;
                    ++ti;
                }
                if (pi < psz) ++pi;
            } else if (pc == 'y') {
                count : int = 0;
                while (pi < psz && pattern.at(pi) == 'y') { ++count; ++pi; }
                val : long = 0L;
                digitsRead : int = 0;
                while (ti < tsz && text.at(ti) >= '0' && text.at(ti) <= '9') {
                    val = val * 10L + (long) (((int) text.at(ti)) - ((int) '0'));
                    ++ti;
                    ++digitsRead;
                    if (count == 2 && digitsRead == 2) break;
                    if (count == 4 && digitsRead == 4) break;
                }
                if (digitsRead == 0) throw TemporalParseException(503);
                if (count == 2 && digitsRead == 2) {
                    year = val >= 70L ? 1900L + val : 2000L + val;
                } else {
                    year = val;
                }
            } else if (pc == 'M') {
                count : int = 0;
                while (pi < psz && pattern.at(pi) == 'M') { ++count; ++pi; }
                val : int = 0;
                digitsRead : int = 0;
                while (ti < tsz && text.at(ti) >= '0' && text.at(ti) <= '9') {
                    val = val * 10 + (((int) text.at(ti)) - ((int) '0'));
                    ++ti;
                    ++digitsRead;
                    if (digitsRead == count) break;
                }
                if (digitsRead == 0) throw TemporalParseException(503);
                month = val;
            } else if (pc == 'd') {
                count : int = 0;
                while (pi < psz && pattern.at(pi) == 'd') { ++count; ++pi; }
                val : int = 0;
                digitsRead : int = 0;
                while (ti < tsz && text.at(ti) >= '0' && text.at(ti) <= '9') {
                    val = val * 10 + (((int) text.at(ti)) - ((int) '0'));
                    ++ti;
                    ++digitsRead;
                    if (digitsRead == count) break;
                }
                if (digitsRead == 0) throw TemporalParseException(503);
                day = val;
            } else if (pc == 'H') {
                count : int = 0;
                while (pi < psz && pattern.at(pi) == 'H') { ++count; ++pi; }
                val : int = 0;
                digitsRead : int = 0;
                while (ti < tsz && text.at(ti) >= '0' && text.at(ti) <= '9') {
                    val = val * 10 + (((int) text.at(ti)) - ((int) '0'));
                    ++ti;
                    ++digitsRead;
                    if (digitsRead == count) break;
                }
                if (digitsRead == 0) throw TemporalParseException(503);
                hour = val;
            } else if (pc == 'm') {
                count : int = 0;
                while (pi < psz && pattern.at(pi) == 'm') { ++count; ++pi; }
                val : int = 0;
                digitsRead : int = 0;
                while (ti < tsz && text.at(ti) >= '0' && text.at(ti) <= '9') {
                    val = val * 10 + (((int) text.at(ti)) - ((int) '0'));
                    ++ti;
                    ++digitsRead;
                    if (digitsRead == count) break;
                }
                if (digitsRead == 0) throw TemporalParseException(503);
                minute = val;
            } else if (pc == 's') {
                count : int = 0;
                while (pi < psz && pattern.at(pi) == 's') { ++count; ++pi; }
                val : int = 0;
                digitsRead : int = 0;
                while (ti < tsz && text.at(ti) >= '0' && text.at(ti) <= '9') {
                    val = val * 10 + (((int) text.at(ti)) - ((int) '0'));
                    ++ti;
                    ++digitsRead;
                    if (digitsRead == count) break;
                }
                if (digitsRead == 0) throw TemporalParseException(503);
                second = val;
            } else if (pc == 'S') {
                count : int = 0;
                while (pi < psz && pattern.at(pi) == 'S') { ++count; ++pi; }
                val : int = 0;
                digitsRead : int = 0;
                while (ti < tsz && text.at(ti) >= '0' && text.at(ti) <= '9') {
                    val = val * 10 + (((int) text.at(ti)) - ((int) '0'));
                    ++ti;
                    ++digitsRead;
                    if (digitsRead == count) break;
                }
                if (digitsRead == 0) throw TemporalParseException(503);
                while (digitsRead < 3) { val = val * 10; ++digitsRead; }
                nano = val * 1000000;
            } else {
                if (ti >= tsz || text.at(ti) != pc) {
                    throw TemporalParseException(503);
                }
                ++pi;
                ++ti;
            }
        }
        if (ti != tsz) {
            throw TemporalParseException(503);
        }
    }

    const parseViaIcu(text: const String&, sec: long&, nanos: int&) : bool {
        if (__k_icu_is_available() == 0) return false;
        u8loc : unsigned byte[]! = _locale.languageTag().toUtf8();
        calName : String = String("gregorian");
        if (_chronology != null) {
            c : Chronology* = _chronology;
            calName = c->id().value();
        }
        u8cal : unsigned byte[]! = calName.toUtf8();
        zoneId : String = _zone.id().name();
        u8zone : unsigned byte[]! = zoneId.toUtf8();
        u8pat : unsigned byte[]! = _pattern.toUtf8();
        u8text : unsigned byte[]! = text.toUtf8();
        res : int = __k_icu_parse_pattern((const byte*) &u8loc[0],
                                          (const byte*) &u8cal[0],
                                          (const byte*) &u8zone[0],
                                          (const byte*) &u8pat[0],
                                          (const byte*) &u8text[0]);
        if (res != 0) return false;
        sec = __k_icu_res_epoch_sec();
        nanos = __k_icu_res_nanos();
        return true;
    }

public:
    PatternTemporalParser(pattern: const String&) {
        _pattern = pattern;
        _locale = Locale();
        _chronology = &GregorianChronology::instance();
        _zone = TimeZone::fixed(ZoneOffset::utc());
    }

    PatternTemporalParser(pattern: const String&, locale: const Locale&, chronology: Chronology*, zone: const TimeZone&) {
        _pattern = pattern;
        _locale = locale;
        _chronology = chronology;
        _zone = zone;
    }

    override parseLocalDate(text: const String&) : LocalDate
        throws(TemporalParseException, InvalidTemporalValueException) {
        y : long = 0L;
        m : int = 0;
        d : int = 0;
        h : int = 0;
        min : int = 0;
        s : int = 0;
        n : int = 0;
        try {
            parsePureK(_pattern, text, y, m, d, h, min, s, n);
            return LocalDate::of(y, m, d);
        } catch (e: TemporalParseException&) {
            sec : long = 0L;
            nanos : int = 0;
            if (parseViaIcu(text, sec, nanos)) {
                days : long = floorDiv(sec, 86400L);
                try {
                    return LocalDate::fromEpochDay(EpochDay(days));
                } catch (e2: TemporalArithmeticException&) {
                    throw InvalidTemporalValueException(501);
                }
            }
            throw e;
        }
    }

    override parseLocalDateTime(text: const String&) : LocalDateTime
        throws(TemporalParseException, InvalidTemporalValueException) {
        y : long = 0L;
        m : int = 0;
        d : int = 0;
        h : int = 0;
        min : int = 0;
        s : int = 0;
        n : int = 0;
        try {
            parsePureK(_pattern, text, y, m, d, h, min, s, n);
            ld : LocalDate = LocalDate::of(y, m, d);
            lt : LocalTime = LocalTime::of(h, min, s, n);
            return LocalDateTime::of(ld, lt);
        } catch (e: TemporalParseException&) {
            sec : long = 0L;
            nanos : int = 0;
            if (parseViaIcu(text, sec, nanos)) {
                days : long = floorDiv(sec, 86400L);
                sod : long = floorMod(sec, 86400L);
                try {
                    ld : LocalDate = LocalDate::fromEpochDay(EpochDay(days));
                    lh : int = (int) (sod / 3600L);
                    lm : int = (int) ((sod % 3600L) / 60L);
                    ls : int = (int) (sod % 60L);
                    lt : LocalTime = LocalTime::of(lh, lm, ls, nanos);
                    return LocalDateTime::of(ld, lt);
                } catch (e2: TemporalArithmeticException&) {
                    throw InvalidTemporalValueException(501);
                }
            }
            throw e;
        }
    }

    override parseInstant(text: const String&) : Instant
        throws(TemporalParseException, LeapSecondException, TimeScaleDataUnavailableException) {
        try {
            ldt : LocalDateTime = parseLocalDateTime(text);
            sec : long = GregorianMath::dateToEpochDay(ldt.year(), ldt.month(), ldt.day());
            timeSec : long = ((long) ldt.time().hour()) * 3600L + ((long) ldt.time().minute()) * 60L + ((long) ldt.time().second());
            epochSec : long = addChecked(multiplyChecked(sec, 86400L), timeSec);
            return Instant::ofEpochSecond(epochSec, ldt.time().nano());
        } catch (e: InvalidTemporalValueException&) {
            throw TemporalParseException(503);
        } catch (e2: TemporalArithmeticException&) {
            throw TemporalParseException(503);
        }
    }

    override parseZonedDateTime(text: const String&, resolver: const LocalDateTimeResolver&) : ZonedDateTime
        throws(TemporalParseException, LeapSecondException, ZoneRulesUnavailableException, LocalTimeGapException, LocalTimeOverlapException) {
        z : TimeZone = _zone;
        try {
            ldt : LocalDateTime = parseLocalDateTime(text);
            return ZonedDateTime::of(ldt, z, resolver);
        } catch (e: InvalidTemporalValueException&) {
            throw TemporalParseException(503);
        } catch (e2: TemporalArithmeticException&) {
            throw TemporalParseException(503);
        }
    }

    override parseCalendarDateTime(text: const String&) : CalendarDateTime
        throws(TemporalParseException, InvalidTemporalValueException, TimeDataUnavailableException) {
        y : long = 0L;
        m : int = 0;
        d : int = 0;
        h : int = 0;
        min : int = 0;
        s : int = 0;
        n : int = 0;
        parsePureK(_pattern, text, y, m, d, h, min, s, n);
        calMonth : CalendarMonth = CalendarMonth::of(m, false);
        f : CalendarFields = CalendarFields::of(null, y, calMonth, d);
        calDate : CalendarDate = _chronology->date(f);
        calTime : LocalTime = LocalTime::of(h, min, s, n);
        return CalendarDateTime::of(calDate, calTime);
    }

    override withLocale(locale: const Locale&) : TemporalParser! {
        return new PatternTemporalParser(_pattern, locale, _chronology, _zone);
    }

    override withChronology(chronology: const Chronology&) : TemporalParser! {
        ChronologyPtr : Chronology* = &((Chronology&) chronology);
        return new PatternTemporalParser(_pattern, _locale, ChronologyPtr, _zone);
    }

    override withZone(zone: const TimeZone&) : TemporalParser! {
        return new PatternTemporalParser(_pattern, _locale, _chronology, zone);
    }
}

/**
 * TemporalParsers — factory for obtaining standard and pattern temporal parsers.
 */
public final class TemporalParsers {
public:
    static iso() : TemporalParser! {
        return new IsoTemporalParser();
    }

    static ofPattern(pattern: const String&) : TemporalParser!
        throws(TemporalParseException, TimeDataUnavailableException) {
        if (pattern.empty()) {
            throw TemporalParseException(503);
        }
        return new PatternTemporalParser(pattern);
    }
}

} // namespace time
