/*
 * K Language standard library — Clock, FixedClock, SequenceClock, SystemClock
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

// ── Private FFI declarations for platform clock runtime ──────────────────────
@ffi::Extern("C") private __k_time_clock_read_realtime() : int;
@ffi::Extern("C") private __k_time_clock_read_monotonic_active() : int;
@ffi::Extern("C") private __k_time_clock_read_monotonic_elapsed() : int;
@ffi::Extern("C") private __k_time_clock_read_resolution(clock_type: int) : int;
@ffi::Extern("C") private __k_time_clock_get_reading_sec() : long;
@ffi::Extern("C") private __k_time_clock_get_reading_nano() : int;
@ffi::Extern("C") private __k_time_clock_map_status(status: int) : int;

/**
 * Injectable contract for absolute time sources.
 */
public interface Clock {
    /**
     * Obtain the current Instant from this clock.
     */
    now() : time::Instant throws(TimeScaleDataUnavailableException, TimeDataUnavailableException);

    /**
     * Obtain the resolution of this clock.
     */
    resolution() : Duration throws(TimeDataUnavailableException);
}

/**
 * Deterministic test clock returning a fixed Instant.
 */
public final class FixedClock : public Clock {
private:
    _instant : time::Instant;

public:
    FixedClock(instant: const time::Instant&) {
        _instant = instant;
    }

    override now() : time::Instant {
        return _instant;
    }

    override resolution() : Duration {
        return Duration::ofNanos(1L);
    }
}

private struct SequenceNode {
public:
    instant : time::Instant;
    next    : SequenceNode! = null;

    SequenceNode(inst: const time::Instant&) {
        instant = inst;
    }
}

/**
 * Deterministic test clock returning a sequence of Instants.
 *
 * When the sequence is exhausted, throws TimeDataUnavailableException(515).
 */
public final class SequenceClock : public Clock {
private:
    _head      : SequenceNode! = null;
    _tail      : SequenceNode* = null;
    _remaining : int;

public:
    SequenceClock(values: const time::Instant[]&) {
        _remaining = 0;
        n : unsigned int = values.size;
        if (n == 0u) {
            return;
        }
        _remaining = (int) n;
        i : unsigned int = 0u;
        while (i < n) {
            node : SequenceNode! = new SequenceNode(values[i]);
            if (_tail != null) {
                _tail->next = node;
                _tail = _tail->next;
            } else {
                _tail = node;
                _head = node;
            }
            ++i;
        }
    }

    override now() : time::Instant throws(TimeDataUnavailableException) {
        if (_head == null) {
            throw TimeDataUnavailableException(515);
        }
        inst : time::Instant = _head.instant;
        next : SequenceNode! = _head.next;
        _head = next;
        if (_head == null) {
            _tail = null;
        }
        _remaining = _remaining - 1;
        return inst;
    }

    override resolution() : Duration {
        return Duration::ofNanos(1L);
    }

    const remaining() : int {
        return _remaining;
    }
}

private _systemClockInstance : SystemClock;

/**
 * System wall-clock facade.
 *
 * In Phase 2, now() throws TimeScaleDataUnavailableException(513) because
 * UTC leap-second history and POSIX mapping are not yet loaded.
 * Phase 3 enables SystemClock.now().
 */
public final class SystemClock : public Clock {
public:
    SystemClock() {}

    static instance() : SystemClock& {
        return _systemClockInstance;
    }

    override now() : time::Instant throws(TimeScaleDataUnavailableException, TimeDataUnavailableException) {
        posix : PosixTimestamp = PosixTimestamp::systemNow();
        return Instant::fromPosixTimestamp(posix);
    }

    override resolution() : time::Duration throws(TimeDataUnavailableException) {
        status : int = __k_time_clock_read_resolution(0);
        if (status != 0) {
            code : int = __k_time_clock_map_status(status);
            throw TimeDataUnavailableException(code);
        }
        sec : long = __k_time_clock_get_reading_sec();
        nano : int = __k_time_clock_get_reading_nano();
        return time::Duration(sec, nano);
    }
}

} // namespace time
