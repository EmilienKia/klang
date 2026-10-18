/*
 * K Language standard library — dynamically loaded modules
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

module k;

/**
 * Reason why ::k::Application::loadModule() could not load a library.
 *
 * The numeric values are shared with the C runtime (runtime/module_ffi.c)
 * and MUST be kept in sync with it.
 */
public enum LoadModuleError {
    /** The file does not exist. */
    NOT_FOUND = 0;
    /** The file (or a path component) cannot be accessed. */
    ACCESS_DENIED = 1;
    /** The path is invalid (empty, too long, symbolic link loop). */
    INVALID_PATH = 2;
    /** The file exists but is not a loadable shared library. */
    NOT_A_LIBRARY = 3;
    /** A library this module depends on could not be found. */
    MISSING_DEPENDENCY = 4;
    /** A symbol required by the module could not be resolved. */
    UNRESOLVED_SYMBOL = 5;
    /** Any other loading failure. */
    LOAD_FAILED = 6;
    /** The image exports a malformed or incomplete K RTTI manifest. */
    INVALID_MANIFEST = 7;
    /** The image exports a manifest ABI version this runtime cannot read. */
    INCOMPATIBLE_MANIFEST = 8;
}

/**
 * The kind of native image represented by a Module.
 */
public enum ModuleKind {
    APPLICATION = 0;
    LIBRARY = 1;
    NATIVE_LIBRARY = 2;
}

/**
 * Residency classification of a loaded native image.
 *
 * POTENTIALLY_RELEASABLE describes the image's classification, not an unload
 * capability: the current public API keeps loaded images resident until exit.
 */
public enum ModuleResidency {
    PROCESS_PINNED = 0;
    POTENTIALLY_RELEASABLE = 1;
}

@ffi::Extern("C") private __k_rtti_unit_ref(descriptor : byte*) : const Unit&;
@ffi::Extern("C") private __k_rtti_namespace_ref(descriptor : byte*) : const Namespace&;
@ffi::Extern("C") private __k_rtti_namespace_ptr(descriptor : const Namespace?) : byte*;
@ffi::Extern("C") private __k_rtti_namespace_count(descriptors : const Namespace?[]?) : unsigned int;
@ffi::Extern("C") private __k_rtti_namespace_at(
    descriptors : const Namespace?[]?, index : unsigned int) : const Namespace?;
@ffi::Extern("C") private __k_module_dependency_count(identity : byte*) : int;
@ffi::Extern("C") private __k_module_dependency_at(identity : byte*, index : int) : byte*;
@ffi::Extern("C") private __k_module_symbol_lookup(handle : byte*, name : const unsigned byte[]) : byte*;
@ffi::Extern("C") private __k_module_symbol_status(result : byte*) : int;
@ffi::Extern("C") private __k_module_symbol_address(result : byte*) : byte*;
@ffi::Extern("C") private __k_module_symbol_take_message(result : byte*) : unsigned byte[]!;
@ffi::Extern("C") private __k_module_symbol_close(result : byte*);

/**
 * A lease on one immutable unit descriptor in a native image.
 *
 * The view retains its owning Module. Descriptor references returned by
 * getDescriptor() are borrowed and must not outlive this view.
 *
 * Copying the view copies its ownership lease, not the descriptor. A default
 * view is empty: check isNull() before descriptor access. getDescriptor(),
 * accept(), and rootNamespace() throw NullDereferenceError on an empty view.
 * A rootNamespace() result retains the owner independently of this view.
 */
public final class UnitView {
    friend Module;
    friend NamespaceView;

private:
    _owner : Shared<Module>;
    _descriptor : byte*;

    UnitView(owner : const Shared<Module>&, descriptor : byte*)
        : _owner(owner), _descriptor(descriptor) {}

public:
    UnitView() : _owner(), _descriptor(null) {}

    UnitView(other : const UnitView&)
        : _owner(other._owner), _descriptor(other._descriptor) {}

    operator =(other : const UnitView&) : UnitView& {
        _owner = other._owner;
        _descriptor = other._descriptor;
        return this;
    }

    /** Return the owning image handle. */
    public const owner() : Shared<Module> {
        return _owner;
    }

    public const isNull() : bool {
        return _descriptor == null;
    }

