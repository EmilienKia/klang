/*
 * K Language generalized symmetric cipher integration tests
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

#include <catch2/catch_all.hpp>

#include "crypto_test_helpers.hpp"

#include <openssl/err.h>
#include <openssl/evp.h>

#include <string>

namespace {

constexpr auto prelude = R"K(
    module __crypto_generalized_symmetric_test__;
    import k::crypto;

    nibble(c: unsigned byte) : unsigned int {
        if (c <= (unsigned byte) '9') return (unsigned int) c - (unsigned int) '0';
        return (unsigned int) c - (unsigned int) 'a' + 10u;
    }

    fromHex(text: const String&) : unsigned byte[]! {
        encoded : unsigned byte[]! = text.toUtf8();
        length : unsigned int = (encoded.size - 1u) / 2u;
        bytes : unsigned byte[]! = new unsigned byte[length];
        i : unsigned int = 0u;
        while (i < length) {
            bytes[i] = (unsigned byte) ((nibble(encoded[2u * i]) << 4u) |
                                       nibble(encoded[2u * i + 1u]));
            ++i;
        }
        delete encoded;
        return bytes;
    }

    hexDigit(n: unsigned int) : byte {
        if (n < 10u) return (byte) ((unsigned int) '0' + n);
        return (byte) ((unsigned int) 'a' + n - 10u);
    }

    bytesToHex(bytes: const unsigned byte[]) : String {
        hex : unsigned byte[]! = new unsigned byte[bytes.size * 2u + 1u];
        i : unsigned int = 0u;
        while (i < bytes.size) {
            value : unsigned int = (unsigned int) bytes[i];
            hex[2u * i] = (unsigned byte) hexDigit(value >> 4u);
            hex[2u * i + 1u] = (unsigned byte) hexDigit(value & 15u);
            ++i;
        }
        hex[bytes.size * 2u] = (unsigned byte) 0;
        result : String(hex);
        delete hex;
        return result;
    }

    copyBytes(source: const unsigned byte[], target: unsigned byte[],
              offset: unsigned int) {
        i : unsigned int = 0u;
        while (i < source.size) {
            target[offset + i] = source[i];
            ++i;
        }
    }
)K";

} // namespace

TEST_CASE("Generalized symmetric descriptors select the requested cipher",
          "[crypto][symmetric][descriptor]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                ctr : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 128u, k::crypto::SymmetricMode::CTR);
                if (!ctr) return 1;
                if (ctr.algorithm() != k::crypto::SymmetricAlgorithm::AES ||
                    ctr.mode() != k::crypto::SymmetricMode::CTR ||
                    ctr.keyBits() != 128u || ctr.keySize() != 16u ||
                    ctr.nonceSize() != 16u || ctr.tagSize() != 0u ||
                    ctr.padding() != k::crypto::SymmetricPadding::None ||
                    ctr.authenticated()) return 2;
                byName : k::crypto::SymmetricCipher! =
                    k::crypto::SymmetricCipher::fromName(String("AES-128-CTR"));
                if (!byName || byName.algorithm() != k::crypto::SymmetricAlgorithm::AES ||
                    byName.mode() != k::crypto::SymmetricMode::CTR ||
                    byName.keyBits() != 128u) return 3;
                unknown : k::crypto::SymmetricCipher! =
                    k::crypto::SymmetricCipher::fromName(String("AES-128-NOT-A-MODE"));
                if (unknown) return 4;
                invalid : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 128u,
                    k::crypto::SymmetricMode::Poly1305);
                if (invalid) return 5;
                invalid = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 129u, k::crypto::SymmetricMode::CTR);
                if (invalid) return 6;

                gcm : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 128u, k::crypto::SymmetricMode::GCM);
                if (!gcm || !gcm.authenticated() || gcm.tagSize() == 0u) return 7;

                xts : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 128u, k::crypto::SymmetricMode::XTS);
                if (xts) {
                    if (xts.algorithm() != k::crypto::SymmetricAlgorithm::AES ||
                        xts.mode() != k::crypto::SymmetricMode::XTS ||
                        xts.keyBits() != 128u || xts.keySize() != 32u ||
                        xts.authenticated()) return 8;
                    key : k::crypto::SymmetricKey! = xts.generateKey();
                    if (!key || key.keySize() != 32u) return 9;
                    shortKey : unsigned byte[]! = new unsigned byte[16u];
                    rejected : bool = false;
                    try {
                        wrong : k::crypto::SymmetricKey! = xts.importKey(shortKey);
                    } catch (e: k::crypto::CryptoInvalidInputException&) {
                        rejected = true;
                    }
                    if (!rejected) return 10;
                }

                camellia : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::Camellia, 128u,
                    k::crypto::SymmetricMode::CTR);
                if (camellia) {
                    if (camellia.algorithm() != k::crypto::SymmetricAlgorithm::Camellia ||
                        camellia.mode() != k::crypto::SymmetricMode::CTR ||
                        camellia.keySize() != 16u || camellia.authenticated()) return 11;
                    namedCamellia : k::crypto::SymmetricCipher! =
                        k::crypto::SymmetricCipher::fromName(String("CAMELLIA-128-CTR"));
                    if (!namedCamellia ||
                        namedCamellia.algorithm() != k::crypto::SymmetricAlgorithm::Camellia)
                        return 12;
                    foreignKey : k::crypto::SymmetricKey! = camellia.generateKey();
                    nonce : unsigned byte[]! = ctr.randomNonce();
                    rejected : bool = false;
                    try {
                        wrongSession : k::crypto::EncryptSession! = ctr.encrypt(foreignKey, nonce);
                    } catch (e: k::crypto::CryptoInvalidInputException&) {
                        rejected = true;
                    }
                    if (!rejected) return 13;
                }
                return 0;
            } catch (e: k::crypto::CryptoException&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("AES CBC padding and CTR direct sessions preserve final and incremental output",
          "[crypto][symmetric][session]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                raw : unsigned byte[]! = fromHex(String("2b7e151628aed2a6abf7158809cf4f3c"));
                plain : unsigned byte[]! = fromHex(String("6bc1bee22e409f96e93d7e117393172a"));
                iv : unsigned byte[]! = fromHex(String("000102030405060708090a0b0c0d0e0f"));
                cbc : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 128u, k::crypto::SymmetricMode::CBC,
                    16u, 0u, k::crypto::SymmetricPadding::PKCS7);
                if (!cbc || cbc.authenticated() ||
                    cbc.padding() != k::crypto::SymmetricPadding::PKCS7) return 1;
                key : k::crypto::SymmetricKey! = cbc.importKey(raw);
                enc : k::crypto::EncryptSession! = cbc.encrypt(key, iv);
                signedPlain : byte[]! = new byte[5u];
                i : unsigned int = 0u;
                while (i < 5u) {
                    signedPlain[i] = (byte) plain[i];
                    ++i;
                }
                target : byte[]! = new byte[32u];
                target[1u] = (byte) 90;
                if (enc.updateTo(signedPlain, 0u, 5u, target, 1u) != 0u ||
                    target[1u] != (byte) 90) return 2;
                if (enc.updateOutputCapacity(11u) < 16u) return 3;
                second : unsigned byte[]! = enc.update(plain, 5u, 11u);
                if (second.size != 16u ||
                    bytesToHex(second) != String("7649abac8119b246cee98e9b12e9197d")) return 4;
                enc.finish();
                ciphertext : unsigned byte[]! = new unsigned byte[32u];
                copyBytes(second, ciphertext, 0u);
                if (enc.readFinal(ciphertext, 16u, 16u) != 16u) return 5;
                if (enc.readFinal(ciphertext, 0u, 1u) != 0u) return 6;

                dec : k::crypto::DecryptSession! = cbc.decrypt(
                    key, iv, k::crypto::DecryptionStaging::Memory);
                recovered : unsigned byte[]! = new unsigned byte[32u];
                dec.update(ciphertext, 0u, 16u);
                if (dec.read(recovered, 0u, 32u) != 0u) return 7;
                dec.update(ciphertext, 16u, 16u);
                if (dec.read(recovered, 0u, 32u) != 16u) return 8;
                dec.finalize();
                if (dec.read(recovered, 16u, 16u) != 0u) return 9;
                i = 0u;
                while (i < 16u) {
                    if (recovered[i] != plain[i]) return 10;
                    ++i;
                }

                ctr : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 128u, k::crypto::SymmetricMode::CTR);
                if (!ctr) return 11;
                counter : unsigned byte[]! =
                    fromHex(String("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff"));
                ctrKey : k::crypto::SymmetricKey! = ctr.importKey(raw);
                ctrEnc : k::crypto::EncryptSession! = ctr.encrypt(ctrKey, counter);
                a : unsigned byte[]! = ctrEnc.update(plain, 0u, 3u);
                b : unsigned byte[]! = ctrEnc.update(plain, 3u, 13u);
                if (a.size != 3u || b.size != 13u) return 12;
                ctrText : unsigned byte[]! = new unsigned byte[16u];
                copyBytes(a, ctrText, 0u);
                copyBytes(b, ctrText, 3u);
                if (bytesToHex(ctrText) != String("874d6191b620e3261bef6864990db6ce")) return 13;
                ctrEnc.finish();
                if (ctrEnc.readFinal(recovered, 0u, 16u) != 0u) return 14;
                ctrDec : k::crypto::DecryptSession! = ctr.decrypt(
                    ctrKey, counter, k::crypto::DecryptionStaging::Memory);
                ctrDec.update(ctrText, 0u, 3u);
                if (ctrDec.read(recovered, 0u, 3u) != 3u) return 15;
                ctrDec.update(ctrText, 3u, 13u);
                if (ctrDec.read(recovered, 3u, 13u) != 13u) return 16;
                ctrDec.finalize();
                i = 0u;
                while (i < 16u) {
                    if (recovered[i] != plain[i]) return 17;
                    ++i;
                }
                return 0;
            } catch (e: k::crypto::CryptoException&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("AES XTS direct sessions support incremental and staged providers",
          "[crypto][symmetric][xts]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                cipher : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 128u, k::crypto::SymmetricMode::XTS);
                if (!cipher) return 77;
                raw : unsigned byte[]! = fromHex(
                    String("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"));
                nonce : unsigned byte[]! = fromHex(String("000102030405060708090a0b0c0d0e0f"));
                plain : unsigned byte[]! = new unsigned byte[47u];
                i : unsigned int = 0u;
                while (i < plain.size) {
                    plain[i] = (unsigned byte) i;
                    ++i;
                }
                key : k::crypto::SymmetricKey! = cipher.importKey(raw);
                enc : k::crypto::EncryptSession! = cipher.encrypt(key, nonce);
                encryptIncremental : bool = enc.updatesProduceOutput();
                first : unsigned byte[]! = enc.update(plain, 0u, 7u);
                second : unsigned byte[]! = enc.update(plain, 7u, 25u);
                third : unsigned byte[]! = enc.update(plain, 32u, 15u);
                produced : unsigned int = first.size + second.size + third.size;
                if (produced > 47u || (encryptIncremental && produced < 16u) ||
                    (!encryptIncremental && produced != 0u)) return 1;
                ciphertext : unsigned byte[]! = new unsigned byte[47u];
                copyBytes(first, ciphertext, 0u);
                copyBytes(second, ciphertext, first.size);
                copyBytes(third, ciphertext, first.size + second.size);
                enc.finish();
                finalCount : unsigned int = enc.readFinal(ciphertext, produced, 47u - produced);
                if (produced + finalCount != 47u) return 2;

                dec : k::crypto::DecryptSession! = cipher.decrypt(
                    key, nonce, k::crypto::DecryptionStaging::Memory);
                decryptIncremental : bool = dec.updatesProduceOutput();
                recovered : unsigned byte[]! = new unsigned byte[47u];
                dec.update(ciphertext, 0u, 32u);
                count : unsigned int = dec.read(recovered, 0u, 47u);
                if (count > 32u || (decryptIncremental && count < 16u) ||
                    (!decryptIncremental && count != 0u)) return 3;
                dec.update(ciphertext, 32u, 15u);
                next : unsigned int = dec.read(recovered, count, 47u - count);
                if (!decryptIncremental && next != 0u) return 6;
                count += next;
                dec.finalize();
                count += dec.read(recovered, count, 47u - count);
                if (count != 47u) return 4;
                i = 0u;
                while (i < plain.size) {
                    if (recovered[i] != plain[i]) return 5;
                    ++i;
                }
                return 0;
            } catch (e: k::crypto::CryptoException&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    const int result = test();
    if (result == 77) SKIP("AES-128-XTS is unavailable from the active provider");
    CHECK(result == 0);
}

TEST_CASE("CTR temporary-file staging supports repeated complete drains",
          "[crypto][symmetric][session][staging]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                cipher : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 128u, k::crypto::SymmetricMode::CTR);
                if (!cipher) return 1;
                key : k::crypto::SymmetricKey! = cipher.generateKey();
                nonce : unsigned byte[]! = cipher.randomNonce();
                plain : unsigned byte[]! = new unsigned byte[257u];
                i : unsigned int = 0u;
                while (i < plain.size) {
                    plain[i] = (unsigned byte) ((i * 37u + 11u) & 255u);
                    ++i;
                }
                enc : k::crypto::EncryptSession! = cipher.encrypt(key, nonce);
                ciphertext : unsigned byte[]! = enc.update(plain, 0u, plain.size);
                if (ciphertext.size != plain.size) return 2;
                enc.finish();
                scratch : unsigned byte[]! = new unsigned byte[1u];
                if (enc.readFinal(scratch, 0u, 1u) != 0u) return 3;

                dec : k::crypto::DecryptSession! = cipher.decrypt(
                    key, nonce, k::crypto::DecryptionStaging::TemporaryFile);
                recovered : unsigned byte[]! = new unsigned byte[257u];
                offset : unsigned int = 0u;
                cycle : unsigned int = 0u;
                while (offset < ciphertext.size) {
                    chunk : unsigned int = cycle % 11u + 1u;
                    if (chunk > ciphertext.size - offset) chunk = ciphertext.size - offset;
                    dec.update(ciphertext, offset, chunk);
                    read : unsigned int = 0u;
                    while (read < chunk) {
                        count : unsigned int = dec.read(recovered, offset + read, chunk - read);
                        if (count == 0u) return 4;
                        read += count;
                    }
                    if (dec.read(scratch, 0u, 1u) != 0u) return 5;
                    offset += chunk;
                    ++cycle;
                }
                dec.finalize();
                if (dec.read(scratch, 0u, 1u) != 0u) return 6;
                i = 0u;
                while (i < plain.size) {
                    if (recovered[i] != plain[i]) return 7;
                    ++i;
                }
                return 0;
            } catch (e: k::crypto::CryptoException&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("AES CCM RFC 3610 vector streams with a length or defers without one",
          "[crypto][symmetric][ccm]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        check(staging: k::crypto::DecryptionStaging, known: bool) : int
                throws(k::crypto::CryptoException) {
            cipher : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                k::crypto::SymmetricAlgorithm::AES, 128u, k::crypto::SymmetricMode::CCM,
                13u, 8u, k::crypto::SymmetricPadding::None);
            if (!cipher || !cipher.authenticated() ||
                cipher.algorithm() != k::crypto::SymmetricAlgorithm::AES ||
                cipher.mode() != k::crypto::SymmetricMode::CCM ||
                cipher.nonceSize() != 13u || cipher.tagSize() != 8u ||
                cipher.maxDataSize() < 23uL || cipher.maxAadSize() < 8uL) return 1;
            raw : unsigned byte[]! = fromHex(String("c0c1c2c3c4c5c6c7c8c9cacbcccdcecf"));
            nonce : unsigned byte[]! = fromHex(String("00000003020100a0a1a2a3a4a5"));
            aad : unsigned byte[]! = fromHex(String("0001020304050607"));
            plain : unsigned byte[]! = fromHex(String("08090a0b0c0d0e0f101112131415161718191a1b1c1d1e"));
            key : k::crypto::SymmetricKey! = cipher.importKey(raw);
            enc : k::crypto::EncryptSession! = cipher.encrypt(key, nonce, staging);
            if (known) enc = cipher.encrypt(key, nonce, staging, 23uL);
            enc.updateAad(aad, 0u, 3u);
            enc.updateAad(aad, 3u, 5u);
            first : unsigned byte[]! = enc.update(plain, 0u, 1u);
            second : unsigned byte[]! = enc.update(plain, 1u, 7u);
            third : unsigned byte[]! = enc.update(plain, 8u, 15u);
            ciphertext : unsigned byte[]! = new unsigned byte[23u];
            if (known) {
                if (first.size != 1u || second.size != 7u || third.size != 15u) return 2;
                copyBytes(first, ciphertext, 0u);
                copyBytes(second, ciphertext, 1u);
                copyBytes(third, ciphertext, 8u);
            } else {
                if (first.size != 0u || second.size != 0u || third.size != 0u) return 3;
            }
            enc.finish();
            if (known) {
                if (enc.readFinal(ciphertext, 0u, 23u) != 0u) return 4;
            } else {
                if (enc.readFinal(ciphertext, 0u, 7u) != 7u ||
                    enc.readFinal(ciphertext, 7u, 16u) != 16u ||
                    enc.readFinal(ciphertext, 0u, 1u) != 0u) return 5;
            }
            tag : unsigned byte[]! = enc.tag();
            if (bytesToHex(ciphertext) !=
                    String("588c979a61c663d2f066d0c2c0f989806d5f6b61dac384") ||
                bytesToHex(tag) != String("17e8d12cfdf926e0")) return 6;

            dec : k::crypto::DecryptSession! = cipher.decrypt(key, nonce, staging);
            if (known) dec = cipher.decrypt(key, nonce, staging, 23uL);
            dec.updateAad(aad, 0u, 8u);
            dec.update(ciphertext, 0u, 2u);
            dec.update(ciphertext, 2u, 9u);
            dec.update(ciphertext, 11u, 12u);
            recovered : unsigned byte[]! = new unsigned byte[23u];
            recovered[0u] = (unsigned byte) 0x5a;
            rejected : bool = false;
            try {
                dec.read(recovered, 0u, 23u);
            } catch (e: k::crypto::CryptoStateException&) {
                rejected = true;
            }
            if (!rejected || recovered[0u] != (unsigned byte) 0x5a) return 7;
            dec.finalize(tag);
            if (dec.read(recovered, 0u, 23u) != 23u) return 8;
            i : unsigned int = 0u;
            while (i < 23u) {
                if (recovered[i] != plain[i]) return 9;
                ++i;
            }

            bad : k::crypto::DecryptSession! = cipher.decrypt(key, nonce, staging);
            if (known) bad = cipher.decrypt(key, nonce, staging, 23uL);
            bad.updateAad(aad, 0u, 8u);
            bad.update(ciphertext, 0u, 23u);
            tag[0u] = (unsigned byte) (tag[0u] ^ (unsigned byte) 1);
            rejected = false;
            try {
                bad.finalize(tag);
            } catch (e: k::crypto::CryptoAuthenticationException&) {
                rejected = true;
            }
            if (!rejected) return 10;
            recovered[0u] = (unsigned byte) 0x5a;
            rejected = false;
            try {
                bad.read(recovered, 0u, 23u);
            } catch (e: k::crypto::CryptoStateException&) {
                rejected = true;
            }
            if (!rejected || recovered[0u] != (unsigned byte) 0x5a) return 11;
            return 0;
        }

        test() : int {
            try {
                result : int = check(k::crypto::DecryptionStaging::Memory, true);
                if (result != 0) return 100 + result;
                result = check(k::crypto::DecryptionStaging::Memory, false);
                if (result != 0) return 200 + result;
                result = check(k::crypto::DecryptionStaging::TemporaryFile, true);
                if (result != 0) return 300 + result;
                result = check(k::crypto::DecryptionStaging::TemporaryFile, false);
                if (result != 0) return 400 + result;
                return 0;
            } catch (e: k::crypto::CryptoException&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("ChaCha20 Poly1305 direct sessions authenticate before exposing plaintext",
          "[crypto][symmetric][chacha20]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        check(cipher: k::crypto::SymmetricCipher&,
              staging: k::crypto::DecryptionStaging) : int throws(k::crypto::CryptoException) {
            raw : unsigned byte[]! = fromHex(
                String("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"));
            nonce : unsigned byte[]! = fromHex(String("000000000000004a00000000"));
            aad : unsigned byte[]! = fromHex(String("01020304050607"));
            plain : unsigned byte[]! = fromHex(String("101112131415161718191a1b1c1d1e1f2021"));
            key : k::crypto::SymmetricKey! = cipher.importKey(raw);
            enc : k::crypto::EncryptSession! = cipher.encrypt(key, nonce);
            enc.updateAad(aad, 0u, 7u);
            first : unsigned byte[]! = enc.update(plain, 0u, 5u);
            second : unsigned byte[]! = enc.update(plain, 5u, 13u);
            if (first.size != 5u || second.size != 13u) return 1;
            ciphertext : unsigned byte[]! = new unsigned byte[18u];
            copyBytes(first, ciphertext, 0u);
            copyBytes(second, ciphertext, 5u);
            enc.finish();
            finalBytes : unsigned byte[]! = new unsigned byte[16u];
            if (enc.readFinal(finalBytes, 0u, 16u) != 0u) return 2;
            tag : unsigned byte[]! = enc.tag();
            if (tag.size != 16u) return 3;

            dec : k::crypto::DecryptSession! = cipher.decrypt(key, nonce, staging);
            dec.updateAad(aad, 0u, 2u);
            dec.updateAad(aad, 2u, 5u);
            dec.update(ciphertext, 0u, 11u);
            dec.update(ciphertext, 11u, 7u);
            recovered : unsigned byte[]! = new unsigned byte[18u];
            recovered[0u] = (unsigned byte) 0x5a;
            rejected : bool = false;
            try {
                dec.read(recovered, 0u, 18u);
            } catch (e: k::crypto::CryptoStateException&) {
                rejected = true;
            }
            if (!rejected || recovered[0u] != (unsigned byte) 0x5a) return 4;
            dec.finalize(tag);
            if (dec.read(recovered, 0u, 18u) != 18u) return 5;
            i : unsigned int = 0u;
            while (i < 18u) {
                if (recovered[i] != plain[i]) return 6;
                ++i;
            }

            bad : k::crypto::DecryptSession! = cipher.decrypt(key, nonce, staging);
            bad.updateAad(aad, 0u, 7u);
            bad.update(ciphertext, 0u, 18u);
            tag[0u] = (unsigned byte) (tag[0u] ^ (unsigned byte) 1);
            rejected = false;
            try {
                bad.finalize(tag);
            } catch (e: k::crypto::CryptoAuthenticationException&) {
                rejected = true;
            }
            if (!rejected) return 7;
            recovered[0u] = (unsigned byte) 0x5a;
            rejected = false;
            try {
                bad.read(recovered, 0u, 18u);
            } catch (e: k::crypto::CryptoStateException&) {
                rejected = true;
            }
            if (!rejected || recovered[0u] != (unsigned byte) 0x5a) return 8;
            return 0;
        }

        test() : int {
            try {
                cipher : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::ChaCha20, 256u,
                    k::crypto::SymmetricMode::Poly1305);
                if (!cipher) return 77;
                if (cipher.algorithm() != k::crypto::SymmetricAlgorithm::ChaCha20 ||
                    cipher.mode() != k::crypto::SymmetricMode::Poly1305 ||
                    cipher.keyBits() != 256u || cipher.keySize() != 32u ||
                    cipher.nonceSize() != 12u || cipher.tagSize() != 16u ||
                    !cipher.authenticated()) return 1;
                named : k::crypto::SymmetricCipher! =
                    k::crypto::SymmetricCipher::fromName(String("CHACHA20-POLY1305"));
                if (!named || named.algorithm() != k::crypto::SymmetricAlgorithm::ChaCha20 ||
                    named.mode() != k::crypto::SymmetricMode::Poly1305) return 2;
                aes : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 256u, k::crypto::SymmetricMode::CTR);
                if (!aes) return 3;
                foreignKey : k::crypto::SymmetricKey! = cipher.generateKey();
                nonce : unsigned byte[]! = aes.randomNonce();
                rejected : bool = false;
                try {
                    wrongSession : k::crypto::EncryptSession! = aes.encrypt(foreignKey, nonce);
                } catch (e: k::crypto::CryptoInvalidInputException&) {
                    rejected = true;
                }
                if (!rejected) return 4;
                result : int = check(cipher, k::crypto::DecryptionStaging::Memory);
                if (result != 0) return 100 + result;
                result = check(cipher, k::crypto::DecryptionStaging::TemporaryFile);
                if (result != 0) return 200 + result;
                return 0;
            } catch (e: k::crypto::CryptoException&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    const int result = test();
    if (result == 77) SKIP("ChaCha20-Poly1305 is unavailable from the active provider");
    CHECK(result == 0);
}

TEST_CASE("AES OCB RFC 7253 vector with incremental AAD",
          "[crypto][symmetric][ocb]") {
    EVP_CIPHER* providerCipher = EVP_CIPHER_fetch(nullptr, "AES-128-OCB", nullptr);
    if (!providerCipher) {
        ERR_clear_error();
        SKIP("AES-128-OCB is unavailable from the active provider");
    }
    EVP_CIPHER_free(providerCipher);

    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                cipher : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 128u, k::crypto::SymmetricMode::OCB,
                    12u, 16u, k::crypto::SymmetricPadding::None);
                if (!cipher || !cipher.authenticated()) return 1;
                raw : unsigned byte[]! = fromHex(String("000102030405060708090a0b0c0d0e0f"));
                nonce : unsigned byte[]! = fromHex(String("bbaa99887766554433221107"));
                plain : unsigned byte[]! =
                    fromHex(String("000102030405060708090a0b0c0d0e0f1011121314151617"));
                aad : unsigned byte[]! =
                    fromHex(String("000102030405060708090a0b0c0d0e0f1011121314151617"));
                key : k::crypto::SymmetricKey! = cipher.importKey(raw);
                enc : k::crypto::EncryptSession! = cipher.encrypt(key, nonce);
                enc.updateAad(aad, 0u, 3u);
                enc.updateAad(aad, 3u, 17u);
                enc.updateAad(aad, 20u, 4u);
                first : unsigned byte[]! = enc.update(plain, 0u, 5u);
                second : unsigned byte[]! = enc.update(plain, 5u, 11u);
                third : unsigned byte[]! = enc.update(plain, 16u, 8u);
                produced : unsigned int = first.size + second.size + third.size;
                if (produced > 24u) return 2;
                ciphertext : unsigned byte[]! = new unsigned byte[24u];
                copyBytes(first, ciphertext, 0u);
                copyBytes(second, ciphertext, first.size);
                copyBytes(third, ciphertext, first.size + second.size);
                enc.finish();
                produced += enc.readFinal(ciphertext, produced, 24u - produced);
                if (produced != 24u ||
                    bytesToHex(ciphertext) != String("1ca2207308c87c010756104d8840ce1952f09673a448a122"))
                    return 3;
                tag : unsigned byte[]! = enc.tag();
                if (bytesToHex(tag) != String("c92c62241051f57356d7f3c90bb0e07f")) return 4;

                dec : k::crypto::DecryptSession! = cipher.decrypt(
                    key, nonce, k::crypto::DecryptionStaging::Memory);
                dec.updateAad(aad, 0u, 7u);
                dec.updateAad(aad, 7u, 11u);
                dec.updateAad(aad, 18u, 6u);
                dec.update(ciphertext, 0u, 3u);
                dec.update(ciphertext, 3u, 10u);
                dec.update(ciphertext, 13u, 11u);
                recovered : unsigned byte[]! = new unsigned byte[24u];
                recovered[0u] = (unsigned byte) 0x5a;
                rejected : bool = false;
                try {
                    dec.read(recovered, 0u, 24u);
                } catch (e: k::crypto::CryptoStateException&) {
                    rejected = true;
                }
                if (!rejected || recovered[0u] != (unsigned byte) 0x5a) return 5;
                dec.finalize(tag);
                if (dec.read(recovered, 0u, 24u) != 24u ||
                    bytesToHex(recovered) != bytesToHex(plain)) return 6;
                return 0;
            } catch (e: k::crypto::CryptoException&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Generalized families and modes follow provider availability",
          "[crypto][symmetric][descriptor][provider]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test(index: int, available: int) : int {
            try {
                cipher : k::crypto::SymmetricCipher! = null;
                named : k::crypto::SymmetricCipher! = null;
                algorithm : k::crypto::SymmetricAlgorithm = k::crypto::SymmetricAlgorithm::AES;
                mode : k::crypto::SymmetricMode = k::crypto::SymmetricMode::CBC;
                if (index == 0) {
                    algorithm = k::crypto::SymmetricAlgorithm::ARIA;
                    mode = k::crypto::SymmetricMode::CBC;
                    cipher = k::crypto::SymmetricCipher::create(
                        algorithm, 128u, mode, 16u, 0u, k::crypto::SymmetricPadding::PKCS7);
                    named = k::crypto::SymmetricCipher::fromName(String("ARIA-128-CBC"));
                } else if (index == 1) {
                    algorithm = k::crypto::SymmetricAlgorithm::Camellia;
                    mode = k::crypto::SymmetricMode::CTR;
                    cipher = k::crypto::SymmetricCipher::create(algorithm, 128u, mode);
                    named = k::crypto::SymmetricCipher::fromName(String("CAMELLIA-128-CTR"));
                } else if (index == 2) {
                    algorithm = k::crypto::SymmetricAlgorithm::SM4;
                    mode = k::crypto::SymmetricMode::OFB;
                    cipher = k::crypto::SymmetricCipher::create(algorithm, 128u, mode);
                    named = k::crypto::SymmetricCipher::fromName(String("SM4-OFB"));
                } else if (index == 3) {
                    mode = k::crypto::SymmetricMode::OCB;
                    cipher = k::crypto::SymmetricCipher::create(
                        algorithm, 128u, mode, 12u, 16u, k::crypto::SymmetricPadding::None);
                    named = k::crypto::SymmetricCipher::fromName(String("AES-128-OCB"));
                } else if (index == 4) {
                    mode = k::crypto::SymmetricMode::CFB1;
                    cipher = k::crypto::SymmetricCipher::create(algorithm, 128u, mode);
                    named = k::crypto::SymmetricCipher::fromName(String("AES-128-CFB1"));
                } else if (index == 5) {
                    mode = k::crypto::SymmetricMode::CFB8;
                    cipher = k::crypto::SymmetricCipher::create(algorithm, 128u, mode);
                    named = k::crypto::SymmetricCipher::fromName(String("AES-128-CFB8"));
                } else {
                    return 1;
                }
                unsupported : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::SM4, 128u,
                    k::crypto::SymmetricMode::Poly1305);
                if (unsupported) return 2;
                unsupported = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 129u,
                    k::crypto::SymmetricMode::CFB1);
                if (unsupported) return 3;
                if (available == 0) {
                    if (cipher || named) return 4;
                    return 0;
                }
                if (!cipher || !named) return 5;
                if (cipher.algorithm() != algorithm || cipher.mode() != mode ||
                    cipher.keyBits() != 128u || cipher.keySize() != 16u ||
                    named.algorithm() != algorithm || named.mode() != mode ||
                    named.keyBits() != 128u) return 6;
                auth : bool = index == 3;
                if (cipher.authenticated() != auth) return 7;

                key : k::crypto::SymmetricKey! = cipher.generateKey();
                nonce : unsigned byte[]! = cipher.randomNonce();
                plain : unsigned byte[]! = new unsigned byte[23u];
                aad : unsigned byte[]! = new unsigned byte[22u];
                i : unsigned int = 0u;
                while (i < plain.size) {
                    plain[i] = (unsigned byte) (i + 33u);
                    ++i;
                }
                i = 0u;
                while (i < aad.size) {
                    aad[i] = (unsigned byte) (i + 1u);
                    ++i;
                }
                enc : k::crypto::EncryptSession! = cipher.encrypt(key, nonce);
                if (auth) {
                    enc.updateAad(aad, 0u, 3u);
                    enc.updateAad(aad, 3u, 17u);
                    enc.updateAad(aad, 20u, 2u);
                }
                first : unsigned byte[]! = enc.update(plain, 0u, 5u);
                second : unsigned byte[]! = enc.update(plain, 5u, 11u);
                third : unsigned byte[]! = enc.update(plain, 16u, 7u);
                produced : unsigned int = first.size + second.size + third.size;
                if (produced > 64u) return 8;
                ciphertext : unsigned byte[]! = new unsigned byte[64u];
                copyBytes(first, ciphertext, 0u);
                copyBytes(second, ciphertext, first.size);
                copyBytes(third, ciphertext, first.size + second.size);
                enc.finish();
                produced += enc.readFinal(ciphertext, produced, 64u - produced);
                if ((index == 0 && produced != 32u) || (index != 0 && produced != 23u))
                    return 9;
                tag : unsigned byte[]! = enc.tag();
                if ((auth && tag.size != 16u) || (!auth && tag.size != 0u)) return 10;

                dec : k::crypto::DecryptSession! = cipher.decrypt(
                    key, nonce, k::crypto::DecryptionStaging::Memory);
                if (auth) {
                    dec.updateAad(aad, 0u, 3u);
                    dec.updateAad(aad, 3u, 17u);
                    dec.updateAad(aad, 20u, 2u);
                }
                dec.update(ciphertext, 0u, 5u);
                dec.update(ciphertext, 5u, produced - 5u);
                if (auth) {
                    dec.finalize(tag);
                } else {
                    dec.finalize();
                }
                recovered : unsigned byte[]! = new unsigned byte[23u];
                count : unsigned int = 0u;
                while (count < recovered.size) {
                    read : unsigned int = dec.read(recovered, count, recovered.size - count);
                    if (read == 0u) return 11;
                    count += read;
                }
                i = 0u;
                while (i < plain.size) {
                    if (recovered[i] != plain[i]) return 12;
                    ++i;
                }
                if (auth) {
                    bad : k::crypto::DecryptSession! = cipher.decrypt(
                        key, nonce, k::crypto::DecryptionStaging::Memory);
                    bad.updateAad(aad, 0u, 3u);
                    bad.updateAad(aad, 3u, 17u);
                    bad.updateAad(aad, 20u, 2u);
                    bad.update(ciphertext, 0u, produced);
                    tag[0u] = (unsigned byte) (tag[0u] ^ (unsigned byte) 1);
                    rejected : bool = false;
                    try {
                        bad.finalize(tag);
                    } catch (e: k::crypto::CryptoAuthenticationException&) {
                        rejected = true;
                    }
                    if (!rejected) return 13;
                }
                return 0;
            } catch (e: k::crypto::CryptoException&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)(int, int)>("test");
    REQUIRE(test);

    const char* names[] = {
        "ARIA-128-CBC", "CAMELLIA-128-CTR", "SM4-OFB",
        "AES-128-OCB", "AES-128-CFB1", "AES-128-CFB8"
    };
    for (int i = 0; i < 6; ++i) {
        INFO("Cipher: " << names[i]);
        EVP_CIPHER* cipher = EVP_CIPHER_fetch(nullptr, names[i], nullptr);
        const int available = cipher != nullptr ? 1 : 0;
        EVP_CIPHER_free(cipher);
        ERR_clear_error();
        INFO("Provider available: " << available);
        CHECK(test(i, available) == 0);
    }
}
