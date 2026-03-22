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

// ═════════════════════════════════════════════════════════════════════════════
// CharHelpers — Static utility methods for char[] buffer operations.
//
// All methods are public and static, intended to be called as:
//   CharHelpers::copy(dst, 0, src, 0, count)
//   CharHelpers::equals(a, b, len)
//
// Parameters use char[]* (non-owning pointer) so that passing a char[]!
// (owner) borrows the address without transferring ownership.
// ═════════════════════════════════════════════════════════════════════════════
public final class CharHelpers {
public:

    /** Copy `count` chars from src[src_off..] to dst[dst_off..]. */
    public static copy(dst: char[], dst_off: unsigned int, src: const char[], src_off: unsigned int, count: unsigned int) {
        i : int = 0;
        while (i < count) {
            dst[dst_off + i] = src[src_off + i];
            i = i + 1;
        }
    }

    /** Compare `len` chars starting from position 0 in both arrays.
     *  Returns true if all characters match. */
    public static equals(a: const char[], b: const char[], len: unsigned int) : bool {
        i : int = 0;
        while (i < len) {
            if (a[i] != b[i]) return false;
            i = i + 1;
        }
        return true;
    }

    /** Fill `count` positions in dst starting at dst_off with char `c`. */
    public static fill(dst: char[], dst_off: unsigned int, c: char, count: unsigned int) {
        i : int = 0;
        while (i < count) {
            dst[dst_off + i] = c;
            i = i + 1;
        }
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// String — Immutable, final class wrapping a null-terminated char buffer.
//
// After construction every String instance satisfies:
//   • _size >= 0
//   • _buf is either null (empty string) or a heap-allocated char[]!
//     whose element at index _size is '\0'.
//
// NOTE: This is a proof-of-concept for ASCII strings only.
// ═════════════════════════════════════════════════════════════════════════════
const final class String {
    _buf  : char[]!;     // owner, null-terminated buffer
    _size : unsigned int;         // number of characters (excluding '\0')

public:
    // ── Constructors ────────────────────────────────────────────────────

    /** Default: empty string. */
    String() -> default;

    /** Construct a String of given size from an owner buffer (takes ownership).
     *  The buffer must already be null-terminated at position `sz`.
     *  This is the primary internal construction path.
     */
    String(buf : char[]!, sz : unsigned int) : _buf(buf), _size(sz) {}

    /** Copy constructor. */
    String(other : const String&) : _buf(null), _size(other._size) {
        if (_size > 0) {
            alloc_sz : unsigned int = _size + 1;
            _buf = new char[alloc_sz];
            CharHelpers::copy(_buf, 0, other._buf, 0, _size + 1);
        }
    }

    /** Construct from a const char[] (e.g. a string literal).
     *  The source array is expected to include a null terminator:
     *  src.size == number_of_characters + 1.
     *  A new owned buffer is allocated and the content is copied.
     */
    String(src : const char[]) : _buf(null), _size(0) {
        if (src.size > 1) {
            _size = src.size - 1;
            alloc_sz : unsigned int = src.size;
            _buf = new char[alloc_sz];
            CharHelpers::copy(_buf, 0, src, 0, src.size);
        }
    }

    /** Construct from a StringBuilder (implicit conversion).
     *  Copies the builder's content into a new contiguous buffer.
     */
    String(sb : const StringBuilder&) : _buf(null), _size(0) {
        _size = sb.size();
        if (_size > 0) {
            alloc_sz : unsigned int = _size + 1;
            _buf = new char[alloc_sz];
            CharHelpers::copy(_buf, 0, sb.data(), 0, _size);
            _buf[_size] = '\0';
        }
    }

    // ── Accessors (all const — implicit in a const class) ───────────────

    /** Number of characters (excluding the terminal '\0'). */
    const size() : unsigned int { return _size; }

    /** True when the string contains no characters. */
    const empty() : bool { return _size == 0; }

    /** Return the character at position `index`. No bounds check (POC). */
    const at(index : int) : char {
        return _buf[index];
    }

    /** Return a non-owning pointer to the internal buffer.
     *  Valid as long as this String is alive. Returns null for empty strings.
     */
    const data() : char[]^ {
        return _buf;
    }

    // ── Comparison operators ────────────────────────────────────────────

    const operator ==(other : const String&) : bool {
        if (_size != other._size) return false;
        return CharHelpers::equals(_buf, other._buf, _size);
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
}


// ═════════════════════════════════════════════════════════════════════════════
// StringBuilder — Mutable string builder backed by a single growable buffer.
//
// Internally it stores:
//   - _buf:      owner of a contiguous char buffer (null-terminated)
//   - _size:     number of characters currently in the buffer
//   - _capacity: total allocated size of the buffer (excluding '\0')
//
// The buffer is always null-terminated at position _size.
// When appending would exceed capacity, the buffer is grown (doubled).
// ═════════════════════════════════════════════════════════════════════════════
class StringBuilder {
    _buf      : char[]!;              // owner, null-terminated buffer
    _size     : unsigned int;         // number of characters
    _capacity : unsigned int;         // allocated capacity (chars, not counting '\0')

public:
    // ── Constructors ────────────────────────────────────────────────────

    /** Default: empty builder. */
    StringBuilder() -> default;

    /** Construct from a String. */
    StringBuilder(s : const String&) : _buf(null), _size(0), _capacity(0) {
        if (s.size() > 0) {
            _capacity = s.size();
            alloc_sz : unsigned int = _capacity + 1;
            _buf = new char[alloc_sz];
            CharHelpers::copy(_buf, 0, s.data(), 0, s.size());
            _size = s.size();
            _buf[_size] = '\0';
        }
    }

    /** Construct from a const char[] (e.g. a string literal).
     *  The source array is expected to include a null terminator:
     *  src.size == number_of_characters + 1.
     *  A new owned buffer is allocated and the content is copied.
     */
    StringBuilder(src : const char[]) : _buf(null), _size(0), _capacity(0) {
        if (src.size > 1) {
            _size = src.size - 1;
            _capacity = _size;
            alloc_sz : unsigned int = src.size;
            _buf = new char[alloc_sz];
            CharHelpers::copy(_buf, 0, src, 0, src.size);
        }
    }

    /** Copy constructor. */
    StringBuilder(other : const StringBuilder&) : _buf(null), _size(0), _capacity(0) {
        if (other._size > 0) {
            _capacity = other._size;
            alloc_sz : unsigned int = _capacity + 1;
            _buf = new char[alloc_sz];
            CharHelpers::copy(_buf, 0, other._buf, 0, other._size + 1);
            _size = other._size;
        }
    }

    // ── Accessors ───────────────────────────────────────────────────────

    /** Total number of characters. */
    const size() : unsigned int { return _size; }

    /** True when the builder contains no characters. */
    const empty() : bool { return _size == 0; }

    /** Return the character at position `index`. No bounds check (POC). */
    const char_at(index : int) : char {
        return _buf[index];
    }

    /** Return a reference to the character at position `index`. No bounds check (POC). */
    char_at(index : int) : char& {
        return _buf[index];
    }


    /** Return a non-owning pointer to the internal buffer.
     *  Valid as long as this StringBuilder is alive and not reallocated.
     *  Returns null for empty builders.
     */
    const data() : char[]* {
        return _buf;
    }

    // ── Mutating methods (return StringBuilder& for fluent chaining) ────

    /** Append a String. */
    append(s : const String&) : StringBuilder& {
        if (s.size() > 0) {
            _ensure_capacity(s.size());
            CharHelpers::copy(_buf, _size, s.data(), 0, s.size());
            _size = _size + s.size();
            _buf[_size] = '\0';
        }
        return this;
    }

    /** Append a const char[] (e.g. a string literal).
     *  src.size is expected to include the null terminator.
     */
    append(src : const char[]) : StringBuilder& {
        if (src.size > 1) {
            count : unsigned int = src.size - 1;
            _ensure_capacity(count);
            CharHelpers::copy(_buf, _size, src, 0, count);
            _size = _size + count;
            _buf[_size] = '\0';
        }
        return this;
    }

    /** Append a single character. */
    append_char(c : char) : StringBuilder& {
        _ensure_capacity(1);
        _buf[_size] = c;
        _size = _size + 1;
        _buf[_size] = '\0';
        return this;
    }

    /** Clear all content. */
    clear() : StringBuilder& {
        _size = 0;
        if (_buf != null) {
            _buf[0] = '\0';
        }
        return this;
    }

    // ── Operators ───────────────────────────────────────────────────────

    /** += with a String. */
    operator +=(s : const String&) : StringBuilder& {
        return append(s);
    }

    /** + with a String: returns a new StringBuilder containing this + s. */
    const operator +(s : const String&) : StringBuilder {
        result : StringBuilder(this);
        result.append(s);
        return result;
    }

    // ── Private helpers ─────────────────────────────────────────────────
private:

    /** Ensure there is room for `extra` more chars. */
    _ensure_capacity(extra : int) {
        needed : int = _size + extra;
        if (needed <= _capacity) {
            return;
        }
        // Grow
        new_cap : unsigned int = _capacity * 2;
        if (new_cap < 16) {
            new_cap = 16;
        }
        if (new_cap < needed) {
            new_cap = needed;
        }
        alloc_sz : unsigned int = new_cap + 1;
        new_buf : char[]! = new char[alloc_sz];
        // Copy existing content (only if there is an existing buffer)
        if (_buf != null) {
            CharHelpers::copy(new_buf, 0, _buf, 0, _size + 1);
            delete _buf;
        }
        _buf = new_buf;
        _capacity = new_cap;
    }
}
