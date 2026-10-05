# Module
**Module:** `k`
**Source:** `libk/libk/src/module.k`, `libk/libk/src/application.k`, `libk/libk/src/runtime/module_ffi.c`
---
## Overview
`Module` represents a native image in the running process. The abstract base
exposes the image path, concrete kind, and process-residency policy. Module
objects are created by `Application`; the application registry and callers
share ownership of the same object.

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
    INVALID_MANIFEST = 7;
    INCOMPATIBLE_MANIFEST = 8;
}

public enum ModuleKind {
    APPLICATION = 0;
    LIBRARY = 1;
    NATIVE_LIBRARY = 2;
}

public enum ModuleResidency {
    PROCESS_PINNED = 0;
    POTENTIALLY_RELEASABLE = 1;
}

public abstract class Module : public Object {
    public const kind() : ModuleKind;
    public const residency() : ModuleResidency;
    public const isProcessPinned() : bool;
    public const path() : const String&;
    public const units() : Vector<UnitView>;
    public const dependencies() : Vector<Shared<Module>>;
}

public final class ApplicationModule : public Module {
    public const linkedModules() : Vector<Shared<Module>>;
    public const linkedUnits() : Vector<UnitView>;
}
public final class LibraryModule : public Module;
public final class NativeLibraryModule : public Module;

public final class UnitView {
    public const owner() : Shared<Module>;
    public const isNull() : bool;
    public const getDescriptor() : const Unit&;
    public const rootNamespace() : NamespaceView;
}

public final class NamespaceView {
    public const owner() : Shared<Module>;
    public const isNull() : bool;
    public const getDescriptor() : const Namespace&;
    public const children() : Vector<NamespaceView>;
    public const parent() : NamespaceView;
}

public final class ModuleManifestError : FatalError;

// In ::k::Application
public const applicationModule() : Shared<ApplicationModule>;
public const linkedModules() : Vector<Shared<Module>>;
public const linkedUnits() : Vector<UnitView>;
public loadModule(path: const String&) : Expected<Shared<Module>, LoadModuleError>;
public lookupModule(path: const String&) : Shared<Module>;
public modules() : Vector<Shared<Module>>;
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
Modules are keyed by the loader's process-local image identity. Loading the same
file again through another path or symbolic link returns another
`Shared<Module>` handle in the same ownership group. A library that is already
loaded in the process without having been requested through this `Application`
(e.g. `libk.so`, or a dependency of the executable) is registered on first
request; global constructors are **not** run again.

### Lifetime and residency
The application registry keeps shared handles to its modules, and returned
handles may outlive the `Application`. Native images currently remain resident
until process exit: there is no unload operation, regardless of the policy
reported by `residency()`. Startup images, including the executable and its
startup-linked set, are `PROCESS_PINNED`. Images first acquired dynamically,
including newly discovered dependencies, are `POTENTIALLY_RELEASABLE` unless
they were already part of the pinned startup set. This is a policy distinction,
not a promise that an image can currently be unloaded. `isProcessPinned()` is
true exactly for `PROCESS_PINNED`.

At application startup, `Application.applicationModule()` represents the current
executable image as an `ApplicationModule`. It is included in the module
registry independently of dynamic-library metadata. A startup snapshot also
registers already-loaded native images, including images loaded as dependencies.
`ApplicationModule.linkedModules()` and `linkedUnits()` (also exposed as
delegating `Application` methods) are stable snapshots of images and K units
present at startup; they do not grow when `loadModule()` later loads new images.
The `ApplicationModule` retains strong handles to the other modules in this
snapshot, so a caller retaining its shared handle can still query the snapshot
after the `Application` object is destroyed. The executable is returned through
the module's self-reference and is not stored as a strong self-edge.
The current process image can be a JIT host with no K unit manifest, so these
snapshots include only descriptors actually contributed by loaded ELF images.

