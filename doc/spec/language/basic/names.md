# Names, Namespaces and Lookup

[← Index](../index.md)

This page describes how names are introduced, organised and resolved in K programs.

---

## Contents

1. [Names and identifiers](#1-names-and-identifiers)
2. [Qualified names](#2-qualified-names)
3. [Namespaces](#3-namespaces)
4. [Modules and the namespace hierarchy](#4-modules-and-the-namespace-hierarchy)
5. [Visibility modifiers](#5-visibility-modifiers)
6. [Name lookup rules](#6-name-lookup-rules)
7. [Scope resolution operator `::`](#7-scope-resolution-operator-)

---

## 1. Names and identifiers

A *name* refers to an entity declared in the program: a variable, function, struct, or namespace.
Names are formed from [identifiers](lexical.md#identifiers).

An identifier is a sequence of letters, digits and underscores, starting with a letter.
Identifiers that consist solely of underscores are reserved.

---

## 2. Qualified names

A *qualified name* consists of one or more identifiers separated by `::`.
It may optionally start with `::` to indicate the root (global) namespace.

### Grammar

```
QualifiedIdentifier:
    [ '::' ] Identifier { '::' Identifier }
```

**Examples:**

```k
x                      // simple name
my::ns::value          // qualified name
::the::module::func    // absolute qualified name from root
Outer::Inner           // nested struct access
```

---

## 3. Namespaces

A *namespace* is a named scope that groups declarations.
Namespaces can be nested.

### Grammar

```
NamespaceDecl:
    'namespace' [ Identifier ] '{' { Declaration } '}'
```

Namespaces may be unnamed (anonymous) — declarations in an unnamed namespace are visible only within the current file.

**Example:**

```k
namespace geometry {
    struct Point {
        x : double;
        y : double;
    }

    distance(a: Point, b: Point) : double {
        // ...
    }
}
```

---

## 4. Modules and the namespace hierarchy

Every K source file begins with an optional `module` declaration.
The module name defines the namespace into which all top-level declarations of that file are placed.
The module name may be a qualified identifier; each component becomes a nested namespace.

### Grammar

```
ModuleDeclaration:
    'module' QualifiedIdentifier ';'
```

**Examples:**

```k
module geometry;          // all declarations go into namespace 'geometry'
module math::linear;      // all declarations go into namespace 'math::linear'
```

If no module declaration is present, declarations fall into the root (anonymous) namespace.

The full qualified name of an entity is: `:: <module-namespace> :: <local-name>`.

**Example:**

```k
module the::test;

myFunc() : int { return 0; }
// fully qualified name: ::the::test::myFunc
```

---

## 5. Visibility of namespace members

Every declaration inside a namespace body (function, variable, struct, nested namespace) has a *visibility* that controls which code may access it.

### Visibility levels

| Keyword     | Accessible from …                                        | Exported to `.kdi`? |
|-------------|----------------------------------------------------------|---------------------|
| `public`    | Everywhere. This is the **default**.                     | ✓ Full definition   |
| `protected` | The same **module** only (same root namespace).          | ✓ Full definition   |
| `private`   | The same **namespace** only.                             | ✗ No (opaque block) |

When **klangc** compiles a module into a library, it writes a **KDI** description
file alongside the binary.  Only `public` and `protected` entities are included
in full — `private` entities are omitted (at struct/class member level they
appear as size-only opaque blocks to preserve layout).  Consumers of the library
can only call or reference `public` entities; `protected` entities are accessible
only within the same root namespace (i.e. the same multi-file module in future).

### Ways to specify visibility

There are two mechanisms, in order of precedence (highest first):

**Per-element specifier** — a visibility keyword placed directly before a single declaration:

```k
module mymod;

public  exported()  : int { return 1; }   // explicit public
protected internal() : int { return 2; }  // module-internal only
private  detail()   : int { return 3; }   // this namespace only

private y : int = 20;                     // private variable
```

**Group visibility specifier** — a visibility keyword followed by `:` sets the current default for all subsequent declarations in the body, until another group specifier or the end of the enclosing body:

```k
module mymod;

// default: public
api1() : int { return 1; }

protected:                               // switch to protected
_cache() : int { return 2; }
_flush() { }

private:                                 // switch to private
_state : int = 0;

public:                                  // back to public
api2() : int { return _state; }
```

Without any specifier, the default visibility is **public**.

### Access enforcement

Accessing a namespace member from outside its allowed scope is a compile-time error:

```
Error 3000E : private variable '_state' is only accessible within namespace 'mymod'; …
Error 3000E : protected function '_cache' is only accessible within the same module; …
```

---

## 6. Name lookup rules

When the compiler encounters a name, it looks it up using the following rules (innermost scope first):

1. **Local variables** of the current block (and enclosing blocks, from innermost outward).
2. **Parameters** of the enclosing function.
3. **Static local variables** of the enclosing function.
4. **Members** of `this` (if inside a non-static member function) — implicit `this.member` lookup.
5. **Enclosing struct members** (for inner/nested struct methods, outer struct members are accessible if there is an implicit `__parent__` reference).
6. **Declarations in the current namespace** (the module namespace).
7. **Declarations in enclosing namespaces**, outward to the root namespace.
8. **Imported modules** — a *qualified* name whose leading component matches an imported (or transitively imported) module is resolved against that module's exported namespace tree.

The first matching declaration wins. If no declaration is found, the compiler reports an error.

> **Step 8 is reached only for qualified names.**  A bare identifier is never
> implicitly resolved to an imported symbol.  Write `mylib::Foo` (or
> `::mylib::Foo` for the absolute form), never just `Foo`.

### Imported symbol resolution (step 8)

An `import mylib;` declaration does **not** inject names into the current
namespace.  It registers `mylib`'s exported API so that a subsequent
qualified name `mylib::Something` can be resolved.

```k
module myapp;
import math::vec;

test() : int {
    // Bare 'dot' — ERROR: 'dot' is not declared in this module
    // math::vec::dot(1, 2)    — OK: fully qualified
    // ::math::vec::dot(1, 2)  — OK: absolute form, always unambiguous
    return math::vec::dot(1, 2);
}
```

The leading-`::` form bypasses steps 1–7 and goes directly to step 8,
resolving from the root namespace.  This is the canonical way to call
an imported function without any risk of shadowing by a local declaration.

### `this` inside member functions

Inside a non-static member function, bare identifiers that match struct fields are implicitly accessed as `this.<name>`.
Explicit `this.<name>` is also valid and has the same meaning.

```k
struct Counter {
    n : int;
    increment() {
        n = n + 1;              // implicit this.n
        this.n = this.n + 1;   // explicit, same meaning
    }
}
```

### Shadowing

An inner declaration *shadows* an outer declaration with the same name.
The outer declaration can be reached via a qualified name.

```k
struct Outer {
    value : int = 10;

    struct Inner {
        value : int = 0;           // shadows Outer::value
        get() : int { return value; }              // returns Inner::value
        getOuter() : int { return Outer::value; }  // explicit access
    }
}
```

---

## 7. Scope resolution operator `::`

The `::` operator accesses a name in a specific namespace or struct scope.

**Examples:**

```k
plop::sub(43)        // call static function 'sub' of struct 'plop'
titi::b = 13;        // access static member 'b' of struct 'titi'
the::module::func()  // call function in nested namespace
Outer::Inner         // type name of nested struct
```

---

*See also:* [Lexical Conventions](lexical.md) · [Keywords](keywords.md) · [Types](types.md) · [Module System](modules.md) · [Libraries — Export and Import](libraries.md) · [Structures](../structs/structs.md#10-member-visibility)
