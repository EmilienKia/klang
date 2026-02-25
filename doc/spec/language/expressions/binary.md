# Binary Operators

[← Index](../index.md) · [Expressions](expressions.md)

Binary operators combine two operands to produce a value.

---

## Contents
1. [Arithmetic operators](#1-arithmetic-operators)
2. [Bitwise operators](#2-bitwise-operators)
3. [Shift operators](#3-shift-operators)
4. [Comparison operators](#4-comparison-operators)
5. [Logical operators](#5-logical-operators)
6. [Conditional (ternary) operator](#6-conditional-ternary-operator)
---
## 1. Arithmetic operators
All arithmetic operators require operands of numeric (integer or floating-point) type.  
The result type follows the usual promotion rules (wider type wins).
| Operator | Operation | Grammar rule |
|----------|-----------|--------------|
| `+`      | Addition  | `AdditiveExpr '+' MultiplicativeExpr` |
| `-`      | Subtraction | `AdditiveExpr '-' MultiplicativeExpr` |
| `*`      | Multiplication | `MultiplicativeExpr '*' PmExpr` |
| `/`      | Division | `MultiplicativeExpr '/' PmExpr` |
| `%`      | Modulo | `MultiplicativeExpr '%' PmExpr` |
```k
a + b        // addition
a - b        // subtraction
a * b        // multiplication
a / b        // division (integer truncation for integer operands)
a % b        // modulo (remainder)
```
**Notes:**
- Integer division truncates toward zero.
- `%` is only meaningful for integer operands.
- Division by zero is undefined behaviour.
---
## 2. Bitwise operators
Bitwise operators require integer operands. They operate on the binary representation.
| Operator | Operation | Grammar rule |
|----------|-----------|--------------|
| `&`      | Bitwise AND | `BinAndExpr '&' EqualityExpr` |
| `\|`     | Bitwise OR  | `InclusiveBinOrExpr '\|' ExclusiveBinOrExpr` |
| `^`      | Bitwise XOR | `ExclusiveBinOrExpr '^' BinAndExpr` |
```k
a & b        // bitwise AND
a | b        // bitwise OR
a ^ b        // bitwise XOR
```
**Examples:**
```k
mask : int = flags & 0xFF;   // keep low 8 bits
combined : int = a | b;      // set union
toggled : int = state ^ bit; // toggle bit
```
---
## 3. Shift operators
Shift operators require integer operands.
| Operator | Operation | Grammar rule |
|----------|-----------|--------------|
| `<<`     | Left shift  | `ShiftingExpr '<<' AdditiveExpr` |
| `>>`     | Right shift | `ShiftingExpr '>>' AdditiveExpr` |
```k
a << b       // shift a left by b bits
a >> b       // shift a right by b bits
```
Left shift fills the vacated bits with zeros.  
Right shift is arithmetic (sign-extending) for signed types and logical (zero-filling) for unsigned types.
---
## 4. Comparison operators
Comparison operators compare two values and return a `bool`.
| Operator | Meaning | Grammar rule |
|----------|---------|--------------|
| `==`     | Equal | `EqualityExpr '==' RelationalExpr` |
| `!=`     | Not equal | `EqualityExpr '!=' RelationalExpr` |
| `<`      | Less than | `RelationalExpr '<' ShiftingExpr` |
| `>`      | Greater than | `RelationalExpr '>' ShiftingExpr` |
| `<=`     | Less than or equal | `RelationalExpr '<=' ShiftingExpr` |
| `>=`     | Greater than or equal | `RelationalExpr '>=' ShiftingExpr` |
```k
a == b       // true if a equals b
a != b       // true if a differs from b
a < b        // true if a is strictly less than b
a > b        // true if a is strictly greater than b
a <= b       // true if a is less than or equal to b
a >= b       // true if a is greater than or equal to b
```
**Examples:**
```k
min(a: int, b: int) : int {
    if (a < b) return a;
    else return b;
}
eq(a: char, b: char) : bool { return a == b; }
```
---
## 5. Logical operators
Logical operators work on boolean values (or values implicitly convertible to `bool`).
| Operator | Meaning | Grammar rule |
|----------|---------|--------------|
| `&&`     | Logical AND | `LogicalAndExpr '&&' InclusiveBinOrExpr` |
| `\|\|`   | Logical OR  | `LogicalOrExpr '\|\|' LogicalAndExpr` |
```k
a && b       // true only if both a and b are true
a || b       // true if at least one of a or b is true
```
**Short-circuit evaluation:**  
`&&` does not evaluate the right operand if the left is `false`.  
`||` does not evaluate the right operand if the left is `true`.
```k
if (p != null && *p > 0) { ... }
```
---
## 6. Conditional (ternary) operator
The conditional expression `cond ? then : else` evaluates `cond` and yields `then` if true, `else` otherwise.
### Grammar
```
ConditionalExpr:
    LogicalOrExpr '?' ConditionalExpr ':' ConditionalExpr
```
Both branches must have compatible types.
```k
max : int = (a > b) ? a : b;
label : char* = (x > 0) ? "positive" : "non-positive";
```
---
*See also:* [Unary Operators](unary.md) · [Assignment Operators](assignment.md) · [Expressions](expressions.md)
