/*
 * K Language standard library — ZoneRules
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
 * ZoneRules — public interface for querying time-zone rules and resolving local times.
 */
public interface ZoneRules {
    const version() : String;
    const offsetAt(instant: const time::Instant&) : ZoneOffset;
    const validOffsets(local: const LocalDateTime&) : ZoneOffset[]!;
    const resolveLocal(local: const LocalDateTime&) : ZoneLocalResolution!;
    const transition(local: const LocalDateTime&) : Optional<ZoneTransition>;
}

/**
 * FixedZoneRules — immutable rules for a fixed UTC offset without transitions.
 */
public final class FixedZoneRules : public ZoneRules {
private:
    _offset : ZoneOffset;

public:
    FixedZoneRules() {
        _offset = ZoneOffset::utc();
    }

    FixedZoneRules(offset: const ZoneOffset&) {
        _offset = offset;
    }

    const offset() : ZoneOffset {
        return _offset;
    }

    override const version() : String {
        return String("fixed");
    }

    override const offsetAt(instant: const time::Instant&) : ZoneOffset {
        return _offset;
    }

    override const validOffsets(local: const LocalDateTime&) : ZoneOffset[]! {
        arr : ZoneOffset[]! = new ZoneOffset[1u];
        arr[0u] = _offset;
        return arr;
    }

    override const resolveLocal(local: const LocalDateTime&) : ZoneLocalResolution! {
        inst : time::Instant = localToInstant(local, _offset);
        return new UniqueResolution(_offset, inst);
    }

    override const transition(local: const LocalDateTime&) : Optional<ZoneTransition> {
        return Optional<ZoneTransition>();
    }
}

/**
 * TzdbZoneRules — time zone rules backed by an immutable native KZoneRulesSnapshot.
 */
public final class TzdbZoneRules : public ZoneRules {
private:
    _snap    : KZoneRulesSnapshot* = null;
    _version : String;

public:
    TzdbZoneRules() {
        _snap = null;
        _version = String();
    }

    TzdbZoneRules(snap: KZoneRulesSnapshot*, version: const String&) {
        _snap = snap;
        _version = version;
        if (_snap != null) {
            Tzdb::retainSnapshot(_snap);
        }
    }

    TzdbZoneRules(other: const TzdbZoneRules&) {
        _snap = other._snap;
        _version = other._version;
        if (_snap != null) {
            Tzdb::retainSnapshot(_snap);
        }
    }

    ~TzdbZoneRules() {
        if (_snap != null) {
            Tzdb::releaseSnapshot(_snap);
            _snap = null;
        }
    }

    override const version() : String {
        return _version;
    }

    override const offsetAt(instant: const time::Instant&) : ZoneOffset {
        if (_snap == null) {
            return ZoneOffset::utc();
        }
        posixSec : long = instant.toPosixSeconds();
        offSec : int = Tzdb::offsetAt(_snap, posixSec);
        return ZoneOffset(offSec);
    }

    override const validOffsets(local: const LocalDateTime&) : ZoneOffset[]! {
        res : ZoneLocalResolution! = resolveLocal(local);
        if (res->isGap()) {
            return new ZoneOffset[0u];
        }
        if (res->isOverlap()) {
            arr : ZoneOffset[]! = new ZoneOffset[2u];
            arr[0u] = res->earlierOffset();
            arr[1u] = res->laterOffset();
            return arr;
        }
        arr : ZoneOffset[]! = new ZoneOffset[1u];
        arr[0u] = res->offset();
        return arr;
    }

    override const resolveLocal(local: const LocalDateTime&) : ZoneLocalResolution! {
        if (_snap == null) {
            inst : time::Instant = localToInstant(local, ZoneOffset::utc());
            return new UniqueResolution(ZoneOffset::utc(), inst);
        }

        localSec : long = 0L;
        try {
            ed : EpochDay = local.date().toEpochDay();
            days : long = ed.daysSinceEpoch();
            localSec = days * 86400L + (long) local.hour() * 3600L +
                       (long) local.minute() * 60L + (long) local.second();
        } catch (e: TemporalArithmeticException&) {
            inst : time::Instant = localToInstant(local, ZoneOffset::utc());
            return new UniqueResolution(ZoneOffset::utc(), inst);
        }

        Tzdb::resolveLocal(_snap, localSec);
        kind : int = Tzdb::resKind();

        if (kind == 0) {
            off : int = Tzdb::resBeforeOffset();
            offset : ZoneOffset = ZoneOffset(off);
            inst : time::Instant = localToInstant(local, offset);
            return new UniqueResolution(offset, inst);
        }

        if (kind == 1) {
            beforeOff : int = Tzdb::resBeforeOffset();
            afterOff : int = Tzdb::resAfterOffset();
            transTime : long = Tzdb::resTransTime();
            transInst : time::Instant = time::Instant::fromPosixTimestamp(PosixTimestamp::ofSeconds(transTime));
            trans : ZoneTransition = ZoneTransition(transInst, ZoneOffset(beforeOff), ZoneOffset(afterOff));
            return new GapResolution(trans);
        }

        // Overlap (kind == 2)
        beforeOff : int = Tzdb::resBeforeOffset();
        afterOff : int = Tzdb::resAfterOffset();
        transTime : long = Tzdb::resTransTime();
        transInst : time::Instant = time::Instant::fromPosixTimestamp(PosixTimestamp::ofSeconds(transTime));
        trans : ZoneTransition = ZoneTransition(transInst, ZoneOffset(beforeOff), ZoneOffset(afterOff));
        earlierOff : ZoneOffset = ZoneOffset(beforeOff);
        laterOff : ZoneOffset = ZoneOffset(afterOff);
        earlierInst : time::Instant = localToInstant(local, earlierOff);
        laterInst : time::Instant = localToInstant(local, laterOff);
        return new OverlapResolution(trans, earlierOff, earlierInst, laterOff, laterInst);
    }

    override const transition(local: const LocalDateTime&) : Optional<ZoneTransition> {
        res : ZoneLocalResolution! = resolveLocal(local);
        if (res->isGap() || res->isOverlap()) {
            return Optional<ZoneTransition>(res->transition());
        }
        return Optional<ZoneTransition>();
    }
}

} // namespace time
