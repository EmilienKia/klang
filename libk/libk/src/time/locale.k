/*
 * K Language standard library — WeekRules and Locale
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
 * WeekRules — definitions of week-based calendar rules (first day of week and minimal days in first week).
 */
public final struct WeekRules {
private:
    _firstDayOfWeek         : int;
    _minimalDaysInFirstWeek : int;

public:
    WeekRules() {
        _firstDayOfWeek = 1;
        _minimalDaysInFirstWeek = 4;
    }

    WeekRules(firstDayOfWeek: int, minimalDaysInFirstWeek: int) {
        _firstDayOfWeek = firstDayOfWeek;
        _minimalDaysInFirstWeek = minimalDaysInFirstWeek;
    }

    static iso() : WeekRules {
        return WeekRules(1, 4);
    }

    static of(firstDayOfWeek: int, minimalDaysInFirstWeek: int) : WeekRules
        throws(InvalidTemporalValueException) {
        if (firstDayOfWeek < 1 || firstDayOfWeek > 7) {
            throw InvalidTemporalValueException(501);
        }
        if (minimalDaysInFirstWeek < 1 || minimalDaysInFirstWeek > 7) {
            throw InvalidTemporalValueException(501);
        }
        return WeekRules(firstDayOfWeek, minimalDaysInFirstWeek);
    }

    const firstDayOfWeek() : int {
        return _firstDayOfWeek;
    }

    const minimalDaysInFirstWeek() : int {
        return _minimalDaysInFirstWeek;
    }

    const operator==(other: const WeekRules&) : bool {
        return _firstDayOfWeek == other._firstDayOfWeek && _minimalDaysInFirstWeek == other._minimalDaysInFirstWeek;
    }

    const operator!=(other: const WeekRules&) : bool {
        return !(this == other);
    }
}

/**
 * Locale — immutable presentation-oriented cultural specification (BCP 47 language tag,
 * optional Chronology preference, and WeekRules).
 */
public final class Locale {
private:
    _languageTag   : String;
    _weekRules     : WeekRules;
    _hasChronology : bool;
    _chronology    : Chronology?;

    Locale(languageTag: const String&, weekRules: const WeekRules&, hasChronology: bool, chronology: Chronology?)
        : _languageTag(languageTag), _weekRules(weekRules), _hasChronology(hasChronology), _chronology(chronology) {}

public:
    Locale()
        : _languageTag(String("und")), _weekRules(WeekRules::iso()), _hasChronology(false), _chronology(null) {}

    Locale(languageTag: const String&)
        : _languageTag(languageTag), _weekRules(WeekRules::iso()), _hasChronology(false), _chronology(null) {}

    Locale(other: const Locale&)
        : _languageTag(other._languageTag), _weekRules(other._weekRules), _hasChronology(other._hasChronology), _chronology(other._chronology) {}

    // ── Factories ────────────────────────────────────────────────────────────

    static of(languageTag: const String&) : Locale
        throws(InvalidTemporalValueException, TimeDataUnavailableException) {
        if (languageTag.empty()) {
            throw InvalidTemporalValueException(501);
        }

        sz : unsigned int = languageTag.size();
        if (languageTag.at(0u) == '-' || languageTag.at(sz - 1u) == '-') {
            throw InvalidTemporalValueException(501);
        }

        i : unsigned int = 0u;
        prevHyphen : bool = false;
        while (i < sz) {
            c : char = languageTag.at(i);
            isAlpha : bool = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            isDigit : bool = (c >= '0' && c <= '9');
            if (c == '-' || c == '_') {
                if (prevHyphen) {
                    throw InvalidTemporalValueException(501);
                }
                prevHyphen = true;
            } else if (isAlpha || isDigit) {
                prevHyphen = false;
            } else {
                throw InvalidTemporalValueException(501);
            }
            ++i;
        }

        if (__k_icu_is_available() != 0) {
            u8 : unsigned byte[]! = languageTag.toUtf8();
            if (__k_icu_is_locale_valid((const byte*) &u8[0]) == 0) {
                throw InvalidTemporalValueException(501);
            }
        }

        return Locale(languageTag);
    }

    static system() : Locale throws(TimeDataUnavailableException) {
        if (__k_icu_is_available() == 0) {
            throw TimeDataUnavailableException(510);
        }
        bytes : unsigned byte[]! = __k_icu_get_system_locale();
        if (bytes == null || bytes.size <= 1u) {
            throw TimeDataUnavailableException(510);
        }
        try {
            return Locale::of(String(bytes));
        } catch (e: InvalidTemporalValueException&) {
            throw TimeDataUnavailableException(510);
        }
    }

    // ── Accessors & Mutators ─────────────────────────────────────────────────

    const languageTag() : String {
        return _languageTag;
    }

    const weekRules() : WeekRules {
        return _weekRules;
    }

    const chronology() : Chronology? {
        if (!_hasChronology) {
            return null;
        }
        return _chronology;
    }

    /**
     * Return a new Locale with the given Chronology configuration.
     */
    const withChronology(chronology: const Chronology&) : Locale {
        chronRef : Chronology& = (Chronology&) chronology;
        chronView : Chronology? = &chronRef;
        return Locale(_languageTag, _weekRules, true, chronView);
    }

    /**
     * Return a new Locale with the given WeekRules configuration.
     */
    const withWeekRules(rules: const WeekRules&) : Locale {
        return Locale(_languageTag, rules, _hasChronology, _chronology);
    }

    // ── Comparisons ──────────────────────────────────────────────────────────

    const operator==(other: const Locale&) : bool {
        if (_languageTag != other._languageTag) return false;
        if (_weekRules != other._weekRules) return false;
        if (_hasChronology != other._hasChronology) return false;
        if (_hasChronology) {
            c1 : Chronology? = _chronology;
            c2 : Chronology? = other._chronology;
            if (c1 != null && c2 != null) {
                if (c1->id() != c2->id()) return false;
            } else if (c1 != c2) {
                return false;
            }
        }
        return true;
    }

    const operator!=(other: const Locale&) : bool {
        return !(this == other);
    }
}

} // namespace time
