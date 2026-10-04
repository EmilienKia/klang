/*
 * Stream channel public API regressions. Copyright 2026 Emilien Kia.
 * Licensed under the Apache License, Version 2.0.
 */
#include <catch2/catch_all.hpp>
#include "../../klang/tests/helpers.hpp"
#include <filesystem>
#include <cstdlib>
#include <string>

namespace {
struct UnixFixture {
    std::filesystem::path directory;
    UnixFixture() {
        char name[] = "/tmp/klang-stream-XXXXXX";
        char* path = mkdtemp(name);
        REQUIRE(path);
        directory = path;
    }
    ~UnixFixture() {
        std::filesystem::remove(directory / "endpoint");
        std::filesystem::remove(directory);
    }
    std::string source(std::string text) const {
        auto offset = text.find("@SOCKET@");
        while (offset != std::string::npos) {
            text.replace(offset, 8, (directory / "endpoint").generic_string());
            offset = text.find("@SOCKET@");
        }
        return text;
    }
};
void run(std::string source) {
    auto jit = gen_jit_with_stdlib(source, LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(test);
    int result = 0;
    REQUIRE_NOTHROW(result = test());
    REQUIRE(result == 1);
}
}

TEST_CASE("Stream API: TCP numeric and bounded hostname Futures progress inside their selected EventLoop",
          "[libk][io][stream-channel][k-api][future][sync][tcp]") {
    std::string host = "127.0.0.1";
    SECTION("Numeric") {}
    SECTION("Hostname") { host = "localhost"; }
    std::string source = R"SRC(
        module __stream_loop_future__;
        published : Promise<int>;
        selected : ExecutorLease!;
        test() : int throws(Throwable) {
            deadline : Duration = Duration::ofSeconds(5L);
            finished : Future<int>! = published.future();
            loop : k::io::EventLoop;
            selected = loop.acquireLease();
            loop.execute([]():void {
                try {
                    deadline : Duration = Duration::ofSeconds(5L);
                    bindAddress : k::io::NetworkAddress("127.0.0.1", 0);
                    server : k::io::ServerSocket! = k::io::ServerSocket::bind(bindAddress);
                    address : k::io::NetworkAddress("@HOST@", server->localPort());
                    accepted : Future<k::io::SocketChannel>! = server->acceptAsync(deadline, *selected);
                    connected : Future<k::io::SocketChannel>! = k::io::SocketChannel::connectAsync(
                        address, deadline, *selected);
                    client : k::io::SocketChannel = connected->get(deadline);
                    again : k::io::SocketChannel = connected->get(deadline);
                    peer : k::io::SocketChannel = accepted->get(deadline);
                    output : k::io::ByteBuffer! = k::io::ByteBuffer::allocateDirect(4u);
                    input : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(4u);
                    output->put((byte) 75); output->put((byte) 76);
                    output->put((byte) 65); output->put((byte) 78); output->flip();
                    read : Future<int>! = peer.readFullyAsync(input, deadline, *selected);
                    if (again.writeFullySync(output, deadline) != 4 || read->get(deadline) != 4 ||
                        input->position() != 4u || input->get(3u) != (byte) 78 ||
                        output->hasLease() || input->hasLease()) {
                        published.trySuccess(-1); return;
                    }
                    closing : Future<k::io::IoDone>! = client.closeAsync(*selected);
                    closing->get(deadline);
                    if (again.isOpen()) { published.trySuccess(-2); return; }
                    input->clear();
                    if (peer.readSync(input, deadline) != 0) { published.trySuccess(-3); return; }
                    peer.closeSync(); server->closeSync();
                    delete output; delete input; delete accepted; delete connected;
                    delete read; delete closing; delete server;
                    published.trySuccess(1);
                } catch (e: Throwable&) { published.trySuccess(-100 - e.getCode()); }
            });
            if (!loop.runOnce(deadline)) return -4;
            value : int = finished->get(deadline);
            delete finished; delete selected;
            return value;
        }
    )SRC";
    source.replace(source.find("@HOST@"), 6, host);
    run(source);
}

