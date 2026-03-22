# Destructors

[← Index](../index.md) · [Structures](structs.md)

A *destructor* is a special member function called automatically when a struct instance is destroyed.

---

## Contents
1. [Instance destructors](#1-instance-destructors)
2. [When destructors are called](#2-when-destructors-are-called)
3. [By-value parameters](#3-by-value-parameters)
4. [Return values and expression temporaries](#4-return-values-and-expression-temporaries)
5. [Static destructors (class finalizers)](#5-static-destructors-class-finalizers)
6. [Destructor and return statement interaction](#6-destructor-and-return-statement-interaction)
7. [Examples](#7-examples)
---
## 1. Instance destructors
An instance destructor is a member function whose name is `+` followed by the struct name.  
It takes no parameters and has no return type.
### Grammar
```
DestructorDecl:
    '+' Identifier '(' ')' BlockStatement
```
**Example:**
```k
struct counter {
    ~counter() {
        dtor_count = dtor_count + 1;
    }
}
```
A struct may have at most one instance destructor.
If no destructor is defined, no destructor code runs when the struct is destroyed (fields of struct type will have their own destructors called, but no user code executes for the outer struct).
---
## 2. When destructors are called
### Local variables
The destructor of a local struct variable is called when the enclosing block exits.  
Variables are destroyed in **reverse declaration order**.
```k
test() {
    a : myStruct;   // constructed first
    b : myStruct;   // constructed second
    // ...
}
// b destroyed first, then a
```
A `return` statement:
1. Evaluates the return expression.
2. Calls destructors of all in-scope local variables in reverse declaration order.
3. Returns control to the caller.
### Global variables
The destructor of a global struct variable is called by the global destructor function, in reverse initialisation order, **after** `main` returns.

### Owner variables (`T!`)

When an owner variable goes out of scope (or is explicitly deleted with `delete`), the
destructor of the owned object is called before the memory is freed.  The same scope-exit
rules apply: if multiple owners exist in the same scope, they are destroyed in reverse
declaration order.

```k
{
    p : Foo! = new Foo(1);
    q : Foo! = new Foo(2);
}   // ~Foo() called on q first, then on p
```

An explicit `delete` calls the destructor immediately and sets the owner to `null`:

```k
p : Foo! = new Foo(42);
delete p;                // ~Foo() called; memory freed; p ← null
delete p;                // no-op: p is already null
```

See [Dynamic Allocation — `new` and `delete`](../memory/new-delete.md) for the full specification.

### Dynamically allocated arrays (`T[N]!`)

When a dynamically allocated array of structs is deleted (explicitly or at scope exit),
destructors are called on each element in **reverse order** (last element first):

```k
items : Item[3]! = new Item[3]{Item(1), Item(2), Item(3)};
delete items;    // ~Item() called on items[2], then items[1], then items[0]
```
---

## 3. By-value parameters

When a function receives a struct parameter **by value** (not by reference, pointer, or other
indirection), the argument is copied into the callee's parameter storage.  If the struct type
has a destructor, the destructor is called on the parameter copy when the function exits —
just like a local variable, in reverse declaration order together with other locals.

```k
dtor_count : int;

struct Obj {
    val : int;
    Obj(v: int) : val(v) {}
    ~Obj() { dtor_count = dtor_count + 1; }
}

consume(o: Obj) : int {
    return o.val;
}

test() : int {
    a : Obj(42);
    result : int = consume(a);   // copy of 'a' is made for the parameter
    return result;
}   // ~Obj() on 'a'; ~Obj() on the copy already ran at the end of consume()
```

After `consume` returns, the parameter copy has already been destroyed inside `consume`.

---

## 4. Return values and expression temporaries

### Struct-by-value return

A function may return a struct by value.  The result is a **temporary** — an unnamed object
with automatic storage duration that lives until the end of the enclosing **full expression
statement**.

```k
make(v: int) : Obj {
    o : Obj(v);
    return o;   // returns a copy of 'o'
}               // local 'o' destroyed here (inside make)

test() : int {
    x : Obj = make(42);   // temporary from make() is copied into 'x'
    return x.val;
}                          // 'x' destroyed here
```

### Expression temporaries and lifetime

When a function call produces a struct-typed rvalue (a temporary), that temporary is
**materialised** into compiler-managed storage.  The temporary is not destroyed immediately
after the call returns — it survives until the **end of the full expression statement** that
contains the call.

This rule is critical for chained member accesses and method calls on temporaries:

```k
struct Builder {
    n : int;
    Builder(v: int) : n(v) {}
    ~Builder() { log = log + 1; }
    add(x: int) : Builder {
        r : Builder(n + x);
        return r;
    }
    get() : int { return n; }
}

log : int;

test() : int {
    return make(1).add(10).add(100).get();
    //     ^temp1   ^temp2  ^temp3
    // All three temporaries are alive during the whole expression.
    // At the semicolon (end of statement), they are destroyed in
    // reverse creation order: temp3, temp2, temp1.
}
```

### Destruction order

Multiple temporaries created within a single full expression are destroyed in **reverse
creation order** at the end of the statement.  This guarantees that no temporary is accessed
after its destruction, even in complex chained expressions.

### Temporaries in control-flow conditions

The same rule applies to expressions used as conditions in `if`, `while`, and `for`
statements: temporaries created during condition evaluation are destroyed after the condition
is evaluated, before the controlled body executes.

```k
if (make(1).get() > 0) {
    // the temporary from make(1) is already destroyed here
}
```

### Member access on temporaries

The `.` operator may be used on a struct-typed rvalue (a temporary returned from a function
call) to access fields or call member functions.  The temporary remains alive for the full
statement:

```k
val : int = make(42).val;           // field access on temporary
res : int = make(42).get();         // member function call on temporary
```

---

## 5. Static destructors (class finalizers)
A *static destructor* is a static no-argument void function whose name is `+` followed by the struct name.  
It acts as a class-level finaliser, called once at program shutdown (after `main` returns), via the global destructor function.
### Grammar
```
StaticDestructorDecl:
    'static' '+' Identifier '(' ')' BlockStatement
```
**Example:**
```k
dtor_called : int;
struct Cleaner {
    static ~Cleaner() {
        dtor_called = 99;
    }
}
// Cleaner's static destructor is called at program finalisation.
// After finalization, dtor_called == 99.
```
**Constraints:**
- A static destructor cannot be called explicitly.
- It must be no-argument and have no return type.
- There may be at most one static destructor per struct.
- A static destructor may exist independently of a static constructor.
### Ordering: static destructors vs. instance destructors
Static destructors are registered in the global destructor function.  
The ordering between static destructors and instance destructor calls for global variables follows the reverse of the registration order in the global constructor (which respects the dependency graph).
---
## 6. Destructor and return statement interaction
When a `return` statement is reached inside a function that has live local struct variables:
1. The return value expression is evaluated first (no destructors called yet).
2. Destructors of in-scope local variables are called in reverse declaration order.
3. The function returns the previously captured return value.
```k
dtor_count : int;
struct counter {
    ~counter() {
        dtor_count = dtor_count + 1;
    }
}
test_local_dtor() : int {
    c : counter;
    return dtor_count;   // return value = 0 (before destructor)
}                        // ~counter() runs here → dtor_count = 1
```
After the call, the returned value is 0, but `dtor_count` is 1.
---
## 7. Examples
### Local destructor
```k
module demo;
dtor_count : int;
struct counter {
    ~counter() {
        dtor_count = dtor_count + 1;
    }
}
test_local_dtor() : int {
    c : counter;
    return dtor_count;  // returns 0 (before destruction)
}
get_dtor_count() : int {
    return dtor_count;   // returns 1 after test_local_dtor()
}
```
### Global destructor
```k
module demo;
dtor_called : int;
struct tracked {
    ~tracked() {
        dtor_called = 77;
    }
}
g : tracked;   // global: ~tracked() called at program finalisation
get_dtor_called() : int {
    return dtor_called;
}
```
### Static destructor
```k
module demo;
dtor_called : int;
struct Cleaner {
    static ~Cleaner() {
        dtor_called = 99;
    }
}
get_val() : int { return dtor_called; }
// Before finalization: get_val() == 0
// After finalization: dtor_called == 99
```
---
*See also:* [Structures](structs.md) · [Constructors](constructors.md) · [Dynamic Allocation](../memory/new-delete.md) · [Return Statement](../statements/return.md)
