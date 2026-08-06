# Aliases and Typedefs

[← Index](../index.md)

An **alias declaration** gives a second name to an existing entity. Unlike a
[`using` directive](using.md) — which only affects name lookup inside the scope
that declares it — an alias declaration is a first-class named declaration: it
is **exported through the module interface** (the `.kdi` file) and is therefore
visible to importing modules.

An alias declares nothing new at code-generation time. Every use is replaced by
the entity it renames, recursively, until a non-alias entity is reached. **No
symbol is ever synthesised for an alias**, and mangled names always use the
fully resolved (alias-free and template-instantiated) type.

---

## Contents

1. [Overview](#1-overview)
2. [Grammar](#2-grammar)
3. [Soft alias — `alias`](#3-soft-alias--alias)
4. [Strong alias — `typedef`](#4-strong-alias--typedef)
5. [Conversions](#5-conversions)
6. [Expression locality](#6-expression-locality)
7. [Overloading and mangling](#7-overloading-and-mangling)
8. [Scope, visibility and export](#8-scope-visibility-and-export)
9. [Restrictions](#9-restrictions)
10. [Comparison with `using`](#10-comparison-with-using)

---

## 1. Overview

There are two forms:

| Form | Kind | Effect |
|------|------|--------|
| `alias Name : entity;` | soft alias | `Name` and `entity` are fully interchangeable |
| `typedef Name : Type;` | strong alias | `Name` is a nominally distinct type over the same representation |

```k
alias   Bytes      : byte[];        // convenience renaming
typedef Identifier : int;           // distinct semantic type over 'int'
```

---

## 2. Grammar

```ebnf
AliasDecl     = SoftAliasDecl | TypedefDecl ;
SoftAliasDecl = "alias"   Identifier ":" ( TypeSpec | QualifiedIdentifier ) ";" ;
TypedefDecl   = "typedef" Identifier ":" TypeSpec ";" ;
```

An alias declaration may appear wherever a declaration may appear (module
level, namespace, aggregate body) and also inside a statement block.

---

## 3. Soft alias — `alias`

A soft alias is a pure convenience renaming. It may target **any kind of
symbol**:

```k
struct Point { x : int; y : int; }

alias Coord   : Point;          // a type
alias Origin  : defaultPoint;   // a global or static variable
alias make    : buildPoint;     // a function
```

Typical uses are shortening a template aggregate with fixed arguments, or
giving a domain-oriented name to a primitive or third-party type:

```k
alias IntVector : Vector<int>;
alias Timestamp : long;
```

A soft alias is **fully transparent**: the alias name and the aliased symbol
denote exactly the same entity, and either can be used in place of the other,
in both directions, without any cast.

---

## 4. Strong alias — `typedef`

A `typedef` targets a **type** only. It introduces a nominally distinct type
over an identical representation, so that a semantic layer can be added on top
of the type system — for instance two kinds of identifier that are both `int`
but denote different things:

```k
typedef UserId  : int;
typedef OrderId : int;
```

At code-generation time a `typedef` is still the type it renames: the layout,
the ABI and the generated code are identical. The distinction exists only in
the K type system.

---

## 5. Conversions

Converting **from** a typedef **to** the type it renames is implicit; the
reverse direction requires an explicit cast:

```k
typedef Identifier : int;

num : int = 4;

id : Identifier = num;      // OK — variable definition states the type
id = 8;                     // OK — literal / compile-time constant
id = (Identifier) num;      // OK — explicit cast
id = num;                   // ERROR — explicit cast required
num = id;                   // OK — implicit in this direction
```

Summary of an expression of the underlying type used where the typedef is
expected:

| Context | Result |
|---------|--------|
| Variable definition initialiser | accepted — the type is stated just before |
| Literal / compile-time constant | accepted |
| Assignment to an existing variable | **error** — explicit cast required |
| Function argument | **warning** (see [§7](#7-overloading-and-mangling)) |
| `return` from a function returning the typedef | **warning** |

The explicit cast is always a no-op at run time: both sides resolve to the same
representation.

---

## 6. Expression locality

Inside a single expression, an operand whose type is an alias (or an addresser
over that alias) may be treated either as the alias or as the type it renames,
whichever the expression needs. An expression is considered a small enough
locality for the intent to be unambiguous, so no explicit cast is required
there:

```k
typedef Meters : int;

a : Meters = 3;
b : Meters = 4;
c : Meters = a + b;         // OK — the sum stays 'Meters'
d : int    = a * 2;         // OK — the alias degrades to 'int'
```

---

## 7. Overloading and mangling

Because a `typedef` is only a renaming of its underlying type, **a function
declaration using a typedef is mangled with the fully resolved type**.
Consequently:

* Declaring both `f(v : Identifier)` and `f(v : int)` is **forbidden** — the two
  would produce the same mangled symbol. This is reported as a dedicated error.
* Calling `f(v : Identifier)` with an argument of the underlying type produces a
  **warning**: the mismatch cannot be enforced at link time, so the compiler
  advises either casting explicitly to the typedef, or declaring the parameter
  with the underlying type.

---

## 8. Scope, visibility and export

| Placement | Visibility | Exported |
|-----------|------------|----------|
| Module or namespace level | `public` by default | yes, unless `private` |
| Aggregate body | follows the current member visibility | yes, unless `private` |
| Statement block | always private | never |

An exported alias appears as written in the `.kdi` file, so an importing module
uses it exactly like any other imported type:

```k
// lib.k
module lib;
typedef Identifier : int;
makeId(v : Identifier) : Identifier { return v; }
```

```k
// app.k
module app;
import lib;
main() : int {
    id : lib::Identifier = 4;
    return lib::makeId(id);
}
```

Declared inside a statement block, an alias is restricted to that block and is
never exported. A block-level `alias` behaves exactly like an aliased `using`
declaration; a block-level `typedef` is more restrictive since it introduces a
distinct type.

---

## 9. Restrictions

* **A namespace cannot be aliased.** Use `using N = namespace X::Y;` instead;
  attempting `alias N : X::Y;` on a namespace is a dedicated error.
* A `typedef` targets a type only; a function or a variable can only be renamed
  with a soft `alias`.
* Alias declarations must not form a cycle.
* Overloading on a `typedef` and on its underlying type is forbidden (see
  [§7](#7-overloading-and-mangling)).

---

## 10. Comparison with `using`

| | `using` | `alias` | `typedef` |
|---|---|---|---|
| Affects name lookup only | yes | yes | no — introduces a type |
| Creates a named declaration | no | yes | yes |
| Exported through the `.kdi` | no | yes | yes |
| Can rename a namespace | yes | no | no |
| Can rename a function or a variable | yes | yes | no |
| Distinct type | n/a | no | yes |

---

[← Index](../index.md) | [Using Directives](using.md)
