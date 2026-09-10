/*
 * K Language runtime — TZif binary reader header
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

#ifndef K_RUNTIME_TIME_TZIF_READER_H
#define K_RUNTIME_TIME_TZIF_READER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int32_t utoff;      /* Offset from UTC in seconds (e.g. 3600 for +01:00) */
    uint8_t is_dst;     /* 1 if daylight saving time, 0 if standard */
    char    abbrev[16]; /* Time zone abbreviation, null-terminated */
} KZoneType;

typedef struct KZoneRulesSnapshot {
    int32_t      ref_count;
    int32_t      transition_count;
    int64_t*     transitions;    /* Epoch second of each transition (length transition_count) */
    uint8_t*     type_indices;   /* Index into types array for each transition (length transition_count) */
    int32_t      type_count;
    KZoneType*   types;          /* Array of zone types (length type_count) */
    char*        posix_tz;       /* POSIX TZ string footer, null-terminated, or NULL */
} KZoneRulesSnapshot;

/**
 * Parse a TZif binary buffer (version 1, 2, or 3).
 * Fully bounds-checked.
 * Returns an allocated KZoneRulesSnapshot with ref_count = 1 on success,
 * or NULL on truncated / malformed input.
 */
KZoneRulesSnapshot* k_tzif_parse(const uint8_t* data, size_t size);

/**
 * Retain / release snapshot memory (reference counted).
 */
void k_zone_rules_snapshot_retain(KZoneRulesSnapshot* snap);
void k_zone_rules_snapshot_release(KZoneRulesSnapshot* snap);

/**
 * Query functions on snapshot.
 */
int32_t k_zone_rules_snapshot_transition_count(const KZoneRulesSnapshot* snap);
int64_t k_zone_rules_snapshot_transition_time(const KZoneRulesSnapshot* snap, int32_t index);
int32_t k_zone_rules_snapshot_transition_offset(const KZoneRulesSnapshot* snap, int32_t index);
const char* k_zone_rules_snapshot_transition_abbrev(const KZoneRulesSnapshot* snap, int32_t index);
int32_t k_zone_rules_snapshot_transition_is_dst(const KZoneRulesSnapshot* snap, int32_t index);

int32_t k_zone_rules_snapshot_offset_at(const KZoneRulesSnapshot* snap, int64_t epoch_second);
const char* k_zone_rules_snapshot_abbrev_at(const KZoneRulesSnapshot* snap, int64_t epoch_second);
int32_t k_zone_rules_snapshot_is_dst_at(const KZoneRulesSnapshot* snap, int64_t epoch_second);
const char* k_zone_rules_snapshot_posix_tz(const KZoneRulesSnapshot* snap);

typedef struct {
    int32_t kind;          /* 0 = unique, 1 = gap, 2 = overlap */
    int32_t before_offset; /* seconds */
    int32_t after_offset;  /* seconds */
    int64_t trans_time;    /* UTC epoch second of transition */
} KZoneLocalResolutionC;

KZoneLocalResolutionC k_zone_rules_snapshot_resolve_local(const KZoneRulesSnapshot* snap, int64_t local_sec);

#ifdef __cplusplus
}
#endif

#endif /* K_RUNTIME_TIME_TZIF_READER_H */
