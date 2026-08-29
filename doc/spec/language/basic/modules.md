# Module System

[← Index](../index.md)

A *module* is the primary compilation unit of K.
A module may be composed of one or more source files; all files in the same module share the same root namespace and have global visibility of each other's declarations (there is no file-private scope).

---

## Contents

1. [Module declaration](#1-module-declaration)
2. [Multi-file modules](#2-multi-file-modules)
3. [Import declarations](#3-import-declarations)
4. [Using imported symbols](#4-using-imported-symbols)
5. [Effect on namespace hierarchy](#5-effect-on-namespace-hierarchy)
6. [Name mangling](#6-name-mangling)

---

## 1. Module declaration

A module declaration appears at the very beginning of a source file, before any other declarations.

### Grammar

```
Unit:
    [ ModuleDeclaration ] { ImportDeclaration } { Declaration }

ModuleDeclaration:
    'module' QualifiedIdentifier ';'

QualifiedIdentifier:
    [ '::' ] Identifier { '::' Identifier }
```

**Examples:**

```k
module myapp;
module math::linear;
module the::deep::nested::module;
```

If a module declaration is absent, declarations are placed in the root namespace.

**Constraints:**
- At most one `module` declaration is allowed per source file.
- The module declaration must precede all other declarations (including imports).

---

## 2. Multi-file modules

A module may span multiple source files.  All files passed to the compiler in a
single invocation contribute to the same compilation unit.

### Module name resolution

The compiler scans all source files for `module` declarations before full
parsing begins.  The following rules apply:

1. **One file declares a module name** — this is the normal case.  The declared
   name becomes the module name and root namespace for the entire unit.  Files
   without a `module` declaration implicitly belong to this module.
2. **Several files declare the same module name** — this is allowed.  Duplicate
   identical declarations are silently accepted.
3. **Several files declare different module names** — this is an **error**.
4. **No file declares a module name** — a warning is emitted and a random
   anonymous name is generated, as for single-file compilation.
5. **The `--module-name` CLI flag** overrides any source-file declaration.

### Visibility

All top-level declarations are globally visible across all files of the module.
There is no file-private scope: a symbol declared in file A can be referenced
from file B without any additional qualifier.

### Duplicate symbols

If a symbol (function, variable, type) with the same fully-qualified name and
signature is defined in more than one file, it is a **compilation error**.

> **Note (current limitation):** Duplicate symbol detection across files of the
> same module is not yet enforced by the compiler.  Duplicate definitions may
> silently overwrite each other.  This will be addressed in a future release.

### Imports

Each file may have its own `import` declarations.  Imports with the same module
name across different files are automatically deduplicated.

### Compilation output

Regardless of how many source files compose a module, the compiler produces a
single output: one object file, one executable, one shared library, or one
static library, along with a single `.kdi` description file.

---

## 3. Import declarations

An import declaration makes the public API of another compiled K module visible
in the current file.  The compiler will locate the corresponding `.kdi`
description file and make its types, functions and variables available for
resolution.

### Grammar

```
ImportDeclaration:
    'import' QualifiedIdentifier ';'

QualifiedIdentifier:
    [ '::' ] Identifier { '::' Identifier }
```

**Examples:**

```k
import math;
import math::vec;
import my::deep::lib;
```

**Constraints:**
- Import declarations must appear after the module declaration and before
  any other declarations.
- Each module name must be unique within a compilation unit.
- Two imported modules must not share the same root namespace component
  (see *Namespace collision rules*).
- An import declaration that cannot be resolved to a `.kdi` file is a
  **fatal error**.  There is no mechanism for optional or conditional imports.

**Transitive imports:**

When module `A` imports module `B`, and `B` was itself compiled against
module `C`, then `C` is a *transitive dependency* of `A`.  **klangc**
loads all transitive KDIs automatically from the `header.dependencies`
field of each KDI it loads.  A module does **not** need to `import C;`
explicitly to use types or functions that were defined in `C` and re-exported
(inherited, returned, or passed) by `B`.

A missing transitive KDI is a **fatal compilation error**; without it, the
aggregate layouts and vtable slots declared in the direct import cannot be
fully reconstructed.

**Unused imports:**

An `import` declaration whose module's symbols are never referenced in the
current compilation unit produces **warning `0x0108`**:

```
Warning 80010 : Imported module 'math::vec' is declared but none of its
                symbols are used in this compilation unit
```

This warning is emitted once per unused `import`, after all resolver passes
complete.  Remove the unused `import` declaration to suppress it.

**Circular imports:**

Cycles in the import graph (`A` imports `B` which imports `A`) are a
**fatal error** (`0x0105`).  The error message includes the full cycle path:

```
Error 80003 : Circular import dependency detected: cycle_a → cycle_b → cycle_a
```

Cycles can only arise from hand-crafted or corrupted `.kdi` files — the
compiler never produces a `.kdi` that would create a cycle.

**Namespace collision rules:**

Two distinct imported modules must not share the same *root namespace
component* (the first `::` segment of their module name).  For example,
`import math::vec; import math::matrix;` is an error because both root
components are `math`.  However, importing `import math::vec;` into a module
declared `module io::reader;` is allowed because the roots differ.

The `--enforce-ns-collision` CLI flag additionally forbids a collision between
the root of the unit being compiled and any imported module.

---

## 4. Using imported symbols

An `import` declaration does **not** inject names into the current namespace.
Every imported symbol must be referenced by its **fully qualified name** —
the complete path from the module namespace root.

```k
module myapp;
import math::vec;

main() : int {
    // Bare 'dot' would be an error — not declared in this module.
    return math::vec::dot(3, 4);
}
```

The absolute form (leading `::`) bypasses all local scopes and resolves
directly from the root namespace; it is always unambiguous:

```k
    return ::math::vec::dot(3, 4);
```

### Using directives with imports

A `using` directive can relax the fully-qualified requirement for imported
symbols.  See [Using Directives](using.md) for the full specification.

```k
module myapp;
import math::vec;

using namespace math::vec;        // inject all members

main() : int {
    return dot(3, 4);             // OK: resolves via using
}
```

Aliases provide a short local name without injecting all members:

```k
module myapp;
import very_long_module;

using namespace m = very_long_module;  // namespace alias

main() : int {
    return m::compute(21);        // OK: resolves via alias
}
```

Aliases are purely local — they are never exported to the `.kdi` file.

**Imported types** (struct, class, interface) are also used with their full
qualified name:

```k
module myapp;
import shapes;

draw(s: shapes::IShape&) : int {
    return s.area();
}

main() : int {
    r : shapes::Rectangle(3, 4);
    return draw(r);
}
```

**Constructors** are invoked as `ModuleName::TypeName(args)`:

```k
    pt : geom::Point(1, 2);
```

**Static members** use `::` on the qualified type name:

```k
    n : int = mylib::Counter::instance_count();
```

For the complete reference on lookup rules, visibility, and inheritance
from imported types, see [Libraries — Export and Import](libraries.md).

---

## 5. Effect on namespace hierarchy

The module name is mapped directly to a namespace path.
Each `::`-separated component of the module name becomes a level of nested namespace.

**Example:**

```k
module math::linear;

dot(a: double, b: double) : double {
    return a * b;
}
```

The function `dot` has the fully qualified name `::math::linear::dot`.
Its mangled name encodes the namespace path.

---

## 6. Name mangling

The K compiler produces mangled names for all global symbols to avoid link-time conflicts across modules.

| Entity kind | Example mangled name |
|-------------|----------------------|
| Global function `test_local` in module `the::test` | `_KFN3the4test10test_localEv` |
| Global variable `g` in module `the::test` | `_KN3the4test1gE` |
| Member function `sum` of struct `plop` in module `the::test` | `_KFMN3the4test4plop3sumEv` |

The mangling scheme encodes:
- `_K` — K language prefix.
- `F` — function.
- `M` — member (of a struct).
- `N` — namespace sequence.
- Each name is prefixed by its length.
- `E` — end of name sequence.
- `v` — void (for functions with no parameters).

> **Note:** The exact mangling scheme is an implementation detail and may evolve.

---

*See also:* [Names and Lookup](names.md) · [Using Directives](using.md) · [Libraries — Export and Import](libraries.md) · [Main Function](main.md)
