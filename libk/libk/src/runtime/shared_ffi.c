/*
 * K Language runtime — atomic shared ownership counters
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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* K owns the int fields; compiler atomic builtins avoid imposing a C
 * _Atomic layout on K storage. All accesses after publication are atomic. */
static void count_error(const char* message) {
    fprintf(stderr, "K runtime: shared reference count %s\n", message);
    abort();
}

int32_t __k_shared_count(const int32_t* count) {
    return __atomic_load_n(count, __ATOMIC_RELAXED);
}

static bool try_retain(int32_t* count, int success_order) {
    int32_t current = __atomic_load_n(count, __ATOMIC_RELAXED);
    while (current != 0) {
        if (current < 0) {
            count_error("is invalid");
        }
        if (current == INT32_MAX) {
            count_error("overflow");
        }
        if (__atomic_compare_exchange_n(count, &current, current + 1, true,
                                        success_order, __ATOMIC_RELAXED)) {
            return true;
        }
    }
    return false;
}

void __k_shared_retain(int32_t* count) {
    if (!try_retain(count, __ATOMIC_RELAXED)) {
        count_error("cannot retain a released group");
    }
}

bool __k_shared_try_retain(int32_t* count) {
    return try_retain(count, __ATOMIC_ACQUIRE);
}

bool __k_shared_release(int32_t* count) {
    int32_t previous = __atomic_fetch_sub(count, 1, __ATOMIC_RELEASE);
    if (previous <= 0) {
        count_error("underflow");
    }
    if (previous == 1) {
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        return true;
    }
    return false;
}
