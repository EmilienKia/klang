/*
 * K Language runtime — Fatal error helpers
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
 *
 * These functions are the single canonical definitions of the K fatal runtime
 * helpers.  They live in libk so that every K module (shared library or
 * executable) links against the same implementation — no per-module
 * duplication.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

/* Portable debug trap: Clang has __builtin_debugtrap(), GCC does not. */
#ifdef NDEBUG
#   define K_FATAL_TRAP()  __builtin_trap()
#elif defined(__clang__)
#   define K_FATAL_TRAP()  __builtin_debugtrap()
#else
#   define K_FATAL_TRAP()  raise(SIGTRAP)
#endif

/* ── Null-pointer fatal handlers ──────────────────────────────────────────── */

__attribute__((noreturn, cold))
void __k_fatal_null_dereference(void) {
    fprintf(stderr, "fatal: null pointer dereference\n");
    K_FATAL_TRAP();
    __builtin_unreachable();
}

__attribute__((noreturn, cold))
void __k_fatal_null_assignation(void) {
    fprintf(stderr, "fatal: null pointer assignation\n");
    K_FATAL_TRAP();
    __builtin_unreachable();
}

__attribute__((noreturn, cold))
void __k_fatal_null_dyncast(void) {
    fprintf(stderr, "fatal: null pointer after dynamic cast\n");
    K_FATAL_TRAP();
    __builtin_unreachable();
}

/* ── Array bounds check failure ───────────────────────────────────────────── */

__attribute__((noreturn, cold))
void __k_fatal_array_bounds_check_failed(unsigned index, unsigned size) {
    fprintf(stderr,
            "runtime error: array index out of bounds (index=%u, size=%u)\n",
            index, size);
    abort();
}

/* ── Memory allocation failure ────────────────────────────────────────────── */
/*
 * Called when malloc/realloc returns NULL in new-expressions or MultiSlot
 * allocate/reallocate intrinsics.
 *
 * For now, this is a hard abort (like array bounds checks). A future step
 * will convert this to throw a K MemoryException using the Itanium ABI,
 * once the linker configuration properly links libstdc++/libc++abi into
 * executables (needed for __cxa_allocate_exception / __cxa_throw).
 *
 * The compiler declares this function as 'noreturn cold' (NOT nounwind)
 * so that when the throw implementation is added, exception unwinding
 * will work correctly through invoke instructions.
 */

__attribute__((noreturn, cold))
void __k_fatal_memory_allocation(void) {
    fprintf(stderr, "fatal: memory allocation failed (out of memory)\n");
    abort();
}



