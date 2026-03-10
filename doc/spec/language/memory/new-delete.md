# Dynamic Allocation — `new` and `delete`

[← Index](../index.md) · [Types — Owner (`!`)](../basic/types.md#7-owner-)

K provides two operators for managing dynamically allocated memory: `new` (allocation and
construction) and `delete` (destruction and deallocation).

Dynamically allocated objects are exclusively managed through [owner types (`T!`)](../basic/types.md#7-owner-).
The C standard library functions `malloc` and `free` are used for the underlying memory
management; they are always available since all K executables and libraries are linked against
the C runtime.

---

## Contents

1. [`new` expression](#1-new-expression)
2. [`delete` expression](#2-delete-expression)
3. [Ownership and lifetime](#3-ownership-and-lifetime)
4. [Interaction with other indirection types](#4-interaction-with-other-indirection-types)
5. [Grammar](#5-grammar)

---

## 1. `new` expression

The `new` expression dynamically allocates memory for an object of the specified type,
invokes its constructor, and returns an [owner (`T!`)](../basic/types.md#7-owner-) that
manages the allocated object.

**Syntax:**

```
'new' TypeName '(' [ ExpressionList ] ')'
```

`TypeName` is a plain type name (a qualified identifier or a primitive type keyword) — not a
type with indirection suffixes.  Allocating an abstract class or an interface directly is
forbidden (they cannot be instantiated); the compiler emits **Error 0x5000** in that case.

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

## 2. `delete` expression

The `delete` expression explicitly destroys and frees the object held by an owner, then sets
the owner to `null`.

**Syntax:**

```
'delete' OwnerExpr
```

`OwnerExpr` must be a **modifiable lvalue** of an owner type (`T!`).  Passing a non-owner
indirection to `delete` is a **compile-time error**.

**Semantics (only when the owner is non-null; no-op otherwise):**

1. The destructor of the **dynamic type** of the owned object is called:
   - For `class` and `interface` types: virtual dispatch — the most-derived destructor is
     called first, then each base destructor in reverse construction order.
   - For `struct` and primitive types: direct call to the declared destructor (if any).
2. `free(ptr)` is called to release the memory block.
3. The owner variable is set to `null`.

If the owner is already `null`, `delete` is a **no-op** — no destructor is called, no
`free` is issued.

**Examples:**

```k
test() {
    p : Foo! = new Foo(10);
    delete p;              // destructor called; memory freed; p ← null
    delete p;              // no-op: p is already null
}
```

**Virtual dispatch on delete:**

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

## 3. Ownership and lifetime

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

## 4. Interaction with other indirection types

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

## 5. Grammar

`new` and `delete` are **keywords**.

```
Keyword: (one of) -- (added)
    new   delete
```

They appear in the `UnaryExpr` production as special prefix forms:

```
UnaryExpr:
    'new' TypeName '(' [ ExpressionList ] ')'    -- NewExpr
    | 'delete' CastExpr                           -- DeleteExpr
    | ( '++' | '--' | '*' | '&' | '+' | '-' | '!' | '~' ) CastExpr
    | PostfixExpr

TypeName:
    QualifiedIdentifier
    | FundamentalTypeSpec
```

`TypeName` accepts only a bare type name — indirection suffixes (`*`, `!`, `&`, …) are not
permitted inside a `new` expression.  The result type of `new T(...)` is always `T!`.

`delete` returns `void`.  `new T(...)` returns `T!`.

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

*See also:* [Types — Owner (`!`)](../basic/types.md#7-owner-) · [Unary Operators](unary.md) · [Assignment Operators](assignment.md) · [Destructors](../structs/destructors.md)

