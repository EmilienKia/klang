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

Multiple source files may be passed on the command line; they are all compiled
as a single compilation unit (module).  All files share the same root namespace
and have global visibility of each other's declarations.

The compiler internally uses LLVM for code generation and delegates linking to
**clang(1)**.


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
Path(s) to the K source files to compile. Positional; may also be specified
explicitly with `--input-file=`_file_. Multiple files are compiled as a single
module (see *Multi-file modules* in the language specification).

**`--module-name` _name_**  
Override the module name regardless of any `module` declaration in the source
files. This is equivalent to having `module` _name_`;` at the top of every
file.  Useful for build systems that determine the module name externally.

---

### Import Options

These options control how **klangc** locates the `.kdi` description files and
binary libraries (`.so` / `.a`) for modules declared with `import` statements.

**`-I` _dir_**, **`--include-path=`_dir_** (repeatable)  
Add _dir_ to the list of directories searched for `.kdi` description files.
When `-L` is not specified, _dir_ is also searched for `.so` / `.a` binaries.  
Directories are tried in the order they are given, after the current directory
and before the environment-variable paths and system directories.

**`-i` _spec_**, **`--include-kdi=`_spec_** (repeatable)  
Explicitly specify a `.kdi` file for an imported module. _spec_ can be either:
- `module::name=/path/to/file.kdi` — explicit module-name → file mapping
- `/path/to/file.kdi` — the module name is read from the file's `header.module_name`

Explicit paths have the highest priority and bypass all directory searches.

**`-L` _dir_**, **`--lib-path=`_dir_** (repeatable)  
Add _dir_ to the list of directories searched for binary library files
(`.so` / `.a`). When both `-I` and `-L` are specified, `-I` applies only to
`.kdi` files and `-L` applies only to binaries.

**`-l` _spec_**, **`--lib=`_spec_** (repeatable)  
Specify a library binary to link against. _spec_ can be:
- A **short name** such as `math.vec` → resolves to `libmath.vec.so`
  (searched in `-L` directories then system library paths).
- A **full or relative path** to a `.so` or `.a` file.

**`--lib-path-env=`_name_**  
Override the name of the environment variable used to pass additional
KDI / library search directories (default: `KLANG_LIB_PATH`).  The variable
value must be a colon-separated list of directory paths (UNIX) or
semicolon-separated (Windows).

**`--no-lib-path-env`**  
Disable environment-variable-based path lookup entirely. The variable named
by `--lib-path-env` (or `KLANG_LIB_PATH`) is ignored even if set.

**`--enforce-ns-collision`**  
By default the root namespace of the unit being compiled prevails over
imported modules without any error.  This flag makes it an error if the
root namespace component of the unit being compiled collides with the root
namespace component of any imported module.

#### Search order summary

For each `import foo::bar;` declaration, **klangc** searches for
`foo.bar.kdi` (description) and `libfoo.bar.so` (binary) in this order:

1. Explicit paths from `-i` (highest priority)
2. Current working directory
3. Directories from `-I` (in order)
4. Directories from `KLANG_LIB_PATH` environment variable (unless `--no-lib-path-env`)
5. System KDI directories: `/usr/local/lib/kdi`, `/usr/lib/kdi`, `/usr/lib/<platform>/kdi`
6. System library directories: `/usr/local/lib`, `/usr/lib`, `/usr/lib/<platform>`

`<platform>` is determined at compile time from the compiler's
`CMAKE_LIBRARY_ARCHITECTURE` (e.g. `x86_64-linux-gnu`).

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

Error codes follow the pattern `0xCCCCC` where the leading digit indicates the
compiler phase.

| Code | Severity | Condition |
|---|---|---|
| `0x80001` | Error | Namespace root collision between two imported modules |
| `0x80002` | Error | `--enforce-ns-collision`: imported root collides with the unit's own root |
| `0x80003` | Error | **Circular import dependency detected** — message includes the full cycle path, e.g. `A → B → C → A` |
| `0x80004` | Error | KDI file not found for an imported module |
| `0x80005` | Error | KDI file found but failed to parse (corrupt or wrong schema version) |
| `0x80010` | Warning | **Imported module declared but none of its symbols are used** — emitted once per unused `import` declaration after all resolver passes complete |

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

**Compile a module that imports another library (direct + transitive resolution):**

```sh
# lib1: interface IVal { val() : int; }
klangc --dyn-lib ival.k

# lib2: abstract class AVal : ival::IVal  — imports ival.k
klangc --dyn-lib -I . aval.k

# lib3: class ConcreteVal : AVal  — imports ival + aval
klangc --dyn-lib -I . cval.k

# exe: imports ival + cval; aval is a transitive dep resolved from -I .
klangc -I . -L . main.k -o myapp
```

`aval` is **never listed** in `main.k`'s `import` statements, yet **klangc**
loads its `.kdi` automatically because `cval.kdi` declares it in its
`dependencies` list.

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

* Multiple source files on the command line are compiled as a **single module**
  (compilation unit).  All files share the same root namespace and have global
  visibility of each other's declarations.  There is no file-private scope.
* If multiple files contain a `module` declaration, they must all declare the
  **same** module name, otherwise compilation fails.  If only one file has a
  `module` declaration, its name is used for the entire unit.  If no file has
  one, a random anonymous name is generated (with a warning).
* The `--module-name` flag overrides any source-level `module` declarations.
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

### Transitive imports

When **klangc** imports a module whose `.kdi` lists its own imports in
`header.dependencies`, those **transitive dependencies** are resolved
automatically — they do not need to be listed with `import` in the source file.
The same search order applies: explicit `-i` paths, current directory, `-I`
directories, `KLANG_LIB_PATH`, system directories.

A **missing transitive dependency** is a **fatal error**: without it the
aggregate layout and vtable slots of the direct import cannot be fully
reconstructed, and compilation is aborted.  This is intentional — unlike
linker warnings for unused symbols, an incomplete type graph would produce
silently broken code.

To make all transitive KDIs available, either:

1. Install the transitive libraries in a system KDI directory (e.g.
   `/usr/lib/kdi`).
2. Pass `-I <dir>` pointing to the directory that contains the transitive
   `.kdi` files (a canonical-name symlink `<base>.kdi` → `<random>.kdi` is
   sufficient).
3. Register each transitive module explicitly with `-i module::name=/path/to/it.kdi`.

---

## BUGS

* No incremental compilation or dependency tracking.
* Duplicate symbol detection across files of the same module is not yet enforced
  (may silently overwrite).
* The optimisation pipeline will be migrated to the LLVM new pass manager in a
  future release.

Please report bugs at the project repository.

---

## ENVIRONMENT

**`KLANG_LIB_PATH`**  
Colon-separated (UNIX) or semicolon-separated (Windows) list of directories
appended to the KDI and library search path, after `-I` directories and
before the system directories.  The variable name can be overridden with
`--lib-path-env=`_name_ or disabled entirely with `--no-lib-path-env`.

---

## SEE ALSO

**clang(1)**, **llc(1)**, **opt(1)**

---

## COPYRIGHT

Copyright 2023–2026 Emilien Kia.  
Licensed under the Apache License, Version 2.0.  
<https://www.apache.org/licenses/LICENSE-2.0>

