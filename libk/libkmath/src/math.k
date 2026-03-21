/*
 * K Language standard library — math module
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

module k::math;

// ═════════════════════════════════════════════════════════════════════════════
// Math — Static utility class providing basic mathematical functions.
//
// All methods are public and static.  Usage:
//   Math::abs(-5)      → 5
//   Math::min(3, 7)    → 3
//   Math::max(3, 7)    → 7
//   Math::clamp(x, 0, 100)
// ═════════════════════════════════════════════════════════════════════════════
public final class Math {
public:
    /** Return the absolute value of an integer. */
    public static abs(x : int) : int {
        if (x < 0) return -x;
        return x;
    }

    /** Return the smaller of two integers. */
    public static min(a : int, b : int) : int {
        if (a <= b) return a;
        return b;
    }

    /** Return the larger of two integers. */
    public static max(a : int, b : int) : int {
        if (a >= b) return a;
        return b;
    }

    /** Clamp a value between a minimum and a maximum. */
    public static clamp(x : int, lo : int, hi : int) : int {
        if (x < lo) return lo;
        if (x > hi) return hi;
        return x;
    }

    /** Return the absolute value of a long. */
    public static absLong(x : long) : long {
        if (x < 0) return -x;
        return x;
    }

    /** Return the smaller of two longs. */
    public static minLong(a : long, b : long) : long {
        if (a <= b) return a;
        return b;
    }

    /** Return the larger of two longs. */
    public static maxLong(a : long, b : long) : long {
        if (a >= b) return a;
        return b;
    }

    /** Clamp a long value between a minimum and a maximum. */
    public static clampLong(x : long, lo : long, hi : long) : long {
        if (x < lo) return lo;
        if (x > hi) return hi;
        return x;
    }
}


