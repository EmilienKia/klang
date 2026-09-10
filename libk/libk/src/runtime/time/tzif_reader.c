/*
 * K Language runtime — TZif binary reader implementation
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

#include "tzif_reader.h"
#include <stdlib.h>
#include <string.h>

static inline int32_t read_be32(const uint8_t* p) {
    return (int32_t)(((uint32_t)p[0] << 24) |
                     ((uint32_t)p[1] << 16) |
                     ((uint32_t)p[2] << 8)  |
                     ((uint32_t)p[3]));
}

static inline int64_t read_be64(const uint8_t* p) {
    return (int64_t)(((uint64_t)p[0] << 56) |
                     ((uint64_t)p[1] << 48) |
                     ((uint64_t)p[2] << 40) |
                     ((uint64_t)p[3] << 32) |
                     ((uint64_t)p[4] << 24) |
                     ((uint64_t)p[5] << 16) |
                     ((uint64_t)p[6] << 8)  |
                     ((uint64_t)p[7]));
}

static void free_snapshot(KZoneRulesSnapshot* snap) {
    if (!snap) return;
    if (snap->transitions) free(snap->transitions);
    if (snap->type_indices) free(snap->type_indices);
    if (snap->types) free(snap->types);
    if (snap->posix_tz) free(snap->posix_tz);
    free(snap);
}

KZoneRulesSnapshot* k_tzif_parse(const uint8_t* data, size_t size) {
    if (!data || size < 44) {
        return NULL;
    }

    /* Check magic "TZif" */
    if (memcmp(data, "TZif", 4) != 0) {
        return NULL;
    }

    uint8_t version = data[4];
    if (version != '\0' && version != '2' && version != '3') {
        return NULL;
    }

    /* Read v1 header counts at offset 20 */
    int32_t ttisgmtcnt = read_be32(data + 20);
    int32_t ttisstdcnt = read_be32(data + 24);
    int32_t leapcnt    = read_be32(data + 28);
    int32_t timecnt    = read_be32(data + 32);
    int32_t typecnt    = read_be32(data + 36);
    int32_t charcnt    = read_be32(data + 40);

    if (ttisgmtcnt < 0 || ttisstdcnt < 0 || leapcnt < 0 ||
        timecnt < 0 || typecnt <= 0 || charcnt < 0 ||
        typecnt > 256 || charcnt > 65536 || timecnt > 200000) {
        return NULL;
    }

    size_t v1_data_size = (size_t)timecnt * 4 +
                          (size_t)timecnt * 1 +
                          (size_t)typecnt * 6 +
                          (size_t)charcnt +
                          (size_t)leapcnt * 8 +
                          (size_t)ttisstdcnt +
                          (size_t)ttisgmtcnt;

    if (44 + v1_data_size > size) {
        return NULL;
    }

    /* If version 2 or 3, prefer the 64-bit v2 block */
    if (version == '2' || version == '3') {
        size_t v2_off = 44 + v1_data_size;
        if (v2_off + 44 > size) {
            return NULL;
        }

        if (memcmp(data + v2_off, "TZif", 4) != 0) {
            return NULL;
        }

        uint8_t v2_ver = data[v2_off + 4];
        if (v2_ver != '2' && v2_ver != '3') {
            return NULL;
        }

        int32_t v2_ttisgmtcnt = read_be32(data + v2_off + 20);
        int32_t v2_ttisstdcnt = read_be32(data + v2_off + 24);
        int32_t v2_leapcnt    = read_be32(data + v2_off + 28);
        int32_t v2_timecnt    = read_be32(data + v2_off + 32);
        int32_t v2_typecnt    = read_be32(data + v2_off + 36);
        int32_t v2_charcnt    = read_be32(data + v2_off + 40);

        if (v2_ttisgmtcnt < 0 || v2_ttisstdcnt < 0 || v2_leapcnt < 0 ||
            v2_timecnt < 0 || v2_typecnt <= 0 || v2_charcnt < 0 ||
            v2_typecnt > 256 || v2_charcnt > 65536 || v2_timecnt > 200000) {
            return NULL;
        }

        size_t v2_data_size = (size_t)v2_timecnt * 8 +
                              (size_t)v2_timecnt * 1 +
                              (size_t)v2_typecnt * 6 +
                              (size_t)v2_charcnt +
                              (size_t)v2_leapcnt * 12 +
                              (size_t)v2_ttisstdcnt +
                              (size_t)v2_ttisgmtcnt;

        if (v2_off + 44 + v2_data_size > size) {
            return NULL;
        }

        KZoneRulesSnapshot* snap = (KZoneRulesSnapshot*)calloc(1, sizeof(KZoneRulesSnapshot));
        if (!snap) return NULL;
        snap->ref_count = 1;
        snap->transition_count = v2_timecnt;
        snap->type_count = v2_typecnt;

        if (v2_timecnt > 0) {
            snap->transitions = (int64_t*)malloc(sizeof(int64_t) * (size_t)v2_timecnt);
            snap->type_indices = (uint8_t*)malloc(sizeof(uint8_t) * (size_t)v2_timecnt);
            if (!snap->transitions || !snap->type_indices) {
                free_snapshot(snap);
                return NULL;
            }

            const uint8_t* p_trans = data + v2_off + 44;
            for (int32_t i = 0; i < v2_timecnt; ++i) {
                snap->transitions[i] = read_be64(p_trans + i * 8);
            }

            const uint8_t* p_tidx = p_trans + (size_t)v2_timecnt * 8;
            for (int32_t i = 0; i < v2_timecnt; ++i) {
                uint8_t idx = p_tidx[i];
                if (idx >= v2_typecnt) {
                    free_snapshot(snap);
                    return NULL;
                }
                snap->type_indices[i] = idx;
            }
        }

        snap->types = (KZoneType*)calloc((size_t)v2_typecnt, sizeof(KZoneType));
        if (!snap->types) {
            free_snapshot(snap);
            return NULL;
        }

        const uint8_t* p_ttinfo = data + v2_off + 44 + (size_t)v2_timecnt * 9;
        const uint8_t* p_abbrev = p_ttinfo + (size_t)v2_typecnt * 6;

        for (int32_t i = 0; i < v2_typecnt; ++i) {
            int32_t utoff = read_be32(p_ttinfo + i * 6);
            uint8_t isdst = p_ttinfo[i * 6 + 4];
            uint8_t desig = p_ttinfo[i * 6 + 5];

            if (desig >= v2_charcnt) {
                free_snapshot(snap);
                return NULL;
            }

            snap->types[i].utoff = utoff;
            snap->types[i].is_dst = (isdst != 0) ? 1 : 0;

            /* Copy null-terminated abbrev */
            const char* abbrev_src = (const char*)(p_abbrev + desig);
            size_t max_len = (size_t)(v2_charcnt - desig);
            size_t actual_len = 0;
            while (actual_len < max_len && abbrev_src[actual_len] != '\0') {
                actual_len++;
            }
            if (actual_len >= max_len) {
                /* Not null-terminated within charcnt */
                free_snapshot(snap);
                return NULL;
            }
            if (actual_len > 15) actual_len = 15;
            memcpy(snap->types[i].abbrev, abbrev_src, actual_len);
            snap->types[i].abbrev[actual_len] = '\0';
        }

        /* Check for POSIX TZ footer */
        size_t posix_start = v2_off + 44 + v2_data_size;
        if (posix_start + 2 <= size && data[posix_start] == '\n') {
            size_t p = posix_start + 1;
            while (p < size && data[p] != '\n') p++;
            if (p < size && data[p] == '\n') {
                size_t tz_len = p - (posix_start + 1);
                if (tz_len > 0) {
                    snap->posix_tz = (char*)malloc(tz_len + 1);
                    if (snap->posix_tz) {
                        memcpy(snap->posix_tz, data + posix_start + 1, tz_len);
                        snap->posix_tz[tz_len] = '\0';
                    }
                }
            }
        }

        return snap;
    }

    /* Version 1 (32-bit transitions) */
    KZoneRulesSnapshot* snap = (KZoneRulesSnapshot*)calloc(1, sizeof(KZoneRulesSnapshot));
    if (!snap) return NULL;
    snap->ref_count = 1;
    snap->transition_count = timecnt;
    snap->type_count = typecnt;

    if (timecnt > 0) {
        snap->transitions = (int64_t*)malloc(sizeof(int64_t) * (size_t)timecnt);
        snap->type_indices = (uint8_t*)malloc(sizeof(uint8_t) * (size_t)timecnt);
        if (!snap->transitions || !snap->type_indices) {
            free_snapshot(snap);
            return NULL;
        }

        const uint8_t* p_trans = data + 44;
        for (int32_t i = 0; i < timecnt; ++i) {
            snap->transitions[i] = (int64_t)read_be32(p_trans + i * 4);
        }

        const uint8_t* p_tidx = p_trans + (size_t)timecnt * 4;
        for (int32_t i = 0; i < timecnt; ++i) {
            uint8_t idx = p_tidx[i];
            if (idx >= typecnt) {
                free_snapshot(snap);
                return NULL;
            }
            snap->type_indices[i] = idx;
        }
    }

    snap->types = (KZoneType*)calloc((size_t)typecnt, sizeof(KZoneType));
    if (!snap->types) {
        free_snapshot(snap);
        return NULL;
    }

    const uint8_t* p_ttinfo = data + 44 + (size_t)timecnt * 5;
    const uint8_t* p_abbrev = p_ttinfo + (size_t)typecnt * 6;

    for (int32_t i = 0; i < typecnt; ++i) {
        int32_t utoff = read_be32(p_ttinfo + i * 6);
        uint8_t isdst = p_ttinfo[i * 6 + 4];
        uint8_t desig = p_ttinfo[i * 6 + 5];

        if (desig >= charcnt) {
            free_snapshot(snap);
            return NULL;
        }

        snap->types[i].utoff = utoff;
        snap->types[i].is_dst = (isdst != 0) ? 1 : 0;

        const char* abbrev_src = (const char*)(p_abbrev + desig);
        size_t max_len = (size_t)(charcnt - desig);
        size_t actual_len = 0;
        while (actual_len < max_len && abbrev_src[actual_len] != '\0') {
            actual_len++;
        }
        if (actual_len >= max_len) {
            free_snapshot(snap);
            return NULL;
        }
        if (actual_len > 15) actual_len = 15;
        memcpy(snap->types[i].abbrev, abbrev_src, actual_len);
        snap->types[i].abbrev[actual_len] = '\0';
    }

    return snap;
}

