# Unary Operators

[← Index](../index.md) · [Expressions](expressions.md)

Unary operators operate on a single operand.

---

## Contents
1. [Arithmetic unary operators](#1-arithmetic-unary-operators)
2. [Logical NOT](#2-logical-not)
3. [Bitwise NOT](#3-bitwise-not)
4. [Address-of operator (`&`)](#4-address-of-operator-)
5. [Dereference operator (`*`)](#5-dereference-operator-)
6. [Cast expression](#6-cast-expression)
7. [Prefix increment and decrement](#7-prefix-increment-and-decrement)
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
```
LogicalNotExpr:
    '!' CastExpr
```
```k
b : bool = !flag;
if (!done) { ... }
```
---
## 3. Bitwise NOT
`~` (tilde) inverts all bits of its operand.  
Operand must be of integer type.
```
BitwiseNotExpr:
    '~' CastExpr
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

The result type is `T~` (a **link** — non-null, mutable address) when `expr` has type `T`.
When `expr` denotes a **const** object (type `const T`), the result is `const T~` — a link to const.

```k
a      : int      = 5;
lnk    : int~     = &a;     // link to a (non-null)
p      : int*     = &a;     // implicitly widened to pointer

const b : int     = 7;
clnk   : const int~ = &b;  // link to const — OK
bad    : int~       = &b;  // Error: '&b' has type 'const int~'; cannot bind to mutable link
```

> **Note:** `&expr` always produces a non-null address. It can be implicitly widened to a nullable type (`T*`, `T^`) but not the other way around.

Applied to a reference variable `r : T&`, `&r` returns a `T~` pointing to the same object as `r`.
Applied to a reference variable `r : const T&`, `&r` returns a `const T~`.

---
## 5. Dereference operator (`*`)

`*expr` dereferences an indirection, yielding a **reference** (`T&`) to the pointed-to object.
`expr` must be of type `T~`, `T^`, or `T*`.

```
DereferenceExpr:
    '*' CastExpr
```

| Operand type | Null-check inserted |
|---|---|
| `T~` (link)    | No — link is non-null |
| `T^` (pinned)  | Yes — calls `__fatal_null_dereference()` if null |
| `T*` (pointer) | Yes — calls `__fatal_null_dereference()` if null |

```k
x   : int  = 42;
lnk : int~ = &x;
p   : int* = &x;

*lnk = 10;       // no null-check; assigns 10 to x
*p   = 20;       // null-check, then assigns 20 to x
y : int = *p;    // null-check, then reads
```

The dereference of a reference (`T&`) is not supported directly via `*` — a reference already acts as the object itself.

---
## 6. Cast expression
Explicit type conversion. Converts `expr` to the specified type.
```
CastExpr:
    '(' TypeSpec ')' CastExpr
    | UnaryExpr
```
```k
d : double = 3.99d;
i : int = (int) d;        // truncates to 3
b : byte = (byte) largeInt;
```
See [Types — Implicit conversions](../basic/types.md#7-implicit-conversions) for the implicit conversion rules.
---
## 7. Prefix increment and decrement
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
*See also:* [Binary Operators](binary.md) · [Expressions](expressions.md) · [Types](../basic/types.md)
