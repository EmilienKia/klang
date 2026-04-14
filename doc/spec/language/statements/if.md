# If Statement

[← Index](../index.md) · [Statements](statements.md)

The `if` statement conditionally executes a branch based on a boolean test expression.

---

## Contents
1. [Syntax](#1-syntax)
2. [Semantics](#2-semantics)
3. [Else-if chains](#3-else-if-chains)
4. [Examples](#4-examples)
5. [Link guard (soft-fail)](#5-link-guard-soft-fail)
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
## 5. Link guard (soft-fail)

When the condition expression of an `if` contains a **link assignment** (rebind)
from a nullable source (pointer `*`, view `?`, or owner `!`), or a **dynamic
downcast** to a link type (`+`), the usual fatal-trap-on-null behaviour is
replaced by a **soft-fail**:

1. If the source is null or the dynamic cast fails (RTTI mismatch), the
   assignment is skipped and the condition evaluates to `false`.
2. If an `else` clause is present, the `else` branch is executed.
3. If there is no `else`, execution continues after the `if` statement.

The link variable retains its previous value on the soft-fail path (the store
is skipped).

> **Note:** Outside of an `if` condition, assigning a null value to a link
> remains a fatal error (calls `__k_fatal_null_assignation` or
> `__k_fatal_null_dyncast`).

### Link rebind guard
```k
test() : int {
    x : int = 42;
    lnk : int+ = &x;
    p : int* = null;
    if (lnk = p) {
        // entered only if p is non-null
        return *lnk;
    } else {
        // entered because p is null — lnk still points to x
        return 0;
    }
}
```

### Dynamic downcast guard
```k
class Base {
    public val : int;
    public Base(v : int) : val(v) {}
    public dummy() : int { return 0; }
}
class Derived : public Base {
    public extra : int;
    public Derived(v : int) : Base(v), extra(99) {}
    public get_extra() : int { return extra; }
}
class Other : public Base {
    public data : int;
    public Other(v : int) : Base(v), data(0) {}
}

get_extra_fn(d : Derived&) : int { return d.get_extra(); }

test() : int {
    o : Other(5);
    bp : Base* = &o;
    d : Derived(1);
    dlnk : Derived+ = &d;
    if (dlnk = (Derived+) bp) {
        // entered only if bp points to a Derived (RTTI match)
        return get_extra_fn(*dlnk);
    } else {
        // entered because bp points to Other, not Derived
        return -1;
    }
}
```

### Nesting
Nested `if` statements each have their own soft-fail destination.  An inner
`if` with a link guard soft-fails to its own `else` (or continuation) without
affecting the outer `if`.

---
*See also:* [Statements](statements.md) · [While Statement](while.md) · [For Statement](for.md) · [Expressions](../expressions/expressions.md)
