/*
 * K Language runtime — ICU bridge header (C)
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

#ifndef K_RUNTIME_TIME_ICU_BRIDGE_H
#define K_RUNTIME_TIME_ICU_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t      size;
    unsigned char data[];
} KIcuByteArray;

int32_t __k_icu_is_available(void);

int32_t __k_icu_calendar_from_epoch_day(const char* cal_name, int64_t epoch_day);
int32_t __k_icu_calendar_to_epoch_day(const char* cal_name, int32_t era, int64_t year, int32_t month, int32_t day, int32_t is_leap);
int32_t __k_icu_calendar_days_in_month(const char* cal_name, int32_t era, int64_t year, int32_t month, int32_t is_leap);
int32_t __k_icu_calendar_months_in_year(const char* cal_name, int32_t era, int64_t year);
int32_t __k_icu_calendar_is_leap_year(const char* cal_name, int32_t era, int64_t year);

int32_t __k_icu_res_status(void);
int32_t __k_icu_res_era(void);
int64_t __k_icu_res_year(void);
int32_t __k_icu_res_month(void);
int32_t __k_icu_res_day(void);
int32_t __k_icu_res_is_leap(void);
int64_t __k_icu_res_epoch_day(void);
int32_t __k_icu_res_days_in_month(void);
int32_t __k_icu_res_months_in_year(void);
int32_t __k_icu_res_is_leap_year(void);
int64_t __k_icu_res_epoch_sec(void);
int32_t __k_icu_res_nanos(void);

KIcuByteArray* __k_icu_get_era_display_name(const char* cal_name, const char* locale_tag, const char* era_id);

int32_t __k_icu_is_locale_valid(const char* locale_tag);
KIcuByteArray* __k_icu_get_system_locale(void);

KIcuByteArray* __k_icu_format_pattern(const char* locale_tag, const char* cal_name, const char* tz_id, const char* pattern, int64_t epoch_sec, int32_t nanos);
int32_t __k_icu_parse_pattern(const char* locale_tag, const char* cal_name, const char* tz_id, const char* pattern, const char* text);

#ifdef __cplusplus
}
#endif

#endif // K_RUNTIME_TIME_ICU_BRIDGE_H
