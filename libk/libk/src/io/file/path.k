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

namespace io::file {

/**
 * Path — an immutable representation of a file system location.
 *
 * A Path represents a relative or absolute location in a file system.
 * It is backed internally by an array of String segments and an absolute flag.
 *
 * Path instances are immutable: all modification operations return a new Path.
 */
public class Path {
    private _segments     : String[]!;
    private _isAbsolute   : bool = false;
    private _fs           : FileSystem* = null;
    private _cachedNative : char[]!;

    @ffi::Extern("C") private static __k_io_fs_getcwd(buf: char*, max: int) : int;

    private _initFromChars(raw: const char[]) {
        len : int = (int) raw.size;
        if (len > 0 && raw[len - 1] == (char) 0) {
            --len;
        }
        if (len == 0) {
            _isAbsolute = false;
            _segments = new String[0];
            _rebuildNative();
            return;
        }

        _isAbsolute = (raw[0] == (char) 47);

        // Pass 1: count non-empty segments
        count : int = 0;
        i : int = 0;
        while (i < len) {
            while (i < len && raw[i] == (char) 47) {
                ++i;
            }
            if (i < len) {
                ++count;
                while (i < len && raw[i] != (char) 47) {
                    ++i;
                }
            }
        }

        _segments = new String[count];

        // Pass 2: extract segments
        idx : int = 0;
        i = 0;
        while (i < len) {
            while (i < len && raw[i] == (char) 47) {
                ++i;
            }
            if (i < len) {
                start : int = i;
                while (i < len && raw[i] != (char) 47) {
                    ++i;
                }
                segLen : int = i - start;
                buf : char[]! = new char[segLen + 1];
                k : int = 0;
                while (k < segLen) {
                    buf[k] = raw[start + k];
                    ++k;
                }
                buf[segLen] = (char) 0;
                _segments[idx] = String(buf);
                delete buf;
                ++idx;
            }
        }
        _rebuildNative();
    }

    private _rebuildNative() {
        if (_cachedNative != null) {
            delete _cachedNative;
            _cachedNative = null;
        }

        totalLen : unsigned int = 0u;
        if (_isAbsolute) {
            totalLen += 1u;
        }
        n : unsigned int = (unsigned int) _segments.size;
        i : unsigned int = 0u;
        while (i < n) {
            if (i > 0u || (_isAbsolute && totalLen > 1u)) {
                totalLen += 1u;
            }
            totalLen += _segments[i].size();
            ++i;
        }

        if (totalLen == 0u) {
            _cachedNative = new char[1];
            _cachedNative[0] = (char) 0;
            return;
        }

        _cachedNative = new char[totalLen + 1u];
        pos : unsigned int = 0u;
        if (_isAbsolute) {
            _cachedNative[pos] = (char) 47;
            ++pos;
        }
        i = 0u;
        while (i < n) {
            if (i > 0u) {
                _cachedNative[pos] = (char) 47;
                ++pos;
            }
            s : char[]! = _segments[i].toUtf32();
            sLen : unsigned int = _segments[i].size();
            k : unsigned int = 0u;
            while (k < sLen) {
                _cachedNative[pos] = s[k];
                ++pos;
                ++k;
            }
            delete s;
            ++i;
        }
        _cachedNative[totalLen] = (char) 0;
    }

public:
    /** Construct an empty relative path. */
    Path() : _segments(null), _isAbsolute(false), _fs(null), _cachedNative(null) {
        _segments = new String[0];
        _rebuildNative();
    }

    /**
     * Construct a path from a character array.
     *
     * @param path Null-terminated or sized character array.
     */
    Path(path: const char[]) : _segments(null), _isAbsolute(false), _fs(null), _cachedNative(null) {
        _initFromChars(path);
    }

    /**
     * Construct a path from a String.
     *
     * @param path The path string.
     */
    Path(path: const String&) : _segments(null), _isAbsolute(false), _fs(null), _cachedNative(null) {
        chars : char[]! = path.toUtf32();
        _initFromChars(chars);
        delete chars;
    }

