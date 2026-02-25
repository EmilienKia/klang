# Structures

[← Index](../index.md)

A *struct* (structure) is a user-defined composite type that aggregates fields and member functions.

---

## Contents
1. [Struct declaration](#1-struct-declaration)
2. [Fields](#2-fields)
3. [Member functions](#3-member-functions)
4. [Using a struct — declaring variables](#4-using-a-struct--declaring-variables)
5. [Field access and member function calls](#5-field-access-and-member-function-calls)
6. [Default field initialisation](#6-default-field-initialisation)
7. [Struct type in function parameters](#7-struct-type-in-function-parameters)
8. [Aggregated structs](#8-aggregated-structs)
9. [Struct specifiers](#9-struct-specifiers)
---
## 1. Struct declaration
A struct is declared with the `struct` keyword, a name, and a body containing field and member function declarations.
### Grammar
```
StructDecl:
    { Specifier } 'struct' Identifier '{' { Declaration } '}'
Specifier: (one of)
    'static'
```
`static` before `struct` declares a *static nested struct* (see [Nested Structures](nested.md)).
**Example:**
```k
module myapp;
struct plop {
    a : int;
    b : int;
    add(c: int) : int {
        return a + b + c;
    }
}
```
Struct declarations may appear at module level or inside another struct body.
---
## 2. Fields
Fields are the data members of a struct.  
They are declared with the same syntax as local variables (name `:` type, optional initialiser).
### Grammar
```
FieldDecl:
    { Specifier } Identifier ':' TypeSpec [ '=' ConditionalExpr ] ';'
```
**Examples:**
```k
struct Point {
    x : double;
    y : double;
}
struct Config {
    timeout : int = 30;      // default value 30
    enabled : bool = true;
    name    : char*;         // pointer field, no default
}
```
**Default values:**  
A field may have a default constant expression (evaluated once for the type, used when the field is zero/value-initialised or when a compiler-generated default constructor runs).
**Static fields:**  
A field declared `static` is shared across all instances.  
See [Static Functions — Static member variables](../functions/static.md#4-static-member-variables).
---
## 3. Member functions
Member functions are declared inside the struct body with the same syntax as free functions.  
They receive an implicit `this` parameter giving access to the current instance.
### Grammar
```
MemberFunctionDecl:
    { Specifier } Identifier '(' [ ParameterList ] ')' [ ':' TypeSpec ] BlockStatement
```
See [Functions](../functions/functions.md) for the full function syntax.
Inside a member function:
- Fields and other member functions are accessible by name (implicit `this.`).
- `this` is also available explicitly: `this.field`.
```k
struct Counter {
    n : int;
    increment() { n = n + 1; }
    get() : int { return n; }
    getExplicit() : int { return this.n; }  // same as get()
}
```
---
## 4. Using a struct — declaring variables
A struct variable is declared like any other variable, using the struct name as the type specifier.
```k
p : plop;              // default construction
q : plop(5);           // explicit constructor
g : plop;              // global struct variable
r : plop&;             // reference to plop (function parameter or binding)
ptr : plop*;           // pointer to plop
```
---
## 5. Field access and member function calls
Fields and member functions are accessed with `.` (object or reference) or `->` (pointer):
```k
p.a = 10;
p.b = 32;
result : int = p.add(8);     // calls member function
ptr->a = 5;
result2 : int = ptr->add(3);
```
---
## 6. Default field initialisation
Fields are zero-initialised by default:
- Integer fields → 0
- Float/double fields → 0.0
- Bool fields → false
- Pointer fields → null (0)
- Struct fields → recursively default-initialised
If a field has an explicit default expression (e.g., `a : int = 5`), that value is used instead of zero when the struct is default-constructed.
```k
struct plop {
    a : int = 5;     // default 5
    b : int = 12;    // default 12
    c : int;         // default 0
}
test_local() : int {
    p : plop;
    return p.sum();  // returns 5 + 12 + 0 = 17
}
```
---
## 7. Struct type in function parameters
Struct objects can be passed to functions by value, by reference, or by pointer:
```k
// Pass by reference (modifiable)
test_ref_call(r: plop&) : int {
    r.b = 72;
    return r.add();
}
// Pass by pointer
update(p: plop*) {
    p->a = 100;
}
```
Passing by value copies the struct; passing by reference or pointer avoids the copy and allows modification.
---
## 8. Aggregated structs
Structs may contain fields of other struct types (aggregation):
```k
struct plop {
    a : int = 3;
    add(c: int) : int { return a + c; }
}
struct titi {
    b : int = 5;
    p : plop;         // nested struct by value
    add() : int { return p.add(b); }
}
test() : int {
    t : titi;
    t.p.a = 7;
    return t.add();   // 7 + 5 = 12
}
```
---
## 9. Struct specifiers
| Specifier  | Meaning |
|------------|---------|
| `static`   | Declares a static nested struct (no implicit parent reference; see [Nested Structures](nested.md)). |
| `public`   | Visibility modifier (applies to subsequent declarations inside the struct body). |
| `protected`| Visibility modifier. |
| `private`  | Visibility modifier. |
---
*See also:* [Constructors](constructors.md) · [Destructors](destructors.md) · [Nested Structures](nested.md) · [Functions](../functions/functions.md) · [Types](../basic/types.md)
