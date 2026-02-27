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
| [Names, Namespaces and Lookup](basic/names.md) | Identifiers, qualified names, lookup rules, namespace visibility |
| [Types](basic/types.md) | Primitive types; the four indirection types (`&` `~` `^` `*`); array and struct types |
| [Module System](basic/modules.md) | Module declarations and imports |
| [Main Function](basic/main.md) | Program entry point |

### Expressions

| Page | Description |
|------|-------------|
| [Expressions](expressions/expressions.md) | Overview, value categories, expression list |
| [Literals](expressions/literals.md) | Integer, floating-point, boolean, character, string, null literals |
| [Identifiers and Name Expressions](expressions/identifiers.md) | Symbol expressions, qualified access |
| [Unary Operators](expressions/unary.md) | `+`, `-`, `~`, `!`, `&` (address-of), `*` (dereference), cast |
| [Binary Operators](expressions/binary.md) | Arithmetic, bitwise, shift, comparison, logical |
| [Assignment Operators](expressions/assignment.md) | `=` and compound assignment |
| [Function Call and Subscript](expressions/call.md) | Invocation, subscript `[]`, member access `.` and `->` |

### Statements

| Page | Description |
|------|-------------|
| [Statements Overview](statements/statements.md) | Block, expression statement, variable declaration |
| [If Statement](statements/if.md) | Conditional branching |
| [While Statement](statements/while.md) | Condition-controlled loop |
| [For Statement](statements/for.md) | Counter-controlled loop |
| [Return Statement](statements/return.md) | Function return |

### Functions

| Page | Description |
|------|-------------|
| [Functions](functions/functions.md) | Declaration, parameters, return type, body |
| [Function Overloading](functions/overloading.md) | Multiple functions with the same name |
| [Static Functions](functions/static.md) | Static member functions |

### Structures

| Page | Description |
|------|-------------|
| [Structures](structs/structs.md) | Declaration, fields, member functions, member visibility |
| [Constructors](structs/constructors.md) | Instance and static constructors |
| [Destructors](structs/destructors.md) | Instance and static destructors |
| [Nested Structures](structs/nested.md) | Static nested and non-static inner structures |

### Grammar Reference

| Page | Description |
|------|-------------|
| [Grammar](grammar.md) | Complete grammar reference with links to descriptions |

---

## Language Overview

K is a statically-typed, compiled programming language with syntax inspired by C/C++ and designed for systems-level programming.

### Key characteristics

- **Statically typed** — all types are known at compile time.
- **Namespace-scoped** — code is organised in namespaces; a source file begins with a `module` declaration that establishes the namespace.
- **Struct-based OOP** — user-defined types are structures with fields, member functions, constructors and destructors.
- **Manual memory control** — pointers (`*`), references (`&`), and arrays are first-class.
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

