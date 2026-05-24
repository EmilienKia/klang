# K Language Reference

Welcome to the **K Language Reference** — the authoritative specification of the K programming language for experienced practitioners.

This reference documents the current state of the language as implemented by the K compiler.  
It is intended as a concise, precise reference, not a tutorial.  
Examples are included where they clarify the normative text.

> **Status:** Working Draft — 2026  
> **Compiler:** K language compiler (klang / klangc)

---

## Table of Contents

### Basic Concepts

| Page | Description |
|------|-------------|
| [Lexical Conventions](basic/lexical.md) | Source encoding, comments, tokens |
| [Keywords](basic/keywords.md) | Reserved words of the language |
| [Names, Namespaces and Lookup](basic/names.md) | Identifiers, qualified names, lookup rules (incl. imported symbols), namespace visibility and library export |
| [Types](basic/types.md) | Primitive types; the five indirection types (`&` `+` `?` `*` `!`); owner (`T!`) — move-only exclusive-ownership type; function reference types (`*(P)`, `T::*(P)`, …); array and struct types; static upcast; dynamic downcast (RTTI); owner upcast/downcast |
| [Module System](basic/modules.md) | Module declarations, imports, transitive dependencies, using imported symbols |
| [Using Directives](basic/using.md) | `using` declarations: namespace injection, specific element injection, aliasing, namespace aliases |
| [Libraries — Export and Import](basic/libraries.md) | Producing libraries, what is exported, importing and using symbols, cross-library inheritance, transitive deps |
| [Main Function](basic/main.md) | Program entry point |

### Expressions

| Page | Description |
|------|-------------|
| [Expressions](expressions/expressions.md) | Overview, value categories, expression list |
| [Literals](expressions/literals.md) | Integer, floating-point, boolean, character, string, null literals |
| [Identifiers and Name Expressions](expressions/identifiers.md) | Symbol expressions, qualified access |
| [Unary Operators](expressions/unary.md) | `+`, `-`, `+`, `!`, `&` (address-of), `*` (dereference), cast |
| [Dynamic Allocation](memory/new-delete.md) | `new T(args)` — allocate and construct, returns `T!`; `delete owner` — destroy and free |
| [Binary Operators](expressions/binary.md) | Arithmetic, bitwise, shift, comparison, logical |
| [Assignment Operators](expressions/assignment.md) | `=` and compound assignment |
| [Function Call and Subscript](expressions/call.md) | Invocation, subscript `[]`, member access `.` and `->`, pointer-to-member operators `.*` and `->*` |
| [Temporary Object Construction](expressions/temporary-construction.md) | `T(args…)` in expression context: stack-allocated anonymous temporary, lifetime, destruction order, sret optimisation |

### Statements

| Page | Description |
|------|-------------|
| [Statements Overview](statements/statements.md) | Block, expression statement, variable declaration |
| [If Statement](statements/if.md) | Conditional branching, condition variable declaration (if-let), link soft-fail |
| [While Statement](statements/while.md) | Condition-controlled loop |
| [For Statement](statements/for.md) | Counter-controlled loop |
| [Return Statement](statements/return.md) | Function return |
| [Break Statement](statements/break.md) | Exit the innermost loop |
| [Continue Statement](statements/continue.md) | Skip to the next iteration of the innermost loop |

### Functions

| Page | Description |
|------|-------------|
| [Functions](functions/functions.md) | Declaration, parameters, return type, body |
| [Function Redirectors](functions/redirectors.md) | Alias a function to another implementation (`-> target;`), chaining, vtable interaction |
| [Function References](functions/function_references.md) | Free and member function reference types (`*(P)`, `T::*(P)`, …), obtaining addresses, calling via `.*` / `->*` |
| [Operator Overloading](functions/operators.md) | Defining custom operators on structs/classes: member and non-member operators, const operators, overload resolution, inheritance, interfaces |
| [Function Overloading](functions/overloading.md) | Multiple functions with the same name |
| [Static Functions](functions/static.md) | Static member functions |
| [Named Return Variables](functions/named-return.md) | Named return variable syntax, guaranteed NRVO, implicit return |

### Structures

