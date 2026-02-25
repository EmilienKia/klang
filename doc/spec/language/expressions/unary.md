# Unary Operators

[← Index](../index.md) · [Expressions](expressions.md)

Unary operators operate on a single operand.

---

## Contents
1. [Arithmetic unary operators](#1-arithmetic-unary-operators)
2. [Logical NOT](#2-logical-not)
3. [Bitwise NOT](#3-bitwise-not)
4. [Address-of operator](#4-address-of-operator)
5. [Dereference operator](#5-dereference-operator)
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
## 4. Address-of operator
`&expr` yields a pointer to the object denoted by `expr`.  
`expr` must be an lvalue (a variable or a dereferenced pointer).
```
AddressOfExpr:
    '&' CastExpr
```
The result type is `T*` when `expr` has type `T`.
```k
a : int = 5;
p : int* = &a;   // p points to a
```
---
## 5. Dereference operator
`*expr` dereferences a pointer, yielding a reference to the pointed-to object.  
`expr` must be of pointer type.
```
DereferenceExpr:
    '*' CastExpr
```
```k
p : int*;
p = &a;
*p = 42;         // assigns 42 to the object pointed to by p
x : int = *p;    // reads the value
```
Combined with assignment:
```k
*p += i + j;
```
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
