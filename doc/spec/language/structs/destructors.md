# Destructors

[← Index](../index.md) · [Structures](structs.md)

A *destructor* is a special member function called automatically when a struct instance is destroyed.

---

## Contents
1. [Instance destructors](#1-instance-destructors)
2. [When destructors are called](#2-when-destructors-are-called)
3. [Static destructors (class finalizers)](#3-static-destructors-class-finalizers)
4. [Destructor and return statement interaction](#4-destructor-and-return-statement-interaction)
5. [Examples](#5-examples)
---
## 1. Instance destructors
An instance destructor is a member function whose name is `~` followed by the struct name.  
It takes no parameters and has no return type.
### Grammar
```
DestructorDecl:
    '~' Identifier '(' ')' BlockStatement
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
## 3. Static destructors (class finalizers)
A *static destructor* is a static no-argument void function whose name is `~` followed by the struct name.  
It acts as a class-level finaliser, called once at program shutdown (after `main` returns), via the global destructor function.
### Grammar
```
StaticDestructorDecl:
    'static' '~' Identifier '(' ')' BlockStatement
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
## 4. Destructor and return statement interaction
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
## 5. Examples
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
