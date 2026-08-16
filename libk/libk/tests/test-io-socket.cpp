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
            if (in->remaining() == 4u) { ++ok; }
            if (in->get(0u) == (byte) 80) { ok += 2; }
            if (in->get(3u) == (byte) 71) { ok += 4; }

            c->close();
            t->join();
            if (worker->ok() == 1) { ok += 8; }

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

TEST_CASE("SocketChannel: peer close is reported as EOF", "[libk][io][socket]") {
    auto jit = jit_k(R"SRC(
        module __socket_eof__;

        class OneShotCloser : public k::Runnable {
            _srv : k::io::ServerSocket*;
        public:
            OneShotCloser(srv: k::io::ServerSocket*) : _srv(srv) {}
            override run() : void {
                c : k::io::SocketChannel! = _srv->accept(2000000000L);
                c->close();
                delete c;
            }
        }

        test() : int {
            addr : k::io::NetworkAddress! = k::io::NetworkAddress::any(0);
            srv : k::io::ServerSocket! = k::io::ServerSocket::bind(addr);
            port : int = srv->localPort();

            worker : OneShotCloser! = new OneShotCloser(srv);
            t : k::Thread! = new k::Thread(worker);
            t->start();

            peerAddr : k::io::NetworkAddress! = k::io::NetworkAddress::loopback(port);
            cli : k::io::SocketChannel! = k::io::SocketChannel::connect(peerAddr, 2000000000L);
            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(8u);
            n : int = cli->read(b, 2000000000L);

            t->join();
            cli->close();
            srv->close();

            delete b;
            delete cli;
            delete peerAddr;
            delete t;
            delete worker;
            delete srv;
            delete addr;
            return n == 0 ? 1 : 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("SocketChannel: blocking read is interruptible", "[libk][io][socket][interruption]") {
    auto jit = jit_k(R"SRC(
        module __socket_read_interrupt__;

        class IdleServer : public k::Runnable {
            _srv : k::io::ServerSocket*;
        public:
            IdleServer(srv: k::io::ServerSocket*) : _srv(srv) {}
            override run() : void {
                c : k::io::SocketChannel! = _srv->accept(2000000000L);
                k::Thread::sleep(k::Duration::ofMillis(300L));
                c->close();
                delete c;
            }
        }

        class Reader : public k::Runnable {
            _sock : k::io::SocketChannel*;
            _res  : int;
        public:
            Reader(sock: k::io::SocketChannel*) : _sock(sock), _res(0) {}
            override run() : void {
                b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(16u);
                try {
                    _sock->read(b);
                    _res = 0;
                } catch (e: k::ThreadInterruptionException&) {
                    _res = 1;
                } catch (e2: k::io::ClosedChannelException&) {
                    _res = 2;
                }
                delete b;
            }
            const res() : int { return _res; }
        }

        test() : int {
            addr : k::io::NetworkAddress! = k::io::NetworkAddress::any(0);
            srv : k::io::ServerSocket! = k::io::ServerSocket::bind(addr);
            port : int = srv->localPort();

            idle : IdleServer! = new IdleServer(srv);
            tSrv : k::Thread! = new k::Thread(idle);
            tSrv->start();

            peerAddr : k::io::NetworkAddress! = k::io::NetworkAddress::loopback(port);
            cli : k::io::SocketChannel! = k::io::SocketChannel::connect(peerAddr, 2000000000L);

            r : Reader! = new Reader(cli);
            tRead : k::Thread! = new k::Thread(r);
            tRead->start();
            k::Thread::sleep(k::Duration::ofMillis(60L));
            tRead->interrupt();
            tRead->join();
            tSrv->join();

            res : int = r->res();

            cli->close();
            srv->close();
            delete tRead; delete r; delete cli; delete peerAddr;
            delete tSrv; delete idle; delete srv; delete addr;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("SocketChannel: close from another thread aborts blocked read",
          "[libk][io][socket][interruption]") {
    auto jit = jit_k(R"SRC(
        module __socket_close_during_read__;

        class IdleServer : public k::Runnable {
            _srv : k::io::ServerSocket*;
        public:
            IdleServer(srv: k::io::ServerSocket*) : _srv(srv) {}
            override run() : void {
                c : k::io::SocketChannel! = _srv->accept(2000000000L);
                k::Thread::sleep(k::Duration::ofMillis(300L));
                c->close();
                delete c;
            }
        }

        class Reader : public k::Runnable {
            _sock : k::io::SocketChannel*;
            _res  : int;
        public:
            Reader(sock: k::io::SocketChannel*) : _sock(sock), _res(0) {}
            override run() : void {
                b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(16u);
                try {
                    _sock->read(b);
                    _res = 0;
                } catch (e: k::io::ClosedChannelException&) {
                    _res = 1;
                } catch (e2: Exception&) {
                    _res = 2;
                }
                delete b;
            }
            const res() : int { return _res; }
        }

        class Closer : public k::Runnable {
            _sock : k::io::SocketChannel*;
        public:
            Closer(sock: k::io::SocketChannel*) : _sock(sock) {}
            override run() : void {
                k::Thread::sleep(k::Duration::ofMillis(60L));
                _sock->close();
            }
        }

        test() : int {
            addr : k::io::NetworkAddress! = k::io::NetworkAddress::any(0);
            srv : k::io::ServerSocket! = k::io::ServerSocket::bind(addr);
            port : int = srv->localPort();

            idle : IdleServer! = new IdleServer(srv);
            tSrv : k::Thread! = new k::Thread(idle);
            tSrv->start();

            peerAddr : k::io::NetworkAddress! = k::io::NetworkAddress::loopback(port);
            cli : k::io::SocketChannel! = k::io::SocketChannel::connect(peerAddr, 2000000000L);

            reader : Reader! = new Reader(cli);
            closer : Closer! = new Closer(cli);
            tRead : k::Thread! = new k::Thread(reader);
            tClose : k::Thread! = new k::Thread(closer);
            tRead->start();
            tClose->start();
            tRead->join();
            tClose->join();
            tSrv->join();

            res : int = reader->res();
            srv->close();

            delete tClose; delete closer;
            delete tRead; delete reader;
            delete cli; delete peerAddr;
            delete tSrv; delete idle;
            delete srv; delete addr;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("Socket: resolves localhost hostnames", "[libk][io][socket]") {
    auto jit = jit_k(R"SRC(
        module __socket_localhost_name__;

        class EchoServer : public k::Runnable {
            _server : k::io::ServerSocket*;
            _ok     : int;
        public:
            EchoServer(server: k::io::ServerSocket*) : _server(server), _ok(0) {}
            override run() : void {
                c : k::io::SocketChannel! = _server->accept(2000000000L);
                b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(1u);
                if (c->read(b, 2000000000L) == 1) {
                    b->flip();
                    c->writeFully(b, 2000000000L);
                    _ok = 1;
                }
                c->close();
                delete c;
                delete b;
            }
            const ok() : int { return _ok; }
        }

        test() : int {
            anyAddr : k::io::NetworkAddress! = k::io::NetworkAddress::any(0);
            srv : k::io::ServerSocket! = k::io::ServerSocket::bind(anyAddr);
            port : int = srv->localPort();

            worker : EchoServer! = new EchoServer(srv);
            t : k::Thread! = new k::Thread(worker);
            t->start();

            named : k::io::NetworkAddress! = k::io::NetworkAddress::of("localhost", port);
            cli : k::io::SocketChannel! = k::io::SocketChannel::connect(named, 2000000000L);

            out : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(1u);
            out->put((byte) 90);
            out->flip();
            cli->writeFully(out, 2000000000L);

            in : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(1u);
            cli->read(in, 2000000000L);
            in->flip();

            res : int = 0;
            if (in->remaining() == 1u) { ++res; }
            if (in->get(0u) == (byte) 90) { res += 2; }

            t->join();
            if (worker->ok() == 1) { res += 4; }

            cli->close();
            srv->close();
            delete in; delete out; delete cli; delete named;
            delete t; delete worker; delete srv; delete anyAddr;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 7);
}

TEST_CASE("SocketChannel: writing after close raises ClosedChannelException",
          "[libk][io][socket]") {
    auto jit = jit_k(R"SRC(
        module __socket_write_after_close__;

        class SinkServer : public k::Runnable {
            _server : k::io::ServerSocket*;
        public:
            SinkServer(server: k::io::ServerSocket*) : _server(server) {}
            override run() : void {
                c : k::io::SocketChannel! = _server->accept(2000000000L);
                k::Thread::sleep(k::Duration::ofMillis(150L));
                c->close();
                delete c;
            }
        }

        test() : int {
            addr : k::io::NetworkAddress! = k::io::NetworkAddress::any(0);
            srv : k::io::ServerSocket! = k::io::ServerSocket::bind(addr);
            port : int = srv->localPort();

            sink : SinkServer! = new SinkServer(srv);
            t : k::Thread! = new k::Thread(sink);
            t->start();

            peer : k::io::NetworkAddress! = k::io::NetworkAddress::loopback(port);
            cli : k::io::SocketChannel! = k::io::SocketChannel::connect(peer, 2000000000L);
            cli->close();

            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(1u);
            b->put((byte) 1);
            b->flip();
            res : int = 0;
            try {
                cli->write(b);
            } catch (e: k::io::ClosedChannelException&) {
                res = 1;
            }

            t->join();
            srv->close();
            delete b; delete cli; delete peer;
            delete t; delete sink; delete srv; delete addr;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}
