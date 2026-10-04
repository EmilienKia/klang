/*
 * Deterministic native proactor regressions: no kernel availability dependency.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
#include <catch2/catch_all.hpp>
#include <dlfcn.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <thread>
#include <algorithm>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <filesystem>
#include <fstream>
#include <cstring>

namespace {
template<class T> T symbol(void* lib, const char* name) {
    auto ptr = dlsym(lib, name);
    REQUIRE(ptr != nullptr);
    return reinterpret_cast<T>(ptr);
}
}

TEST_CASE("Future native: concurrent cancellation cannot bypass an extracted producer hook",
          "[libk][future][proactor][mock][race]") {
    struct { void* library; } rt{
        dlopen((std::string(LIBK_LIB_DIR) + "/libk.so").c_str(), RTLD_NOW | RTLD_GLOBAL)};
    REQUIRE(rt.library);
    auto create = symbol<void* (*)()>(rt.library, "__k_future_create");
    auto destroy = symbol<void (*)(void*)>(rt.library, "__k_future_destroy");
    auto set = symbol<void (*)(void*, void*, bool (*)(void*), void (*)(void*))>(
        rt.library, "__k_future_set_cancel_hook");
    auto cancel = symbol<bool (*)(void*)>(rt.library, "__k_future_cancel");
    auto complete = symbol<bool (*)(void*, int)>(rt.library, "__k_future_complete");
    auto state = symbol<int (*)(const void*)>(rt.library, "__k_future_state");
    struct Hook {
        std::mutex mutex;
        std::condition_variable changed;
        bool entered = false, proceed = false;
        std::atomic<int> drops{0};
        static bool cancel(void* ctx) {
            auto& h = *static_cast<Hook*>(ctx);
            std::unique_lock lock(h.mutex);
            h.entered = true;
            h.changed.notify_all();
            h.changed.wait(lock, [&] { return h.proceed; });
            return true;
        }
        static void drop(void* ctx) { ++static_cast<Hook*>(ctx)->drops; }
    } hook;
    void* future = create();
    set(future, &hook, Hook::cancel, Hook::drop);
    bool first = true;
    std::thread producer([&] { first = cancel(future); });
    {
        std::unique_lock lock(hook.mutex);
        hook.changed.wait(lock, [&] { return hook.entered; });
    }
    bool second = cancel(future);
    bool publication = complete(future, 1);
    {
        std::lock_guard lock(hook.mutex);
        hook.proceed = true;
        hook.changed.notify_all();
    }
    producer.join();
    REQUIRE_FALSE(second);
    REQUIRE(publication);
    REQUIRE_FALSE(first);
    REQUIRE(state(future) == 1);
    REQUIRE(hook.drops == 1);
    destroy(future);
}

