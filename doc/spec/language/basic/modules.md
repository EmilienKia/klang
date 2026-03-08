# Module System

[← Index](../index.md)

A *module* is the primary compilation unit of K.
Every source file may declare a module name; this declaration establishes the namespace that all top-level declarations in the file belong to.

---

## Contents

1. [Module declaration](#1-module-declaration)
2. [Import declarations](#2-import-declarations)
3. [Using imported symbols](#3-using-imported-symbols)
4. [Effect on namespace hierarchy](#4-effect-on-namespace-hierarchy)
5. [Name mangling](#5-name-mangling)

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

## 2. Import declarations

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
current compilation unit produces **warning `0x80010`**:

```
Warning 80010 : Imported module 'math::vec' is declared but none of its
                symbols are used in this compilation unit
```

This warning is emitted once per unused `import`, after all resolver passes
complete.  Remove the unused `import` declaration to suppress it.

**Circular imports:**

Cycles in the import graph (`A` imports `B` which imports `A`) are a
**fatal error** (`0x80003`).  The error message includes the full cycle path:

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

## 3. Using imported symbols

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

## 4. Effect on namespace hierarchy

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

## 5. Name mangling

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

*See also:* [Names and Lookup](names.md) · [Libraries — Export and Import](libraries.md) · [Main Function](main.md)
