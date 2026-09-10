/*
 * K Language standard library — Local Time Resolution & Policies
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
 * ZoneTransition — represents a discontinuity in local time between two offsets.
 *
 * Models daylight saving time transitions (spring forward gap or autumn fallback overlap)
 * or historical offset changes.
 */
public final class ZoneTransition {
private:
    _instant      : time::Instant;
    _beforeOffset : ZoneOffset;
    _afterOffset  : ZoneOffset;

public:
    ZoneTransition() {
        _instant = time::Instant::epoch();
        _beforeOffset = ZoneOffset::utc();
        _afterOffset = ZoneOffset::utc();
    }

    ZoneTransition(instant: const time::Instant&, beforeOffset: const ZoneOffset&, afterOffset: const ZoneOffset&) {
        _instant = instant;
        _beforeOffset = beforeOffset;
        _afterOffset = afterOffset;
    }

    // ── Accessors ────────────────────────────────────────────────────────────

    const instant() : time::Instant {
        return _instant;
    }

    const beforeOffset() : ZoneOffset {
        return _beforeOffset;
    }

    const afterOffset() : ZoneOffset {
        return _afterOffset;
    }

    /**
     * Return true if this transition represents a gap (spring forward: afterOffset > beforeOffset).
     */
    const isGap() : bool {
        return _afterOffset > _beforeOffset;
    }

    /**
     * Return true if this transition represents an overlap (autumn fallback: beforeOffset > afterOffset).
     */
    const isOverlap() : bool {
        return _beforeOffset > _afterOffset;
    }

    /**
     * Return the positive duration of this transition.
     */
    const duration() : time::Duration {
        diff : int = _afterOffset.totalSeconds() - _beforeOffset.totalSeconds();
        if (diff < 0) {
            diff = -diff;
        }
        return time::Duration::ofSeconds((long) diff);
    }

    /**
     * Return the local date/time immediately before the transition.
     */
    const dateTimeBefore() : LocalDateTime {
        try {
            posixSec : long = _instant.toPosixSeconds();
            secBefore : long = posixSec + (long) _beforeOffset.totalSeconds();
            d : LocalDate = LocalDate::fromEpochDay(EpochDay(floorDiv(secBefore, 86400L)));
            sod : long = floorMod(secBefore, 86400L);
            t : LocalTime = LocalTime::of((int) (sod / 3600L), (int) ((sod % 3600L) / 60L),
                                          (int) (sod % 60L), _instant.nanoAdjustment());
            return LocalDateTime::of(d, t);
        } catch (e: TemporalArithmeticException&) {
            return LocalDateTime();
        } catch (e2: InvalidTemporalValueException&) {
            return LocalDateTime();
        }
    }

    /**
     * Return the local date/time immediately after the transition.
     */
    const dateTimeAfter() : LocalDateTime {
        try {
            posixSec : long = _instant.toPosixSeconds();
            secAfter : long = posixSec + (long) _afterOffset.totalSeconds();
            d : LocalDate = LocalDate::fromEpochDay(EpochDay(floorDiv(secAfter, 86400L)));
            sod : long = floorMod(secAfter, 86400L);
            t : LocalTime = LocalTime::of((int) (sod / 3600L), (int) ((sod % 3600L) / 60L),
                                          (int) (sod % 60L), _instant.nanoAdjustment());
            return LocalDateTime::of(d, t);
        } catch (e: TemporalArithmeticException&) {
            return LocalDateTime();
        } catch (e2: InvalidTemporalValueException&) {
            return LocalDateTime();
        }
    }

    // ── Equality ─────────────────────────────────────────────────────────────

    const operator==(other: const ZoneTransition&) : bool {
        return _instant == other._instant &&
               _beforeOffset == other._beforeOffset &&
               _afterOffset == other._afterOffset;
    }

    const operator!=(other: const ZoneTransition&) : bool {
        return !(this == other);
    }
}

/**
 * ZoneLocalResolution — base polymorphic result of resolving a LocalDateTime against ZoneRules.
 */
public abstract class ZoneLocalResolution {
public:
    const isUnique() : bool { return false; }
    const isGap() : bool { return false; }
    const isOverlap() : bool { return false; }

    const offset() : ZoneOffset { return ZoneOffset::utc(); }
    const instant() : time::Instant { return time::Instant::epoch(); }

    const transition() : ZoneTransition { return ZoneTransition(); }
    const duration() : time::Duration { return time::Duration::zero(); }

