/*
 * K Language standard library — server socket tests (Phase 5)
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

TEST_CASE("ServerSocket: localPort is available after bind", "[libk][io][server-socket]") {
    auto jit = jit_k(R"SRC(
        module __server_socket_port__;

        test() : int {
            addr : k::io::NetworkAddress! = k::io::NetworkAddress::any(0);
            srv : k::io::ServerSocket! = k::io::ServerSocket::bind(addr);
            p : int = srv->localPort();
            res : int = 0;
            if (p > 0) { res = res + 1; }
            if (srv->isOpen()) { res = res + 2; }
            srv->close();
            if (!srv->isOpen()) { res = res + 4; }
            // Idempotent close
            srv->close();
            res = res + 8;
            delete srv;
            delete addr;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 15);
}

TEST_CASE("ServerSocket: connect then accept succeeds", "[libk][io][server-socket]") {
    auto jit = jit_k(R"SRC(
        module __server_socket_accept_ok__;

        class Acceptor : public k::Runnable {
            _srv : k::io::ServerSocket*;
            _ok  : int;
        public:
            Acceptor(srv: k::io::ServerSocket*) : _srv(srv), _ok(0) {}
            override run() : void {
                try {
                    c : k::io::SocketChannel! = _srv->accept(2000000000L);
                    if (c->isOpen()) { _ok = 1; }
                    c->close();
                    delete c;
                } catch (e: Exception&) {
                    _ok = 0;
                }
            }
            const ok() : int { return _ok; }
        }

        test() : int {
            addr : k::io::NetworkAddress! = k::io::NetworkAddress::any(0);
            srv : k::io::ServerSocket! = k::io::ServerSocket::bind(addr);
            port : int = srv->localPort();

            a : Acceptor! = new Acceptor(srv);
            t : k::Thread! = new k::Thread(a);
            t->start();

            peer : k::io::NetworkAddress! = k::io::NetworkAddress::loopback(port);
            cli : k::io::SocketChannel! = k::io::SocketChannel::connect(peer, 2000000000L);
            cli->close();
            delete cli;
            delete peer;

            t->join();
            res : int = a->ok();
            srv->close();
            delete t;
            delete a;
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

TEST_CASE("ServerSocket: close from another thread aborts blocking accept",
          "[libk][io][server-socket][interrupt]") {
    auto jit = jit_k(R"SRC(
        module __server_socket_close_abort_accept__;

        class Acceptor : public k::Runnable {
            _srv : k::io::ServerSocket*;
            _res : int;
        public:
            Acceptor(srv: k::io::ServerSocket*) : _srv(srv), _res(0) {}
            override run() : void {
                try {
                    c : k::io::SocketChannel! = _srv->accept();
                    c->close();
                    delete c;
                    _res = 0;
                } catch (e: k::io::ClosedChannelException&) {
                    _res = 1;
                } catch (e2: Exception&) {
                    _res = 2;
                }
            }
            const res() : int { return _res; }
        }

        class Closer : public k::Runnable {
            _srv : k::io::ServerSocket*;
        public:
            Closer(srv: k::io::ServerSocket*) : _srv(srv) {}
            override run() : void {
                k::Thread::sleep(k::Duration::ofMillis(60L));
                _srv->close();
            }
        }

        test() : int {
            addr : k::io::NetworkAddress! = k::io::NetworkAddress::any(0);
            srv : k::io::ServerSocket! = k::io::ServerSocket::bind(addr);

            acc : Acceptor! = new Acceptor(srv);
            closeTask : Closer! = new Closer(srv);
            tAcc : k::Thread! = new k::Thread(acc);
            tClose : k::Thread! = new k::Thread(closeTask);
            tAcc->start();
            tClose->start();
            tAcc->join();
            tClose->join();

            r : int = acc->res();
            delete tClose;
            delete tAcc;
            delete closeTask;
            delete acc;
            delete srv;
            delete addr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}
