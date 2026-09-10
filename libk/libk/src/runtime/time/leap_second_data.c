/*
 * K Language runtime — Leap second data implementation
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

#include "leap_second_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define NTP_OFFSET 2208988800LL
#define MAX_LEAP_RECORDS 256

/*
 * Pinned historical table of the 27 leap seconds inserted between 1972 and 2017.
 * Each entry records the effective boundary (00:00:00 of the following day),
 * the new TAI - UTC difference, and the civil date of the leap second itself.
 */
static const KLeapSecondRecord PINNED_RECORDS[] = {
    { 2287785600LL,  78796800LL, 11, 1, 1972,  6, 30 },
    { 2303683200LL,  94694400LL, 12, 1, 1972, 12, 31 },
    { 2335219200LL, 126230400LL, 13, 1, 1973, 12, 31 },
    { 2366755200LL, 157766400LL, 14, 1, 1974, 12, 31 },
    { 2398291200LL, 189302400LL, 15, 1, 1975, 12, 31 },
    { 2429913600LL, 220924800LL, 16, 1, 1976, 12, 31 },
    { 2461449600LL, 252460800LL, 17, 1, 1977, 12, 31 },
    { 2492985600LL, 283996800LL, 18, 1, 1978, 12, 31 },
    { 2524521600LL, 315532800LL, 19, 1, 1979, 12, 31 },
    { 2571782400LL, 362793600LL, 20, 1, 1981,  6, 30 },
    { 2603318400LL, 394329600LL, 21, 1, 1982,  6, 30 },
    { 2634854400LL, 425865600LL, 22, 1, 1983,  6, 30 },
    { 2698012800LL, 489024000LL, 23, 1, 1985,  6, 30 },
    { 2776982400LL, 567993600LL, 24, 1, 1987, 12, 31 },
    { 2840140800LL, 631152000LL, 25, 1, 1989, 12, 31 },
    { 2871676800LL, 662688000LL, 26, 1, 1990, 12, 31 },
    { 2918937600LL, 709948800LL, 27, 1, 1992,  6, 30 },
    { 2950473600LL, 741484800LL, 28, 1, 1993,  6, 30 },
    { 2982009600LL, 773020800LL, 29, 1, 1994,  6, 30 },
    { 3029443200LL, 820454400LL, 30, 1, 1995, 12, 31 },
    { 3076704000LL, 867715200LL, 31, 1, 1997,  6, 30 },
    { 3124137600LL, 915148800LL, 32, 1, 1998, 12, 31 },
    { 3345062400LL, 1136073600LL, 33, 1, 2005, 12, 31 },
    { 3439756800LL, 1230768000LL, 34, 1, 2008, 12, 31 },
    { 3550089600LL, 1341100800LL, 35, 1, 2012,  6, 30 },
    { 3644697600LL, 1435708800LL, 36, 1, 2015,  6, 30 },
    { 3692217600LL, 1483228800LL, 37, 1, 2016, 12, 31 }
};

#define PINNED_COUNT ((int)(sizeof(PINNED_RECORDS) / sizeof(PINNED_RECORDS[0])))

static KLeapSecondRecord g_active_records[MAX_LEAP_RECORDS];
static int g_active_count = 0;

static void init_pinned_if_needed(void) {
    if (g_active_count == 0) {
        memcpy(g_active_records, PINNED_RECORDS, sizeof(PINNED_RECORDS));
        g_active_count = PINNED_COUNT;
    }
}

void k_leap_second_reset_pinned(void) {
    memcpy(g_active_records, PINNED_RECORDS, sizeof(PINNED_RECORDS));
    g_active_count = PINNED_COUNT;
}

static void civil_from_posix_seconds(int64_t s, int32_t* out_year, int32_t* out_month, int32_t* out_day) {
    int64_t days = (s >= 0) ? (s / 86400LL) : ((s - 86399LL) / 86400LL);
    days += 719468LL;
    int64_t era = (days >= 0 ? days : days - 146096LL) / 146097LL;
    unsigned doe = (unsigned)(days - era * 146097LL);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = (int64_t)yoe + era * 400LL;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned d = doy - (153 * mp + 2) / 5 + 1;
    unsigned m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) y++;
    *out_year = (int32_t)y;
    *out_month = (int32_t)m;
    *out_day = (int32_t)d;
}

