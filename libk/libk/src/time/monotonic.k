/*
 * K Language standard library — Monotonic clocks and MonotonicInstant
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
 * Identifier of a monotonic clock domain.
 *
 * MonotonicInstants from different clock domains are non-comparable.
 */
@lang::NoArgConstructor
@lang::AllArgsConstructor
public final struct MonotonicClockId {
private:
    _id : int = 0;

public:
    const id() : int {
        return _id;
    }

    const operator==(other: const MonotonicClockId&) : bool {
        return _id == other._id;
    }

    const operator!=(other: const MonotonicClockId&) : bool {
        return _id != other._id;
    }
}

/**
 * Immutable point in time within a specific monotonic clock domain.
 *
 * Relational operations and subtraction across different clock domains
 * throw InvalidTemporalValueException.
 * Invariant: 0 <= nanoAdjustment < 1_000_000_000.
 */
@lang::NoArgConstructor
@lang::AllArgsConstructor
public final struct MonotonicInstant {
private:
    _clockId : MonotonicClockId;
    _seconds : long = 0L;
    _nanos   : int = 0;

public:
    static of(clockId: const MonotonicClockId&, seconds: long, nanos: int) : MonotonicInstant
        throws(TemporalArithmeticException) {
        norm : NormalizedSecondNanos = NormalizedSecondNanos(seconds, nanos);
        return MonotonicInstant(clockId, norm.seconds, norm.nanos);
    }

    const clockId() : MonotonicClockId {
        return _clockId;
    }

    const secondsPart() : long {
        return _seconds;
    }

    const nanoAdjustment() : int {
        return _nanos;
    }

    const until(other: const MonotonicInstant&) : Duration
        throws(InvalidTemporalValueException, TemporalArithmeticException) {
        if (_clockId != other._clockId) {
            throw InvalidTemporalValueException(501);
        }
        sec : long = subtractChecked(other._seconds, _seconds);
        nanosDiff : int = other._nanos - _nanos;
        if (nanosDiff < 0) {
            sec = subtractChecked(sec, 1L);
            nanosDiff = nanosDiff + 1000000000;
        }
        return Duration(sec, nanosDiff);
    }

    const minus(other: const MonotonicInstant&) : Duration
        throws(InvalidTemporalValueException, TemporalArithmeticException) {
        if (_clockId != other._clockId) {
            throw InvalidTemporalValueException(501);
        }
        sec : long = subtractChecked(_seconds, other._seconds);
        nanosDiff : int = _nanos - other._nanos;
        if (nanosDiff < 0) {
            sec = subtractChecked(sec, 1L);
            nanosDiff = nanosDiff + 1000000000;
        }
        return Duration(sec, nanosDiff);
    }

    const plus(duration: const Duration&) : MonotonicInstant
        throws(TemporalArithmeticException) {
        sec : long = addChecked(_seconds, duration.secondsPart());
        nanosSum : int = _nanos + duration.nanoAdjustment();
        if (nanosSum >= 1000000000) {
            sec = addChecked(sec, 1L);
            nanosSum = nanosSum - 1000000000;
        }
        return MonotonicInstant(_clockId, sec, nanosSum);
    }

    const minus(duration: const Duration&) : MonotonicInstant
        throws(TemporalArithmeticException) {
        sec : long = subtractChecked(_seconds, duration.secondsPart());
        nanosDiff : int = _nanos - duration.nanoAdjustment();
        if (nanosDiff < 0) {
            sec = subtractChecked(sec, 1L);
            nanosDiff = nanosDiff + 1000000000;
        }
        return MonotonicInstant(_clockId, sec, nanosDiff);
    }

    const compareTo(other: const MonotonicInstant&) : int
        throws(InvalidTemporalValueException) {
        if (_clockId != other._clockId) {
            throw InvalidTemporalValueException(501);
        }
        if (_seconds < other._seconds) return -1;
        if (_seconds > other._seconds) return 1;
        if (_nanos < other._nanos) return -1;
        if (_nanos > other._nanos) return 1;
        return 0;
    }

    const operator-(other: const MonotonicInstant&) : Duration
        throws(InvalidTemporalValueException, TemporalArithmeticException) {
        return minus(other);
    }

    const operator+(duration: const Duration&) : MonotonicInstant
        throws(TemporalArithmeticException) {
        return plus(duration);
    }

    const operator-(duration: const Duration&) : MonotonicInstant
        throws(TemporalArithmeticException) {
        return minus(duration);
    }

    const operator==(other: const MonotonicInstant&) : bool {
        if (_clockId != other._clockId) {
            return false;
        }
        return _seconds == other._seconds && _nanos == other._nanos;
    }

