/*
 * K Language runtime — Leap second data header
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

#ifndef K_RUNTIME_TIME_LEAP_SECOND_DATA_H
#define K_RUNTIME_TIME_LEAP_SECOND_DATA_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t ntp_seconds;     /* NTP seconds (since 1900-01-01) */
    int64_t posix_seconds;   /* POSIX seconds of transition (since 1970-01-01) */
    int64_t tai_offset;      /* TAI - UTC difference in seconds */
    int32_t correction;      /* +1 (or -1) */
    int32_t year;
    int32_t month;
    int32_t day;
} KLeapSecondRecord;

/**
 * Load leap seconds from a file in NIST/IANA leapseconds format.
 * If file_path is NULL or file cannot be opened/parsed, resets to the
 * built-in historical table.
 * Returns 0 on success, non-zero on error.
 */
int k_leap_second_load(const char* file_path);

/**
 * Check if the given epoch second corresponds to a leap second event.
 * Accepts POSIX epoch seconds (around transition boundary), NTP epoch seconds,
 * or continuous timeline seconds.
 * Returns 1 if yes, 0 if no.
 */
int k_leap_second_is_leap_second(int64_t epoch_second);

/**
 * Return TAI - UTC offset in seconds at epoch_second.
 */
int64_t k_leap_second_tai_offset(int64_t epoch_second);

/**
 * Return the number of loaded/pinned leap second records.
 */
int k_leap_second_count(void);

/**
 * Check if year, month, day is a valid leap second date (e.g. 2016-12-31).
 */
int k_leap_second_is_leap_date(int64_t year, int32_t month, int32_t day);

/**
 * Convert POSIX seconds to continuous K timeline seconds.
 */
int64_t k_leap_second_posix_to_continuous(int64_t posix_sec);

/**
 * Convert continuous K timeline seconds to POSIX seconds under policy:
 *   0 = reject (returns 0 and sets *out_error = 504 on leap second)
 *   1 = fold to previous second (23:59:59)
 *   2 = fold to following second (00:00:00)
 */
int64_t k_leap_second_continuous_to_posix(int64_t continuous_sec, int32_t policy, int32_t* out_error);

/**
 * If continuous_sec is an inserted leap second, writes year, month, day and returns 1.
 * Otherwise returns 0.
 */
int32_t k_leap_second_get_leap_date_for_instant(int64_t continuous_sec, int64_t* out_year, int32_t* out_month, int32_t* out_day);

/**
 * Reset leap seconds to the default pinned historical table.
 */
void k_leap_second_reset_pinned(void);

#ifdef __cplusplus
}
#endif

#endif /* K_RUNTIME_TIME_LEAP_SECOND_DATA_H */
