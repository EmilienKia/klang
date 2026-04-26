# K Language — Complete Specification Summary

> **Version:** Working Draft — 2026  
> **Compiler:** klangc  
> **Formal grammar:** see [`grammar.ebnf`](grammar.ebnf) and [`grammar.md`](grammar.md)  
> **This document** is a self-contained summary of the K language specification. It covers all language rules; detailed files are referenced for examples and edge cases.

---

## Table of Contents

1. [Lexical Conventions](#1-lexical-conventions)
2. [Keywords](#2-keywords)
3. [Types](#3-types)
4. [Names, Namespaces and Resolution](#4-names-namespaces-and-resolution)
5. [Modules and Imports](#5-modules-and-imports)
6. [`using` Directives](#6-using-directives)
7. [Libraries — Export and Import](#7-libraries--export-and-import)
8. [Expressions](#8-expressions)
9. [Statements](#9-statements)
10. [Functions](#10-functions)
11. [Structures (`struct`)](#11-structures-struct)
12. [Classes (`class`)](#12-classes-class)
13. [Interfaces (`interface`)](#13-interfaces-interface)
14. [Inheritance](#14-inheritance)
15. [Constructors](#15-constructors)
16. [Destructors](#16-destructors)
17. [Nested Structures](#17-nested-structures)
18. [Designated Initializers](#18-designated-initializers)
19. [Enumerations (`enum`)](#19-enumerations-enum)
20. [Annotations](#20-annotations)
21. [Dynamic Allocation (`new` / `delete`)](#21-dynamic-allocation-new--delete)
22. [Uniform Array Initialization](#22-uniform-array-initialization)
23. [FFI (Foreign Function Interface)](#23-ffi-foreign-function-interface)
24. [`main` Function](#24-main-function)
25. [Templates](#25-templates)

---

## 1. Lexical Conventions

> Details: [lexical.md](basic/lexical.md)

- **Source encoding:** ASCII.
- **Whitespace:** space, tab, form-feed, line terminators (`LF`, `CR`, `CR LF`). Separate tokens, otherwise ignored.
- **Comments:**
  - End-of-line: `// …` until end of line.
  - Block: `/* … */` — non-nestable.
- **Tokens:** identifiers, keywords, literals, punctuators, operators.
- **Identifiers:** sequence of letters (`A-Z`, `a-z`, `_`) and digits (`0-9`), starting with a letter. Identifiers composed solely of `_` are reserved. Must not be a keyword.
- **Punctuators:** `(` `)` `{` `}` `[` `]` `;` `,` `::` `...` `@`
- **Operators:** `.` `->` `.*` `->*` `?` `:` `!` `~` `#` `=` `+` `-` `*` `/` `&` `|` `?` `%` `<<` `>>` `+=` `-=` `*=` `/=` `&=` `|=` `^=` `%=` `<<=` `>>=` `==` `!=` `<` `>` `<=` `>=` `&&` `||` `++` `--` `**`

---

## 2. Keywords

> Details: [keywords.md](basic/keywords.md)

```
bool     byte     char     short    int      long
float    double   unsigned
struct   class    interface   annotation   namespace   module   import   using   friend
static   const    abstract   final   override
public   protected   private
this     return
if       else     while    for      break
new      delete   default  enum
operator
template typename
```

All keywords are reserved and cannot be used as identifiers.

---

## 3. Types

> Details: [types.md](basic/types.md)

K is statically typed. Every expression has a type known at compile time.

### 3.1 Primitive Types

#### Integers

| Keyword           | Bits | Signed |
|-------------------|------|--------|
| `bool`            | 1    | —      |
| `byte` / `char`   | 8    | yes    |
| `unsigned byte`   | 8    | no     |
| `short`           | 16   | yes    |
| `unsigned short`  | 16   | no     |
| `int`             | 32   | yes    |
| `unsigned int`    | 32   | no     |
| `long`            | 64   | yes    |
| `unsigned long`   | 64   | no     |

`byte` and `char` are the same underlying type (8-bit signed). `unsigned` is a modifier.

#### Floating-Point

| Keyword  | Bits | Standard        |
|----------|------|-----------------|
| `float`  | 32   | IEEE 754 single |
| `double` | 64   | IEEE 754 double |

#### Boolean

`bool`: `true` or `false`. Distinct type from integers.

### 3.2 Indirection Types (Summary)

K has **six** indirection types. Four observers form a 2×2 matrix, plus an **owner** and a **drain**.

|                          | **Non-null**          | **Nullable**        |
|--------------------------|-----------------------|---------------------|
| **Immutable binding**    | `T&` — reference      | `T?` — view         |
| **Mutable binding**      | `T+` — link           | `T*` — pointer      |

- **`T!`** — owner: move-only, nullable, exclusive ownership of a dynamically allocated object.
- **`T#`** — drain: immutable binding, non-null, permission to steal the internal resources of the referenced object (analogous to C++ move).

All observer indirection types share the same memory size (one pointer) and calling convention.

#### Reference (`T&`)
- Immutable binding, non-null. Transparent: `r = 42` assigns to the object.
- Must be initialized with an lvalue. No rebinding. No null.
- `&r` produces a `T+`.

#### Link (`T+`)
- Mutable binding, non-null. Rebindable with `lnk = &y`.
- Assigning a scalar value (`lnk = val`) writes to the bound object (transparent).
- Assigning an address (`lnk = &y`) rebinds the link.
- Warning + runtime null-check if initialized from a nullable source.

#### View (`T?`)
- Immutable binding, nullable. No rebinding after initialization.
- `*view` with runtime null-check.

#### Pointer (`T*`)
- Mutable binding, nullable. Rebindable. `*p` with runtime null-check.
- Assigning a `T` to the pointer = error (use `*p = val`). Assigning an address = rebind.

#### Owner (`T!`)
- Move-only, nullable, exclusive ownership.
- `b = a`: move (a ← null, b takes ownership). The previous object in b is destroyed first.
- Auto-delete on scope exit if non-null.
- `delete owner`: destruction + free + owner ← null. No-op if already null.
- Only valid sources for `T!`: another `T!` (move), `new T(…)`, or `null`.
- Observer assignment (`T!` → `T*`, `T+`, `T?`, `T&`) copies the address without ownership transfer.

#### Drain (`T#`)
- Immutable binding, non-null, drain permission.
- Is **not** implicitly created from a reference: the `#` operator is explicitly required.
- **Is** implicitly convertible to `T&`, `T+`, `T?`, `T*`.
- After draining, the source object must remain in a valid state (typically default-constructed).

### 3.3 Null

`null` is a literal of a dedicated type. Implicitly convertible to `T*`, `T?`, `T!`. **Not** convertible to `T&` and `T+`. In boolean context: `false`.

Address comparison `==`/`!=` with any indirection (including `T+`, `T?`). `null == null` → `true`.

### 3.4 Array Types

Internal representation: `{ uint32 count; T[N] data; }`.

| Form           | Description                                    |
|----------------|------------------------------------------------|
| `T[N]`         | Fixed-size array (stack-allocated value)        |
| `T[N]&`        | Reference to a fixed-size array                 |
| `T[]` / `T[]&` | Reference to an unsized array                   |
| `T[N]!`        | Owner of a dynamically allocated array          |
| `T[]!`         | Owner of a runtime-sized dynamic array          |

- Implicit conversion `T[N]` → `T[]` (sized → unsized): zero-cost.
- Subscript: `arr[i]` — runtime bounds check (unsigned comparison). Out-of-bounds → `abort()`.
- Virtual member `size`: `arr.size` returns `unsigned int` (element count).
- Indirection arrays: `int+[]`, `int*[]`, `int?[]`, `int![]` are supported. Each element is an address slot.

### 3.5 Struct Types

See §11–§18.

### 3.6 Function Reference Types

| Form                    | Description                               |
|-------------------------|-------------------------------------------|
| `*(Params)`, `?(Params)`, `+(Params)` | Reference to a free or static function |
| `T::*(Params)`, `T::?(Params)`, `T::+(Params)` | Reference to a member method of `T` |

The parameter type list (excluding the implicit `this` for member methods) serves as discriminant for overload resolution.

### 3.7 Implicit Conversions

#### Widening (lossless)
`byte`/`char` → `short` → `int` → `long`; `float` → `double`; signed ↔ unsigned (same width).

#### Narrowing (possible loss)
`long` → `int` → `short` → `byte`; `double` → `float`. Accepted but with truncation risk.

#### Static Upcast (aggregate types)
`Derived*` → `Base*` implicitly. Compile-time GEP to address the Base sub-object.

#### Dynamic Downcast (class/interface)
`Base*` → `Derived*` with runtime RTTI. On mismatch: null (for `T*`, `T?`) or fatal trap (for `T+`, `T&`).

#### Owner Upcast/Downcast
Owner follows the same rules as indirections for upcast/downcast, with the same move constraints.

#### Indirection → `bool`
`T*`, `T+`, `T?`, `T!` non-null → `true`, null → `false`. Applicable in `if`, `while`, `for`, `&&`, `||`, `!`. `T&` is **not** convertible to `bool`.

### 3.8 Const

- `const` on a variable: immutable after initialization. No assignment, no increment/decrement.
- `const T*`, `const T&`, etc.: the pointed-to value cannot be modified.
- `const` on a member function: `this` is `const T&`, the function cannot modify fields.
- `const struct`: all non-static member functions are implicitly `const`.

### 3.9 Type Specifier Grammar

```
TypeSpec:
    [ 'const' ] ( FundamentalTypeSpec | QualifiedIdentifier ) { TypeSuffix }
TypeSuffix:
    '[' [ IntegerLiteral ] ']'   // array
    | '!'   // owner
    | '&'   // reference
    | '+'   // link
    | '?'   // view
    | '*'   // pointer
    | '#'   // drain
```

---

## 4. Names, Namespaces and Resolution

> Details: [names.md](basic/names.md)

### 4.1 Qualified Names

```
QualifiedIdentifier:
    [ '::' ] IdentifierSegment { '::' IdentifierSegment }

IdentifierSegment:
    Identifier [ TemplateArgList ]
```

Leading `::` = resolution from the root namespace (absolute).

Each segment of a qualified name may optionally include a template argument list (e.g., `Container<int>::Iterator`).

### 4.2 Namespaces

```
NamespaceDecl:
    'namespace' [ Identifier ] '{' { Declaration } '}'
```

Anonymous namespaces (without identifier): visibility limited to the current file.

### 4.3 Namespace Member Visibility

| Keyword      | Accessible from…                      | Exported in `.kdi`? |
|--------------|---------------------------------------|---------------------|
| `public`     | Everywhere (**default**)              | ✓ Full              |
| `protected`  | Same **module** only                  | ✓ Full              |
| `private`    | Same **namespace** only               | ✗ No (opaque block) |

Two mechanisms: per-element specifier (`public myFunc()…`) or group specifier (`public:` affects all following declarations).

### 4.4 Name Resolution Rules

Order (from innermost to outermost):
1. Local variables of the current block (and enclosing blocks).
2. Function parameters.
3. `static` local variables of the function.
4. Members of `this` (if in a non-static method) — implicit `this.member` lookup.
5. Members of the enclosing struct (for inner structs, via `__parent__`).
6. `using` directives at the current scope level.
7. Declarations in the current namespace (module namespace).
8. Declarations in enclosing namespaces, up to the root.
9. Imported modules (only for qualified names).

The first match wins. Shadowing: an inner declaration hides an outer one.

### 4.5 Scope Resolution Operator `::`

Access to a name in a specific namespace or struct scope. `::mod::name` = absolute form from root.

---

## 5. Modules and Imports

> Details: [modules.md](basic/modules.md)

### 5.1 Module Declaration

```
ModuleDeclaration:
    'module' QualifiedIdentifier ';'
```

- Must be at the very beginning of the file, before any import or declaration.
- At most one `module` per source file.
- If absent: declarations are in the root namespace.
- The module name defines the namespace hierarchy: `module math::linear` → namespace `math::linear`.

### 5.2 Multi-File Modules

All files passed to the compiler form a single compilation unit. Declarations are globally visible between files of the same module.

### 5.3 Import

```
ImportDeclaration:
    'import' QualifiedIdentifier ';'
```

- After the `module`, before other declarations.
- Resolution via `.kdi` file (CBOR description).
- Automatic transitive imports (recursive dependency loading).
- Unresolved import = fatal error. Circular import = fatal error (`0x80003`).
- Unused import = warning `0x80010`.
- Root namespace collision between two imports = error.
- **An import does NOT inject names** into the current namespace. Every imported symbol must be referenced by its fully qualified name.

### 5.4 Name Mangling

Scheme: `_K` + `F` (function) + `M` (member) + `N` (namespace sequence, each name prefixed by its length) + `E` (end) + parameter types.

Template arguments are encoded between `I` and `E` markers after the template entity name:
- Type argument: the mangled type (same encoding as parameter types).
- Value argument: `L` + type code + decimal value + `E` (negative with `n` prefix).
- Boolean value: `Lb0E` or `Lb1E`.

Examples: `Pair<int>` → `N4PairIiEE`, `Array<int, 10>` → `N5ArrayIiLi10EEE`.

---

## 6. `using` Directives

> Details: [using.md](basic/using.md)

Four forms:

| Form                               | Effect                                              |
|------------------------------------|-----------------------------------------------------|
| `using namespace X::Y;`            | All members of `X::Y` directly accessible           |
| `using X::Y::foo;`                 | Only `foo` is injected into the current scope        |
| `using Alias = X::Y::foo;`         | `foo` accessible under the name `Alias`              |
| `using namespace NS = X::Y;`       | `X::Y` accessible via the prefix `NS::member`        |

- Affects name lookup only — does not create new symbols.
- Aliases are never exported in `.kdi`.
- Non-transitive: a `using` in namespace A does not affect namespace B.
- Lookup priority: after direct members of the scope, before the parent scope.
- Can appear at namespace scope, in an aggregate body, or in a function/block body.
- Works with imported modules.

---

## 7. Libraries — Export and Import

> Details: [libraries.md](basic/libraries.md)

- A module without `main` is compiled into a shared library (`.so`) + KDI file (`.kdi`).
- The KDI is a CBOR binary describing the entire public and protected API.
- `private` members appear in the KDI as opaque blocks (size only) to preserve layout.
- KDI search order: `-i`, current directory, `-I`, `KLANG_LIB_PATH`, system directories.
- Inheritance from imported aggregates: supported (struct←struct, class←class/interface, interface←interface).
- Cross-type inheritance forbidden (error `30035`).

---

## 8. Expressions

> Details: [expressions.md](expressions/expressions.md)

### 8.1 Literals

> Details: [literals.md](expressions/literals.md)

#### Integers
Decimal, hexadecimal (`0x`), octal (`0…`), binary (`0b`).
Suffixes: (none)=`int`, `u`=`unsigned int`, `s`=`short`, `l`=`long`, `ul`/`lu`=`unsigned long`, `b`=`byte`.

#### Floating-Point
Format: `digits.digits[exponent][suffix]`. Suffix: (none)=`float`, `d`=`double`.

#### Booleans
`true`, `false`.

#### Characters
`'c'` — type `char` (8-bit signed). Escape sequences: `\n`, `\r`, `\t`, `\\`, `\'`, `\"`, `\0`, `\xNN`, `\NNN`.

#### Strings
`"…"` — type `const char[N]&` where N = length + 1 (implicit null terminator).
Internal layout: `{ i32 size, [N x i8] data }`. Deduplication per compilation unit.
Implicit conversion `const char[N]&` → `const char[]` (zero-cost).

#### Null
`null` — dedicated type. Convertible to `T*`, `T?`, `T!`. Not convertible to `T&`, `T+`.

### 8.2 Operator Precedence (highest to lowest)

| Prec. | Operators | Associativity |
|-------|-----------|---------------|
| 1     | `()` `[]` `.` `->` `++`(post) `--`(post) | Left→Right |
| 2     | `++`(pre) `--`(pre) `+` `-` `!` `~` `&` `*` `(type)` `new` `delete` `#` | Right→Left |
| 3     | `.*` `->*` | Left→Right |
| 4     | `*` `/` `%` | Left→Right |
| 5     | `+` `-` | Left→Right |
| 6     | `<<` `>>` | Left→Right |
| 7     | `<` `>` `<=` `>=` | Left→Right |
| 8     | `==` `!=` | Left→Right |
| 9     | `&` (bitwise AND) | Left→Right |
| 10    | `?` (bitwise XOR) | Left→Right |
| 11    | `\|` (bitwise OR) | Left→Right |
| 12    | `&&` | Left→Right |
| 13    | `\|\|` | Left→Right |
| 14    | `?:` (ternary) | Right→Left |
| 15    | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | Right→Left |

### 8.3 Unary Operators

> Details: [unary.md](expressions/unary.md)

- **`+x`**: identity (arithmetic type required).
- **`-x`**: arithmetic negation.
- **`!x`**: logical NOT → `bool`. Operand: `bool`, numeric, or indirection (→ nullity test).
- **`~x`**: bitwise NOT (integer required).
- **`&x`**: address-of — produces `T+` (link). If `x` is `const`, produces `const T+`.
- **`*x`**: dereference — produces `T&`. Operand `T+` (no null-check), `T?` or `T*` (null-check).
- **`#x`**: drain — produces `T#`. `x` must be a mutable lvalue.
- **Cast**: `(TypeSpec) expr` — explicit conversion (primitive or indirection upcast/downcast).
- **`++x`/`--x`**: pre-increment/decrement (returns the new value).
- **`x++`/`x--`**: post-increment/decrement (returns the old value).

### 8.4 Binary Operators

> Details: [binary.md](expressions/binary.md)

- **Arithmetic**: `+`, `-`, `*`, `/`, `%`. Integer division truncates towards zero. Division by zero = UB.
- **Bitwise**: `&` (AND), `|` (OR), `?` (XOR).
- **Shifts**: `<<`, `>>`. Right shift is arithmetic (signed) or logical (unsigned).
- **Comparisons**: `==`, `!=`, `<`, `>`, `<=`, `>=` → `bool`.
  - Address comparison for `T*`, `T+`, `T?`, `T!` (`==`/`!=` only). `T&` compares values.
- **Logical**: `&&`, `||` — short-circuit evaluation (and-then / or-else).
- **Ternary**: `cond ? then : else`.

### 8.5 Assignment Operators

> Details: [assignment.md](expressions/assignment.md)

`=` and compound: `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`.

Semantics depend on the target type — see §3.2 for per-indirection-type behavior.

Assignment is right-associative: `a = b = c` assigns `c` to `b`, then the result to `a`.

### 8.6 Function Call, Subscript and Member Access

> Details: [call.md](expressions/call.md)

- **Call**: `f(args…)`. Pass by value (copy) or by reference (`T&`).
- **Default parameters**: only trailing parameters may have default values.
- **Subscript**: `arr[i]` — runtime bounds-check. Works on any `T[N]`, `T[]`, and any indirection to an array.
- **Member access `.`**: fields and methods. Works on temporaries (rvalues) — method chaining supported. `arr.size` = element count.
- **Pointer access `→`**: equivalent to `(*p).member`, with null-check.
- **Member function reference call**: `(obj.*mfp)(args)` and `(ptr->*mfp)(args)` — parentheses required.

### 8.7 Temporary Construction

> Details: [temporary-construction.md](expressions/temporary-construction.md)

`T(args…)` in an expression context creates an anonymous temporary object allocated on the stack.
- Result: `T&`.
- Lifetime: until the end of the full statement (`;`).
- Destruction in reverse order of construction.
- Usable as a function argument, for method chaining, or in a `return`.
- Constructors `→ delete` produce an error if selected.
- Static constructors do not participate in resolution.

---

## 9. Statements

> Details: [statements.md](statements/statements.md)

### 9.1 Block

`{ statements… }` — introduces a new scope. Variables destroyed on block exit.

### 9.2 Expression Statement

`expr ;` — used for side effects. `;` alone = empty statement.
- If produces an unassigned `T!`: warning `0x5010`, object destroyed immediately.
- Struct temporaries destroyed at end of statement in reverse order.

### 9.3 Variable Declaration

```
{ Specifier } Identifier ':' TypeSpec [ Initialiser ] ';'
```

Specifiers: `static`, `const`.

Initializer: `= expr`, `(args…)` (constructor), `(args…)[N]` (uniform array init), `{init…}` (brace-init), `{.member = expr, …}` (designated).

- `const`: immutable after initialization.
- `static` local: persists across calls, initializer evaluated once.
- Lifetime: start = declaration, end = exit of enclosing block. Destruction in reverse order.
- Owner variables (`T!`): auto-delete on scope exit.

### 9.4 If / Else

> Details: [if.md](statements/if.md)

```
'if' '(' Expression ')' Statement [ 'else' Statement ]
'if' '(' IfCondVarDecl ')' Statement [ 'else' Statement ]
```

The test expression may be any type convertible to `bool`. `else if` chaining via nesting.

**Condition variable declaration (if-let):** A local variable may be declared as the condition.
Its value, cast to `bool`, determines the branch. The variable is scoped to the `if` (destroyed
at end of then/else). For non-nullable addressors (`&`, `+`), null triggers a soft-fail to `else`
and the variable does not exist on that path.

### 9.5 While

> Details: [while.md](statements/while.md)

```
'while' '(' Expression ')' Statement
```

### 9.6 For

> Details: [for.md](statements/for.md)

```
'for' '(' [ VarDecl ] ';' [ Expr ] ';' [ Expr ] ')' Statement
```

Init variable scoped to the loop. Omitted condition = infinite loop.

### 9.7 Return

> Details: [return.md](statements/return.md)

```
'return' [ Expression ] ';'
```

- Returning `T!`: move to the caller (local variable ← null before cleanup).
- Returning struct by value: copy into a temporary at the caller.
- The return expression is evaluated **before** local variable destruction.
- With a [named return variable](#104-named-return-variables): `return;` returns the named variable; reaching `}` also does.

### 9.8 Break

> Details: [break.md](statements/break.md)

```
'break' ';'
```

- Exits the **innermost** enclosing `while` or `for` loop.
- Local variables scoped inside the loop are destroyed in reverse declaration order before the loop exits.
- Using `break` outside a loop is a compile-time error.

---

## 10. Functions

> Details: [functions.md](functions/functions.md)

### 10.1 Declaration

```
{ Specifier } [ '+' ] Identifier '(' [ ParameterList ] ')'
    [ ReturnVarName ] [ ':' TypeSpec [ Initialiser ] ]
    FunctionBody
```

- No forward declarations without a body.
- Functions can refer to each other within the same file (no top-down ordering required).
- Return type after `:`. Omitted = void.
- `FunctionBody`: block `{…}`, redirector `→ target;`, `→ default;`, `→ delete;`.

### 10.2 Parameters

```
ParameterSpec:
    { Specifier } [ Identifier [ '...' ] ':' ] TypeSpec [ '=' ConditionalExpr ]
```

- Name is optional (anonymous if omitted).
- Default values only for trailing parameters.
- Pass by value (`T`): copy. If struct with destructor: destructor called on exit from the callee.
- Pass by reference (`T&`): lvalue required as argument.
- Pass by pointer (`T*`), link (`T+`), etc.

#### Varargs Parameters

A parameter declared with `...` after the name is a **varargs parameter** (variable-length
argument list). It is syntactic sugar for an unsized array parameter (`T[]`):

```k
fun sum(values... : int) : int { /* values is int[] */ }
fun format(fmt: int, args... : int) : int { /* ... */ }
```

Rules:
- Must be the **last** parameter of the function.
- Only **one** varargs parameter per function.
- Cannot have a default value.
- At the call site, individual trailing arguments are packed into a stack-allocated array:
  `sum(1, 2, 3)` → compiler creates `int[3]{1, 2, 3}` and passes it.
- An explicit array of the matching type can be passed directly (no packing):
  `arr : int[3]{1, 2, 3}; sum(arr);`
- Zero arguments for the varargs position is valid: `sum()` → empty `int[0]` array.
- Non-varargs overloads are preferred over varargs overloads during resolution.
- Template varargs are supported: `template<typename T> fun first(args... : T) : T&`.
- Template parameter packs / expansion / fold expressions are **not** supported.

### 10.3 Function Overloading

> Details: [overloading.md](functions/overloading.md)

Multiple functions with the same name differentiated by their parameter types.
- Resolution: exact match > widening > narrowing.
- Functions differing only by return type: forbidden.
- Applicable to free functions, members and constructors.

### 10.4 Named Return Variables

> Details: [named-return.md](functions/named-return.md)

```
make(v : int) result : Obj(v) { result.val = result.val + 1; }
```

- Variable declared at the start of the body. Implicit return on reaching `}`.
- `return;` = return the named variable. `return expr;` = warning + assignment.
- For aggregates returned by value: **guaranteed NRVO** (zero copy).
- Forbidden on constructors, destructors, `abstract`, `void`.

### 10.5 Function Redirectors

> Details: [redirectors.md](functions/redirectors.md)

```
foo() : int -> bar;
foo(a: int) -> bar(int);           // with disambiguation
public compute() : int -> Base::impl;  // visibility change
```

- No body: LLVM `GlobalAlias` to the target.
- Strictly compatible prototypes.
- Redirector chains allowed (transitive). Circular = error.
- Redirector visibility is independent of the target.
- Compatible with vtable and virtual dispatch.

### 10.6 Function References

> Details: [function_references.md](functions/function_references.md)

| Type                     | Description                                |
|--------------------------|--------------------------------------------|
| `*(Params)`              | Pointer to free function (nullable, rebindable) |
| `?(Params)`              | View to free function (nullable, immutable) |
| `+(Params)`              | Link to free function (non-null, rebindable) |
| `T::*(Params)` etc.      | Same for member method of `T`              |

- Obtaining address: `fp = add_one;` (symbol without `()`).
- Free call: `fp(args)`.
- Member call: `(obj.*mfp)(args)` / `(ptr->*mfp)(args)`.
- Overload disambiguation by declared type.
- First-class values: passable as parameter, returnable.

### 10.7 Operator Overloading

> Details: [operators.md](functions/operators.md)

Declaration with `operator` + operator symbol.

**Overloadable operators:**
- Binary: `+` `-` `*` `/` `%` `&` `|` `?` `<<` `>>` `&&` `||` `==` `!=` `<` `>` `<=` `>=` `=` `+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `<<=` `>>=`
- Unary: `+` `-` `~` `!` `++_` `--_` `_++` `_--`
- Cast: `operator() : TargetType`

Rules:
- **Member**: `this` = left operand. Binary = 1 explicit parameter. Unary = 0 parameters.
- **Non-member**: free function. Binary = 2 parameters. Unary = 1 parameter.
- Resolution: lowest conversion weight wins. On equal weight, **member takes priority**.
- `const operator`: callable on const objects. A non-const operator on a const object = error.
- Inheritance: operators are inherited. Redeclaring an operator in a derived type hides **all** homonymous operators from the parent (name hiding).
- Virtual dispatch for classes.
- Interfaces: abstract operators can be declared.
- Exported in libraries (member and non-member).
- `operator ++_` / `--_` = prefix. `operator _++` / `_--` = postfix.
- **Restrictions**: at least one aggregate operand, no new operators, no arity change, `[]` and `()` (call) are not overloadable.

### 10.8 Static Functions

> Details: [static.md](functions/static.md)

`static` before the name. No `this`. Called via `Struct::func()`.
- Can access `static` members and global variables.
- Cannot access non-static fields.

### 10.9 Global Variables

Declared at module level. Initialized before `main` (dependency order). Destructors after `main` (reverse order).

---

## 11. Structures (`struct`)

> Details: [structs.md](structs/structs.md)

### 11.1 Declaration

```
{ Specifier } 'struct' Identifier [ ':' BaseClause ] '{' { Declaration } '}'
```

### 11.2 Fields

```
{ Specifier } Identifier ':' TypeSpec [ '=' ConditionalExpr ] ';'
```

- Default values (constant expression).
- `static`: shared among all instances.

### 11.3 Member Functions

- Receive an implicit `this` (reference to the instance).
- Fields accessible by name (implicit `this.field`).

### 11.4 Member Visibility

| Keyword      | Accessible from…                       |
|--------------|----------------------------------------|
| `public`     | Everywhere (**default for struct**)    |
| `protected`  | Type and subtypes                      |
| `private`    | Type only                              |

By individual specifier or by group (`public:`, `protected:`, `private:`).

### 11.5 `final` Struct

`final struct S { … }`: cannot be used as a base.

### 11.6 `const` Member Functions

`const` before the function name: `this` is `const T&`. Callable on both mutable and const objects.

### 11.7 `const struct`

`const struct S { … }`: all non-static member functions are implicitly `const`.
- Can only inherit from `const struct`.
- A mutable struct can inherit from a `const struct`.

### 11.8 Aggregate Structs

A struct may contain fields of struct type → composition.

### 11.9 No Vtable

Calls via a `Base&` reference always invoke the `Base` implementation, regardless of the dynamic type.

---

## 12. Classes (`class`)

> Details: [classes.md](structs/classes.md)

### 12.1 Differences from `struct`

| Feature                         | `struct`             | `class`                         |
|---------------------------------|----------------------|---------------------------------|
| Virtual dispatch (vtable)       | ✗                    | ✓ Automatic (all non-static, non-private methods) |
| Virtual bases                   | ✗ Error              | ✓ Implicitly virtual            |
| Default variable visibility     | `public`             | `protected`                     |
| Default function visibility     | `public`             | `public`                        |
| `abstract`                      | ✗ Error              | ✓                               |
| Cross-inheritance               | ✗ Error              | ✗ Error                         |

### 12.2 Automatic Virtual Dispatch

Every non-static, non-private method of a `class` is automatically virtual. No `virtual` keyword.

### 12.3 `final` on Methods

- **New `final` function** (no override): NOT in the vtable. Direct call only.
- **`final` override**: applied to the inherited vtable slot but seals it. Subclasses can no longer override (warning if attempted).

### 12.4 Private Methods: Non-Virtual

`private` methods have no vtable slot. A `private` that shadows an inherited virtual = error.

### 12.5 Constructors and Destructors: Non-Virtual

Called directly by the compiler. Base destructor chaining is correct.

### 12.6 Virtual Bases and Diamond

All class bases are **implicitly virtual** → in a diamond, a single copy of the shared base. The most-derived class constructs/destroys the shared base.

### 12.7 `override` Specifier

`override` before the method name: asserts that the method overrides an inherited virtual. If no actual override = error. Omitting `override` on a valid override = warning.

### 12.8 Abstract Classes

`abstract class C { … }`: cannot be instantiated directly. `abstract` method = no body, must be overridden by concrete subclasses.

### 12.9 RTTI and Dynamic Downcast

Classes have an RTTI slot in the vtable (slot 0). Downcast via explicit cast `(Derived*) base_ptr` uses runtime RTTI. Nullable targets → null on mismatch. Non-null targets → fatal trap.

---

## 13. Interfaces (`interface`)

> Details: [interfaces.md](structs/interfaces.md)

```
{ Specifier } 'interface' Identifier [ ':' BaseClause ] '{' { Declaration } '}'
```

- All methods are implicitly abstract (no body).
- No fields, constructors, destructors.
- All methods are implicitly `public`.
- Can only inherit from other interfaces.
- Implemented by a `class` that provides a body for each method.
- Virtual dispatch via vtable.
- `abstract` accepted but redundant (warning).
- `final` prevents inheritance.
- Multiple implementation (a class can implement several interfaces).

---

## 14. Inheritance

> Details: [inheritance.md](structs/inheritance.md)

### 14.1 Cross-Inheritance Rules

| Base       | Derived    | Allowed  |
|------------|------------|----------|
| `struct`   | `struct`   | ✓        |
| `struct`   | `class`    | ✗ (error 30035) |
| `class`    | `struct`   | ✗ (error 30035) |
| `class`    | `class`    | ✓ (base implicitly virtual) |
| `class`    | `interface`| — (not in this direction) |
| `interface`| `class`    | ✓ (the class implements the interface) |
| `interface`| `interface`| ✓        |
| `interface`| `struct`   | ✗        |

### 14.2 Diamond

- **Struct**: two independent copies of the base (no sharing).
- **Class**: a single shared copy (implicitly virtual bases).

### 14.3 `final` and `const`

- `final` struct/class/interface: cannot be a base (error 30012).
- `const struct`: can only inherit from `const struct` (error 30033).

### 14.4 Static Indirection Upcast

`Derived*` → `Base*` implicitly. Compile-time GEP. Works for `&`, `+`, `?`, `*`.

---

## 15. Constructors

> Details: [constructors.md](structs/constructors.md)

### 15.1 Instance Constructors

Name = struct name. No return type.

```
StructName '(' [ ParameterList ] ')' [ ':' MemberInitList ] BlockStatement
```

- **Member initializer list**: `field(expr)` — initializes before the body.
- Fields not listed: default value or zero.

### 15.2 Compiler-Generated Default Constructor

If no instance constructor is defined and the default constructor is not `→ delete`, the compiler generates one (initializes each field to its default value or zero).

Static constructors do NOT count and do NOT suppress the generated default constructor.

### 15.3 Constructor Overloading

Multiple constructors with different parameter lists. Resolution by argument types.

### 15.4 Defaulted and Deleted Constructors

```
StructName() -> default;    // force generation even if other ctors exist
StructName() -> delete;     // forbid default construction
```

### 15.5 Static Constructors (Class Initializers)

```
static StructName() { /* called at program startup */ }
```

- Called automatically before `main`.
- Cannot be called explicitly.
- Does NOT suppress the default instance constructor.
- Does NOT participate in resolution for temporary construction.

---

## 16. Destructors

> Details: [destructors.md](structs/destructors.md)

### 16.1 Instance

```
~StructName() { /* … */ }
```

`+` prefix in grammar (`'+' Identifier '(' ')' BlockStatement`). At most one per struct.

### 16.2 When Destructors Are Called

- **Local variables**: on block exit, reverse declaration order.
- **`return`**: expression evaluated first, then destructors in reverse order.
- **`break`**: loop-scoped destructors in reverse order, then loop exits.
- **Global variables**: after `main`, reverse initialization order.
- **Owner variables (`T!`)**: on explicit `delete` or on scope exit.
- **Dynamic arrays**: destructors in reverse order (last → first), then `free`.
- **By-value parameters**: destructor at end of the callee.
- **Expression temporaries**: destroyed at end of statement (`;`) in reverse creation order.
- **Temporaries in conditions** (`if`, `while`, `for`): destroyed after condition evaluation, before the body.

### 16.3 Static Destructors (Class Finalizers)

```
static ~StructName() { /* called after main */ }
```

---

## 17. Nested Structures

> Details: [nested.md](structs/nested.md)

### 17.1 Static Nested Struct

`static struct Inner { … }` — no implicit parent reference. Directly instantiable via `Outer::Inner(…)`.

### 17.2 Non-Static Inner Struct

`struct Inner { … }` (without `static`) — contains a hidden `__parent__` field (pointer to the enclosing instance).
- Instantiation inside a method of the enclosing struct: `__parent__` = `this` automatically.
- Access to enclosing struct fields via automatic name resolution (through `__parent__`).
- Shadowing: an inner field hides an outer field. Explicit access: `Outer::fieldName`.

---

## 18. Designated Initializers

> Details: [designated-init.md](structs/designated-init.md)

```
p : Point { .x = 10, .y = 20 };
```

### 18.1 Two Forms per Member

- **Assignment**: `.member = expr`
- **Constructor call**: `.member(args…)`
- **Nested**: `.member = { .sub = … }`

### 18.2 Rules

- Each `.field` must name an existing accessible field.
- No duplicates, no mixing positional/designated.
- Unmentioned fields: default value or zero.
- Order-independent (no need to follow declaration order).
- Qualified member support for inheritance: `.Base::field = expr`.
- Nested initialization supported for struct fields.

---

## 19. Enumerations (`enum`)

> Details: [enums.md](enums/enums.md)

```
{ Specifier } 'enum' Identifier [ ':' TypeSpec ] '{' { EnumEntry } '}' ';'
```

### 19.1 Entries

`Identifier [ '=' ( IntegerLiteral | Identifier ) | BraceInitList | '(' [ ExpressionList ] ')' ] [ 'default' ] ';'`

- Auto-incrementation from the last value + 1 (first = 0).
- Duplicate values allowed (aliases).
- `default`: at most one entry. If absent, the first is the default.

### 19.2 Underlying Type

The compiler chooses the smallest primitive integer type containing all values. Unsigned if all ≥ 0.
With an explicit primitive type (`enum E : unsigned byte`), that explicit primitive is used.

### 19.2.1 Object-backed typed enums

`enum E : AggregateType { ... }` defines an object-backed enum:

- Runtime representation is an integer index.
- Each entry maps to one object in a static backing table.
- `E -> const AggregateType&` conversion returns a reference to that table entry.
- `AggregateType -> E` conversion performs a runtime lookup against the backing
  table and requires value equality support for the aggregate.
- `AggregateType -> E` on non-match is fatal by default.
- In `if` condition-variable declarations (`if (e : E = obj)`), a non-match
  follows the existing soft-fail path and branches to `else`.
- Entry initializers support designated braces (`NAME{.f = v}`), constructor form (`NAME(args)`), and zero-init (`NAME`).
- Implicit entries in object-backed enums derive from the previous entry value
  (copy + increment semantics when supported by the underlying object shape).

### 19.3 Enum Derivation

`enum Derived : Base { … }` — simple inheritance. Inherits all entries from the base. Can add more.
- Multi-level chains supported.
- Implicit conversion `Derived` → `Base` (upcast). `Base` → `Derived` = error.
- The derived underlying type covers all entries (inherited + local).
- For typed enums, derivation inherits the base enum underlying object type.

### 19.4 Usage

- Qualified access: `Color::BLUE`.
- Construction: `Color(GREEN)`, `Color(2)`, `Color` (default).
- Implicit conversion enum ↔ primitive integer.
- Six comparison operators supported.

---

## 20. Annotations

> Details: [annotations.md](annotations/annotations.md)

### 20.1 Declaration

```
{ AnnotationDef } { Specifier } 'annotation' Identifier [ ':' BaseClause ] '{' { Declaration } '}'
```

- Implicitly inherits from `k::Annotation`.
- Implicitly `const` (fields immutable after construction, methods implicitly `const`).
- Vtable with a single RTTI slot (slot 0). Methods are **not** virtual.
- Default visibility: `public` (like structs).

### 20.2 Members

- Variables: primitive types, internal enums, arrays of primitives, `const char[]`, other annotation types (by value), arrays of annotation views.
- Methods: implicitly `const`, direct calls only.
- Internal enums: declared in the annotation body.
- **Forbidden**: classes, structs, pointers, owners, mutable types.

### 20.3 Application

```
'@' QualifiedIdentifier [ '(' args… ')' | '{' .f=v, … '}' | '{' init… '}' ]
```

Three forms: default (`@Ann`), positional (`@Ann(args…)`), designated (`@Ann{.f=v, …}`).

Supported targets: `class`, `interface`, `annotation`, functions (with RTTI), constructors/destructors (SOURCE only), parameters. **NOT** `struct` (no vtable).

### 20.4 Construction Rules

- Arguments in declaration order of fields (positional) or by name (designated).
- **All expressions must be compile-time constants.**
- Recursive construction for annotation fields.
- Default values for unspecified fields.

### 20.5 Meta-Annotations

Defined in `k::annotations` (module `k`).

| Meta-annotation | Effect |
|-----------------|--------|
| `@Retention(Policy)` | `SOURCE` = compiler only; `RUNTIME` = emitted in RTTI binary (**default**). |
| `@Inherited` | The annotation propagates to subclasses (not to interface implementations). Explicit override possible. |
| `@Target({ElementType::…})` | Restricts the target element types. Absent = everything allowed. |

`Target.ElementType`: `CLASS`, `INTERFACE`, `ANNOTATION`, `FUNCTION`, `CONSTRUCTOR`.

### 20.6 Runtime Reading

Via `getClass().getAnnotations()` → `const Annotation?[]?`. Each annotation has `getAnnotationType()` for identification.

### 20.7 Export

Annotation types are exported via `.kdi`. Instances attached to classes/interfaces are serialized in RTTI metadata.

---

## 21. Dynamic Allocation (`new` / `delete`)

> Details: [new-delete.md](memory/new-delete.md)

### 21.1 `new` — Single Object

```
'new' TypeName '(' [ ExpressionList ] ')'
```

- Allocates via `malloc(sizeof(T))`, calls the constructor, returns `T!`.
- `TypeName` = raw type name (no indirection suffix).
- Forbidden on abstract classes (error `0x0057`).
- Unassigned result: warning `0x5010`, object destroyed immediately.

### 21.2 `new` — Array

```
'new' TypeName '[' [ Expression ] ']' [ '{' InitList '}' ]
'new' TypeName '{' [ InitList ] '}'
```

| Form | Result type |
|------|------------|
| `new T[N]{…}` (N constant) | `T[N]!` |
| `new T[expr]` (runtime) | `T[]!` |
| `new T{e₀,…,eₖ}` | `T[k]!` |
| `new T{}` | `T[0]!` |

- Static or dynamic size. Init list forbidden with dynamic size (`0x422A`).
- Uninitialized elements = zero (primitives) or default constructor (structs).
- Empty slots (`, ,`) = default-init.
- Internal layout: `{ uint32 count; T[N] data; }`.

### 21.3 `delete`

```
'delete' OwnerExpr
```

- `OwnerExpr` = modifiable lvalue of type `T!`, `T[N]!`, or `T[]!`.
- `delete` on non-owner = error (`0x005A`).
- If null: no-op. Otherwise: destructor (virtual dispatch for classes) + `free` + owner ← null.
- Arrays: destructors in reverse order (last → first), then `free`.
- Result type: `void`.

### 21.4 Lifetime and Ownership

- **Scope-based deletion**: non-null owner on scope exit → auto-delete.
- **Assignment**: the previous object is destroyed before transfer.
- **`= null`**: destroys the object and sets the owner to null.
- **Unassigned return**: warning `0x5010`, object destroyed immediately.

### 21.5 Interaction with Observer Indirections

Owner → `T*`, `T?`, `T+`, `T&`: address copy, owner retains ownership. Observers become dangling after owner destruction.

---

## 22. Uniform Array Initialization

> Details: [uniform-array-init.md](memory/uniform-array-init.md)

```
arr : T(args…)[N];              // stack, all elements constructed with the same args
p : T[N]! = new T(args…)[N];    // heap, same
```

- `TypeName(ExpressionList)[Expression]` — constructor arguments applied to each element.
- Constant size (stack and heap) or runtime size (heap only).
- Construction in order (index 0 to N−1). Destruction in reverse order.
- No ambiguity with `new T(args)` (single object): the presence of `[N]` after `)` distinguishes the forms.
- Layout identical to existing arrays.

---

## 23. FFI (Foreign Function Interface)

> Details: [ffi.md](functions/ffi.md)

### 23.1 `@ffi::Extern`

```
@ffi::Extern("C") my_c_func(x : int) : int;
```

- Marks a function as externally defined (no K body).
- `language` mandatory, only `"C"` supported.
- Mangling: direct C name (no K name mangling).
- On a class member: must be `static`.
- Optional `symbol` member to specify the exact C symbol.

### 23.2 `@ffi::CString`

```
@ffi::Extern("C") strlen(@ffi::CString s : const char*) : long;
```

- Parameter annotation only, on `@Extern("C")` functions.
- Semantic marker: the parameter is a null-terminated C `char*`.
- Parameter type: must be an addresser (`&`, `*`, `?`, `+`, `!`) to `char`.

---

## 24. `main` Function

> Details: [main.md](basic/main.md)

```
main() : int { return 0; }
main() { /* void, exit code = 0 */ }
```

- Must be at module level (top-level namespace).
- At most one `main` per compilation unit.
- No K-level parameters (no direct access to CLI arguments).
- The compiler generates a C `main(int, char**)` wrapper that:
  1. Calls global constructors.
  2. Calls the K `main()`.
  3. Calls global destructors.
  4. Returns the exit code.

---

## 25. Templates

> Details: [templates.md](templates/templates.md)

Templates provide compile-time parametric polymorphism. A template declaration introduces
one or more compile-time parameters — type or value — that are substituted when the
template is instantiated with concrete arguments.

K templates follow a **monomorphization** model: each unique set of template arguments
produces a distinct, fully-independent concrete entity. There is no type erasure or runtime
dispatch on template parameters.

### 25.1 Template Declaration

```
TemplateDeclaration:
    'template' '<' TemplateParameterList '>'

TemplateParameterList:
    TemplateParameter { ',' TemplateParameter }

TemplateParameter:
    TemplateParameterKind Identifier [ ':' TypeSpec ] [ '=' ConditionalExpr ]

TemplateParameterKind:
    'typename' | 'struct' | 'class' | 'interface'
    | Identifier | TypeSpec                            (* value parameter *)
```

The `TemplateDeclaration` is placed **after** annotations and **before** specifiers:

```k
template<typename T>
struct Pair {
    first  : T;
    second : T;
}

template<typename T>
max(a: T&, b: T&) : T {
    if(a > b) return a;
    return b;
}
```

### 25.2 Template Parameters

#### Type Parameters

| Keyword     | Constraint                                     |
|-------------|------------------------------------------------|
| `typename`  | Any type                                       |
| `struct`    | Must be a `struct` type                        |
| `class`     | Must be a `class` type                         |
| `interface` | Must be an `interface` type                    |

Optional base-type constraint: `class T : Animal` — T must be or derive from `Animal`.

Optional default type: `typename T = int`.

#### Value Parameters

No keyword prefix. A mandatory `: TypeSpec` specifies the type of the compile-time constant.

```k
template<typename T, N : unsigned int = 16>
struct FixedArray {
    data : T[N];
}
```

Value parameter types must be compile-time-evaluable: primitive integers, `bool`, `char`, enums.
All primitive types (`bool`, `char`, `byte`, `short`, `int`, `long`, `float`, `double`, and
their unsigned variants) are supported as value parameter types.

#### Parameter Ordering

Type and value parameters may be mixed freely. Once a parameter has a default, all subsequent
parameters must also have defaults.

### 25.3 Template Instantiation

Templates are instantiated by supplying concrete arguments in angle brackets:

```
TemplateArgList:
    '<' TemplateArg { ',' TemplateArg } '>'

TemplateArg:
    TypeSpec | ConditionalExpr
```

```k
p : Pair<int>;
result : int = max<int>(x, y);
arr : FixedArray<float, 10>;
```

In Phase 1, all template arguments must be supplied explicitly (no deduction). Trailing
arguments with defaults may be omitted, including the `<>` syntax for all-defaulted templates.

### 25.4 Instantiation Semantics

1. Look up the template declaration.
2. Match each argument to the corresponding parameter (kind filter, constraint, type).
3. Apply defaults for trailing omitted parameters.
4. Check the instantiation cache — reuse if already instantiated.
5. Perform monomorphization: clone model members, substitute types/values.
6. Register the concrete entity for future reuse.

Instantiation occurs lazily (on first use). Concrete entities are placed in the same scope
as the template definition.

### 25.5 Type Constraints

The `TemplateParameterKind` acts as a **kind filter**: `struct`, `class`, or `interface`
restricts the type argument to that specific aggregate kind.

The optional `: BaseType` constraint requires the argument to be or derive from `BaseType`.

Both are validated at instantiation time. Violations produce diagnostic errors:
- `0x0184` — type argument is not an aggregate (for `struct`/`class`/`interface` kind filter)
- `0x0182` — type argument is wrong aggregate kind
- `0x0183` — type argument does not satisfy base-type constraint

### 25.6 Name Mangling

Template instantiations are mangled with `I…E` markers after the entity name:

| Template Use | Mangled Form |
|---|---|
| `Pair<int>` | `N4PairIiEE` |
| `Pair<unsigned long>` | `N4PairIyEE` |
| `Array<int, 10>` | `N5ArrayIiLi10EEE` |
| `swap<float>` | `_KFN4swapIfEE…` |

### 25.7 Interaction with Other Features

- **Inheritance**: template aggregates may inherit from non-template or instantiated template types.
- **Virtual dispatch**: template classes have their own vtable per instantiation.
- **Annotations**: propagated to each instantiation.
- **Visibility**: specifiers on the template definition apply to all instantiations.
- **Constructors/Destructors**: instantiated with the aggregate; no independent template params.
- **Operator overloading**: template functions may be operator overloads.
- **`using` directives**: `using IntPair = Pair<int>;` works. Parameterized `using` aliases are Phase 2.

### 25.8 Parsing Ambiguity

The `<` token is both the less-than operator and the template argument list opener. The parser
resolves this by checking whether the preceding identifier names a template declaration. Inside
a template argument list, nested `<`/`>` are balanced. `>>` is split into two `>` when inside
nested template arguments (e.g., `Pair<Pair<int>>`).

### 25.9 Phase 1 Limitations

Phase 1 does **not** support:
- Partial or full specialization.
- Template template parameters.
- Variadic template parameters (parameter packs).
- Template argument deduction from function arguments.
- Concepts or type traits beyond base-type constraints.
- Templates on constructors, destructors, operators, or enums independently.
- `extern template` declarations.
- Template aliases (`template<typename T> using Vec = Array<T, 16>`).

---

## Appendix — Indirection Type Summary

| Suffix | Name      | Null | Rebind | Ownership | Deref null-check   |
|--------|-----------|------|--------|-----------|---------------------|
| `&`    | Reference | ✗    | ✗      | No        | N/A (transparent)   |
| `+`    | Link      | ✗    | ✓      | No        | No (non-null)       |
| `?`    | View      | ✓    | ✗      | No        | Yes                 |
| `*`    | Pointer   | ✓    | ✓      | No        | Yes                 |
| `!`    | Owner     | ✓    | ✓ (move) | **Yes** | Yes                 |
| `#`    | Drain     | ✗    | ✗      | No        | N/A (non-null)      |

---

## Appendix — Assignment Semantics by Indirection Type

| LHS    | `= val` (T)                      | `= &y` / `= lnk` (indirection) | `= owner` (T!)                     |
|--------|-----------------------------------|---------------------------------|-------------------------------------|
| `T&`   | Assigns to the referenced object  | Error (no rebind)               | Error                               |
| `T+`   | Assigns to the bound object       | **Rebind**                      | Address copy (observer)             |
| `T?`   | Error (no rebind)                 | Error (no rebind)               | Error                               |
| `T*`   | Error (`*p = val` required)       | **Rebind**                      | Address copy (observer)             |
| `T!`   | Error                             | Error                           | **Move** (source ← null, previous destroyed) |

---

## 26. Generics

> Formal grammar: see [`grammar.ebnf`](grammar.ebnf) §GenericDeclaration.

Generics provide **uniform** parametric polymorphism over type arguments. Unlike
templates (§25), a generic aggregate or function is compiled **exactly once**:
all generic type parameters are mapped to opaque pointers at the LLVM IR level,
and a single binary artefact serves all concrete type arguments.

This model resembles Java/C# generics (type erasure at the code level) but
preserves static type-checking at the K source level.

### 26.1 Generic Declaration

```
GenericDeclaration:
    'generic' '<' GenericParameterList '>'

GenericParameterList:
    GenericParameter { ',' GenericParameter }

GenericParameter:
    GenericParameterKind Identifier [ ':' TypeSpec ]

GenericParameterKind:
    'typename' | 'struct' | 'class' | 'interface'
```

The `GenericDeclaration` prefix is placed **after** annotations and **before**
specifiers, in the same syntactic position as `TemplateDeclaration`.

```k
// Generic aggregate
generic<class T> class Box {
    private val : T!;
    public:
    Box(v : T!) { val = v; }
    get() : T* { return val; }
}

// Generic free function
generic<class T> identity(v : T*) : T* { return v; }

// Generic method inside a non-generic class
class Util {
    generic<class T> wrap(v : T*) : T* { return v; }
}
```

### 26.2 Generic Parameters

Only **type parameters** are allowed. Value parameters (`N : unsigned int`) are
rejected at parse time with diagnostic `0x01A0`
(`ERR_GENERIC_VALUE_PARAM_NOT_ALLOWED`).

| Keyword     | Constraint                                | Owner (`!`) allowed |
|-------------|-------------------------------------------|---------------------|
| `typename`  | Any type                                  | ✗                   |
| `struct`    | Must be a `struct` type                   | ✗                   |
| `class`     | Must be a `class` type                    | ✓                   |
| `interface` | Must be an `interface` type               | ✓                   |

No default types are supported (templates support `= DefaultType`; generics do not).

### 26.3 Constraints on Type Parameter Usage

Within the body of a generic declaration, the type parameters may appear **only
through an addresser** (`&`, `*`, `!`, `?`, `+`, `#`). Direct bare usage (e.g.
`local : T;`) is forbidden because the compiler maps type parameters to uniform
opaque pointers, making value-type layout unknown.

| Usage pattern          | Valid? | Error code |
|------------------------|--------|------------|
| `T&`, `T*`, `T?`       | ✓      | —          |
| `T+`, `T#`             | ✓      | —          |
| `T!` with `class T`    | ✓      | —          |
| `T!` with `typename T` | ✗      | `0x01B1`   |
| `T!` with `struct T`   | ✗      | `0x01B1`   |
| `T` (bare)             | ✗      | `0x01B0`   |

The **owner** addresser (`!`) requires a `class` or `interface` constraint
because the uniform synthesised code calls the virtual destructor through the
vtable. With `typename` or `struct`, no vtable exists, so destruction through a
uniform pointer is unsafe.

These rules are enforced at compile time by the `generic_constraint_validator`
pass (runs immediately after model building).

### 26.4 Uniform Synthesis

The compiler synthesises the generic body **once** per declaration, keyed under
`"<generic_synthesis>"` in the internal instantiation cache. All distinct
concrete type arguments (e.g. `Box<Dog>`, `Box<Cat>`) share the same synthesised
LLVM IR and the same mangled symbol — no type-argument suffix is appended.

Instantiation at use sites:
1. Parse the concrete type argument list `<Dog>`.
2. Validate the constraint (kind filter, `!` owner rule).
3. Look up the `"<generic_synthesis>"` entry — always a cache hit from the
   second use onward.
4. Record a usage descriptor (`generic_aggregate_instance`) mapping the single
   synthesised aggregate to the concrete types for source-level type-checking.

### 26.5 Type-Checking at Usage Sites

Despite uniform code synthesis, the compiler still type-checks generic usages
at the call site with the concrete type information:

- `box.get()` on a `Box<Dog>` is statically known to return `Dog*`.
- Passing a `Cat*` to a `Box<Dog>::set()` is a compile-time error.

This is achieved via `tpl_info::generic_usages`, a map from the concrete
instantiation key to a `generic_aggregate_instance` descriptor that holds the
`{param → concrete_type}` mapping.

### 26.6 Name Mangling

Generic aggregates and functions are mangled **without** a type-argument suffix,
unlike template instantiations:

| Generic use  | Mangled form (example)          |
|--------------|---------------------------------|
| `Box`        | `N3BoxE` (no type suffix)       |
| `Box::get`   | `_KFN3Box3getEv`                |
| `identity`   | `_KFN8identityEP` (one ptr arg) |

This ensures a single binary symbol for all concrete uses.

### 26.7 Cross-Module Use (KDI)

Generic aggregates and functions are exported to `.kdi` files as
**signature-only** entries: the source text is not included (there is no
monomorphization at import sites). The KDI entry preserves:

- Parameter names and kind constraints (`typename`, `class`, `interface`).
- Member/parameter/return type signatures using `kdi_template_param_ref`
  placeholders.

Importing modules can instantiate the generic normally; the compiler resolves the
single synthesised aggregate symbol from the importing module's link-time
dependencies.

### 26.8 Invariance

Generic types are **invariant**: `Box<Dog>` is not a subtype of `Box<Animal>`,
even if `Dog` extends `Animal`. This matches the semantics of Java/C# generics in
their default (invariant) mode. Covariance and contravariance are future features.

### 26.9 Differences from Templates

| Feature                    | `template`                         | `generic`                           |
|----------------------------|------------------------------------|-------------------------------------|
| Value parameters           | ✓                                  | ✗                                   |
| Code synthesis             | Once per concrete argument set     | Once total (uniform materialization)|
| Default type parameters    | ✓                                  | ✗                                   |
| Partial specialization     | Future                             | Not applicable                      |
| KDI export                 | Full source text                   | Signature + constraints only        |
| Name mangling              | Type-arg suffix (e.g. `I4DogE`)    | No suffix                           |
| Covariance                 | Not applicable                     | Invariant (future: co/contra)       |
| Direct bare-T usage        | ✓                                  | ✗ (must use an addresser)           |

---

*This summary is complete and self-contained. For detailed examples, edge cases, and error codes, consult the individual specification files referenced in each section, as well as the formal grammar in [`grammar.ebnf`](grammar.ebnf).*
