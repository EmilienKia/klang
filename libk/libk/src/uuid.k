/*
 * K Language standard library — UUID (Universally Unique Identifier)
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

// ── FFI declarations for UUID runtime helpers ────────────────────────────────
@ffi::Extern("C") private __k_uuid_random_u64() : unsigned long;
@ffi::Extern("C") private __k_uuid_gregorian_time_100ns() : unsigned long;
@ffi::Extern("C") private __k_uuid_unix_time_millis() : unsigned long;
@ffi::Extern("C") private __k_uuid_get_node_id() : unsigned long;
@ffi::Extern("C") private __k_uuid_get_clock_sequence() : unsigned short;

/**
 * Universally Unique Identifier (UUID) — RFC 4122 and RFC 9562 compliant.
 *
 * An immutable 128-bit identifier supporting UUID versions 1 through 8,
 * Nil UUID, Max UUID, standard and custom namespaces, version-specific
 * builders, and version-specific helper accessors.
 */
public final class Uuid {
private:
    _mostSigBits : unsigned long;
    _leastSigBits : unsigned long;

    static encodeNibble(nibble: unsigned int) : byte {
        n : unsigned int = nibble & 0x0Fu;
        if (n < 10u) {
            return (byte) (((unsigned int) '0') + n);
        } else {
            return (byte) (((unsigned int) 'a') + (n - 10u));
        }
    }

    static decodeHexNibble(c: char) : int {
        uc : unsigned int = (unsigned int) c;
        if (uc >= ((unsigned int) '0') && uc <= ((unsigned int) '9')) {
            return (int) (uc - ((unsigned int) '0'));
        }
        if (uc >= ((unsigned int) 'a') && uc <= ((unsigned int) 'f')) {
            return (int) (uc - ((unsigned int) 'a') + 10u);
        }
        if (uc >= ((unsigned int) 'A') && uc <= ((unsigned int) 'F')) {
            return (int) (uc - ((unsigned int) 'A') + 10u);
        }
        return -1;
    }

    static encodeHexByte(b: byte, out: byte[], offset: unsigned int) {
        ub : unsigned int = ((unsigned int) b) & 0xFFu;
        out[offset] = encodeNibble(ub >> 4u);
        out[offset + 1u] = encodeNibble(ub);
    }

    static readU64BE(buf: const byte[], offset: unsigned int) : unsigned long {
        val : unsigned long = 0uL;
        i : unsigned int = 0u;
        while (i < 8u) {
            val = (val << 8u) | (((unsigned long) buf[offset + i]) & 0xFFuL);
            ++i;
        }
        return val;
    }

    static writeU64BE(val: unsigned long, out: byte[], offset: unsigned int) {
        i : unsigned int = 0u;
        while (i < 8u) {
            shift : unsigned int = (7u - i) * 8u;
            out[offset + i] = (byte) ((val >> shift) & 0xFFuL);
            ++i;
        }
    }

public:
    // ── Predefined Namespace Constants (RFC 4122) ────────────────────────────

    /** Name string is a fully-qualified domain name (6ba7b810-9dad-11d1-80b4-00c04fd430c8). */
    public static namespaceDns() : Uuid {
        return Uuid(0x6ba7b8109dad11d1uL, 0x80b400c04fd430c8uL);
    }

    /** Name string is a URL (6ba7b811-9dad-11d1-80b4-00c04fd430c8). */
    public static namespaceUrl() : Uuid {
        return Uuid(0x6ba7b8119dad11d1uL, 0x80b400c04fd430c8uL);
    }

    /** Name string is an ISO OID (6ba7b812-9dad-11d1-80b4-00c04fd430c8). */
    public static namespaceOid() : Uuid {
        return Uuid(0x6ba7b8129dad11d1uL, 0x80b400c04fd430c8uL);
    }

    /** Name string is an X.500 DN (6ba7b814-9dad-11d1-80b4-00c04fd430c8). */
    public static namespaceX500() : Uuid {
        return Uuid(0x6ba7b8149dad11d1uL, 0x80b400c04fd430c8uL);
    }

    // ── Constructors ─────────────────────────────────────────────────────────

    /** Default constructor — creates a Nil UUID (00000000-0000-0000-0000-000000000000). */
    Uuid() : _mostSigBits(0uL), _leastSigBits(0uL) {}

