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

#include <filesystem>
#include <string>
#include <cstdlib>
#include "../../klang/tests/helpers.hpp"

#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR not defined — set via CMake target_compile_definitions"
#endif

TEST_CASE("Unix socket proactor: stream Future and Sync round-trip", "[libk][io][socket][unix][stream-channel]") {
    char name[] = "/tmp/klang-unix-stream-XXXXXX";
    char* directory = mkdtemp(name);
    REQUIRE(directory);
    struct Cleanup {
        std::filesystem::path directory;
        ~Cleanup() {
            std::filesystem::remove(directory / "endpoint");
            std::filesystem::remove(directory);
        }
    } cleanup{directory};
    std::string source = R"SRC(
        module __unix_stream_proactor__;
        test() : int throws(Throwable) {
            deadline : Duration = Duration::ofSeconds(5L);
            path : k::io::file::Path("@SOCKET@");
            server : k::io::ServerSocket! = k::io::ServerSocket::bind(path);
            accepted : Future<k::io::SocketChannel>! = server->acceptAsync(deadline);
            connected : Future<k::io::SocketChannel>! = k::io::SocketChannel::connectAsync(path, deadline);
            client : k::io::SocketChannel = connected->get(deadline);
            peer : k::io::SocketChannel = accepted->get(deadline);
            output : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(4u);
            input : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(4u);
            output->put((byte) 85); output->put((byte) 78);
            output->put((byte) 73); output->put((byte) 88); output->flip();
            read : Future<int>! = peer.readFullyAsync(input, deadline);
            if (client.writeFullySync(output, deadline) != 4 || read->get(deadline) != 4) return -1;
            input->flip(); output->clear();
            reply : Future<int>! = client.readFullyAsync(output, deadline);
            if (peer.writeFullySync(input, deadline) != 4 || reply->get(deadline) != 4 ||
                output->get(0u) != (byte) 85 || output->get(3u) != (byte) 88) return -2;
            client.closeSync(); peer.closeSync(); server->closeSync();
            delete accepted; delete connected; delete read; delete reply;
            delete output; delete input; delete server;
            return 1;
        }
    )SRC";
    source.replace(source.find("@SOCKET@"), 8, (cleanup.directory / "endpoint").generic_string());
    auto jit = gen_jit_with_stdlib(source, LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(test);
    int result = 0;
    REQUIRE_NOTHROW(result = test());
    REQUIRE(result == 1);
}

TEST_CASE("Unix socket proactor: datagram sendTo/connect", "[libk][io][socket][unix][udp]") {
    char name[] = "/tmp/klang-unix-datagram-XXXXXX";
    char* directory = mkdtemp(name);
    REQUIRE(directory);
    struct Cleanup {
        std::filesystem::path directory;
        ~Cleanup() {
            std::filesystem::remove(directory / "receiver");
            std::filesystem::remove(directory / "sender");
            std::filesystem::remove(directory);
        }
    } cleanup{directory};
    std::string source = R"SRC(
        module __unix_datagram_proactor__;
        test() : int throws(Throwable) {
            deadline : Duration = Duration::ofSeconds(5L);
            receiverPath : k::io::file::Path("@RECEIVER@");
            senderPath : k::io::file::Path("@SENDER@");
            receiver : k::io::DatagramSocket! = k::io::DatagramSocket::bind(receiverPath);
            sender : k::io::DatagramSocket! = k::io::DatagramSocket::bind(senderPath);
            remote : k::io::NetworkAddress = k::io::NetworkAddress::unix(receiverPath);
            output : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(1u);
            input : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(1u);
            output->put((byte) 68); output->flip();
            read : Future<k::io::DatagramReceive>! = receiver->receiveAsync(input, deadline);
            if (sender->sendToSync(output, remote, deadline) != 1) return -1;
            packet : k::io::DatagramReceive = read->get(deadline);
            if (packet.bytesTransferred() != 1 || packet.source().family() != 1 ||
                input->get(0u) != (byte) 68) return -2;
            sender->connectSync(remote, deadline);
            output->rewind(); input->clear();
            if (sender->sendSync(output, deadline) != 1 ||
                receiver->receiveSync(input, deadline).bytesTransferred() != 1) return -3;
            sender->closeSync(); receiver->closeSync();
            delete read; delete output; delete input; delete sender; delete receiver;
            return 1;
        }
    )SRC";
    source.replace(source.find("@RECEIVER@"), 10, (cleanup.directory / "receiver").generic_string());
    source.replace(source.find("@SENDER@"), 8, (cleanup.directory / "sender").generic_string());
    auto jit = gen_jit_with_stdlib(source, LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(test);
    int result = 0;
    REQUIRE_NOTHROW(result = test());
    REQUIRE(result == 1);
    REQUIRE_FALSE(std::filesystem::exists(cleanup.directory / "receiver"));
    REQUIRE_FALSE(std::filesystem::exists(cleanup.directory / "sender"));
}
