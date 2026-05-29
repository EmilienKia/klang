# Assignment Operators

[← Index](../index.md) · [Expressions](expressions.md)

Assignment operators store a value into a variable or memory location.

> **Operator overloading:** When the left-hand operand is of a user-defined aggregate type (struct, class, or interface), the compiler looks for a matching assignment operator function (`operator =`, `operator +=`, etc.) instead of applying built-in semantics. See [Operator Overloading](../functions/operators.md) for the full specification.

---

## Contents
1. [Simple assignment](#1-simple-assignment)
2. [Assignment through indirection types](#2-assignment-through-indirection-types)
   - [Reference (`T&`)](#reference-t--transparent-object-semantics)
   - [Link (`T+`)](#link-t--rebind-or-transparent-depending-on-rhs-type)
   - [View (`T?`)](#view-t--immutable-binding)
   - [Pointer (`T*`)](#pointer-t--rebind)
   - [Owner (`T!`)](#owner-t--move-semantics)
3. [Compound assignment operators](#3-compound-assignment-operators)
4. [Operand requirements](#4-operand-requirements)

---
## 1. Simple assignment

`lhs = rhs` evaluates `rhs`, converts it to the type of `lhs` if needed, and stores the result in `lhs`.  
The value of the assignment expression is the value stored.

### Grammar
```
AssignmentExpr:
    ConditionalExpr [ AssignmentOperator AssignmentExpr ]
AssignmentOperator: (one of)
    =   *=   /=   %=   +=   -=   >>=   <<=   &=   ^=   |=
```

Assignment is right-associative: `a = b = c` assigns `c` to `b`, then the result to `a`.

**Examples:**
```k
x = 42;
p.b = 72;
*ptr = value;
arr[i] = v;
```

---
## 2. Assignment through indirection types

The four indirection types have different assignment semantics:

### Reference (`T&`) — transparent object semantics

Assigning to a reference variable assigns to the **object it refers to**. The binding itself never changes.

```k
x : int = 10;
y : int = 20;
r : int& = x;
r = y;              // copies y's value into x; r remains bound to x
r = 99;             // assigns 99 to x
```

Taking the address of a reference with `&r` produces a `T+` pointing to the same object.

### Link (`T+`) — rebind or transparent, depending on RHS type

The compiler distinguishes between a **rebind** and an **assignment to the pointed-to object** by the type of the right-hand side:

- If the RHS is an **indirection of the same element type** (`T+`, `T?`, or `T*`), the assignment **rebinds** the link — the link is made to point to the new address.
- Otherwise the assignment is **transparent** — it assigns to the linked object.

```k
x : int = 10;
y : int = 20;
lnk : int+ = &x;

lnk = 99;           // transparent: assigns 99 to x (RHS is int, not int*)
lnk = &y;           // REBIND: lnk now points to y (RHS is int+)
lnk = &x;           // REBIND back to x
```

Rebinding a link from a nullable source (`T?` or `T*`) emits a **compile-time warning** and inserts a **runtime null-check** (throws `NullAssignationError` if null).

### View (`T?`) — immutable binding

Assigning to a view variable after its initialisation is a **compile-time error**.

```k
view : int? = &x;
view = &y;           // ERROR: view is immutable — cannot be rebound
```

To modify the pointed-to object, use dereference:
```k
*view = 99;          // modifies the object (with null-check at runtime)
```

### Pointer (`T*`) — rebind

Assigning to a pointer variable always **rebinds** it — the pointer is made to point to the new address.  
To modify the pointed-to object, use dereference.

```k
p : int* = &x;
p = &y;             // rebind: p now points to y
p = null;           // rebind: p now points to nothing (null)
*p = 99;            // modifies y (with null-check)
```

The RHS of a pointer assignment must be a pointer (`T*`), a link (`T+`), a view (`T?`),
or `null`.  Assigning a plain value is a compile-time error.

### Owner (`T!`) — move semantics

Assignment to an owner variable always performs a **move**: ownership is transferred from the
source to the destination.  The source owner is set to `null` after the transfer.  If the
destination already held an object, that object is **deleted first** (destructor called +
`free` issued).

```k
a : Foo! = new Foo(1);   // a owns Foo(1)
b : Foo!;                // b is null

b = a;                   // MOVE: a ← null; b now owns Foo(1)
b = new Foo(2);          // Foo(1) deleted; b now owns Foo(2)
b = null;                // Foo(2) deleted; b ← null
```

The right-hand side of a `T!` assignment must be:
- another `T!` variable (move from it), or
- `null` (deletes the current object if any), or
- a `new T(args)` expression.

Assigning a raw pointer/link/view to a `T!` variable is a **compile-time error** — an owner
can only be populated by `new` or by a move from another owner.

**Summary of owner assignment:**

| RHS type | Effect |
|---|---|
| `T!` variable | Move: RHS ← null; LHS takes ownership (LHS previous object deleted) |
| `new T(args)` | LHS takes ownership of fresh object (LHS previous object deleted) |
| `null` | LHS previous object deleted; LHS ← null |
| `T*`, `T+`, `T?`, `T&` | **Compile-time error** |

### Summary table (all indirection types)

| LHS type | `x = val` (val is `T`) | `x = &y` / `x = lnk` (val is `T+`, `T*`, `T?`) | `x = owner` (val is `T!`) |
|---|---|---|---|
| `T&` | Assigns `val` to the referenced object | Compile-time error (no rebind) | Compile-time error |
| `T+` | Assigns `val` to the linked object | **Rebinds** `x` to point to `y` | Copies raw address (observer; owner retains ownership) |
| `T?` | Compile-time error (no rebind) | Compile-time error (no rebind) | Compile-time error |
| `T*` | Compile-time error (must use `*x = val`) | **Rebinds** `x` to point to `y` | Copies raw address (observer; owner retains ownership) |
| `T!` | Compile-time error | Compile-time error | **Move**: source ← null; destination takes ownership |

### Upcast (aggregate types)

When `Derived` inherits from `Base`, an indirection to `Derived` can be assigned to an indirection to `Base` (**static upcast**):

```k
struct Animal { legs : int; Animal(n : int) : legs(n) {} }
struct Dog : public Animal { Dog(n : int) : Animal(n) {} }

use() {
    d : Dog(4);

    // ref: only at construction
    r : Animal& = d;       // OK — binds to Base sub-object of d
    // r = d2;             // ERROR: ref cannot rebind

    // lien: init and rebind
    lnk : Animal+ = &d;    // OK
    d2 : Dog(2);
    lnk = &d2;             // OK: rebind to another Derived

    // ptr: init and rebind
    ptr : Animal* = &d;    // OK
    ptr = &d2;             // OK

    //view: only at construction
    p : Animal? = &d;      // OK
    // p = &d2;            // ERROR: view cannot rebind

    // Nullable source → non-null target: warning + runtime null-check
    dp : Dog* = &d;
    lnk2 : Animal+ = dp;   // Warning 0x4505 — null-check at runtime

    // Type mismatch
    // struct Cat {}
    // c : Cat();
    // bad : Animal* = &c; // ERROR 0x4700: Cat does not inherit from Animal
}
```

See [Types — §13.3](../basic/types.md#133-static-indirection-upcast-aggregate-types) for the complete specification.

---
## 3. Compound assignment operators

Compound assignment applies a binary operation and stores the result back in the left operand.  
`lhs op= rhs` is semantically equivalent to `lhs = lhs op rhs` (with `lhs` evaluated once).

| Operator | Equivalent to |
|----------|---------------|
| `+=`     | `lhs = lhs + rhs` |
| `-=`     | `lhs = lhs - rhs` |
| `*=`     | `lhs = lhs * rhs` |
| `/=`     | `lhs = lhs / rhs` |
| `%=`     | `lhs = lhs % rhs` |
| `&=`     | `lhs = lhs & rhs` |
| `\|=`    | `lhs = lhs \| rhs` |
| `^=`     | `lhs = lhs ? rhs` |
| `<<=`    | `lhs = lhs << rhs` |
| `>>=`    | `lhs = lhs >> rhs` |

**Examples:**
```k
r += i;         // r = r + i
i = i - 1;     // could also be written: i -= 1
*p += i + j;   // dereference and add (with null-check)
p.b += 12;     // member field compound assignment
n += 1;        // increment by 1 (same as ++n)
```

---
## 4. Operand requirements

- The left-hand side (`lhs`) must be an assignable location (lvalue): a variable, a parameter, a dereferenced pointer/link/view, an array subscript, or a struct field access.
- Assigning to a `T?` (view) variable directly (rebind) is a compile-time error.
- The right-hand side (`rhs`) is an expression of a compatible type.
- Implicit widening or narrowing conversions are applied as described in [Types — Implicit conversions](../basic/types.md#13-implicit-conversions).

---
*See also:* [Binary Operators](binary.md) · [Expressions](expressions.md) · [Types](../basic/types.md) · [Unary Operators](unary.md) · [Dynamic Allocation](../memory/new-delete.md) · [Operator Overloading](../functions/operators.md)
