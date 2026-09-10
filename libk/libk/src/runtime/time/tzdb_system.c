/*
 * K Language runtime — System TZDB provider implementation
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

#include "tzdb_system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>

#define DEFAULT_TZDB_ROOT "/usr/share/zoneinfo"

static char g_tzdb_root[PATH_MAX] = DEFAULT_TZDB_ROOT;
static char g_cached_version[64] = "";

void k_tzdb_set_root(const char* root_path) {
    if (root_path && *root_path) {
        strncpy(g_tzdb_root, root_path, sizeof(g_tzdb_root) - 1);
        g_tzdb_root[sizeof(g_tzdb_root) - 1] = '\0';
    } else {
        strncpy(g_tzdb_root, DEFAULT_TZDB_ROOT, sizeof(g_tzdb_root) - 1);
        g_tzdb_root[sizeof(g_tzdb_root) - 1] = '\0';
    }
    g_cached_version[0] = '\0';
}

const char* k_tzdb_get_root(void) {
    return g_tzdb_root;
}

const char* k_tzdb_get_version(void) {
    if (g_cached_version[0] != '\0') {
        return g_cached_version;
    }

    /* 1. Try reading tzdata.zi in the root directory */
    char zi_path[PATH_MAX + 32];
    snprintf(zi_path, sizeof(zi_path), "%s/tzdata.zi", g_tzdb_root);
    FILE* fp = fopen(zi_path, "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            char* p = line;
            while (*p && isspace((unsigned char)*p)) p++;
            if (strncmp(p, "# version ", 10) == 0 || strncmp(p, "#version ", 9) == 0) {
                char* v = (p[1] == 'v') ? p + 9 : p + 10;
                while (*v && isspace((unsigned char)*v)) v++;
                char* end = v;
                while (*end && !isspace((unsigned char)*end)) end++;
                *end = '\0';
                if (*v) {
                    strncpy(g_cached_version, v, sizeof(g_cached_version) - 1);
                    g_cached_version[sizeof(g_cached_version) - 1] = '\0';
                    fclose(fp);
                    return g_cached_version;
                }
            }
        }
        fclose(fp);
    }

    /* 2. Try reading version file in the root directory */
    char ver_path[PATH_MAX + 32];
    snprintf(ver_path, sizeof(ver_path), "%s/version", g_tzdb_root);
    fp = fopen(ver_path, "r");
    if (fp) {
        char line[64];
        if (fgets(line, sizeof(line), fp)) {
            char* p = line;
            while (*p && isspace((unsigned char)*p)) p++;
            char* end = p;
            while (*end && !isspace((unsigned char)*end)) end++;
            *end = '\0';
            if (*p) {
                strncpy(g_cached_version, p, sizeof(g_cached_version) - 1);
                g_cached_version[sizeof(g_cached_version) - 1] = '\0';
                fclose(fp);
                return g_cached_version;
            }
        }
        fclose(fp);
    }

    strncpy(g_cached_version, "unknown", sizeof(g_cached_version) - 1);
    g_cached_version[sizeof(g_cached_version) - 1] = '\0';
    return g_cached_version;
}

KZoneRulesSnapshot* k_tzdb_load_zone(const char* zone_id) {
    if (!zone_id || !*zone_id) {
        return NULL;
    }

    /* Validate zone identifier syntax */
    if (zone_id[0] == '/') return NULL;
    if (strstr(zone_id, "..")) return NULL;
    if (strstr(zone_id, "//")) return NULL;

    size_t id_len = strlen(zone_id);
    if (zone_id[id_len - 1] == '/') return NULL;

    for (size_t i = 0; i < id_len; ++i) {
        char c = zone_id[i];
        if (!isalnum((unsigned char)c) && c != '/' && c != '_' && c != '-' && c != '+') {
            return NULL;
        }
    }

    char full_path[PATH_MAX + 512];
    int n = snprintf(full_path, sizeof(full_path), "%s/%s", g_tzdb_root, zone_id);
    if (n < 0 || (size_t)n >= sizeof(full_path)) {
        return NULL;
    }

    char resolved[PATH_MAX];
    if (!realpath(full_path, resolved)) {
        return NULL;
    }

    char root_resolved[PATH_MAX];
    if (realpath(g_tzdb_root, root_resolved)) {
        size_t rlen = strlen(root_resolved);
        if (strncmp(resolved, root_resolved, rlen) != 0 ||
            (resolved[rlen] != '/' && resolved[rlen] != '\0')) {
            /* Path escaped configured root! */
            return NULL;
        }
    }

    FILE* fp = fopen(resolved, "rb");
    if (!fp) {
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long sz = ftell(fp);
    if (sz <= 0 || sz > 10 * 1024 * 1024) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    size_t rd = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    if (rd != (size_t)sz) {
        free(buf);
        return NULL;
    }

    KZoneRulesSnapshot* snap = k_tzif_parse(buf, (size_t)sz);
    free(buf);
    return snap;
}

void k_tzdb_snapshot_release(KZoneRulesSnapshot* snap) {
    k_zone_rules_snapshot_release(snap);
}
