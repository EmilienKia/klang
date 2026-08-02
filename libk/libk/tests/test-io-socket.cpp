/*
 * K Language standard library — socket tests (Phase 5)
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

TEST_CASE("Socket: loopback connect/send/recv round-trip", "[libk][io][socket]") {
    auto jit = jit_k(R"SRC(
        module __socket_roundtrip__;

        class EchoServer : public k::Runnable {
            _server : k::io::ServerSocket*;
            _ok     : int;
        public:
            EchoServer(server: k::io::ServerSocket*) : _server(server), _ok(0) {}
            override run() : void {
                try {
                    c : k::io::SocketChannel! = _server->accept(2000000000L);
                    b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(8u);
                    n : int = c->read(b, 2000000000L);
                    if (n == 4) {
                        b->flip();
                        c->writeFully(b, k::io::IO_NO_TIMEOUT);
                        _ok = 1;
                    }
                    c->close();
                    delete c;
                    delete b;
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

            worker : EchoServer! = new EchoServer(srv);
            t : k::Thread! = new k::Thread(worker);
            t->start();

            peerAddr : k::io::NetworkAddress! = k::io::NetworkAddress::loopback(port);
            c : k::io::SocketChannel! = k::io::SocketChannel::connect(peerAddr, 2000000000L);

            out : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(4u);
            out->put((byte) 80);
            out->put((byte) 73);
            out->put((byte) 78);
            out->put((byte) 71);
            out->flip();
            c->writeFully(out, 2000000000L);

            in : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(4u);
            c->read(in, 2000000000L);
            in->flip();
            ok : int = 0;
            if (in->remaining() == 4u) { ok = ok + 1; }
            if (in->get(0u) == (byte) 80) { ok = ok + 2; }
            if (in->get(3u) == (byte) 71) { ok = ok + 4; }

            c->close();
            t->join();
            if (worker->ok() == 1) { ok = ok + 8; }

            srv->close();
            delete in; delete out; delete c;
            delete t; delete worker; delete srv; delete peerAddr; delete addr;
            return ok;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 15);
}

TEST_CASE("ServerSocket: accept timeout raises TimeoutException", "[libk][io][socket]") {
    auto jit = jit_k(R"SRC(
        module __socket_accept_timeout__;

        test() : int {
            addr : k::io::NetworkAddress! = k::io::NetworkAddress::any(0);
            srv : k::io::ServerSocket! = k::io::ServerSocket::bind(addr);
            res : int = 0;
            try {
                c : k::io::SocketChannel! = srv->accept(50000000L);
                delete c;
            } catch (e: k::TimeoutException&) {
                res = 1;
            }
            srv->close();
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

TEST_CASE("ServerSocket: accept is interruptible", "[libk][io][socket][interruption]") {
    auto jit = jit_k(R"SRC(
        module __socket_accept_interrupt__;

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
                } catch (e: k::ThreadInterruptionException&) {
                    _res = 1;
                }
            }
            const res() : int { return _res; }
        }

        test() : int {
            addr : k::io::NetworkAddress! = k::io::NetworkAddress::any(0);
            srv : k::io::ServerSocket! = k::io::ServerSocket::bind(addr);
            a : Acceptor! = new Acceptor(srv);
            t : k::Thread! = new k::Thread(a);
            t->start();
            k::Thread::sleep(k::Duration::ofMillis(60L));
            t->interrupt();
            t->join();
            r : int = a->res();
            srv->close();
            delete t;
            delete a;
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

