# Math
**Module:** `k`  
**Namespace:** `k::math`  
**Source:** `libk/libk/src/math/math.k`  
**Linking:** Automatic — part of the base `k` library
---
## Overview
The `k::math` namespace provides basic mathematical utility functions through
the `Math` static final class.  All methods are public and static.

`k::math` is compiled into the base `k` module (`libk.so`), which is
automatically linked with every K program.  No explicit `import` declaration
is needed — `k::math::Math` is directly accessible in any K compilation unit.

---
## Math
`public final class Math` — static utility methods for common mathematical
operations on integers and longs.
### Static Methods (int)
| Method | Returns | Description |
|--------|---------|-------------|
| `abs(x: int)` | `int` | Absolute value of `x`. |
| `min(a: int, b: int)` | `int` | Smaller of `a` and `b`. |
| `max(a: int, b: int)` | `int` | Larger of `a` and `b`. |
| `clamp(x: int, lo: int, hi: int)` | `int` | `x` clamped to the range `[lo, hi]`. |
### Static Methods (long)
| Method | Returns | Description |
|--------|---------|-------------|
| `absLong(x: long)` | `long` | Absolute value of `x`. |
| `minLong(a: long, b: long)` | `long` | Smaller of `a` and `b`. |
| `maxLong(a: long, b: long)` | `long` | Larger of `a` and `b`. |
| `clampLong(x: long, lo: long, hi: long)` | `long` | `x` clamped to the range `[lo, hi]`. |
---
## Usage Example
```k
module myapp;

main() : int {
    x : int = -42;
    a : int = k::math::Math::abs(x);          // 42
    m : int = k::math::Math::min(a, 10);      // 10
    c : int = k::math::Math::clamp(a, 0, 100); // 42

    y : long = -1000L;
    b : long = k::math::Math::absLong(y);              // 1000
    d : long = k::math::Math::clampLong(b, 0L, 500L);  // 500
    return c;
}
```
