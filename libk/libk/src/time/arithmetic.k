/*
 * K Language standard library — checked temporal arithmetic
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

// ── Unit conversion constants ────────────────────────────────────────────────

public const NANOS_PER_MICRO    : long = 1000L;
public const MICROS_PER_MILLI   : long = 1000L;
public const MILLIS_PER_SECOND  : long = 1000L;
public const NANOS_PER_MILLI    : long = 1000000L;
public const NANOS_PER_SECOND   : long = 1000000000L;
public const SECONDS_PER_MINUTE : long = 60L;
public const MINUTES_PER_HOUR   : long = 60L;
public const SECONDS_PER_HOUR   : long = 3600L;
public const HOURS_PER_DAY      : long = 24L;
public const SECONDS_PER_DAY    : long = 86400L;

// ── Checked 64-bit integer arithmetic helpers ────────────────────────────────

/**
 * Add two 64-bit signed integers, throwing TemporalArithmeticException on overflow.
 */
public addChecked(a: long, b: long) : long throws(TemporalArithmeticException) {
    if (b > 0L && a > 9223372036854775807L - b) {
        throw TemporalArithmeticException();
    }
    if (b < 0L && a < (-9223372036854775807L - 1L) - b) {
        throw TemporalArithmeticException();
    }
    return a + b;
}

/**
 * Subtract two 64-bit signed integers, throwing TemporalArithmeticException on overflow.
 */
public subtractChecked(a: long, b: long) : long throws(TemporalArithmeticException) {
    if (b > 0L && a < (-9223372036854775807L - 1L) + b) {
        throw TemporalArithmeticException();
    }
    if (b < 0L && a > 9223372036854775807L + b) {
        throw TemporalArithmeticException();
    }
    return a - b;
}

/**
 * Multiply two 64-bit signed integers, throwing TemporalArithmeticException on overflow.
 */
public multiplyChecked(a: long, b: long) : long throws(TemporalArithmeticException) {
    if (a == 0L || b == 0L) {
        return 0L;
    }
    if (a > 0L) {
        if (b > 0L) {
            if (a > 9223372036854775807L / b) {
                throw TemporalArithmeticException();
            }
        } else {
            if (b < (-9223372036854775807L - 1L) / a) {
                throw TemporalArithmeticException();
            }
        }
    } else {
        if (b > 0L) {
            if (a < (-9223372036854775807L - 1L) / b) {
                throw TemporalArithmeticException();
            }
        } else {
            if (a == (-9223372036854775807L - 1L) || b == (-9223372036854775807L - 1L)) {
                throw TemporalArithmeticException();
            }
            if (-a > 9223372036854775807L / (-b)) {
                throw TemporalArithmeticException();
            }
        }
    }
    return a * b;
}

/**
 * Negate a 64-bit signed integer, throwing TemporalArithmeticException on overflow.
 */
public negateChecked(a: long) : long throws(TemporalArithmeticException) {
    if (a == (-9223372036854775807L - 1L)) {
        throw TemporalArithmeticException();
    }
    return -a;
}

/**
 * Divide two 64-bit signed integers, throwing TemporalArithmeticException on
 * division by zero or overflow (LONG_MIN / -1).
 */
public divideChecked(a: long, b: long) : long throws(TemporalArithmeticException) {
    if (b == 0L) {
        throw TemporalArithmeticException();
    }
    if (a == (-9223372036854775807L - 1L) && b == -1L) {
        throw TemporalArithmeticException();
    }
    return a / b;
}

/**
 * Integer floor division rounding towards negative infinity.
 */
public floorDiv(a: long, b: long) : long {
    if (b == 0L) {
        return 0L;
    }
    if (a == (-9223372036854775807L - 1L) && b == -1L) {
        return (-9223372036854775807L - 1L);
    }
    q : long = a / b;
    r : long = a % b;
    if ((a < 0L != b < 0L) && r != 0L) {
        q = q - 1L;
    }
    return q;
}

/**
 * Integer floor modulo returning floor remainder: a - floorDiv(a, b) * b.
 */
