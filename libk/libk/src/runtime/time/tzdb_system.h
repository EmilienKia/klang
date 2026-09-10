/*
 * K Language runtime — System TZDB provider header
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

#ifndef K_RUNTIME_TIME_TZDB_SYSTEM_H
#define K_RUNTIME_TIME_TZDB_SYSTEM_H

#include "tzif_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Configure the root directory for TZDB files.
 * If root_path is NULL or empty, defaults to "/usr/share/zoneinfo".
 */
void k_tzdb_set_root(const char* root_path);

/**
 * Get the current root directory for TZDB files.
 */
const char* k_tzdb_get_root(void);

/**
 * Obtain the version string of the loaded TZDB (e.g. "2024a", "2026c").
 * If version cannot be determined, returns "unknown".
 */
const char* k_tzdb_get_version(void);

/**
 * Safely load a zone by identifier (e.g. "Europe/Paris", "UTC").
 * Validates that the path does not escape the configured root directory.
 * Returns a retained KZoneRulesSnapshot* on success, or NULL on error / missing zone.
 */
KZoneRulesSnapshot* k_tzdb_load_zone(const char* zone_id);

/**
 * Release a snapshot obtained from k_tzdb_load_zone.
 */
void k_tzdb_snapshot_release(KZoneRulesSnapshot* snap);

#ifdef __cplusplus
}
#endif

#endif /* K_RUNTIME_TIME_TZDB_SYSTEM_H */
