/*
 * K Language runtime — time helpers (C)
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
 *
 * Provides nanosecond-precision time reading from POSIX clocks.
 * Called by k::Duration and k::Instant from time.k.
 */

#include <stdint.h>
#include <time.h>

/**
 * Return the current CLOCK_MONOTONIC value as a signed 64-bit nanosecond
 * count.  Used by k::Instant::now().
 */
int64_t __k_time_monotonic_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

/**
 * Return the current CLOCK_REALTIME value as a signed 64-bit nanosecond
 * count since the Unix epoch.  Not used by the runtime clock but available
 * for calendar/wall-clock purposes.
 */
int64_t __k_time_realtime_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}
