# Inheritance
Both `struct` and `class` support single and multiple inheritance.
The key difference: **only `class` supports virtual dispatch and virtual base classes**.
An `interface` may only inherit from other interfaces.

## struct inheritance — pure aggregation
A `struct` has no vtable and no virtual dispatch. Calling through a base reference always calls Base's own implementation.
| Rule | Result |
|------|--------|
| `struct : public struct` | Allowed |
| `struct : public class` | Error 30035 — forbidden |
| `struct : public interface` | Error 30035 — forbidden |

## class inheritance — virtual dispatch
Every non-static, non-private class method is automatically virtual. Dispatch through a base reference calls the most-derived override.

All class bases are **implicitly virtual** — there is no `virtual` keyword in the base clause.
This ensures diamond-safe inheritance automatically (only one shared copy of each base in a diamond).

| Rule | Result |
|------|--------|
| `class : public class` | Allowed (base is implicitly virtual) |
| `class : public interface` | Allowed — class implements the interface |
| `class : public struct` | Error 30035 — forbidden |

## interface inheritance
An interface may inherit from other interfaces, creating a sub-interface that accumulates method contracts.
| Rule | Result |
|------|--------|
| `interface : public interface` | Allowed |
| `interface : public class` | Error — forbidden |
| `interface : public struct` | Error — forbidden |

*See also:* [Interfaces](interfaces.md)

## Diamond — struct (two independent copies)
A struct diamond creates **two independent** base sub-objects (no sharing).

## Diamond — class (one shared virtual base)
Because all class bases are implicitly virtual, a class diamond always creates **one** shared copy
of the common base in the most-derived class.
The most-derived class is responsible for constructing and destructing the shared base (exactly once).

## Cross-inheritance is forbidden
struct may only inherit from struct; class only from class or interface; interface only from interface. Mixing otherwise is Error 30035.

## final and const rules
- A `final` struct/class/interface cannot be used as a base (Error 30012).
- A `const struct` may only inherit from other `const struct`s (Error 30033).
- A mutable struct may inherit from a `const struct`.

## Static indirection upcast

When `Derived` inherits from `Base`, an indirection (`&`, `~`, `^`, `*`) to `Derived` can be
implicitly assigned to an indirection to `Base`. The compiler inserts a compile-time GEP
adjustment to address the `Base` sub-object within the `Derived` object.

```k
struct Animal { legs : int; Animal(n : int) : legs(n) {} }
struct Dog : public Animal { Dog(n : int) : Animal(n) {} }

use() {
    d : Dog(4);

    r   : Animal& = d;      // ref<Base> — immutable binding
    lnk : Animal~ = &d;     // lien<Base> — mutable binding
    p   : Animal^ = &d;     // pin<Base>  — immutable binding, nullable
    ptr : Animal* = &d;     // ptr<Base>  — mutable binding, nullable

    // Rebind (link and ptr only):
    d2 : Dog(2);
    lnk = &d2;     // OK: link can rebind
    ptr = &d2;     // OK: ptr can rebind
    // r = d2;     // ERROR: ref cannot rebind
    // p = &d2;    // ERROR: pin cannot rebind
}
```

**Rules and error codes:**

| Situation | Result |
|-----------|--------|
| `Derived&` → `Base&` (init) | OK — compile-time GEP |
| `Derived~` → `Base~` (init or rebind) | OK |
| `Derived^` → `Base^` (init) | OK |
| `Derived*` → `Base*` (init or rebind) | OK |
| Nullable source (`^` or `*`) → non-null target (`~` or `&`) | Warning 0x4505 + runtime null-check |
| Types have no inheritance relationship | Compile error (0x4005 / 0x4506 / 0x4605 / 0x4700) |
| Rebinding an immutable indirection (`ref`, `pin`) | Compile error |

*See also:* [Types — §11.3](../basic/types.md#113-static-indirection-upcast-aggregate-types)