    /**
     * Construct a path from segments.
     *
     * @param segs Array of string segments.
     * @param isAbs Whether the path is absolute.
     * @param fs Optional attached FileSystem.
     */
    Path(segs: const String[], isAbs: bool)
        : _segments(null), _isAbsolute(isAbs), _fs(null), _cachedNative(null) {
        n : unsigned int = (unsigned int) segs.size;
        _segments = new String[n];
        i : unsigned int = 0u;
        while (i < n) {
            _segments[i] = segs[i];
            ++i;
        }
        _rebuildNative();
    }

    Path(segs: const String[], isAbs: bool, fs: FileSystem*)
        : _segments(null), _isAbsolute(isAbs), _fs(fs), _cachedNative(null) {
        n : unsigned int = (unsigned int) segs.size;
        _segments = new String[n];
        i : unsigned int = 0u;
        while (i < n) {
            _segments[i] = segs[i];
            ++i;
        }
        _rebuildNative();
    }

    /** Copy constructor. */
    Path(other: const Path&)
        : _segments(null), _isAbsolute(other._isAbsolute), _fs(other._fs), _cachedNative(null) {
        n : unsigned int = (unsigned int) other._segments.size;
        _segments = new String[n];
        i : unsigned int = 0u;
        while (i < n) {
            _segments[i] = other._segments[i];
            ++i;
        }
        _rebuildNative();
    }

