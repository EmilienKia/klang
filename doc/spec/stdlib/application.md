# Application
**Module:** `k`
**Source:** `libk/libk/src/application.k`
---
## Overview
`Application` is the abstract base class for every K program's entry-point
object. The compiler links every K program's `main` function (or explicit
`class Application`) into a subclass of `::k::Application`, instantiates it
once at process start-up, and invokes its resolved `main` method.

See the language reference [Main Function](../language/basic/main.md) for the
full entry-point mechanism (implicit `main`, user-declared `class
Application`, and multi-level application entry-point chains).
---
## Declaration
```k
public abstract class Application : public Object {
    public const env() : const EnvironmentMap&;
}
```
---
## Methods
### `env() : const EnvironmentMap&`
Return a read-only view of the process environment variables.

The map is populated once, in the `Application` constructor, before any user
code (including the resolved `main` method) runs. Keys and values are the raw
UTF-8 strings provided by the OS.

**Returns:** A constant reference to the process's [`EnvironmentMap`](#environmentmap).

**Example:**
```k
import k;

main() : int {
    e : const EnvironmentMap& = env();
    v : OptionalConstRef<String> = e.get(String("HOME"));
    if (v.hasValue()) return 0;
    return 1;
}
```

---
## EnvironmentMap
A minimal, read-only key/value map of `String`s, returned by `env()`.

```k
public final class EnvironmentMap : public Object {
    public const size() : unsigned int;
    public const containsKey(key: const String&) : bool;
    public const get(key: const String&) : OptionalConstRef<String>;
}
```

### `size() : unsigned int`
Number of environment variable entries.

### `containsKey(key: const String&) : bool`
True if `key` is present among the process environment variables.

### `get(key: const String&) : OptionalConstRef<String>`
Look up `key`, returning an empty [`OptionalConstRef`](optional.md) if absent.

---
## Design Notes
- `Application` cannot be instantiated directly; a concrete subclass must
  provide (directly or through an entry-point chain — see the language
  reference) exactly one usable `main` method.
- `Application` is a **class** (not a struct), carrying a vtable pointer, so
  that an entry-point chain of abstract subclasses can each override or
  delegate `main` through ordinary virtual dispatch.
- `EnvironmentMap` uses linear lookup over two parallel `Vector<String>`
  buffers rather than the generic `Map<K,V>` hierarchy. Environment variable
  lists are always small in practice (well under a hundred entries), so this
  is not a performance concern.

---

*See also:* [Main Function](../language/basic/main.md) · [Object](object.md) · [Optional Types](optional.md) · [Collections](collections.md)
