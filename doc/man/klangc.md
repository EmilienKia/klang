# KLANGC(1) — K Language Compiler — User Commands

## NAME

**klangc** — K language compiler

## SYNOPSIS

```
klangc [options] input-file...
```

## DESCRIPTION

**klangc** is the command-line driver for the K language compiler. It reads
one or more K source files, performs lexical analysis, parsing, semantic
resolution, LLVM IR generation and optional optimisation, then emits either a
native object file or a linked executable.

The compiler internally uses LLVM for code generation and delegates linking to
**clang(1)**.

> **Note:** The current version supports only **one input file** at a time.
> If multiple files are provided, only the first one is processed.

---

## OPTIONS

Options are grouped by category below, following the convention established by
GCC/Clang documentation.

### Global Options

**`-h`**, **`--help`**  
Display a short help message and exit with status `1`.

**`-v`**, **`--version`**  
Display the compiler name, version number and the effective target triple, then
exit with status `2`.

**`-c`**, **`--compile`**  
Compile source file(s) to a native object file (`.o`) but do not invoke the
linker. The output file name defaults to the input file name with its extension
replaced by `.o` (see `-o`).

**`-o` _file_**, **`--output=`_file_**  
Place the primary output (object file or executable) into _file_.  
When `-c` is not specified and no `-o` is given, the output name is derived
from the input file name by stripping its extension.

**`input-file`**  
Path to the K source file to compile. Positional; may also be specified
explicitly with `--input-file=`_file_. Only one file is currently processed.

---

### Target Options

**`--target=`_triple_**  
Generate code for the specified target triple (e.g. `x86_64-pc-linux-gnu`,
`aarch64-unknown-linux-gnu`). When omitted, the host default triple is used.

**`--print-target-triple`**  
Print the normalised target triple and exit with status `4`.

**`--print-effective-triple`**  
Equivalent to `--print-target-triple`; print the effective target triple and
exit with status `4`.

**`--print-targets`**  
Print all targets registered in the LLVM build and exit with status `3`.

---

### LLVM IR Export Options

The following options control the export of LLVM IR text at two distinct
stages of the compilation pipeline:

* **raw IR** — the IR produced immediately after code generation, before any
  optimisation pass has been applied.
* **optimised IR** — the IR produced after all optimisation passes have been
  applied.

When a file path is not provided (or `-` is given), the IR is written to
**stdout**.

If no explicit output file name is provided, **klangc** automatically derives
a file name from the primary output path by replacing (or appending) a
stage-specific suffix:

| Stage | Automatic suffix |
|-------|-----------------|
| Raw (generated) IR | `.raw.ll` |
| Optimised IR | `.opt.ll` |

For example, compiling `hello.k` to `hello` with `--emit-raw-ir` produces
`hello.raw.ll` alongside the executable.

---

**`--emit-raw-ir`**  
Export the LLVM IR text produced after code generation and before any
optimisation. When no destination file is specified via `--raw-ir-file`, the
output goes to **stdout**.

**`--raw-ir-file[=`_file_`]`**  
Write the raw (pre-optimisation) LLVM IR to _file_. Implies `--emit-raw-ir`.  
Omitting the value, or passing `-`, sends the output to **stdout**.  
If _file_ is not given and the primary output is known, the name is derived
automatically (e.g. `output.raw.ll`).

**`--emit-opt-ir`**  
Export the LLVM IR text produced after optimisation. When no destination file
is specified via `--opt-ir-file`, the output goes to **stdout**.

**`--opt-ir-file[=`_file_`]`**  
Write the optimised LLVM IR to _file_. Implies `--emit-opt-ir`.  
Omitting the value, or passing `-`, sends the output to **stdout**.  
If _file_ is not given and the primary output is known, the name is derived
automatically (e.g. `output.opt.ll`).

---

## EXIT STATUS

| Code | Meaning |
|------|---------|
| `0`  | Success |
| `1`  | Help displayed / unrecognised option |
| `2`  | Version information displayed |
| `3`  | Target list displayed |
| `4`  | Target triple displayed |
| `-1` | Compilation or code-generation error |

---

## DIAGNOSTICS

Diagnostic messages are printed to **stderr** in the following format:

```
<file>:<line>:<col>: <severity> <code> : <message>
  <line number> | <source line>
                | ^~~~
```

Severity levels are `Info`, `Warning`, `Error` and `Fatal`. Error codes are
displayed as zero-padded five-digit hexadecimal values.

---

## SOURCE FILE FORMAT

K source files are plain text files, conventionally given the `.k` extension.
The language supports:

* **Primitive types:** `int`, `bool`, `float`, `char`, … (and sized variants
  such as `int8`, `uint64`, `float32`, etc.)
* **Aggregate types:** `struct` and `class` definitions with member fields and
  methods.
* **Global variables** and **global functions** (including a `main` entry
  point).
* **Namespaces** for symbol scoping.
* **Arithmetic, relational and assignment expressions**, including compound
  assignments (`+=`, `-=`, …).
* **Control-flow statements:** `if`/`else`, `while`, `return`.

---

## EXAMPLES

**Compile and link a source file into an executable:**

```sh
klangc hello.k
```

Produces the executable `hello` (same name as input, without extension).

---

**Compile to an object file:**

```sh
klangc -c hello.k -o hello.o
```

---

**Compile for a specific target:**

```sh
klangc --target=aarch64-unknown-linux-gnu -c hello.k -o hello.aarch64.o
```

---

**Dump raw (pre-optimisation) LLVM IR to stdout:**

```sh
klangc --emit-raw-ir hello.k
```

---

**Save both raw and optimised IR to automatically-named files:**

```sh
klangc --emit-raw-ir --emit-opt-ir -o hello hello.k
# Produces: hello  hello.raw.ll  hello.opt.ll
```

---

**Save optimised IR to an explicit file while compiling to object:**

```sh
klangc -c --opt-ir-file=hello_opt.ll hello.k -o hello.o
```

---

**Print the host target triple:**

```sh
klangc --print-effective-triple
```

---

## FILES

| Path | Description |
|------|-------------|
| `*.k` | K language source file |
| `*.o` | Native object file produced by `-c` |
| `*.raw.ll` | Auto-generated raw LLVM IR text file |
| `*.opt.ll` | Auto-generated optimised LLVM IR text file |

---

## NOTES

* Multiple source files on the command line are currently **not supported**.
  Only the first file is compiled; a warning is emitted for the rest.
* Linking is performed by invoking **clang(1)** with `-pie`. The `clang`
  binary must therefore be present in `PATH`.
* The optimisation pipeline uses the LLVM legacy pass manager with the
  following passes: instruction combining, expression reassociation, GVN
  (global value numbering), dead-code elimination and CFG simplification.
* JIT execution (used internally in the `klang` REPL) is not exposed through
  the `klangc` command-line driver.

---

## BUGS

* Only one input file can be compiled per invocation.
* No incremental compilation or dependency tracking.
* The optimisation pipeline will be migrated to the LLVM new pass manager in a
  future release.

Please report bugs at the project repository.

---

## SEE ALSO

**clang(1)**, **llc(1)**, **opt(1)**

---

## COPYRIGHT

Copyright 2023–2026 Emilien Kia.  
Licensed under the Apache License, Version 2.0.  
<https://www.apache.org/licenses/LICENSE-2.0>

