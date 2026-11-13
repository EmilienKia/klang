/*
 * K Language HMAC capability snapshot integration tests
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
#include <openssl/params.h>
#include <openssl/provider.h>
#include <openssl/proverr.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

enum class MockFailure {
    digest, key, selective, unknown, silent, mixed, update, final, updateReject,
    finalReject, successError, badSize, badFinal
};

MockFailure mockFailure;

struct MockMac {
    size_t size;
    char* digest;
};

void* mockMacNew(void*) {
    return std::calloc(1, sizeof(MockMac));
}

void mockMacFree(void* ctx) {
    if (ctx) std::free(static_cast<MockMac*>(ctx)->digest);
    std::free(ctx);
}

int mockMacSetCtxParams(void* ctx, const OSSL_PARAM params[]) {
    if (!params) return 1;
    const OSSL_PARAM* param = OSSL_PARAM_locate_const(params, OSSL_MAC_PARAM_DIGEST);
    if (!param) return 1;
    const char* name = nullptr;
    if (!OSSL_PARAM_get_utf8_string_ptr(param, &name)) return 0;
    const size_t size = std::strlen(name) + 1;
    char* copy = static_cast<char*>(std::malloc(size));
    if (!copy) {
        ERR_raise(ERR_LIB_PROV, ERR_R_MALLOC_FAILURE);
        return 0;
    }
    std::memcpy(copy, name, size);
    auto* mac = static_cast<MockMac*>(ctx);
    std::free(mac->digest);
    mac->digest = copy;
    return 1;
}

const OSSL_PARAM* mockMacSettableCtxParams(void*, void*) {
    static const OSSL_PARAM params[] = {
        OSSL_PARAM_utf8_string(OSSL_MAC_PARAM_DIGEST, nullptr, 0),
        OSSL_PARAM_END
    };
    return params;
}

int mockMacInit(void* ctx, const unsigned char*, size_t, const OSSL_PARAM params[]) {
    if (mockFailure == MockFailure::digest || mockFailure == MockFailure::mixed) {
        ERR_raise(ERR_LIB_PROV, PROV_R_INVALID_DIGEST);
        if (mockFailure == MockFailure::mixed) ERR_raise(ERR_LIB_USER, 7);
        return 0;
    }
    if (mockFailure == MockFailure::key) {
        ERR_raise(ERR_LIB_PROV, PROV_R_KEY_SIZE_TOO_SMALL);
        return 0;
    }
    if (mockFailure == MockFailure::unknown) {
        ERR_raise(ERR_LIB_PROV, PROV_R_IN_ERROR_STATE);
        return 0;
    }
    if (mockFailure == MockFailure::silent) return 0;

    const OSSL_PARAM* param = params ? OSSL_PARAM_locate_const(params, OSSL_MAC_PARAM_DIGEST)
                                    : nullptr;
    const char* name = static_cast<MockMac*>(ctx)->digest;
    if (param && !OSSL_PARAM_get_utf8_string_ptr(param, &name)) return 0;
    if (!name) return 0;
    if (mockFailure == MockFailure::selective && std::strcmp(name, "SHA-256") == 0) {
        ERR_raise(ERR_LIB_PROV, PROV_R_INVALID_DIGEST);
        return 0;
    }
    EVP_MD* md = EVP_MD_fetch(nullptr, name, nullptr);
    if (!md) return 0;
    const int size = EVP_MD_get_size(md);
    EVP_MD_free(md);
    if (size <= 0) return 0;
    static_cast<MockMac*>(ctx)->size = static_cast<size_t>(size);
    if (mockFailure == MockFailure::successError) ERR_raise(ERR_LIB_USER, 7);
    return 1;
}

int mockMacUpdate(void*, const unsigned char*, size_t) {
    if (mockFailure == MockFailure::updateReject) {
        ERR_raise(ERR_LIB_PROV, PROV_R_DIGEST_NOT_ALLOWED);
        return 0;
    }
    if (mockFailure == MockFailure::update) {
        ERR_raise(ERR_LIB_PROV, PROV_R_IN_ERROR_STATE);
        return 0;
    }
    return 1;
}

int mockMacFinal(void* ctx, unsigned char* out, size_t* written, size_t capacity) {
    if (mockFailure == MockFailure::finalReject) {
        ERR_raise(ERR_LIB_PROV, PROV_R_NOT_SUPPORTED);
        return 0;
    }
    if (mockFailure == MockFailure::final) {
        ERR_raise(ERR_LIB_PROV, PROV_R_IN_ERROR_STATE);
        return 0;
    }
    const size_t size = static_cast<MockMac*>(ctx)->size;
    if (capacity < size) return 0;
    std::memset(out, 0, size);
    *written = mockFailure == MockFailure::badFinal ? size - 1 : size;
    return 1;
}

int mockMacGetCtxParams(void* ctx, OSSL_PARAM params[]) {
    OSSL_PARAM* size = OSSL_PARAM_locate(params, OSSL_MAC_PARAM_SIZE);
    if (size && !OSSL_PARAM_set_size_t(
            size, mockFailure == MockFailure::badSize ? 0 : static_cast<MockMac*>(ctx)->size)) {
        return 0;
    }
    return 1;
}

const OSSL_DISPATCH mockMacDispatch[] = {
    {OSSL_FUNC_MAC_NEWCTX, reinterpret_cast<void (*)(void)>(mockMacNew)},
    {OSSL_FUNC_MAC_FREECTX, reinterpret_cast<void (*)(void)>(mockMacFree)},
    {OSSL_FUNC_MAC_INIT, reinterpret_cast<void (*)(void)>(mockMacInit)},
    {OSSL_FUNC_MAC_UPDATE, reinterpret_cast<void (*)(void)>(mockMacUpdate)},
    {OSSL_FUNC_MAC_FINAL, reinterpret_cast<void (*)(void)>(mockMacFinal)},
    {OSSL_FUNC_MAC_GET_CTX_PARAMS, reinterpret_cast<void (*)(void)>(mockMacGetCtxParams)},
    {OSSL_FUNC_MAC_SET_CTX_PARAMS, reinterpret_cast<void (*)(void)>(mockMacSetCtxParams)},
    {OSSL_FUNC_MAC_SETTABLE_CTX_PARAMS, reinterpret_cast<void (*)(void)>(mockMacSettableCtxParams)},
    {0, nullptr}
};

const OSSL_ALGORITHM mockMacs[] = {
    {"HMAC", "provider=klanghmacmock", mockMacDispatch, nullptr},
    {nullptr, nullptr, nullptr, nullptr}
};

const OSSL_ALGORITHM* mockQuery(void*, int operation, int* noStore) {
    *noStore = 0;
    return operation == OSSL_OP_MAC ? mockMacs : nullptr;
}

const OSSL_DISPATCH mockProviderDispatch[] = {
    {OSSL_FUNC_PROVIDER_QUERY_OPERATION, reinterpret_cast<void (*)(void)>(mockQuery)},
    {0, nullptr}
};

int mockProviderInit(const OSSL_CORE_HANDLE*, const OSSL_DISPATCH*,
                     const OSSL_DISPATCH** out, void** ctx) {
    *out = mockProviderDispatch;
    *ctx = nullptr;
    return 1;
}

constexpr auto prelude = R"K(
    module __hmac_capabilities_test__;
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

    usableHmac(name: const String&) : bool {
        try {
            bytes: unsigned byte[32];
            i: unsigned int = 0u;
            while (i < 32u) {
                bytes[i] = (unsigned byte)i;
                ++i;
            }
            key: k::crypto::HmacKey! = k::crypto::HmacKey::fromBytes(bytes);
            if (!key) return false;
            empty: k::crypto::Hmac! = k::crypto::Hmac::create(name, *key);
            if (!empty) return false;
            emptySize: unsigned int = empty.tagSize();
            emptyTag: unsigned byte[]! = empty.finalize();
            if (emptySize == 0u || emptyTag.size != emptySize) return false;
            nonempty: k::crypto::Hmac! = k::crypto::Hmac::create(name, *key);
            if (!nonempty) return false;
            message: unsigned byte[1];
            message[0] = 0;
            nonempty.update(message);
            tag: unsigned byte[]! = nonempty.finalize();
            return tag.size == emptySize;
        } catch (e: k::crypto::CryptoException&) {
            return false;
        }
    }
)K";

int childResult(pid_t child) {
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    if (waited != child || !WIFEXITED(status)) return -1;
    return WEXITSTATUS(status);
}

} // namespace

TEST_CASE("HMAC capabilities own stable, unique, usable digest names",
          "[crypto][capabilities][hmac]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                snapshot: k::crypto::CryptoCapabilities! = k::crypto::CryptoCapabilities::snapshot();
                second: k::crypto::CryptoCapabilities! = k::crypto::CryptoCapabilities::snapshot();
                names: Sequence<k::crypto::MessageDigestAlgorithm>! = snapshot.hmacDigestAlgorithms();
                again: Sequence<k::crypto::MessageDigestAlgorithm>! = snapshot.hmacDigestAlgorithms();
                other: Sequence<k::crypto::MessageDigestAlgorithm>! = second.hmacDigestAlgorithms();
                digests: Sequence<k::crypto::MessageDigestAlgorithm>! = snapshot.digestAlgorithms();
                delete snapshot;
                delete second;
                if (!uniqueNames(*names) || !sameNames(*names, *again) ||
                    !sameNames(*names, *other) || !sameNames(*names, *names)) return 1;
                if (containsName(*names, String("SHAKE-128")) ||
                    containsName(*names, String("SHAKE-256")) ||
                    containsName(*names, String("SHAKE128")) ||
                    containsName(*names, String("SHAKE256"))) return 6;
                if (usableHmac(k::crypto::MessageDigestAlgorithms::SHA256) &&
                    !containsName(*names, k::crypto::MessageDigestAlgorithms::SHA256)) return 5;
                it: ConstIterator<k::crypto::MessageDigestAlgorithm>! = names.constIterator();
                cur: OptionalConstRef<k::crypto::MessageDigestAlgorithm> = it.next();
                while (cur.hasValue()) {
                    if (!containsName(*digests, cur.get())) return 2;
                    if (!usableHmac(cur.get())) return 3;
                    cur = it.next();
                }
                return 0;
            } catch (e: k::crypto::CryptoException&) {
                return 4;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("HMAC snapshots retain names after provider restrictions change",
          "[crypto][capabilities][hmac][provider]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        saved: k::crypto::CryptoCapabilities! = null;
        names: Sequence<k::crypto::MessageDigestAlgorithm>! = null;

        prepare() : int {
            try {
                saved = k::crypto::CryptoCapabilities::snapshot();
                names = saved.hmacDigestAlgorithms();
                it: ConstIterator<String>! = names.constIterator();
                return it.next().hasValue() ? 0 : 77;
            } catch (e: k::crypto::CryptoException&) {
                return 1;
            }
        }

        verify() : int {
            try {
                old: Sequence<k::crypto::MessageDigestAlgorithm>! = saved.hmacDigestAlgorithms();
                if (!sameNames(*names, *old)) return 2;
                fresh: k::crypto::CryptoCapabilities! = k::crypto::CryptoCapabilities::snapshot();
                restricted: Sequence<k::crypto::MessageDigestAlgorithm>! =
                    fresh.hmacDigestAlgorithms();
                it: ConstIterator<String>! = restricted.constIterator();
                if (it.next().hasValue()) return 3;
                delete saved;
                if (!sameNames(*names, *old)) return 4;
                return 0;
            } catch (e: k::crypto::CryptoException&) {
                return 5;
            }
        }
    )K");
    REQUIRE(jit);
    auto prepare = jit->lookup_symbol<int(*)()>("prepare");
    auto verify = jit->lookup_symbol<int(*)()>("verify");
    REQUIRE(prepare);
    REQUIRE(verify);
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        ERR_clear_error();
        const int result = prepare();
        if (result != 0) _exit(result);
        OSSL_LIB_CTX* custom = OSSL_LIB_CTX_new();
        if (!custom) _exit(80);
        OSSL_PROVIDER* provider = OSSL_PROVIDER_load(custom, "default");
        if (!provider) _exit(77);
        if (EVP_set_default_properties(custom, "provider=klangtestmissing") != 1) _exit(81);
        OSSL_LIB_CTX* previous = OSSL_LIB_CTX_set0_default(custom);
        if (!previous || OSSL_LIB_CTX_set0_default(nullptr) != custom) _exit(82);
        ERR_raise(ERR_LIB_USER, 7);
        const unsigned long error = ERR_peek_error();
        if (error == 0) _exit(83);
        const int checked = verify();
        if (checked != 0) _exit(checked);
        if (ERR_get_error() != error || ERR_get_error() != 0) _exit(84);
        if (OSSL_LIB_CTX_set0_default(nullptr) != custom) _exit(85);
        if (OSSL_LIB_CTX_set0_default(previous) != custom) _exit(86);
        OSSL_PROVIDER_unload(provider);
        OSSL_LIB_CTX_free(custom);
        _exit(0);
    }
    const int result = childResult(child);
    if (result == 77) SKIP("No HMAC digest is available under the initial provider selection");
    CHECK(result == 0);
}

TEST_CASE("A usable digest does not imply HMAC provider availability",
          "[crypto][capabilities][hmac][provider]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                snapshot: k::crypto::CryptoCapabilities! = k::crypto::CryptoCapabilities::snapshot();
                digests: Sequence<k::crypto::MessageDigestAlgorithm>! = snapshot.digestAlgorithms();
                hmac: Sequence<k::crypto::MessageDigestAlgorithm>! = snapshot.hmacDigestAlgorithms();
                it: ConstIterator<String>! = hmac.constIterator();
                if (!containsName(*digests, String("MD4"))) return 1;
                if (it.next().hasValue()) return 2;
                return 0;
            } catch (e: k::crypto::CryptoException&) {
                return 3;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        ERR_clear_error();
        OSSL_PROVIDER* legacy = OSSL_PROVIDER_load(nullptr, "legacy");
        if (!legacy) _exit(77);
        if (EVP_set_default_properties(nullptr, "provider=legacy") != 1) _exit(80);
        EVP_MD* md = EVP_MD_fetch(nullptr, "MD4", nullptr);
        EVP_MAC* mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
        if (!md || mac) {
            EVP_MD_free(md);
            EVP_MAC_free(mac);
            _exit(77);
        }
        EVP_MD_free(md);
        ERR_clear_error();
        _exit(test());
    }
    const int result = childResult(child);
    if (result == 77) SKIP("A legacy digest without a legacy HMAC provider is unavailable");
    CHECK(result == 0);
}

TEST_CASE("HMAC discovery distinguishes provider rejection from backend failure",
          "[crypto][capabilities][hmac][provider]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __hmac_capability_failure_test__;
        import k::crypto;

        probe() : int {
            try {
                snapshot: k::crypto::CryptoCapabilities! = k::crypto::CryptoCapabilities::snapshot();
                names: Sequence<k::crypto::MessageDigestAlgorithm>! = snapshot.hmacDigestAlgorithms();
                it: ConstIterator<String>! = names.constIterator();
                current: OptionalConstRef<String> = it.next();
                any: bool = current.hasValue();
                while (current.hasValue()) {
                    if (current.get() == String("SHA-256")) return 3;
                    current = it.next();
                }
                return any ? 2 : 0;
            } catch (e: k::crypto::CryptoOperationException&) {
                return 1;
            } catch (e: k::crypto::CryptoException&) {
                return 4;
            }
        }
    )K");
    REQUIRE(jit);
    auto probe = jit->lookup_symbol<int(*)()>("probe");
    REQUIRE(probe);
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        ERR_clear_error();
        OSSL_LIB_CTX* custom = OSSL_LIB_CTX_new();
        if (!custom) _exit(80);
        OSSL_PROVIDER* standard = OSSL_PROVIDER_load(custom, "default");
        if (!standard) _exit(77);
        if (!OSSL_PROVIDER_add_builtin(custom, "klanghmacmock", mockProviderInit)) _exit(81);
        OSSL_PROVIDER* mock = OSSL_PROVIDER_load(custom, "klanghmacmock");
        if (!mock) _exit(82);
        if (!EVP_set_default_properties(custom, "?provider=klanghmacmock")) _exit(83);
        EVP_MD* digest = EVP_MD_fetch(custom, "SHA-256", nullptr);
        if (!digest) _exit(77);
        EVP_MD_free(digest);
        digest = EVP_MD_fetch(custom, "SHA-512", nullptr);
        if (!digest) _exit(77);
        EVP_MD_free(digest);
        EVP_MAC* mac = EVP_MAC_fetch(custom, "HMAC", nullptr);
        if (!mac || EVP_MAC_get0_provider(mac) != mock) _exit(84);
        EVP_MAC_free(mac);
        OSSL_LIB_CTX* previous = OSSL_LIB_CTX_set0_default(custom);
        if (!previous) _exit(85);

        mockFailure = MockFailure::selective;
        const unsigned char key[32] = {};
        const unsigned char input[1] = {};
        unsigned char output[64];
        size_t written = 0;
        if (!EVP_Q_mac(custom, "HMAC", nullptr, "SHA-512", nullptr,
                       key, sizeof(key), input, sizeof(input), output,
                       sizeof(output), &written) || written != sizeof(output)) _exit(87);

        struct Case { MockFailure mode; int expected; };
        const Case cases[] = {
            {MockFailure::digest, 0}, {MockFailure::key, 0},
            {MockFailure::selective, 2},
            {MockFailure::updateReject, 0}, {MockFailure::finalReject, 0},
            {MockFailure::unknown, 1}, {MockFailure::silent, 1},
            {MockFailure::mixed, 1}, {MockFailure::update, 1},
            {MockFailure::final, 1}, {MockFailure::successError, 1},
            {MockFailure::badSize, 1}, {MockFailure::badFinal, 1}
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            mockFailure = cases[i].mode;
            ERR_clear_error();
            if (probe() != cases[i].expected || ERR_peek_error() != 0) _exit(10 + i);
            ERR_raise(ERR_LIB_USER, 99);
            const unsigned long error = ERR_peek_error();
            if (!error || probe() != cases[i].expected ||
                ERR_get_error() != error || ERR_get_error() != 0) _exit(40 + i);
        }
        if (OSSL_LIB_CTX_set0_default(nullptr) != custom ||
            OSSL_LIB_CTX_set0_default(previous) != custom) _exit(86);
        OSSL_PROVIDER_unload(mock);
        OSSL_PROVIDER_unload(standard);
        OSSL_LIB_CTX_free(custom);
        _exit(0);
    }
    const int result = childResult(child);
    if (result == 77) SKIP("The default provider or SHA-256 is unavailable");
    CHECK(result == 0);
}
