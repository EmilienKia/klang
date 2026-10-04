/*
 * K Language standard library — Application entry-point infrastructure
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

// ── Internal C FFI helpers (not part of the public API) ──────────────────────

/** Compiler entry-point hook; runs before concrete Application destruction. */
@ffi::Extern("C") private __k_application_shutdown();

/**
 * C helper: return argv[i] as an owned K unsigned-byte array.
 *
 * @param argv   the C argv pointer (char**) treated as an opaque pointer
 * @param i      the index into argv
 * @return       a newly heap-allocated K unsigned-byte array holding the bytes
 *               of argv[i] (WITHOUT a null terminator, K arrays are size-prefixed)
 */
@ffi::Extern("C") private __k_cstr_at(argv: byte*, i: int): unsigned byte[]!;

/**
 * C helper: return the number of entries in the C environ array.
 */
@ffi::Extern("C") private __k_env_count(): int;

/**
 * C helper: return environ[i]'s key (the part before '=') as an owned K byte array.
 */
@ffi::Extern("C") private __k_env_key_at(i: int): unsigned byte[]!;

/**
 * C helper: return environ[i]'s value (the part after '=') as an owned K byte array.
 */
@ffi::Extern("C") private __k_env_value_at(i: int): unsigned byte[]!;

/**
 * C helper: resolve a UTF-8 path (including trailing NUL) to an absolute,
 * symlink-free path. Returns null and sets `err` (a LoadModuleError value)
 * on failure.
 */
@ffi::Extern("C") private __k_module_realpath(path: const unsigned byte[], err: int*): unsigned byte[]!;

/**
 * C helper: dlopen() the library at the given absolute UTF-8 path (dependencies
 * loaded, symbols resolved, global constructors run). Returns the native handle,
 * or null and sets `err` (a LoadModuleError value) on failure.
 */
@ffi::Extern("C") private __k_module_open(path: const unsigned byte[], err: int*): byte*;

@ffi::Extern("C") private __k_module_identity(handle: byte*) : byte*;

@ffi::Extern("C") private __k_module_snapshot_open() : byte*;
@ffi::Extern("C") private __k_module_snapshot_count(snapshot: byte*) : int;
@ffi::Extern("C") private __k_module_snapshot_identity(snapshot: byte*, index: int) : byte*;
@ffi::Extern("C") private __k_module_snapshot_take_handle(snapshot: byte*, index: int) : byte*;
@ffi::Extern("C") private __k_module_snapshot_manifest_status(snapshot: byte*, index: int) : int;
@ffi::Extern("C") private __k_module_snapshot_unit_count(snapshot: byte*, image_index: int) : int;
@ffi::Extern("C") private __k_module_snapshot_unit_at(snapshot: byte*, image_index: int, unit_index: int) : byte*;
@ffi::Extern("C") private __k_module_snapshot_path(snapshot: byte*, index: int) : unsigned byte[]!;
@ffi::Extern("C") private __k_module_snapshot_close(snapshot: byte*);
@ffi::Extern("C") private __k_module_close(handle: byte*);
@ffi::Extern("C") private __k_module_depends_on(identity: byte*, candidate_identity: byte*) : int;
@ffi::Extern("C") private __k_module_registry_lock();
@ffi::Extern("C") private __k_module_registry_unlock();
@ffi::Extern("C") private __k_application_current(): Application*;
@ffi::Extern("C") private __k_application_activate(application: Application*): int;
@ffi::Extern("C") private __k_application_deactivate(application: Application*): int;
@ffi::Extern("C") private __k_application_activation_error(status: int);
@ffi::Extern("C") private __k_application_begin_stream_configuration(application: Application*);
@ffi::Extern("C") private __k_application_standard_streams_frozen(application: Application*) : int;
@ffi::Extern("C") private __k_application_report_stream_flush_error(stream: int, error: int);

/**
 * Return the absolute path of the current process image as an owned K byte
 * array. The array includes its trailing NUL byte.
 */
@ffi::Extern("C") private __k_module_current_path(): unsigned byte[]!;

// ── argv → String[] conversion (called from the synthesised C main proxy) ────

/**
 * Convert C argc/argv into a K String array.
 *
 * Called by the compiler-synthesised C main proxy when the user's main()
 * declares an `args : const String[]` parameter.
 *
 * @param argc  the C argc value
 * @param argv  the C argv value (char**)
 * @return      a heap-allocated K String array of length argc
 */
public __k_argv_to_string_array(argc: int, argv: byte*): String[]! {
    args : String[]! = new String[argc];
    i : int = 0;
    while (i < argc) {
        bytes : unsigned byte[]! = __k_cstr_at(argv, i);
        args[i] = String(bytes);
        ++i;
    }
    return args;
}

/**
 * Convert a raw UTF-8 byte array (as returned by the env FFI helpers) into a String.
 */
