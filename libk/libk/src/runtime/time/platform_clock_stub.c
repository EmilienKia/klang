/*
 * K Language runtime — Generic platform clock stub (C)
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

KPlatformClockReading k_platform_clock_realtime(void) {
    KPlatformClockReading r = {0, 0, K_CLOCK_STATUS_UNSUPPORTED};
    return r;
}

KPlatformClockReading k_platform_clock_monotonic_active(void) {
    KPlatformClockReading r = {0, 0, K_CLOCK_STATUS_UNSUPPORTED};
    return r;
}

KPlatformClockReading k_platform_clock_monotonic_elapsed(void) {
    KPlatformClockReading r = {0, 0, K_CLOCK_STATUS_UNSUPPORTED};
    return r;
}

KPlatformClockReading k_platform_clock_process_cpu(void) {
    KPlatformClockReading r = {0, 0, K_CLOCK_STATUS_UNSUPPORTED};
    return r;
}

KPlatformClockReading k_platform_clock_thread_cpu(void) {
    KPlatformClockReading r = {0, 0, K_CLOCK_STATUS_UNSUPPORTED};
    return r;
}

KPlatformClockReading k_platform_clock_get_resolution(int clock_type) {
    (void)clock_type;
    KPlatformClockReading r = {0, 0, K_CLOCK_STATUS_UNSUPPORTED};
    return r;
}

void k_platform_clock_set_provider_seam(KPlatformClockProviderFn seam) {
    (void)seam;
}
