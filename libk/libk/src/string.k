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
// Parameters use const char[] for read-only access so that owners (char[]!),
// pointers (char[]*), links (char[]+), views (char[]?), and references
// (char[]) can all be passed without ownership transfer.
// ═════════════════════════════════════════════════════════════════════════════
public final class CharHelpers {
public:

    /** Copy `count` chars from src[srcOff..] to dst[dstOff..].
     *  Buffers must not overlap. */
    public static copy(dst: char[], dstOff: unsigned int, src: const char[], srcOff: unsigned int, count: unsigned int) {
        i : int = 0;
        while (i < count) {
            dst[dstOff + i] = src[srcOff + i];
            i = i + 1;
        }
    }

    /** Copy `count` chars in reverse order to safely handle overlapping
     *  regions when dstOff > srcOff within the same buffer. */
    public static copyBackward(dst: char[], dstOff: unsigned int, src: const char[], srcOff: unsigned int, count: unsigned int) {
        i : int = count;
        while (i > 0) {
            i = i - 1;
            dst[dstOff + i] = src[srcOff + i];
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

    /** Compare `len` chars at given offsets in both arrays.
     *  Returns true if all characters match. */
    public static equalsAt(a: const char[], aOff: unsigned int, b: const char[], bOff: unsigned int, len: unsigned int) : bool {
        i : int = 0;
        while (i < len) {
            if (a[aOff + i] != b[bOff + i]) return false;
            i = i + 1;
        }
        return true;
    }

    /** Fill `count` positions in dst starting at dstOff with char `c`. */
    public static fill(dst: char[], dstOff: unsigned int, c: char, count: unsigned int) {
        i : int = 0;
        while (i < count) {
            dst[dstOff + i] = c;
            i = i + 1;
        }
    }

    /** Find first occurrence of char `c` in buf[off..off+len).
     *  Returns index relative to buf start, or -1 if not found. */
    public static findChar(buf: const char[], off: unsigned int, len: unsigned int, c: char) : int {
        i : int = 0;
        while (i < len) {
            if (buf[off + i] == c) {
                r : int = off + i;
                return r;
            }
            i = i + 1;
        }
        return -1;
    }

    /** Find last occurrence of char `c` in buf[off..off+len).
     *  Returns index relative to buf start, or -1 if not found. */
    public static rfindChar(buf: const char[], off: unsigned int, len: unsigned int, c: char) : int {
        i : int = len;
        while (i > 0) {
            i = i - 1;
            if (buf[off + i] == c) {
                r : int = off + i;
                return r;
            }
        }
        return -1;
    }

    /** Find first occurrence of needle[nOff..nOff+nLen) in
     *  haystack[hOff..hOff+hLen).
     *  Returns index relative to haystack start, or -1 if not found.
     *  Uses brute-force O(n*m) algorithm. */
    public static find(haystack: const char[], hOff: unsigned int, hLen: unsigned int, needle: const char[], nOff: unsigned int, nLen: unsigned int) : int {
        if (nLen == 0) {
            r : int = hOff;
            return r;
        }
        if (nLen > hLen) return -1;
        limit : int = hLen - nLen + 1;
        i : int = 0;
        while (i < limit) {
            if (equalsAt(haystack, hOff + i, needle, nOff, nLen)) {
                r : int = hOff + i;
                return r;
            }
            i = i + 1;
        }
        return -1;
    }

    /** Find last occurrence of needle[nOff..nOff+nLen) in
     *  haystack[hOff..hOff+hLen).
     *  Returns index relative to haystack start, or -1 if not found. */
    public static rfind(haystack: const char[], hOff: unsigned int, hLen: unsigned int, needle: const char[], nOff: unsigned int, nLen: unsigned int) : int {
        if (nLen == 0) {
            r : int = hOff + hLen;
            return r;
        }
        if (nLen > hLen) return -1;
        i : int = hLen - nLen + 1;
        while (i > 0) {
            i = i - 1;
            if (equalsAt(haystack, hOff + i, needle, nOff, nLen)) {
                r : int = hOff + i;
                return r;
            }
        }
        return -1;
    }

    /** Lexicographic compare of `len` chars at given offsets.
     *  Returns <0 if a < b, 0 if equal, >0 if a > b. */
    public static compare(a: const char[], aOff: unsigned int, b: const char[], bOff: unsigned int, len: unsigned int) : int {
        i : int = 0;
        while (i < len) {
            diff : int = a[aOff + i] - b[bOff + i];
            if (diff != 0) return diff;
            i = i + 1;
        }
        return 0;
    }

    /** Return true if `c` is a whitespace character (space, tab, newline,
     *  carriage return). */
    public static isWhitespace(c: char) : bool {
        if (c == ' ') return true;
        if (c == '\t') return true;
        if (c == '\n') return true;
        if (c == '\r') return true;
        return false;
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// String — Immutable, final class wrapping a null-terminated char buffer.
//
// After construction every String instance satisfies:
//   • _size >= 0
//   • _buf is either null (empty string) or a heap-allocated char[]!
//     whose element at index _size is '\0'.
//   • _size does not include the null terminator and may be smaller
//     than the allocated buffer size.
//
// NOTE: This is a proof-of-concept for ASCII strings only.
// ═════════════════════════════════════════════════════════════════════════════
const final class String {
    _buf  : char[]!;              // owner, null-terminated buffer
    _size : unsigned int;         // number of characters (excluding '\0')

public:
    // ── Constructors ────────────────────────────────────────────────────

    /** Default: empty string (null buffer, size 0). */
    String() -> default;

    /** Construct a String of given size from an owner buffer (takes ownership).
     *  The buffer must already be null-terminated at position `sz`.
     *  This is the primary internal construction path.
     */
    String(buf : char[]!, sz : unsigned int) : _buf(buf), _size(sz) {}

    /** Copy constructor — allocates a new buffer and copies the content. */
    String(other : const String&) : _buf(null), _size(other._size) {
        if (_size > 0) {
            allocSz : unsigned int = _size + 1;
            _buf = new char[allocSz];
            CharHelpers::copy(_buf, 0, other._buf, 0, _size + 1);
        }
    }

    /** Drain constructor — steals the buffer and size from another String,
     *  leaving it in a valid empty state. */
    String(other : String#) : _buf(other._buf), _size(other._size) {
        other._buf = null;
        other._size = 0;
    }

    /** Construct from a const char[] (e.g. a string literal).
     *  The source array is expected to include a null terminator:
     *  src.size == number_of_characters + 1.
     *  A new owned buffer is allocated and the content is copied.
     */
    String(src : const char[]) : _buf(null), _size(0) {
        if (src.size > 1) {
            _size = src.size - 1;
            allocSz : unsigned int = src.size;
            _buf = new char[allocSz];
            CharHelpers::copy(_buf, 0, src, 0, src.size);
        }
    }

    /** Construct from a StringBuilder.
     *  The StringBuilder maintains a single-fragment invariant, so
     *  copyTo always does a simple sequential copy.
     */
    String(sb : const StringBuilder&) : _buf(null), _size(0) {
        _size = sb.size();
        if (_size > 0) {
            allocSz : unsigned int = _size + 1;
            _buf = new char[allocSz];
            sb.copyTo(_buf, 0);
            _buf[_size] = '\0';
        }
    }

    // ── Accessors (all const — implicit in a const class) ───────────────

    /** Number of characters (excluding the terminal '\0'). */
    const size() : unsigned int { return _size; }

    /** True when the string contains no characters. */
    const empty() : bool { return _size == 0; }

    /** Return the character at position `index`. No bounds check (POC). */
    const at(index : unsigned int) : char {
        return _buf[index];
    }

    /** Return a non-owning pointer to the internal buffer.
     *  Valid as long as this String is alive. Returns null for empty strings.
     */
    const data() : char[]? {
        return _buf;
    }

    // ── Search methods ──────────────────────────────────────────────────

    /** Find the first occurrence of character `c`.
     *  Returns the index, or -1 if not found. */
    const find(c : char) : int {
        return CharHelpers::findChar(_buf, 0, _size, c);
    }

    /** Find the first occurrence of `needle` in this string.
     *  Returns the starting index, or -1 if not found. */
    const find(needle : const String&) : int {
        if (needle._size == 0) return 0;
        if (_size == 0) return -1;
        return CharHelpers::find(_buf, 0, _size, needle._buf, 0, needle._size);
    }

    /** Find the last occurrence of character `c`.
     *  Returns the index, or -1 if not found. */
    const rfind(c : char) : int {
        return CharHelpers::rfindChar(_buf, 0, _size, c);
    }

    /** Find the last occurrence of `needle` in this string.
     *  Returns the starting index, or -1 if not found. */
    const rfind(needle : const String&) : int {
        if (needle._size == 0) {
            r : int = _size;
            return r;
        }
        if (_size == 0) return -1;
        return CharHelpers::rfind(_buf, 0, _size, needle._buf, 0, needle._size);
    }

    /** Return true if this string contains the character `c`. */
    const contains(c : char) : bool {
        return find(c) >= 0;
    }

    /** Return true if this string contains `needle`. */
    const contains(needle : const String&) : bool {
        return find(needle) >= 0;
    }

    /** Alias for find(needle). */
    const indexOf(needle : const String&) : int {
        return find(needle);
    }

    /** Alias for find(c). */
    const indexOf(c : char) : int {
        return find(c);
    }

    // ── Prefix / suffix tests ───────────────────────────────────────────

    /** Return true if this string begins with `prefix`. */
    const beginsWith(prefix : const String&) : bool {
        if (prefix._size > _size) return false;
        if (prefix._size == 0) return true;
        return CharHelpers::equalsAt(_buf, 0, prefix._buf, 0, prefix._size);
    }

    /** Return true if this string ends with `suffix`. */
    const endsWith(suffix : const String&) : bool {
        if (suffix._size > _size) return false;
        if (suffix._size == 0) return true;
        off : unsigned int = _size - suffix._size;
        return CharHelpers::equalsAt(_buf, off, suffix._buf, 0, suffix._size);
    }

    // ── Extraction ──────────────────────────────────────────────────────

    /** Return a new String containing `len` characters starting at `start`.
     *  If start + len > size(), the result is truncated. */
    const substr(start : unsigned int, len : unsigned int) : String {
        if (start >= _size) {
            emptyStr : String;
            return emptyStr;
        }
        actual : unsigned int = len;
        if (start + actual > _size) {
            actual = _size - start;
        }
        if (actual == 0) {
            emptyStr : String;
            return emptyStr;
        }
        allocSz : unsigned int = actual + 1;
        buf : char[]! = new char[allocSz];
        CharHelpers::copy(buf, 0, _buf, start, actual);
        buf[actual] = '\0';
        result : String(buf, actual);
        return result;
    }

    /** Return a new String containing the first `count` characters. */
    const first(count : unsigned int) : String {
        return substr(0, count);
    }

    /** Return a new String containing the last `count` characters. */
    const last(count : unsigned int) : String {
        if (count >= _size) {
            copy : String(this);
            return copy;
        }
        return substr(_size - count, count);
    }

    // ── Comparison ──────────────────────────────────────────────────────

    /** Lexicographic comparison.
     *  Returns <0 if this < other, 0 if equal, >0 if this > other. */
    const compareTo(other : const String&) : int {
        minLen : unsigned int = _size;
        if (other._size < minLen) minLen = other._size;
        if (minLen > 0) {
            cmp : int = CharHelpers::compare(_buf, 0, other._buf, 0, minLen);
            if (cmp != 0) return cmp;
        }
        if (_size < other._size) return -1;
        if (_size > other._size) return 1;
        return 0;
    }

    // ── Comparison operators ────────────────────────────────────────────

    /** Equality: same length and identical characters. */
    const operator ==(other : const String&) : bool {
        if (_size != other._size) return false;
        if (_size == 0) return true;
        return CharHelpers::equals(_buf, other._buf, _size);
    }

    /** Inequality. */
    const operator !=(other : const String&) : bool {
        return !(this == other);
    }

    /** Less than (lexicographic). */
    const operator <(other : const String&) : bool {
        return compareTo(other) < 0;
    }

    /** Less than or equal (lexicographic). */
    const operator <=(other : const String&) : bool {
        return compareTo(other) <= 0;
    }

    /** Greater than (lexicographic). */
    const operator >(other : const String&) : bool {
        return compareTo(other) > 0;
    }

    /** Greater than or equal (lexicographic). */
    const operator >=(other : const String&) : bool {
        return compareTo(other) >= 0;
    }

    // ── Concatenation ──────────────────────────────────────────────────

    /** Concatenation — returns a new StringBuilder containing this + other. */
    const operator +(other : const String&) : StringBuilder {
        sb : StringBuilder(this);
        sb.append(other);
        return sb;
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// StringBuilder — Mutable string builder backed by an array of fragments.
//
// Internally it stores:
//   - _pool:      owner of a flat char buffer holding all fragment data
//   - _poolUsed:  bytes currently used in the pool
//   - _poolCap:   allocated pool capacity
//   - _starts:    owner of unsigned int array — start offset of each fragment
//   - _lens:      owner of unsigned int array — length of each fragment
//   - _fragCount: number of active fragments
//   - _fragCap:   allocated capacity of _starts/_lens arrays
//   - _totalSize: total number of characters across all fragments
//
// Fragment data is appended to the end of _pool. Logical ordering is tracked
// by the _starts/_lens arrays. Insertions add new data at the pool tail and
// insert a metadata entry (shifting only unsigned int indices, not char data).
//
// Consolidation (merging all fragments into a single contiguous buffer)
// happens only when converting to String or when a mutation requires
// contiguous access (reverse, trim, remove).
// ═════════════════════════════════════════════════════════════════════════════
class StringBuilder {
    _pool      : char[]!;            // flat buffer for all fragment data
    _poolUsed  : unsigned int;       // bytes used in pool
    _poolCap   : unsigned int;       // allocated pool capacity
    _starts    : unsigned int[]!;    // start offset of each fragment in _pool
    _lens      : unsigned int[]!;    // length of each fragment
    _fragCount : unsigned int;       // number of fragments
    _fragCap   : unsigned int;       // capacity of _starts/_lens arrays
    _totalSize : unsigned int;       // total chars across all fragments

public:
    // ── Constructors ────────────────────────────────────────────────────

    /** Default: empty builder. */
    StringBuilder() -> default;

    /** Construct from a String. */
    StringBuilder(s : const String&) : _pool(null), _poolUsed(0), _poolCap(0), _starts(null), _lens(null), _fragCount(0), _fragCap(0), _totalSize(0) {
        if (s.size() > 0) {
            _ensurePoolCap(s.size());
            _ensureFragCap(1);
            CharHelpers::copy(_pool, 0, s.data(), 0, s.size());
            _poolUsed = s.size();
            _starts[0] = 0;
            _lens[0] = s.size();
            _fragCount = 1;
            _totalSize = s.size();
        }
    }

    /** Construct from a const char[] (e.g. a string literal).
     *  The source array is expected to include a null terminator:
     *  src.size == number_of_characters + 1.
     */
    StringBuilder(src : const char[]) : _pool(null), _poolUsed(0), _poolCap(0), _starts(null), _lens(null), _fragCount(0), _fragCap(0), _totalSize(0) {
        if (src.size > 1) {
            sz : unsigned int = src.size - 1;
            _ensurePoolCap(sz);
            _ensureFragCap(1);
            CharHelpers::copy(_pool, 0, src, 0, sz);
            _poolUsed = sz;
            _starts[0] = 0;
            _lens[0] = sz;
            _fragCount = 1;
            _totalSize = sz;
        }
    }

    /** Copy constructor — deep copies with consolidation.
     *  Uses charAt-based copy to work around a compiler codegen issue
     *  with multi-fragment copyTo called across method boundaries.
     */
    StringBuilder(other : const StringBuilder&) : _pool(null), _poolUsed(0), _poolCap(0), _starts(null), _lens(null), _fragCount(0), _fragCap(0), _totalSize(0) {
        if (other._totalSize > 0) {
            _ensurePoolCap(other._totalSize);
            _ensureFragCap(1);
            i : unsigned int = 0;
            while (i < other._totalSize) {
                _pool[i] = other.charAt(i);
                i = i + 1;
            }
            _poolUsed = other._totalSize;
            _starts[0] = 0;
            _lens[0] = other._totalSize;
            _fragCount = 1;
            _totalSize = other._totalSize;
        }
    }

    /** Drain constructor from another StringBuilder.
     *  Steals all internal state from the source, leaving it empty.
     */
    StringBuilder(other : StringBuilder#) : _pool(other._pool), _poolUsed(other._poolUsed), _poolCap(other._poolCap), _starts(other._starts), _lens(other._lens), _fragCount(other._fragCount), _fragCap(other._fragCap), _totalSize(other._totalSize) {
        other._pool = null;
        other._poolUsed = 0;
        other._poolCap = 0;
        other._starts = null;
        other._lens = null;
        other._fragCount = 0;
        other._fragCap = 0;
        other._totalSize = 0;
    }

    // ── Accessors ───────────────────────────────────────────────────────

    /** Total number of characters across all fragments. */
    const size() : unsigned int { return _totalSize; }

    /** True when the builder contains no characters. */
    const empty() : bool { return _totalSize == 0; }

    /** Return the character at the given logical position.
     *  Traverses fragments — O(fragCount). No bounds check. */
    const charAt(index : unsigned int) : char {
        pos : unsigned int = 0;
        i : unsigned int = 0;
        while (i < _fragCount) {
            if (index < pos + _lens[i]) {
                return _pool[_starts[i] + index - pos];
            }
            pos = pos + _lens[i];
            i = i + 1;
        }
        return '\0';
    }

    /** Copy all fragment content to a destination buffer starting at dstOff. */
    const copyTo(dst : char[], dstOff : unsigned int) {
        off : unsigned int = dstOff;
        i : unsigned int = 0;
        while (i < _fragCount) {
            CharHelpers::copy(dst, off, _pool, _starts[i], _lens[i]);
            off = off + _lens[i];
            i = i + 1;
        }
    }

    // ── Conversion ──────────────────────────────────────────────────────

    /** Conversion operator — build an immutable String.
     *  Consolidates first to ensure single-fragment state, then copies. */
    operator() : String {
        if (_totalSize == 0) {
            emptyStr : String;
            return emptyStr;
        }
        _consolidate();
        allocSz : unsigned int = _totalSize + 1;
        buf : char[]! = new char[allocSz];
        CharHelpers::copy(buf, 0, _pool, 0, _totalSize);
        buf[_totalSize] = '\0';
        result : String(buf, _totalSize);
        return result;
    }

    /** Build and return an immutable String from the current content.
     *  This is a named alternative to the conversion operator.
     *  Copies the content into a new null-terminated buffer. */
    const toString() : String {
        result : String(this);
        return result;
    }

    // ── Search methods ──────────────────────────────────────────────────

    /** Find the first occurrence of character `c`.
     *  Returns the logical index, or -1 if not found. */
    const find(c : char) : int {
        pos : int = 0;
        i : unsigned int = 0;
        while (i < _fragCount) {
            j : int = 0;
            while (j < _lens[i]) {
                if (_pool[_starts[i] + j] == c) return pos + j;
                j = j + 1;
            }
            pos = pos + _lens[i];
            i = i + 1;
        }
        return -1;
    }

    /** Find the first occurrence of `needle`.
     *  Returns the starting index, or -1 if not found. */
    const find(needle : const String&) : int {
        if (needle.size() == 0) return 0;
        if (_totalSize == 0) return -1;
        tmp : String(this);
        return tmp.find(needle);
    }

    /** Find the last occurrence of character `c`.
     *  Returns the logical index, or -1 if not found. */
    const rfind(c : char) : int {
        result : int = -1;
        pos : int = 0;
        i : unsigned int = 0;
        while (i < _fragCount) {
            j : int = 0;
            while (j < _lens[i]) {
                if (_pool[_starts[i] + j] == c) result = pos + j;
                j = j + 1;
            }
            pos = pos + _lens[i];
            i = i + 1;
        }
        return result;
    }

    /** Find the last occurrence of `needle`.
     *  Returns the starting index, or -1 if not found. */
    const rfind(needle : const String&) : int {
        if (needle.size() == 0) {
            r : int = _totalSize;
            return r;
        }
        if (_totalSize == 0) return -1;
        tmp : String(this);
        return tmp.rfind(needle);
    }

    /** Return true if the content contains character `c`. */
    const contains(c : char) : bool {
        return find(c) >= 0;
    }

    /** Return true if the content contains `needle`. */
    const contains(needle : const String&) : bool {
        return find(needle) >= 0;
    }

    /** Alias for find(needle). */
    const indexOf(needle : const String&) : int {
        return find(needle);
    }

    /** Alias for find(c). */
    const indexOf(c : char) : int {
        return find(c);
    }

    /** Return true if the content begins with `prefix`. */
    const beginsWith(prefix : const String&) : bool {
        if (prefix.size() > _totalSize) return false;
        if (prefix.size() == 0) return true;
        tmp : String(this);
        return tmp.beginsWith(prefix);
    }

    /** Return true if the content ends with `suffix`. */
    const endsWith(suffix : const String&) : bool {
        if (suffix.size() > _totalSize) return false;
        if (suffix.size() == 0) return true;
        tmp : String(this);
        return tmp.endsWith(suffix);
    }

    /** Extract a substring as a new String.
     *  Uses charAt to avoid cross-method codegen issues. */
    const substr(start : unsigned int, len : unsigned int) : String {
        if (start >= _totalSize) {
            empty : String;
            return empty;
        }
        actual : unsigned int = len;
        if (start + actual > _totalSize) {
            actual = _totalSize - start;
        }
        if (actual == 0) {
            empty : String;
            return empty;
        }
        allocSz : unsigned int = actual + 1;
        buf : char[]! = new char[allocSz];
        i : unsigned int = 0;
        while (i < actual) {
            buf[i] = charAt(start + i);
            i = i + 1;
        }
        buf[actual] = '\0';
        result : String(buf, actual);
        return result;
    }

    /** Return a new String containing the first `count` characters. */
    const first(count : unsigned int) : String {
        return substr(0, count);
    }

    /** Return a new String containing the last `count` characters. */
    const last(count : unsigned int) : String {
        if (count >= _totalSize) {
            copy : String(this);
            return copy;
        }
        return substr(_totalSize - count, count);
    }

    // ── Mutating methods (return StringBuilder& for fluent chaining) ────

    /** Consolidate all fragments into a single contiguous buffer.
     *  Safe to call multiple times; no-op when already consolidated. */
    consolidate() : StringBuilder& {
        _consolidate();
        return this;
    }

    /** Append a String. O(1) amortized — no char copying of existing data. */
    append(s : const String&) : StringBuilder& {
        if (s.size() > 0) {
            _ensurePoolCap(s.size());
            _ensureFragCap(1);
            poolStart : unsigned int = _poolUsed;
            CharHelpers::copy(_pool, poolStart, s.data(), 0, s.size());
            _poolUsed = _poolUsed + s.size();
            _starts[_fragCount] = poolStart;
            _lens[_fragCount] = s.size();
            _fragCount = _fragCount + 1;
            _totalSize = _totalSize + s.size();
        }
        return this;
    }

    /** Append a const char[] (e.g. a string literal).
     *  src.size is expected to include the null terminator.
     */
    append(src : const char[]) : StringBuilder& {
        if (src.size > 1) {
            count : unsigned int = src.size - 1;
            _ensurePoolCap(count);
            _ensureFragCap(1);
            poolStart : unsigned int = _poolUsed;
            CharHelpers::copy(_pool, poolStart, src, 0, count);
            _poolUsed = _poolUsed + count;
            _starts[_fragCount] = poolStart;
            _lens[_fragCount] = count;
            _fragCount = _fragCount + 1;
            _totalSize = _totalSize + count;
        }
        return this;
    }

    /** Append another StringBuilder's content.
     *  Uses charAt-based copy to work around a compiler codegen issue. */
    append(other : const StringBuilder&) : StringBuilder& {
        if (other._totalSize > 0) {
            _ensurePoolCap(other._totalSize);
            _ensureFragCap(1);
            poolStart : unsigned int = _poolUsed;
            i : unsigned int = 0;
            while (i < other._totalSize) {
                _pool[poolStart + i] = other.charAt(i);
                i = i + 1;
            }
            _poolUsed = _poolUsed + other._totalSize;
            _starts[_fragCount] = poolStart;
            _lens[_fragCount] = other._totalSize;
            _fragCount = _fragCount + 1;
            _totalSize = _totalSize + other._totalSize;
        }
        return this;
    }

    /** Append a single character. */
    appendChar(c : char) : StringBuilder& {
        _ensurePoolCap(1);
        _ensureFragCap(1);
        poolStart : unsigned int = _poolUsed;
        _pool[poolStart] = c;
        _poolUsed = _poolUsed + 1;
        _starts[_fragCount] = poolStart;
        _lens[_fragCount] = 1;
        _fragCount = _fragCount + 1;
        _totalSize = _totalSize + 1;
        return this;
    }

    /** Prepend a String at the beginning.
     *  Consolidates first to maintain single-fragment invariant. */
    prepend(s : const String&) : StringBuilder& {
        if (s.size() > 0) {
            _consolidate();
            newTotal : unsigned int = _totalSize + s.size();
            newCap : unsigned int = newTotal + 1;
            newPool : char[]! = new char[newCap];
            // Copy prefix
            CharHelpers::copy(newPool, 0, s.data(), 0, s.size());
            // Copy existing content
            if (_totalSize > 0) {
                CharHelpers::copy(newPool, s.size(), _pool, 0, _totalSize);
            }
            delete _pool;
            _pool = newPool;
            _poolUsed = newTotal;
            _poolCap = newCap;
            _starts[0] = 0;
            _lens[0] = newTotal;
            _fragCount = 1;
            _totalSize = newTotal;
        }
        return this;
    }

    /** Prepend a const char[] at the beginning. */
    prepend(src : const char[]) : StringBuilder& {
        if (src.size > 1) {
            tmp : String(src);
            return insert(0, tmp);
        }
        return this;
    }

    /** Insert a String at the given logical position.
     *  Consolidates first to maintain single-fragment invariant. */
    insert(pos : unsigned int, s : const String&) : StringBuilder& {
        if (s.size() == 0) return this;
        if (pos >= _totalSize) return append(s);
        if (pos == 0) return prepend(s);
        _consolidate();
        newTotal : unsigned int = _totalSize + s.size();
        newCap : unsigned int = newTotal + 1;
        newPool : char[]! = new char[newCap];
        // Copy [0..pos)
        CharHelpers::copy(newPool, 0, _pool, 0, pos);
        // Copy inserted content
        CharHelpers::copy(newPool, pos, s.data(), 0, s.size());
        // Copy [pos.._totalSize)
        tailLen : unsigned int = _totalSize - pos;
        if (tailLen > 0) {
            CharHelpers::copy(newPool, pos + s.size(), _pool, pos, tailLen);
        }
        delete _pool;
        _pool = newPool;
        _poolUsed = newTotal;
        _poolCap = newCap;
        _starts[0] = 0;
        _lens[0] = newTotal;
        _fragCount = 1;
        _totalSize = newTotal;
        return this;
    }

    /** Insert a const char[] at the given logical position. */
    insert(pos : unsigned int, src : const char[]) : StringBuilder& {
        tmp : String(src);
        return insert(pos, tmp);
    }

    /** Remove `count` characters starting at position `start`.
     *  Consolidates fragments first, then removes in the contiguous buffer. */
    remove(start : unsigned int, count : unsigned int) : StringBuilder& {
        if (count == 0) return this;
        if (start >= _totalSize) return this;
        _consolidate();
        actual : unsigned int = count;
        if (start + actual > _totalSize) {
            actual = _totalSize - start;
        }
        tailStart : unsigned int = start + actual;
        if (tailStart < _totalSize) {
            CharHelpers::copy(_pool, start, _pool, tailStart, _totalSize - tailStart);
        }
        _totalSize = _totalSize - actual;
        _poolUsed = _totalSize;
        if (_totalSize == 0) {
            _fragCount = 0;
        } else {
            _lens[0] = _totalSize;
        }
        return this;
    }

    /** Replace `count` characters starting at `start` with the content of `s`. */
    replace(start : unsigned int, count : unsigned int, s : const String&) : StringBuilder& {
        remove(start, count);
        return insert(start, s);
    }

    /** Replace `count` characters starting at `start` with a char[] literal. */
    replace(start : unsigned int, count : unsigned int, src : const char[]) : StringBuilder& {
        tmp : String(src);
        return replace(start, count, tmp);
    }

    /** Reverse the entire content in place. Consolidates first. */
    reverse() : StringBuilder& {
        if (_totalSize <= 1) return this;
        _consolidate();
        left : unsigned int = 0;
        right : unsigned int = _totalSize - 1;
        while (left < right) {
            tmp : char = _pool[left];
            _pool[left] = _pool[right];
            _pool[right] = tmp;
            left = left + 1;
            right = right - 1;
        }
        return this;
    }

    /** Remove leading whitespace characters (space, tab, newline, CR). */
    trimLeft() : StringBuilder& {
        if (_totalSize == 0) return this;
        _consolidate();
        start : unsigned int = 0;
        while (start < _totalSize) {
            if (!CharHelpers::isWhitespace(_pool[start])) {
                if (start > 0) {
                    return remove(0, start);
                }
                return this;
            }
            start = start + 1;
        }
        return clear();
    }

    /** Remove trailing whitespace characters (space, tab, newline, CR). */
    trimRight() : StringBuilder& {
        if (_totalSize == 0) return this;
        _consolidate();
        end : unsigned int = _totalSize;
        while (end > 0) {
            if (!CharHelpers::isWhitespace(_pool[end - 1])) {
                if (end < _totalSize) {
                    _totalSize = end;
                    _poolUsed = _totalSize;
                    _lens[0] = _totalSize;
                }
                return this;
            }
            end = end - 1;
        }
        return clear();
    }

    /** Remove leading and trailing whitespace. */
    trim() : StringBuilder& {
        trimLeft();
        return trimRight();
    }

    /** Clear all content. Pool memory is retained. */
    clear() : StringBuilder& {
        _fragCount = 0;
        _totalSize = 0;
        _poolUsed = 0;
        return this;
    }

    // ── Operators ───────────────────────────────────────────────────────

    /** Equality: same total length and identical content. */
    const operator ==(other : const StringBuilder&) : bool {
        if (_totalSize != other._totalSize) return false;
        if (_totalSize == 0) return true;
        a : String(this);
        b : String(other);
        return a == b;
    }

    /** Inequality. */
    const operator !=(other : const StringBuilder&) : bool {
        return !(this == other);
    }

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

    /** Ensure the pool has room for `extra` more chars. */
    _ensurePoolCap(extra : unsigned int) {
        needed : unsigned int = _poolUsed + extra;
        if (needed <= _poolCap) return;
        newCap : unsigned int = _poolCap * 2;
        if (newCap < 32) newCap = 32;
        if (newCap < needed) newCap = needed;
        newPool : char[]! = new char[newCap];
        if (_pool != null) {
            CharHelpers::copy(newPool, 0, _pool, 0, _poolUsed);
            delete _pool;
        }
        _pool = newPool;
        _poolCap = newCap;
    }

    /** Ensure _starts/_lens arrays have room for `extra` more entries. */
    _ensureFragCap(extra : unsigned int) {
        needed : unsigned int = _fragCount + extra;
        if (needed <= _fragCap) return;
        newCap : unsigned int = _fragCap * 2;
        if (newCap < 4) newCap = 4;
        if (newCap < needed) newCap = needed;
        newStarts : unsigned int[]! = new unsigned int[newCap];
        newLens : unsigned int[]! = new unsigned int[newCap];
        if (_starts != null) {
            i : unsigned int = 0;
            while (i < _fragCount) {
                newStarts[i] = _starts[i];
                newLens[i] = _lens[i];
                i = i + 1;
            }
            delete _starts;
            delete _lens;
        }
        _starts = newStarts;
        _lens = newLens;
        _fragCap = newCap;
    }

    /** Shift fragment entries at [startIdx.._fragCount) right by `count` positions. */
    _shiftFragsRight(startIdx : unsigned int, count : unsigned int) {
        if (_fragCount <= startIdx) return;
        i : unsigned int = _fragCount;
        while (i > startIdx) {
            i = i - 1;
            _starts[i + count] = _starts[i];
            _lens[i + count] = _lens[i];
        }
    }

    /** Consolidate all fragments into a single contiguous buffer.
     *  After this call, _fragCount <= 1. */
    _consolidate() {
        if (_fragCount <= 1) return;
        if (_totalSize == 0) {
            _fragCount = 0;
            _poolUsed = 0;
            return;
        }
        newCap : unsigned int = _totalSize + 1;
        newPool : char[]! = new char[newCap];
        // Inline copyTo to avoid compiler codegen bug with multi-fragment
        // method calls in certain contexts.
        off : unsigned int = 0;
        i : unsigned int = 0;
        while (i < _fragCount) {
            CharHelpers::copy(newPool, off, _pool, _starts[i], _lens[i]);
            off = off + _lens[i];
            i = i + 1;
        }
        newPool[_totalSize] = '\0';
        delete _pool;
        _pool = newPool;
        _poolUsed = _totalSize;
        _poolCap = newCap;
        _starts[0] = 0;
        _lens[0] = _totalSize;
        _fragCount = 1;
    }

    /** Find the fragment index containing logical position `pos`. */
    const _findFragIdx(pos : unsigned int) : unsigned int {
        idx : unsigned int = 0;
        cumPos : unsigned int = 0;
        while (idx < _fragCount) {
            if (cumPos + _lens[idx] > pos) {
                return idx;
            }
            cumPos = cumPos + _lens[idx];
            idx = idx + 1;
        }
        return _fragCount;
    }

    /** Find the local offset within the fragment that contains logical position `pos`. */
    const _findFragLocalOff(pos : unsigned int) : unsigned int {
        cumPos : unsigned int = 0;
        idx : unsigned int = 0;
        while (idx < _fragCount) {
            if (cumPos + _lens[idx] > pos) {
                r : unsigned int = pos - cumPos;
                return r;
            }
            cumPos = cumPos + _lens[idx];
            idx = idx + 1;
        }
        return 0;
    }
}
