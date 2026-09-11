/*
 * K Language standard library — TemporalFormatter, TemporalFormatters
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
 * TemporalFormatter — contract for formatting temporal values into text.
 */
public interface TemporalFormatter {
    formatInstant(value: const Instant&, zone: const TimeZone&) : String throws(TimeDataUnavailableException);
    formatLocalDate(value: const LocalDate&) : String throws(TimeDataUnavailableException);
    formatLocalDateTime(value: const LocalDateTime&) : String throws(TimeDataUnavailableException);
    formatZonedDateTime(value: const ZonedDateTime&) : String throws(TimeDataUnavailableException);
    formatCalendarDateTime(value: const CalendarDateTime&) : String throws(TimeDataUnavailableException);
    withLocale(locale: const Locale&) : TemporalFormatter!;
    withChronology(chronology: const Chronology&) : TemporalFormatter!;
    withZone(zone: const TimeZone&) : TemporalFormatter!;
}

// ═════════════════════════════════════════════════════════════════════════════
// IsoTemporalFormatter — canonical ISO-8601 formatter
// ═════════════════════════════════════════════════════════════════════════════

public final class IsoTemporalFormatter : public TemporalFormatter {
private:
    _locale     : Locale;
    _chronology : Chronology*;
    _zone       : TimeZone;

public:
    IsoTemporalFormatter() {
        _locale = Locale();
        _chronology = &IsoChronology::instance();
        _zone = TimeZone::fixed(ZoneOffset::utc());
    }

    IsoTemporalFormatter(locale: const Locale&, chronology: Chronology*, zone: const TimeZone&) {
        _locale = locale;
        _chronology = chronology;
        _zone = zone;
    }

    override formatInstant(value: const Instant&, zone: const TimeZone&) : String throws(TimeDataUnavailableException) {
        if (zone.id().name() == String("UTC")) {
            return Iso::formatInstant(value);
        }
        zdt : ZonedDateTime = ZonedDateTime::fromInstant(value, zone);
        return Iso::formatZonedDateTime(zdt);
    }

    override formatLocalDate(value: const LocalDate&) : String throws(TimeDataUnavailableException) {
        return Iso::formatLocalDate(value);
    }

    override formatLocalDateTime(value: const LocalDateTime&) : String throws(TimeDataUnavailableException) {
        return Iso::formatLocalDateTime(value);
    }

    override formatZonedDateTime(value: const ZonedDateTime&) : String throws(TimeDataUnavailableException) {
        return Iso::formatZonedDateTime(value);
    }

    override formatCalendarDateTime(value: const CalendarDateTime&) : String throws(TimeDataUnavailableException) {
        sb : StringBuilder;
        IsoWriter::appendDigits(sb, value.date().fields().yearOfEra(), 4);
        sb.appendChar('-');
        IsoWriter::appendTwoDigits(sb, value.date().fields().month().number());
        sb.appendChar('-');
        IsoWriter::appendTwoDigits(sb, value.date().fields().day());
        sb.appendChar('T');
        sb.append(Iso::formatLocalTime(value.time()));
        return sb.toString();
    }

    override withLocale(locale: const Locale&) : TemporalFormatter! {
        return new IsoTemporalFormatter(locale, _chronology, _zone);
    }

    override withChronology(chronology: const Chronology&) : TemporalFormatter! {
        ChronologyPtr : Chronology* = &((Chronology&) chronology);
        return new IsoTemporalFormatter(_locale, ChronologyPtr, _zone);
    }