private __k_bytes_to_string(bytes: unsigned byte[]!): String {
    return String(bytes);
}

// ── EnvironmentMap — minimal read-only String→String map for env() ───────────

/**
 * EnvironmentMap — a minimal, read-only key/value map of Strings, backed by two
 * parallel vectors with linear lookup.
 *
 * Used exclusively to back ::k::Application::env(). A dedicated, minimal type
 * is used here (rather than the generic k::Map<K,V>/HashMap<K,V>/TreeMap<K,V>
 * hierarchy) to sidestep an unrelated, deep, pre-existing runtime memory
 * corruption bug affecting HashMapNode<K,V>/MapEntry<K,V> when BOTH K and V are
 * the SAME class type (e.g. HashMap<String,String>): constructing/populating
 * such a map reliably corrupts the heap (glibc "malloc(): invalid ..."/
 * "free(): invalid pointer" aborts), even with no iteration or destructor
 * logic exercised beyond put(). This is unrelated to Application/env() and is
 * worth root-causing separately; environment variable lists are always small
 * (typically well under a hundred entries), so linear lookup here is not a
 * performance concern in practice.
 */
public final class EnvironmentMap : public Object {
    private _keys : Vector<String>!;
    private _values : Vector<String>!;

    EnvironmentMap() {
        _keys = new Vector<String>();
        _values = new Vector<String>();
    }

    ~EnvironmentMap() {
        delete _keys;
        delete _values;
    }

    /** Append a new key/value pair. Does not check for duplicate keys. */
    public put(key: const String&, value: const String&) {
        _keys.append(key);
        _values.append(value);
    }

    /** Number of key/value pairs held by this map. */
    public const size() : unsigned int {
        return _keys.size();
    }

    /** True if `key` is present in the map. */
    public const containsKey(key: const String&) : bool {
        i : unsigned int = 0;
        n : unsigned int = _keys.size();
        while (i < n) {
            if (_keys.get(i) == key) {
                return true;
            }
            ++i;
        }
        return false;
    }

    /** Look up `key`, returning an empty optional if absent. */
    public const get(key: const String&) : OptionalConstRef<String> {
        i : unsigned int = 0;
        n : unsigned int = _keys.size();
        while (i < n) {
            if (_keys.get(i) == key) {
                found : OptionalConstRef<String>;
                v : const String& = _values.get(i);
                found.set(v);
                return found;
            }
            ++i;
        }
        return OptionalConstRef<String>();
    }
}

// ── Loaded-module bookkeeping ────────────────────────────────────────────────

/**
 * Map a LoadModuleError numeric value (as produced by the C runtime) to the enum.
 */
private __k_module_error_from_code(code: int): LoadModuleError {
    r : LoadModuleError = LoadModuleError::LOAD_FAILED;
    if (code == 0) { r = LoadModuleError::NOT_FOUND; }
    else if (code == 1) { r = LoadModuleError::ACCESS_DENIED; }
    else if (code == 2) { r = LoadModuleError::INVALID_PATH; }
    else if (code == 3) { r = LoadModuleError::NOT_A_LIBRARY; }
    else if (code == 4) { r = LoadModuleError::MISSING_DEPENDENCY; }
    else if (code == 5) { r = LoadModuleError::UNRESOLVED_SYMBOL; }
    else if (code == 7) { r = LoadModuleError::INVALID_MANIFEST; }
    else if (code == 8) { r = LoadModuleError::INCOMPATIBLE_MANIFEST; }
    return r;
}

// ── ::k::Application — abstract base for all K application entry points ──────

/**
 * Abstract base class for all K application entry points.
 *
 * The compiler synthesises a concrete subclass of Application in every
 * executable module (a module that defines a top-level `main` function or an
 * explicit `class Application`).
 *
 * Applications inherit from ::k::Object (via Application) and always have
 * access to:
 * - `env()` — a read-only map of environment variables
 *
 * `applicationModule()` exposes a shared representation of the process image,
 * while `modules()` returns a stable snapshot of the registered image handles.
 *
 * The concrete subclass must implement one of the four standard `main`
 * overloads:
 *   main()
 *   main() : int
 *   main(args : const String[])
 *   main(args : const String[]) : int
 *
 * or any other `main` overload declared by an intermediate abstract
 * Application base (see language spec §Application chain).
 *
 * The generated entry point drains the process I/O runtime and callback
 * carriers before destroying this object, including when main unwinds.
 * This must precede the concrete destructor, not merely this base's cleanup.
 */
public abstract class Application : public Object {

    // ── Application-owned standard streams ───────────────────────────────────
    protected _stdin : io::InputStream<byte>!;
    protected _stdout : io::OutputStream<byte>!;
    protected _stderr : io::OutputStream<byte>!;

