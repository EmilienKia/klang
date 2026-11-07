/*
 * K Language crypto standard library tests
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

TEST_CASE("OpenSSL version number is available through k::crypto",
          "[crypto][openssl]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __crypto_openssl_test__;
        import k::crypto;

        test() : int {
            version : unsigned long = k::crypto::openSslVersionNumber();
            return (version == 0uL) ? 1 : 0;
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Message digest SHA256", "[crypto][digest]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __crypto_message_digest_sha256_test__;
        import k::crypto;

        hexDigit(n: unsigned int) : byte {
            value : unsigned int = n & 0x0Fu;
            if (value < 10u) return (byte) (((unsigned int) '0') + value);
            return (byte) (((unsigned int) 'a') + (value - 10u));
        }

        bytesToHex(bytes: const unsigned byte[]) : String {
            hex : unsigned byte[]! = new unsigned byte[bytes.size * 2u + 1u];
            i : unsigned int = 0u;
            while (i < bytes.size) {
                value : unsigned int = ((unsigned int) bytes[i]) & 0xFFu;
                hex[i * 2u] = (unsigned byte) hexDigit(value >> 4u);
                hex[i * 2u + 1u] = (unsigned byte) hexDigit(value);
                ++i;
            }
            hex[bytes.size * 2u] = (unsigned byte) 0;
            result : String(hex);
            delete hex;
            return result;
        }

        digestText(message: const String&) : String throws(k::crypto::MessageDigestException) {
            encoded : unsigned byte[]! = message.toUtf8();
            length : unsigned int = (encoded.size > 0u) ? encoded.size - 1u : 0u;
            bytes : unsigned byte[]! = new unsigned byte[length];
            i : unsigned int = 0u;
            while (i < length) {
                bytes[i] = encoded[i];
                ++i;
            }

            md : k::crypto::MessageDigest! = k::crypto::MessageDigest::SHA256();
            md.update(bytes);
            digest : unsigned byte[]! = md.finalize();
            result : String = bytesToHex(digest);
            delete digest;
            delete bytes;
            delete encoded;
            return result;
        }

        test() : int {
            result : int = 0;
            try {
                if (digestText(String("")) ==
                    String("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")) {
                    result += 1;
                }
                if (digestText(String("abc")) ==
                    String("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) {
                    result += 2;
                }
                if (digestText(String("The quick brown fox jumps over the lazy dog")) ==
                    String("d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592")) {
                    result += 4;
                }
                if (digestText(String("The quick brown fox jumps over the lazy dog.")) ==
                    String("ef537f25c895bfa782526529a9b63d97aa631564d5d789c2b765448c8635fb6c")) {
                    result += 8;
                }
            } catch (e: k::crypto::MessageDigestException&) {
                return -1;
            }
            return result;
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 15);
}

TEST_CASE("Message digest factory vectors", "[crypto][digest]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __crypto_message_digest_factory_test__;
        import k::crypto;

        hexDigit(n: unsigned int) : byte {
            value : unsigned int = n & 0x0Fu;
            if (value < 10u) return (byte) (((unsigned int) '0') + value);
            return (byte) (((unsigned int) 'a') + (value - 10u));
        }

        bytesToHex(bytes: const unsigned byte[]) : String {
            hex : unsigned byte[]! = new unsigned byte[bytes.size * 2u + 1u];
            i : unsigned int = 0u;
            while (i < bytes.size) {
                value : unsigned int = ((unsigned int) bytes[i]) & 0xFFu;
                hex[i * 2u] = (unsigned byte) hexDigit(value >> 4u);
                hex[i * 2u + 1u] = (unsigned byte) hexDigit(value);
                ++i;
            }
            hex[bytes.size * 2u] = (unsigned byte) 0;
            result : String(hex);
            delete hex;
            return result;
        }

        digestEmpty(md: k::crypto::MessageDigest!) : String
                throws(k::crypto::MessageDigestException) {
            empty : unsigned byte[]! = new unsigned byte[0];
            md.update(empty);
            digest : unsigned byte[]! = md.finalize();
            result : String = bytesToHex(digest);
            delete digest;
            delete empty;
            return result;
        }

        test() : int {
            try {
                if (digestEmpty(k::crypto::MessageDigest::MD5()) !=
                    String("d41d8cd98f00b204e9800998ecf8427e")) return 1;
                if (digestEmpty(k::crypto::MessageDigest::SHA1()) !=
                    String("da39a3ee5e6b4b0d3255bfef95601890afd80709")) return 2;
                if (digestEmpty(k::crypto::MessageDigest::SHA224()) !=
                    String("d14a028c2a3a2bc9476102bb288234c415a2b01f828ea62ac5b3e42f")) return 3;
                if (digestEmpty(k::crypto::MessageDigest::SHA384()) !=
                    String("38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b")) return 4;
                if (digestEmpty(k::crypto::MessageDigest::SHA512()) !=
                    String("cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e")) return 5;
                if (digestEmpty(k::crypto::MessageDigest::SHA512_224()) !=
                    String("6ed0dd02806fa89e25de060c19d3ac86cabb87d6a0ddd05c333b84f4")) return 6;
                if (digestEmpty(k::crypto::MessageDigest::SHA512_256()) !=
                    String("c672b8d1ef56ed28ab87c3622c5114069bdd3ad7b8f9737498d0c01ecef0967a")) return 7;
                if (digestEmpty(k::crypto::MessageDigest::SHA3_224()) !=
                    String("6b4e03423667dbb73b6e15454f0eb1abd4597f9a1b078e3f5b5a6bc7")) return 8;
                if (digestEmpty(k::crypto::MessageDigest::SHA3_256()) !=
                    String("a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a")) return 9;
                if (digestEmpty(k::crypto::MessageDigest::SHA3_384()) !=
                    String("0c63a75b845e4f7d01107d852e4c2485c51a50aaaa94fc61995e71bbee983a2ac3713831264adb47fb6bd1e058d5f004")) return 10;
                if (digestEmpty(k::crypto::MessageDigest::SHA3_512()) !=
                    String("a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a615b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26")) return 11;
            } catch (e: k::crypto::MessageDigestException&) {
                return -1;
            }
            return 0;
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Message digest name lookup", "[crypto][digest]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __crypto_message_digest_name_test__;
        import k::crypto;

        hexDigit(n: unsigned int) : byte {
            value : unsigned int = n & 0x0Fu;
            if (value < 10u) return (byte) (((unsigned int) '0') + value);
            return (byte) (((unsigned int) 'a') + (value - 10u));
        }

        bytesToHex(bytes: const unsigned byte[]) : String {
            hex : unsigned byte[]! = new unsigned byte[bytes.size * 2u + 1u];
            i : unsigned int = 0u;
            while (i < bytes.size) {
                value : unsigned int = ((unsigned int) bytes[i]) & 0xFFu;
                hex[i * 2u] = (unsigned byte) hexDigit(value >> 4u);
                hex[i * 2u + 1u] = (unsigned byte) hexDigit(value);
                ++i;
            }
            hex[bytes.size * 2u] = (unsigned byte) 0;
            result : String(hex);
            delete hex;
            return result;
        }

        hasStandardDigest(algorithm: k::crypto::MessageDigestAlgorithms) : bool {
            digest : k::crypto::MessageDigest! =
                k::crypto::MessageDigest::fromName(algorithm);
            return digest != null;
        }

        test() : int {
            try {
                mdByEnum : k::crypto::MessageDigest! =
                    k::crypto::MessageDigest::fromName(
                        k::crypto::MessageDigestAlgorithms::SHA256);
                if (!mdByEnum) return 6;

                enumBytes : unsigned byte[]! = new unsigned byte[3u];
                enumBytes[0u] = (unsigned byte) 'a';
                enumBytes[1u] = (unsigned byte) 'b';
                enumBytes[2u] = (unsigned byte) 'c';
                mdByEnum.update(enumBytes);
                enumDigest : unsigned byte[]! = mdByEnum.finalize();
                enumCorrect : bool = bytesToHex(enumDigest) ==
                    String("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
                delete enumDigest;
                delete enumBytes;
                if (!enumCorrect) return 7;

                if (!hasStandardDigest(k::crypto::MessageDigestAlgorithms::MD5)) return 8;
                if (!hasStandardDigest(k::crypto::MessageDigestAlgorithms::SHA1)) return 9;
                if (!hasStandardDigest(k::crypto::MessageDigestAlgorithms::SHA224)) return 10;
                if (!hasStandardDigest(k::crypto::MessageDigestAlgorithms::SHA256)) return 11;
                if (!hasStandardDigest(k::crypto::MessageDigestAlgorithms::SHA384)) return 12;
                if (!hasStandardDigest(k::crypto::MessageDigestAlgorithms::SHA512)) return 13;
                if (!hasStandardDigest(k::crypto::MessageDigestAlgorithms::SHA512_224)) return 14;
                if (!hasStandardDigest(k::crypto::MessageDigestAlgorithms::SHA512_256)) return 15;
                if (!hasStandardDigest(k::crypto::MessageDigestAlgorithms::SHA3_224)) return 16;
                if (!hasStandardDigest(k::crypto::MessageDigestAlgorithms::SHA3_256)) return 17;
                if (!hasStandardDigest(k::crypto::MessageDigestAlgorithms::SHA3_384)) return 18;
                if (!hasStandardDigest(k::crypto::MessageDigestAlgorithms::SHA3_512)) return 19;

                sha256Name : const k::crypto::MessageDigestAlgorithm! =
                    new String("SHA256");
                md : k::crypto::MessageDigest! =
                    k::crypto::MessageDigest::fromName(sha256Name);
                if (!md) return 1;
                if (sha256Name.size() != 6u) return 4;

                bytes : unsigned byte[]! = new unsigned byte[3u];
                bytes[0u] = (unsigned byte) 'a';
                bytes[1u] = (unsigned byte) 'b';
                bytes[2u] = (unsigned byte) 'c';
                md.update(bytes);
                digest : unsigned byte[]! = md.finalize();
                correct : bool = bytesToHex(digest) ==
                    String("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
                delete digest;
                delete bytes;
                if (!correct) return 2;

                customName : const k::crypto::MessageDigestAlgorithm! =
                    new String("BLAKE2B-512");
                custom : k::crypto::MessageDigest! =
                    k::crypto::MessageDigest::fromName(customName);
                if (!custom) return 19;

                unsupportedName : const k::crypto::MessageDigestAlgorithm! =
                    new String("KLANG-UNSUPPORTED-DIGEST");
                unsupported : k::crypto::MessageDigest! =
                    k::crypto::MessageDigest::fromName(unsupportedName);
                if (unsupported != null) return 3;
                if (unsupportedName.size() != 24u) return 5;
            } catch (e: k::crypto::MessageDigestException&) {
                return -1;
            }
            return 0;
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("MessageDigestOutputStream forwards and hashes bytes",
          "[crypto][digest][output-stream]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __crypto_message_digest_output_stream_test__;
        import k::io::crypto;

        hexDigit(n: unsigned int) : byte {
            value : unsigned int = n & 0x0Fu;
            if (value < 10u) return (byte) (((unsigned int) '0') + value);
            return (byte) (((unsigned int) 'a') + (value - 10u));
        }

        bytesToHex(bytes: const unsigned byte[]) : String {
            hex : unsigned byte[]! = new unsigned byte[bytes.size * 2u + 1u];
            i : unsigned int = 0u;
            while (i < bytes.size) {
                value : unsigned int = ((unsigned int) bytes[i]) & 0xFFu;
                hex[i * 2u] = (unsigned byte) hexDigit(value >> 4u);
                hex[i * 2u + 1u] = (unsigned byte) hexDigit(value);
                ++i;
            }
            hex[bytes.size * 2u] = (unsigned byte) 0;
            result : String(hex);
            delete hex;
            return result;
        }

        test() : int {
            try {
                sink : k::io::ArrayOutputStream<byte>;
                stream : k::io::crypto::MessageDigestOutputStream! =
                    k::io::crypto::MessageDigestOutputStream::SHA256(sink);
                if (stream == null) return 1;

                stream.write((byte) 'a');
                bytes : byte[]! = new byte[4u];
                bytes[0u] = (byte) 'x';
                bytes[1u] = (byte) 'b';
                bytes[2u] = (byte) 'c';
                bytes[3u] = (byte) 'y';
                stream.write(bytes, 1u, 2u);
                stream.close();

                if (sink.size() != 3) return 2;
                copied : byte[]! = sink.toArray();
                if (copied[0] != (byte) 'a' || copied[1] != (byte) 'b' ||
                    copied[2] != (byte) 'c') return 3;

                digest : const unsigned byte[]? = stream.getDigest();
                if (digest == null) return 4;
                if (bytesToHex(digest) !=
                    String("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) {
                    return 5;
                }
                return 0;
            } catch (e: k::crypto::MessageDigestException&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("MessageDigestOutputStream slice, close result, and frozen writes",
          "[crypto][digest][output-stream]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __crypto_message_digest_output_slice_test__;
        import k::io::crypto;

        hexDigit(n: unsigned int) : byte {
            value : unsigned int = n & 0x0Fu;
            if (value < 10u) return (byte) (((unsigned int) '0') + value);
            return (byte) (((unsigned int) 'a') + (value - 10u));
        }

        bytesToHex(bytes: const unsigned byte[]) : String {
            hex : unsigned byte[]! = new unsigned byte[bytes.size * 2u + 1u];
            i : unsigned int = 0u;
            while (i < bytes.size) {
                value : unsigned int = ((unsigned int) bytes[i]) & 0xFFu;
                hex[i * 2u] = (unsigned byte) hexDigit(value >> 4u);
                hex[i * 2u + 1u] = (unsigned byte) hexDigit(value);
                ++i;
            }
            hex[bytes.size * 2u] = (unsigned byte) 0;
            result : String(hex);
            delete hex;
            return result;
        }

        test() : int {
            try {
                sink : k::io::ArrayOutputStream<byte>;
                stream : k::io::crypto::MessageDigestOutputStream! =
                    k::io::crypto::MessageDigestOutputStream::SHA256(sink);
                source : byte[]! = new byte[5u];
                source[0u] = (byte) 'x';
                source[1u] = (byte) 'b';
                source[2u] = (byte) 'c';
                source[3u] = (byte) 'd';
                source[4u] = (byte) 'y';
                stream.write(source, 1u, 3u);
                stream.close();
                first : const unsigned byte[]? = stream.getDigest();
                if (first == null) return 1;
                firstText : String = bytesToHex(first);
                if (firstText !=
                    String("a6b0f90d2ac2b8d1f250c687301aef132049e9016df936680e81fa7bc7d81d70")) {
                    return 2;
                }

                stream.close();
                second : const unsigned byte[]? = stream.getDigest();
                if (second == null || bytesToHex(second) != firstText) return 3;
                if (sink.size() != 3) return 4;

                rejected : bool = false;
                try {
                    stream.write((byte) '!');
                } catch (e: IllegalStateException&) {
                    rejected = true;
                }
                if (!rejected) return 5;

                copied : byte[]! = sink.toArray();
                if (copied[0] != (byte) 'b' || copied[1] != (byte) 'c' ||
                    copied[2] != (byte) 'd') return 6;
                return 0;
            } catch (e: k::crypto::MessageDigestException&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("MessageDigestOutputStream factories mirror MessageDigest",
          "[crypto][digest][output-stream]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __crypto_message_digest_output_factories_test__;
        import k::io::crypto;

        check(stream: k::io::crypto::MessageDigestOutputStream!)
                : bool throws(k::crypto::MessageDigestException) {
            if (stream == null) return false;
            stream.close();
            return stream.getDigest() != null;
        }

        test() : int {
            try {
                sink : k::io::ArrayOutputStream<byte>;
                if (!check(k::io::crypto::MessageDigestOutputStream::MD5(sink))) return 1;
                if (!check(k::io::crypto::MessageDigestOutputStream::SHA1(sink))) return 2;
                if (!check(k::io::crypto::MessageDigestOutputStream::SHA224(sink))) return 3;
                if (!check(k::io::crypto::MessageDigestOutputStream::SHA256(sink))) return 4;
                if (!check(k::io::crypto::MessageDigestOutputStream::SHA384(sink))) return 5;
                if (!check(k::io::crypto::MessageDigestOutputStream::SHA512(sink))) return 6;
                if (!check(k::io::crypto::MessageDigestOutputStream::SHA512_224(sink))) return 7;
                if (!check(k::io::crypto::MessageDigestOutputStream::SHA512_256(sink))) return 8;
                if (!check(k::io::crypto::MessageDigestOutputStream::SHA3_224(sink))) return 9;
                if (!check(k::io::crypto::MessageDigestOutputStream::SHA3_256(sink))) return 10;
                if (!check(k::io::crypto::MessageDigestOutputStream::SHA3_384(sink))) return 11;
                if (!check(k::io::crypto::MessageDigestOutputStream::SHA3_512(sink))) return 12;

                name : const k::crypto::MessageDigestAlgorithm! = new String("SHA256");
                if (!check(k::io::crypto::MessageDigestOutputStream::fromName(name, sink))) return 13;

                unsupportedName : const k::crypto::MessageDigestAlgorithm! =
                    new String("KLANG-UNSUPPORTED-DIGEST");
                unsupported : k::io::crypto::MessageDigestOutputStream! =
                    k::io::crypto::MessageDigestOutputStream::fromName(unsupportedName, sink);
                if (unsupported != null) return 14;
                return 0;
            } catch (e: k::crypto::MessageDigestException&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("MessageDigest const slice update checks bounds",
          "[crypto][digest][slice]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __crypto_message_digest_slice_bounds_test__;
        import k::crypto;

        test() : int {
            try {
                digest : k::crypto::MessageDigest! = k::crypto::MessageDigest::SHA256();
                bytes : unsigned byte[]! = new unsigned byte[2u];
                rejected : bool = false;
                try {
                    digest.update(bytes, 1u, 2u);
                } catch (e: IndexOutOfBoundsError&) {
                    rejected = true;
                }
                return rejected ? 0 : 1;
            } catch (e: k::crypto::MessageDigestException&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("AES-128-GCM known answer and imported key ownership",
          "[crypto][symmetric]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __crypto_aes_gcm_known_answer_test__;
        import k::crypto;

        hexDigit(n: unsigned int) : byte {
            value : unsigned int = n & 0x0Fu;
            if (value < 10u) return (byte) (((unsigned int) '0') + value);
            return (byte) (((unsigned int) 'a') + (value - 10u));
        }

        bytesToHex(bytes: const unsigned byte[]) : String {
            hex : unsigned byte[]! = new unsigned byte[bytes.size * 2u + 1u];
            i : unsigned int = 0u;
            while (i < bytes.size) {
                value : unsigned int = (unsigned int) bytes[i];
                hex[i * 2u] = (unsigned byte) hexDigit(value >> 4u);
                hex[i * 2u + 1u] = (unsigned byte) hexDigit(value);
                ++i;
            }
            hex[bytes.size * 2u] = (unsigned byte) 0;
            return String(hex);
        }

        test() : int {
            try {
                cipher : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::AES128GCM();
                if (!cipher) return 1;
                rawKey : unsigned byte[]! = new unsigned byte[16u];
                nonce : unsigned byte[]! = new unsigned byte[12u];
                plaintext : unsigned byte[]! = new unsigned byte[16u];
                i : unsigned int = 0u;
                while (i < 16u) {
                    rawKey[i] = (unsigned byte) 0;
                    plaintext[i] = (unsigned byte) 0;
                    ++i;
                }
                i = 0u;
                while (i < 12u) {
                    nonce[i] = (unsigned byte) 0;
                    ++i;
                }
                key : k::crypto::SymmetricKey! = cipher.importKey(rawKey);
                rawKey[0u] = (unsigned byte) 1;
                delete rawKey;
                enc : k::crypto::EncryptSession! = cipher.encrypt(key, nonce);
                nonce[0u] = (unsigned byte) 1;
                usedNonce : unsigned byte[]! = enc.nonce();
                if (usedNonce.size != 12u || usedNonce[0u] != (unsigned byte) 0) return 7;
                usedNonce[0u] = (unsigned byte) 2;
                usedNonce = enc.nonce();
                if (usedNonce[0u] != (unsigned byte) 0) return 8;
                ciphertext : unsigned byte[]! = enc.update(plaintext, 0u, 16u);
                tag : unsigned byte[]! = enc.finalize();
                if (bytesToHex(ciphertext) != String("0388dace60b6a392f328c2b971b2fe78")) return 2;
                if (bytesToHex(tag) != String("ab6e47d42cec13bdf53a67b21257bddf")) return 3;

                dec : k::crypto::DecryptSession! = cipher.decrypt(
                    key, usedNonce, k::crypto::DecryptionStaging::Memory);
                dec.update(ciphertext, 0u, ciphertext.size);
                dec.finalize(tag);
                recovered : unsigned byte[]! = new unsigned byte[16u];
                if (dec.read(recovered, 0u, 16u) != 16u) return 4;
                i = 0u;
                while (i < 16u) {
                    if (recovered[i] != (unsigned byte) 0) return 5;
                    ++i;
                }
                if (dec.read(recovered, 0u, 16u) != 0u) return 6;
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

TEST_CASE("Symmetric cipher factories and key validation",
          "[crypto][symmetric]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __crypto_symmetric_factories_test__;
        import k::crypto;

        check(cipher: k::crypto::SymmetricCipher!, expectedName: const String&,
              expectedKeySize: unsigned int) : int throws(k::crypto::CryptoException) {
            if (!cipher) return 1;
            if (cipher.name() != expectedName) return 2;
            if (cipher.keySize() != expectedKeySize || cipher.nonceSize() != 12u ||
                cipher.tagSize() != 16u) return 3;
            key : k::crypto::SymmetricKey! = cipher.generateKey();
            if (!key || key.keySize() != expectedKeySize) return 4;
            nonce : unsigned byte[]! = cipher.randomNonce();
            if (nonce.size != cipher.nonceSize()) return 5;
            return 0;
        }

        test() : int {
            try {
                if (check(k::crypto::SymmetricCipher::AES128GCM(),
                          String("AES-128-GCM"), 16u) != 0) return 1;
                if (check(k::crypto::SymmetricCipher::AES192GCM(),
                          String("AES-192-GCM"), 24u) != 0) return 2;
                if (check(k::crypto::SymmetricCipher::AES256GCM(),
                          String("AES-256-GCM"), 32u) != 0) return 3;
                cipher : k::crypto::SymmetricCipher! =
                    k::crypto::SymmetricCipher::fromName(String("AES-128-GCM"));
                if (!cipher || cipher.keySize() != 16u) return 4;
                unsupported : k::crypto::SymmetricCipher! =
                    k::crypto::SymmetricCipher::fromName(String("KLANG-UNSUPPORTED-CIPHER"));
                if (unsupported != null) return 5;
                badKey : unsigned byte[]! = new unsigned byte[15u];
                rejected : bool = false;
                try {
                    key : k::crypto::SymmetricKey! = cipher.importKey(badKey);
                } catch (e: k::crypto::CryptoInvalidInputException&) {
                    rejected = true;
                }
                if (!rejected) return 6;
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

TEST_CASE("AES-GCM chunked authenticated decryption stages plaintext",
          "[crypto][symmetric]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __crypto_aes_gcm_staging_test__;
        import k::crypto;

        check(staging: k::crypto::DecryptionStaging) : int throws(k::crypto::CryptoException) {
            cipher : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::AES256GCM();
            key : k::crypto::SymmetricKey! = cipher.generateKey();
            enc : k::crypto::EncryptSession! = cipher.encrypt(key);
            nonce : unsigned byte[]! = enc.nonce();
            if (nonce.size != 12u) return 1;

            aad : byte[]! = new byte[6u];
            aad[0u] = (byte) 'x';
            aad[1u] = (byte) 'a';
            aad[2u] = (byte) 'a';
            aad[3u] = (byte) 'd';
            aad[4u] = (byte) '!';
            aad[5u] = (byte) 'y';
            plaintext : unsigned byte[]! = new unsigned byte[9u];
            plaintext[0u] = (unsigned byte) 'x';
            plaintext[1u] = (unsigned byte) 'm';
            plaintext[2u] = (unsigned byte) 'e';
            plaintext[3u] = (unsigned byte) 's';
            plaintext[4u] = (unsigned byte) 's';
            plaintext[5u] = (unsigned byte) 'a';
            plaintext[6u] = (unsigned byte) 'g';
            plaintext[7u] = (unsigned byte) 'e';
            plaintext[8u] = (unsigned byte) 'y';
            signedPlaintext : byte[]! = new byte[4u];
            unsignedAad : unsigned byte[]! = new unsigned byte[4u];
            i : unsigned int = 0u;
            while (i < 4u) {
                signedPlaintext[i] = (byte) plaintext[i + 4u];
                unsignedAad[i] = (unsigned byte) aad[i + 1u];
                ++i;
            }
            enc.updateAad(aad, 1u, 2u);
            enc.updateAad(aad, 3u, 2u);
            first : unsigned byte[]! = enc.update(plaintext, 1u, 3u);
            second : unsigned byte[]! = enc.update(signedPlaintext, 0u, 4u);
            tag : unsigned byte[]! = enc.finalize();
            if (first.size != 3u || second.size != 4u || tag.size != 16u) return 2;

            dec : k::crypto::DecryptSession! = cipher.decrypt(key, nonce, staging);
            dec.updateAad(unsignedAad, 0u, 4u);
            dec.update(first, 0u, first.size);
            signedCiphertext : byte[]! = new byte[6u];
            i = 0u;
            while (i < second.size) {
                signedCiphertext[i + 1u] = (byte) second[i];
                ++i;
            }
            dec.update(signedCiphertext, 1u, 4u);
            recovered : unsigned byte[]! = new unsigned byte[9u];
            i = 0u;
            while (i < recovered.size) {
                recovered[i] = (unsigned byte) 0x5a;
                ++i;
            }
            rejected : bool = false;
            try {
                dec.read(recovered, 1u, 7u);
            } catch (e: k::crypto::CryptoStateException&) {
                rejected = true;
            }
            if (!rejected) return 3;
            rejected = false;
            try {
                dec.read(recovered, 1u, 0u);
            } catch (e: k::crypto::CryptoStateException&) {
                rejected = true;
            }
            if (!rejected) return 13;
            i = 0u;
            while (i < recovered.size) {
                if (recovered[i] != (unsigned byte) 0x5a) return 4;
                ++i;
            }
            dec.finalize(tag);
            if (dec.read(recovered, 1u, 0u) != 0u) return 14;
            if (dec.read(recovered, 1u, 3u) != 3u) return 5;
            signedRecovered : byte[]! = new byte[6u];
            if (dec.read(signedRecovered, 1u, 4u) != 4u) return 6;
            if (dec.read(recovered, 0u, 1u) != 0u) return 7;
            if (recovered[0u] != (unsigned byte) 0x5a ||
                recovered[8u] != (unsigned byte) 0x5a) return 8;
            i = 1u;
            while (i < 4u) {
                if (recovered[i] != plaintext[i]) return 9;
                ++i;
            }
            i = 0u;
            while (i < 4u) {
                if ((unsigned byte) signedRecovered[i + 1u] != plaintext[i + 4u]) return 9;
                ++i;
            }

            bad : k::crypto::DecryptSession! = cipher.decrypt(key, nonce, staging);
            bad.updateAad(aad, 1u, 4u);
            bad.update(first, 0u, first.size);
            bad.update(second, 0u, second.size);
            tag[0u] = (unsigned byte) (tag[0u] ^ (unsigned byte) 1);
            rejected = false;
            try {
                bad.finalize(tag);
            } catch (e: k::crypto::CryptoAuthenticationException&) {
                rejected = true;
            }
            if (!rejected) return 10;
            recovered[1u] = (unsigned byte) 0x5a;
            rejected = false;
            try {
                bad.read(recovered, 1u, 7u);
            } catch (e: k::crypto::CryptoStateException&) {
                rejected = true;
            }
            if (!rejected) return 11;
            if (recovered[1u] != (unsigned byte) 0x5a) return 12;
            return 0;
        }

        test() : int {
            try {
                memory : int = check(k::crypto::DecryptionStaging::Memory);
                if (memory != 0) return 100 + memory;
                file : int = check(k::crypto::DecryptionStaging::TemporaryFile);
                if (file != 0) return 200 + file;
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

TEST_CASE("AES-GCM sessions reject reuse after finalization",
          "[crypto][symmetric][state]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __crypto_aes_gcm_state_test__;
        import k::crypto;

        test() : int {
            try {
                cipher : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::AES128GCM();
                key : k::crypto::SymmetricKey! = cipher.generateKey();
                nonce : unsigned byte[]! = cipher.randomNonce();
                input : unsigned byte[]! = new unsigned byte[1u];
                input[0u] = (unsigned byte) 42;
                enc : k::crypto::EncryptSession! = cipher.encrypt(key, nonce);
                if (enc.tagSize() != 16u) return 5;
                ciphertext : unsigned byte[]! = enc.update(input, 0u, 1u);
                tag : byte[]! = new byte[18u];
                tag[0u] = (byte) 42;
                tag[17u] = (byte) 42;
                enc.finalizeTo(tag, 1u);
                if (tag[0u] != (byte) 42 || tag[17u] != (byte) 42) return 6;
                rejected : bool = false;
                try {
                    extra : unsigned byte[]! = enc.update(input, 0u, 1u);
                } catch (e: k::crypto::CryptoStateException&) {
                    rejected = true;
                }
                if (!rejected) return 1;
                rejected = false;
                try {
                    extraTag : unsigned byte[]! = enc.finalize();
                } catch (e: k::crypto::CryptoStateException&) {
                    rejected = true;
                }
                if (!rejected) return 2;
                dec : k::crypto::DecryptSession! = cipher.decrypt(
                    key, nonce, k::crypto::DecryptionStaging::Memory);
                if (dec.tagSize() != 16u) return 7;
                decNonce : unsigned byte[]! = dec.nonce();
                if (decNonce.size != nonce.size) return 8;
                i : unsigned int = 0u;
                while (i < nonce.size) {
                    if (decNonce[i] != nonce[i]) return 8;
                    ++i;
                }
                dec.update(ciphertext, 0u, 1u);
                dec.finalize(tag, 1u, 16u);
                rejected = false;
                try {
                    dec.update(ciphertext, 0u, 1u);
                } catch (e: k::crypto::CryptoStateException&) {
                    rejected = true;
                }
                if (!rejected) return 3;
                rejected = false;
                try {
                    dec.finalize(tag, 1u, 16u);
                } catch (e: k::crypto::CryptoStateException&) {
                    rejected = true;
                }
                if (!rejected) return 4;
                enc.close();
                enc.close();
                dec.close();
                dec.close();
                if (enc.tagSize() != 16u || dec.tagSize() != 16u) return 9;
                encNonce : unsigned byte[]! = enc.nonce();
                decNonce = dec.nonce();
                if (encNonce.size != nonce.size || decNonce.size != nonce.size) return 10;
                i = 0u;
                while (i < nonce.size) {
                    if (encNonce[i] != nonce[i] || decNonce[i] != nonce[i]) return 10;
                    ++i;
                }
                rejected = false;
                try {
                    dec.read(input, 0u, 1u);
                } catch (e: k::crypto::CryptoStateException&) {
                    rejected = true;
                }
                if (!rejected) return 11;
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

TEST_CASE("Cipher output encryption and authenticated input decryption",
          "[crypto][symmetric][cipher-stream]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __crypto_cipher_output_to_input_test__;
        import k::io::crypto;

        test() : int {
            try {
                cipher : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::AES128GCM();
                key : k::crypto::SymmetricKey! = cipher.generateKey();
                nonce : unsigned byte[]! = cipher.randomNonce();
                aad : byte[]! = new byte[2u];
                aad[0u] = (byte) 'a';
                aad[1u] = (byte) 'd';
                enc : k::crypto::EncryptSession! = cipher.encrypt(key, nonce);
                enc.updateAad(aad, 0u, 2u);
                sink : k::io::ArrayOutputStream<byte>;
                output : k::io::crypto::EncryptCipherOutputStream! =
                    new k::io::crypto::EncryptCipherOutputStream(sink, enc);
                if (enc != null || output.isComplete()) return 1;
                output.write((byte) 'A');
                chunk : byte[]! = new byte[5u];
                chunk[0u] = (byte) 'x';
                chunk[1u] = (byte) 'B';
                chunk[2u] = (byte) 'C';
                chunk[3u] = (byte) 'D';
                chunk[4u] = (byte) 'y';
                output.write(chunk, 1u, 3u);
                output.flush();
                if (output.isComplete() || sink.size() != 4) return 2;
                output.close();
                if (!output.isComplete() || sink.size() != 20) return 3;
                output.close();
                if (sink.size() != 20) return 4;
                delete output;
                encrypted : byte[]! = sink.toArray();
                source : k::io::ArrayInputStream<byte>(encrypted, 20);
                dec : k::crypto::DecryptSession! = cipher.decrypt(
                    key, nonce, k::crypto::DecryptionStaging::Memory);
                dec.updateAad(aad, 0u, 2u);
                input : k::io::crypto::DecryptCipherInputStream! =
                    new k::io::crypto::DecryptCipherInputStream(source, dec);
                if (dec != null || input.isVerified() || input.isComplete()) return 5;
                first : Optional<byte> = input.read();
                if (!first.hasValue() || first.getOr((byte) 0) != (byte) 'A') return 6;
                if (!input.isVerified() || input.isComplete()) return 7;
                recovered : byte[]! = new byte[5u];
                recovered[0u] = (byte) 'x';
                recovered[4u] = (byte) 'y';
                count : Expected<unsigned int, k::io::StreamOutOfData> =
                    input.read(recovered, 1u, 3u);
                if (count.hasError() || count.getResultOr(0u) != 3u) return 8;
                if (recovered[0u] != (byte) 'x' || recovered[1u] != (byte) 'B' ||
                    recovered[2u] != (byte) 'C' || recovered[3u] != (byte) 'D' ||
                    recovered[4u] != (byte) 'y') return 9;
                if (input.isComplete() || input.read().hasValue()) return 10;
                if (!input.isComplete() || input.read().hasValue()) return 11;
                input.close();
                input.close();
                delete input;
                if (source.available().getResultOr(1u) != 0u) return 12;
                return 0;
            } catch (e: Throwable&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Cipher input encryption and authenticated output decryption",
          "[crypto][symmetric][cipher-stream]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __crypto_cipher_input_to_output_test__;
        import k::io::crypto;

        check(length: unsigned int) : int throws(Throwable) {
            cipher : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::AES128GCM();
            key : k::crypto::SymmetricKey! = cipher.generateKey();
            nonce : unsigned byte[]! = cipher.randomNonce();
            plain : byte[]! = new byte[length];
            i : unsigned int = 0u;
            while (i < length) {
                plain[i] = (byte) (i + 33u);
                ++i;
            }
            source : k::io::ArrayInputStream<byte>(plain, (int) length);
            enc : k::crypto::EncryptSession! = cipher.encrypt(key, nonce);
            input : k::io::crypto::EncryptCipherInputStream! =
                new k::io::crypto::EncryptCipherInputStream(source, enc);
            if (enc != null || input.isComplete()) return 1;
            encryptedSink : k::io::ArrayOutputStream<byte>;
            first : Optional<byte> = input.read();
            if (!first.hasValue()) return 2;
            encryptedSink.write(first.getOr((byte) 0));
            buffer : byte[]! = new byte[5u];
            while (true) {
                result : Expected<unsigned int, k::io::StreamOutOfData> =
                    input.read(buffer, 1u, 3u);
                if (result.hasError()) return 3;
                count : unsigned int = result.getResultOr(0u);
                if (count == 0u) break;
                encryptedSink.write(buffer, 1u, count);
            }
            if (!input.isComplete() || encryptedSink.size() != (int) length + 16) return 4;
            if (input.read().hasValue()) return 5;
            input.close();
            input.close();
            delete input;

            encrypted : byte[]! = encryptedSink.toArray();
            dec : k::crypto::DecryptSession! = cipher.decrypt(
                key, nonce, k::crypto::DecryptionStaging::TemporaryFile);
            plainSink : k::io::ArrayOutputStream<byte>;
            output : k::io::crypto::DecryptCipherOutputStream! =
                new k::io::crypto::DecryptCipherOutputStream(plainSink, dec);
            if (dec != null || output.isVerified() || output.isComplete()) return 6;
            output.write(encrypted[0u]);
            i = 1u;
            while (i < encrypted.size) {
                count : unsigned int = encrypted.size - i;
                if (count > 3u) count = 3u;
                output.write(encrypted, i, count);
                i += count;
            }
            output.flush();
            if (plainSink.size() != 0 || output.isVerified() || output.isComplete()) return 7;
            output.close();
            if (!output.isVerified() || !output.isComplete() ||
                plainSink.size() != (int) length) return 8;
            output.close();
            if (plainSink.size() != (int) length) return 9;
            delete output;
            recovered : byte[]! = plainSink.toArray();
            i = 0u;
            while (i < length) {
                if (recovered[i] != plain[i]) return 10;
                ++i;
            }
            return 0;
        }

        test() : int {
            try {
                empty : int = check(0u);
                if (empty != 0) return 100 + empty;
                chunked : int = check(7u);
                if (chunked != 0) return 200 + chunked;
                return 0;
            } catch (e: Throwable&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Cipher streams reject invalid tags and early input close",
          "[crypto][symmetric][cipher-stream]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __crypto_cipher_stream_auth_test__;
        import k::io::crypto;

        check(cipher: k::crypto::SymmetricCipher&, key: const k::crypto::SymmetricKey&,
              nonce: const unsigned byte[], data: byte[], length: int)
                : int throws(Throwable) {
            source : k::io::ArrayInputStream<byte>(&data, length);
            dec : k::crypto::DecryptSession! = cipher.decrypt(
                key, nonce, k::crypto::DecryptionStaging::Memory);
            input : k::io::crypto::DecryptCipherInputStream! =
                new k::io::crypto::DecryptCipherInputStream(source, dec);
            buf : byte[]! = new byte[3u];
            buf[1u] = (byte) 77;
            rejected : bool = false;
            try {
                input.read(buf, 1u, 1u);
            } catch (e: k::crypto::CryptoAuthenticationException&) {
                rejected = true;
            }
            if (!rejected || buf[1u] != (byte) 77 ||
                input.isVerified() || input.isComplete()) return 1;
            input.close();
            input.close();

            sink : k::io::ArrayOutputStream<byte>;
            dec2 : k::crypto::DecryptSession! = cipher.decrypt(
                key, nonce, k::crypto::DecryptionStaging::Memory);
            output : k::io::crypto::DecryptCipherOutputStream! =
                new k::io::crypto::DecryptCipherOutputStream(sink, dec2);
            output.write(data, 0u, (unsigned int) length);
            output.flush();
            if (sink.size() != 0 || output.isVerified() || output.isComplete()) return 2;
            rejected = false;
            try {
                output.close();
            } catch (e: k::crypto::CryptoAuthenticationException&) {
                rejected = true;
            }
            if (!rejected || sink.size() != 0 ||
                output.isVerified() || output.isComplete()) return 3;
            return 0;
        }

        test() : int {
            try {
                cipher : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::AES128GCM();
                key : k::crypto::SymmetricKey! = cipher.generateKey();
                nonce : unsigned byte[]! = cipher.randomNonce();
                sink : k::io::ArrayOutputStream<byte>;
                enc : k::crypto::EncryptSession! = cipher.encrypt(key, nonce);
                output : k::io::crypto::EncryptCipherOutputStream! =
                    new k::io::crypto::EncryptCipherOutputStream(sink, enc);
                output.write((byte) 42);
                output.close();
                encrypted : byte[]! = sink.toArray();

                source : k::io::ArrayInputStream<byte>(encrypted, 17);
                dec : k::crypto::DecryptSession! = cipher.decrypt(
                    key, nonce, k::crypto::DecryptionStaging::Memory);
                input : k::io::crypto::DecryptCipherInputStream! =
                    new k::io::crypto::DecryptCipherInputStream(source, dec);
                input.close();
                input.close();
                if (input.isVerified() || input.isComplete() ||
                    source.available().getResultOr(0u) != 17u) return 1;
                buf : byte[]! = new byte[1u];
                if (!input.read(buf, 0u, 1u).hasError()) return 2;
                earlySource : k::io::ArrayInputStream<byte>(encrypted, 17);
                earlyEnc : k::crypto::EncryptSession! = cipher.encrypt(key);
                earlyInput : k::io::crypto::EncryptCipherInputStream! =
                    new k::io::crypto::EncryptCipherInputStream(earlySource, earlyEnc);
                earlyInput.close();
                earlyInput.close();
                if (earlyInput.isComplete() ||
                    earlySource.available().getResultOr(0u) != 17u) return 3;

                encrypted[16u] = (byte) ((unsigned byte) encrypted[16u] ^ 1u);
                wrong : int = check(cipher, key, nonce, encrypted, 17);
                if (wrong != 0) return 10 + wrong;
                shortTag : int = check(cipher, key, nonce, encrypted, 7);
                if (shortTag != 0) return 20 + shortTag;
                return 0;
            } catch (e: Throwable&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Cipher streams propagate underlying failures",
          "[crypto][symmetric][cipher-stream]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __crypto_cipher_stream_failure_test__;
        import k::io::crypto;

        failureMode : int = 0;
        outputCloses : int = 0;
        inputCloses : int = 0;

        class FailingOutput : public k::io::FilterOutputStream<byte> {
            public FailingOutput(output: k::io::OutputStream<byte>*)
                : FilterOutputStream(output) {}

            public override write(data: const byte[], off: unsigned int, len: unsigned int)
                    : k::io::OutputStream<byte>& throws(k::crypto::CryptoException) {
                if (failureMode == 1 || failureMode == 4) {
                    throw k::crypto::CryptoOperationException();
                }
                return _out->write(data, off, len);
            }

            public override flush() : k::io::OutputStream<byte>&
                    throws(k::crypto::CryptoException) {
                if (failureMode == 3) throw k::crypto::CryptoOperationException();
                return _out->flush();
            }

            public override close() : k::io::OutputStream<byte>&
                    throws(k::crypto::CryptoException) {
                ++outputCloses;
                if (failureMode == 2) throw k::crypto::CryptoOperationException();
                if (failureMode == 4) throw k::crypto::CryptoStateException();
                return _out->close();
            }
        }

        class FailingInput : public k::io::FilterInputStream<byte> {
            public FailingInput(input: k::io::InputStream<byte>*)
                : FilterInputStream(input) {}

            public override read(data: byte[], off: unsigned int, len: unsigned int)
                    : Expected<unsigned int, k::io::StreamOutOfData> {
                err : k::io::StreamOutOfData = k::io::StreamOutOfData::Closed;
                return Expected<unsigned int, k::io::StreamOutOfData>::error(err);
            }

            public override close() {
                ++inputCloses;
                _in->close();
            }
        }

        test() : int {
            stage : int = 1;
            try {
                cipher : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::AES128GCM();
                key : k::crypto::SymmetricKey! = cipher.generateKey();
                nonce : unsigned byte[]! = cipher.randomNonce();
                stage = 2;
                byteSink : k::io::ArrayOutputStream<byte>;
                failingSink : FailingOutput(&byteSink);
                enc : k::crypto::EncryptSession! = cipher.encrypt(key, nonce);
                output : k::io::crypto::EncryptCipherOutputStream! =
                    new k::io::crypto::EncryptCipherOutputStream(failingSink, enc);
                failureMode = 1;
                outputCloses = 0;
                rejected : bool = false;
                stage = 3;
                try {
                    output.write((byte) 42);
                } catch (e: k::crypto::CryptoOperationException&) {
                    rejected = true;
                }
                if (!rejected || output.isComplete() || byteSink.size() != 0) return 1;
                rejected = false;
                stage = 4;
                try {
                    output.flush();
                } catch (e: k::crypto::CryptoStateException&) {
                    rejected = true;
                }
                if (!rejected) return 2;
                rejected = false;
                stage = 5;
                try {
                    output.close();
                } catch (e: k::crypto::CryptoStateException&) {
                    rejected = true;
                }
                if (!rejected || outputCloses != 1 || output.isComplete()) return 3;
                rejected = false;
                stage = 6;
                try {
                    output.close();
                } catch (e: k::crypto::CryptoStateException&) {
                    rejected = true;
                }
                if (!rejected || outputCloses != 1) return 4;

                failureMode = 0;
                stage = 7;
                encryptedSink : k::io::ArrayOutputStream<byte>;
                nonce2 : unsigned byte[]! = cipher.randomNonce();
                enc2 : k::crypto::EncryptSession! = cipher.encrypt(key, nonce2);
                producer : k::io::crypto::EncryptCipherOutputStream! =
                    new k::io::crypto::EncryptCipherOutputStream(encryptedSink, enc2);
                stage = 8;
                producer.write((byte) 42);
                stage = 9;
                producer.close();
                encrypted : byte[]! = encryptedSink.toArray();
                stage = 10;
                plainSink : k::io::ArrayOutputStream<byte>;
                failingPlainSink : FailingOutput(&plainSink);
                dec : k::crypto::DecryptSession! = cipher.decrypt(
                    key, nonce2, k::crypto::DecryptionStaging::Memory);
                consumer : k::io::crypto::DecryptCipherOutputStream! =
                    new k::io::crypto::DecryptCipherOutputStream(failingPlainSink, dec);
                consumer.write(encrypted);
                failureMode = 2;
                outputCloses = 0;
                rejected = false;
                stage = 11;
                try {
                    consumer.close();
                } catch (e: k::crypto::CryptoOperationException&) {
                    rejected = true;
                }
                if (!rejected || outputCloses != 1 || !consumer.isVerified() ||
                    consumer.isComplete() || plainSink.size() != 1) return 5;

                failureMode = 0;
                stage = 12;
                empty : k::io::ArrayInputStream<byte>;
                failingSource : FailingInput(&empty);
                enc3 : k::crypto::EncryptSession! = cipher.encrypt(key);
                encryptInput : k::io::crypto::EncryptCipherInputStream! =
                    new k::io::crypto::EncryptCipherInputStream(failingSource, enc3);
                inputCloses = 0;
                buf : byte[]! = new byte[1u];
                buf[0u] = (byte) 77;
                if (!encryptInput.read(buf, 0u, 1u).hasError() ||
                    encryptInput.isComplete() || buf[0u] != (byte) 77) return 6;
                stage = 13;
                encryptInput.close();
                encryptInput.close();
                if (inputCloses != 1) return 7;

                stage = 14;
                empty2 : k::io::ArrayInputStream<byte>;
                failingSource2 : FailingInput(&empty2);
                dec2 : k::crypto::DecryptSession! = cipher.decrypt(
                    key, nonce, k::crypto::DecryptionStaging::Memory);
                decryptInput : k::io::crypto::DecryptCipherInputStream! =
                    new k::io::crypto::DecryptCipherInputStream(failingSource2, dec2);
                if (!decryptInput.read(buf, 0u, 1u).hasError() ||
                    decryptInput.isVerified() || decryptInput.isComplete() ||
                    buf[0u] != (byte) 77) return 8;
                stage = 15;
                decryptInput.close();
                decryptInput.close();
                if (inputCloses != 2) return 9;

                stage = 16;
                writeSink : k::io::ArrayOutputStream<byte>;
                writeFailure : FailingOutput(&writeSink);
                enc4 : k::crypto::EncryptSession! = cipher.encrypt(key);
                failedWriter : k::io::crypto::EncryptCipherOutputStream! =
                    new k::io::crypto::EncryptCipherOutputStream(writeFailure, enc4);
                failureMode = 1;
                rejected = false;
                try {
                    failedWriter.write((byte) 42);
                } catch (e: k::crypto::CryptoOperationException&) {
                    rejected = true;
                }
                if (!rejected) return 10;
                failureMode = 2;
                outputCloses = 0;
                rejected = false;
                stage = 17;
                try {
                    failedWriter.close();
                } catch (e: k::crypto::CryptoOperationException&) {
                    rejected = true;
                }
                if (!rejected || outputCloses != 1 || failedWriter.isComplete() ||
                    writeSink.size() != 0) return 11;

                stage = 18;
                flushSink : k::io::ArrayOutputStream<byte>;
                flushFailure : FailingOutput(&flushSink);
                dec3 : k::crypto::DecryptSession! = cipher.decrypt(
                    key, nonce2, k::crypto::DecryptionStaging::Memory);
                failedFlusher : k::io::crypto::DecryptCipherOutputStream! =
                    new k::io::crypto::DecryptCipherOutputStream(flushFailure, dec3);
                failedFlusher.write(encrypted);
                failureMode = 3;
                rejected = false;
                stage = 19;
                try {
                    failedFlusher.flush();
                } catch (e: k::crypto::CryptoOperationException&) {
                    rejected = true;
                }
                if (!rejected || flushSink.size() != 0) return 12;
                failureMode = 2;
                outputCloses = 0;
                rejected = false;
                stage = 20;
                try {
                    failedFlusher.close();
                } catch (e: k::crypto::CryptoOperationException&) {
                    rejected = true;
                }
                if (!rejected || outputCloses != 1 || failedFlusher.isVerified() ||
                    failedFlusher.isComplete() || flushSink.size() != 0) return 13;

                stage = 21;
                badSink : k::io::ArrayOutputStream<byte>;
                badClose : FailingOutput(&badSink);
                dec4 : k::crypto::DecryptSession! = cipher.decrypt(
                    key, nonce2, k::crypto::DecryptionStaging::Memory);
                badOutput : k::io::crypto::DecryptCipherOutputStream! =
                    new k::io::crypto::DecryptCipherOutputStream(badClose, dec4);
                encrypted[encrypted.size - 1u] =
                    (byte) ((unsigned byte) encrypted[encrypted.size - 1u] ^ 1u);
                badOutput.write(encrypted);
                failureMode = 2;
                outputCloses = 0;
                rejected = false;
                stage = 22;
                try {
                    badOutput.close();
                } catch (e: k::crypto::CryptoAuthenticationException&) {
                    rejected = true;
                }
                if (!rejected || outputCloses != 1 || badOutput.isVerified() ||
                    badOutput.isComplete() || badSink.size() != 0) return 14;

                stage = 23;
                tagSink : k::io::ArrayOutputStream<byte>;
                tagFailure : FailingOutput(&tagSink);
                enc5 : k::crypto::EncryptSession! = cipher.encrypt(key);
                failedTag : k::io::crypto::EncryptCipherOutputStream! =
                    new k::io::crypto::EncryptCipherOutputStream(tagFailure, enc5);
                failureMode = 4;
                outputCloses = 0;
                rejected = false;
                stage = 24;
                try {
                    failedTag.close();
                } catch (e: k::crypto::CryptoOperationException&) {
                    rejected = true;
                }
                if (!rejected || outputCloses != 1 || failedTag.isComplete() ||
                    tagSink.size() != 0) return 15;
                stage = 25;
                return 0;
            } catch (e: Throwable&) {
                return -(stage * 100000 + e.getCode());
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Symmetric cipher string aliases select algorithms, modes and padding",
          "[crypto][symmetric][string-alias]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __crypto_symmetric_alias_test__;
        import k::crypto;

        test() : int {
            algorithm : const k::crypto::SymmetricAlgorithm! = new String("AES");
            mode : const k::crypto::SymmetricMode! = new String("CBC");
            padding : const k::crypto::SymmetricPadding! = new String("PKCS7");
            cipher : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                algorithm, 128u, mode, 16u, 0u, padding);
            if (!cipher || cipher.algorithm() != String("AES") ||
                cipher.mode() != String("CBC") || cipher.padding() != String("PKCS7"))
                return 1;
            byEnum : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                k::crypto::SymmetricAlgorithms::AES, 128u, k::crypto::SymmetricModes::CBC,
                16u, 0u, k::crypto::SymmetricPaddings::PKCS7);
            if (!byEnum || byEnum.algorithm() != String("AES") ||
                byEnum.mode() != String("CBC") || byEnum.padding() != String("PKCS7"))
                return 2;
            badAlgorithm : const k::crypto::SymmetricAlgorithm! = new String("aes");
            badMode : const k::crypto::SymmetricMode! = new String("UNKNOWN");
            badPadding : const k::crypto::SymmetricPadding! = new String("UNKNOWN");
            invalid : k::crypto::SymmetricCipher! =
                k::crypto::SymmetricCipher::create(badAlgorithm, 128u, mode);
            if (invalid) return 3;
            invalid = k::crypto::SymmetricCipher::create(algorithm, 128u, badMode);
            if (invalid) return 4;
            invalid = k::crypto::SymmetricCipher::create(
                algorithm, 128u, mode, 16u, 0u, badPadding);
            if (invalid) return 5;
            return 0;
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}
