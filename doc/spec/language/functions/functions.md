# Functions

[← Index](../index.md)

A *function* is a named, callable unit of code that takes parameters and optionally returns a value.

---

## Contents
1. [Function declaration and definition](#1-function-declaration-and-definition)
2. [Parameters](#2-parameters)
3. [Return type](#3-return-type)
4. [Function body](#4-function-body)
5. [Free functions vs. member functions](#5-free-functions-vs-member-functions)
6. [Global variables](#6-global-variables)
7. [Specifiers](#7-specifiers)
---
## 1. Function declaration and definition
In K, a function is always declared with its full definition (body).  
There are no forward declarations without a body in the current language version.
### Grammar
```
FunctionDecl:
    { Specifier } [ '~' ] Identifier '(' [ ParameterList ] ')' [ ':' TypeSpec ] BlockStatement
ParameterList:
    ParameterSpec { ',' ParameterSpec }
ParameterSpec:
    { Specifier } [ Identifier ':' ] TypeSpec [ '=' ConditionalExpr ]
Specifier: (one of)
    'public'  'protected'  'private'  'static'
```
- The function name is an identifier (preceded optionally by `~` for destructors).
- Parameters are enclosed in parentheses, separated by commas.
- The optional return type follows a `:`.
- If the return type is omitted, the function returns nothing (void).
- The body follows immediately as a block statement.
**Forward references:**  
Functions may call other functions that are defined later in the same file; the compiler does not require top-down ordering.
---
## 2. Parameters
Each parameter has an optional name and a required type, separated by `:`.  
If the name is omitted, the parameter is anonymous (can be used for signature matching but not referenced in the body).
### Grammar
```
ParameterSpec:
    { Specifier } [ Identifier ':' ] TypeSpec [ '=' ConditionalExpr ]
```
**Examples:**
```k
increment(i: int) : int { return i + 1; }
multiply(a: int, b: int) : int { return a * b; }
add(c: int) : int { return a + b + c; }   // 'a' and 'b' from enclosing struct
```
### Default parameter values
A parameter may have a default value expression:
```k
increment(n: int, step: int = 1) : int { return n + step; }
```
Only trailing parameters may have default values.  
The default expression is evaluated at each call site that omits the argument.
### Pass-by-value vs pass-by-reference
- A parameter of type `T` receives a copy of the argument.
- A parameter of type `T&` receives a reference to the argument (the caller's variable is affected).
- A parameter of type `T*` receives a pointer; the callee may modify `*ptr`.
```k
assign(var: int&, val: int) : int {
    var = val;    // modifies the caller's variable
    return var;
}
```
---
## 3. Return type
The return type follows `:` after the parameter list.  
If absent, the function is void (returns nothing).
```k
// Returns int
increment(i: int) : int { return i + 1; }
// Returns void (no return type)
init() {
    a = 4;
    b = 5;
}
// Returns double
half(x: double) : double { return x / 2.0d; }
```
---
## 4. Function body
The body of a function is a block statement (`{ ... }`).  
It may contain any number of statements, including variable declarations, control flow, and expressions.
```k
factorial(n: int) : int {
    result : int = 1;
    for (i: int = 2; i <= n; i += 1) {
        result *= i;
    }
    return result;
}
```
---
## 5. Free functions vs. member functions
**Free functions** are defined at namespace (module) level:
```k
module myapp;
add(a: int, b: int) : int { return a + b; }
```
**Member functions** are defined inside a `struct` body:
```k
struct Counter {
    n : int;
    increment() { n = n + 1; }
    get() : int { return n; }
}
```
Member functions receive an implicit `this` parameter (a reference to the enclosing struct instance).  
Inside a member function, struct fields are accessible by name without qualification (implicit `this.field`).
See also [Static Functions](static.md) for `static` member functions.
---
## 6. Global variables
Variables declared at the module level (outside any function or struct) are *global variables*.
### Grammar
```
VariableDecl (global):
    { Specifier } Identifier ':' TypeSpec [ Initialiser ] ';'
```
Global variables are initialised once before `main` is called (in dependency order, determined by the global constructor function).  
Their destructors are called after `main` returns (global destructor function).
**Examples:**
```k
module myapp;
a : int = 5;
b : int = 12;
g : plop;          // global struct, default-constructed
test() : int {
    return a + b;
}
```
For non-trivial initialisers (calling functions), the order of initialisation follows the dependency graph computed by the compiler.
---
## 7. Specifiers
| Specifier  | Meaning |
|------------|---------|
| `static`   | Declares a static member function (no implicit `this`). Can be called without an instance via `Struct::func()`. |
| `public`   | Sets default visibility to public for subsequent declarations. |
| `protected`| Sets default visibility to protected. |
| `private`  | Sets default visibility to private. |
> **Note:** `const` is reserved but not yet enforced in the current version.
---
*See also:* [Function Overloading](overloading.md) · [Static Functions](static.md) · [Structures](../structs/structs.md) · [Main Function](../basic/main.md) · [Return Statement](../statements/return.md)
