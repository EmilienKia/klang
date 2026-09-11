# Learn K

This tutorial series introduces K through small, independent programs. Read
the chapters in order the first time; later chapters build on the vocabulary
and conventions established earlier.

K is a statically typed, native-compiled language with C/C++-inspired syntax,
value-oriented aggregates, explicit resource management, and a standard
library that is automatically available to every non-`k` module.

| Chapter | Topic | You will learn |
|---------|-------|----------------|
| [1. Getting started](01-getting-started.md) | The toolchain and first program | How to build Klang, compile a source file, and define `main` |
| [2. Values and control flow](02-values-and-control-flow.md) | Types, variables, and decisions | Primitive values, arrays, conditions, and loops |
| [3. Functions and arrays](03-functions-and-arrays.md) | Reusable code | Parameters, return values, references, defaults, and array arguments |
| [4. Structs and object-oriented programming](04-structs-and-oop.md) | Domain types | Structs, constructors, classes, interfaces, and inheritance |
| [5. Standard library essentials](05-standard-library-essentials.md) | Everyday data and I/O | Strings, output, optionals, and collections |
| [6. Resources and errors](06-resources-and-errors.md) | Safe lifetime management | Owners, pointers, `new`/`delete`, exceptions, and cleanup |
| [7. Modules and libraries](07-modules-and-libraries.md) | Multi-module programs | Public APIs, KDI metadata, imports, and linking |
| [8. Generics and advanced techniques](08-generics-and-advanced-techniques.md) | Compile-time abstraction | Templates, deduction, callbacks, and value parameters |

## Before you begin

The examples assume a working `klangc` compiler and the K standard library.
Build them with the command shown in chapter 1, or install Klang so that
`klangc`, `libk`, and its KDI descriptors are discoverable.

Each complete program starts with a `module` declaration and returns an exit
status from `main`. The base module `k` is imported automatically, so standard
library names can be used without writing `import k;`.

## Further reading

These tutorials explain concepts by example. For complete rules and API
details, consult the [language reference](../spec/language/index.md), the
[standard library reference](../spec/stdlib/index.md), and the
[klangc manual](../man/klangc.md).
