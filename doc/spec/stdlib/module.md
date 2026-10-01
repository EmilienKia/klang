# Module
**Module:** `k`
**Source:** `libk/libk/src/module.k`, `libk/libk/src/application.k`, `libk/libk/src/runtime/module_ffi.c`
---
## Overview
`Module` represents a dynamic library (`.so`) loaded in the running process
through [`Application::loadModule()`](application.md#loadmodulepath-const-string--expectedmodule-loadmoduleerror).
It currently carries only the native library handle and the absolute real path
of the library file.

`Module` instances are created and owned by the `Application`; user code cannot
construct them (the constructor is `protected`, and only `Application` is a
`friend`).

---
## Declaration
```k
public enum LoadModuleError {
    NOT_FOUND = 0;
    ACCESS_DENIED = 1;
    INVALID_PATH = 2;
    NOT_A_LIBRARY = 3;
    MISSING_DEPENDENCY = 4;
    UNRESOLVED_SYMBOL = 5;
    LOAD_FAILED = 6;
}

public class Module : public Object {
    public const path() : const String&;
}

// In ::k::Application
public loadModule(path: const String&) : Expected<Module*, LoadModuleError>;
```

---
## Loading semantics
`loadModule()` resolves `path` to an absolute, symbolic-link-free path, then
loads the file with `dlopen(RTLD_NOW | RTLD_GLOBAL)`. On return:

- the dependencies of the library (`DT_NEEDED`) are loaded;
- every symbol is resolved (a missing one is a load error, not a later crash);
- global constructors have run, **after** those of the libraries it depends on.
  K global objects are therefore fully constructed, and a global object may
  register itself (or a value) in an object of a library it depends on, e.g. to
  chain plugins into a registry;
- the symbols of the library are visible to the libraries loaded afterwards.

The loader always receives a file path, never a bare library name: no
`LD_LIBRARY_PATH` search is performed for the requested file.

### De-duplication
Modules are keyed by their **native handle**. Loading the same file again,
through another path or a symbolic link, returns the same `Module`. A library
that is already loaded in the process without having been requested through
this `Application` (e.g. `libk.so`, or a dependency of the executable) gets a
dedicated `Module` the first time it is requested, and global constructors are
**not** run again.

### Lifetime
The returned `Module*` stays valid until the `Application` is destroyed. A
module is never unloaded: its handle stays open until the process exits, and
its global destructors run at process exit.

### Errors
No exception is thrown for a load failure; `loadModule()` returns an `Expected`
holding a `LoadModuleError`:

| Value | Meaning |
|---|---|
| `NOT_FOUND` | The file does not exist. |
| `ACCESS_DENIED` | The file (or a path component) is not accessible. |
| `INVALID_PATH` | Path too long, symbolic-link loop, or otherwise invalid. |
| `NOT_A_LIBRARY` | The file exists but is not a loadable shared library (not an ELF, wrong architecture, a directory…). |
| `MISSING_DEPENDENCY` | A library the module depends on cannot be found. |
| `UNRESOLVED_SYMBOL` | A symbol required by the module cannot be resolved. |
| `LOAD_FAILED` | Any other failure. |

The classification of `dlopen` failures relies on glibc messages; unrecognised
ones map to `LOAD_FAILED`. The numeric values are shared with the C runtime
(`runtime/module_ffi.c`).

### Thread safety
`loadModule()` is thread-safe (the load, the de-duplication lookup and the
registration are performed under a reentrant lock, so a global constructor may
itself call `loadModule()` on the same thread). A `Module` is immutable.

---
## Example
```k
import modreg;   // library owning a plugin registry

main() : int {
    r : Expected<Module*, LoadModuleError> = loadModule(String("./libmyplugin.so"));
    if (r.hasError()) {
        return 1;
    }
    m : Module* = r.getResult();
    // m->path() is the absolute real path of the library.
    return 0;
}
```

---
## Design Notes
- The result is `Expected<Module*, …>` rather than `Expected<Module&, …>`:
  a reference-typed `Expected` alternative cannot be re-bound after
  construction, so `Expected` can only carry references through a pointer.
  The pointer is never null on success.
- `Application` keeps the loaded modules in an owning singly linked list keyed
  by handle: the standard collections cannot hold owner (`!`) values.
- `Module`'s constructor and fields are `protected` rather than `private`
  because friendship grants access to protected members only.
- Not provided yet: symbol lookup, unloading.

---

*See also:* [Application](application.md) · [Expected](expected.md)
