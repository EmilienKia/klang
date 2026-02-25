# If Statement

[← Index](../index.md) · [Statements](statements.md)

The `if` statement conditionally executes a branch based on a boolean test expression.

---

## Contents
1. [Syntax](#1-syntax)
2. [Semantics](#2-semantics)
3. [Else-if chains](#3-else-if-chains)
4. [Examples](#4-examples)
---
## 1. Syntax
### Grammar
```
IfElseStatement:
    'if' '(' Expression ')' Statement
    | 'if' '(' Expression ')' Statement 'else' Statement
```
The test expression is enclosed in parentheses.  
Both the `then` branch and the `else` branch may be a single statement or a block statement.
---
## 2. Semantics
1. The test expression is evaluated.
2. If the result is true (non-zero), the `then` branch is executed.
3. If the result is false (zero) and an `else` clause is present, the `else` branch is executed.
4. If the result is false and there is no `else`, execution continues after the `if` statement.
The test expression may be of any type convertible to `bool` (integers, pointers, etc.).
---
## 3. Else-if chains
An `else` clause may immediately contain another `if`, forming an else-if chain.  
There is no explicit `else if` keyword; this is simply nesting.
```k
if (cond1) {
    // branch 1
} else if (cond2) {
    // branch 2
} else {
    // default branch
}
```
The `else` always binds to the nearest preceding unmatched `if` (dangling-else rule).
---
## 4. Examples
### Simple if-then-else
```k
min(a: int, b: int) : int {
    if (a < b)
        return a;
    else
        return b;
}
```
### If-then-else with blocks
```k
max(a: int, b: int) : int {
    if (a > b) {
        return a;
    } else {
        return b;
    }
}
```
### Else-if chain (fibonacci)
```k
fibo(i: unsigned short) : unsigned int {
    if (i == 0) return 1;
    else if (i == 1) return 1;
    return fibo(i - 1) + fibo(i - 2);
}
```
### Pointer guard
```k
assign(i: int, j: int) : int {
    p : int*;
    if (i < j) {
        p = &a;
    } else {
        p = &b;
    }
    *p += i + j;
    return *p;
}
```
---
*See also:* [Statements](statements.md) · [While Statement](while.md) · [For Statement](for.md) · [Expressions](../expressions/expressions.md)
