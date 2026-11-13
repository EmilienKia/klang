/*
 * K Language HKDF integration tests
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

#include <dlfcn.h>
#include <openssl/async.h>
#include <openssl/core_dispatch.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/provider.h>
#include <openssl/proverr.h>

#include <chrono>
#include <csignal>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <string>
#include <thread>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

namespace {

constexpr auto prelude = R"K(
    module __crypto_hkdf_test__;
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
    repeated(value: unsigned byte, count: unsigned int) : unsigned byte[]! {
        bytes: unsigned byte[]! = new unsigned byte[count];
        i: unsigned int = 0u;
        while (i < count) { bytes[i] = value; ++i; }
        return bytes;
    }
    sequence(start: unsigned int, count: unsigned int) : unsigned byte[]! {
        bytes: unsigned byte[]! = new unsigned byte[count];
        i: unsigned int = 0u;
        while (i < count) { bytes[i] = (unsigned byte) (start + i); ++i; }
        return bytes;
    }
    equal(a: const unsigned byte[], b: const unsigned byte[]) : bool {
        return k::crypto::Hmac::constantTimeEquals(a, b);
    }
    checkVector(h: const k::crypto::Hkdf&, ikm: const unsigned byte[],
                salt: const unsigned byte[], info: const unsigned byte[],
                expectedPrk: const unsigned byte[], expectedOkm: const unsigned byte[])
            : bool throws(k::crypto::CryptoException) {
        p: k::crypto::HkdfPrk! = h.extract(ikm, salt);
        prk: unsigned byte[]! = p.exportBytes();
        if (!equal(prk, expectedPrk)) return false;
        expanded: unsigned byte[]! = p.expand(info, expectedOkm.size);
        combined: unsigned byte[]! = h.derive(ikm, salt, info, expectedOkm.size);
        imported: k::crypto::HkdfPrk! = h.importPrk(expectedPrk);
        importedOutput: unsigned byte[]! = imported.expand(info, expectedOkm.size);
        return equal(expanded, expectedOkm) && equal(combined, expectedOkm) &&
               equal(importedOutput, expectedOkm);
    }
)K";

bool providerDerives(size_t infoSize, size_t outputSize) {
    EVP_KDF *kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
    EVP_KDF_CTX *ctx = kdf ? EVP_KDF_CTX_new(kdf) : nullptr;
    char digest[] = "SHA-256";
    unsigned char ikm[22] = {0};
    unsigned char salt[32] = {0};
    std::vector<unsigned char> info(infoSize ? infoSize : 1, 7), out(outputSize ? outputSize : 1);
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest, 0),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, ikm, sizeof(ikm)),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, salt, sizeof(salt)),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, info.data(), infoSize),
        OSSL_PARAM_construct_end()
    };
    bool ok = ctx && EVP_KDF_derive(ctx, out.data(), outputSize, params) == 1;
    EVP_KDF_CTX_free(ctx);
    EVP_KDF_free(kdf);
    ERR_clear_error();
    return ok;
}

bool providerExpands(size_t infoSize, size_t outputSize) {
    EVP_KDF *kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
    EVP_KDF_CTX *ctx = kdf ? EVP_KDF_CTX_new(kdf) : nullptr;
    int mode = EVP_KDF_HKDF_MODE_EXPAND_ONLY;
    char digest[] = "SHA-256";
    unsigned char prk[32] = {};
    std::vector<unsigned char> info(infoSize ? infoSize : 1, 7), out(outputSize ? outputSize : 1);
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE, &mode),
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest, 0),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY, prk, sizeof(prk)),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, info.data(), infoSize),
        OSSL_PARAM_construct_end()
    };
    bool ok = ctx && EVP_KDF_derive(ctx, out.data(), outputSize, params) == 1;
    EVP_KDF_CTX_free(ctx);
    EVP_KDF_free(kdf);
    ERR_clear_error();
    return ok;
}

int mockFailure = 0;
int mockSetBehavior = 0;
int mockDerives = 0;
int mockNews = 0;
int mockSets = 0;
int mockDigestParams = 0;
size_t mockInfoLength = 0;
size_t mockKeyLength = 0;
size_t mockSaltLength = 0;
bool mockSaltZero = false;
bool mockFetchDigest = false;
bool mockNoStore = false;
int mockNoStoreKdfQueries = 0;
OSSL_LIB_CTX *mockObservedContext = nullptr;
bool mockBlock = false;
bool mockEntered = false;
bool mockRelease = false;
std::mutex mockMutex;
std::condition_variable mockCondition;
bool mockRegisterThreadStop = false;
bool mockEnforceThreadAffinity = false;
std::thread::id mockExpectedThread;
int mockAffinityViolations = 0;
const OSSL_CORE_HANDLE *mockCoreHandle = nullptr;
OSSL_FUNC_core_thread_start_fn *mockCoreThreadStart = nullptr;
std::thread::id mockProviderThread;
std::thread::id mockStopThread;
OSSL_LIB_CTX *mockStopContext = nullptr;
int mockStops = 0;

void mockThreadStop(void *context) {
    mockStopContext = static_cast<OSSL_LIB_CTX *>(context);
    mockStopThread = std::this_thread::get_id();
    ++mockStops;
}

bool mockWrongThread() {
    if (!mockEnforceThreadAffinity || std::this_thread::get_id() == mockExpectedThread)
        return false;
    ++mockAffinityViolations;
    ERR_raise(ERR_LIB_PROV, PROV_R_IN_ERROR_STATE);
    return true;
}

void *mockKdfNew(void *) {
    ++mockNews;
    if (mockWrongThread()) return nullptr;
    static int context;
    return &context;
}
void mockKdfFree(void *) {}
int mockKdfSet(void *, const OSSL_PARAM *) {
    ++mockSets;
    if (mockWrongThread()) return 0;
    switch (mockSetBehavior) {
        case 1: ERR_raise(ERR_LIB_PROV, PROV_R_INVALID_DIGEST_SIZE); return 0;
        case 2: ERR_raise(ERR_LIB_PROV, ERR_R_DISABLED); return 0;
        case 3: ERR_raise(ERR_LIB_PROV, PROV_R_INVALID_DIGEST_LENGTH); return 0;
        case 4: ERR_raise(ERR_LIB_PROV, PROV_R_INVALID_MODE); return 0;
        case 5: ERR_raise(ERR_LIB_PROV, ERR_R_MALLOC_FAILURE); return 0;
        case 6:
            ERR_raise(ERR_LIB_PROV, PROV_R_INVALID_DIGEST_SIZE);
            ERR_raise(ERR_LIB_PROV, PROV_R_IN_ERROR_STATE);
            return 0;
        case 7: ERR_raise(ERR_LIB_PROV, PROV_R_IN_ERROR_STATE); return 0;
        case 8: return 0;
        case 9: ERR_raise(ERR_LIB_USER, 55); return 1;
        default: return 1;
    }
}
int mockKdfDerive(void *, unsigned char *out, size_t length, const OSSL_PARAM *params) {
    ++mockDerives;
    if (mockWrongThread()) return 0;
    if (mockRegisterThreadStop) {
        mockProviderThread = std::this_thread::get_id();
        OSSL_LIB_CTX *context = OSSL_LIB_CTX_set0_default(nullptr);
        if (!mockCoreThreadStart ||
            !context || !mockCoreThreadStart(mockCoreHandle, mockThreadStop, context)) return 0;
    }
    if (mockFetchDigest) {
        mockObservedContext = OSSL_LIB_CTX_set0_default(nullptr);
        EVP_MD *digest = EVP_MD_fetch(nullptr, "SHA-256", nullptr);
        if (!digest) return 0;
        EVP_MD_free(digest);
    }
    if (mockBlock) {
        std::unique_lock lock(mockMutex);
        mockEntered = true;
        mockCondition.notify_all();
        mockCondition.wait(lock, [] { return mockRelease; });
        if (length) std::memset(out, 0xa5, length);
        return 1;
    }
    const OSSL_PARAM *info = OSSL_PARAM_locate_const(params, OSSL_KDF_PARAM_INFO);
    const OSSL_PARAM *key = OSSL_PARAM_locate_const(params, OSSL_KDF_PARAM_KEY);
    const OSSL_PARAM *salt = OSSL_PARAM_locate_const(params, OSSL_KDF_PARAM_SALT);
    mockInfoLength = info ? info->data_size : 0;
    mockKeyLength = key ? key->data_size : 0;
    mockSaltLength = salt ? salt->data_size : 0;
    mockSaltZero = salt && salt->data;
    if (mockSaltZero) {
        const auto *bytes = static_cast<const unsigned char *>(salt->data);
        for (size_t i = 0; i < salt->data_size; ++i) mockSaltZero &= bytes[i] == 0;
    }
    if (length) std::memset(out, mockFailure ? 0xa5 : 0, length);
    if (mockFailure) {
        ERR_raise(ERR_LIB_USER, ERR_R_INTERNAL_ERROR);
        return mockFailure == 2 ? 1 : 0;
    }
    return 1;
}
const OSSL_DISPATCH mockKdfDispatch[] = {
    {OSSL_FUNC_KDF_NEWCTX, reinterpret_cast<void (*)(void)>(mockKdfNew)},
    {OSSL_FUNC_KDF_FREECTX, reinterpret_cast<void (*)(void)>(mockKdfFree)},
    {OSSL_FUNC_KDF_SET_CTX_PARAMS, reinterpret_cast<void (*)(void)>(mockKdfSet)},
    {OSSL_FUNC_KDF_DERIVE, reinterpret_cast<void (*)(void)>(mockKdfDerive)},
    {0, nullptr}
};
const OSSL_ALGORITHM mockKdfs[] = {
    {"HKDF", "provider=klang-hkdf-test", mockKdfDispatch, "Test-only HKDF"},
    {nullptr, nullptr, nullptr, nullptr}
};
const OSSL_ALGORITHM *mockQuery(void *, int operation, int *noStore) {
    *noStore = mockNoStore ? 1 : 0;
    if (mockWrongThread()) return nullptr;
    if (mockNoStore && operation == OSSL_OP_KDF) ++mockNoStoreKdfQueries;
    return operation == OSSL_OP_KDF ? mockKdfs : nullptr;
}
const OSSL_DISPATCH mockProviderDispatch[] = {
    {OSSL_FUNC_PROVIDER_QUERY_OPERATION, reinterpret_cast<void (*)(void)>(mockQuery)},
    {0, nullptr}
};
int mockProviderInit(const OSSL_CORE_HANDLE *handle, const OSSL_DISPATCH *in,
                     const OSSL_DISPATCH **out, void **context) {
    mockCoreHandle = handle;
    mockCoreThreadStart = nullptr;
    for (const OSSL_DISPATCH *entry = in; entry->function_id; ++entry) {
        if (entry->function_id == OSSL_FUNC_CORE_THREAD_START)
            mockCoreThreadStart = OSSL_FUNC_core_thread_start(entry);
    }
    *out = mockProviderDispatch;
    *context = nullptr;
    return 1;
}

void *failingDigestNew(void *) {
    static int context;
    return &context;
}
void failingDigestFree(void *) {}
int failingDigestInit(void *, const OSSL_PARAM *) {
    ERR_raise(ERR_LIB_USER, ERR_R_INTERNAL_ERROR);
    return 0;
}
int failingDigestUpdate(void *, const unsigned char *, size_t) { return 1; }
int failingDigestFinal(void *, unsigned char *, size_t *, size_t) { return 0; }
int failingDigestParams(OSSL_PARAM *params) {
    ++mockDigestParams;
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
int variantDerives = 0;
int variantQueries = 0;
bool variantNoStore = false;
void *otherDigestNew(void *) {
    static int context;
    return &context;
}
const OSSL_DISPATCH otherDigestDispatch[] = {
    {OSSL_FUNC_DIGEST_NEWCTX, reinterpret_cast<void (*)(void)>(otherDigestNew)},
    {OSSL_FUNC_DIGEST_FREECTX, reinterpret_cast<void (*)(void)>(failingDigestFree)},
    {OSSL_FUNC_DIGEST_INIT, reinterpret_cast<void (*)(void)>(failingDigestInit)},
    {OSSL_FUNC_DIGEST_UPDATE, reinterpret_cast<void (*)(void)>(failingDigestUpdate)},
    {OSSL_FUNC_DIGEST_FINAL, reinterpret_cast<void (*)(void)>(failingDigestFinal)},
    {OSSL_FUNC_DIGEST_GET_PARAMS, reinterpret_cast<void (*)(void)>(failingDigestParams)},
    {0, nullptr}
};
int longDigestParams(OSSL_PARAM *params) {
    if (!failingDigestParams(params)) return 0;
    OSSL_PARAM *size = OSSL_PARAM_locate(params, OSSL_DIGEST_PARAM_SIZE);
    return !size || OSSL_PARAM_set_size_t(size, 64);
}
const OSSL_DISPATCH longDigestDispatch[] = {
    {OSSL_FUNC_DIGEST_NEWCTX, reinterpret_cast<void (*)(void)>(otherDigestNew)},
    {OSSL_FUNC_DIGEST_FREECTX, reinterpret_cast<void (*)(void)>(failingDigestFree)},
    {OSSL_FUNC_DIGEST_INIT, reinterpret_cast<void (*)(void)>(failingDigestInit)},
    {OSSL_FUNC_DIGEST_UPDATE, reinterpret_cast<void (*)(void)>(failingDigestUpdate)},
    {OSSL_FUNC_DIGEST_FINAL, reinterpret_cast<void (*)(void)>(failingDigestFinal)},
    {OSSL_FUNC_DIGEST_GET_PARAMS, reinterpret_cast<void (*)(void)>(longDigestParams)},
    {0, nullptr}
};
int otherKdfDerive(void *ctx, unsigned char *out, size_t length, const OSSL_PARAM *params) {
    ++variantDerives;
    return mockKdfDerive(ctx, out, length, params);
}
const OSSL_DISPATCH otherKdfDispatch[] = {
    {OSSL_FUNC_KDF_NEWCTX, reinterpret_cast<void (*)(void)>(mockKdfNew)},
    {OSSL_FUNC_KDF_FREECTX, reinterpret_cast<void (*)(void)>(mockKdfFree)},
    {OSSL_FUNC_KDF_SET_CTX_PARAMS, reinterpret_cast<void (*)(void)>(mockKdfSet)},
    {OSSL_FUNC_KDF_DERIVE, reinterpret_cast<void (*)(void)>(otherKdfDerive)},
    {0, nullptr}
};
const OSSL_ALGORITHM variantKdfs[] = {
    {"HKDF", "provider=klang-variant,klang.kdf=one", mockKdfDispatch, nullptr},
    {"HKDF", "provider=klang-variant,klang.kdf=two", otherKdfDispatch, nullptr},
    {nullptr, nullptr, nullptr, nullptr}
};
const OSSL_ALGORITHM variantDigests[] = {
    {"KLANG-VARIANT-DIGEST", "provider=klang-variant,klang.digest=one",
     failingDigestDispatch, nullptr},
    {"KLANG-VARIANT-DIGEST", "provider=klang-variant,klang.digest=two",
     otherDigestDispatch, nullptr},
    {"KLANG-VARIANT-DIGEST", "provider=klang-variant,klang.digest=long",
     longDigestDispatch, nullptr},
    {nullptr, nullptr, nullptr, nullptr}
};
const OSSL_ALGORITHM *variantQuery(void *, int operation, int *noStore) {
    ++variantQueries;
    *noStore = variantNoStore ? 1 : 0;
    if (operation == OSSL_OP_KDF) return variantKdfs;
    if (operation == OSSL_OP_DIGEST) return variantDigests;
    return nullptr;
}
const OSSL_DISPATCH variantProviderDispatch[] = {
    {OSSL_FUNC_PROVIDER_QUERY_OPERATION, reinterpret_cast<void (*)(void)>(variantQuery)},
    {0, nullptr}
};
int variantProviderInit(const OSSL_CORE_HANDLE *, const OSSL_DISPATCH *,
                        const OSSL_DISPATCH **out, void **context) {
    *out = variantProviderDispatch;
    *context = nullptr;
    return 1;
}
const OSSL_ALGORITHM failingDigests[] = {
    {"KLANG-FAIL-DIGEST", nullptr, failingDigestDispatch, "Test-only failing digest"},
    {nullptr, nullptr, nullptr, nullptr}
};
const OSSL_ALGORITHM *failingQuery(void *, int operation, int *noStore) {
    *noStore = 0;
    return operation == OSSL_OP_DIGEST ? failingDigests : nullptr;
}
const OSSL_DISPATCH failingProviderDispatch[] = {
    {OSSL_FUNC_PROVIDER_QUERY_OPERATION, reinterpret_cast<void (*)(void)>(failingQuery)},
    {0, nullptr}
};
int failingProviderInit(const OSSL_CORE_HANDLE *, const OSSL_DISPATCH *,
                        const OSSL_DISPATCH **out, void **context) {
    *out = failingProviderDispatch;
    *context = nullptr;
    return 1;
}

struct ScopedDefaultContext {
    OSSL_LIB_CTX *previous;
    explicit ScopedDefaultContext(OSSL_LIB_CTX *ctx) : previous(OSSL_LIB_CTX_set0_default(ctx)) {}
    ~ScopedDefaultContext() { if (previous) OSSL_LIB_CTX_set0_default(previous); }
};

using NativeHkdfNew = void *(*)(const unsigned char *, int *);
using NativeHkdfFree = void (*)(void *);
using NativeHkdfImport = void *(*)(const void *, const unsigned char *, unsigned, int *);
using NativeHkdfExtract = void *(*)(const void *, const unsigned char *, unsigned,
                                    const unsigned char *, unsigned, int, int *);
using NativeHkdfExpand = int (*)(const void *, const unsigned char *, unsigned,
                                 unsigned char *, unsigned);
using NativeHkdfDerive = int (*)(const void *, int, const unsigned char *, unsigned,
                                const unsigned char *, unsigned, int, const unsigned char *,
                                unsigned, unsigned char *, unsigned);

struct CancelWork {
    NativeHkdfDerive derive;
    const void *algorithm;
    unsigned char key[32] = {};
    unsigned char output[32] = {};
};

void *cancelHkdfCaller(void *arg) {
    auto *work = static_cast<CancelWork *>(arg);
    ERR_raise(ERR_LIB_USER, 90);
    work->derive(work->algorithm, 0, work->key, sizeof(work->key), nullptr, 0, 1,
                 nullptr, 0, work->output, sizeof(work->output));
    return nullptr;
}

struct AsyncHkdfWork {
    NativeHkdfNew make;
    NativeHkdfFree free;
    NativeHkdfFree freePrk;
    NativeHkdfImport importPrk;
    NativeHkdfExtract extract;
    NativeHkdfDerive derive;
    NativeHkdfExpand expand;
    int (*kProbe)();
    const void *algorithm;
    const void *prk;
    int expectedCancelState;
    unsigned long firstError;
    unsigned long lastError;
};

int asyncHkdfCaller(void *arg) {
    auto *work = *static_cast<AsyncHkdfWork **>(arg);
    int observed;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &observed) != 0) return 1;
    if (pthread_setcancelstate(observed, nullptr) != 0 || observed != work->expectedCancelState)
        return 2;
    unsigned char key[32] = {}, output[32];
    int status = 0;
    void *unexpected = work->make(
        reinterpret_cast<const unsigned char *>("KLANG-VARIANT-DIGEST"), &status);
    if (unexpected) work->free(unexpected);
    if (unexpected || status != -3) return 3;
    status = 0;
    unexpected = work->importPrk(work->algorithm, key, sizeof(key), &status);
    if (unexpected) work->freePrk(unexpected);
    if (unexpected || status != -3) return 4;
    status = 0;
    unexpected = work->extract(work->algorithm, key, sizeof(key), nullptr, 0, 1, &status);
    if (unexpected) work->freePrk(unexpected);
    if (unexpected || status != -3) return 5;
    std::memset(output, 0x5a, sizeof(output));
    if (work->derive(work->algorithm, 0, key, sizeof(key), nullptr, 0, 1, nullptr, 0,
                     output, sizeof(output)) != -3) return 6;
    for (auto byte : output) if (byte != 0) return 7;
    std::memset(output, 0x5a, sizeof(output));
    if (work->expand(work->prk, nullptr, 0, output, sizeof(output)) != -3) return 8;
    for (auto byte : output) if (byte != 0) return 9;
    if (work->kProbe() != 0) return 10;
    if (ERR_peek_error() != work->firstError || ERR_peek_last_error() != work->lastError)
        return 11;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &observed) != 0) return 12;
    if (pthread_setcancelstate(observed, nullptr) != 0 || observed != work->expectedCancelState)
        return 13;
    return 0;
}

struct ContextLifetimeWork {
    NativeHkdfDerive derive;
    const void *algorithm;
    OSSL_LIB_CTX *context;
    std::mutex mutex;
    std::condition_variable condition;
    bool completed = false;
    bool release = false;
    int status = -99;
    std::thread::id callerThread;
};

void *contextLifetimeCaller(void *arg) {
    auto *work = static_cast<ContextLifetimeWork *>(arg);
    unsigned char key[32] = {}, output[32] = {};
    ERR_clear_error();
    int status = work->derive(work->algorithm, 0, key, sizeof(key), nullptr, 0, 1,
                              nullptr, 0, output, sizeof(output));
    OPENSSL_thread_stop_ex(work->context);
    std::unique_lock lock(work->mutex);
    work->status = status;
    work->callerThread = std::this_thread::get_id();
    work->completed = true;
    work->condition.notify_all();
    work->condition.wait(lock, [work] { return work->release; });
    return nullptr;
}

} // namespace

TEST_CASE("HKDF SHA-256 RFC 5869 extract expand and combined vectors", "[crypto][hkdf]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                h: k::crypto::Hkdf! = k::crypto::Hkdf::SHA256();
                h512: k::crypto::Hkdf! = k::crypto::Hkdf::SHA512();
                if (!h || !h512 || h.hashSize() != 32u || h.maxOutputSize() != 8160u ||
                    h512.hashSize() != 64u || h512.maxOutputSize() != 16320u ||
                    h.algorithm() != k::crypto::MessageDigestAlgorithms::SHA256) return 1;
                ikm: unsigned byte[]! = repeated((unsigned byte) 0x0b, 22u);
                salt: unsigned byte[]! = sequence(0u, 13u);
                info: unsigned byte[]! = sequence(0xf0u, 10u);
                prk: unsigned byte[]! = fromHex(String(
                    "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5"));
                okm: unsigned byte[]! = fromHex(String(
                    "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"));
                if (!checkVector(h, ikm, salt, info, prk, okm)) return 2;
                ikm2: unsigned byte[]! = sequence(0u, 80u);
                salt2: unsigned byte[]! = sequence(0x60u, 80u);
                info2: unsigned byte[]! = sequence(0xb0u, 80u);
                prk2: unsigned byte[]! = fromHex(String(
                    "06a6b88c5853361a06104c9ceb35b45cef760014904671014a193f40c15fc244"));
                okm2: unsigned byte[]! = fromHex(String(
                    "b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c59045a99cac7827271cb41c65e590e09da3275600c2f09b8367793a9aca3db71cc30c58179ec3e87c14c01d5c1f3434f1d87"));
                if (!checkVector(h, ikm2, salt2, info2, prk2, okm2)) return 3;
                empty: unsigned byte[]! = new unsigned byte[0u];
                prk3: unsigned byte[]! = fromHex(String(
                    "19ef24a32c717b167f33a91d6f648bdf96596776afdb6377ac434c1c293ccb04"));
                okm3: unsigned byte[]! = fromHex(String(
                    "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8"));
                if (!checkVector(h, ikm, empty, empty, prk3, okm3)) return 4;
                p: k::crypto::HkdfPrk! = h.extract(ikm);
                if (!equal(p.exportBytes(), prk3) || !equal(h.derive(ikm, empty, 42u), okm3))
                    return 5;
                p512: k::crypto::HkdfPrk! = h512.extract(ikm, salt);
                if (!equal(p512.expand(info, 48u), h512.derive(ikm, salt, info, 48u))) return 6;
                extension: k::crypto::Hkdf! = k::crypto::Hkdf::create(String("SHA3-256"));
                if (extension) {
                    px: k::crypto::HkdfPrk! = extension.extract(ikm, salt);
                    if (!equal(px.expand(info, 48u), extension.derive(ikm, salt, info, 48u)))
                        return 7;
                }
                return 0;
            } catch (e: k::crypto::CryptoException&) { return -1; }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("HKDF SHA-1 RFC 5869 appendix A.4 to A.7 vectors", "[crypto][hkdf][rfc5869]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                h: k::crypto::Hkdf! = k::crypto::Hkdf::create(k::crypto::MessageDigestAlgorithms::SHA1);
                if (!h) return 77;
                if (h.hashSize() != 20u || h.maxOutputSize() != 5100u) return 1;
                empty: unsigned byte[]! = new unsigned byte[0u];

                ikm4: unsigned byte[]! = repeated((unsigned byte) 0x0b, 11u);
                if (!checkVector(h, ikm4, sequence(0u, 13u), sequence(0xf0u, 10u),
                    fromHex(String("9b6c18c432a7bf8f0e71c8eb88f4b30baa2ba243")),
                    fromHex(String("085a01ea1b10f36933068b56efa5ad81a4f14b822f5b091568a9cdd4f155fda2c22e422478d305f3f896"))))
                    return 2;

                if (!checkVector(h, sequence(0u, 80u), sequence(0x60u, 80u), sequence(0xb0u, 80u),
                    fromHex(String("8adae09a2a307059478d309b26c4115a224cfaf6")),
                    fromHex(String("0bd770a74d1160f7c9f12cd5912a06ebff6adcae899d92191fe4305673ba2ffe8fa3f1a4e5ad79f3f334b3b202b2173c486ea37ce3d397ed034c7f9dfeb15c5e927336d0441f4c4300e2cff0d0900b52d3b4"))))
                    return 3;

                ikm6: unsigned byte[]! = repeated((unsigned byte) 0x0b, 22u);
                if (!checkVector(h, ikm6, empty, empty,
                    fromHex(String("da8c8a73c7fa77288ec6f5e7c297786aa0d32d01")),
                    fromHex(String("0ac1af7002b3d761d1e55298da9d0506b9ae52057220a306e07b6b87e8df21d0ea00033de03984d34918"))))
                    return 4;

                ikm7: unsigned byte[]! = repeated((unsigned byte) 0x0c, 22u);
                prk7: unsigned byte[]! = fromHex(String("2adccada18779e7c2077ad2eb19d3f3e731385dd"));
                okm7: unsigned byte[]! = fromHex(String(
                    "2c91117204d745f3500d636a62f64f0ab3bae548aa53d423b0d1f27ebba6f5e5673a081d70cce7acfc48"));
                p7: k::crypto::HkdfPrk! = h.extract(ikm7);
                if (!equal(p7.exportBytes(), prk7) || !equal(p7.expand(empty, 42u), okm7) ||
                    !equal(h.derive(ikm7, empty, 42u), okm7)) return 5;
                zeros: unsigned byte[]! = repeated((unsigned byte) 0, 20u);
                if (!equal(h.extract(ikm7, zeros).exportBytes(), prk7) ||
                    !equal(h.extract(ikm7, empty).exportBytes(), prk7)) return 6;
                return 0;
            } catch (e: k::crypto::CryptoException&) { return -1; }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    const int result = test();
    if (result == 77) SKIP("SHA-1 HKDF is unavailable from the selected provider");
    CHECK(result == 0);
}

TEST_CASE("HKDF SHA-224 SHA-384 and SHA-512 match independent HMAC-based vectors",
          "[crypto][hkdf][rfc5869]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        check(algorithm: const String&, hashSize: unsigned int, prkHex: const String&,
              okmHex: const String&) : int {
            try {
                h: k::crypto::Hkdf! = k::crypto::Hkdf::create(algorithm);
                if (!h) return 77;
                if (h.hashSize() != hashSize || h.maxOutputSize() != hashSize * 255u ||
                    h.algorithm() != algorithm) return 1;
                ikm: unsigned byte[]! = repeated((unsigned byte) 0x0b, 22u);
                if (!checkVector(h, ikm, sequence(0u, 13u), sequence(0xf0u, 10u),
                                 fromHex(prkHex), fromHex(okmHex))) return 2;
                return 0;
            } catch (e: k::crypto::CryptoException&) { return -1; }
        }
        sha224() : int {
            return check(String("SHA-224"), 28u,
                String("94f65bed12265c1fa2747db60cadfcabbbbaede6be5a7a450de78231"),
                String("2f21cd7cbc818ca5c561b933728e2e08e154a87e1432399a820dee13aa222d0cee6152fa539ab70f8e80"));
        }
        sha384() : int {
            return check(String("SHA-384"), 48u,
                String("704b39990779ce1dc548052c7dc39f303570dd13fb39f7acc564680bef80e8dec70ee9a7e1f3e293ef68eceb072a5ade"),
                String("9b5097a86038b805309076a44b3a9f38063e25b516dcbf369f394cfab43685f748b6457763e4f0204fc5"));
        }
        sha512() : int {
            return check(String("SHA-512"), 64u,
                String("665799823737ded04a88e47e54a5890bb2c3d247c7a4254a8e61350723590a26c36238127d8661b88cf80ef802d57e2f7cebcf1e00e083848be19929c61b4237"),
                String("832390086cda71fb47625bb5ceb168e4c8e26a1a16ed34d9fc7fe92c1481579338da362cb8d9f925d7cb"));
        }
    )K");
    REQUIRE(jit);
    for (const char *name : {"sha224", "sha384", "sha512"}) {
        DYNAMIC_SECTION(name) {
            auto test = jit->lookup_symbol<int(*)()>(name);
            REQUIRE(test);
            const int result = test();
            if (result == 77) SKIP("digest HKDF is unavailable from the selected provider");
            CHECK(result == 0);
        }
    }
}

TEST_CASE("HKDF empty input keying material and maximum output boundary",
          "[crypto][hkdf][rfc5869]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                h: k::crypto::Hkdf! = k::crypto::Hkdf::SHA256();
                h512: k::crypto::Hkdf! = k::crypto::Hkdf::SHA512();
                if (!h || !h512) return 77;
                empty: unsigned byte[]! = new unsigned byte[0u];
                salt: unsigned byte[]! = sequence(0u, 13u);
                info: unsigned byte[]! = sequence(0xf0u, 10u);
                if (!checkVector(h, empty, salt, info,
                    fromHex(String("90a33d186b940bac8a4e69efce8b74ba4c718640140d54d853b5e30ed8d7706b")),
                    fromHex(String("4dd449ba1911c57d79603e7e902452f79601b5e4d7b235ce0e11a7789a17766099725013cd1c97a52d03"))))
                    return 1;
                p: k::crypto::HkdfPrk! = h.extract(empty);
                if (!equal(p.exportBytes(), fromHex(String(
                        "b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad"))) ||
                    !equal(p.expand(empty, 42u), fromHex(String(
                        "eb70f01dede9afafa449eee1b1286504e1f62388b3f7dd4f956697b0e828fe181e59c2ec0fe6e7e7ac26"))))
                    return 2;

                ikm: unsigned byte[]! = repeated((unsigned byte) 0x0b, 22u);
                okm: unsigned byte[]! = h.derive(ikm, salt, info, h.maxOutputSize());
                if (okm.size != 8160u) return 3;
                head: unsigned byte[]! = fromHex(String(
                    "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"));
                tail: unsigned byte[]! = fromHex(String(
                    "76a3f78bcffe95fecf91923c22ad6ee64d48a6d1b981d7e523d5c0f22154ee88"));
                i: unsigned int = 0u;
                while (i < 32u) {
                    if (okm[i] != head[i] || okm[okm.size - 32u + i] != tail[i]) return 4;
                    ++i;
                }
                okm512: unsigned byte[]! = h512.derive(ikm, salt, info, h512.maxOutputSize());
                tail512: unsigned byte[]! = fromHex(String(
                    "6f4c862c43cc05f02bbc375f8e523cf7a8148162f9266a8e90e9de9261973f47931623de9936c096438e9f180736960acc54a28763012fec34d40b7c12ee8560"));
                if (okm512.size != 16320u) return 5;
                i = 0u;
                while (i < 64u) {
                    if (okm512[okm512.size - 64u + i] != tail512[i]) return 6;
                    ++i;
                }
                return 0;
            } catch (e: k::crypto::CryptoException&) { return -1; }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("HKDF key-producing helpers reject lengths above the RFC limit",
          "[crypto][hkdf][rfc5869]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                h: k::crypto::Hkdf! = k::crypto::Hkdf::SHA256();
                raw: unsigned byte[]! = sequence(0u, 32u);
                info: unsigned byte[]! = sequence(0xf0u, 10u);
                key: k::crypto::HmacKey! = k::crypto::HmacKey::fromBytes(raw);
                prk: k::crypto::HkdfPrk! = h.importPrk(raw);
                tooLong: unsigned int = h.maxOutputSize() + 1u;
                rejected: int = 0;
                try { h.deriveHmacKey(raw, info, tooLong); }
                catch (e: k::crypto::CryptoInvalidInputException&) { ++rejected; }
                try { h.deriveHmacKey(key, raw, info, tooLong); }
                catch (e: k::crypto::CryptoInvalidInputException&) { ++rejected; }
                try { h.derive(key, info, tooLong); }
                catch (e: k::crypto::CryptoInvalidInputException&) { ++rejected; }
                try { prk.expandHmacKey(info, tooLong); }
                catch (e: k::crypto::CryptoInvalidInputException&) { ++rejected; }
                try { prk.expand(info, 0xffffffffu); }
                catch (e: k::crypto::CryptoInvalidInputException&) { ++rejected; }
                if (rejected != 5) return 1;
                exact: k::crypto::HmacKey! = h.deriveHmacKey(raw, info, h.maxOutputSize());
                if (exact.keySize() != h.maxOutputSize()) return 2;
                return 0;
            } catch (e: k::crypto::CryptoException&) { return -1; }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("HKDF descriptors and PRKs can be shared by concurrent readers",
          "[crypto][hkdf][thread]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        sharedHkdf: k::crypto::Hkdf! = null;
        sharedPrk: k::crypto::HkdfPrk! = null;
        setup() : int {
            try {
                sharedHkdf = k::crypto::Hkdf::SHA256();
                sharedPrk = sharedHkdf.importPrk(fromHex(String(
                    "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5")));
                return 0;
            } catch (e: k::crypto::CryptoException&) { return -1; }
        }
        worker(iterations: int) : int {
            try {
                ikm: unsigned byte[]! = repeated((unsigned byte) 0x0b, 22u);
                salt: unsigned byte[]! = sequence(0u, 13u);
                info: unsigned byte[]! = sequence(0xf0u, 10u);
                okm: unsigned byte[]! = fromHex(String(
                    "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"));
                i: int = 0;
                while (i < iterations) {
                    p: k::crypto::HkdfPrk! = sharedHkdf.extract(ikm, salt);
                    if (!equal(sharedHkdf.derive(ikm, salt, info, 42u), okm) ||
                        !equal(sharedPrk.expand(info, 42u), okm) ||
                        !equal(p.expand(info, 42u), okm)) return 1;
                    ++i;
                }
                return 0;
            } catch (e: k::crypto::CryptoException&) { return -1; }
        }
        teardown() {
            sharedPrk = null;
            sharedHkdf = null;
        }
    )K");
    REQUIRE(jit);
    auto setup = jit->lookup_symbol<int(*)()>("setup");
    auto worker = jit->lookup_symbol<int(*)(int)>("worker");
    auto teardown = jit->lookup_symbol<void(*)()>("teardown");
    REQUIRE(setup);
    REQUIRE(worker);
    REQUIRE(teardown);
    REQUIRE(setup() == 0);
    std::vector<int> results(4, -99);
    std::vector<std::thread> threads;
    for (size_t i = 0; i < results.size(); ++i)
        threads.emplace_back([&, i] { results[i] = worker(25); });
    for (auto &thread : threads) thread.join();
    teardown();
    for (int result : results) CHECK(result == 0);
}

TEST_CASE("HKDF guide example derives the RFC 5869 prefix as an AES-256-GCM key",
          "[crypto][hkdf][guide]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        deriveAesKey(ikm: const unsigned byte[], salt: const unsigned byte[],
                     info: const unsigned byte[]) : k::crypto::SymmetricKey!
                throws(k::crypto::CryptoException) {
            cipher: k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::AES256GCM();
            hkdf: k::crypto::Hkdf! = k::crypto::Hkdf::SHA256();
            if (cipher == null || hkdf == null) return null;

            prk: k::crypto::HkdfPrk! = hkdf.extract(ikm, salt);
            return prk.expandSymmetricKey(cipher, info);
        }
        seal(cipher: const k::crypto::SymmetricCipher&, key: const k::crypto::SymmetricKey&,
             plain: const unsigned byte[]) : unsigned byte[]! throws(k::crypto::CryptoException) {
            nonce: unsigned byte[]! = repeated((unsigned byte) 0, cipher.nonceSize());
            enc: k::crypto::EncryptSession! = cipher.encrypt(key, nonce);
            out: unsigned byte[]! = enc.update(plain, 0u, plain.size);
            enc.finish();
            finalBytes: unsigned byte[16];
            if (enc.readFinal(finalBytes, 0u, 16u) != 0u) return null;
            return out;
        }
        test() : int {
            try {
                cipher: k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::AES256GCM();
                if (!cipher) return 77;
                derived: k::crypto::SymmetricKey! = deriveAesKey(
                    repeated((unsigned byte) 0x0b, 22u), sequence(0u, 13u), sequence(0xf0u, 10u));
                if (!derived || derived.keySize() != 32u) return 1;
                direct: k::crypto::SymmetricKey! = cipher.importKey(fromHex(String(
                    "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf")));
                plain: unsigned byte[]! = fromHex(String("000102030405060708090a0b0c0d0e0f"));
                a: unsigned byte[]! = seal(cipher, derived, plain);
                b: unsigned byte[]! = seal(cipher, direct, plain);
                if (!a || !b || a.size != plain.size || !equal(a, b)) return 2;
                return 0;
            } catch (e: k::crypto::CryptoException&) { return -1; }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    const int result = test();
    if (result == 77) SKIP("AES-256-GCM is unavailable");
    CHECK(result == 0);
}

TEST_CASE("HKDF ownership bounds and key integration", "[crypto][hkdf]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        makePrk() : k::crypto::HkdfPrk! throws(k::crypto::CryptoException) {
            h: k::crypto::Hkdf! = k::crypto::Hkdf::SHA256();
            raw: unsigned byte[]! = sequence(0u, 32u);
            p: k::crypto::HkdfPrk! = h.importPrk(raw);
            raw[0u] = (unsigned byte) 0xff;
            delete raw;
            delete h;
            return p;
        }
        test() : int {
            try {
                h: k::crypto::Hkdf! = k::crypto::Hkdf::SHA256();
                p: k::crypto::HkdfPrk! = makePrk();
                info: unsigned byte[]! = fromHex(String("0001000200ff"));
                raw: unsigned byte[]! = sequence(0u, 32u);
                exported: unsigned byte[]! = p.exportBytes();
                if (p.keySize() != 32u || p.algorithm() != h.algorithm() || !equal(raw, exported))
                    return 1;
                exported[1u] = (unsigned byte) 0xff;
                if (!equal(raw, p.exportBytes())) return 2;
                max: unsigned byte[]! = p.expand(info, h.maxOutputSize());
                if (max.size != h.maxOutputSize()) return 3;
                maxCombined: unsigned byte[]! = h.derive(raw, info, h.maxOutputSize());
                extracted: k::crypto::HkdfPrk! = h.extract(raw);
                if (!equal(maxCombined, extracted.expand(info, h.maxOutputSize()))) return 16;
                failed: bool = false;
                try { p.expand(info, h.maxOutputSize() + 1u); }
                catch (e: k::crypto::CryptoInvalidInputException&) { failed = true; }
                if (!failed) return 4;
                failed = false;
                try { h.derive(raw, info, h.maxOutputSize() + 1u); }
                catch (e: k::crypto::CryptoInvalidInputException&) { failed = true; }
                if (!failed) return 5;
                shortPrk: unsigned byte[]! = new unsigned byte[31u];
                failed = false;
                try { h.importPrk(shortPrk); }
                catch (e: k::crypto::CryptoInvalidInputException&) { failed = true; }
                if (!failed) return 6;
                longer: unsigned byte[]! = sequence(0u, 33u);
                longerPrk: k::crypto::HkdfPrk! = h.importPrk(longer);
                if (!equal(longer, longerPrk.exportBytes())) return 7;
                cipher: k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithms::AES, 128u, k::crypto::SymmetricModes::GCM);
                xts: k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithms::AES, 128u, k::crypto::SymmetricModes::XTS);
                if (!cipher || !xts) return 8;
                badXts: unsigned byte[]! = repeated((unsigned byte) 0, 32u);
                failed = false;
                try { xts.importKey(badXts); }
                catch (e: k::crypto::CryptoInvalidInputException&) { failed = true; }
                if (!failed) return 9;
                sym: k::crypto::SymmetricKey! = p.expandSymmetricKey(cipher, info);
                xtsKey: k::crypto::SymmetricKey! = p.expandSymmetricKey(xts, info);
                mac: k::crypto::HmacKey! = p.expandHmacKey(info, 32u);
                if (sym.keySize() != 16u || xtsKey.keySize() != 32u || mac.keySize() != 32u)
                    return 10;
                macBytes: unsigned byte[]! = p.expand(info, 32u);
                directMac: k::crypto::HmacKey! = k::crypto::HmacKey::fromBytes(macBytes);
                mac1: k::crypto::Hmac! = k::crypto::Hmac::SHA256(mac);
                mac2: k::crypto::Hmac! = k::crypto::Hmac::SHA256(directMac);
                mac1.update(info);
                mac2.update(info);
                if (!equal(mac1.finalize(), mac2.finalize())) return 14;
                symBytes: unsigned byte[]! = p.expand(info, 16u);
                directSym: k::crypto::SymmetricKey! = cipher.importKey(symBytes);
                if (!equal(h.extract(sym).exportBytes(), h.extract(directSym).exportBytes())) return 15;
                input: k::crypto::HmacKey! = k::crypto::HmacKey::fromBytes(raw);
                inputSym: k::crypto::SymmetricKey! = xts.importKey(raw);
                salt: unsigned byte[]! = fromHex(String("000100ff"));
                a: unsigned byte[]! = h.derive(raw, salt, info, 42u);
                b: unsigned byte[]! = h.derive(input, salt, info, 42u);
                c: unsigned byte[]! = h.derive(inputSym, salt, info, 42u);
                if (!equal(a, b) || !equal(a, c)) return 11;
                if (!equal(h.extract(raw, salt).exportBytes(), h.extract(input, salt).exportBytes()) ||
                    !equal(h.extract(raw).exportBytes(), h.extract(inputSym).exportBytes()) ||
                    !equal(h.derive(raw, info, 42u), h.derive(input, info, 42u)) ||
                    !equal(h.derive(raw, info, 42u), h.derive(inputSym, info, 42u))) return 12;
                d: k::crypto::SymmetricKey! = h.deriveSymmetricKey(input, salt, cipher, info);
                e: k::crypto::SymmetricKey! = h.deriveSymmetricKey(inputSym, cipher, info);
                f: k::crypto::HmacKey! = h.deriveHmacKey(inputSym, salt, info, 16u);
                g: k::crypto::HmacKey! = h.deriveHmacKey(raw, info, 16u);
                if (d.keySize() != 16u || e.keySize() != 16u ||
                    f.keySize() != 16u || g.keySize() != 16u) return 13;
                return 0;
            } catch (e: k::crypto::CryptoException&) { return -1; }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("HKDF extracted PRK survives its inputs and derives interoperable AES keys",
          "[crypto][hkdf][ownership]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        extractAndDiscardInputs() : k::crypto::HkdfPrk! throws(k::crypto::CryptoException) {
            h: k::crypto::Hkdf! = k::crypto::Hkdf::SHA256();
            ikm: unsigned byte[]! = repeated((unsigned byte) 0x0b, 22u);
            salt: unsigned byte[]! = sequence(0u, 13u);
            p: k::crypto::HkdfPrk! = h.extract(ikm, salt);
            ikm[0u] = (unsigned byte) 0;
            salt[0u] = (unsigned byte) 0xff;
            delete ikm;
            delete salt;
            delete h;
            return p;
        }
        test() : int {
            try {
                p: k::crypto::HkdfPrk! = extractAndDiscardInputs();
                expected: unsigned byte[]! = fromHex(String(
                    "077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5"));
                if (!equal(p.exportBytes(), expected)) return 1;
                info: unsigned byte[]! = fromHex(String("0001000200ff"));
                changed: unsigned byte[]! = fromHex(String("000100020000"));
                first: unsigned byte[]! = p.expand(info, 32u);
                if (!equal(first, p.expand(info, 32u)) ||
                    equal(first, p.expand(changed, 32u))) return 2;
                shortOutput: unsigned byte[]! = p.expand(info, 16u);
                i: unsigned int = 0u;
                while (i < shortOutput.size) {
                    if (shortOutput[i] != first[i]) return 3;
                    ++i;
                }

                cipher: k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::AES256GCM();
                h: k::crypto::Hkdf! = k::crypto::Hkdf::SHA256();
                if (!cipher || !h) return 4;
                key1: k::crypto::SymmetricKey! = p.expandSymmetricKey(cipher, info);
                ikm: unsigned byte[]! = repeated((unsigned byte) 0x0b, 22u);
                salt: unsigned byte[]! = sequence(0u, 13u);
                key2: k::crypto::SymmetricKey! = h.deriveSymmetricKey(ikm, salt, cipher, info);
                delete p;
                delete h;
                nonce: unsigned byte[]! = repeated((unsigned byte) 0, cipher.nonceSize());
                plain: unsigned byte[]! = fromHex(String("000102030405060708090a0b0c0d0e0f"));
                enc: k::crypto::EncryptSession! = cipher.encrypt(key1, nonce);
                ciphertext: unsigned byte[]! = enc.update(plain, 0u, plain.size);
                enc.finish();
                finalBytes: unsigned byte[16];
                if (enc.readFinal(finalBytes, 0u, 16u) != 0u) return 5;
                tag: unsigned byte[]! = enc.tag();
                dec: k::crypto::DecryptSession! = cipher.decrypt(
                    key2, nonce, k::crypto::DecryptionStaging::Memory);
                dec.update(ciphertext, 0u, ciphertext.size);
                dec.finalize(tag);
                recovered: unsigned byte[]! = new unsigned byte[plain.size];
                if (dec.read(recovered, 0u, recovered.size) != plain.size) return 6;
                if (!equal(plain, recovered)) return 7;
                return 0;
            } catch (e: k::crypto::CryptoException&) { return -1; }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("HKDF keys cannot be copied", "[crypto][hkdf][ownership]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test(p: const k::crypto::HkdfPrk&) : int {
            copy: k::crypto::HkdfPrk = p;
            return (int) copy.keySize();
        }
    )K");
    CHECK_FALSE(jit);
}

TEST_CASE("HKDF PRK owner cannot be copied through nested union wrappers",
          "[crypto][hkdf][ownership][union][regression]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        union Storage { empty: byte; prk: k::crypto::HkdfPrk!; }
        struct Inner { data: Storage; }
        struct Outer { inner: Inner; marker: int; }
        test() : int {
            try {
                h: k::crypto::Hkdf! = k::crypto::Hkdf::SHA256();
                bytes: unsigned byte[]! = sequence(0u, 32u);
                source: Outer;
                source.inner.data.prk = h.importPrk(bytes);
                target: Outer;
                caught: bool = false;
                try { target = source; }
                catch (error: FatalError&) { caught = error.getCode() == 0x55434f50; }
                if (!caught || source.inner.data.prk.keySize() != 32u) return -1;
                return 42;
            } catch (error: k::crypto::CryptoException&) { return -2; }
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        std::signal(SIGABRT, SIG_DFL);
        std::signal(SIGSEGV, SIG_DFL);
        _exit(fn() == 42 ? 0 : 1);
    }
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
}

TEST_CASE("HKDF PRK drain cannot be passed by value", "[crypto][hkdf][ownership]") {
    auto compiler = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(LIBK_KDI_DIR);
    resolver->add_search_dir(LIBK_CRYPTO_KDI_DIR);
    compiler->set_file_resolver(resolver);
    REQUIRE_THROWS_MATCHES(compiler->parse_source("hkdf-drain-copy.k", R"K(
        module hkdf_drain_copy_test;
        import k::crypto;
        consume(value: k::crypto::HkdfPrk) {}
        test(p: k::crypto::HkdfPrk&) { consume(#p); }
    )K"), k::log::compiler_error,
        Catch::Matchers::Predicate<k::log::compiler_error>(
            [](const auto& error) {
                return error.get_diagnostic().message.find("deleted") != std::string::npos;
            }, "deleted HKDF PRK copy constructor"));
}

TEST_CASE("HKDF provider limits and zero output are not faked", "[crypto][hkdf]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test(infoLength: unsigned int, outputLength: unsigned int, expand: int) : int {
            try {
                h: k::crypto::Hkdf! = k::crypto::Hkdf::SHA256();
                ikm: unsigned byte[]! = repeated((unsigned byte) 0, 22u);
                info: unsigned byte[]! = repeated((unsigned byte) 7, infoLength);
                if (expand != 0) {
                    rawPrk: unsigned byte[]! = repeated((unsigned byte) 0, 32u);
                    p: k::crypto::HkdfPrk! = h.importPrk(rawPrk);
                    expanded: unsigned byte[]! = p.expand(info, outputLength);
                    if (expanded.size != outputLength) return -1;
                    return 1;
                }
                result: unsigned byte[]! = h.derive(ikm, info, outputLength);
                if (result.size != outputLength) return -1;
                return 1;
            } catch (e: k::crypto::CryptoOperationException&) { return 0; }
              catch (e: k::crypto::CryptoException&) { return -2; }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)(unsigned, unsigned, int)>("test");
    REQUIRE(test);
    CHECK(test(1024, 32, 0) == (providerDerives(1024, 32) ? 1 : 0));
    CHECK(test(1025, 32, 0) == (providerDerives(1025, 32) ? 1 : 0));
    CHECK(test(0, 0, 0) == (providerDerives(0, 0) ? 1 : 0));
    CHECK(test(1024, 32, 1) == (providerExpands(1024, 32) ? 1 : 0));
    CHECK(test(1025, 32, 1) == (providerExpands(1025, 32) ? 1 : 0));
    CHECK(test(0, 0, 1) == (providerExpands(0, 0) ? 1 : 0));
}

TEST_CASE("HKDF unavailable names and caller error marks survive", "[crypto][hkdf]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                bad: k::crypto::Hkdf! = k::crypto::Hkdf::create(String("KLANG-NO-SUCH-DIGEST"));
                xof: k::crypto::Hkdf! = k::crypto::Hkdf::create(String("SHAKE128"));
                if (bad || xof) return 1;
                h: k::crypto::Hkdf! = k::crypto::Hkdf::SHA256();
                if (!h) return 2;
                ikm: unsigned byte[]! = repeated((unsigned byte) 0, 22u);
                info: unsigned byte[]! = repeated((unsigned byte) 1, 1025u);
                try { h.derive(ikm, info, 32u); }
                catch (e: k::crypto::CryptoOperationException&) {}
                return 0;
            } catch (e: k::crypto::CryptoException&) { return -1; }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    auto make = reinterpret_cast<NativeHkdfNew>(dlsym(RTLD_DEFAULT, "__k_hkdf_new"));
    auto free = reinterpret_cast<NativeHkdfFree>(dlsym(RTLD_DEFAULT, "__k_hkdf_free"));
    auto importPrk = reinterpret_cast<NativeHkdfImport>(dlsym(RTLD_DEFAULT, "__k_hkdf_import"));
    auto freePrk = reinterpret_cast<NativeHkdfFree>(dlsym(RTLD_DEFAULT, "__k_hkdf_prk_free"));
    REQUIRE(make);
    REQUIRE(free);
    REQUIRE(importPrk);
    REQUIRE(freePrk);
    ERR_clear_error();
    ERR_raise_data(ERR_LIB_USER, 100, "marked");
    auto first = ERR_peek_error();
    REQUIRE(ERR_set_mark() == 1);
    ERR_raise_data(ERR_LIB_USER, 101, "after mark");
    auto last = ERR_peek_last_error();
    CHECK(test() == 0);
    int status = 0;
    std::unique_ptr<void, NativeHkdfFree> unavailable(
        make(reinterpret_cast<const unsigned char *>("KLANG-NO-SUCH-DIGEST"), &status), free);
    CHECK_FALSE(unavailable);
    CHECK(status == 1);
    std::unique_ptr<void, NativeHkdfFree> h(
        make(reinterpret_cast<const unsigned char *>("SHA-256"), &status), free);
    REQUIRE(h);
    REQUIRE(status == 0);
    unsigned char shortKey[31] = {};
    std::unique_ptr<void, NativeHkdfFree> invalid(
        importPrk(h.get(), shortKey, sizeof(shortKey), &status), freePrk);
    CHECK_FALSE(invalid);
    CHECK(status == -1);
    CHECK(ERR_peek_error() == first);
    CHECK(ERR_peek_last_error() == last);
    CHECK(ERR_pop_to_mark() == 1);
    CHECK(ERR_peek_last_error() == first);
    ERR_clear_error();
}

TEST_CASE("HKDF factory handles missing KDF and extension digest provider failures",
          "[crypto][hkdf][provider]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test(kind: int) : int {
            try {
                h: k::crypto::Hkdf! =
                    k::crypto::Hkdf::create(String("KLANG-FAIL-DIGEST"));
                if (kind == 0) return h ? 1 : 0;
                if (!h || h.hashSize() != 32u) return 2;
                ikm: unsigned byte[]! = repeated((unsigned byte) 1, 32u);
                info: unsigned byte[]! = new unsigned byte[0u];
                try { h.derive(ikm, info, 32u); }
                catch (e: k::crypto::CryptoOperationException&) { return 0; }
                return 3;
            } catch (e: k::crypto::CryptoOperationException&) {
                return kind == 1 ? 0 : 4;
            } catch (e: k::crypto::CryptoException&) { return 5; }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)(int)>("test");
    REQUIRE(test);
    std::unique_ptr<OSSL_LIB_CTX, decltype(&OSSL_LIB_CTX_free)> context(
        OSSL_LIB_CTX_new(), OSSL_LIB_CTX_free);
    REQUIRE(context);
    REQUIRE(OSSL_PROVIDER_add_builtin(context.get(), "klang-failing", failingProviderInit) == 1);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> failing(
        OSSL_PROVIDER_load(context.get(), "klang-failing"), OSSL_PROVIDER_unload);
    REQUIRE(failing);
    ScopedDefaultContext selected(context.get());
    REQUIRE(selected.previous);
    ERR_clear_error();
    CHECK(test(0) == 0);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> standard(
        OSSL_PROVIDER_load(context.get(), "default"), OSSL_PROVIDER_unload);
    REQUIRE(standard);
    ERR_clear_error();
    ERR_raise_data(ERR_LIB_USER, 100, "caller");
    auto previous = ERR_peek_error();
    REQUIRE(ERR_set_mark() == 1);
    CHECK(test(1) == 0);
    CHECK(ERR_peek_error() == previous);
    CHECK(ERR_pop_to_mark() == 1);
    ERR_clear_error();
}

TEST_CASE("HKDF factory classifies provider rejections and failures",
          "[crypto][hkdf][provider]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                h: k::crypto::Hkdf! = k::crypto::Hkdf::SHA256();
                return h ? 1 : 0;
            } catch (e: k::crypto::CryptoOperationException&) {
                return 2;
            } catch (e: k::crypto::CryptoException&) {
                return 3;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    std::unique_ptr<OSSL_LIB_CTX, decltype(&OSSL_LIB_CTX_free)> context(
        OSSL_LIB_CTX_new(), OSSL_LIB_CTX_free);
    REQUIRE(context);
    REQUIRE(OSSL_PROVIDER_add_builtin(context.get(), "klang-hkdf-test", mockProviderInit) == 1);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> standard(
        OSSL_PROVIDER_load(context.get(), "default"), OSSL_PROVIDER_unload);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> mock(
        OSSL_PROVIDER_load(context.get(), "klang-hkdf-test"), OSSL_PROVIDER_unload);
    REQUIRE(standard);
    REQUIRE(mock);
    REQUIRE(EVP_set_default_properties(context.get(), "?provider=klang-hkdf-test") == 1);
    std::unique_ptr<EVP_KDF, decltype(&EVP_KDF_free)> selectedKdf(
        EVP_KDF_fetch(context.get(), "HKDF", nullptr), EVP_KDF_free);
    REQUIRE(selectedKdf);
    REQUIRE(EVP_KDF_get0_provider(selectedKdf.get()) == mock.get());
    ScopedDefaultContext selected(context.get());
    REQUIRE(selected.previous);
    struct ResetBehavior { ~ResetBehavior() { mockSetBehavior = 0; ERR_clear_error(); } } reset;
    for (int behavior = 1; behavior <= 9; ++behavior) {
        INFO("mock set behavior " << behavior);
        mockSetBehavior = behavior;
        const int expected = behavior <= 4 ? 0 : 2;
        ERR_clear_error();
        CHECK(test() == expected);
        CHECK(ERR_peek_error() == 0);
        ERR_raise_data(ERR_LIB_USER, 90, "marked");
        const auto first = ERR_peek_error();
        REQUIRE(ERR_set_mark() == 1);
        ERR_raise_data(ERR_LIB_USER, 91, "later");
        const auto last = ERR_peek_last_error();
        CHECK(test() == expected);
        CHECK(ERR_peek_error() == first);
        CHECK(ERR_peek_last_error() == last);
        CHECK(ERR_pop_to_mark() == 1);
        CHECK(ERR_peek_last_error() == first);
        ERR_clear_error();
    }
}

TEST_CASE("HKDF propagates provider failures clears output and rejects degenerate XTS",
          "[crypto][hkdf][provider]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test(kind: int) : int {
            try {
                h: k::crypto::Hkdf! = k::crypto::Hkdf::SHA256();
                if (!h) return 1;
                ikm: unsigned byte[]! = repeated((unsigned byte) 1, 32u);
                info: unsigned byte[]! = repeated((unsigned byte) 7, 1500u);
                if (kind == 0) {
                    try { h.derive(ikm, info, 32u); }
                    catch (e: k::crypto::CryptoOperationException&) { return 0; }
                    return 2;
                }
                if (kind == 2) {
                    empty: unsigned byte[]! = new unsigned byte[0u];
                    derived: unsigned byte[]! = h.derive(empty, info, 32u);
                    return derived.size == 32u ? 0 : 5;
                }
                if (kind == 4) {
                    empty: unsigned byte[]! = new unsigned byte[0u];
                    derived: unsigned byte[]! = h.derive(empty, empty, info, 32u);
                    return derived.size == 32u ? 0 : 5;
                }
                xts: k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
                    k::crypto::SymmetricAlgorithms::AES, 128u, k::crypto::SymmetricModes::XTS);
                if (!xts) return 3;
                try {
                    if (kind == 3) {
                        p: k::crypto::HkdfPrk! = h.extract(ikm);
                        p.expandSymmetricKey(xts, info);
                    } else {
                        h.deriveSymmetricKey(ikm, xts, info);
                    }
                }
                catch (e: k::crypto::CryptoInvalidInputException&) { return 0; }
                return 4;
            } catch (e: k::crypto::CryptoException&) { return -1; }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)(int)>("test");
    REQUIRE(test);
    std::unique_ptr<OSSL_LIB_CTX, decltype(&OSSL_LIB_CTX_free)> context(
        OSSL_LIB_CTX_new(), OSSL_LIB_CTX_free);
    REQUIRE(context);
    REQUIRE(OSSL_PROVIDER_add_builtin(context.get(), "klang-hkdf-test", mockProviderInit) == 1);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> standard(
        OSSL_PROVIDER_load(context.get(), "default"), OSSL_PROVIDER_unload);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> mock(
        OSSL_PROVIDER_load(context.get(), "klang-hkdf-test"), OSSL_PROVIDER_unload);
    REQUIRE(standard);
    REQUIRE(mock);
    REQUIRE(EVP_set_default_properties(context.get(), "?provider=klang-hkdf-test") == 1);
    ScopedDefaultContext selected(context.get());
    REQUIRE(selected.previous);
    ERR_clear_error();
    mockFailure = 1;
    mockDerives = 0;
    ERR_raise_data(ERR_LIB_USER, 90, "caller");
    auto caller = ERR_peek_error();
    REQUIRE(ERR_set_mark() == 1);
    CHECK(test(0) == 0);
    CHECK(mockDerives == 1);
    CHECK(mockInfoLength == 1500);
    CHECK(ERR_peek_error() == caller);
    CHECK(ERR_pop_to_mark() == 1);
    ERR_clear_error();

    using New = void *(*)(const unsigned char *, int *);
    using Free = void (*)(void *);
    using Import = void *(*)(const void *, const unsigned char *, unsigned, int *);
    using Expand = int (*)(const void *, const unsigned char *, unsigned, unsigned char *, unsigned);
    using Derive = int (*)(const void *, int, const unsigned char *, unsigned,
                           const unsigned char *, unsigned, int, const unsigned char *,
                           unsigned, unsigned char *, unsigned);
    auto make = reinterpret_cast<New>(dlsym(RTLD_DEFAULT, "__k_hkdf_new"));
    auto free = reinterpret_cast<Free>(dlsym(RTLD_DEFAULT, "__k_hkdf_free"));
    auto derive = reinterpret_cast<Derive>(dlsym(RTLD_DEFAULT, "__k_hkdf_derive"));
    auto importPrk = reinterpret_cast<Import>(dlsym(RTLD_DEFAULT, "__k_hkdf_import"));
    auto extract = reinterpret_cast<NativeHkdfExtract>(dlsym(RTLD_DEFAULT, "__k_hkdf_extract"));
    auto expand = reinterpret_cast<Expand>(dlsym(RTLD_DEFAULT, "__k_hkdf_expand"));
    auto freePrk = reinterpret_cast<Free>(dlsym(RTLD_DEFAULT, "__k_hkdf_prk_free"));
    REQUIRE(make);
    REQUIRE(free);
    REQUIRE(derive);
    REQUIRE(importPrk);
    REQUIRE(extract);
    REQUIRE(expand);
    REQUIRE(freePrk);
    int status = 0;
    std::unique_ptr<void, Free> h(
        make(reinterpret_cast<const unsigned char *>("SHA-256"), &status), free);
    REQUIRE(h);
    REQUIRE(status == 0);
    unsigned char key[32] = {};
    std::unique_ptr<void, Free> prk(importPrk(h.get(), key, sizeof(key), &status), freePrk);
    REQUIRE(prk);
    REQUIRE(status == 0);
    unsigned char out[32];
    std::memset(out, 0x5a, sizeof(out));
    CHECK(derive(h.get(), 0, key, sizeof(key), nullptr, 0, 1, nullptr, 0, out, sizeof(out)) == -3);
    for (auto byte : out) CHECK(byte == 0);
    status = 0;
    std::unique_ptr<void, Free> failedExtract(
        extract(h.get(), key, sizeof(key), nullptr, 0, 1, &status), freePrk);
    CHECK_FALSE(failedExtract);
    CHECK(status == -3);
    mockFailure = 2;
    ERR_raise_data(ERR_LIB_USER, 93, "marked");
    const auto successErrorCaller = ERR_peek_error();
    REQUIRE(ERR_set_mark() == 1);
    std::memset(out, 0x5a, sizeof(out));
    CHECK(derive(h.get(), 0, key, sizeof(key), nullptr, 0, 1, nullptr, 0, out, sizeof(out)) == -3);
    for (auto byte : out) CHECK(byte == 0);
    std::memset(out, 0x5a, sizeof(out));
    CHECK(expand(prk.get(), nullptr, 0, out, sizeof(out)) == -3);
    for (auto byte : out) CHECK(byte == 0);
    status = 0;
    failedExtract.reset(extract(h.get(), key, sizeof(key), nullptr, 0, 1, &status));
    CHECK_FALSE(failedExtract);
    CHECK(status == -3);
    CHECK(ERR_peek_error() == successErrorCaller);
    CHECK(ERR_pop_to_mark() == 1);
    ERR_clear_error();
    mockFailure = 0;
    status = -1;
    failedExtract.reset(extract(h.get(), key, sizeof(key), nullptr, 0, 1, &status));
    CHECK(failedExtract);
    CHECK(status == 0);
    failedExtract.reset();
    unsigned char longInfo[1500] = {};
    longInfo[1] = 0xff;
    mockInfoLength = 0;
    CHECK(expand(prk.get(), longInfo, sizeof(longInfo), out, sizeof(out)) == 0);
    CHECK(mockInfoLength == sizeof(longInfo));
    mockDerives = 0;
    REQUIRE(EVP_set_default_properties(context.get(), "provider=klang-hkdf-test") == 1);
    std::memset(out, 0x5a, sizeof(out));
    CHECK(derive(h.get(), 0, key, sizeof(key), nullptr, 0, 1, nullptr, 0, out, sizeof(out)) == -3);
    CHECK(mockDerives == 0);
    for (auto byte : out) CHECK(byte == 0);
    REQUIRE(EVP_set_default_properties(context.get(), "?provider=klang-hkdf-test") == 1);
    std::unique_ptr<EVP_MD, decltype(&EVP_MD_free)> before(
        EVP_MD_fetch(context.get(), "SHA-256", nullptr), EVP_MD_free);
    REQUIRE(before);
    std::unique_ptr<EVP_KDF, decltype(&EVP_KDF_free)> beforeKdf(
        EVP_KDF_fetch(context.get(), "HKDF", nullptr), EVP_KDF_free);
    REQUIRE(beforeKdf);
    REQUIRE(EVP_KDF_get0_provider(beforeKdf.get()) == mock.get());
    REQUIRE(EVP_set_default_properties(context.get(), "provider=default") == 1);
    std::unique_ptr<EVP_MD, decltype(&EVP_MD_free)> after(
        EVP_MD_fetch(context.get(), "SHA-256", nullptr), EVP_MD_free);
    REQUIRE(after);
    REQUIRE(EVP_MD_get0_provider(before.get()) == EVP_MD_get0_provider(after.get()));
    std::unique_ptr<EVP_KDF, decltype(&EVP_KDF_free)> afterKdf(
        EVP_KDF_fetch(context.get(), "HKDF", nullptr), EVP_KDF_free);
    REQUIRE(afterKdf);
    REQUIRE(EVP_KDF_get0_provider(afterKdf.get()) == standard.get());
    ERR_clear_error();
    ERR_raise_data(ERR_LIB_USER, 91, "marked");
    const auto marked = ERR_peek_error();
    REQUIRE(ERR_set_mark() == 1);
    ERR_raise_data(ERR_LIB_USER, 92, "later");
    const auto later = ERR_peek_last_error();
    std::memset(out, 0x5a, sizeof(out));
    CHECK(derive(h.get(), 0, key, sizeof(key), nullptr, 0, 1, nullptr, 0, out, sizeof(out)) == -3);
    for (auto byte : out) CHECK(byte == 0);
    std::memset(out, 0x5a, sizeof(out));
    CHECK(expand(prk.get(), nullptr, 0, out, sizeof(out)) == -3);
    for (auto byte : out) CHECK(byte == 0);
    CHECK(mockDerives == 0);
    CHECK(ERR_peek_error() == marked);
    CHECK(ERR_peek_last_error() == later);
    CHECK(ERR_pop_to_mark() == 1);
    CHECK(ERR_peek_last_error() == marked);
    ERR_clear_error();
    REQUIRE(EVP_set_default_properties(context.get(), "?provider=klang-hkdf-test") == 1);
    CHECK(derive(h.get(), 0, key, sizeof(key), nullptr, 0, 1, nullptr, 0, out, sizeof(out)) == 0);
    CHECK(mockDerives == 1);
    prk.reset();
    h.reset();
    mockDerives = 0;
    CHECK(test(1) == 0);
    CHECK(mockDerives == 1);
    CHECK(mockInfoLength == 1500);
    mockDerives = 0;
    CHECK(test(2) == 0);
    CHECK(mockDerives == 1);
    CHECK(mockKeyLength == 0);
    CHECK(mockSaltLength == 32);
    CHECK(mockSaltZero);
    mockDerives = 0;
    CHECK(test(3) == 0);
    CHECK(mockDerives == 2);
    mockDerives = 0;
    CHECK(test(4) == 0);
    CHECK(mockDerives == 1);
    CHECK(mockKeyLength == 0);
    CHECK(mockSaltLength == 0);
    CHECK(mockSaltZero);
    ERR_clear_error();
}

TEST_CASE("HKDF provider execution uses the descriptor context and restores the caller context",
          "[crypto][hkdf][provider]") {
    auto jit = gen_jit_with_crypto(std::string(prelude));
    REQUIRE(jit);
    auto make = reinterpret_cast<NativeHkdfNew>(dlsym(RTLD_DEFAULT, "__k_hkdf_new"));
    auto free = reinterpret_cast<NativeHkdfFree>(dlsym(RTLD_DEFAULT, "__k_hkdf_free"));
    auto derive = reinterpret_cast<NativeHkdfDerive>(dlsym(RTLD_DEFAULT, "__k_hkdf_derive"));
    REQUIRE(make);
    REQUIRE(free);
    REQUIRE(derive);
    std::unique_ptr<OSSL_LIB_CTX, decltype(&OSSL_LIB_CTX_free)> bound(
        OSSL_LIB_CTX_new(), OSSL_LIB_CTX_free);
    std::unique_ptr<OSSL_LIB_CTX, decltype(&OSSL_LIB_CTX_free)> caller(
        OSSL_LIB_CTX_new(), OSSL_LIB_CTX_free);
    REQUIRE(bound);
    REQUIRE(caller);
    REQUIRE(OSSL_PROVIDER_add_builtin(bound.get(), "klang-hkdf-test", mockProviderInit) == 1);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> standard(
        OSSL_PROVIDER_load(bound.get(), "default"), OSSL_PROVIDER_unload);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> mock(
        OSSL_PROVIDER_load(bound.get(), "klang-hkdf-test"), OSSL_PROVIDER_unload);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> nullProvider(
        OSSL_PROVIDER_load(caller.get(), "null"), OSSL_PROVIDER_unload);
    REQUIRE(standard);
    REQUIRE(mock);
    REQUIRE(nullProvider);
    REQUIRE(EVP_set_default_properties(bound.get(), "?provider=klang-hkdf-test") == 1);
    ScopedDefaultContext selected(bound.get());
    REQUIRE(selected.previous);
    int status = 0;
    std::unique_ptr<void, NativeHkdfFree> h(
        make(reinterpret_cast<const unsigned char *>("SHA-256"), &status), free);
    REQUIRE(h);
    REQUIRE(status == 0);
    struct Reset {
        ~Reset() { mockFetchDigest = false; mockObservedContext = nullptr; ERR_clear_error(); }
    } reset;
    mockFetchDigest = true;
    ScopedDefaultContext different(caller.get());
    REQUIRE(different.previous);
    unsigned char key[32] = {}, out[32] = {};
    for (bool marked : {false, true}) {
        ERR_clear_error();
        if (marked) {
            ERR_raise(ERR_LIB_USER, 90);
            REQUIRE(ERR_set_mark() == 1);
            ERR_raise(ERR_LIB_USER, 91);
        }
        auto first = ERR_peek_error();
        auto last = ERR_peek_last_error();
        mockObservedContext = nullptr;
        CHECK(derive(h.get(), 0, key, sizeof(key), nullptr, 0, 1, nullptr, 0,
                     out, sizeof(out)) == 0);
        CHECK(mockObservedContext == bound.get());
        CHECK(OSSL_LIB_CTX_set0_default(nullptr) == caller.get());
        CHECK(ERR_peek_error() == first);
        CHECK(ERR_peek_last_error() == last);
        if (marked) CHECK(ERR_pop_to_mark() == 1);
    }
}

TEST_CASE("HKDF uses the same-provider implementation selected by current properties",
          "[crypto][hkdf][provider]") {
    auto jit = gen_jit_with_crypto(std::string(prelude));
    REQUIRE(jit);
    auto make = reinterpret_cast<NativeHkdfNew>(dlsym(RTLD_DEFAULT, "__k_hkdf_new"));
    auto free = reinterpret_cast<NativeHkdfFree>(dlsym(RTLD_DEFAULT, "__k_hkdf_free"));
    auto derive = reinterpret_cast<NativeHkdfDerive>(dlsym(RTLD_DEFAULT, "__k_hkdf_derive"));
    REQUIRE(make);
    REQUIRE(free);
    REQUIRE(derive);
    std::unique_ptr<OSSL_LIB_CTX, decltype(&OSSL_LIB_CTX_free)> context(
        OSSL_LIB_CTX_new(), OSSL_LIB_CTX_free);
    REQUIRE(context);
    REQUIRE(OSSL_PROVIDER_add_builtin(context.get(), "klang-variant", variantProviderInit) == 1);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> standard(
        OSSL_PROVIDER_load(context.get(), "default"), OSSL_PROVIDER_unload);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> variant(
        OSSL_PROVIDER_load(context.get(), "klang-variant"), OSSL_PROVIDER_unload);
    REQUIRE(standard);
    REQUIRE(variant);
    constexpr auto initial = "?provider=klang-variant,?klang.kdf=one,?klang.digest=one";
    REQUIRE(EVP_set_default_properties(context.get(), initial) == 1);
    ScopedDefaultContext selected(context.get());
    REQUIRE(selected.previous);
    int status = 0;
    std::unique_ptr<void, NativeHkdfFree> h(
        make(reinterpret_cast<const unsigned char *>("KLANG-VARIANT-DIGEST"), &status), free);
    REQUIRE(h);
    REQUIRE(status == 0);
    std::unique_ptr<EVP_KDF, decltype(&EVP_KDF_free)> firstKdf(
        EVP_KDF_fetch(context.get(), "HKDF", nullptr), EVP_KDF_free);
    std::unique_ptr<EVP_MD, decltype(&EVP_MD_free)> firstDigest(
        EVP_MD_fetch(context.get(), "KLANG-VARIANT-DIGEST", nullptr), EVP_MD_free);
    REQUIRE(firstKdf);
    REQUIRE(firstDigest);
    REQUIRE(EVP_KDF_get0_provider(firstKdf.get()) == variant.get());
    REQUIRE(EVP_MD_get0_provider(firstDigest.get()) == variant.get());
    unsigned char key[32] = {}, out[32] = {};
    mockDerives = variantDerives = 0;
    CHECK(derive(h.get(), 0, key, sizeof(key), nullptr, 0, 1, nullptr, 0,
                 out, sizeof(out)) == 0);
    CHECK(mockDerives == 1);

    REQUIRE(EVP_set_default_properties(
        context.get(), "?provider=klang-variant,?klang.kdf=two,?klang.digest=one") == 1);
    std::unique_ptr<EVP_KDF, decltype(&EVP_KDF_free)> otherKdf(
        EVP_KDF_fetch(context.get(), "HKDF", nullptr), EVP_KDF_free);
    REQUIRE(otherKdf);
    REQUIRE(otherKdf.get() != firstKdf.get());
    REQUIRE(EVP_KDF_get0_provider(otherKdf.get()) == EVP_KDF_get0_provider(firstKdf.get()));
    std::memset(out, 0x5a, sizeof(out));
    CHECK(derive(h.get(), 0, key, sizeof(key), nullptr, 0, 1, nullptr, 0,
                 out, sizeof(out)) == 0);
    for (auto byte : out) CHECK(byte == 0);
    CHECK(variantDerives == 1);

    REQUIRE(EVP_set_default_properties(
        context.get(), "?provider=klang-variant,?klang.kdf=one,?klang.digest=two") == 1);
    std::unique_ptr<EVP_MD, decltype(&EVP_MD_free)> otherDigest(
        EVP_MD_fetch(context.get(), "KLANG-VARIANT-DIGEST", nullptr), EVP_MD_free);
    REQUIRE(otherDigest);
    REQUIRE(otherDigest.get() != firstDigest.get());
    REQUIRE(EVP_MD_get0_provider(otherDigest.get()) == EVP_MD_get0_provider(firstDigest.get()));
    std::memset(out, 0x5a, sizeof(out));
    CHECK(derive(h.get(), 0, key, sizeof(key), nullptr, 0, 1, nullptr, 0,
                 out, sizeof(out)) == 0);
    for (auto byte : out) CHECK(byte == 0);
    CHECK(mockDerives == 3);
    CHECK(variantDerives == 1);
    REQUIRE(EVP_set_default_properties(
        context.get(), "?provider=klang-variant,?klang.kdf=one,?klang.digest=long") == 1);
    std::unique_ptr<EVP_MD, decltype(&EVP_MD_free)> longDigest(
        EVP_MD_fetch(context.get(), "KLANG-VARIANT-DIGEST", nullptr), EVP_MD_free);
    REQUIRE(longDigest);
    REQUIRE(EVP_MD_get0_provider(longDigest.get()) == EVP_MD_get0_provider(firstDigest.get()));
    REQUIRE(EVP_MD_get_size(longDigest.get()) == 64);
    std::memset(out, 0x5a, sizeof(out));
    CHECK(derive(h.get(), 0, key, sizeof(key), nullptr, 0, 1, nullptr, 0,
                 out, sizeof(out)) == -3);
    for (auto byte : out) CHECK(byte == 0);
    CHECK(mockDerives == 3);
    ERR_clear_error();
}

TEST_CASE("HKDF caller cleans non-global context before outliving it",
          "[crypto][hkdf][provider]") {
    auto jit = gen_jit_with_crypto(std::string(prelude));
    REQUIRE(jit);
    auto make = reinterpret_cast<NativeHkdfNew>(dlsym(RTLD_DEFAULT, "__k_hkdf_new"));
    auto free = reinterpret_cast<NativeHkdfFree>(dlsym(RTLD_DEFAULT, "__k_hkdf_free"));
    auto derive = reinterpret_cast<NativeHkdfDerive>(dlsym(RTLD_DEFAULT, "__k_hkdf_derive"));
    REQUIRE(make);
    REQUIRE(free);
    REQUIRE(derive);
    struct Reset {
        ~Reset() { mockRegisterThreadStop = false; ERR_clear_error(); }
    } reset;
    std::unique_ptr<OSSL_LIB_CTX, decltype(&OSSL_LIB_CTX_free)> context(
        OSSL_LIB_CTX_new(), OSSL_LIB_CTX_free);
    REQUIRE(context);
    REQUIRE(OSSL_PROVIDER_add_builtin(context.get(), "klang-hkdf-test", mockProviderInit) == 1);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> standard(
        OSSL_PROVIDER_load(context.get(), "default"), OSSL_PROVIDER_unload);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> mock(
        OSSL_PROVIDER_load(context.get(), "klang-hkdf-test"), OSSL_PROVIDER_unload);
    REQUIRE(standard);
    REQUIRE(mock);
    REQUIRE(mockCoreThreadStart);
    REQUIRE(EVP_set_default_properties(context.get(), "?provider=klang-hkdf-test") == 1);
    std::unique_ptr<void, NativeHkdfFree> h(nullptr, free);
    {
        ScopedDefaultContext selected(context.get());
        REQUIRE(selected.previous);
        int status = 0;
        h.reset(make(reinterpret_cast<const unsigned char *>("SHA-256"), &status));
        REQUIRE(h);
        REQUIRE(status == 0);
    }
    ContextLifetimeWork work{derive, h.get(), context.get()};
    mockProviderThread = {};
    mockStopThread = {};
    mockStopContext = nullptr;
    mockStops = 0;
    mockRegisterThreadStop = true;
    pthread_t caller;
    REQUIRE(pthread_create(&caller, nullptr, contextLifetimeCaller, &work) == 0);
    bool completed;
    {
        std::unique_lock lock(work.mutex);
        completed = work.condition.wait_for(
            lock, std::chrono::seconds(5), [&work] { return work.completed; });
    }
    auto callingThread = std::thread::id();
    int stops = 0;
    std::thread::id providerThread, stopThread;
    bool stoppedExpectedContext = false;
    if (completed) {
        callingThread = work.callerThread;
        stops = mockStops;
        providerThread = mockProviderThread;
        stopThread = mockStopThread;
        stoppedExpectedContext = mockStopContext == context.get();
        h.reset();
        mock.reset();
        standard.reset();
        context.reset();
    }
    {
        std::lock_guard lock(work.mutex);
        work.release = true;
    }
    work.condition.notify_all();
    int joined = pthread_join(caller, nullptr);
    REQUIRE(joined == 0);
    REQUIRE(completed);
    CHECK(work.status == 0);
    CHECK(stops == 1);
    CHECK(stoppedExpectedContext);
    CHECK(providerThread == callingThread);
    CHECK(stopThread == providerThread);
}

TEST_CASE("HKDF rejects active OpenSSL async jobs without changing caller state",
          "[crypto][hkdf][provider]") {
    if (!ASYNC_is_capable()) SKIP("OpenSSL async jobs are unsupported on this platform");
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                h: k::crypto::Hkdf! =
                    k::crypto::Hkdf::create(String("KLANG-VARIANT-DIGEST"));
            }
            catch (e: k::crypto::CryptoOperationException&) { return 0; }
            catch (e: k::crypto::CryptoException&) { return 1; }
            return 2;
        }
    )K");
    REQUIRE(jit);
    auto kProbe = jit->lookup_symbol<int(*)()>("test");
    auto make = reinterpret_cast<NativeHkdfNew>(dlsym(RTLD_DEFAULT, "__k_hkdf_new"));
    auto free = reinterpret_cast<NativeHkdfFree>(dlsym(RTLD_DEFAULT, "__k_hkdf_free"));
    auto freePrk = reinterpret_cast<NativeHkdfFree>(dlsym(RTLD_DEFAULT, "__k_hkdf_prk_free"));
    auto importPrk = reinterpret_cast<NativeHkdfImport>(dlsym(RTLD_DEFAULT, "__k_hkdf_import"));
    auto extract = reinterpret_cast<NativeHkdfExtract>(dlsym(RTLD_DEFAULT, "__k_hkdf_extract"));
    auto derive = reinterpret_cast<NativeHkdfDerive>(dlsym(RTLD_DEFAULT, "__k_hkdf_derive"));
    auto expand = reinterpret_cast<NativeHkdfExpand>(dlsym(RTLD_DEFAULT, "__k_hkdf_expand"));
    REQUIRE(kProbe);
    REQUIRE(make);
    REQUIRE(free);
    REQUIRE(freePrk);
    REQUIRE(importPrk);
    REQUIRE(extract);
    REQUIRE(derive);
    REQUIRE(expand);
    struct Reset { ~Reset() { variantNoStore = false; ERR_clear_error(); } } reset;
    variantNoStore = true;
    std::unique_ptr<OSSL_LIB_CTX, decltype(&OSSL_LIB_CTX_free)> context(
        OSSL_LIB_CTX_new(), OSSL_LIB_CTX_free);
    REQUIRE(context);
    REQUIRE(OSSL_PROVIDER_add_builtin(context.get(), "klang-variant", variantProviderInit) == 1);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> variant(
        OSSL_PROVIDER_load(context.get(), "klang-variant"), OSSL_PROVIDER_unload);
    REQUIRE(variant);
    REQUIRE(EVP_set_default_properties(
        context.get(), "?provider=klang-variant,?klang.kdf=one,?klang.digest=one") == 1);
    ScopedDefaultContext selected(context.get());
    REQUIRE(selected.previous);
    int status = 0;
    std::unique_ptr<void, NativeHkdfFree> h(
        make(reinterpret_cast<const unsigned char *>("KLANG-VARIANT-DIGEST"), &status), free);
    REQUIRE(h);
    REQUIRE(status == 0);
    unsigned char key[32] = {};
    std::unique_ptr<void, NativeHkdfFree> prk(
        importPrk(h.get(), key, sizeof(key), &status), freePrk);
    REQUIRE(prk);
    REQUIRE(status == 0);
    std::unique_ptr<ASYNC_WAIT_CTX, decltype(&ASYNC_WAIT_CTX_free)> waitContext(
        ASYNC_WAIT_CTX_new(), ASYNC_WAIT_CTX_free);
    REQUIRE(waitContext);
    int originalState;
    REQUIRE(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &originalState) == 0);
    struct RestoreState {
        int state;
        ~RestoreState() { pthread_setcancelstate(state, nullptr); }
    } restore{originalState};
    for (int cancelState : {PTHREAD_CANCEL_ENABLE, PTHREAD_CANCEL_DISABLE}) {
        REQUIRE(pthread_setcancelstate(cancelState, nullptr) == 0);
        for (bool marked : {false, true}) {
            INFO("cancellation state " << cancelState << ", marked errors " << marked);
            ERR_clear_error();
            if (marked) {
                ERR_raise(ERR_LIB_USER, 90);
                REQUIRE(ERR_set_mark() == 1);
                ERR_raise(ERR_LIB_USER, 91);
            }
            AsyncHkdfWork work{make, free, freePrk, importPrk, extract, derive, expand,
                               kProbe, h.get(), prk.get(), cancelState,
                               ERR_peek_error(), ERR_peek_last_error()};
            AsyncHkdfWork *argument = &work;
            ASYNC_JOB *job = nullptr;
            int result = -1;
            variantQueries = mockDigestParams = mockNews = mockSets = mockDerives = 0;
            int outcome = ASYNC_start_job(&job, waitContext.get(), &result,
                                          asyncHkdfCaller, &argument, sizeof(argument));
            REQUIRE(outcome == ASYNC_FINISH);
            CHECK(job == nullptr);
            CHECK(result == 0);
            CHECK(variantQueries == 0);
            CHECK(mockDigestParams == 0);
            CHECK(mockNews == 0);
            CHECK(mockSets == 0);
            CHECK(mockDerives == 0);
            int observed;
            REQUIRE(pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &observed) == 0);
            CHECK(observed == cancelState);
            REQUIRE(pthread_setcancelstate(observed, nullptr) == 0);
            CHECK(ERR_peek_error() == work.firstError);
            CHECK(ERR_peek_last_error() == work.lastError);
            if (marked) {
                CHECK(ERR_pop_to_mark() == 1);
                CHECK(ERR_peek_last_error() == work.firstError);
            }
        }
    }
}

TEST_CASE("HKDF cancellation waits for the worker and wipes output",
          "[crypto][hkdf][provider]") {
    auto jit = gen_jit_with_crypto(std::string(prelude));
    REQUIRE(jit);
    auto make = reinterpret_cast<NativeHkdfNew>(dlsym(RTLD_DEFAULT, "__k_hkdf_new"));
    auto free = reinterpret_cast<NativeHkdfFree>(dlsym(RTLD_DEFAULT, "__k_hkdf_free"));
    auto derive = reinterpret_cast<NativeHkdfDerive>(dlsym(RTLD_DEFAULT, "__k_hkdf_derive"));
    REQUIRE(make);
    REQUIRE(free);
    REQUIRE(derive);
    std::unique_ptr<OSSL_LIB_CTX, decltype(&OSSL_LIB_CTX_free)> context(
        OSSL_LIB_CTX_new(), OSSL_LIB_CTX_free);
    REQUIRE(context);
    REQUIRE(OSSL_PROVIDER_add_builtin(context.get(), "klang-hkdf-test", mockProviderInit) == 1);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> standard(
        OSSL_PROVIDER_load(context.get(), "default"), OSSL_PROVIDER_unload);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> mock(
        OSSL_PROVIDER_load(context.get(), "klang-hkdf-test"), OSSL_PROVIDER_unload);
    REQUIRE(standard);
    REQUIRE(mock);
    REQUIRE(EVP_set_default_properties(context.get(), "?provider=klang-hkdf-test") == 1);
    ScopedDefaultContext selected(context.get());
    REQUIRE(selected.previous);
    int status = 0;
    std::unique_ptr<void, NativeHkdfFree> h(
        make(reinterpret_cast<const unsigned char *>("SHA-256"), &status), free);
    REQUIRE(h);
    REQUIRE(status == 0);
    CancelWork work{derive, h.get()};
    std::memset(work.output, 0x5a, sizeof(work.output));
    mockEntered = mockRelease = false;
    mockBlock = true;
    pthread_t caller;
    int created = pthread_create(&caller, nullptr, cancelHkdfCaller, &work);
    if (created != 0) mockBlock = false;
    REQUIRE(created == 0);
    bool entered;
    {
        std::unique_lock lock(mockMutex);
        entered = mockCondition.wait_for(lock, std::chrono::seconds(5), [] { return mockEntered; });
    }
    int cancelled = entered ? pthread_cancel(caller) : -1;
    {
        std::lock_guard lock(mockMutex);
        mockRelease = true;
    }
    mockCondition.notify_all();
    void *result = nullptr;
    int joined = pthread_join(caller, &result);
    mockBlock = false;
    REQUIRE(entered);
    REQUIRE(cancelled == 0);
    REQUIRE(joined == 0);
    CHECK(result == PTHREAD_CANCELED);
    for (auto byte : work.output) CHECK(byte == 0);
    ERR_clear_error();
}

TEST_CASE("Snapshot-advertised HKDF supports a thread-affine provider",
          "[crypto][hkdf][provider]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            stage: int = 10;
            try {
                snapshot: k::crypto::CryptoCapabilities! =
                    k::crypto::CryptoCapabilities::snapshot();
                configs: Sequence<k::crypto::HkdfConfiguration>! = snapshot.hkdfConfigurations();
                it: ConstIterator<k::crypto::HkdfConfiguration>! = configs.constIterator();
                cur: OptionalConstRef<k::crypto::HkdfConfiguration> = it.next();
                while (cur.hasValue()) {
                    c: const k::crypto::HkdfConfiguration& = cur.get();
                    if (c.algorithm() == String("SHA-256")) {
                        if (c.hashSize() != 32u || !c.supportsExtract() ||
                            !c.supportsExpand() || !c.supportsDerive()) return 1;
                        stage = 11;
                        h: k::crypto::Hkdf! = k::crypto::Hkdf::create(c.algorithm());
                        if (!h) return 2;
                        ikm: unsigned byte[32];
                        salt: unsigned byte[16];
                        info: unsigned byte[8];
                        i: unsigned int = 0u;
                        while (i < 32u) { ikm[i] = (unsigned byte)i; ++i; }
                        i = 0u;
                        while (i < 16u) { salt[i] = (unsigned byte)i; ++i; }
                        i = 0u;
                        while (i < 8u) { info[i] = (unsigned byte)i; ++i; }
                        stage = 12;
                        p: k::crypto::HkdfPrk! = h.extract(ikm, salt);
                        stage = 13;
                        imported: k::crypto::HkdfPrk! = h.importPrk(ikm);
                        stage = 14;
                        expanded: unsigned byte[]! = p.expand(info, 32u);
                        importedOutput: unsigned byte[]! = imported.expand(info, 32u);
                        stage = 15;
                        derived: unsigned byte[]! = h.derive(ikm, salt, info, 32u);
                        if (p.keySize() != 32u || expanded.size != 32u ||
                            importedOutput.size != 32u || derived.size != 32u) return 3;
                        i = 0u;
                        while (i < 32u) {
                            if (expanded[i] != 0 || importedOutput[i] != 0 ||
                                derived[i] != 0) return 4;
                            ++i;
                        }
                        return 0;
                    }
                    cur = it.next();
                }
                return 5;
            } catch (e: k::crypto::CryptoException&) { return stage; }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    auto make = reinterpret_cast<NativeHkdfNew>(dlsym(RTLD_DEFAULT, "__k_hkdf_new"));
    auto free = reinterpret_cast<NativeHkdfFree>(dlsym(RTLD_DEFAULT, "__k_hkdf_free"));
    auto derive = reinterpret_cast<NativeHkdfDerive>(dlsym(RTLD_DEFAULT, "__k_hkdf_derive"));
    auto symmetricSnapshot = reinterpret_cast<void *(*)(int *)>(
        dlsym(RTLD_DEFAULT, "__k_cap_symmetric_new"));
    auto freeSymmetricSnapshot = reinterpret_cast<void (*)(void *)>(
        dlsym(RTLD_DEFAULT, "__k_cap_symmetric_free"));
    REQUIRE(make);
    REQUIRE(free);
    REQUIRE(derive);
    REQUIRE(symmetricSnapshot);
    REQUIRE(freeSymmetricSnapshot);
    std::unique_ptr<OSSL_LIB_CTX, decltype(&OSSL_LIB_CTX_free)> context(
        OSSL_LIB_CTX_new(), OSSL_LIB_CTX_free);
    REQUIRE(context);
    REQUIRE(OSSL_PROVIDER_add_builtin(context.get(), "klang-hkdf-test", mockProviderInit) == 1);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> standard(
        OSSL_PROVIDER_load(context.get(), "default"), OSSL_PROVIDER_unload);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> mock(
        OSSL_PROVIDER_load(context.get(), "klang-hkdf-test"), OSSL_PROVIDER_unload);
    REQUIRE(standard);
    REQUIRE(mock);
    REQUIRE(EVP_set_default_properties(context.get(), "?provider=klang-hkdf-test") == 1);
    std::unique_ptr<EVP_KDF, decltype(&EVP_KDF_free)> selectedKdf(
        EVP_KDF_fetch(context.get(), "HKDF", nullptr), EVP_KDF_free);
    REQUIRE(selectedKdf);
    REQUIRE(EVP_KDF_get0_provider(selectedKdf.get()) == mock.get());
    ScopedDefaultContext selected(context.get());
    REQUIRE(selected.previous);
    struct Reset {
        ~Reset() { mockEnforceThreadAffinity = false; ERR_clear_error(); }
    } reset;
    mockExpectedThread = std::this_thread::get_id();
    mockAffinityViolations = 0;
    mockDerives = 0;
    mockEnforceThreadAffinity = true;
    ERR_clear_error();
    CHECK(test() == 0);
    CHECK(mockDerives > 3);
    CHECK(mockAffinityViolations == 0);
    CHECK(ERR_peek_error() == 0);
    int status = 0;
    std::unique_ptr<void, NativeHkdfFree> h(
        make(reinterpret_cast<const unsigned char *>("SHA-256"), &status), free);
    REQUIRE(h);
    REQUIRE(status == 0);
    ERR_raise(ERR_LIB_USER, 90);
    const auto first = ERR_peek_error();
    REQUIRE(ERR_set_mark() == 1);
    ERR_raise(ERR_LIB_USER, 91);
    const auto last = ERR_peek_last_error();
    int violations = mockAffinityViolations;
    status = 0;
    void *symmetric = symmetricSnapshot(&status);
    CHECK(symmetric == nullptr);
    CHECK(status == -3);
    if (symmetric) freeSymmetricSnapshot(symmetric);
    CHECK(mockAffinityViolations > violations);
    CHECK(ERR_peek_error() == first);
    CHECK(ERR_peek_last_error() == last);
    violations = mockAffinityViolations;
    unsigned char key[32] = {}, output[32] = {};
    CHECK(derive(h.get(), 0, key, sizeof(key), nullptr, 0, 1, nullptr, 0,
                 output, sizeof(output)) == -3);
    CHECK(mockAffinityViolations > violations);
    CHECK(ERR_peek_error() == first);
    CHECK(ERR_peek_last_error() == last);
    CHECK(ERR_pop_to_mark() == 1);
    CHECK(ERR_peek_last_error() == first);
}

TEST_CASE("HKDF accepts no-store digest methods with stable semantics",
          "[crypto][hkdf][provider]") {
    auto jit = gen_jit_with_crypto(std::string(prelude));
    REQUIRE(jit);
    auto make = reinterpret_cast<NativeHkdfNew>(dlsym(RTLD_DEFAULT, "__k_hkdf_new"));
    auto free = reinterpret_cast<NativeHkdfFree>(dlsym(RTLD_DEFAULT, "__k_hkdf_free"));
    auto derive = reinterpret_cast<NativeHkdfDerive>(dlsym(RTLD_DEFAULT, "__k_hkdf_derive"));
    REQUIRE(make);
    REQUIRE(free);
    REQUIRE(derive);
    struct Reset { ~Reset() { variantNoStore = false; ERR_clear_error(); } } reset;
    variantNoStore = true;
    std::unique_ptr<OSSL_LIB_CTX, decltype(&OSSL_LIB_CTX_free)> context(
        OSSL_LIB_CTX_new(), OSSL_LIB_CTX_free);
    REQUIRE(context);
    REQUIRE(OSSL_PROVIDER_add_builtin(context.get(), "klang-variant", variantProviderInit) == 1);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> variant(
        OSSL_PROVIDER_load(context.get(), "klang-variant"), OSSL_PROVIDER_unload);
    REQUIRE(variant);
    REQUIRE(EVP_set_default_properties(
        context.get(), "?provider=klang-variant,?klang.kdf=one,?klang.digest=one") == 1);
    ScopedDefaultContext selected(context.get());
    REQUIRE(selected.previous);
    variantQueries = 0;
    int status = 0;
    std::unique_ptr<void, NativeHkdfFree> h(
        make(reinterpret_cast<const unsigned char *>("KLANG-VARIANT-DIGEST"), &status), free);
    REQUIRE(h);
    REQUIRE(status == 0);
    std::unique_ptr<EVP_MD, decltype(&EVP_MD_free)> first(
        EVP_MD_fetch(context.get(), "KLANG-VARIANT-DIGEST", nullptr), EVP_MD_free);
    std::unique_ptr<EVP_MD, decltype(&EVP_MD_free)> second(
        EVP_MD_fetch(context.get(), "KLANG-VARIANT-DIGEST", nullptr), EVP_MD_free);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(EVP_MD_get0_provider(first.get()) == variant.get());
    REQUIRE(EVP_MD_get0_provider(second.get()) == variant.get());
    REQUIRE(variantQueries > 0);
    unsigned char key[32] = {}, out[32];
    std::memset(out, 0x5a, sizeof(out));
    mockDerives = 0;
    CHECK(derive(h.get(), 0, key, sizeof(key), nullptr, 0, 1, nullptr, 0,
                 out, sizeof(out)) == 0);
    CHECK(mockDerives == 1);
    for (auto byte : out) CHECK(byte == 0);
}

TEST_CASE("Snapshot-advertised HKDF operates with a no-store provider",
          "[crypto][hkdf][provider]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                snapshot: k::crypto::CryptoCapabilities! =
                    k::crypto::CryptoCapabilities::snapshot();
                configs: Sequence<k::crypto::HkdfConfiguration>! = snapshot.hkdfConfigurations();
                it: ConstIterator<k::crypto::HkdfConfiguration>! = configs.constIterator();
                cur: OptionalConstRef<k::crypto::HkdfConfiguration> = it.next();
                while (cur.hasValue()) {
                    c: const k::crypto::HkdfConfiguration& = cur.get();
                    if (c.algorithm() == String("SHA-256")) {
                        if (c.hashSize() != 32u || !c.supportsExtract() ||
                            !c.supportsExpand() || !c.supportsDerive()) return 1;
                        h: k::crypto::Hkdf! = k::crypto::Hkdf::create(c.algorithm());
                        if (!h) return 2;
                        ikm: unsigned byte[32];
                        salt: unsigned byte[16];
                        info: unsigned byte[8];
                        i: unsigned int = 0u;
                        while (i < 32u) { ikm[i] = (unsigned byte)i; ++i; }
                        i = 0u;
                        while (i < 16u) { salt[i] = (unsigned byte)i; ++i; }
                        i = 0u;
                        while (i < 8u) { info[i] = (unsigned byte)i; ++i; }
                        p: k::crypto::HkdfPrk! = h.extract(ikm, salt);
                        imported: k::crypto::HkdfPrk! = h.importPrk(ikm);
                        expanded: unsigned byte[]! = imported.expand(info, 32u);
                        derived: unsigned byte[]! = h.derive(ikm, salt, info, 32u);
                        if (p.keySize() != 32u || expanded.size != 32u ||
                            derived.size != 32u) return 3;
                        i = 0u;
                        while (i < 32u) {
                            if (expanded[i] != 0 || derived[i] != 0) return 4;
                            ++i;
                        }
                        return 0;
                    }
                    cur = it.next();
                }
                return 5;
            } catch (e: k::crypto::CryptoException&) { return 6; }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    auto make = reinterpret_cast<NativeHkdfNew>(dlsym(RTLD_DEFAULT, "__k_hkdf_new"));
    auto free = reinterpret_cast<NativeHkdfFree>(dlsym(RTLD_DEFAULT, "__k_hkdf_free"));
    auto derive = reinterpret_cast<NativeHkdfDerive>(dlsym(RTLD_DEFAULT, "__k_hkdf_derive"));
    REQUIRE(make);
    REQUIRE(free);
    REQUIRE(derive);
    struct Reset { ~Reset() { mockNoStore = false; ERR_clear_error(); } } reset;
    mockNoStore = true;
    std::unique_ptr<OSSL_LIB_CTX, decltype(&OSSL_LIB_CTX_free)> context(
        OSSL_LIB_CTX_new(), OSSL_LIB_CTX_free);
    REQUIRE(context);
    REQUIRE(OSSL_PROVIDER_add_builtin(context.get(), "klang-hkdf-test", mockProviderInit) == 1);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> standard(
        OSSL_PROVIDER_load(context.get(), "default"), OSSL_PROVIDER_unload);
    std::unique_ptr<OSSL_PROVIDER, decltype(&OSSL_PROVIDER_unload)> mock(
        OSSL_PROVIDER_load(context.get(), "klang-hkdf-test"), OSSL_PROVIDER_unload);
    REQUIRE(standard);
    REQUIRE(mock);
    REQUIRE(EVP_set_default_properties(context.get(), "?provider=klang-hkdf-test") == 1);
    ScopedDefaultContext selected(context.get());
    REQUIRE(selected.previous);
    mockNoStoreKdfQueries = 0;
    int status = 0;
    std::unique_ptr<void, NativeHkdfFree> h(
        make(reinterpret_cast<const unsigned char *>("SHA-256"), &status), free);
    REQUIRE(h);
    REQUIRE(status == 0);
    std::unique_ptr<EVP_KDF, decltype(&EVP_KDF_free)> first(
        EVP_KDF_fetch(context.get(), "HKDF", nullptr), EVP_KDF_free);
    std::unique_ptr<EVP_KDF, decltype(&EVP_KDF_free)> second(
        EVP_KDF_fetch(context.get(), "HKDF", nullptr), EVP_KDF_free);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(EVP_KDF_get0_provider(first.get()) == mock.get());
    REQUIRE(EVP_KDF_get0_provider(second.get()) == mock.get());
    REQUIRE(mockNoStoreKdfQueries > 0);
    unsigned char key[32] = {}, out[32];
    std::memset(out, 0x5a, sizeof(out));
    mockDerives = 0;
    CHECK(derive(h.get(), 0, key, sizeof(key), nullptr, 0, 1, nullptr, 0,
                 out, sizeof(out)) == 0);
    CHECK(mockDerives == 1);
    for (auto byte : out) CHECK(byte == 0);
    CHECK(test() == 0);
    REQUIRE(EVP_set_default_properties(context.get(), "provider=default") == 1);
    std::unique_ptr<EVP_KDF, decltype(&EVP_KDF_free)> restricted(
        EVP_KDF_fetch(context.get(), "HKDF", nullptr), EVP_KDF_free);
    REQUIRE(restricted);
    REQUIRE(EVP_KDF_get0_provider(restricted.get()) == standard.get());
    int calls = mockDerives;
    std::memset(out, 0x5a, sizeof(out));
    CHECK(derive(h.get(), 0, key, sizeof(key), nullptr, 0, 1, nullptr, 0,
                 out, sizeof(out)) == -3);
    CHECK(mockDerives == calls);
    for (auto byte : out) CHECK(byte == 0);
}
