# Return Statement

[← Index](../index.md) · [Statements](statements.md)

The `return` statement terminates execution of the current function and optionally returns a value to the caller.

---

## Contents
1. [Syntax](#1-syntax)
2. [Semantics](#2-semantics)
3. [Return and local variable destruction](#3-return-and-local-variable-destruction)
4. [Examples](#4-examples)
---
## 1. Syntax
### Grammar
```
ReturnStatement:
    'return' [ Expression ] ';'
```
---
## 2. Semantics
- If the function has a non-void return type, a `return` with an expression is required before the function terminates. The expression is evaluated, converted to the return type, and returned.
- If the function has no return type (void), `return` may appear without an expression, or may be omitted (falling off the end of the function body is valid for void functions).
- Multiple `return` statements may appear in a function. The first one reached terminates the function.
The return value expression is evaluated **before** any local destructors run.
---
## 3. Return and local variable destruction
When a `return` is executed inside a block that has live local variables of struct type with destructors, the destructors are invoked in reverse declaration order **after** the return value expression is evaluated but **before** control is transferred to the caller.
```k
dtor_count : int;
struct counter {
    ~counter() {
        dtor_count = dtor_count + 1;
    }
}
test_local_dtor() : int {
    c : counter;
    return dtor_count;   // evaluates to 0 (dtor not yet called)
}                        // ~counter() called here → dtor_count becomes 1
```
After `test_local_dtor()` returns, `dtor_count` is 1, but the returned value is 0.
---
## 4. Examples
### Return with expression
```k
increment(i: int) : int {
    return i + 1;
}
```
### Void return
```k
init() {
    a = 4;
    b = 5;
    return;   // optional; reaching end of body is equivalent
}
```
### Multiple return paths
```k
fibo(i: unsigned short) : unsigned int {
    if (i == 0) return 1;
    else if (i == 1) return 1;
    return fibo(i - 1) + fibo(i - 2);
}
```
### Return from main
```k
main() : int {
    return 0;    // exit code 0 (success)
}
```
---
*See also:* [Statements](statements.md) · [Functions](../functions/functions.md) · [Destructors](../structs/destructors.md)