    /** Return the borrowed compiler-emitted descriptor. */
    public const getDescriptor() : const Unit& {
        if (_descriptor == null) {
            throw NullDereferenceError();
        }
        return __k_rtti_unit_ref(_descriptor);
    }

    /** Visit this descriptor while keeping its image resident for the call. */
    public const accept(visitor : RttiVisitor&) : void {
        if (_descriptor == null) {
            throw NullDereferenceError();
        }
        owner : Shared<Module> = _owner;
        if (!owner) {
            throw NullDereferenceError();
        }
        getDescriptor().accept(visitor);
    }

    /** Return an owner-retaining view of this unit's root namespace. */
    public const rootNamespace() : NamespaceView {
        if (_descriptor == null) {
            throw NullDereferenceError();
        }
        root : const Namespace? = getDescriptor().getRootNamespace();
        if (root == null) {
            throw NullDereferenceError();
        }
        return NamespaceView(_owner, __k_rtti_namespace_ptr(root));
    }
}

/**
 * Owner-retaining lease on a borrowed compiler-emitted namespace descriptor.
 *
 * Copies, children(), and a non-empty parent() each retain the same Module
 * independently. They can outlive the UnitView or NamespaceView used to
 * obtain them. The descriptor itself is not copied or owned by the view.
 *
 * A default view and the parent of a root namespace are empty. Check isNull()
 * before access: descriptor access, visitation, and navigation on an empty
 * view throw NullDereferenceError. A raw reference from getDescriptor() must
 * not outlive an owner-retaining view that protects its image.
 */
public final class NamespaceView {
    friend UnitView;

private:
    _owner : Shared<Module>;
    _descriptor : byte*;

    NamespaceView(owner : const Shared<Module>&, descriptor : byte*)
        : _owner(owner), _descriptor(descriptor) {}

public:
    NamespaceView() : _owner(), _descriptor(null) {}

    NamespaceView(other : const NamespaceView&)
        : _owner(other._owner), _descriptor(other._descriptor) {}

    operator =(other : const NamespaceView&) : NamespaceView& {
        _owner = other._owner;
        _descriptor = other._descriptor;
        return this;
    }

    public const owner() : Shared<Module> {
        return _owner;
    }

    public const isNull() : bool {
        return _descriptor == null;
    }

    public const getDescriptor() : const Namespace& {
        if (_descriptor == null) {
            throw NullDereferenceError();
        }
        return __k_rtti_namespace_ref(_descriptor);
    }

    /** Visit this descriptor while keeping its image resident for the call. */
    public const accept(visitor : RttiVisitor&) : void {
        if (_descriptor == null) {
            throw NullDereferenceError();
        }
        owner : Shared<Module> = _owner;
        if (!owner) {
            throw NullDereferenceError();
        }
        getDescriptor().accept(visitor);
    }

    public const children() : Vector<NamespaceView> {
        if (_descriptor == null) {
            throw NullDereferenceError();
        }
        result : Vector<NamespaceView>;
        descriptors : const Namespace?[]? = getDescriptor().getNamespaces();
        if (descriptors != null) {
            i : unsigned int = 0;
            while (i < __k_rtti_namespace_count(descriptors)) {
                child : const Namespace? = __k_rtti_namespace_at(descriptors, i);
                if (child != null) {
                    result.append(NamespaceView(_owner, __k_rtti_namespace_ptr(child)));
                }
                ++i;
            }
        }
        return result;
    }

    public const parent() : NamespaceView {
        if (_descriptor == null) {
            throw NullDereferenceError();
        }
        ancestor : const Namespace? = getDescriptor().getParent();
        if (ancestor == null) {
            return NamespaceView();
        }
        return NamespaceView(_owner, __k_rtti_namespace_ptr(ancestor));
    }
}

