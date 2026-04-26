# Function Overloading

[← Index](../index.md) · [Functions](functions.md)

*Function overloading* allows multiple functions to share the same name, differentiated by their parameter types.

---

## Contents
1. [Overload declaration](#1-overload-declaration)
2. [Overload resolution rules](#2-overload-resolution-rules)
3. [Implicit conversions in overload resolution](#3-implicit-conversions-in-overload-resolution)
4. [Constructor overloading](#4-constructor-overloading)
5. [Examples](#5-examples)
---
## 1. Overload declaration
Two or more functions with the same name in the same scope are overloads if they differ in the number or types of their parameters.
```k
module myapp;
process(x: int) : int { return x + 1; }
process(x: double) : double { return x + 1.0d; }
process(x: int, y: int) : int { return x + y; }
```
**Constraints:**
- Functions that differ only in return type cannot be overloaded.
- All overloads of a name must be visible in the same scope.
---
## 2. Overload resolution rules
When a function call is encountered, the compiler selects the *best matching* overload:
1. **Exact match:** argument types match parameter types exactly.
2. **After implicit conversion:** argument types match after applying a widening or narrowing conversion.
3. If no unique best match can be found, the call is ambiguous and a compile error is reported.
The resolution applies to:
- Free function calls.
- Member function calls (considering the implicit `this` argument as well).
- Constructor calls.
---
## 3. Implicit conversions in overload resolution
When no exact match exists, the compiler considers implicit conversions:
- **Widening** (preferred): e.g., `short` argument matched to `int` parameter.
- **Narrowing** (accepted): e.g., `int` argument matched to `short` parameter.
- **Varargs packing** (lowest priority): when a varargs overload is the only match.

See [Types — Implicit conversions](../basic/types.md#7-implicit-conversions) for the full list.

### Overload priority order

| Priority | Category | Example |
|----------|----------|---------|
| 1 (best) | Exact match | `int` → `int` |
| 2 | Reference conversion | `T&` → `const T&` |
| 3 | Widening | `short` → `int` |
| 4 | Narrowing | `int` → `short` |
| 5 | Construction | `int` → `Wrapper(int)` |
| 6 (worst) | Varargs packing | `int, int, int` → `int[]` via varargs |

Non-varargs overloads are always preferred over varargs overloads when both match:

```k
fun pick(a: int, b: int) : int { return 1; }
fun pick(args... : int) : int  { return 2; }

pick(10, 20);       // calls pick(int, int) → 1 (exact match preferred)
pick(10, 20, 30);   // calls pick(args... : int) → 2 (only varargs matches)
```
**Example:**
```k
add(a: int, b: int) : int { return a + b; }
test() : int {
    s : short = 10s;
    return add(s, 32);   // 's' (short) is widened to int
}
```
---
## 4. Constructor overloading
Constructors are overloaded the same way as regular functions.  
The compiler selects the constructor based on the argument types in the construction expression.
```k
struct plop {
    a : int = 1;
    plop(c: int) {
        a = 3;
    }
    plop(d: double) {
        a = 5;
    }
}
test_int() : int {
    p : plop(2);       // calls plop(int)  → a == 3
    return p.a;
}
test_double() : int {
    p : plop(2.0d);    // calls plop(double) → a == 5
    return p.a;
}
```
---
## 5. Examples
### Free function overloads
```k
module demo;
describe(x: int) : int    { return x; }
describe(x: double) : double { return x; }
test() {
    describe(42);       // calls describe(int)
    describe(3.14d);    // calls describe(double)
}
```
### Member function overloads
```k
struct Printer {
    print(x: int)    { /* print int */ }
    print(x: double) { /* print double */ }
    print(x: char)   { /* print char */ }
}
```
---
*See also:* [Functions](functions.md) · [Types — Implicit conversions](../basic/types.md#7-implicit-conversions) · [Constructors](../structs/constructors.md)
