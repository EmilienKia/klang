/*
 * Datagram public API regressions. Copyright 2026 Emilien Kia.
 * Licensed under the Apache License, Version 2.0.
 */
#include <catch2/catch_all.hpp>
#include "../../klang/tests/helpers.hpp"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <string>

namespace {
void runDatagram(std::string source) {
    auto jit = gen_jit_with_stdlib(source, LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(test);
    int result = 0;
    REQUIRE_NOTHROW(result = test());
    REQUIRE(result == 1);
}
struct DatagramFixture {
    std::filesystem::path directory;
    DatagramFixture() {
        char name[] = "/tmp/klang-datagram-XXXXXX";
        char* path = mkdtemp(name);
        REQUIRE(path);
        directory = path;
    }
    ~DatagramFixture() {
        for (auto name : {"receiver", "sender", "occupied"})
            std::filesystem::remove(directory / name);
        std::filesystem::remove(directory);
    }
    std::string source(std::string text) const {
        for (auto name : {"receiver", "sender", "occupied"}) {
            std::string token = "@" + std::string(name) + "@";
            auto offset = text.find(token);
            while (offset != std::string::npos) {
                text.replace(offset, token.size(), (directory / name).generic_string());
                offset = text.find(token);
            }
        }
        return text;
    }
};
}

TEST_CASE("Datagram API: selected EventLoop task waits on independent hostname and receive Futures",
          "[libk][io][datagram-channel][k-api][future][udp][dns]") {
    runDatagram(R"SRC(
        module __datagram_selected_future__;
        finished : Promise<int>;
        selected : ExecutorLease!;
        test() : int throws(Throwable) {
            deadline : Duration = Duration::ofSeconds(5L);
            result : Future<int>! = finished.future();
            loop : k::io::EventLoop;
            selected = loop.acquireLease();
            loop.execute([]():void {
                try {
                    deadline : Duration = Duration::ofSeconds(5L);
                    local : k::io::NetworkAddress("127.0.0.1", 0);
                    receiver : k::io::DatagramSocket! = k::io::DatagramSocket::bind(local);
                    sender : k::io::DatagramSocket! = k::io::DatagramSocket::open();
                    remote : k::io::NetworkAddress("localhost", receiver->localPort());
                    output : k::io::ByteBuffer! = k::io::ByteBuffer::allocateDirect(4u);
                    input : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(2u);
                    output->put((byte) 75); output->put((byte) 76);
                    output->put((byte) 65); output->put((byte) 78); output->flip();
                    receive : Future<k::io::DatagramReceive>! = receiver->receiveAsync(input, deadline, *selected);
                    send : Future<int>! = sender->sendToAsync(output, remote, deadline, *selected);
                    sent : int = send->get(deadline);
                    packet : k::io::DatagramReceive = receive->get(deadline);
                    shared : Future<k::io::DatagramReceive>! = receive->share();
                    again : k::io::DatagramReceive = shared->get(deadline);
                    source : k::io::NetworkAddress = packet.source();
                    senderPort : int = sender->localPort();
                    sender->closeSync(); receiver->closeSync();
                    delete receive; delete shared; delete send; delete sender; delete receiver;
                    sourceText : char[]! = source.host();
                    hostname : char[]! = remote.host();
                    valid : bool = sent == 4 && packet.bytesTransferred() == 2 &&
                        packet.datagramLength() == 4L && packet.isTruncated() &&
                        again.source().port() == senderPort && source.family() == 4 &&
                        source.port() == senderPort && input->position() == 2u &&
                        output->position() == 4u && input->get(1u) == (byte) 76 &&
                        !input->hasLease() && !output->hasLease() &&
                        sourceText.size == 10 && sourceText[9] == (char) 0 &&
                        String(sourceText) == String("127.0.0.1") &&
                        hostname.size == 10 && hostname[9] == (char) 0 &&
                        String(hostname) == String("localhost");
                    delete sourceText; delete hostname; delete input; delete output;
                    finished.trySuccess(valid ? 1 : -1);
                } catch (e: Throwable&) { finished.trySuccess(-100 - e.getCode()); }
            });
            if (!loop.runOnce(deadline)) return -2;
            value : int = result->get(deadline);
            delete result; delete selected;
            return value;
        }
    )SRC");
}

TEST_CASE("Datagram API: connected zero packets and empty receive buffers each consume one packet",
          "[libk][io][datagram-channel][k-api][udp][empty][sync]") {
    runDatagram(R"SRC(
        module __datagram_empty_packets__;
        test() : int throws(Throwable) {
            deadline : Duration = Duration::ofSeconds(5L);
            local : k::io::NetworkAddress("127.0.0.1", 0);
            receiver : k::io::DatagramSocket! = k::io::DatagramSocket::bind(local);
            sender : k::io::DatagramSocket! = k::io::DatagramSocket::bind(local);
            remote : k::io::NetworkAddress("localhost", receiver->localPort());
            connection : Future<k::io::IoDone>! = sender->connectAsync(remote, deadline);
            connection->get(deadline);
            back : k::io::NetworkAddress("127.0.0.1", sender->localPort());
            receiver->connectSync(back, deadline);
            empty : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(0u);
            full : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(1u);
            full->put((byte) 90); full->flip();
            if (sender->sendSync(empty, deadline) != 0) return -1;
            first : k::io::DatagramReceive = receiver->receiveSync(empty, deadline);
            if (first.bytesTransferred() != 0 || first.datagramLength() != 0L ||
                first.isTruncated() || first.source().port() != sender->localPort()) return -2;
            if (sender->sendSync(full, deadline) != 1) return -3;
            second : k::io::DatagramReceive = receiver->receiveSync(empty, deadline);
            if (second.bytesTransferred() != 0 || second.datagramLength() != 1L ||
                !second.isTruncated() || empty->position() != 0u) return -4;
            full->rewind();
            sender->sendSync(full, deadline); full->clear();
            third : k::io::DatagramReceive = receiver->receiveSync(full, deadline);
            if (third.bytesTransferred() != 1 || third.isTruncated() ||
                full->get(0u) != (byte) 90 || full->hasLease() || empty->hasLease()) return -5;
            receiver->closeSync(); sender->closeSync();
            delete connection; delete receiver; delete sender; delete empty; delete full;
            return 1;
        }
    )SRC");
}

TEST_CASE("Datagram API: Unix callbacks retain source values beyond native quiescence",
          "[libk][io][datagram-channel][k-api][unix][callback]") {
    DatagramFixture fixture;
    runDatagram(fixture.source(R"SRC(
        module __datagram_unix_callbacks__;
        saved : k::io::DatagramReceive;
        callbacks : int = 0;
        valid : bool = true;
        test() : int throws(Throwable) {
            deadline : Duration = Duration::ofSeconds(5L);
            receiverPath : k::io::file::Path("@receiver@");
            senderPath : k::io::file::Path("@sender@");
            receiver : k::io::DatagramSocket! = k::io::DatagramSocket::bind(receiverPath);
            sender : k::io::DatagramSocket! = k::io::DatagramSocket::bind(senderPath);
            remote : k::io::NetworkAddress = k::io::NetworkAddress::unix(receiverPath);
            loop : k::io::EventLoop;
            lease : ExecutorLease! = loop.acquireLease();
            input : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(1u);
            output : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(1u);
            output->put((byte) 85); output->flip();
            receive : k::io::IoOperation! = receiver->receive(input, deadline,
                [](result: k::io::IoResult<k::io::DatagramReceive>):void {
                    valid = valid && result.hasResult();
                    if (result.hasResult()) saved = result.getResult();
                    current : k::io::IoOperation! = k::io::IoOperation::current();
                    valid = valid && current != null;
                    if (current != null) valid = valid && current->isIoDone() && current->isCallbackRunning();
                    delete current; ++callbacks;
                }, *lease);
            send : k::io::IoOperation! = sender->sendTo(output, remote, 5000000000L,
                [](result: k::io::IoResult<int>):void {
                    valid = valid && result.hasResult() && result.getResult() == 1; ++callbacks;
                }, *lease);
            if (receive->waitQuiescent(deadline) != 0 || send->waitQuiescent(deadline) != 0 ||
                callbacks != 0 || input->hasLease()) return -1;
            if (!loop.runOnce(deadline) || !loop.runOnce(deadline) || callbacks != 2) return -2;
            delete receive; delete send;
            source : k::io::NetworkAddress = saved.source();
            text : char[]! = source.host();
            senderName : String("@sender@");
            valid = valid && text.size > 0 && text[text.size - 1] == (char) 0 &&
                String(text) == senderName && saved.bytesTransferred() == 1 &&
                saved.datagramLength() == 1L && !saved.isTruncated() && source.family() == 1;
            connect : k::io::IoOperation! = sender->connect(receiverPath, deadline,
                [](r: k::io::IoResult<k::io::IoDone>):void {
                    valid = valid && r.hasResult(); ++callbacks;
                }, *lease);
            if (connect->waitQuiescent(deadline) != 0 || !loop.runOnce(deadline) || callbacks != 3) return -4;
            input->clear(); output->rewind();
            reply : Future<k::io::DatagramReceive>! = receiver->receiveAsync(input, deadline);
            connectedSend : k::io::IoOperation! = sender->send(output, deadline,
                [](r: k::io::IoResult<int>):void {
                    valid = valid && r.hasResult() && r.getResult() == 1; ++callbacks;
                }, *lease);
            if (reply->get(deadline).bytesTransferred() != 1 ||
                connectedSend->waitQuiescent(deadline) != 0 || !loop.runOnce(deadline) || callbacks != 4) return -5;
            closing : k::io::IoOperation! = sender->close(
                [](r: k::io::IoResult<k::io::IoDone>):void {
                    valid = valid && r.hasResult(); ++callbacks;
                }, *lease);
            if (closing->waitQuiescent(deadline) != 0 || !loop.runOnce(deadline) || callbacks != 5) return -6;
            receiver->closeSync();
            delete connect; delete reply; delete connectedSend; delete closing;
            delete text; delete input; delete output; delete lease; delete sender; delete receiver;
            return valid ? 1 : -3;
        }
    )SRC"));
    REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "sender"));
    REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "receiver"));
}