int k_leap_second_load(const char* file_path) {
    if (!file_path || !*file_path) {
        k_leap_second_reset_pinned();
        return 0;
    }

    FILE* fp = fopen(file_path, "r");
    if (!fp) {
        k_leap_second_reset_pinned();
        return -1;
    }

    KLeapSecondRecord parsed[MAX_LEAP_RECORDS];
    int count = 0;
    char line[512];
    int64_t prev_tai = 10;

    while (fgets(line, sizeof(line), fp)) {
        char* p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '#' || *p == '\0') {
            continue;
        }
        if (isdigit((unsigned char)*p)) {
            char* endptr = NULL;
            int64_t ntp_sec = strtoll(p, &endptr, 10);
            if (endptr == p) continue;
            while (*endptr && isspace((unsigned char)*endptr)) endptr++;
            char* endptr2 = NULL;
            int64_t tai_off = strtoll(endptr, &endptr2, 10);
            if (endptr2 == endptr) continue;

            if (count < MAX_LEAP_RECORDS) {
                int64_t posix_sec = ntp_sec - NTP_OFFSET;
                int32_t y = 0, m = 0, d = 0;
                civil_from_posix_seconds(posix_sec - 1LL, &y, &m, &d);
                parsed[count].ntp_seconds = ntp_sec;
                parsed[count].posix_seconds = posix_sec;
                parsed[count].tai_offset = tai_off;
                parsed[count].correction = (int32_t)(tai_off - prev_tai);
                parsed[count].year = y;
                parsed[count].month = m;
                parsed[count].day = d;
                prev_tai = tai_off;
                count++;
            }
        }
    }
    fclose(fp);

    if (count > 0) {
        memcpy(g_active_records, parsed, sizeof(KLeapSecondRecord) * (size_t)count);
        g_active_count = count;
        return 0;
    }

    k_leap_second_reset_pinned();
    return -1;
}

int k_leap_second_count(void) {
    init_pinned_if_needed();
    return g_active_count;
}

int k_leap_second_is_leap_date(int64_t year, int32_t month, int32_t day) {
    init_pinned_if_needed();
    for (int i = 0; i < g_active_count; ++i) {
        if (g_active_records[i].year == year &&
            g_active_records[i].month == month &&
            g_active_records[i].day == day) {
            return 1;
        }
    }
    return 0;
}

int k_leap_second_is_leap_second(int64_t epoch_second) {
    init_pinned_if_needed();
    for (int i = 0; i < g_active_count; ++i) {
        const KLeapSecondRecord* r = &g_active_records[i];
        /* Check POSIX second (23:59:59/60 boundary) */
        if (epoch_second == r->posix_seconds - 1LL || epoch_second == r->posix_seconds) {
            return 1;
        }
        /* Check NTP second */
        if (epoch_second == r->ntp_seconds - 1LL || epoch_second == r->ntp_seconds) {
            return 1;
        }
        /* Check continuous timeline second (Instant coordinate: POSIX + leap seconds accumulated) */
        int64_t continuous_sec = r->posix_seconds - 1LL + (r->tai_offset - 10LL);
        if (epoch_second == continuous_sec) {
            return 1;
        }
    }
    return 0;
}

int64_t k_leap_second_tai_offset(int64_t epoch_second) {
    init_pinned_if_needed();
    int64_t posix_sec = epoch_second;
    if (epoch_second >= 2000000000LL) {
        posix_sec = epoch_second - NTP_OFFSET;
    }

    if (g_active_count == 0) return 10;
    if (posix_sec < g_active_records[0].posix_seconds) {
        return 10;
    }

    int best = 0;
    for (int i = 0; i < g_active_count; ++i) {
        if (g_active_records[i].posix_seconds <= posix_sec) {
            best = i;
        } else {
            break;
        }
    }
    return g_active_records[best].tai_offset;
}

int64_t k_leap_second_posix_to_continuous(int64_t posix_sec) {
    init_pinned_if_needed();
    int count = 0;
    for (int i = 0; i < g_active_count; ++i) {
        if (posix_sec >= g_active_records[i].posix_seconds) {
            count++;
        }
    }
    return posix_sec + (int64_t) count;
}

int64_t k_leap_second_continuous_to_posix(int64_t continuous_sec, int32_t policy, int32_t* out_error) {
    init_pinned_if_needed();
    if (out_error) *out_error = 0;

    for (int i = 0; i < g_active_count; ++i) {
        const KLeapSecondRecord* r = &g_active_records[i];
        int64_t leap_cont_sec = r->posix_seconds - 1LL + (r->tai_offset - 10LL);
        if (continuous_sec == leap_cont_sec) {
            if (policy == 0) {
                /* reject */
                if (out_error) *out_error = 504;
                return 0;
            } else if (policy == 1) {
                /* fold to previous second (23:59:59) */
                return r->posix_seconds - 1LL;
            } else if (policy == 2) {
                /* fold to following second (00:00:00) */
                return r->posix_seconds;
            }
        }
    }

    int count = 0;
    for (int i = 0; i < g_active_count; ++i) {
        const KLeapSecondRecord* r = &g_active_records[i];
        int64_t leap_cont_sec = r->posix_seconds - 1LL + (r->tai_offset - 10LL);
        if (continuous_sec > leap_cont_sec) {
            count++;
        }
    }
    return continuous_sec - (int64_t) count;
}

int32_t k_leap_second_get_leap_date_for_instant(int64_t continuous_sec, int64_t* out_year, int32_t* out_month, int32_t* out_day) {
    init_pinned_if_needed();
    for (int i = 0; i < g_active_count; ++i) {
        const KLeapSecondRecord* r = &g_active_records[i];
        int64_t leap_cont_sec = r->posix_seconds - 1LL + (r->tai_offset - 10LL);
        if (continuous_sec == leap_cont_sec) {
            if (out_year) *out_year = (int64_t) r->year;
            if (out_month) *out_month = r->month;
            if (out_day) *out_day = r->day;
            return 1;
        }
    }
    return 0;
}

