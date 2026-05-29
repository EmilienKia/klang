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
 *
 * All fatal helpers throw a K FatalError-derived exception using the Itanium
 * C++ ABI (__cxa_allocate_exception + __cxa_throw), enabling proper stack
 * unwinding and catch dispatch.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

/* ══════════════════════════════════════════════════════════════════════════════
 * Itanium C++ ABI functions
 * ══════════════════════════════════════════════════════════════════════════════ */
extern void* __cxa_allocate_exception(size_t thrown_size);
extern void  __cxa_throw(void* thrown_exception, void* tinfo, void (*dest)(void*))
    __attribute__((noreturn));

/* ══════════════════════════════════════════════════════════════════════════════
 * K runtime symbols (from libk.so) — constructors
 * ══════════════════════════════════════════════════════════════════════════════ */

/* OutOfMemory() default constructor */
extern void _KFMN1k11OutOfMemoryC1Ev(void* self);

/* NullDereferenceError() default constructor */
extern void _KFMN1k20NullDereferenceErrorC1Ev(void* self);

/* NullAssignationError() default constructor */
extern void _KFMN1k20NullAssignationErrorC1Ev(void* self);

/* NullCastError() default constructor */
extern void _KFMN1k13NullCastErrorC1Ev(void* self);

/* IndexOutOfBoundsError() default constructor */
extern void _KFMN1k21IndexOutOfBoundsErrorC1Ev(void* self);

/* ══════════════════════════════════════════════════════════════════════════════
 * RTTI typeinfo globals
 * ══════════════════════════════════════════════════════════════════════════════ */

extern char _KTRIN1k11OutOfMemoryE;
extern char _KTRIN1k10FatalErrorE;
extern char _KTRIN1k9ThrowableE;
extern char _KTRIN1k6ObjectE;

extern char _KTRIN1k16NullPointerErrorE;
extern char _KTRIN1k20NullDereferenceErrorE;
extern char _KTRIN1k20NullAssignationErrorE;
extern char _KTRIN1k13NullCastErrorE;
extern char _KTRIN1k21IndexOutOfBoundsErrorE;

/* ══════════════════════════════════════════════════════════════════════════════
 * Typeinfo chain globals (weak — may already be emitted by the compiler)
 * ══════════════════════════════════════════════════════════════════════════════ */

/*
 * _k_thrown_typeinfo_chain — pointer to the null-terminated typeinfo chain
 * of the most recently thrown exception in this thread/module.  The catch
 * dispatch code reads this to perform polymorphic type matching.
 *
 * Each entry is { void* typeinfo, uint32_t byte_offset, uint32_t pad }
 * (16 bytes on LP64).
 */
__attribute__((weak))
void* _k_thrown_typeinfo_chain = (void*)0;

/* Primary (non-chain) typeinfo pointer */
__attribute__((weak))
void* _k_thrown_typeinfo = (void*)0;

/* ══════════════════════════════════════════════════════════════════════════════
 * Typeinfo chain entry layout
 * ══════════════════════════════════════════════════════════════════════════════ */

struct __k_ti_chain_entry {
    void*    ti;
    unsigned int offset;
    unsigned int _pad;
};

/* ══════════════════════════════════════════════════════════════════════════════
 * Object sizes
 *
 * On LP64, each vptr is 8 bytes. The _code field is int (4 bytes) + 4 pad.
 *
 * 4-level hierarchy (OutOfMemory, IndexOutOfBoundsError):
 *   Class -> FatalError -> Throwable -> Object
 *   = 4 vptrs (32) + int _code (4) + pad (4) = 40 bytes
 *
 * 5-level hierarchy (NullDereferenceError, NullAssignationError, NullCastError):
 *   Class -> NullPointerError -> FatalError -> Throwable -> Object
 *   = 5 vptrs (40) + int _code (4) + pad (4) = 48 bytes
 * ══════════════════════════════════════════════════════════════════════════════ */

#define K_OUT_OF_MEMORY_SIZE           40
#define K_NULL_POINTER_ERROR_SIZE      48  /* 5-level: via NullPointerError */
#define K_INDEX_OUT_OF_BOUNDS_SIZE     40  /* 4-level: directly from FatalError */

/* ══════════════════════════════════════════════════════════════════════════════
 * Static typeinfo chains
 * ══════════════════════════════════════════════════════════════════════════════ */

/* OutOfMemory -> FatalError -> Throwable -> Object */
static const struct __k_ti_chain_entry __k_out_of_memory_ti_chain[] = {
    { &_KTRIN1k11OutOfMemoryE,   0 },   /* self */
    { &_KTRIN1k10FatalErrorE,    8 },   /* FatalError base at offset 8 */
    { &_KTRIN1k9ThrowableE,     16 },   /* Throwable base at offset 16 */
    { &_KTRIN1k6ObjectE,        24 },   /* Object base at offset 24 */
    { (void*)0,                  0 }    /* null terminator */
};

/* NullDereferenceError -> NullPointerError -> FatalError -> Throwable -> Object */
static const struct __k_ti_chain_entry __k_null_dereference_ti_chain[] = {
    { &_KTRIN1k20NullDereferenceErrorE,  0 },   /* self */
    { &_KTRIN1k16NullPointerErrorE,      8 },   /* NullPointerError base at offset 8 */
    { &_KTRIN1k10FatalErrorE,           16 },   /* FatalError base at offset 16 */
    { &_KTRIN1k9ThrowableE,             24 },   /* Throwable base at offset 24 */
    { &_KTRIN1k6ObjectE,                32 },   /* Object base at offset 32 */
    { (void*)0,                          0 }    /* null terminator */
};

