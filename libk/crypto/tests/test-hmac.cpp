/*
 * K Language HMAC integration tests
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */
#include <catch2/catch_all.hpp>

#include "crypto_test_helpers.hpp"

#include <openssl/core_dispatch.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/evperr.h>
#include <openssl/params.h>
#include <openssl/provider.h>

#include <memory>
#include <string>
#include <vector>

namespace {

struct ErrorEntry {
    unsigned long code;
    std::string file;
    int line;
    std::string function;
    std::string data;
    int flags;

    bool operator==(const ErrorEntry& other) const {
        return code == other.code && file == other.file && line == other.line &&
               function == other.function && data == other.data && flags == other.flags;
    }
};

struct ErrorQueueGuard {
    ~ErrorQueueGuard() { ERR_clear_error(); }
};

void seedErrors() {
    ERR_raise_data(ERR_LIB_USER, 100, "first caller error");
    ERR_raise_data(ERR_LIB_USER, 101, "second caller error");
}

std::vector<ErrorEntry> takeErrors() {
    std::vector<ErrorEntry> result;
    const char *file, *function, *data;
    int line, flags;
    unsigned long code;
    while ((code = ERR_get_error_all(&file, &line, &function, &data, &flags)) != 0) {
        result.push_back({code, file ? file : "", line, function ? function : "",
                          data ? data : "", flags});
    }
    return result;
}

struct ScopedDefaultContext {
    OSSL_LIB_CTX *previous;
    explicit ScopedDefaultContext(OSSL_LIB_CTX *ctx) : previous(OSSL_LIB_CTX_set0_default(ctx)) {}
    ~ScopedDefaultContext() { if (previous) OSSL_LIB_CTX_set0_default(previous); }
};

void *failingDigestNew(void *) {
    static int context;
    return &context;
}
void failingDigestFree(void *) {}
int failingDigestInit(void *, const OSSL_PARAM *) {
    ERR_raise(ERR_LIB_USER, ERR_R_MALLOC_FAILURE);
    ERR_raise(ERR_LIB_EVP, EVP_R_UNSUPPORTED_ALGORITHM);
    return 0;
}
int failingDigestUpdate(void *, const unsigned char *, size_t) { return 1; }
int failingDigestFinal(void *, unsigned char *, size_t *, size_t) { return 0; }
int failingDigestParams(OSSL_PARAM *params) {
    OSSL_PARAM *param = OSSL_PARAM_locate(params, OSSL_DIGEST_PARAM_BLOCK_SIZE);
    if (param && !OSSL_PARAM_set_size_t(param, 64)) return 0;
    param = OSSL_PARAM_locate(params, OSSL_DIGEST_PARAM_SIZE);
    if (param && !OSSL_PARAM_set_size_t(param, 32)) return 0;
    param = OSSL_PARAM_locate(params, OSSL_DIGEST_PARAM_XOF);
    if (param && !OSSL_PARAM_set_int(param, 0)) return 0;
    return 1;
}

const OSSL_DISPATCH failingDigestDispatch[] = {
    {OSSL_FUNC_DIGEST_NEWCTX, reinterpret_cast<void (*)(void)>(failingDigestNew)},
    {OSSL_FUNC_DIGEST_FREECTX, reinterpret_cast<void (*)(void)>(failingDigestFree)},
    {OSSL_FUNC_DIGEST_INIT, reinterpret_cast<void (*)(void)>(failingDigestInit)},
    {OSSL_FUNC_DIGEST_UPDATE, reinterpret_cast<void (*)(void)>(failingDigestUpdate)},
    {OSSL_FUNC_DIGEST_FINAL, reinterpret_cast<void (*)(void)>(failingDigestFinal)},
    {OSSL_FUNC_DIGEST_GET_PARAMS, reinterpret_cast<void (*)(void)>(failingDigestParams)},
    {0, nullptr}
};
const OSSL_ALGORITHM failingDigests[] = {
    {"KLANG-FAIL-DIGEST", nullptr, failingDigestDispatch, "Test-only failing digest"},
    {nullptr, nullptr, nullptr, nullptr}
};
const OSSL_ALGORITHM *failingProviderQuery(void *, int operation, int *noStore) {
    *noStore = 0;
    return operation == OSSL_OP_DIGEST ? failingDigests : nullptr;
}
const OSSL_DISPATCH failingProviderDispatch[] = {
    {OSSL_FUNC_PROVIDER_QUERY_OPERATION, reinterpret_cast<void (*)(void)>(failingProviderQuery)},
    {0, nullptr}
};
int failingProviderInit(const OSSL_CORE_HANDLE *, const OSSL_DISPATCH *,
                        const OSSL_DISPATCH **out, void **context) {
    *out = failingProviderDispatch;
    *context = nullptr;
    return 1;
}

bool providerAcceptsEmptyKey() {
    EVP_MAC *mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    if (!mac) {
        ERR_clear_error();
        return false;
    }
    EVP_MAC_CTX *ctx = EVP_MAC_CTX_new(mac);
    char name[] = "SHA-256";
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, name, 0),
        OSSL_PARAM_construct_end()
    };
    const unsigned char empty = 0;
    const bool accepted = ctx && EVP_MAC_init(ctx, &empty, 0, params) == 1;
    EVP_MAC_CTX_free(ctx);
    EVP_MAC_free(mac);
    ERR_clear_error();
    return accepted;
}