/**
 * A native image (module) loaded in the running process.
 *
 * Instances are created by ::k::Application and shared between the
 * application, its module registry, and callers. Images remain resident until
 * process exit; there is currently no unload operation.
 *
 * ### Handles, descriptors, and symbols
 *
 * - Keep Shared<Module> values rather than borrowing the registry's elements.
 *   Repeated loads of the same native image share its ownership group.
 * - units() describes only this image, not its dependencies. A native image
 *   without K metadata can have no units.
 * - UnitView and NamespaceView are copyable owner-retaining leases. Returning
 *   a view from a helper is safe; returning only its borrowed descriptor
 *   reference loses that lifetime protection.
 * - lookupSymbol() uses the loader scope, so dependencies can provide an exact
 *   native symbol name. Its NativeSymbol result retains this Module; a raw
 *   address extracted from it does not carry that ownership.
 *
 * Residency is not a request to unload, and this API provides no general
 * synchronisation for application data or code reached through a symbol.
 *
 * ### Example: keep a namespace after dropping the unit snapshot
 *
 * linkedUnits() is available in the compiler-generated Application main.
 * The namespace view below retains its image after the vector is cleared.
 *
 * ```k
 * module module_view_example;
 *
 * main() : int {
 *     units : Vector<UnitView> = linkedUnits();
 *     if (units.size() == 0u) {
 *         return 1;
 *     }
 *     root : NamespaceView = units.get(0).rootNamespace();
 *     units.clear();
 *     root.getDescriptor();
 *     return 0;
 * }
 * ```
 */
public abstract class Module : public Object {
    // Only Application (friend) can build a Module: friends have access to
    // protected members.
    friend Application;

protected:
    /** Native library handle (dlopen). */
    _handle : byte*;

    /** Stable identity of the loaded image within the process. */
    _identity : byte*;

    /** Absolute real path of the library file. */
    _path : String;

    /** Borrowed RTTI unit pointers; the image is retained by the Module. */
    _unitDescriptors : Vector<byte*>!;

    /** Weak self-reference lets views acquire a lease without a strong cycle. */
    _self : Weak<Module>;

    /** Direct ELF dependency edges, held weakly to avoid dependency cycles. */
    _dependencies : Vector<Weak<Module>>!;

    _dependenciesInitialized : bool;

    _residency : ModuleResidency;

    Module(handle : byte*, identity : byte*, path : const String&,
           unit_descriptors : const Vector<byte*>&, residency : ModuleResidency)
        : _handle(handle), _identity(identity), _path(path),
          _unitDescriptors(new Vector<byte*>()), _self(),
          _dependencies(new Vector<Weak<Module>>()), _dependenciesInitialized(false),
          _residency(residency) {
        i : unsigned int = 0;
        while (i < unit_descriptors.size()) {
            _unitDescriptors.append(unit_descriptors.get(i));
            ++i;
        }
    }

public:
    ~Module() {
        delete _unitDescriptors;
        delete _dependencies;
    }

    /** Return the concrete kind of this native image. */
    public abstract const kind() : ModuleKind;

    /** Dispatch to the callback for this concrete module kind. */
    public abstract const accept(visitor : RttiVisitor&) : void;

    public const residency() : ModuleResidency {
        return _residency;
    }

    public const isProcessPinned() : bool {
        return _residency == ModuleResidency::PROCESS_PINNED;
    }

    /**
     * Absolute real path of the loaded library file (symbolic links resolved).
     */
    public const path() : const String& {
        return _path;
    }

    /**
     * Look up an exact native symbol name in this handle's loader scope.
     *
     * Dependencies may provide the symbol; the result retains this Module.
     * A successful lookup can contain a zero address. Check the Expected
     * outcome rather than treating the address alone as the lookup status.
     */
    public const lookupSymbol(name : const String&)
        : Expected<ffi::NativeSymbol, ffi::SymbolLookupError> {
        owner : Shared<Module> = _self.lock();
        if (!owner) {
            throw FatalError();
        }
        utf8 : unsigned byte[]! = name.toUtf8();
        native_result : byte* = null;
        try {
            native_result = __k_module_symbol_lookup(_handle, utf8);
        } finally {
            delete utf8;
        }

        result : Expected<ffi::NativeSymbol, ffi::SymbolLookupError>;
        if (native_result == null) {
            error : ffi::SymbolLookupError(ffi::SymbolLookupErrorCode::OUT_OF_MEMORY,
                String("Cannot allocate native symbol lookup result"));
            result.setError(error);
            return result;
        }

        try {
            {
            status : int = __k_module_symbol_status(native_result);
            if (status == 0) {
                symbol : ffi::NativeSymbol(owner, __k_module_symbol_address(native_result));
                result.setResult(symbol);
            } else {
                bytes : unsigned byte[]! = __k_module_symbol_take_message(native_result);
                if (bytes == null) {
                    error : ffi::SymbolLookupError(ffi::SymbolLookupErrorCode::OUT_OF_MEMORY,
                        String("Cannot allocate native symbol lookup diagnostic"));
                    result.setError(error);
                } else {
                    try {
                        {
                        message : String(bytes);
                        error : ffi::SymbolLookupError((ffi::SymbolLookupErrorCode) status, message);
                        result.setError(error);
                        }
                    } finally {
                        delete bytes;
                    }
                }
            }
            }
        } finally {
            __k_module_symbol_close(native_result);
        }
        return result;
    }

