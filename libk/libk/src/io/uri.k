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
 * Compact internal structure recording component ranges in an RFC 3986 URI.
 */
public class UriRanges {
public:
    schemeStart   : int = -1;
    schemeLen     : unsigned int = 0u;

    authStart     : int = -1;
    authLen       : unsigned int = 0u;

    userInfoStart : int = -1;
    userInfoLen   : unsigned int = 0u;

    hostStart     : int = -1;
    hostLen       : unsigned int = 0u;

    port          : int = -1;

    pathStart     : int = -1;
    pathLen       : unsigned int = 0u;

    queryStart    : int = -1;
    queryLen      : unsigned int = 0u;

    fragmentStart : int = -1;
    fragmentLen   : unsigned int = 0u;

    sspStart      : int = -1;
    sspLen        : unsigned int = 0u;

    isOpaque      : bool = false;

    UriRanges() {}

    UriRanges(other: const UriRanges&)
        : schemeStart(other.schemeStart), schemeLen(other.schemeLen),
          authStart(other.authStart), authLen(other.authLen),
          userInfoStart(other.userInfoStart), userInfoLen(other.userInfoLen),
          hostStart(other.hostStart), hostLen(other.hostLen),
          port(other.port),
          pathStart(other.pathStart), pathLen(other.pathLen),
          queryStart(other.queryStart), queryLen(other.queryLen),
          fragmentStart(other.fragmentStart), fragmentLen(other.fragmentLen),
          sspStart(other.sspStart), sspLen(other.sspLen),
          isOpaque(other.isOpaque) {}
}


/**
 * Uri — an immutable Uniform Resource Identifier (RFC 3986 / RFC 2396).
 *
 * Represents both absolute URIs and relative URI references.
 * Backed by a compact unsigned byte array with precalculated component ranges.
 * Accessors recompute slice representations dynamically on demand.
 */
public class Uri {
private:
    _raw    : unsigned byte[]!;
    _len    : unsigned int = 0u;
    _ranges : UriRanges;

    static isAlpha(b: unsigned byte) : bool {
        return (b >= (unsigned byte) 'A' && b <= (unsigned byte) 'Z') ||
               (b >= (unsigned byte) 'a' && b <= (unsigned byte) 'z');
    }

    static isDigit(b: unsigned byte) : bool {
        return b >= (unsigned byte) '0' && b <= (unsigned byte) '9';
    }

    static isSchemeChar(b: unsigned byte) : bool {
        return isAlpha(b) || isDigit(b) ||
               b == (unsigned byte) '+' || b == (unsigned byte) '-' || b == (unsigned byte) '.';
    }

    static isUnreserved(b: unsigned byte) : bool {
        return isAlpha(b) || isDigit(b) ||
               b == (unsigned byte) '-' || b == (unsigned byte) '_' ||
               b == (unsigned byte) '.' || b == (unsigned byte) '~';
    }

    static isSubDelim(b: unsigned byte) : bool {
        return b == (unsigned byte) '!' || b == (unsigned byte) '$' ||
               b == (unsigned byte) '&' || b == (unsigned byte) '\'' ||
               b == (unsigned byte) '(' || b == (unsigned byte) ')' ||
               b == (unsigned byte) '*' || b == (unsigned byte) '+' ||
               b == (unsigned byte) ',' || b == (unsigned byte) ';' ||
               b == (unsigned byte) '=';
    }

    static isPathChar(b: unsigned byte) : bool {
        return isUnreserved(b) || isSubDelim(b) ||
               b == (unsigned byte) ':' || b == (unsigned byte) '@' || b == (unsigned byte) '/';
    }

    static isQueryChar(b: unsigned byte) : bool {
        return isPathChar(b) || b == (unsigned byte) '?' ||
               b == (unsigned byte) '[' || b == (unsigned byte) ']';
    }

