# Statements

[← Index](../index.md)

A *statement* is an executable unit of code inside a function body.
Statements are executed sequentially within a block.

---

## Contents
1. [Statement grammar overview](#1-statement-grammar-overview)
2. [Block statement](#2-block-statement)
3. [Expression statement](#3-expression-statement)
4. [Variable declaration statement](#4-variable-declaration-statement)
5. [Statement list and links](#5-statement-list-and-links)
---
## 1. Statement grammar overview
```
Statement:
    BlockStatement
    | ReturnStatement
    | IfElseStatement
    | WhileStatement
    | ForStatement
    | VariableDecl ';'
    | ExpressionStatement
BlockStatement:
    '{' { Statement } '}'
ExpressionStatement:
    [ Expression ] ';'
```
---
## 2. Block statement
A block encloses a sequence of statements and introduces a new scope for local variables.  
Variables declared inside a block are destroyed when the block exits.
### Grammar
```
BlockStatement:
    '{' { Statement } '}'
```
Blocks are used as the body of functions, `if`/`else` branches, `while` and `for` loops.
**Example:**
```k
{
    x : int = 5;
    y : int = x + 1;
    // x and y are destroyed here
}
```
---
## 3. Expression statement
An expression followed by a semicolon.  
Used to evaluate expressions for their side effects (assignments, function calls, increment/decrement).
### Grammar
```
ExpressionStatement:
    [ Expression ] ';'
```
A lone `;` is a valid empty statement.
**Examples:**
```k
x = 42;
p.add(8);
i++;
++counter;
```

> **Note:** If an expression statement produces an owner type (`T!`) — for example a bare
> `new Foo();` or a call to a function returning `T!` — the compiler emits
> **Warning 0x5010** and the object is deleted immediately after construction.
> See [Dynamic Allocation — Unassigned result](../memory/new-delete.md#1-new-expression--single-object).
---
## 4. Variable declaration statement
A local variable declaration introduces a named variable in the current scope.  
The variable's lifetime begins at the point of declaration and ends when the enclosing block exits.
### Grammar
```
VariableDecl:
    { Specifier } Identifier ':' TypeSpec [ Initialiser ] ';'
Specifier: (one of)
    'static'
    'const'
Initialiser:
    '=' ConditionalExpr                     -- value initialisation
    | '(' [ ExpressionList ] ')'            -- constructor initialisation
    | BraceInitList                         -- brace initialisation (arrays)
BraceInitList:
    '{' [ InitElement { ',' InitElement } ] '}'
InitElement:
    ConditionalExpr
    | (empty)                               -- default construction
```
The type specifier follows a colon (`:`) after the variable name.
**Examples:**
```k
x : int;                     // uninitialized (zero for primitives)
n : int = 42;                // value initialization
const MAX : int = 100;       // const variable — cannot be modified after initialization
s : short = 10s;             // typed with short literal
result : double = 3.14d;
flag : bool = true;
p : plop;                    // default-constructed struct
q : plop(5);                 // struct constructed with argument
arr : int[4];                // array of 4 ints
arr2 : int[3] {1, 2, 3};    // array with brace initializer list
arr3 : int[] {10, 20};       // array with size inferred from init list
ptr : int* = &x;             // pointer initialized to address of x
cptr : const int* = &x;      // pointer to const int — pointed value cannot be modified
ref : int& = x;              // reference bound to x — x must be an addressable variable
own : Foo! = new Foo(42);    // owner — Foo allocated on the heap
arrOwn : int[3]! = new int[3]{1, 2, 3};  // owner of a dynamically allocated array
```

> **Note on const:** A `const` variable must be initialised at declaration and cannot be
> subsequently assigned, incremented or decremented. See [Types — Const-ness](../basic/types.md#12-const-ness).

> **Note:** Reference variables (`int&`) must be initialised with an addressable object (lvalue).
> A literal or arithmetic expression is not allowed. The binding is permanent: assigning to
> `ref` modifies the referred-to object, not the reference itself.
> See [Types — Reference types](../basic/types.md#2-reference-types) for full constraints.
### Static local variables
A local variable declared with `static` persists across function calls.  
Its initialiser is evaluated only the first time the declaration is reached.
```k
test_static() : int {
    static i : int = init();   // init() called only once
    i += 1;
    return i;
}
// First call returns init() + 1
// Second call returns init() + 2
// etc.
```
### Variable lifetime and destruction
For struct-typed local variables:
- The constructor is called when the declaration is reached.
- The destructor is called when the enclosing block exits (or a `return` is reached), in reverse declaration order.
```k
test_local_dtor() : int {
    c : counter;           // constructor called here
    return dtor_count;     // return expression evaluated BEFORE c is destroyed
}                          // destructor called here (after return value is captured)
```

For owner-typed local variables (`T!`, `T[N]!`):
- If the owner is still non-null at scope exit, it is automatically deleted (destructor called + memory freed).
- Multiple owners in the same scope are destroyed in reverse declaration order.
- See [Dynamic Allocation — Ownership and lifetime](../memory/new-delete.md#4-ownership-and-lifetime).
---
## 5. Statement list and links
| Statement | Page |
|-----------|------|
| Block     | This page |
| Expression statement | This page |
| Variable declaration | This page |
| `if` / `else` | [If Statement](if.md) |
| `while`   | [While Statement](while.md) |
| `for`     | [For Statement](for.md) |
| `return`  | [Return Statement](return.md) |
---
*See also:* [Expressions](../expressions/expressions.md) · [Functions](../functions/functions.md) · [Types](../basic/types.md) · [Dynamic Allocation](../memory/new-delete.md)