    /**
     * Return owner-retaining views for units directly contained in this image.
     * Dependencies' units are never included.
     */
    public const units() : Vector<UnitView> {
        views : Vector<UnitView>;
        owner : Shared<Module> = _self.lock();
        if (!owner) {
            throw FatalError();
        }
        i : unsigned int = 0;
        while (i < _unitDescriptors.size()) {
            views.append(UnitView(owner, _unitDescriptors.get(i)));
            ++i;
        }
        return views;
    }

    /** Return strong handles for this image's direct native dependencies. */
    public const dependencies() : Vector<Shared<Module>> {
        result : Vector<Shared<Module>>;
        i : unsigned int = 0;
        while (i < _dependencies.size()) {
            dependency : Shared<Module> = _dependencies.get(i).lock();
            if (!dependency) {
                throw FatalError();
            }
            result.append(dependency);
            ++i;
        }
        return result;
    }
}

/**
 * The executable image hosting the current Application.
 *
 * linkedModules() returns this image followed by the startup-linked images;
 * linkedUnits() flattens their owner-retaining unit views. These are startup
 * snapshots, unlike the Application registry that also records later loads.
 */
public final class ApplicationModule : public Module {
    friend Application;

private:
    ApplicationModule(handle : byte*, identity : byte*, path : const String&,
                      unit_descriptors : const Vector<byte*>&, residency : ModuleResidency)
        : Module(handle, identity, path, unit_descriptors, residency),
          _linkedModules(new Vector<Shared<Module>>()) {}

    _linkedModules : Vector<Shared<Module>>!;

public:
    ~ApplicationModule() {
        delete _linkedModules;
    }

    override const kind() : ModuleKind { return ModuleKind::APPLICATION; }

    override const accept(visitor : RttiVisitor&) : void {
        visitor.visitApplicationModule(this);
    }

    public const linkedModules() : Vector<Shared<Module>> {
        result : Vector<Shared<Module>>;
        self : Shared<Module> = _self.lock();
        if (!self) {
            throw FatalError();
        }
        result.append(self);
        i : unsigned int = 0;
        while (i < _linkedModules.size()) {
            result.append(_linkedModules.get(i));
            ++i;
        }
        return result;
    }

    public const linkedUnits() : Vector<UnitView> {
        result : Vector<UnitView>;
        images : Vector<Shared<Module>> = linkedModules();
        i : unsigned int = 0;
        while (i < images.size()) {
            views : Vector<UnitView> = images.get(i).get()->units();
            j : unsigned int = 0;
            while (j < views.size()) {
                result.append(views.get(j));
                ++j;
            }
            ++i;
        }
        return result;
    }
}

/**
 * A Klang library image.
 */
public final class LibraryModule : public Module {
    friend Application;

private:
    LibraryModule(handle : byte*, identity : byte*, path : const String&,
                  unit_descriptors : const Vector<byte*>&, residency : ModuleResidency)
        : Module(handle, identity, path, unit_descriptors, residency) {}

public:
    override const kind() : ModuleKind { return ModuleKind::LIBRARY; }

    override const accept(visitor : RttiVisitor&) : void {
        visitor.visitLibraryModule(this);
    }
}

/**
 * A native shared image.
 */
public final class NativeLibraryModule : public Module {
    friend Application;

private:
    NativeLibraryModule(handle : byte*, identity : byte*, path : const String&,
                        unit_descriptors : const Vector<byte*>&, residency : ModuleResidency)
        : Module(handle, identity, path, unit_descriptors, residency) {}

public:
    override const kind() : ModuleKind { return ModuleKind::NATIVE_LIBRARY; }

    override const accept(visitor : RttiVisitor&) : void {
        visitor.visitNativeLibraryModule(this);
    }
}

public final class ModuleManifestError : public FatalError {
public:
    ModuleManifestError(code : int) : FatalError(code) {}
}
