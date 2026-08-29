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
6. [Casting and Type Adaptation](#6-casting-and-type-adaptation)
7. [Discriminant and `index()`](#7-discriminant-and-index)
8. [Kind Enum](#8-kind-enum)
9. [Destruction](#9-destruction)
10. [Passing to Functions](#10-passing-to-functions)
11. [Nested Unions](#11-nested-unions)
12. [Union Inheritance](#12-union-inheritance)
13. [Polymorphic Unions](#13-polymorphic-unions)
14. [Template Unions](#14-template-unions)
15. [Cross-Module Export and Import](#15-cross-module-export-and-import)
16. [Restrictions](#16-restrictions)

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

### Runtime discriminant check on read

Reading an alternative performs a **runtime discriminant check** before exposing
the storage as the requested alternative type.

- If the requested alternative is currently active, access succeeds.
- If the active discriminant does not match the requested alternative, the
  default behaviour is a **fatal trap**.

```k
union Value {
    i: int;
    d: double;
}

test() : int {
    v : Value;
    v.d = 3.14;
    return v.i;    // fatal trap: active alternative is 'd', not 'i'
}
```

### Special case: soft-fail inside classic `if-let`

When an explicit union alternative access is used to initialize a condition-variable
declaration in the classic `if-let` form, an alternative mismatch does **not** trap.
Instead, it follows the `if-let` soft-fail path:

- the condition evaluates to `false`,
- control branches to `else` (or continues after the `if` when there is no `else`),
- the condition variable is not created on that path.

```k
union Value {
    i: int;
    d: double;
}

test() : int {
    v : Value;
    v.d = 3.14;
    if (x : int = v.i) {
        return x;
    } else {
        return -1;   // entered because active alternative is 'd'
    }
}
```

This soft-fail exception applies to all condition-variable forms:

- `if (var : T = u.alt)`
- `if (var1 : T1 = ...; var2 : T2 = u.alt; ...)`
- `if (var : T = u.alt; test)`
- `if (var1 : T1 = ...; var2 : T2 = u.alt; ...; test)`

In all these forms, an alternative mismatch marks the binding as failed, skips
subsequent initializers and skips evaluation of the trailing test expression,
branching safely to `else` (or continuing past the `if`).

---

## 6. Casting and Type Adaptation

Unions support explicit casting and implicit type adaptation to their alternative types
or to the common base of a polymorphic union.

### 6.1 Explicit Casts

A union value or reference can be cast directly to an alternative type by value,
mutable reference, or const reference:

```k
union Value {
    i: int;
    d: double;
}

v : Value;
v.i = 42;

x : int = (int) v;               // Cast by value
(int&) v = 100;                  // Cast by reference (mutates active alternative)
cr : const int& = (const int&) v; // Cast by const reference
```

- **Runtime Check**: Casting evaluates the active discriminant. If the requested alternative
  is active, access succeeds. If mismatched, a fatal trap (`NullCastError`) is triggered.
- **Ambiguity Rule**: If multiple alternatives share the exact same type, casting to that type
  is rejected at compile-time (`ERR_UNION_CAST_AMBIGUOUS`). Explicit member access (`v.alt`)
  must be used instead.

### 6.2 Implicit Type Adaptation

A union can be implicitly adapted to an alternative type in variable initializations,
assignments, and function arguments:

```k
print_int(x: int) : void { /* ... */ }

v : Value;
v.i = 10;

a : int = v;        // Variable initialization
b : int = 0;
b = v;              // Assignment
print_int(v);       // Function parameter passing
```

### 6.3 Soft-fail in `if` Conditions

When a union cast or implicit adaptation is used in an `if` condition-variable binding,
an alternative mismatch follows the soft-fail path (branches to `else` without throwing or trapping):

```k
v : Value;
v.d = 3.14;

if (x : int = v) {
    // Skipped because active alternative is 'd', not 'int'
} else {
    // Safely entered
}
```

### 6.4 Addresser Conversions with `&u`

Applying the address-of operator `&` to a union `u` allows converting to addressers (`*`, `?`, `+`):

- **Nullable addressers (`*` pointer, `?` view)**:
  - If the active alternative matches, returns a non-null pointer/view to the storage.
  - If the active alternative does not match, returns `null` (no exception/trap).
  - In an `if` condition, `if (p : int* = &u)` evaluates to `false` when `p == null`.
- **Non-nullable addressers (`+` link)**:
  - Requires the alternative to match; mismatch triggers a fatal trap (or soft-fail in `if`).

```k
v : Value;
v.d = 3.14;

p : int* = &v;   // p is null because 'd' is active (no trap)
if (q : int* = &v) {
    // Not executed because q == null
}
```

### 6.5 Polymorphic Union Base Casting

For a polymorphic union (`union AnyAnimal : Animal`), casting to the base type (`Animal&`, `Animal*`, `Animal?`, `Animal+`)
or any of its ancestors (`Object&`) is **guaranteed to succeed at runtime** for any active member,
automatically performing the dynamic upcast:

```k
a : AnyAnimal;
a.dog = Dog();

ref : Animal& = a;   // Always succeeds, binds to active Dog as Animal&
ptr : Animal* = &a;  // Produces non-null Animal* pointer
```

---

## 7. Discriminant and `index()`

Every union value exposes a synthetic member function:

```k
v.index()    // returns the uint32 discriminant of the active alternative
```

- Returns an `unsigned int` (matching the Kind enum's underlying type).
- After default construction: `0`.
- After assignment to an alternative: the alternative's index.

---

## 8. Kind Enum

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

- For unions with [inheritance](#12-union-inheritance), the Kind enum covers
  the full inheritance chain (parent alternatives first, own alternatives after).

---

## 9. Destruction

When a union value goes out of scope:

- The compiler emits a **switch** on the discriminant.
- For each alternative that has a non-trivial destructor (e.g., a struct with a
  destructor, a class, or an owner), the destructor is called.
- Alternatives with trivial types (primitives, pointers, references) require no
  cleanup.

This ensures correct resource management regardless of which alternative is
active.

---

## 10. Passing to Functions

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

## 11. Nested Unions

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

## 12. Union Inheritance

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

### 12.1 Rules

- Single inheritance only — at most one parent.
- The parent must itself be a union (not a struct, class, or enum).
- Multi-level chains are supported: `A → B → C`.
- Circular inheritance is detected and rejected.

### 12.2 Discriminant Numbering

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

### 12.3 Kind Enum

The derived union's synthesized Kind enum covers the full chain:

```
enum Derived::Kind { i = 0; d = 1; s = 2; b = 3; }
```

### 12.4 Storage

Storage is sized to the largest alternative across the **full** inheritance
chain. A derived union is always at least as large as its parent.

### 12.5 Alternative Access

All parent alternatives are directly accessible on the derived union:

```k
v: Derived;
v.i = 42;         // inherited alternative — valid
v.s = "hello";    // own alternative — valid
```

### 12.6 Upcast and Downcast

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

### 12.7 Restrictions

- Template unions **cannot** have an inheritance clause (compile-time error).
- Inheriting from a type that is neither a union nor a class/interface is rejected.

---

## 13. Polymorphic Unions

A **polymorphic union** is a union of concrete class types that all inherit directly
or indirectly from a common base class (which may be concrete, or abstract and therefore
not directly an alternative) or an interface.

The common base class or interface is specified in place of the parent union in the
declaration:

```k
abstract class Animal {
public:
    abstract speak() : void;
}

class Dog : Animal {
public:
    override speak() : void { print("Woof!"); }
}

class Cat : Animal {
public:
    override speak() : void { print("Meow!"); }
}

union AnyAnimal : Animal {
    dog: Dog;
    cat: Cat;
}
```

### 13.1 Resolution and Disambiguation

At symbol resolution time, there is no ambiguity with traditional union inheritance:
- If the type named after `:` resolves to another union, union inheritance applies.
- If it resolves to a `class` or `interface`, it is a polymorphic union with that type as its base.
- If it resolves to any other type (such as a `struct`, `enum`, or primitive), compilation fails with an error.

### 13.2 Rules for Alternatives

1. Every alternative declared in a polymorphic union must be a **concrete class** (not an abstract class, interface, struct, primitive, or pointer).
2. Every alternative must directly or indirectly inherit from the common base class or interface.
3. If the base class is concrete, it may also be one of the alternatives.

### 13.3 Polymorphic Union Inheritance

A union may inherit from a polymorphic union:

```k
class Bird : Animal {
public:
    override speak() : void { print("Chirp!"); }
}

union ExtendedAnimal : AnyAnimal {
    bird: Bird;
}
```

- `ExtendedAnimal` inherits `Animal` as its polymorphic base.
- All new alternatives declared in `ExtendedAnimal` must also be descendants of `Animal`.

### 13.4 Operators `->` and `*`

Polymorphic unions provide direct polymorphic access to the active member without explicit downcasting:

- **Arrow operator (`->`)**: When applied to a polymorphic union value `u`, member access (methods, fields) is resolved directly against the common base class or interface:
  ```k
  a : AnyAnimal;
  a.dog = Dog();
  a->speak();   // calls Dog::speak() via virtual dispatch on Animal
  ```
- **Prefix dereference operator (`*`)**: When applied to a polymorphic union value `u`, it returns a reference to the active member upcast to the base type (`BaseType&` or `const BaseType&`):
  ```k
  feed(animal: Animal&) : void { /* ... */ }

  a : AnyAnimal;
  a.cat = Cat();
  feed(*a);     // passes Cat upcast to Animal&
  ```

If `u` is `const`, `*u` returns `const BaseType&`, and `u->` only permits access to `const` members of the base.

---

## 14. Template Unions

Unions can be parameterized with template type or value parameters:

```k
template<typename T>
union Optional {
    value: T;
    empty: byte;
}
```

### 14.1 Instantiation

```k
opt: Optional<int>;
opt.value = 42;
```

Each distinct set of template arguments produces a separate union type
(monomorphization), with its own Kind enum and discriminant numbering.

### 14.2 Template Unions in Template Structs

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

### 14.3 Cross-Module Template Unions

Template union definitions are exported to `.kdi` files including their template
parameters and alternative declarations. Importing modules can instantiate
them with their own type arguments.

### 14.4 Restrictions

- Template unions cannot have an inheritance clause.

---

## 15. Cross-Module Export and Import

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
- Union inheritance and polymorphic base relationships are preserved in KDI: a derived
  or polymorphic union's base is recorded and resolved at import time.

---

## 16. Restrictions

The following are **not** allowed inside a union body:

| Feature | Allowed? |
|---------|----------|
| Alternatives (typed members) | ✓ |
| Functions / methods | ✗ |
| Constructors | ✗ |
| Destructors | ✗ |
| Nested types (struct, class, enum, union) | ✗ |
| Static members | ✗ |
| Base type other than union, class, or interface | ✗ |
| Multiple base types | ✗ |
| Drain addresser (`#`) on alternative types | ✗ |
| Template union with inheritance / base clause | ✗ |

---

## See Also

- [Enumerations](../enums/enums.md) — related discriminated type concept
- [Templates](../templates/templates.md) — template parameters for unions
- [Nested Structures](../structs/nested.md) — nesting unions inside aggregates

---

*This specification describes the current state of unions as implemented by the K compiler.*