    const operator!=(other: const MonotonicInstant&) : bool {
        if (_clockId != other._clockId) {
            return true;
        }
        return _seconds != other._seconds || _nanos != other._nanos;
    }

    const operator<(other: const MonotonicInstant&) : bool
        throws(InvalidTemporalValueException) {
        if (_clockId != other._clockId) {
            throw InvalidTemporalValueException(501);
        }
        if (_seconds != other._seconds) return _seconds < other._seconds;
        return _nanos < other._nanos;
    }

    const operator<=(other: const MonotonicInstant&) : bool
        throws(InvalidTemporalValueException) {
        if (_clockId != other._clockId) {
            throw InvalidTemporalValueException(501);
        }
        if (_seconds != other._seconds) return _seconds < other._seconds;
        return _nanos <= other._nanos;
    }

    const operator>(other: const MonotonicInstant&) : bool
        throws(InvalidTemporalValueException) {
        if (_clockId != other._clockId) {
            throw InvalidTemporalValueException(501);
        }
        if (_seconds != other._seconds) return _seconds > other._seconds;
        return _nanos > other._nanos;
    }

    const operator>=(other: const MonotonicInstant&) : bool
        throws(InvalidTemporalValueException) {
        if (_clockId != other._clockId) {
            throw InvalidTemporalValueException(501);
        }
        if (_seconds != other._seconds) return _seconds > other._seconds;
        return _nanos >= other._nanos;
    }
}

/**
 * Injectable contract for monotonic clock sources.
 */
public interface MonotonicClock {
    id() : MonotonicClockId;
    now() : MonotonicInstant throws(TimeDataUnavailableException);
    resolution() : Duration throws(TimeDataUnavailableException);
    includesSuspend() : bool;
}

/**
 * Deterministic test monotonic clock.
 */
public final class FixedMonotonicClock : public MonotonicClock {
private:
    _id      : MonotonicClockId;
    _current : MonotonicInstant;

public:
    FixedMonotonicClock(id: const MonotonicClockId&, initial: const MonotonicInstant&) {
        _id = id;
        _current = initial;
    }

    override id() : MonotonicClockId {
        return _id;
    }

    override now() : MonotonicInstant {
        return _current;
    }

    override resolution() : Duration {
        return Duration::ofNanos(1L);
    }

    override includesSuspend() : bool {
        return false;
    }

    set(instant: const MonotonicInstant&) : void throws(InvalidTemporalValueException) {
        if (instant.clockId() != _id) {
            throw InvalidTemporalValueException(501);
        }
        _current = instant;
    }

    advance(duration: const Duration&) : void throws(TemporalArithmeticException) {
        _current = _current + duration;
    }
}

private _activeClockInstance : SystemMonotonicClock(MonotonicClockId(1), false);
private _elapsedClockInstance : SystemMonotonicClock(MonotonicClockId(2), true);

/**
 * System monotonic clock implementation backed by native platform clocks.
 */
public final class SystemMonotonicClock : public MonotonicClock {
private:
    _id              : MonotonicClockId;
    _includesSuspend : bool;

public:
    SystemMonotonicClock(id: const MonotonicClockId&, includesSuspend: bool) {
        _id = id;
        _includesSuspend = includesSuspend;
    }

    static activeTime() : SystemMonotonicClock& throws(TimeDataUnavailableException) {
        return _activeClockInstance;
    }

    static elapsedTime() : SystemMonotonicClock& throws(TimeDataUnavailableException) {
        return _elapsedClockInstance;
    }

    override id() : MonotonicClockId {
        return _id;
    }

    override includesSuspend() : bool {
        return _includesSuspend;
    }

    override now() : MonotonicInstant throws(TimeDataUnavailableException) {
        status : int = 0;
        if (_includesSuspend) {
            status = __k_time_clock_read_monotonic_elapsed();
        } else {
            status = __k_time_clock_read_monotonic_active();
        }
        if (status != 0) {
            code : int = __k_time_clock_map_status(status);
            throw TimeDataUnavailableException(code);
        }
        sec : long = __k_time_clock_get_reading_sec();
        nano : int = __k_time_clock_get_reading_nano();
        return MonotonicInstant(_id, sec, nano);
    }

    override resolution() : Duration throws(TimeDataUnavailableException) {
        clockType : int = 1;
        if (_includesSuspend) {
            clockType = 2;
        }
        status : int = __k_time_clock_read_resolution(clockType);
        if (status != 0) {
            code : int = __k_time_clock_map_status(status);
            throw TimeDataUnavailableException(code);
        }
        sec : long = __k_time_clock_get_reading_sec();
        nano : int = __k_time_clock_get_reading_nano();
        return Duration(sec, nano);
    }
}

} // namespace time
