# Unary Operators

[← Index](../index.md) · [Expressions](expressions.md)

Unary operators operate on a single operand.

> **Operator overloading:** When the operand is of a user-defined aggregate type (struct, class, or interface), the compiler looks for a matching operator function instead of applying built-in semantics. See [Operator Overloading](../functions/operators.md) for the full specification.

---

## Contents
1. [Arithmetic unary operators](#1-arithmetic-unary-operators)
2. [Logical NOT](#2-logical-not)
3. [Bitwise NOT](#3-bitwise-not)
4. [Address-of operator (`&`)](#4-address-of-operator-)
5. [Dereference operator (`*`)](#5-dereference-operator-)
6. [Drain operator (`#`)](#6-drain-operator-)
7. [Cast expression](#7-cast-expression)
8. [Prefix increment and decrement](#8-prefix-increment-and-decrement)
---
## 1. Arithmetic unary operators
### Unary plus (`+`)
Returns the value of its operand unchanged.  
Operand must be of arithmetic (integer or floating-point) type.
```
UnaryPlusExpr:
    '+' CastExpr
```
```k
result : int = +x;
```
### Unary minus (`-`)
Returns the arithmetic negation of its operand.  
Operand must be of arithmetic type.
```
UnaryMinusExpr:
    '-' CastExpr
```
```k
result : int = -x;
neg : double = -3.14d;
```
---
## 2. Logical NOT
`!` returns `true` if its operand is zero or `false`, and `false` otherwise.  
The result type is `bool`.

The operand must be of a boolean-compatible type: `bool`, any numeric primitive, or
an indirection type (`T*`, `T+`, `T?`, `T!`). For indirection types, `!ptr` is
equivalent to `ptr == null`.
```
LogicalNotExpr:
    '!' CastExpr
```
```k
b : bool = !flag;
if (!done) { ... }
p : int* = get_ptr();
if (!p) { /* p is null */ }
```
---
## 3. Bitwise NOT
`+` (tilde) inverts all bits of its operand.  
Operand must be of integer type.
```
BitwiseNotExpr:
    '+' CastExpr
```
```k
mask : int = ~0xFF;    // all bits set except the low 8
```
---
## 4. Address-of operator (`&`)

`&expr` yields the address of the object denoted by `expr`.
`expr` must be an lvalue (a variable, parameter, array element, or struct member).

```
AddressOfExpr:
    '&' CastExpr
```

The result type is `T+` (a **link** — non-null, mutable address) when `expr` has type `T`.
When `expr` denotes a **const** object (type `const T`), the result is `const T+` — a link to const.

```k
a      : int      = 5;
lnk    : int+     = &a;     // link to a (non-null)
p      : int*     = &a;     // implicitly widened to pointer

const b : int     = 7;
clnk   : const int+ = &b;  // link to const — OK
bad    : int+       = &b;  // Error: '&b' has type 'const int+'; cannot bind to mutable link
```

> **Note:** `&expr` always produces a non-null address. It can be implicitly widened to a nullable type (`T*`, `T?`) but not the other way around.

This makes `&` the natural way to populate arrays of indirections (see [Types — §9.7](../basic/types.md#97-arrays-of-indirection-types)):

```k
a : int = 1;
b : int = 2;
arr : int+[] {&a, &b};   // array of links
ptrs : int*[] {&a, &b};  // array of pointers (link widened to pointer)
views : int?[] {&a, &b};  // array of view (link widened to view)
```

Applied to a reference variable `r : T&`, `&r` returns a `T+` pointing to the same object as `r`.
Applied to a reference variable `r : const T&`, `&r` returns a `const T+`.

---
## 5. Dereference operator (`*`)

`*expr` dereferences an indirection, yielding a **reference** (`T&`) to the pointed-to object.
`expr` must be of type `T+`, `T?`, or `T*`.

```
DereferenceExpr:
    '*' CastExpr
```

| Operand type | Null-check inserted |
|---|---|
| `T+` (link)    | No — link is non-null |
| `T?` (view)  | Yes — calls `__k_fatal_null_dereference()` if null |
| `T*` (pointer) | Yes — calls `__k_fatal_null_dereference()` if null |

```k
x   : int  = 42;
lnk : int+ = &x;
p   : int* = &x;

*lnk = 10;       // no null-check; assigns 10 to x
*p   = 20;       // null-check, then assigns 20 to x
y : int = *p;    // null-check, then reads
```

The dereference of a reference (`T&`) is not supported directly via `*` — a reference already acts as the object itself.

---
## 6. Drain operator (`#`)

`#expr` produces a **drain** (`T#`) from an lvalue, granting the consumer permission to
steal the internal resources of the referenced object.

```
DrainExpr:
    '#' CastExpr
```

`expr` must be a mutable lvalue (variable, parameter, or struct member). The operand
must **not** be `const` — draining a const object is a compile-time error.

The result type is `T#` (a drain — non-null, immutable binding) when `expr` has type `T`.

```k
x : int = 42;
consume(#x);          // passes x by drain to consume()

struct Resource {
    value : int = 0;
    Resource(other : Resource#) {
        value = other.value;
        other.value = 0;  // reset source (drain semantics)
    }
}

a : Resource(42);
b : Resource(#a);     // drain constructor — steals a's resources
// a.value is now 0, b.value is 42
```

**Key rules:**
- A reference (`T&`) is **not** implicitly convertible to a drain (`T#`). The `#` operator
  must be used explicitly.
- A drain (`T#`) **is** implicitly convertible to a reference (`T&`), link (`T+`),
  view (`T?`), or pointer (`T*`).
- After draining, the source object must remain in a **valid, reusable state** (typically
  equivalent to default construction).
- When both reference and drain overloads exist for a function, the drain overload is
  selected when the argument is a drain expression (`#x`), and the reference overload
  is selected for plain references.

> **Note:** Draining primitives (e.g. `int#`) is semantically a no-op — a copy is performed.
> This is permitted but provides no performance benefit.

---
## 7. Cast expression
Explicit type conversion. Converts `expr` to the specified type.
```
CastExpr:
    '(' TypeSpec ')' CastExpr
    | UnaryExpr
```

### Primitive casts
```k
d : double = 3.99d;
i : int = (int) d;        // truncates to 3
b : byte = (byte) largeInt;
```

### Indirection casts (ref / lnk / view / ptr)

#### Static upcast (Derived→Base) — compile-time GEP

```k
struct Base { val : int; Base(v : int) : val(v) {} }
struct Derived : public Base { extra : int; Derived(v : int) : Base(v), extra(0) {} }

d  : Derived(55);
pd : Derived* = &d;
ld : Derived+ = &d;

pb : Base*  = (Base*) pd;    // ptr<Derived> → ptr<Base>
lb : Base+  = (Base+) ld;    // lnk<Derived> → lnk<Base>
pb2 : Base* = (Base*) ld;   // lnk<Derived> → ptr<Base> (cross-kind)
```

#### Dynamic downcast (Base→Derived) — runtime RTTI check

Only for `class` and `interface` types (not `struct`).

```k
class Base {
    public dummy() : int { return 0; }
}
class Derived : public Base {
    public extra : int;
    public Derived(v : int) : extra(v) {}
    public get_extra() : int { return extra; }
}

bp : Base* = &some_derived_obj;

// ptr/view target — nullable: null if RTTI mismatch
dp  : Derived* = (Derived*) bp;
dp2 : Derived? = (Derived?) bp;

// lnk/ref target — non-null: fatal trap if RTTI mismatch
dl : Derived+ = (Derived+) bp;   // __k_fatal_null_dyncast() if bp ≠ Derived*
```

See [Types — §11.5](../basic/types.md#115-explicit-cast) for full rules.

---
## 8. Prefix increment and decrement
`++expr` increments `expr` by 1 and returns the new value.  
`--expr` decrements `expr` by 1 and returns the new value.
```
PrefixIncrExpr:
    '++' CastExpr
PrefixDecrExpr:
    '--' CastExpr
```
```k
++i;         // increments i; result is the new value
x = ++n;     // increments n, then assigns the new value to x
```
**Postfix variants** (evaluated after the current expression):
```k
i++;         // returns old value of i, then increments
i--;         // returns old value of i, then decrements
```
---
*See also:* [Binary Operators](binary.md) · [Expressions](expressions.md) · [Types](../basic/types.md) · [Operator Overloading](../functions/operators.md)
