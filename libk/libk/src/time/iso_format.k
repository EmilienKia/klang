/*
 * K Language standard library — Canonical ISO Formatter
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
 * Internal helper for ISO text formatting.
 */
public final class IsoWriter {
public:
    static appendDigits(sb: StringBuilder&, value: long, minDigits: int) {
        if (value == 0L && minDigits <= 1) {
            sb.appendChar('0');
            return;
        }

        digits : char[32];
        count : int = 0;
        cur : long = value;
        while (cur > 0L || count < minDigits) {
            rem : int = (int) (cur % 10L);
            cur = cur / 10L;
            digits[count] = (char) ((int)'0' + rem);
            count += 1;
        }

        if (count == 0) {
            sb.appendChar('0');
            return;
        }

        i : int = count;
        while (i > 0) {
            i -= 1;
            sb.appendChar(digits[i]);
        }
    }

    static appendTwoDigits(sb: StringBuilder&, value: int) {
        sb.appendChar((char) ((int)'0' + ((value / 10) % 10)));
        sb.appendChar((char) ((int)'0' + (value % 10)));
    }

    static appendYear(sb: StringBuilder&, year: long) {
        if (year >= 0L && year <= 9999L) {
            appendDigits(sb, year, 4);
        } else if (year > 9999L) {
            sb.appendChar('+');
            appendDigits(sb, year, 4);
        } else {
            sb.appendChar('-');
            if (year == (-9223372036854775807L - 1L)) {
                sb.append("9223372036854775808");
            } else {
                appendDigits(sb, -year, 4);
            }
        }
    }

    static appendFraction(sb: StringBuilder&, nanos: int) {
        if (nanos <= 0) {
            return;
        }
        digits : char[9];
        cur : int = nanos;
        idx : int = 8;
        while (idx >= 0) {
            digits[idx] = (char) ((int)'0' + (cur % 10));
            cur = cur / 10;
            idx -= 1;
        }
        lastNonZero : int = 8;
        while (lastNonZero >= 0 && digits[lastNonZero] == '0') {
            lastNonZero -= 1;
        }
        sb.appendChar('.');
        i : int = 0;
        while (i <= lastNonZero) {
            sb.appendChar(digits[i]);
            i += 1;
        }
    }

    static formatDuration(value: const Duration&) : String {
        sec : long = value.secondsPart();
        nanos : int = value.nanoAdjustment();
        if (sec == 0L && nanos == 0) {
            return String("PT0S");
        }
        sb : StringBuilder;
        sb.append("PT");
        if (sec < 0L) {
            sb.appendChar('-');
            if (nanos == 0) {
                if (sec == (-9223372036854775807L - 1L)) {
                    sb.append("9223372036854775808");
                } else {
                    appendDigits(sb, -sec, 1);
                }
            } else {
                magSec : long = -(sec + 1L);
                magNanos : int = 1000000000 - nanos;
                appendDigits(sb, magSec, 1);
                appendFraction(sb, magNanos);
            }
        } else {
            appendDigits(sb, sec, 1);
            if (nanos > 0) {
                appendFraction(sb, nanos);
            }
        }
        sb.appendChar('S');
        return sb.toString();
    }

    static formatLocalDate(value: const LocalDate&) : String {
        sb : StringBuilder;
        appendYear(sb, value.year());
        sb.appendChar('-');
        appendTwoDigits(sb, value.month());
        sb.appendChar('-');
        appendTwoDigits(sb, value.day());
        return sb.toString();
    }

    static formatLocalTime(value: const LocalTime&) : String {
        sb : StringBuilder;
        appendTwoDigits(sb, value.hour());
        sb.appendChar(':');
        appendTwoDigits(sb, value.minute());
        sb.appendChar(':');
        appendTwoDigits(sb, value.second());
        if (value.nano() > 0) {
            appendFraction(sb, value.nano());
        }
        return sb.toString();
    }

    static formatLocalDateTime(value: const LocalDateTime&) : String {
        sb : StringBuilder;
        sb.append(formatLocalDate(value.date()));
        sb.appendChar('T');
        sb.append(formatLocalTime(value.time()));
        return sb.toString();
    }

    static formatZoneOffset(value: const ZoneOffset&) : String {
        sec : int = value.totalSeconds();
        if (sec == 0) {
            return String("Z");
        }
        sb : StringBuilder;
        if (sec < 0) {
            sb.appendChar('-');
            sec = -sec;
        } else {
            sb.appendChar('+');
        }
        h : int = sec / 3600;
        rem : int = sec % 3600;
        m : int = rem / 60;
        s : int = rem % 60;
        appendTwoDigits(sb, h);
        sb.appendChar(':');
        appendTwoDigits(sb, m);
        if (s != 0) {
            sb.appendChar(':');
            appendTwoDigits(sb, s);
        }
        return sb.toString();
    }
}

/**
 * Iso — canonical ISO-8601 text formatting and parsing for temporal types.
 */
public final class Iso {
public:
    static formatDuration(value: const Duration&) : String {
        return IsoWriter::formatDuration(value);
    }

    static formatLocalDate(value: const LocalDate&) : String {
        return IsoWriter::formatLocalDate(value);
    }

    static formatLocalTime(value: const LocalTime&) : String {
        return IsoWriter::formatLocalTime(value);
    }

    static formatLocalDateTime(value: const LocalDateTime&) : String {
        return IsoWriter::formatLocalDateTime(value);
    }

    static formatZoneOffset(value: const ZoneOffset&) : String {
        return IsoWriter::formatZoneOffset(value);
    }

    static parseDuration(text: const String&) : Duration
        throws(TemporalParseException, TemporalArithmeticException) {
        return IsoReader::parseDuration(text);
    }

    static parseDuration(text: const char[]) : Duration
        throws(TemporalParseException, TemporalArithmeticException) {
        return parseDuration(String(text));
    }

    static parseLocalDate(text: const String&) : LocalDate
        throws(TemporalParseException, InvalidTemporalValueException) {
        return IsoReader::parseLocalDate(text);
    }

    static parseLocalDate(text: const char[]) : LocalDate
        throws(TemporalParseException, InvalidTemporalValueException) {
        return parseLocalDate(String(text));
    }

    static parseLocalTime(text: const String&) : LocalTime
        throws(TemporalParseException, InvalidTemporalValueException) {
        return IsoReader::parseLocalTime(text);
    }

    static parseLocalTime(text: const char[]) : LocalTime
        throws(TemporalParseException, InvalidTemporalValueException) {
        return parseLocalTime(String(text));
    }

    static parseLocalDateTime(text: const String&) : LocalDateTime
        throws(TemporalParseException, InvalidTemporalValueException) {
        return IsoReader::parseLocalDateTime(text);
    }

    static parseLocalDateTime(text: const char[]) : LocalDateTime
        throws(TemporalParseException, InvalidTemporalValueException) {
        return parseLocalDateTime(String(text));
    }

    static parseZoneOffset(text: const String&) : ZoneOffset
        throws(TemporalParseException, InvalidTemporalValueException) {
        return IsoReader::parseZoneOffset(text);
    }

    static parseZoneOffset(text: const char[]) : ZoneOffset
        throws(TemporalParseException, InvalidTemporalValueException) {
        return parseZoneOffset(String(text));
    }
}

} // namespace time
