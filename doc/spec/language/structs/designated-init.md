# Designated Struct Initializers

[← Index](../index.md) · [Structures](structs.md)

A *designated struct initializer* names individual fields when constructing a struct instance using brace syntax.
Instead of relying on positional order, each field is explicitly prefixed with `.member`.

---

## Contents
1. [Overview](#1-overview)
2. [Grammar](#2-grammar)
3. [Assignment form](#3-assignment-form)
4. [Constructor call form](#4-constructor-call-form)
5. [Mixed forms](#5-mixed-forms)
6. [Qualified member form](#6-qualified-member-form)
7. [Partial initialization and defaults](#7-partial-initialization-and-defaults)
8. [Order independence](#8-order-independence)
9. [Empty brace init](#9-empty-brace-init)
10. [Nested designated init](#10-nested-designated-init)
11. [Inherited members](#11-inherited-members)
12. [Visibility enforcement](#12-visibility-enforcement)
13. [Errors](#13-errors)
---

## 1. Overview

When initializing a struct variable, the designated initializer syntax allows specifying which fields to set by name, rather than providing values in declaration order.
This is especially useful for:

- Structs with many fields, where positional initialization is error-prone.
- Partial initialization — only a subset of fields is explicitly set; the rest receive their default value.
- Inherited structs where base and derived members must be distinguished.

A designated initializer is a brace-init list where every element is prefixed with `.member`:

```k
p : Point { .x = 10, .y = 20 };
```

Two forms of per-member initialization are available:
- **Assignment form:** `.member = expr` — assigns a value expression to the field.
- **Constructor call form:** `.member(args…)` — invokes a constructor for the field's type with the given arguments.

---

## 2. Grammar

```
DesignatedBraceInitList:
    '{' DesignatedInitElement { ',' DesignatedInitElement } '}'

DesignatedInitElement:
    '.' DesignatedMemberName '=' ConditionalExpr
  | '.' DesignatedMemberName '(' [ ExpressionList ] ')'
  | '.' DesignatedMemberName '=' DesignatedBraceInitList

DesignatedMemberName:
    [ Identifier '::' { Identifier '::' } ] Identifier
```

The `DesignatedBraceInitList` is an alternative to the positional `BraceInitList` in the `Initialiser` production:

```
Initialiser:
    '=' ConditionalExpr
  | '(' [ ExpressionList ] ')'
  | '(' [ ExpressionList ] ')' '[' Expression ']'
  | BraceInitList
  | DesignatedBraceInitList
```

> **Constraint:** Mixing positional and designated elements in the same brace-init list is a compile-time error.
> A list must be entirely positional or entirely designated.

---

## 3. Assignment form

The assignment form `.member = expr` evaluates the expression and stores the result into the named field.

```k
struct Point {
    x : int;
    y : int;
}

test() : int {
    p : Point { .x = 10, .y = 20 };
    return p.x + p.y;   // 30
}
```

The expression can be any valid expression, including arithmetic, function calls, and variable references:

```k
make(a : int, b : int) : int {
    p : Point { .x = a + 1, .y = b * 2 };
    return p.x + p.y;   // (a+1) + (b*2)
}
```

---

## 4. Constructor call form

The constructor call form `.member(args…)` invokes a constructor of the field's type with the given arguments.
For primitive types, this is equivalent to value initialization.

```k
struct S {
    x : int;
}

test() : int {
    s : S { .x(42) };
    return s.x;   // 42
}
```

For struct-typed fields, the constructor form calls the appropriate overloaded constructor:

```k
struct Inner {
    val : int;
    Inner() { val = 0; }
    Inner(v : int) { val = v; }
}

struct Outer {
    inner : Inner;
    x : int;
}

test() : int {
    o : Outer { .inner(55), .x = 10 };
    return o.inner.val;   // 55
}
```

An empty constructor form `.member()` calls the default constructor:

```k
s : S { .x() };   // calls default init for x (0 for int)
```

---

## 5. Mixed forms

Assignment and constructor call forms may be freely mixed in the same designated initializer list:

```k
struct Pair {
    x : int;
    y : int;
}

test() {
    p : Pair { .x = 10, .y(20) };   // OK: .x via assignment, .y via ctor call
}
```

---

## 6. Qualified member form

When a struct inherits from multiple bases that define fields with the same name, the member name alone is ambiguous.
A *qualified* member name resolves the ambiguity by prefixing the base struct name:

```k
struct A {
    v : int;
}
struct B {
    v : int;
}
struct D : public A, public B {
    w : int;
}

test() : int {
    d : D { .A::v = 10, .B::v = 20, .w = 30 };
    return d.A::v + d.B::v + d.w;   // 60
}
```

The qualified form works with both assignment and constructor call syntax:

```k
d : D { .A::v(10), .B::v(20), .w = 30 };
```

Multi-level qualification is supported for deeply nested inheritance hierarchies:

```k
d : D { .A::B::x = 1 };
```

> **Note:** When the member name is unambiguous (either defined in the struct itself or in a single base), the qualifier is optional.
> Using `.v = 10` is sufficient when only one base defines `v`.

---

## 7. Partial initialization and defaults

Not all fields need to appear in a designated initializer.
Fields that are not listed are initialized with their default value:

- **Primitive types** (int, float, bool, etc.) → zero-initialized (0, 0.0, false).
- **Struct-typed fields** → default-constructed (the default constructor is called).
- **Fields with a declared default value** → the declared default is used.

```k
struct Trio {
    a : int;
    b : int;
    c : int;
}

test() : int {
    t : Trio { .b = 42 };
    // t.a == 0   (zero-init)
    // t.b == 42  (designated)
    // t.c == 0   (zero-init)
    return t.a + t.b + t.c;   // 42
}
```

Struct-typed fields that are not designated are default-constructed:

```k
struct Counter {
    count : int;
    Counter() { count = 99; }
}

struct S {
    c : Counter;
    x : int;
}

test() : int {
    s : S { .x = 10 };
    return s.c.count;   // 99 — Counter's default ctor was called
}
```

This applies regardless of the field's visibility: private and protected struct-typed members that are not designated still get their default constructor called.

---

## 8. Order independence

Members can appear in any order in the designated initializer list.
The compiler maps each designator to the correct field regardless of declaration order:

```k
struct Point {
    x : int;
    y : int;
}

test() : int {
    p : Point { .y = 20, .x = 10 };   // reverse order — OK
    return p.x;   // 10
}
```

---

## 9. Empty brace init

An empty brace initializer `{}` on a struct type zero-initializes all primitive fields and default-constructs all struct-typed fields:

```k
struct Pair {
    x : int;
    y : int;
}

test() : int {
    p : Pair {};
    return p.x + p.y;   // 0 + 0 = 0
}
```

When the struct has struct-typed fields with constructors, those constructors are called:

```k
struct A { val : int; A() { val = 11; } }
struct B { val : int; B() { val = 22; } }

struct S {
    a : A;
    b : B;
}

test() : int {
    s : S {};
    return s.a.val + s.b.val;   // 11 + 22 = 33
}
```

---

## 10. Nested designated init

When a field is itself a struct, its value can be specified using a nested designated initializer:

```k
struct Inner {
    a : int;
    b : int;
}

struct Outer {
    inner : Inner;
    c : int;
}

test() : int {
    o : Outer { .inner = { .a = 10, .b = 20 }, .c = 30 };
    return o.inner.a + o.inner.b + o.c;   // 60
}
```

The nested initializer follows the same rules (order independence, partial init, etc.) as a top-level designated init.

---

## 11. Inherited members

Inherited members (from direct or transitive base structs) can be designated without qualification when the name is unambiguous:

```k
struct A { x : int; }
struct B : public A { y : int; }
struct C : public B { z : int; }

test() : int {
    c : C { .x = 10, .y = 20, .z = 30 };
    return c.x + c.y + c.z;   // 60
}
```

Partial designation works across the inheritance hierarchy — non-designated inherited members are zero-initialized or default-constructed:

```k
test() : int {
    c : C { .x = 42 };
    // c.x == 42, c.y == 0, c.z == 0
    return c.x;
}
```

For ambiguous member names in multiple inheritance, use the [qualified member form](#6-qualified-member-form).

---

## 12. Visibility enforcement

Designated initializers respect field visibility.
Only **public** fields may be designated from outside the struct.
Private and protected fields cannot be named in a designated init from outside their permitted scope:

```k
struct S {
private:
    x : int;
public:
    y : int;
}

test() : int {
    s : S { .x = 1 };   // ERROR: private member 'x' is not accessible
    return 0;
}
```

> **Note:** Private and protected struct-typed fields that are **not** designated still have their default constructor called — they are not skipped.
> The visibility restriction only prevents explicitly naming them in the designator list from outside.

---

## 13. Errors

The following compile-time errors are raised for invalid designated initializer usage:

| Condition | Error |
|-----------|-------|
| **Mixing positional and designated elements** | A brace-init list must be entirely positional or entirely designated. A list like `{ 1, .y = 2 }` or `{ .x = 1, 2 }` is rejected. |
| **Non-struct type** | Designated init is only valid on struct (or class) types. Using it on a primitive or array (e.g. `x : int { .a = 1 }`) is an error. |
| **Unknown member name** | The designated member name does not exist in the target struct or any of its bases. |
| **Duplicate member name** | The same member appears twice in the designated list (e.g. `{ .x = 1, .x = 2 }`). |
| **Inaccessible member (private/protected)** | The designated member is private or protected and the initializer is outside the permitted scope. |
| **Ambiguous inherited member without qualifier** | In multiple inheritance, two or more bases define a member with the same name and no qualifier is provided (e.g. `{ .v = 1 }` when both `A::v` and `B::v` exist). |

**Examples of error situations:**

```k
// ERROR: mixing positional and designated
s : S { 1, .y = 2 };

// ERROR: designated init on non-struct type
x : int { .a = 1 };

// ERROR: unknown member 'z'
struct S { x : int; }
s : S { .z = 1 };

// ERROR: duplicate member 'x'
s : S { .x = 1, .x = 2 };

// ERROR: ambiguous member 'v' without qualifier
struct A { v : int; }
struct B { v : int; }
struct D : public A, public B {}
d : D { .v = 1 };           // use .A::v or .B::v instead
```

---

*See also:* [Structures](structs.md) · [Constructors](constructors.md) · [Inheritance](inheritance.md) · [Statements — Variable declaration](../statements/statements.md#4-variable-declaration-statement) · [Grammar](../grammar.md)

