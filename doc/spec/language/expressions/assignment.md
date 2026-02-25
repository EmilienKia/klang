# Assignment Operators

[← Index](../index.md) · [Expressions](expressions.md)

Assignment operators store a value into a variable or memory location.

---

## Contents
1. [Simple assignment](#1-simple-assignment)
2. [Compound assignment operators](#2-compound-assignment-operators)
3. [Operand requirements](#3-operand-requirements)
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
## 2. Compound assignment operators
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
| `^=`     | `lhs = lhs ^ rhs` |
| `<<=`    | `lhs = lhs << rhs` |
| `>>=`    | `lhs = lhs >> rhs` |
**Examples:**
```k
r += i;         // r = r + i
i = i - 1;     // could also be written: i -= 1
*p += i + j;   // dereference and add
p.b += 12;     // member field compound assignment
n += 1;        // increment by 1 (same as ++n)
```
---
## 3. Operand requirements
- The left-hand side (`lhs`) must be an assignable location (lvalue): a variable, a parameter, a dereferenced pointer, an array subscript, or a struct field access.
- The right-hand side (`rhs`) is an expression of a compatible type.
- Implicit widening or narrowing conversions are applied as described in [Types — Implicit conversions](../basic/types.md#7-implicit-conversions).
---
*See also:* [Binary Operators](binary.md) · [Expressions](expressions.md) · [Types](../basic/types.md)
