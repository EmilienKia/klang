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
#include <dirent.h>
#include <errno.h>

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

static int k_utf8_to_utf32(const char* src, uint32_t* dst, size_t dst_cap) {
    if (!src || !dst || dst_cap == 0) return -1;
    size_t out = 0;
    size_t in = 0;
    while (src[in] != '\0' && out + 1 < dst_cap) {
        uint8_t c = (uint8_t)src[in++];
        uint32_t cp;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0) {
            if ((src[in] & 0xC0) != 0x80) break;
            cp = ((c & 0x1F) << 6) | (src[in++] & 0x3F);
        } else if ((c & 0xF0) == 0xE0) {
            if ((src[in] & 0xC0) != 0x80 || (src[in+1] & 0xC0) != 0x80) break;
            cp = ((c & 0x0F) << 12) | ((src[in++] & 0x3F) << 6);
            cp |= (src[in++] & 0x3F);
        } else if ((c & 0xF8) == 0xF0) {
            if ((src[in] & 0xC0) != 0x80 || (src[in+1] & 0xC0) != 0x80 || (src[in+2] & 0xC0) != 0x80) break;
            cp = ((c & 0x07) << 18) | ((src[in++] & 0x3F) << 12);
            cp |= ((src[in++] & 0x3F) << 6);
            cp |= (src[in++] & 0x3F);
        } else {
            break;
        }
        dst[out++] = cp;
    }
    dst[out] = 0;
    return (int)out;
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

/* ── Extended Filesystem Operations ─────────────────────────────────────── */

int __k_io_fs_lstat_type(const uint32_t* path) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    struct stat st;
    if (lstat(pbuf, &st) != 0) return -1;
    if (S_ISREG(st.st_mode)) return 0;
    if (S_ISDIR(st.st_mode)) return 1;
    if (S_ISLNK(st.st_mode)) return 2;
    return 3;
}

int64_t __k_io_fs_stat_size(const uint32_t* path) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    struct stat st;
    if (stat(pbuf, &st) != 0) return -1;
    return (int64_t)st.st_size;
}

int __k_io_fs_mkdir(const uint32_t* path, int mode) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    return (mkdir(pbuf, (mode_t)mode) == 0) ? 0 : -1;
}

int __k_io_fs_rmdir(const uint32_t* path) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    return (rmdir(pbuf) == 0) ? 0 : -1;
}

int __k_io_fs_unlink(const uint32_t* path) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    return (unlink(pbuf) == 0) ? 0 : -1;
}

int __k_io_fs_rename(const uint32_t* src, const uint32_t* dst) {
    char sbuf[K_PATH_BUF];
    char dbuf[K_PATH_BUF];
    k_utf32_to_utf8(src, sbuf, sizeof(sbuf));
    k_utf32_to_utf8(dst, dbuf, sizeof(dbuf));
    return (rename(sbuf, dbuf) == 0) ? 0 : -1;
}

int __k_io_fs_copyfile(const uint32_t* src, const uint32_t* dst, int overwrite) {
    char sbuf[K_PATH_BUF];
    char dbuf[K_PATH_BUF];
    k_utf32_to_utf8(src, sbuf, sizeof(sbuf));
    k_utf32_to_utf8(dst, dbuf, sizeof(dbuf));
    int sfd = open(sbuf, O_RDONLY);
    if (sfd < 0) return -1;
    struct stat st;
    if (fstat(sfd, &st) != 0) {
        close(sfd);
        return -1;
    }
    int flags = O_WRONLY | O_CREAT;
    if (overwrite) {
        flags |= O_TRUNC;
    } else {
        flags |= O_EXCL;
    }
    int dfd = open(dbuf, flags, st.st_mode & 0777);
    if (dfd < 0) {
        close(sfd);
        return -1;
    }
    char buf[65536];
    ssize_t n;
    int err = 0;
    while ((n = read(sfd, buf, sizeof(buf))) > 0) {
        char* p = buf;
        ssize_t remaining = n;
        while (remaining > 0) {
            ssize_t written = write(dfd, p, remaining);
            if (written <= 0) {
                err = -1;
                break;
            }
            remaining -= written;
            p += written;
        }
        if (err != 0) break;
    }
    if (n < 0) err = -1;
    close(sfd);
    close(dfd);
    return err;
}

int __k_io_fs_readlink(const uint32_t* path, uint32_t* target_buf, int max_chars) {
    char pbuf[K_PATH_BUF];
    char tbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    ssize_t len = readlink(pbuf, tbuf, sizeof(tbuf) - 1);
    if (len < 0) return -1;
    tbuf[len] = '\0';
    return k_utf8_to_utf32(tbuf, target_buf, (size_t)max_chars);
}

int __k_io_fs_symlink(const uint32_t* target, const uint32_t* link) {
    char tbuf[K_PATH_BUF];
    char lbuf[K_PATH_BUF];
    k_utf32_to_utf8(target, tbuf, sizeof(tbuf));
    k_utf32_to_utf8(link, lbuf, sizeof(lbuf));
    return (symlink(tbuf, lbuf) == 0) ? 0 : -1;
}

int __k_io_fs_getcwd(uint32_t* buf, int max_chars) {
    char cwd[K_PATH_BUF];
    if (!getcwd(cwd, sizeof(cwd))) return -1;
    return k_utf8_to_utf32(cwd, buf, (size_t)max_chars);
}

void* __k_io_fs_opendir(const uint32_t* path) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    return (void*)opendir(pbuf);
}

int __k_io_fs_readdir(void* dir, uint32_t* name_buf, int max_chars, int* out_type) {
    if (!dir) return -1;
    struct dirent* entry;
    while ((entry = readdir((DIR*)dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (out_type) {
#ifdef DT_DIR
            if (entry->d_type == DT_DIR) {
                *out_type = 1;
            } else if (entry->d_type == DT_REG) {
                *out_type = 0;
            } else if (entry->d_type == DT_LNK) {
                *out_type = 2;
            } else {
                *out_type = 3;
            }
#else
            *out_type = 3;
#endif
        }
        k_utf8_to_utf32(entry->d_name, name_buf, (size_t)max_chars);
        return 1;
    }
    return 0; // EOF
}

int __k_io_fs_closedir(void* dir) {
    if (!dir) return -1;
    return closedir((DIR*)dir);
}


