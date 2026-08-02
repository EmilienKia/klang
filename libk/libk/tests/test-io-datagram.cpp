/*
 * K Language standard library — datagram socket tests (Phase 5)
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

TEST_CASE("DatagramSocket: UDP loopback sendTo/receive", "[libk][io][socket][udp]") {
    auto jit = jit_k(R"SRC(
        module __dgram_loopback__;

        class Receiver : public k::Runnable {
            _sock : k::io::DatagramSocket*;
            _ok   : int;
        public:
            Receiver(sock: k::io::DatagramSocket*) : _sock(sock), _ok(0) {}
            override run() : void {
                b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(8u);
                n : int = _sock->receive(b, 2000000000L);
                if (n == 4) {
                    b->flip();
                    if (b->get(0u) == (byte) 80 && b->get(3u) == (byte) 71) {
                        _ok = 1;
                    }
                }
                delete b;
            }
            const ok() : int { return _ok; }
        }

        test() : int {
            bindAddr : k::io::NetworkAddress! = k::io::NetworkAddress::any(0);
            recvSock : k::io::DatagramSocket! = k::io::DatagramSocket::bind(bindAddr);
            recvPort : int = recvSock->localPort();

            recvWorker : Receiver! = new Receiver(recvSock);
            recvThread : k::Thread! = new k::Thread(recvWorker);
            recvThread->start();

            sendSock : k::io::DatagramSocket! = k::io::DatagramSocket::open();
            dst : k::io::NetworkAddress! = k::io::NetworkAddress::loopback(recvPort);
            out : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(4u);
            out->put((byte) 80);
            out->put((byte) 73);
            out->put((byte) 78);
            out->put((byte) 71);
            out->flip();
            sent : int = sendSock->sendTo(out, dst, 2000000000L);

            recvThread->join();

            res : int = 0;
            if (sent == 4) { res = res + 1; }
            if (recvWorker->ok() == 1) { res = res + 2; }

            sendSock->close();
            recvSock->close();
            delete out;
            delete dst;
            delete sendSock;
            delete recvThread;
            delete recvWorker;
            delete recvSock;
            delete bindAddr;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 3);
}

