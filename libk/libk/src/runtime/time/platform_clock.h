/*
 * K Language runtime — platform clock abstraction (C)
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

#ifndef KLANG_RUNTIME_PLATFORM_CLOCK_H
#define KLANG_RUNTIME_PLATFORM_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Status codes */
#define K_CLOCK_STATUS_OK           0
#define K_CLOCK_STATUS_UNAVAILABLE  1
#define K_CLOCK_STATUS_FAILED       2
#define K_CLOCK_STATUS_UNSUPPORTED  3
#define K_CLOCK_STATUS_INVALID      4

/* Clock types / kinds */
enum {
    K_CLOCK_REALTIME          = 0,
    K_CLOCK_MONOTONIC_ACTIVE  = 1,
    K_CLOCK_MONOTONIC_ELAPSED = 2,
    K_CLOCK_PROCESS_CPU       = 3,
    K_CLOCK_THREAD_CPU        = 4
};

typedef struct {
    int64_t seconds;
    int32_t nanos;
    int32_t status;
} KPlatformClockReading;

KPlatformClockReading k_platform_clock_realtime(void);
KPlatformClockReading k_platform_clock_monotonic_active(void);
KPlatformClockReading k_platform_clock_monotonic_elapsed(void);
KPlatformClockReading k_platform_clock_process_cpu(void);
KPlatformClockReading k_platform_clock_thread_cpu(void);
KPlatformClockReading k_platform_clock_get_resolution(int clock_type);

/* Seam for tests */
typedef KPlatformClockReading (*KPlatformClockProviderFn)(int clock_kind);
void k_platform_clock_set_provider_seam(KPlatformClockProviderFn seam);

#ifdef __cplusplus
}
#endif

#endif /* KLANG_RUNTIME_PLATFORM_CLOCK_H */
