/*
 * K Language standard library — temporal exceptions
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
 * Base checked exception for all time-related errors.
 */
public class TemporalException : public Exception {
public:
    TemporalException() : Exception(500) {}
    TemporalException(code: int) : Exception(code) {}
    TemporalException(code: int, cause: Throwable?) : Exception(code, cause) {}
}

/**
 * Thrown when a temporal value or argument is outside its valid domain.
 */
public class InvalidTemporalValueException : public TemporalException {
public:
    InvalidTemporalValueException() : TemporalException(501) {}
    InvalidTemporalValueException(code: int) : TemporalException(code) {}
    InvalidTemporalValueException(code: int, cause: Throwable?) : TemporalException(code, cause) {}
}

/**
 * Thrown when checked temporal arithmetic encounters an overflow, underflow,
 * or division by zero.
 */
public class TemporalArithmeticException : public TemporalException {
public:
    TemporalArithmeticException() : TemporalException(502) {}
    TemporalArithmeticException(code: int) : TemporalException(code) {}
    TemporalArithmeticException(code: int, cause: Throwable?) : TemporalException(code, cause) {}
}

/**
 * Thrown when parsing a temporal text representation fails.
 */
public class TemporalParseException : public TemporalException {
public:
    TemporalParseException() : TemporalException(503) {}
    TemporalParseException(code: int) : TemporalException(code) {}
    TemporalParseException(code: int, cause: Throwable?) : TemporalException(code, cause) {}
}

/**
 * Thrown when a leap second operation fails or encounters an invalid leap second.
 */
public class LeapSecondException : public TemporalException {
public:
    LeapSecondException() : TemporalException(504) {}
    LeapSecondException(code: int) : TemporalException(code) {}
    LeapSecondException(code: int, cause: Throwable?) : TemporalException(code, cause) {}
}

/**
 * Thrown when time data (rules, zones, scales) is unavailable.
 */
public class TimeDataUnavailableException : public TemporalException {
public:
    TimeDataUnavailableException() : TemporalException(510) {}
    TimeDataUnavailableException(code: int) : TemporalException(code) {}
    TimeDataUnavailableException(code: int, cause: Throwable?) : TemporalException(code, cause) {}
}

/**
 * Thrown when time zone rules cannot be found or loaded.
 */
public class ZoneRulesUnavailableException : public TimeDataUnavailableException {
public:
    ZoneRulesUnavailableException() : TimeDataUnavailableException(511) {}
    ZoneRulesUnavailableException(code: int) : TimeDataUnavailableException(code) {}
    ZoneRulesUnavailableException(code: int, cause: Throwable?) : TimeDataUnavailableException(code, cause) {}
}

/**
 * Thrown when the system default time zone cannot be determined.
 */
public class SystemTimeZoneUnavailableException : public TimeDataUnavailableException {
public:
    SystemTimeZoneUnavailableException() : TimeDataUnavailableException(512) {}
    SystemTimeZoneUnavailableException(code: int) : TimeDataUnavailableException(code) {}
    SystemTimeZoneUnavailableException(code: int, cause: Throwable?) : TimeDataUnavailableException(code, cause) {}
}

/**
 * Thrown when time scale data is unavailable.
 */
public class TimeScaleDataUnavailableException : public TimeDataUnavailableException {
public:
    TimeScaleDataUnavailableException() : TimeDataUnavailableException(513) {}
    TimeScaleDataUnavailableException(code: int) : TimeDataUnavailableException(code) {}
    TimeScaleDataUnavailableException(code: int, cause: Throwable?) : TimeDataUnavailableException(code, cause) {}
}

/**
 * Base exception for local time resolution conflicts (gaps and overlaps).
 */
public class LocalTimeResolutionException : public TemporalException {
public:
    LocalTimeResolutionException() : TemporalException(520) {}
    LocalTimeResolutionException(code: int) : TemporalException(code) {}
    LocalTimeResolutionException(code: int, cause: Throwable?) : TemporalException(code, cause) {}
}

/**
 * Thrown when a local time falls into a transition gap (spring forward).
 */
public class LocalTimeGapException : public LocalTimeResolutionException {
public:
    LocalTimeGapException() : LocalTimeResolutionException(521) {}
    LocalTimeGapException(code: int) : LocalTimeResolutionException(code) {}
    LocalTimeGapException(code: int, cause: Throwable?) : LocalTimeResolutionException(code, cause) {}
}

/**
 * Thrown when a local time is ambiguous due to an overlap (fall back).
 */
public class LocalTimeOverlapException : public LocalTimeResolutionException {
public:
    LocalTimeOverlapException() : LocalTimeResolutionException(522) {}
    LocalTimeOverlapException(code: int) : LocalTimeResolutionException(code) {}
    LocalTimeOverlapException(code: int, cause: Throwable?) : LocalTimeResolutionException(code, cause) {}
}

} // namespace time
