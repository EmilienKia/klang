/*
 * K Language standard library — Canonical ISO Parser
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
 * Internal helper for ISO text parsing.
 */
public final class IsoReader {
public:
    static parseDigits(text: const String&, start: unsigned int, count: unsigned int) : int
        throws(TemporalParseException) {
        if (start + count > text.size()) {
            throw TemporalParseException();
        }
        val : int = 0;
        i : unsigned int = 0u;
        while (i < count) {
            c : char = text.at(start + i);
            if (c < '0' || c > '9') {
                throw TemporalParseException();
            }
            val = val * 10 + (((int) c) - ((int) '0'));
            ++i;
        }
        return val;
    }

    static parseZoneOffset(text: const String&) : ZoneOffset
        throws(TemporalParseException, InvalidTemporalValueException) {
        sz : unsigned int = text.size();
        if (sz == 0u) {
            throw TemporalParseException();
        }
        if (sz == 1u) {
            if (text.at(0u) == 'Z') {
                return ZoneOffset::utc();
            }
            throw TemporalParseException();
        }
        signChar : char = text.at(0u);
        if (signChar != '+' && signChar != '-') {
            throw TemporalParseException();
        }
        negative : bool = (signChar == '-');
        h : int = 0;
        m : int = 0;
        s : int = 0;
        if (sz == 5u) {
            // +HHMM or -HHMM
            h = parseDigits(text, 1u, 2u);
            m = parseDigits(text, 3u, 2u);
        } else if (sz == 6u) {
            // +HH:MM or -HH:MM
            if (text.at(3u) != ':') {
                throw TemporalParseException();
            }
            h = parseDigits(text, 1u, 2u);
            m = parseDigits(text, 4u, 2u);
        } else if (sz == 7u) {
            // +HHMMSS or -HHMMSS
            h = parseDigits(text, 1u, 2u);
            m = parseDigits(text, 3u, 2u);
            s = parseDigits(text, 5u, 2u);
        } else if (sz == 9u) {
            // +HH:MM:SS or -HH:MM:SS
            if (text.at(3u) != ':' || text.at(6u) != ':') {
                throw TemporalParseException();
            }
            h = parseDigits(text, 1u, 2u);
            m = parseDigits(text, 4u, 2u);
            s = parseDigits(text, 7u, 2u);
        } else {
            throw TemporalParseException();
        }

        if (h < 0 || h > 18 || m < 0 || m > 59 || s < 0 || s > 59) {
            throw InvalidTemporalValueException();
        }
        total : int = h * 3600 + m * 60 + s;
        if (negative) {
            total = -total;
        }
        if (total < -64800 || total > 64800) {
            throw InvalidTemporalValueException();
        }
        return ZoneOffset(total);
    }

    static parseLocalDate(text: const String&) : LocalDate
        throws(TemporalParseException, InvalidTemporalValueException) {
        sz : unsigned int = text.size();
        if (sz < 10u) {
            throw TemporalParseException();
        }
        idx : unsigned int = 0u;
        isNegative : bool = false;
        hasSign : bool = false;
        if (text.at(0u) == '-') {
            isNegative = true;
            hasSign = true;
            idx = 1u;
        } else if (text.at(0u) == '+') {
            hasSign = true;
            idx = 1u;
        }

        yStart : unsigned int = idx;
        while (idx < sz && text.at(idx) != '-') {
            c : char = text.at(idx);
            if (c < '0' || c > '9') {
                throw TemporalParseException();
            }
            ++idx;
        }
        if (idx == sz || text.at(idx) != '-') {
            throw TemporalParseException();
        }
        yLen : unsigned int = idx - yStart;
        if (!hasSign) {
            if (yLen != 4u) {
                throw TemporalParseException();
            }
        } else {
            if (yLen < 4u) {
                throw TemporalParseException();
            }
        }

        if (sz != idx + 6u) {
            throw TemporalParseException();
        }

        yearVal : long = 0L;
        k : unsigned int = yStart;
        while (k < idx) {
            d : int = ((int) text.at(k)) - ((int) '0');
            if (yearVal > (9223372036854775807L - (long)d) / 10L) {
                throw TemporalParseException();
            }
            yearVal = yearVal * 10L + (long) d;
            ++k;
        }
        if (isNegative) {
            yearVal = -yearVal;
        }

        mStart : unsigned int = idx + 1u;
        cM0 : char = text.at(mStart);
        cM1 : char = text.at(mStart + 1u);
        if (cM0 < '0' || cM0 > '9' || cM1 < '0' || cM1 > '9') {
            throw TemporalParseException();
        }
        m : int = (((int) cM0) - ((int) '0')) * 10 + (((int) cM1) - ((int) '0'));

        if (text.at(mStart + 2u) != '-') {
            throw TemporalParseException();
        }

        dStart : unsigned int = mStart + 3u;
        cD0 : char = text.at(dStart);
        cD1 : char = text.at(dStart + 1u);
        if (cD0 < '0' || cD0 > '9' || cD1 < '0' || cD1 > '9') {
            throw TemporalParseException();
        }
        d : int = (((int) cD0) - ((int) '0')) * 10 + (((int) cD1) - ((int) '0'));

        if (m < 1 || m > 12) {
            throw InvalidTemporalValueException();
        }
        maxDay : int = GregorianMath::lengthOfMonth(yearVal, m);
        if (d < 1 || d > maxDay) {
            throw InvalidTemporalValueException();
        }
        return LocalDate(yearVal, m, d);
    }