public floorMod(a: long, b: long) : long {
    if (b == 0L) {
        return 0L;
    }
    if (a == (-9223372036854775807L - 1L) && b == -1L) {
        return 0L;
    }
    r : long = a % b;
    if ((a < 0L != b < 0L) && r != 0L) {
        r = r + b;
    }
    return r;
}

// ── CheckedArithmetic static wrapper class ───────────────────────────────────

public final class CheckedArithmetic {
public:
    static addChecked(a: long, b: long) : long throws(TemporalArithmeticException) {
        return time::addChecked(a, b);
    }

    static subtractChecked(a: long, b: long) : long throws(TemporalArithmeticException) {
        return time::subtractChecked(a, b);
    }

    static multiplyChecked(a: long, b: long) : long throws(TemporalArithmeticException) {
        return time::multiplyChecked(a, b);
    }

    static negateChecked(a: long) : long throws(TemporalArithmeticException) {
        return time::negateChecked(a);
    }

    static divideChecked(a: long, b: long) : long throws(TemporalArithmeticException) {
        return time::divideChecked(a, b);
    }

    static floorDiv(a: long, b: long) : long {
        return time::floorDiv(a, b);
    }

    static floorMod(a: long, b: long) : long {
        return time::floorMod(a, b);
    }
}

// ── NormalizedSecondNanos ───────────────────────────────────────────────────

/**
 * Normalized pair of seconds (long) and nanoseconds (int).
 *
 * Invariant: 0 <= nanos < 1_000_000_000.
 */
public struct NormalizedSecondNanos {
public:
    seconds : long;
    nanos   : int;

    NormalizedSecondNanos() {
        seconds = 0L;
        nanos = 0;
    }

    NormalizedSecondNanos(sec: long, n: int) throws(TemporalArithmeticException) {
        ln : long = (long) n;
        if (ln >= 0L && ln < NANOS_PER_SECOND) {
            seconds = sec;
            nanos = n;
        } else {
            secAdj : long = floorDiv(ln, NANOS_PER_SECOND);
            nanoRem : long = floorMod(ln, NANOS_PER_SECOND);
            seconds = addChecked(sec, secAdj);
            nanos = (int) nanoRem;
        }
    }

    NormalizedSecondNanos(sec: long, n: long) throws(TemporalArithmeticException) {
        if (n >= 0L && n < NANOS_PER_SECOND) {
            seconds = sec;
            nanos = (int) n;
        } else {
            secAdj : long = floorDiv(n, NANOS_PER_SECOND);
            nanoRem : long = floorMod(n, NANOS_PER_SECOND);
            seconds = addChecked(sec, secAdj);
            nanos = (int) nanoRem;
        }
    }

    static of(sec: long, n: int) : NormalizedSecondNanos throws(TemporalArithmeticException) {
        return NormalizedSecondNanos(sec, n);
    }

    static of(sec: long, n: long) : NormalizedSecondNanos throws(TemporalArithmeticException) {
        return NormalizedSecondNanos(sec, n);
    }

    const getSeconds() : long {
        return seconds;
    }

    const getNanos() : int {
        return nanos;
    }

    const operator==(other: const NormalizedSecondNanos&) : bool {
        return seconds == other.seconds && nanos == other.nanos;
    }

    const operator!=(other: const NormalizedSecondNanos&) : bool {
        return seconds != other.seconds || nanos != other.nanos;
    }

    const operator<(other: const NormalizedSecondNanos&) : bool {
        if (seconds != other.seconds) {
            return seconds < other.seconds;
        }
        return nanos < other.nanos;
    }

    const operator<=(other: const NormalizedSecondNanos&) : bool {
        if (seconds != other.seconds) {
            return seconds < other.seconds;
        }
        return nanos <= other.nanos;
    }

    const operator>(other: const NormalizedSecondNanos&) : bool {
        if (seconds != other.seconds) {
            return seconds > other.seconds;
        }
        return nanos > other.nanos;
    }

    const operator>=(other: const NormalizedSecondNanos&) : bool {
        if (seconds != other.seconds) {
            return seconds > other.seconds;
        }
        return nanos >= other.nanos;
    }
}

} // namespace time
