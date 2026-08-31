/*
 * K Language standard library — MD5 Message-Digest Algorithm (RFC 1321)
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

namespace crypto {

/**
 * MD5 Message-Digest Algorithm implementation (RFC 1321).
 *
 * Computes a 128-bit (16-byte) cryptographic hash.
 */
public final class Md5 {
private:
    _state : unsigned int[4];
    _count : unsigned long;
    _buffer : byte[64];

    static rotl(x: unsigned int, n: unsigned int) : unsigned int {
        return (x << n) | (x >> (32u - n));
    }

    static ff(a: unsigned int, b: unsigned int, c: unsigned int, d: unsigned int, x: unsigned int, s: unsigned int, ac: unsigned int) : unsigned int {
        return b + rotl(a + ((b & c) | ((~b) & d)) + x + ac, s);
    }

    static gg(a: unsigned int, b: unsigned int, c: unsigned int, d: unsigned int, x: unsigned int, s: unsigned int, ac: unsigned int) : unsigned int {
        return b + rotl(a + ((b & d) | (c & (~d))) + x + ac, s);
    }

    static hh(a: unsigned int, b: unsigned int, c: unsigned int, d: unsigned int, x: unsigned int, s: unsigned int, ac: unsigned int) : unsigned int {
        return b + rotl(a + (b ^ c ^ d) + x + ac, s);
    }

    static ii(a: unsigned int, b: unsigned int, c: unsigned int, d: unsigned int, x: unsigned int, s: unsigned int, ac: unsigned int) : unsigned int {
        return b + rotl(a + (c ^ (b | (~d))) + x + ac, s);
    }

    static decodeU32LE(buf: const byte[], offset: unsigned int) : unsigned int {
        b0 : unsigned int = ((unsigned int) buf[offset]) & 0xFFu;
        b1 : unsigned int = ((unsigned int) buf[offset + 1u]) & 0xFFu;
        b2 : unsigned int = ((unsigned int) buf[offset + 2u]) & 0xFFu;
        b3 : unsigned int = ((unsigned int) buf[offset + 3u]) & 0xFFu;
        return b0 | (b1 << 8u) | (b2 << 16u) | (b3 << 24u);
    }

    static encodeU32LE(val: unsigned int, out: byte[], offset: unsigned int) {
        out[offset] = (byte) (val & 0xFFu);
        out[offset + 1u] = (byte) ((val >> 8u) & 0xFFu);
        out[offset + 2u] = (byte) ((val >> 16u) & 0xFFu);
        out[offset + 3u] = (byte) ((val >> 24u) & 0xFFu);
    }

