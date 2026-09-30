# K Language — Generics

> **Status: WORK IN PROGRESS.** This page is a *feature overview*, not a finished
> specification. The generics feature is under active development: several rules below
> describe the intended model, and the [Current limitations](#8-current-limitations)
> section lists where the implementation still differs from it.
>
> **Version:** Working Draft — 2026  
> **Formal grammar:** see [`../grammar.ebnf`](../grammar.ebnf) (`GenericDeclaration`)  
> **Related:** [Templates](templates.md), summary §26 in [`../summary.md`](../summary.md)

---

## Table of Contents

1. [Overview](#1-overview)
2. [Generics versus templates](#2-generics-versus-templates)
3. [Declaration syntax](#3-declaration-syntax)
4. [Generic parameters](#4-generic-parameters)
5. [Restrictions inside a generic body](#5-restrictions-inside-a-generic-body)
6. [Use sites](#6-use-sites)
7. [Diagnostics](#7-diagnostics)
8. [Current limitations](#8-current-limitations)
9. [Examples](#9-examples)

---

## 1. Overview

A **generic** is a template with restrictions. It is declared with the `generic` keyword
instead of `template`, and applies to the same kinds of declarations (aggregates,
free functions and member functions).

The defining property of a generic is that it has **one single synthesis**: the body of a
generic aggregate or function is compiled exactly once, whatever the type arguments used
by its users. To make that possible:

- every generic type parameter is **erased** inside the body: the compiler only sees an
  opaque pointer, and only the *base restriction* declared for the parameter (kind
  `typename` / `struct` / `class` / `interface`, optionally `: Base`) is known to the
  instantiation checks;
- the body can therefore only **move addresses around** (store, pass, return, compare
  with `null`); it cannot look inside the objects designated by a generic parameter;
- the **users** of the generic keep the full type information: the concrete type
  arguments are checked at the use site, and the results are seen with their real types
  (`Box<Dog>::get()` returns a `Dog*`).

This resembles the generics of Java or C# at the code-generation level (type erasure),
while keeping static type checking at the K source level.

Consequences of the single synthesis:

- one binary artefact serves all type arguments (no code duplication): `pick<Dog>` and
  `pick<Cat>` call the same function;
- the mangled name has **no type-argument suffix**;
- KDI export is **signature only**: the source text of a generic body is not exported.

---

## 2. Generics versus templates

| Aspect | `template` | `generic` |
|--------|------------|-----------|
| Code synthesis | One concrete entity per distinct argument set (monomorphization) | One single synthesis, type arguments erased |
| Parameter kinds | Type and value parameters, packs | **Type parameters only** |
| Default type arguments | Yes | No |
| Constraints | Kind filter, base-type constraint | Kind filter, base-type constraint (checked at instantiation only) |
| Use of a parameter inside the body | Any (it is substituted by the real type) | **Only through a non-owning addresser** (`&`, `*`, `?`, `+`) |
| Members of a parameter inside the body | Accessible (`v->field`, `v.method()`) | **Forbidden** (error) |
| Owner (`T!`) / drain (`T#`) of a parameter | Allowed | **Forbidden** (error) |
| Owner / drain **as type argument** (`Box<Dog!>`) | Allowed | **Forbidden** (error) |
| Specialization, packs, value parameters | Yes / planned | Not applicable |
| KDI export | Full source text | Signature only |
| Name mangling | Type-argument suffix (e.g. `I4DogE`) | No suffix |
| Variance | Not applicable | Invariant (`Box<Dog>` is not a `Box<Animal>`) |

The two features are otherwise independent: a `template` can use a generic and
vice versa. Use a `template` when the body needs to look inside `T`; use a `generic` when
the body is only a container or relay of addresses.

---

## 3. Declaration syntax

```
GenericDeclaration:
    'generic' '<' GenericParameterList '>'

GenericParameterList:
    GenericParameter { ',' GenericParameter }

GenericParameter:
    GenericParameterKind Identifier [ ':' TypeSpec ]

GenericParameterKind:
    'typename' | 'struct' | 'class' | 'interface'
```

The declaration prefix is placed after annotations and before specifiers, at the same
position as a template declaration.

```k
// Generic aggregate
generic<class T> class Box {
    private val : T*;
    public:
    Box() { val = null; }
    set(v : T*) { val = v; }
    get() : T* { return val; }
}

// Generic free function
generic<class T> identity(v : T*) : T* { return v; }

// Generic method of a non-generic class
class Util {
    generic<class T> wrap(v : T*) : T* { return v; }
}
```

Value parameters (`generic<int N>`) are rejected at parse time
(`ERR_GENERIC_VALUE_PARAM_NOT_ALLOWED`, `0x036E`).

---

## 4. Generic parameters

| Keyword | Accepted type arguments |
|---------|-------------------------|
| `typename` | Any type that is not an owner or a drain |
| `struct` | A `struct` type |
| `class` | A `class` type |
| `interface` | An `interface` type |

An optional `: Base` constraint requires the argument to be `Base` or to derive from it.
The constraint is **only checked when the generic is instantiated**; it does not give the
body access to the members of `Base` (see §5).

Type arguments that are **owner** (`Dog!`) or **drain** (`Dog#`) types are rejected for
every kind of parameter (diagnostic `0x1107`). Pointers, references, links and views are
accepted as type arguments only through the parameter's own addresser inside the body.

---

## 5. Restrictions inside a generic body

Inside the body of a generic declaration (member types, parameter types, return types,
local variables, casts, `new` expressions and explicit template arguments):

### 5.1 A parameter can only appear behind a non-owning addresser

| Usage | Valid | Diagnostic |
|-------|-------|------------|
| `T&`, `T*`, `T?`, `T+` | Yes | — |
| `T` (bare) | No | `0x1101` |
| `T!` (owner) | No | `0x1102` |
| `T#` (drain) | No | `0x1106` |

A bare `T` is forbidden because its layout is unknown. Owners and drains are forbidden
because they require **explicit member calls on the designated object** — the destructor
of an owned object, the state stealing of a drained one — and the erased body cannot
perform them on an object of unknown type.

### 5.2 Members of an erased parameter cannot be used

Any access to a member (field or method) through an expression whose type is an erased
generic parameter is an error (diagnostic `0x1108`). This is intended:

```k
generic<class T> class Box {
    private val : T*;
    public:
    read() : int { return val->id; }     // error 0x1108: 'id' through erased 'T*'
}
```

Member accesses on the concrete type must be done by the users of the generic, at the use
site, where the real type is known. The same rule applies to generic functions and to
generic methods.

### 5.3 What the body can do

Store an address in a member or local, pass it to another generic entity, return it,
compare it with `null` or with another address of the same parameter, and rebind or
assign addressers according to the usual indirection rules.

```k
generic<class T> class Optional {
    private val : T*;
    public:
    Optional() { val = null; }
    set(v : T*) { val = v; }
    get() : T* { return val; }
    hasValue() : bool { return val != null; }
}
```

---

## 6. Use sites

Type arguments are given with the usual template syntax and are fully type-checked at
the use site:

```k
class Animal { }
class Dog : Animal { }

test() : int {
    d : Dog;
    b : Optional<Dog>;
    b.set(&d);                    // Dog* accepted
    p : Dog* = b.get();           // returns Dog*
    // b.set(&someCat);           // error: Cat* is not Dog*
    return b.hasValue() ? 1 : 0;
}
```

Generic types are **invariant**: `Optional<Dog>` is not an `Optional<Animal>`. Explicit
type arguments on member calls of a non-generic class are supported
(`host.wrap<zoo::Dog>(&d)`), including namespace-qualified types, aliases and enclosing
template parameters.

The user is responsible for the type checks and casts that the erased body cannot do
(for instance downcasting a `T*` obtained from a generic container).

---

## 7. Diagnostics

| Code | Name | Meaning |
|------|------|---------|
| `0x036E` | `ERR_GENERIC_VALUE_PARAM_NOT_ALLOWED` | Value parameter in a generic declaration |
| `0x1101` | `ERR_GENERIC_DIRECT_TYPE_USAGE` | Bare `T` (no addresser) |
| `0x1102` | `ERR_GENERIC_OWNER_NOT_SUPPORTED` | `T!` on a generic parameter |
| `0x1106` | `ERR_GENERIC_DRAIN_NOT_SUPPORTED` | `T#` on a generic parameter |
| `0x1107` | `ERR_GENERIC_ARG_OWNER_DRAIN_NOT_ALLOWED` | Owner or drain type used as type argument |
| `0x1108` | `ERR_GENERIC_MEMBER_ACCESS_ON_ERASED_PARAM` | Member access through an erased parameter |

`0x1103` (constraint violated), `0x1104` (too many arguments) and `0x1105` (too few
arguments) are reserved; those situations are currently reported by the template
diagnostics (for instance `0x1005` when an argument is not an aggregate of the required
kind).

Negative examples:

```k
generic<class T> class A { v : T; }                    // 0x1101
generic<class T> class B { v : T!; }                   // 0x1102
generic<class T> class C { take(v : T#) { } }          // 0x1106
generic<typename T> class D { p : T*; }
f() { x : D<Dog!>; }                                   // 0x1107
generic<class T> class E { p : T*; g() : int { return p->id; } }   // 0x1108
```

---

## 8. Current limitations

This list reflects the state of the implementation and is expected to shrink.

1. **Owners and drains are not supported** as generic parameters or generic arguments (§5.1).
   Supporting owners would require the erased body to receive an *owned callable* that
   destroys the owned object; this is under study.
2. **Explicit type arguments naming a generic parameter are rejected inside a generic
   body** (`ident<T>(v)` with `T` a parameter of the enclosing generic: bare `T` error
   `0x1101`). Let the type argument be deduced instead (`ident(v)`).
3. **The erasure target is a plain opaque pointer** whatever the parameter kind or the
   `: Base` constraint. Constraints are validated at instantiation only; they do not give
   access to the members of the base type.
4. **A generic method declared inside a generic class** follows the class: it is erased with
   the class, and its own type parameters are still instantiated like a template's.
5. **Cross-module use of generic aggregates is not reliable yet.** Generic free functions can
   be exported and used through KDI (the consumer re-synthesises the single erased body from
   the exported source). For a generic *aggregate*, the KDI signature carries no member
   layout and the erased synthesis is only produced when the defining module instantiates it,
   so importing modules can build an object of the wrong size or miss the symbols. Generic
   methods of a non-generic class (and, more generally, member templates of a non-template
   class) are not exported through KDI.
6. **No default type arguments, no value parameters, no packs, no specialization.**
7. **Generic methods are never virtual.**
8. **Owned members of a generic (or non-generic) aggregate are not destroyed
   automatically** when the aggregate is destroyed; this is not specific to generics.

---

## 9. Examples

### 9.1 A typed container without member access

```k
generic<class T> class Slot {
    private item : T*;
    public:
    Slot() { item = null; }
    put(v : T*) { item = v; }
    take() : T* { return item; }
    empty() : bool { return item == null; }
}

class Dog { public name : int = 1; }

run() : int {
    d : Dog;
    s : Slot<Dog>;
    s.put(&d);
    r : Dog* = s.take();
    return r->name;               // member access done by the user, on Dog
}
```

### 9.2 A generic method with explicit type argument

```k
class Registry {
    public:
    generic<class T> keep(v : T*) : T* { return v; }
}

run() : int {
    d : zoo::Dog;
    r : Registry;
    p : zoo::Dog* = r.keep<zoo::Dog>(&d);
    return p != null ? 0 : 1;
}
```

### 9.3 When a template is the right tool

```k
// The body reads a field of T: this cannot be a generic.
template<class T> readId(v : T*) : int { return v->id; }
```