| Page | Description |
|------|-------------|
| [Structures](structs/structs.md) | Declaration, fields, member functions, member visibility, struct specifiers |
| [Classes and Virtuality](structs/classes.md) | The `class` keyword, automatic virtual dispatch, vtable rules, virtual base classes, RTTI, dynamic downcast |
| [Interfaces](structs/interfaces.md) | The `interface` keyword, implicit abstraction, virtual dispatch through interfaces |
| [Inheritance](structs/inheritance.md) | Single and multiple inheritance, diamond patterns, cross-type restrictions, static upcast, dynamic downcast |
| [Constructors](structs/constructors.md) | Instance and static constructors |
| [Designated Struct Initializers](structs/designated-init.md) | Named-field initialization with `.member = expr` and `.member(args…)` forms |
| [Destructors](structs/destructors.md) | Instance and static destructors; by-value parameter destruction; expression temporaries lifetime |
| [Nested Structures](structs/nested.md) | Static nested and non-static inner structures |

### Unions

| Page | Description |
|------|-------------|
| [Unions](unions/unions.md) | Discriminated (tagged) unions: alternatives, discriminant, `index()`, Kind enum, destruction, nesting, union inheritance (upcast/downcast), template unions |

### Annotations

| Page | Description |
|------|-------------|
| [Annotations](annotations/annotations.md) | The `annotation` keyword, declaring annotation types, inner enums, applying `@Annotation` to aggregates, meta-annotations (`@Target`, `@Inherited`, `@Retention`), reading annotations at runtime via RTTI |

### Templates

| Page | Description |
|------|-------------|
| [Templates](templates/templates.md) | Template declarations, type and value parameters, kind constraints, base-type constraints, default parameters, explicit instantiation, monomorphization, name mangling |

### Dynamic Memory

| Page | Description |
|------|-------------|
| [Dynamic Allocation — `new` and `delete`](memory/new-delete.md) | `new T(args)` allocation and construction; `delete owner` destruction and deallocation; owner lifetime rules; observer assignment |
| [Uniform Array Initialization](memory/uniform-array-init.md) | `var : T(args)[N]` and `new T(args)[N]` — initialize all array elements with the same constructor arguments |

### Standard Library

| Page | Description |
|------|-------------|
| [Standard Library Overview](../stdlib/index.md) | Base library (module `k`), optional libraries, conventions |
| [Object](../stdlib/object.md) | Root base class for all K classes |
| [String Types](../stdlib/string.md) | `CharHelpers`, `String`, `StringBuilder` |
| [RTTI Types](../stdlib/rtti.md) | `Visibility`, `TypeInfo`, `AggregateType`, `Class`, `Interface`, `AnnotationType`, `Annotation`, meta-annotation types (`Retention`, `Inherited`, `Target`) |
| [Math](../stdlib/math.md) | Optional `k::math` module — `abs`, `min`, `max`, `clamp` |

### Grammar Reference

| Page | Description |
|------|-------------|
| [Grammar](grammar.md) | Complete grammar reference with links to descriptions |

### Quick Reference

| Page | Description |
|------|-------------|
| [Language Summary](summary.md) | A single, self-contained summary of every K language rule. Designed to be read alongside [`grammar.ebnf`](grammar.ebnf) and sufficient on its own — no need to consult the individual specification pages above. |

---

## Language Overview

K is a statically-typed, compiled programming language with syntax inspired by C/C++ and designed for systems-level programming.

### Key characteristics

- **Statically typed** — all types are known at compile time.
- **Namespace-scoped** — code is organised in namespaces; a source file begins with a `module` declaration that establishes the namespace.
- **Struct-based OOP** — user-defined types are structures with fields, member functions, constructors and destructors.
- **Manual memory management** — five indirection types: references (`&`), links (`+`), view (`?`), pointers (`*`), and owners (`!`). Dynamic allocation via `new`/`delete`; owners enforce single-ownership and automatic destruction at scope exit.
- **Function references** — free and member function reference types (`*(P)`, `T::*(P)`) allow storing and invoking function addresses; member function pointers use `.*` and `->*`.
- **Operator overloading** — user-defined types can define custom behaviour for built-in operators (`+`, `-`, `==`, etc.) via member or non-member `operator` functions, with const-correctness, inheritance, and virtual dispatch support.
- **Templates** — compile-time parametric polymorphism via monomorphization. Template functions and aggregates (`struct`, `class`, `interface`) with type and value parameters, kind constraints, base-type constraints, and default parameters.
- **Compiled to native code** — the compiler emits LLVM IR and produces native executables or libraries.

### Hello, World equivalent

```k
module hello;

main() : int {
    return 0;
}
```

### A complete example — Fibonacci

```k
module fibo;

fibo(i: unsigned short) : unsigned int {
    if (i == 0) return 1;
    else if (i == 1) return 1;
    return fibo(i - 1) + fibo(i - 2);
}

main() : int {
    return fibo(8);   // returns 55
}
```

---

*This reference evolves with the language. Pages marked with a note indicate areas that may change in future language versions.*
