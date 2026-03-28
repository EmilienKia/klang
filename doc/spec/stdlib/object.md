# Object
**Module:** `k`  
**Source:** `libk/libk/src/object.k`
---
## Overview
`Object` is the root base class for all K classes.  It provides a minimal
common interface that every class in K can rely on.
The compiler auto-links the base standard library (module `::k`) with every
K program or library, so `Object` is always available without an explicit
import.
---
## Declaration
```k
const class Object {
public:
    const hash() : int { return 0; }
    final const getClass() : const Class&;
}
```
---
## Methods
### `hash() : int`
Return a hash code for this object.
The default implementation returns `0`.  Subclasses should override this
method to provide a meaningful hash based on the object's identity or content.
**Returns:** An integer hash code.

### `getClass() : const Class&`
Return the [`Class`](rtti.md#5-class) RTTI descriptor for the concrete
(most-derived) class of this object.

This method is declared `final` — it cannot be overridden. It reads the
object's vtable slot 0 (the RTTI pointer) to identify the concrete type
at runtime. The returned `Class` reference provides access to the type's
name, fully qualified name, base types, nested types, visibility, and
annotations.

**Returns:** A reference to the `k::Class` descriptor for the concrete type.

**Example:**
```k
import k;

test() : int {
    s : k::String("hello");
    name : k::String(s.getClass().getName());
    expected : k::String("String");
    if (name == expected) return 42;
    return 0;
}
```
---
## Design Notes
- `Object` is intentionally minimal.  Future versions may add methods such as
  `equals()` or `toString()`.
- Because `Object` is a **class** (not a struct), it carries a vtable pointer,
  enabling virtual dispatch on `hash()` and RTTI via `getClass()`.
- `Object` is declared `const` — all its methods are implicitly `const`.

---

*See also:* [RTTI Types](rtti.md) · [Annotations](../language/annotations/annotations.md) · [Classes — RTTI](../language/structs/classes.md#14-rtti-and-dynamic-downcast)