/* NullAssignationError -> NullPointerError -> FatalError -> Throwable -> Object */
static const struct __k_ti_chain_entry __k_null_assignation_ti_chain[] = {
    { &_KTRIN1k20NullAssignationErrorE,  0 },   /* self */
    { &_KTRIN1k16NullPointerErrorE,      8 },   /* NullPointerError base at offset 8 */
    { &_KTRIN1k10FatalErrorE,           16 },   /* FatalError base at offset 16 */
    { &_KTRIN1k9ThrowableE,             24 },   /* Throwable base at offset 24 */
    { &_KTRIN1k6ObjectE,                32 },   /* Object base at offset 32 */
    { (void*)0,                          0 }    /* null terminator */
};

/* NullCastError -> NullPointerError -> FatalError -> Throwable -> Object */
static const struct __k_ti_chain_entry __k_null_cast_ti_chain[] = {
    { &_KTRIN1k13NullCastErrorE,         0 },   /* self */
    { &_KTRIN1k16NullPointerErrorE,      8 },   /* NullPointerError base at offset 8 */
    { &_KTRIN1k10FatalErrorE,           16 },   /* FatalError base at offset 16 */
    { &_KTRIN1k9ThrowableE,             24 },   /* Throwable base at offset 24 */
    { &_KTRIN1k6ObjectE,                32 },   /* Object base at offset 32 */
    { (void*)0,                          0 }    /* null terminator */
};

/* IndexOutOfBoundsError -> FatalError -> Throwable -> Object */
static const struct __k_ti_chain_entry __k_index_out_of_bounds_ti_chain[] = {
    { &_KTRIN1k21IndexOutOfBoundsErrorE,  0 },   /* self */
    { &_KTRIN1k10FatalErrorE,             8 },   /* FatalError base at offset 8 */
    { &_KTRIN1k9ThrowableE,              16 },   /* Throwable base at offset 16 */
    { &_KTRIN1k6ObjectE,                 24 },   /* Object base at offset 24 */
    { (void*)0,                           0 }    /* null terminator */
};

/* ══════════════════════════════════════════════════════════════════════════════
 * Helper macro: allocate, construct, set typeinfo chain, throw
 * ══════════════════════════════════════════════════════════════════════════════ */

#define K_THROW_FATAL(size, ctor_fn, ti_symbol, ti_chain)               \
    do {                                                                  \
        void* exc_mem = __cxa_allocate_exception(size);                  \
        ctor_fn(exc_mem);                                                \
        _k_thrown_typeinfo_chain = (void*)(ti_chain);                    \
        _k_thrown_typeinfo = &(ti_symbol);                               \
        __cxa_throw(exc_mem, &(ti_symbol), (void(*)(void*))0);          \
    } while(0)

/* ══════════════════════════════════════════════════════════════════════════════
 * Null-pointer fatal handlers — throw NullDereferenceError / etc.
 * ══════════════════════════════════════════════════════════════════════════════ */

__attribute__((noreturn, cold))
void __k_fatal_null_dereference(void) {
    K_THROW_FATAL(K_NULL_POINTER_ERROR_SIZE,
                  _KFMN1k20NullDereferenceErrorC1Ev,
                  _KTRIN1k20NullDereferenceErrorE,
                  __k_null_dereference_ti_chain);
}

__attribute__((noreturn, cold))
void __k_fatal_null_assignation(void) {
    K_THROW_FATAL(K_NULL_POINTER_ERROR_SIZE,
                  _KFMN1k20NullAssignationErrorC1Ev,
                  _KTRIN1k20NullAssignationErrorE,
                  __k_null_assignation_ti_chain);
}

__attribute__((noreturn, cold))
void __k_fatal_null_dyncast(void) {
    K_THROW_FATAL(K_NULL_POINTER_ERROR_SIZE,
                  _KFMN1k13NullCastErrorC1Ev,
                  _KTRIN1k13NullCastErrorE,
                  __k_null_cast_ti_chain);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Array bounds check failure — throw IndexOutOfBoundsError
 * ══════════════════════════════════════════════════════════════════════════════ */

__attribute__((noreturn, cold))
void __k_fatal_array_bounds_check_failed(unsigned index, unsigned size) {
    (void)index;
    (void)size;
    K_THROW_FATAL(K_INDEX_OUT_OF_BOUNDS_SIZE,
                  _KFMN1k21IndexOutOfBoundsErrorC1Ev,
                  _KTRIN1k21IndexOutOfBoundsErrorE,
                  __k_index_out_of_bounds_ti_chain);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Memory allocation failure — throw OutOfMemory
 * ══════════════════════════════════════════════════════════════════════════════ */

__attribute__((noreturn, cold))
void __k_fatal_memory_allocation(void) {
    K_THROW_FATAL(K_OUT_OF_MEMORY_SIZE,
                  _KFMN1k11OutOfMemoryC1Ev,
                  _KTRIN1k11OutOfMemoryE,
                  __k_out_of_memory_ti_chain);
}
