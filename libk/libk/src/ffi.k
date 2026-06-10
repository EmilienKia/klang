/*
 * K Language standard library -- base module
 *
 * Copyright 2023-2026 Emilien Kia
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

/**
 * Declare that a function is implemented by an external foreign symbol.
 *
 * Use this annotation on a function declaration to bind it to an implementation
 * provided by another language or runtime, typically C.
 */
@annotations::Retention(Policy::SOURCE)
@annotations::Target({ElementType::FUNCTION})
annotation Extern {
    /**
     * Name the foreign language or ABI used by the target symbol.
     */
    language : const char[];
    /**
     * Optionally name the foreign library that exports the symbol.
     */
    library : const char[]? = null;
    /**
     * Optionally override the foreign symbol name to bind.
     */
    symbol : const char[]? = null;
}

/**
 * Mark a parameter as a C-style string for FFI marshaling.
 *
 * This annotation is used on FFI parameters whose ABI representation is a
 * null-terminated native string rather than a regular K array descriptor.
 */
@annotations::Retention(Policy::SOURCE)
@annotations::Target({ElementType::PARAMETER})
annotation CString {
}


} // namespace ffi