    transform(block: const byte[], offset: unsigned int) {
        a : unsigned int = _state[0];
        b : unsigned int = _state[1];
        c : unsigned int = _state[2];
        d : unsigned int = _state[3];

        x0 : unsigned int = decodeU32LE(block, offset);
        x1 : unsigned int = decodeU32LE(block, offset + 4u);
        x2 : unsigned int = decodeU32LE(block, offset + 8u);
        x3 : unsigned int = decodeU32LE(block, offset + 12u);
        x4 : unsigned int = decodeU32LE(block, offset + 16u);
        x5 : unsigned int = decodeU32LE(block, offset + 20u);
        x6 : unsigned int = decodeU32LE(block, offset + 24u);
        x7 : unsigned int = decodeU32LE(block, offset + 28u);
        x8 : unsigned int = decodeU32LE(block, offset + 32u);
        x9 : unsigned int = decodeU32LE(block, offset + 36u);
        x10 : unsigned int = decodeU32LE(block, offset + 40u);
        x11 : unsigned int = decodeU32LE(block, offset + 44u);
        x12 : unsigned int = decodeU32LE(block, offset + 48u);
        x13 : unsigned int = decodeU32LE(block, offset + 52u);
        x14 : unsigned int = decodeU32LE(block, offset + 56u);
        x15 : unsigned int = decodeU32LE(block, offset + 60u);

        // Round 1
        a = ff(a, b, c, d, x0, 7u, 0xd76aa478u);
        d = ff(d, a, b, c, x1, 12u, 0xe8c7b756u);
        c = ff(c, d, a, b, x2, 17u, 0x242070dbu);
        b = ff(b, c, d, a, x3, 22u, 0xc1bdceeeu);
        a = ff(a, b, c, d, x4, 7u, 0xf57c0fafu);
        d = ff(d, a, b, c, x5, 12u, 0x4787c62au);
        c = ff(c, d, a, b, x6, 17u, 0xa8304613u);
        b = ff(b, c, d, a, x7, 22u, 0xfd469501u);
        a = ff(a, b, c, d, x8, 7u, 0x698098d8u);
        d = ff(d, a, b, c, x9, 12u, 0x8b44f7afu);
        c = ff(c, d, a, b, x10, 17u, 0xffff5bb1u);
        b = ff(b, c, d, a, x11, 22u, 0x895cd7beu);
        a = ff(a, b, c, d, x12, 7u, 0x6b901122u);
        d = ff(d, a, b, c, x13, 12u, 0xfd987193u);
        c = ff(c, d, a, b, x14, 17u, 0xa679438eu);
        b = ff(b, c, d, a, x15, 22u, 0x49b40821u);

        // Round 2
        a = gg(a, b, c, d, x1, 5u, 0xf61e2562u);
        d = gg(d, a, b, c, x6, 9u, 0xc040b340u);
        c = gg(c, d, a, b, x11, 14u, 0x265e5a51u);
        b = gg(b, c, d, a, x0, 20u, 0xe9b6c7aau);
        a = gg(a, b, c, d, x5, 5u, 0xd62f105du);
        d = gg(d, a, b, c, x10, 9u, 0x02441453u);
        c = gg(c, d, a, b, x15, 14u, 0xd8a1e681u);
        b = gg(b, c, d, a, x4, 20u, 0xe7d3fbc8u);
        a = gg(a, b, c, d, x9, 5u, 0x21e1cde6u);
        d = gg(d, a, b, c, x14, 9u, 0xc33707d6u);
        c = gg(c, d, a, b, x3, 14u, 0xf4d50d87u);
        b = gg(b, c, d, a, x8, 20u, 0x455a14edu);
        a = gg(a, b, c, d, x13, 5u, 0xa9e3e905u);
        d = gg(d, a, b, c, x2, 9u, 0xfcefa3f8u);
        c = gg(c, d, a, b, x7, 14u, 0x676f02d9u);
        b = gg(b, c, d, a, x12, 20u, 0x8d2a4c8au);

        // Round 3
        a = hh(a, b, c, d, x5, 4u, 0xfffa3942u);
        d = hh(d, a, b, c, x8, 11u, 0x8771f681u);
        c = hh(c, d, a, b, x11, 16u, 0x6d9d6122u);
        b = hh(b, c, d, a, x14, 23u, 0xfde5380cu);
        a = hh(a, b, c, d, x1, 4u, 0xa4beea44u);
        d = hh(d, a, b, c, x4, 11u, 0x4bdecfa9u);
        c = hh(c, d, a, b, x7, 16u, 0xf6bb4b60u);
        b = hh(b, c, d, a, x10, 23u, 0xbebfbc70u);
        a = hh(a, b, c, d, x13, 4u, 0x289b7ec6u);
        d = hh(d, a, b, c, x0, 11u, 0xeaa127fau);
        c = hh(c, d, a, b, x3, 16u, 0xd4ef3085u);
        b = hh(b, c, d, a, x6, 23u, 0x04881d05u);
        a = hh(a, b, c, d, x9, 4u, 0xd9d4d039u);
        d = hh(d, a, b, c, x12, 11u, 0xe6db99e5u);
        c = hh(c, d, a, b, x15, 16u, 0x1fa27cf8u);
        b = hh(b, c, d, a, x2, 23u, 0xc4ac5665u);

        // Round 4
        a = ii(a, b, c, d, x0, 6u, 0xf4292244u);
        d = ii(d, a, b, c, x7, 10u, 0x432aff97u);
        c = ii(c, d, a, b, x14, 15u, 0xab9423a7u);
        b = ii(b, c, d, a, x5, 21u, 0xfc93a039u);
        a = ii(a, b, c, d, x12, 6u, 0x655b59c3u);
        d = ii(d, a, b, c, x3, 10u, 0x8f0ccc92u);
        c = ii(c, d, a, b, x10, 15u, 0xffeff47du);
        b = ii(b, c, d, a, x1, 21u, 0x85845dd1u);
        a = ii(a, b, c, d, x8, 6u, 0x6fa87e4fu);
        d = ii(d, a, b, c, x15, 10u, 0xfe2ce6e0u);
        c = ii(c, d, a, b, x6, 15u, 0xa3014314u);
        b = ii(b, c, d, a, x13, 21u, 0x4e0811a1u);
        a = ii(a, b, c, d, x4, 6u, 0xf7537e82u);
        d = ii(d, a, b, c, x11, 10u, 0xbd3af235u);
        c = ii(c, d, a, b, x2, 15u, 0x2ad7d2bbu);
        b = ii(b, c, d, a, x9, 21u, 0xeb86d391u);

        _state[0] += a;
        _state[1] += b;
        _state[2] += c;
        _state[3] += d;
    }

public:
    Md5() : _count(0uL) {
        reset();
    }

