# Module System

[← Index](../index.md)

A *module* is the primary compilation unit of K.
Every source file may declare a module name; this declaration establishes the namespace that all top-level declarations in the file belong to.

---

## Contents

1. [Module declaration](#1-module-declaration)
2. [Import declarations](#2-import-declarations)
3. [Effect on namespace hierarchy](#3-effect-on-namespace-hierarchy)
4. [Name mangling](#4-name-mangling)

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

An import declaration makes declarations from another module visible in the current file.

### Grammar

```
ImportDeclaration:
    'import' Identifier ';'
```

> **Note:** The import system is in an early stage. Currently only a single identifier is supported as the import target. This will be expanded in future versions.

---

## 3. Effect on namespace hierarchy

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

## 4. Name mangling

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

*See also:* [Names and Lookup](names.md) · [Main Function](main.md)
