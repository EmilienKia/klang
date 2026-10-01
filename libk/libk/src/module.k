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
}

/**
 * A dynamic library (module) loaded in the running process.
 *
 * Instances are created and owned by ::k::Application (see
 * Application::loadModule()); they cannot be constructed by user code.
 * A module is never unloaded: its native handle stays open until the process
 * exits.
 */
public class Module : public Object {
    // Only Application (friend) can build a Module: friends have access to
    // protected members.
    friend Application;

protected:
    /** Native library handle (dlopen). */
    _handle : byte*;

    /** Absolute real path of the library file. */
    _path : String;

    Module(handle : byte*, path : const String&) : _handle(handle), _path(path) {}

public:
    ~Module() {}

    /**
     * Absolute real path of the loaded library file (symbolic links resolved).
     */
    public const path() : const String& {
        return _path;
    }
}
