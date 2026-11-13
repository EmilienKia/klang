/*
 * K Language crypto capability snapshot integration tests
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
#include <openssl/provider.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace {

struct OpenSSLError {
    unsigned long code;
    std::string file;
    int line;
    std::string function;
    std::string data;
    int flags;

    bool operator==(const OpenSSLError& other) const {
        return code == other.code && file == other.file && line == other.line &&
               function == other.function && data == other.data && flags == other.flags;
    }
};

std::vector<OpenSSLError> takeOpenSSLErrors() {
    std::vector<OpenSSLError> errors;
    const char* file;
    const char* function;
    const char* data;
    int line;
    int flags;
    unsigned long code;
    while ((code = ERR_get_error_all(&file, &line, &function, &data, &flags)) != 0) {
        errors.push_back({code, file ? file : "", line, function ? function : "",
                          data ? data : "", flags});
    }
    return errors;
}

bool rawChaChaSupported(const EVP_CIPHER* cipher) {
    if (!cipher || EVP_CIPHER_get_mode(cipher) != EVP_CIPH_STREAM_CIPHER ||
        EVP_CIPHER_get_key_length(cipher) != 32 || EVP_CIPHER_get_iv_length(cipher) != 16 ||
        (EVP_CIPHER_get_flags(cipher) & EVP_CIPH_FLAG_AEAD_CIPHER)) return false;
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(
        EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx) return false;
    std::array<unsigned char, 32> key{};
    std::array<unsigned char, 16> iv{};
    std::array<unsigned char, 128> input{};
    std::array<unsigned char, 144> encrypted{};
    std::array<unsigned char, 144> recovered{};
    auto init = [&](int encrypt) {
        return EVP_CipherInit_ex2(ctx.get(), cipher, nullptr, nullptr, encrypt, nullptr) == 1 &&
               EVP_CipherInit_ex2(ctx.get(), nullptr, key.data(), iv.data(), encrypt, nullptr) == 1;
    };
    for (unsigned int i = 0; i < 19; ++i) input[i] = static_cast<unsigned char>(i);
    int a = 0, b = 0, f = 0, d = 0, e = 0, g = 0;
    if (!init(1) ||
        EVP_CipherUpdate(ctx.get(), encrypted.data(), &a, input.data(), 7) != 1 ||
        EVP_CipherUpdate(ctx.get(), encrypted.data() + a, &b, input.data() + 7, 12) != 1 ||
        EVP_CipherFinal_ex(ctx.get(), encrypted.data() + a + b, &f) != 1 ||
        a + b + f != 19 ||
        EVP_CIPHER_CTX_reset(ctx.get()) != 1 || !init(0) ||
        EVP_CipherUpdate(ctx.get(), recovered.data(), &d, encrypted.data(), 7) != 1 ||
        EVP_CipherUpdate(ctx.get(), recovered.data() + d, &e, encrypted.data() + 7,
                         a + b + f - 7) != 1 ||
        EVP_CipherFinal_ex(ctx.get(), recovered.data() + d + e, &g) != 1 ||
        d + e + g != 19 || std::memcmp(input.data(), recovered.data(), 19) != 0)
        return false;

    static constexpr unsigned char expected[16] = {
        0x76, 0xb8, 0xe0, 0xad, 0xa0, 0xf1, 0x3d, 0x90,
        0x40, 0x5d, 0x6a, 0xe5, 0x53, 0x86, 0xbd, 0x28
    };
    input.fill(0);
    int n = 0, m = 0;
    if (EVP_CIPHER_CTX_reset(ctx.get()) != 1 || !init(1) ||
        EVP_CipherUpdate(ctx.get(), encrypted.data(), &n, input.data(), 64) != 1 ||
        n != 64 || std::memcmp(encrypted.data(), expected, sizeof(expected)) != 0) return false;
    for (unsigned int i = 0; i < 4; ++i) iv[i] = 0xff;
    if (EVP_CIPHER_CTX_reset(ctx.get()) != 1 || !init(1) ||
        EVP_CipherUpdate(ctx.get(), encrypted.data(), &n, input.data(), 128) != 1 ||
        n != 128) return false;
    for (unsigned int i = 0; i < 4; ++i) iv[i] = 0;
    iv[4] = 1;
    return EVP_CIPHER_CTX_reset(ctx.get()) == 1 && init(1) &&
           EVP_CipherUpdate(ctx.get(), recovered.data(), &m, input.data(), 64) == 1 &&
           m == 64 && std::memcmp(encrypted.data() + 64, recovered.data(), 64) == 0;
}

std::string hexBytes(const std::array<unsigned char, 144>& bytes, std::size_t length) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(length * 2);
    for (std::size_t i = 0; i < length; ++i) {
        result += digits[bytes[i] >> 4];
        result += digits[bytes[i] & 15];
    }
    return result;
}

bool referenceChaCha20(const EVP_CIPHER* cipher, unsigned char counter,
                       std::array<unsigned char, 144>& output, int length) {
    std::array<unsigned char, 32> key{};
    std::array<unsigned char, 16> iv{};
    std::array<unsigned char, 128> plaintext{};
    for (unsigned int i = 0; i < key.size(); ++i) key[i] = static_cast<unsigned char>(i);
    for (unsigned int i = 0; i < plaintext.size(); ++i) plaintext[i] = static_cast<unsigned char>(i);
    iv[0] = counter;
    for (unsigned int i = 1; i < 8; ++i) iv[i] = 0xff;

    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(
        EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), cipher, nullptr, key.data(), iv.data()) != 1)
        return false;
    int produced = 0;
    int final = 0;
    return EVP_EncryptUpdate(ctx.get(), output.data(), &produced, plaintext.data(), length) == 1 &&
           EVP_EncryptFinal_ex(ctx.get(), output.data() + produced, &final) == 1 &&
           produced + final == length;
}

constexpr auto prelude = R"K(
    module __crypto_capabilities_test__;
    import k::crypto;

    containsName(seq: const Sequence<String>&, name: const String&) : bool {
        it: ConstIterator<String>! = seq.constIterator();
        cur: OptionalConstRef<String> = it.next();
        while (cur.hasValue()) {
            if (cur.get() == name) return true;
            cur = it.next();
        }
        return false;
    }

    emptyNames(seq: const Sequence<String>&) : bool {
        it: ConstIterator<String>! = seq.constIterator();
        return !it.next().hasValue();
    }

    uniqueNames(seq: const Sequence<String>&) : bool {
        outer: ConstIterator<String>! = seq.constIterator();
        value: OptionalConstRef<String> = outer.next();
        while (value.hasValue()) {
            inner: ConstIterator<String>! = seq.constIterator();
            candidate: OptionalConstRef<String> = inner.next();
            matches: unsigned int = 0u;
            while (candidate.hasValue()) {
                if (candidate.get() == value.get()) ++matches;
                candidate = inner.next();
            }
            if (matches != 1u) return false;
            value = outer.next();
        }
        return true;
    }

    sameNames(left: const Sequence<String>&, right: const Sequence<String>&) : bool {
        a: ConstIterator<String>! = left.constIterator();
        b: ConstIterator<String>! = right.constIterator();
        x: OptionalConstRef<String> = a.next();
        y: OptionalConstRef<String> = b.next();
        while (x.hasValue() && y.hasValue()) {
            if (x.get() != y.get()) return false;
            x = a.next();
            y = b.next();
        }
        return !x.hasValue() && !y.hasValue();
    }

    sameNameSet(left: const Sequence<String>&, right: const Sequence<String>&) : bool {
        a: ConstIterator<String>! = left.constIterator();
        x: OptionalConstRef<String> = a.next();
        while (x.hasValue()) {
            if (!containsName(right, x.get())) return false;
            x = a.next();
        }
        b: ConstIterator<String>! = right.constIterator();
        y: OptionalConstRef<String> = b.next();
        while (y.hasValue()) {
            if (!containsName(left, y.get())) return false;
            y = b.next();
        }
        return true;
    }

    usableDigest(name: const String&) : bool {
        digest: k::crypto::MessageDigest! = k::crypto::MessageDigest::fromName(name);
        if (!digest) return false;
        try {
            output: unsigned byte[]! = digest.finalize();
            return output.size > 0u;
        } catch (e: k::crypto::MessageDigestException&) {
            return false;
        }
    }

    advertisedIfUsable(names: const Sequence<String>&, name: const String&) : bool {
        return !usableDigest(name) || containsName(names, name);
    }

    advertisedCipherIfUsable(algorithms: const Sequence<String>&,
            modes: const Sequence<String>&, paddings: const Sequence<String>&,
            algorithm: const k::crypto::SymmetricAlgorithm&, bits: unsigned int,
            mode: const k::crypto::SymmetricMode&) : bool {
        cipher: k::crypto::SymmetricCipher! =
            k::crypto::SymmetricCipher::create(algorithm, bits, mode);
        if (!cipher) return true;
        return containsName(algorithms, algorithm) && containsName(modes, mode) &&
               containsName(paddings, cipher.padding());
    }

    hasConfiguration(seq: const Sequence<k::crypto::SymmetricCipherConfiguration>&,
                     field: int, name: const String&, algorithm: const String&,
                     bits: unsigned int, mode: const String&) : bool {
        it: ConstIterator<k::crypto::SymmetricCipherConfiguration>! = seq.constIterator();
        cur: OptionalConstRef<k::crypto::SymmetricCipherConfiguration> = it.next();
        while (cur.hasValue()) {
            c: const k::crypto::SymmetricCipherConfiguration& = cur.get();
            if (field == 0 && c.algorithm() == name) return true;
            if (field == 1 && c.mode() == name) return true;
            if (field == 2 && c.padding() == name) return true;
            if (field == 3 && c.algorithm() == algorithm &&
                c.keyBits() == bits && c.mode() == name) return true;
            if (field == 4 && c.algorithm() == algorithm &&
                c.keyBits() == bits && c.mode() == mode && c.padding() == name) return true;
            cur = it.next();
        }
        return false;
    }

    projectionHasWitnesses(names: const Sequence<String>&,
            configs: const Sequence<k::crypto::SymmetricCipherConfiguration>&,
            field: int, algorithm: const String&, bits: unsigned int,
            mode: const String&) : bool {
        it: ConstIterator<String>! = names.constIterator();
        cur: OptionalConstRef<String> = it.next();
        while (cur.hasValue()) {
            if (!hasConfiguration(configs, field, cur.get(), algorithm, bits, mode)) return false;
            cur = it.next();
        }
        return true;
    }

    firstGroup(configs: const Sequence<k::crypto::SymmetricCipherConfiguration>&,
               before: unsigned int, algorithm: const String&, bits: unsigned int,
               mode: const String&, matchMode: bool) : bool {
        it: ConstIterator<k::crypto::SymmetricCipherConfiguration>! = configs.constIterator();
        cur: OptionalConstRef<k::crypto::SymmetricCipherConfiguration> = it.next();
        index: unsigned int = 0u;
        while (cur.hasValue() && index < before) {
            c: const k::crypto::SymmetricCipherConfiguration& = cur.get();
            if (c.algorithm() == algorithm && c.keyBits() == bits &&
                (!matchMode || c.mode() == mode)) return false;
            ++index;
            cur = it.next();
        }
        return true;
    }

    filteredIncludesAll(names: const Sequence<String>&,
            configs: const Sequence<k::crypto::SymmetricCipherConfiguration>&,
            algorithm: const String&, bits: unsigned int, mode: const String&,
            matchMode: bool) : bool {
        it: ConstIterator<k::crypto::SymmetricCipherConfiguration>! = configs.constIterator();
        cur: OptionalConstRef<k::crypto::SymmetricCipherConfiguration> = it.next();
        while (cur.hasValue()) {
            c: const k::crypto::SymmetricCipherConfiguration& = cur.get();
            if (c.algorithm() == algorithm && c.keyBits() == bits) {
                if (!matchMode && !containsName(names, c.mode())) return false;
                if (matchMode && c.mode() == mode && !containsName(names, c.padding())) return false;
            }
            cur = it.next();
        }
        return true;
    }

    hasRawChaChaWitness(configs: const Sequence<k::crypto::SymmetricCipherConfiguration>&,
                        algorithm: const String&, mode: const String&,
                        padding: const String&) : bool {
        it: ConstIterator<k::crypto::SymmetricCipherConfiguration>! = configs.constIterator();
        cur: OptionalConstRef<k::crypto::SymmetricCipherConfiguration> = it.next();
        while (cur.hasValue()) {
            c: const k::crypto::SymmetricCipherConfiguration& = cur.get();
            if (c.algorithm() == algorithm && c.keyBits() == 256u && c.mode() == mode &&
                c.padding() == padding && c.nonceSize() == 16u && c.tagSize() == 0u) return true;
            cur = it.next();
        }
        return false;
    }

    sameConfigurations(left: const Sequence<k::crypto::SymmetricCipherConfiguration>&,
                       right: const Sequence<k::crypto::SymmetricCipherConfiguration>&) : bool {
        a: ConstIterator<k::crypto::SymmetricCipherConfiguration>! = left.constIterator();
        b: ConstIterator<k::crypto::SymmetricCipherConfiguration>! = right.constIterator();
        x: OptionalConstRef<k::crypto::SymmetricCipherConfiguration> = a.next();
        y: OptionalConstRef<k::crypto::SymmetricCipherConfiguration> = b.next();
        while (x.hasValue() && y.hasValue()) {
            if (x.get().algorithm() != y.get().algorithm() ||
                x.get().keyBits() != y.get().keyBits() ||
                x.get().mode() != y.get().mode() ||
                x.get().padding() != y.get().padding() ||
                x.get().nonceSize() != y.get().nonceSize() ||
                x.get().tagSize() != y.get().tagSize()) return false;
            x = a.next();
            y = b.next();
        }
        return !x.hasValue() && !y.hasValue();
    }
)K";

} // namespace

TEST_CASE("Capability digest snapshots own repeatable usable provider names",
          "[crypto][capabilities][digest]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                first: k::crypto::CryptoCapabilities! = k::crypto::CryptoCapabilities::snapshot();
                second: k::crypto::CryptoCapabilities! = k::crypto::CryptoCapabilities::snapshot();
                names: Sequence<k::crypto::MessageDigestAlgorithm>! = first.digestAlgorithms();
                delete first;
                other: Sequence<k::crypto::MessageDigestAlgorithm>! = second.digestAlgorithms();
                delete second;
                if (!uniqueNames(*names) || !uniqueNames(*other) ||
                    !sameNameSet(*names, *other)) return 1;
                if (!advertisedIfUsable(*names, k::crypto::MessageDigestAlgorithms::MD5) ||
                    !advertisedIfUsable(*names, k::crypto::MessageDigestAlgorithms::SHA1) ||
                    !advertisedIfUsable(*names, k::crypto::MessageDigestAlgorithms::SHA224) ||
                    !advertisedIfUsable(*names, k::crypto::MessageDigestAlgorithms::SHA256) ||
                    !advertisedIfUsable(*names, k::crypto::MessageDigestAlgorithms::SHA384) ||
                    !advertisedIfUsable(*names, k::crypto::MessageDigestAlgorithms::SHA512) ||
                    !advertisedIfUsable(*names, k::crypto::MessageDigestAlgorithms::SHA512_224) ||
                    !advertisedIfUsable(*names, k::crypto::MessageDigestAlgorithms::SHA512_256) ||
                    !advertisedIfUsable(*names, k::crypto::MessageDigestAlgorithms::SHA3_224) ||
                    !advertisedIfUsable(*names, k::crypto::MessageDigestAlgorithms::SHA3_256) ||
                    !advertisedIfUsable(*names, k::crypto::MessageDigestAlgorithms::SHA3_384) ||
                    !advertisedIfUsable(*names, k::crypto::MessageDigestAlgorithms::SHA3_512)) return 2;
                extension: k::crypto::MessageDigestAlgorithm = String("BLAKE2B-512");
                if (!advertisedIfUsable(*names, extension)) return 5;

                it: ConstIterator<k::crypto::MessageDigestAlgorithm>! = names.constIterator();
                cur: OptionalConstRef<k::crypto::MessageDigestAlgorithm> = it.next();
                count: unsigned int = 0u;
                while (cur.hasValue()) {
                    if (!usableDigest(cur.get())) return 6;
                    ++count;
                    cur = it.next();
                }
                if (count == 0u || !sameNames(*names, *names)) return 7;
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

TEST_CASE("Symmetric capability projections have exact constructible witnesses",
          "[crypto][capabilities][symmetric]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                snapshot: k::crypto::CryptoCapabilities! = k::crypto::CryptoCapabilities::snapshot();
                algorithms: Sequence<k::crypto::SymmetricAlgorithm>! = snapshot.symmetricAlgorithms();
                modes: Sequence<k::crypto::SymmetricMode>! = snapshot.symmetricModes();
                paddings: Sequence<k::crypto::SymmetricPadding>! = snapshot.symmetricPaddings();
                configs: Sequence<k::crypto::SymmetricCipherConfiguration>! =
                    snapshot.symmetricConfigurations();
                if (!uniqueNames(*algorithms) || !uniqueNames(*modes) ||
                    !uniqueNames(*paddings)) return 1;
                if (!advertisedCipherIfUsable(*algorithms, *modes, *paddings,
                        k::crypto::SymmetricAlgorithms::AES, 128u, k::crypto::SymmetricModes::GCM) ||
                    !advertisedCipherIfUsable(*algorithms, *modes, *paddings,
                        k::crypto::SymmetricAlgorithms::AES, 128u, k::crypto::SymmetricModes::CBC) ||
                    !advertisedCipherIfUsable(*algorithms, *modes, *paddings,
                        k::crypto::SymmetricAlgorithms::AES, 128u, k::crypto::SymmetricModes::CTR) ||
                    !advertisedCipherIfUsable(*algorithms, *modes, *paddings,
                        k::crypto::SymmetricAlgorithms::SM4, 128u, k::crypto::SymmetricModes::OFB) ||
                    !advertisedCipherIfUsable(*algorithms, *modes, *paddings,
                        k::crypto::SymmetricAlgorithms::ChaCha20, 256u,
                        k::crypto::SymmetricModes::Poly1305)) return 13;
                empty: String = String("");
                if (!projectionHasWitnesses(*algorithms, *configs, 0, empty, 0u, empty) ||
                    !projectionHasWitnesses(*modes, *configs, 1, empty, 0u, empty) ||
                    !projectionHasWitnesses(*paddings, *configs, 2, empty, 0u, empty)) return 2;

                unknown: k::crypto::SymmetricAlgorithm = String("UNKNOWN-PROVIDER-ALGORITHM");
                unknownMode: k::crypto::SymmetricMode = String("UNKNOWN-PROVIDER-MODE");
                noModes: Sequence<k::crypto::SymmetricMode>! = snapshot.symmetricModes(unknown, 128u);
                noPaddings: Sequence<k::crypto::SymmetricPadding>! =
                    snapshot.symmetricPaddings(unknown, 128u, unknownMode);
                noKnownPaddings: Sequence<k::crypto::SymmetricPadding>! =
                    snapshot.symmetricPaddings(k::crypto::SymmetricAlgorithms::AES, 128u, unknownMode);
                unsupported: Sequence<k::crypto::SymmetricPadding>! =
                    snapshot.symmetricPaddings(k::crypto::SymmetricAlgorithms::AES, 128u,
                                               k::crypto::SymmetricModes::Poly1305);
                if (!emptyNames(*noModes) || !emptyNames(*noPaddings) ||
                    !emptyNames(*noKnownPaddings) || !emptyNames(*unsupported)) return 3;
                noKeyModes: Sequence<k::crypto::SymmetricMode>! =
                    snapshot.symmetricModes(k::crypto::SymmetricAlgorithms::AES, 129u);
                if (!emptyNames(*noKeyModes)) return 4;
                retainedModes: Sequence<k::crypto::SymmetricMode>! =
                    snapshot.symmetricModes(k::crypto::SymmetricAlgorithms::AES, 128u);
                independentModes: Sequence<k::crypto::SymmetricMode>! =
                    snapshot.symmetricModes(k::crypto::SymmetricAlgorithms::AES, 128u);
                retainedPaddings: Sequence<k::crypto::SymmetricPadding>! =
                    snapshot.symmetricPaddings(k::crypto::SymmetricAlgorithms::AES, 128u,
                                               k::crypto::SymmetricModes::GCM);
                independentPaddings: Sequence<k::crypto::SymmetricPadding>! =
                    snapshot.symmetricPaddings(k::crypto::SymmetricAlgorithms::AES, 128u,
                                               k::crypto::SymmetricModes::GCM);

                it: ConstIterator<k::crypto::SymmetricCipherConfiguration>! = configs.constIterator();
                cur: OptionalConstRef<k::crypto::SymmetricCipherConfiguration> = it.next();
                count: unsigned int = 0u;
                while (cur.hasValue()) {
                    c: const k::crypto::SymmetricCipherConfiguration& = cur.get();
                    cipher: k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                        c.algorithm(), c.keyBits(), c.mode(), c.nonceSize(), c.tagSize(), c.padding());
                    if (!cipher || cipher.algorithm() != c.algorithm() ||
                        cipher.keyBits() != c.keyBits() || cipher.mode() != c.mode() ||
                        cipher.padding() != c.padding() || cipher.nonceSize() != c.nonceSize() ||
                        cipher.tagSize() != c.tagSize()) return 5;
                    if (!containsName(*algorithms, c.algorithm()) ||
                        !containsName(*modes, c.mode()) ||
                        !containsName(*paddings, c.padding())) return 6;
                    if (firstGroup(*configs, count, c.algorithm(), c.keyBits(), empty, false)) {
                        filteredModes: Sequence<k::crypto::SymmetricMode>! =
                            snapshot.symmetricModes(c.algorithm(), c.keyBits());
                        if (!uniqueNames(*filteredModes) ||
                            !filteredIncludesAll(*filteredModes, *configs, c.algorithm(),
                                                 c.keyBits(), empty, false) ||
                            !projectionHasWitnesses(*filteredModes, *configs, 3,
                                                    c.algorithm(), c.keyBits(), empty)) return 7;
                    }
                    if (firstGroup(*configs, count, c.algorithm(), c.keyBits(), c.mode(), true)) {
                        filteredPaddings: Sequence<k::crypto::SymmetricPadding>! =
                            snapshot.symmetricPaddings(c.algorithm(), c.keyBits(), c.mode());
                        if (!uniqueNames(*filteredPaddings) ||
                            !filteredIncludesAll(*filteredPaddings, *configs, c.algorithm(),
                                                 c.keyBits(), c.mode(), true) ||
                            !projectionHasWitnesses(*filteredPaddings, *configs, 4,
                                                    c.algorithm(), c.keyBits(), c.mode())) return 8;
                    }
                    ++count;
                    cur = it.next();
                }
                if (count == 0u) return 9;
                delete snapshot;
                if (!uniqueNames(*algorithms) || !uniqueNames(*modes) ||
                    !uniqueNames(*paddings) || !sameNames(*algorithms, *algorithms) ||
                    !sameNames(*retainedModes, *independentModes) ||
                    !sameNames(*retainedPaddings, *independentPaddings)) return 10;
                delete independentModes;
                delete independentPaddings;
                if (!sameNames(*retainedModes, *retainedModes) ||
                    !sameNames(*retainedPaddings, *retainedPaddings)) return 14;
                again: ConstIterator<k::crypto::SymmetricCipherConfiguration>! = configs.constIterator();
                after: OptionalConstRef<k::crypto::SymmetricCipherConfiguration> = again.next();
                seen: unsigned int = 0u;
                while (after.hasValue()) {
                    if (!containsName(*algorithms, after.get().algorithm())) return 11;
                    ++seen;
                    after = again.next();
                }
                if (seen != count) return 12;
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

TEST_CASE("Capability snapshots freeze provider selection in an isolated process",
          "[crypto][capabilities][provider]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        saved: k::crypto::CryptoCapabilities! = null;
        savedDigests: Sequence<k::crypto::MessageDigestAlgorithm>! = null;
        savedAlgorithms: Sequence<k::crypto::SymmetricAlgorithm>! = null;
        savedModes: Sequence<k::crypto::SymmetricMode>! = null;
        savedPaddings: Sequence<k::crypto::SymmetricPadding>! = null;
        savedConfigs: Sequence<k::crypto::SymmetricCipherConfiguration>! = null;

        prepare() : int {
            try {
                if (!usableDigest(k::crypto::MessageDigestAlgorithms::SHA256)) return 77;
                saved = k::crypto::CryptoCapabilities::snapshot();
                savedDigests = saved.digestAlgorithms();
                savedAlgorithms = saved.symmetricAlgorithms();
                savedModes = saved.symmetricModes();
                savedPaddings = saved.symmetricPaddings();
                savedConfigs = saved.symmetricConfigurations();
                if (!containsName(*savedDigests, k::crypto::MessageDigestAlgorithms::SHA256)) return 1;
                return 0;
            } catch (e: k::crypto::CryptoException&) {
                return 2;
            }
        }

        queueError() : int {
            missing: k::crypto::MessageDigest! = k::crypto::MessageDigest::fromName(
                String("KLANG-CAPABILITY-TEST-NO-SUCH-DIGEST"));
            return missing ? 1 : 0;
        }

        verify() : int {
            try {
                oldDigests: Sequence<k::crypto::MessageDigestAlgorithm>! = saved.digestAlgorithms();
                oldAlgorithms: Sequence<k::crypto::SymmetricAlgorithm>! = saved.symmetricAlgorithms();
                oldModes: Sequence<k::crypto::SymmetricMode>! = saved.symmetricModes();
                oldPaddings: Sequence<k::crypto::SymmetricPadding>! = saved.symmetricPaddings();
                oldConfigs: Sequence<k::crypto::SymmetricCipherConfiguration>! =
                    saved.symmetricConfigurations();
                if (!sameNames(*savedDigests, *oldDigests) ||
                    !sameNames(*savedAlgorithms, *oldAlgorithms) ||
                    !sameNames(*savedModes, *oldModes) ||
                    !sameNames(*savedPaddings, *oldPaddings) ||
                    !sameConfigurations(*savedConfigs, *oldConfigs)) return 4;

                fresh: k::crypto::CryptoCapabilities! = k::crypto::CryptoCapabilities::snapshot();
                freshDigests: Sequence<k::crypto::MessageDigestAlgorithm>! = fresh.digestAlgorithms();
                freshAlgorithms: Sequence<k::crypto::SymmetricAlgorithm>! = fresh.symmetricAlgorithms();
                freshModes: Sequence<k::crypto::SymmetricMode>! = fresh.symmetricModes();
                freshPaddings: Sequence<k::crypto::SymmetricPadding>! = fresh.symmetricPaddings();
                freshConfigs: Sequence<k::crypto::SymmetricCipherConfiguration>! =
                    fresh.symmetricConfigurations();
                configIt: ConstIterator<k::crypto::SymmetricCipherConfiguration>! =
                    freshConfigs.constIterator();
                if (!emptyNames(*freshDigests) || !emptyNames(*freshAlgorithms) ||
                    !emptyNames(*freshModes) || !emptyNames(*freshPaddings) ||
                    configIt.next().hasValue()) return 5;
                if (!containsName(*savedDigests, k::crypto::MessageDigestAlgorithms::SHA256)) return 6;
                return 0;
            } catch (e: k::crypto::CryptoException&) {
                return 7;
            }
        }
    )K");
    REQUIRE(jit);
    auto prepare = jit->lookup_symbol<int(*)()>("prepare");
    auto queueError = jit->lookup_symbol<int(*)()>("queueError");
    auto verify = jit->lookup_symbol<int(*)()>("verify");
    REQUIRE(prepare);
    REQUIRE(queueError);
    REQUIRE(verify);

    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        ERR_clear_error();
        const int result = prepare();
        if (result != 0) _exit(result);
        if (EVP_set_default_properties(nullptr, "provider=klangtestmissing") != 1) _exit(80);
        EVP_MD* probe = EVP_MD_fetch(nullptr, "SHA-256", nullptr);
        if (probe) {
            EVP_MD_free(probe);
            _exit(81);
        }
        ERR_clear_error();
        if (queueError() != 0) _exit(82);
        const auto expectedErrors = takeOpenSSLErrors();
        if (expectedErrors.empty() || queueError() != 0) _exit(83);
        const int checked = verify();
        if (checked != 0) _exit(checked);
        if (takeOpenSSLErrors() != expectedErrors) _exit(84);
        _exit(0);
    }
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    REQUIRE(waited == child);
    REQUIRE(WIFEXITED(status));
    const int result = WEXITSTATUS(status);
    if (result == 77) SKIP("SHA-256 is unavailable under the initial provider selection");
    CHECK(result == 0);
}

TEST_CASE("Capability snapshots honor the calling thread's OpenSSL library context",
          "[crypto][capabilities][provider]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        queueError() : int {
            missing: k::crypto::MessageDigest! = k::crypto::MessageDigest::fromName(
                String("KLANG-CAPABILITY-TEST-NO-SUCH-DIGEST"));
            return missing ? 1 : 0;
        }

        globalSnapshot() : int {
            try {
                snapshot: k::crypto::CryptoCapabilities! = k::crypto::CryptoCapabilities::snapshot();
                names: Sequence<k::crypto::MessageDigestAlgorithm>! = snapshot.digestAlgorithms();
                return containsName(*names, k::crypto::MessageDigestAlgorithms::SHA256) ? 0 : 1;
            } catch (e: k::crypto::CryptoException&) {
                return 2;
            }
        }

        restrictedSnapshot() : int {
            try {
                snapshot: k::crypto::CryptoCapabilities! = k::crypto::CryptoCapabilities::snapshot();
                digests: Sequence<k::crypto::MessageDigestAlgorithm>! = snapshot.digestAlgorithms();
                algorithms: Sequence<k::crypto::SymmetricAlgorithm>! = snapshot.symmetricAlgorithms();
                modes: Sequence<k::crypto::SymmetricMode>! = snapshot.symmetricModes();
                paddings: Sequence<k::crypto::SymmetricPadding>! = snapshot.symmetricPaddings();
                configs: Sequence<k::crypto::SymmetricCipherConfiguration>! =
                    snapshot.symmetricConfigurations();
                it: ConstIterator<k::crypto::SymmetricCipherConfiguration>! = configs.constIterator();
                if (!emptyNames(*digests) || !emptyNames(*algorithms) ||
                    !emptyNames(*modes) || !emptyNames(*paddings) ||
                    it.next().hasValue()) return 3;
                return 0;
            } catch (e: k::crypto::CryptoException&) {
                return 4;
            }
        }
    )K");
    REQUIRE(jit);
    auto queueError = jit->lookup_symbol<int(*)()>("queueError");
    auto globalSnapshot = jit->lookup_symbol<int(*)()>("globalSnapshot");
    auto restrictedSnapshot = jit->lookup_symbol<int(*)()>("restrictedSnapshot");
    REQUIRE(queueError);
    REQUIRE(globalSnapshot);
    REQUIRE(restrictedSnapshot);

    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        ERR_clear_error();
        if (EVP_set_default_properties(nullptr, "") != 1) _exit(80);
        EVP_MD* globalDigest = EVP_MD_fetch(nullptr, "SHA-256", nullptr);
        if (!globalDigest) _exit(77);
        EVP_MD_free(globalDigest);
        if (globalSnapshot() != 0) _exit(81);

        OSSL_LIB_CTX* custom = OSSL_LIB_CTX_new();
        if (!custom) _exit(82);
        OSSL_PROVIDER* provider = OSSL_PROVIDER_load(custom, "default");
        if (!provider) _exit(77);
        if (EVP_set_default_properties(custom, "provider=klangtestmissing") != 1) _exit(83);
        OSSL_LIB_CTX* previous = OSSL_LIB_CTX_set0_default(custom);
        if (OSSL_LIB_CTX_set0_default(nullptr) != custom) _exit(84);
        EVP_MD* unavailable = EVP_MD_fetch(nullptr, "SHA-256", nullptr);
        if (unavailable) {
            EVP_MD_free(unavailable);
            _exit(85);
        }
        ERR_clear_error();
        if (queueError() != 0) _exit(86);
        const auto expectedErrors = takeOpenSSLErrors();
        if (expectedErrors.empty() || queueError() != 0) _exit(87);
        if (restrictedSnapshot() != 0) _exit(88);
        if (OSSL_LIB_CTX_set0_default(nullptr) != custom) _exit(89);
        if (takeOpenSSLErrors() != expectedErrors) _exit(90);

        if (OSSL_LIB_CTX_set0_default(previous) != custom ||
            OSSL_LIB_CTX_set0_default(nullptr) != previous) _exit(91);
        EVP_MD* restored = EVP_MD_fetch(nullptr, "SHA-256", nullptr);
        if (!restored) _exit(92);
        EVP_MD_free(restored);
        OSSL_PROVIDER_unload(provider);
        OSSL_LIB_CTX_free(custom);
        _exit(0);
    }
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    REQUIRE(waited == child);
    REQUIRE(WIFEXITED(status));
    const int result = WEXITSTATUS(status);
    if (result == 77) SKIP("The default provider is unavailable");
    CHECK(result == 0);
}

TEST_CASE("Raw ChaCha20 capability supports its 64-bit counter boundary",
          "[crypto][capabilities][symmetric][chacha20]") {
    std::unique_ptr<EVP_CIPHER, decltype(&EVP_CIPHER_free)> providerCipher(
        EVP_CIPHER_fetch(nullptr, "CHACHA20", nullptr), EVP_CIPHER_free);
    const int available = rawChaChaSupported(providerCipher.get()) ? 1 : 0;
    std::array<unsigned char, 144> expected128{};
    std::array<unsigned char, 144> expected64{};
    if (available) {
        REQUIRE(referenceChaCha20(providerCipher.get(), 0xfe, expected128, 128));
        REQUIRE(referenceChaCha20(providerCipher.get(), 0xff, expected64, 64));
    }
    if (!available) ERR_clear_error();

    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        nibble(c: unsigned byte) : unsigned int {
            if (c <= (unsigned byte) '9') return (unsigned int) c - (unsigned int) '0';
            return (unsigned int) c - (unsigned int) 'a' + 10u;
        }

        fromHex(text: const String&) : unsigned byte[]! {
            encoded: unsigned byte[]! = text.toUtf8();
            length: unsigned int = (encoded.size - 1u) / 2u;
            result: unsigned byte[]! = new unsigned byte[length];
            i: unsigned int = 0u;
            while (i < length) {
                result[i] = (unsigned byte) ((nibble(encoded[2u * i]) << 4u) |
                                            nibble(encoded[2u * i + 1u]));
                ++i;
            }
            delete encoded;
            return result;
        }

        test(available: int) : int {
            try {
                algorithm: k::crypto::SymmetricAlgorithm = String("CHACHA20");
                mode: k::crypto::SymmetricMode = String("STREAM");
                padding: k::crypto::SymmetricPadding = String("NONE");
                snapshot: k::crypto::CryptoCapabilities! = k::crypto::CryptoCapabilities::snapshot();
                algorithms: Sequence<k::crypto::SymmetricAlgorithm>! = snapshot.symmetricAlgorithms();
                modes: Sequence<k::crypto::SymmetricMode>! = snapshot.symmetricModes(algorithm, 256u);
                paddings: Sequence<k::crypto::SymmetricPadding>! =
                    snapshot.symmetricPaddings(algorithm, 256u, mode);
                configs: Sequence<k::crypto::SymmetricCipherConfiguration>! =
                    snapshot.symmetricConfigurations();
                cipher: k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    algorithm, 256u, mode, 16u, 0u, padding);
                if (available == 0) {
                    if (cipher || containsName(*modes, mode)) return 1;
                    return 0;
                }
                if (!cipher || !containsName(*algorithms, algorithm) ||
                    !containsName(*modes, mode) || !containsName(*paddings, padding) ||
                    !hasRawChaChaWitness(*configs, algorithm, mode, padding)) return 2;
                if (cipher.algorithm() != algorithm || cipher.mode() != mode ||
                    cipher.keyBits() != 256u || cipher.keySize() != 32u ||
                    cipher.nonceSize() != 16u || cipher.tagSize() != 0u ||
                    cipher.padding() != padding || cipher.authenticated()) return 3;

                expected128: unsigned byte[]! = fromHex(String(")K" + hexBytes(expected128, 128) + R"K("));
                expected64: unsigned byte[]! = fromHex(String(")K" + hexBytes(expected64, 64) + R"K("));
                raw: unsigned byte[]! = new unsigned byte[32u];
                iv: unsigned byte[]! = new unsigned byte[16u];
                plain: unsigned byte[]! = new unsigned byte[129u];
                i: unsigned int = 0u;
                while (i < 32u) { raw[i] = (unsigned byte) i; ++i; }
                i = 0u;
                while (i < 16u) { iv[i] = (unsigned byte) 0; ++i; }
                iv[0u] = (unsigned byte) 0xfe;
                i = 1u;
                while (i < 8u) { iv[i] = (unsigned byte) 0xff; ++i; }
                i = 0u;
                while (i < 129u) { plain[i] = (unsigned byte) i; ++i; }

                key: k::crypto::SymmetricKey! = cipher.importKey(raw);
                enc: k::crypto::EncryptSession! = cipher.encrypt(key, iv);
                a: unsigned byte[]! = enc.update(plain, 0u, 63u);
                b: unsigned byte[]! = enc.update(plain, 63u, 65u);
                if (a.size != 63u || b.size != 65u) return 4;
                text: unsigned byte[]! = new unsigned byte[128u];
                i = 0u;
                while (i < 63u) { text[i] = a[i]; ++i; }
                i = 0u;
                while (i < 65u) { text[63u + i] = b[i]; ++i; }
                i = 0u;
                while (i < 128u) {
                    if (text[i] != expected128[i]) return 5;
                    ++i;
                }
                enc.finish();
                scratch: unsigned byte[]! = new unsigned byte[1u];
                if (enc.readFinal(scratch, 0u, 1u) != 0u) return 6;

                overflow: k::crypto::EncryptSession! = cipher.encrypt(key, iv);
                full: unsigned byte[]! = overflow.update(plain, 0u, 128u);
                rejected: bool = false;
                try {
                    extra: unsigned byte[]! = overflow.update(plain, 128u, 1u);
                } catch (e: k::crypto::CryptoException&) {
                    rejected = true;
                }
                if (!rejected) return 7;

                dec: k::crypto::DecryptSession! = cipher.decrypt(
                    key, iv, k::crypto::DecryptionStaging::Memory);
                dec.update(text, 0u, 128u);
                recovered: unsigned byte[]! = new unsigned byte[128u];
                if (dec.read(recovered, 0u, 128u) != 128u) return 8;
                i = 0u;
                while (i < 128u) {
                    if (recovered[i] != plain[i]) return 9;
                    ++i;
                }
                dec.finalize();

                decOverflow: k::crypto::DecryptSession! = cipher.decrypt(
                    key, iv, k::crypto::DecryptionStaging::Memory);
                decOverflow.update(text, 0u, 128u);
                rejected = false;
                try {
                    decOverflow.update(plain, 128u, 1u);
                } catch (e: k::crypto::CryptoException&) {
                    rejected = true;
                }
                if (!rejected) return 10;

                iv[0u] = (unsigned byte) 0xff;
                last: k::crypto::EncryptSession! = cipher.encrypt(key, iv);
                first: unsigned byte[]! = last.update(plain, 0u, 1u);
                middle: unsigned byte[]! = last.update(plain, 1u, 62u);
                end: unsigned byte[]! = last.update(plain, 63u, 1u);
                if (first.size != 1u || middle.size != 62u || end.size != 1u) return 11;
                lastText: unsigned byte[]! = new unsigned byte[64u];
                lastText[0u] = first[0u];
                i = 0u;
                while (i < 62u) { lastText[i + 1u] = middle[i]; ++i; }
                lastText[63u] = end[0u];
                i = 0u;
                while (i < 64u) {
                    if (lastText[i] != expected64[i]) return 12;
                    ++i;
                }
                last.finish();
                if (last.readFinal(scratch, 0u, 1u) != 0u) return 13;

                lastOverflow: k::crypto::EncryptSession! = cipher.encrypt(key, iv);
                part: unsigned byte[]! = lastOverflow.update(plain, 0u, 63u);
                edge: unsigned byte[]! = lastOverflow.update(plain, 63u, 1u);
                rejected = false;
                try {
                    extra: unsigned byte[]! = lastOverflow.update(plain, 64u, 1u);
                } catch (e: k::crypto::CryptoException&) {
                    rejected = true;
                }
                if (!rejected) return 14;

                lastDec: k::crypto::DecryptSession! = cipher.decrypt(
                    key, iv, k::crypto::DecryptionStaging::Memory);
                lastDec.update(lastText, 0u, 1u);
                lastDec.update(lastText, 1u, 62u);
                lastDec.update(lastText, 63u, 1u);
                if (lastDec.read(recovered, 0u, 64u) != 64u) return 15;
                i = 0u;
                while (i < 64u) {
                    if (recovered[i] != plain[i]) return 16;
                    ++i;
                }
                lastDec.finalize();

                lastDecOverflow: k::crypto::DecryptSession! = cipher.decrypt(
                    key, iv, k::crypto::DecryptionStaging::Memory);
                lastDecOverflow.update(lastText, 0u, 63u);
                lastDecOverflow.update(lastText, 63u, 1u);
                rejected = false;
                try {
                    lastDecOverflow.update(plain, 64u, 1u);
                } catch (e: k::crypto::CryptoException&) {
                    rejected = true;
                }
                if (!rejected) return 17;
                return 0;
            } catch (e: k::crypto::CryptoException&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)(int)>("test");
    REQUIRE(test);
    CHECK(test(available) == 0);
}
