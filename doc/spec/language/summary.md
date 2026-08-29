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
19b. [Unions (`union`)](#19b-unions-union)
20. [Annotations](#20-annotations)
21. [Dynamic Allocation (`new` / `delete`)](#21-dynamic-allocation-new--delete)
22. [Uniform Array Initialization](#22-uniform-array-initialization)
23. [FFI (Foreign Function Interface)](#23-ffi-foreign-function-interface)
24. [`main` Function](#24-main-function)
25. [Templates](#25-templates)

---

## 1. Lexical Conventions

> Details: [lexical.md](basic/lexical.md)

- **Source encoding:** UTF-8. Identifiers are ASCII-only; extended characters are only meaningful inside character/string literals and comments (invalid UTF-8 in a comment is a warning).
- **Whitespace:** space, tab, form-feed, line terminators (`LF`, `CR`, `CR LF`). Separate tokens, otherwise ignored.
- **Comments:**
  - End-of-line: `// …` until end of line.
  - Block: `/* … */` — non-nestable.
- **Tokens:** identifiers, keywords, literals, punctuators, operators.
- **Identifiers:** sequence of letters (`A-Z`, `a-z`, `_`) and digits (`0-9`), starting with a letter. Identifiers composed solely of `_` are reserved. Must not be a keyword.
- **Punctuators:** `(` `)` `{` `}` `[` `]` `;` `,` `::` `...` `@`
- **Operators:** `.` `->` `.*` `->*` `?` `:` `!` `~` `#` `=` `+` `-` `*` `/` `&` `|` `?` `%` `<<` `>>` `+=` `-=` `*=` `/=` `&=` `|=` `^=` `%=` `<<=` `>>=` `==` `!=` `<` `>` `<=` `>=` `<=>` `&&` `||` `++` `--` `**`

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
if       else     while    for      break    continue
new      delete   default  enum     union
operator
template typename generic
throw    try      catch    throws   finally
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
| `byte`            | 8    | yes    |
| `unsigned byte`   | 8    | no     |
| `char`            | 32   | no (Unicode code point) |
| `short`           | 16   | yes    |
| `unsigned short`  | 16   | no     |
| `int`             | 32   | signed |
| `unsigned int`    | 32   | no     |
| `long`            | 64   | yes    |
| `unsigned long`   | 64   | no     |

`byte` is the signed 8-bit integer; `unsigned byte` is its unsigned counterpart.
`char` is a distinct 32-bit **unsigned** type holding a single Unicode scalar
value (UTF-32, native endianness); it has no signed/unsigned variant and
`unsigned char` is not a valid type. `char` converts implicitly to/from
`unsigned int` (same width) and to/from `int` with the usual signed↔unsigned
rules. `unsigned` is a modifier for `byte`, `short`, `int`, `long`.

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
- Subscript: `arr[i]` — runtime bounds check (unsigned comparison). Out-of-bounds throws `IndexOutOfBoundsError`.
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
`byte` → `short` → `int` → `long`; `float` → `double`; signed ↔ unsigned (same width). `char` ↔ `unsigned int` (same 32-bit width); `char` → `long`.

#### Narrowing (possible loss)
`long` → `int` → `short` → `byte`; `double` → `float`. Accepted but with truncation risk.

#### Static Upcast (aggregate types)
`Derived*` → `Base*` implicitly. Compile-time GEP to address the Base sub-object.

#### Dynamic Downcast (class/interface)
`Base*` → `Derived*` with runtime RTTI. On mismatch: null (for `T*`, `T?`) or throws `NullCastError` (for `T+`, `T&`).

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
Suffixes: (none)=`int`, `u`=`unsigned int`, `s`=`short`, `l`=`long`, `ul`/`lu`=`unsigned long`, `b`=`byte`, `ub`=`unsigned byte`.

#### Floating-Point
Format: `digits.digits[exponent][suffix]`. Suffix: (none)=`float`, `d`=`double`.

#### Booleans
`true`, `false`.

#### Characters
`'c'` — a single Unicode scalar value; type `char` (32-bit, UTF-32). Encoding
prefixes force the element type: `u8'c'`→`unsigned byte`, `u'c'`/`u16'c'`→`unsigned short`,
`U'c'`/`u32'c'`→`char` (the value must fit in one code unit of the encoding).
Escape sequences: `\n`, `\r`, `\t`, `\a`, `\b`, `\f`, `\v`, `\\`, `\'`, `\"`, `\?`, `\0`, `\NNN` (octal), `\xNN` (hex), `\uNNNN`, `\UNNNNNNNN`.

#### Strings
`"…"` — internal layout `{ i32 size, [N x elem] data }` where N = code-unit count + 1 (implicit null terminator) and `elem` is the encoding's code-unit type.
Encoding prefixes force the element type: `u8"…"`→`unsigned byte[]` (UTF-8), `u"…"`/`u16"…"`→`unsigned short[]` (UTF-16), `U"…"`/`u32"…"`→`char[]` (UTF-32).
An **unprefixed** literal defaults to `char[]` (UTF-32). When it is passed as a function/constructor argument whose parameter expects `unsigned byte[]` or `unsigned short[]` (UTF-8 / UTF-16), the literal adopts that element type instead (the compiler re-types a clone, so overload resolution can still score it against several candidates). Deduplication per compilation unit. Implicit conversion `const T[N]&` → `const T[]` (zero-cost).

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
| 7     | `<=>` | Left→Right |
| 8     | `<` `>` `<=` `>=` | Left→Right |
| 9     | `==` `!=` | Left→Right |
| 10    | `&` (bitwise AND) | Left→Right |
| 11    | `?` (bitwise XOR) | Left→Right |
| 12    | `\|` (bitwise OR) | Left→Right |
| 13    | `&&` | Left→Right |
| 14    | `\|\|` | Left→Right |
| 15    | `?:` (ternary) | Right→Left |
| 16    | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | Right→Left |

### 8.3 Unary Operators

> Details: [unary.md](expressions/unary.md)

- **`+x`**: identity (arithmetic type required).
- **`-x`**: arithmetic negation.
- **`!x`**: logical NOT → `bool`. Operand: `bool`, numeric, or indirection (→ nullity test).
- **`~x`**: bitwise NOT (integer required).
- **`&x`**: address-of — produces `T+` (link). If `x` is `const`, produces `const T+`.
- **`*x`**: dereference — produces `T&`. Operand `T+` (no null-check), `T?` or `T*` (null-check; throws `NullDereferenceError` if null).
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
- **Three-way comparison**: `<=>` → signed integer or floating-point primitive (negative/zero/positive for less/equal/greater). Also overloadable on aggregates; used as a comparison-operator fallback source (see [operators.md](functions/operators.md#9-comparison-operator-fallback-synthesis)).
- **Logical**: `&&`, `||` — short-circuit evaluation (and-then / or-else).
- **Ternary**: `cond ? then : else` — condition converted to `bool`, only the
  selected branch is evaluated, and branch types must unify via implicit
  conversion to a common result type.

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
- **Unified Call Syntax (UCS)**: `obj.func(args...)` / `obj.func<TArgs...>(args...)` can invoke matching static methods on the receiver's aggregate/base hierarchy or matching free functions taking `obj` as their first parameter (`func(obj, args...)`), with aggregate static members prioritized over free functions, supporting both non-template and template functions with argument deduction.
- **Pointer access `→`**: equivalent to `(*p).member`, with null-check.
- **Member function reference call**: `(obj.*mfp)(args)` and `(ptr->*mfp)(args)` — parentheses required.
- **Template-qualified scope call**: expression calls support `Type<T>::func(args...)`,
  `ns::Type<T>::func(args...)`, and `::ns::Type<T>::func(args...)`.
  It applies to static members and explicit non-virtual member calls (`Type<T>::method(obj, ...)`).
  The call result can be chained with further member access — e.g. a static factory
  `Optional<byte>::empty().getOr(x)` — its (possibly template) return type is instantiated.

### 8.7 Temporary Construction

> Details: [temporary-construction.md](expressions/temporary-construction.md)

`T(args…)` in an expression context creates an anonymous temporary object allocated on the stack.
- Result: `T&`.
- Lifetime: until the end of the full statement (`;`).
- Destruction in reverse order of construction.
- Usable as a function argument, for method chaining, or in a `return`.
- Constructors `→ delete` produce an error if selected.
- Static constructors do not participate in resolution.
- Array temporary form is supported: `T[]{init...}` (including in `return` expressions).
  - Result type: `T[N]&` (`N` inferred from initializer size).
  - Element initialization semantics are identical to local array brace-init.

---

## 9. Statements

> Details: [statements.md](statements/statements.md)

### 9.1 Block

`{ statements… }` — introduces a new scope. Variables destroyed on block exit.

### 9.2 Expression Statement

`expr ;` — used for side effects. `;` alone = empty statement.
- If produces an unassigned `T!`: warning `0x5010`, object destroyed immediately.
- Struct temporaries and temporary arrays are destroyed at end of statement in reverse order.

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

**Condition-variable declaration (`if-let`):** A local variable may be declared as the condition.
Its value, cast to `bool` (or trailing test expression), determines the branch. The variable is
scoped to the `if` (destroyed at end of then branch, or cleaned up before else). Pattern-like
soft-fail applies to all condition-variable forms (classic `if-let`, multi-variable `if(var1; var2; ...)`,
and forms with a trailing test expression `if(var1; ...; test)`):

- for addressor initialization failures (for example null when binding a non-null addressor or null pointer);
- for explicit union alternative accesses used as initializers when the union does not currently
  hold the requested alternative.

On such a soft-fail, the condition is considered `false`, subsequent variable initializers and trailing
test expressions are skipped, and control goes to `else` (or continues after the `if`).

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
sum(values... : int) : int { /* values is int[] */ }
format(fmt: int, args... : int) : int { /* ... */ }
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
- Template varargs are supported: `template<typename T> first(args... : T) : T&`.
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

### 10.6 Callables and Lambdas

> Details: [callables.md](functions/callables.md) · [lambdas.md](functions/lambdas.md)

Callables are fat pointers `{fn_ptr, ctx_ptr}` supporting uniform invocation across free functions, methods, functors, and lambdas:

| Type                     | Description                                |
|--------------------------|--------------------------------------------|
| `*(Params):Ret`          | Pointer callable (nullable, rebindable)    |
| `?(Params):Ret`          | View callable (nullable, immutable)        |
| `+(Params):Ret`          | Link callable (non-null, rebindable)       |
| `&(Params):Ret`          | Reference callable (non-null, immutable)   |
| `!(Params):Ret`          | Owner callable (exclusive ownership, move-only, heap closure) |
| `T::*(Params):Ret` etc.  | Unbound member method reference            |

- Free function: `fp : *(int):int = add_one;` (context is null).
- Bound method: `fp : &(int):int = obj.add;` (context is object address).
- Owned member bind: `fp : !(int):int = obj.add;` (only global/static/`this` receivers).
- Lambda: `[captures](params):Ret { body }`.
  - Borrowed lambda (`&`): stack closure, local lifetime.
  - Owned lambda (`!`): heap closure, move-only, destroyed on scope exit (local captures by value only).
- Invocations: `fp(args)`.
- Return type is required (omitted = void).
- Move-only semantics apply to `!(Params):Ret`. Borrowed callables cannot be converted to owned callables.

### 10.7 Operator Overloading

> Details: [operators.md](functions/operators.md)

Declaration with `operator` + operator symbol.

**Overloadable operators:**
- Binary: `+` `-` `*` `/` `%` `&` `|` `?` `<<` `>>` `&&` `||` `==` `!=` `<` `>` `<=` `>=` `<=>` `=` `+=` `-=` `*=` `/=` `%=` `&=` `|=` `^=` `<<=` `>>=`
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
- `operator <=>` (three-way comparison): returns a signed integer or floating-point primitive (negative/zero/positive). Used as a fallback synthesis source for `==`/`!=`/`<`/`>`/`<=`/`>=` when the exact comparison operator isn't declared on the aggregate (see [operators.md §9](functions/operators.md#9-comparison-operator-fallback-synthesis)).
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

`override` before the method name: asserts that the method overrides an inherited virtual.  
Override matching uses method name + constness + parameter types; parameter names are ignored.  
If no actual override = error. Omitting `override` on a valid override = warning.

### 12.8 Abstract Classes

`abstract class C { … }`: cannot be instantiated directly. `abstract` method = no body, must be overridden by concrete subclasses.

### 12.9 RTTI and Dynamic Downcast

Classes have an RTTI slot in the vtable (slot 0). Downcast via explicit cast `(Derived*) base_ptr` uses runtime RTTI. Nullable targets → null on mismatch. Non-null targets → throws `NullCastError`.

---

## 13. Interfaces (`interface`)

> Details: [interfaces.md](structs/interfaces.md)

```
{ Specifier } 'interface' Identifier [ ':' BaseClause ] '{' { Declaration } '}'
```

- All methods are implicitly abstract (no body), unless declared `default` (see §13.1).
- No fields, constructors, destructors.
- All methods are implicitly `public`.
- Can only inherit from other interfaces.
- Implemented by a `class` that provides a body for each method.
- Virtual dispatch via vtable.
- `abstract` accepted but redundant (warning).
- `final` prevents inheritance.
- Multiple implementation (a class can implement several interfaces).

### 13.1 Default Methods (`default`)

An interface member function declared with the `default` prefix specifier and a
body is a **default method**: a concrete, virtual method (mangled and emitted
normally). A class that implements the interface but does **not** override it
inherits the default implementation through its vtable slot, and therefore does
not need to be `abstract`.

```
interface Greeter {
    name() : string;                                 // abstract contract
    default greet() : string {                       // default implementation
        return "hello, " + this.name();              // may call abstract/default methods
    }
}
class French : public Greeter {
    name() override : string { return "monde"; }     // greet() is inherited
}
```

- Only valid on interface member functions, and a body is required.
- Incompatible with `static`, `final`, `abstract`, `private`, constructors,
  destructors and `-> default/delete/redirect`.
- A default body may call other (abstract or default) methods of the interface;
  those dispatch dynamically to the concrete implementation.
- **Template interfaces**: a default method of a `template<...>` interface is not
  synthesised at the definition site; it is synthesised (with `linkonce_odr`
  linkage) for each concrete instantiation, exactly like every other template
  member method.
- **Cross-module**: for a non-template interface the default method symbol lives
  in the interface's library; an implementing class in another module references
  it through the imported vtable slot and links against that library.

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

### 15.6 Class Constructor Virtual-Dispatch Setup

- For `class`, the compiler initializes the class vptr before the user constructor
  body executes.
- A post-body fixup then restores the most-derived vptr/vbptr state after base
  constructor execution.
- This makes constructor-body virtual calls safe.
- `struct` has no vptr and is not affected.

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

## 17bis. Friend Declarations

A `friend` declaration inside an aggregate body grants the named entity access
to **both protected and private** members of the declaring aggregate.

```k
struct Secret {
    private:
    _x : int;
    friend Buddy;          // non-template friend
    friend Getter<T>;      // template friend (same type param)
    friend Getter<int>;    // template friend (concrete type)
    friend struct Filtered<T>;   // with type filter
    friend free_helper;    // free function friend (all instantiations)
    friend free_helper<T>; // free function friend (matching type param only)
}
```

### Syntax

```
FriendDecl = 'friend' , [ FriendFilter ] , QualifiedIdentifier , [ TemplateArgList ] , ';'
FriendFilter = 'struct' | 'interface' | 'class'
```

The optional `TemplateArgList` constrains which instantiation of a template is
accepted as a friend:

| Declaration form | Meaning |
|---|---|
| `friend Foo;` | All types and all instantiations of `Foo` are friends |
| `friend Foo<T>;` | Only the instantiation of `Foo` whose arg equals the current aggregate's `T` |
| `friend Foo<int>;` | Only the concrete instantiation `Foo<int>` |
| `friend free_func;` | All instantiations of the free function template are friends |
| `friend free_func<T>;` | Only `free_func` instantiated with the same type as `T` |

### Rules

- Friendship is **not inherited**: a subclass of a friend is not automatically a friend.
- Friendship does **not propagate**: a friend of `X` cannot access members of `X`'s
  base classes through that friendship (it can access only what belongs to `X`).
- Friendship applies to both **private and protected** members.
- A `friend struct Foo<T>;` filter verifies that the named entity is indeed a `struct`.
- The `TemplateArgList` in a friend declaration inside a template aggregate may
  reference the enclosing template's type parameters (e.g. `friend Bar<T>;`).
  When the enclosing aggregate is instantiated, `T` is substituted with the concrete
  type, so `Box<int>` carries `friend Bar<int>` and `Box<double>` carries `friend Bar<double>`.

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
{ Specifier } 'enum' Identifier [ ':' TypeSpec ] '{' { EnumEntry } '}'
```

Like every block declaration, an enum has no trailing `;`. A stray `;` after
the closing brace is tolerated as an empty declaration (reported with a
warning) by the enclosing declaration list.

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

## 19b. Unions (`union`)

### 19b.1 Declaration

```
{ Specifier } 'union' Identifier [ ':' QualifiedIdentifier ] '{' { UnionMemberDecl } '}'
UnionMemberDecl = [ 'const' ] Identifier ':' TypeSpec ';'
```

### 19b.2 Semantics

A discriminated (tagged) union holds one active alternative at a time. A hidden
`uint32` discriminant tracks which alternative is active.

| Operation | Behaviour |
|-----------|-----------|
| Default construction | Constructs alternative 0; storage is zero-initialized. |
| Explicit member assignment (`u.alt = x`) | Stores value, updates discriminant. |
| Member access (`u.alt`) | Runtime-checks the discriminant; mismatch traps by default. In classic `if-let`, mismatch soft-fails to the false branch. |
| Destruction | Switch on discriminant; calls destructor of active alternative if non-trivial. |

### 19b.3 Memory Layout

```
{ uint32 discriminant, [max_size x i8] storage }
```

Storage is sized to the largest alternative (including all inherited alternatives
when the union has a parent).

### 19b.4 Inheritance

A union may inherit from exactly one parent union:

```
union Derived : Base { new_alt: SomeType; }
```

Rules:
- A union may have at most one parent union (single inheritance only).
- The parent must be a union type (not struct/class/enum).
- All parent alternatives are accessible on the derived union with their original
  discriminant values.
- New alternatives in the derived union are assigned discriminant values starting
  at `parent.total_alternative_count()`.
- The synthesised `Kind` enum of the derived union covers the full inheritance chain.
- Storage is sized to the largest alternative across the full chain.
- **Downcast (parent → derived):** always valid — the discriminant value is
  within the derived union's range by definition.
- **Upcast (derived → parent):** requires a runtime discriminant check. If the
  active alternative belongs to the derived-only range, a fatal trap is emitted.
- Template unions with an inheritance clause are rejected (not yet supported).
- Circular union inheritance is detected and rejected.

### 19b.5 Restrictions

- No functions, constructors, or nested types inside unions.
- Drain addresser (`#`) is forbidden on alternative types.
- Unions can be passed by value or by reference to functions.

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

### 25.3 Template Instantiation & Argument Deduction

Templates can be instantiated explicitly with concrete arguments in angle brackets:

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

#### Function Template Argument Deduction
Function templates support automated deduction of type and value arguments from call-site
arguments and contextual target types:
- **Invocation Arguments**: `a : int = identity(42);` (deduces `T=int`). Supports composites (`Vector<T>`), indirections (`T*`, `T&`, `T!`), callables (`*(T):R`), sized arrays (`T[N]`), and parameter packs (`Ts...`).
- **Contextual Return-Type Deduction**: When a template parameter only appears in the return type (or is partially deduced), Klang infers it from the expected target type in variable declarations (`x: int = make()`), return statements (`return make()`), assignments (`x = make()`), casts (`(int) make()`), ternary branches, array initializers, designated struct initializers, and function arguments.

#### Class Template Argument Deduction (CTAD)
Constructors of template classes and structs support automated argument deduction:
- **Temporary construction**: `Pair(1, 2)` deduces `Pair<int, int>`.
- **Dynamic allocation**: `new Pair(1, 2)` deduces `Pair<int, int>!` (returns owner).
- **Variable declarations**: `p : Pair(1, 2);` or `p : Pair = Pair(1, 2);` deduces `p : Pair<int, int>`.
- **Implicit copy guide**: `p2 : Pair = Pair(p1);` preserves existing specialization `Pair<int, int>`.
- **Uniform array init**: `arr : Pair(1, 2)[5];` deduces `arr : Pair<int, int>[5]`.

Trailing arguments with defaults may be omitted.

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
- In expression scope calls, template arguments are currently supported on the
  leading qualifier only (`Type<T>::member(...)`). Mid-chain forms such as
  `ns::Type<T>::member(...)` are not yet supported.

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

## 27. Exception Handling

K provides structured exception handling via `throw`, `try-catch`, and `throws` clauses.
Throwable types follow a class hierarchy rooted at `::k::Throwable` (from the standard library),
with a checked/unchecked distinction:

- **Checked exceptions** (`::k::Exception` subtypes): must be declared in `throws` clauses.
- **Unchecked fatal errors** (`::k::FatalError` subtypes): propagate freely without declaration.

### 27.1 Throwable Type Constraint

**Only classes derived from `::k::Throwable` (or `Throwable` itself) may be thrown.**

- Throwing a struct or class that does not derive from `::k::Throwable` → compile-time error `0x01C0`.
- Throwing a non-class type (primitive, array, etc.) → compile-time error `0x01C0`.
- This constraint is enforced whenever the stdlib is available (i.e. for all modules except `k` itself).

User-defined exception types should inherit from `Exception` (checked) or `FatalError` (unchecked):

```k
class MyError : public Exception {
    public:
    MyError() : Exception(100) { }
}
```

### 27.2 Throw Statement

```
ThrowStatement:
    'throw' Expression ';'
    'throw' ';'
```

The first form throws a new exception. The expression must evaluate to a class type derived from `::k::Throwable`.
At runtime, the compiler:
1. Allocates exception storage via `__cxa_allocate_exception`.
2. Copies the value into the exception storage.
3. Calls `__cxa_throw` to initiate stack unwinding.

The second form (`throw;`) is a **rethrow** — it re-throws the exception currently being handled.
It is only valid inside a `catch` block (compile-time error `0x01C9` otherwise).
At runtime, it calls `__cxa_rethrow()` without allocating or copying.
The rethrown exception type is subject to the same contract rules as a normal throw.

Stack unwinding destroys all local objects with destructors (in reverse declaration
order) in each stack frame between the throw point and the matching catch handler.

### 27.3 Try-Catch Statement

```
TryCatchStatement:
    'try' BlockStatement { CatchClause } [ FinallyClause ]

CatchClause:
    'catch' '(' ParameterDecl ')' BlockStatement

FinallyClause:
    'finally' BlockStatement
```

At least one catch clause or a finally clause must be present.

Example:

```k
try {
    riskyOperation();
} catch (e: IOException&) {
    handleIO(e);
} catch (e: Throwable&) {
    handleGeneric(e);
} finally {
    cleanup();
}
```

Rules:
- Multiple catch clauses are evaluated in order; the **first matching type wins**.
- Match is by exact type or base class (if the thrown type derives from the caught type).
- Unmatched exceptions propagate to the next enclosing try-catch or out of the function.
- The catch parameter receives a **reference** (`T&`) to the exception object.
- Catch parameter types must derive from `::k::Throwable` (error `0x01C1` otherwise).
- All function calls within a try block are compiled as LLVM `invoke` instructions
  (instead of `call`) to enable unwinding through the landing pad.
- The `finally` block, if present, **always executes** regardless of whether:
  - The try body completes normally (no exception thrown).
  - An exception is thrown and caught by a matching catch clause.
  - An exception is thrown but not caught (propagated to outer handler).
  - A `return`, `break`, or `continue` exits the try or catch body early.
- The `finally` block does **not** suppress exceptions — after the finally block
  executes, uncaught exceptions continue propagating.
- `try { } finally { }` without any catch clause is valid — the finally block
  runs on both normal completion and exception propagation.
- **Early exit semantics**: when `return`, `break`, or `continue` appears inside
  a try or catch body that has an associated finally block:
  - All enclosing finally blocks (innermost to outermost) are emitted inline
    before the control flow exit takes effect.
  - If exiting from a catch body, `__cxa_end_catch()` is called before the
    finally block executes.
  - For `break`/`continue`, only finally blocks between the statement and the
    enclosing loop boundary are emitted.
  - For nested try-finally, all finally blocks are emitted in order (innermost
    first).

### 27.4 Throws Clause

```
ThrowsClause:
    'throws' '(' [ TypeSpec { ',' TypeSpec } ] ')'
```

The `throws` clause appears after the return type and before the function body:

```k
myFunc(a: int) : int throws(IOException, ParseException) {
    // ...
}
```

Rules:
- All types in the `throws` clause must derive from `::k::Throwable` (error `0x01C4` otherwise).
- A type in the `throws` clause that cannot be resolved → error `0x01C3`.
- The throws clause is part of the function's public interface and exported in `.kdi` files.

### 27.5 Exception Contract Rules (Checked Exceptions)

When a function declares a `throws` clause, the compiler enforces static contract verification
**for checked exceptions only** (types derived from `::k::Exception`):

1. **Throw check:** Any `throw` statement in the function body that throws an
   `Exception`-derived type must throw a type that is either:
   - Declared in the function's `throws` clause, **or**
   - Caught by an enclosing `try-catch` within the same function.
   
   Violation → error `0x01C5`.

2. **Call check:** Any call to a function that itself has a `throws` clause must
   have all its declared exception types either:
   - Caught by an enclosing `try-catch`, **or**
   - Declared in the caller's own `throws` clause (propagation).
   
   Violation → error `0x01C6`.

**Unchecked types** (derived from `::k::FatalError`) are exempt from contract enforcement —
they may be thrown from any function without declaration. This allows runtime errors like
`OutOfMemory` to propagate without polluting every function signature.

Functions **without** a `throws` clause are **not checked** — they may throw freely
and are not required to handle exceptions from called functions. This allows gradual
adoption and FFI interop.

### 27.6 Stack Unwinding and Cleanup

When an exception propagates through a stack frame:
- All local variables with destructors are destroyed in reverse declaration order.
- Owner variables (`T!`) are auto-deleted.
- The cleanup is implemented via LLVM landing pads with cleanup clauses.
- Nested try-catch blocks within the same function use direct CFG branching
  (resume to outer handler if innerhandlers don't match).

### 27.7 Implementation Details

- Uses the **Itanium C++ ABI** unwinding mechanism (`__cxa_allocate_exception`,
  `__cxa_throw`, `__cxa_begin_catch`, `__cxa_end_catch`).
- Type matching uses pointer equality on module-level typeinfo globals
  (`_KTI<mangled_name>`). Before throwing, the compiler stores the typeinfo pointer
  in a per-module `_k_thrown_typeinfo` thread-local global.
- Each catch clause in a landing pad compares the caught typeinfo against the
  expected typeinfo for its declared type.
- Stack unwinding properly destroys local objects via cleanup landing pads.
- Nested try-catch uses direct CFG branching (no re-throw) for intra-function propagation.

### 27.8 Diagnostic Codes

| Code | Identifier | Description |
|------|-----------|-------------|
| `0x01C0` | `ERR_THROW_NOT_EXCEPTION_TYPE` | Thrown type does not derive from `::k::Throwable` |
| `0x01C1` | `ERR_CATCH_NOT_EXCEPTION_TYPE` | Catch clause type does not derive from `::k::Throwable` |
| `0x01C2` | `ERR_CATCH_MUST_BE_REFERENCE` | Catch clause must use reference addresser (`&`) |
| `0x01C3` | `ERR_THROWS_TYPE_NOT_FOUND` | Type in throws clause cannot be resolved |
| `0x01C4` | `ERR_THROWS_NOT_EXCEPTION_TYPE` | Type in throws clause does not derive from `::k::Throwable` |
| `0x01C5` | `ERR_THROW_NOT_IN_THROWS_SPEC` | Throw of undeclared checked exception in function with throws clause |
| `0x01C6` | `ERR_CALL_UNHANDLED_EXCEPTION` | Call to throwing function without handling/declaring its checked exceptions |

### 27.9 Known Limitations

- No `finally` interaction with `return`/`break`/`continue` inside try/catch bodies
  (the finally block may be skipped if a return statement exits the function from
  within the try or catch body — Phase 2 improvement).
- No exception specification on constructors/destructors.

---

*This summary is complete and self-contained. For detailed examples, edge cases, and error codes, consult the individual specification files referenced in each section, as well as the formal grammar in [`grammar.ebnf`](grammar.ebnf).*