    private _stdinUtf8Reader : io::Utf8Reader!;
    private _stdinBufferedReader : io::BufferedReader!;
    private _stdoutUtf8Writer : io::Utf8Writer!;
    private _stdoutTextPrinter : io::TextPrinter!;
    private _stdoutPrint : io::PrintStream!;
    private _stderrUtf8Writer : io::Utf8Writer!;
    private _stderrTextPrinter : io::TextPrinter!;
    private _stderrPrint : io::PrintStream!;

    // ── Environment variable map ─────────────────────────────────────────────
    private _env : EnvironmentMap!;

    // ── Process image and shared module registry ─────────────────────────────
    private _module : Shared<ApplicationModule>;
    private _modules : Vector<Shared<Module>>!;

    // ── Constructor ──────────────────────────────────────────────────────────
    Application() {
        __k_application_begin_stream_configuration(&this);
        _stdin = new io::file::FileInputStream(io::file::__k_io_file_get_stdin());
        _stdinUtf8Reader = new io::Utf8Reader(&*_stdin);
        _stdinBufferedReader = new io::BufferedReader(&*_stdinUtf8Reader);
        _stdout = new io::file::FileOutputStream(io::file::__k_io_file_get_stdout());
        _stdoutUtf8Writer = new io::Utf8Writer(&*_stdout);
        _stdoutTextPrinter = new io::TextPrinter(&*_stdoutUtf8Writer);
        _stdoutPrint = new io::PrintStream(&*_stdout);
        _stderr = new io::file::FileOutputStream(io::file::__k_io_file_get_stderr());
        _stderrUtf8Writer = new io::Utf8Writer(&*_stderr);
        _stderrTextPrinter = new io::TextPrinter(&*_stderrUtf8Writer);
        _stderrPrint = new io::PrintStream(&*_stderr);

        _modules = new Vector<Shared<Module>>();
        image_snapshot : byte* = __k_module_snapshot_open();
        image_count : int = __k_module_snapshot_count(image_snapshot);
        manifest_check_index : int = 0;
        while (manifest_check_index < image_count) {
            startup_status : int =
                __k_module_snapshot_manifest_status(image_snapshot, manifest_check_index);
            if (startup_status == 2 || startup_status == 3) {
                __k_module_snapshot_close(image_snapshot);
                if (startup_status == 2) {
                    throw ModuleManifestError((int) LoadModuleError::INCOMPATIBLE_MANIFEST);
                }
                throw ModuleManifestError((int) LoadModuleError::INVALID_MANIFEST);
            }
            ++manifest_check_index;
        }
        image_index : int = 0;
        if (image_count > 0) {
            app_path_bytes : unsigned byte[]! = __k_module_snapshot_path(image_snapshot, 0);
            app_path : String = String(app_path_bytes);
            delete app_path_bytes;
            app_units : Vector<byte*>;
            app_unit_count : int = __k_module_snapshot_unit_count(image_snapshot, 0);
            app_unit_index : int = 0;
            while (app_unit_index < app_unit_count) {
                app_descriptor : byte* =
                    __k_module_snapshot_unit_at(image_snapshot, 0, app_unit_index);
                app_units.append(app_descriptor);
                ++app_unit_index;
            }
            app_handle : byte* = __k_module_snapshot_take_handle(image_snapshot, 0);
            app_identity : byte* = __k_module_snapshot_identity(image_snapshot, 0);
            _module = Shared<ApplicationModule>(
                new ApplicationModule(
                    app_handle, app_identity, app_path, app_units,
                    ModuleResidency::PROCESS_PINNED));
            app_module : Shared<Module> = Shared<Module>(_module);
            app_module.get()->_self = Weak<Module>(app_module);
            _modules.append(app_module);

            image_index = 1;
            while (image_index < image_count) {
                image_status : int =
                    __k_module_snapshot_manifest_status(image_snapshot, image_index);
                if (image_status == 0 || image_status == 1) {
                    image_path_bytes : unsigned byte[]! =
                        __k_module_snapshot_path(image_snapshot, image_index);
                    image_path : String = String(image_path_bytes);
                    delete image_path_bytes;
                    image_identity : byte* =
                        __k_module_snapshot_identity(image_snapshot, image_index);
                    image_handle : byte* =
                        __k_module_snapshot_take_handle(image_snapshot, image_index);

                    image_units : Vector<byte*>;
                    if (image_status == 1) {
                        image_unit_count : int =
                            __k_module_snapshot_unit_count(image_snapshot, image_index);
                        image_unit_index : int = 0;
                        while (image_unit_index < image_unit_count) {
                            image_descriptor : byte* = __k_module_snapshot_unit_at(
                                image_snapshot, image_index, image_unit_index);
                            image_units.append(image_descriptor);
                            ++image_unit_index;
                        }
                    }

                    if (image_status == 1) {
                        concrete : Shared<LibraryModule> = Shared<LibraryModule>(
                            new LibraryModule(
                                image_handle, image_identity, image_path, image_units,
                                ModuleResidency::PROCESS_PINNED));
                        registered : Shared<Module> = Shared<Module>(concrete);
                        registered.get()->_self = Weak<Module>(registered);
                        _modules.append(registered);
                    } else {
                        concrete : Shared<NativeLibraryModule> = Shared<NativeLibraryModule>(
                            new NativeLibraryModule(
                                image_handle, image_identity, image_path, image_units,
                                ModuleResidency::PROCESS_PINNED));
                        registered : Shared<Module> = Shared<Module>(concrete);
                        registered.get()->_self = Weak<Module>(registered);
                        _modules.append(registered);
                    }
                }
                ++image_index;
            }
        }
        __k_module_snapshot_close(image_snapshot);
        linked_index : unsigned int = 0;
        while (linked_index < _modules.size()) {
            if (_modules.get(linked_index).get() != _module.get()) {
                _module.get()->_linkedModules.append(_modules.get(linked_index));
            }
            ++linked_index;
        }
        __k_module_populate_dependencies();

        _env = new EnvironmentMap();
        n : int = __k_env_count();
        i : int = 0;
        while (i < n) {
            key_bytes : unsigned byte[]! = __k_env_key_at(i);
            val_bytes : unsigned byte[]! = __k_env_value_at(i);
            key : String = __k_bytes_to_string(key_bytes);
            val : String = __k_bytes_to_string(val_bytes);
            _env.put(key, val);
            ++i;
        }
    }