TEST_CASE("Datagram API: bind failure preserves a preexisting pathname and remote destruction never unlinks",
          "[libk][io][datagram-channel][k-api][unix][ownership]") {
    DatagramFixture fixture;
    {
        std::ofstream output(fixture.directory / "occupied");
        output << "keep";
        REQUIRE(output.good());
    }
    runDatagram(fixture.source(R"SRC(
        module __datagram_path_ownership__;
        test() : int throws(Throwable) {
            occupied : k::io::file::Path("@occupied@");
            rejected : bool = false;
            try { socket : k::io::DatagramSocket! = k::io::DatagramSocket::bind(occupied); delete socket; }
            catch (e: k::io::IOException&) { rejected = e.getErrno() == 98; }
            path : k::io::file::Path("@receiver@");
            first : k::io::DatagramSocket! = k::io::DatagramSocket::bind(path);
            {
                remote : k::io::NetworkAddress = k::io::NetworkAddress::unix(path);
                copy : k::io::NetworkAddress = remote;
            }
            secondRejected : bool = false;
            try { second : k::io::DatagramSocket! = k::io::DatagramSocket::bind(path); delete second; }
            catch (e: k::io::IOException&) { secondRejected = e.getErrno() == 98; }
            shared : k::io::DatagramSocket = *first;
            delete first;
            valid : bool = shared.isOpen();
            shared.closeSync();
            replacement : k::io::DatagramSocket! = k::io::DatagramSocket::bind(path);
            replacement->closeSync(); delete replacement;
            return rejected && secondRejected && valid ? 1 : -1;
        }
    )SRC"));
    std::ifstream input(fixture.directory / "occupied");
    std::string contents;
    input >> contents;
    REQUIRE(contents == "keep");
    REQUIRE_FALSE(std::filesystem::exists(fixture.directory / "receiver"));
}

TEST_CASE("Datagram API: shared direction overlap cancellation timeout and close release leases",
          "[libk][io][datagram-channel][k-api][cancel][timeout][close][direction]") {
    runDatagram(R"SRC(
        module __datagram_cancel_close__;
        test() : int throws(Throwable) {
            deadline : Duration = Duration::ofSeconds(5L);
            socket : k::io::DatagramSocket! = k::io::DatagramSocket::open();
            copy : k::io::DatagramSocket = *socket;
            first : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(2u);
            second : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(2u);
            pending : Future<k::io::DatagramReceive>! = socket->receiveAsync(first, deadline);
            overlap : Future<k::io::DatagramReceive>! = copy.receiveAsync(second, deadline);
            exhausted : bool = false;
            try { overlap->get(deadline); }
            catch (e: ExecutionException&) {
                cause : k::io::IOException* = (k::io::IOException*) e.getCause();
                exhausted = cause != null && cause->getCode() == 200 && cause->getErrno() == 16;
            }
            if (!exhausted || second->hasLease() || !first->hasLease()) return -1;
            if (!pending->cancel()) return -2;
            try { pending->get(deadline); return -3; }
            catch (e: CancellationException&) {}
            if (pending->waitQuiescent(deadline) != 0 || first->hasLease()) return -4;
            timedOut : bool = false;
            try { socket->receiveSync(first, Duration::ofMillis(10L)); }
            catch (e: TimeoutException&) { timedOut = true; }
            if (!timedOut || first->hasLease()) return -5;
            closingReceive : Future<k::io::DatagramReceive>! = copy.receiveAsync(first, deadline);
            closing : Future<k::io::IoDone>! = socket->closeAsync();
            closing->get(deadline);
            cancelled : bool = false;
            try { closingReceive->get(deadline); }
            catch (e: CancellationException&) { cancelled = true; }
            if (!cancelled || copy.isOpen() || socket->isOpen() || first->hasLease()) return -6;
            closed : bool = false;
            try { copy.receiveSync(second, deadline); }
            catch (e: k::io::ClosedChannelException&) { closed = true; }
            delete pending; delete overlap; delete closing; delete closingReceive;
            delete first; delete second; delete socket;
            return closed ? 1 : -7;
        }
    )SRC");
}

TEST_CASE("Datagram API: default and direct callbacks run once and propagate setup failures",
          "[libk][io][datagram-channel][k-api][default-executor][callback][errors]") {
    runDatagram(R"SRC(
        module __datagram_default_callbacks__;
        received : Promise<int>;
        sent : Promise<int>;
        rejected : Promise<int>;
        test() : int throws(Throwable) {
            deadline : Duration = Duration::ofSeconds(5L);
            receiver : k::io::DatagramSocket! = k::io::DatagramSocket::open();
            sender : k::io::DatagramSocket! = k::io::DatagramSocket::open();
            remote : k::io::NetworkAddress("127.0.0.1", receiver->localPort());
            empty : k::io::NetworkAddress;
            input : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(1u);
            output : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(1u);
            output->put((byte) 42); output->flip();
            readResult : Future<int>! = received.future();
            sendResult : Future<int>! = sent.future();
            errorResult : Future<int>! = rejected.future();
            read : k::io::IoOperation! = receiver->receive(input, deadline,
                [](r: k::io::IoResult<k::io::DatagramReceive>):void {
                    received.trySuccess(r.hasResult() && r.getResult().bytesTransferred() == 1 ? 1 : -1);
                });
            write : k::io::IoOperation! = sender->sendTo(output, remote, deadline,
                [](r: k::io::IoResult<int>):void { sent.trySuccess(r.hasResult() && r.getResult() == 1 ? 1 : -2); });
            if (readResult->get(deadline) != 1 || sendResult->get(deadline) != 1) return -1;
            directExecutor : DirectExecutor;
            direct : ExecutorLease! = directExecutor.acquireLease();
            output->rewind();
            invalid : k::io::IoOperation! = sender->sendTo(output, empty, deadline,
                [](r: k::io::IoResult<int>):void {
                    rejected.trySuccess(r.hasError() && r.getError().kind() == k::io::IoErrorKind::InvalidArgument ? 1 : -3);
                }, *direct);
            if (errorResult->get(deadline) != 1 || output->hasLease() || input->hasLease()) return -2;
            receiver->closeSync(); sender->closeSync();
            delete read; delete write; delete invalid; delete direct;
            delete readResult; delete sendResult; delete errorResult;
            delete input; delete output; delete sender; delete receiver;
            return 1;
        }
    )SRC");
}

TEST_CASE("Datagram API: in-flight handle and buffer wrapper destruction preserve native storage",
          "[libk][io][datagram-channel][k-api][lifetime]") {
    runDatagram(R"SRC(
        module __datagram_wrapper_lifetime__;
        test() : int throws(Throwable) {
            empty : k::io::NetworkAddress;
            emptyText : char[]! = empty.host();
            emptyValid : bool = emptyText.size == 1 && emptyText[0] == (char) 0;
            delete emptyText;
            deadline : Duration = Duration::ofSeconds(5L);
            receiver : k::io::DatagramSocket! = k::io::DatagramSocket::open();
            sender : k::io::DatagramSocket! = k::io::DatagramSocket::open();
            remote : k::io::NetworkAddress("127.0.0.1", receiver->localPort());
            input : k::io::ByteBuffer! = k::io::ByteBuffer::allocateDirect(1u);
            output : k::io::ByteBuffer! = k::io::ByteBuffer::allocateDirect(1u);
            output->put((byte) 73); output->flip();
            read : Future<k::io::DatagramReceive>! = receiver->receiveAsync(input, deadline);
            delete input; delete receiver;
            send : Future<int>! = sender->sendToAsync(output, remote, deadline);
            delete output; delete sender;
            if (send->get(deadline) != 1) return -1;
            packet : k::io::DatagramReceive = read->get(deadline);
            source : k::io::NetworkAddress = packet.source();
            delete read; delete send;
            text : char[]! = source.host();
            valid : bool = emptyValid && packet.bytesTransferred() == 1 &&
                source.family() == 4 && source.port() > 0 && text.size == 10 &&
                text[9] == (char) 0 && String(text) == String("127.0.0.1");
            delete text;
            return valid ? 1 : -2;
        }
    )SRC");
}
