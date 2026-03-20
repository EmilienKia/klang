/*
 * K Language standard library — base module
 *
 * Copyright 2023-2026 Emilien Kia
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

// ─────────────────────────────────────────────────────────────────────────────
// Internal helper: compute the length of a null-terminated char array.
// Scans until '\0' is found.  Does NOT count the terminal zero.
// ─────────────────────────────────────────────────────────────────────────────
_strlen(s : const char[]) : int {
    n : int = 0;
    while (s[n] != '\0') {
        n = n + 1;
    }
    return n;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helper: copy `count` chars from `src` starting at `src_off`
// into `dst` starting at `dst_off`.
// ─────────────────────────────────────────────────────────────────────────────
_memcpy(dst : char[], src : const char[], dst_off : int, src_off : int, count : int) {
    i : int = 0;
    while (i < count) {
        dst[dst_off + i] = src[src_off + i];
        i = i + 1;
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// String — Immutable, final class wrapping a null-terminated char buffer.
//
// After construction every String instance satisfies:
//   • _size >= 0
//   • _buf is either null (empty string) or a heap-allocated char array
//     whose element at index _size is '\0'.
// ═════════════════════════════════════════════════════════════════════════════
const final class String {
    _buf  : char[]!;     // owner, null-terminated buffer
    _size : int;         // number of characters (excluding '\0')

public:
    // ── Constructors ────────────────────────────────────────────────────

    /** Default: empty string. */
    String() : _buf(null), _size(0) {}

    /** Construct from a null-terminated char array (e.g. a string literal). */
    String(s : const char[]) : _buf(null), _size(0) {
        _size = _strlen(s);
        if (_size > 0) {
            _buf = new char[_size + 1];
            _memcpy(_buf, s, 0, 0, _size);
            _buf[_size] = '\0';
        }
    }

    /** Copy constructor. */
    String(other : const String&) : _buf(null), _size(other._size) {
        if (_size > 0) {
            _buf = new char[_size + 1];
            _memcpy(_buf, other._buf, 0, 0, _size);
            _buf[_size] = '\0';
        }
    }

    /** Construct from a StringBuilder (implicit conversion). */
    String(sb : const StringBuilder&) : _buf(null), _size(0) {
        _size = sb.size();
        if (_size > 0) {
            _buf = new char[_size + 1];
            sb._copy_to(_buf, 0);
            _buf[_size] = '\0';
        }
    }

    /** Destructor. */
    ~String() {
        delete _buf;
    }

    // ── Accessors (all const — implicit in a const class) ───────────────

    /** Number of characters (excluding the terminal '\0'). */
    const size() : int { return _size; }

    /** True when the string contains no characters. */
    const empty() : bool { return _size == 0; }

    /** Return the character at position `index`. No bounds check (POC). */
    const at(index : int) : char {
        return _buf[index];
    }

    // ── Comparison operators ────────────────────────────────────────────

    const operator ==(other : const String&) : bool {
        if (_size != other._size) return false;
        i : int = 0;
        while (i < _size) {
            if (_buf[i] != other._buf[i]) return false;
            i = i + 1;
        }
        return true;
    }

    const operator !=(other : const String&) : bool {
        return !(this == other);
    }

    // ── Concatenation ──────────────────────────────────────────────────

    const operator +(other : const String&) : StringBuilder {
        sb : StringBuilder(this);
        sb.append(other);
        return sb;
    }

    const operator +(other : const char[]) : StringBuilder {
        sb : StringBuilder(this);
        sb.append_raw(other);
        return sb;
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// StringBuilder — Mutable string builder backed by a list of fragments.
//
// Internally it stores a dynamic array of Fragment structs.  Each fragment
// owns a null-terminated char buffer and a size.  The logical string is
// the concatenation of all fragments in order.
// ═════════════════════════════════════════════════════════════════════════════
class StringBuilder {
    // ── Fragment storage ────────────────────────────────────────────────
    _frags    : char[]![]!;   // owner of array of owner char buffers
    _sizes    : int[]!;       // owner of array of fragment sizes
    _count    : int;          // number of fragments currently used
    _capacity : int;          // allocated length of _frags / _sizes

public:
    // ── Constructors ────────────────────────────────────────────────────

    /** Default: empty builder. */
    StringBuilder() : _frags(null), _sizes(null), _count(0), _capacity(0) {}

    /** Construct from a String. */
    StringBuilder(s : const String&) : _frags(null), _sizes(null), _count(0), _capacity(0) {
        if (s.size() > 0) {
            _grow();
            _push_chars(s);
        }
    }

    /** Construct from a null-terminated char array. */
    StringBuilder(s : const char[]) : _frags(null), _sizes(null), _count(0), _capacity(0) {
        len : int = _strlen(s);
        if (len > 0) {
            _grow();
            _push_raw(s, len);
        }
    }

    /** Copy constructor. */
    StringBuilder(other : const StringBuilder&) : _frags(null), _sizes(null), _count(0), _capacity(0) {
        i : int = 0;
        while (i < other._count) {
            _ensure_capacity();
            _push_raw(other._frags[i], other._sizes[i]);
            i = i + 1;
        }
    }

    /** Destructor — free every fragment buffer, then the arrays. */
    ~StringBuilder() {
        i : int = 0;
        while (i < _count) {
            delete _frags[i];
            i = i + 1;
        }
        delete _frags;
        delete _sizes;
    }

    // ── Accessors ───────────────────────────────────────────────────────

    /** Total number of characters across all fragments. */
    const size() : int {
        n : int = 0;
        i : int = 0;
        while (i < _count) {
            n = n + _sizes[i];
            i = i + 1;
        }
        return n;
    }

    /** True when the builder contains no characters. */
    const empty() : bool { return size() == 0; }

    /** Number of internal fragments. */
    const fragment_count() : int { return _count; }

    /** Copy all content into `dst` starting at `dst_off`. Used by String ctor. */
    const _copy_to(dst : char[], off : int) {
        pos : int = off;
        i : int = 0;
        while (i < _count) {
            _memcpy(dst, _frags[i], pos, 0, _sizes[i]);
            pos = pos + _sizes[i];
            i = i + 1;
        }
    }

    // ── Mutating methods (return StringBuilder& for fluent chaining) ────

    /** Append a String. */
    append(s : const String&) : StringBuilder& {
        if (s.size() > 0) {
            _ensure_capacity();
            _push_chars(s);
        }
        return this;
    }

    /** Append a null-terminated char array. */
    append_raw(s : const char[]) : StringBuilder& {
        len : int = _strlen(s);
        if (len > 0) {
            _ensure_capacity();
            _push_raw(s, len);
        }
        return this;
    }

    /** Clear all content. */
    clear() : StringBuilder& {
        i : int = 0;
        while (i < _count) {
            delete _frags[i];
            i = i + 1;
        }
        _count = 0;
        return this;
    }

    // ── Operators ───────────────────────────────────────────────────────

    /** += with a String. */
    operator +=(s : const String&) : StringBuilder& {
        return append(s);
    }

    /** += with a char array. */
    operator +=(s : const char[]) : StringBuilder& {
        return append_raw(s);
    }

    /** + with a String: returns a new StringBuilder containing this + s. */
    const operator +(s : const String&) : StringBuilder {
        result : StringBuilder(this);
        result.append(s);
        return result;
    }

    /** + with a char array: returns a new StringBuilder containing this + s. */
    const operator +(s : const char[]) : StringBuilder {
        result : StringBuilder(this);
        result.append_raw(s);
        return result;
    }

    // ── Private helpers ─────────────────────────────────────────────────
private:

    /** Ensure there is room for at least one more fragment. */
    _ensure_capacity() {
        if (_count >= _capacity) {
            _grow();
        }
    }

    /** Double (or initialise) the backing arrays. */
    _grow() {
        new_cap : int = 4;
        if (_capacity > 0) {
            new_cap = _capacity * 2;
        }

        new_frags : char[]![]! = new char[]![new_cap];
        new_sizes : int[]!     = new int[new_cap];

        // Copy existing entries
        i : int = 0;
        while (i < _count) {
            new_frags[i] = _frags[i];   // move owner
            new_sizes[i] = _sizes[i];
            i = i + 1;
        }
        // Zero remaining entries
        while (i < new_cap) {
            new_frags[i] = null;
            new_sizes[i] = 0;
            i = i + 1;
        }

        delete _frags;
        delete _sizes;
        _frags    = new_frags;
        _sizes    = new_sizes;
        _capacity = new_cap;
    }

    /** Append a fragment by copying from a String's internal buffer. */
    _push_chars(s : const String&) {
        len : int = s.size();
        buf : char[]! = new char[len + 1];
        i : int = 0;
        while (i < len) {
            buf[i] = s.at(i);
            i = i + 1;
        }
        buf[len] = '\0';

        _frags[_count] = buf;
        _sizes[_count] = len;
        _count = _count + 1;
    }

    /** Append a fragment by copying from a raw char array. */
    _push_raw(s : const char[], len : int) {
        buf : char[]! = new char[len + 1];
        _memcpy(buf, s, 0, 0, len);
        buf[len] = '\0';

        _frags[_count] = buf;
        _sizes[_count] = len;
        _count = _count + 1;
    }
}

