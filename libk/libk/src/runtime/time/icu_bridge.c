/*
 * K Language runtime — ICU bridge implementation (C)
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

#include "icu_bridge.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

typedef struct {
    int32_t status;
    int32_t era;
    int64_t year;
    int32_t month;
    int32_t day;
    int32_t is_leap;
    int64_t epoch_day;
    int32_t days_in_month;
    int32_t months_in_year;
    int32_t is_leap_year;
    int64_t epoch_sec;
    int32_t nanos;
} KIcuLastResult;

static __thread KIcuLastResult g_last_icu_res;

static KIcuByteArray* make_icu_byte_array(const char* s) {
    size_t len = s ? strlen(s) : 0;
    KIcuByteArray* arr = (KIcuByteArray*) malloc(sizeof(uint32_t) + len + 1);
    if (!arr) abort();
    arr->size = (uint32_t) (len + 1);
    if (len > 0) {
        memcpy(arr->data, s, len);
    }
    arr->data[len] = '\0';
    return arr;
}

int32_t __k_icu_res_status(void)         { return g_last_icu_res.status; }
int32_t __k_icu_res_era(void)            { return g_last_icu_res.era; }
int64_t __k_icu_res_year(void)           { return g_last_icu_res.year; }
int32_t __k_icu_res_month(void)          { return g_last_icu_res.month; }
int32_t __k_icu_res_day(void)            { return g_last_icu_res.day; }
int32_t __k_icu_res_is_leap(void)        { return g_last_icu_res.is_leap; }
int64_t __k_icu_res_epoch_day(void)      { return g_last_icu_res.epoch_day; }
int32_t __k_icu_res_days_in_month(void)  { return g_last_icu_res.days_in_month; }
int32_t __k_icu_res_months_in_year(void) { return g_last_icu_res.months_in_year; }
int32_t __k_icu_res_is_leap_year(void)   { return g_last_icu_res.is_leap_year; }
int64_t __k_icu_res_epoch_sec(void)      { return g_last_icu_res.epoch_sec; }
int32_t __k_icu_res_nanos(void)          { return g_last_icu_res.nanos; }

#if defined(K_TIME_HAVE_ICU) && K_TIME_HAVE_ICU

#include <unicode/utypes.h>
#include <unicode/ucal.h>
#include <unicode/udat.h>
#include <unicode/uloc.h>
#include <unicode/ustring.h>

int32_t __k_icu_is_available(void) {
    return 1;
}

static void canonicalize_cal_name(const char* in, char* out, size_t out_len) {
    if (!in || !*in) {
        snprintf(out, out_len, "gregorian");
        return;
    }
    if (strcasecmp(in, "Japanese") == 0)      snprintf(out, out_len, "japanese");
    else if (strcasecmp(in, "Buddhist") == 0) snprintf(out, out_len, "buddhist");
    else if (strcasecmp(in, "Hebrew") == 0)   snprintf(out, out_len, "hebrew");
    else if (strcasecmp(in, "Islamic") == 0)  snprintf(out, out_len, "islamic");
    else if (strcasecmp(in, "Gregorian") == 0)snprintf(out, out_len, "gregorian");
    else if (strcasecmp(in, "ISO") == 0 || strcasecmp(in, "Iso") == 0) snprintf(out, out_len, "iso8601");
    else if (strcasecmp(in, "Julian") == 0)   snprintf(out, out_len, "julian");
    else {
        size_t i = 0;
        while (in[i] && i + 1 < out_len) {
            out[i] = (char) tolower((unsigned char) in[i]);
            ++i;
        }
        out[i] = '\0';
    }
}

static UCalendar* open_calendar(const char* cal_name) {
    char canon[64];
    canonicalize_cal_name(cal_name, canon, sizeof(canon));

    char loc[128];
    snprintf(loc, sizeof(loc), "en_US@calendar=%s", canon);

    UChar tz_utc[] = {'U', 'T', 'C', 0};
    UErrorCode status = U_ZERO_ERROR;
    UCalendar* cal = ucal_open(tz_utc, 3, loc, UCAL_DEFAULT, &status);
    if (!U_SUCCESS(status)) {
        return NULL;
    }
    return cal;
}

int32_t __k_icu_calendar_from_epoch_day(const char* cal_name, int64_t epoch_day) {
    UCalendar* cal = open_calendar(cal_name);
    if (!cal) {
        g_last_icu_res.status = -1;
        return -1;
    }

    UErrorCode status = U_ZERO_ERROR;
    UDate udate = ((double) epoch_day) * 86400000.0 + 43200000.0;
    ucal_setMillis(cal, udate, &status);
    if (!U_SUCCESS(status)) {
        ucal_close(cal);
        g_last_icu_res.status = -1;
        return -1;
    }

    g_last_icu_res.status = 0;
    g_last_icu_res.era = ucal_get(cal, UCAL_ERA, &status);
    g_last_icu_res.year = ucal_get(cal, UCAL_YEAR, &status);
    g_last_icu_res.month = ucal_get(cal, UCAL_MONTH, &status) + 1; // 1-based
    g_last_icu_res.day = ucal_get(cal, UCAL_DATE, &status);
    g_last_icu_res.is_leap = ucal_get(cal, UCAL_IS_LEAP_MONTH, &status) != 0 ? 1 : 0;
    g_last_icu_res.epoch_day = epoch_day;

    ucal_close(cal);
    return 0;
}

int32_t __k_icu_calendar_to_epoch_day(const char* cal_name, int32_t era, int64_t year, int32_t month, int32_t day, int32_t is_leap) {
    UCalendar* cal = open_calendar(cal_name);
    if (!cal) {
        g_last_icu_res.status = -1;
        return -1;
    }

    UErrorCode status = U_ZERO_ERROR;
    ucal_clear(cal);
    if (era >= 0) {
        ucal_set(cal, UCAL_ERA, era);
    }
    ucal_set(cal, UCAL_YEAR, (int32_t) year);
    ucal_set(cal, UCAL_MONTH, month - 1);
    if (is_leap) {
        ucal_set(cal, UCAL_IS_LEAP_MONTH, 1);
    }
    ucal_set(cal, UCAL_DATE, day);
    ucal_set(cal, UCAL_HOUR_OF_DAY, 12);

    UDate ms = ucal_getMillis(cal, &status);
    if (!U_SUCCESS(status)) {
        ucal_close(cal);
        g_last_icu_res.status = -1;
        return -1;
    }

    int64_t ed = ms >= 0.0 ? (int64_t) ((ms + 43200000.0) / 86400000.0)
                           : (int64_t) ((ms - 43200000.0) / 86400000.0);
    g_last_icu_res.status = 0;
    g_last_icu_res.epoch_day = ed;

    ucal_close(cal);
    return 0;
}

int32_t __k_icu_calendar_days_in_month(const char* cal_name, int32_t era, int64_t year, int32_t month, int32_t is_leap) {
    UCalendar* cal = open_calendar(cal_name);
    if (!cal) {
        g_last_icu_res.status = -1;
        return -1;
    }

    UErrorCode status = U_ZERO_ERROR;
    ucal_clear(cal);
    if (era >= 0) {
        ucal_set(cal, UCAL_ERA, era);
    }
    ucal_set(cal, UCAL_YEAR, (int32_t) year);
    ucal_set(cal, UCAL_MONTH, month - 1);
    if (is_leap) {
        ucal_set(cal, UCAL_IS_LEAP_MONTH, 1);
    }
    ucal_set(cal, UCAL_DATE, 1);

    int32_t max_days = ucal_getLimit(cal, UCAL_DATE, UCAL_ACTUAL_MAXIMUM, &status);
    ucal_close(cal);
    if (!U_SUCCESS(status) || max_days <= 0) {
        g_last_icu_res.status = -1;
        return -1;
    }
    g_last_icu_res.status = 0;
    g_last_icu_res.days_in_month = max_days;
    return 0;
}

int32_t __k_icu_calendar_months_in_year(const char* cal_name, int32_t era, int64_t year) {
    UCalendar* cal = open_calendar(cal_name);
    if (!cal) {
        g_last_icu_res.status = -1;
        return -1;
    }

    UErrorCode status = U_ZERO_ERROR;
    ucal_clear(cal);
    if (era >= 0) {
        ucal_set(cal, UCAL_ERA, era);
    }
    ucal_set(cal, UCAL_YEAR, (int32_t) year);

    int32_t max_months = ucal_getLimit(cal, UCAL_MONTH, UCAL_ACTUAL_MAXIMUM, &status) + 1;
    ucal_close(cal);
    if (!U_SUCCESS(status) || max_months <= 0) {
        g_last_icu_res.status = -1;
        return -1;
    }
    g_last_icu_res.status = 0;
    g_last_icu_res.months_in_year = max_months;
    return 0;
}

int32_t __k_icu_calendar_is_leap_year(const char* cal_name, int32_t era, int64_t year) {
    UCalendar* cal = open_calendar(cal_name);
    if (!cal) {
        g_last_icu_res.status = -1;
        return -1;
    }

    UErrorCode status = U_ZERO_ERROR;
    ucal_clear(cal);
    if (era >= 0) {
        ucal_set(cal, UCAL_ERA, era);
    }
    ucal_set(cal, UCAL_YEAR, (int32_t) year);

    int32_t days = ucal_getLimit(cal, UCAL_DAY_OF_YEAR, UCAL_ACTUAL_MAXIMUM, &status);
    int32_t months = ucal_getLimit(cal, UCAL_MONTH, UCAL_ACTUAL_MAXIMUM, &status) + 1;
    ucal_close(cal);

    int32_t is_leap = 0;
    if (months > 12 || days == 366 || days == 355 || days == 384 || days == 385) {
        is_leap = 1;
    }
    g_last_icu_res.status = 0;
    g_last_icu_res.is_leap_year = is_leap;
    return 0;
}

KIcuByteArray* __k_icu_get_era_display_name(const char* cal_name, const char* locale_tag, const char* era_id) {
    UErrorCode status = U_ZERO_ERROR;
    char canon[64];
    canonicalize_cal_name(cal_name, canon, sizeof(canon));

    char loc[128];
    if (locale_tag && *locale_tag) {
        snprintf(loc, sizeof(loc), "%s@calendar=%s", locale_tag, canon);
    } else {
        snprintf(loc, sizeof(loc), "en_US@calendar=%s", canon);
    }

    UChar tz_utc[] = {'U', 'T', 'C', 0};
    UDateFormat* df = udat_open(UDAT_NONE, UDAT_NONE, loc, tz_utc, 3, NULL, 0, &status);
    if (!U_SUCCESS(status)) {
        return NULL;
    }

    int era_index = -1;
    if (strcmp(era_id, "BC") == 0 || strcmp(era_id, "BCE") == 0) era_index = 0;
    else if (strcmp(era_id, "AD") == 0 || strcmp(era_id, "CE") == 0) era_index = 1;
    else if (sscanf(era_id, "%d", &era_index) != 1) {
        int count = udat_countSymbols(df, UDAT_ERAS);
        for (int i = 0; i < count; ++i) {
            UChar buf[64];
            int32_t len = udat_getSymbols(df, UDAT_ERAS, i, buf, 64, &status);
            if (U_SUCCESS(status)) {
                char u8[128];
                int32_t u8_len;
                u_strToUTF8(u8, sizeof(u8), &u8_len, buf, len, &status);
                if (strcasecmp(u8, era_id) == 0) {
                    era_index = i;
                    break;
                }
            }
        }
        if (era_index == -1) {
            count = udat_countSymbols(df, UDAT_ERA_NAMES);
            for (int i = 0; i < count; ++i) {
                UChar buf[64];
                int32_t len = udat_getSymbols(df, UDAT_ERA_NAMES, i, buf, 64, &status);
                if (U_SUCCESS(status)) {
                    char u8[128];
                    int32_t u8_len;
                    u_strToUTF8(u8, sizeof(u8), &u8_len, buf, len, &status);
                    if (strcasecmp(u8, era_id) == 0) {
                        era_index = i;
                        break;
                    }
                }
            }
        }
    }

    if (era_index < 0) {
        udat_close(df);
        return NULL;
    }

    UChar buf[128];
    int32_t len = udat_getSymbols(df, UDAT_ERA_NAMES, era_index, buf, 128, &status);
    if (!U_SUCCESS(status) || len == 0) {
        status = U_ZERO_ERROR;
        len = udat_getSymbols(df, UDAT_ERAS, era_index, buf, 128, &status);
    }
    if (!U_SUCCESS(status) || len == 0) {
        udat_close(df);
        return NULL;
    }

    char out_utf8[256];
    int32_t u8_len;
    u_strToUTF8(out_utf8, sizeof(out_utf8), &u8_len, buf, len, &status);
    udat_close(df);

    if (!U_SUCCESS(status)) {
        return NULL;
    }
    return make_icu_byte_array(out_utf8);
}

int32_t __k_icu_is_locale_valid(const char* locale_tag) {
    if (!locale_tag || !*locale_tag) return 0;
    char loc[128];
    UErrorCode status = U_ZERO_ERROR;
    int32_t parsed_len = 0;
    uloc_forLanguageTag(locale_tag, loc, sizeof(loc), &parsed_len, &status);
    return (U_SUCCESS(status) && parsed_len > 0) ? 1 : 0;
}

KIcuByteArray* __k_icu_get_system_locale(void) {
    const char* def = uloc_getDefault();
    if (!def || !*def) {
        return NULL;
    }
    char tag[128];
    UErrorCode status = U_ZERO_ERROR;
    uloc_toLanguageTag(def, tag, sizeof(tag), 0, &status);
    if (!U_SUCCESS(status) || !*tag) {
        return NULL;
    }
    return make_icu_byte_array(tag);
}

KIcuByteArray* __k_icu_format_pattern(const char* locale_tag, const char* cal_name, const char* tz_id, const char* pattern, int64_t epoch_sec, int32_t nanos) {
    UErrorCode status = U_ZERO_ERROR;
    char canon[64];
    canonicalize_cal_name(cal_name, canon, sizeof(canon));

    char loc[128];
    if (locale_tag && *locale_tag) {
        snprintf(loc, sizeof(loc), "%s@calendar=%s", locale_tag, canon);
    } else {
        snprintf(loc, sizeof(loc), "en_US@calendar=%s", canon);
    }

    UChar tz_uchars[64];
    int32_t tz_len = 0;
    if (tz_id && *tz_id) {
        u_charsToUChars(tz_id, tz_uchars, strlen(tz_id) + 1);
        tz_len = (int32_t) strlen(tz_id);
    } else {
        u_charsToUChars("UTC", tz_uchars, 4);
        tz_len = 3;
    }

    UChar upattern[128];
    u_charsToUChars(pattern ? pattern : "", upattern, pattern ? (strlen(pattern) + 1) : 1);

    UDateFormat* df = udat_open(UDAT_PATTERN, UDAT_PATTERN, loc, tz_uchars, tz_len, upattern, -1, &status);
    if (!U_SUCCESS(status)) {
        return NULL;
    }

    double ms = ((double) epoch_sec) * 1000.0 + ((double) nanos) / 1000000.0;
    UChar uresult[256];
    int32_t res_len = udat_format(df, ms, uresult, 256, NULL, &status);
    if (!U_SUCCESS(status)) {
        udat_close(df);
        return NULL;
    }

    char out_utf8[512];
    int32_t u8_len;
    u_strToUTF8(out_utf8, sizeof(out_utf8), &u8_len, uresult, res_len, &status);
    udat_close(df);

    if (!U_SUCCESS(status)) {
        return NULL;
    }
    return make_icu_byte_array(out_utf8);
}

int32_t __k_icu_parse_pattern(const char* locale_tag, const char* cal_name, const char* tz_id, const char* pattern, const char* text) {
    UErrorCode status = U_ZERO_ERROR;
    char canon[64];
    canonicalize_cal_name(cal_name, canon, sizeof(canon));

    char loc[128];
    if (locale_tag && *locale_tag) {
        snprintf(loc, sizeof(loc), "%s@calendar=%s", locale_tag, canon);
    } else {
        snprintf(loc, sizeof(loc), "en_US@calendar=%s", canon);
    }

    UChar tz_uchars[64];
    int32_t tz_len = 0;
    if (tz_id && *tz_id) {
        u_charsToUChars(tz_id, tz_uchars, strlen(tz_id) + 1);
        tz_len = (int32_t) strlen(tz_id);
    } else {
        u_charsToUChars("UTC", tz_uchars, 4);
        tz_len = 3;
    }

    UChar upattern[128];
    u_charsToUChars(pattern ? pattern : "", upattern, pattern ? (strlen(pattern) + 1) : 1);

    UDateFormat* df = udat_open(UDAT_PATTERN, UDAT_PATTERN, loc, tz_uchars, tz_len, upattern, -1, &status);
    if (!U_SUCCESS(status)) {
        return -1;
    }

    UChar utext[256];
    u_charsToUChars(text ? text : "", utext, text ? (strlen(text) + 1) : 1);

    int32_t parse_pos = 0;
    UDate ms = udat_parse(df, utext, -1, &parse_pos, &status);
    udat_close(df);

    if (!U_SUCCESS(status) || parse_pos <= 0) {
        return -1;
    }

    int64_t sec = (int64_t) floor(ms / 1000.0);
    int32_t rem_ms = (int32_t) ((int64_t) ms - sec * 1000L);
    if (rem_ms < 0) rem_ms += 1000;
    g_last_icu_res.status = 0;
    g_last_icu_res.epoch_sec = sec;
    g_last_icu_res.nanos = rem_ms * 1000000;
    return 0;
}

#else

// ── Fallback stubs when ICU is disabled ──────────────────────────────────────

int32_t __k_icu_is_available(void) {
    return 0;
}

int32_t __k_icu_calendar_from_epoch_day(const char* cal_name, int64_t epoch_day) {
    (void) cal_name; (void) epoch_day;
    g_last_icu_res.status = -1;
    return -1;
}

int32_t __k_icu_calendar_to_epoch_day(const char* cal_name, int32_t era, int64_t year, int32_t month, int32_t day, int32_t is_leap) {
    (void) cal_name; (void) era; (void) year; (void) month; (void) day; (void) is_leap;
    g_last_icu_res.status = -1;
    return -1;
}

int32_t __k_icu_calendar_days_in_month(const char* cal_name, int32_t era, int64_t year, int32_t month, int32_t is_leap) {
    (void) cal_name; (void) era; (void) year; (void) month; (void) is_leap;
    g_last_icu_res.status = -1;
    return -1;
}

int32_t __k_icu_calendar_months_in_year(const char* cal_name, int32_t era, int64_t year) {
    (void) cal_name; (void) era; (void) year;
    g_last_icu_res.status = -1;
    return -1;
}

int32_t __k_icu_calendar_is_leap_year(const char* cal_name, int32_t era, int64_t year) {
    (void) cal_name; (void) era; (void) year;
    g_last_icu_res.status = -1;
    return -1;
}

KIcuByteArray* __k_icu_get_era_display_name(const char* cal_name, const char* locale_tag, const char* era_id) {
    (void) cal_name; (void) locale_tag; (void) era_id;
    return NULL;
}

int32_t __k_icu_is_locale_valid(const char* locale_tag) {
    (void) locale_tag;
    return 0;
}

KIcuByteArray* __k_icu_get_system_locale(void) {
    return NULL;
}

KIcuByteArray* __k_icu_format_pattern(const char* locale_tag, const char* cal_name, const char* tz_id, const char* pattern, int64_t epoch_sec, int32_t nanos) {
    (void) locale_tag; (void) cal_name; (void) tz_id; (void) pattern; (void) epoch_sec; (void) nanos;
    return NULL;
}

int32_t __k_icu_parse_pattern(const char* locale_tag, const char* cal_name, const char* tz_id, const char* pattern, const char* text) {
    (void) locale_tag; (void) cal_name; (void) tz_id; (void) pattern; (void) text;
    return -1;
}

#endif
