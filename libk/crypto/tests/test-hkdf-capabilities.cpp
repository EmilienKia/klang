/*
 * K Language HKDF capability snapshot integration tests
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

#include <openssl/async.h>
#include <openssl/core_dispatch.h>
#include <openssl/core_names.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/provider.h>
#include <openssl/proverr.h>

#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <dlfcn.h>
#include <mutex>
#include <pthread.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

enum class MockBehavior {
    extract, expand, derive, reject, unknown, silent, mixed, successError,
    noMemory, setReject, setUnknown, setSilent
};
MockBehavior behavior;
bool asyncPauseRequested;
int asyncPauseResult;
int asyncDeriveCalls;

void* mockNew(void*) {
    if (behavior == MockBehavior::noMemory) {
        ERR_raise(ERR_LIB_PROV, ERR_R_MALLOC_FAILURE);
        return nullptr;
    }
    return std::calloc(1, 1);
}
void mockFree(void* ctx) { std::free(ctx); }

int mockSetParams(void*, const OSSL_PARAM params[]) {
    if (behavior == MockBehavior::setReject) {
        ERR_raise(ERR_LIB_PROV, PROV_R_INVALID_DIGEST);
        return 0;
    }
    if (behavior == MockBehavior::setUnknown) {
        ERR_raise(ERR_LIB_PROV, PROV_R_IN_ERROR_STATE);
        return 0;
    }
    if (behavior == MockBehavior::setSilent) return 0;
    const OSSL_PARAM* digest = OSSL_PARAM_locate_const(params, OSSL_KDF_PARAM_DIGEST);
    const char* name = nullptr;
    if (!digest || !OSSL_PARAM_get_utf8_string_ptr(digest, &name)) {
        ERR_raise(ERR_LIB_USER, 4);
        return 0;
    }
    return 1;
}

int mockDerive(void*, unsigned char* output, size_t outputSize, const OSSL_PARAM params[]) {
    ++asyncDeriveCalls;
    if (asyncPauseRequested && ASYNC_get_current_job() != nullptr) {
        asyncPauseRequested = false;
        asyncPauseResult = ASYNC_pause_job();
    }
    const OSSL_PARAM* modeParam = OSSL_PARAM_locate_const(params, OSSL_KDF_PARAM_MODE);
    const OSSL_PARAM* digestParam = OSSL_PARAM_locate_const(params, OSSL_KDF_PARAM_DIGEST);
    const OSSL_PARAM* key = OSSL_PARAM_locate_const(params, OSSL_KDF_PARAM_KEY);
    const OSSL_PARAM* salt = OSSL_PARAM_locate_const(params, OSSL_KDF_PARAM_SALT);
    const OSSL_PARAM* info = OSSL_PARAM_locate_const(params, OSSL_KDF_PARAM_INFO);
    const char* name = nullptr;
    int mode = -1;
    if (!modeParam || !OSSL_PARAM_get_int(modeParam, &mode) || !digestParam ||
        !OSSL_PARAM_get_utf8_string_ptr(digestParam, &name) || !key) {
        ERR_raise(ERR_LIB_USER, 1);
        return 0;
    }
    EVP_MD* md = EVP_MD_fetch(nullptr, name, nullptr);
    if (!md) return 0;
    const int size = EVP_MD_get_size(md);
    EVP_MD_free(md);
    if (size <= 0 || outputSize != static_cast<size_t>(size) ||
        (mode == EVP_KDF_HKDF_MODE_EXPAND_ONLY
            ? key->data_size != outputSize || salt || !info || info->data_size != 8
            : key->data_size != 32 || !salt || salt->data_size != 16 ||
              (mode == EVP_KDF_HKDF_MODE_EXTRACT_ONLY ? info != nullptr
                                                      : !info || info->data_size != 8))) {
        ERR_raise(ERR_LIB_USER, 2);
        return 0;
    }
    if (behavior == MockBehavior::unknown || behavior == MockBehavior::mixed) {
        ERR_raise(ERR_LIB_PROV, PROV_R_IN_ERROR_STATE);
        if (behavior == MockBehavior::mixed) ERR_raise(ERR_LIB_PROV, PROV_R_INVALID_MODE);
        return 0;
    }
    if (behavior == MockBehavior::silent) return 0;
    if (behavior == MockBehavior::successError) {
        ERR_raise(ERR_LIB_USER, 3);
        return 1;
    }
    const bool accepted = (behavior == MockBehavior::extract &&
                           mode == EVP_KDF_HKDF_MODE_EXTRACT_ONLY) ||
                          (behavior == MockBehavior::expand &&
                           mode == EVP_KDF_HKDF_MODE_EXPAND_ONLY) ||
                          (behavior == MockBehavior::derive &&
                           mode == EVP_KDF_HKDF_MODE_EXTRACT_AND_EXPAND);
    if (!accepted) {
        ERR_raise(ERR_LIB_PROV, PROV_R_INVALID_MODE);
        return 0;
    }
    for (size_t i = 0; i < outputSize; ++i) output[i] = 0;
    return 1;
}

const OSSL_DISPATCH mockKdfDispatch[] = {
    {OSSL_FUNC_KDF_NEWCTX, reinterpret_cast<void (*)(void)>(mockNew)},
    {OSSL_FUNC_KDF_FREECTX, reinterpret_cast<void (*)(void)>(mockFree)},
    {OSSL_FUNC_KDF_DERIVE, reinterpret_cast<void (*)(void)>(mockDerive)},
    {OSSL_FUNC_KDF_SET_CTX_PARAMS, reinterpret_cast<void (*)(void)>(mockSetParams)},
    {0, nullptr}
};

const OSSL_ALGORITHM mockKdfs[] = {
    {"HKDF", "provider=klanghkdfmock", mockKdfDispatch, nullptr},
    {nullptr, nullptr, nullptr, nullptr}
};

const OSSL_ALGORITHM* mockQuery(void*, int operation, int* noStore) {
    *noStore = 0;
    return operation == OSSL_OP_KDF ? mockKdfs : nullptr;
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

bool mixedDisabledFetch;
int disabledFetchQueries;
int disabledFetchOperation = OSSL_OP_KDF;

const OSSL_ALGORITHM* disabledFetchQuery(void*, int operation, int* noStore) {
    *noStore = 1;
    if (operation == disabledFetchOperation) {
        ++disabledFetchQueries;
        ERR_raise(ERR_LIB_PROV, ERR_R_DISABLED);
        if (mixedDisabledFetch) ERR_raise(ERR_LIB_PROV, PROV_R_IN_ERROR_STATE);
    }
    return nullptr;
}

const OSSL_DISPATCH disabledFetchDispatch[] = {
    {OSSL_FUNC_PROVIDER_QUERY_OPERATION, reinterpret_cast<void (*)(void)>(disabledFetchQuery)},
    {0, nullptr}
};

int disabledFetchProviderInit(const OSSL_CORE_HANDLE*, const OSSL_DISPATCH*,
                              const OSSL_DISPATCH** out, void** ctx) {
    *out = disabledFetchDispatch;
    *ctx = nullptr;
    return 1;
}

using CapabilityDiscover = void*(*)(int*);
using CapabilityRelease = void(*)(void*);

struct AsyncCapabilityWork {
    CapabilityDiscover discover;
    CapabilityRelease release;
};

int asyncCapabilityCaller(void* arg) {
    auto* work = *static_cast<AsyncCapabilityWork**>(arg);
    int status = -1;
    void* snapshot = work->discover(&status);
    const bool success = snapshot && status == 0;
    work->release(snapshot);
    return success ? 0 : 1;
}

struct CapabilityCancelWork {
    OSSL_LIB_CTX* context;
    OSSL_LIB_CTX* previous = nullptr;
    CapabilityDiscover discover;
    CapabilityRelease release;
    bool marked;
    bool startDisabled = false;
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool unblock = false;
    bool cleaned = false;
    bool cleanedEarly = false;
    bool completedDiscovery = false;
};

CapabilityCancelWork* cancelWork = nullptr;

const OSSL_ALGORITHM* cancelProviderQuery(void*, int operation, int* noStore) {
    *noStore = 1;
    if (operation == OSSL_OP_DIGEST && cancelWork) {
        std::unique_lock lock(cancelWork->mutex);
        if (!cancelWork->entered) {
            cancelWork->entered = true;
            cancelWork->condition.notify_all();
            cancelWork->condition.wait(lock, [] { return cancelWork->unblock; });
        }
    }
    return nullptr;
}

const OSSL_DISPATCH cancelProviderDispatch[] = {
    {OSSL_FUNC_PROVIDER_QUERY_OPERATION, reinterpret_cast<void (*)(void)>(cancelProviderQuery)},
    {0, nullptr}
};

int cancelProviderInit(const OSSL_CORE_HANDLE*, const OSSL_DISPATCH*,
                       const OSSL_DISPATCH** out, void** ctx) {
    *out = cancelProviderDispatch;
    *ctx = nullptr;
    return 1;
}

void cleanupCapabilityCaller(void* arg) {
    auto* work = static_cast<CapabilityCancelWork*>(arg);
    {
        std::lock_guard lock(work->mutex);
        work->cleanedEarly = !work->unblock;
        work->cleaned = true;
    }
    work->condition.notify_all();
    if (work->previous) OSSL_LIB_CTX_set0_default(work->previous);
    OPENSSL_thread_stop_ex(work->context);
    OPENSSL_thread_stop();
}

void* cancelCapabilityCaller(void* arg) {
    auto* work = static_cast<CapabilityCancelWork*>(arg);
    int setup = work->startDisabled ? pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, nullptr) : 0;
    pthread_cleanup_push(cleanupCapabilityCaller, work);
    work->previous = OSSL_LIB_CTX_set0_default(work->context);
    if (work->previous && setup == 0) {
        if (work->marked) ERR_raise(ERR_LIB_USER, 97);
        int status = -1;
        void* snapshot = work->discover(&status);
        {
            std::lock_guard lock(work->mutex);
            work->completedDiscovery = snapshot && status == 0;
        }
        work->release(snapshot);
        if (work->startDisabled) {
            pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);
            pthread_testcancel();
        }
    }
    pthread_cleanup_pop(1);
    return nullptr;
}

int childResult(pid_t child) {
    int status = 0;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    return waited == child && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

constexpr auto prelude = R"K(
    module __hkdf_capabilities_test__;
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

    uniqueNames(seq: const Sequence<String>&) : bool {
        outer: ConstIterator<String>! = seq.constIterator();
        x: OptionalConstRef<String> = outer.next();
        while (x.hasValue()) {
            inner: ConstIterator<String>! = seq.constIterator();
            y: OptionalConstRef<String> = inner.next();
            count: unsigned int = 0u;
            while (y.hasValue()) {
                if (x.get() == y.get()) ++count;
                y = inner.next();
            }
            if (count != 1u) return false;
            x = outer.next();
        }
        return true;
    }

    sameConfigurations(left: const Sequence<k::crypto::HkdfConfiguration>&,
            right: const Sequence<k::crypto::HkdfConfiguration>&) : bool {
        a: ConstIterator<k::crypto::HkdfConfiguration>! = left.constIterator();
        b: ConstIterator<k::crypto::HkdfConfiguration>! = right.constIterator();
        x: OptionalConstRef<k::crypto::HkdfConfiguration> = a.next();
        y: OptionalConstRef<k::crypto::HkdfConfiguration> = b.next();
        while (x.hasValue() && y.hasValue()) {
            if (x.get().algorithm() != y.get().algorithm() ||
                x.get().hashSize() != y.get().hashSize() ||
                x.get().supportsExtract() != y.get().supportsExtract() ||
                x.get().supportsExpand() != y.get().supportsExpand() ||
                x.get().supportsDerive() != y.get().supportsDerive()) return false;
            x = a.next();
            y = b.next();
        }
        return !x.hasValue() && !y.hasValue();
    }

    usableSha256() : bool {
        try {
            hkdf: k::crypto::Hkdf! = k::crypto::Hkdf::SHA256();
            if (!hkdf) return false;
            ikm: unsigned byte[32];
            salt: unsigned byte[16];
            info: unsigned byte[8];
            i: unsigned int = 0u;
            while (i < 32u) { ikm[i] = (unsigned byte)i; ++i; }
            i = 0u;
            while (i < 16u) { salt[i] = (unsigned byte)i; ++i; }
            i = 0u;
            while (i < 8u) { info[i] = (unsigned byte)i; ++i; }
            extracted: k::crypto::HkdfPrk! = hkdf.extract(ikm, salt);
            raw: unsigned byte[32];
            i = 0u;
            while (i < 32u) { raw[i] = (unsigned byte)i; ++i; }
            imported: k::crypto::HkdfPrk! = hkdf.importPrk(raw);
            expanded: unsigned byte[]! = imported.expand(info, 32u);
            derived: unsigned byte[]! = hkdf.derive(ikm, salt, info, 32u);
            return extracted.keySize() == 32u && expanded.size == 32u && derived.size == 32u;
        } catch (e: k::crypto::CryptoException&) {
            return false;
        }
    }
)K";

} // namespace

TEST_CASE("Inline capability discovery preserves ASYNC scheduler cancellation state",
          "[crypto][capabilities][hkdf][provider]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __capability_async_test__;
        import k::crypto;
        test() : int { return 0; }
    )K");
    REQUIRE(jit);
    auto discover = reinterpret_cast<CapabilityDiscover>(
        dlsym(RTLD_DEFAULT, "__k_cap_hkdf_new"));
    auto release = reinterpret_cast<CapabilityRelease>(
        dlsym(RTLD_DEFAULT, "__k_cap_digest_free"));
    REQUIRE(discover);
    REQUIRE(release);
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        ERR_clear_error();
        if (!ASYNC_is_capable() || !ASYNC_init_thread(0, 0)) _exit(77);
        ASYNC_WAIT_CTX* wait = ASYNC_WAIT_CTX_new();
        OSSL_LIB_CTX* context = OSSL_LIB_CTX_new();
        if (!wait || !context ||
            !OSSL_PROVIDER_add_builtin(context, "klanghkdfmock", mockProviderInit)) _exit(80);
        OSSL_PROVIDER* standard = OSSL_PROVIDER_load(context, "default");
        OSSL_PROVIDER* mock = OSSL_PROVIDER_load(context, "klanghkdfmock");
        if (!standard || !mock ||
            !EVP_set_default_properties(context, "?provider=klanghkdfmock")) _exit(81);
        EVP_MD* md = EVP_MD_fetch(context, "SHA-256", nullptr);
        if (!md) _exit(77);
        EVP_MD_free(md);
        EVP_KDF* kdf = EVP_KDF_fetch(context, "HKDF", nullptr);
        if (!kdf || EVP_KDF_get0_provider(kdf) != mock) _exit(82);
        EVP_KDF_free(kdf);
        OSSL_LIB_CTX* previous = OSSL_LIB_CTX_set0_default(context);
        if (!previous) _exit(83);
        behavior = MockBehavior::extract;
        AsyncCapabilityWork work{discover, release};
        for (int initial : {PTHREAD_CANCEL_ENABLE, PTHREAD_CANCEL_DISABLE}) {
            for (int marked = 0; marked != 2; ++marked) {
                if (pthread_setcancelstate(initial, nullptr) != 0) _exit(84);
                ERR_clear_error();
                unsigned long first = 0, last = 0;
                if (marked) {
                    ERR_raise(ERR_LIB_USER, 98);
                    first = ERR_peek_error();
                    if (!first || !ERR_set_mark()) _exit(85);
                    ERR_raise(ERR_LIB_USER, 99);
                    last = ERR_peek_last_error();
                }
                asyncPauseRequested = true;
                asyncPauseResult = -1;
                asyncDeriveCalls = 0;
                AsyncCapabilityWork* argument = &work;
                ASYNC_JOB* job = nullptr;
                int result = -1;
                int outcome = ASYNC_start_job(&job, wait, &result, asyncCapabilityCaller,
                                              &argument, sizeof(argument));
                if (!marked) {
                    if (outcome != ASYNC_PAUSE || job == nullptr) _exit(86);
                    int observed;
                    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &observed) != 0 ||
                        observed != initial || pthread_setcancelstate(initial, nullptr) != 0)
                        _exit(87);
                    outcome = ASYNC_start_job(&job, wait, &result, asyncCapabilityCaller,
                                              &argument, sizeof(argument));
                    if (asyncPauseResult != 1) _exit(88);
                }
                if (outcome != ASYNC_FINISH || job != nullptr || result != 0 ||
                    asyncDeriveCalls == 0) _exit(89);
                int observed;
                if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &observed) != 0 ||
                    observed != initial || pthread_setcancelstate(initial, nullptr) != 0)
                    _exit(90);
                if (marked) {
                    if (asyncPauseResult != -1 || ERR_peek_error() != first ||
                        ERR_peek_last_error() != last || !ERR_pop_to_mark() ||
                        ERR_get_error() != first || ERR_get_error()) _exit(91);
                } else if (ERR_peek_error()) {
                    _exit(92);
                }
            }
        }
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, nullptr);
        ASYNC_WAIT_CTX_free(wait);
        ASYNC_cleanup_thread();
        if (OSSL_LIB_CTX_set0_default(previous) != context) _exit(93);
        OPENSSL_thread_stop_ex(context);
        OSSL_PROVIDER_unload(mock);
        OSSL_PROVIDER_unload(standard);
        OSSL_LIB_CTX_free(context);
        _exit(0);
    }
    const int result = childResult(child);
    if (result == 77) SKIP("OpenSSL ASYNC jobs or SHA-256 are unavailable");
    CHECK(result == 0);
}

TEST_CASE("Capability discovery defers cancellation until its provider work completes",
          "[crypto][capabilities][hkdf][provider]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __capability_cancellation_test__;
        import k::crypto;
        test() : int { return 0; }
    )K");
    REQUIRE(jit);
    auto release = reinterpret_cast<CapabilityRelease>(
        dlsym(RTLD_DEFAULT, "__k_cap_digest_free"));
    CapabilityDiscover discover[] = {
        reinterpret_cast<CapabilityDiscover>(dlsym(RTLD_DEFAULT, "__k_cap_digest_new")),
        reinterpret_cast<CapabilityDiscover>(dlsym(RTLD_DEFAULT, "__k_cap_hmac_digest_new")),
        reinterpret_cast<CapabilityDiscover>(dlsym(RTLD_DEFAULT, "__k_cap_hkdf_new"))
    };
    REQUIRE(release);
    for (auto function : discover) REQUIRE(function);
    for (int scenario = 0; scenario < 6; ++scenario) {
        const pid_t child = fork();
        REQUIRE(child >= 0);
        if (child == 0) {
            ERR_clear_error();
            OSSL_LIB_CTX* context = OSSL_LIB_CTX_new();
            if (!context ||
                !OSSL_PROVIDER_add_builtin(context, "klangcapcancel", cancelProviderInit))
                _exit(80);
            OSSL_PROVIDER* standard = OSSL_PROVIDER_load(context, "default");
            OSSL_PROVIDER* mock = OSSL_PROVIDER_load(context, "klangcapcancel");
            if (!standard || !mock) _exit(81);
            if (!EVP_set_default_properties(context, "provider=klangcapcancel")) _exit(82);
            CapabilityCancelWork work;
            work.context = context;
            work.discover = discover[scenario >= 3 ? 2 : scenario];
            work.release = release;
            work.marked = scenario != 3 && scenario != 5;
            work.startDisabled = scenario >= 4;
            cancelWork = &work;
            pthread_t caller;
            if (pthread_create(&caller, nullptr, cancelCapabilityCaller, &work) != 0) _exit(83);
            bool entered;
            {
                std::unique_lock lock(work.mutex);
                entered = work.condition.wait_for(lock, std::chrono::seconds(5),
                                                  [&] { return work.entered; });
            }
            int cancelled = pthread_cancel(caller);
            bool early;
            {
                std::unique_lock lock(work.mutex);
                work.condition.wait_for(lock, std::chrono::milliseconds(250),
                                        [&] { return work.cleaned; });
                early = work.cleaned;
                work.unblock = true;
            }
            work.condition.notify_all();
            void* result = nullptr;
            int joined = pthread_join(caller, &result);
            if (!entered || cancelled != 0 || early || joined != 0 ||
                result != PTHREAD_CANCELED || !work.cleaned || work.cleanedEarly ||
                (work.startDisabled && !work.completedDiscovery)) _exit(84);
            cancelWork = nullptr;
            OSSL_PROVIDER_unload(mock);
            OSSL_PROVIDER_unload(standard);
            OSSL_LIB_CTX_free(context);
            _exit(0);
        }
        INFO("Cancellation scenario " << scenario);
        CHECK(childResult(child) == 0);
    }
}

TEST_CASE("HKDF discovery treats disabled KDF fetch as unavailable",
          "[crypto][capabilities][hkdf][provider]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __hkdf_disabled_fetch_test__;
        import k::crypto;
        test() : int { return 0; }
    )K");
    REQUIRE(jit);
    auto discover = reinterpret_cast<void*(*)(int*)>(dlsym(RTLD_DEFAULT, "__k_cap_hkdf_new"));
    auto count = reinterpret_cast<unsigned int(*)(const void*)>(
        dlsym(RTLD_DEFAULT, "__k_cap_digest_count"));
    auto release = reinterpret_cast<void(*)(void*)>(dlsym(RTLD_DEFAULT, "__k_cap_digest_free"));
    REQUIRE(discover);
    REQUIRE(count);
    REQUIRE(release);
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        ERR_clear_error();
        OSSL_LIB_CTX* custom = OSSL_LIB_CTX_new();
        if (!custom) _exit(80);
        if (!OSSL_PROVIDER_add_builtin(custom, "klangdisabled", disabledFetchProviderInit))
            _exit(81);
        OSSL_PROVIDER* provider = OSSL_PROVIDER_load(custom, "klangdisabled");
        if (!provider) _exit(82);
        OSSL_LIB_CTX* previous = OSSL_LIB_CTX_set0_default(custom);
        if (!previous) _exit(83);
        disabledFetchOperation = OSSL_OP_KDF;
        mixedDisabledFetch = false;
        EVP_KDF* unavailable = EVP_KDF_fetch(custom, "HKDF", nullptr);
        if (unavailable) _exit(91);
        bool sawDisabled = false;
        unsigned long error;
        while ((error = ERR_get_error()) != 0) {
            if (!ERR_SYSTEM_ERROR(error) && ERR_GET_REASON(error) == ERR_R_DISABLED)
                sawDisabled = true;
        }
        if (!sawDisabled) _exit(92);
        for (int mixed = 0; mixed != 2; ++mixed) {
            mixedDisabledFetch = mixed != 0;
            for (int marked = 0; marked != 2; ++marked) {
                ERR_clear_error();
                unsigned long first = 0, last = 0;
                if (marked) {
                    ERR_raise(ERR_LIB_USER, 98);
                    first = ERR_peek_error();
                    if (!first || !ERR_set_mark()) _exit(84);
                    ERR_raise(ERR_LIB_USER, 99);
                    last = ERR_peek_last_error();
                }
                disabledFetchQueries = 0;
                int status = 0;
                void* snapshot = discover(&status);
                if (disabledFetchQueries == 0) _exit(85);
                if (mixed) {
                    if (snapshot || status == 0) _exit(86);
                } else {
                    if (!snapshot || status != 0 || count(snapshot) != 0) _exit(87);
                    release(snapshot);
                }
                if (marked) {
                    if (ERR_peek_error() != first || ERR_peek_last_error() != last ||
                        !ERR_pop_to_mark() || ERR_get_error() != first || ERR_get_error())
                        _exit(88);
                } else if (ERR_peek_error()) {
                    _exit(89);
                }
            }
        }
        if (OSSL_LIB_CTX_set0_default(previous) != custom) _exit(90);
        OSSL_PROVIDER_unload(provider);
        OSSL_LIB_CTX_free(custom);
        _exit(0);
    }
    CHECK(childResult(child) == 0);
}

TEST_CASE("HMAC discovery retains disabled fetch failure classification",
          "[crypto][capabilities][hmac][provider]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __hmac_disabled_fetch_test__;
        import k::crypto;
        test() : int { return 0; }
    )K");
    REQUIRE(jit);
    auto discover = reinterpret_cast<CapabilityDiscover>(
        dlsym(RTLD_DEFAULT, "__k_cap_hmac_digest_new"));
    auto release = reinterpret_cast<CapabilityRelease>(
        dlsym(RTLD_DEFAULT, "__k_cap_digest_free"));
    REQUIRE(discover);
    REQUIRE(release);
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        ERR_clear_error();
        OSSL_LIB_CTX* custom = OSSL_LIB_CTX_new();
        if (!custom ||
            !OSSL_PROVIDER_add_builtin(custom, "klangdisabled", disabledFetchProviderInit))
            _exit(80);
        OSSL_PROVIDER* provider = OSSL_PROVIDER_load(custom, "klangdisabled");
        if (!provider) _exit(81);
        OSSL_LIB_CTX* previous = OSSL_LIB_CTX_set0_default(custom);
        if (!previous) _exit(82);
        disabledFetchOperation = OSSL_OP_MAC;
        mixedDisabledFetch = false;
        EVP_MAC* unavailable = EVP_MAC_fetch(custom, "HMAC", nullptr);
        if (unavailable) _exit(88);
        bool sawDisabled = false;
        unsigned long error;
        while ((error = ERR_get_error()) != 0) {
            if (!ERR_SYSTEM_ERROR(error) && ERR_GET_REASON(error) == ERR_R_DISABLED)
                sawDisabled = true;
        }
        if (!sawDisabled) _exit(89);
        for (int marked = 0; marked != 2; ++marked) {
            ERR_clear_error();
            unsigned long first = 0, last = 0;
            if (marked) {
                ERR_raise(ERR_LIB_USER, 98);
                first = ERR_peek_error();
                if (!first || !ERR_set_mark()) _exit(83);
                ERR_raise(ERR_LIB_USER, 99);
                last = ERR_peek_last_error();
            }
            disabledFetchQueries = 0;
            int status = 0;
            void* snapshot = discover(&status);
            if (snapshot) {
                release(snapshot);
                _exit(84);
            }
            if (status == 0 || disabledFetchQueries == 0) _exit(84);
            if (marked) {
                if (ERR_peek_error() != first || ERR_peek_last_error() != last ||
                    !ERR_pop_to_mark() || ERR_get_error() != first || ERR_get_error())
                    _exit(85);
            } else if (ERR_peek_error()) {
                _exit(86);
            }
        }
        if (OSSL_LIB_CTX_set0_default(previous) != custom) _exit(87);
        OSSL_PROVIDER_unload(provider);
        OSSL_LIB_CTX_free(custom);
        _exit(0);
    }
    CHECK(childResult(child) == 0);
}

TEST_CASE("HKDF capability snapshots own unique usable configurations",
          "[crypto][capabilities][hkdf]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        test() : int {
            try {
                snapshot: k::crypto::CryptoCapabilities! = k::crypto::CryptoCapabilities::snapshot();
                second: k::crypto::CryptoCapabilities! = k::crypto::CryptoCapabilities::snapshot();
                names: Sequence<String>! = snapshot.hkdfDigestAlgorithms();
                again: Sequence<String>! = snapshot.hkdfDigestAlgorithms();
                other: Sequence<String>! = second.hkdfDigestAlgorithms();
                configs: Sequence<k::crypto::HkdfConfiguration>! = snapshot.hkdfConfigurations();
                copies: Sequence<k::crypto::HkdfConfiguration>! = snapshot.hkdfConfigurations();
                fresh: Sequence<k::crypto::HkdfConfiguration>! = second.hkdfConfigurations();
                delete snapshot;
                delete second;
                if (!uniqueNames(*names) || !sameNames(*names, *again) ||
                    !sameNames(*names, *other) || !sameNames(*names, *names) ||
                    !sameConfigurations(*configs, *copies) ||
                    !sameConfigurations(*configs, *fresh) ||
                    !sameConfigurations(*configs, *configs)) return 1;
                if (containsName(*names, String("SHAKE-128")) ||
                    containsName(*names, String("SHAKE-256")) ||
                    containsName(*names, String("SHAKE128")) ||
                    containsName(*names, String("SHAKE256"))) return 2;
                if (usableSha256() && !containsName(*names, String("SHA-256"))) return 12;
                if (containsName(*names, String("SHA-256")) &&
                    (containsName(*names, String("SHA256")) ||
                     containsName(*names, String("SHA2-256")))) return 13;
                ikm: unsigned byte[32];
                salt: unsigned byte[16];
                info: unsigned byte[8];
                i: unsigned int = 0u;
                while (i < 32u) { ikm[i] = (unsigned byte)i; ++i; }
                i = 0u;
                while (i < 16u) { salt[i] = (unsigned byte)i; ++i; }
                i = 0u;
                while (i < 8u) { info[i] = (unsigned byte)i; ++i; }
                iter: ConstIterator<k::crypto::HkdfConfiguration>! = configs.constIterator();
                cur: OptionalConstRef<k::crypto::HkdfConfiguration> = iter.next();
                count: unsigned int = 0u;
                while (cur.hasValue()) {
                    c: const k::crypto::HkdfConfiguration& = cur.get();
                    if (!containsName(*names, c.algorithm()) || c.hashSize() == 0u ||
                        !(c.supportsExtract() || c.supportsExpand() || c.supportsDerive())) return 3;
                    hkdf: k::crypto::Hkdf! = k::crypto::Hkdf::create(c.algorithm());
                    if (!hkdf || hkdf.hashSize() != c.hashSize()) return 4;
                    if (c.supportsExtract()) {
                        prk: k::crypto::HkdfPrk! = hkdf.extract(ikm, salt);
                        if (!prk || prk.keySize() != c.hashSize()) return 5;
                    }
                    if (c.supportsExpand()) {
                        raw: unsigned byte[]! = new unsigned byte[c.hashSize()];
                        i = 0u;
                        while (i < c.hashSize()) { raw[i] = (unsigned byte)i; ++i; }
                        prk: k::crypto::HkdfPrk! = hkdf.importPrk(raw);
                        if (!prk) return 6;
                        output: unsigned byte[]! = prk.expand(info, c.hashSize());
                        if (output.size != c.hashSize()) return 7;
                    }
                    if (c.supportsDerive()) {
                        output: unsigned byte[]! = hkdf.derive(ikm, salt, info, c.hashSize());
                        if (output.size != c.hashSize()) return 8;
                    }
                    ++count;
                    cur = iter.next();
                }
                namesIt: ConstIterator<String>! = names.constIterator();
                name: OptionalConstRef<String> = namesIt.next();
                while (name.hasValue()) {
                    if (count == 0u) return 9;
                    --count;
                    name = namesIt.next();
                }
                return count == 0u ? 0 : 10;
            } catch (e: k::crypto::CryptoException&) {
                return 11;
            }
        }
    )K");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("HKDF capability snapshots survive provider restrictions",
          "[crypto][capabilities][hkdf][provider]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        saved: k::crypto::CryptoCapabilities! = null;
        names: Sequence<String>! = null;
        configs: Sequence<k::crypto::HkdfConfiguration>! = null;
        prepare() : int {
            try {
                saved = k::crypto::CryptoCapabilities::snapshot();
                names = saved.hkdfDigestAlgorithms();
                configs = saved.hkdfConfigurations();
                it: ConstIterator<String>! = names.constIterator();
                return it.next().hasValue() ? 0 : 77;
            } catch (e: k::crypto::CryptoException&) { return 1; }
        }
        verify() : int {
            try {
                old: Sequence<String>! = saved.hkdfDigestAlgorithms();
                oldConfigs: Sequence<k::crypto::HkdfConfiguration>! = saved.hkdfConfigurations();
                fresh: k::crypto::CryptoCapabilities! = k::crypto::CryptoCapabilities::snapshot();
                restricted: Sequence<String>! = fresh.hkdfDigestAlgorithms();
                restrictedConfigs: Sequence<k::crypto::HkdfConfiguration>! = fresh.hkdfConfigurations();
                ni: ConstIterator<String>! = restricted.constIterator();
                ci: ConstIterator<k::crypto::HkdfConfiguration>! = restrictedConfigs.constIterator();
                delete saved;
                if (!sameNames(*names, *old) || !sameConfigurations(*configs, *oldConfigs)) return 2;
                if (ni.next().hasValue() || ci.next().hasValue()) return 3;
                return 0;
            } catch (e: k::crypto::CryptoException&) { return 4; }
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
        if (!EVP_set_default_properties(custom, "provider=klangtestmissing")) _exit(81);
        OSSL_LIB_CTX* previous = OSSL_LIB_CTX_set0_default(custom);
        if (!previous) _exit(82);
        ERR_raise(ERR_LIB_USER, 7);
        const unsigned long error = ERR_peek_error();
        if (!ERR_set_mark()) _exit(83);
        ERR_raise(ERR_LIB_USER, 8);
        const unsigned long last = ERR_peek_last_error();
        if (verify() != 0 || ERR_peek_error() != error || ERR_peek_last_error() != last ||
            !ERR_pop_to_mark() || ERR_get_error() != error || ERR_get_error() != 0) _exit(84);
        if (OSSL_LIB_CTX_set0_default(previous) != custom) _exit(85);
        OSSL_PROVIDER_unload(provider);
        OSSL_LIB_CTX_free(custom);
        _exit(0);
    }
    const int result = childResult(child);
    if (result == 77) SKIP("No HKDF digest is available under the initial provider selection");
    CHECK(result == 0);
}

TEST_CASE("HKDF discovery probes modes separately and propagates backend failures",
          "[crypto][capabilities][hkdf][provider]") {
    auto jit = gen_jit_with_crypto(std::string(prelude) + R"K(
        probe(expected: int) : int {
            try {
                snapshot: k::crypto::CryptoCapabilities! = k::crypto::CryptoCapabilities::snapshot();
                names: Sequence<String>! = snapshot.hkdfDigestAlgorithms();
                hmac: Sequence<String>! = snapshot.hmacDigestAlgorithms();
                configs: Sequence<k::crypto::HkdfConfiguration>! = snapshot.hkdfConfigurations();
                if (!containsName(*hmac, String("SHA-256"))) return 17;
                if (!uniqueNames(*names)) return 10;
                it: ConstIterator<k::crypto::HkdfConfiguration>! = configs.constIterator();
                cur: OptionalConstRef<k::crypto::HkdfConfiguration> = it.next();
                if (expected == 0 && cur.hasValue()) return 18;
                found: bool = false;
                while (cur.hasValue()) {
                    c: const k::crypto::HkdfConfiguration& = cur.get();
                    if (!containsName(*names, c.algorithm()) || c.hashSize() == 0u) return 11;
                    if (c.supportsExtract() != (expected == 1) ||
                        c.supportsExpand() != (expected == 2) ||
                        c.supportsDerive() != (expected == 3)) return 12;
                    if (c.algorithm() == String("SHA-256")) {
                        if (c.hashSize() != 32u) return 13;
                        found = true;
                    }
                    cur = it.next();
                }
                if (expected == 0) {
                    if (found || containsName(*names, String("SHA-256"))) return 14;
                } else if (!found) return 15;
                return 0;
            } catch (e: k::crypto::CryptoOperationException&) {
                return 1;
            } catch (e: k::crypto::CryptoException&) {
                return 16;
            }
        }
    )K");
    REQUIRE(jit);
    auto probe = jit->lookup_symbol<int(*)(int)>("probe");
    REQUIRE(probe);
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        ERR_clear_error();
        OSSL_LIB_CTX* custom = OSSL_LIB_CTX_new();
        if (!custom) _exit(80);
        OSSL_PROVIDER* standard = OSSL_PROVIDER_load(custom, "default");
        if (!standard) _exit(77);
        if (!OSSL_PROVIDER_add_builtin(custom, "klanghkdfmock", mockProviderInit)) _exit(81);
        OSSL_PROVIDER* mock = OSSL_PROVIDER_load(custom, "klanghkdfmock");
        if (!mock || !EVP_set_default_properties(custom, "?provider=klanghkdfmock")) _exit(82);
        EVP_MD* digest = EVP_MD_fetch(custom, "SHA-256", nullptr);
        if (!digest) _exit(77);
        EVP_MD_free(digest);
        EVP_KDF* kdf = EVP_KDF_fetch(custom, "HKDF", nullptr);
        if (!kdf || EVP_KDF_get0_provider(kdf) != mock) _exit(83);
        EVP_KDF_free(kdf);
        OSSL_LIB_CTX* previous = OSSL_LIB_CTX_set0_default(custom);
        if (!previous) _exit(84);
        struct Case { MockBehavior mode; int argument; int expected; };
        const Case cases[] = {
            {MockBehavior::extract, 1, 0}, {MockBehavior::expand, 2, 0},
            {MockBehavior::derive, 3, 0}, {MockBehavior::reject, 0, 0},
            {MockBehavior::setReject, 0, 0},
            {MockBehavior::unknown, 0, 1}, {MockBehavior::silent, 0, 1},
            {MockBehavior::mixed, 0, 1}, {MockBehavior::successError, 0, 1},
            {MockBehavior::noMemory, 0, 1}, {MockBehavior::setUnknown, 0, 1},
            {MockBehavior::setSilent, 0, 1}
        };
        for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            behavior = cases[i].mode;
            ERR_clear_error();
            if (probe(cases[i].argument) != cases[i].expected || ERR_peek_error()) _exit(10 + i);
            ERR_raise(ERR_LIB_USER, 99);
            const unsigned long error = ERR_peek_error();
            if (!error || probe(cases[i].argument) != cases[i].expected ||
                ERR_get_error() != error || ERR_get_error()) _exit(40 + i);
        }
        if (OSSL_LIB_CTX_set0_default(previous) != custom) _exit(85);
        OSSL_PROVIDER_unload(mock);
        OSSL_PROVIDER_unload(standard);
        OSSL_LIB_CTX_free(custom);
        _exit(0);
    }
    const int result = childResult(child);
    if (result == 77) SKIP("The default provider or SHA-256 is unavailable");
    CHECK(result == 0);
}

TEST_CASE("HKDF operations reject provider success with an error",
          "[crypto][hkdf][provider]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __hkdf_provider_success_error_test__;
        import k::crypto;

        probe(mode: int) : int {
            hkdf: k::crypto::Hkdf! = null;
            prk: k::crypto::HkdfPrk! = null;
            ikm: unsigned byte[32];
            salt: unsigned byte[16];
            info: unsigned byte[8];
            i: unsigned int = 0u;
            while (i < 32u) { ikm[i] = (unsigned byte)i; ++i; }
            i = 0u;
            while (i < 16u) { salt[i] = (unsigned byte)i; ++i; }
            i = 0u;
            while (i < 8u) { info[i] = (unsigned byte)i; ++i; }
            try {
                hkdf = k::crypto::Hkdf::SHA256();
                if (!hkdf) return 2;
                if (mode == 2) {
                    prk = hkdf.importPrk(ikm);
                    if (!prk) return 6;
                }
            } catch (e: k::crypto::CryptoException&) {
                return 5;
            }
            try {
                if (mode == 1) {
                    extracted: k::crypto::HkdfPrk! = hkdf.extract(ikm, salt);
                } else if (mode == 2) {
                    output: unsigned byte[]! = prk.expand(info, 32u);
                } else {
                    output: unsigned byte[]! = hkdf.derive(ikm, salt, info, 32u);
                }
                return 3;
            } catch (e: k::crypto::CryptoOperationException&) {
                return 0;
            } catch (e: k::crypto::CryptoException&) {
                return 4;
            }
        }
    )K");
    REQUIRE(jit);
    auto probe = jit->lookup_symbol<int(*)(int)>("probe");
    REQUIRE(probe);
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        ERR_clear_error();
        OSSL_LIB_CTX* custom = OSSL_LIB_CTX_new();
        if (!custom) _exit(80);
        OSSL_PROVIDER* standard = OSSL_PROVIDER_load(custom, "default");
        if (!standard) _exit(77);
        if (!OSSL_PROVIDER_add_builtin(custom, "klanghkdfmock", mockProviderInit)) _exit(81);
        OSSL_PROVIDER* mock = OSSL_PROVIDER_load(custom, "klanghkdfmock");
        if (!mock || !EVP_set_default_properties(custom, "?provider=klanghkdfmock")) _exit(82);
        EVP_MD* digest = EVP_MD_fetch(custom, "SHA-256", nullptr);
        if (!digest) _exit(77);
        EVP_MD_free(digest);
        EVP_KDF* kdf = EVP_KDF_fetch(custom, "HKDF", nullptr);
        if (!kdf || EVP_KDF_get0_provider(kdf) != mock) _exit(83);
        EVP_KDF_free(kdf);
        OSSL_LIB_CTX* previous = OSSL_LIB_CTX_set0_default(custom);
        if (!previous) _exit(84);
        behavior = MockBehavior::successError;
        for (int mode = 1; mode <= 3; ++mode) {
            ERR_clear_error();
            if (probe(mode) != 0 || ERR_peek_error()) _exit(10 + mode);
            ERR_raise(ERR_LIB_USER, 98);
            const unsigned long first = ERR_peek_error();
            if (!first || !ERR_set_mark()) _exit(20 + mode);
            ERR_raise(ERR_LIB_USER, 99);
            const unsigned long last = ERR_peek_last_error();
            if (probe(mode) != 0 || ERR_peek_error() != first ||
                ERR_peek_last_error() != last || !ERR_pop_to_mark() ||
                ERR_get_error() != first || ERR_get_error()) _exit(30 + mode);
        }
        if (OSSL_LIB_CTX_set0_default(previous) != custom) _exit(85);
        OSSL_PROVIDER_unload(mock);
        OSSL_PROVIDER_unload(standard);
        OSSL_LIB_CTX_free(custom);
        _exit(0);
    }
    const int result = childResult(child);
    if (result == 77) SKIP("The default provider or SHA-256 is unavailable");
    CHECK(result == 0);
}