    static parseRanges(raw: const unsigned byte[], len: unsigned int, ranges: UriRanges&) {
        ranges.schemeStart = -1;
        ranges.schemeLen = 0u;
        ranges.authStart = -1;
        ranges.authLen = 0u;
        ranges.userInfoStart = -1;
        ranges.userInfoLen = 0u;
        ranges.hostStart = -1;
        ranges.hostLen = 0u;
        ranges.port = -1;
        ranges.pathStart = -1;
        ranges.pathLen = 0u;
        ranges.queryStart = -1;
        ranges.queryLen = 0u;
        ranges.fragmentStart = -1;
        ranges.fragmentLen = 0u;
        ranges.sspStart = -1;
        ranges.sspLen = 0u;
        ranges.isOpaque = false;

        if (len == 0u) {
            ranges.pathStart = 0;
            ranges.pathLen = 0u;
            ranges.sspStart = 0;
            ranges.sspLen = 0u;
            return;
        }

        // 1. Fragment (#)
        fragIdx : int = -1;
        i : unsigned int = 0u;
        while (i < len) {
            if (raw[i] == (unsigned byte) '#') {
                fragIdx = (int) i;
                break;
            }
            ++i;
        }

        endNonFrag : unsigned int = len;
        if (fragIdx >= 0) {
            ranges.fragmentStart = fragIdx + 1;
            ranges.fragmentLen = len - (unsigned int) (fragIdx + 1);
            endNonFrag = (unsigned int) fragIdx;
        }

        // 2. Scheme (:)
        colonIdx : int = -1;
        j : unsigned int = 0u;
        while (j < endNonFrag) {
            b : unsigned byte = raw[j];
            if (b == (unsigned byte) ':') {
                colonIdx = (int) j;
                break;
            }
            if (b == (unsigned byte) '/' || b == (unsigned byte) '?') {
                break;
            }
            ++j;
        }

        sspStart : unsigned int = 0u;
        if (colonIdx > 0 && isAlpha(raw[0])) {
            validScheme : bool = true;
            k : unsigned int = 1u;
            while (k < (unsigned int) colonIdx) {
                if (!isSchemeChar(raw[k])) {
                    validScheme = false;
                    break;
                }
                ++k;
            }
            if (validScheme) {
                ranges.schemeStart = 0;
                ranges.schemeLen = (unsigned int) colonIdx;
                sspStart = ((unsigned int) colonIdx) + 1u;
            }
        }

        sspLen : unsigned int = endNonFrag - sspStart;
        ranges.sspStart = (int) sspStart;
        ranges.sspLen = sspLen;

        // 3. Query (?)
        qIdx : int = -1;
        m : unsigned int = sspStart;
        while (m < endNonFrag) {
            if (raw[m] == (unsigned byte) '?') {
                qIdx = (int) m;
                break;
            }
            ++m;
        }

        endHier : unsigned int = endNonFrag;
        if (qIdx >= 0) {
            ranges.queryStart = qIdx + 1;
            ranges.queryLen = endNonFrag - (unsigned int) (qIdx + 1);
            endHier = (unsigned int) qIdx;
        }

        // 4. Hierarchical vs Opaque
        if (ranges.schemeStart >= 0 && sspStart < endHier && raw[sspStart] != (unsigned byte) '/') {
            ranges.isOpaque = true;
            return;
        }

        // 5. Authority & Path
        if (sspStart + 1u < endHier && raw[sspStart] == (unsigned byte) '/' && raw[sspStart + 1u] == (unsigned byte) '/') {
            authStart : unsigned int = sspStart + 2u;
            authEnd : unsigned int = endHier;
            p : unsigned int = authStart;
            while (p < endHier) {
                if (raw[p] == (unsigned byte) '/') {
                    authEnd = p;
                    break;
                }
                ++p;
            }

            ranges.authStart = (int) authStart;
            ranges.authLen = authEnd - authStart;

            // Inside Authority: [userinfo@]host[:port]
            atIdx : int = -1;
            q : unsigned int = authStart;
            while (q < authEnd) {
                if (raw[q] == (unsigned byte) '@') {
                    atIdx = (int) q;
                    break;
                }
                ++q;
            }

            hostPortStart : unsigned int = authStart;
            if (atIdx >= 0) {
                ranges.userInfoStart = (int) authStart;
                ranges.userInfoLen = (unsigned int) atIdx - authStart;
                hostPortStart = ((unsigned int) atIdx) + 1u;
            }

            if (hostPortStart < authEnd) {
                if (raw[hostPortStart] == (unsigned byte) '[') {
                    // IPv6 literal
                    closeBracket : int = -1;
                    bIdx : unsigned int = hostPortStart + 1u;
                    while (bIdx < authEnd) {
                        if (raw[bIdx] == (unsigned byte) ']') {
                            closeBracket = (int) bIdx;
                            break;
                        }
                        ++bIdx;
                    }
                    if (closeBracket >= 0) {
                        ranges.hostStart = (int) hostPortStart;
                        ranges.hostLen = ((unsigned int) closeBracket) + 1u - hostPortStart;
                        afterBracket : unsigned int = ((unsigned int) closeBracket) + 1u;
                        if (afterBracket < authEnd && raw[afterBracket] == (unsigned byte) ':') {
                            ranges.port = parsePort(raw, afterBracket + 1u, authEnd);
                        }
                    } else {
                        ranges.hostStart = (int) hostPortStart;
                        ranges.hostLen = authEnd - hostPortStart;
                    }
                } else {
                    portColon : int = -1;
                    h : unsigned int = hostPortStart;
                    while (h < authEnd) {
                        if (raw[h] == (unsigned byte) ':') {
                            portColon = (int) h;
                            break;
                        }
                        ++h;
                    }
                    if (portColon >= 0) {
                        ranges.hostStart = (int) hostPortStart;
                        ranges.hostLen = ((unsigned int) portColon) - hostPortStart;
                        ranges.port = parsePort(raw, ((unsigned int) portColon) + 1u, authEnd);
                    } else {
                        ranges.hostStart = (int) hostPortStart;
                        ranges.hostLen = authEnd - hostPortStart;
                    }
                }
            }

            ranges.pathStart = (int) authEnd;
            ranges.pathLen = endHier - authEnd;
        } else {
            ranges.pathStart = (int) sspStart;
            ranges.pathLen = endHier - sspStart;
        }
    }