    const earlierOffset() : ZoneOffset { return ZoneOffset::utc(); }
    const earlierInstant() : time::Instant { return time::Instant::epoch(); }
    const laterOffset() : ZoneOffset { return ZoneOffset::utc(); }
    const laterInstant() : time::Instant { return time::Instant::epoch(); }
}

/**
 * UniqueResolution — exactly one offset and instant apply to the local time.
 */
public final class UniqueResolution : public ZoneLocalResolution {
private:
    _offset  : ZoneOffset;
    _instant : time::Instant;

public:
    UniqueResolution(offset: const ZoneOffset&, instant: const time::Instant&) {
        _offset = offset;
        _instant = instant;
    }

    override const isUnique() : bool { return true; }
    override const offset() : ZoneOffset { return _offset; }
    override const instant() : time::Instant { return _instant; }
}

/**
 * GapResolution — the local time falls in a gap (clocks jumped forward).
 */
public final class GapResolution : public ZoneLocalResolution {
private:
    _transition : ZoneTransition;

public:
    GapResolution(transition: const ZoneTransition&) {
        _transition = transition;
    }

    override const isGap() : bool { return true; }
    override const transition() : ZoneTransition { return _transition; }
    override const duration() : time::Duration { return _transition.duration(); }
}

/**
 * OverlapResolution — the local time falls in an overlap (clocks repeated).
 * Two valid offsets and instants exist, ordered chronologically.
 */
public final class OverlapResolution : public ZoneLocalResolution {
private:
    _transition     : ZoneTransition;
    _earlierOffset  : ZoneOffset;
    _earlierInstant : time::Instant;
    _laterOffset    : ZoneOffset;
    _laterInstant   : time::Instant;

public:
    OverlapResolution(transition: const ZoneTransition&,
                      earlierOffset: const ZoneOffset&, earlierInstant: const time::Instant&,
                      laterOffset: const ZoneOffset&, laterInstant: const time::Instant&) {
        _transition = transition;
        _earlierOffset = earlierOffset;
        _earlierInstant = earlierInstant;
        _laterOffset = laterOffset;
        _laterInstant = laterInstant;
    }

    override const isOverlap() : bool { return true; }
    override const transition() : ZoneTransition { return _transition; }
    override const earlierOffset() : ZoneOffset { return _earlierOffset; }
    override const earlierInstant() : time::Instant { return _earlierInstant; }
    override const laterOffset() : ZoneOffset { return _laterOffset; }
    override const laterInstant() : time::Instant { return _laterInstant; }
}

// ── Internal helper for local date/time + offset to Instant ───────────────────
private localToInstant(local: const LocalDateTime&, offset: const ZoneOffset&) : time::Instant {
    try {
        ed : EpochDay = local.date().toEpochDay();
        days : long = ed.daysSinceEpoch();
        localSec : long = days * 86400L + (long) local.hour() * 3600L +
                          (long) local.minute() * 60L + (long) local.second();
        posixSec : long = localSec - (long) offset.totalSeconds();
        return time::Instant::fromPosixTimestamp(PosixTimestamp::ofSeconds(posixSec, local.nano()));
    } catch (e: TemporalArithmeticException&) {
        return time::Instant::epoch();
    }
}

/**
 * LocalDateTimeResolver — interface for policies that resolve local time ambiguity.
 */
public interface LocalDateTimeResolver {
    const resolve(local: const LocalDateTime&, resolution: const ZoneLocalResolution&) : time::Instant
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException);
}

/**
 * StrictResolver — rejects both gaps and overlaps with exceptions.
 */
public final class StrictResolver : public LocalDateTimeResolver {
public:
    StrictResolver() {}

    static instance() : StrictResolver& {
        return _strictResolverInstance;
    }

    override const resolve(local: const LocalDateTime&, resolution: const ZoneLocalResolution&) : time::Instant
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException) {
        if (resolution.isUnique()) {
            return resolution.instant();
        }
        if (resolution.isGap()) {
            throw LocalTimeGapException(521);
        }
        if (resolution.isOverlap()) {
            throw LocalTimeOverlapException(522);
        }
        return resolution.instant();
    }
}
private _strictResolverInstance : StrictResolver;

/**
 * EarlierResolver — throws on gap; on overlap selects the earlier instant.
 */
