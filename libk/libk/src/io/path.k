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

/**
 * Path — an immutable filesystem location.
 *
 * A Path holds a null-terminated copy of the path text, so it stays valid
 * independently of the string it was built from and can be handed to the
 * platform without further conversion.
 *
 * Path performs no I/O of its own beyond the inspection helpers below;
 * opening a path is the job of ::k::io::FileChannel and the file streams.
 */
public class Path {
    _path : char[]!;
    _len  : int;
    @ffi::Extern("C") private static __k_async_path_exists(path: const char*) : int;
    @ffi::Extern("C") private static __k_async_path_is_file(path: const char*) : int;
    @ffi::Extern("C") private static __k_async_path_is_directory(path: const char*) : int;
    @ffi::Extern("C") private static __k_async_path_size(path: const char*) : long;
    @ffi::Extern("C") private static __k_async_path_delete(path: const char*) : int;
    @ffi::Extern("C") private static __k_async_path_mkdir(path: const char*) : int;

public:
    /** Construct an empty path. */
    Path() : _path(null), _len(0) {
        _path = new char[1];
        _path[0] = (char) 0;
    }

    /**
     * Construct a path from a character array.
     *
     * @param path Path text; a null terminator is appended internally, and any
     *             terminator already present in `path` is preserved as-is.
     */
    Path(path: const char[]) : _path(null), _len(0) {
        _len = path.size;
        // A literal that already carries its terminator must not gain a second
        // one, otherwise the stored length would be off by one.
        if (_len > 0 && path[_len - 1] == (char) 0) {
            --_len;
        }
        _path = new char[_len + 1];
        i : int = 0;
        while (i < _len) {
            _path[i] = path[i];
            ++i;
        }
        _path[_len] = (char) 0;
    }

    /**
     * Build a path from a character array.
     *
     * @param path Path text.
     * @return A newly allocated Path.
     */
    static of(path: const char[]) : Path! {
        return new Path(path);
    }

    /**
     * Return the stored path text, null terminator included.
     *
     * @return The path characters.
     */
    const toString() : const char[]? {
        return _path;
    }

    /**
     * Return the number of characters in the path, terminator excluded.
     *
     * @return Path length.
     */
    const length() : int {
        return _len;
    }

    /**
     * Return a pointer to the null-terminated path text.
     * Intended for the platform bridge; the pointer stays valid as long as
     * this Path is alive.
     *
     * @return Pointer to the first path character.
     */
    const nativePath() : const char* {
        return &_path[0];
    }

    // ── Composition ──────────────────────────────────────────────────────

    /**
     * Return the last component of the path, after the final separator.
     *
     * @return A newly allocated null-terminated array holding the file name.
     */
    const fileName() : char[]! {
        start : int = _len;
        while (start > 0 && _path[start - 1] != (char) 47) {
            --start;
        }
        n : int = _len - start;
        out : char[]! = new char[n + 1];
        i : int = 0;
        while (i < n) {
            out[i] = _path[start + i];
            ++i;
        }
        out[n] = (char) 0;
        return out;
    }

    /**
     * Return the parent directory of this path.
     *
     * @return A newly allocated Path for the parent directory, or `null` when
     *         the path has no separator and therefore no parent.
     */
    const parent() : Path! {
        cut : int = _len;
        while (cut > 0 && _path[cut - 1] != (char) 47) {
            --cut;
        }
        if (cut == 0) {
            return null;
        }
        // Keep the root slash when the parent is "/".
        if (cut > 1) {
            --cut;
        }
        buf : char[]! = new char[cut + 1];
        i : int = 0;
        while (i < cut) {
            buf[i] = _path[i];
            ++i;
        }
        buf[cut] = (char) 0;
        p : Path! = new Path(buf);
        delete buf;
        return p;
    }

    /**
     * Resolve `other` against this path.
     *
     * @param other Relative component to append; an absolute `other`
     *              (starting with '/') replaces this path entirely.
     * @return A newly allocated Path holding the resolved location.
     */
    const resolve(other: const char[]) : Path! {
        otherLen : int = other.size;
        if (otherLen > 0 && other[otherLen - 1] == (char) 0) {
            --otherLen;
        }
        if (otherLen == 0) {
            return new Path(_path);
        }
        if (other[0] == (char) 47) {
            return new Path(other);
        }

        sep : int = 1;
        if (_len == 0 || _path[_len - 1] == (char) 47) {
            sep = 0;
        }
        total : int = _len + sep + otherLen;
        buf : char[]! = new char[total + 1];
        i : int = 0;
        while (i < _len) {
            buf[i] = _path[i];
            ++i;
        }
        if (sep == 1) {
            buf[_len] = (char) 47;
        }
        j : int = 0;
        while (j < otherLen) {
            buf[_len + sep + j] = other[j];
            ++j;
        }
        buf[total] = (char) 0;
        p : Path! = new Path(buf);
        delete buf;
        return p;
    }

    // ── Inspection ───────────────────────────────────────────────────────

    /** @return `true` when a file or directory exists at this path. */
    const exists() : bool {
        return __k_async_path_exists(&_path[0]) != 0;
    }

    /** @return `true` when this path refers to a regular file. */
    const isFile() : bool {
        return __k_async_path_is_file(&_path[0]) != 0;
    }

    /** @return `true` when this path refers to a directory. */
    const isDirectory() : bool {
        return __k_async_path_is_directory(&_path[0]) != 0;
    }

    /** @return The file size in bytes, or -1 when the path does not exist. */
    const size() : long {
        return __k_async_path_size(&_path[0]);
    }

    // ── Mutation ─────────────────────────────────────────────────────────

    /**
     * Remove the file this path refers to.
     *
     * @return `true` when the entry was removed.
     */
    const remove() : bool {
        return __k_async_path_delete(&_path[0]) != 0;
    }

    /**
     * Create a directory at this path.
     *
     * @return `true` when the directory was created.
     */
    const createDirectory() : bool {
        return __k_async_path_mkdir(&_path[0]) != 0;
    }
}

} // namespace io
