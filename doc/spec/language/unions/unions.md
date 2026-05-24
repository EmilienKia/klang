# Unions (Discriminated / Tagged)

[← Index](../index.md) · [Summary](../summary.md)

Unions provide **discriminated** (tagged) storage: a single value drawn from a
fixed set of alternatives, with a hidden discriminant that tracks which
alternative is currently active.

---

## Contents

1. [Declaration](#1-declaration)
2. [Alternatives](#2-alternatives)
3. [Memory Layout](#3-memory-layout)
4. [Construction](#4-construction)
5. [Member Access](#5-member-access)
6. [Discriminant and `index()`](#6-discriminant-and-index)
7. [Kind Enum](#7-kind-enum)
8. [Destruction](#8-destruction)
9. [Passing to Functions](#9-passing-to-functions)
10. [Nested Unions](#10-nested-unions)
11. [Union Inheritance](#11-union-inheritance)
12. [Template Unions](#12-template-unions)
13. [Cross-Module Export and Import](#13-cross-module-export-and-import)
14. [Restrictions](#14-restrictions)

---

## 1. Declaration

```
UnionDecl:
    { AnnotationDef } { Specifier } 'union' Identifier
        [ ':' QualifiedIdentifier ]
        '{' { UnionMemberDecl } '}'

UnionMemberDecl:
    [ 'const' ] Identifier ':' TypeSpec ';'
```

Example:

```k
union Value {
    i: int;
    d: double;
    s: String;
}
```

- `union` is a keyword.
- The identifier names the union type.
- An optional `: BaseUnion` clause introduces [union inheritance](#11-union-inheritance).
- The body contains one or more *alternative* declarations.

---

## 2. Alternatives

Each member of a union body declares an **alternative** — a named variant that
the union can hold.

```k
union Payload {
    text: const char[]&;   // alternative 0
    number: int;           // alternative 1
    flag: bool;            // alternative 2
}
```

- Alternatives are ordered; each receives a zero-based **discriminant index**
  (0, 1, 2, …).
- An alternative may be marked `const`:

  ```k
  union Cfg {
      const name: const char[]&;
      value: int;
  }
  ```

  A `const` alternative cannot be re-assigned through the union after initial
  construction.
- Alternative types may be any valid K type: primitives, structs, classes,
  pointers, references, owners, arrays — **except** the drain addresser (`#`).

---

## 3. Memory Layout

A union value occupies:

```
{ uint32_t discriminant; [max_size × i8] storage }
```

- **discriminant** — a 32-bit unsigned integer identifying the active alternative.
- **storage** — a byte array sized to the largest alternative (considering
  alignment). All alternatives share this storage region (overlapping, like a
  C `union`).

The total size is therefore `4 + padding + max(sizeof(alternative_i))`.

---

## 4. Construction

### Default construction

Declaring a union variable without an initializer:

```k
v: Value;
```

- Alternative 0 is active.
- Storage is zero-initialized (all bytes = 0).

### Explicit member assignment

```k
v: Value;
v.d = 3.14;   // activates alternative 'd', discriminant ← 1
```

Assigning to any alternative name updates the discriminant and stores the value.

---

## 5. Member Access

```k
x: int = v.i;      // read alternative 'i'
v.d = 2.71;        // write alternative 'd'
```

- **Read**: returns a reference to the storage reinterpreted as the
  alternative's type.
- **Write**: stores the value and updates the discriminant.

> **Note:** Reading an alternative that is not currently active is valid at the
> memory level (bitcast) but the value is meaningless. The language does not
> insert a runtime check on read — the programmer is responsible for checking
> `index()` before reading.

---

## 6. Discriminant and `index()`

Every union value exposes a synthetic member function:

```k
v.index()    // returns the uint32 discriminant of the active alternative
```

- Returns an `unsigned int` (matching the Kind enum's underlying type).
- After default construction: `0`.
- After assignment to an alternative: the alternative's index.

---

## 7. Kind Enum

The compiler synthesizes a **Kind** enum for each union:

```k
union Value {
    i: int;       // Kind::i = 0
    d: double;    // Kind::d = 1
    s: String;    // Kind::s = 2
}
// Compiler generates:  enum Value::Kind { i = 0; d = 1; s = 2; }
```

- Enum entries match the alternative names and their discriminant indices.
- The Kind enum can be used for comparisons:

  ```k
  if (v.index() == Value::Kind::d) { /* ... */ }
  ```

- For unions with [inheritance](#11-union-inheritance), the Kind enum covers
  the full inheritance chain (parent alternatives first, own alternatives after).

---

## 8. Destruction

When a union value goes out of scope:

- The compiler emits a **switch** on the discriminant.
- For each alternative that has a non-trivial destructor (e.g., a struct with a
  destructor, a class, or an owner), the destructor is called.
- Alternatives with trivial types (primitives, pointers, references) require no
  cleanup.

This ensures correct resource management regardless of which alternative is
active.

---

## 9. Passing to Functions

Unions can be passed to functions:

- **By value**: the entire union (discriminant + storage) is copied.
- **By reference** (`&`, `+`, `?`, `*`): a pointer to the union is passed.

```k
readValue(v: Value&) : int {
    return v.i;
}

consume(v: Value) : double {
    return v.d;
}
```

---

## 10. Nested Unions

A union may be declared **inside** a struct or class:

```k
struct Container {
    union Storage {
        i: int;
        d: double;
    }
    data: Storage;
    name: const char[]&;
}
```

- The nested union is a distinct type scoped to the enclosing aggregate.
- Access: `Container::Storage`.
- Instances of the enclosing struct contain the nested union as a regular field.
- Nested unions in **template** structs are fully supported: the union is cloned
  and its alternative types are substituted during template instantiation.

---

## 11. Union Inheritance

A union may inherit from **exactly one** parent union:

```k
union Base {
    i: int;
    d: double;
}

union Derived : Base {
    s: const char[]&;
    b: bool;
}
```

### 11.1 Rules

- Single inheritance only — at most one parent.
- The parent must itself be a union (not a struct, class, or enum).
- Multi-level chains are supported: `A → B → C`.
- Circular inheritance is detected and rejected.

### 11.2 Discriminant Numbering

- Parent alternatives keep their original discriminant values (0, 1, … for `Base`).
- New alternatives in the derived union are numbered starting at
  `parent.total_alternative_count()`.

Example (`Derived` above):

| Alternative | Discriminant |
|-------------|-------------|
| `i` (from Base) | 0 |
| `d` (from Base) | 1 |
| `s` (own) | 2 |
| `b` (own) | 3 |

### 11.3 Kind Enum

The derived union's synthesized Kind enum covers the full chain:

```
enum Derived::Kind { i = 0; d = 1; s = 2; b = 3; }
```

### 11.4 Storage

Storage is sized to the largest alternative across the **full** inheritance
chain. A derived union is always at least as large as its parent.

### 11.5 Alternative Access

All parent alternatives are directly accessible on the derived union:

```k
v: Derived;
v.i = 42;         // inherited alternative — valid
v.s = "hello";    // own alternative — valid
```

### 11.6 Upcast and Downcast

| Direction | Semantics |
|-----------|-----------|
| **Downcast** (parent → derived) | Always valid. Parent discriminant values are a subset of the derived range. Assignment copies the data directly. |
| **Upcast** (derived → parent) | Requires a **runtime discriminant check**. If the active alternative is derived-only (not in the parent's range), a fatal trap is emitted. If valid, the data is copied. |

```k
base: Base;
derived: Derived;

base.i = 10;
derived = base;     // downcast — always safe

derived.s = "hi";
base = derived;     // upcast — FATAL TRAP (alternative 's' not in Base)
```

### 11.7 Restrictions

- Template unions **cannot** have an inheritance clause (compile-time error).
- Inheriting from a non-union type is rejected.

---

## 12. Template Unions

Unions can be parameterized with template type or value parameters:

```k
template<typename T>
union Optional {
    value: T;
    empty: byte;
}
```

### 12.1 Instantiation

```k
opt: Optional<int>;
opt.value = 42;
```

Each distinct set of template arguments produces a separate union type
(monomorphization), with its own Kind enum and discriminant numbering.

### 12.2 Template Unions in Template Structs

A union may be nested inside a template struct. During instantiation, the
union's alternatives have their types substituted along with the rest of the
enclosing struct:

```k
template<typename T>
struct Wrapper {
    union Storage {
        val: T;
        err: int;
    }
    _storage: Storage;
}

w: Wrapper<double>;
w._storage.val = 3.14;     // type of 'val' is double
```

### 12.3 Cross-Module Template Unions

Template union definitions are exported to `.kdi` files including their template
parameters and alternative declarations. Importing modules can instantiate
them with their own type arguments.

### 12.4 Restrictions

- Template unions cannot have an inheritance clause.

---

## 13. Cross-Module Export and Import

Union types are exported in `.kdi` files and can be imported by other modules:

```k
// Module A
module mylib;
union Result {
    ok: int;
    err: const char[]&;
}
```

```k
// Module B
module consumer;
import mylib;

check() : int {
    r: mylib::Result;
    r.ok = 0;
    return r.ok;
}
```

- Union layout (discriminant + storage size) is preserved across module
  boundaries.
- Template unions can also be exported and instantiated by importing modules.
- Union inheritance relationships are preserved in KDI: a derived union's parent
  is recorded and resolved at import time.

---

## 14. Restrictions

The following are **not** allowed inside a union body:

| Feature | Allowed? |
|---------|----------|
| Alternatives (typed members) | ✓ |
| Functions / methods | ✗ |
| Constructors | ✗ |
| Destructors | ✗ |
| Nested types (struct, class, enum, union) | ✗ |
| Static members | ✗ |
| Inheritance from non-union types | ✗ |
| Multiple parent unions | ✗ |
| Drain addresser (`#`) on alternative types | ✗ |
| Template union with inheritance clause | ✗ |

---

## See Also

- [Enumerations](../enums/enums.md) — related discriminated type concept
- [Templates](../templates/templates.md) — template parameters for unions
- [Nested Structures](../structs/nested.md) — nesting unions inside aggregates

---

*This specification describes the current state of unions as implemented by the K compiler.*