    /** Construct a UUID from two 64-bit unsigned integers. */
    Uuid(mostSigBits: unsigned long, leastSigBits: unsigned long)
        : _mostSigBits(mostSigBits), _leastSigBits(leastSigBits) {}

    /** Construct a UUID from a 16-byte array in big-endian network byte order. */
    Uuid(bytes: const byte[]) : _mostSigBits(0uL), _leastSigBits(0uL) {
        if (bytes.size >= 16u) {
            _mostSigBits = readU64BE(bytes, 0u);
            _leastSigBits = readU64BE(bytes, 8u);
        }
    }

    /** Copy constructor. */
    Uuid(other: const Uuid&)
        : _mostSigBits(other._mostSigBits), _leastSigBits(other._leastSigBits) {}

    // ── Static Builders ──────────────────────────────────────────────────────

    /** Return the Nil UUID (all bits zero). */
    public static nil() : Uuid {
        return Uuid(0uL, 0uL);
    }

    /** Return the Max UUID (all bits one, RFC 9562). */
    public static max() : Uuid {
        return Uuid(0xFFFFFFFFFFFFFFFFuL, 0xFFFFFFFFFFFFFFFFuL);
    }

    /** Create a UUID directly from two 64-bit integer values. */
    public static fromBits(mostSigBits: unsigned long, leastSigBits: unsigned long) : Uuid {
        return Uuid(mostSigBits, leastSigBits);
    }

    /** Create a UUID from a 16-byte array. */
    public static fromBytes(bytes: const byte[]) : Uuid {
        return Uuid(bytes);
    }

    /** Create a UUID from a 16-byte slice starting at the given offset. */
    public static fromBytes(bytes: const byte[], offset: unsigned int) : Uuid {
        if (offset + 16u <= bytes.size) {
            ms : unsigned long = readU64BE(bytes, offset);
            ls : unsigned long = readU64BE(bytes, offset + 8u);
            return Uuid(ms, ls);
        }
        return Uuid(0uL, 0uL);
    }

    /**
     * Version 1 Builder (Gregorian Time + Clock Sequence + Node MAC address).
     */
    public static v1(node: unsigned long, clockSeq: int, timestamp100ns: unsigned long) : Uuid {
        timeLow : unsigned long = timestamp100ns & 0xFFFFFFFFuL;
        timeMid : unsigned long = (timestamp100ns >> 32u) & 0xFFFFuL;
        timeHi : unsigned long = (timestamp100ns >> 48u) & 0x0FFFuL;

        mostSig : unsigned long = (timeLow << 32u) | (timeMid << 16u) | 0x1000uL | timeHi;
        clockSeqVal : unsigned long = ((unsigned long) clockSeq) & 0x3FFFuL;
        nodeVal : unsigned long = node & 0xFFFFFFFFFFFFuL;
        leastSig : unsigned long = (0x8000uL | clockSeqVal) << 48u | nodeVal;

        return Uuid(mostSig, leastSig);
    }

    /**
     * Version 1 Builder using system clock, system sequence, and hardware node MAC.
     */
    public static v1() : Uuid {
        ts : unsigned long = __k_uuid_gregorian_time_100ns();
        cs : int = (int) __k_uuid_get_clock_sequence();
        nd : unsigned long = __k_uuid_get_node_id();
        return v1(nd, cs, ts);
    }

    /**
     * Version 2 Builder (DCE Security: POSIX UID/GID + Domain).
     */
    public static v2(domain: byte, localId: unsigned int, node: unsigned long, clockSeq: int, timestamp100ns: unsigned long) : Uuid {
        timeMid : unsigned long = (timestamp100ns >> 32u) & 0xFFFFuL;
        timeHi : unsigned long = (timestamp100ns >> 48u) & 0x0FFFuL;

        mostSig : unsigned long = (((unsigned long) localId) << 32u) | (timeMid << 16u) | 0x2000uL | timeHi;
        clockSeqHi : unsigned long = ((unsigned long) (clockSeq & 0x3F)) << 8u;
        domainVal : unsigned long = ((unsigned long) domain) & 0xFFuL;
        nodeVal : unsigned long = node & 0xFFFFFFFFFFFFuL;

        leastSig : unsigned long = (0x8000uL | clockSeqHi | domainVal) << 48u | nodeVal;
        return Uuid(mostSig, leastSig);
    }

