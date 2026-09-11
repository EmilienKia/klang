# K Tutorials and Topic Guides

This directory contains learning material and practical guides for the K
programming language and standard library.

---

## 1. Step-by-Step Tutorials (`learn/`)

The [Learn K](learn/README.md) tutorial series introduces K from scratch
through small, independent programs:

| Chapter | Topic | Link |
|---------|-------|------|
| 1 | The toolchain and first program | [01-getting-started.md](learn/01-getting-started.md) |
| 2 | Types, variables, and decisions | [02-values-and-control-flow.md](learn/02-values-and-control-flow.md) |
| 3 | Reusable code and functions | [03-functions-and-arrays.md](learn/03-functions-and-arrays.md) |
| 4 | Domain types, structs, and OOP | [04-structs-and-oop.md](learn/04-structs-and-oop.md) |
| 5 | Everyday data, strings, collections, and I/O | [05-standard-library-essentials.md](learn/05-standard-library-essentials.md) |
| 6 | Memory management, owners, and exceptions | [06-resources-and-errors.md](learn/06-resources-and-errors.md) |
| 7 | Multi-module programs, KDI, and libraries | [07-modules-and-libraries.md](learn/07-modules-and-libraries.md) |
| 8 | Generics, templates, and deduction | [08-generics-and-advanced-techniques.md](learn/08-generics-and-advanced-techniques.md) |

---

## 2. In-Depth Subsystem Guides (`topics/`)

Detailed architectural overviews, class classifications, interaction models,
and practical examples for major standard library subsystems:

| Topic | Description | Guide |
|-------|-------------|-------|
| **Date and Time** (`k::time`) | Complete guide to K's temporal architecture: timeline coordinates, civil dates, durations vs periods, monotonic clocks, time zones, DST resolutions, alternative chronologies, scales, and formatting. | [topics/time.md](topics/time.md) |

---

## Further reading

For formal specifications and reference manuals:
- [Language Specification](../spec/language/index.md)
- [Standard Library Reference](../spec/stdlib/index.md)
- [Compiler Manual](../man/klangc.md)

