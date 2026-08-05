/*
 * K Language standard library — time primitives
 *
 * Copyright 2023-2026 Emilien Kia
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

// ── FFI declarations for time helpers ───────────────────────────────────────
@ffi::Extern("C") private __k_time_realtime_nanos() : long;

// ═════════════════════════════════════════════════════════════════════════════
// Duration — a signed time span expressed as nanoseconds.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Duration — an immutable time span.
 *
 * Stored internally as a signed 64-bit nanosecond count.
 * Positive values represent a forward time span; negative values are valid
 * but most runtime APIs treat them as zero (immediate / no-wait).
 */
public struct Duration {
private:
    _nanos : long;

public:
    // ── Construction ─────────────────────────────────────────────────────────

    /** Construct a zero-length Duration. */
    Duration() {
        _nanos = 0;
    }

    /**
     * Construct a Duration from a nanosecond count (internal use / FFI).
     * @param nanos Signed nanosecond count.
     */
    Duration(nanos: long) {
        _nanos = nanos;
    }

    // ── Factory methods ──────────────────────────────────────────────────────

    /** Return a Duration of exactly zero. */
    static zero() : Duration {
        return Duration(0L);
    }

    /**
     * Return a Duration of the given number of nanoseconds.
     * @param n Nanoseconds.
     */
    static ofNanos(n: long) : Duration {
        return Duration(n);
    }

    /**
     * Return a Duration of the given number of milliseconds.
     * @param ms Milliseconds.
     */
    static ofMillis(ms: long) : Duration {
        return Duration(ms * 1000000L);
    }

    /**
     * Return a Duration of the given number of seconds.
     * @param s Seconds.
     */
    static ofSeconds(s: long) : Duration {
        return Duration(s * 1000000000L);
    }

    /**
     * Return a Duration of the given number of minutes.
     * @param m Minutes.
     */
    static ofMinutes(m: long) : Duration {
        return Duration(m * 60000000000L);
    }

    // ── Accessors ────────────────────────────────────────────────────────────

    /** Return the total duration in nanoseconds. */
    const toNanos() : long {
        return _nanos;
    }

    /** Return the total duration in milliseconds (truncated). */
    const toMillis() : long {
        return _nanos / 1000000L;
    }

    /** Return the total duration in seconds (truncated). */
    const toSeconds() : long {
        return _nanos / 1000000000L;
    }

    /** Return true if this duration is negative or zero. */
    const isNegativeOrZero() : bool {
        return _nanos <= 0L;
    }

    /** Return true if this duration is positive (> 0). */
    const isPositive() : bool {
        return _nanos > 0L;
    }

    // ── Operators ────────────────────────────────────────────────────────────

    /** Add two durations. */
    const operator+(other: const Duration&) : Duration {
        return Duration(_nanos + other._nanos);
    }

    /** Subtract two durations. */
    const operator-(other: const Duration&) : Duration {
        return Duration(_nanos - other._nanos);
    }

    /** Compare two durations for equality. */
    const operator==(other: const Duration&) : bool {
        return _nanos == other._nanos;
    }

    /** Compare two durations — less-than. */
    const operator<(other: const Duration&) : bool {
        return _nanos < other._nanos;
    }

    /** Compare two durations — greater-than. */
    const operator>(other: const Duration&) : bool {
        return _nanos > other._nanos;
    }

    /** Compare two durations — less-than-or-equal. */
    const operator<=(other: const Duration&) : bool {
        return _nanos <= other._nanos;
    }

    /** Compare two durations — greater-than-or-equal. */
    const operator>=(other: const Duration&) : bool {
        return _nanos >= other._nanos;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Instant — a point in time on the monotonic clock.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Instant — an immutable point in time on the monotonic clock.
 *
 * Based on CLOCK_MONOTONIC. Suitable for elapsed-time and deadline
 * calculations; not suitable for wall-clock time or calendar operations.
 */
public struct Instant {
private:
    _epochNanos : long;  // nanoseconds since an unspecified monotonic epoch
    @ffi::Extern("C") private static __k_time_monotonic_nanos() : long;

public:
    // ── Construction ─────────────────────────────────────────────────────────

    /** Construct the epoch instant (time zero). */
    Instant() {
        _epochNanos = 0L;
    }

    /**
     * Internal constructor from raw nanosecond value.
     * @param epochNanos Nanoseconds on the monotonic clock.
     */
    Instant(epochNanos: long) {
        _epochNanos = epochNanos;
    }

    // ── Factory methods ──────────────────────────────────────────────────────

    /**
     * Return the current instant on the monotonic clock.
     * Backed by CLOCK_MONOTONIC via FFI.
     */
    static now() : Instant {
        return Instant(__k_time_monotonic_nanos());
    }

    // ── Arithmetic ───────────────────────────────────────────────────────────

    /**
     * Return an instant advanced by the given duration.
     * @param d Duration to add.
     */
    const plus(d: const Duration&) : Instant {
        return Instant(_epochNanos + d.toNanos());
    }

    /**
     * Return the duration between this instant and an earlier one.
     * If other is after this, the result is negative.
     * @param other The earlier instant.
     */
    const minus(other: const Instant&) : Duration {
        return Duration(_epochNanos - other._epochNanos);
    }

    // ── Comparisons ──────────────────────────────────────────────────────────

    /** Return true if this instant is strictly before other. */
    const isBefore(other: const Instant&) : bool {
        return _epochNanos < other._epochNanos;
    }

    /** Return true if this instant is strictly after other. */
    const isAfter(other: const Instant&) : bool {
        return _epochNanos > other._epochNanos;
    }

    /** Return the raw nanosecond value on the monotonic clock. */
    const toEpochNanos() : long {
        return _epochNanos;
    }

    const operator==(other: const Instant&) : bool {
        return _epochNanos == other._epochNanos;
    }

    const operator<(other: const Instant&) : bool {
        return _epochNanos < other._epochNanos;
    }

    const operator>(other: const Instant&) : bool {
        return _epochNanos > other._epochNanos;
    }
}