`ModuleKind` describes image contents, independently of the residency policy:
the executable is an `ApplicationModule`, an image with its own valid K RTTI
manifest is a `LibraryModule`, and an image without its own manifest is a
`NativeLibraryModule`. A dependency's manifest is never attributed to the
requested image. Malformed or unsupported manifests cause explicit
`INVALID_MANIFEST` or `INCOMPATIBLE_MANIFEST` errors when that image is loaded.
An empty manifest is valid.

`Module.dependencies()` returns strong handles to the direct ELF `DT_NEEDED`
images of that module, not transitive dependencies or every registered image.
Dependency edges are held weakly by modules to avoid ownership cycles.

`Module.units()` returns views of only the K units directly contained in that
image. It excludes dependency units. `UnitView` retains a shared handle to its
owning module; its `getDescriptor()` result is borrowed and must not outlive the
view. `UnitView.rootNamespace()` and `NamespaceView.children()` return views
that independently retain the same owning module. Namespace and declaration
descriptors obtained from a view remain borrowed and must not outlive that
view. Empty default-constructed views report `isNull()`; descriptor access on
an empty view throws `NullDereferenceError`. Descriptors are never wrapped in
independent `Shared` ownership.

The Linux v1 image-manifest ABI is an immutable array of
`KRTTIManifestEntryV1` records in the ELF section `k_rtti_units`. Each record
contains an ABI version, record size, and pointer to a compiler-emitted `Unit`
descriptor. The image-local `__k_rtti_manifest_v1` accessor returns the
section's entry range. Compiler objects contribute records independently;
archives retain contributions with their object members, so ordinary static
link extraction rules remain unchanged: extracted members are discoverable,
unextracted members are not. The runtime validates record ownership, image-section bounds, alignment, ABI,
and the exact v1 record size before exposing descriptors. This ABI is versioned
separately from the KDI format. Constructing `Application` fails with an
unchecked `ModuleManifestError` whose code is `INVALID_MANIFEST` or
`INCOMPATIBLE_MANIFEST` if any startup image has such a manifest.
`loadModule()` reports the same condition in its `Expected` error result for
the requested image or any newly discovered dependency. A rejected dynamic
acquisition closes its temporary loader reference and does not publish any
modules from that acquisition.

`lookupModule(path)` returns an existing registered image by its canonical
path, or an empty shared handle when no matching image is registered. It never
loads an image. `modules()` returns a stable vector snapshot; its entries share
ownership with the corresponding registry entries, and changes to the returned
vector do not mutate the registry.

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
| `INVALID_MANIFEST` | The requested image or one of its newly discovered dependencies has a malformed K RTTI manifest. |
| `INCOMPATIBLE_MANIFEST` | The requested image or one of its newly discovered dependencies advertises an unsupported manifest version. |

The classification of `dlopen` failures relies on glibc messages; unrecognised
ones map to `LOAD_FAILED`. The numeric values are shared with the C runtime
(`runtime/module_ffi.c`).

### Thread safety
`loadModule()` is thread-safe. The native loader and image discovery run outside
the module-registry lock; registration and de-duplication are protected by a
runtime mutex. A `Module` is immutable after registration. The runtime does not promise safe
reentrant module loading from arbitrary global constructors.

---
## Example
```k
import modreg;   // library owning a plugin registry

main() : int {
    r : Expected<Shared<Module>, LoadModuleError> = loadModule(String("./libmyplugin.so"));
    if (r.hasError()) {
        return 1;
    }
    m : Shared<Module> = r.getResult();
    // m.get()->path() is the absolute real path of the library.
    return 0;
}
```

---
## Design Notes
- The result is `Expected<Shared<Module>, …>` so that the caller and registry
  can share the module's ownership group while preserving its concrete
  destruction type.
- `Module`'s constructor and fields are `protected` rather than `private`
  because friendship grants access to protected members only.
- Not provided yet: symbol lookup, unloading.

---

*See also:* [Application](application.md) · [Expected](expected.md)
