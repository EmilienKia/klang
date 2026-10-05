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
    public const applicationModule() : Shared<ApplicationModule>;
    public const linkedModules() : Vector<Shared<Module>>;
    public const linkedUnits() : Vector<UnitView>;
    public loadModule(path: const String&) : Expected<Shared<Module>, LoadModuleError>;
    public lookupModule(path: const String&) : Shared<Module>;
    public modules() : Vector<Shared<Module>>;
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

### `applicationModule() : Shared<ApplicationModule>`
Return the shared representation of the current process's executable image.
The returned handle is a copy, so callers may retain it independently of the
`Application` object.

The accessor uses this name because `module` is a K keyword and the language
does not support escaped or contextual member identifiers.

### `ApplicationModule.linkedModules() : Vector<Shared<Module>>`
Return the stable startup snapshot of native images loaded when `Application`
was constructed. This includes the executable and already-loaded dependencies.
Images loaded later with `loadModule()` are not added to this snapshot.
`Application.linkedModules()` delegates to this method. The
`ApplicationModule` retains the other startup module handles, so the snapshot
remains queryable while a caller retains the returned `Shared<ApplicationModule>`
even after the `Application` object is destroyed.

### `ApplicationModule.linkedUnits() : Vector<UnitView>`
Return owner-retaining views of the K units directly contained in startup
images. A unit view keeps its owning image handle alive. Descriptor references
obtained from `UnitView.getDescriptor()` and navigated from it are borrowed;
keep the view alive while using them. A JIT host without a K manifest
contributes no application-image unit descriptors.
`Application.linkedUnits()` delegates to this method.

Constructing an `Application` throws an unchecked `ModuleManifestError` when a
startup image has an invalid or incompatible K RTTI manifest. Its code is the
corresponding `LoadModuleError` value.

### `loadModule(path: const String&) : Expected<Shared<Module>, LoadModuleError>`
Load a dynamic library from a local file path. See [Module](module.md) for the
full contract (dependencies, global constructors, de-duplication, errors).

**Returns:** the (non-null) [`Module`](module.md) on success, or a
[`LoadModuleError`](module.md#loadmoduleerror) describing the failure. No
exception is thrown for a load failure.

### `lookupModule(path: const String&) : Shared<Module>`
Return an already registered module matching the canonical path, or an empty
handle if none is registered. This does not load the image.

### `modules() : Vector<Shared<Module>>`
Return a stable snapshot of the application image and all currently registered
shared images. Entries share ownership with the registry; modifying the
returned vector does not change the registry.

The process image is discovered during startup, and already-loaded dynamic
images are classified from their own v1 K RTTI manifest when present. A
dependency's manifest is not used to classify its parent image. See
[Module](module.md) for manifest validation, static archive extraction, image
residency, and `UnitView` lifetime rules.

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

*See also:* [Module](module.md) · [Main Function](../language/basic/main.md) · [Object](object.md) · [Optional Types](optional.md) · [Collections](collections.md)
