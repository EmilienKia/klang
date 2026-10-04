/*
 * FileChannel composite API regressions.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
#include <catch2/catch_all.hpp>
#include "../../klang/tests/helpers.hpp"
#include <filesystem>
#include <fstream>
#include <cstdlib>

namespace {
struct Fixture {
    std::filesystem::path directory;
    Fixture() {
        char name[] = "/tmp/klang-fully-XXXXXX";
        char* path = mkdtemp(name);
        REQUIRE(path);
        directory = path;
    }
    ~Fixture() {
        std::filesystem::remove(directory / "data.bin");
        std::filesystem::remove(directory);
    }
    std::string source(std::string text) const {
        const auto path = (directory / "data.bin").generic_string();
        size_t offset = 0;
        while ((offset = text.find("@FILE@", offset)) != std::string::npos) {
            text.replace(offset, 6, path);
            offset += path.size();
        }
        return text;
    }
    std::string contents() const {
        std::ifstream input(directory / "data.bin", std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), {});
    }
};
}

TEST_CASE("FileChannel fully API: selected EventLoop delivers one final Expected including EOF",
          "[libk][io][file-fully][k-api][callback]") {
    Fixture fixture;
    auto jit = gen_jit_with_stdlib(fixture.source(R"SRC(
        module __fully_callbacks__;
        callbacks : int = 0;
        valid : bool = true;
        record(result: bool, bytes: long) {
            context : k::io::IoOperation! = k::io::IoOperation::current();
            valid = valid && result && context != null;
            if (context != null)
                valid = valid && context->isCallbackRunning() &&
                    context->isIoDone() && context->bytesTransferred() == bytes;
            ++callbacks;
            delete context;
        }
        test() : int throws(Throwable) {
            deadline : Duration = Duration::ofSeconds(5L);
            path : k::io::file::Path("@FILE@");
            channel : k::io::file::FileChannel = k::io::file::FileChannel::openSync(
                path, k::io::file::OPEN_READ | k::io::file::OPEN_WRITE |
                k::io::file::OPEN_CREATE | k::io::file::OPEN_TRUNCATE);
            loop : k::io::EventLoop;
            selected : ExecutorLease! = loop.acquireLease();
            buffer : k::io::ByteBuffer! = k::io::ByteBuffer::allocateDirect(4u);
            buffer->put((byte) 65); buffer->put((byte) 66);
            buffer->put((byte) 67); buffer->put((byte) 68); buffer->flip();
            completion : k::io::IoCompletion<int> = [](r: k::io::IoResult<int>):void {
                record(r.hasResult() && r.getResult() == 4, 4L);
            };
            write : k::io::IoOperation! = channel.writeFully(buffer, completion,
                5000000000L, *selected);
            if (completion != null || buffer == null || write == null ||
                write->waitQuiescent(deadline) != 0 || callbacks != 0 ||
                write->stage() != k::io::IoOperationStage::CallbackQueued) return -1;
            if (!loop.runOnce(deadline) || callbacks != 1 || !valid ||
                buffer->position() != 4u || buffer->hasLease() || channel.position() != 4L) return -2;
            buffer->clear();
            read : k::io::IoOperation! = channel.readFully(buffer, 0L, deadline,
                [](r: k::io::IoResult<int>):void {
                    record(r.hasResult() && r.getResult() == 4, 4L);
                }, *selected);
            if (read->waitQuiescent(deadline) != 0 || callbacks != 1) return -3;
            if (!loop.runOnce(deadline) || callbacks != 2 || !valid ||
                channel.position() != 4L || buffer->position() != 4u ||
                buffer->get(3u) != (byte) 68) return -4;
            large : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(8u);
            eof : k::io::IoOperation! = channel.readFully(large, 0L, 5000000000L,
                [](r: k::io::IoResult<int>):void {
                    good : bool = r.hasError();
                    if (good) {
                        error : k::io::IoError = r.getError();
                        good = error.kind() == k::io::IoErrorKind::EndOfStream &&
                            error.bytesTransferred() == 4L;
                    }
                    record(good, 4L);
                }, *selected);
            if (eof->waitQuiescent(deadline) != 0 || callbacks != 2 ||
                eof->error().kind() != k::io::IoErrorKind::EndOfStream ||
                eof->bytesTransferred() != 4L || large->position() != 4u ||
                large->hasLease()) return -5;
            if (!loop.runOnce(deadline) || callbacks != 3 || !valid) return -6;
            empty : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(0u);
            zero : k::io::IoOperation! = channel.readFully(empty, deadline,
                [](r: k::io::IoResult<int>):void {
                    record(r.hasResult() && r.getResult() == 0, 0L);
                }, *selected);
            if (zero->waitQuiescent(deadline) != 0 || zero->bytesTransferred() != 0L ||
                !loop.runOnce(deadline) || callbacks != 4 || !valid) return -7;
            invalid : k::io::IoOperation! = channel.writeFully(empty, -1L,
                [](r: k::io::IoResult<int>):void {
                    good : bool = r.hasError();
                    if (good) good = r.getError().kind() == k::io::IoErrorKind::InvalidArgument;
                    record(good, 0L);
                }, *selected);
            if (invalid->waitQuiescent(deadline) != 0 || !loop.runOnce(deadline) ||
                callbacks != 5 || !valid || empty->hasLease()) return -8;
            poll : Duration = Duration::ofMillis(0L);
            if (loop.runOnce(poll)) return -9;
            channel.closeSync();
            delete write; delete read; delete eof; delete zero; delete invalid;
            delete buffer; delete large; delete empty; delete selected;
            return 1;
        }
    )SRC"), LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(test);
    int result = 0;
    REQUIRE_NOTHROW(result = test());
    REQUIRE(result == 1);
    REQUIRE(fixture.contents() == "ABCD");
}

TEST_CASE("FileChannel fully API: Future and Sync progress inside the selected EventLoop task",
          "[libk][io][file-fully][k-api][future][sync]") {
    Fixture fixture;
    auto jit = gen_jit_with_stdlib(fixture.source(R"SRC(
        module __fully_loop_future__;
        published : Promise<int>;
        selected : ExecutorLease!;
        test() : int throws(Throwable) {
            deadline : Duration = Duration::ofSeconds(5L);
            done : Future<int>! = published.future();
            loop : k::io::EventLoop;
            selected = loop.acquireLease();
            loop.execute([]():void {
                try {
                    deadline : Duration = Duration::ofSeconds(5L);
                    path : k::io::file::Path("@FILE@");
                    channel : k::io::file::FileChannel = k::io::file::FileChannel::openSync(
                        path, k::io::file::OPEN_READ | k::io::file::OPEN_WRITE |
                        k::io::file::OPEN_CREATE | k::io::file::OPEN_TRUNCATE);
                    buffer : k::io::ByteBuffer! = k::io::ByteBuffer::allocateDirect(4u);
                    buffer->put((byte) 75); buffer->put((byte) 76);
                    buffer->put((byte) 65); buffer->put((byte) 78); buffer->flip();
                    write : Future<int>! = channel.writeFullyAsync(buffer, deadline, *selected);
                    if (write->get(deadline) != 4 || buffer == null || buffer->hasLease() ||
                        channel.position() != 4L) { published.trySuccess(-1); return; }
                    buffer->clear();
                    read : Future<int>! = channel.readFullyAsync(buffer, 0L, 5000000000L, *selected);
                    if (read->get(deadline) != 4 || buffer->position() != 4u ||
                        channel.position() != 4L) { published.trySuccess(-2); return; }
                    buffer->flip();
                    if (channel.writeFullySync(buffer, 0L, deadline) != 4)
                        { published.trySuccess(-3); return; }
                    empty : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(0u);
                    zero : Future<int>! = channel.readFullyAsync(empty, *selected, 5000000000L);
                    defaultZero : Future<int>! = channel.writeFullyAsyncWithTimeout(empty, 5000000000L);
                    if (zero->get(deadline) != 0 || defaultZero->get(deadline) != 0 ||
                        channel.readFullySyncWithTimeout(empty, 5000000000L) != 0)
                        { published.trySuccess(-4); return; }
                    large : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(8u);
                    eof : Future<int>! = channel.readFullyAsync(large, 0L, deadline);
                    eofCount : int = 0;
                    try { eof->get(deadline); }
                    catch (e: ExecutionException&) {
                        error : k::io::EndOfStreamException* = (k::io::EndOfStreamException*) e.getCause();
                        if (error != null && error->getBytesTransferred() == 4L) ++eofCount;
                    }
                    large->clear();
                    try { channel.readFullySync(large, 0L, deadline); }
                    catch (e: k::io::EndOfStreamException&) {
                        if (e.getBytesTransferred() == 4L) ++eofCount;
                    }
                    if (eofCount != 2 || large->position() != 4u || large->hasLease())
                        { published.trySuccess(-5); return; }
                    channel.closeSync();
                    delete buffer; delete empty; delete large;
                    delete write; delete read; delete zero; delete defaultZero; delete eof;
                    published.trySuccess(1);
                } catch (e: Throwable&) {
                    published.trySuccess(-100 - e.getCode());
                }
            });
            if (!loop.runOnce(deadline)) return -10;
            result : int = done->get(deadline);
            delete selected; delete done;
            return result;
        }
    )SRC"), LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(test);
    int result = 0;
    REQUIRE_NOTHROW(result = test());
    REQUIRE(result == 1);
    REQUIRE(fixture.contents() == "KLAN");
}

TEST_CASE("FileChannel fully API: default executor, empty success and invalid Future are observable",
          "[libk][io][file-fully][k-api][default-executor]") {
    bool direct = false;
    SECTION("System default") {}
    SECTION("Direct selected") { direct = true; }
    Fixture fixture;
    auto jit = gen_jit_with_stdlib(fixture.source(R"SRC(
        module __fully_default__;
        delivered : Promise<int>;
        test(direct: bool) : int throws(Throwable) {
            deadline : Duration = Duration::ofSeconds(5L);
            path : k::io::file::Path("@FILE@");
            channel : k::io::file::FileChannel = k::io::file::FileChannel::openSync(
                path, k::io::file::OPEN_READ | k::io::file::OPEN_WRITE |
                k::io::file::OPEN_CREATE | k::io::file::OPEN_TRUNCATE);
            empty : k::io::ByteBuffer! = k::io::ByteBuffer::allocateDirect(0u);
            received : Future<int>! = delivered.future();
            completion : k::io::IoCompletion<int> = [](r: k::io::IoResult<int>):void {
                context : k::io::IoOperation! = k::io::IoOperation::current();
                good : bool = r.hasResult() && r.getResult() == 0 &&
                    context != null && context->isCallbackRunning() &&
                    context->bytesTransferred() == 0L;
                delivered.trySuccess(good ? 1 : -1);
                delete context;
            };
            executor : DirectExecutor;
            selected : ExecutorLease! = executor.acquireLease();
            operation : k::io::IoOperation!;
            if (direct) operation = channel.writeFully(empty, deadline, completion, *selected);
            else operation = channel.writeFully(empty, deadline, completion);
            if (operation == null || operation->waitQuiescent(deadline) != 0 ||
                received->get(deadline) != 1 || empty == null || empty->hasLease()) return -2;
            invalid : Future<int>! = channel.readFullyAsync(empty, -1L);
            code : int = 0;
            try { invalid->get(deadline); }
            catch (e: ExecutionException&) {
                error : k::io::IOException* = (k::io::IOException*) e.getCause();
                if (error != null && error->getErrno() == 22) code = 1;
            }
            if (code != 1 || empty->hasLease()) return -3;
            if (channel.writeFullySync(empty) != 0 || channel.readFullySync(empty) != 0 ||
                channel.writeFullySyncWithTimeout(empty, 5000000000L) != 0) return -4;
            channel.closeSync();
            delete operation; delete received; delete invalid; delete empty; delete selected;
            return 1;
        }
    )SRC"), LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)(bool)>("test");
    REQUIRE(test);
    int result = 0;
    REQUIRE_NOTHROW(result = test(direct));
    REQUIRE(result == 1);
    REQUIRE(fixture.contents().empty());
}
