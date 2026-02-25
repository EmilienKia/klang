# Static Functions

[← Index](../index.md) · [Functions](functions.md)

A *static member function* is a member function that does not receive an implicit `this` parameter.
It belongs to the struct type itself, not to any particular instance.

---

## Contents
1. [Declaring a static function](#1-declaring-a-static-function)
2. [Calling a static function](#2-calling-a-static-function)
3. [Access rules inside a static function](#3-access-rules-inside-a-static-function)
4. [Static member variables](#4-static-member-variables)
5. [Examples](#5-examples)
---
## 1. Declaring a static function
A member function is declared static by placing the `static` keyword before the function name.
### Grammar
```
FunctionDecl (static member):
    'static' Identifier '(' [ ParameterList ] ')' [ ':' TypeSpec ] BlockStatement
```
**Example:**
```k
struct plop {
    static s : int = 12;
    add(a: int) : int {
        return a + s;       // non-static: can access s (static member)
    }
    static sub(b: int) : int {
        return b - s;       // static: can access s (static member)
    }
}
```
---
## 2. Calling a static function
Static member functions are called using the scope resolution operator `::`:
```k
result : int = plop::sub(43);   // calls static function
```
They may also be called via an instance (as a convenience), but this does not pass `this`:
```k
p : plop;
p.add(19);     // non-static: 'this' is 'p'
plop::sub(43); // static: no 'this'
```
---
## 3. Access rules inside a static function
A static function:
- **Cannot** access non-static member fields or methods (no `this`).
- **Can** access static member fields and other static functions.
- **Can** access global variables and free functions.
```k
struct Counter {
    static count : int;
    static increment() {
        count += 1;          // OK: static field
    }
    static getCount() : int {
        return count;        // OK: static field
    }
    // increment() cannot access 'this.value' if value were a non-static field
}
```
---
## 4. Static member variables
Member variables may also be declared `static`.  
A static member variable is shared by all instances of the struct (there is one copy).
```k
struct titi {
    a : int = 5;           // per-instance
    static b : int = 12;   // shared across all instances
}
```
Static member variables are accessed via `StructName::fieldName`:
```k
titi::b = 13;              // assigns to the shared field
```
They may also be read through an instance:
```k
t : titi;
x : int = t.a;    // instance field
y : int = titi::b; // static field via type name (preferred)
```
---
## 5. Examples
### Static method and static field
```k
module demo;
struct plop {
    static s : int = 12;
    add(a: int) : int {
        return a + s;
    }
    static sub(b: int) : int {
        return b - s;
    }
}
test_add() : int {
    p : plop;
    return p.add(19);    // 19 + 12 = 31
}
test_sub() : int {
    return plop::sub(43);  // 43 - 12 = 31
}
```
### Static member variable with reassignment
```k
module demo;
g : int = 7;
struct titi {
    a : int = 5;
    static b : int = 12;
    add() : int {
        l : int = 28;
        return a + b + l + g;
    }
}
test() : int {
    t : titi;
    t.a = 6;
    titi::b = 13;
    return t.add();   // 6 + 13 + 28 + 7 = 54
}
```
---
*See also:* [Functions](functions.md) · [Structures](../structs/structs.md) · [Constructors — Static constructors](../structs/constructors.md)
