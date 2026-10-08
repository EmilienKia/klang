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
    public static getApplication() : OptionalRef<Application>;
    public final stdin() : io::InputStream<byte>&;
    public final stdinBytes() : io::InputStream<byte>&;
    public final stdinReader() : io::BufferedReader&;
    public final stdout() : io::TextPrinter&;
    public final stderr() : io::TextPrinter&;
    public final stdoutBytes() : io::OutputStream<byte>&;
    public final stderrBytes() : io::OutputStream<byte>&;
    public final stdoutPrint() : io::PrintStream&;
    public final stderrPrint() : io::PrintStream&;
    public final flushStandardStreams() : bool;
    public const env() : const EnvironmentMap&;
    public const applicationModule() : Shared<ApplicationModule>;
    public const linkedModules() : Vector<Shared<Module>>;
    public const linkedUnits() : Vector<UnitView>;
    public loadModule(path: const String&) : Expected<Shared<Module>, LoadModuleError>;
    public lookupModule(path: const String&) : Shared<Module>;
    public modules() : Vector<Shared<Module>>;
protected:
    setStandardInput(input: io::InputStream<byte>!);
    setStandardOutput(output: io::OutputStream<byte>!);
    setStandardError(output: io::OutputStream<byte>!);
    final decorateStandardInput(input: io::FilterInputStream<byte>!);
    final decorateStandardOutput(output: io::FilterOutputStream<byte>!);
    final decorateStandardError(output: io::FilterOutputStream<byte>!);
}
```
---
## Methods
### `getApplication() : OptionalRef<Application>`
Return an empty optional when no application entry point is active; otherwise
return a borrowed reference to the exact `Application` base subobject of the
application instance selected by the executable or JIT entry point.

The reference becomes available only after the full concrete application has
been constructed and immediately before its resolved `main` method is invoked.
It is withdrawn after `main` returns or throws, before the application
destructor runs. It is therefore empty in global constructors, the
`Application` constructor and destructor, and global destructors. Calling
`get()` requires `hasValue()` to be true.

The returned reference does not own or extend the lifetime of the application.
It is valid only while the selected entry point is executing. The active
application is process-wide rather than thread-local; application-created
threads may observe it, but must finish before `main` returns.

```k
app : OptionalRef<k::Application> = k::Application::getApplication();
if (app.hasValue()) {
    // Use app.get() only within the active main call.
}
```

### Standard streams

Each `Application` owns its standard input/output/error byte roots and their
UTF-8 and legacy printing adapters. The corresponding namespace functions in
[`io`](io.md#standard-i-o-streams) borrow the active instance's adapters and
throw `io::NoActiveApplicationError` when called outside an active entry
point. Returned references are valid only until that entry point ends.

The instance stream accessors and decoration helpers are final, non-virtual
methods. Derived constructors may use the instance accessors to borrow the
current roots and adapters before activation; namespace accessors still require
an active entry point. Stream configuration and freezing use non-virtual
installation helpers and cannot be intercepted by overriding these methods.

Derived `Application` constructors may call the protected
`setStandardInput`, `setStandardOutput`, and `setStandardError` methods to
transfer ownership of replacement roots. Null roots are rejected with
`io::StandardStreamConfigurationError`. Replacement adapters are prepared
before the existing configuration is changed; if preparation fails, the
existing roots and adapters remain in place. The compiler's entry-point
activation hook freezes configuration before `main`; later replacement
attempts throw the same error. Output adapters are flushed before the active
application is withdrawn on normal and exceptional `main` exit. A flush
failure is reported to native standard error; it changes a successful `main`
result to status 1.
During exceptional exit, flush errors are reported without replacing the
original exception. Teardown destroys adapters before their roots and does not
flush them a second time.

An application may instead call `decorateStandardInput`,
`decorateStandardOutput`, or `decorateStandardError` with a
`FilterInputStream<byte>` or `FilterOutputStream<byte>` (or a derived class)
constructed over the current root. These helpers preserve the existing standard-stream adapters'
behavior while routing operations through the decorator. The helper validates
that the decorator wraps the current root. It prepares the replacement
adapters first; output decoration also flushes the old adapters before
committing. Only then does the library transfer the previous root into the
decorator's owned inner-stream slot and install the decorator as the new root.
The ownership handoff itself does not invoke user callbacks or allocate, so
teardown keeps the entire decorator chain alive and destroys it from the
outside in. A failed preparation or flush leaves the previous root and
adapters installed.

For example, a derived application can wrap standard output to intercept
writes before forwarding them:

```k
class PrefixOutput : public io::FilterOutputStream<byte> {
    public PrefixOutput(output: io::OutputStream<byte>*)
        : FilterOutputStream(output) {}

    override write(value: byte) : io::OutputStream<byte>& {
        _out->write((byte) '>');
        _out->write(value);
        return this;
    }
}

class Application : public k::Application {
    public Application() {
        decorateStandardOutput(
            new PrefixOutput((io::OutputStream<byte>*)&*_stdout));
    }
}
```

The default roots borrow libc `stdin`, `stdout`, and `stderr`. Closing or
destroying their adapters detaches the stream objects without closing the
native handles. `FileInputStream(path)` and `FileOutputStream(path)` continue
to own and close files opened from paths.

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
corresponding `LoadModuleError` value. The `__k_rtti_manifest_v1` symbol names
the generation-1 accessor; it returns a manifest record with ABI version 2.
The accessor suffix does not mean that ABI-version-1 manifest records are
accepted.

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
images are classified from their own `__k_rtti_manifest_v1` accessor when
present. That generation-1 accessor returns a manifest record with ABI version
2; its suffix does not indicate acceptance of ABI-version-1 records. A
dependency's manifest is not used to classify its parent image. This manifest
ABI is separate from the Application anchor ABI. See
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
- Native executable entry points publish through the executable-owned v1
  Application anchor, so dynamic modules and copies of the runtime share one
  process slot. A JIT host explicitly registers with the shared runtime
  provider when it adds a `main` module and unregisters when the JIT is
  destroyed; without an executable anchor or registered JIT host, the optional
  is empty. Both mechanisms use one process slot and neither extends the
  application's lifetime.
- Legacy JIT modules that do not reference the Application activation ABI do
  not register a singleton host. A generated JIT entry point that does
  reference that ABI requires both host-registration accessors.

---

*See also:* [Module](module.md) · [Main Function](../language/basic/main.md) · [Object](object.md) · [Optional Types](optional.md) · [Collections](collections.md)
