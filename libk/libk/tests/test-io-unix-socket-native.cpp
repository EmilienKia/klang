/*
 * K Language standard library — Unix socket native tests (Phase 5+)
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

#include <dlfcn.h>
#include <filesystem>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR not defined — set via CMake target_compile_definitions"
#endif

namespace {

using unix_connect_fn = long long (*)(const uint32_t*, long long);
using unix_bind_listen_fn = int (*)(const uint32_t*, int, int);
using unix_accept_fn = long long (*)(int, long long);
using unix_dgram_bind_fn = int (*)(const uint32_t*, int);
using unix_dgram_connect_fn = long long (*)(int, const uint32_t*, long long);
using unix_dgram_sendto_fn = long long (*)(int, const uint32_t*, const void*, int, long long);
using net_send_fn = long long (*)(int, const void*, int, long long);
using net_recv_fn = long long (*)(int, void*, int, long long);
using net_close_fn = int (*)(int);

struct unix_fns {
    unix_connect_fn connect = nullptr;
    unix_bind_listen_fn bind_listen = nullptr;
    unix_accept_fn accept = nullptr;
    unix_dgram_bind_fn dgram_bind = nullptr;
    unix_dgram_connect_fn dgram_connect = nullptr;
    unix_dgram_sendto_fn dgram_sendto = nullptr;
    net_send_fn send = nullptr;
    net_recv_fn recv = nullptr;
    net_close_fn close = nullptr;
};

std::vector<uint32_t> utf32(std::string_view s) {
    std::vector<uint32_t> out;
    out.reserve(s.size() + 1);
    for (unsigned char c : s) {
        out.push_back(static_cast<uint32_t>(c));
    }
    out.push_back(0);
    return out;
}

template <typename T>
T load_symbol(void* handle, const char* name) {
    auto* sym = dlsym(handle, name);
    REQUIRE(sym != nullptr);
    return reinterpret_cast<T>(sym);
}

unix_fns load_libk() {
    const std::string path = std::string(LIBK_LIB_DIR) + "/libk.so";
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    REQUIRE(handle != nullptr);

    unix_fns fns;
    fns.connect = load_symbol<unix_connect_fn>(handle, "__k_net_unix_connect");
    fns.bind_listen = load_symbol<unix_bind_listen_fn>(handle, "__k_net_unix_bind_listen");
    fns.accept = load_symbol<unix_accept_fn>(handle, "__k_net_unix_accept");
    fns.dgram_bind = load_symbol<unix_dgram_bind_fn>(handle, "__k_net_unix_dgram_bind");
    fns.dgram_connect = load_symbol<unix_dgram_connect_fn>(handle, "__k_net_unix_dgram_connect");
    fns.dgram_sendto = load_symbol<unix_dgram_sendto_fn>(handle, "__k_net_unix_dgram_sendto");
    fns.send = load_symbol<net_send_fn>(handle, "__k_net_send");
    fns.recv = load_symbol<net_recv_fn>(handle, "__k_net_recv");
    fns.close = load_symbol<net_close_fn>(handle, "__k_net_close");
    return fns;
}

} // anonymous namespace

TEST_CASE("Unix socket backend: stream round-trip", "[libk][io][socket][unix]") {
    auto fns = load_libk();
    const std::string path = "/tmp/klang_unix_stream.sock";
    std::filesystem::remove(path);
    auto upath = utf32(path);

    int server_fd = fns.bind_listen(upath.data(), 64, 1);
    REQUIRE(server_fd >= 0);

    std::atomic<int> server_ok{0};
    std::atomic<bool> client_done{false};
    std::thread server([&] {
        int client_fd = static_cast<int>(fns.accept(server_fd, 2'000'000'000LL));
        if (client_fd < 0) {
            server_ok = -1;
            return;
        }
        char buf[4] = {};
        if (fns.recv(client_fd, buf, 4, 2'000'000'000LL) == 4 &&
            fns.send(client_fd, buf, 4, 2'000'000'000LL) == 4 &&
            [&] {
                while (!client_done.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                return fns.close(client_fd) == 0;
            }()) {
            server_ok = 1;
        } else {
            server_ok = -1;
        }
    });

    int client_fd = static_cast<int>(fns.connect(upath.data(), 2'000'000'000LL));
    REQUIRE(client_fd >= 0);

    const char payload[4] = {'U', 'N', 'I', 'X'};
    REQUIRE(fns.send(client_fd, payload, 4, 2'000'000'000LL) == 4);
    char reply[4] = {};
    REQUIRE(fns.recv(client_fd, reply, 4, 2'000'000'000LL) == 4);
    REQUIRE(std::memcmp(reply, payload, 4) == 0);
    client_done = true;
    REQUIRE(fns.close(client_fd) == 0);
    REQUIRE(fns.close(server_fd) == 0);

    server.join();
    REQUIRE(server_ok == 1);
    std::filesystem::remove(path);
}

TEST_CASE("Unix socket backend: datagram sendTo/connect", "[libk][io][socket][unix][udp]") {
    auto fns = load_libk();
    const std::string recv_path = "/tmp/klang_unix_dgram_recv.sock";
    const std::string send_path = "/tmp/klang_unix_dgram_send.sock";
    std::filesystem::remove(recv_path);
    std::filesystem::remove(send_path);

    auto recv_u32 = utf32(recv_path);
    auto send_u32 = utf32(send_path);

    int recv_fd = fns.dgram_bind(recv_u32.data(), 1);
    int send_fd = fns.dgram_bind(send_u32.data(), 1);
    REQUIRE(recv_fd >= 0);
    REQUIRE(send_fd >= 0);

    const char payload[4] = {'D', 'G', 'R', 'M'};
    REQUIRE(fns.dgram_sendto(send_fd, recv_u32.data(), payload, 4, 2'000'000'000LL) == 4);
    char buf[4] = {};
    REQUIRE(fns.recv(recv_fd, buf, 4, 2'000'000'000LL) == 4);
    REQUIRE(std::memcmp(buf, payload, 4) == 0);

    REQUIRE(fns.dgram_connect(send_fd, recv_u32.data(), 2'000'000'000LL) == 0);
    const char payload2[4] = {'C', 'O', 'N', 'N'};
    REQUIRE(fns.send(send_fd, payload2, 4, 2'000'000'000LL) == 4);
    char buf2[4] = {};
    REQUIRE(fns.recv(recv_fd, buf2, 4, 2'000'000'000LL) == 4);
    REQUIRE(std::memcmp(buf2, payload2, 4) == 0);

    REQUIRE(fns.close(send_fd) == 0);
    REQUIRE(fns.close(recv_fd) == 0);
    std::filesystem::remove(send_path);
    std::filesystem::remove(recv_path);
}
