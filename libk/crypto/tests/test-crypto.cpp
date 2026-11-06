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
