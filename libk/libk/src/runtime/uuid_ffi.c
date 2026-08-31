/*
 * K Language runtime — UUID FFI helpers (C)
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
 * Provides low-level system access for UUID generation:
 *   - Cryptographically secure random generation (getrandom / urandom)
 *   - Gregorian 100-nanosecond timestamp calculation (RFC 4122 / RFC 9562)
 *   - Unix epoch millisecond timestamp calculation
 *   - MAC address / node ID discovery
 *   - Clock sequence state management
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>

/**
 * Offset in 100-nanosecond intervals between the Gregorian calendar reform
 * (1582-10-15 00:00:00 UTC) and the Unix epoch (1970-01-01 00:00:00 UTC).
 * 12219292800 seconds * 10,000,000 = 122192928000000000 (0x01B21DD213814000ULL).
 */
#define GREGORIAN_OFFSET_100NS 0x01B21DD213814000ULL

static uint64_t g_cached_node_id = 0;
static int g_node_id_initialized = 0;
static uint16_t g_clock_sequence = 0;
static int g_clock_seq_initialized = 0;

/**
 * Fill a buffer with cryptographically secure random bytes.
 */
void __k_uuid_random_bytes(uint8_t* out, int len) {
    if (!out || len <= 0) {
        return;
    }

    // Try getrandom first (non-blocking)
    ssize_t ret = getrandom(out, (size_t)len, 0);
    if (ret == (ssize_t)len) {
        return;
    }

    // Fallback to /dev/urandom
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t total = 0;
        while (total < len) {
            ssize_t r = read(fd, out + total, (size_t)(len - total));
            if (r <= 0) {
                break;
            }
            total += r;
        }
        close(fd);
        if (total == len) {
            return;
        }
    }

    // Secondary fallback using pseudo-random and time entropy
    for (int i = 0; i < len; ++i) {
        out[i] = (uint8_t)(rand() & 0xFF);
    }
}

/**
 * Return a 64-bit random value.
 */
uint64_t __k_uuid_random_u64(void) {
    uint64_t val = 0;
    __k_uuid_random_bytes((uint8_t*)&val, sizeof(val));
    return val;
}

/**
 * Return the current time as 100-nanosecond intervals since October 15, 1582
 * (RFC 4122 / RFC 9562 Gregorian timestamp).
 */
uint64_t __k_uuid_gregorian_time_100ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t intervals = ((uint64_t)ts.tv_sec * 10000000ULL) + ((uint64_t)ts.tv_nsec / 100ULL);
    return intervals + GREGORIAN_OFFSET_100NS;
}

/**
 * Return the current Unix epoch time in milliseconds (for UUIDv7).
 */
uint64_t __k_uuid_unix_time_millis(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

/**
 * Return the 48-bit node identifier (hardware MAC address or multicast random ID).
 */
uint64_t __k_uuid_get_node_id(void) {
    if (g_node_id_initialized) {
        return g_cached_node_id;
    }

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        struct ifconf ifc;
        char buf[1024];
        ifc.ifc_len = sizeof(buf);
        ifc.ifc_buf = buf;
        if (ioctl(sock, SIOCGIFCONF, &ifc) == 0) {
            struct ifreq* ifr = ifc.ifc_req;
            int count = ifc.ifc_len / (int)sizeof(struct ifreq);
            for (int i = 0; i < count; ++i) {
                struct ifreq req;
                memset(&req, 0, sizeof(req));
                strncpy(req.ifr_name, ifr[i].ifr_name, IFNAMSIZ - 1);

                if (ioctl(sock, SIOCGIFFLAGS, &req) == 0) {
                    if (req.ifr_flags & IFF_LOOPBACK) {
                        continue;
                    }
                }

                if (ioctl(sock, SIOCGIFHWADDR, &req) == 0) {
                    unsigned char* mac = (unsigned char*)req.ifr_hwaddr.sa_data;
                    if (mac[0] || mac[1] || mac[2] || mac[3] || mac[4] || mac[5]) {
                        uint64_t node = 0;
                        for (int j = 0; j < 6; ++j) {
                            node = (node << 8) | (uint64_t)mac[j];
                        }
                        g_cached_node_id = node & 0xFFFFFFFFFFFFULL;
                        g_node_id_initialized = 1;
                        close(sock);
                        return g_cached_node_id;
                    }
                }
            }
        }
        close(sock);
    }

    // Fallback: generate 48-bit random with multicast bit set (RFC 4122 Section 4.1.6)
    uint64_t rnd = __k_uuid_random_u64();
    g_cached_node_id = (rnd & 0xFFFFFFFFFFFFULL) | 0x010000000000ULL;
    g_node_id_initialized = 1;
    return g_cached_node_id;
}

/**
 * Return a 14-bit clock sequence (0..16383).
 */
uint16_t __k_uuid_get_clock_sequence(void) {
    if (!g_clock_seq_initialized) {
        uint64_t rnd = __k_uuid_random_u64();
        g_clock_sequence = (uint16_t)(rnd & 0x3FFFU);
        g_clock_seq_initialized = 1;
    } else {
        g_clock_sequence = (uint16_t)((g_clock_sequence + 1U) & 0x3FFFU);
    }
    return g_clock_sequence;
}
