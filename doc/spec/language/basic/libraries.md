````markdown
# Libraries — Export and Import

[← Index](../index.md) · [Module System](modules.md) · [Names and Lookup](names.md)

This page describes how K modules are compiled into libraries, what is exported,
and how consumers import and use the exported API.

---

## Contents

1. [Producing a library](#1-producing-a-library)
2. [What is exported — visibility and the KDI file](#2-what-is-exported--visibility-and-the-kdi-file)
3. [Importing a library](#3-importing-a-library)
4. [Using imported symbols](#4-using-imported-symbols)
   - 4.1 [Global functions](#41-global-functions)
   - 4.2 [Global variables](#42-global-variables)
   - 4.3 [Aggregate types (struct / class / interface)](#43-aggregate-types-struct--class--interface)
   - 4.4 [Member functions and constructors](#44-member-functions-and-constructors)
   - 4.5 [Static members](#45-static-members)
5. [Inheriting from imported aggregates](#5-inheriting-from-imported-aggregates)
6. [Transitive dependencies](#6-transitive-dependencies)
7. [Name lookup for imported symbols](#7-name-lookup-for-imported-symbols)
8. [Private members — obfuscation in the KDI](#8-private-members--obfuscation-in-the-kdi)
9. [Worked example](#9-worked-example)

---

## 1. Producing a library

A K source file that has **no `main` function** is automatically compiled as a
shared library (`.so`) by **klangc**.  The module name determines the output
file name: `::` separators are replaced by `.`, and the `lib` prefix and `.so`
suffix are added.

```
module math::vec;         →  libmath.vec.so
module my::deep::lib;     →  libmy.deep.lib.so
```

Alongside the binary, **klangc** always generates a **KDI file** (`.kdi`) with
the same stem.  The KDI file is a CBOR binary that describes every public and
protected API entity of the module — the information a consumer needs to import
and use the library without the original source.

Explicit production flags:

| Invocation | Output |
|---|---|
| `klangc mylib.k` (no `main`) | `libmylib.so` + `libmylib.kdi` |
| `klangc --dyn-lib mylib.k` | `libmylib.so` + `libmylib.kdi` |
| `klangc --static-lib mylib.k` | `libmylib.a` + `libmylib.kdi` |
| `klangc --dyn-lib --static-lib mylib.k` | `libmylib.so` + `libmylib.a` + `libmylib.kdi` |

See **klangc(1)** for the full set of options (`-o`, `--no-emit-kdi`,
`--emit-kdi-json`, etc.).

---

## 2. What is exported — visibility and the KDI file

The KDI file records **public** and **protected** entities only.  **Private**
members are *obfuscated*: they appear as opaque size-only blocks in the layout
description so that subclass layout is correct without revealing implementation
details.

### Namespace-level entities

| Visibility | Exported to `.kdi`? | Accessible by consumers? |
|---|---|---|
| `public` (default) | ✓ Yes | ✓ Yes |
| `protected` | ✓ Yes | Within the same root namespace only |
| `private` | ✗ No | ✗ No |

### Struct / class / interface members

| Visibility | Exported to `.kdi`? | Accessible by consumers? |
|---|---|---|
| `public` | ✓ Full layout + symbol | ✓ Yes |
| `protected` | ✓ Full layout + symbol | Sub-classes only |
| `private` | Opaque size block only | ✗ No |

The opaque size block preserves the physical struct layout so that a
consumer class inheriting from the exported type can be compiled with the
correct field offsets, without knowing what the private fields contain.

**Implication for library design:** If the private section of a struct changes
size or layout, consumers must recompile — the ABI is not stable across such
changes.  Making all private fields the same size (`int64` padding) or using
the *pimpl* pattern is recommended for stable ABI.

---

## 3. Importing a library

An `import` declaration at the top of a source file (after `module`) makes a
compiled module's API available:

```k
module myapp;
import math::vec;
```

**klangc** searches for `math.vec.kdi` (description) and `libmath.vec.so`
(binary) in this order:

1. Explicit paths from `-i math::vec=/path/to/math.vec.kdi` (highest priority).
2. Current working directory.
3. Directories added with `-I <dir>` (in order).
4. Directories in the `KLANG_LIB_PATH` environment variable.
5. System KDI directories: `/usr/local/lib/kdi`, `/usr/lib/kdi`, `/usr/lib/<platform>/kdi`.
6. System library directories: `/usr/local/lib`, `/usr/lib`, `/usr/lib/<platform>`.

A module that cannot be resolved is a **fatal compilation error** — there is no
mechanism for optional imports.

An `import` declaration whose symbols are never referenced produces
**warning `0x80010`** (emitted after all resolver passes).  Remove the unused
`import` to suppress it.

Cycles in the import graph (A depends on B which depends on A) produce
**error `0x80003`** with the full cycle path: `A → B → A`.  Cycles can only
arise from hand-crafted or corrupted `.kdi` files.

Multiple libraries may be imported in the same file; each must have a unique
root namespace component:

```k
module myapp;
import math::vec;    // root: math
import io::stream;   // root: io    — OK
// import math::mat; // ERROR: root 'math' already taken by 'math::vec'
```

---

## 4. Using imported symbols

All imported symbols live under their original module namespace and are
**always accessed by their fully qualified name**.  An `import` statement
does **not** inject names into the current namespace (there is no `using`
or wildcard import).

### 4.1 Global functions

```k
module myapp;
import math::vec;

main() : int {
    result : int = math::vec::dot(3, 4);  // fully qualified call
    return result;
}
```

The absolute form (leading `::`) is also valid and unambiguous:

```k
    result : int = ::math::vec::dot(3, 4);
```

### 4.2 Global variables

```k
module myapp;
import physics::constants;

main() : int {
    g : int = physics::constants::GRAVITY;
    return g;
}
```

### 4.3 Aggregate types (struct / class / interface)

A type from an imported module is referenced by its fully qualified name.
Variables of that type can be declared, passed as parameters, and returned
from functions:

```k
module myapp;
import shapes;

draw(s: shapes::Shape&) : int {
    return s.area();
}

main() : int {
    sq : shapes::Square(5);      // construct imported type
    return draw(sq);
}
```

### 4.4 Member functions and constructors

Imported constructors and member functions are called using the normal `.`
syntax (or `->` for pointers) once a variable of the imported type is
declared:

```k
obj : mylib::MyClass(arg1, arg2);  // constructor
val : int = obj.method();          // member call
obj.set_field(42);                 // mutating member
```

### 4.5 Static members

Static member functions and variables are accessed via the `::` scope
resolution operator on the qualified type name:

```k
count : int = mylib::MyClass::static_count();
mylib::MyClass::reset();
```

---

## 5. Inheriting from imported aggregates

A local struct, class, or interface may inherit from an imported aggregate.
The KDI file supplies all necessary layout information (field offsets, vtable
slots, base sub-object positions) so the compiler can generate correct code.

### Inheriting from an imported struct

```k
module local_mod;
import geom;

struct MyPoint : public geom::Point {
    z : int;
    MyPoint(x: int, y: int, zv: int) : geom::Point(x, y), z(zv) {}
    sum() : int { return this.x + this.y + this.z; }
}
```

### Implementing an imported interface

```k
module local_mod;
import shapes;

class MyCircle : public shapes::IShape {
    radius : int;
    MyCircle(r: int) : radius(r) {}
    area()      : int { return radius * radius; }  // implements shapes::IShape
    perimeter() : int { return 6 * radius; }
    sides()     : int { return 0; }
}
```

### Extending an imported class

```k
module local_mod;
import animals;

class GuideDog : public animals::Dog {
    handler : int;
    GuideDog(v: int, h: int) : animals::Dog(v), handler(h) {}
    guide() : int { return handler; }
    speak() : int { return handler * 10; }  // override virtual method
}
```

**Private member obfuscation and layout:**  
When inheriting from a type that has private fields, those fields appear as
opaque padding in the KDI.  The derived class is allocated the correct total
size, and the base sub-object offset is correct — the derived class simply
cannot name or access those fields directly.

**Rules:**

- A local `struct` may only inherit from an imported `struct` (not class/interface).
- A local `class` may inherit from imported `class` or `interface`.
- A local `interface` may only inherit from imported `interface`.
- Cross-type inheritance (e.g. local struct inheriting imported class) is
  a compile-time error (error 30035).

---

## 6. Transitive dependencies

When the module being compiled imports `B`, and `B` was itself compiled with an
`import C;` declaration, then `C` is a *transitive dependency*.  **klangc**
loads all transitive KDIs automatically from the `header.dependencies` field
embedded in each KDI.

The consumer **does not** need to write `import C;` explicitly, even if it uses
types defined in `C` that are referenced through `B`'s API.

```k
// B's .kdi declares: dependencies: ["C"]
// klangc loads C.kdi automatically when processing "import B"

module myapp;
import B;                   // klangc also loads C transitively

use_b(b: B::Wrapper&) : int {
    return b.get_c_value();  // C::Value type is resolved via the transitive KDI
}
```

A **missing transitive KDI** is a fatal compilation error.  The search
order for transitive KDIs is identical to the order for direct imports.

---

## 7. Name lookup for imported symbols

Imported symbols are **not** injected into the current module's namespace.
They are resolved only when the compiler encounters a *qualified name* whose
leading component matches an imported module name.

The full lookup order is:

1. Local variables (innermost block outward).
2. Function parameters.
3. `this` members (inside non-static member functions).
4. Enclosing struct members (nested struct access).
5. Declarations in the current module namespace.
6. Declarations in enclosing namespaces, up to root.
7. **Imported modules** — a qualified name `mod::name` resolves against all
   loaded imported modules (direct and transitive) whose namespace prefix
   matches `mod`.

Step 7 is reached **only for qualified names** — a bare identifier `name`
never resolves to an imported symbol.  This is intentional: it prevents
accidental name capture across module boundaries.

The leading-`::` form (`::mod::name`) bypasses steps 1–6 and goes directly
to step 7, making the intent unambiguous:

```k
::math::vec::dot(a, b)     // unambiguous: always resolves from root
math::vec::dot(a, b)       // equivalent when 'math' is not a local name
```

---

## 8. Private members — obfuscation in the KDI

Private struct/class/interface members are **not** stored in the KDI.
In their place, the KDI contains one or more `opaque_block` layout entries
that record only:

- The LLVM field index of the first hidden field in the block.
- The number of consecutive LLVM fields covered.
- The total bit-size of the block.

This is sufficient for the consumer compiler to:

- Compute the correct total size of the imported aggregate.
- Lay out base sub-objects correctly inside derived types.
- Generate correct vtable slot assignments.

But the consumer **cannot**:

- Read or write the private fields by name.
- Know the count, names, or types of the individual private fields.
- Rely on the stability of the opaque block layout across library versions.

---

## 9. Worked example

**Library: `shapes` (libshapes.so + libshapes.kdi)**

```k
module shapes;

interface IShape {
    area()      : int;
    perimeter() : int;
}

class Rectangle : public IShape {
    width  : int;
    height : int;
    Rectangle(w: int, h: int) : width(w), height(h) {}
    area()      : int { return width * height; }
    perimeter() : int { return 2 * (width + height); }
private:
    _cached_area : int = 0;   // private — appears as opaque block in .kdi
}

make_rect(w: int, h: int) : Rectangle {
    r : Rectangle(w, h);
    return r;
}
```

Compile:

```sh
klangc shapes.k
# produces: libshapes.so  libshapes.kdi
```

---

**Consumer: `myapp` (executable)**

```k
module myapp;
import shapes;

total_area(s: shapes::IShape&) : int {
    return s.area();
}

main() : int {
    r : shapes::Rectangle(3, 4);    // construct imported class
    a : int = total_area(r);        // dispatch via IShape& — virtual call
    return a;                       // → 12
}
```

Compile and link:

```sh
klangc -I . -L . myapp.k -o myapp
./myapp       # exits with code 12
```

---

**Extending the library locally:**

```k
module myext;
import shapes;

class Square : public shapes::Rectangle {
    Square(s: int) : shapes::Rectangle(s, s) {}
    is_square() : int { return 1; }
}

main() : int {
    sq : Square(5);
    a  : int = sq.area();   // virtual — Rectangle::area → 25
    return a;
}
```

---

*See also:* [Module System](modules.md) · [Names and Lookup](names.md) ·
[Inheritance](../structs/inheritance.md) · [klangc(1)](../../../../doc/man/klangc.md) ·
[KDI Schema](../../../../doc/spec/kdi/kdi-schema-abstract.md)
````