    static parseLocalTime(text: const String&) : LocalTime
        throws(TemporalParseException, InvalidTemporalValueException) {
        sz : unsigned int = text.size();
        if (sz < 5u) {
            throw TemporalParseException();
        }
        cH0 : char = text.at(0u);
        cH1 : char = text.at(1u);
        if (cH0 < '0' || cH0 > '9' || cH1 < '0' || cH1 > '9') {
            throw TemporalParseException();
        }
        h : int = (((int) cH0) - ((int) '0')) * 10 + (((int) cH1) - ((int) '0'));

        if (text.at(2u) != ':') {
            throw TemporalParseException();
        }
        cM0 : char = text.at(3u);
        cM1 : char = text.at(4u);
        if (cM0 < '0' || cM0 > '9' || cM1 < '0' || cM1 > '9') {
            throw TemporalParseException();
        }
        m : int = (((int) cM0) - ((int) '0')) * 10 + (((int) cM1) - ((int) '0'));

        s : int = 0;
        nanos : int = 0;
        if (sz == 5u) {
            // HH:MM
        } else {
            if (text.at(5u) != ':') {
                throw TemporalParseException();
            }
            if (sz < 8u) {
                throw TemporalParseException();
            }
            cS0 : char = text.at(6u);
            cS1 : char = text.at(7u);
            if (cS0 < '0' || cS0 > '9' || cS1 < '0' || cS1 > '9') {
                throw TemporalParseException();
            }
            s = (((int) cS0) - ((int) '0')) * 10 + (((int) cS1) - ((int) '0'));

            if (sz == 8u) {
                // HH:MM:SS
            } else {
                if (text.at(8u) != '.') {
                    throw TemporalParseException();
                }
                fracLen : unsigned int = sz - 9u;
                if (fracLen < 1u || fracLen > 9u) {
                    throw TemporalParseException();
                }
                fIdx : unsigned int = 0u;
                while (fIdx < fracLen) {
                    fc : char = text.at(9u + fIdx);
                    if (fc < '0' || fc > '9') {
                        throw TemporalParseException();
                    }
                    nanos = nanos * 10 + (((int) fc) - ((int) '0'));
                    ++fIdx;
                }
                while (fracLen < 9u) {
                    nanos = nanos * 10;
                    ++fracLen;
                }
            }
        }

        if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 60) {
            throw InvalidTemporalValueException();
        }
        if (s == 60 && (h != 23 || m != 59)) {
            throw InvalidTemporalValueException();
        }
        return LocalTime(h, m, s, nanos);
    }

    static parseLocalDateTime(text: const String&) : LocalDateTime
        throws(TemporalParseException, InvalidTemporalValueException) {
        tIdx : int = text.find('T');
        if (tIdx < 0) {
            tIdx = text.find('t');
        }
        if (tIdx < 0) {
            throw TemporalParseException();
        }
        utIdx : unsigned int = (unsigned int) tIdx;
        dateStr : String = text.substr(0u, utIdx);
        timeStr : String = text.substr(utIdx + 1u, text.size() - utIdx - 1u);
        date : LocalDate = parseLocalDate(dateStr);
        time : LocalTime = parseLocalTime(timeStr);
        return LocalDateTime(date, time);
    }

    static parseDuration(text: const String&) : Duration
        throws(TemporalParseException, TemporalArithmeticException) {
        sz : unsigned int = text.size();
        if (sz < 3u) {
            throw TemporalParseException();
        }
        idx : unsigned int = 0u;
        isNegative : bool = false;
        if (text.at(0u) == '-') {
            isNegative = true;
            idx = 1u;
        } else if (text.at(0u) == '+') {
            idx = 1u;
        }

        if (idx + 2u > sz) {
            throw TemporalParseException();
        }
        if (text.at(idx) != 'P' || text.at(idx + 1u) != 'T') {
            throw TemporalParseException();
        }
        idx += 2u;

        if (idx < sz && (text.at(idx) == '-' || text.at(idx) == '+')) {
            if (isNegative) {
                throw TemporalParseException();
            }
            if (text.at(idx) == '-') {
                isNegative = true;
            }
            idx += 1u;
        }
        if (idx >= sz) {
            throw TemporalParseException();
        }

        hasH : bool = false;
        hasM : bool = false;
        hasS : bool = false;
        totalSec : long = 0L;
        totalNanos : int = 0;

        while (idx < sz) {
            c : char = text.at(idx);
            if (c < '0' || c > '9') {
                throw TemporalParseException();
            }
            numStart : unsigned int = idx;
            while (idx < sz && text.at(idx) >= '0' && text.at(idx) <= '9') {
                ++idx;
            }
            numEnd : unsigned int = idx;

            hasFrac : bool = false;
            fracVal : int = 0;
            if (idx < sz && text.at(idx) == '.') {
                hasFrac = true;
                ++idx;
                fracStart : unsigned int = idx;
                while (idx < sz && text.at(idx) >= '0' && text.at(idx) <= '9') {
                    ++idx;
                }
                fracDigits : unsigned int = idx - fracStart;
                if (fracDigits < 1u || fracDigits > 9u) {
                    throw TemporalParseException();
                }
                f : unsigned int = 0u;
                while (f < fracDigits) {
                    fracVal = fracVal * 10 + (((int) text.at(fracStart + f)) - ((int) '0'));
                    ++f;
                }
                while (fracDigits < 9u) {
                    fracVal = fracVal * 10;
                    ++fracDigits;
                }
            }

            if (idx >= sz) {
                throw TemporalParseException();
            }
            designator : char = text.at(idx);
            ++idx;

            intVal : long = 0L;
            p : unsigned int = numStart;
            if (isNegative) {
                while (p < numEnd) {
                    d : int = ((int) text.at(p)) - ((int) '0');
                    intVal = subtractChecked(multiplyChecked(intVal, 10L), (long) d);
                    ++p;
                }
            } else {
                while (p < numEnd) {
                    d : int = ((int) text.at(p)) - ((int) '0');
                    intVal = addChecked(multiplyChecked(intVal, 10L), (long) d);
                    ++p;
                }
            }

            if (designator == 'H') {
                if (hasH || hasM || hasS || hasFrac) {
                    throw TemporalParseException();
                }
                hasH = true;
                secFromH : long = multiplyChecked(intVal, 3600L);
                totalSec = addChecked(totalSec, secFromH);
            } else if (designator == 'M') {
                if (hasM || hasS || hasFrac) {
                    throw TemporalParseException();
                }
                hasM = true;
                secFromM : long = multiplyChecked(intVal, 60L);
                totalSec = addChecked(totalSec, secFromM);
            } else if (designator == 'S') {
                if (hasS) {
                    throw TemporalParseException();
                }
                hasS = true;
                totalSec = addChecked(totalSec, intVal);
                if (isNegative) {
                    if (fracVal > 0) {
                        totalSec = subtractChecked(totalSec, 1L);
                        totalNanos = 1000000000 - fracVal;
                    }
                } else {
                    totalNanos = fracVal;
                }
            } else {
                throw TemporalParseException();
            }
        }

        if (!hasH && !hasM && !hasS) {
            throw TemporalParseException();
        }

        return Duration(totalSec, totalNanos);
    }
}

} // namespace time
