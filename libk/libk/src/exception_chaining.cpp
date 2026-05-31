/*
 * K Language Runtime — Exception chaining helpers
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

/**
 * @file exception_chaining.cpp
 *
 * Runtime helpers for exception chaining (cause retention).
 *
 * When a new exception is thrown with a cause pointer referencing another
 * exception (e.g. `throw NewException(code, caught_ex)`), the cause's
 * ABI-managed memory must be kept alive until the outer exception is freed.
 *
 * These functions manipulate the exception reference count using the
 * platform's std::exception_ptr machinery (which abstracts the Itanium ABI
 * refcount details).
 *
 * Assumptions:
 *   - sizeof(std::exception_ptr) == sizeof(void*)
 *   - exception_ptr's internal state is a single pointer to exception data
 *   - Copy-constructing an exception_ptr increments the refcount
 *   - Destructing an exception_ptr decrements the refcount
 *
 * These hold for GCC/libstdc++ and Clang/libc++ on LP64 platforms.
 */

#include <exception>
#include <cstring>

static_assert(sizeof(std::exception_ptr) == sizeof(void*),
              "exception_ptr must be pointer-sized for K exception chaining");

extern "C" {

/**
 * Retain the currently active exception (increment its reference count).
 *
 * Calls std::current_exception() to obtain a ref-counted handle to the
 * active caught exception. Returns the raw exception pointer (suitable
 * for later release via __k_exception_release).
 *
 * Must be called from within a catch block where an exception is active.
 * Returns nullptr if no exception is currently active.
 */
void* __k_exception_retain_current() {
    std::exception_ptr eptr = std::current_exception();
    if (!eptr) {
        return nullptr;
    }
    void* raw_ptr = nullptr;
    std::memcpy(&raw_ptr, &eptr, sizeof(void*));
    // Zero the exception_ptr to prevent its destructor from decrementing.
    std::memset(&eptr, 0, sizeof(eptr));
    return raw_ptr;
}

/**
 * Release a previously retained exception (decrement its reference count).
 *
 * If the refcount reaches zero, the ABI frees the exception memory.
 * The pointer must have been previously returned by __k_exception_retain_current().
 */
void __k_exception_release(void* exc_ptr) {
    if (!exc_ptr) return;
    // Reconstruct an exception_ptr from the raw pointer.
    // When this goes out of scope, its destructor decrements the refcount.
    std::exception_ptr eptr;
    std::memcpy(&eptr, &exc_ptr, sizeof(void*));
}

} // extern "C"
