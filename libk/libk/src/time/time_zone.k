/*
 * K Language standard library — TimeZone
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
 * TimeZone — immutable pairing of a ZoneId and its ZoneRules.
 *
 * Represents an identifiable time zone location backed by an explicit rules snapshot.
 * Equality compares both ZoneId and the effective rules/database version.
 */
public final class TimeZone {
private:
    _id         : ZoneId;
    _version    : String;
    _isFixed    : bool;
    _fixedRules : FixedZoneRules;
    _tzdbRules  : TzdbZoneRules;

    TimeZone(id: const ZoneId&, isFixed: bool, fixedRules: const FixedZoneRules&, tzdbRules: const TzdbZoneRules&, version: const String&) {
        _id = id;
        _isFixed = isFixed;
        _fixedRules = fixedRules;
        _tzdbRules = tzdbRules;
        _version = version;
    }

public:
    TimeZone() {
        _id = ZoneId();
        _isFixed = true;
        _fixedRules = FixedZoneRules(ZoneOffset::utc());
        _tzdbRules = TzdbZoneRules();
        _version = String("fixed");
    }

    TimeZone(other: const TimeZone&) {
        _id = other._id;
        _isFixed = other._isFixed;
        _fixedRules = other._fixedRules;
        _tzdbRules = other._tzdbRules;
        _version = other._version;
    }

    // ── Factories ────────────────────────────────────────────────────────────

    /**
     * Obtain a TimeZone for the given ZoneId from the active system TZDB database.
     * Throws ZoneRulesUnavailableException(511) if rules cannot be found or loaded.
     */
    static of(id: const ZoneId&) : TimeZone throws(ZoneRulesUnavailableException) {
        snap : KZoneRulesSnapshot* = Tzdb::loadZone(id);
        ver : String = Tzdb::version();
        rules : TzdbZoneRules = TzdbZoneRules(snap, ver);
        Tzdb::releaseSnapshot(snap);
        return TimeZone(id, false, FixedZoneRules(), rules, ver);
    }

    /**
     * Obtain a TimeZone for the given ZoneId and explicit rules version.
     * Throws ZoneRulesUnavailableException(511) if the requested version is not active.
     */
    static of(id: const ZoneId&, version: const String&) : TimeZone throws(ZoneRulesUnavailableException) {
        currentVer : String = Tzdb::version();
        if (version != currentVer) {
            throw ZoneRulesUnavailableException(511);
        }
        return of(id);
    }

    /**
     * Obtain a TimeZone with a fixed UTC offset and no daylight saving transitions.
     */
    static fixed(offset: const ZoneOffset&) : TimeZone {
        totSec : int = offset.totalSeconds();
        name : String;
        if (totSec == 0) {
            name = String("UTC");
        } else {
            sb : StringBuilder;
            sb.append("UTC");
            mag : int = totSec;
            if (mag < 0) {
                sb.appendChar('-');
                mag = -mag;
            } else {
                sb.appendChar('+');
            }
            h : int = mag / 3600;
            m : int = (mag % 3600) / 60;
            IsoWriter::appendDigits(sb, (long) h, 1);
            if (m != 0) {
                IsoWriter::appendDigits(sb, (long) m, 2);
            }
            name = sb.toString();
        }

        try {
            zid : ZoneId = ZoneId::of(name);
            fixedRules : FixedZoneRules = FixedZoneRules(offset);
            return TimeZone(zid, true, fixedRules, TzdbZoneRules(), String("fixed"));
        } catch (e: InvalidTemporalValueException&) {
            return TimeZone();
        }
    }

    /**
     * Identify and return the host system's configured time zone.
     * Throws SystemTimeZoneUnavailableException(512) if the local zone cannot be identified,
     * or ZoneRulesUnavailableException(511) if rules for the identified zone cannot be loaded.
     */
    static system() : TimeZone throws(SystemTimeZoneUnavailableException, ZoneRulesUnavailableException) {
        name : String = Tzdb::detectSystemZone();
        if (name.empty()) {
            throw SystemTimeZoneUnavailableException(512);
        }
        zid : ZoneId;
        try {
            zid = ZoneId::of(name);
        } catch (e: InvalidTemporalValueException&) {
            throw SystemTimeZoneUnavailableException(512);
        }
        return of(zid);
    }

    // ── Accessors ────────────────────────────────────────────────────────────

    const id() : ZoneId {
        return _id;
    }

    const version() : String {
        return _version;
    }

    const rules() : ZoneRules& {
        if (_isFixed) {
            return (ZoneRules&) _fixedRules;
        }
        return (ZoneRules&) _tzdbRules;
    }

    // ── Equality ─────────────────────────────────────────────────────────────

    const operator==(other: const TimeZone&) : bool {
        return _id == other._id && _version == other._version;
    }

    const operator!=(other: const TimeZone&) : bool {
        return !(this == other);
    }
}

} // namespace time
