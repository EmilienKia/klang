/*
 * K Language standard library — base module
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

module k;

namespace io {

// ═════════════════════════════════════════════════════════════════════════════
// CFile — Opaque struct representing a C FILE* stream handle.
// ═════════════════════════════════════════════════════════════════════════════
public struct CFile {
}

// ── FFI declarations for file I/O C wrappers ────────────────────────────
@ffi::Extern("C") __k_io_file_fopen(path: const char*, mode: const char*) : CFile*;
@ffi::Extern("C") __k_io_file_fclose(fp: CFile*) : int;
@ffi::Extern("C") __k_io_file_fread_byte(fp: CFile*) : int;
@ffi::Extern("C") __k_io_file_fwrite_byte(fp: CFile*, b: int) : int;
@ffi::Extern("C") __k_io_file_fread(fp: CFile*, buf: byte*, len: int) : int;
@ffi::Extern("C") __k_io_file_fwrite(fp: CFile*, buf: const byte*, len: int) : int;
@ffi::Extern("C") __k_io_file_fflush(fp: CFile*) : int;
@ffi::Extern("C") __k_io_file_fileno(fp: CFile*) : int;
@ffi::Extern("C") __k_io_file_exists(path: const char*) : int;
@ffi::Extern("C") __k_io_file_is_file(path: const char*) : int;
@ffi::Extern("C") __k_io_file_is_directory(path: const char*) : int;
@ffi::Extern("C") __k_io_file_length(path: const char*) : long;
@ffi::Extern("C") __k_io_file_delete(path: const char*) : int;
@ffi::Extern("C") __k_io_file_create_new(path: const char*) : int;
@ffi::Extern("C") __k_io_file_last_separator(path: const char*) : int;


// ═════════════════════════════════════════════════════════════════════════════
// FileDescriptor — Wraps an OS-level file descriptor (integer).
//
// Inspired by java.io.FileDescriptor.
// ═════════════════════════════════════════════════════════════════════════════
public struct FileDescriptor {
    _fd : int;

public:
    /** Default constructor — invalid descriptor (-1). */
    FileDescriptor() : _fd(-1) {}

    /** Construct from a raw OS file descriptor. */
    FileDescriptor(fd: int) : _fd(fd) {}

    /** Return the raw OS file descriptor. */
    const getFd() : int {
        return _fd;
    }

    /** Return true if this descriptor refers to an open file (fd >= 0). */
    const valid() : bool {
        return _fd >= 0;
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// File — Represents a file or directory path on the filesystem.
//
// Inspired by java.io.File.
// ═════════════════════════════════════════════════════════════════════════════
public class File {
    _path : char[]!;
    _len  : int;

public:
    /**
     * Construct a File from a path string.
     * The path must be null-terminated (include \0 at the end of the literal).
     */
    File(path: const char[]) : _path(null), _len(0) {
        _len = path.size;
        _path = new char[_len + 1];
        i : int = 0;
        while (i < _len) {
            _path[i] = path[i];
            i = i + 1;
        }
        _path[_len] = (char) 0;
    }

    /** Return the path as a view. */
    const getPath() : const char[]? {
        return _path;
    }

    /** Return the length of the path string. */
    const getPathLength() : int {
        return _len;
    }

    /**
     * Return the name component of the path (after the last '/').
     * If there is no separator, returns a copy of the entire path.
     */
    getName() : char[]! {
        sep : int = __k_io_file_last_separator(&_path[0]);
        start : int = sep + 1;
        nameLen : int = _len - start;
        result : char[]! = new char[nameLen];
        i : int = 0;
        while (i < nameLen) {
            result[i] = _path[start + i];
            i = i + 1;
        }
        return result;
    }

    /** Return true if the file or directory exists. */
    const exists() : bool {
        return __k_io_file_exists(&_path[0]) != 0;
    }

    /** Return true if the path refers to a regular file. */
    const isFile() : bool {
        return __k_io_file_is_file(&_path[0]) != 0;
    }

    /** Return true if the path refers to a directory. */
    const isDirectory() : bool {
        return __k_io_file_is_directory(&_path[0]) != 0;
    }

    /** Return the file size in bytes, or -1 if the file does not exist. */
    const length() : long {
        return __k_io_file_length(&_path[0]);
    }

    /**
     * Remove the file or directory.
     * Returns true on success, false on failure.
     */
    remove() : bool {
        return __k_io_file_delete(&_path[0]) == 0;
    }

    /**
     * Atomically create a new, empty file if it does not yet exist.
     * Returns true if the file was created, false if it already exists.
     */
    createNewFile() : bool {
        return __k_io_file_create_new(&_path[0]) == 0;
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// FileInputStream — An InputStream that reads bytes from a file.
//
// Inspired by java.io.FileInputStream.
// ═════════════════════════════════════════════════════════════════════════════
public class FileInputStream : public InputStream {
    _fp : CFile*;

public:
    /** Construct a FileInputStream from a path string. */
    FileInputStream(path: const char[]) : _fp(null) {
        f : File(path);
        p : const char[]? = f.getPath();
        _fp = __k_io_file_fopen(&p[0], &"rb\0"[0]);
    }

    /** Return true if the underlying file was opened successfully. */
    const isOpen() : bool {
        return _fp != null;
    }

    // ── InputStream implementation ───────────────────────────────────────

    read() : int {
        if (_fp == null) return -1;
        return __k_io_file_fread_byte(_fp);
    }

    read(buf: byte[], off: unsigned int, len: unsigned int) : int {
        if (_fp == null) return -1;
        if (len == 0) return 0;
        return __k_io_file_fread(_fp, &buf[off], (int) len);
    }

    read(buf: byte[]) : int {
        return read(buf, 0, (unsigned int) buf.size);
    }

    skip(n: unsigned long) : unsigned long {
        skipped : unsigned long = 0;
        while (skipped < n) {
            b : int = read();
            if (b == -1) return skipped;
            skipped = skipped + 1;
        }
        return skipped;
    }

    available() : int {
        return 0;
    }

    close() {
        if (_fp != null) {
            __k_io_file_fclose(_fp);
            _fp = null;
        }
    }

    // ── Additional ───────────────────────────────────────────────────────

    /** Return a FileDescriptor for the underlying OS file descriptor. */
    getFD() : FileDescriptor {
        if (_fp == null) {
            invalid : FileDescriptor;
            return invalid;
        }
        fd : FileDescriptor(__k_io_file_fileno(_fp));
        return fd;
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// FileOutputStream — An OutputStream that writes bytes to a file.
//
// Inspired by java.io.FileOutputStream.
// ═════════════════════════════════════════════════════════════════════════════
public class FileOutputStream : public OutputStream {
    _fp : CFile*;

public:
    /** Construct a FileOutputStream from a path string (write mode). */
    FileOutputStream(path: const char[]) : _fp(null) {
        f : File(path);
        p : const char[]? = f.getPath();
        _fp = __k_io_file_fopen(&p[0], &"wb\0"[0]);
    }

    /** Construct a FileOutputStream from a path string with append flag. */
    FileOutputStream(path: const char[], append: bool) : _fp(null) {
        f : File(path);
        p : const char[]? = f.getPath();
        if (append) {
            _fp = __k_io_file_fopen(&p[0], &"ab\0"[0]);
        } else {
            _fp = __k_io_file_fopen(&p[0], &"wb\0"[0]);
        }
    }

    /** Return true if the underlying file was opened successfully. */
    const isOpen() : bool {
        return _fp != null;
    }

    // ── OutputStream implementation ──────────────────────────────────────

    write(b: int) : OutputStream& {
        if (_fp != null) {
            __k_io_file_fwrite_byte(_fp, b);
        }
        return this;
    }

    write(buf: const byte[], off: unsigned int, len: unsigned int) : OutputStream& {
        if (_fp != null && len > 0) {
            __k_io_file_fwrite(_fp, &buf[off], (int) len);
        }
        return this;
    }

    write(buf: const byte[]) : OutputStream& {
        return write(buf, 0, (unsigned int) buf.size);
    }

    flush() : OutputStream& {
        if (_fp != null) {
            __k_io_file_fflush(_fp);
        }
        return this;
    }

    close() : OutputStream& {
        if (_fp != null) {
            __k_io_file_fflush(_fp);
            __k_io_file_fclose(_fp);
            _fp = null;
        }
        return this;
    }

    // ── Additional ───────────────────────────────────────────────────────

    /** Return a FileDescriptor for the underlying OS file descriptor. */
    getFD() : FileDescriptor {
        if (_fp == null) {
            invalid : FileDescriptor;
            return invalid;
        }
        fd : FileDescriptor(__k_io_file_fileno(_fp));
        return fd;
    }
}

} // namespace io






