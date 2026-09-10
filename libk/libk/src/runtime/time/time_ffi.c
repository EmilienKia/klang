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
#include <stdio.h>
#include <ctype.h>
#include <unistd.h>

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

int64_t __k_time_leap_second_posix_to_continuous(int64_t posix_sec) {
    return k_leap_second_posix_to_continuous(posix_sec);
}

int64_t __k_time_leap_second_continuous_to_posix(int64_t continuous_sec, int32_t policy, int32_t* out_error) {
    return k_leap_second_continuous_to_posix(continuous_sec, policy, out_error);
}

int32_t __k_time_leap_second_get_leap_date(int64_t continuous_sec, int64_t* out_year, int32_t* out_month, int32_t* out_day) {
    return k_leap_second_get_leap_date_for_instant(continuous_sec, out_year, out_month, out_day);
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

void __k_tzdb_snapshot_retain(KZoneRulesSnapshot* snap) {
    k_zone_rules_snapshot_retain(snap);
}

static __thread KZoneLocalResolutionC g_last_local_res;

void __k_tzdb_snapshot_resolve_local(const KZoneRulesSnapshot* snap, int64_t local_sec) {
    g_last_local_res = k_zone_rules_snapshot_resolve_local(snap, local_sec);
}

int32_t __k_tzdb_snapshot_res_kind(void) {
    return g_last_local_res.kind;
}

int32_t __k_tzdb_snapshot_res_before_offset(void) {
    return g_last_local_res.before_offset;
}

int32_t __k_tzdb_snapshot_res_after_offset(void) {
    return g_last_local_res.after_offset;
}

int64_t __k_tzdb_snapshot_res_trans_time(void) {
    return g_last_local_res.trans_time;
}

static char g_system_zone_override[128] = "";

void __k_tzdb_set_system_zone_override(const char* name) {
    if (name) {
        strncpy(g_system_zone_override, name, sizeof(g_system_zone_override) - 1);
        g_system_zone_override[sizeof(g_system_zone_override) - 1] = '\0';
    } else {
        g_system_zone_override[0] = '\0';
    }
}

KByteArray* __k_tzdb_detect_system_zone(void) {
    if (g_system_zone_override[0] != '\0') {
        if (strcmp(g_system_zone_override, "__UNAVAILABLE__") == 0) {
            return NULL;
        }
        return make_k_byte_array(g_system_zone_override);
    }

    /* 1. Check TZ environment variable */
    const char* tz = getenv("TZ");
    if (tz && *tz) {
        if (*tz == ':') tz++;
        if (*tz) return make_k_byte_array(tz);
    }

    /* 2. Check /etc/timezone */
    FILE* fp = fopen("/etc/timezone", "r");
    if (fp) {
        char buf[128];
        if (fgets(buf, sizeof(buf), fp)) {
            char* p = buf;
            while (*p && isspace((unsigned char)*p)) p++;
            char* end = p + strlen(p) - 1;
            while (end >= p && isspace((unsigned char)*end)) *end-- = '\0';
            fclose(fp);
            if (*p) return make_k_byte_array(p);
        } else {
            fclose(fp);
        }
    }

    /* 3. Check /etc/localtime symlink */
    char linkbuf[256];
    ssize_t len = readlink("/etc/localtime", linkbuf, sizeof(linkbuf) - 1);
    if (len > 0) {
        linkbuf[len] = '\0';
        const char* zi = strstr(linkbuf, "zoneinfo/");
        if (zi) {
            zi += strlen("zoneinfo/");
            if (*zi) return make_k_byte_array(zi);
        }
    }

    return NULL;
}


