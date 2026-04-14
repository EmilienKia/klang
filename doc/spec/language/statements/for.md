# For Statement

[← Index](../index.md) · [Statements](statements.md)

The `for` statement provides a counter-controlled loop with an optional initialiser, condition, and step expression.

---

## Contents
1. [Syntax](#1-syntax)
2. [Semantics](#2-semantics)
3. [Scope of the loop variable](#3-scope-of-the-loop-variable)
4. [Examples](#4-examples)
---
## 1. Syntax
### Grammar
```
ForStatement:
    'for' '(' ForInit ';' [ Expression ] ';' [ Expression ] ')' Statement
ForInit:
    VariableDeclNoSemicolon
    | (empty)
VariableDeclNoSemicolon:
    { Specifier } Identifier ':' TypeSpec [ Initialiser ]
```
The parenthesised header has three parts separated by semicolons:
1. **Init:** optional variable declaration (no trailing `;` needed here).
2. **Condition:** optional boolean expression.
3. **Step:** optional expression evaluated at the end of each iteration.
---
## 2. Semantics
1. The init declaration is evaluated (if present), introducing a loop variable.
2. The condition is evaluated (if present). If false (or absent and treated as true), the loop exits.
3. The body statement is executed.
4. The step expression is evaluated (if present).
5. Return to step 2.
If the condition is omitted, the loop runs indefinitely (break or return must exit it).

A [`break`](break.md) statement inside the body immediately exits the loop.
---
## 3. Scope of the loop variable
A variable declared in the `for` init is scoped to the `for` statement itself (including its body).  
It is destroyed when the `for` statement completes.
---
## 4. Examples
### Basic counted loop
```k
sum(i : short) : int {
    r : int;
    r = 0;
    for (n: short = 0; n < i; n += 1) {
        r += n;
    }
    return r;
}
// sum(5) = 0 + 1 + 2 + 3 + 4 = 10
```
### For loop with decrement step
```k
countdown(n : int) {
    for (i: int = n; i > 0; i -= 1) {
        // process i
    }
}
```
### For loop with no init
```k
processAll(arr: int[8]&, len: int) {
    i : int = 0;
    for (; i < len; i += 1) {
        arr[i] = arr[i] * 2;
    }
}
```
---
*See also:* [Statements](statements.md) · [While Statement](while.md) · [If Statement](if.md) · [Break Statement](break.md)
