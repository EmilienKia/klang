/*
 * K Language standard library — socket interruption/timeout tests (Phase 5)
 *
 * Copyright 2023-2026 Emilien Kia
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

#include "../../klang/tests/helpers.hpp"

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR not defined — set via CMake target_compile_definitions"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR not defined — set via CMake target_compile_definitions"
#endif

namespace {

std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

} // anonymous namespace

TEST_CASE("SocketChannel: read timeout is reported", "[libk][io][socket][interrupt]") {
    auto jit = jit_k(R"SRC(
        module __socket_read_timeout__;

        class Holder : public k::Runnable {
            _srv : k::io::ServerSocket*;
        public:
            Holder(srv: k::io::ServerSocket*) : _srv(srv) {}
            override run() : void {
                c : k::io::SocketChannel! = _srv->accept(2000000000L);
                // Keep the connection open but send nothing.
                k::Thread::sleep(k::Duration::ofMillis(200L));
                c->close();
                delete c;
            }
        }

        test() : int {
            addr : k::io::NetworkAddress! = k::io::NetworkAddress::any(0);
            srv : k::io::ServerSocket! = k::io::ServerSocket::bind(addr);
            port : int = srv->localPort();

            holder : Holder! = new Holder(srv);
            t : k::Thread! = new k::Thread(holder);
            t->start();

            peer : k::io::NetworkAddress! = k::io::NetworkAddress::loopback(port);
            cli : k::io::SocketChannel! = k::io::SocketChannel::connect(peer, 2000000000L);
            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(8u);
            res : int = 0;
            try {
                cli->read(b, 50000000L);
            } catch (e: k::TimeoutException&) {
                res = 1;
            }

            t->join();
            cli->close();
            srv->close();
            delete b;
            delete cli;
            delete peer;
            delete t;
            delete holder;
            delete srv;
            delete addr;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("SocketChannel: connect to closed localhost port fails cleanly",
          "[libk][io][socket][interrupt]") {
    auto jit = jit_k(R"SRC(
        module __socket_connect_fail__;

        test() : int {
            target : k::io::NetworkAddress! = k::io::NetworkAddress::loopback(9);
            res : int = 0;
            try {
                c : k::io::SocketChannel! = k::io::SocketChannel::connect(target, 100000000L);
                c->close();
                delete c;
                res = -1;
            } catch (e: k::io::IOException&) {
                res = 1;
            } catch (e2: k::TimeoutException&) {
                // Platform/network dependent path; still an expected failure mode.
                res = 1;
            }
            delete target;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