    ~Application() {
        if (_stdinBufferedReader != null) {
            delete _stdinBufferedReader;
            _stdinBufferedReader = null;
        }
        if (_stdinUtf8Reader != null) {
            delete _stdinUtf8Reader;
            _stdinUtf8Reader = null;
        }
        if (_stdin != null) {
            delete _stdin;
            _stdin = null;
        }

        if (_stdoutPrint != null) {
            delete _stdoutPrint;
            _stdoutPrint = null;
        }
        if (_stdoutTextPrinter != null) {
            delete _stdoutTextPrinter;
            _stdoutTextPrinter = null;
        }

        if (_stdoutUtf8Writer != null) {
            delete _stdoutUtf8Writer;
            _stdoutUtf8Writer = null;
        }
        if (_stdout != null) {
            delete _stdout;
            _stdout = null;
        }

        if (_stderrPrint != null) {
            delete _stderrPrint;
            _stderrPrint = null;
        }
        if (_stderrTextPrinter != null) {
            delete _stderrTextPrinter;
            _stderrTextPrinter = null;
        }

        if (_stderrUtf8Writer != null) {
            delete _stderrUtf8Writer;
            _stderrUtf8Writer = null;
        }
        if (_stderr != null) {
            delete _stderr;
            _stderr = null;
        }
        delete _modules;
    }

    // ── Accessors ────────────────────────────────────────────────────────────

    /**
     * Return a borrowed reference to the active process Application, if any.
     * The result is empty before entry-point activation and after it ends.
     */
    public static getApplication() : OptionalRef<Application> {
        application : Application* = __k_application_current();
        if (application == null) {
            return OptionalRef<Application>();
        }
        return OptionalRef<Application>(*application);
    }

    /** Borrow the configured standard input byte stream. */
    public final stdinBytes() : io::InputStream<byte>& {
        return *_stdin;
    }

    /** Borrow the configured standard input byte stream. */
    public final stdin() : io::InputStream<byte>& {
        return *_stdin;
    }

    /** Borrow the UTF-8 buffered standard input reader. */
    public final stdinReader() : io::BufferedReader& {
        return *_stdinBufferedReader;
    }

    /** Borrow the configured standard output byte stream. */
    public final stdoutBytes() : io::OutputStream<byte>& {
        return *_stdout;
    }

    /** Borrow the configured standard error byte stream. */
    public final stderrBytes() : io::OutputStream<byte>& {
        return *_stderr;
    }

    /** Borrow the UTF-8 standard output printer. */
    public final stdout() : io::TextPrinter& {
        return *_stdoutTextPrinter;
    }

    /** Borrow the UTF-8 standard error printer. */
    public final stderr() : io::TextPrinter& {
        return *_stderrTextPrinter;
    }

    /** Borrow the legacy byte-oriented standard output printer. */
    public final stdoutPrint() : io::PrintStream& {
        return *_stdoutPrint;
    }

    /** Borrow the legacy byte-oriented standard error printer. */
    public final stderrPrint() : io::PrintStream& {
        return *_stderrPrint;
    }

