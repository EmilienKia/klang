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
    | BreakStatement
    | ContinueStatement
    | IfElseStatement
    | WhileStatement
    | ForStatement
    | VariableDecl ';'
    | ExpressionStatement
BlockStatement:
    '{' { Statement } '}'
ExpressionStatement:
    [ Expression ] ';'
BreakStatement:
    'break' ';'
ContinueStatement:
    'continue' ';'
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

> **Note:** If an expression statement produces struct-typed temporaries (e.g. function calls
> returning a struct by value, including chained method calls), those temporaries are
> destroyed at the end of the statement, in **reverse creation order**.
> See [Destructors — Return values and expression temporaries](../structs/destructors.md#4-return-values-and-expression-temporaries).
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
    | '(' [ ExpressionList ] ')' '[' Expression ']'  -- uniform array init
    | BraceInitList                         -- brace initialisation (arrays)
    | DesignatedBraceInitList               -- designated struct init
BraceInitList:
    '{' [ InitElement { ',' InitElement } ] '}'
InitElement:
    ConditionalExpr
    | (empty)                               -- default construction
DesignatedBraceInitList:
    '{' DesignatedInitElement { ',' DesignatedInitElement } '}'
DesignatedInitElement:
    '.' DesignatedMemberName '=' ConditionalExpr
  | '.' DesignatedMemberName '(' [ ExpressionList ] ')'
  | '.' DesignatedMemberName '=' DesignatedBraceInitList
DesignatedMemberName:
    [ Identifier '::' { Identifier '::' } ] Identifier
```
The type specifier follows a colon (`:`) after the variable name.

After parsing `( args )`, if the next token is `[`, it is a
[uniform array init](../memory/uniform-array-init.md) — all elements are initialized with
the same constructor arguments.  Otherwise, it is a single-variable constructor init.

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
arr4 : int(42)[5];           // uniform array init — all 5 elements set to 42
pts : Point(1, 2)[3];        // uniform array init — all 3 Points constructed with (1, 2)
ptr : int* = &x;             // pointer initialized to address of x
cptr : const int* = &x;      // pointer to const int — pointed value cannot be modified
ref : int& = x;              // reference bound to x — x must be an addressable variable
own : Foo! = new Foo(42);    // owner — Foo allocated on the heap
arrOwn : int[3]! = new int[3]{1, 2, 3};  // owner of a dynamically allocated array
arrLnk : int+[] {&x, &n};   // array of links to int (see Types §9.7)
arrPtr : int*[] {&x, &n};   // array of pointers to int
arrView : int?[] {&x, &n};   // array of view to int
arrOwnI : int![] {new int(1), new int(2)};  // array of owners of int
pt : Point { .x = 10, .y = 20 };            // designated struct init
tr : Trio { .b = 42 };                       // partial designated init (a, c default to 0)
d : D { .Base::v = 42, .w = 10 };           // qualified designated init (inheritance)
o : Outer { .inner = { .a = 1, .b = 2 } };  // nested designated init
```

> **Note:** See [Designated Struct Initializers](../structs/designated-init.md) for the full reference on the `.member = expr` and `.member(args…)` syntax.

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
- The destructor is called when the enclosing block exits (or a `return`, `break`, or `continue` is reached), in reverse declaration order.
```k
test_local_dtor() : int {
    c : counter;           // constructor called here
    return dtor_count;     // return expression evaluated BEFORE c is destroyed
}                          // destructor called here (after return value is captured)
```

> **Expression temporaries in variable declarations:**  
> If the initialiser expression of a variable declaration creates struct-typed temporaries
> (e.g. `x : Obj = make(42);`), those temporaries are destroyed at the end of the
> variable declaration statement, after the destination variable has been initialised.

> **Expression temporaries in control-flow conditions:**  
> Temporaries created in the condition expression of `if`, `while`, or `for` statements
> are destroyed after the condition is evaluated, before the controlled body executes.
> See [Destructors — Return values and expression temporaries](../structs/destructors.md#4-return-values-and-expression-temporaries).

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
| `break`   | [Break Statement](break.md) |
| `continue`| [Continue Statement](continue.md) |
| `throw`   | [Exception Handling](exceptions.md) |
| `try` / `catch` | [Exception Handling](exceptions.md) |
---
*See also:* [Expressions](../expressions/expressions.md) · [Functions](../functions/functions.md) · [Types](../basic/types.md) · [Dynamic Allocation](../memory/new-delete.md)