public final class EarlierResolver : public LocalDateTimeResolver {
public:
    EarlierResolver() {}

    static instance() : EarlierResolver& {
        return _earlierResolverInstance;
    }

    override const resolve(local: const LocalDateTime&, resolution: const ZoneLocalResolution&) : time::Instant
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException) {
        if (resolution.isUnique()) {
            return resolution.instant();
        }
        if (resolution.isGap()) {
            throw LocalTimeGapException(521);
        }
        if (resolution.isOverlap()) {
            return resolution.earlierInstant();
        }
        return resolution.instant();
    }
}
private _earlierResolverInstance : EarlierResolver;

/**
 * LaterResolver — throws on gap; on overlap selects the later instant.
 */
public final class LaterResolver : public LocalDateTimeResolver {
public:
    LaterResolver() {}

    static instance() : LaterResolver& {
        return _laterResolverInstance;
    }

    override const resolve(local: const LocalDateTime&, resolution: const ZoneLocalResolution&) : time::Instant
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException) {
        if (resolution.isUnique()) {
            return resolution.instant();
        }
        if (resolution.isGap()) {
            throw LocalTimeGapException(521);
        }
        if (resolution.isOverlap()) {
            return resolution.laterInstant();
        }
        return resolution.instant();
    }
}
private _laterResolverInstance : LaterResolver;

/**
 * PreferOffsetResolver — throws on gap; on overlap selects the matching offset or earlier.
 */
public final class PreferOffsetResolver : public LocalDateTimeResolver {
private:
    _offset : ZoneOffset;

public:
    PreferOffsetResolver(offset: const ZoneOffset&) {
        _offset = offset;
    }

    const preferredOffset() : ZoneOffset {
        return _offset;
    }

    override const resolve(local: const LocalDateTime&, resolution: const ZoneLocalResolution&) : time::Instant
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException) {
        if (resolution.isUnique()) {
            return resolution.instant();
        }
        if (resolution.isGap()) {
            throw LocalTimeGapException(521);
        }
        if (resolution.isOverlap()) {
            if (_offset == resolution.earlierOffset()) {
                return resolution.earlierInstant();
            }
            if (_offset == resolution.laterOffset()) {
                return resolution.laterInstant();
            }
            return resolution.earlierInstant();
        }
        return resolution.instant();
    }
}

/**
 * ShiftForwardResolver — on gap shifts forward by gap duration; on overlap returns earlier (or throws if configured).
 */
public final class ShiftForwardResolver : public LocalDateTimeResolver {
private:
    _overlapThrows : bool;

public:
    ShiftForwardResolver() {
        _overlapThrows = false;
    }

    ShiftForwardResolver(overlapThrows: bool) {
        _overlapThrows = overlapThrows;
    }

    static instance() : ShiftForwardResolver& {
        return _shiftForwardResolverInstance;
    }

    override const resolve(local: const LocalDateTime&, resolution: const ZoneLocalResolution&) : time::Instant
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException) {
        if (resolution.isUnique()) {
            return resolution.instant();
        }
        if (resolution.isGap()) {
            trans : ZoneTransition = resolution.transition();
            return localToInstant(local, trans.beforeOffset());
        }
        if (resolution.isOverlap()) {
            if (_overlapThrows) {
                throw LocalTimeOverlapException(522);
            }
            return resolution.earlierInstant();
        }
        return resolution.instant();
    }
}
private _shiftForwardResolverInstance : ShiftForwardResolver;

/**
 * ShiftBackwardResolver — on gap shifts backward by gap duration; on overlap throws.
 */
public final class ShiftBackwardResolver : public LocalDateTimeResolver {
public:
    ShiftBackwardResolver() {}

    static instance() : ShiftBackwardResolver& {
        return _shiftBackwardResolverInstance;
    }

    override const resolve(local: const LocalDateTime&, resolution: const ZoneLocalResolution&) : time::Instant
        throws(LocalTimeGapException, LocalTimeOverlapException, TemporalArithmeticException) {
        if (resolution.isUnique()) {
            return resolution.instant();
        }
        if (resolution.isGap()) {
            trans : ZoneTransition = resolution.transition();
            return localToInstant(local, trans.afterOffset());
        }
        if (resolution.isOverlap()) {
            throw LocalTimeOverlapException(522);
        }
        return resolution.instant();
    }
}
private _shiftBackwardResolverInstance : ShiftBackwardResolver;

} // namespace time
