/*
 * K Language generalized symmetric cipher stream integration tests
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

#include <string>

namespace {

constexpr auto prelude = R"K(
    module __crypto_generalized_cipher_stream_test__;
    import k::io::crypto;

    class ChunkedInput : public k::io::FilterInputStream<byte> {
    public:
        ChunkedInput(input: k::io::InputStream<byte>&) : FilterInputStream(&input) {}

        override read(data: byte[], off: unsigned int, len: unsigned int)
                : Expected<unsigned int, k::io::StreamOutOfData> {
            if (len > 3u) len = 3u;
            return _in->read(data, off, len);
        }
    }

    checkTampering(cipher: k::crypto::SymmetricCipher&,
                   key: const k::crypto::SymmetricKey&, nonce: const unsigned byte[],
                   aad: const byte[], encrypted: byte[]) : int throws(Throwable) {
        encrypted[encrypted.size - 1u] =
            (byte) ((unsigned byte) encrypted[encrypted.size - 1u] ^ 1u);
        source : k::io::ArrayInputStream<byte>(&encrypted, (int) encrypted.size);
        chunks : ChunkedInput(source);
        dec : k::crypto::DecryptSession! = cipher.decrypt(
            key, nonce, k::crypto::DecryptionStaging::Memory);
        dec.updateAad(aad, 0u, aad.size);
        input : k::io::crypto::DecryptCipherInputStream! =
            new k::io::crypto::DecryptCipherInputStream(chunks, dec);
        buffer : byte[]! = new byte[7u];
        i : unsigned int = 0u;
        while (i < buffer.size) {
            buffer[i] = (byte) 90;
            ++i;
        }
        rejected : bool = false;
        try {
            input.read(buffer, 1u, 5u);
        } catch (e: k::crypto::CryptoAuthenticationException&) {
            rejected = true;
        }
        if (!rejected || input.isVerified() || input.isComplete()) return 1;
        i = 0u;
        while (i < buffer.size) {
            if (buffer[i] != (byte) 90) return 2;
            ++i;
        }
        input.close();

        sink : k::io::ArrayOutputStream<byte>;
        bad : k::crypto::DecryptSession! = cipher.decrypt(
            key, nonce, k::crypto::DecryptionStaging::TemporaryFile);
        bad.updateAad(aad, 0u, aad.size);
        output : k::io::crypto::DecryptCipherOutputStream! =
            new k::io::crypto::DecryptCipherOutputStream(sink, bad);
        i = 0u;
        while (i < encrypted.size) {
            count : unsigned int = encrypted.size - i;
            if (count > 5u) count = 5u;
            output.write(encrypted, i, count);
            i += count;
        }
        output.flush();
        if (sink.size() != 0 || output.isVerified() || output.isComplete()) return 3;
        rejected = false;
        try {
            output.close();
        } catch (e: k::crypto::CryptoAuthenticationException&) {
            rejected = true;
        }
        if (!rejected || sink.size() != 0 || output.isVerified() || output.isComplete())
            return 4;
        return 0;
    }

    check(cipher: k::crypto::SymmetricCipher&, expectedSize: unsigned int,
          cbc: bool, deferred: bool) : int throws(Throwable) {
        auth : bool = cipher.authenticated();
        key : k::crypto::SymmetricKey! = cipher.generateKey();
        nonce : unsigned byte[]! = cipher.randomNonce();
        aad : byte[]! = new byte[3u];
        aad[0u] = (byte) 1;
        aad[1u] = (byte) 2;
        aad[2u] = (byte) 3;
        plain : byte[]! = new byte[37u];
        i : unsigned int = 0u;
        while (i < plain.size) {
            plain[i] = (byte) (i + 33u);
            ++i;
        }

        enc : k::crypto::EncryptSession! = cipher.encrypt(key, nonce);
        if (deferred) enc = cipher.encrypt(key, nonce, k::crypto::DecryptionStaging::Memory);
        if (auth) enc.updateAad(aad, 0u, aad.size);
        encryptedSink : k::io::ArrayOutputStream<byte>;
        output : k::io::crypto::EncryptCipherOutputStream! =
            new k::io::crypto::EncryptCipherOutputStream(encryptedSink, enc);
        output.write(plain[0u]);
        output.write(plain, 1u, 7u);
        output.flush();
        if ((cbc || deferred) && encryptedSink.size() != 0) return 1;
        output.write(plain, 8u, 29u);
        if (cbc && encryptedSink.size() != 32) return 2;
        if (deferred && encryptedSink.size() != 0) return 3;
        output.close();
        if (!output.isComplete() || encryptedSink.size() != (int) expectedSize) return 4;
        encrypted : byte[]! = encryptedSink.toArray();

        source : k::io::ArrayInputStream<byte>(encrypted, (int) encrypted.size);
        chunks : ChunkedInput(source);
        dec : k::crypto::DecryptSession! = cipher.decrypt(
            key, nonce, k::crypto::DecryptionStaging::Memory);
        if (auth) dec.updateAad(aad, 0u, aad.size);
        inputIncremental : bool = dec.updatesProduceOutput();
        if (!auth && (cbc || cipher.mode() == k::crypto::SymmetricMode::CTR) &&
            !inputIncremental) return 25;
        input : k::io::crypto::DecryptCipherInputStream! =
            new k::io::crypto::DecryptCipherInputStream(chunks, dec);
        if (input.authenticated() != auth || input.isVerified()) return 5;
        recovered : byte[]! = new byte[37u];
        result : Expected<unsigned int, k::io::StreamOutOfData> = input.read(recovered, 0u, 5u);
        if (result.hasError()) return 6;
        count : unsigned int = result.getResultOr(0u);
        if (count == 0u || count > 5u || input.isVerified() != auth) return 7;
        if (!auth && inputIncremental && source.available().getResultOr(0u) == 0u) return 8;
        if (!auth && !inputIncremental && source.available().getResultOr(1u) != 0u) return 26;
        while (count < plain.size) {
            requested : unsigned int = plain.size - count;
            if (requested > 5u) requested = 5u;
            result = input.read(recovered, count, requested);
            if (result.hasError() || result.getResultOr(0u) == 0u) return 9;
            count += result.getResultOr(0u);
        }
        if (input.read().hasValue() || !input.isComplete() ||
            input.isVerified() != auth) return 10;
        i = 0u;
        while (i < plain.size) {
            if (recovered[i] != plain[i]) return 11;
            ++i;
        }
        input.close();

        plainSource : k::io::ArrayInputStream<byte>(plain, (int) plain.size);
        plainChunks : ChunkedInput(plainSource);
        nonce2 : unsigned byte[]! = cipher.randomNonce();
        enc2 : k::crypto::EncryptSession! = cipher.encrypt(key, nonce2);
        if (deferred) enc2 = cipher.encrypt(key, nonce2, k::crypto::DecryptionStaging::Memory);
        if (auth) enc2.updateAad(aad, 0u, aad.size);
        encryptInput : k::io::crypto::EncryptCipherInputStream! =
            new k::io::crypto::EncryptCipherInputStream(plainChunks, enc2);
        encryptedSink2 : k::io::ArrayOutputStream<byte>;
        buffer : byte[]! = new byte[7u];
        while (true) {
            result = encryptInput.read(buffer, 1u, 5u);
            if (result.hasError()) return 12;
            count = result.getResultOr(0u);
            if (count == 0u) break;
            encryptedSink2.write(buffer, 1u, count);
        }
        if (!encryptInput.isComplete() || encryptedSink2.size() != (int) expectedSize) return 13;
        encryptInput.close();
        encrypted2 : byte[]! = encryptedSink2.toArray();
        dec2 : k::crypto::DecryptSession! = cipher.decrypt(
            key, nonce2, k::crypto::DecryptionStaging::TemporaryFile);
        if (auth) dec2.updateAad(aad, 0u, aad.size);
        outputIncremental : bool = dec2.updatesProduceOutput();
        if (!auth && (cbc || cipher.mode() == k::crypto::SymmetricMode::CTR) &&
            !outputIncremental) return 27;
        plainSink : k::io::ArrayOutputStream<byte>;
        decryptOutput : k::io::crypto::DecryptCipherOutputStream! =
            new k::io::crypto::DecryptCipherOutputStream(plainSink, dec2);
        if (decryptOutput.authenticated() != auth || decryptOutput.isVerified()) return 14;
        decryptOutput.write(encrypted2, 0u, 32u);
        if (auth && (plainSink.size() != 0 || decryptOutput.isVerified())) return 15;
        if (!auth && decryptOutput.isVerified()) return 16;
        if (!auth && outputIncremental && plainSink.size() == 0) return 28;
        if (!auth && !outputIncremental && plainSink.size() != 0) return 29;
        i = 32u;
        while (i < encrypted2.size) {
            count = encrypted2.size - i;
            if (count > 3u) count = 3u;
            decryptOutput.write(encrypted2, i, count);
            i += count;
        }
        decryptOutput.flush();
        if (auth && (plainSink.size() != 0 || decryptOutput.isVerified())) return 17;
        if (!auth && !outputIncremental && plainSink.size() != 0) return 30;
        decryptOutput.close();
        if (!decryptOutput.isComplete() || decryptOutput.isVerified() != auth ||
            plainSink.size() != (int) plain.size) return 18;
        recovered = plainSink.toArray();
        i = 0u;
        while (i < plain.size) {
            if (recovered[i] != plain[i]) return 19;
            ++i;
        }
        if (auth) {
            tampering : int = checkTampering(cipher, key, nonce2, aad, encrypted2);
            if (tampering != 0) return 20 + tampering;
        }
        return 0;
    }
)K";

} // namespace

TEST_CASE("CBC and CTR cipher streams preserve incremental and final output",
          "[crypto][symmetric][cipher-stream][generalized]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                cbc : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 128u, k::crypto::SymmetricMode::CBC,
                    16u, 0u, k::crypto::SymmetricPadding::PKCS7);
                if (!cbc) return 1;
                result : int = check(cbc, 48u, true, false);
                if (result != 0) return 100 + result;
                ctr : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 128u, k::crypto::SymmetricMode::CTR);
                if (!ctr) return 2;
                result = check(ctr, 37u, false, false);
                if (result != 0) return 200 + result;
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

TEST_CASE("GCM and deferred CCM streams verify before releasing plaintext",
          "[crypto][symmetric][cipher-stream][generalized]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                gcm : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 128u, k::crypto::SymmetricMode::GCM);
                if (!gcm || !gcm.authenticated()) return 1;
                result : int = check(gcm, 53u, false, false);
                if (result != 0) return 100 + result;
                ccm : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 128u, k::crypto::SymmetricMode::CCM,
                    13u, 8u, k::crypto::SymmetricPadding::None);
                if (!ccm || !ccm.authenticated()) return 2;
                result = check(ccm, 45u, false, true);
                if (result != 0) return 200 + result;
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

TEST_CASE("AES XTS cipher streams retain and finish the final partial block",
          "[crypto][symmetric][cipher-stream][generalized][xts]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                xts : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 128u, k::crypto::SymmetricMode::XTS);
                if (!xts) return 77;
                result : int = check(xts, 37u, false, false);
                if (result != 0) return 100 + result;
                return 0;
            } catch (e: Throwable&) {
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

TEST_CASE("Untagged encrypt input completes on the last final ciphertext byte",
          "[crypto][symmetric][cipher-stream][completion][xts]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                cipher : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 128u, k::crypto::SymmetricMode::XTS);
                if (!cipher) return 77;
                key : k::crypto::SymmetricKey! = cipher.generateKey();
                nonce : unsigned byte[]! = cipher.randomNonce();
                plain : byte[]! = new byte[16u];
                i : unsigned int = 0u;
                while (i < plain.size) {
                    plain[i] = (byte) i;
                    ++i;
                }
                enc : k::crypto::EncryptSession! = cipher.encrypt(key, nonce);
                pending : unsigned byte[]! = enc.update(plain, 0u, 16u);
                if (pending.size != 0u) return 1;
                empty : byte[]! = new byte[0u];
                source : k::io::ArrayInputStream<byte>(empty, 0);
                input : k::io::crypto::EncryptCipherInputStream! =
                    new k::io::crypto::EncryptCipherInputStream(source, enc);
                encrypted : byte[]! = new byte[16u];
                result : Expected<unsigned int, k::io::StreamOutOfData> =
                    input.read(encrypted, 0u, 16u);
                if (result.hasError() || result.getResultOr(0u) != 16u) return 2;
                if (!input.isComplete()) return 3;
                input.close();
                if (!input.isComplete()) return 4;
                return 0;
            } catch (e: Throwable&) {
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

TEST_CASE("Deferred CCM encrypt input drains large final output before its tag",
          "[crypto][symmetric][cipher-stream][completion][ccm]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                cipher : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithm::AES, 128u, k::crypto::SymmetricMode::CCM,
                    13u, 8u, k::crypto::SymmetricPadding::None);
                if (!cipher) return 1;
                key : k::crypto::SymmetricKey! = cipher.generateKey();
                nonce : unsigned byte[]! = cipher.randomNonce();
                plain : byte[]! = new byte[4113u];
                i : unsigned int = 0u;
                while (i < plain.size) {
                    plain[i] = (byte) (i & 127u);
                    ++i;
                }
                source : k::io::ArrayInputStream<byte>(plain, 4113);
                enc : k::crypto::EncryptSession! = cipher.encrypt(
                    key, nonce, k::crypto::DecryptionStaging::Memory);
                input : k::io::crypto::EncryptCipherInputStream! =
                    new k::io::crypto::EncryptCipherInputStream(source, enc);
                encrypted : byte[]! = new byte[4121u];
                count : unsigned int = 0u;
                while (count < 4113u) {
                    request : unsigned int = 4113u - count;
                    if (request > 257u) request = 257u;
                    result : Expected<unsigned int, k::io::StreamOutOfData> =
                        input.read(encrypted, count, request);
                    if (result.hasError() || result.getResultOr(0u) == 0u) return 2;
                    count += result.getResultOr(0u);
                    if (input.isComplete()) return 3;
                }
                result : Expected<unsigned int, k::io::StreamOutOfData> =
                    input.read(encrypted, 4113u, 7u);
                if (result.hasError() || result.getResultOr(0u) != 7u ||
                    input.isComplete()) return 4;
                result = input.read(encrypted, 4120u, 1u);
                if (result.hasError() || result.getResultOr(0u) != 1u ||
                    !input.isComplete()) return 5;
                input.close();
                if (!input.isComplete()) return 6;

                encryptedSource : k::io::ArrayInputStream<byte>(encrypted, 4121);
                dec : k::crypto::DecryptSession! = cipher.decrypt(
                    key, nonce, k::crypto::DecryptionStaging::Memory);
                decryptInput : k::io::crypto::DecryptCipherInputStream! =
                    new k::io::crypto::DecryptCipherInputStream(encryptedSource, dec);
                recovered : byte[]! = new byte[4113u];
                count = 0u;
                while (count < 4113u) {
                    result = decryptInput.read(recovered, count, 4113u - count);
                    if (result.hasError() || result.getResultOr(0u) == 0u) return 7;
                    count += result.getResultOr(0u);
                }
                if (!decryptInput.isVerified() || decryptInput.read().hasValue()) return 8;
                i = 0u;
                while (i < plain.size) {
                    if (recovered[i] != plain[i]) return 9;
                    ++i;
                }
                decryptInput.close();
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

TEST_CASE("GCM encrypt input completes only after the entire tag is read",
          "[crypto][symmetric][cipher-stream][completion][gcm]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                cipher : k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::AES128GCM();
                key : k::crypto::SymmetricKey! = cipher.generateKey();
                nonce : unsigned byte[]! = cipher.randomNonce();
                plain : byte[]! = new byte[23u];
                i : unsigned int = 0u;
                while (i < plain.size) {
                    plain[i] = (byte) i;
                    ++i;
                }
                source : k::io::ArrayInputStream<byte>(plain, 23);
                enc : k::crypto::EncryptSession! = cipher.encrypt(key, nonce);
                input : k::io::crypto::EncryptCipherInputStream! =
                    new k::io::crypto::EncryptCipherInputStream(source, enc);
                encrypted : byte[]! = new byte[39u];
                result : Expected<unsigned int, k::io::StreamOutOfData> =
                    input.read(encrypted, 0u, 23u);
                if (result.hasError() || result.getResultOr(0u) != 23u ||
                    input.isComplete()) return 1;
                result = input.read(encrypted, 23u, 15u);
                if (result.hasError() || result.getResultOr(0u) != 15u ||
                    input.isComplete()) return 2;
                result = input.read(encrypted, 38u, 1u);
                if (result.hasError() || result.getResultOr(0u) != 1u ||
                    !input.isComplete()) return 3;
                input.close();
                if (!input.isComplete()) return 4;
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