    /**
     * Version 2 Builder with current system time and hardware node MAC.
     */
    public static v2(domain: byte, localId: unsigned int) : Uuid {
        ts : unsigned long = __k_uuid_gregorian_time_100ns();
        cs : int = (int) __k_uuid_get_clock_sequence();
        nd : unsigned long = __k_uuid_get_node_id();
        return v2(domain, localId, nd, cs, ts);
    }

    /**
     * Version 3 Builder (Name-based MD5).
     */
    public static v3(namespaceUuid: const Uuid&, nameBytes: const byte[]) : Uuid {
        nsBytes : byte[]! = namespaceUuid.toByteArray();
        hash : byte[]! = crypto::Md5::digestConcat(nsBytes, nameBytes);
        delete nsBytes;

        ms : unsigned long = readU64BE(hash, 0u);
        ls : unsigned long = readU64BE(hash, 8u);
        delete hash;

        // Set version 3 (0b0011) and RFC 4122 variant (0b10)
        ms = (ms & 0xFFFFFFFFFFFF0FFFuL) | 0x0000000000003000uL;
        ls = (ls & 0x3FFFFFFFFFFFFFFFuL) | 0x8000000000000000uL;

        return Uuid(ms, ls);
    }

    /**
     * Version 3 Builder (Name-based MD5 with String name).
     */
    public static v3(namespaceUuid: const Uuid&, name: const String&) : Uuid {
        u8 : unsigned byte[]! = name.toUtf8();
        n : unsigned int = (u8.size > 1u) ? (u8.size - 1u) : 0u;
        raw : byte[]! = new byte[n];
        i : unsigned int = 0u;
        while (i < n) {
            raw[i] = (byte) u8[i];
            ++i;
        }
        delete u8;

        res : Uuid = v3(namespaceUuid, raw);
        delete raw;
        return res;
    }

    /**
     * Version 4 Builder (Random UUID).
     */
    public static v4() : Uuid {
        ms : unsigned long = __k_uuid_random_u64();
        ls : unsigned long = __k_uuid_random_u64();

        // Set version 4 (0b0100) and RFC 4122 variant (0b10)
        ms = (ms & 0xFFFFFFFFFFFF0FFFuL) | 0x0000000000004000uL;
        ls = (ls & 0x3FFFFFFFFFFFFFFFuL) | 0x8000000000000000uL;

        return Uuid(ms, ls);
    }

    /** Alias for v4(). */
    public static random() : Uuid {
        return v4();
    }

    /**
     * Version 5 Builder (Name-based SHA-1).
     */
    public static v5(namespaceUuid: const Uuid&, nameBytes: const byte[]) : Uuid {
        nsBytes : byte[]! = namespaceUuid.toByteArray();
        hash : byte[]! = crypto::Sha1::digestConcat(nsBytes, nameBytes);
        delete nsBytes;

        ms : unsigned long = readU64BE(hash, 0u);
        ls : unsigned long = readU64BE(hash, 8u);
        delete hash;

        // Set version 5 (0b0101) and RFC 4122 variant (0b10)
        ms = (ms & 0xFFFFFFFFFFFF0FFFuL) | 0x0000000000005000uL;
        ls = (ls & 0x3FFFFFFFFFFFFFFFuL) | 0x8000000000000000uL;

        return Uuid(ms, ls);
    }

    /**
     * Version 5 Builder (Name-based SHA-1 with String name).
     */
    public static v5(namespaceUuid: const Uuid&, name: const String&) : Uuid {
        u8 : unsigned byte[]! = name.toUtf8();
        n : unsigned int = (u8.size > 1u) ? (u8.size - 1u) : 0u;
        raw : byte[]! = new byte[n];
        i : unsigned int = 0u;
        while (i < n) {
            raw[i] = (byte) u8[i];
            ++i;
        }
        delete u8;

        res : Uuid = v5(namespaceUuid, raw);
        delete raw;
        return res;
    }

    /**
     * Version 6 Builder (Reordered Gregorian Time, RFC 9562).
     */
    public static v6(node: unsigned long, clockSeq: int, timestamp100ns: unsigned long) : Uuid {
        timeHiMid : unsigned long = (timestamp100ns >> 12u) & 0xFFFFFFFFFFFFuL;
        timeLow : unsigned long = timestamp100ns & 0x0FFFuL;

        mostSig : unsigned long = (timeHiMid << 16u) | 0x6000uL | timeLow;
        clockSeqVal : unsigned long = ((unsigned long) clockSeq) & 0x3FFFuL;
        nodeVal : unsigned long = node & 0xFFFFFFFFFFFFuL;
        leastSig : unsigned long = (0x8000uL | clockSeqVal) << 48u | nodeVal;

        return Uuid(mostSig, leastSig);
    }

