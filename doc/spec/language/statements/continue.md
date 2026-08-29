# Continue Statement

[← Index](../index.md) · [Statements](statements.md)

The `continue` statement skips the remainder of the current iteration and jumps to the next iteration of the innermost enclosing loop (`while` or `for`).

---

## Contents
1. [Syntax](#1-syntax)
2. [Semantics](#2-semantics)
3. [Continue and local variable destruction](#3-continue-and-local-variable-destruction)
4. [Restrictions](#4-restrictions)
5. [Examples](#5-examples)
---
## 1. Syntax
### Grammar
```
ContinueStatement:
    'continue' ';'
```
---
## 2. Semantics
- `continue` immediately ends the current iteration of the **innermost** enclosing `while` or `for` loop.
- In a `while` loop, control jumps back to the **condition** expression.
- In a `for` loop, control jumps to the **step** expression (if any), then back to the condition.
- In nested loops, only the innermost loop is affected; the outer loop continues normally.
- Code after a `continue` in the same block is unreachable.
---
## 3. Continue and local variable destruction
When a `continue` is executed inside a block that has live local variables of struct type with destructors (or owner-typed variables), the destructors are invoked in reverse declaration order **before** control is transferred to the next iteration.

Only variables scoped **inside** the loop are destroyed. Variables declared before the loop are not affected.

```k
dtor_count : int;

struct counter {
    ~counter() {
        dtor_count = dtor_count + 1;
    }
}

test_continue_dtor(n : int) : int {
    i : int = 0;
    while(i < n) {
        c : counter;        // constructed each iteration
        i = i + 1;
        if(i % 2 == 0) {
            continue;       // ~counter() called for 'c' before jumping to condition
        }
        // ... more code ...
    }
    return dtor_count;
}
```

For owner-typed local variables (`T!`), if the owner is still non-null when the `continue` is reached, the object is automatically deleted (destructor called + memory freed) before the next iteration begins.
---
## 4. Restrictions
- `continue` can only appear inside the body of a `while` or `for` loop.
- Using `continue` outside a loop is a **compile-time error** (diagnostic `0x042F`).

```k
bad() : int {
    continue;    // ERROR: 'continue' can only appear inside a loop body
    return 0;
}

bad2(x : int) : int {
    if(x > 0) {
        continue;   // ERROR: 'continue' can only appear inside a loop body
    }
    return 0;
}
```
---
## 5. Examples
### Continue in a while loop — skip even numbers
```k
sum_odd(n : int) : int {
    r : int = 0;
    i : int = 0;
    while(i < n) {
        i = i + 1;
        if(i % 2 == 0) {
            continue;    // skip to next iteration
        }
        r = r + i;
    }
    return r;
}
// sum_odd(5) = 1 + 3 + 5 = 9
// sum_odd(10) = 1 + 3 + 5 + 7 + 9 = 25
```
### Continue in a for loop — skip multiples
```k
sum_skip_multiples(n : int, skip : int) : int {
    r : int = 0;
    for(i : int = 0; i < n; i += 1) {
        if(i % skip == 0) {
            continue;    // step expression (i += 1) is still executed
        }
        r = r + i;
    }
    return r;
}
// sum_skip_multiples(6, 2) = 1 + 3 + 5 = 9  (skipped 0, 2, 4)
```
### Continue in nested loops (innermost only)
```k
nested_continue(n : int) : int {
    total : int = 0;
    i : int = 0;
    while(i < n) {
        j : int = 0;
        while(j < n) {
            j = j + 1;
            if(j % 2 == 0) {
                continue;   // affects inner loop only
            }
            total = total + 1;
        }
        i = i + 1;
    }
    return total;
}
// nested_continue(4) = 4 * 2 = 8  (inner loop counts only odd j: 1, 3)
```
---
*See also:* [Statements](statements.md) · [While Statement](while.md) · [For Statement](for.md) · [Break Statement](break.md) · [Return Statement](return.md)