    /**
     * Flush configured output streams before the entry point withdraws this
     * Application from the process-wide active slot.
     */
    public final flushStandardStreams() : bool {
        success : bool = true;
        if (_stdoutTextPrinter != null) {
            try {
                _stdoutTextPrinter->flush();
            } catch (error : io::IOException&) {
                __k_application_report_stream_flush_error(0, error.getErrno());
                success = false;
            }
        }
        if (_stderrTextPrinter != null) {
            try {
                _stderrTextPrinter->flush();
            } catch (error : io::IOException&) {
                __k_application_report_stream_flush_error(1, error.getErrno());
                success = false;
            }
        }
        return success;
    }

    /**
     * Replace standard input during Application construction. Ownership of
     * `input` transfers to this Application after its adapters are prepared.
     * A preparation failure leaves the current stream configuration intact.
     */
protected:
    final __replaceStandardInputAdapters(reader: io::Utf8Reader!,
                                         buffered: io::BufferedReader!) {
        __checkStandardStreamConfiguration();
        __commitStandardInputAdapters(reader, buffered);
    }

    final __commitStandardInputAdapters(reader: io::Utf8Reader!,
                                        buffered: io::BufferedReader!) {
        if (_stdinBufferedReader != null) {
            delete _stdinBufferedReader;
            _stdinBufferedReader = null;
        }
        if (_stdinUtf8Reader != null) {
            delete _stdinUtf8Reader;
            _stdinUtf8Reader = null;
        }
        _stdinUtf8Reader = reader;
        _stdinBufferedReader = buffered;
    }

    final __checkStandardStreamConfiguration() {
        if (__k_application_standard_streams_frozen(&this) != 0) {
            throw io::StandardStreamConfigurationError();
        }
    }

    final __replaceStandardOutputAdapters(writer: io::Utf8Writer!,
                                          printer: io::TextPrinter!,
                                          legacy: io::PrintStream!) {
        __checkStandardStreamConfiguration();
        __flushStandardOutputAdapters();
        __commitStandardOutputAdapters(writer, printer, legacy);
    }

    final __flushStandardOutputAdapters() {
        if (_stdoutTextPrinter != null) {
            _stdoutTextPrinter->flush();
        } else if (_stdoutPrint != null) {
            _stdoutPrint->flush();
        }
    }

    final __commitStandardOutputAdapters(writer: io::Utf8Writer!,
                                         printer: io::TextPrinter!,
                                         legacy: io::PrintStream!) {
        if (_stdoutTextPrinter != null) {
            delete _stdoutTextPrinter;
            _stdoutTextPrinter = null;
        }
        if (_stdoutPrint != null) {
            delete _stdoutPrint;
            _stdoutPrint = null;
        }
        if (_stdoutUtf8Writer != null) {
            delete _stdoutUtf8Writer;
            _stdoutUtf8Writer = null;
        }
        _stdoutUtf8Writer = writer;
        _stdoutTextPrinter = printer;
        _stdoutPrint = legacy;
    }

    final __replaceStandardErrorAdapters(writer: io::Utf8Writer!,
                                         printer: io::TextPrinter!,
                                         legacy: io::PrintStream!) {
        __checkStandardStreamConfiguration();
        __flushStandardErrorAdapters();
        __commitStandardErrorAdapters(writer, printer, legacy);
    }

    final __flushStandardErrorAdapters() {
        if (_stderrTextPrinter != null) {
            _stderrTextPrinter->flush();
        } else if (_stderrPrint != null) {
            _stderrPrint->flush();
        }
    }

    final __commitStandardErrorAdapters(writer: io::Utf8Writer!,
                                        printer: io::TextPrinter!,
                                        legacy: io::PrintStream!) {
        if (_stderrTextPrinter != null) {
            delete _stderrTextPrinter;
            _stderrTextPrinter = null;
        }
        if (_stderrPrint != null) {
            delete _stderrPrint;
            _stderrPrint = null;
        }
        if (_stderrUtf8Writer != null) {
            delete _stderrUtf8Writer;
            _stderrUtf8Writer = null;
        }
        _stderrUtf8Writer = writer;
        _stderrTextPrinter = printer;
        _stderrPrint = legacy;
    }

    template<typename T>
    setStandardInput(input: T!) {
        if (input == null) {
            throw io::StandardStreamConfigurationError();
        }
        reader : io::Utf8Reader! =
            new io::Utf8Reader((io::InputStream<byte>*)&*input);
        buffered : io::BufferedReader! = new io::BufferedReader(&*reader);

        __replaceStandardInputAdapters(reader, buffered);
        _stdin = input;
    }