    /**
     * Version 6 Builder using system time, sequence, and node MAC.
     */
    public static v6() : Uuid {
        ts : unsigned long = __k_uuid_gregorian_time_100ns();
        cs : int = (int) __k_uuid_get_clock_sequence();
        nd : unsigned long = __k_uuid_get_node_id();
        return v6(nd, cs, ts);
    }

    /**
     * Version 7 Builder (Unix Epoch Milliseconds + Random Sequence, RFC 9562).
     */
    public static v7(timestampMs: unsigned long, randA: unsigned int, randB: unsigned long) : Uuid {
        ts48 : unsigned long = timestampMs & 0xFFFFFFFFFFFFuL;
        ra12 : unsigned long = ((unsigned long) randA) & 0x0FFFuL;

        mostSig : unsigned long = (ts48 << 16u) | 0x7000uL | ra12;
        leastSig : unsigned long = 0x8000000000000000uL | (randB & 0x3FFFFFFFFFFFFFFFuL);

        return Uuid(mostSig, leastSig);
    }

    /**
     * Version 7 Builder with explicit millisecond timestamp and random entropy.
     */
    public static v7(timestampMs: unsigned long) : Uuid {
        rndA : unsigned long = __k_uuid_random_u64();
        rndB : unsigned long = __k_uuid_random_u64();
        return v7(timestampMs, (unsigned int) (rndA & 0x0FFFuL), rndB);
    }

    /**
     * Version 7 Builder with current system time and random entropy.
     */
    public static v7() : Uuid {
        ts : unsigned long = __k_uuid_unix_time_millis();
        return v7(ts);
    }

    /**
     * Version 8 Builder (Custom Vendor-specific data, RFC 9562).
     */
    public static v8(customMostSig: unsigned long, customLeastSig: unsigned long) : Uuid {
        ms : unsigned long = (customMostSig & 0xFFFFFFFFFFFF0FFFuL) | 0x0000000000008000uL;
        ls : unsigned long = (customLeastSig & 0x3FFFFFFFFFFFFFFFuL) | 0x8000000000000000uL;
        return Uuid(ms, ls);
    }

    /**
     * Version 8 Builder from 16-byte custom data.
     */
    public static v8(customBytes: const byte[]) : Uuid {
        if (customBytes.size >= 16u) {
            ms : unsigned long = readU64BE(customBytes, 0u);
            ls : unsigned long = readU64BE(customBytes, 8u);
            return v8(ms, ls);
        }
        return v8(0uL, 0uL);
    }

    // ── General Accessors ────────────────────────────────────────────────────

    /** Return the most significant 64 bits. */
    const mostSignificantBits() : unsigned long {
        return _mostSigBits;
    }

    /** Return the least significant 64 bits. */
    const leastSignificantBits() : unsigned long {
        return _leastSigBits;
    }

    /**
     * Return the UUID version number (1 to 8, or 0 for Nil).
     */
    const version() : int {
        return (int) ((_mostSigBits >> 12u) & 0x0FuL);
    }

    /**
     * Return the UUID variant number:
     *   0 = Reserved for NCS backward compatibility
     *   2 = RFC 4122 / Leach-Salz
     *   6 = Reserved for Microsoft Corporation backward compatibility
     *   7 = Reserved for future definition
     */
    const variant() : int {
        if ((_leastSigBits & 0x8000000000000000uL) == 0uL) {
            return 0;
        }
        if ((_leastSigBits & 0x4000000000000000uL) == 0uL) {
            return 2;
        }
        if ((_leastSigBits & 0x2000000000000000uL) == 0uL) {
            return 6;
        }
        return 7;
    }

    /** Tell whether this is the Nil UUID. */
    const isNil() : bool {
        return _mostSigBits == 0uL && _leastSigBits == 0uL;
    }

    /** Tell whether this is the Max UUID (RFC 9562). */
    const isMax() : bool {
        return _mostSigBits == 0xFFFFFFFFFFFFFFFFuL && _leastSigBits == 0xFFFFFFFFFFFFFFFFuL;
    }

