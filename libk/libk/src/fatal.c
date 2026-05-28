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
 * Throws a K MemoryException using the Itanium C++ ABI:
 *   1. Allocate exception storage via __cxa_allocate_exception (uses the
 *      emergency buffer so it works even under OOM).
 *   2. Call the K MemoryException() constructor on the storage.
 *   3. Set up _k_thrown_typeinfo_chain with the full class hierarchy so that
 *      catch clauses for RuntimeException or Exception also match.
 *   4. Call __cxa_throw to initiate stack unwinding.
 *
 * The compiler declares this function as 'noreturn cold' (NOT nounwind)
 * so exception unwinding works correctly through invoke instructions.
 */

/* ── Itanium C++ ABI functions ────────────────────────────────────────────── */
extern void* __cxa_allocate_exception(size_t thrown_size);
extern void  __cxa_throw(void* thrown_exception, void* tinfo, void (*dest)(void*))
    __attribute__((noreturn));

/* ── K runtime symbols (from libk.so) ─────────────────────────────────────── */

/* MemoryException default constructor: initialises vtable + calls base ctors */
extern void _KFMN1k15MemoryExceptionC1Ev(void* self);

/* RTTI typeinfo globals for the MemoryException class hierarchy */
extern char _KTRIN1k15MemoryExceptionE;
extern char _KTRIN1k16RuntimeExceptionE;
extern char _KTRIN1k9ExceptionE;
extern char _KTRIN1k6ObjectE;

/*
 * _k_thrown_typeinfo_chain — pointer to the null-terminated typeinfo chain
 * of the most recently thrown exception in this thread/module.  The catch
 * dispatch code reads this to perform polymorphic type matching.
 *
 * Each entry is { void* typeinfo, uint32_t byte_offset } (padded to 16 bytes
 * on 64-bit). The chain for MemoryException is:
 *   [MemoryException@0, RuntimeException@8, Exception@16, Object@24, {null,0}]
 *
 * This global is defined with weak linkage so it works whether the compiler
 * has already emitted it in the module or not.
 */
__attribute__((weak))
void* _k_thrown_typeinfo_chain = (void*)0;

/* Also the primary (non-chain) typeinfo pointer for backward compat */
__attribute__((weak))
void* _k_thrown_typeinfo = (void*)0;

/*
 * Static typeinfo chain for MemoryException.
 * Layout: array of { void* ti_ptr, uint32_t offset, uint32_t pad }.
 * On LP64 this is 16 bytes per entry due to alignment.
 */
struct __k_ti_chain_entry {
    void*    ti;
    unsigned int offset;
    unsigned int _pad;
};

static const struct __k_ti_chain_entry __k_memory_exception_ti_chain[] = {
    { &_KTRIN1k15MemoryExceptionE,  0 },   /* self */
    { &_KTRIN1k16RuntimeExceptionE, 8 },   /* RuntimeException base at offset 8 */
    { &_KTRIN1k9ExceptionE,        16 },   /* Exception base at offset 16 */
    { &_KTRIN1k6ObjectE,           24 },   /* Object base at offset 24 */
    { (void*)0,                     0 }    /* null terminator */
};

/* Size of MemoryException object:
 * {vptr, RuntimeException{vptr, Exception{vptr, Object{vptr}, int _code, pad}}}
 * = 40 bytes on LP64. */
#define K_MEMORY_EXCEPTION_SIZE 40

__attribute__((noreturn, cold))
void __k_fatal_memory_allocation(void) {
    /* 1. Allocate exception storage (uses emergency buffer under OOM) */
    void* exc_mem = __cxa_allocate_exception(K_MEMORY_EXCEPTION_SIZE);

    /* 2. Construct MemoryException in-place (sets up vtable chain + _code=1) */
    _KFMN1k15MemoryExceptionC1Ev(exc_mem);

    /* 3. Set up typeinfo chain for polymorphic catch dispatch */
    _k_thrown_typeinfo_chain = (void*)__k_memory_exception_ti_chain;
    _k_thrown_typeinfo = &_KTRIN1k15MemoryExceptionE;

    /* 4. Throw — does not return, unwinds to nearest matching catch */
    __cxa_throw(exc_mem, &_KTRIN1k15MemoryExceptionE, (void(*)(void*))0);
}



