# Math
**Module:** `k::math`  
**Source:** `libk/libkmath/src/math.k`  
**Linking:** Optional — requires `import k::math;`
---
## Overview
The `k::math` module provides basic mathematical utility functions through
the `Math` static final class.  All methods are public and static.
This is an **optional** standard library — it is not linked automatically.
Users must add an explicit `import k::math;` declaration.
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
---
## Usage Example
```k
module myapp;
import k::math;
main() : int {
    x : int = -42;
    a : int = k::math::Math::abs(x);     // 42
    m : int = k::math::Math::min(a, 10); // 10
    c : int = k::math::Math::clamp(a, 0, 100); // 42
    return c;
}
```
