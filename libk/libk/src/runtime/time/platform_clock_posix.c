/*
 * K Language runtime — POSIX platform clock implementation (C)
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

#define _GNU_SOURCE
#include "platform_clock.h"

#include <time.h>
#include <errno.h>

static KPlatformClockProviderFn g_provider_seam = (void*)0;

void k_platform_clock_set_provider_seam(KPlatformClockProviderFn seam) {
    g_provider_seam = seam;
}

static KPlatformClockReading normalize_and_pack(int64_t sec, int64_t nsec) {
    if (nsec < 0 || nsec >= 1000000000LL) {
        int64_t sec_adj = nsec / 1000000000LL;
        int64_t nano_rem = nsec % 1000000000LL;
        if (nano_rem < 0) {
            sec_adj -= 1;
            nano_rem += 1000000000LL;
        }
        sec += sec_adj;
        nsec = nano_rem;
    }
    KPlatformClockReading r;
    r.seconds = sec;
    r.nanos = (int32_t)nsec;
    r.status = K_CLOCK_STATUS_OK;
    return r;
}

static KPlatformClockReading read_posix_clock(clockid_t clk, int clock_kind) {
    if (g_provider_seam != (void*)0) {
        return g_provider_seam(clock_kind);
    }
    struct timespec ts;
    if (clock_gettime(clk, &ts) < 0) {
        KPlatformClockReading r;
        r.seconds = 0;
        r.nanos = 0;
        r.status = (errno == EINVAL || errno == ENOTSUP) ? K_CLOCK_STATUS_UNSUPPORTED : K_CLOCK_STATUS_FAILED;
        return r;
    }
    return normalize_and_pack(ts.tv_sec, ts.tv_nsec);
}

KPlatformClockReading k_platform_clock_realtime(void) {
    return read_posix_clock(CLOCK_REALTIME, K_CLOCK_REALTIME);
}

KPlatformClockReading k_platform_clock_monotonic_active(void) {
    return read_posix_clock(CLOCK_MONOTONIC, K_CLOCK_MONOTONIC_ACTIVE);
}

KPlatformClockReading k_platform_clock_monotonic_elapsed(void) {
#ifdef CLOCK_BOOTTIME
    return read_posix_clock(CLOCK_BOOTTIME, K_CLOCK_MONOTONIC_ELAPSED);
#else
    return read_posix_clock(CLOCK_MONOTONIC, K_CLOCK_MONOTONIC_ELAPSED);
#endif
}

KPlatformClockReading k_platform_clock_process_cpu(void) {
#ifdef CLOCK_PROCESS_CPUTIME_ID
    return read_posix_clock(CLOCK_PROCESS_CPUTIME_ID, K_CLOCK_PROCESS_CPU);
#else
    if (g_provider_seam != (void*)0) {
        return g_provider_seam(K_CLOCK_PROCESS_CPU);
    }
    KPlatformClockReading r = {0, 0, K_CLOCK_STATUS_UNSUPPORTED};
    return r;
#endif
}

KPlatformClockReading k_platform_clock_thread_cpu(void) {
#ifdef CLOCK_THREAD_CPUTIME_ID
    return read_posix_clock(CLOCK_THREAD_CPUTIME_ID, K_CLOCK_THREAD_CPU);
#else
    if (g_provider_seam != (void*)0) {
        return g_provider_seam(K_CLOCK_THREAD_CPU);
    }
    KPlatformClockReading r = {0, 0, K_CLOCK_STATUS_UNSUPPORTED};
    return r;
#endif
}

KPlatformClockReading k_platform_clock_get_resolution(int clock_type) {
    if (g_provider_seam != (void*)0) {
        KPlatformClockReading seam_res = g_provider_seam(clock_type);
        if (seam_res.status != K_CLOCK_STATUS_OK) {
            return seam_res;
        }
    }
    clockid_t clk;
    switch (clock_type) {
        case K_CLOCK_REALTIME:
            clk = CLOCK_REALTIME;
            break;
        case K_CLOCK_MONOTONIC_ACTIVE:
            clk = CLOCK_MONOTONIC;
            break;
        case K_CLOCK_MONOTONIC_ELAPSED:
#ifdef CLOCK_BOOTTIME
            clk = CLOCK_BOOTTIME;
#else
            clk = CLOCK_MONOTONIC;
#endif
            break;
        case K_CLOCK_PROCESS_CPU:
#ifdef CLOCK_PROCESS_CPUTIME_ID
            clk = CLOCK_PROCESS_CPUTIME_ID;
            break;
#else
            {
                KPlatformClockReading r = {0, 0, K_CLOCK_STATUS_UNSUPPORTED};
                return r;
            }
#endif
        case K_CLOCK_THREAD_CPU:
#ifdef CLOCK_THREAD_CPUTIME_ID
            clk = CLOCK_THREAD_CPUTIME_ID;
            break;
#else
            {
                KPlatformClockReading r = {0, 0, K_CLOCK_STATUS_UNSUPPORTED};
                return r;
            }
#endif
        default:
            {
                KPlatformClockReading r = {0, 0, K_CLOCK_STATUS_UNSUPPORTED};
                return r;
            }
    }
    struct timespec ts;
    if (clock_getres(clk, &ts) < 0) {
        KPlatformClockReading r = {0, 0, K_CLOCK_STATUS_FAILED};
        return r;
    }
    return normalize_and_pack(ts.tv_sec, ts.tv_nsec);
}