    reset() {
        _state[0] = 0x67452301u;
        _state[1] = 0xefcdab89u;
        _state[2] = 0x98badcfeu;
        _state[3] = 0x10325476u;
        _count = 0uL;
        i : unsigned int = 0u;
        while (i < 64u) {
            _buffer[i] = (byte) 0;
            ++i;
        }
    }

    update(data: const byte[], offset: unsigned int, length: unsigned int) {
        if (length == 0u) return;

        bufIndex : unsigned int = (unsigned int) (_count % 64uL);
        _count += (unsigned long) length;

        partLen : unsigned int = 64u - bufIndex;
        i : unsigned int = 0u;

        if (length >= partLen) {
            j : unsigned int = 0u;
            while (j < partLen) {
                _buffer[bufIndex + j] = data[offset + j];
                ++j;
            }
            transform(_buffer, 0u);

            i = partLen;
            while (i + 64u <= length) {
                transform(data, offset + i);
                i += 64u;
            }
            bufIndex = 0u;
        }

        while (i < length) {
            _buffer[bufIndex] = data[offset + i];
            ++bufIndex;
            ++i;
        }
    }

    update(data: const byte[]) {
        update(data, 0u, data.size);
    }

    finalize() : byte[]! {
        totalBits : unsigned long = _count * 8uL;
        bufIndex : unsigned int = (unsigned int) (_count % 64uL);
        padLen : unsigned int = (bufIndex < 56u) ? (56u - bufIndex) : (120u - bufIndex);

        pad : byte[]! = new byte[padLen + 8u];
        pad[0] = (byte) 0x80;
        k : unsigned int = 1u;
        while (k < padLen) {
            pad[k] = (byte) 0;
            ++k;
        }

        // 64-bit length in little endian
        b : unsigned int = 0u;
        while (b < 8u) {
            pad[padLen + b] = (byte) ((totalBits >> (b * 8u)) & 0xFFuL);
            ++b;
        }

        update(pad, 0u, padLen + 8u);
        delete pad;

        out : byte[]! = new byte[16u];
        encodeU32LE(_state[0], out, 0u);
        encodeU32LE(_state[1], out, 4u);
        encodeU32LE(_state[2], out, 8u);
        encodeU32LE(_state[3], out, 12u);
        return out;
    }

    /**
     * Compute the 16-byte MD5 digest of the input byte array.
     */
    static digest(data: const byte[]) : byte[]! {
        md5 : Md5;
        md5.update(data);
        return md5.finalize();
    }

    /**
     * Compute the 16-byte MD5 digest of a slice of the input byte array.
     */
    static digest(data: const byte[], offset: unsigned int, length: unsigned int) : byte[]! {
        md5 : Md5;
        md5.update(data, offset, length);
        return md5.finalize();
    }

    /**
     * Compute the 16-byte MD5 digest of two concatenated byte arrays.
     */
    static digestConcat(a: const byte[], b: const byte[]) : byte[]! {
        md5 : Md5;
        md5.update(a);
        md5.update(b);
        return md5.finalize();
    }
}

} // namespace crypto