constexpr auto prelude = R"K(
    module __crypto_hmac_test__;
    import k::crypto;

    nibble(c: unsigned byte) : unsigned int {
        if (c <= (unsigned byte) '9') return (unsigned int) c - (unsigned int) '0';
        return (unsigned int) c - (unsigned int) 'a' + 10u;
    }

    fromHex(text: const String&) : unsigned byte[]! {
        encoded: unsigned byte[]! = text.toUtf8();
        length: unsigned int = (encoded.size - 1u) / 2u;
        bytes: unsigned byte[]! = new unsigned byte[length];
        i: unsigned int = 0u;
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
        hex: unsigned byte[]! = new unsigned byte[bytes.size * 2u + 1u];
        i: unsigned int = 0u;
        while (i < bytes.size) {
            value: unsigned int = (unsigned int) bytes[i];
            hex[2u * i] = (unsigned byte) hexDigit(value >> 4u);
            hex[2u * i + 1u] = (unsigned byte) hexDigit(value & 15u);
            ++i;
        }
        hex[bytes.size * 2u] = (unsigned byte) 0;
        result: String(hex);
        delete hex;
        return result;
    }

    repeated(value: unsigned byte, count: unsigned int) : unsigned byte[]! {
        data: unsigned byte[]! = new unsigned byte[count];
        i: unsigned int = 0u;
        while (i < count) {
            data[i] = value;
            ++i;
        }
        return data;
    }
)K";

} // namespace

