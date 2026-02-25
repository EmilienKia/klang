# Identifiers and Name Expressions

[← Index](../index.md) · [Expressions](expressions.md)

An *identifier expression* or *name expression* denotes a declared entity by name.

---

## Contents
1. [Simple identifier expressions](#1-simple-identifier-expressions)
2. [Qualified identifier expressions](#2-qualified-identifier-expressions)
3. [Scope resolution in expressions](#3-scope-resolution-in-expressions)
---
## 1. Simple identifier expressions
A single identifier that resolves to a variable, parameter, or function in scope.
### Grammar
```
IdentifierExpr:
    QualifiedIdentifier
QualifiedIdentifier:
    [ '::' ] Identifier { '::' Identifier }
```
When the identifier resolves to a variable, the expression yields the stored value (after load).  
When it resolves to a function, the expression can be used as a callee in a function call.
**Examples:**
```k
x           // refers to variable 'x'
counter     // refers to variable or function 'counter'
myFunc      // refers to function 'myFunc'
```
Name lookup rules are described in [Names and Lookup](../basic/names.md).
---
## 2. Qualified identifier expressions
A sequence of identifiers separated by `::` accesses a name in a specific namespace or struct scope.
**Examples:**
```k
geometry::Point         // struct type from namespace 'geometry'
plop::sub               // static member function 'sub' of struct 'plop'
the::module::func       // function in nested namespace
Outer::Inner            // nested struct name
Outer::value            // field of outer struct (from an inner struct method)
```
A qualified name starting with `::` is anchored to the root namespace:
```k
::the::module::func()   // absolute path from root namespace
```
---
## 3. Scope resolution in expressions
The `::` operator is used both in type specifiers (for type names) and in expressions (for value and function names).
**Static member access:**
Static fields and functions of a struct are accessed with `StructName::member`:
```k
titi::b = 13;           // assign to static field 'b' of struct 'titi'
plop::sub(43)           // call static function 'sub' of struct 'plop'
```
**Outer struct field access from inner struct:**
Inside a non-static inner struct's method, fields of the outer struct can be accessed using `OuterStruct::fieldName`:
```k
struct Outer {
    value : int = 10;
    struct Inner {
        value : int = 0;
        getOuter() : int { return Outer::value; }  // disambiguation
    }
}
```
---
*See also:* [Names and Lookup](../basic/names.md) · [Expressions](expressions.md) · [Function Call](call.md)
