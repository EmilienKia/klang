/*
 * K Language runtime — fatal exception allocation layouts
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

#ifndef K_FATAL_ABI_H
#define K_FATAL_ABI_H

/*
 * Mirror exception.k's single-inheritance layouts, including Object's vptr
 * and Throwable's cause fields. Runtime allocations must cover every byte
 * written by the K constructors. The RTTI/exception tests compare these
 * layouts against the imported KDI types to detect ABI drift.
 */
struct k_throwable_layout {
    void* vptr;
    void* object_vptr;
    int code;
    void* cause;
    void* cause_handle;
};

struct k_fatal_error_layout {
    void* vptr;
    struct k_throwable_layout base;
};

struct k_null_pointer_error_layout {
    void* vptr;
    struct k_fatal_error_layout base;
};

/* OutOfMemory and IndexOutOfBoundsError. */
struct k_direct_fatal_error_layout {
    void* vptr;
    struct k_fatal_error_layout base;
};

/* NullDereferenceError, NullAssignationError and NullCastError. */
struct k_null_fatal_error_layout {
    void* vptr;
    struct k_null_pointer_error_layout base;
};

#endif
