/*
 * K Language runtime — File I/O C wrappers
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
 * Thin wrappers around POSIX / C stdio functions for the K standard library
 * File I/O classes (File, FileDescriptor, FileInputStream, FileOutputStream).
 *
 * All functions are prefixed __k_io_file_ and use void* for the opaque
 * FILE* handle (mapped to CFile* on the K side).
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

/* ── UTF-32 (K char) → UTF-8 path transcoding ───────────────────────────────
 * K `char` is a 32-bit Unicode scalar value (UTF-32). Filesystem paths passed
 * to libc must be UTF-8 byte strings, so every path/mode argument coming from
 * the K side is a null-terminated UTF-32 string that we transcode here. */

#define K_PATH_BUF 8192

static void k_utf32_to_utf8(const uint32_t* src, char* dst, size_t dst_cap) {
    size_t out = 0;
    if (!src) { if (dst_cap) dst[0] = '\0'; return; }
    for (size_t i = 0; src[i] != 0 && out + 4 < dst_cap; ++i) {
        uint32_t cp = src[i];
        if (cp < 0x80) {
            dst[out++] = (char)cp;
        } else if (cp < 0x800) {
            dst[out++] = (char)(0xC0 | (cp >> 6));
            dst[out++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            dst[out++] = (char)(0xE0 | (cp >> 12));
            dst[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[out++] = (char)(0x80 | (cp & 0x3F));
        } else {
            dst[out++] = (char)(0xF0 | (cp >> 18));
            dst[out++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            dst[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[out++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    dst[out] = '\0';
}

/* ── FILE* open / close ─────────────────────────────────────────────────── */

void* __k_io_file_fopen(const uint32_t* path, const uint32_t* mode) {
    char pbuf[K_PATH_BUF];
    char mbuf[16];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    k_utf32_to_utf8(mode, mbuf, sizeof(mbuf));
    return (void*)fopen(pbuf, mbuf);
}

int __k_io_file_fclose(void* fp) {
    if (!fp) return -1;
    return fclose((FILE*)fp);
}

/* ── Single-byte read / write ───────────────────────────────────────────── */

int __k_io_file_fread_byte(void* fp) {
    if (!fp) return -1;
    int c = fgetc((FILE*)fp);
    return (c == EOF) ? -1 : c;
}

int __k_io_file_fwrite_byte(void* fp, int32_t b) {
    if (!fp) return -1;
    int c = fputc(b & 0xFF, (FILE*)fp);
    return (c == EOF) ? -1 : 0;
}

/* ── Bulk read / write ──────────────────────────────────────────────────── */

int32_t __k_io_file_fread(void* fp, uint8_t* buf, int32_t len) {
    if (!fp || !buf || len <= 0) return -1;
    size_t n = fread(buf, 1, (size_t)len, (FILE*)fp);
    if (n == 0) {
        if (feof((FILE*)fp)) return 0;
        return ferror((FILE*)fp) ? -1 : 0;
    }
    return (int32_t)n;
}

int32_t __k_io_file_fwrite(void* fp, const uint8_t* buf, int32_t len) {
    if (!fp || !buf || len <= 0) return 0;
    size_t n = fwrite(buf, 1, (size_t)len, (FILE*)fp);
    return (int32_t)n;
}

/* ── Flush ──────────────────────────────────────────────────────────────── */

int __k_io_file_fflush(void* fp) {
    if (!fp) return -1;
    return fflush((FILE*)fp);
}

/* ── FileDescriptor helpers ─────────────────────────────────────────────── */

int __k_io_file_fileno(void* fp) {
    if (!fp) return -1;
    return fileno((FILE*)fp);
}

void* __k_io_file_get_stdin(void) {
    return (void*)stdin;
}

void* __k_io_file_get_stdout(void) {
    return (void*)stdout;
}

void* __k_io_file_get_stderr(void) {
    return (void*)stderr;
}

/* ── File metadata (stat-based) ─────────────────────────────────────────── */

int __k_io_file_exists(const uint32_t* path) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    struct stat st;
    return (stat(pbuf, &st) == 0) ? 1 : 0;
}

int __k_io_file_is_file(const uint32_t* path) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    struct stat st;
    if (stat(pbuf, &st) != 0) return 0;
    return S_ISREG(st.st_mode) ? 1 : 0;
}

int __k_io_file_is_directory(const uint32_t* path) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    struct stat st;
    if (stat(pbuf, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

int64_t __k_io_file_length(const uint32_t* path) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    struct stat st;
    if (stat(pbuf, &st) != 0) return -1;
    return (int64_t)st.st_size;
}

/* ── File management ────────────────────────────────────────────────────── */

int __k_io_file_delete(const uint32_t* path) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    return (remove(pbuf) == 0) ? 0 : -1;
}

int __k_io_file_create_new(const uint32_t* path) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    int fd = open(pbuf, O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) return -1;
    close(fd);
    return 0;
}

/* ── Path utilities ─────────────────────────────────────────────────────── */

/* Return the code-point index of the last '/' separator, or -1 if none.
 * Operates directly on the UTF-32 path so the returned index matches the
 * K-side char[] indexing used by File.getName(). */
int __k_io_file_last_separator(const uint32_t* path) {
    if (!path) return -1;
    int last = -1;
    for (int i = 0; path[i] != 0; ++i) {
        if (path[i] == (uint32_t)'/') last = i;
    }
    return last;
}