    static parsePort(raw: const unsigned byte[], start: unsigned int, end: unsigned int) : int {
        if (start >= end) return -1;
        val : int = 0;
        i : unsigned int = start;
        while (i < end) {
            b : unsigned byte = raw[i];
            if (!isDigit(b)) {
                return -1;
            }
            val = val * 10 + ((int) (b - (unsigned byte) '0'));
            if (val > 65535) return -1;
            ++i;
        }
        return val;
    }

    initFromBytes(raw: const unsigned byte[]) {
        n : unsigned int = raw.size;
        actualLen : unsigned int = 0u;
        if (n > 1u) {
            actualLen = n - 1u;
        }
        _len = actualLen;
        _raw = new unsigned byte[_len + 1u];
        if (_len > 0u) {
            CharHelpers::copyBytes(_raw, 0u, raw, 0u, _len);
        }
        _raw[_len] = (unsigned byte) 0;
        parseRanges(_raw, _len, _ranges);
    }

    const _sliceToString(start: int, len: unsigned int) : String {
        if (start < 0 || len == 0u) {
            return String();
        }
        temp : unsigned byte[]! = new unsigned byte[len + 1u];
        CharHelpers::copyBytes(temp, 0u, _raw, (unsigned int) start, len);
        temp[len] = (unsigned byte) 0;
        s : String(temp);
        delete temp;
        return s;
    }

    const _decodeSlice(start: int, len: unsigned int, formUrl: bool) : String {
        if (start < 0 || len == 0u) {
            return String();
        }
        temp : unsigned byte[]! = new unsigned byte[len + 1u];
        outLen : unsigned int = 0u;
        i : unsigned int = 0u;
        while (i < len) {
            b : unsigned byte = _raw[((unsigned int) start) + i];
            if (formUrl && b == (unsigned byte) '+') {
                temp[outLen] = (unsigned byte) ' ';
                ++outLen;
                ++i;
            } else if (b == (unsigned byte) '%' && i + 2u < len) {
                h1 : int = HexDecoder::decodeNibble((byte) _raw[((unsigned int) start) + i + 1u]);
                h2 : int = HexDecoder::decodeNibble((byte) _raw[((unsigned int) start) + i + 2u]);
                if (h1 >= 0 && h2 >= 0) {
                    val : unsigned int = (((unsigned int) h1) << 4) | ((unsigned int) h2);
                    temp[outLen] = (unsigned byte) (val & 0xFFu);
                    ++outLen;
                    i += 3u;
                } else {
                    temp[outLen] = (unsigned byte) '%';
                    ++outLen;
                    ++i;
                }
            } else {
                temp[outLen] = b;
                ++outLen;
                ++i;
            }
        }
        res : unsigned byte[]! = new unsigned byte[outLen + 1u];
        CharHelpers::copyBytes(res, 0u, temp, 0u, outLen);
        res[outLen] = (unsigned byte) 0;
        delete temp;
        s : String(res);
        delete res;
        return s;
    }

    static removeDotSegments(path: const String&) : String {
        if (path.size() == 0u) {
            return String();
        }
        bytes : unsigned byte[]! = path.toUtf8();
        n : unsigned int = bytes.size;
        if (n <= 1u) {
            delete bytes;
            return String();
        }
        len : unsigned int = n - 1u;
        segs : Vector<String>;
        isAbs : bool = bytes[0] == (unsigned byte) '/';

        pos : unsigned int = 0u;
        while (pos < len) {
            nextSlash : int = -1;
            i : unsigned int = pos;
            while (i < len) {
                if (bytes[i] == (unsigned byte) '/') {
                    nextSlash = (int) i;
                    break;
                }
                ++i;
            }
            endSeg : unsigned int = nextSlash >= 0 ? (unsigned int) nextSlash : len;
            if (endSeg > pos) {
                segLen : unsigned int = endSeg - pos;
                if (segLen == 1u && bytes[pos] == (unsigned byte) '.') {
                    // Do nothing
                } else if (segLen == 2u && bytes[pos] == (unsigned byte) '.' && bytes[pos + 1u] == (unsigned byte) '.') {
                    if (segs.size() > 0u) {
                        segs.removeLast();
                    }
                } else {
                    tmp : unsigned byte[]! = new unsigned byte[segLen + 1u];
                    CharHelpers::copyBytes(tmp, 0u, bytes, pos, segLen);
                    tmp[segLen] = (unsigned byte) 0;
                    segStr : String(tmp);
                    delete tmp;
                    segs.append(segStr);
                }
            }
            if (nextSlash >= 0) {
                pos = ((unsigned int) nextSlash) + 1u;
            } else {
                break;
            }
        }
        delete bytes;

        sb : StringBuilder;
        if (isAbs) {
            sb.append("/");
        }
        sIdx : unsigned int = 0u;
        sNum : unsigned int = segs.size();
        while (sIdx < sNum) {
            if (sIdx > 0u) {
                sb.append("/");
            }
            sb.append(segs[sIdx]);
            ++sIdx;
        }
        return sb.toString();
    }

public:
    /** Construct an empty URI. */
    Uri() : _raw(null), _len(0u) {
        _raw = new unsigned byte[1];
        _raw[0] = (unsigned byte) 0;
        parseRanges(_raw, 0u, _ranges);
    }