TEST_CASE("Stream API: Unix accept and connect publish copyable values on a selected EventLoop",
          "[libk][io][stream-channel][k-api][callback][unix]") {
    UnixFixture fixture;
    run(fixture.source(R"SRC(
        module __stream_unix_callbacks__;
        client : k::io::SocketChannel;
        peer : k::io::SocketChannel;
        callbacks : int = 0;
        valid : bool = true;
        record() {
            current : k::io::IoOperation! = k::io::IoOperation::current();
            valid = valid && current != null;
            if (current != null) valid = valid && current->isCallbackRunning() && current->isIoDone();
            ++callbacks;
            delete current;
        }
        test() : int throws(Throwable) {
            deadline : Duration = Duration::ofSeconds(5L);
            path : k::io::file::Path("@SOCKET@");
            server : k::io::ServerSocket! = k::io::ServerSocket::bind(path);
            loop : k::io::EventLoop;
            selected : ExecutorLease! = loop.acquireLease();
            accept : k::io::IoOperation! = server->accept(deadline,
                [](r: k::io::IoResult<k::io::SocketChannel>):void {
                    valid = valid && r.hasResult();
                    if (r.hasResult()) peer = r.getResult();
                    record();
                }, *selected);
            connect : k::io::IoOperation! = k::io::SocketChannel::connect(path, deadline,
                [](r: k::io::IoResult<k::io::SocketChannel>):void {
                    valid = valid && r.hasResult();
                    if (r.hasResult()) client = r.getResult();
                    record();
                }, *selected);
            if (accept->waitQuiescent(deadline) != 0 || connect->waitQuiescent(deadline) != 0 ||
                callbacks != 0) return -1;
            if (!loop.runOnce(deadline) || !loop.runOnce(deadline) || callbacks != 2 ||
                !valid || !client.isOpen() || !peer.isOpen()) return -2;
            output : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(2u);
            input : k::io::ByteBuffer! = k::io::ByteBuffer::allocateDirect(2u);
            output->put((byte) 85); output->put((byte) 88); output->flip();
            read : k::io::IoOperation! = peer.readFully(input, deadline,
                [](r: k::io::IoResult<int>):void {
                    valid = valid && r.hasResult() && r.getResult() == 2;
                    record();
                }, *selected);
            write : k::io::IoOperation! = client.writeFully(output, 5000000000L,
                [](r: k::io::IoResult<int>):void {
                    valid = valid && r.hasResult() && r.getResult() == 2;
                    record();
                }, *selected);
            if (read->waitQuiescent(deadline) != 0 || write->waitQuiescent(deadline) != 0 ||
                !loop.runOnce(deadline) || !loop.runOnce(deadline) || callbacks != 4 ||
                !valid || input->get(1u) != (byte) 88) return -3;
            client.closeSync();
            input->clear();
            eof : bool = false;
            try { peer.readFullySync(input, deadline); }
            catch (e: k::io::EndOfStreamException&) { eof = e.getBytesTransferred() == 0L; }
            if (!eof || input->hasLease()) return -4;
            peer.closeSync();
            close : k::io::IoOperation! = server->close(
                [](r: k::io::IoResult<k::io::IoDone>):void {
                    valid = valid && r.hasResult(); record();
                }, *selected);
            if (close->waitQuiescent(deadline) != 0 || !loop.runOnce(deadline) ||
                callbacks != 5 || !valid || server->isOpen()) return -5;
            delete output; delete input; delete accept; delete connect;
            delete read; delete write; delete close; delete selected; delete server;
            return 1;
        }
    )SRC"));
}

TEST_CASE("Stream API: shared copies reject overlapping reads and close drains both directions",
          "[libk][io][stream-channel][k-api][direction][close]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __stream_shared_directions__;
        stage : int = 0;
        overlapCode : int = 0;
        overlapErrno : int = 0;
        firstDone : bool = false;
        firstLease : bool = false;
        secondLease : bool = false;
        clientOpen : bool = false;
        peerOpen : bool = false;
        copyOpen : bool = false;
        progress() : int { return stage; }
        overlapExceptionCode() : int { return overlapCode; }
        overlapPlatformErrno() : int { return overlapErrno; }
        firstIsDone() : int { return firstDone ? 1 : 0; }
        firstHasLease() : int { return firstLease ? 1 : 0; }
        secondHasLease() : int { return secondLease ? 1 : 0; }
        clientIsOpen() : int { return clientOpen ? 1 : 0; }
        peerIsOpen() : int { return peerOpen ? 1 : 0; }
        copyIsOpen() : int { return copyOpen ? 1 : 0; }
        test() : int throws(Throwable) {
            stage = 1;
            deadline : Duration = Duration::ofSeconds(5L);
            local : k::io::NetworkAddress("127.0.0.1", 0);
            server : k::io::ServerSocket! = k::io::ServerSocket::bind(local);
            address : k::io::NetworkAddress("127.0.0.1", server->localPort());
            accepted : Future<k::io::SocketChannel>! = server->acceptAsync(deadline);
            client : k::io::SocketChannel = k::io::SocketChannel::connectSync(address, deadline);
            peer : k::io::SocketChannel = accepted->get(deadline);
            copy : k::io::SocketChannel = peer;
            first : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(4u);
            second : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(4u);
            if (first->remaining() != 4u || second->remaining() != 4u) return -3;
            stage = 2;
            pending : Future<int>! = peer.readFullyAsync(first, deadline);
            firstDone = pending->isDone();
            firstLease = first->hasLease();
            clientOpen = client.isOpen();
            peerOpen = peer.isOpen();
            copyOpen = copy.isOpen();
            if (firstDone || !firstLease || !clientOpen || !peerOpen || !copyOpen) return -4;
            stage = 3;
            overlap : Future<int>! = copy.readAsync(second, deadline);
            exhausted : bool = false;
            try { overlap->get(deadline); }
            catch (e: ExecutionException&) {
                cause : k::io::IOException* = (k::io::IOException*) e.getCause();
                if (cause != null) {
                    overlapCode = cause->getCode();
                    overlapErrno = cause->getErrno();
                    exhausted = overlapCode == 200 && overlapErrno == 16;
                }
            }
            firstDone = pending->isDone();
            firstLease = first->hasLease();
            secondLease = second->hasLease();
            if (!exhausted || secondLease || firstDone || !firstLease) return -1;
            stage = 4;
            peer.closeSync();
            cancelled : bool = false;
            try { pending->get(deadline); }
            catch (e: CancellationException&) { cancelled = true; }
            firstDone = pending->isDone();
            firstLease = first->hasLease();
            secondLease = second->hasLease();
            peerOpen = peer.isOpen();
            copyOpen = copy.isOpen();
            if (!cancelled || !firstDone || firstLease || secondLease || peerOpen || copyOpen) return -2;
            stage = 5;
            client.closeSync(); server->closeSync();
            delete first; delete second; delete pending; delete overlap;
            delete accepted; delete server;
            return 1;
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(test);
    auto diagnostic = [&](const char* name) {
        auto getter = jit->lookup_symbol<int (*)()>(name);
        REQUIRE(getter);
        return getter();
    };
    int result = 0;
    REQUIRE_NOTHROW(result = test());
    INFO("Shared directions stage=" << diagnostic("progress")
         << " overlap exception code/errno=" << diagnostic("overlapExceptionCode")
         << "/" << diagnostic("overlapPlatformErrno")
         << " first done/lease=" << diagnostic("firstIsDone")
         << "/" << diagnostic("firstHasLease")
         << " second lease=" << diagnostic("secondHasLease")
         << " client/peer/copy open=" << diagnostic("clientIsOpen")
         << "/" << diagnostic("peerIsOpen") << "/" << diagnostic("copyIsOpen"));
    REQUIRE(result == 1);
}

TEST_CASE("Stream API: default callbacks and preparation failures use the canonical result taxonomy",
          "[libk][io][stream-channel][k-api][default-executor][prepare]") {
    run(R"SRC(
        module __stream_default_errors__;
        delivered : Promise<int>;
        test() : int throws(Throwable) {
            deadline : Duration = Duration::ofSeconds(5L);
            finished : Future<int>! = delivered.future();
            invalid : k::io::NetworkAddress;
            operation : k::io::IoOperation! = k::io::SocketChannel::connect(invalid, deadline,
                [](r: k::io::IoResult<k::io::SocketChannel>):void {
                    good : bool = r.hasError();
                    if (good) good = r.getError().kind() == k::io::IoErrorKind::InvalidArgument &&
                        r.getError().getErrno() == 22;
                    current : k::io::IoOperation! = k::io::IoOperation::current();
                    good = good && current != null;
                    delivered.trySuccess(good ? 1 : -1);
                    delete current;
                });
            if (finished->get(deadline) != 1 || operation->waitQuiescent(deadline) != 0) return -1;
            closed : k::io::ServerSocket;
            rejected : Future<k::io::SocketChannel>! = closed.acceptAsync(deadline);
            observed : bool = false;
            try { rejected->get(deadline); }
            catch (e: ExecutionException&) {
                cause : k::io::ClosedChannelException* = (k::io::ClosedChannelException*) e.getCause();
                observed = cause != null;
            }
            closed.closeSync();
            delete finished; delete operation; delete rejected;
            return observed ? 1 : -2;
        }
    )SRC");
}

TEST_CASE("NetworkAddress: construction rejects out-of-range ports with canonical I/O exceptions",
          "[libk][io][stream-channel][k-api][address][prepare]") {
    run(R"SRC(
        module __stream_address_port_bounds__;
        test() : int throws(Throwable) {
            try {
                invalid : k::io::NetworkAddress("127.0.0.1", -1);
                return -1;
            } catch (e: k::io::IOException&) {
                if (e.getCode() != 200 || e.getErrno() != 22) return -2;
            }
            try {
                invalid : k::io::NetworkAddress("127.0.0.1", 65536);
                return -3;
            } catch (e: k::io::IOException&) {
                if (e.getCode() != 200 || e.getErrno() != 22) return -4;
            }
            lower : k::io::NetworkAddress("127.0.0.1", 0);
            upper : k::io::NetworkAddress("127.0.0.1", 65535);
            if (lower.port() != 0 || lower.family() != 4 || !lower.isResolved() ||
                upper.port() != 65535 || upper.family() != 4 || !upper.isResolved()) return -5;
            return 1;
        }
    )SRC");
}
