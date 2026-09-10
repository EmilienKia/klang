/*
 * K Language runtime — Time FFI wrappers (C)
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

#include "platform_clock.h"
#include <stdint.h>

static __thread KPlatformClockReading g_last_reading;

int32_t __k_time_clock_read_realtime(void) {
    g_last_reading = k_platform_clock_realtime();
    return g_last_reading.status;
}

int32_t __k_time_clock_read_monotonic_active(void) {
    g_last_reading = k_platform_clock_monotonic_active();
    return g_last_reading.status;
}

int32_t __k_time_clock_read_monotonic_elapsed(void) {
    g_last_reading = k_platform_clock_monotonic_elapsed();
    return g_last_reading.status;
}

int32_t __k_time_clock_read_process_cpu(void) {
    g_last_reading = k_platform_clock_process_cpu();
    return g_last_reading.status;
}

int32_t __k_time_clock_read_thread_cpu(void) {
    g_last_reading = k_platform_clock_thread_cpu();
    return g_last_reading.status;
}

int32_t __k_time_clock_read_resolution(int32_t clock_type) {
    g_last_reading = k_platform_clock_get_resolution(clock_type);
    return g_last_reading.status;
}

int64_t __k_time_clock_get_reading_sec(void) {
    return g_last_reading.seconds;
}

int32_t __k_time_clock_get_reading_nano(void) {
    return g_last_reading.nanos;
}

int32_t __k_time_clock_get_reading_status(void) {
    return g_last_reading.status;
}

int64_t __k_time_clock_get_realtime_sec(void) {
    return g_last_reading.seconds;
}

int32_t __k_time_clock_get_realtime_nano(void) {
    return g_last_reading.nanos;
}

int32_t __k_time_clock_get_realtime_status(void) {
    return g_last_reading.status;
}

int32_t __k_time_clock_map_status(int32_t status) {
    switch (status) {
        case K_CLOCK_STATUS_OK:
            return 0;
        case K_CLOCK_STATUS_UNAVAILABLE:
            return 510;
        case K_CLOCK_STATUS_FAILED:
            return 510;
        case K_CLOCK_STATUS_UNSUPPORTED:
            return 510;
        default:
            return 510;
    }
}
