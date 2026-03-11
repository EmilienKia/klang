# Types

[← Index](../index.md)

K is a statically-typed language. Every expression has a type determined at compile time.

---

## Contents

1. [Primitive types](#1-primitive-types)
2. [Indirection types — overview](#2-indirection-types--overview)
3. [Reference (`&`)](#3-reference-)
4. [Link (`~`)](#4-link-)
5. [Pinned (`^`)](#5-pinned-)
6. [Pointer (`*`)](#6-pointer-)
7. [Owner (`!`)](#7-owner-)
8. [Indirection operators](#8-indirection-operators)
9. [Array types](#9-array-types)
   - 9.1 [Internal representation](#91-internal-representation)
   - 9.2 [Sized array value — `T[N]`](#92-sized-array-value--tn)
   - 9.3 [Sized array reference — `T[N]&`](#93-sized-array-reference--tn)
   - 9.4 [Unsized array reference — `T[]`](#94-unsized-array-reference--t)
   - 9.5 [Array assignment](#95-array-assignment)
   - 9.6 [Subscript operator](#96-subscript-operator)
10. [Struct types](#10-struct-types)
11. [Function reference types](#11-function-reference-types)
    - 11.1 [Free function reference types](#111-free-function-reference-types)
    - 11.2 [Member function reference types](#112-member-function-reference-types)
12. [Type specifiers — grammar](#12-type-specifiers--grammar)
13. [Implicit conversions](#13-implicit-conversions)
    - 13.1 [Widening conversions (primitives)](#131-widening-conversions-no-data-loss)
    - 13.2 [Narrowing conversions](#132-narrowing-conversions-possible-data-loss)
    - 13.3 [Static indirection upcast (aggregate types)](#133-static-indirection-upcast-aggregate-types)
    - 13.4 [Dynamic indirection downcast (class/interface)](#134-dynamic-indirection-downcast-classinterface)
    - 13.5 [Owner upcast and downcast](#135-owner-upcast-and-downcast)
    - 13.6 [Explicit cast](#136-explicit-cast)
    - 13.7 [Implicit indirection-to-bool conversion](#137-implicit-indirection-to-bool-conversion)
14. [Const-ness](#14-const-ness)

---

## 1. Primitive types

Primitive types are built-in types that represent scalar values.

### Integer types

| Keyword           | Bits | Signed | Range |
|-------------------|------|--------|-------|
| `bool`            | 1    | —      | `false` / `true` |
| `byte`            | 8    | yes    | −128 … 127 |
| `char`            | 8    | yes    | −128 … 127 (character alias) |
| `unsigned byte`   | 8    | no     | 0 … 255 |
| `short`           | 16   | yes    | −32 768 … 32 767 |
| `unsigned short`  | 16   | no     | 0 … 65 535 |
| `int`             | 32   | yes    | −2 147 483 648 … 2 147 483 647 |
| `unsigned int`    | 32   | no     | 0 … 4 294 967 295 |
| `long`            | 64   | yes    | −2⁶³ … 2⁶³−1 |
| `unsigned long`   | 64   | no     | 0 … 2⁶⁴−1 |

`byte` and `char` refer to the same underlying 8-bit signed integer type.
`unsigned` is a modifier that makes the following integer type unsigned.

### Floating-point types

| Keyword  | Bits | Standard          |
|----------|------|-------------------|
| `float`  | 32   | IEEE 754 single   |
| `double` | 64   | IEEE 754 double   |

### Boolean type

`bool` represents a boolean value: `true` or `false`.
`bool` is a distinct type from integer types.

---

## 2. Indirection types — overview

K has five indirection types.  Four of them form a 2×2 matrix along two independent axes; the
fifth — the *owner* — has additional ownership semantics and is described separately.

### The four observer indirections

|              | **Non-null** (strong) | **Nullable** |
|--------------|-----------------------|--------------|
| **Immutable binding** | `T&` — reference      | `T^` — pinned |
| **Mutable binding**   | `T~` — link           | `T*` — pointer |

* **Binding mutability** — whether the indirection variable can be *rebound* after
  initialisation (i.e. made to point to a different object).
* **Nullability** — whether the indirection may hold a null address at runtime.

All four observer types carry the address of an object of type `T`. They differ only in the
above properties; their bit-width and calling convention are identical.  They are called
**observers** because they do not own the pointed-to object — they are never responsible for
deleting it.

**Grammar (observer types):**

```
TypeSuffix:
    '&'    -- reference (immutable binding, non-null)
    | '~'  -- link      (mutable binding,   non-null)
    | '^'  -- pinned    (immutable binding, nullable)
    | '*'  -- pointer   (mutable binding,   nullable)
```

**Assignment semantics summary:**

| Type | `x = val` (val is a value) | `x = &y` / `x = lnk` (val is an indirection) |
|------|------|------|
| `T&` | assigns `val` to the referenced object | compile-time error (rebind forbidden) |
| `T~` | assigns `val` to the linked object | **rebinds** `x` to point to `y` |
| `T^` | compile-time error (rebind forbidden) | compile-time error (rebind forbidden) |
| `T*` | compile-time error (must use `*x = val`) | **rebinds** `x` to point to `y` |

**Null safety:**

Assigning or initialising a `T~` (link) from a nullable source (`T^` or `T*`) emits a
**compile-time warning** and inserts a **runtime null-check**; if the source is null at
runtime, `__fatal_null_assignation()` is called (which traps).

Dereferencing (`*x` or `x->m`) a `T^` or `T*` value likewise inserts a runtime null-check;
if null, `__fatal_null_dereference()` is called.

**Static upcast (aggregate types):**

When `Derived` inherits from `Base`, an indirection of type `T<Derived>` can be implicitly
assigned to an indirection of type `T<Base>`.  
The pointer is adjusted at compile time via a GEP to address the `Base` sub-object.  
See [§13.3 — Static indirection upcast](#133-static-indirection-upcast-aggregate-types) for full details.

**Dynamic downcast (class/interface types):**

When a `Base` indirection may point to a `Derived` object at runtime (and both are `class` or
`interface` types), it can be assigned to a `Derived` indirection.  
A runtime RTTI check is emitted; on mismatch, null is assigned (and fatal for non-null
targets).  
See [§13.4 — Dynamic indirection downcast](#134-dynamic-indirection-downcast-classinterface) for full details.

### The owner (`T!`)

`T!` is a **move-only**, **nullable** indirection that carries **exclusive ownership** of a
dynamically allocated object.  It differs from the four observer types in several key ways:

| Property | `T*` (pointer) | `T!` (owner) |
|---|---|---|
| Nullable | yes | yes |
| Mutable binding | yes | yes (move only) |
| Owns the object | no | **yes** |
| Can `delete` object | no | **yes** |
| Copy semantics | copy address | **move** (source → null) |
| Auto-delete on scope exit | no | **yes** |

See [§7 — Owner (`!`)](#7-owner-) for full details.

**Grammar (full type suffix, including owner):**

```
TypeSuffix:
    '[' [ IntegerLiteral ] ']'   -- array (sized or unsized)
    | '!'                        -- owner (move-only, nullable, exclusive ownership)
    | '&'                        -- reference (immutable binding, non-null)
    | '~'                        -- link (mutable binding, non-null)
    | '^'                        -- pinned (immutable binding, nullable)
    | '*'                        -- pointer (mutable binding, nullable)
```

### Null literal type

The `null` keyword is a literal with a dedicated type, distinct from every other type
in the type system.  It represents a null pointer value — an indirection that points to
no object.

`null` is implicitly convertible to **nullable** indirection types: `T*`, `T^`, and `T!`.
It is **not** convertible to non-null types: `T&` (reference) and `T~` (link).

In a boolean context (`if`, `while`, `for`, `&&`, `||`, `!`), `null` converts to `false`.
When compared with `==` or `!=` against any indirection (including `T~` and `T^`), `null`
participates in address comparison.

See also: [Null literal](../expressions/literals.md#6-null-literal).

---

## 3. Reference (`&`)

A reference is an alias for an existing object. It acts exactly as the object itself.

**Syntax:** `T&`

**Properties:**

- Immutable binding — cannot be rebound after initialisation.
- Non-null — always refers to a valid object.
- Transparent — operations on a reference apply to the referenced object:
  `r = 42` assigns to the object, not to `r`.
- Taking the address of a reference with `&r` returns a link (`T~`) to the same object.

**Examples:**

```k
set(var: int&, val: int) {
    var = val;          // modifies the referenced object
}

test() : int {
    x : int = 10;
    r : int& = x;       // r is bound to x
    r = 42;             // assigns 42 to x through r
    return x;           // returns 42
}
```

**Constraints:**

1. **Mandatory initialisation** — must be initialised at declaration:
   ```k
   r : int&;           // ERROR: reference without initialiser
   ```
2. **Initialiser must be addressable** — the initialiser must be an lvalue (variable, parameter, struct member); a temporary or arithmetic result is rejected:
   ```k
   r : int& = 42;      // ERROR: 42 is not addressable
   r : int& = x + 1;   // ERROR: temporary
   ```
3. **Type must match exactly** — no implicit conversions when binding:
   ```k
   d : double = 3.14d;
   r : int& = d;       // ERROR: int& cannot refer to a double
   ```
4. **No rebind** — assigning through a reference modifies the object, not the binding:
   ```k
   x : int = 10;
   y : int = 20;
   r : int& = x;
   r = y;              // copies y's value into x; r is still bound to x
   ```
5. **No null** — there is no null reference.

---

## 4. Link (`~`)

A link is a **rebindable**, non-null address. It combines the non-null safety of a reference with the ability to be pointed at a different object later.

**Syntax:** `T~`

**Properties:**

- Mutable binding — can be rebound after initialisation using `lnk = &y` or `lnk = other_link`.
- Non-null — always points to a valid object; rebinding from a nullable source triggers a runtime null-check.
- Transparent on value operations — `lnk = val` (where `val` is of type `T`) assigns to the linked object.
- Rebind on address operations — `lnk = &y` or `lnk = other_link` rebinds the link.

**Examples:**

```k
test() : int {
    x : int = 10;
    y : int = 20;
    lnk : int~ = &x;   // lnk points to x
    lnk = 99;           // assigns 99 to x through lnk (transparent)
    lnk = &y;           // REBIND: lnk now points to y
    return *lnk;        // returns 20 (y's value)
}
```

**Dereference:** `*lnk` produces a reference (`T&`) to the linked object. No null-check is inserted (link is non-null).

**Member access:** `lnk->m` accesses member `m` of the linked struct. Equivalent to `(*lnk).m`.

**Constraints:**

1. **Mandatory initialisation** — must be initialised at declaration:
   ```k
   lnk : int~;         // ERROR: link without initialiser
   ```
2. **Initialiser must be an indirection** — must be initialised from a reference, link, pinned or pointer:
   ```k
   lnk : int~ = 42;    // ERROR: 42 is not an address
   ```
3. **Warning on nullable source** — if initialised or rebound from a `T^` or `T*`, a warning is emitted and a runtime null-check is inserted.

---

## 5. Pinned (`^`)

A pinned is an **immutable binding** that may be null. Think of it as a non-rebindable raw pointer.

**Syntax:** `T^`

**Properties:**

- Immutable binding — cannot be rebound after initialisation. `pin = &y` is a compile-time error.
- Nullable — may hold null.
- Requires explicit dereference — `*pin` to access the object (with a runtime null-check).

**Examples:**

```k
test() : int {
    x : int = 42;
    pin : int^ = &x;    // pin holds the address of x; cannot be changed later
    return *pin;        // dereferences with null-check; returns 42
}

maybe_null() : int {
    pin : int^ = null;
    return *pin;        // runtime trap: __fatal_null_dereference()
}
```

**Dereference:** `*pin` inserts a runtime null-check. If null, `__fatal_null_dereference()` is called.

**Member access:** `pin->m` accesses member `m` of the pinned struct, with null-check.

**Constraints:**

1. **Mandatory initialisation** — must be initialised at declaration:
   ```k
   pin : int^;         // ERROR: pinned without initialiser
   ```
2. **No rebind** — any assignment to `pin` after initialisation is a compile-time error:
   ```k
   pin : int^ = &x;
   pin = &y;           // ERROR: pinned is immutable
   ```

---

## 6. Pointer (`*`)

A pointer holds the address of an object and can be reassigned to point to a different object (or null).

**Syntax:** `T*`

**Properties:**

- Mutable binding — can be rebound at any time.
- Nullable — may hold null.
- Requires explicit dereference — `*p` to access or assign the object.

**Examples:**

```k
test() : int {
    x : int = 10;
    y : int = 20;
    p : int* = &x;      // p points to x
    *p = 99;            // assigns 99 to x
    p = &y;             // rebind: p now points to y
    return *p;          // dereferences with null-check; returns 20
}
```

**Dereference:** `*p` inserts a runtime null-check. If null, `__fatal_null_dereference()` is called.

**Member access:** `p->m` accesses member `m` of the pointed-to struct, with null-check.

**Address-of:** `&p` yields a link (`T*~`) to the pointer variable itself (address of the pointer).

---

## 7. Owner (`!`)

An *owner* is a **move-only**, **nullable** indirection that holds the address of a
**dynamically allocated** object and is **solely responsible for its lifetime**.  Only an
owner can delete (destroy and free) the object it points to.

**Syntax:** `T!`

**Properties:**

- **Mutable binding** — an owner variable can be reassigned at any time.  Reassignment is
  always a *move*, not a copy.
- **Nullable** — may hold `null`; a null owner manages no object.
- **Exclusive ownership** — at most one `T!` variable points to a given dynamically
  allocated object at any time.
- **Move semantics** — assigning one owner to another *transfers* ownership; the source is
  set to `null`.  If the destination already held an object, that object is deleted first.
- **Automatic destruction** — when an owner variable goes out of scope while non-null, its
  object is automatically deleted (destructor called + `free` issued).
- **Observer-safe** — the four non-owner indirection types (`T&`, `T~`, `T^`, `T*`) may
  hold the address of the owned object; the owner retains exclusive responsibility for
  deletion.  These types are *non-owning observers* and must not outlive the owner.

**Default initialisation:**

An owner declared without an explicit initialiser is implicitly initialised to `null`:

```k
p : Foo!;             // OK: p is null
q : Foo! = null;      // identical
```

**Examples:**

```k
test() {
    obj : Foo! = new Foo(42);    // dynamic allocation; obj owns the Foo
    p   : Foo* = obj;            // observer pointer: raw address, no ownership
    ref : Foo& = *obj;           // observer reference: transparent alias
    // p and ref must not be used after obj is deleted
}   // obj goes out of scope → destructor called + memory freed
```

**Move semantics:**

Assignment between two owners always transfers ownership (implicit move).  The source owner
becomes `null` after the transfer, regardless of whether the destination had a previous
object.  If the destination was non-null, its previous object is deleted first:

```k
a : Foo! = new Foo(1);   // a owns Foo(1)
b : Foo!;                // b is null

b = a;                   // MOVE: a ← null; b now owns Foo(1)

c : Foo! = new Foo(2);   // c owns Foo(2)
b = c;                   // Foo(1) deleted first; c ← null; b now owns Foo(2)
```

**Assigning `null`:**

Assigning `null` to a non-null owner deletes the owned object and sets the owner to `null`.
Assigning `null` to an already-null owner is a no-op:

```k
p : Foo! = new Foo();
p = null;                // Foo deleted; p ← null
p = null;                // no-op
```

**Owner as function parameter or return type:**

When an owner is passed to a function parameter of owner type, or returned from a function,
ownership is transferred.  If a returned owner is not assigned to an owner variable, the
object is deleted immediately:

```k
make_foo() : Foo! { return new Foo(99); }   // caller receives ownership
consume(f : Foo!)  { /* f deleted on return if still non-null */ }

test() {
    obj : Foo! = make_foo();   // ownership transferred to obj
    consume(obj);              // MOVE: obj ← null; f owns Foo inside consume
    // obj is null here
    make_foo();                // Warning 0x5010: Foo(99) deleted immediately
}
```

**Observer assignment (owner → non-owner):**

An owner may be assigned to a non-owner indirection type.  The address is copied but the
owner **retains ownership**.  The receiving type is a non-owning observer:

```k
owner : Foo! = new Foo(7);
obs   : Foo* = owner;          // raw address; owner still owns Foo
lnk   : Foo~ = owner;          // non-null check inserted at binding
ref   : Foo& = *owner;         // reference (via dereference, with null-check)
```

> **Note:** passing an owner to a `Foo*` parameter passes the raw address only; it does
> **not** transfer ownership.  To transfer ownership, the parameter must be `Foo!`.

**Upcast and downcast:**

*Non-polymorphic types (primitives, arrays, `struct`):*

An owner of a non-polymorphic type may only be assigned to an owner of the **exact same
type**.  No implicit conversion is performed.

```k
struct Pt { x : int; y : int; }
a : Pt! = new Pt();
b : Pt! = a;          // OK: same type (move)
// c : int! = a;      // ERROR: Pt! cannot be converted to int!
```

*Polymorphic types (`class`, `interface`) — static upcast (derived → base):*

An owner of a derived class can be implicitly assigned to an owner of a base class.  The
pointer is adjusted at compile time (GEP).  Ownership is transferred; the source becomes
`null`.

```k
class Animal { Animal() {} }
class Dog : public Animal { Dog() : Animal() {} }

d : Dog!    = new Dog();
a : Animal! = d;           // static upcast; d ← null; a owns the Dog
```

*Polymorphic types — dynamic downcast (base → derived):*

An owner of a base class or interface can be assigned to an owner of a derived class, but
this requires a runtime RTTI check.  A **compile-time Warning 0x5001** is always emitted.

| RTTI result | Effect |
|---|---|
| **Match** (object is actually `Derived`) | pointer adjusted (GEP); ownership transferred; source ← `null` |
| **No match** | owned object deleted (destructor + `free`); source ← `null`; destination ← `null` |

```k
a : Animal! = new Dog();  // a owns a Dog (stored as Animal!)
d : Dog!;                 // Warning 0x5001: dynamic owner downcast
d = a;                    // RTTI OK → a ← null; d owns Dog

a2 : Animal! = new Cat();
d2 : Dog!;                // Warning 0x5001
d2 = a2;                  // RTTI fail → Cat deleted; a2 ← null; d2 ← null
```

**Dereference and member access:**

| Expression | Result | Null-check |
|---|---|---|
| `*owner` | `T&` — reference to owned object | Yes — calls `__fatal_null_dereference()` if null |
| `owner->m` | member `m` of owned object | Yes — calls `__fatal_null_dereference()` if null |

**Constraints:**

1. An owner is **not copyable** — it can only be moved.  `T!` never appears as the source
   of a copy; any assignment of a `T!` to a `T!` variable is a move.
2. An explicit cast `(T!) expr` is not permitted — owner values may only be produced by
   `new` or transferred from another `T!` variable.
3. Observers derived from an owner **must not** be used after the owner has been moved or
   deleted (use-after-free is a programmer responsibility; no compile-time lifetime tracking
   is performed beyond the null-check on dereference).

**Internal representation:**

An owner variable is stored as a single pointer-sized slot (identical bit-width to `T*`).
Ownership semantics are enforced entirely at compile time; no runtime header or reference
count is added.

*See also:* [Dynamic Allocation — `new` and `delete`](../memory/new-delete.md)

---

## 8. Indirection operators

The following operators apply to all five indirection types (including owner).

### Address-of (`&expr`)

Takes the address of an lvalue. The result type depends on the context:

- Applied to an ordinary variable or parameter of type `T`: produces `T~` (a link).
- Applied to a `T&` reference variable: produces `T~` (a link to the same object).

```k
x : int = 5;
lnk : int~ = &x;     // address-of produces a link
```

> **Note:** In previous versions, `&expr` returned `T*`. It now returns `T~` (non-null address), which can be widened to `T*` or `T^` by implicit conversion.

### Dereference (`*expr`)

Yields a reference (`T&`) to the object at the address held by `expr`.

| Operand type | Null-check at runtime |
|---|---|
| `T~` | No (link is non-null) |
| `T^` | Yes — calls `__fatal_null_dereference()` if null |
| `T*` | Yes — calls `__fatal_null_dereference()` if null |
| `T&` | Not applicable (& is not dereferenceable by `*`) |

```k
p  : int* = &x;
lnk : int~ = &x;
*p   = 42;           // null-check + assign
*lnk = 42;           // no null-check + assign
```

### Member-of-pointer (`expr->m`)

Accesses member `m` of the struct pointed to by `expr`. Equivalent to `(*expr).m`.

Inserts a runtime null-check for `T^` and `T*` operands.

```k
struct Point { x : int = 0; y : int = 0; }

test() : int {
    pt : Point();
    p  : Point* = &pt;
    return p->x + p->y;     // null-check then member access
}
```

---

## 9. Array types

An array type represents a fixed-size sequence of elements of the same type.
Arrays in K are **value types** — each array variable holds its own storage.

### 9.1 Internal representation

A sized array of type `T[N]` is represented internally as a struct:

```
{ uint32 count; T[N] data; }
```

* **`count`** (field 0) — number of elements (`N`), stored as a 32-bit unsigned integer.
  The maximum number of elements in a single array is 2³² − 1.
* **`data`** (field 1) — contiguous block of `N` elements of type `T`.

This layout is opaque to the programmer; it is specified here for documentation completeness.

---

### 9.2 Sized array value — `T[N]`

A sized array **value** variable declares and allocates a concrete array of `N` elements.

```
SizedArrayTypeSuffix:
    '[' IntegerLiteral ']'
```

**Examples:**

```k
arr : int[4];           // local array of 4 ints, zero-initialised
g   : double[100];      // global array of 100 doubles, zero-initialised
```

**Initialisation:**
- A sized array variable is **zero-initialised** at its declaration by default (primitives are set to
  `0`; struct elements will be default-constructed).
- Explicit initialisation with a brace initializer list is supported:
  ```k
  arr : int[3] {1, 2, 3};           // each element explicitly initialised
  arr : int[5] {1, 1+1, 2+1, 2*2, 10/2};  // expressions are allowed
  arr : int[] {10, 20, 30};         // size inferred from the number of elements (3)
  arr : int[0] {};                  // empty array
  ```
- An empty slot in the list represents default construction:
  ```k
  arr : int[3] {1, , 3};            // element 1 is zero-initialised
  ```
- If fewer elements are provided than the array size, remaining elements are default-initialised
  and a **warning** is emitted.
- If more elements are provided than the array size, a **compile error** is emitted.
- For aggregate element types, each element can be a constructor invocation:
  ```k
  arr : Point[3] { Point(1,2), Point(3,4), Point(5,6) };
  ```

**Lifetime:**
- An array never changes its size or type during its lifetime.
- The compiler allocates the full storage at the point of declaration.

---

### 9.3 Sized array reference — `T[N]&`

A sized array **reference** is a reference that **owns a fresh copy** of the source array.
It is semantically equivalent to binding the alias name to a privately owned copy.

```
SizedArrayReferenceType:
    T '[' N ']' '&'
```

**Copy-initialisation rules:**

Let `dst : T[N]& = src` where `src` has type `T[M]` or `T[M]&`:

| Relationship | Elements copied | Remaining dest elements |
|---|---|---|
| dest.size ≤ src.size | first `N` elements of `src` | — |
| dest.size = src.size | all `N` elements | — |
| dest.size > src.size | all `M` elements of `src` | zero-initialised (primitives) / default-constructed (structs) |

**Constraints on `T[N]&`:**

1. **Mandatory initialisation** — An array reference must be initialised at its declaration:
   ```k
   r : int[4]&;          // ERROR: array reference without initialiser
   ```

2. **Initialiser must be an array reference (lvalue)** — The initialiser must be a sized array
   variable (or parameter); a bare value or non-array is rejected:
   ```k
   x : int = 5;
   r : int[4]& = x;      // ERROR: int is not an array
   ```

3. **Element types must match exactly** — The element type of source and destination must be
   identical; no implicit conversions between element types are applied:
   ```k
   src : double[4];
   r : int[4]& = src;    // ERROR: element type mismatch (double ≠ int)
   ```

4. **No rebind** — Once bound, the reference always refers to its own copy; it cannot be
   rebound to point to another array.

5. **No null** — An array reference is always bound to a valid array.

---

### 9.4 Unsized array reference — `T[]` (= `T[]&`)

An unsized array type `T[]` is a reference to an array whose size is not known at the
declaration site.  It is exactly equivalent to `T[]&` (the explicit reference form).

```
UnsizedArrayRefType:
    T '[' ']'          -- canonical form; identical to T '[]' '&'
    | T '[' ']' '&'    -- explicit reference form; same representation
```

Both notations compile to the same internal representation: a pointer to a
`{ uint32 count; T[?] data; }` struct.

**Constraints:**
- All constraints that apply to `T[N]&` (mandatory initialisation, element type match, no
  rebind, no null) also apply to `T[]`.
- The size of the referenced array is determined at the binding site.

**Examples:**

```k
// Both declarations are identical:
a : int[];      // reference to int array of unknown size
b : int[]&;     // same — explicit & is optional but permitted
```

---

### 9.5 Array assignment

Assigning one array to another performs an **element-wise copy**.  The destination array
never changes its size.

| Relationship | Elements overwritten | Tail elements |
|---|---|---|
| dest.size ≤ src.size | first `dest.size` elements | unchanged |
| dest.size = src.size | all elements | — |
| dest.size > src.size | first `src.size` elements | unchanged |

> **Important:** Unlike initialisation via `T[N]&`, assignment does **not** zero-initialise
> or default-construct the tail elements of the destination.  Only the copied range is
> modified.

```k
a : int[3];
a[0] = 1; a[1] = 2; a[2] = 3;
b : int[3];
b = a;             // element-wise copy: b = {1, 2, 3}
a[0] = 99;         // does not affect b
```

---

### 9.6 Subscript operator

Elements are accessed via the subscript operator `[]`.

```k
arr[0] = 42;
x = arr[i];
```

* Indices are zero-based.
* The index expression must be implicitly convertible to `unsigned int`.
* **Runtime bounds checking** is performed on every subscript access: the index
  is compared (unsigned) against the element count stored in the array header.
  An out-of-bounds access prints a diagnostic to `stderr` and calls `abort()`.

  ```
  runtime error: array index out of bounds (index=5, size=3)
  ```

---


## 10. Struct types

A struct type is a user-defined composite type. See the [Structures](../structs/structs.md) reference for full details.

A struct type is referenced by name (simple or qualified).

```k
p : plop;               // variable of struct type 'plop'
r : plop&;              // reference to 'plop'
ptr : plop*;            // pointer to 'plop'
```

---

## 11. Function reference types
A *function reference type* describes a variable that holds the **address of a function** and can be called later. K distinguishes free function references and member function references.
For full details, see [Function References](../functions/function_references.md).
### 11.1 Free function reference types
A free function reference holds the address of a free function or a `static` member function.
**Syntax:** `qualifier '(' [ TypeList ] ')'`
| Qualifier | Nullable | Rebindable |
|-----------|----------|------------|
| `*(Params)` | Yes | Yes |
| `^(Params)` | Yes | No  |
| `~(Params)` | No  | Yes |
The return type is not written — it is inferred from the target function.
```k
add_one(x : int) : int { return x + 1; }
fp  : *(int) = add_one;    // nullable, rebindable pointer to (int)->?
lnk : ~(int) = add_one;    // non-null link  to (int)->?
pin : ^(int) = add_one;    // nullable, fixed pin to (int)->?
result : int = fp(41);     // call through the reference -> 42
```
### 11.2 Member function reference types
A member function reference holds the address of a non-static member function of a specific struct `T`.
**Syntax:** `T '::' qualifier '(' [ TypeList ] ')'`
The `T::` prefix identifies the struct. The implicit `this` parameter is **not** listed.
```k
struct Counter {
    value : int;
    add(x : int) : int { return value + x; }
}
mfp  : Counter::*(int) = Counter::add;   // pointer to Counter::add
c : Counter;
c.value = 40;
result : int = (c.*mfp)(2);    // calls c.add(2) -> 42
```
To call through an indirection (`*`, `^`, `~`), use `->*`:
```k
lnk : Counter~ = c;
result : int = (lnk->*mfp)(2);   // -> 42
```
See [Function References](../functions/function_references.md) for the full call syntax.
---
## 12. Type specifiers — grammar

Types appear in variable declarations, parameter declarations, and return type annotations.

### Grammar

```
TypeSpec:
    FundamentalTypeSpec { TypeSuffix }
    | QualifiedIdentifier { TypeSuffix }

FundamentalTypeSpec:
    [ 'unsigned' ] ( 'byte' | 'char' | 'short' | 'int' | 'long' | 'float' | 'double' )
    | 'bool'

TypeSuffix:
    '[' [ IntegerLiteral ] ']'     -- array suffix (sized or unsized)
    | '!'                          -- owner (move-only, nullable, exclusive ownership)
    | '&'                          -- reference (immutable binding, non-null)
    | '~'                          -- link (mutable binding, non-null)
    | '^'                          -- pinned (immutable binding, nullable)
    | '*'                          -- pointer (mutable binding, nullable)
```

Suffixes may be chained: `int*` is a pointer to int; `int[4]` is a 4-element int array;
`int[4]&` is a reference to a 4-element int array.

Note: `T!` (owner) does **not** compose with further suffixes — `Foo!*` or `Foo!!` are not
valid type expressions.  An owner is always a top-level type suffix.

**Examples:**

```k
int
double
unsigned int
short*
int~
int^
int[4]
int[4]&
plop&
plop~
plop*
plop!       -- owner of a dynamically allocated plop
```

---

## 13. Implicit conversions

The compiler performs implicit type conversions in certain contexts (e.g., function call arguments, assignments):

### 13.1 Widening conversions (no data loss)

A narrower integer or float type is widened to a broader one automatically.

| From        | To                     |
|-------------|------------------------|
| `byte`/`char` | `short`, `int`, `long` |
| `short`     | `int`, `long`          |
| `int`       | `long`                 |
| `float`     | `double`               |
| integer     | `float` or `double`    |

### 13.2 Narrowing conversions (possible data loss)

Narrowing conversions are also accepted implicitly by the current compiler (e.g., passing an `int` where a `short` is expected). The programmer is responsible for ensuring correctness.

> **Note:** This behaviour may be tightened in future versions to require an explicit cast for narrowing conversions.

### 13.3 Static indirection upcast (aggregate types)

When the pointed-at type `Derived` inherits from `Base`, an indirection of type `T<Derived>` can be implicitly converted to an indirection of type `T<Base>`. This is a **static (compile-time) upcast** — the pointer is adjusted at compile time via a GEP instruction to point to the `Base` sub-object within the `Derived` object.

This applies to all four indirection types:

| Source                | Destination(s)           | Notes |
|-----------------------|--------------------------|-------|
| `Derived&`            | `Base&`                  | Only at construction (ref is immutable binding) |
| `Derived~`            | `Base~`                  | Init and rebind |
| `Derived^`            | `Base^`                  | Only at construction (pin is immutable binding) |
| `Derived*`            | `Base*`                  | Init and rebind |
| `Derived*` or `Derived~` | `Base~`               | Initialisation only (link is non-null; null-check inserted if source is nullable) |
| `Derived^` or `Derived*` | `Base*`               | Rebind |

**Rules:**

1. `Base` must be a direct or transitive base class/struct/interface of `Derived`. The relationship is verified at compile time.
2. If the types have no inheritance relationship, a **compile-time error** is emitted (`0x4506` for links, `0x4605` for pinned, `0x4700` for pointers, `0x4005` for references).
3. Rebind constraints are respected:
   - `ref` (`&`) and `pin` (`^`) can only be bound at construction — no rebind (compile-time error).
   - `link` (`~`) and `ptr` (`*`) can be rebound at any time.
4. Null-safety is preserved:
   - Assigning a nullable source (`ptr*` or `pin^`) to a non-null destination (`link~` or `ref&`) inserts a **runtime null-check** (`__fatal_null_assignation()` if null) and emits **warning 0x4505**.
5. Transitive upcasts (e.g. `C*→A*` where `C→B→A`) are supported via chained GEP.
6. Virtual base upcasts are supported via the vbptr mechanism.

**Examples:**

```k
struct Animal {
    legs : int;
    Animal(n : int) : legs(n) {}
}
struct Dog : public Animal {
    name_hash : int;
    Dog(n : int) : Animal(n), name_hash(42) {}
}

use() {
    d : Dog(4);

    // ref: bind once at construction
    r : Animal& = d;         // OK: r sees d.legs = 4
    // r = d2;               // ERROR: ref cannot be rebound

    // lien: bind and rebind
    d2 : Dog(2);
    lnk : Animal~ = &d;      // OK
    lnk = &d2;               // rebind to d2

    // pin: bind once (nullable)
    p : Animal^ = &d;        // OK
    // p = &d2;              // ERROR: pin cannot be rebound

    // ptr: bind and rebind (nullable)
    ptr : Animal* = &d;      // OK
    ptr = &d2;               // rebind OK
    ptr = null;              // OK: ptr can be null

    // Init lien from ptr (nullable → non-null: null-check inserted)
    dp : Dog* = &d;
    lnk2 : Animal~ = dp;    // Warning 0x4505: null-check at runtime

    // Error: unrelated type
    // struct Cat { name_hash : int; Cat() : name_hash(0) {} }
    // c : Cat();
    // bad : Animal* = &c;   // ERROR 0x4700: Cat does not inherit from Animal
}
```

### 13.4 Dynamic indirection downcast (class/interface)

When a `Base*` (or `Base~`, `Base^`, `Base&`) indirection may actually point to a `Derived` object
at runtime, K allows assigning it to a `Derived*` (or `Derived~`, `Derived^`, `Derived&`).
This is a **dynamic (RTTI-based) downcast** — the compiler inserts a runtime type check.

**Applicability:**

| Applies to | Notes |
|------------|-------|
| `class` types | Yes — classes carry RTTI via their vtable |
| `interface` types | Yes — interfaces carry RTTI |
| `struct` types | **No** — structs have no vtable/RTTI; compile-time error |
| Primitive types | **No** |

**Semantics:**

1. At runtime, the RTTI pointer stored in the object's vtable is compared with the RTTI descriptor of `Derived`.
2. If they match, the pointer is adjusted (byte offset subtracted) to point to the start of the `Derived` sub-object, and the result is assigned.
3. If they do not match, **null** is assigned to the target.
4. Null assigned to a **non-null** indirection (`link ~` or `reference &`) immediately invokes `__fatal_null_dyncast()`.

**Binding rules (same as static upcast):**

| Target type | When allowed | Null-on-mismatch behaviour |
|-------------|--------------|---------------------------|
| `Derived&`  | Init only (immutable binding) | fatal trap (non-null) |
| `Derived~`  | Init only (immutable binding) | fatal trap (non-null) |
| `Derived^`  | Init only (immutable binding, nullable) | null assigned |
| `Derived*`  | Init and rebind | null assigned |

**Error conditions:**

| Situation | Result |
|-----------|--------|
| Source and target are unrelated classes | Compile error |
| Source or target is a `struct` type | Compile error |
| Rebinding a `ref` or `pin` | Compile error |

**Examples:**

```k
class Base {
    public val : int;
    public Base(v : int) : val(v) {}
    public dummy() : int { return 0; }
}
class Derived : public Base {
    public extra : int;
    public Derived(v : int) : Base(v), extra(99) {}
    public get_extra() : int { return extra; }
}

get_extra_fn(d : Derived&) : int { return d.get_extra(); }

test() : int {
    d  : Derived(42);
    bp : Base*    = &d;          // static upcast: Base* pointing at a Derived object

    // ptr — nullable, null on mismatch
    dp : Derived* = bp;          // dynamic downcast; dp non-null if RTTI matches
    // *dp crashes if dp is null

    // pin — nullable, null on mismatch  
    pp : Derived^ = bp;          // same as ptr but immutable binding

    // lnk — non-null; fatal trap if RTTI mismatches
    dl : Derived~ = bp;          // __fatal_null_dyncast() if bp does not point to Derived

    // ref — non-null; fatal trap if RTTI mismatches
    dr : Derived& = d;           // ref<Base> bound to d
    // dr2 : Derived& = br;      // would trap if br does not point to a Derived

    return get_extra_fn(*dp);    // → 99
}
```

**Transitive hierarchies:**

Dynamic downcast works through any depth of inheritance:

```k
// C → B → A: ptr<C> from ptr<A>
ap : A* = &c_obj;
cp : C* = ap;    // RTTI check: matches only if *ap is actually a C
```

**Interface downcast:**

A `ptr<IBase>` can be dynamically downcast to `ptr<Derived>` where `Derived` implements `IBase`:

```k
ip : IBase* = &d;    // static upcast to interface
dp : Derived* = ip;  // dynamic downcast via RTTI
```

---

### 13.5 Owner upcast and downcast

The rules for assigning between owner types depend on whether the pointed type is
*polymorphic* (`class` or `interface`) or *non-polymorphic* (primitive, array, `struct`).

#### Non-polymorphic types

An owner may only be assigned to an owner of the **exact same type**:

| Source | Destination | Result |
|---|---|---|
| `T!` | `T!` | OK — implicit move |
| `T!` | `U!` (U ≠ T) | **Compile error** |

#### Polymorphic types — static upcast (derived → base)

When `Derived` inherits from `Base` (`class` or `interface` only), a `Derived!` can be
implicitly assigned to a `Base!`.  The pointer is adjusted at compile time (GEP).
Ownership is transferred; the source becomes `null`.

No warning is emitted — the relationship is verified at compile time.

```k
class Animal { Animal() {} }
class Dog : public Animal { Dog() : Animal() {} }

d : Dog!    = new Dog();
a : Animal! = d;           // static upcast; d ← null; a owns the Dog
```

#### Polymorphic types — dynamic downcast (base → derived)

When a `Base!` is assigned to a `Derived!`, a runtime RTTI check is required.  The compiler
always emits **Warning 0x5001** for such assignments.

Behaviour at runtime:

| RTTI result | Effect on source | Effect on destination |
|---|---|---|
| **Match** | source ← `null` | destination receives ownership; pointer adjusted (GEP) |
| **No match** | owned object deleted (destructor + `free`); source ← `null` | destination ← `null` |

```k
class Animal { Animal() {} }
class Dog    : public Animal { Dog()    : Animal() {} }
class Cat    : public Animal { Cat()    : Animal() {} }

a  : Animal! = new Dog();
d  : Dog!;                  // Warning 0x5001: dynamic owner downcast
d  = a;                     // RTTI OK: Dog → a ← null; d owns Dog

a2 : Animal! = new Cat();
d2 : Dog!;                  // Warning 0x5001
d2 = a2;                    // RTTI fail: Cat deleted; a2 ← null; d2 ← null
```

---

### 13.6 Explicit cast

A C-style cast converts an expression to a named type:

```k
x : int = (int) someDouble;
```

**Grammar:**

```
CastExpr:
    '(' TypeSpec ')' CastExpr
```

#### 13.6.1 Explicit cast for primitive types

The standard numeric conversion rules apply (truncation, sign extension, float↔int, etc.).  
See §13.1 and §13.2 for widening and narrowing rules.

```k
d : double = 3.99d;
i : int = (int) d;          // truncates to 3
b : byte = (byte) largeInt; // narrow
```

#### 13.6.2 Explicit cast for indirection types (ref / lnk / pin / ptr)

An explicit cast may be applied to any observer indirection type.  
Two cases are distinguished based on the direction of the cast:

**A. Explicit static upcast (Derived→Base)**

When the cast target type is a *base* of the expression's pointed type, the cast is a **static (compile-time) GEP upcast** — identical to the implicit upcast (§13.3).

Allowed source → target combinations:

| Source expression type        | Explicit cast target | Notes |
|-------------------------------|----------------------|-------|
| `ptr<Derived>`                | `(Base*)`            | GEP to base subobject |
| `lnk<Derived>`                | `(Base~)`            | GEP to base subobject |
| `pin<Derived>`                | `(Base^)`            | GEP to base subobject |
| `ref<Derived>` (via variable) | `(Base&)`            | GEP to base subobject |
| `lnk<Derived>`                | `(Base*)`            | Cross-kind: GEP; result is nullable |
| `pin<Derived>`                | `(Base*)`            | Cross-kind: GEP; result is nullable |
| `ptr<Derived>`                | `(Base~)`            | Cross-kind: GEP; note: ptr is nullable but ~ is non-null (null-check inserted) |

```k
struct Base { val : int; Base(v : int) : val(v) {} }
struct Derived : public Base { extra : int; Derived(v : int) : Base(v), extra(0) {} }

d  : Derived(42);
pd : Derived* = &d;
pb : Base* = (Base*) pd;    // explicit static upcast: GEP
lb : Base~ = (Base~) pd;    // cross-kind with null-check
```

**B. Explicit dynamic downcast (Base→Derived)**

When the cast target type is a *derived class* (or interface implementor) of the expression's pointed type, the cast is a **dynamic (RTTI) downcast** — identical to the implicit dynamic downcast (§13.4).

Note: explicit cast to `(T!)` is **not permitted** — owner values may only be produced by
`new` or transferred from another owner variable.

Applies only to `class` and `interface` types. **Not allowed for `struct` types** (no RTTI).

| Source expression type | Explicit cast target | Null-on-mismatch behaviour |
|------------------------|----------------------|---------------------------|
| `ptr<Base>`            | `(Derived*)`         | null assigned |
| `ptr<Base>`            | `(Derived^)`         | null assigned |
| `ptr<Base>`            | `(Derived~)`         | fatal trap (`__fatal_null_dyncast`) |
| `lnk<Base>`            | `(Derived~)`         | fatal trap |
| `lnk<Base>`            | `(Derived*)`         | null assigned |
| `ref<Base>`            | `(Derived&)`         | fatal trap |

```k
class Base {
    public val : int;
    public Base(v : int) : val(v) {}
    public dummy() : int { return 0; }
}
class Derived : public Base {
    public extra : int;
    public Derived(v : int) : Base(v), extra(99) {}
    public get_extra() : int { return extra; }
}

get_fn(d : Derived&) : int { return d.get_extra(); }

test() : int {
    d  : Derived(42);
    bp : Base* = &d;          // static upcast

    dp  : Derived* = (Derived*) bp;   // explicit downcast — null if RTTI fails
    dl  : Derived~ = (Derived~) bp;   // explicit downcast — fatal if RTTI fails
    dp2 : Derived^ = (Derived^) bp;   // explicit downcast — null if RTTI fails

    return get_fn(*dp);    // → 99 (if dp is non-null)
}
```

**C. Error conditions**

| Situation | Result |
|-----------|--------|
| Source and target have no inheritance relationship | Compile error `0x40033` |
| Target is a `struct` type and source is derived class (no RTTI) | Compile error `0x40033` |
| Ref source and target have no inheritance relationship | Compile error `0x40034` |
| Target type name cannot be resolved | Compile error `0x40035` |

#### 13.6.3 Implicit cast in function call arguments

When a function expects a `Base*`, `Base~`, `Base^`, or `Base&` parameter, and the caller passes a `Derived*`, `Derived~`, `Derived^`, or `Derived&` expression, the compiler automatically inserts a **static upcast** — no explicit cast syntax is needed.

```k
get_val(p : Base*) : int { return p->val; }

test() : int {
    d : Derived(66);
    pd : Derived* = &d;
    return get_val(pd);    // implicit upcast: ptr<Derived> → ptr<Base>
}
```

This also works for `ref<ptr<Derived>>` when the parameter expects `ptr<Base>` — the reference is automatically loaded and the pointer is upcast.

### 13.7 Implicit indirection-to-bool conversion

Indirection types — `T*`, `T~`, `T^`, `T!` — and the `null` literal are implicitly
convertible to `bool` wherever a boolean is expected:

| Source | Result |
|---|---|
| Non-null `T*`, `T~`, `T^`, `T!` | `true` |
| Null `T*`, `T^`, `T!` | `false` |
| `null` literal | `false` |

This applies in `if`/`while`/`for` conditions, logical operators (`&&`, `||`, `!`),
and any other `adapt_type(expr, bool)` context.

References (`T&`) are **not** implicitly convertible to `bool` — they can never be null.

```k
p : int* = get_ptr();
if (p) { val : int = *p; }   // p is non-null → true
if (!p) { /* null */ }        // !p → p == null
```

See also: [Logical operators — implicit boolean conversion](../expressions/binary.md#implicit-boolean-conversion-for-indirection-types).

---


## 14. Const-ness

The `const` qualifier marks a variable or parameter as **immutable after construction**.
Const-ness is a **compile-time** property only; it has no impact on the generated IR code.

---

### 14.1 Const variables and parameters

The `const` keyword is a **declaration specifier** placed before the variable name, or a **type qualifier** placed before the base type in the type specifier, or both. All three forms are **semantically identical**:

```k
const x   : int = 42;       // const as specifier
y         : const int = 42; // const as type qualifier
const z   : const int = 42; // both — identical to the two above
```

Similarly for parameters:

```k
f(const n : int) : int { return n; }   // specifier form
f(n : const int) : int { return n; }   // type qualifier form — identical
f(const n : const int) : int { return n; } // both forms — identical
```

For indirection types, the `const` qualifier always applies to the **pointed-at object**, whether it is specified via the variable specifier or via the type:

```k
const lnk : int~  = &x;  // "const lnk : int~"   → link to const int
lnk : const int~  = &x;  // "lnk : const int~"   → link to const int — identical
const lnk : const int~ = &x; // both — identical
```

A `const` variable/parameter:
- **must** be initialised at its declaration (or at construction for parameters).
- **cannot** be assigned after initialisation.
- **cannot** be incremented or decremented (`++`, `--`, prefix or postfix).

```k
const x : int = 5;
x = 6;   // Error: cannot assign to a const variable
x++;     // Error: cannot apply '++' to a const variable
```

---

### 14.2 Const type qualifier in type specifiers

`const` can appear as a **type qualifier** directly before the base type in any type specifier.
This is equivalent to placing `const` as a variable specifier (see §14.1).

```k
x   : int      = 10;
cp  : const int* = &x;    // pointer to const int — the pointed value cannot be modified
lnk : const int~ = &x;   // link to const int
```

The full grammar for type specifiers with `const`:

```
TypeSpec:
    'const' BaseTypeSpec { TypeSuffix }   -- const applied to the base type
    BaseTypeSpec { TypeSuffix }

BaseTypeSpec:
    FundamentalType
    QualifiedIdentifier
```

`const int*` means **pointer to const int**: the pointer itself can be rebound, but the object
pointed to cannot be modified through it.

> **Note:** `const` always applies to the **base type** that follows it — before any indirection
> suffixes (`*`, `~`, `^`, `&`). It is not possible to write a "const pointer" (i.e. a pointer
> whose binding is immutable); for that, use a pinned (`^`) or reference (`&`).

The three forms of a const pointer declaration are all equivalent:

```k
const p  : int* = &x;         // specifier form
p : const int*   = &x;         // type qualifier form
const p : const int* = &x;     // both — all three mean "pointer to const int"
```

---

### 14.3 Const and indirection types

For all four indirection kinds, `const` applies to the **pointed-at object**, not to the pointer/reference/link/pinned itself:

| Indirection type  | Mutable binding? | Pointed value mutable? |
|-------------------|-----------------|------------------------|
| `T&`              | no (always immutable) | yes |
| `const T&`        | no              | no  |
| `T~`              | yes (rebindable)| yes |
| `const T~`        | yes (rebindable)| no  |
| `T^`              | no (immutable)  | yes |
| `const T^`        | no              | no  |
| `T*`              | yes             | yes |
| `const T*`        | yes             | no  |

> Immutable-binding indirections (references `&` and pinned `^`) are not further constrained by `const`
> on the binding itself (they are already immutable by design), but `const` still restricts modifications
> to the pointed object.

---

### 14.4 Const pointer/link compatibility rules

A `const T*` (or `const T~`) **can** be initialised or assigned from a `T*` (or `T~`) — this is a safe widening:

```k
x  : int  = 5;
p  : int* = &x;
cp : const int* = p;  // OK — widening: mutable → const
```

The reverse is **forbidden** — assigning a `const T*` to a `T*` would allow modification of a const object:

```k
cp : const int* = &x;
p  : int* = cp;   // Error: cannot assign pointer-to-const to pointer-to-mutable
```

Similarly for links:

```k
clnk : const int~ = &x;
lnk  : int~ = clnk;   // Error: cannot initialise link-to-mutable from link-to-const
```

A const variable can only be addressed or referenced by a const indirection:

```k
const x : int = 42;
lnk  : int~       = &x;  // Error: '&x' has type 'const int~'; cannot bind to mutable link
clnk : const int~ = &x;  // OK
```

---

### 14.5 Const and function overloading

`const` on a **by-value** parameter is part of the function's implementation contract (the caller's type is unaffected).
Two functions differing only in the `const`-ness of a by-value parameter are **ambiguous** at the call site:

```k
pick(n : int) : int       { return 1; }
pick(const n : int) : int { return 2; }

pick(0);  // Error: ambiguous — both overloads are equally viable
```

Their **mangled names** are distinct (the `const` modifier `K` is emitted in the parameter encoding),
so the two definitions can coexist as separate symbols, but resolution from a call with a plain `int`
argument is rejected as ambiguous.

---

### 14.6 Name mangling

The const qualifier is encoded in the mangled symbol name using the modifier prefix **`K`**:

| Source type | Mangling |
|-------------|----------|
| `int`       | `i`      |
| `const int` | `Ki`     |
| `int*`      | `Pi`     |
| `const int*`| `PKi`    |
| `int~`      | `Li`     |
| `const int~`| `LKi`    |

A function `f(const n : int) : int` has its parameter encoded as `Ki` instead of `i`.

---

### 14.7 Const member functions

A member function declared with the `const` specifier receives its implicit `this` parameter as a **const reference** (`const Struct&`) instead of a mutable reference.

Inside a const member function:
- All fields (direct and inherited) are implicitly const — read-only.
- Only other const member functions may be called on `this`.
- `++` / `--` on any field is a compile-time error.

```k
struct Point {
    x : int;
    y : int;
    Point(a : int, b : int) : x(a), y(b) {}

    const sum() : int { return this.x + this.y; }  // OK: reads only
    // scale(f : int) called on a const Point → ERROR
}
```

A const member function can be called on both mutable **and** const objects/references.  
A mutable member function can only be called on mutable objects.

> `const` and `static` are incompatible on a member function: static functions have no `this` and the combination is a compile-time error.

#### Const/mutable overloading (classes)

In a **class**, a `const` and a mutable method may share the same name (overloaded on receiver constness).  
The mutable overload is selected on mutable receivers; the const overload is selected on const receivers:

```k
class C {
    public x : int;
    C() : x(5) {}
    get() : int       { return this.x; }       // mutable overload
    const get() : int { return this.x + 100; } // const overload
}
// mutable receiver → get() = 5
// const receiver   → const get() = 105
```

A mutable method in a derived class does **not** override a const base method — they occupy distinct vtable slots.

See [Structures — §12](../structs/structs.md#12-const-member-functions) and [Classes — §13](../structs/classes.md#13-const-member-functions-in-classes) for full details.

---

### 14.8 Const structs

A struct declared `const` ensures that **all** non-static member functions are treated as const.
Constructors and destructors are exempt.

A non-static member function that is **not** explicitly declared `const` inside a const struct is **automatically promoted to const** at compile time. A `Warning 30010` is emitted to signal the implicit promotion — add `const` explicitly to the declaration to suppress it.

```k
const struct ReadOnly {
    x : int;
    ReadOnly(v : int) : x(v) {}
    const get() : int { return this.x; }         // explicit const — no warning
    also_get() : int { return this.x; }          // Warning 30010: promoted to const
}
```

Inheritance rules:
- A `const` struct may only inherit from other `const` structs.
- A mutable struct may inherit from a `const` struct; inherited const methods remain const.

See [Structures — §13](../structs/structs.md#13-const-structs) for full details.

---

*See also:* [Keywords](keywords.md) · [Expressions](../expressions/expressions.md) · [Statements](../statements/statements.md) · [Structures](../structs/structs.md)
