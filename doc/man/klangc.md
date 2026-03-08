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

**`--dyn-lib`**  
Produce a shared library (`.so`) from the source file, regardless of whether
the module defines a `main` function. Linking is performed with
`clang -shared -fPIC`. If `-o` is not specified, the output name is derived
automatically (see *Library Output Naming* below).  
A warning is emitted if the module contains a `main` function.

**`--static-lib`**  
Produce a static archive (`.a`) from the source file, regardless of whether
the module defines a `main` function. The archive is created with `ar rcs`.
If `-o` is not specified, the output name is derived automatically.  
A warning is emitted if the module contains a `main` function.

**`--dyn-lib` + `--static-lib`** (combined)  
Produce **both** a shared library and a static archive in a **single
compilation pass** — the object file is generated only once and then fed to
both `clang -shared` and `ar`. `-o` is ignored in this mode (a warning is
emitted); output names are always derived automatically.  
A warning is also emitted if the module contains a `main` function.

**`--emit-kdi-json`**  
When producing a library (`.so` or `.a`), also write a `.kdi.json` file
alongside the `.kdi` CBOR file.  The JSON file is a human-readable equivalent
of the binary KDI file and has the same content.  The output name is always
`<lib-stem>.kdi.json` (e.g. `libmath.utils.kdi.json`).  If neither
`--dyn-lib` nor `--static-lib` is active and the module has no `main`, the
option is applied to the auto-produced shared library.

**`--no-emit-kdi`**  
Suppress automatic `.kdi` generation when producing a library.  By default,
**klangc** always writes a `.kdi` description file alongside every `.so` or
`.a` it produces.  This flag disables that behaviour entirely.  Implies that
`--emit-kdi-json` has no effect.

**`-o` _file_**, **`--output=`_file_**  
Place the primary output (object file, executable, or library) into _file_.  
When `-c` is not specified and no `-o` is given, the output name is derived
automatically according to the following priority:

| Flags active | Output | Automatic name |
|---|---|---|
| `-c` | `.o` | input stem + `.o` |
| `--dyn-lib` + `--static-lib` | `.so` + `.a` | `lib`_base_`.so` / `lib`_base_`.a` |
| `--dyn-lib` | `.so` | `lib`_base_`.so` |
| `--static-lib` | `.a` | `lib`_base_`.a` |
| _(none, no `main`)_ | `.so` | `lib`_base_`.so` |
| _(none, has `main`)_ | executable | _base_ |

Where _base_ = `unit_name_to_lib_base(module_name)` — see *Library Output Naming*.

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

## LIBRARY OUTPUT NAMING

When a library name is derived automatically (i.e. no `-o` is given), it is
built from the **module name** declared in the source file using the following
algorithm, implemented by `compiler::unit_name_to_lib_base()`:

1. Every `::` namespace separator in the module name is replaced by `.`.
2. The result is used as the **base name** (_base_).
3. A prefix `lib` and the appropriate suffix (`.so` or `.a`) are appended.
4. The file is placed in the **current working directory**.

**Examples:**

| Module declaration | _base_ | `.so` name | `.a` name |
|--------------------|--------|-----------|----------|
| `module mylib;` | `mylib` | `libmylib.so` | `libmylib.a` |
| `module math::utils;` | `math.utils` | `libmath.utils.so` | `libmath.utils.a` |
| `module com::example::foo;` | `com.example.foo` | `libcom.example.foo.so` | `libcom.example.foo.a` |

When `--dyn-lib` and `--static-lib` are combined, **both** files are produced
from a **single** intermediate object file — the LLVM module is compiled to a
`.o` only once, then passed to `clang -shared -fPIC` and `ar rcs` in sequence.

---

## EXAMPLES

**Compile and link a source file into an executable:**

```sh
klangc hello.k
```

Produces the executable `hello` (same name as input, without extension).

---

**Compile a library module into a shared library (no `main` function):**

```sh
klangc mylib.k
```

If `mylib.k` declares `module mylib;` and has no `main` function, this
produces `libmylib.so` in the current directory.

---

**Force shared library output (even if `main` is present):**

```sh
klangc --dyn-lib mylib.k
```

