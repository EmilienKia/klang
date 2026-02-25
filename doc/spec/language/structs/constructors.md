# Constructors

[← Index](../index.md) · [Structures](structs.md)

A *constructor* is a special member function that initialises a struct instance.

---

## Contents
1. [Instance constructors](#1-instance-constructors)
2. [Member initializer list](#2-member-initializer-list)
3. [Compiler-generated default constructor](#3-compiler-generated-default-constructor)
4. [Constructor invocation syntax](#4-constructor-invocation-syntax)
5. [Constructor overloading](#5-constructor-overloading)
6. [Static constructors (class initializers)](#6-static-constructors-class-initializers)
7. [Examples](#7-examples)
---
## 1. Instance constructors
An instance constructor is a member function whose name matches the struct name.  
It has no return type.
### Grammar
```
ConstructorDecl:
    Identifier '(' [ ParameterList ] ')' [ ':' MemberInitList ] BlockStatement
MemberInitList:
    MemberInit { ',' MemberInit }
MemberInit:
    Identifier '(' [ ExpressionList ] ')'
```
**Example:**
```k
struct plop {
    a : int = 3;
    plop(c: int) {
        a = c + 1;
    }
}
```
---
## 2. Member initializer list
A constructor may have a member initializer list after `:`, before the body.  
This initializes fields directly, before the body executes.
```k
struct Point {
    x : int;
    y : int;
    Point(px: int, py: int) : x(px), y(py) {
        // body runs after x and y are initialized
    }
}
```
Member initializers use the syntax `fieldName(expression)`.  
Fields not listed are default-initialized (zero or their declared default value).
---
## 3. Compiler-generated default constructor
If a struct has no explicitly defined constructors, the compiler generates a default (no-argument) constructor.  
The generated constructor initializes each field to its declared default value (or zero if no default is specified).
If the struct has any user-defined constructor, **no** compiler-generated default constructor is added.  
In that case, a default-construction expression (`p : plop;`) requires that a user-defined no-argument constructor exists.
---
## 4. Constructor invocation syntax
### Variable declaration with constructor
```k
p : plop(5);           // calls plop(int) constructor
pt : Point(10, 20);    // calls Point(int, int) constructor
```
### Default construction
```k
p : plop;              // calls default (no-argument) constructor
```
If the struct has no constructor, the compiler-generated one is called.
---
## 5. Constructor overloading
Multiple constructors may be defined with different parameter lists.  
The compiler selects the matching constructor based on argument types.
```k
struct plop {
    a : int = 1;
    plop(c: int) {
        a = 3;
    }
    plop(d: double) {
        a = 5;
    }
}
test_int() : int {
    p : plop(2);       // calls plop(int), a = 3
    return p.a;
}
test_double() : int {
    p : plop(2.0d);    // calls plop(double), a = 5
    return p.a;
}
```
---
## 6. Static constructors (class initializers)
A *static constructor* is a static no-argument void function whose name is exactly the struct name.  
It acts as a class-level initialiser, called once at program startup (before `main`), via the global constructor function.
### Grammar
```
StaticConstructorDecl:
    'static' Identifier '(' ')' [ ':' StaticDepList ] BlockStatement
StaticDepList:
    StaticDep { ',' StaticDep }
StaticDep:
    QualifiedIdentifier '(' ')'
```
The optional dependency list (`: A(), gvar()`) declares that the named struct's static constructor or global variable must be initialised before this one.  
The compiler uses this information to produce a topologically sorted initialisation order.
**Example:**
```k
ctor_called : int;
struct Tracker {
    static Tracker() {
        ctor_called = 42;
    }
}
// Tracker's static constructor is called at program init.
// After init, ctor_called == 42.
```
**With dependency declaration:**
```k
struct B {
    static B() : A() {   // B's static ctor runs after A's
        // ...
    }
}
```
**Constraints:**
- A static constructor cannot be called explicitly.
- It must be no-argument and have no return type.
- There may be at most one static constructor per struct.
---
## 7. Examples
### Basic constructor
```k
module demo;
struct plop {
    a : int = 3;
    plop(c: int) {
        a = c + 1;
    }
    add(c: int) : int {
        return a + c;
    }
}
test() : int {
    p : plop(5);
    return p.add(7);   // (5 + 1) + 7 = 13
}
```
### Constructor with member initializer list
```k
struct Rect {
    w : int;
    h : int;
    Rect(width: int, height: int) : w(width), h(height) {}
    area() : int { return w * h; }
}
test() : int {
    r : Rect(4, 5);
    return r.area();  // 20
}
```
### Static constructor
```k
value : int;
struct Config {
    static Config() {
        value = 100;
    }
}
get_value() : int {
    return value;   // 100, set at program init
}
```
---
*See also:* [Structures](structs.md) · [Destructors](destructors.md) · [Function Overloading](../functions/overloading.md)
