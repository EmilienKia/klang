# Break Statement

[← Index](../index.md) · [Statements](statements.md)

The `break` statement terminates execution of the innermost enclosing loop (`while` or `for`).

---

## Contents
1. [Syntax](#1-syntax)
2. [Semantics](#2-semantics)
3. [Break and local variable destruction](#3-break-and-local-variable-destruction)
4. [Restrictions](#4-restrictions)
5. [Examples](#5-examples)
---
## 1. Syntax
### Grammar
```
BreakStatement:
    'break' ';'
```
---
## 2. Semantics
- `break` immediately exits the **innermost** enclosing `while` or `for` loop.
- Execution continues with the first statement after the loop.
- In nested loops, only the innermost loop is exited; the outer loop continues normally.
- Code after a `break` in the same block is unreachable.
---
## 3. Break and local variable destruction
When a `break` is executed inside a block that has live local variables of struct type with destructors (or owner-typed variables), the destructors are invoked in reverse declaration order **before** control is transferred to the statement after the loop.

Only variables scoped **inside** the loop are destroyed. Variables declared before the loop are not affected.

```k
dtor_count : int;

struct counter {
    ~counter() {
        dtor_count = dtor_count + 1;
    }
}

test_break_dtor() : int {
    i : int = 0;
    while(i < 10) {
        c : counter;        // constructed each iteration
        if(i >= 3) {
            break;          // ~counter() called for 'c' before exiting the loop
        }
        i = i + 1;
    }                       // ~counter() also called on normal iteration exit
    return dtor_count;
}
```

For owner-typed local variables (`T!`), if the owner is still non-null when the `break` is reached, the object is automatically deleted (destructor called + memory freed) before the loop exits.
---
## 4. Restrictions
- `break` can only appear inside the body of a `while` or `for` loop.
- Using `break` outside a loop is a **compile-time error** (diagnostic `0x017C`).

```k
bad() : int {
    break;       // ERROR: 'break' can only appear inside a loop body
    return 0;
}

bad2(x : int) : int {
    if(x > 0) {
        break;   // ERROR: 'break' can only appear inside a loop body
    }
    return 0;
}
```
---
## 5. Examples
### Break in a while loop
```k
find_first_ge(limit : int) : int {
    i : int = 0;
    while(i < 100) {
        if(i >= limit) {
            break;
        }
        i = i + 1;
    }
    return i;
}
// find_first_ge(5) = 5
// find_first_ge(200) = 100  (loop condition exits normally)
```
### Break in a for loop
```k
sum_until(limit : int) : int {
    r : int = 0;
    for(i : int = 0; i < 100; i += 1) {
        if(i >= limit) {
            break;
        }
        r = r + i;
    }
    return r;
}
// sum_until(5) = 0 + 1 + 2 + 3 + 4 = 10
```
### Break in nested loops (innermost only)
```k
nested_break(n : int) : int {
    total : int = 0;
    i : int = 0;
    while(i < n) {
        j : int = 0;
        while(j < n) {
            if(j >= 3) {
                break;       // exits inner loop only
            }
            total = total + 1;
            j = j + 1;
        }
        // continues here after inner break
        i = i + 1;
    }
    return total;
}
// nested_break(5) = 5 * 3 = 15  (inner loop runs at most 3 iterations)
```
---
*See also:* [Statements](statements.md) · [While Statement](while.md) · [For Statement](for.md) · [Continue Statement](continue.md) · [Return Statement](return.md)

