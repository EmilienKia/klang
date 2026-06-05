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
// Encoding — the three Unicode storage/transfer encodings supported by the
// string classes and the character-array conversion helpers.
// ═════════════════════════════════════════════════════════════════════════════
public enum Encoding {
    UTF8;
    UTF16;
    UTF32;
};

// ═════════════════════════════════════════════════════════════════════════════
// ConsolidationPolicy — chooses the storage encoding used when a StringBuilder
// is converted to an immutable String (toString / operator() String).
//   MostCompact  : pick the encoding with the smallest byte footprint (default).
//   FirstFragment: use the encoding of the builder's first fragment.
//   ForceUtf8 / ForceUtf16 / ForceUtf32 : always use the named encoding.
// ═════════════════════════════════════════════════════════════════════════════
public enum ConsolidationPolicy {
    MostCompact;
    FirstFragment;
    ForceUtf8;
    ForceUtf16;
    ForceUtf32;
};

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

    // ── Unicode transcoding (UTF-32 ⇄ UTF-8 / UTF-16) ────────────────────

    /** Number of UTF-8 bytes needed to encode the code point `c`. */
    public static utf8Len(c: char) : unsigned int {
        if (c < 0x80u)    return 1u;
        if (c < 0x800u)   return 2u;
        if (c < 0x10000u) return 3u;
        return 4u;
    }

    /** Number of UTF-16 code units (1 or 2) needed to encode `c`. */
    public static utf16Len(c: char) : unsigned int {
        if (c < 0x10000u) return 1u;
        return 2u;
    }

    /** Total UTF-8 byte count for the first `len` code points of `src`. */
    public static utf8Size(src: const char[], len: unsigned int) : unsigned int {
        total : unsigned int = 0u;
        i : unsigned int = 0u;
        while (i < len) {
            total = total + utf8Len(src[i]);
            i = i + 1u;
        }
        return total;
    }

    /** Total UTF-16 code-unit count for the first `len` code points of `src`. */
    public static utf16Size(src: const char[], len: unsigned int) : unsigned int {
        total : unsigned int = 0u;
        i : unsigned int = 0u;
        while (i < len) {
            total = total + utf16Len(src[i]);
            i = i + 1u;
        }
        return total;
    }

    /** Encode `len` UTF-32 code points from `src` into a new, null-terminated
     *  UTF-8 `unsigned byte[]`. */
    public static toUtf8(src: const char[], len: unsigned int) : unsigned byte[]! {
        n : unsigned int = utf8Size(src, len);
        out : unsigned byte[]! = new unsigned byte[n + 1u];
        o : unsigned int = 0u;
        i : unsigned int = 0u;
        while (i < len) {
            c : char = src[i];
            if (c < 0x80u) {
                out[o] = (unsigned byte) c;
                o = o + 1u;
            } else if (c < 0x800u) {
                out[o] = (unsigned byte) (0xC0u | (c >> 6));
                out[o + 1u] = (unsigned byte) (0x80u | (c & 0x3Fu));
                o = o + 2u;
            } else if (c < 0x10000u) {
                out[o] = (unsigned byte) (0xE0u | (c >> 12));
                out[o + 1u] = (unsigned byte) (0x80u | ((c >> 6) & 0x3Fu));
                out[o + 2u] = (unsigned byte) (0x80u | (c & 0x3Fu));
                o = o + 3u;
            } else {
                out[o] = (unsigned byte) (0xF0u | (c >> 18));
                out[o + 1u] = (unsigned byte) (0x80u | ((c >> 12) & 0x3Fu));
                out[o + 2u] = (unsigned byte) (0x80u | ((c >> 6) & 0x3Fu));
                out[o + 3u] = (unsigned byte) (0x80u | (c & 0x3Fu));
                o = o + 4u;
            }
            i = i + 1u;
        }
        out[o] = (unsigned byte) 0;
        return out;
    }

    /** Encode `len` UTF-32 code points from `src` into a new, null-terminated
     *  UTF-16 `unsigned short[]` (surrogate pairs for supplementary planes). */
    public static toUtf16(src: const char[], len: unsigned int) : unsigned short[]! {
        n : unsigned int = utf16Size(src, len);
        out : unsigned short[]! = new unsigned short[n + 1u];
        o : unsigned int = 0u;
        i : unsigned int = 0u;
        while (i < len) {
            c : char = src[i];
            if (c < 0x10000u) {
                out[o] = (unsigned short) c;
                o = o + 1u;
            } else {
                v : char = c - 0x10000u;
                out[o] = (unsigned short) (0xD800u + (v >> 10));
                out[o + 1u] = (unsigned short) (0xDC00u + (v & 0x3FFu));
                o = o + 2u;
            }
            i = i + 1u;
        }
        out[o] = (unsigned short) 0;
        return out;
    }

    // ── Unicode decoding (UTF-8 / UTF-16 → UTF-32) ───────────────────────

    /** Number of code points encoded in the first `len` bytes of UTF-8 `src`. */
    public static utf8CodePointCount(src: const unsigned byte[], len: unsigned int) : unsigned int {
        count : unsigned int = 0u;
        i : unsigned int = 0u;
        while (i < len) {
            b : int = src[i];
            adv : unsigned int = 1u;
            if (b >= 0xF0) {
                adv = 4u;
            } else if (b >= 0xE0) {
                adv = 3u;
            } else if (b >= 0xC0) {
                adv = 2u;
            }
            i = i + adv;
            count = count + 1u;
        }
        return count;
    }

    /** Number of code points encoded in the first `len` UTF-16 code units. */
    public static utf16CodePointCount(src: const unsigned short[], len: unsigned int) : unsigned int {
        count : unsigned int = 0u;
        i : unsigned int = 0u;
        while (i < len) {
            u : int = src[i];
            if (u >= 0xD800 && u < 0xDC00) {
                i = i + 2u;
            } else {
                i = i + 1u;
            }
            count = count + 1u;
        }
        return count;
    }

    /** Decode the first `len` bytes of UTF-8 `src` into a new, null-terminated
     *  UTF-32 `char[]`. Assumes well-formed input. */
    public static utf8ToUtf32(src: const unsigned byte[], len: unsigned int) : char[]! {
        n : unsigned int = utf8CodePointCount(src, len);
        out : char[]! = new char[n + 1u];
        o : unsigned int = 0u;
        i : unsigned int = 0u;
        while (i < len) {
            b0 : int = src[i];
            cp : char = 0;
            if (b0 < 0x80) {
                cp = (char) b0;
                i = i + 1u;
            } else if (b0 < 0xE0) {
                b1 : int = src[i + 1u];
                cp = (char) (((b0 & 0x1F) << 6) | (b1 & 0x3F));
                i = i + 2u;
            } else if (b0 < 0xF0) {
                b1 : int = src[i + 1u];
                b2 : int = src[i + 2u];
                cp = (char) (((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F));
                i = i + 3u;
            } else {
                b1 : int = src[i + 1u];
                b2 : int = src[i + 2u];
                b3 : int = src[i + 3u];
                cp = (char) (((b0 & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F));
                i = i + 4u;
            }
            out[o] = cp;
            o = o + 1u;
        }
        out[o] = (char) 0;
        return out;
    }

    /** Decode the first `len` UTF-16 code units of `src` into a new,
     *  null-terminated UTF-32 `char[]`. Assumes well-formed input. */
    public static utf16ToUtf32(src: const unsigned short[], len: unsigned int) : char[]! {
        n : unsigned int = utf16CodePointCount(src, len);
        out : char[]! = new char[n + 1u];
        o : unsigned int = 0u;
        i : unsigned int = 0u;
        while (i < len) {
            u : int = src[i];
            cp : char = 0;
            if (u >= 0xD800 && u < 0xDC00) {
                lo : int = src[i + 1u];
                cp = (char) (0x10000 + (((u - 0xD800) << 10) | (lo - 0xDC00)));
                i = i + 2u;
            } else {
                cp = (char) u;
                i = i + 1u;
            }
            out[o] = cp;
            o = o + 1u;
        }
        out[o] = (char) 0;
        return out;
    }

    // ── Typed buffer copies (byte / short) ───────────────────────────────

    /** Copy `count` unsigned bytes from src[srcOff..] to dst[dstOff..]. */
    public static copyBytes(dst: unsigned byte[], dstOff: unsigned int, src: const unsigned byte[], srcOff: unsigned int, count: unsigned int) {
        i : unsigned int = 0u;
        while (i < count) { dst[dstOff + i] = src[srcOff + i]; i = i + 1u; }
    }

    /** Copy `count` unsigned shorts from src[srcOff..] to dst[dstOff..]. */
    public static copyShorts(dst: unsigned short[], dstOff: unsigned int, src: const unsigned short[], srcOff: unsigned int, count: unsigned int) {
        i : unsigned int = 0u;
        while (i < count) { dst[dstOff + i] = src[srcOff + i]; i = i + 1u; }
    }

    // ── Single code-point access in UTF-8 / UTF-16 (O(index) walk) ───────

    /** Return the code point at logical position `cpIndex` in UTF-8 `buf`. */
    public static utf8CodePointAt(buf: const unsigned byte[], cpIndex: unsigned int) : char {
        i : unsigned int = 0u;
        cp : unsigned int = 0u;
        while (cp < cpIndex) {
            b : int = buf[i];
            if (b >= 0xF0) { i = i + 4u; }
            else if (b >= 0xE0) { i = i + 3u; }
            else if (b >= 0xC0) { i = i + 2u; }
            else { i = i + 1u; }
            cp = cp + 1u;
        }
        b0 : int = buf[i];
        if (b0 < 0x80) { return (char) b0; }
        if (b0 < 0xE0) { return (char) (((b0 & 0x1F) << 6) | (buf[i + 1u] & 0x3F)); }
        if (b0 < 0xF0) { return (char) (((b0 & 0x0F) << 12) | ((buf[i + 1u] & 0x3F) << 6) | (buf[i + 2u] & 0x3F)); }
        return (char) (((b0 & 0x07) << 18) | ((buf[i + 1u] & 0x3F) << 12) | ((buf[i + 2u] & 0x3F) << 6) | (buf[i + 3u] & 0x3F));
    }

    /** Return the code point at logical position `cpIndex` in UTF-16 `buf`. */
    public static utf16CodePointAt(buf: const unsigned short[], cpIndex: unsigned int) : char {
        i : unsigned int = 0u;
        cp : unsigned int = 0u;
        while (cp < cpIndex) {
            u : int = buf[i];
            if (u >= 0xD800 && u < 0xDC00) { i = i + 2u; } else { i = i + 1u; }
            cp = cp + 1u;
        }
        u0 : int = buf[i];
        if (u0 >= 0xD800 && u0 < 0xDC00) {
            lo : int = buf[i + 1u];
            return (char) (0x10000 + (((u0 - 0xD800) << 10) | (lo - 0xDC00)));
        }
        return (char) u0;
    }

    // ── Offset-aware access / range decode (for heterogeneous fragments) ──

    /** Return the code point at logical position `cpIndex`, where the UTF-8
     *  sequence starts at byte offset `byteStart` in `buf`. O(cpIndex). */
    public static utf8CodePointAtFrom(buf: const unsigned byte[], byteStart: unsigned int, cpIndex: unsigned int) : char {
        i : unsigned int = byteStart;
        cp : unsigned int = 0u;
        while (cp < cpIndex) {
            b : int = buf[i];
            if (b >= 0xF0) { i = i + 4u; }
            else if (b >= 0xE0) { i = i + 3u; }
            else if (b >= 0xC0) { i = i + 2u; }
            else { i = i + 1u; }
            cp = cp + 1u;
        }
        b0 : int = buf[i];
        if (b0 < 0x80) { return (char) b0; }
        if (b0 < 0xE0) { return (char) (((b0 & 0x1F) << 6) | (buf[i + 1u] & 0x3F)); }
        if (b0 < 0xF0) { return (char) (((b0 & 0x0F) << 12) | ((buf[i + 1u] & 0x3F) << 6) | (buf[i + 2u] & 0x3F)); }
        return (char) (((b0 & 0x07) << 18) | ((buf[i + 1u] & 0x3F) << 12) | ((buf[i + 2u] & 0x3F) << 6) | (buf[i + 3u] & 0x3F));
    }

    /** Return the code point at logical position `cpIndex`, where the UTF-16
     *  sequence starts at unit offset `unitStart` in `buf`. O(cpIndex). */
    public static utf16CodePointAtFrom(buf: const unsigned short[], unitStart: unsigned int, cpIndex: unsigned int) : char {
        i : unsigned int = unitStart;
        cp : unsigned int = 0u;
        while (cp < cpIndex) {
            u : int = buf[i];
            if (u >= 0xD800 && u < 0xDC00) { i = i + 2u; } else { i = i + 1u; }
            cp = cp + 1u;
        }
        u0 : int = buf[i];
        if (u0 >= 0xD800 && u0 < 0xDC00) {
            lo : int = buf[i + 1u];
            return (char) (0x10000 + (((u0 - 0xD800) << 10) | (lo - 0xDC00)));
        }
        return (char) u0;
    }

    /** Decode `byteLen` UTF-8 bytes from `src[byteStart..]` into `dst[dstOff..]`
     *  as UTF-32 code points. Returns the number of code points written. */
    public static utf8DecodeRangeInto(src: const unsigned byte[], byteStart: unsigned int, byteLen: unsigned int, dst: char[], dstOff: unsigned int) : unsigned int {
        i : unsigned int = byteStart;
        endPos : unsigned int = byteStart + byteLen;
        o : unsigned int = dstOff;
        n : unsigned int = 0u;
        while (i < endPos) {
            b0 : int = src[i];
            cp : char = 0;
            if (b0 < 0x80) {
                cp = (char) b0;
                i = i + 1u;
            } else if (b0 < 0xE0) {
                cp = (char) (((b0 & 0x1F) << 6) | (src[i + 1u] & 0x3F));
                i = i + 2u;
            } else if (b0 < 0xF0) {
                cp = (char) (((b0 & 0x0F) << 12) | ((src[i + 1u] & 0x3F) << 6) | (src[i + 2u] & 0x3F));
                i = i + 3u;
            } else {
                cp = (char) (((b0 & 0x07) << 18) | ((src[i + 1u] & 0x3F) << 12) | ((src[i + 2u] & 0x3F) << 6) | (src[i + 3u] & 0x3F));
                i = i + 4u;
            }
            dst[o] = cp;
            o = o + 1u;
            n = n + 1u;
        }
        return n;
    }

    /** Decode `unitLen` UTF-16 code units from `src[unitStart..]` into
     *  `dst[dstOff..]` as UTF-32 code points. Returns the number written. */
    public static utf16DecodeRangeInto(src: const unsigned short[], unitStart: unsigned int, unitLen: unsigned int, dst: char[], dstOff: unsigned int) : unsigned int {
        i : unsigned int = unitStart;
        endPos : unsigned int = unitStart + unitLen;
        o : unsigned int = dstOff;
        n : unsigned int = 0u;
        while (i < endPos) {
            u : int = src[i];
            cp : char = 0;
            if (u >= 0xD800 && u < 0xDC00) {
                lo : int = src[i + 1u];
                cp = (char) (0x10000 + (((u - 0xD800) << 10) | (lo - 0xDC00)));
                i = i + 2u;
            } else {
                cp = (char) u;
                i = i + 1u;
            }
            dst[o] = cp;
            o = o + 1u;
            n = n + 1u;
        }
        return n;
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// String — Immutable, final class wrapping a null-terminated char buffer.
//
// After construction every String instance satisfies:
//   • _size >= 0 (number of code points, excluding any terminator)
//   • _store holds the content in one of three encodings (UTF-8/16/32).
//     For an empty string (_size == 0) the active alternative is UTF-32
//     with a null buffer (no allocation).
//   • The active union alternative determines rawEncoding().
//
// The public API always exposes code points as `char` (UTF-32) regardless
// of the underlying storage encoding.
// ═════════════════════════════════════════════════════════════════════════════
const final class String {
    // CharStore — discriminated union backing String's compact storage.
    // Alternative order fixes the Kind index: utf8=0, utf16=1, utf32=2.
    // Declared as a nested type so the KDI import path can reconstruct the
    // full layout of String (a top-level union would not be collected into
    // the combined IR used to materialise imported aggregates).
    union CharStore {
        utf8  : unsigned byte[]!;
        utf16 : unsigned short[]!;
        utf32 : char[]!;
    }

private:
    _store : CharStore;          // compact storage (UTF-8 / UTF-16 / UTF-32)
    _size  : unsigned int;       // number of characters (code points)

public:
    // ── Constructors ────────────────────────────────────────────────────

    /** Default: empty string (no data; UTF-32 by convention). */
    String() : _size(0) {
        _store.utf32 = null;
    }

    /** Construct a String from a UTF-32 owner buffer (takes ownership).
     *  The buffer must be null-terminated at position `sz`. */
    String(buf : char[]!, sz : unsigned int) : _size(sz) {
        _store.utf32 = buf;
    }

    /** Copy constructor — duplicates the content in the same encoding. */
    String(other : const String&) : _size(other._size) {
        oenc : int = (int) other._store.index();
        if (other._size == 0u) {
            _store.utf32 = null;
        } else if (oenc == 2) {
            b : char[]! = new char[other._size + 1u];
            CharHelpers::copy(b, 0u, other._store.utf32, 0u, other._size + 1u);
            _store.utf32 = b;
        } else if (oenc == 1) {
            n : unsigned int = other._store.utf16.size;
            b : unsigned short[]! = new unsigned short[n];
            CharHelpers::copyShorts(b, 0u, other._store.utf16, 0u, n);
            _store.utf16 = b;
        } else {
            n : unsigned int = other._store.utf8.size;
            b : unsigned byte[]! = new unsigned byte[n];
            CharHelpers::copyBytes(b, 0u, other._store.utf8, 0u, n);
            _store.utf8 = b;
        }
    }

    /** Drain constructor — steals the buffer from another String. */
    String(other : String#) : _size(other._size) {
        oenc : int = (int) other._store.index();
        if (oenc == 2) {
            _store.utf32 = other._store.utf32;
            other._store.utf32 = null;
        } else if (oenc == 1) {
            _store.utf16 = other._store.utf16;
            other._store.utf16 = null;
        } else {
            _store.utf8 = other._store.utf8;
            other._store.utf8 = null;
        }
        other._size = 0u;
    }

    /** Construct from a const char[] (string literal) → UTF-32 storage. */
    String(src : const char[]) : _size(0) {
        if (src.size > 1u) {
            _size = src.size - 1u;
            b : char[]! = new char[src.size];
            CharHelpers::copy(b, 0u, src, 0u, src.size);
            _store.utf32 = b;
        } else {
            _store.utf32 = null;
        }
    }

    /** Construct from a UTF-8 `unsigned byte[]` (e.g. a `u8"…"` literal),
     *  stored as UTF-8. src.size includes the trailing null byte. */
    String(src : const unsigned byte[]) : _size(0) {
        if (src.size > 1u) {
            _size = CharHelpers::utf8CodePointCount(src, src.size - 1u);
            b : unsigned byte[]! = new unsigned byte[src.size];
            CharHelpers::copyBytes(b, 0u, src, 0u, src.size);
            _store.utf8 = b;
        } else {
            _store.utf32 = null;
        }
    }

    /** Construct from a UTF-16 `unsigned short[]` (e.g. a `u"…"` literal),
     *  stored as UTF-16. src.size includes the trailing null code unit. */
    String(src : const unsigned short[]) : _size(0) {
        if (src.size > 1u) {
            _size = CharHelpers::utf16CodePointCount(src, src.size - 1u);
            b : unsigned short[]! = new unsigned short[src.size];
            CharHelpers::copyShorts(b, 0u, src, 0u, src.size);
            _store.utf16 = b;
        } else {
            _store.utf32 = null;
        }
    }

    /** Construct from a UTF-32 `char[]`, transcoded to the requested storage
     *  encoding `enc`. */
    String(src : const char[], enc : Encoding) : _size(0) {
        n : unsigned int = 0u;
        if (src.size > 1u) {
            n = src.size - 1u;
        }
        _size = n;
        if (n == 0u) {
            _store.utf32 = null;
        } else if (enc == Encoding::UTF8) {
            _store.utf8 = CharHelpers::toUtf8(src, n);
        } else if (enc == Encoding::UTF16) {
            _store.utf16 = CharHelpers::toUtf16(src, n);
        } else {
            b : char[]! = new char[n + 1u];
            CharHelpers::copy(b, 0u, src, 0u, n);
            b[n] = (char) 0;
            _store.utf32 = b;
        }
    }

    /** Construct from a StringBuilder → UTF-32 storage. */
    String(sb : const StringBuilder&) : _size(0) {
        _size = sb.size();
        if (_size > 0u) {
            b : char[]! = new char[_size + 1u];
            sb.copyTo(b, 0u);
            b[_size] = (char) 0;
            _store.utf32 = b;
        } else {
            _store.utf32 = null;
        }
    }

    /** Internal: decode the content into a new, null-terminated UTF-32 `char[]`.
     *  Centralises all reads of the storage union. */
    const _u32() : char[]! {
        idx : int = (int) _store.index();
        if (idx == 2) {
            out : char[]! = new char[_size + 1u];
            if (_size > 0u) {
                CharHelpers::copy(out, 0u, _store.utf32, 0u, _size);
            }
            out[_size] = (char) 0;
            return out;
        }
        if (idx == 1) {
            return CharHelpers::utf16ToUtf32(_store.utf16, _store.utf16.size - 1u);
        }
        return CharHelpers::utf8ToUtf32(_store.utf8, _store.utf8.size - 1u);
    }

    // ── Accessors (all const — implicit in a const class) ───────────────

    /** Number of characters (excluding the terminal '\0'). */
    const size() : unsigned int { return _size; }

    /** True when the string contains no characters. */
    const empty() : bool { return _size == 0; }

    /** Return the character (code point) at logical position `index`.
     *  No bounds check. O(index) for UTF-8/UTF-16, O(1) for UTF-32. */
    const at(index : unsigned int) : char {
        idx : int = (int) _store.index();
        if (idx == 2) {
            return _store.utf32[index];
        }
        if (idx == 1) {
            return CharHelpers::utf16CodePointAt(_store.utf16, index);
        }
        return CharHelpers::utf8CodePointAt(_store.utf8, index);
    }

    /** Return the character (code point) at logical position `index`.
     *  Read-only subscript (String is immutable). */
    const operator [](index : unsigned int) : char {
        return at(index);
    }

    /** The underlying storage encoding. */
    const rawEncoding() : Encoding {
        idx : int = (int) _store.index();
        if (idx == 0) return Encoding::UTF8;
        if (idx == 1) return Encoding::UTF16;
        return Encoding::UTF32;
    }

    // ── Search methods ──────────────────────────────────────────────────

    /** Find the first occurrence of character `c`.
     *  Returns the index, or -1 if not found. */
    const find(c : char) : int {
        if (_size == 0u) return -1;
        b : char[]! = _u32();
        return CharHelpers::findChar(b, 0u, _size, c);
    }

    /** Find the first occurrence of `needle` in this string.
     *  Returns the starting index, or -1 if not found. */
    const find(needle : const String&) : int {
        if (needle._size == 0) return 0;
        if (_size == 0) return -1;
        b : char[]! = _u32();
        nb : char[]! = needle._u32();
        return CharHelpers::find(b, 0u, _size, nb, 0u, needle._size);
    }

    /** Find the last occurrence of character `c`.
     *  Returns the index, or -1 if not found. */
    const rfind(c : char) : int {
        if (_size == 0u) return -1;
        b : char[]! = _u32();
        return CharHelpers::rfindChar(b, 0u, _size, c);
    }

    /** Find the last occurrence of `needle` in this string.
     *  Returns the starting index, or -1 if not found. */
    const rfind(needle : const String&) : int {
        if (needle._size == 0) {
            r : int = _size;
            return r;
        }
        if (_size == 0) return -1;
        b : char[]! = _u32();
        nb : char[]! = needle._u32();
        return CharHelpers::rfind(b, 0u, _size, nb, 0u, needle._size);
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

    /** Count the number of non-overlapping occurrences of character `c`. */
    const count(c : char) : unsigned int {
        if (_size == 0u) return 0u;
        b : char[]! = _u32();
        n : unsigned int = 0u;
        i : unsigned int = 0u;
        while (i < _size) {
            if (b[i] == c) n = n + 1u;
            i = i + 1u;
        }
        return n;
    }

    /** Count the number of non-overlapping occurrences of `needle`. */
    const count(needle : const String&) : unsigned int {
        if (needle._size == 0) return 0;
        if (_size == 0u) return 0u;
        b : char[]! = _u32();
        nb : char[]! = needle._u32();
        n : unsigned int = 0u;
        i : unsigned int = 0u;
        limit : unsigned int = _size;
        while (i + needle._size <= limit) {
            if (CharHelpers::equalsAt(b, i, nb, 0u, needle._size)) {
                n = n + 1u;
                i = i + needle._size;
            } else {
                i = i + 1u;
            }
        }
        return n;
    }

    // ── Prefix / suffix tests ───────────────────────────────────────────

    /** Return true if this string begins with `prefix`. */
    const beginsWith(prefix : const String&) : bool {
        if (prefix._size > _size) return false;
        if (prefix._size == 0) return true;
        b : char[]! = _u32();
        pb : char[]! = prefix._u32();
        return CharHelpers::equalsAt(b, 0u, pb, 0u, prefix._size);
    }

    /** Return true if this string ends with `suffix`. */
    const endsWith(suffix : const String&) : bool {
        if (suffix._size > _size) return false;
        if (suffix._size == 0) return true;
        off : unsigned int = _size - suffix._size;
        b : char[]! = _u32();
        sb : char[]! = suffix._u32();
        return CharHelpers::equalsAt(b, off, sb, 0u, suffix._size);
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
        src : char[]! = _u32();
        allocSz : unsigned int = actual + 1;
        buf : char[]! = new char[allocSz];
        CharHelpers::copy(buf, 0u, src, start, actual);
        buf[actual] = (char) 0;
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
        if (minLen > 0u) {
            b : char[]! = _u32();
            ob : char[]! = other._u32();
            cmp : int = CharHelpers::compare(b, 0u, ob, 0u, minLen);
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
        b : char[]! = _u32();
        ob : char[]! = other._u32();
        return CharHelpers::equals(b, ob, _size);
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

    // ── Encoding / array conversions ────────────────────────────────────

    /** Return the content as a new, null-terminated UTF-32 `char[]`. */
    const toUtf32() : char[]! {
        return _u32();
    }

    /** Return the content as a new, null-terminated UTF-8 `unsigned byte[]`. */
    const toUtf8() : unsigned byte[]! {
        if ((int) _store.index() == 0) {
            // Already UTF-8: return a copy.
            n : unsigned int = _store.utf8.size;
            out : unsigned byte[]! = new unsigned byte[n];
            CharHelpers::copyBytes(out, 0u, _store.utf8, 0u, n);
            return out;
        }
        b : char[]! = _u32();
        return CharHelpers::toUtf8(b, _size);
    }

    /** Return the content as a new, null-terminated UTF-16 `unsigned short[]`. */
    const toUtf16() : unsigned short[]! {
        if ((int) _store.index() == 1) {
            n : unsigned int = _store.utf16.size;
            out : unsigned short[]! = new unsigned short[n];
            CharHelpers::copyShorts(out, 0u, _store.utf16, 0u, n);
            return out;
        }
        b : char[]! = _u32();
        return CharHelpers::toUtf16(b, _size);
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
// ═════════════════════════════════════════════════════════════════════════════
// StringBuilder — Mutable string builder with heterogeneous, multi-encoding
// fragment storage.
//
// Content is appended as a sequence of fragments, each kept in its NATIVE
// encoding (no transcoding on append). Three growable pools hold the raw units:
//   - _poolU8  : unsigned byte[]  — UTF-8 fragments
//   - _poolU16 : unsigned short[] — UTF-16 fragments
//   - _poolU32 : char[]           — UTF-32 fragments
// Four parallel metadata arrays describe each fragment in logical order:
//   - _fragEnc   : 0=UTF8, 1=UTF16, 2=UTF32
//   - _fragStart : start offset (in pool units) within the matching pool
//   - _fragUnits : number of pool units the fragment occupies
//   - _fragCp    : number of code points in the fragment
//
// The public API is char-based (code points). Random read access decodes per
// fragment; bulk reads decode everything to UTF-32 on demand. Any in-place
// MUTATION first consolidates all fragments into a single UTF-32 fragment, so
// mutation logic always operates on a contiguous _poolU32 buffer.
// ═════════════════════════════════════════════════════════════════════════════
class StringBuilder {
    _poolU8    : unsigned byte[]!;    _u8Used  : unsigned int;   _u8Cap  : unsigned int;
    _poolU16   : unsigned short[]!;   _u16Used : unsigned int;   _u16Cap : unsigned int;
    _poolU32   : char[]!;             _u32Used : unsigned int;   _u32Cap : unsigned int;
    _fragEnc   : unsigned byte[]!;    // encoding tag per fragment (0/1/2)
    _fragStart : unsigned int[]!;     // start offset (pool units) per fragment
    _fragUnits : unsigned int[]!;     // unit count per fragment
    _fragCp    : unsigned int[]!;     // code-point count per fragment
    _fragCount : unsigned int;        // number of fragments
    _fragCap   : unsigned int;        // capacity of the metadata arrays
    _totalSize : unsigned int;        // total code points across all fragments

public:
    // ── Constructors ────────────────────────────────────────────────────

    /** Default: empty builder (no allocation). */
    StringBuilder() : _u8Used(0), _u8Cap(0), _u16Used(0), _u16Cap(0), _u32Used(0), _u32Cap(0), _fragCount(0), _fragCap(0), _totalSize(0) {
        _poolU8 = null;
        _poolU16 = null;
        _poolU32 = null;
        _fragEnc = null;
        _fragStart = null;
        _fragUnits = null;
        _fragCp = null;
    }

    /** Construct from a String, mirroring the String's native storage encoding.
     *  (Logic is inlined rather than delegating to append(): calling a member
     *  method from a constructor body is not yet supported — the vtable pointer
     *  is initialised only after the body runs.) */
    StringBuilder(s : const String&) : _u8Used(0), _u8Cap(0), _u16Used(0), _u16Cap(0), _u32Used(0), _u32Cap(0), _fragCount(0), _fragCap(0), _totalSize(0) {
        _poolU8 = null;
        _poolU16 = null;
        _poolU32 = null;
        _fragEnc = null;
        _fragStart = null;
        _fragUnits = null;
        _fragCp = null;
        n : unsigned int = s.size();
        if (n > 0u) {
            fcap : unsigned int = 4u;
            ec : unsigned byte[]! = new unsigned byte[fcap];
            st : unsigned int[]! = new unsigned int[fcap];
            un : unsigned int[]! = new unsigned int[fcap];
            cp : unsigned int[]! = new unsigned int[fcap];
            st[0] = 0u;
            cp[0] = n;
            e : int = (int) s.rawEncoding();
            if (e == 0) {
                units : unsigned byte[]! = s.toUtf8();
                ulen : unsigned int = units.size - 1u;
                p : unsigned byte[]! = new unsigned byte[ulen];
                CharHelpers::copyBytes(p, 0u, units, 0u, ulen);
                _poolU8 = p;
                _u8Used = ulen;
                _u8Cap = ulen;
                ec[0] = (unsigned byte) 0;
                un[0] = ulen;
            } else if (e == 1) {
                units : unsigned short[]! = s.toUtf16();
                ulen : unsigned int = units.size - 1u;
                p : unsigned short[]! = new unsigned short[ulen];
                CharHelpers::copyShorts(p, 0u, units, 0u, ulen);
                _poolU16 = p;
                _u16Used = ulen;
                _u16Cap = ulen;
                ec[0] = (unsigned byte) 1;
                un[0] = ulen;
            } else {
                units : char[]! = s.toUtf32();
                p : char[]! = new char[n];
                CharHelpers::copy(p, 0u, units, 0u, n);
                _poolU32 = p;
                _u32Used = n;
                _u32Cap = n;
                ec[0] = (unsigned byte) 2;
                un[0] = n;
            }
            _fragEnc = ec;
            _fragStart = st;
            _fragUnits = un;
            _fragCp = cp;
            _fragCount = 1u;
            _fragCap = fcap;
            _totalSize = n;
        }
    }

    /** Construct from a const char[] (e.g. a string literal). Stored as UTF-32.
     *  src.size is expected to include the null terminator.
     *  (Logic inlined — see the note on StringBuilder(const String&).) */
    StringBuilder(src : const char[]) : _u8Used(0), _u8Cap(0), _u16Used(0), _u16Cap(0), _u32Used(0), _u32Cap(0), _fragCount(0), _fragCap(0), _totalSize(0) {
        _poolU8 = null;
        _poolU16 = null;
        _poolU32 = null;
        _fragEnc = null;
        _fragStart = null;
        _fragUnits = null;
        _fragCp = null;
        if (src.size > 1u) {
            n : unsigned int = src.size - 1u;
            p : char[]! = new char[n];
            CharHelpers::copy(p, 0u, src, 0u, n);
            _poolU32 = p;
            _u32Used = n;
            _u32Cap = n;
            fcap : unsigned int = 4u;
            ec : unsigned byte[]! = new unsigned byte[fcap];
            st : unsigned int[]! = new unsigned int[fcap];
            un : unsigned int[]! = new unsigned int[fcap];
            cp : unsigned int[]! = new unsigned int[fcap];
            ec[0] = (unsigned byte) 2;
            st[0] = 0u;
            un[0] = n;
            cp[0] = n;
            _fragEnc = ec;
            _fragStart = st;
            _fragUnits = un;
            _fragCp = cp;
            _fragCount = 1u;
            _fragCap = fcap;
            _totalSize = n;
        }
    }

    /** Construct from a UTF-8 `unsigned byte[]` (e.g. a `u8"…"` literal),
     *  stored natively as UTF-8. src.size includes the trailing null byte.
     *  (Logic inlined — see the note on StringBuilder(const String&).) */
    StringBuilder(src : const unsigned byte[]) : _u8Used(0), _u8Cap(0), _u16Used(0), _u16Cap(0), _u32Used(0), _u32Cap(0), _fragCount(0), _fragCap(0), _totalSize(0) {
        _poolU8 = null;
        _poolU16 = null;
        _poolU32 = null;
        _fragEnc = null;
        _fragStart = null;
        _fragUnits = null;
        _fragCp = null;
        if (src.size > 1u) {
            ulen : unsigned int = src.size - 1u;
            ncp : unsigned int = CharHelpers::utf8CodePointCount(src, ulen);
            if (ncp > 0u) {
                p : unsigned byte[]! = new unsigned byte[ulen];
                CharHelpers::copyBytes(p, 0u, src, 0u, ulen);
                _poolU8 = p;
                _u8Used = ulen;
                _u8Cap = ulen;
                fcap : unsigned int = 4u;
                ec : unsigned byte[]! = new unsigned byte[fcap];
                st : unsigned int[]! = new unsigned int[fcap];
                un : unsigned int[]! = new unsigned int[fcap];
                cp : unsigned int[]! = new unsigned int[fcap];
                ec[0] = (unsigned byte) 0;
                st[0] = 0u;
                un[0] = ulen;
                cp[0] = ncp;
                _fragEnc = ec;
                _fragStart = st;
                _fragUnits = un;
                _fragCp = cp;
                _fragCount = 1u;
                _fragCap = fcap;
                _totalSize = ncp;
            }
        }
    }

    /** Construct from a UTF-16 `unsigned short[]` (e.g. a `u"…"` literal),
     *  stored natively as UTF-16. src.size includes the trailing null code unit.
     *  (Logic inlined — see the note on StringBuilder(const String&).) */
    StringBuilder(src : const unsigned short[]) : _u8Used(0), _u8Cap(0), _u16Used(0), _u16Cap(0), _u32Used(0), _u32Cap(0), _fragCount(0), _fragCap(0), _totalSize(0) {
        _poolU8 = null;
        _poolU16 = null;
        _poolU32 = null;
        _fragEnc = null;
        _fragStart = null;
        _fragUnits = null;
        _fragCp = null;
        if (src.size > 1u) {
            ulen : unsigned int = src.size - 1u;
            ncp : unsigned int = CharHelpers::utf16CodePointCount(src, ulen);
            if (ncp > 0u) {
                p : unsigned short[]! = new unsigned short[ulen];
                CharHelpers::copyShorts(p, 0u, src, 0u, ulen);
                _poolU16 = p;
                _u16Used = ulen;
                _u16Cap = ulen;
                fcap : unsigned int = 4u;
                ec : unsigned byte[]! = new unsigned byte[fcap];
                st : unsigned int[]! = new unsigned int[fcap];
                un : unsigned int[]! = new unsigned int[fcap];
                cp : unsigned int[]! = new unsigned int[fcap];
                ec[0] = (unsigned byte) 1;
                st[0] = 0u;
                un[0] = ulen;
                cp[0] = ncp;
                _fragEnc = ec;
                _fragStart = st;
                _fragUnits = un;
                _fragCp = cp;
                _fragCount = 1u;
                _fragCap = fcap;
                _totalSize = ncp;
            }
        }
    }

    /** Copy constructor — deep-copies all pools and fragment metadata,
     *  preserving each fragment's native encoding.
     *  (Logic inlined — see the note on StringBuilder(const String&).) */
    StringBuilder(other : const StringBuilder&) : _u8Used(0), _u8Cap(0), _u16Used(0), _u16Cap(0), _u32Used(0), _u32Cap(0), _fragCount(0), _fragCap(0), _totalSize(0) {
        _poolU8 = null;
        _poolU16 = null;
        _poolU32 = null;
        _fragEnc = null;
        _fragStart = null;
        _fragUnits = null;
        _fragCp = null;
        if (other._totalSize > 0u) {
            if (other._u8Used > 0u) {
                len : unsigned int = other._u8Used;
                p : unsigned byte[]! = new unsigned byte[len];
                CharHelpers::copyBytes(p, 0u, other._poolU8, 0u, len);
                _poolU8 = p;
                _u8Used = len;
                _u8Cap = len;
            }
            if (other._u16Used > 0u) {
                len : unsigned int = other._u16Used;
                p : unsigned short[]! = new unsigned short[len];
                CharHelpers::copyShorts(p, 0u, other._poolU16, 0u, len);
                _poolU16 = p;
                _u16Used = len;
                _u16Cap = len;
            }
            if (other._u32Used > 0u) {
                len : unsigned int = other._u32Used;
                p : char[]! = new char[len];
                CharHelpers::copy(p, 0u, other._poolU32, 0u, len);
                _poolU32 = p;
                _u32Used = len;
                _u32Cap = len;
            }
            fc : unsigned int = other._fragCount;
            ec : unsigned byte[]! = new unsigned byte[fc];
            st : unsigned int[]! = new unsigned int[fc];
            un : unsigned int[]! = new unsigned int[fc];
            cp : unsigned int[]! = new unsigned int[fc];
            i : unsigned int = 0u;
            while (i < fc) {
                ec[i] = other._fragEnc[i];
                st[i] = other._fragStart[i];
                un[i] = other._fragUnits[i];
                cp[i] = other._fragCp[i];
                i = i + 1u;
            }
            _fragEnc = ec;
            _fragStart = st;
            _fragUnits = un;
            _fragCp = cp;
            _fragCount = fc;
            _fragCap = fc;
            _totalSize = other._totalSize;
        }
    }

    /** Drain constructor — steals all internal state, leaving the source empty. */
    StringBuilder(other : StringBuilder#) : _u8Used(other._u8Used), _u8Cap(other._u8Cap), _u16Used(other._u16Used), _u16Cap(other._u16Cap), _u32Used(other._u32Used), _u32Cap(other._u32Cap), _fragCount(other._fragCount), _fragCap(other._fragCap), _totalSize(other._totalSize) {
        _poolU8 = other._poolU8;
        _poolU16 = other._poolU16;
        _poolU32 = other._poolU32;
        _fragEnc = other._fragEnc;
        _fragStart = other._fragStart;
        _fragUnits = other._fragUnits;
        _fragCp = other._fragCp;
        other._poolU8 = null;     other._u8Used = 0u;   other._u8Cap = 0u;
        other._poolU16 = null;    other._u16Used = 0u;  other._u16Cap = 0u;
        other._poolU32 = null;    other._u32Used = 0u;  other._u32Cap = 0u;
        other._fragEnc = null;
        other._fragStart = null;
        other._fragUnits = null;
        other._fragCp = null;
        other._fragCount = 0u;
        other._fragCap = 0u;
        other._totalSize = 0u;
    }

    // ── Accessors ───────────────────────────────────────────────────────

    /** Total number of characters (code points) across all fragments. */
    const size() : unsigned int { return _totalSize; }

    /** True when the builder contains no characters. */
    const empty() : bool { return _totalSize == 0; }

    /** The native storage encoding of the first fragment, or UTF-32 if empty.
     *  Note: a builder may hold fragments of mixed encodings; this reports the
     *  encoding of the first one (and the encoding chosen by the FirstFragment
     *  consolidation policy). */
    const rawEncoding() : Encoding {
        if (_fragCount == 0u) return Encoding::UTF32;
        e : int = (int) _fragEnc[0];
        if (e == 0) return Encoding::UTF8;
        if (e == 1) return Encoding::UTF16;
        return Encoding::UTF32;
    }

    /** Return the character (code point) at the given logical position.
     *  Read-only. O(fragCount + intra-fragment offset). No bounds check. */
    const charAt(index : unsigned int) : char {
        pos : unsigned int = 0u;
        i : unsigned int = 0u;
        while (i < _fragCount) {
            if (index < pos + _fragCp[i]) {
                local : unsigned int = index - pos;
                e : int = (int) _fragEnc[i];
                if (e == 2) {
                    return _poolU32[_fragStart[i] + local];
                }
                if (e == 1) {
                    return CharHelpers::utf16CodePointAtFrom(_poolU16, _fragStart[i], local);
                }
                return CharHelpers::utf8CodePointAtFrom(_poolU8, _fragStart[i], local);
            }
            pos = pos + _fragCp[i];
            i = i + 1u;
        }
        return (char) 0;
    }

    /** Read the code point at `index` (read-only subscript). */
    const at(index : unsigned int) : char { return charAt(index); }

    /** Read the code point at `index` (read-only subscript). */
    const operator [](index : unsigned int) : char { return charAt(index); }

    /** Mutable element access — returns a write-proxy. `sb[i] = c` stores the
     *  code point via set(); `(char) sb[i]` reads it. The proxy is invalidated
     *  by any structural mutation (insert/remove/clear/…). */
    operator [](index : unsigned int) : CharRef {
        r : CharRef(this, index);
        return r;
    }

    /** Mutable element access (named form) — returns a write-proxy. */
    at(index : unsigned int) : CharRef {
        r : CharRef(this, index);
        return r;
    }

    /** Copy all fragment content into `dst` (UTF-32 code points) at `dstOff`. */
    const copyTo(dst : char[], dstOff : unsigned int) {
        off : unsigned int = dstOff;
        i : unsigned int = 0u;
        while (i < _fragCount) {
            e : int = (int) _fragEnc[i];
            if (e == 2) {
                CharHelpers::copy(dst, off, _poolU32, _fragStart[i], _fragCp[i]);
            } else if (e == 1) {
                CharHelpers::utf16DecodeRangeInto(_poolU16, _fragStart[i], _fragUnits[i], dst, off);
            } else {
                CharHelpers::utf8DecodeRangeInto(_poolU8, _fragStart[i], _fragUnits[i], dst, off);
            }
            off = off + _fragCp[i];
            i = i + 1u;
        }
    }

    // ── Conversion ──────────────────────────────────────────────────────

    /** Conversion operator — build an immutable String using the default
     *  (MostCompact) consolidation policy. */
    operator() : String {
        return toString(ConsolidationPolicy::MostCompact);
    }

    /** Build an immutable String using the default (MostCompact) policy. */
    const toString() : String {
        return toString(ConsolidationPolicy::MostCompact);
    }

    /** Build an immutable String, choosing the storage encoding per `policy`. */
    const toString(policy : ConsolidationPolicy) : String {
        if (_totalSize == 0u) {
            emptyStr : String;
            return emptyStr;
        }
        enc : Encoding = _resolvePolicy(policy);
        return _buildString(enc);
    }

    /** Build an immutable String stored in the given encoding `enc`. */
    const toString(enc : Encoding) : String {
        if (_totalSize == 0u) {
            emptyStr : String;
            return emptyStr;
        }
        return _buildString(enc);
    }

    // ── Encoding / array conversions ────────────────────────────────────

    /** Return the content as a new, null-terminated UTF-32 `char[]`. */
    const toUtf32() : char[]! {
        return _decodeAll();
    }

    /** Return the content as a new, null-terminated UTF-8 `unsigned byte[]`. */
    const toUtf8() : unsigned byte[]! {
        tmp : char[]! = _decodeAll();
        result : unsigned byte[]! = CharHelpers::toUtf8(tmp, _totalSize);
        return result;
    }

    /** Return the content as a new, null-terminated UTF-16 `unsigned short[]`. */
    const toUtf16() : unsigned short[]! {
        tmp : char[]! = _decodeAll();
        result : unsigned short[]! = CharHelpers::toUtf16(tmp, _totalSize);
        return result;
    }

    // ── Search methods ──────────────────────────────────────────────────

    /** Find the first occurrence of character `c`. Returns index or -1. */
    const find(c : char) : int {
        if (_totalSize == 0u) return -1;
        b : char[]! = _decodeAll();
        return CharHelpers::findChar(b, 0u, _totalSize, c);
    }

    /** Find the first occurrence of `needle`. Returns start index or -1. */
    const find(needle : const String&) : int {
        if (needle.size() == 0u) return 0;
        if (_totalSize == 0u) return -1;
        b : char[]! = _decodeAll();
        nb : char[]! = needle.toUtf32();
        return CharHelpers::find(b, 0u, _totalSize, nb, 0u, needle.size());
    }

    /** Find the last occurrence of character `c`. Returns index or -1. */
    const rfind(c : char) : int {
        if (_totalSize == 0u) return -1;
        b : char[]! = _decodeAll();
        return CharHelpers::rfindChar(b, 0u, _totalSize, c);
    }

    /** Find the last occurrence of `needle`. Returns start index or -1. */
    const rfind(needle : const String&) : int {
        if (needle.size() == 0u) {
            r : int = _totalSize;
            return r;
        }
        if (_totalSize == 0u) return -1;
        tmp : String(this);
        return tmp.rfind(needle);
    }

    /** Return true if the content contains character `c`. */
    const contains(c : char) : bool { return find(c) >= 0; }

    /** Return true if the content contains `needle`. */
    const contains(needle : const String&) : bool { return find(needle) >= 0; }

    /** Alias for find(needle). */
    const indexOf(needle : const String&) : int { return find(needle); }

    /** Alias for find(c). */
    const indexOf(c : char) : int { return find(c); }

    /** Count non-overlapping occurrences of character `c`. */
    const count(c : char) : unsigned int {
        if (_totalSize == 0u) return 0u;
        b : char[]! = _decodeAll();
        n : unsigned int = 0u;
        i : unsigned int = 0u;
        while (i < _totalSize) {
            if (b[i] == c) n = n + 1u;
            i = i + 1u;
        }
        return n;
    }

    /** Count non-overlapping occurrences of `needle`. */
    const count(needle : const String&) : unsigned int {
        if (needle.size() == 0u) return 0u;
        tmp : String(this);
        return tmp.count(needle);
    }

    /** Return true if the content begins with `prefix`. */
    const beginsWith(prefix : const String&) : bool {
        if (prefix.size() > _totalSize) return false;
        if (prefix.size() == 0u) return true;
        tmp : String(this);
        return tmp.beginsWith(prefix);
    }

    /** Return true if the content ends with `suffix`. */
    const endsWith(suffix : const String&) : bool {
        if (suffix.size() > _totalSize) return false;
        if (suffix.size() == 0u) return true;
        tmp : String(this);
        return tmp.endsWith(suffix);
    }

    /** Extract a substring as a new String (UTF-32 storage). */
    const substr(start : unsigned int, len : unsigned int) : String {
        if (start >= _totalSize) {
            empty : String;
            return empty;
        }
        actual : unsigned int = len;
        if (start + actual > _totalSize) {
            actual = _totalSize - start;
        }
        if (actual == 0u) {
            empty : String;
            return empty;
        }
        buf : char[]! = new char[actual + 1u];
        i : unsigned int = 0u;
        while (i < actual) {
            buf[i] = charAt(start + i);
            i = i + 1u;
        }
        buf[actual] = (char) 0;
        result : String(buf, actual);
        return result;
    }

    /** Return a new String containing the first `count` characters. */
    const first(count : unsigned int) : String { return substr(0u, count); }

    /** Return a new String containing the last `count` characters. */
    const last(count : unsigned int) : String {
        if (count >= _totalSize) {
            copy : String(this);
            return copy;
        }
        return substr(_totalSize - count, count);
    }

    // ── Mutating methods (return StringBuilder& for fluent chaining) ────

    /** Consolidate all fragments into a single UTF-32 fragment. */
    consolidate() : StringBuilder& {
        _consolidate();
        return this;
    }

    /** Append a String, mirroring its native storage encoding (no transcode). */
    append(s : const String&) : StringBuilder& {
        n : unsigned int = s.size();
        if (n == 0u) return this;
        e : int = (int) s.rawEncoding();
        if (e == 0) {
            units : unsigned byte[]! = s.toUtf8();
            ulen : unsigned int = units.size - 1u;
            start : unsigned int = _u8Used;
            _ensureU8(ulen);
            CharHelpers::copyBytes(_poolU8, start, units, 0u, ulen);
            _u8Used = _u8Used + ulen;
            _pushFrag(0u, start, ulen, n);
        } else if (e == 1) {
            units : unsigned short[]! = s.toUtf16();
            ulen : unsigned int = units.size - 1u;
            start : unsigned int = _u16Used;
            _ensureU16(ulen);
            CharHelpers::copyShorts(_poolU16, start, units, 0u, ulen);
            _u16Used = _u16Used + ulen;
            _pushFrag(1u, start, ulen, n);
        } else {
            units : char[]! = s.toUtf32();
            start : unsigned int = _u32Used;
            _ensureU32(n);
            CharHelpers::copy(_poolU32, start, units, 0u, n);
            _u32Used = _u32Used + n;
            _pushFrag(2u, start, n, n);
        }
        return this;
    }

    /** Append a const char[] (e.g. a string literal) as a UTF-32 fragment.
     *  src.size is expected to include the null terminator. */
    append(src : const char[]) : StringBuilder& {
        if (src.size <= 1u) return this;
        n : unsigned int = src.size - 1u;
        start : unsigned int = _u32Used;
        _ensureU32(n);
        CharHelpers::copy(_poolU32, start, src, 0u, n);
        _u32Used = _u32Used + n;
        _pushFrag(2u, start, n, n);
        return this;
    }

    /** Append a UTF-8 fragment (e.g. a `u8"…"` literal), stored natively.
     *  src.size includes the trailing null byte. */
    append(src : const unsigned byte[]) : StringBuilder& {
        if (src.size <= 1u) return this;
        ulen : unsigned int = src.size - 1u;
        cp : unsigned int = CharHelpers::utf8CodePointCount(src, ulen);
        if (cp == 0u) return this;
        start : unsigned int = _u8Used;
        _ensureU8(ulen);
        CharHelpers::copyBytes(_poolU8, start, src, 0u, ulen);
        _u8Used = _u8Used + ulen;
        _pushFrag(0u, start, ulen, cp);
        return this;
    }

    /** Append a UTF-16 fragment (e.g. a `u"…"` literal), stored natively.
     *  src.size includes the trailing null code unit. */
    append(src : const unsigned short[]) : StringBuilder& {
        if (src.size <= 1u) return this;
        ulen : unsigned int = src.size - 1u;
        cp : unsigned int = CharHelpers::utf16CodePointCount(src, ulen);
        if (cp == 0u) return this;
        start : unsigned int = _u16Used;
        _ensureU16(ulen);
        CharHelpers::copyShorts(_poolU16, start, src, 0u, ulen);
        _u16Used = _u16Used + ulen;
        _pushFrag(1u, start, ulen, cp);
        return this;
    }

    /** Append another StringBuilder's content, mirroring each of its fragments. */
    append(other : const StringBuilder&) : StringBuilder& {
        if (other._totalSize == 0u) return this;
        i : unsigned int = 0u;
        while (i < other._fragCount) {
            e : int = (int) other._fragEnc[i];
            ostart : unsigned int = other._fragStart[i];
            ounits : unsigned int = other._fragUnits[i];
            ocp : unsigned int = other._fragCp[i];
            if (e == 0) {
                start : unsigned int = _u8Used;
                _ensureU8(ounits);
                CharHelpers::copyBytes(_poolU8, start, other._poolU8, ostart, ounits);
                _u8Used = _u8Used + ounits;
                _pushFrag(0u, start, ounits, ocp);
            } else if (e == 1) {
                start : unsigned int = _u16Used;
                _ensureU16(ounits);
                CharHelpers::copyShorts(_poolU16, start, other._poolU16, ostart, ounits);
                _u16Used = _u16Used + ounits;
                _pushFrag(1u, start, ounits, ocp);
            } else {
                start : unsigned int = _u32Used;
                _ensureU32(ounits);
                CharHelpers::copy(_poolU32, start, other._poolU32, ostart, ounits);
                _u32Used = _u32Used + ounits;
                _pushFrag(2u, start, ounits, ocp);
            }
            i = i + 1u;
        }
        return this;
    }

    /** Append a single character (UTF-32 fragment). */
    appendChar(c : char) : StringBuilder& {
        start : unsigned int = _u32Used;
        _ensureU32(1u);
        _poolU32[start] = c;
        _u32Used = _u32Used + 1u;
        _pushFrag(2u, start, 1u, 1u);
        return this;
    }

    /** Set the code point at logical position `index`. No-op if out of range.
     *  Consolidates to UTF-32 first, then writes in place. */
    set(index : unsigned int, c : char) : StringBuilder& {
        if (index >= _totalSize) return this;
        _consolidate();
        _poolU32[index] = c;
        return this;
    }

    /** Prepend a String at the beginning. */
    prepend(s : const String&) : StringBuilder& {
        if (s.size() == 0u) return this;
        return insert(0u, s);
    }

    /** Prepend a const char[] at the beginning. */
    prepend(src : const char[]) : StringBuilder& {
        if (src.size > 1u) {
            tmp : String(src);
            return insert(0u, tmp);
        }
        return this;
    }

    /** Insert a String at the given logical position. */
    insert(pos : unsigned int, s : const String&) : StringBuilder& {
        if (s.size() == 0u) return this;
        if (pos >= _totalSize) return append(s);
        _consolidate();
        sLen : unsigned int = s.size();
        newTotal : unsigned int = _totalSize + sLen;
        newPool : char[]! = new char[newTotal + 1u];
        CharHelpers::copy(newPool, 0u, _poolU32, 0u, pos);
        sbuf : char[]! = s.toUtf32();
        CharHelpers::copy(newPool, pos, sbuf, 0u, sLen);
        tailLen : unsigned int = _totalSize - pos;
        if (tailLen > 0u) {
            CharHelpers::copy(newPool, pos + sLen, _poolU32, pos, tailLen);
        }
        newPool[newTotal] = (char) 0;
        delete _poolU32;
        _poolU32 = newPool;
        _u32Cap = newTotal + 1u;
        _setSingleU32Frag(newTotal);
        return this;
    }

    /** Insert a const char[] at the given logical position. */
    insert(pos : unsigned int, src : const char[]) : StringBuilder& {
        tmp : String(src);
        return insert(pos, tmp);
    }

    /** Remove `count` characters starting at position `start`. */
    remove(start : unsigned int, count : unsigned int) : StringBuilder& {
        if (count == 0u) return this;
        if (start >= _totalSize) return this;
        _consolidate();
        actual : unsigned int = count;
        if (start + actual > _totalSize) {
            actual = _totalSize - start;
        }
        tailStart : unsigned int = start + actual;
        if (tailStart < _totalSize) {
            CharHelpers::copy(_poolU32, start, _poolU32, tailStart, _totalSize - tailStart);
        }
        _setSingleU32Frag(_totalSize - actual);
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

    /** Replace all non-overlapping occurrences of `needle` with `replacement`. */
    replaceAll(needle : const String&, replacement : const String&) : StringBuilder& {
        if (needle.size() == 0u) return this;
        if (_totalSize == 0u) return this;
        _consolidate();
        nbuf : char[]! = needle.toUtf32();
        pos : unsigned int = 0u;
        while (pos + needle.size() <= _totalSize) {
            found : int = CharHelpers::find(_poolU32, pos, _totalSize - pos, nbuf, 0u, needle.size());
            if (found < 0) return this;
            ufound : unsigned int = found;
            remove(ufound, needle.size());
            insert(ufound, replacement);
            pos = ufound + replacement.size();
        }
        return this;
    }

    /** Reverse the entire content in place. */
    reverse() : StringBuilder& {
        if (_totalSize <= 1u) return this;
        _consolidate();
        left : unsigned int = 0u;
        right : unsigned int = _totalSize - 1u;
        while (left < right) {
            tmp : char = _poolU32[left];
            _poolU32[left] = _poolU32[right];
            _poolU32[right] = tmp;
            left = left + 1u;
            right = right - 1u;
        }
        return this;
    }

    /** Remove leading whitespace characters (space, tab, newline, CR). */
    trimLeft() : StringBuilder& {
        if (_totalSize == 0u) return this;
        _consolidate();
        start : unsigned int = 0u;
        while (start < _totalSize) {
            if (!CharHelpers::isWhitespace(_poolU32[start])) {
                if (start > 0u) {
                    return remove(0u, start);
                }
                return this;
            }
            start = start + 1u;
        }
        return clear();
    }

    /** Remove trailing whitespace characters (space, tab, newline, CR). */
    trimRight() : StringBuilder& {
        if (_totalSize == 0u) return this;
        _consolidate();
        end : unsigned int = _totalSize;
        while (end > 0u) {
            if (!CharHelpers::isWhitespace(_poolU32[end - 1u])) {
                if (end < _totalSize) {
                    _setSingleU32Frag(end);
                }
                return this;
            }
            end = end - 1u;
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
        _fragCount = 0u;
        _totalSize = 0u;
        _u8Used = 0u;
        _u16Used = 0u;
        _u32Used = 0u;
        return this;
    }

    // ── Operators ───────────────────────────────────────────────────────

    /** Equality: same total length and identical content. */
    const operator ==(other : const StringBuilder&) : bool {
        if (_totalSize != other._totalSize) return false;
        if (_totalSize == 0u) return true;
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

    /** Ensure the UTF-8 pool has room for `extra` more bytes. */
    _ensureU8(extra : unsigned int) {
        needed : unsigned int = _u8Used + extra;
        if (needed <= _u8Cap) return;
        newCap : unsigned int = _u8Cap * 2u;
        if (newCap < 32u) newCap = 32u;
        if (newCap < needed) newCap = needed;
        np : unsigned byte[]! = new unsigned byte[newCap];
        if (_poolU8 != null) {
            CharHelpers::copyBytes(np, 0u, _poolU8, 0u, _u8Used);
            delete _poolU8;
        }
        _poolU8 = np;
        _u8Cap = newCap;
    }

    /** Ensure the UTF-16 pool has room for `extra` more code units. */
    _ensureU16(extra : unsigned int) {
        needed : unsigned int = _u16Used + extra;
        if (needed <= _u16Cap) return;
        newCap : unsigned int = _u16Cap * 2u;
        if (newCap < 32u) newCap = 32u;
        if (newCap < needed) newCap = needed;
        np : unsigned short[]! = new unsigned short[newCap];
        if (_poolU16 != null) {
            CharHelpers::copyShorts(np, 0u, _poolU16, 0u, _u16Used);
            delete _poolU16;
        }
        _poolU16 = np;
        _u16Cap = newCap;
    }

    /** Ensure the UTF-32 pool has room for `extra` more code points. */
    _ensureU32(extra : unsigned int) {
        needed : unsigned int = _u32Used + extra;
        if (needed <= _u32Cap) return;
        newCap : unsigned int = _u32Cap * 2u;
        if (newCap < 32u) newCap = 32u;
        if (newCap < needed) newCap = needed;
        np : char[]! = new char[newCap];
        if (_poolU32 != null) {
            CharHelpers::copy(np, 0u, _poolU32, 0u, _u32Used);
            delete _poolU32;
        }
        _poolU32 = np;
        _u32Cap = newCap;
    }

    /** Ensure the fragment metadata arrays have room for `extra` more entries. */
    _ensureFragCap(extra : unsigned int) {
        needed : unsigned int = _fragCount + extra;
        if (needed <= _fragCap) return;
        newCap : unsigned int = _fragCap * 2u;
        if (newCap < 4u) newCap = 4u;
        if (newCap < needed) newCap = needed;
        newEnc : unsigned byte[]! = new unsigned byte[newCap];
        newStart : unsigned int[]! = new unsigned int[newCap];
        newUnits : unsigned int[]! = new unsigned int[newCap];
        newCp : unsigned int[]! = new unsigned int[newCap];
        if (_fragEnc != null) {
            i : unsigned int = 0u;
            while (i < _fragCount) {
                newEnc[i] = _fragEnc[i];
                newStart[i] = _fragStart[i];
                newUnits[i] = _fragUnits[i];
                newCp[i] = _fragCp[i];
                i = i + 1u;
            }
            delete _fragEnc;
            delete _fragStart;
            delete _fragUnits;
            delete _fragCp;
        }
        _fragEnc = newEnc;
        _fragStart = newStart;
        _fragUnits = newUnits;
        _fragCp = newCp;
        _fragCap = newCap;
    }

    /** Append one fragment descriptor (data already copied into its pool). */
    _pushFrag(enc : unsigned byte, start : unsigned int, units : unsigned int, cp : unsigned int) {
        if (cp == 0u) return;
        _ensureFragCap(1u);
        _fragEnc[_fragCount] = enc;
        _fragStart[_fragCount] = start;
        _fragUnits[_fragCount] = units;
        _fragCp[_fragCount] = cp;
        _fragCount = _fragCount + 1u;
        _totalSize = _totalSize + cp;
    }

    /** Set the builder to a single UTF-32 fragment of `len` code points,
     *  already present at the start of _poolU32. */
    _setSingleU32Frag(len : unsigned int) {
        _u8Used = 0u;
        _u16Used = 0u;
        _u32Used = len;
        if (len == 0u) {
            _fragCount = 0u;
            _totalSize = 0u;
            return;
        }
        _ensureFragCap(1u);
        _fragEnc[0] = (unsigned byte) 2;
        _fragStart[0] = 0u;
        _fragUnits[0] = len;
        _fragCp[0] = len;
        _fragCount = 1u;
        _totalSize = len;
    }

    /** Decode all fragments into a new, null-terminated UTF-32 `char[]`. */
    const _decodeAll() : char[]! {
        out : char[]! = new char[_totalSize + 1u];
        copyTo(out, 0u);
        out[_totalSize] = (char) 0;
        return out;
    }

    /** Consolidate all fragments into a single UTF-32 fragment in _poolU32. */
    _consolidate() {
        if (_totalSize == 0u) {
            _fragCount = 0u;
            _u8Used = 0u;
            _u16Used = 0u;
            _u32Used = 0u;
            return;
        }
        // Already a single contiguous UTF-32 fragment: nothing to do.
        if (_fragCount == 1u && (int) _fragEnc[0] == 2 && _fragStart[0] == 0u) {
            return;
        }
        newCap : unsigned int = _totalSize + 1u;
        newPool : char[]! = new char[newCap];
        copyTo(newPool, 0u);
        newPool[_totalSize] = (char) 0;
        if (_poolU32 != null) {
            delete _poolU32;
        }
        _poolU32 = newPool;
        _u32Cap = newCap;
        len : unsigned int = _totalSize;
        _setSingleU32Frag(len);
    }

    /** Resolve a consolidation policy to a concrete storage encoding. */
    const _resolvePolicy(policy : ConsolidationPolicy) : Encoding {
        if (policy == ConsolidationPolicy::ForceUtf8)  return Encoding::UTF8;
        if (policy == ConsolidationPolicy::ForceUtf16) return Encoding::UTF16;
        if (policy == ConsolidationPolicy::ForceUtf32) return Encoding::UTF32;
        if (policy == ConsolidationPolicy::FirstFragment) {
            return rawEncoding();
        }
        // MostCompact: choose the encoding with the smallest byte footprint.
        dec : char[]! = _decodeAll();
        u8bytes  : unsigned int = CharHelpers::utf8Size(dec, _totalSize);
        u16bytes : unsigned int = CharHelpers::utf16Size(dec, _totalSize) * 2u;
        u32bytes : unsigned int = _totalSize * 4u;
        if (u8bytes <= u16bytes && u8bytes <= u32bytes) return Encoding::UTF8;
        if (u16bytes <= u32bytes) return Encoding::UTF16;
        return Encoding::UTF32;
    }

    /** Build an immutable String holding the content in encoding `enc`. */
    const _buildString(enc : Encoding) : String {
        dec : char[]! = _decodeAll();
        result : String(dec, enc);
        return result;
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// CharRef — Write-proxy returned by StringBuilder's non-const subscript/at().
//
//   sb[i] = c;          // writes code point c via StringBuilder.set(i, c)
//   x : char = (char) sb[i];   // explicit read of the current code point
//
// CharRef holds a (non-owning) link back to its StringBuilder plus a logical
// code-point index. It is invalidated by any structural mutation of the builder.
// ═════════════════════════════════════════════════════════════════════════════
class CharRef {
    _sb  : StringBuilder+;
    _idx : unsigned int;

public:
    CharRef(sb : StringBuilder+, idx : unsigned int) : _sb(sb), _idx(idx) {}

    /** Read the current code point (explicit char cast). */
    operator () : char {
        return _sb->charAt(_idx);
    }

    /** Write a code point at the referenced position. */
    operator =(c : char) : void {
        _sb->set(_idx, c);
    }
}
