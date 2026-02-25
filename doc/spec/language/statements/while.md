# While Statement

[← Index](../index.md) · [Statements](statements.md)

The `while` statement repeatedly executes its body as long as a condition is true.

---

## Contents
1. [Syntax](#1-syntax)
2. [Semantics](#2-semantics)
3. [Examples](#3-examples)
---
## 1. Syntax
### Grammar
```
WhileStatement:
    'while' '(' Expression ')' Statement
```
The condition expression is enclosed in parentheses.  
The body may be a single statement or a block.
---
## 2. Semantics
1. The condition expression is evaluated.
2. If true (non-zero), the body is executed.
3. After the body executes, control returns to step 1.
4. If the condition is false, execution continues after the `while` statement.
If the condition is false on the first evaluation, the body is never executed.
---
## 3. Examples
### Basic while loop
```k
cumul(i : int) : int {
    r : int;
    r = 0;
    while (i > 0) {
        r += i;
        i = i - 1;
    }
    return r;
}
// cumul(5) = 5 + 4 + 3 + 2 + 1 = 15
```
### While with a pointer
```k
process(p : int*) {
    while (*p != 0) {
        *p = *p + 1;
        p = p + 1;     // advance pointer (requires pointer arithmetic support)
    }
}
```
---
*See also:* [Statements](statements.md) · [If Statement](if.md) · [For Statement](for.md)