    /** Construct by parsing a raw URI string. */
    Uri(raw: const String&) : _raw(null), _len(0u) {
        bytes : unsigned byte[]! = raw.toUtf8();
        initFromBytes(bytes);
        delete bytes;
    }

    /** Construct by parsing a raw URI character array. */
    Uri(raw: const char[]) : _raw(null), _len(0u) {
        s : String(raw);
        bytes : unsigned byte[]! = s.toUtf8();
        initFromBytes(bytes);
        delete bytes;
    }

    /** Construct by parsing a raw URI byte array. */
    Uri(raw: const unsigned byte[]) : _raw(null), _len(0u) {
        initFromBytes(raw);
    }

    /** Copy constructor. */
    Uri(other: const Uri&) : _len(other._len), _ranges(other._ranges), _raw(null) {
        _raw = new unsigned byte[_len + 1u];
        if (_len > 0u) {
            CharHelpers::copyBytes(_raw, 0u, other._raw, 0u, _len);
        }
        _raw[_len] = (unsigned byte) 0;
    }

    /** Drain constructor. */
    Uri(other: Uri#) : _len(other._len), _ranges(other._ranges), _raw(null) {
        _raw = other._raw;
        other._raw = null;
        other._len = 0u;
    }

    ~Uri() {
        if (_raw != null) {
            delete _raw;
            _raw = null;
        }
    }

    // ── Static Factories ─────────────────────────────────────────────────

    /** Static factory from a char array. */
    static of(raw: const char[]) : Uri! {
        return new Uri(raw);
    }

    /** Static factory from a String. */
    static of(raw: const String&) : Uri! {
        return new Uri(raw);
    }

    /** Static factory from an unsigned byte array. */
    static of(raw: const unsigned byte[]) : Uri! {
        return new Uri(raw);
    }

    /** Static factory parsing a String. */
    static create(raw: const String&) : Uri! {
        return new Uri(raw);
    }

    static intToString(val: int) : String {
        if (val == 0) {
            return String("0");
        }
        buf : unsigned byte[]! = new unsigned byte[16];
        idx : int = 15;
        buf[idx] = (unsigned byte) 0;
        --idx;
        neg : bool = val < 0;
        v : int = neg ? -val : val;
        while (v > 0) {
            buf[idx] = (unsigned byte) (((unsigned int) '0') + ((unsigned int) (v % 10)));
            --idx;
            v = v / 10;
        }
        if (neg) {
            buf[idx] = (unsigned byte) '-';
            --idx;
        }
        start : unsigned int = (unsigned int) (idx + 1);
        len : unsigned int = 15u - start;
        res : unsigned byte[]! = new unsigned byte[len + 1u];
        CharHelpers::copyBytes(res, 0u, buf, start, len);
        res[len] = (unsigned byte) 0;
        delete buf;
        s : String(res);
        delete res;
        return s;
    }

    /**
     * Construct a hierarchical URI from separate components.
     */
    static fromParts(scheme: const String&, userInfo: const String&,
                     host: const String&, port: int, path: const String&,
                     query: const String&, fragment: const String&) : Uri! {
        sb : StringBuilder;
        if (scheme.size() > 0u) {
            sb.append(scheme);
            sb.append(":");
        }
        hasAuth : bool = host.size() > 0u || userInfo.size() > 0u || port >= 0;
        if (hasAuth) {
            sb.append("//");
            if (userInfo.size() > 0u) {
                sb.append(encodeComponent(userInfo));
                sb.append("@");
            }
            if (host.size() > 0u) {
                sb.append(host);
            }
            if (port >= 0) {
                sb.append(":");
                pStr : String = intToString(port);
                sb.append(pStr);
            }
        }
        if (path.size() > 0u) {
            if (hasAuth && !path.beginsWith(String("/"))) {
                sb.append("/");
            }
            sb.append(encodePath(path));
        }
        if (query.size() > 0u) {
            sb.append("?");
            sb.append(query);
        }
        if (fragment.size() > 0u) {
            sb.append("#");
            sb.append(encodeComponent(fragment));
        }
        return new Uri(sb.toString());
    }

    static fromParts(scheme: const char[], userInfo: const char[],
                     host: const char[], port: int, path: const char[],
                     query: const char[], fragment: const char[]) : Uri! {
        return fromParts(String(scheme), String(userInfo), String(host), port, String(path), String(query), String(fragment));
    }

