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
class Object {
public:
    hash() : int { return 0; }
}
```
---
## Methods
### `hash() : int`
Return a hash code for this object.
The default implementation returns `0`.  Subclasses should override this
method to provide a meaningful hash based on the object's identity or content.
**Returns:** An integer hash code.
---
## Design Notes
- `Object` is intentionally minimal.  Future versions may add methods such as
  `equals()`, `toString()`, or `typeName()`.
- Because `Object` is a **class** (not a struct), it carries a vtable pointer,
  enabling virtual dispatch on `hash()` and future virtual methods.