void k_zone_rules_snapshot_retain(KZoneRulesSnapshot* snap) {
    if (snap) {
        snap->ref_count++;
    }
}

void k_zone_rules_snapshot_release(KZoneRulesSnapshot* snap) {
    if (!snap) return;
    snap->ref_count--;
    if (snap->ref_count <= 0) {
        free_snapshot(snap);
    }
}

int32_t k_zone_rules_snapshot_transition_count(const KZoneRulesSnapshot* snap) {
    return snap ? snap->transition_count : 0;
}

int64_t k_zone_rules_snapshot_transition_time(const KZoneRulesSnapshot* snap, int32_t index) {
    if (!snap || index < 0 || index >= snap->transition_count) return 0;
    return snap->transitions[index];
}

int32_t k_zone_rules_snapshot_transition_offset(const KZoneRulesSnapshot* snap, int32_t index) {
    if (!snap || index < 0 || index >= snap->transition_count) return 0;
    uint8_t idx = snap->type_indices[index];
    return snap->types[idx].utoff;
}

const char* k_zone_rules_snapshot_transition_abbrev(const KZoneRulesSnapshot* snap, int32_t index) {
    if (!snap || index < 0 || index >= snap->transition_count) return "";
    uint8_t idx = snap->type_indices[index];
    return snap->types[idx].abbrev;
}

