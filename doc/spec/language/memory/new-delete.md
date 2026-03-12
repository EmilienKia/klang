# Dynamic Allocation — `new` and `delete`

[← Index](../index.md) · [Types — Owner (`!`)](../basic/types.md#7-owner-) · [Array Types](../basic/types.md#9-array-types)

K provides two operators for managing dynamically allocated memory: `new` (allocation and
construction) and `delete` (destruction and deallocation).

Dynamically allocated objects are exclusively managed through [owner types (`T!`)](../basic/types.md#7-owner-).
The C standard library functions `malloc` and `free` are used for the underlying memory
management; they are always available since all K executables and libraries are linked against
the C runtime.

---

## Contents

1. [`new` expression — single object](#1-new-expression--single-object)
2. [`new` expression — array](#2-new-expression--array)
3. [`delete` expression](#3-delete-expression)
4. [Ownership and lifetime](#4-ownership-and-lifetime)
5. [Interaction with other indirection types](#5-interaction-with-other-indirection-types)
6. [Grammar](#6-grammar)

---

## 1. `new` expression — single object

The `new` expression dynamically allocates memory for a single object of the specified type,
invokes its constructor, and returns an [owner (`T!`)](../basic/types.md#7-owner-) that
manages the allocated object.

**Syntax:**

```
'new' TypeName '(' [ ExpressionList ] ')'
```

`TypeName` is a plain type name (a qualified identifier or a primitive type keyword) — not a
type with indirection suffixes.  Allocating an abstract class or an interface directly is
forbidden (they cannot be instantiated); the compiler emits **Error 0x0057** in that case.

**Semantics (in order):**

1. `malloc(sizeof(T))` is called to allocate a raw memory block.
   If `malloc` returns null (allocation failure), behaviour is currently undefined.
2. The constructor of `T` matching the provided argument list is invoked on the allocated
   memory.
3. The resulting address is wrapped in a `T!` owner value and returned as the expression
   result.

**Return type:** `T!`

**Examples:**

```k
struct Point { x : int; y : int; Point(a : int, b : int) : x(a), y(b) {} }
class Node   { public val : int; public Node(v : int) : val(v) {} }

test() {
    p : Point! = new Point(3, 4);     // struct allocation
    n : Node!  = new Node(99);        // class allocation
    i : int!   = new int(0);          // primitive allocation
}   // p, n, i go out of scope → each object deleted automatically
```

**Unassigned result:**

If the result of `new` is not assigned to an owner variable — for example, it is used as a
bare expression statement or passed to a non-owner parameter — the compiler emits
**Warning 0x5010** and the object is deleted immediately after construction:

```k
new Foo(1);          // Warning 0x5010: result of 'new' immediately discarded
                     // Foo(1) is constructed then deleted on the same line
```

---

## 2. `new` expression — array

The array form of `new` dynamically allocates an array of elements and returns an owner of
the corresponding [array type](../basic/types.md#9-array-types).

**Syntax:**

```
'new' TypeName '[' [ Expression ] ']' [ '{' [ InitList ] '}' ]
'new' TypeName '{' [ InitList ] '}'
```

- `TypeName` is the **element** type (primitive, struct, or class — not abstract).
- The optional `Expression` between the brackets is the **array size** — either a
  compile-time integer constant (**static** allocation) or a runtime expression
  (**dynamic** allocation).
- The optional brace-enclosed `InitList` provides per-element initializers (only for static
  allocations).
- The **bare-brace** form (`new T{…}`) is syntactic sugar for `new T[]{…}` — an array with
  size inferred from the initializer list.  In particular, `new T{}` creates a valid empty
  array with 0 elements.

**Return type:**

| Kind | Return type | Description |
|------|-------------|-------------|
| Static (size known at compile time) | `T[N]!` | Owner of a sized array of `N` elements. |
| Dynamic (size is a runtime expression) | `T[]!` | Owner of an unsized array. |

### 2.1 Size determination

The array size is determined by exactly one of the following rules:

**Static (compile-time) forms:**

| Form | Size | Description |
|------|------|-------------|
| `new T[N]{…}` | `N` | Explicit compile-time size; init list may have 0 to `N` elements. |
| `new T[N]` | `N` | Explicit compile-time size, no init list; all elements default-initialized. |
| `new T[]{e₀, e₁, …, eₖ}` | `k` | Size inferred from non-empty init list (must have ≥ 1 element). |
| `new T[]{}` | `0` | Empty init list; valid empty array (0 elements). |
| `new T{e₀, e₁, …, eₖ}` | `k` | Bare-brace form; equivalent to `new T[]{e₀, …, eₖ}`. |
| `new T{}` | `0` | Bare-brace empty; equivalent to `new T[]{}` — valid empty array. |
| `new T[]` | — | **Error 0x4229**: no explicit size and no init list — size cannot be inferred. |

**Dynamic (runtime) forms:**

| Form | Size | Description |
|------|------|-------------|
| `new T[expr]` | runtime | `expr` is a runtime expression; all elements default-initialized. |

When the bracket expression is not a compile-time integer literal, it is treated as a
**dynamic-sized** allocation:

- The size expression must be convertible to `unsigned int` (**Error 0x4221** if not).
- **Brace initializer lists are forbidden** with dynamic-sized arrays (**Error 0x422A**).
  All elements are default-initialized (zero for primitives, default constructor for structs).
- For struct element types, a default constructor (zero arguments) must exist.
- The result type is `T[]!` — an owner of an **unsized** array type.

### 2.2 Element initialization

**Primitive element types:**

- Elements with an explicit initializer expression are set to that value.
- Elements without an initializer (beyond the init list, or empty slots) are
  **zero-initialized**.
- Expressions are permitted: `new int[3]{1+2, 3*4, 10/2}`.

**Struct / class element types:**

- An explicit element must be a constructor invocation: `new Point[2]{Point(1,2), Point(3,4)}`.
- Empty slots (`, ,`) or elements beyond the init list use the **default constructor** (zero
  arguments).  If no default constructor exists, this is a compile-time error.
- The element type must not be abstract (**Error 0x4226**).

**Empty slots in the init list:**

Two consecutive commas represent an empty slot — the corresponding element is
default-initialized:

```k
arr : int[3]! = new int[3]{1, , 3};    // arr[1] = 0 (default)
```

**Too many initializers:**

If the init list has more elements than the explicit array size, the compiler emits
**Error 0x4222**.

**Fewer initializers:**

If the init list has fewer elements than the explicit array size (but at least one), the
compiler emits **Warning 0x4223** and the remaining elements are default-initialized.

### 2.3 Internal representation

A dynamically allocated array is stored as:

```
{ uint32 count; T[N] data; }     // static (compile-time size N)
{ uint32 count; T[0] data; }     // dynamic (runtime size; data extends past the struct)
```

This is the same [internal representation](../basic/types.md#91-internal-representation) used
for stack-allocated arrays.  The entire struct (including the `count` header) is allocated with
a single `malloc` call.  For dynamic-sized arrays, the allocation size is
`header_size + sizeof(T) * n` where `n` is the runtime size value.

Elements are initialized in order (index 0 first).

### 2.4 Element access

Elements of a dynamically allocated array are accessed with the standard subscript operator:

```k
arr : int[3]! = new int[3]{10, 20, 30};
x : int = arr[1];       // x = 20
arr[2] = 99;            // arr[2] is now 99
```

A **runtime bounds check** is performed on every subscript access: the index is compared
(unsigned) against the element count stored in the array header.  If the index is out of
bounds, the program prints a diagnostic to `stderr` and calls `abort()`:

```
runtime error: array index out of bounds (index=5, size=3)
```

### 2.5 Examples

```k
// Primitive array — explicit size
a : int[5]! = new int[5]{1, 2, 3, 4, 5};
delete a;

// Primitive array — size inferred from init list
b : int[3]! = new int[]{10, 20, 30};

// Primitive array — no init (all zeros)
c : int[4]! = new int[4];

// Primitive array — empty array
d : int[0]! = new int[]{};

// Bare-brace form — size inferred (equivalent to new int[]{10, 20, 30})
e : int[3]! = new int{10, 20, 30};

// Bare-brace form — empty array (equivalent to new int[]{})
f : int[0]! = new int{};

// Struct array — constructor calls
struct Pair { x : int; y : int; Pair(a : int, b : int) : x(a), y(b) {} }
pairs : Pair[2]! = new Pair[]{Pair(1, 2), Pair(3, 4)};

// Struct array — default construction
struct Item { val : int = 0; Item() { val = 42; } ~Item() {} }
items : Item[3]! = new Item[3]{};     // 3 Items, each default-constructed
delete items;                          // 3 destructors called in reverse order

// Dynamic-sized array — size from runtime expression
n : unsigned int = 10;
dyn : int[]! = new int[n];            // 10 ints, all zero-initialized
dyn[0] = 42;
delete dyn;

// Dynamic-sized struct array — default ctor called for each element
count : unsigned int = 5;
dynItems : Item[]! = new Item[count]; // 5 Items, each default-constructed
delete dynItems;                       // 5 destructors called in reverse order
```

---

## 3. `delete` expression

The `delete` expression explicitly destroys and frees the object held by an owner, then sets
the owner to `null`.

**Syntax:**

```
'delete' OwnerExpr
```

`OwnerExpr` must be a **modifiable lvalue** of an owner type (`T!`, `T[N]!`, or `T[]!`).
Passing a non-owner indirection to `delete` is a **compile-time error** (**Error 0x005A**).

**Semantics (only when the owner is non-null; no-op otherwise):**

### 3.1 Single-object delete

1. The destructor of the **dynamic type** of the owned object is called:
   - For `class` and `interface` types: virtual dispatch — the most-derived destructor is
     called first, then each base destructor in reverse construction order.
   - For `struct` and primitive types: direct call to the declared destructor (if any).
2. `free(ptr)` is called to release the memory block.
3. The owner variable is set to `null`.

### 3.2 Array delete

When the owner holds a dynamically allocated array (`T[N]!` or `T[]!`):

1. Destructors are called on each element **in reverse order** (last element first, down to
   index 0).  For static arrays, the loop is unrolled at compile time; for dynamic arrays,
   an IR loop is emitted.  For primitive element types, no destructor call is needed.
2. `free(ptr)` is called to release the entire array allocation (header + data).
3. The owner variable is set to `null`.

The reverse destruction order mirrors the forward construction order used by `new`.

### 3.3 Null safety

If the owner is already `null`, `delete` is a **no-op** — no destructor is called, no
`free` is issued.  This makes double-delete safe:

```k
p : Foo! = new Foo(10);
delete p;              // destructor called; memory freed; p ← null
delete p;              // no-op: p is already null
```

### 3.4 Virtual dispatch on delete

When the owned object's static type is a base class or interface, but the dynamic type is a
derived class, the derived destructor is invoked first:

```k
class Base    { ~Base()    {} }
class Derived : public Base { ~Derived() {} }

test() {
    b : Base! = new Derived();   // static: Base!;  dynamic: Derived
    delete b;                    // calls ~Derived() then ~Base()
}
```

**Result type:** `void`.  `delete` may only be used as an expression statement; its value
cannot be used in a larger expression.

---

## 4. Ownership and lifetime

### Scope-based deletion

An owner that is non-null when it goes out of scope is **automatically deleted**.  This is
equivalent to the compiler inserting an implicit `delete` at the end of every scope in which
an owner is declared:

```k
{
    p : Foo! = new Foo(1);
    // ... use p ...
}   // implicit: delete p;
```

This applies equally to single-object owners and array owners:

```k
{
    arr : int[3]! = new int[3]{1, 2, 3};
}   // implicit: delete arr;  (free is called, no element destructors for primitives)
```

```k
{
    items : Item[2]! = new Item[2]{};
}   // implicit: delete items;  (~Item() called on element 1, then element 0, then free)
```

This applies to:

- the end of a function or block;
- the destruction of an enclosing object (owner fields are deleted in reverse declaration
  order, before the enclosing destructor body returns);
- a function return where the return value is not assigned to any variable (see below).

### Assignment and prior deletion

When an owner is the target of a move-assignment, its previous object (if non-null) is
deleted **before** the new ownership is accepted:

```k
a : Foo! = new Foo(1);
b : Foo! = new Foo(2);
b = a;     // step 1: Foo(2) deleted;
           // step 2: ownership of Foo(1) transferred from a to b;
           // step 3: a ← null
```

### Assigning `null`

Assigning `null` to a non-null owner deletes the owned object and sets the owner to `null`.
Assigning `null` to an already-null owner is a no-op:

```k
p : Foo! = new Foo();
p = null;               // Foo deleted; p ← null
p = null;               // no-op
```

### Unassigned return value

If a function returns a `T!` and the caller does not assign the result to an owner variable,
the compiler emits **Warning 0x5010** and the returned object is deleted immediately:

```k
make() : Foo! { return new Foo(5); }

test() {
    make();              // Warning 0x5010: Foo(5) constructed and immediately deleted
    obj : Foo! = make(); // OK: obj takes ownership of Foo(5)
}
```

---

## 5. Interaction with other indirection types

### Observer assignment (owner → non-owner)

An owner may be assigned to any of the four non-owner indirection types.  The raw address is
copied but the owner **retains exclusive ownership**.  The receiving indirection is a
**non-owning observer** — it must not outlive the owner.

| Assignment form | Observer type | Null-check |
|---|---|---|
| `obs : T* = owner;` | Pointer — mutable, nullable | — |
| `obs : T^ = owner;` | Pinned — immutable, nullable | — |
| `lnk : T~ = owner;` | Link — mutable, non-null | Yes (at binding site) |
| `ref : T& = *owner;` | Reference (via dereference) | Yes (from `*owner`) |

```k
owner : Foo! = new Foo(42);
obs   : Foo* = owner;          // observer pointer; owner still owns Foo
lnk   : Foo~ = owner;          // observer link; non-null check inserted
ref   : Foo& = *owner;         // observer reference (via dereference)
```

> **Warning:** Observers become **dangling** once the owner is deleted, moved, or goes out
> of scope.  The compiler does not track observer lifetimes — this is the programmer's
> responsibility.

### Non-owner parameter (pass by observer)

Passing an owner to a function that expects a `T*`, `T~`, `T^`, or `T&` parameter copies the
raw address **without** transferring ownership.  The called function is an observer:

```k
use(p : Foo*) : int { return p->val; }   // observer parameter

test() {
    obj : Foo! = new Foo(7);
    use(obj);        // raw address passed; obj retains ownership
    // obj still valid here
}   // obj deleted here
```

To transfer ownership into a function, the parameter must be declared as `T!`:

```k
consume(f : Foo!) { /* f owns Foo; deleted when consume returns */ }

test() {
    obj : Foo! = new Foo(7);
    consume(obj);    // MOVE: obj ← null; f owns Foo inside consume
}
```

---

## 6. Grammar

`new` and `delete` are **keywords**.

```
Keyword: (one of) -- (added)
    new   delete
```

They appear in the `UnaryExpr` production as special prefix forms:

```
UnaryExpr:
    'new' TypeName '(' [ ExpressionList ] ')'                        -- NewExpr (single object)
    | 'new' TypeName '[' [ Expression ] ']' [ BraceInitList ]        -- NewArrayExpr
    | 'new' TypeName BraceInitList                                   -- NewBareArrayExpr
    | 'delete' CastExpr                                              -- DeleteExpr
    | ( '++' | '--' | '*' | '&' | '+' | '-' | '!' | '~' ) CastExpr
    | PostfixExpr

TypeName:
    QualifiedIdentifier
    | FundamentalTypeSpec

BraceInitList:
    '{' [ InitList ] '}'

InitList:
    [ Expression ] { ',' [ Expression ] }
```

**How `new` is parsed:**

After consuming the `new` keyword, the parser calls `parse_type_spec(stop_before_bracket=true)`
to parse the base type without consuming any trailing `[`:

- If the next token is `[`, this is the **array form with brackets**.  The parser reads the
  optional size expression (any expression, not just a literal) and the closing `]`, then
  optionally parses a brace initializer list `{ … }`.
- If the next token is `{`, this is the **bare-brace array form** (`new T{…}`).  The size is
  inferred from the initializer list.  This is equivalent to `new T[]{…}`.
- Otherwise, this is the **single-object form**, and a parenthesised argument list `( … )` is
  expected.

`TypeName` accepts only a bare type name — indirection suffixes (`*`, `!`, `&`, …) are not
permitted inside a `new` expression.

**Result types:**

| Form | Result type |
|------|-------------|
| `new T(args)` | `T!` |
| `new T[N]{…}` (N = compile-time constant) | `T[N]!` |
| `new T[expr]` (expr = runtime expression) | `T[]!` |
| `new T{e₀, …, eₖ}` | `T[k]!` |
| `new T{}` | `T[0]!` |

`delete` returns `void`.

The `!` type suffix used to declare owner variables is part of the `TypeSuffix` production:

```
TypeSuffix:
    '[' [ IntegerLiteral ] ']'   -- array (sized or unsized)
    | '!'                        -- owner (move-only, nullable, exclusive ownership)
    | '&'                        -- reference (immutable binding, non-null)
    | '~'                        -- link (mutable binding, non-null)
    | '^'                        -- pinned (immutable binding, nullable)
    | '*'                        -- pointer (mutable binding, nullable)
```

---

## Error and warning codes

| Code | Severity | Condition |
|------|----------|-----------|
| 0x0057 | Error | Cannot `new` an abstract class (single-object form). |
| 0x005A | Error | `delete` applied to a non-owner type. |
| 0x4221 | Error | Array size expression for `new[]` is not convertible to `unsigned int`. |
| 0x4222 | Error | Init list has more elements than the explicit array size. |
| 0x4223 | Warning | Init list has fewer elements than the array size; remaining elements are default-initialized. |
| 0x4224 | Error | Cannot convert an init list element to the array element type. |
| 0x4225 | Error | Struct for array element type is not resolved. |
| 0x4226 | Error | Cannot `new` an array of an abstract class. |
| 0x4227 | Error | No matching constructor for an element in the init list. |
| 0x4228 | Error | No matching single-parameter constructor for an implicit element. |
| 0x4229 | Error | Cannot infer array size: no explicit size and no init list provided. |
| 0x422A | Error | Brace initializer lists are not allowed for dynamically-sized `new[]` arrays. |
| 0x5010 | Warning | Result of `new` (or function returning `T!`) is immediately discarded — the object is deleted right after construction. |

---

*See also:* [Types — Owner (`!`)](../basic/types.md#7-owner-) · [Array Types](../basic/types.md#9-array-types) · [Unary Operators](../expressions/unary.md) · [Assignment Operators](../expressions/assignment.md) · [Destructors](../structs/destructors.md)