    /**
     * Return the 16-byte representation in network byte order (big-endian).
     */
    const toByteArray() : byte[]! {
        out : byte[]! = new byte[16u];
        writeU64BE(_mostSigBits, out, 0u);
        writeU64BE(_leastSigBits, out, 8u);
        return out;
    }

    // ── Version-specific Helper Accessors ────────────────────────────────────

    /**
     * Return the 60-bit timestamp in 100ns units since October 15, 1582.
     * Applicable to Version 1 and Version 6 UUIDs.
     */
    const timestamp() : unsigned long {
        v : int = version();
        if (v == 1) {
            timeLow : unsigned long = (_mostSigBits >> 32u) & 0xFFFFFFFFuL;
            timeMid : unsigned long = (_mostSigBits >> 16u) & 0xFFFFuL;
            timeHi : unsigned long = _mostSigBits & 0x0FFFuL;
            return (timeHi << 48u) | (timeMid << 32u) | timeLow;
        }
        if (v == 6) {
            timeHiMid : unsigned long = _mostSigBits >> 16u;
            timeLow : unsigned long = _mostSigBits & 0x0FFFuL;
            return (timeHiMid << 12u) | timeLow;
        }
        return 0uL;
    }

    /**
     * Return the 14-bit clock sequence.
     * Applicable to Version 1, 2, and 6 UUIDs.
     */
    const clockSequence() : int {
        return (int) ((_leastSigBits >> 48u) & 0x3FFFuL);
    }

    /**
     * Return the 48-bit node identifier (IEEE 802 MAC address or random node).
     * Applicable to Version 1, 2, and 6 UUIDs.
     */
    const node() : unsigned long {
        return _leastSigBits & 0xFFFFFFFFFFFFuL;
    }

    /**
     * Return the 6-byte node identifier array.
     */
    const nodeBytes() : byte[]! {
        out : byte[]! = new byte[6u];
        nd : unsigned long = node();
        i : unsigned int = 0u;
        while (i < 6u) {
            shift : unsigned int = (5u - i) * 8u;
            out[i] = (byte) ((nd >> shift) & 0xFFuL);
            ++i;
        }
        return out;
    }

    /**
     * Return the local domain byte (POSIX UID/GID domain).
     * Applicable to Version 2 UUIDs.
     */
    const localDomain() : byte {
        return (byte) ((_leastSigBits >> 48u) & 0xFFuL);
    }

    /**
     * Return the local identifier (POSIX UID/GID).
     * Applicable to Version 2 UUIDs.
     */
    const localIdentifier() : unsigned int {
        return (unsigned int) ((_mostSigBits >> 32u) & 0xFFFFFFFFuL);
    }

    /**
     * Return the Unix timestamp in milliseconds since 1970-01-01 UTC.
     * Applicable to Version 7 UUIDs.
     */
    const unixTimestampMillis() : unsigned long {
        return _mostSigBits >> 16u;
    }

    /**
     * Return the 12-bit sub-millisecond sequence / random fraction (rand_a).
     * Applicable to Version 7 UUIDs.
     */
    const subSequence() : unsigned int {
        return (unsigned int) (_mostSigBits & 0x0FFFuL);
    }

    /**
     * Return the 62-bit random entropy (rand_b).
     * Applicable to Version 7 UUIDs.
     */
    const randomBits() : unsigned long {
        return _leastSigBits & 0x3FFFFFFFFFFFFFFFuL;
    }

    // ── String Formatting & Parsing ──────────────────────────────────────────

    /**
     * Return the standard 36-character lowercase string (8-4-4-4-12 format).
     * Example: "6ba7b810-9dad-11d1-80b4-00c04fd430c8"
     */
    const toString() : String {
        buf : unsigned byte[]! = new unsigned byte[37u];
        raw : byte[]! = toByteArray();

        // 8 chars (bytes 0..3)
        encodeHexByte(raw[0], (byte[]) buf, 0u);
        encodeHexByte(raw[1], (byte[]) buf, 2u);
        encodeHexByte(raw[2], (byte[]) buf, 4u);
        encodeHexByte(raw[3], (byte[]) buf, 6u);
        buf[8u] = (unsigned byte) '-';

        // 4 chars (bytes 4..5)
        encodeHexByte(raw[4], (byte[]) buf, 9u);
        encodeHexByte(raw[5], (byte[]) buf, 11u);
        buf[13u] = (unsigned byte) '-';

        // 4 chars (bytes 6..7)
        encodeHexByte(raw[6], (byte[]) buf, 14u);
        encodeHexByte(raw[7], (byte[]) buf, 16u);
        buf[18u] = (unsigned byte) '-';

        // 4 chars (bytes 8..9)
        encodeHexByte(raw[8], (byte[]) buf, 19u);
        encodeHexByte(raw[9], (byte[]) buf, 21u);
        buf[23u] = (unsigned byte) '-';

        // 12 chars (bytes 10..15)
        encodeHexByte(raw[10], (byte[]) buf, 24u);
        encodeHexByte(raw[11], (byte[]) buf, 26u);
        encodeHexByte(raw[12], (byte[]) buf, 28u);
        encodeHexByte(raw[13], (byte[]) buf, 30u);
        encodeHexByte(raw[14], (byte[]) buf, 32u);
        encodeHexByte(raw[15], (byte[]) buf, 34u);
        buf[36u] = (unsigned byte) 0;

        delete raw;
        str : String(buf);
        delete buf;
        return str;
    }