TEST_CASE("HMAC SHA-256 and SHA-512 RFC 4231 vectors",
          "[crypto][hmac][vectors]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                raw: unsigned byte[]! = repeated((unsigned byte) 0x0b, 20u);
                key: k::crypto::HmacKey! = k::crypto::HmacKey::fromBytes(raw);
                if (key.keySize() != 20u) return 1;
                text: unsigned byte[]! = String("Hi There").toUtf8();
                sha256: k::crypto::Hmac! = k::crypto::Hmac::SHA256(key);
                sha512: k::crypto::Hmac! = k::crypto::Hmac::SHA512(key);
                if (!sha256 || !sha512 || sha256.tagSize() != 32u ||
                    sha512.tagSize() != 64u ||
                    sha256.algorithm() != k::crypto::MessageDigestAlgorithms::SHA256)
                    return 2;
                sha256.update(text, 0u, text.size - 1u);
                sha512.update(text, 0u, text.size - 1u);
                tag256: unsigned byte[]! = sha256.finalize();
                tag512: unsigned byte[]! = sha512.finalize();
                if (bytesToHex(tag256) != String(
                    "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"))
                    return 3;
                if (bytesToHex(tag512) != String(
                    "87aa7cdea5ef619d4ff0b4241a1d6cb02379f4e2ce4ec2787ad0b30545e17cdedaa833b7d6b8a702038b274eaea3f4e4be9d914eeb61f1702e696c203a126854"))
                    return 4;

                longRaw: unsigned byte[]! = repeated((unsigned byte) 0xaa, 131u);
                longKey: k::crypto::HmacKey! = k::crypto::HmacKey::fromBytes(longRaw);
                message: unsigned byte[]! =
                    String("Test Using Larger Than Block-Size Key - Hash Key First").toUtf8();
                long256: k::crypto::Hmac! = k::crypto::Hmac::create(
                    k::crypto::MessageDigestAlgorithms::SHA256, longKey);
                long512: k::crypto::Hmac! = k::crypto::Hmac::SHA512(longKey);
                if (!long256 || !long512 || longKey.keySize() != 131u) return 5;
                long256.update(message, 0u, message.size - 1u);
                long512.update(message, 0u, message.size - 1u);
                result256: unsigned byte[]! = long256.finalize();
                result512: unsigned byte[]! = long512.finalize();
                if (bytesToHex(result256) != String(
                    "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"))
                    return 6;
                if (bytesToHex(result512) != String(
                    "80b24263c7c1a3ebb71493c1dd7be8b49b46d1f41b4aeec1121b013783f8f3526b56d037e05f2598bd0fd2215d6a1e5295e64f73f63f0aec8b915a985d786598"))
                    return 7;
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

TEST_CASE("HMAC supports empty keys and messages when the provider accepts them",
          "[crypto][hmac][empty]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                empty: unsigned byte[]! = new unsigned byte[0u];
                key: k::crypto::HmacKey! = k::crypto::HmacKey::fromBytes(empty);
                if (key.keySize() != 0u) return 1;
                h: k::crypto::Hmac! = null;
                try {
                    h = k::crypto::Hmac::SHA256(key);
                } catch (e: k::crypto::CryptoException&) {
                    return -2;
                }
                if (!h) return -2;
                h.update(empty);
                h.update(empty, 0u, 0u);
                tag: unsigned byte[]! = h.finalize();
                if (bytesToHex(tag) != String(
                    "b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad"))
                    return 2;
                return 0;
            } catch (e: k::crypto::CryptoException&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    const bool acceptsEmptyKey = providerAcceptsEmptyKey();
    CHECK(test() == (acceptsEmptyKey ? 0 : -2));
}

TEST_CASE("HMAC copies imported keys and independently owns initialized contexts",
          "[crypto][hmac][ownership]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        makeHmac() : k::crypto::Hmac! throws(k::crypto::CryptoException) {
            raw: unsigned byte[]! = repeated((unsigned byte) 0x0b, 20u);
            key: k::crypto::HmacKey! = k::crypto::HmacKey::fromBytes(raw);
            raw[0u] = (unsigned byte) 0;
            h: k::crypto::Hmac! = k::crypto::Hmac::SHA256(key);
            delete key;
            delete raw;
            return h;
        }
        test() : int {
            try {
                h: k::crypto::Hmac! = makeHmac();
                if (!h) return 1;
                signedData: byte[10];
                signedData[0u] = (byte) 'x';
                signedData[1u] = (byte) 'H';
                signedData[2u] = (byte) 'i';
                signedData[3u] = (byte) ' ';
                signedData[4u] = (byte) 'T';
                signedData[5u] = (byte) 'h';
                signedData[6u] = (byte) 'e';
                signedData[7u] = (byte) 'r';
                signedData[8u] = (byte) 'e';
                signedData[9u] = (byte) 'x';
                h.update(signedData, 1u, 3u);
                unsignedData: unsigned byte[]! = String("There").toUtf8();
                h.update(unsignedData, 0u, 5u);
                h.update(signedData, 10u, 0u);
                tag: unsigned byte[]! = h.finalize();
                if (bytesToHex(tag) != String(
                    "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"))
                    return 2;
                whole: k::crypto::Hmac! = makeHmac();
                signedWhole: byte[8];
                i: unsigned int = 0u;
                while (i < 8u) {
                    signedWhole[i] = signedData[i + 1u];
                    ++i;
                }
                whole.update(signedWhole);
                fullTag: unsigned byte[]! = whole.finalize();
                if (!k::crypto::Hmac::constantTimeEquals(tag, fullTag)) return 3;
                unsignedWhole: unsigned byte[]! = new unsigned byte[8u];
                i = 0u;
                while (i < 8u) {
                    unsignedWhole[i] = (unsigned byte) signedWhole[i];
                    ++i;
                }
                unsignedHmac: k::crypto::Hmac! = makeHmac();
                unsignedHmac.update(unsignedWhole);
                unsignedTag: unsigned byte[]! = unsignedHmac.finalize();
                if (!k::crypto::Hmac::constantTimeEquals(tag, unsignedTag)) return 4;
                signedPart: byte[2];
                signedPart[0u] = signedData[2u];
                signedPart[1u] = signedData[3u];
                unsignedPart: unsigned byte[3];
                i = 0u;
                while (i < 3u) {
                    unsignedPart[i] = unsignedData[i];
                    ++i;
                }
                chained: k::crypto::Hmac! = makeHmac();
                chainTag: unsigned byte[]! = chained.update(signedData, 1u, 1u)
                    .update(signedPart).update(unsignedPart)
                    .update(unsignedData, 3u, 2u).finalize();
                if (!k::crypto::Hmac::constantTimeEquals(tag, chainTag)) return 5;
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

TEST_CASE("HMAC verification, truncation and terminal lifecycle",
          "[crypto][hmac][lifecycle]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        makeHmac(key: const k::crypto::HmacKey&) : k::crypto::Hmac!
                throws(k::crypto::CryptoException) {
            h: k::crypto::Hmac! = k::crypto::Hmac::SHA256(key);
            msg: unsigned byte[]! = String("Hi There").toUtf8();
            h.update(msg, 0u, 8u);
            return h;
        }
        test() : int {
            try {
                raw: unsigned byte[]! = repeated((unsigned byte) 0x0b, 20u);
                key: k::crypto::HmacKey! = k::crypto::HmacKey::fromBytes(raw);
                expected: unsigned byte[]! = fromHex(String(
                    "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
                prefix: unsigned byte[]! = fromHex(String("b0344c61d8db38535ca8afceaf0bf12b"));
                empty: unsigned byte[]! = new unsigned byte[0u];
                if (!k::crypto::Hmac::constantTimeEquals(expected, expected) ||
                    !k::crypto::Hmac::constantTimeEquals(empty, empty) ||
                    k::crypto::Hmac::constantTimeEquals(prefix, expected)) return 1;
                h: k::crypto::Hmac! = makeHmac(key);
                if (!h.verify(expected)) return 2;
                failed: bool = false;
                try { h.update(empty); }
                catch (e: k::crypto::CryptoStateException&) { failed = true; }
                if (!failed) return 3;
                failed = false;
                try { h.finalize(); }
                catch (e: k::crypto::CryptoStateException&) { failed = true; }
                if (!failed) return 4;

                mismatch: k::crypto::Hmac! = makeHmac(key);
                if (mismatch.verify(prefix)) return 5;
                failed = false;
                try { mismatch.verify(expected); }
                catch (e: k::crypto::CryptoStateException&) { failed = true; }
                if (!failed) return 6;

                trunc: k::crypto::Hmac! = makeHmac(key);
                shortened: unsigned byte[]! = trunc.finalizeTruncated(16u);
                if (!k::crypto::Hmac::constantTimeEquals(shortened, prefix)) return 7;
                verify: k::crypto::Hmac! = makeHmac(key);
                if (!verify.verifyTruncated(prefix, 16u)) return 8;
                wrongLength: k::crypto::Hmac! = makeHmac(key);
                if (wrongLength.verifyTruncated(expected, 16u)) return 9;
                failed = false;
                try { wrongLength.finalize(); }
                catch (e: k::crypto::CryptoStateException&) { failed = true; }
                if (!failed) return 15;
                prefix[15u] = (unsigned byte) 0;
                if (k::crypto::Hmac::constantTimeEquals(shortened, prefix)) return 16;
                wrongTag: k::crypto::Hmac! = makeHmac(key);
                if (wrongTag.verifyTruncated(prefix, 16u)) return 10;

                full: k::crypto::Hmac! = makeHmac(key);
                if (!full.verifyTruncated(expected, 32u)) return 17;
                one: k::crypto::Hmac! = makeHmac(key);
                first: unsigned byte[]! = one.finalizeTruncated(1u);
                if (first.size != 1u || first[0u] != expected[0u]) return 18;
                badFull: k::crypto::Hmac! = makeHmac(key);
                expected[31u] = (unsigned byte) 0;
                if (badFull.verify(expected)) return 19;
                expected[31u] = (unsigned byte) 0xf7;

                invalid: k::crypto::Hmac! = makeHmac(key);
                failed = false;
                try { invalid.finalizeTruncated(0u); }
                catch (e: k::crypto::CryptoInvalidInputException&) { failed = true; }
                if (!failed) return 11;
                failed = false;
                try { invalid.verifyTruncated(prefix, 33u); }
                catch (e: k::crypto::CryptoInvalidInputException&) { failed = true; }
                if (!failed) return 12;
                failed = false;
                try { invalid.update(empty, 1u, 0u); }
                catch (e: k::crypto::CryptoInvalidInputException&) { failed = true; }
                if (!failed) return 13;
                failed = false;
                try { invalid.update(expected, 31u, 2u); }
                catch (e: k::crypto::CryptoInvalidInputException&) { failed = true; }
                if (!failed || !invalid.verify(expected)) return 14;
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

TEST_CASE("HMAC rejects an unavailable digest without a fallback",
          "[crypto][hmac][provider]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                raw: unsigned byte[]! = repeated((unsigned byte) 1, 16u);
                key: k::crypto::HmacKey! = k::crypto::HmacKey::fromBytes(raw);
                missing: k::crypto::Hmac! =
                    k::crypto::Hmac::create(String("KLANG-NO-SUCH-DIGEST"), key);
                if (missing) return 1;
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

TEST_CASE("HMAC creation preserves preexisting OpenSSL errors and marks",
          "[crypto][hmac][provider][error-queue]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test(kind: int) : int {
            try {
                raw: unsigned byte[]! = repeated((unsigned byte) 1, 16u);
                key: k::crypto::HmacKey! = k::crypto::HmacKey::fromBytes(raw);
                if (kind == 0) {
                    missing: k::crypto::Hmac! =
                        k::crypto::Hmac::create(String("KLANG-NO-SUCH-DIGEST"), key);
                    return missing ? 1 : 0;
                }
                available: k::crypto::Hmac! = k::crypto::Hmac::SHA256(key);
                if (!available) return 2;
                tag: unsigned byte[]! = available.finalize();
                return tag.size == 32u ? 0 : 3;
            } catch (e: k::crypto::CryptoException&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)(int)>("test");
    REQUIRE(test);
    ErrorQueueGuard guard;
    ERR_clear_error();

    seedErrors();
    const auto expected = takeErrors();
    REQUIRE(expected.size() == 2);
    for (int kind : {0, 1}) {
        seedErrors();
        const int result = test(kind);
        const auto actual = takeErrors();
        CHECK(result == 0);
        CHECK(actual == expected);
    }

    ERR_raise_data(ERR_LIB_USER, 102, "first marked error");
    REQUIRE(ERR_set_mark() == 1);
    ERR_raise_data(ERR_LIB_USER, 103, "second marked error");
    REQUIRE(ERR_set_mark() == 1);
    CHECK(test(0) == 0);
    CHECK(ERR_pop_to_mark() == 1);
    CHECK(ERR_pop_to_mark() == 1);
    const auto remaining = takeErrors();
    REQUIRE(remaining.size() == 1);
    CHECK(ERR_GET_REASON(remaining[0].code) == 102);
    CHECK(remaining[0].data == "first marked error");
}

TEST_CASE("HMAC isolated creation retains the calling thread provider context",
          "[crypto][hmac][provider][error-queue]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                raw: unsigned byte[]! = repeated((unsigned byte) 1, 16u);
                key: k::crypto::HmacKey! = k::crypto::HmacKey::fromBytes(raw);
                h: k::crypto::Hmac! = k::crypto::Hmac::SHA256(key);
                return h ? 1 : 0;
            } catch (e: k::crypto::CryptoException&) {
                return -1;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    ErrorQueueGuard guard;
    std::unique_ptr<OSSL_LIB_CTX, decltype(&OSSL_LIB_CTX_free)> ctx(
        OSSL_LIB_CTX_new(), OSSL_LIB_CTX_free);
    REQUIRE(ctx);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> provider(
        OSSL_PROVIDER_load(ctx.get(), "default"), OSSL_PROVIDER_unload);
    REQUIRE(provider);
    REQUIRE(EVP_set_default_properties(ctx.get(), "provider=klang-no-such-provider") == 1);
    ScopedDefaultContext selected(ctx.get());
    REQUIRE(selected.previous);
    ERR_clear_error();
    seedErrors();
    const auto expected = takeErrors();
    seedErrors();
    const int result = test();
    const auto actual = takeErrors();
    CHECK(result == 0);
    CHECK(actual == expected);
}

TEST_CASE("HMAC provider errors do not become unavailable-algorithm results",
          "[crypto][hmac][provider][failure]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                raw: unsigned byte[]! = repeated((unsigned byte) 1, 16u);
                key: k::crypto::HmacKey! = k::crypto::HmacKey::fromBytes(raw);
                h: k::crypto::Hmac! =
                    k::crypto::Hmac::create(String("KLANG-FAIL-DIGEST"), key);
                return h ? 1 : 2;
            } catch (e: k::crypto::CryptoOperationException&) {
                return 0;
            } catch (e: k::crypto::CryptoException&) {
                return 3;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    ErrorQueueGuard guard;
    std::unique_ptr<OSSL_LIB_CTX, decltype(&OSSL_LIB_CTX_free)> ctx(
        OSSL_LIB_CTX_new(), OSSL_LIB_CTX_free);
    REQUIRE(ctx);
    REQUIRE(OSSL_PROVIDER_add_builtin(ctx.get(), "klang-failing", failingProviderInit) == 1);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> standard(
        OSSL_PROVIDER_load(ctx.get(), "default"), OSSL_PROVIDER_unload);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> failing(
        OSSL_PROVIDER_load(ctx.get(), "klang-failing"), OSSL_PROVIDER_unload);
    REQUIRE(standard);
    REQUIRE(failing);
    ScopedDefaultContext selected(ctx.get());
    REQUIRE(selected.previous);
    std::unique_ptr<EVP_MD, decltype(&EVP_MD_free)> digest(
        EVP_MD_fetch(ctx.get(), "KLANG-FAIL-DIGEST", nullptr), EVP_MD_free);
    REQUIRE(digest);
    REQUIRE(EVP_MD_get_size(digest.get()) == 32);
    std::unique_ptr<EVP_MAC, decltype(&EVP_MAC_free)> mac(
        EVP_MAC_fetch(ctx.get(), "HMAC", nullptr), EVP_MAC_free);
    REQUIRE(mac);
    std::unique_ptr<EVP_MAC_CTX, decltype(&EVP_MAC_CTX_free)> macctx(
        EVP_MAC_CTX_new(mac.get()), EVP_MAC_CTX_free);
    REQUIRE(macctx);
    char name[] = "KLANG-FAIL-DIGEST";
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, name, 0),
        OSSL_PARAM_construct_end()
    };
    const unsigned char key[16] = {};
    ERR_clear_error();
    REQUIRE(EVP_MAC_init(macctx.get(), key, sizeof(key), params) == 0);
    const auto nativeErrors = takeErrors();
    bool sawFatal = false;
    bool sawUnsupported = false;
    for (const auto& error : nativeErrors) {
        sawFatal |= ERR_FATAL_ERROR(error.code) != 0;
        sawUnsupported |= ERR_GET_LIB(error.code) == ERR_LIB_EVP &&
                          ERR_GET_REASON(error.code) == EVP_R_UNSUPPORTED_ALGORITHM;
    }
    REQUIRE(sawFatal);
    REQUIRE(sawUnsupported);

    CHECK(test() == 0);
    CHECK(ERR_peek_error() == 0);
    seedErrors();
    const auto expected = takeErrors();
    seedErrors();
    const int result = test();
    const auto actual = takeErrors();
    CHECK(result == 0);
    CHECK(actual == expected);
}
