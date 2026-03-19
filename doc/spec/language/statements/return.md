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

### Owner return (move semantics)

When a function returns an owner variable (`T!`), ownership is **transferred** to the caller.
The local owner alloca is set to `null` before scope cleanup runs, so the object is not
double-freed:

```k
make() : Foo! {
    f : Foo! = new Foo(42);
    return f;           // f moved to caller; f's alloca ← null
}                       // scope cleanup: f is null → no-op
```

If the caller does not assign the returned owner to a variable, the compiler emits
**Warning 0x5010** and the object is immediately deleted at the call site.
See [Dynamic Allocation — Ownership and lifetime](../memory/new-delete.md#4-ownership-and-lifetime).

### Struct return (by value)

When a function returns a struct by value, a copy of the struct is produced as an
**expression temporary** at the call site.  The local variable inside the callee is destroyed
normally at function exit; the returned copy is a new temporary that lives until the end of
the enclosing full expression statement in the caller.

```k
struct Obj {
    val : int;
    Obj(v: int) : val(v) {}
    ~Obj() { dtor_count = dtor_count + 1; }
}

make(v: int) : Obj {
    o : Obj(v);
    return o;        // copy of 'o' is returned
}                    // local 'o' destroyed here

test() : int {
    x : Obj = make(42);   // temporary from make() copied into 'x';
                           // temporary destroyed at end of statement
    return x.val;
}                          // 'x' destroyed here
```

See [Destructors — Return values and expression temporaries](../structs/destructors.md#4-return-values-and-expression-temporaries)
for the full lifetime rules of struct temporaries.
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
*See also:* [Statements](statements.md) · [Functions](../functions/functions.md) · [Destructors](../structs/destructors.md) · [Dynamic Allocation](../memory/new-delete.md)