    /**
     * Decorate standard input with a FilterInputStream during construction.
     * The decorator must have been constructed over the current standard
     * input. On success it takes ownership of that root, so nested decorators
     * remain alive for as long as the configured root.
     */
    final decorateStandardInput(input: io::FilterInputStream<byte>!) {
        if (input == null) {
            throw io::StandardStreamConfigurationError();
        }
        filter : io::FilterInputStream<byte>* =
            (io::FilterInputStream<byte>*)&*input;
        if (filter == null || !filter->__k_canAdoptInput(&*_stdin)) {
            throw io::StandardStreamConfigurationError();
        }

        __checkStandardStreamConfiguration();
        reader : io::Utf8Reader! =
            new io::Utf8Reader((io::InputStream<byte>*)&*input);
        buffered : io::BufferedReader! = new io::BufferedReader(&*reader);
        __commitStandardInputAdapters(reader, buffered);
        previous : ::k::io::InputStream<byte>! = _stdin;
        filter->__k_adoptInput(previous);
        _stdin = input;
    }

    /**
     * Replace standard output during Application construction. Ownership of
     * `output` transfers to this Application after its adapters are prepared.
     * A preparation failure leaves the current stream configuration intact.
     */
    template<typename T>
    setStandardOutput(output: T!) {
        if (output == null) {
            throw io::StandardStreamConfigurationError();
        }
        writer : io::Utf8Writer! =
            new io::Utf8Writer((io::OutputStream<byte>*)&*output);
        printer : io::TextPrinter! = new io::TextPrinter(&*writer);
        legacy : io::PrintStream! =
            new io::PrintStream((io::OutputStream<byte>*)&*output);

        __replaceStandardOutputAdapters(writer, printer, legacy);
        _stdout = output;
    }

    /**
     * Decorate standard output during construction. The decorator must have
     * been constructed over the current standard output. Its previous root is
     * adopted only after the replacement adapters are prepared and the old
     * output adapters flush successfully.
     */
    final decorateStandardOutput(output: io::FilterOutputStream<byte>!) {
        if (output == null) {
            throw io::StandardStreamConfigurationError();
        }
        filter : io::FilterOutputStream<byte>* =
            (io::FilterOutputStream<byte>*)&*output;
        if (filter == null || !filter->__k_canAdoptOutput(&*_stdout)) {
            throw io::StandardStreamConfigurationError();
        }

        __checkStandardStreamConfiguration();
        writer : io::Utf8Writer! =
            new io::Utf8Writer((io::OutputStream<byte>*)&*output);
        printer : io::TextPrinter! = new io::TextPrinter(&*writer);
        legacy : io::PrintStream! =
            new io::PrintStream((io::OutputStream<byte>*)&*output);

        __flushStandardOutputAdapters();
        __commitStandardOutputAdapters(writer, printer, legacy);
        filter->__k_adoptOutput(_stdout);
        _stdout = output;
    }

    /**
     * Replace standard error during Application construction. Ownership of
     * `output` transfers to this Application after its adapters are prepared.
     * A preparation failure leaves the current stream configuration intact.
     */
    template<typename T>
    setStandardError(output: T!) {
        if (output == null) {
            throw io::StandardStreamConfigurationError();
        }
        writer : io::Utf8Writer! =
            new io::Utf8Writer((io::OutputStream<byte>*)&*output);
        printer : io::TextPrinter! = new io::TextPrinter(&*writer);
        legacy : io::PrintStream! =
            new io::PrintStream((io::OutputStream<byte>*)&*output);

        __replaceStandardErrorAdapters(writer, printer, legacy);
        _stderr = output;
    }

    /**
     * Decorate standard error during construction. The decorator must have
     * been constructed over the current standard error. Its previous root is
     * adopted only after the replacement adapters are prepared and the old
     * output adapters flush successfully.
     */
    final decorateStandardError(output: io::FilterOutputStream<byte>!) {
        if (output == null) {
            throw io::StandardStreamConfigurationError();
        }
        filter : io::FilterOutputStream<byte>* =
            (io::FilterOutputStream<byte>*)&*output;
        if (filter == null || !filter->__k_canAdoptOutput(&*_stderr)) {
            throw io::StandardStreamConfigurationError();
        }

        __checkStandardStreamConfiguration();
        writer : io::Utf8Writer! =
            new io::Utf8Writer((io::OutputStream<byte>*)&*output);
        printer : io::TextPrinter! = new io::TextPrinter(&*writer);
        legacy : io::PrintStream! =
            new io::PrintStream((io::OutputStream<byte>*)&*output);

        __flushStandardErrorAdapters();
        __commitStandardErrorAdapters(writer, printer, legacy);
        filter->__k_adoptOutput(_stderr);
        _stderr = output;
    }

public:
    /**
     * Return a read-only view of the process environment variables.
     *
     * The map is populated once at application start-up, before `main()` is
     * called.  Keys and values are the raw UTF-8 strings provided by the OS.
     *
     * @return  const reference to the environment map
     */
    public const env() : const EnvironmentMap& {
        return *_env;
    }