    /**
     * Return the URN representation: "urn:uuid:xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx".
     */
    const toUrn() : String {
        buf : unsigned byte[]! = new unsigned byte[46u];
        urnPrefix : const unsigned byte[] = u8"urn:uuid:";
        i : unsigned int = 0u;
        while (i < 9u) {
            buf[i] = urnPrefix[i];
            ++i;
        }

        raw : byte[]! = toByteArray();
        encodeHexByte(raw[0], (byte[]) buf, 9u);
        encodeHexByte(raw[1], (byte[]) buf, 11u);
        encodeHexByte(raw[2], (byte[]) buf, 13u);
        encodeHexByte(raw[3], (byte[]) buf, 15u);
        buf[17u] = (unsigned byte) '-';

        encodeHexByte(raw[4], (byte[]) buf, 18u);
        encodeHexByte(raw[5], (byte[]) buf, 20u);
        buf[22u] = (unsigned byte) '-';

        encodeHexByte(raw[6], (byte[]) buf, 23u);
        encodeHexByte(raw[7], (byte[]) buf, 25u);
        buf[27u] = (unsigned byte) '-';

        encodeHexByte(raw[8], (byte[]) buf, 28u);
        encodeHexByte(raw[9], (byte[]) buf, 30u);
        buf[32u] = (unsigned byte) '-';

        encodeHexByte(raw[10], (byte[]) buf, 33u);
        encodeHexByte(raw[11], (byte[]) buf, 35u);
        encodeHexByte(raw[12], (byte[]) buf, 37u);
        encodeHexByte(raw[13], (byte[]) buf, 39u);
        encodeHexByte(raw[14], (byte[]) buf, 41u);
        encodeHexByte(raw[15], (byte[]) buf, 43u);
        buf[45u] = (unsigned byte) 0;

        delete raw;
        str : String(buf);
        delete buf;
        return str;
    }

    /**
     * Attempt to parse a UUID string (36-char hyphenated, 45-char URN, or 32-char hex).
     */
    public static tryParse(str: const String&) : Optional<Uuid> {
        sz : unsigned int = str.size();
        startOff : unsigned int = 0u;

        // Check for "urn:uuid:" prefix (case-insensitive)
        if (sz >= 9u) {
            c0 : char = str.at(0u);
            c1 : char = str.at(1u);
            c2 : char = str.at(2u);
            c3 : char = str.at(3u);
            c4 : char = str.at(4u);
            c5 : char = str.at(5u);
            c6 : char = str.at(6u);
            c7 : char = str.at(7u);
            c8 : char = str.at(8u);
            if ((c0 == 'u' || c0 == 'U') && (c1 == 'r' || c1 == 'R') && (c2 == 'n' || c2 == 'N') &&
                c3 == ':' &&
                (c4 == 'u' || c4 == 'U') && (c5 == 'u' || c5 == 'U') && (c6 == 'i' || c6 == 'I') && (c7 == 'd' || c7 == 'D') &&
                c8 == ':') {
                startOff = 9u;
                sz -= 9u;
            }
        }

        ms : unsigned long = 0uL;
        ls : unsigned long = 0uL;

        if (sz == 36u) {
            // Check hyphens at 8, 13, 18, 23
            if (str.at(startOff + 8u) != '-' || str.at(startOff + 13u) != '-' ||
                str.at(startOff + 18u) != '-' || str.at(startOff + 23u) != '-') {
                return Optional<Uuid>::empty();
            }

            nibbleCount : unsigned int = 0u;
            i : unsigned int = 0u;
            while (i < 36u) {
                if (i == 8u || i == 13u || i == 18u || i == 23u) {
                    ++i;
                    continue;
                }
                c : char = str.at(startOff + i);
                val : int = decodeHexNibble(c);
                if (val < 0) {
                    return Optional<Uuid>::empty();
                }
                if (nibbleCount < 16u) {
                    ms = (ms << 4u) | ((unsigned long) val);
                } else {
                    ls = (ls << 4u) | ((unsigned long) val);
                }
                ++nibbleCount;
                ++i;
            }
            return Optional<Uuid>(Uuid(ms, ls));
        }

        if (sz == 32u) {
            nibbleCount : unsigned int = 0u;
            i : unsigned int = 0u;
            while (i < 32u) {
                c : char = str.at(startOff + i);
                val : int = decodeHexNibble(c);
                if (val < 0) {
                    return Optional<Uuid>::empty();
                }
                if (nibbleCount < 16u) {
                    ms = (ms << 4u) | ((unsigned long) val);
                } else {
                    ls = (ls << 4u) | ((unsigned long) val);
                }
                ++nibbleCount;
                ++i;
            }
            return Optional<Uuid>(Uuid(ms, ls));
        }

        return Optional<Uuid>::empty();
    }