int32_t k_zone_rules_snapshot_transition_is_dst(const KZoneRulesSnapshot* snap, int32_t index) {
    if (!snap || index < 0 || index >= snap->transition_count) return 0;
    uint8_t idx = snap->type_indices[index];
    return snap->types[idx].is_dst;
}

static int32_t find_type_index_at(const KZoneRulesSnapshot* snap, int64_t epoch_second) {
    if (!snap || snap->type_count == 0) return 0;
    if (snap->transition_count == 0 || epoch_second < snap->transitions[0]) {
        /* RFC 8536: before first transition, use first standard time ttinfo if present */
        for (int32_t i = 0; i < snap->type_count; ++i) {
            if (snap->types[i].is_dst == 0) return i;
        }
        return 0;
    }

    int32_t low = 0, high = snap->transition_count - 1;
    int32_t best = 0;
    while (low <= high) {
        int32_t mid = low + (high - low) / 2;
        if (snap->transitions[mid] <= epoch_second) {
            best = mid;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }
    return snap->type_indices[best];
}

int32_t k_zone_rules_snapshot_offset_at(const KZoneRulesSnapshot* snap, int64_t epoch_second) {
    if (!snap || snap->type_count == 0) return 0;
    int32_t idx = find_type_index_at(snap, epoch_second);
    return snap->types[idx].utoff;
}

const char* k_zone_rules_snapshot_abbrev_at(const KZoneRulesSnapshot* snap, int64_t epoch_second) {
    if (!snap || snap->type_count == 0) return "";
    int32_t idx = find_type_index_at(snap, epoch_second);
    return snap->types[idx].abbrev;
}

int32_t k_zone_rules_snapshot_is_dst_at(const KZoneRulesSnapshot* snap, int64_t epoch_second) {
    if (!snap || snap->type_count == 0) return 0;
    int32_t idx = find_type_index_at(snap, epoch_second);
    return snap->types[idx].is_dst;
}

const char* k_zone_rules_snapshot_posix_tz(const KZoneRulesSnapshot* snap) {
    if (!snap || !snap->posix_tz) return "";
    return snap->posix_tz;
}
