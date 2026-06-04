# Temporary Object Construction

[← Index](../index.md) · [Expressions](expressions.md)

A *temporary construction expression* creates an anonymous, stack-allocated object
within an expression context.  The temporary is constructed at the point of evaluation,
usable for the remainder of the enclosing full expression, and destroyed at the end of the
statement.

---

## Contents

1. [Overview](#1-overview)
2. [Grammar](#2-grammar)
3. [Semantics](#3-semantics)
4. [Lifetime and destruction order](#4-lifetime-and-destruction-order)
5. [Constructor resolution](#5-constructor-resolution)
6. [Use as function argument](#6-use-as-function-argument)
7. [Method chaining on temporaries](#7-method-chaining-on-temporaries)
8. [Interaction with `return`](#8-interaction-with-return)
9. [Interaction with static constructors](#9-interaction-with-static-constructors)
10. [Restrictions](#10-restrictions)
11. [Examples](#11-examples)

---

## 1. Overview

In K, naming a type followed by a constructor argument list inside an expression creates a
**temporary object** of that type.  This mirrors the C++ syntax for functional-style
temporaries:

```k
TypeName(args…)
```

The temporary is:
- allocated on the local stack (like a local variable),
- constructed by calling the appropriate instance constructor,
- usable for the rest of the full expression (field access, method calls, passed as argument),
- destroyed at the end of the enclosing full expression statement, in reverse construction order.

The result type of the expression is a reference to the constructed struct type (`T&`).

---

## 2. Grammar

Temporary construction supports two forms:

```
TemporaryConstructionExpr:
    QualifiedTypeName '(' [ ExpressionList ] ')'
  | QualifiedTypeName BraceInitList
  | QualifiedTypeName '[' ']' BraceInitList
```

---

## 3. Semantics

When the compiler encounters a temporary construction expression in an expression context:

1. **Allocation** — a stack slot is allocated for the temporary in the current function's
   entry block (like a local variable, but unnamed).
2. **Zero-initialisation** — the slot is zero-initialised.
3. **Construction** —
   - `T(args...)`: constructor overload resolution on `T`;
   - `T{...}`: brace-initialization of `T` (designated or positional);
   - `T[]{...}`: temporary sized array of element type `T` with inferred size.
4. **Expression result** —
   - object temporary: reference (`T&`);
   - array temporary: reference to inferred sized array (`T[N]&`).
5. **Destruction registration** — if the temporary requires cleanup, it is registered for
   cleanup at the end of the enclosing full expression statement.

---

## 4. Lifetime and destruction order

All temporaries created within a single full expression statement share the same lifetime:
they survive until the semicolon `;` (end of the expression statement), at which point they
are destroyed in **reverse construction order**.

```k
g_log : int;

struct Obj {
    id : int;
    Obj(v: int) : id(v) { g_log = g_log * 10 + id; }
    ~Obj()              { g_log = g_log * 10 + id; }
    get() : int         { return id; }
}

test() : int {
    g_log = 0;
    sum : int = Obj(1).get() + Obj(2).get() + Obj(3).get();
    // Construction order: Obj(1), Obj(2), Obj(3) → g_log = 123
    // Destruction  order: Obj(3), Obj(2), Obj(1) → g_log = 123321
    return sum;    // 1 + 2 + 3 = 6
}
```

The same rule applies to temporaries created in control-flow conditions (`if`, `while`,
`for`): they are destroyed after the condition is evaluated, before the controlled body
executes.

---

## 5. Constructor resolution

The temporary construction uses the same overload resolution algorithm as variable
declaration with constructor arguments:

1. All **instance constructors** of the struct are considered (including compiler-generated
   and `-> default` constructors).
2. Constructors marked `-> delete` participate in overload resolution.  If a deleted
   constructor is the best match, a compilation error is emitted.
3. **Static constructors** (class initializers) are **not** considered — they are not
   instance constructors (see §9).

```k
struct Point {
    x : int;
    y : int;
    Point(px: int, py: int) : x(px), y(py) {}
}

test() : int {
    return Point(3, 4).x;   // calls Point(int, int) → 3
}
```

If no matching constructor is found, the compiler emits:
> *"No matching constructor found for temporary construction of 'T' with N argument(s)"*

---

## 6. Use as function argument

A temporary can be passed directly as a function argument.  This is useful for constructing
a value inline without declaring a named local variable:

```k
struct Vec2 {
    x : int;
    y : int;
    Vec2(px: int, py: int) : x(px), y(py) {}
}

manhattan(v: Vec2&) : int {
    return v.x + v.y;
}

test() : int {
    return manhattan(Vec2(3, 4));   // → 7
}
```

The temporary `Vec2(3, 4)` is alive for the entire statement, including during the execution
of `manhattan`.

---

## 7. Method chaining on temporaries

Since the result of a temporary construction is a reference, methods can be called on it
directly, and the return values can themselves be temporaries that support further chaining:

```k
struct Builder {
    n : int;
    Builder(v: int) : n(v) {}
    add(x: int) : Builder {
        r : Builder(n + x);
        return r;
    }
    get() : int { return n; }
}

test() : int {
    return Builder(1).add(10).add(100).get();
    // Builder(1) → temporary #1
    // .add(10)   → temporary #2 (returned by add)
    // .add(100)  → temporary #3 (returned by add)
    // .get()     → 111
    // Destruction: #3, #2, #1 (reverse order)
}
```

---

## 8. Interaction with `return`

A temporary construction expression may appear in a `return` statement:

```k
make(v: int) : Obj {
    return Obj(v);
}
```

The compiler applies the standard sret (struct-return) optimisation: when the sole return
expression is a temporary construction, the object may be constructed directly into the
caller's return-value storage, avoiding an extra copy.

When a `return` statement contains a temporary construction as part of a larger expression,
standard destruction rules apply — other temporaries in the expression are destroyed before
the function returns:

```k
test() : int {
    return Obj(1).get();
    // Obj(1) temporary is destroyed after get() returns,
    // before the function returns to the caller.
}
```

---

## 9. Interaction with static constructors

Static constructors (class initializers) are entirely separate from instance constructors.
They:

- are called automatically at program startup,
- **cannot** be called explicitly,
- do **not** participate in temporary construction overload resolution,
- do **not** suppress the compiler-generated default instance constructor.

If a struct has a static constructor but no explicit instance constructors, the
compiler-generated default constructor is available for temporary construction:

```k
counter : int;
struct Tracker {
    static Tracker() { counter = 42; }    // class initializer — runs at startup
}

test() {
    t : Tracker = Tracker();   // OK: uses the compiler-generated default constructor
}
```

If you want to prevent instance construction of a struct that has a static constructor,
explicitly delete the default constructor:

```k
struct Singleton {
    static Singleton() { /* … */ }
    Singleton() -> delete;              // no instances allowed
}

bad() {
    Singleton();   // ERROR: deleted constructor
}
```

---

## 10. Restrictions

| Restriction | Reason |
|-------------|--------|
| `T(args...)` / `T{...}` only for struct/class types | Primitive types and enums are not constructed with object temporary syntax. |
| Abstract classes cannot be instantiated | A temporary of an abstract class would be incomplete. |
| Deleted constructors produce an error | If the best-matching constructor is `-> delete`, the construction is rejected. |
| `T[]{...}` uses positional array init only | Designated member syntax (`.x = ...`) is for struct designated init, not array elements. |

---

## 11. Examples

### Simple temporary in an expression

```k
struct Counter {
    n : int;
    Counter(v: int) : n(v) {}
    value() : int { return n; }
}

test() : int {
    return Counter(42).value();   // → 42
}
```

### Temporary as function argument

```k
struct Pair {
    a : int;
    b : int;
    Pair(x: int, y: int) : a(x), b(y) {}
}

sum(p: Pair&) : int {
    return p.a + p.b;
}

test() : int {
    return sum(Pair(10, 20));   // → 30
}
```

### Multiple temporaries with destruction order

```k
g_dtor : int;

struct Tracked {
    id : int;
    Tracked(v: int) : id(v) {}
    ~Tracked() { g_dtor = g_dtor * 10 + id; }
    get() : int { return id; }
}

test() : int {
    result : int = Tracked(1).get() + Tracked(2).get();
    // After this statement: g_dtor = 21 (destroyed in reverse: 2, then 1)
    return result;   // → 3
}
```

### Qualified type name

```k
namespace shapes {
    struct Circle {
        radius : int;
        Circle(r: int) : radius(r) {}
        area_approx() : int { return 3 * radius * radius; }
    }
}

test() : int {
    return shapes::Circle(5).area_approx();   // → 75
}
```

### Preventing temporary construction with `-> delete`

```k
struct Resource {
    Resource() -> delete;           // no default construction
    Resource(name: const char[]) { /* … */ }
}

bad() {
    Resource();                     // ERROR: deleted constructor
}

good() {
    r : Resource = Resource("db");  // OK: uses Resource(const char[])
}
```

---

*See also:* [Expressions](expressions.md) · [Constructors](../structs/constructors.md) · [Destructors — Expression temporaries](../structs/destructors.md#4-return-values-and-expression-temporaries) · [Function Call](call.md)