    /** Drain constructor. */
    Path(other: Path#)
        : _segments(other._segments), _isAbsolute(other._isAbsolute),
          _fs(other._fs), _cachedNative(other._cachedNative) {
        other._segments = null;
        other._cachedNative = null;
        other._fs = null;
        other._isAbsolute = false;
    }

    operator =(other: const Path&) : Path& {
        if (this == other) {
            return this;
        }
        if (_segments != null) {
            delete _segments;
            _segments = null;
        }
        if (_cachedNative != null) {
            delete _cachedNative;
            _cachedNative = null;
        }
        _isAbsolute = other._isAbsolute;
        _fs = other._fs;
        n : unsigned int = (unsigned int) other._segments.size;
        _segments = new String[n];
        i : unsigned int = 0u;
        while (i < n) {
            _segments[i] = other._segments[i];
            ++i;
        }
        _rebuildNative();
        return this;
    }

    ~Path() {
        if (_segments != null) {
            delete _segments;
            _segments = null;
        }
        if (_cachedNative != null) {
            delete _cachedNative;
            _cachedNative = null;
        }
    }

    // ── Static Factories ──────────────────────────────────────────────────

    /** Factory from char array. */
    static of(path: const char[]) : Path! {
        return new Path(path);
    }

    /** Factory from String. */
    static of(path: const String&) : Path! {
        return new Path(path);
    }

    /** Factory from String with attached FileSystem. */
    static of(path: const String&, fs: FileSystem*) : Path! {
        p : Path! = new Path(path);
        p->_fs = fs;
        return p;
    }

    /** Factory from segments. */
    static of(segs: const String[], isAbs: bool) : Path! {
        return new Path(segs, isAbs);
    }

    static of(segs: const String[], isAbs: bool, fs: FileSystem*) : Path! {
        return new Path(segs, isAbs, fs);
    }

    /**
     * Construct a Path from a URI.
     * The URI scheme must be "file".
     *
     * @param uri The URI to convert.
     * @return Converted Path or null if invalid scheme.
     */
    static fromUri(uri: const ::k::io::Uri&) : Path! {
        sch : String = uri.scheme();
        if (sch != String("file")) {
            return null;
        }
        rawP : String = uri.path();
        return Path::of(rawP);
    }

    // ── Predicates & Component Inspection ─────────────────────────────────

    /** Return true if this path is absolute. */
    const isAbsolute() : bool {
        return _isAbsolute;
    }

    /** Return true if this path is relative. */
    const isRelative() : bool {
        return !_isAbsolute;
    }

    /** Return true if this path has no segments and is not absolute. */
    const isEmpty() : bool {
        return !_isAbsolute && _segments.size == 0;
    }

    /** Return the number of elements/segments in this path. */
    const segmentCount() : unsigned int {
        return (unsigned int) _segments.size;
    }

    /** Return the segment at the specified index. */
    const segment(index: unsigned int) : String {
        return _segments[index];
    }

    /** Return a view of the segment array. */
    const segments() : const String[]? {
        return _segments;
    }

    /** Return the last element of this path as a String (or empty String if none). */
    const fileName() : String {
        n : unsigned int = segmentCount();
        if (n == 0u) {
            return String();
        }
        return _segments[n - 1u];
    }

    /**
     * Return the extension of the last component (without the dot),
     * or an empty string if no extension exists.
     */
    const extension() : String {
        name : String = fileName();
        sz : unsigned int = name.size();
        if (sz <= 1u) {
            return String();
        }
        dot : int = name.rfind('.');
        if (dot <= 0) {
            return String();
        }
        udot : unsigned int = (unsigned int) dot;
        if (udot + 1u >= sz) {
            return String();
        }
        return name.substr(udot + 1u, sz - (udot + 1u));
    }

    /** Tell whether the file name has an extension. */
    const hasExtension() : bool {
        return extension().size() > 0u;
    }

    /** Return the file name without its extension (the stem). */
    const stem() : String {
        name : String = fileName();
        sz : unsigned int = name.size();
        if (sz <= 1u) {
            return name;
        }
        dot : int = name.rfind('.');
        if (dot <= 0) {
            return name;
        }
        return name.substr(0u, (unsigned int) dot);
    }

    /** Alias for stem(). */
    const nameWithoutExtension() : String {
        return stem();
    }

    /**
     * Return the parent directory of this path.
     *
     * @return Newly allocated parent Path, or null if no parent.
     */
    const parent() : Path! {
        n : unsigned int = segmentCount();
        if (n > 1u) {
            newSegs : String[]! = new String[n - 1u];
            i : unsigned int = 0u;
            while (i < n - 1u) {
                newSegs[i] = _segments[i];
                ++i;
            }
            p : Path! = new Path(newSegs, _isAbsolute, _fs);
            delete newSegs;
            return p;
        }
        if (n == 1u) {
            if (_isAbsolute) {
                emptySegs : String[]! = new String[0];
                p : Path! = new Path(emptySegs, true, _fs);
                delete emptySegs;
                return p;
            }
            return null;
        }
        return null;
    }

    /**
     * Return the root component if absolute ("/"), or null if relative.
     */
    const root() : Path! {
        if (!_isAbsolute) {
            return null;
        }
        emptySegs : String[]! = new String[0];
        p : Path! = new Path(emptySegs, true, _fs);
        delete emptySegs;
        return p;
    }

    // ── Fluent Transformations ────────────────────────────────────────────

    /**
     * Return a new Path with the file name replaced.
     */
    const withFileName(name: const String&) : Path! {
        n : unsigned int = segmentCount();
        if (n == 0u) {
            one : String[]! = new String[1];
            one[0] = name;
            p : Path! = new Path(one, _isAbsolute, _fs);
            delete one;
            return p;
        }
        newSegs : String[]! = new String[n];
        i : unsigned int = 0u;
        while (i < n - 1u) {
            newSegs[i] = _segments[i];
            ++i;
        }
        newSegs[n - 1u] = name;
        res : Path! = new Path(newSegs, _isAbsolute, _fs);
        delete newSegs;
        return res;
    }

    const withFileName(name: const char[]) : Path! {
        s : String(name);
        return withFileName(s);
    }

    /**
     * Return a new Path with the extension changed or set.
     */
    const withExtension(ext: const String&) : Path! {
        st : String = stem();
        newName : String;
        if (ext.size() > 0u) {
            sb : StringBuilder(st);
            sb.append(".");
            sb.append(ext);
            newName = sb.toString();
        } else {
            newName = st;
        }
        return withFileName(newName);
    }

    const withExtension(ext: const char[]) : Path! {
        s : String(ext);
        return withExtension(s);
    }

    /**
     * Prepend another path's segments to this path.
     */
    const prefix(pref: const Path&) : Path! {
        pCount : unsigned int = pref.segmentCount();
        nCount : unsigned int = segmentCount();
        total : unsigned int = pCount + nCount;
        newSegs : String[]! = new String[total];
        i : unsigned int = 0u;
        while (i < pCount) {
            newSegs[i] = pref.segment(i);
            ++i;
        }
        j : unsigned int = 0u;
        while (j < nCount) {
            newSegs[pCount + j] = _segments[j];
            ++j;
        }
        effFs : FileSystem* = pref._fs;
        if (_fs != null) {
            effFs = _fs;
        }
        res : Path! = new Path(newSegs, pref._isAbsolute, effFs);
        delete newSegs;
        return res;
    }

    /**
     * Return a relative path containing only the last element.
     */
    const last() : Path! {
        n : unsigned int = segmentCount();
        if (n == 0u) {
            return new Path();
        }
        one : String[]! = new String[1];
        one[0] = _segments[n - 1u];
        p : Path! = new Path(one, false, _fs);
        delete one;
        return p;
    }

    /**
     * Resolve other path against this path.
     * If other is absolute, returns a copy of other.
     */
    const resolve(other: const Path&) : Path! {
        if (other._isAbsolute) {
            return new Path(other);
        }
        oCount : unsigned int = other.segmentCount();
        if (oCount == 0u) {
            return new Path(this);
        }
        nCount : unsigned int = segmentCount();
        total : unsigned int = nCount + oCount;
        newSegs : String[]! = new String[total];
        i : unsigned int = 0u;
        while (i < nCount) {
            newSegs[i] = _segments[i];
            ++i;
        }
        j : unsigned int = 0u;
        while (j < oCount) {
            newSegs[nCount + j] = other.segment(j);
            ++j;
        }
        res : Path! = new Path(newSegs, _isAbsolute, _fs);
        delete newSegs;
        return res;
    }

    /** Resolve a string path against this path. */
    const resolve(other: const String&) : Path! {
        o : Path(other);
        return resolve(o);
    }

    /** Resolve a char array path against this path. */
    const resolve(other: const char[]) : Path! {
        o : Path(other);
        return resolve(o);
    }

    /**
     * Normalize this path by eliminating '.' and '..' components.
     */
    const normalize() : Path! {
        n : unsigned int = segmentCount();
        if (n == 0u) {
            return new Path(this);
        }
        stack : String[]! = new String[n];
        top : unsigned int = 0u;
        i : unsigned int = 0u;
        while (i < n) {
            seg : const String& = _segments[i];
            if (seg == String(".")) {
                // skip
            } else if (seg == String("..")) {
                if (top > 0u && stack[top - 1u] != String("..")) {
                    --top;
                } else if (!_isAbsolute) {
                    stack[top] = seg;
                    ++top;
                }
            } else {
                stack[top] = seg;
                ++top;
            }
            ++i;
        }
        newSegs : String[]! = new String[top];
        k : unsigned int = 0u;
        while (k < top) {
            newSegs[k] = stack[k];
            ++k;
        }
        delete stack;
        res : Path! = new Path(newSegs, _isAbsolute, _fs);
        delete newSegs;
        return res;
    }

    /**
     * Return an absolute path. If this path is already absolute, returns a copy.
     * If relative, resolves against the current working directory.
     */
    const absolute() : Path! {
        if (_isAbsolute) {
            return new Path(this);
        }
        cwdBuf : char[]! = new char[4096];
        n : int = __k_io_fs_getcwd(&cwdBuf[0], 4096);
        if (n <= 0) {
            delete cwdBuf;
            return new Path(this);
        }
        base : Path(cwdBuf);
        delete cwdBuf;
        return base.resolve(this);
    }

    /**
     * Compute relative path from base to this.
     */
    const relative(base: const Path&) : Path! {
        if (_isAbsolute != base._isAbsolute) {
            return new Path(this);
        }
        bCount : unsigned int = base.segmentCount();
        tCount : unsigned int = segmentCount();

        common : unsigned int = 0u;
        while (common < bCount && common < tCount && base.segment(common) == segment(common)) {
            ++common;
        }

        upCount : unsigned int = bCount - common;
        downCount : unsigned int = tCount - common;
        total : unsigned int = upCount + downCount;

        if (total == 0u) {
            return new Path();
        }

        newSegs : String[]! = new String[total];
        idx : unsigned int = 0u;
        i : unsigned int = 0u;
        while (i < upCount) {
            newSegs[idx] = String("..");
            ++idx;
            ++i;
        }
        j : unsigned int = 0u;
        while (j < downCount) {
            newSegs[idx] = segment(common + j);
            ++idx;
            ++j;
        }
        res : Path! = new Path(newSegs, false, _fs);
        delete newSegs;
        return res;
    }

    /** Attach this Path to a specific FileSystem. */
    const attachedTo(fs: FileSystem&) : Path! {
        return new Path(_segments, _isAbsolute, &fs);
    }

    /** Return the attached FileSystem, or null. */
    const fileSystem() : FileSystem* {
        return _fs;
    }

    // ── String Conversions & Native Bridge ────────────────────────────────

    /** Return the native null-terminated character pointer. */
    const nativePath() : const char* {
        return &_cachedNative[0];
    }

    /** Return the path text as a char array. */
    const toString() : const char[]? {
        return _cachedNative;
    }

    /** Return the path text as a String. */
    const toPathString() : String {
        return String(_cachedNative);
    }

    /** Return the character length of the path text. */
    const length() : int {
        if (_cachedNative == null) {
            return 0;
        }
        sz : unsigned int = _cachedNative.size;
        return sz > 1u ? (int) (sz - 1u) : 0;
    }

    /**
     * Convert this Path to a "file:" URI.
     */
    const toUri() : ::k::io::Uri! {
        sb : StringBuilder;
        sb.append("file:");
        if (_isAbsolute) {
            sb.append("//");
        }
        pStr : String = toPathString();
        encPath : String = ::k::io::Uri::encodePath(pStr);
        sb.append(encPath);
        return new ::k::io::Uri(sb.toString());
    }

    // ── FileSystem Operations ─────────────────────────────────────────────

    /** Get the effective FileSystem (attached or local). */
    const getEffectiveFileSystem() : FileSystem& {
        if (_fs != null) {
            return *_fs;
        }
        return FileSystem::local();
    }

    /** Return the type of filesystem entry at this path. */
    const entryType() : FileSystemEntryType {
        return getEffectiveFileSystem().entryType(this);
    }

    /** Return specialized FileSystemEntry union at this path. */
    const entry() : AnyFileSystemEntry {
        return getEffectiveFileSystem().entry(this);
    }

    /** Tell whether an entry exists at this path. */
    const exists() : bool {
        return getEffectiveFileSystem().exists(this);
    }

    /** Tell whether this path refers to a regular file. */
    const isFile() : bool {
        return entryType() == FileSystemEntryType::File;
    }

    /** Tell whether this path refers to a directory. */
    const isDirectory() : bool {
        return entryType() == FileSystemEntryType::Directory;
    }

    /** Tell whether this path refers to a symbolic link. */
    const isSymlink() : bool {
        return entryType() == FileSystemEntryType::Symlink;
    }

    /** Return the size in bytes of the file at this path. */
    const size() : long {
        return getEffectiveFileSystem().size(this);
    }

    /** Remove the file or directory at this path. */
    const remove() : bool {
        return getEffectiveFileSystem().remove(this);
    }

    /** Create a directory at this path. */
    const createDirectory() : bool {
        return getEffectiveFileSystem().createDirectory(this);
    }

    // ── Equality ──────────────────────────────────────────────────────────

    const operator ==(other: const Path&) : bool {
        if (_isAbsolute != other._isAbsolute) return false;
        n : unsigned int = segmentCount();
        if (n != other.segmentCount()) return false;
        i : unsigned int = 0u;
        while (i < n) {
            if (_segments[i] != other._segments[i]) return false;
            ++i;
        }
        return true;
    }

    const operator !=(other: const Path&) : bool {
        return !(this == other);
    }
}

} // namespace io::file