---

**Produce a static archive:**

```sh
klangc --static-lib mylib.k
# Produces: libmylib.a
```

---

**Produce both a shared library and a static archive in one pass:**

```sh
klangc --dyn-lib --static-lib mylib.k
# Produces: libmylib.so  libmylib.a  (single compilation pass)
```

---

**Explicit output name for a library:**

```sh
klangc --dyn-lib -o /usr/local/lib/libmylib.so mylib.k
```

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
| `lib*.so` | Shared library produced by `--dyn-lib` or auto-detection |
| `lib*.a` | Static archive produced by `--static-lib` |
| `lib*.kdi` | K Description Interface file — generated automatically alongside every library |
| `lib*.kdi.json` | Human-readable JSON equivalent of the `.kdi` file (opt-in via `--emit-kdi-json`) |
| `*.raw.ll` | Auto-generated raw LLVM IR text file |
| `*.opt.ll` | Auto-generated optimised LLVM IR text file |

---

## KDI DESCRIPTION FILES

Whenever **klangc** produces a library (`.so` or `.a`), it also generates a
**KDI** (K Description Interface) file in the same directory and with the same
stem, but with the `.kdi` extension.

```
libmath.utils.so   →   libmath.utils.kdi
libmath.utils.a    →   libmath.utils.kdi   (same file if both produced)
```

A KDI file is a **CBOR**-encoded binary file (schema version **0.1**) that
describes the public and protected API surface of the compiled module:

* **Header** — module name, library base name, path to the library binary,
  compiler version, and schema version.
* **Type table** — flat list of all aggregate type entries referenced by the
  module (fully-qualified name + LLVM struct mangled name).
* **Namespace tree** — recursive tree of namespaces containing:
  * Public / protected **global functions** with parameter types, return type
    and mangled symbol name.
  * Public / protected **global variables** with type and mangled symbol name.
  * Public / protected **aggregates** (struct / class / interface) with:
    - Inheritance clause (base FQ name, visibility, virtual flag, byte offset).
    - Physical layout (fields in LLVM index order): vptr, base subobjects,
      vbptr, vbase subobjects, parent reference, public/protected member
      variables, and **opaque blocks** for private fields (size in bits only).
    - Public / protected constructors (mangled C1 + C2 names).
    - Public / protected destructor (mangled D1 + D2 names).
    - Public / protected member methods (virtual flag, abstract, final, vtable
      slot index, mangled name).
    - Public / protected static variables.
    - Vtable layout for classes and interfaces (primary + secondary vtables,
      thunks).
    - Nested public / protected aggregates.

Private members are **obfuscated**: they appear as opaque blocks that only
expose their cumulative bit-size, preserving enough layout information for
subclasses to be compiled correctly without exposing implementation details.

KDI files can be inspected with the **kditool(1)** utility:

```sh
kditool dump libmath.utils.kdi
kditool validate libmath.utils.kdi
```

---

## NOTES

* Multiple source files on the command line are currently **not supported**.
  Only the first file is compiled; a warning is emitted for the rest.
* When a module has no `main` function and neither `-c`, `--dyn-lib` nor
  `--static-lib` is specified, **klangc** automatically produces a shared
  library (`.so`).
* `--dyn-lib` and `--static-lib` may be combined.  In that case the
  intermediate object file is generated **once** and passed to both
  `clang -shared -fPIC` (`.so`) and `ar rcs` (`.a`). `-o` is ignored
  in this combined mode.
* A **KDI** description file (`.kdi`) is generated automatically alongside
  every library output (`.so` or `.a`).  When both are produced in a single
  pass (`--dyn-lib --static-lib`), a single `.kdi` file is emitted, keyed
  on the `.so` path.  See *KDI DESCRIPTION FILES* above.
* Shared-library linking uses **clang(1)** with `-shared -fPIC`.  Static
  archives are created with **ar(1)** (`rcs`).  Executable linking uses
  **clang(1)** with `-pie`.  Both `clang` and `ar` must be present in `PATH`.
* All compilations use the **PIC** (Position-Independent Code) relocation
  model, making the generated objects compatible with both shared libraries
  and PIE executables.
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