    /**
     * Parse a UUID string. Returns Nil UUID if parsing fails.
     */
    public static fromString(str: const String&) : Uuid {
        opt : Optional<Uuid> = tryParse(str);
        if (opt.hasValue()) {
            return opt.get();
        }
        return nil();
    }

    /** Parse constructor. */
    Uuid(str: const String&) : _mostSigBits(0uL), _leastSigBits(0uL) {
        opt : Optional<Uuid> = tryParse(str);
        if (opt.hasValue()) {
            u : Uuid = opt.get();
            _mostSigBits = u._mostSigBits;
            _leastSigBits = u._leastSigBits;
        }
    }

    // ── Comparison, Operators & Hash ─────────────────────────────────────────

    /** Equality operator. */
    const operator ==(other: const Uuid&) : bool {
        return _mostSigBits == other._mostSigBits && _leastSigBits == other._leastSigBits;
    }

    /** Inequality operator. */
    const operator !=(other: const Uuid&) : bool {
        return _mostSigBits != other._mostSigBits || _leastSigBits != other._leastSigBits;
    }

    /** Less-than operator (unsigned 128-bit lexicographical comparison). */
    const operator <(other: const Uuid&) : bool {
        if (_mostSigBits < other._mostSigBits) return true;
        if (_mostSigBits > other._mostSigBits) return false;
        return _leastSigBits < other._leastSigBits;
    }

    /** Less-than-or-equal operator. */
    const operator <=(other: const Uuid&) : bool {
        if (_mostSigBits < other._mostSigBits) return true;
        if (_mostSigBits > other._mostSigBits) return false;
        return _leastSigBits <= other._leastSigBits;
    }

    /** Greater-than operator. */
    const operator >(other: const Uuid&) : bool {
        if (_mostSigBits > other._mostSigBits) return true;
        if (_mostSigBits < other._mostSigBits) return false;
        return _leastSigBits > other._leastSigBits;
    }

    /** Greater-than-or-equal operator. */
    const operator >=(other: const Uuid&) : bool {
        if (_mostSigBits > other._mostSigBits) return true;
        if (_mostSigBits < other._mostSigBits) return false;
        return _leastSigBits >= other._leastSigBits;
    }

    /**
     * Three-way comparison (-1 if this < other, 0 if equal, 1 if this > other).
     */
    const compareTo(other: const Uuid&) : int {
        if (_mostSigBits < other._mostSigBits) return -1;
        if (_mostSigBits > other._mostSigBits) return 1;
        if (_leastSigBits < other._leastSigBits) return -1;
        if (_leastSigBits > other._leastSigBits) return 1;
        return 0;
    }

    /**
     * Compute a 64-bit hash code suitable for hash tables.
     */
    const hashCode() : unsigned long {
        h : unsigned long = _mostSigBits ^ _leastSigBits;
        h = (h ^ (h >> 30u)) * 0xbf58476d1ce4e5b9uL;
        h = (h ^ (h >> 27u)) * 0x94d049bb133111ebuL;
        h = h ^ (h >> 31u);
        return h;
    }
}
