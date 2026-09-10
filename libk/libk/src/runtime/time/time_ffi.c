/*
 * K Language runtime — Time FFI wrappers (C)
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

#include "platform_clock.h"
#include "leap_second_data.h"
#include "tzif_reader.h"
#include "tzdb_system.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static __thread KPlatformClockReading g_last_reading;

typedef struct {
    uint32_t      size;
    unsigned char data[];
} KByteArray;

static KByteArray* make_k_byte_array(const char* s) {
    size_t len = s ? strlen(s) : 0;
    KByteArray* arr = (KByteArray*) malloc(sizeof(uint32_t) + len + 1);
    if (!arr) abort();
    arr->size = (uint32_t) (len + 1);
    if (len > 0) {
        memcpy(arr->data, s, len);
    }
    arr->data[len] = '\0';
    return arr;
}

int32_t __k_time_clock_read_realtime(void) {
    g_last_reading = k_platform_clock_realtime();
    return g_last_reading.status;
}

int32_t __k_time_clock_read_monotonic_active(void) {
    g_last_reading = k_platform_clock_monotonic_active();
    return g_last_reading.status;
}

int32_t __k_time_clock_read_monotonic_elapsed(void) {
    g_last_reading = k_platform_clock_monotonic_elapsed();
    return g_last_reading.status;
}

int32_t __k_time_clock_read_process_cpu(void) {
    g_last_reading = k_platform_clock_process_cpu();
    return g_last_reading.status;
}

int32_t __k_time_clock_read_thread_cpu(void) {
    g_last_reading = k_platform_clock_thread_cpu();
    return g_last_reading.status;
}

int32_t __k_time_clock_read_resolution(int32_t clock_type) {
    g_last_reading = k_platform_clock_get_resolution(clock_type);
    return g_last_reading.status;
}

int64_t __k_time_clock_get_reading_sec(void) {
    return g_last_reading.seconds;
}

int32_t __k_time_clock_get_reading_nano(void) {
    return g_last_reading.nanos;
}

int32_t __k_time_clock_get_reading_status(void) {
    return g_last_reading.status;
}

int64_t __k_time_clock_get_realtime_sec(void) {
    return g_last_reading.seconds;
}

int32_t __k_time_clock_get_realtime_nano(void) {
    return g_last_reading.nanos;
}

int32_t __k_time_clock_get_realtime_status(void) {
    return g_last_reading.status;
}

int32_t __k_time_clock_map_status(int32_t status) {
    switch (status) {
        case K_CLOCK_STATUS_OK:
            return 0;
        case K_CLOCK_STATUS_UNAVAILABLE:
            return 510;
        case K_CLOCK_STATUS_FAILED:
            return 510;
        case K_CLOCK_STATUS_UNSUPPORTED:
            return 510;
        default:
            return 510;
    }
}

/* ── Leap seconds FFI ──────────────────────────────────────────────────────── */

int32_t __k_time_leap_second_is_leap_second(int64_t epoch_second) {
    return k_leap_second_is_leap_second(epoch_second);
}

int64_t __k_time_leap_second_tai_offset(int64_t epoch_second) {
    return k_leap_second_tai_offset(epoch_second);
}

int32_t __k_time_leap_second_count(void) {
    return k_leap_second_count();
}

int32_t __k_time_leap_second_load(const char* file_path) {
    return k_leap_second_load(file_path);
}

int32_t __k_time_leap_second_is_leap_date(int64_t year, int32_t month, int32_t day) {
    return k_leap_second_is_leap_date(year, month, day);
}

/* ── TZDB & TZif FFI ───────────────────────────────────────────────────────── */

void __k_tzdb_set_root(const char* root) {
    k_tzdb_set_root(root);
}

KByteArray* __k_tzdb_get_version_bytes(void) {
    const char* v = k_tzdb_get_version();
    return make_k_byte_array(v);
}

KZoneRulesSnapshot* __k_tzdb_load_zone(const char* zone_id) {
    return k_tzdb_load_zone(zone_id);
}

void __k_tzdb_snapshot_release(KZoneRulesSnapshot* snap) {
    k_tzdb_snapshot_release(snap);
}

int32_t __k_tzdb_snapshot_transition_count(const KZoneRulesSnapshot* snap) {
    return k_zone_rules_snapshot_transition_count(snap);
}

int64_t __k_tzdb_snapshot_transition_time(const KZoneRulesSnapshot* snap, int32_t index) {
    return k_zone_rules_snapshot_transition_time(snap, index);
}

int32_t __k_tzdb_snapshot_transition_offset(const KZoneRulesSnapshot* snap, int32_t index) {
    return k_zone_rules_snapshot_transition_offset(snap, index);
}

KByteArray* __k_tzdb_snapshot_transition_abbrev(const KZoneRulesSnapshot* snap, int32_t index) {
    const char* a = k_zone_rules_snapshot_transition_abbrev(snap, index);
    return make_k_byte_array(a);
}

int32_t __k_tzdb_snapshot_transition_is_dst(const KZoneRulesSnapshot* snap, int32_t index) {
    return k_zone_rules_snapshot_transition_is_dst(snap, index);
}

int32_t __k_tzdb_snapshot_offset_at(const KZoneRulesSnapshot* snap, int64_t epoch_second) {
    return k_zone_rules_snapshot_offset_at(snap, epoch_second);
}

KByteArray* __k_tzdb_snapshot_abbrev_at(const KZoneRulesSnapshot* snap, int64_t epoch_second) {
    const char* a = k_zone_rules_snapshot_abbrev_at(snap, epoch_second);
    return make_k_byte_array(a);
}

int32_t __k_tzdb_snapshot_is_dst_at(const KZoneRulesSnapshot* snap, int64_t epoch_second) {
    return k_zone_rules_snapshot_is_dst_at(snap, epoch_second);
}

KByteArray* __k_tzdb_snapshot_posix_tz(const KZoneRulesSnapshot* snap) {
    const char* p = k_zone_rules_snapshot_posix_tz(snap);
    return make_k_byte_array(p);
}