    /**
     * Construct a hierarchical URI from scheme, host, path, fragment.
     */
    static fromParts(scheme: const String&, host: const String&,
                     path: const String&, fragment: const String&) : Uri! {
        return fromParts(scheme, String(), host, -1, path, String(), fragment);
    }

    static fromParts(scheme: const char[], host: const char[],
                     path: const char[], fragment: const char[]) : Uri! {
        return fromParts(String(scheme), String(), String(host), -1, String(path), String(), String(fragment));
    }

    /**
     * Construct an opaque or custom URI from scheme, scheme-specific-part, fragment.
     */
    static fromParts(scheme: const String&, ssp: const String&, fragment: const String&) : Uri! {
        sb : StringBuilder;
        if (scheme.size() > 0u) {
            sb.append(scheme);
            sb.append(":");
        }
        sb.append(ssp);
        if (fragment.size() > 0u) {
            sb.append("#");
            sb.append(encodeComponent(fragment));
        }
        return new Uri(sb.toString());
    }

    static fromParts(scheme: const char[], ssp: const char[], fragment: const char[]) : Uri! {
        return fromParts(String(scheme), String(ssp), String(fragment));
    }

    // ── Codec & Encoding Utilities ───────────────────────────────────────

    /** Decode a percent-encoded string. */
    static decode(str: const String&) : String {
        bytes : unsigned byte[]! = str.toUtf8();
        u : Uri(str);
        res : String = u._decodeSlice(0, bytes.size > 1u ? bytes.size - 1u : 0u, false);
        delete bytes;
        return res;
    }

    /** Decode a percent-encoded or form-url-encoded string. */
    static decode(str: const String&, formUrl: bool) : String {
        bytes : unsigned byte[]! = str.toUtf8();
        u : Uri(str);
        res : String = u._decodeSlice(0, bytes.size > 1u ? bytes.size - 1u : 0u, formUrl);
        delete bytes;
        return res;
    }

    /** Decode a percent-encoded unsigned byte array. */
    static decode(bytes: const unsigned byte[]) : String {
        u : Uri(bytes);
        return u._decodeSlice(0, bytes.size > 1u ? bytes.size - 1u : 0u, false);
    }

    /** Percent-encode a general URI component (unreserved kept, others %XX). */
    static encodeComponent(str: const String&) : String {
        if (str.size() == 0u) return String();
        bytes : unsigned byte[]! = str.toUtf8();
        n : unsigned int = bytes.size;
        if (n <= 1u) {
            delete bytes;
            return String();
        }
        len : unsigned int = n - 1u;
        sb : StringBuilder;
        i : unsigned int = 0u;
        while (i < len) {
            b : unsigned byte = bytes[i];
            if (isUnreserved(b)) {
                tmp : unsigned byte[]! = new unsigned byte[2];
                tmp[0] = b;
                tmp[1] = (unsigned byte) 0;
                sb.append(tmp);
                delete tmp;
            } else {
                ub : unsigned int = ((unsigned int) b) & 0xFFu;
                sb.append("%");
                c1 : byte = HexEncoder::encodeNibble(ub >> 4, true);
                c2 : byte = HexEncoder::encodeNibble(ub & 0x0Fu, true);
                tmp : unsigned byte[]! = new unsigned byte[3];
                tmp[0] = (unsigned byte) c1;
                tmp[1] = (unsigned byte) c2;
                tmp[2] = (unsigned byte) 0;
                sb.append(tmp);
                delete tmp;
            }
            ++i;
        }
        delete bytes;
        return sb.toString();
    }

    /** Percent-encode a URI path (unreserved and valid path delimiters kept). */
    static encodePath(str: const String&) : String {
        if (str.size() == 0u) return String();
        bytes : unsigned byte[]! = str.toUtf8();
        n : unsigned int = bytes.size;
        if (n <= 1u) {
            delete bytes;
            return String();
        }
        len : unsigned int = n - 1u;
        sb : StringBuilder;
        i : unsigned int = 0u;
        while (i < len) {
            b : unsigned byte = bytes[i];
            if (isPathChar(b)) {
                tmp : unsigned byte[]! = new unsigned byte[2];
                tmp[0] = b;
                tmp[1] = (unsigned byte) 0;
                sb.append(tmp);
                delete tmp;
            } else {
                ub : unsigned int = ((unsigned int) b) & 0xFFu;
                sb.append("%");
                c1 : byte = HexEncoder::encodeNibble(ub >> 4, true);
                c2 : byte = HexEncoder::encodeNibble(ub & 0x0Fu, true);
                tmp : unsigned byte[]! = new unsigned byte[3];
                tmp[0] = (unsigned byte) c1;
                tmp[1] = (unsigned byte) c2;
                tmp[2] = (unsigned byte) 0;
                sb.append(tmp);
                delete tmp;
            }
            ++i;
        }
        delete bytes;
        return sb.toString();
    }

    // ── Inspection / Predicates ──────────────────────────────────────────

