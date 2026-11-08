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

using CapabilityDiscover = void*(*)(int*);
using CapabilityRelease = void(*)(void*);

struct CapabilityCancelWork {
    OSSL_LIB_CTX* context;
    OSSL_LIB_CTX* previous = nullptr;
    CapabilityDiscover discover;
    CapabilityRelease release;
    bool marked;
    int queryOperation = OSSL_OP_DIGEST;
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
    if (cancelWork && operation == cancelWork->queryOperation) {
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

} // namespace

TEST_CASE("Capability discovery defers cancellation until its provider work completes",
          "[crypto][capabilities][provider][threading]") {
    auto jit = gen_jit_with_crypto(R"K(
        module __capability_cancellation_test__;
        import k::crypto;
        test() : int { return 0; }
    )K");
    REQUIRE(jit);
    CapabilityRelease release[] = {
        reinterpret_cast<CapabilityRelease>(dlsym(RTLD_DEFAULT, "__k_cap_digest_free")),
        reinterpret_cast<CapabilityRelease>(dlsym(RTLD_DEFAULT, "__k_cap_digest_free")),
        reinterpret_cast<CapabilityRelease>(dlsym(RTLD_DEFAULT, "__k_cap_symmetric_free"))
    };
    CapabilityDiscover discover[] = {
        reinterpret_cast<CapabilityDiscover>(dlsym(RTLD_DEFAULT, "__k_cap_digest_new")),
        reinterpret_cast<CapabilityDiscover>(dlsym(RTLD_DEFAULT, "__k_cap_hmac_digest_new")),
        reinterpret_cast<CapabilityDiscover>(dlsym(RTLD_DEFAULT, "__k_cap_symmetric_new"))
    };
    for (auto function : release) REQUIRE(function);
    for (auto function : discover) REQUIRE(function);
    for (int scenario = 0; scenario < 12; ++scenario) {
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
            const int kind = scenario % 3;
            work.discover = discover[kind];
            work.release = release[kind];
            work.queryOperation = kind == 2 ? OSSL_OP_CIPHER : OSSL_OP_DIGEST;
            work.marked = (scenario / 3) % 2 == 0;
            work.startDisabled = scenario >= 6;
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
