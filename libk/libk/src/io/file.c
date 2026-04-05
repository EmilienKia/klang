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

/* ── FILE* open / close ─────────────────────────────────────────────────── */

void* __k_io_file_fopen(const char* path, const char* mode) {
    return (void*)fopen(path, mode);
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
        return feof((FILE*)fp) ? -1 : -1;
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

int __k_io_file_exists(const char* path) {
    struct stat st;
    return (stat(path, &st) == 0) ? 1 : 0;
}

int __k_io_file_is_file(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISREG(st.st_mode) ? 1 : 0;
}

int __k_io_file_is_directory(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 1 : 0;
}

int64_t __k_io_file_length(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    return (int64_t)st.st_size;
}

/* ── File management ────────────────────────────────────────────────────── */

int __k_io_file_delete(const char* path) {
    return (remove(path) == 0) ? 0 : -1;
}

int __k_io_file_create_new(const char* path) {
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0644);
    if (fd < 0) return -1;
    close(fd);
    return 0;
}

/* ── Path utilities ─────────────────────────────────────────────────────── */

int __k_io_file_last_separator(const char* path) {
    if (!path) return -1;
    int last = -1;
    for (int i = 0; path[i] != '\0'; ++i) {
        if (path[i] == '/') last = i;
    }
    return last;
}