    /** Return true if this URI has a scheme. */
    const isAbsolute() : bool {
        return _ranges.schemeStart >= 0;
    }

    /** Return true if this URI is opaque (absolute and ssp does not start with '/'). */
    const isOpaque() : bool {
        return _ranges.isOpaque;
    }

    /** Return true if this URI is hierarchical. */
    const isHierarchical() : bool {
        return !_ranges.isOpaque;
    }

    /** Return true if this URI has a scheme component. */
    const hasScheme() : bool {
        return _ranges.schemeStart >= 0;
    }

    /** Return true if this URI has an authority component. */
    const hasAuthority() : bool {
        return _ranges.authStart >= 0;
    }

    /** Return true if this URI has a user-info component. */
    const hasUserInfo() : bool {
        return _ranges.userInfoStart >= 0;
    }

    /** Return true if this URI has a host component. */
    const hasHost() : bool {
        return _ranges.hostStart >= 0;
    }

    /** Return true if this URI has an explicit port component. */
    const hasPort() : bool {
        return _ranges.port >= 0;
    }

    /** Return true if this URI has a path component. */
    const hasPath() : bool {
        return _ranges.pathStart >= 0 && _ranges.pathLen > 0u;
    }

    /** Return true if this URI has a query component. */
    const hasQuery() : bool {
        return _ranges.queryStart >= 0;
    }

    /** Return true if this URI has a fragment component. */
    const hasFragment() : bool {
        return _ranges.fragmentStart >= 0;
    }

    // ── Raw & Decoded Accessors (Recomputed dynamically) ─────────────────

    /** Return the scheme component (lowercase). */
    const scheme() : String {
        return _sliceToString(_ranges.schemeStart, _ranges.schemeLen);
    }

    /** Return the raw scheme-specific-part. */
    const rawSchemeSpecificPart() : String {
        return _sliceToString(_ranges.sspStart, _ranges.sspLen);
    }

    /** Return the decoded scheme-specific-part. */
    const schemeSpecificPart() : String {
        return _decodeSlice(_ranges.sspStart, _ranges.sspLen, false);
    }

    /** Return the raw authority component. */
    const rawAuthority() : String {
        return _sliceToString(_ranges.authStart, _ranges.authLen);
    }

    /** Return the decoded authority component. */
    const authority() : String {
        return _decodeSlice(_ranges.authStart, _ranges.authLen, false);
    }

    /** Return the raw user-info component. */
    const rawUserInfo() : String {
        return _sliceToString(_ranges.userInfoStart, _ranges.userInfoLen);
    }

    /** Return the decoded user-info component. */
    const userInfo() : String {
        return _decodeSlice(_ranges.userInfoStart, _ranges.userInfoLen, false);
    }

    /** Return the host component. */
    const host() : String {
        return _sliceToString(_ranges.hostStart, _ranges.hostLen);
    }

    /** Return the port number, or -1 if not defined. */
    const port() : int {
        return _ranges.port;
    }

    /** Return the raw path component. */
    const rawPath() : String {
        return _sliceToString(_ranges.pathStart, _ranges.pathLen);
    }

    /** Return the decoded path component. */
    const path() : String {
        return _decodeSlice(_ranges.pathStart, _ranges.pathLen, false);
    }

    /** Return the raw query component. */
    const rawQuery() : String {
        return _sliceToString(_ranges.queryStart, _ranges.queryLen);
    }

    /** Return the decoded query component. */
    const query() : String {
        return _decodeSlice(_ranges.queryStart, _ranges.queryLen, false);
    }

    /** Return a new QueryParams parsed from this URI's query component. */
    const queryParams() : QueryParams! {
        if (_ranges.queryStart < 0 || _ranges.queryLen == 0u) {
            return QueryParams::of();
        }
        qStr : String = rawQuery();
        return QueryParams::parse(qStr);
    }

    /** Return the value of a specific query parameter. */
    const queryParam(name: const String&) : String {
        if (_ranges.queryStart < 0 || _ranges.queryLen == 0u) {
            return String();
        }
        qp : QueryParams! = queryParams();
        res : String = qp.get(name);
        delete qp;
        return res;
    }

    /** Return the value of a specific query parameter. */
    const queryParam(name: const char[]) : String {
        return queryParam(String(name));
    }

    /** Return the raw fragment component. */
    const rawFragment() : String {
        return _sliceToString(_ranges.fragmentStart, _ranges.fragmentLen);
    }

    /** Return the decoded fragment component. */
    const fragment() : String {
        return _decodeSlice(_ranges.fragmentStart, _ranges.fragmentLen, false);
    }

    /** Return the full raw URI text as a String. */
    const toString() : String {
        return _sliceToString(0, _len);
    }

    /** Return the full raw URI text as null-terminated UTF-8 bytes. */
    const toUtf8() : unsigned byte[]! {
        res : unsigned byte[]! = new unsigned byte[_len + 1u];
        if (_len > 0u) {
            CharHelpers::copyBytes(res, 0u, _raw, 0u, _len);
        }
        res[_len] = (unsigned byte) 0;
        return res;
    }

