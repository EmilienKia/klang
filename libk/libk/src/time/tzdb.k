/*
 * K Language standard library — System TZDB provider
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
 * Opaque handle representing a native KZoneRulesSnapshot*.
 */
public struct KZoneRulesSnapshot {
}

// ── Private FFI declarations for TZDB and TZif runtime ───────────────────────
@ffi::Extern("C") private __k_tzdb_set_root(root: const byte*) : void;
@ffi::Extern("C") private __k_tzdb_get_version_bytes() : unsigned byte[]!;
@ffi::Extern("C") private __k_tzdb_load_zone(zone_id: const byte*) : KZoneRulesSnapshot*;
@ffi::Extern("C") private __k_tzdb_snapshot_release(snap: KZoneRulesSnapshot*) : void;
@ffi::Extern("C") private __k_tzdb_snapshot_transition_count(snap: const KZoneRulesSnapshot*) : int;
@ffi::Extern("C") private __k_tzdb_snapshot_transition_time(snap: const KZoneRulesSnapshot*, index: int) : long;
@ffi::Extern("C") private __k_tzdb_snapshot_transition_offset(snap: const KZoneRulesSnapshot*, index: int) : int;
@ffi::Extern("C") private __k_tzdb_snapshot_transition_abbrev(snap: const KZoneRulesSnapshot*, index: int) : unsigned byte[]!;
@ffi::Extern("C") private __k_tzdb_snapshot_transition_is_dst(snap: const KZoneRulesSnapshot*, index: int) : int;
@ffi::Extern("C") private __k_tzdb_snapshot_offset_at(snap: const KZoneRulesSnapshot*, epoch_second: long) : int;
@ffi::Extern("C") private __k_tzdb_snapshot_abbrev_at(snap: const KZoneRulesSnapshot*, epoch_second: long) : unsigned byte[]!;
@ffi::Extern("C") private __k_tzdb_snapshot_is_dst_at(snap: const KZoneRulesSnapshot*, epoch_second: long) : int;
@ffi::Extern("C") private __k_tzdb_snapshot_posix_tz(snap: const KZoneRulesSnapshot*) : unsigned byte[]!;

/**
 * Tzdb — access to the system time zone database and zone snapshots.
 *
 * Manages the time zone database root, reports database version,
 * and loads immutable ZoneRules snapshots from TZif files.
 */
public final class Tzdb {
public:
    /**
     * Return the version of the active time zone database (e.g. "2024a", "2026c").
     */
    static version() : String {
        bytes : unsigned byte[]! = __k_tzdb_get_version_bytes();
        return String(bytes);
    }

    /**
     * Configure the root directory for TZDB files (test seam / custom location).
     */
    static setRoot(rootPath: const String&) {
        u8 : unsigned byte[]! = rootPath.toUtf8();
        __k_tzdb_set_root((const byte*) &u8[0]);
    }

    /**
     * Check whether rules for the given ZoneId are available in the active database.
     */
    static isZoneAvailable(zoneId: const ZoneId&) : bool {
        u8 : unsigned byte[]! = zoneId.name().toUtf8();
        snap : KZoneRulesSnapshot* = __k_tzdb_load_zone((const byte*) &u8[0]);
        if (snap == null) {
            return false;
        }
        __k_tzdb_snapshot_release(snap);
        return true;
    }

    /**
     * Load an immutable native zone rules snapshot for the given ZoneId.
     * Throws ZoneRulesUnavailableException(511) if the zone cannot be loaded.
     */
    static loadZone(zoneId: const ZoneId&) : KZoneRulesSnapshot* throws(ZoneRulesUnavailableException) {
        u8 : unsigned byte[]! = zoneId.name().toUtf8();
        snap : KZoneRulesSnapshot* = __k_tzdb_load_zone((const byte*) &u8[0]);
        if (snap == null) {
            throw ZoneRulesUnavailableException(511);
        }
        return snap;
    }

    /**
     * Release a snapshot previously obtained from loadZone.
     */
    static releaseSnapshot(snap: KZoneRulesSnapshot*) {
        if (snap != null) {
            __k_tzdb_snapshot_release(snap);
        }
    }

    /**
     * Return the number of transitions in the given snapshot.
     */
    static transitionCount(snap: const KZoneRulesSnapshot*) : int {
        return __k_tzdb_snapshot_transition_count(snap);
    }

    /**
     * Return the epoch second of the transition at index.
     */
    static transitionTime(snap: const KZoneRulesSnapshot*, index: int) : long {
        return __k_tzdb_snapshot_transition_time(snap, index);
    }

    /**
     * Return the UTC offset in seconds at transition index.
     */
    static transitionOffset(snap: const KZoneRulesSnapshot*, index: int) : int {
        return __k_tzdb_snapshot_transition_offset(snap, index);
    }

    /**
     * Return the abbreviation string at transition index.
     */
    static transitionAbbrev(snap: const KZoneRulesSnapshot*, index: int) : String {
        bytes : unsigned byte[]! = __k_tzdb_snapshot_transition_abbrev(snap, index);
        return String(bytes);
    }

    /**
     * Return true if the transition at index corresponds to daylight saving time.
     */
    static transitionIsDst(snap: const KZoneRulesSnapshot*, index: int) : bool {
        return __k_tzdb_snapshot_transition_is_dst(snap, index) != 0;
    }

    /**
     * Return the effective UTC offset in seconds at epochSecond.
     */
    static offsetAt(snap: const KZoneRulesSnapshot*, epochSecond: long) : int {
        return __k_tzdb_snapshot_offset_at(snap, epochSecond);
    }

    /**
     * Return the abbreviation string effective at epochSecond.
     */
    static abbrevAt(snap: const KZoneRulesSnapshot*, epochSecond: long) : String {
        bytes : unsigned byte[]! = __k_tzdb_snapshot_abbrev_at(snap, epochSecond);
        return String(bytes);
    }

    /**
     * Return true if daylight saving time is active at epochSecond.
     */
    static isDstAt(snap: const KZoneRulesSnapshot*, epochSecond: long) : bool {
        return __k_tzdb_snapshot_is_dst_at(snap, epochSecond) != 0;
    }

    /**
     * Return the POSIX TZ string footer associated with the snapshot, if any.
     */
    static posixTz(snap: const KZoneRulesSnapshot*) : String {
        bytes : unsigned byte[]! = __k_tzdb_snapshot_posix_tz(snap);
        return String(bytes);
    }
}

} // namespace time