    /** Return the shared representation of the process's application image. */
    public const applicationModule() : Shared<ApplicationModule> {
        return _module;
    }

    /** Return the stable startup snapshot of loaded native images. */
    public const linkedModules() : Vector<Shared<Module>> {
        return _module.get()->linkedModules();
    }

    /** Return owner-retaining RTTI views for units linked at process startup. */
    public const linkedUnits() : Vector<UnitView> {
        return _module.get()->linkedUnits();
    }

    private __k_module_populate_dependencies() : void {
        module_index : unsigned int = 0;
        while (module_index < _modules.size()) {
            source_module : Shared<Module>& = _modules.get(module_index);
            if (!source_module.get()->_dependenciesInitialized) {
                dependency_count : int =
                    __k_module_dependency_count(source_module.get()->_identity);
                dependency_index : int = 0;
                while (dependency_index < dependency_count) {
                    dependency_identity : byte* = __k_module_dependency_at(
                        source_module.get()->_identity, dependency_index);
                    candidate_index : unsigned int = 0;
                    while (candidate_index < _modules.size()) {
                        candidate : Shared<Module>& = _modules.get(candidate_index);
                        if (candidate.get()->_identity == dependency_identity) {
                            source_module.get()->_dependencies.append(Weak<Module>(candidate));
                            break;
                        }
                        ++candidate_index;
                    }
                    ++dependency_index;
                }
                source_module.get()->_dependenciesInitialized = true;
            }
            ++module_index;
        }
    }

    // ── Dynamic modules ──────────────────────────────────────────────────────

    /**
     * Load a dynamic library from a local file path.
     *
     * The library is loaded with all its dependencies, its symbols are resolved
     * immediately and its global constructors (including those of K global
     * objects) have run when this function returns. Symbols are made available
     * globally to the libraries loaded afterwards. Dependencies are initialised
     * before the library itself, so a global object may register itself in
     * an object of a library it depends on.
     *
     * Modules are identified by their native image identity: loading a file
     * already loaded by this process (explicitly, through another path or
     * symbolic link, or implicitly as a dependency) returns the same ownership
     * group.
     * Loaded images remain resident until process exit.
     *
     * This function is thread-safe. It must not be called by a global constructor
     * of a library being loaded from another thread.
     *
     * @param path  path of the library file (relative or absolute)
     * @return      shared ownership of the module on success, or the reason of
     *              failure
     */
    public loadModule(path: const String&) : Expected<Shared<Module>, LoadModuleError> {
        err : int = 0;
        utf8 : unsigned byte[]! = path.toUtf8();
        real : unsigned byte[]! = __k_module_realpath(utf8, &err);
        delete utf8;
        if (real == null) {
            failure : LoadModuleError = __k_module_error_from_code(err);
            failure_result : Expected<Shared<Module>, LoadModuleError>;
            failure_result.setError(failure);
            return failure_result;
        }

        realPath : String = String(real);
        handle : byte* = null;
        identity : byte* = null;
        found : Shared<Module>;
        handle = __k_module_open(real, &err);
        if (handle == null) {
            delete real;
            failure : LoadModuleError = __k_module_error_from_code(err);
            failure_result : Expected<Shared<Module>, LoadModuleError>;
            failure_result.setError(failure);
            return failure_result;
        }
        identity = __k_module_identity(handle);
        image_snapshot : byte* = __k_module_snapshot_open();
        image_count : int = __k_module_snapshot_count(image_snapshot);
        target_index : int = -1;
        image_index : int = 0;
        while (image_index < image_count && target_index < 0) {
            if (__k_module_snapshot_identity(image_snapshot, image_index) == identity) {
                target_index = image_index;
            }
            ++image_index;
        }
        if (target_index < 0) {
            __k_module_snapshot_close(image_snapshot);
            __k_module_close(handle);
            delete real;
            failure_result : Expected<Shared<Module>, LoadModuleError>;
            failure : LoadModuleError = LoadModuleError::LOAD_FAILED;
            failure_result.setError(failure);
            return failure_result;
        }
        image_index = 0;
        while (image_index < image_count) {
            image_identity : byte* =
                __k_module_snapshot_identity(image_snapshot, image_index);
            depends_on_target : bool =
                __k_module_depends_on(identity, image_identity) != 0;
            image_status : int =
                __k_module_snapshot_manifest_status(image_snapshot, image_index);
            if ((image_index == target_index || depends_on_target)
                && (image_status == 2 || image_status == 3)) {
                __k_module_snapshot_close(image_snapshot);
                __k_module_close(handle);
                delete real;
                failure_result : Expected<Shared<Module>, LoadModuleError>;
                if (image_status == 2) {
                    failure : LoadModuleError = LoadModuleError::INCOMPATIBLE_MANIFEST;
                    failure_result.setError(failure);
                } else {
                    failure : LoadModuleError = LoadModuleError::INVALID_MANIFEST;
                    failure_result.setError(failure);
                }
                return failure_result;
            }
            ++image_index;
        }
        __k_module_close(handle);

        __k_module_registry_lock();
        try {
            {
            image_index = 0;
            while (image_index < image_count) {
                image_identity : byte* =
                    __k_module_snapshot_identity(image_snapshot, image_index);
                existing : Shared<Module>;
                i : unsigned int = 0;
                while (i < _modules.size() && !existing) {
                    candidate : Shared<Module>& = _modules.get(i);
                    if (candidate.get()->_identity == image_identity) {
                        existing = candidate;
                    }
                    ++i;
                }
                if (!existing) {
                    image_status : int =
                        __k_module_snapshot_manifest_status(image_snapshot, image_index);
                    if (image_status == 0 || image_status == 1) {
                        image_path_bytes : unsigned byte[]! =
                            __k_module_snapshot_path(image_snapshot, image_index);
                        image_path : String = String(image_path_bytes);
                        delete image_path_bytes;
                        image_handle : byte* =
                            __k_module_snapshot_take_handle(image_snapshot, image_index);
                        image_units : Vector<byte*>;
                        if (image_status == 1) {
                            image_unit_count : int =
                                __k_module_snapshot_unit_count(image_snapshot, image_index);
                            image_unit_index : int = 0;
                            while (image_unit_index < image_unit_count) {
                                image_descriptor : byte* = __k_module_snapshot_unit_at(
                                    image_snapshot, image_index, image_unit_index);
                                image_units.append(image_descriptor);
                                ++image_unit_index;
                            }
                        }
                        if (image_status == 1) {
                            concrete : Shared<LibraryModule> = Shared<LibraryModule>(
                                new LibraryModule(
                                    image_handle, image_identity, image_path, image_units,
                                    ModuleResidency::POTENTIALLY_RELEASABLE));
                            registered : Shared<Module> = Shared<Module>(concrete);
                            registered.get()->_self = Weak<Module>(registered);
                            if (image_index == target_index) {
                                found = registered;
                            }
                            _modules.append(registered);
                        } else {
                            concrete : Shared<NativeLibraryModule> = Shared<NativeLibraryModule>(
                                new NativeLibraryModule(
                                    image_handle, image_identity, image_path, image_units,
                                    ModuleResidency::POTENTIALLY_RELEASABLE));
                            registered : Shared<Module> = Shared<Module>(concrete);
                            registered.get()->_self = Weak<Module>(registered);
                            if (image_index == target_index) {
                                found = registered;
                            }
                            _modules.append(registered);
                        }
                    }
                } else if (image_index == target_index) {
                    found = existing;
                }
                ++image_index;
            }
            __k_module_populate_dependencies();
            }
        } finally {
            __k_module_registry_unlock();
        }
        __k_module_snapshot_close(image_snapshot);
        delete real;

        if (!found) {
            failure_result : Expected<Shared<Module>, LoadModuleError>;
            failure : LoadModuleError = LoadModuleError::LOAD_FAILED;
            failure_result.setError(failure);
            return failure_result;
        }
        success_result : Expected<Shared<Module>, LoadModuleError>;
        success_result.setResult(found);
        return success_result;
    }