    /** Return a direct read-only view to the internal byte array. */
    const raw() : const unsigned byte[]? {
        return _raw;
    }

    /** Return the length in bytes. */
    const length() : unsigned int {
        return _len;
    }

    // ── Mutators with* (Immutable creation) ──────────────────────────────

    /** Return a new Uri with the given scheme. */
    const withScheme(newScheme: const String&) : Uri! {
        if (isOpaque()) {
            return fromParts(newScheme, rawSchemeSpecificPart(), rawFragment());
        }
        return fromParts(newScheme, rawUserInfo(), host(), port(), rawPath(), rawQuery(), rawFragment());
    }

    const withScheme(newScheme: const char[]) : Uri! {
        return withScheme(String(newScheme));
    }

    const withScheme(newScheme: const unsigned byte[]) : Uri! {
        return withScheme(String(newScheme));
    }

    /** Return a new Uri without any scheme (relative reference). */
    const withoutScheme() : Uri! {
        return withScheme(String());
    }

    /** Return a new Uri with the given user-info. */
    const withUserInfo(newUserInfo: const String&) : Uri! {
        return fromParts(scheme(), newUserInfo, host(), port(), rawPath(), rawQuery(), rawFragment());
    }

    const withUserInfo(newUserInfo: const char[]) : Uri! {
        return withUserInfo(String(newUserInfo));
    }

    const withUserInfo(newUserInfo: const unsigned byte[]) : Uri! {
        return withUserInfo(String(newUserInfo));
    }

    const withoutUserInfo() : Uri! {
        return withUserInfo(String());
    }

    /** Return a new Uri with the given host. */
    const withHost(newHost: const String&) : Uri! {
        return fromParts(scheme(), rawUserInfo(), newHost, port(), rawPath(), rawQuery(), rawFragment());
    }

    const withHost(newHost: const char[]) : Uri! {
        return withHost(String(newHost));
    }

    const withHost(newHost: const unsigned byte[]) : Uri! {
        return withHost(String(newHost));
    }

    /** Return a new Uri with the given port. */
    const withPort(newPort: int) : Uri! {
        return fromParts(scheme(), rawUserInfo(), host(), newPort, rawPath(), rawQuery(), rawFragment());
    }

    const withoutPort() : Uri! {
        return withPort(-1);
    }

    /** Return a new Uri with the given authority (user:pass@host:port). */
    const withAuthority(newAuth: const String&) : Uri! {
        sb : StringBuilder;
        if (hasScheme()) {
            sb.append(scheme());
            sb.append(":");
        }
        if (newAuth.size() > 0u) {
            sb.append("//");
            sb.append(newAuth);
        }
        rPath : String = rawPath();
        if (rPath.size() > 0u) {
            if (newAuth.size() > 0u && !rPath.beginsWith(String("/"))) {
                sb.append("/");
            }
            sb.append(rPath);
        }
        if (hasQuery()) {
            sb.append("?");
            sb.append(rawQuery());
        }
        if (hasFragment()) {
            sb.append("#");
            sb.append(rawFragment());
        }
        return new Uri(sb.toString());
    }

    const withAuthority(newAuth: const char[]) : Uri! {
        return withAuthority(String(newAuth));
    }

    const withAuthority(newAuth: const unsigned byte[]) : Uri! {
        return withAuthority(String(newAuth));
    }

    const withoutAuthority() : Uri! {
        return withAuthority(String());
    }

    /** Return a new Uri with the given path. */
    const withPath(newPath: const String&) : Uri! {
        return fromParts(scheme(), rawUserInfo(), host(), port(), newPath, rawQuery(), rawFragment());
    }

    const withPath(newPath: const char[]) : Uri! {
        return withPath(String(newPath));
    }

    const withPath(newPath: const unsigned byte[]) : Uri! {
        return withPath(String(newPath));
    }

    /** Return a new Uri with the given query string. */
    const withQuery(newQuery: const String&) : Uri! {
        return fromParts(scheme(), rawUserInfo(), host(), port(), rawPath(), newQuery, rawFragment());
    }

    const withQuery(newQuery: const char[]) : Uri! {
        return withQuery(String(newQuery));
    }

    const withQuery(newQuery: const unsigned byte[]) : Uri! {
        return withQuery(String(newQuery));
    }

    const withoutQuery() : Uri! {
        return withQuery(String());
    }

    /** Return a new Uri with the given QueryParams container. */
    const withQueryParams(params: const QueryParams&) : Uri! {
        return withQuery(params.toString());
    }

    /** Return a new Uri with a query parameter appended. */
    const withQueryParam(name: const String&, value: const String&) : Uri! {
        qp : QueryParams! = queryParams();
        qp.add(name, value);
        res : Uri! = withQuery(qp.toString());
        delete qp;
        return res;
    }

    const withQueryParam(name: const char[], value: const char[]) : Uri! {
        return withQueryParam(String(name), String(value));
    }

