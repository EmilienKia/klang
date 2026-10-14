/*
 * K Language standard library — owner-retaining native symbols
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

namespace ffi {

/** Numeric values are shared with runtime/module_ffi.c. */
public enum SymbolLookupErrorCode {
    INVALID_NAME = 1;
    NOT_FOUND = 2;
    LOOKUP_FAILED = 3;
    OUT_OF_MEMORY = 4;
}

/** An owned diagnostic independent of the native loader's thread-local state. */
public final class SymbolLookupError {
private:
    _code : SymbolLookupErrorCode;
    _message : String;

public:
    SymbolLookupError(code : SymbolLookupErrorCode, message : const String&)
        : _code(code), _message(message) {}

    SymbolLookupError(other : const SymbolLookupError&)
        : _code(other._code), _message(other._message) {}

    operator =(other : const SymbolLookupError&) : SymbolLookupError& {
        _code = other._code;
        _message = other._message;
        return this;
    }

    public const code() : SymbolLookupErrorCode {
        return _code;
    }

    public const message() : const String& {
        return _message;
    }
}

/**
 * A native symbol address retaining the module used for lookup.
 *
 * A successful lookup may have a zero address. Such a symbol is not empty:
 * its owner remains present even when hasAddress() is false.
 */
public final class NativeSymbol {
    friend ::k::Module;

private:
    _owner : Shared<Module>;
    _address : byte*;

    NativeSymbol(owner : const Shared<Module>&, address : byte*)
        : _owner(owner), _address(address) {}

public:
    NativeSymbol() : _owner(), _address(null) {}

    NativeSymbol(other : const NativeSymbol&)
        : _owner(other._owner), _address(other._address) {}

    operator =(other : const NativeSymbol&) : NativeSymbol& {
        _owner = other._owner;
        _address = other._address;
        return this;
    }

    public const owner() : Shared<Module> {
        return _owner;
    }

    public const isEmpty() : bool {
        return !_owner;
    }

    public const hasAddress() : bool {
        return _address != null;
    }

    /** Borrowed, untyped native address; retain this symbol while using it. */
    public const rawAddress() : byte* {
        return _address;
    }
}

} // namespace ffi
