/*
 * K Language standard library — SHA-1 Message-Digest Algorithm (RFC 3174)
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
 * SHA-1 Message-Digest Algorithm implementation (RFC 3174 / FIPS 180-1).
 *
 * Computes a 160-bit (20-byte) cryptographic hash.
 */
public final class Sha1 {
private:
    _state : unsigned int[5];
    _count : unsigned long;
    _buffer : byte[64];

    static rotl(x: unsigned int, n: unsigned int) : unsigned int {
        return (x << n) | (x >> (32u - n));
    }

    static decodeU32BE(buf: const byte[], offset: unsigned int) : unsigned int {
        b0 : unsigned int = ((unsigned int) buf[offset]) & 0xFFu;
        b1 : unsigned int = ((unsigned int) buf[offset + 1u]) & 0xFFu;
        b2 : unsigned int = ((unsigned int) buf[offset + 2u]) & 0xFFu;
        b3 : unsigned int = ((unsigned int) buf[offset + 3u]) & 0xFFu;
        return (b0 << 24u) | (b1 << 16u) | (b2 << 8u) | b3;
    }

    static encodeU32BE(val: unsigned int, out: byte[], offset: unsigned int) {
        out[offset] = (byte) ((val >> 24u) & 0xFFu);
        out[offset + 1u] = (byte) ((val >> 16u) & 0xFFu);
        out[offset + 2u] = (byte) ((val >> 8u) & 0xFFu);
        out[offset + 3u] = (byte) (val & 0xFFu);
    }

    transform(block: const byte[], offset: unsigned int) {
        w : unsigned int[80];
        t : unsigned int = 0u;
        while (t < 16u) {
            w[t] = decodeU32BE(block, offset + (t * 4u));
            ++t;
        }
        while (t < 80u) {
            w[t] = rotl(w[t - 3u] ^ w[t - 8u] ^ w[t - 14u] ^ w[t - 16u], 1u);
            ++t;
        }

        a : unsigned int = _state[0];
        b : unsigned int = _state[1];
        c : unsigned int = _state[2];
        d : unsigned int = _state[3];
        e : unsigned int = _state[4];

        t = 0u;
        while (t < 80u) {
            f : unsigned int = 0u;
            k : unsigned int = 0u;
            if (t < 20u) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            } else if (t < 40u) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (t < 60u) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }

            temp : unsigned int = rotl(a, 5u) + f + e + k + w[t];
            e = d;
            d = c;
            c = rotl(b, 30u);
            b = a;
            a = temp;
            ++t;
        }

        _state[0] += a;
        _state[1] += b;
        _state[2] += c;
        _state[3] += d;
        _state[4] += e;
    }

public:
    Sha1() : _count(0uL) {
        reset();
    }

    reset() {
        _state[0] = 0x67452301u;
        _state[1] = 0xEFCDAB89u;
        _state[2] = 0x98BADCFEu;
        _state[3] = 0x10325476u;
        _state[4] = 0xC3D2E1F0u;
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

        // 64-bit length in big endian
        b : unsigned int = 0u;
        while (b < 8u) {
            pad[padLen + b] = (byte) ((totalBits >> ((7u - b) * 8u)) & 0xFFuL);
            ++b;
        }

        update(pad, 0u, padLen + 8u);
        delete pad;

        out : byte[]! = new byte[20u];
        encodeU32BE(_state[0], out, 0u);
        encodeU32BE(_state[1], out, 4u);
        encodeU32BE(_state[2], out, 8u);
        encodeU32BE(_state[3], out, 12u);
        encodeU32BE(_state[4], out, 16u);
        return out;
    }

    /**
     * Compute the 20-byte SHA-1 digest of the input byte array.
     */
    static digest(data: const byte[]) : byte[]! {
        sha : Sha1;
        sha.update(data);
        return sha.finalize();
    }

    /**
     * Compute the 20-byte SHA-1 digest of a slice of the input byte array.
     */
    static digest(data: const byte[], offset: unsigned int, length: unsigned int) : byte[]! {
        sha : Sha1;
        sha.update(data, offset, length);
        return sha.finalize();
    }

    /**
     * Compute the 20-byte SHA-1 digest of two concatenated byte arrays.
     */
    static digestConcat(a: const byte[], b: const byte[]) : byte[]! {
        sha : Sha1;
        sha.update(a);
        sha.update(b);
        return sha.finalize();
    }
}

} // namespace crypto