    /**
     * Return shared ownership of a module already registered for `path`, or an
     * empty handle when no matching module is known. This does not load images.
     */
    public lookupModule(path: const String&) : Shared<Module> {
        err : int = 0;
        utf8 : unsigned byte[]! = path.toUtf8();
        real : unsigned byte[]! = __k_module_realpath(utf8, &err);
        delete utf8;
        if (real == null) {
            return Shared<Module>();
        }

        realPath : String = String(real);
        delete real;
        found : Shared<Module>;
        __k_module_registry_lock();
        try {
            {
            i : unsigned int = 0;
            while (i < _modules.size() && !found) {
                candidate : Shared<Module>& = _modules.get(i);
                if (candidate.get()->path() == realPath) {
                    found = candidate;
                }
                ++i;
            }
            }
        } finally {
            __k_module_registry_unlock();
        }
        return found;
    }

    /**
     * Return a stable snapshot of the modules currently registered by this
     * Application. Each entry shares ownership with its registered image.
     */
    public modules() : Vector<Shared<Module>> {
        snapshot : Vector<Shared<Module>>;
        __k_module_registry_lock();
        try {
            {
            i : unsigned int = 0;
            while (i < _modules.size()) {
                snapshot.append(_modules.get(i));
                ++i;
            }
            }

        } finally {
            __k_module_registry_unlock();
        }
        return snapshot;
    }
}

public __k_application_flush_standard_streams(application: Application*) : int {
    if (application == null) return 0;
    if (application->flushStandardStreams()) return 0;
    return 1;
}
