# Binary Operators

[← Index](../index.md) · [Expressions](expressions.md)

Binary operators combine two operands to produce a value.

> **Operator overloading:** When one operand is of a user-defined aggregate type (struct, class, or interface), the compiler looks for a matching operator function instead of applying built-in semantics. See [Operator Overloading](../functions/operators.md) for the full specification.

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
| `?`      | Bitwise XOR | `ExclusiveBinOrExpr '?' BinAndExpr` |
```k
a & b        // bitwise AND
a | b        // bitwise OR
a ? b        // bitwise XOR
```
**Examples:**
```k
mask : int = flags & 0xFF;   // keep low 8 bits
combined : int = a | b;      // set union
toggled : int = state ? bit; // toggle bit
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

### Address comparison for indirection types

The `==` and `!=` operators can be used to compare the **addresses** held by
indirection types: pointer (`T*`), link (`T+`), view (`T?`), and owner (`T!`).
The `null` literal may appear on either side.

These operators compare the raw pointer addresses, **not** the pointed-to objects.
Two indirections are equal if they point to the same memory address.

```k
x : int = 42;
p1 : int* = &x;
p2 : int* = &x;
p1 == p2     // true — same address
p1 == null   // false — p1 is non-null
null == null  // true
```

| Left type | Right type | Result | Semantics |
|---|---|---|---|
| `T*`, `T+`, `T?`, `T!` | `T*`, `T+`, `T?`, `T!` | `bool` | Address comparison |
| `T*`, `T+`, `T?`, `T!` | `null` | `bool` | Null check |
| `null` | `T*`, `T+`, `T?`, `T!` | `bool` | Null check |
| `null` | `null` | `bool` | Always `true` for `==`, `false` for `!=` |

> **Note:** References (`T&`) are **excluded** from address comparison. When `==`
> or `!=` is applied to references, the comparison is on the **pointed-to values**
> (existing primitive/struct comparison semantics).

> **Note:** Only `==` and `!=` are supported for address comparison. Relational
> operators (`<`, `>`, `<=`, `>=`) on indirections are a compile-time error.

### Comparison operators on aggregate types

When either operand is a user-defined aggregate type (struct, class, or interface),
the operator must be provided by an [operator function](../functions/operators.md).
Rather than requiring all six operators to be declared explicitly, the compiler
applies a fallback rule: if the exact operator is not declared, it tries to
**synthesize** it from another declared comparison operator (e.g. synthesizing
`!=` from a declared `==` via negation, or synthesizing all four relational
operators from a single declared `<`). See
[Operators — §9. Comparison operator fallback (synthesis)](../functions/operators.md#9-comparison-operator-fallback-synthesis)
for the exhaustive derivation table and priority rule.

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
**Short-circuit evaluation (and-then / or-else):**

`&&` and `||` use **short-circuit evaluation**: the right operand is only evaluated
if its value could change the result.

- `&&` (**and-then**): if the left operand is `false`, the result is `false` without
  evaluating the right operand.
- `||` (**or-else**): if the left operand is `true`, the result is `true` without
  evaluating the right operand.

This makes null-guard patterns safe — the dereference is only reached when the
pointer is known to be non-null:
```k
if (p != null && *p > 0) { ... }   // *p only evaluated if p is non-null
if (p && *p > 0) { ... }           // same, using implicit bool conversion
if (failed || *fallback == 0) { ... }  // *fallback skipped if failed is true
```

### Implicit boolean conversion for indirection types

Indirection types — pointer (`T*`), link (`T+`), view (`T?`), and owner (`T!`) —
are **implicitly convertible to `bool`**. A non-null indirection converts to `true`;
a null indirection converts to `false`.

This implicit conversion applies wherever a `bool` is expected:
- `if` / `while` / `for` conditions
- logical operators `&&`, `||`, `!`
- any expression context requiring `bool`

| Source type | Conversion | Result |
|---|---|---|
| `T*`, `T+`, `T?`, `T!` (non-null) | → `bool` | `true` |
| `T*`, `T+`, `T?`, `T!` (null) | → `bool` | `false` |
| `null` literal | → `bool` | `false` |

> **Note:** References (`T&`) are **not** implicitly convertible to `bool`.
> A reference can never be null, so such a conversion would be meaningless.

**Examples:**
```k
p : int* = get_ptr();
if (p) {
    // p is non-null — safe to dereference
    val : int = *p;
}
if (!p) {
    // p is null
}
// Combine with logical operators:
if (p && q) { ... }   // both non-null
if (p || q) { ... }   // at least one non-null
```
---
## 6. Conditional (ternary) operator
The conditional expression `cond ? then : else` evaluates `cond` and yields
`then` if true, `else` otherwise.

### Grammar
```
ConditionalExpr:
    LogicalOrExpr '?' ConditionalExpr ':' ConditionalExpr
```

### Semantics

1. `cond` is implicitly converted to `bool` (same conversion rules as `if`/`while` conditions).
2. Exactly one branch is evaluated:
   - if `cond` is `true`, only `then` is evaluated;
   - if `cond` is `false`, only `else` is evaluated.
3. The two branches are type-unified:
   - if both branches already have the same type, that type is used;
   - otherwise, implicit conversions are attempted and the best common target is selected;
   - if no common type can be formed, compilation fails.
4. The resulting type can be primitive, enum, union, aggregate, reference, pointer/link/view, or owner, as long as the branch conversions are valid.

For aggregate-by-value results, normal temporary materialization and full-expression
cleanup rules apply (same lifetime rules as other expression temporaries).

```k
max : int = (a > b) ? a : b;
label : char* = (x > 0) ? "positive" : "non-positive";
selected : int& = choose_left ? left : right;
```
---
*See also:* [Unary Operators](unary.md) · [Assignment Operators](assignment.md) · [Expressions](expressions.md) · [Operator Overloading](../functions/operators.md)