    /** Return a new Uri with a query parameter removed. */
    const withoutQueryParam(name: const String&) : Uri! {
        qp : QueryParams! = queryParams();
        qp.remove(name);
        res : Uri! = withQuery(qp.toString());
        delete qp;
        return res;
    }

    const withoutQueryParam(name: const char[]) : Uri! {
        return withoutQueryParam(String(name));
    }

    /** Return a new Uri with the given fragment. */
    const withFragment(newFragment: const String&) : Uri! {
        if (isOpaque()) {
            return fromParts(scheme(), rawSchemeSpecificPart(), newFragment);
        }
        return fromParts(scheme(), rawUserInfo(), host(), port(), rawPath(), rawQuery(), newFragment);
    }

    const withFragment(newFragment: const char[]) : Uri! {
        return withFragment(String(newFragment));
    }

    const withFragment(newFragment: const unsigned byte[]) : Uri! {
        return withFragment(String(newFragment));
    }

    const withoutFragment() : Uri! {
        return withFragment(String());
    }

    // ── RFC 3986 Operations ──────────────────────────────────────────────

    /**
     * Normalize this URI: lowercase scheme/host, removes dot segments in path.
     */
    const normalize() : Uri! {
        if (isOpaque() || !hasPath()) {
            return new Uri(this);
        }
        normPath : String = removeDotSegments(rawPath());
        return fromParts(scheme(), rawUserInfo(), host(), port(), normPath, rawQuery(), rawFragment());
    }

    /**
     * Resolve a relative URI reference against this base URI (RFC 3986 section 5.2).
     */
    const resolve(relative: const Uri&) : Uri! {
        if (relative.isAbsolute()) {
            return new Uri(relative);
        }
        if (isOpaque()) {
            return new Uri(relative);
        }
        if (relative.hasAuthority()) {
            normPath : String = removeDotSegments(relative.rawPath());
            return fromParts(scheme(), relative.rawUserInfo(), relative.host(), relative.port(),
                             normPath, relative.rawQuery(), relative.rawFragment());
        }

        relPath : String = relative.rawPath();
        tarPath : String;
        tarQuery : String = relative.hasQuery() ? relative.rawQuery() : rawQuery();

        if (relPath.size() == 0u) {
            tarPath = rawPath();
        } else if (relPath.beginsWith(String("/"))) {
            tarPath = removeDotSegments(relPath);
        } else {
            // Merge paths
            bPath : String = rawPath();
            sb : StringBuilder;
            if (hasAuthority() && bPath.size() == 0u) {
                sb.append("/");
                sb.append(relPath);
            } else {
                lastSlash : int = bPath.rfind('/');
                if (lastSlash >= 0) {
                    sb.append(bPath.first(((unsigned int) lastSlash) + 1u));
                }
                sb.append(relPath);
            }
            tarPath = removeDotSegments(sb.toString());
        }

        return fromParts(scheme(), rawUserInfo(), host(), port(), tarPath, tarQuery, relative.rawFragment());
    }

    const resolve(relative: const String&) : Uri! {
        u : Uri(relative);
        return resolve(u);
    }

    const resolve(relative: const char[]) : Uri! {
        u : Uri(relative);
        return resolve(u);
    }

    const resolve(relative: const unsigned byte[]) : Uri! {
        u : Uri(relative);
        return resolve(u);
    }

    /**
     * Relativize `target` against this base URI.
     */
    const relativize(target: const Uri&) : Uri! {
        if (!isAbsolute() || !target.isAbsolute() || isOpaque() || target.isOpaque()) {
            return new Uri(target);
        }
        if (scheme() != target.scheme() || rawAuthority() != target.rawAuthority()) {
            return new Uri(target);
        }

        bPath : String = rawPath();
        tPath : String = target.rawPath();
        if (tPath.beginsWith(bPath)) {
            remPath : String = tPath.substr(bPath.size(), tPath.size() - bPath.size());
            if (remPath.beginsWith(String("/"))) {
                remPath = remPath.substr(1u, remPath.size() - 1u);
            }
            sb : StringBuilder;
            sb.append(remPath);
            if (target.hasQuery()) {
                sb.append("?");
                sb.append(target.rawQuery());
            }
            if (target.hasFragment()) {
                sb.append("#");
                sb.append(target.rawFragment());
            }
            return new Uri(sb.toString());
        }
        return new Uri(target);
    }

    // ── Comparison operators ────────────────────────────────────────────

    const compareTo(other: const Uri&) : int {
        s1 : String = toString();
        s2 : String = other.toString();
        return s1.compareTo(s2);
    }

    const operator ==(other: const Uri&) : bool {
        if (_len != other._len) return false;
        if (_len == 0u) return true;
        i : unsigned int = 0u;
        while (i < _len) {
            if (_raw[i] != other._raw[i]) return false;
            ++i;
        }
        return true;
    }

    const operator !=(other: const Uri&) : bool {
        return !(this == other);
    }
}

} // namespace io
