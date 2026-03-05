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

---

## Dynamic indirection downcast (class/interface only)

When a `Base*` (or `Base~`, `Base^`, `Base&`) indirection may point at a `Derived` object at
runtime, K allows assigning it to a `Derived*` (or `Derived~`, `Derived^`, `Derived&`) via a
**runtime RTTI check**.

This applies **only to `class` and `interface` types** — structs have no vtable/RTTI and
attempting a dynamic downcast on struct pointers is a **compile-time error**.

**Semantics:**

1. The compiler loads the RTTI pointer from the object's vtable slot 0.
2. It compares the loaded RTTI pointer with the RTTI descriptor of `Derived`.
3. On match: the raw pointer is adjusted (byte-offset subtraction) to point to the start of the
   `Derived` sub-object; the result is assigned.
4. On mismatch: **null** is assigned.
5. Null assigned to a non-null target (`~` or `&`) immediately calls `__fatal_null_dyncast()`.

**Binding constraints:**

| Target type | When allowed | On RTTI mismatch |
|-------------|--------------|-----------------|
| `Derived&`  | Init only (immutable binding) | fatal trap |
| `Derived~`  | Init only (non-null link) | fatal trap |
| `Derived^`  | Init only (nullable pin) | null assigned |
| `Derived*`  | Init and rebind (nullable ptr) | null assigned |

**Examples:**

```k
class Animal {
    public Animal(v : int) : name_code(v) {}
    public speak() : int { return name_code; }
    public name_code : int;
}
class Dog : public Animal {
    public Dog(v : int) : Animal(v), tricks(v * 2) {}
    public speak() : int { return tricks; }
    public get_tricks() : int { return tricks; }
    public tricks : int;
}

tricks_fn(d : Dog&) : int { return d.get_tricks(); }

test() : int {
    d   : Dog(7);
    al  : Animal~ = &d;      // static upcast to Animal~
    dl  : Dog~    = al;      // dynamic downcast; traps if RTTI mismatches
    return tricks_fn(*dl);   // → 14
}
```

**Transitive downcast** (e.g. `ptr<C>` from `ptr<A>` where `C→B→A`) is fully supported:
the byte offset through the entire inheritance chain is computed at compile time and subtracted
from the source pointer at runtime when RTTI matches.

**Interface downcast:**

A pointer to an interface can be dynamically downcast to a pointer to a concrete implementing class:

```k
interface IBase { get_val() : int; }
class Derived : public IBase {
    public val : int;
    public Derived(v : int) : val(v) {}
    public get_val() : int { return val; }
}

test() : int {
    d  : Derived(21);
    ip : IBase*   = &d;      // static upcast to IBase*
    dp : Derived* = ip;      // dynamic downcast via RTTI → non-null
    return dp->get_val();    // → 21 (if supported)
}
```

*See also:* [Types — §11.4](../basic/types.md#114-dynamic-indirection-downcast-classinterface) · [Classes — §14](classes.md#14-rtti-and-dynamic-downcast)

