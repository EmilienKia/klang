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
 * The concrete subclass must implement one of the four standard `main`
 * overloads:
 *   main()
 *   main() : int
 *   main(args : const String[])
 *   main(args : const String[]) : int
 *
 * or any other `main` overload declared by an intermediate abstract
 * Application base (see language spec §Application chain).
 */
public abstract class Application : public Object {

    // ── Environment variable map ─────────────────────────────────────────────
    private _env : EnvironmentMap!;

    // ── Constructor ──────────────────────────────────────────────────────────
    Application() {
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

    // ── Accessors ────────────────────────────────────────────────────────────

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
}