    override withZone(zone: const TimeZone&) : TemporalFormatter! {
        return new IsoTemporalFormatter(_locale, _chronology, zone);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// PatternTemporalFormatter — pattern-based temporal formatter
// ═════════════════════════════════════════════════════════════════════════════

public final class PatternTemporalFormatter : public TemporalFormatter {
private:
    _pattern    : String;
    _locale     : Locale;
    _chronology : Chronology*;
    _zone       : TimeZone;

    static formatPureK(pattern: const String&, year: long, month: int, day: int,
                       hour: int, minute: int, second: int, nano: int) : String {
        sb : StringBuilder;
        sz : unsigned int = pattern.size();
        i : unsigned int = 0u;
        while (i < sz) {
            c : char = pattern.at(i);
            if (c == '\'') {
                // Quoted literal
                ++i;
                while (i < sz && pattern.at(i) != '\'') {
                    sb.appendChar(pattern.at(i));
                    ++i;
                }
                if (i < sz) ++i;
            } else if (c == 'y') {
                count : int = 0;
                while (i < sz && pattern.at(i) == 'y') { ++count; ++i; }
                if (count == 2) {
                    y2 : int = (int) (year % 100L);
                    if (y2 < 0) y2 = -y2;
                    IsoWriter::appendTwoDigits(sb, y2);
                } else {
                    IsoWriter::appendDigits(sb, year, count);
                }
            } else if (c == 'M') {
                count : int = 0;
                while (i < sz && pattern.at(i) == 'M') { ++count; ++i; }
                if (count >= 2) IsoWriter::appendTwoDigits(sb, month);
                else IsoWriter::appendDigits(sb, (long) month, 1);
            } else if (c == 'd') {
                count : int = 0;
                while (i < sz && pattern.at(i) == 'd') { ++count; ++i; }
                if (count >= 2) IsoWriter::appendTwoDigits(sb, day);
                else IsoWriter::appendDigits(sb, (long) day, 1);
            } else if (c == 'H') {
                count : int = 0;
                while (i < sz && pattern.at(i) == 'H') { ++count; ++i; }
                if (count >= 2) IsoWriter::appendTwoDigits(sb, hour);
                else IsoWriter::appendDigits(sb, (long) hour, 1);
            } else if (c == 'm') {
                count : int = 0;
                while (i < sz && pattern.at(i) == 'm') { ++count; ++i; }
                if (count >= 2) IsoWriter::appendTwoDigits(sb, minute);
                else IsoWriter::appendDigits(sb, (long) minute, 1);
            } else if (c == 's') {
                count : int = 0;
                while (i < sz && pattern.at(i) == 's') { ++count; ++i; }
                if (count >= 2) IsoWriter::appendTwoDigits(sb, second);
                else IsoWriter::appendDigits(sb, (long) second, 1);
            } else if (c == 'S') {
                count : int = 0;
                while (i < sz && pattern.at(i) == 'S') { ++count; ++i; }
                millis : int = nano / 1000000;
                IsoWriter::appendDigits(sb, (long) millis, count > 3 ? 3 : count);
            } else {
                sb.appendChar(c);
                ++i;
            }
        }
        return sb.toString();
    }

    const formatViaIcu(epochSec: long, nanos: int) : String? {
        if (__k_icu_is_available() == 0) return null;
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
        bytes : unsigned byte[]! = __k_icu_format_pattern((const byte*) &u8loc[0],
                                                          (const byte*) &u8cal[0],
                                                          (const byte*) &u8zone[0],
                                                          (const byte*) &u8pat[0],
                                                          epochSec, nanos);
        if (bytes == null || bytes.size <= 1u) return null;
        return String(bytes);
    }

public:
    PatternTemporalFormatter(pattern: const String&) {
        _pattern = pattern;
        _locale = Locale();
        _chronology = &GregorianChronology::instance();
        _zone = TimeZone::fixed(ZoneOffset::utc());
    }

    PatternTemporalFormatter(pattern: const String&, locale: const Locale&, chronology: Chronology*, zone: const TimeZone&) {
        _pattern = pattern;
        _locale = locale;
        _chronology = chronology;
        _zone = zone;
    }

    override formatInstant(value: const Instant&, zone: const TimeZone&) : String throws(TimeDataUnavailableException) {
        zdt : ZonedDateTime = ZonedDateTime::fromInstant(value, zone);
        return formatZonedDateTime(zdt);
    }

    override formatLocalDate(value: const LocalDate&) : String throws(TimeDataUnavailableException) {
        return formatPureK(_pattern, value.year(), value.month(), value.day(), 0, 0, 0, 0);
    }

    override formatLocalDateTime(value: const LocalDateTime&) : String throws(TimeDataUnavailableException) {
        return formatPureK(_pattern, value.year(), value.month(), value.day(),
                           value.hour(), value.minute(), value.second(), value.nano());
    }

    override formatZonedDateTime(value: const ZonedDateTime&) : String throws(TimeDataUnavailableException) {
        sec : long = value.instant().toPosixSeconds();
        resIcu : String? = formatViaIcu(sec, value.instant().nanoAdjustment());
        if (resIcu != null) return *resIcu;
        return formatLocalDateTime(value.localDateTime());
    }

    override formatCalendarDateTime(value: const CalendarDateTime&) : String throws(TimeDataUnavailableException) {
        f : CalendarFields = value.date().fields();
        return formatPureK(_pattern, f.yearOfEra(), f.month().number(), f.day(),
                           value.time().hour(), value.time().minute(), value.time().second(), value.time().nano());
    }

    override withLocale(locale: const Locale&) : TemporalFormatter! {
        return new PatternTemporalFormatter(_pattern, locale, _chronology, _zone);
    }

    override withChronology(chronology: const Chronology&) : TemporalFormatter! {
        ChronologyPtr : Chronology* = &((Chronology&) chronology);
        return new PatternTemporalFormatter(_pattern, _locale, ChronologyPtr, _zone);
    }

    override withZone(zone: const TimeZone&) : TemporalFormatter! {
        return new PatternTemporalFormatter(_pattern, _locale, _chronology, zone);
    }
}

/**
 * TemporalFormatters — factory for obtaining standard and pattern temporal formatters.
 */
public final class TemporalFormatters {
public:
    static iso() : TemporalFormatter! {
        return new IsoTemporalFormatter();
    }

    static ofPattern(pattern: const String&) : TemporalFormatter!
        throws(TemporalParseException, TimeDataUnavailableException) {
        if (pattern.empty()) {
            throw TemporalParseException(503);
        }
        return new PatternTemporalFormatter(pattern);
    }
}

} // namespace time
