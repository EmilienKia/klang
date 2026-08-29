*See also:* [Function Overloading](overloading.md) · [Operator Overloading](operators.md) · [Static Functions](static.md) · [Structures](../structs/structs.md) · [Main Function](../basic/main.md) · [Return Statement](../statements/return.md)
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
    { Specifier } [ '+' ] Identifier '(' [ ParameterList ] ')'
        [ Identifier ]
        [ ':' TypeSpec [ Initialiser ] ]
        FunctionBody
FunctionBody: (one of)
    BlockStatement
    '->' QualifiedIdentifier [ '(' [ TypeSpecList ] ')' ] ';'
    '->' 'default' ';'
    '->' 'delete' ';'
    { Specifier } [ '+' ] Identifier '(' [ ParameterList ] ')' [ ':' TypeSpec ] BlockStatement
ParameterList:
    ParameterSpec { ',' ParameterSpec }
ParameterSpec:
    { Specifier } [ Identifier [ '...' ] ':' ] TypeSpec [ '=' ConditionalExpr ]
Specifier: (one of)
    'public'  'protected'  'private'  'static'
```
- The function name is an identifier (preceded optionally by `+` for destructors).
- Parameters are enclosed in parentheses, separated by commas.
- The optional return type follows a `:`.
- An optional identifier before the `:` names the return variable (see [Named Return Variables](named-return.md)).
- Instead of a body block, a function may redirect to another function with `-> target;` (see [Function Redirectors](redirectors.md)).
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
    { Specifier } [ Identifier [ '...' ] ':' ] TypeSpec [ '=' ConditionalExpr ]
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

### Varargs parameters

A parameter declared with `...` after its name is a **varargs** (variable-length argument list)
parameter. It is syntactic sugar for an unsized array parameter (`T[]`):

```k
sum(values... : int) : int {
    // 'values' is of type int[] inside the body
    return values[0] + values[1] + values[2];
}
```

**Rules:**

- Must be the **last** parameter of the function.
- Only **one** varargs parameter per function.
- Cannot have a default value.
- At the call site, trailing arguments are automatically packed into a stack-allocated array:
  ```k
  sum(1, 2, 3);        // compiler creates int[3]{1, 2, 3} and passes it
  ```
- An explicit array of the matching type may be passed directly (no packing):
  ```k
  arr : int[3]{1, 2, 3};
  sum(arr);             // arr passed directly as int[]
  ```
- Zero arguments for the varargs position is valid (an empty array is created):
  ```k
  count(values... : int) : int { return values.size; }
  count();              // returns 0
  ```
- Non-varargs overloads are preferred over varargs overloads during resolution.
- Template varargs are supported:
  ```k
  template<typename T>
  first(args... : T) : T& { return args[0]; }
  ```
- Template parameter packs, expansion, and fold expressions are **not** supported.

**Errors** (compile-time):
- `0x0378` — Varargs parameter is not the last parameter.
- `0x0379` — Varargs parameter has a default value.
- `0x037A` — Multiple varargs parameters in a single function.
---
## 3. Return type
The return type optionally follows `:` after the parameter list.  
If absent, the return type is **deduced from the function body**:
- If the function body contains no `return` statements (or only empty `return;`), the function returns `void` (returns nothing).
- If the function body returns expressions (`return expr;`), the return type is inferred from the returned expression types. All return expressions must be compatible with the deduced return type.

> **Style & diagnostic rule:** Explicit return type annotation is strongly encouraged for non-void classic functions and methods. Omitting the return type on a classic function that returns a value (non-void) emits a compiler warning (`WARN_FUNC_RETURN_TYPE_OMITTED`, code `0x080E`). Functions returning `void` may omit `: void` without warning. For lambda expressions, return type omission is standard and emits no warning.

```k
// Explicit return type (int) — recommended
increment(i: int) : int { return i + 1; }

// Deduced return type (int) from return expression (emits warning 0x080E)
add(a: int, b: int) { return a + b; }

// Deduced void (no warning)
init() {
    a = 4;
    b = 5;
}

// Explicit double
half(x: double) : double { return x / 2.0d; }
```

**Returning structs by value:**  
A function may return a struct type.  At the call site, the returned value is an expression
temporary whose lifetime extends to the end of the enclosing full expression statement.
This allows chained member access and method calls on the returned value:

```k
make(v: int) : Point { p : Point(v, v); return p; }

test() : int {
    return make(42).x;   // member access on the returned temporary
}
```

See [Destructors — Return values and expression temporaries](../structs/destructors.md#4-return-values-and-expression-temporaries)
for the full lifetime rules.

### Named return variables

A function may name its return variable by placing an identifier between the closing
parenthesis and the colon of the return type.  The syntax mirrors a variable declaration:

```k
make(v : int) result : Obj(v) {
    result.val = result.val + 1;
}   // 'result' is implicitly returned
```

When a named return variable is present:
- The variable is declared as a local at the very beginning of the function body.
- Reaching the closing `}` of the function implicitly returns the named variable (no
  `return` statement needed).
- A bare `return;` may be used for early exit — it also returns the named variable.
- `return expr;` is accepted but emits a **warning**: the expression is assigned to the
  named variable, then the function returns.
- For aggregate types returned by value, **NRVO is guaranteed** (the named variable is
  constructed directly in the caller's destination — zero copy).
- The named return variable must not be `const`.
- Named return variables are not allowed on constructors, destructors, `abstract` functions,
  or `void` functions.

See [Named Return Variables](named-return.md) for the full specification.
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
*See also:* [Function Redirectors](redirectors.md) · [Function Overloading](overloading.md) · [Operator Overloading](operators.md) · [Static Functions](static.md) · [Structures](../structs/structs.md) · [Main Function](../basic/main.md) · [Return Statement](../statements/return.md)
