/*
 * K Language standard library — FileChannel tests (Phase 4)
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

/**
 * Tests for k::io::file::FileChannel: proactor primitives and temporary
 * synchronous source-compatibility bridges.
 *
 * Coverage:
 *  - opening a missing file raises FileNotFoundException
 *  - create / write / read round-trip through ByteBuffer
 *  - positional reads and writes leave the channel position untouched
 *  - sequential reads and writes advance the channel position
 *  - readFully raises EndOfStreamException past the end of the file
 *  - writeFully transfers every remaining byte
 *  - size / truncate / force
 *  - operating on a closed channel raises ClosedChannelException
 *  - concurrent positional reads from several threads
 */

#include <catch2/catch_all.hpp>

#include "../../klang/tests/helpers.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <atomic>
#include <unistd.h>

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR not defined — set via CMake target_compile_definitions"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR not defined — set via CMake target_compile_definitions"
#endif

namespace {

std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src, bool dump = false) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR, dump);
}

void write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

struct FileFixture {
    std::filesystem::path directory;
    FileFixture() {
        static std::atomic<unsigned> sequence{0};
        do {
            directory = ".klang-file-channel-" + std::to_string(getpid()) +
                        "-" + std::to_string(sequence.fetch_add(1));
        } while (!std::filesystem::create_directory(directory));
    }
    ~FileFixture() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
    std::string path() const { return (directory / "data.bin").generic_string(); }
    std::string source(std::string text) const {
        size_t offset = 0;
        while ((offset = text.find("@FILE@", offset)) != std::string::npos) {
            text.replace(offset, 6, path());
            offset += path().size();
        }
        return text;
    }
};

} // anonymous namespace

TEST_CASE("FileChannel primitives: shared Future values, FIFO transfers and Sync bridges",
          "[libk][io][file-channel][proactor][file-primitives]") {
    FileFixture fixture;
    auto jit = jit_k(fixture.source(R"SRC(
        module __fc_proactor_values__;
        test() : int throws(ThreadInterruptionException, TimeoutException,
            CancellationException, ExecutionException, IllegalStateException) {
            deadline : Duration = Duration::ofSeconds(5L);
            opened : Future<k::io::file::FileChannel>! = null;
            {
                path : k::io::file::Path("@FILE@");
                opened = k::io::file::FileChannel::openAsync(path,
                    k::io::file::OPEN_READ | k::io::file::OPEN_WRITE |
                    k::io::file::OPEN_CREATE | k::io::file::OPEN_TRUNCATE);
            }
            shared : Future<k::io::file::FileChannel>! = opened->share();
            {
                first : k::io::file::FileChannel = opened->get(deadline);
                second : k::io::file::FileChannel = shared->get(deadline);
                third : k::io::file::FileChannel = opened->get(deadline);
                assigned : k::io::file::FileChannel;
                if (assigned.isOpen()) return -1;
                assigned = first;
                delete opened;
                delete shared;
                if (!first.isOpen() || !second.isOpen() || !third.isOpen() ||
                    !assigned.isOpen()) return -2;
                a : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(2u);
                b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(3u);
                a->put((byte) 97); a->put((byte) 98); a->flip();
                b->put((byte) 99); b->put((byte) 100); b->put((byte) 101); b->flip();
                a->acquireLease();
                blocked : int = 0;
                try { a->position(0u); }
                catch (e: k::io::BufferInUseException&) { ++blocked; }
                try { a->limit(2u); }
                catch (e: k::io::BufferInUseException&) { ++blocked; }
                try { a->clear(); }
                catch (e: k::io::BufferInUseException&) { ++blocked; }
                try { a->put(0u, (byte) 120); }
                catch (e: k::io::BufferInUseException&) { ++blocked; }
                a->releaseLease();
                if (blocked != 4 || a->position() != 0u || a->limit() != 2u ||
                    a->get(0u) != (byte) 97 || a->hasLease()) return -15;
                writable : k::io::WritableChannel& = first;
                writeA : Future<int>! = writable.writeAsync(a);
                writeB : Future<int>! = second.writeAsync(b);
                if (writeA->get(deadline) != 2 || writeB->get(deadline) != 3) return -3;
                if (a->position() != 2u || b->position() != 3u || a->hasLease() || b->hasLease() ||
                    third.position() != 5L || assigned.position() != 5L) return -4;
                bytes : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(5u);
                read : Future<int>! = assigned.readAsync(bytes, 0L, deadline);
                if (read->get(deadline) != 5 || first.position() != 5L) return -5;
                bytes->flip();
                if (bytes->get(0u) != (byte) 97 || bytes->get(4u) != (byte) 101) return -6;
                size : Future<long>! = first.sizeAsync(deadline);
                if (size->get(deadline) != 5L) return -7;
                truncate : Future<k::io::IoDone>! = third.truncateAsync(3L, deadline);
                truncate->get(deadline);
                if (second.position() != 3L) return -8;
                force : Future<k::io::IoDone>! = assigned.forceAsync(deadline);
                force->get(deadline);
                empty : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(0u);
                zero : Future<int>! = second.readAsync(empty, 0L);
                if (zero->get(deadline) != 0 || second.position() != 3L) return -9;
                eofBuffer : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(1u);
                eof : Future<int>! = second.readAsync(eofBuffer, 3L, deadline);
                if (eof->get(deadline) != 0 || eofBuffer->position() != 0u ||
                    eofBuffer->hasLease() || second.position() != 3L) return -16;
                close : Future<k::io::IoDone>! = first.closeAsync();
                close->get(deadline);
                if (second.isOpen() || third.isOpen() || assigned.isOpen()) return -10;
                second.closeSync();
            }
            syncPath : k::io::file::Path("@FILE@");
            sync : k::io::file::FileChannel = k::io::file::FileChannel::openSync(
                syncPath.nativePath(), k::io::file::OPEN_READ | k::io::file::OPEN_WRITE, deadline);
            buffer : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(3u);
            readable : k::io::ReadableChannel& = sync;
            if (readable.readSync(buffer) != 3 || sync.position() != 3L ||
                sync.sizeSync(deadline) != 3L) return -11;
            sync.position(0L);
            buffer->flip();
            if (sync.writeSync(buffer, 0L, deadline) != 3 || sync.position() != 0L) return -12;
            sync.forceSync(deadline);
            sync.truncateSync(2L, deadline);
            if (sync.sizeSync() != 2L) return -13;
            sync.closeSync();
            return !sync.isOpen() ? 1 : -14;
        }
    )SRC"));
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(test);
    int result = 0;
    REQUIRE_NOTHROW(result = test());
    REQUIRE(result == 1);
    REQUIRE(read_file(fixture.path()) == "ab");
}

TEST_CASE("FileChannel primitives: every callback is dispatched on its selected EventLoop",
          "[libk][io][file-channel][proactor][file-primitives][callback]") {
    FileFixture fixture;
    auto jit = jit_k(fixture.source(R"SRC(
        module __fc_proactor_callbacks__;
        observed : int = 0;
        channel : k::io::file::FileChannel;
        recordCallback(valid: bool) {
            context : k::io::ExecutionContext! = k::io::ExecutionContext::current();
            operation : k::io::IoOperation! = k::io::IoOperation::current();
            if (!valid || context == null || operation == null ||
                !operation->isIoDone() || !operation->isCallbackRunning()) observed = -100;
            else if (observed >= 0) ++observed;
        }
        test() : int throws(ThreadInterruptionException, TimeoutException,
            CancellationException, ExecutionException, IllegalStateException, RejectedExecutionException) {
            deadline : Duration = Duration::ofSeconds(5L);
            loop : k::io::EventLoop;
            lease : ExecutorLease! = loop.acquireLease();
            path : k::io::file::Path("@FILE@");
            openCompletion : k::io::IoCompletion<k::io::file::FileChannel> =
                [](r: k::io::IoResult<k::io::file::FileChannel>) {
                    if (r.hasResult()) channel = r.getResult();
                    recordCallback(r.hasResult());
                };
            open : k::io::IoOperation! = k::io::file::FileChannel::open(path,
                k::io::file::OPEN_READ | k::io::file::OPEN_WRITE |
                k::io::file::OPEN_CREATE | k::io::file::OPEN_TRUNCATE,
                deadline, openCompletion, *lease);
            if (openCompletion != null || open == null ||
                open->waitQuiescent(deadline) != 0 || observed != 0) return -1;
            if (!loop.runOnce(deadline) || observed != 1 || !channel.isOpen()) return -2;
            buffer : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(3u);
            buffer->put((byte) 65); buffer->put((byte) 66); buffer->put((byte) 67); buffer->flip();
            writable : k::io::WritableChannel& = channel;
            writeCompletion : k::io::IoCompletion<int> =
                [](r: k::io::IoResult<int>) { recordCallback(r.hasResult() && r.getResult() == 3); };
            write : k::io::IoOperation! = writable.write(buffer, writeCompletion, *lease);
            if (write->waitQuiescent(deadline) != 0 || observed != 1) return -3;
            loop.runOnce(deadline);
            if (observed != 2 || buffer->position() != 3u || channel.position() != 3L) return -4;
            channel.position(0L);
            buffer->clear();
            readable : k::io::ReadableChannel& = channel;
            readCompletion : k::io::IoCompletion<int> =
                [](r: k::io::IoResult<int>) { recordCallback(r.hasResult() && r.getResult() == 3); };
            read : k::io::IoOperation! = readable.read(buffer, readCompletion, *lease);
            if (read->waitQuiescent(deadline) != 0 || observed != 2) return -5;
            loop.runOnce(deadline);
            if (observed != 3 || buffer->position() != 3u || channel.position() != 3L) return -6;
            buffer->flip();
            positionalWriteCompletion : k::io::IoCompletion<int> =
                [](r: k::io::IoResult<int>) { recordCallback(r.hasResult() && r.getResult() == 3); };
            positionalWrite : k::io::IoOperation! = channel.write(buffer, 0L, deadline,
                positionalWriteCompletion, *lease);
            if (positionalWrite->waitQuiescent(deadline) != 0) return -16;
            loop.runOnce(deadline);
            buffer->clear();
            positionalReadCompletion : k::io::IoCompletion<int> =
                [](r: k::io::IoResult<int>) { recordCallback(r.hasResult() && r.getResult() == 3); };
            positionalRead : k::io::IoOperation! = channel.read(buffer, 0L, deadline,
                positionalReadCompletion, *lease);
            if (positionalRead->waitQuiescent(deadline) != 0) return -17;
            loop.runOnce(deadline);
            if (observed != 5 || channel.position() != 3L) return -18;
            sizeCompletion : k::io::IoCompletion<long> =
                [](r: k::io::IoResult<long>) { recordCallback(r.hasResult() && r.getResult() == 3L); };
            size : k::io::IoOperation! = channel.size(deadline, sizeCompletion, *lease);
            if (size->waitQuiescent(deadline) != 0) return -7;
            loop.runOnce(deadline);
            truncateCompletion : k::io::IoCompletion<k::io::IoDone> =
                [](r: k::io::IoResult<k::io::IoDone>) { recordCallback(r.hasResult()); };
            truncate : k::io::IoOperation! = channel.truncate(1L, deadline, truncateCompletion, *lease);
            if (truncate->waitQuiescent(deadline) != 0 || channel.position() != 1L) return -8;
            loop.runOnce(deadline);
            forceCompletion : k::io::IoCompletion<k::io::IoDone> =
                [](r: k::io::IoResult<k::io::IoDone>) { recordCallback(r.hasResult()); };
            force : k::io::IoOperation! = channel.force(deadline, forceCompletion, *lease);
            if (force->waitQuiescent(deadline) != 0) return -9;
            loop.runOnce(deadline);
            base : k::io::Channel& = channel;
            closeCompletion : k::io::IoCompletion<k::io::IoDone> =
                [](r: k::io::IoResult<k::io::IoDone>) { recordCallback(r.hasResult()); };
            close : k::io::IoOperation! = base.close(closeCompletion, *lease);
            if (close->waitQuiescent(deadline) != 0) return -10;
            loop.runOnce(deadline);
            if (observed != 9 || channel.isOpen()) return -11;
            closedCompletion : k::io::IoCompletion<int> =
                [](r: k::io::IoResult<int>) {
                    recordCallback(r.hasError() && r.getError().kind() == k::io::IoErrorKind::Closed);
                };
            closed : k::io::IoOperation! = readable.read(buffer, closedCompletion, *lease);
            if (closed == null || closed->waitQuiescent(deadline) != 0) return -12;
            loop.runOnce(deadline);
            if (observed != 10 || buffer->hasLease()) return -13;
            failed : Future<int>! = readable.readAsync(buffer);
            try { failed->get(deadline); return -14; }
            catch (e: ExecutionException&) {
                cause : Throwable* = e.getCause();
                if (cause == null || cause->getCode() != 202) return -19;
                error : k::io::ClosedChannelException* = (k::io::ClosedChannelException*) cause;
                if (error == null || error->getErrno() != 9) return -20;
            }
            try { readable.readSync(buffer); return -15; }
            catch (e: k::io::ClosedChannelException&) {
                if (e.getErrno() != 9) return -21;
            }
            return observed;
        }
        testExecutor(direct: bool) : int throws(ThreadInterruptionException, TimeoutException,
            CancellationException, ExecutionException, IllegalStateException, RejectedExecutionException) {
            deadline : Duration = Duration::ofSeconds(5L);
            executor : DirectExecutor;
            lease : ExecutorLease! = null;
            if (direct) lease = executor.acquireLease();
            promise : Promise<int>! = new Promise<int>();
            published : Future<int>! = promise->future();
            path : k::io::file::Path("@FILE@");
            completion : k::io::IoCompletion<k::io::file::FileChannel> =
                [promise](r: k::io::IoResult<k::io::file::FileChannel>) {
                    context : k::io::IoOperation! = k::io::IoOperation::current();
                    if (!r.hasResult() || context == null || !context->isCallbackRunning()) {
                        promise->trySuccess(-1);
                        return;
                    }
                    channel = r.getResult();
                    promise->trySuccess(channel.isOpen() ? 1 : 0);
                };
            open : k::io::IoOperation! = null;
            if (direct)
                open = k::io::file::FileChannel::open(path, k::io::file::OPEN_READ, completion, *lease);
            else
                open = k::io::file::FileChannel::open(path.nativePath(), k::io::file::OPEN_READ, completion);
            if (open == null || promise != null || completion != null || published->get(deadline) != 1 ||
                open->waitQuiescent(deadline) != 0 || channel.sizeSync(deadline) != 1L) return -1;
            closePromise : Promise<int>! = new Promise<int>();
            closed : Future<int>! = closePromise->future();
            closeCompletion : k::io::IoCompletion<k::io::IoDone> =
                [closePromise](r: k::io::IoResult<k::io::IoDone>) {
                    context : k::io::IoOperation! = k::io::IoOperation::current();
                    closePromise->trySuccess(r.hasResult() && context != null &&
                        context->isCallbackRunning() ? 1 : 0);
                };
            close : k::io::IoOperation! = null;
            if (direct) close = channel.close(closeCompletion, *lease);
            else close = channel.close(closeCompletion);
            if (close == null || closePromise != null || closeCompletion != null || closed->get(deadline) != 1 ||
                close->waitQuiescent(deadline) != 0 || channel.isOpen()) return -2;
            return 1;
        }
    )SRC"));
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    auto testExecutor = jit->lookup_symbol<int (*)(bool)>("testExecutor");
    REQUIRE(test);
    REQUIRE(testExecutor);
    int result = 0;
    REQUIRE_NOTHROW(result = test());
    REQUIRE(result == 10);
    for (bool direct : {false, true}) {
        INFO("executor=" << (direct ? "direct" : "system"));
        REQUIRE_NOTHROW(result = testExecutor(direct));
        REQUIRE(result == 1);
    }
    REQUIRE(read_file(fixture.path()) == "A");
}

TEST_CASE("FileChannel primitives: Future publication progresses while EventLoop waits inside a task",
          "[libk][io][file-channel][proactor][file-primitives][future][event-loop]") {
    FileFixture fixture;
    auto jit = jit_k(fixture.source(R"SRC(
        module __fc_proactor_future_executor__;
        callbacks : int = 0;
        taskError : int = 0;
        taskCause : int = 0;
        taskStage : int = 0;
        taskResult : int = 0;
        taskCaught : int = 0;
        hostStage : int = 0;
        transferred : int = -1;
        stepResult : int = -1;
        forceDescriptor : int = -1;
        forceOpen : int = -1;
        forceStage : int = -1;
        forceKind : int = -1;
        forceCode : int = -1;
        forceBytes : long = -1L;
        callbackState : int = -2;
        callbackKind : int = -1;
        callbackCode : int = -1;
        callbackBytes : long = -1L;
        callbackNativeStage : int = -1;
        callbackNativeKind : int = -1;
        callbackNativeCode : int = -1;
        callbackNativeBytes : long = -1L;
        taskErrorCode() : int { return taskError; }
        taskCauseCode() : int { return taskCause; }
        taskProgress() : int { return taskStage; }
        taskPublishedResult() : int { return taskResult; }
        taskCaughtException() : int { return taskCaught; }
        hostProgress() : int { return hostStage; }
        taskTransferred() : int { return transferred; }
        runOnceResult() : int { return stepResult; }
        forceDescriptorValue() : int { return forceDescriptor; }
        forceOpenValue() : int { return forceOpen; }
        forceStageValue() : int { return forceStage; }
        forceKindValue() : int { return forceKind; }
        forceCodeValue() : int { return forceCode; }
        forceBytesValue() : long { return forceBytes; }
        callbackStateValue() : int { return callbackState; }
        callbackKindValue() : int { return callbackKind; }
        callbackCodeValue() : int { return callbackCode; }
        callbackBytesValue() : long { return callbackBytes; }
        callbackNativeStageValue() : int { return callbackNativeStage; }
        callbackNativeKindValue() : int { return callbackNativeKind; }
        callbackNativeCodeValue() : int { return callbackNativeCode; }
        callbackNativeBytesValue() : long { return callbackNativeBytes; }
        recordStep(value : bool) : bool {
            stepResult = value ? 1 : 0;
            return value;
        }
        test() : int throws(ThreadInterruptionException, TimeoutException,
            CancellationException, ExecutionException, IllegalStateException, RejectedExecutionException) {
            deadline : Duration = Duration::ofSeconds(5L);
            loop : k::io::EventLoop;
            loopPtr : k::io::EventLoop* = &loop;
            promise : Promise<int>;
            promisePtr : Promise<int>* = &promise;
            result : Future<int>! = promise.future();
            task : !() = [loopPtr, promisePtr]() {
                taskStage = 1;
                try {
                    deadline : Duration = Duration::ofSeconds(5L);
                    lease : ExecutorLease! = loopPtr->acquireLease();
                    taskStage = 2;
                    path : k::io::file::Path("@FILE@");
                    opened : Future<k::io::file::FileChannel>! = k::io::file::FileChannel::openAsync(
                        path, k::io::file::OPEN_READ | k::io::file::OPEN_WRITE |
                        k::io::file::OPEN_CREATE | k::io::file::OPEN_TRUNCATE, -1L, *lease);
                    taskStage = 3;
                    channel : k::io::file::FileChannel = opened->get(deadline);
                    taskStage = 4;
                    forceDescriptor = channel.nativeDescriptor();
                    forceOpen = channel.isOpen() ? 1 : 0;
                    completion : k::io::IoCompletion<k::io::IoDone> =
                        [](r: k::io::IoResult<k::io::IoDone>) {
                            if (r.hasResult()) callbackState = 1;
                            else if (r.hasError()) callbackState = 0;
                            else callbackState = -1;
                            if (callbackState == 1) ++callbacks;
                            else callbacks = -1;
                            if (r.hasError()) {
                                error : k::io::IoError = r.getError();
                                callbackKind = (int) error.kind();
                                callbackCode = error.getErrno();
                                callbackBytes = error.bytesTransferred();
                            }
                            operation : k::io::IoOperation! = k::io::IoOperation::current();
                            if (operation != null) {
                                nativeError : k::io::IoError = operation->error();
                                callbackNativeStage = (int) operation->stage();
                                callbackNativeKind = (int) nativeError.kind();
                                callbackNativeCode = nativeError.getErrno();
                                callbackNativeBytes = operation->bytesTransferred();
                            }
                        };
                    force : k::io::IoOperation! = channel.force(deadline, completion, *lease);
                    taskStage = 5;
                    if (force->waitQuiescent(deadline) != 0) {
                        taskResult = -2;
                        promisePtr->trySuccess(-2);
                        return;
                    }
                    forceError : k::io::IoError = force->error();
                    forceStage = (int) force->stage();
                    forceKind = (int) forceError.kind();
                    forceCode = forceError.getErrno();
                    forceBytes = force->bytesTransferred();
                    buffer : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(1u);
                    buffer->put((byte) 88); buffer->flip();
                    taskStage = 6;
                    written : Future<int>! = channel.writeAsync(buffer, 0L, -1L, *lease);
                    taskStage = 7;
                    n : int = written->get(deadline);
                    buffer->clear();
                    taskStage = 8;
                    read : Future<int>! = channel.readAsync(buffer, 0L, -1L, *lease);
                    taskStage = 9;
                    count : int = read->get(deadline);
                    valid : bool = n == 1 && count == 1 && buffer->position() == 1u &&
                        !buffer->hasLease() && buffer->get(0u) == (byte) 88;
                    buffer->clear();
                    taskStage = 10;
                    eof : Future<int>! = channel.readAsync(buffer, 1L, -1L, *lease);
                    taskStage = 11;
                    eofCount : int = eof->get(deadline);
                    valid = valid && eofCount == 0 && buffer->position() == 0u && !buffer->hasLease();
                    taskStage = 12;
                    channel.closeSync();
                    taskStage = 13;
                    taskResult = valid && callbacks == 0 ? 1 : 0;
                    promisePtr->trySuccess(valid && callbacks == 0 ? 1 : 0);
                } catch (e: Throwable&) {
                    taskCaught = 1;
                    taskError = e.getCode();
                    cause : Throwable* = e.getCause();
                    taskCause = cause == null ? -999 : cause->getCode();
                    taskResult = -1;
                    promisePtr->trySuccess(-1);
                }
            };
            hostStage = 1;
            loop.execute(task);
            hostStage = 2;
            transferred = task == null ? 1 : 0;
            if (task != null || !recordStep(loop.runOnce(deadline))) return -1;
            hostStage = 3;
            if (result->get(deadline) != 1) return -3;
            hostStage = 4;
            if (callbacks != 0 || !recordStep(loop.runOnce(deadline))) return -2;
            hostStage = 5;
            return callbacks;
        }
    )SRC"), std::getenv("KLANG_F1_DUMP_IR") != nullptr);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    auto taskErrorCode = jit->lookup_symbol<int (*)()>("taskErrorCode");
    auto taskCauseCode = jit->lookup_symbol<int (*)()>("taskCauseCode");
    auto taskProgress = jit->lookup_symbol<int (*)()>("taskProgress");
    auto taskPublishedResult = jit->lookup_symbol<int (*)()>("taskPublishedResult");
    auto taskCaughtException = jit->lookup_symbol<int (*)()>("taskCaughtException");
    auto hostProgress = jit->lookup_symbol<int (*)()>("hostProgress");
    auto taskTransferred = jit->lookup_symbol<int (*)()>("taskTransferred");
    auto runOnceResult = jit->lookup_symbol<int (*)()>("runOnceResult");
    REQUIRE(test);
    REQUIRE(taskErrorCode);
    REQUIRE(taskCauseCode);
    REQUIRE(taskProgress);
    REQUIRE(taskPublishedResult);
    REQUIRE(taskCaughtException);
    REQUIRE(hostProgress);
    REQUIRE(taskTransferred);
    REQUIRE(runOnceResult);
    auto diagnostic = [&](const char* name) {
        auto getter = jit->lookup_symbol<int (*)()>(name);
        REQUIRE(getter);
        return getter();
    };
    auto bytesDiagnostic = [&](const char* name) {
        auto getter = jit->lookup_symbol<long long (*)()>(name);
        REQUIRE(getter);
        return getter();
    };
    int result = 0;
    REQUIRE_NOTHROW(result = test());
    INFO("EventLoop task stage=" << taskProgress() << " result=" << taskPublishedResult()
         << " caught=" << taskCaughtException()
         << " Throwable code=" << taskErrorCode() << " cause code=" << taskCauseCode()
         << " host stage=" << hostProgress() << " task transferred=" << taskTransferred()
         << " runOnce result=" << runOnceResult()
         << " force fd/open=" << diagnostic("forceDescriptorValue") << "/"
         << diagnostic("forceOpenValue")
         << " quiescent force stage/kind/code/bytes=" << diagnostic("forceStageValue") << "/"
         << diagnostic("forceKindValue") << "/" << diagnostic("forceCodeValue") << "/"
         << bytesDiagnostic("forceBytesValue")
         << " callback result-state/kind/code/bytes=" << diagnostic("callbackStateValue") << "/"
         << diagnostic("callbackKindValue") << "/" << diagnostic("callbackCodeValue") << "/"
         << bytesDiagnostic("callbackBytesValue")
         << " callback native stage/kind/code/bytes=" << diagnostic("callbackNativeStageValue") << "/"
         << diagnostic("callbackNativeKindValue") << "/" << diagnostic("callbackNativeCodeValue") << "/"
         << bytesDiagnostic("callbackNativeBytesValue"));
    REQUIRE(result == 1);
    REQUIRE(read_file(fixture.path()) == "X");
}

TEST_CASE("FileChannel primitives: errors remain observable operations and Sync preserves missing-file taxonomy",
          "[libk][io][file-channel][proactor][file-primitives][error]") {
    FileFixture fixture;
    auto jit = jit_k(fixture.source(R"SRC(
        module __fc_proactor_errors__;
        observed : int = 0;
        test() : int throws(ThreadInterruptionException, TimeoutException,
            CancellationException, ExecutionException, IllegalStateException, RejectedExecutionException) {
            deadline : Duration = Duration::ofSeconds(5L);
            loop : k::io::EventLoop;
            lease : ExecutorLease! = loop.acquireLease();
            missingPath : k::io::file::Path("@FILE@-missing");
            path : k::io::file::Path("@FILE@");
            missingCompletion : k::io::IoCompletion<k::io::file::FileChannel> =
                [](r: k::io::IoResult<k::io::file::FileChannel>) {
                    if (r.hasError() && r.getError().kind() == k::io::IoErrorKind::System &&
                        r.getError().getErrno() == 2) ++observed;
                };
            missing : k::io::IoOperation! = k::io::file::FileChannel::open(missingPath,
                k::io::file::OPEN_READ, missingCompletion, *lease);
            if (missing == null || missing->waitQuiescent(deadline) != 0 || observed != 0) return -1;
            loop.runOnce(deadline);
            invalidCompletion : k::io::IoCompletion<k::io::file::FileChannel> =
                [](r: k::io::IoResult<k::io::file::FileChannel>) {
                    if (r.hasError() && r.getError().kind() == k::io::IoErrorKind::InvalidArgument &&
                        r.getError().getErrno() == 22) ++observed;
                };
            invalid : k::io::IoOperation! = k::io::file::FileChannel::open(path,
                k::io::file::OPEN_READ | k::io::file::OPEN_EXCLUSIVE, invalidCompletion, *lease);
            if (invalid == null || invalid->waitQuiescent(deadline) != 0) return -2;
            loop.runOnce(deadline);
            try {
                unused : k::io::file::FileChannel = k::io::file::FileChannel::openSync(missingPath);
                return -3;
            } catch (e: k::io::FileNotFoundException&) {
                if (e.getErrno() != 2) return -6;
                ++observed;
            }
            failed : Future<k::io::file::FileChannel>! =
                k::io::file::FileChannel::openAsync(missingPath.nativePath(), k::io::file::OPEN_READ);
            try { unused : k::io::file::FileChannel = failed->get(deadline); return -4; }
            catch (e: ExecutionException&) {
                cause : Throwable* = e.getCause();
                if (cause == null || cause->getCode() != 201) return -5;
                error : k::io::FileNotFoundException* = (k::io::FileNotFoundException*) cause;
                if (error == null || error->getErrno() != 2) return -7;
                ++observed;
            }
            cold : k::io::file::FileChannel;
            cold.closeSync();
            close : Future<k::io::IoDone>! = cold.closeAsync();
            close->get(deadline);
            return observed;
        }
    )SRC"));
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(test);
    int result = 0;
    REQUIRE_NOTHROW(result = test());
    REQUIRE(result == 4);
    REQUIRE_FALSE(std::filesystem::exists(fixture.path()));
}

TEST_CASE("FileChannel: opening a missing file raises FileNotFoundException",
          "[libk][io][file-channel]") {
    std::filesystem::remove("/tmp/klang_fc_absent.bin");
    auto jit = jit_k(R"SRC(
        module __fc_missing__;
        test() : int {
            p : k::io::file::Path("/tmp/klang_fc_absent.bin");
            try {
                c : k::io::file::FileChannel! = k::io::file::FileChannel::open(p);
                delete c;
                return 0;
            } catch (e: k::io::FileNotFoundException&) {
                return 1;
            } catch (e2: k::io::IOException&) {
                return 2;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("FileChannel: create, write and read back", "[libk][io][file-channel]") {
    const std::string path = "/tmp/klang_fc_roundtrip.bin";
    std::filesystem::remove(path);
    auto jit = jit_k(R"SRC(
        module __fc_roundtrip__;
        test() : int {
            p : k::io::file::Path("/tmp/klang_fc_roundtrip.bin");
            res : int = 0;

            opts : int = k::io::file::OPEN_WRITE | k::io::file::OPEN_CREATE | k::io::file::OPEN_TRUNCATE;
            out : k::io::file::FileChannel! = k::io::file::FileChannel::open(p, opts);
            src : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(5u);
            src->put((byte) 75);
            src->put((byte) 76);
            src->put((byte) 65);
            src->put((byte) 78);
            src->put((byte) 71);
            src->flip();
            if (out->write(src) == 5) { ++res; }
            if (out->position() == 5L) { res += 2; }
            out->force();
            if (out->size() == 5L) { res += 4; }
            out->close();
            if (out->isOpen() == false) { res += 8; }
            delete out;
            delete src;

            in : k::io::file::FileChannel! = k::io::file::FileChannel::open(p);
            dst : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(16u);
            n : int = in->read(dst);
            if (n == 5) { res += 16; }
            dst->flip();
            if (dst->get() == (byte) 75) { res += 32; }
            if (dst->get(4u) == (byte) 71) { res += 64; }
            if (in->read(dst, 5L) == 0) { res += 128; }
            in->close();
            delete in;
            delete dst;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 255);
    REQUIRE(read_file(path) == "KLANG");
    std::filesystem::remove(path);
}

TEST_CASE("FileChannel: positional access leaves the position untouched",
          "[libk][io][file-channel]") {
    const std::string path = "/tmp/klang_fc_positional.bin";
    write_file(path, "0123456789");
    auto jit = jit_k(R"SRC(
        module __fc_positional__;
        test() : int {
            p : k::io::file::Path("/tmp/klang_fc_positional.bin");
            res : int = 0;
            c : k::io::file::FileChannel! = k::io::file::FileChannel::open(p, k::io::file::OPEN_READ);

            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(3u);
            if (c->read(b, 4L) == 3) { ++res; }
            if (c->position() == 0L) { res += 2; }
            b->flip();
            if (b->get() == (byte) 52) { res += 4; }
            if (b->get() == (byte) 53) { res += 8; }

            b->clear();
            if (c->read(b, 9L) == 1) { res += 16; }

            b->clear();
            if (c->read(b, 10L) == 0) { res += 32; }

            c->close();
            delete c;
            delete b;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 63);
    REQUIRE(read_file(path) == "0123456789");
    std::filesystem::remove(path);
}

TEST_CASE("FileChannel: readFully raises EndOfStreamException past the end",
          "[libk][io][file-channel]") {
    const std::string path = "/tmp/klang_fc_readfully.bin";
    write_file(path, "abcd");
    auto jit = jit_k(R"SRC(
        module __fc_readfully__;
        test() : int {
            p : k::io::file::Path("/tmp/klang_fc_readfully.bin");
            res : int = 0;
            c : k::io::file::FileChannel! = k::io::file::FileChannel::open(p, k::io::file::OPEN_READ);

            small : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(4u);
            c->readFullySync(small, 0L);
            if (small->hasRemaining() == false) { ++res; }
            small->flip();
            if (small->get(3u) == (byte) 100) { res += 2; }

            big : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(8u);
            try {
                c->readFullySync(big, 0L);
            } catch (e: k::io::EndOfStreamException&) {
                res += 4;
                if (e.getBytesTransferred() == 4L) { res += 8; }
            }

            c->close();
            delete c;
            delete small;
            delete big;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 15);
    std::filesystem::remove(path);
}

TEST_CASE("FileChannel: writeFully transfers every remaining byte",
          "[libk][io][file-channel]") {
    const std::string path = "/tmp/klang_fc_writefully.bin";
    std::filesystem::remove(path);
    auto jit = jit_k(R"SRC(
        module __fc_writefully__;
        test() : int {
            p : k::io::file::Path("/tmp/klang_fc_writefully.bin");
            res : int = 0;
            opts : int = k::io::file::OPEN_WRITE | k::io::file::OPEN_CREATE | k::io::file::OPEN_TRUNCATE;
            c : k::io::file::FileChannel! = k::io::file::FileChannel::open(p, opts);

            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(64u);
            i : unsigned int = 0u;
            while (i < 64u) {
                b->put((byte) 65);
                ++i;
            }
            b->flip();
            c->writeFullySync(b, 0L);
            if (b->hasRemaining() == false) { ++res; }
            c->force();
            if (c->size() == 64L) { res += 2; }

            c->truncate(10L);
            if (c->size() == 10L) { res += 4; }

            c->close();
            delete c;
            delete b;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 7);
    REQUIRE(read_file(path) == "AAAAAAAAAA");
    std::filesystem::remove(path);
}

TEST_CASE("FileChannel: a closed channel raises ClosedChannelException",
          "[libk][io][file-channel]") {
    const std::string path = "/tmp/klang_fc_closed.bin";
    write_file(path, "xyz");
    auto jit = jit_k(R"SRC(
        module __fc_closed__;
        test() : int {
            p : k::io::file::Path("/tmp/klang_fc_closed.bin");
            res : int = 0;
            c : k::io::file::FileChannel! = k::io::file::FileChannel::open(p, k::io::file::OPEN_READ);
            if (c->isOpen()) { ++res; }
            if (c->nativeDescriptor() >= 0) { res += 2; }
            c->close();
            if (c->nativeDescriptor() == -1) { res += 4; }

            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(4u);
            try {
                c->read(b, 0L);
            } catch (e: k::io::ClosedChannelException&) {
                res += 8;
            }
            try {
                c->size();
            } catch (e2: k::io::ClosedChannelException&) {
                res += 16;
            }
            // Closing twice must stay harmless.
            c->close();
            res += 32;

            delete c;
            delete b;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 63);
    std::filesystem::remove(path);
}

TEST_CASE("FileChannel: append mode writes at the end", "[libk][io][file-channel]") {
    const std::string path = "/tmp/klang_fc_append.bin";
    write_file(path, "head-");
    auto jit = jit_k(R"SRC(
        module __fc_append__;
        test() : int {
            p : k::io::file::Path("/tmp/klang_fc_append.bin");
            opts : int = k::io::file::OPEN_WRITE | k::io::file::OPEN_APPEND;
            c : k::io::file::FileChannel! = k::io::file::FileChannel::open(p, opts);
            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(4u);
            b->put((byte) 116);
            b->put((byte) 97);
            b->put((byte) 105);
            b->put((byte) 108);
            b->flip();
            c->writeFullySync(b, c->size());
            c->force();
            c->close();
            delete c;
            delete b;
            return 1;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
    REQUIRE(read_file(path) == "head-tail");
    std::filesystem::remove(path);
}

TEST_CASE("FileChannel: exclusive create fails on an existing file",
          "[libk][io][file-channel]") {
    const std::string path = "/tmp/klang_fc_excl.bin";
    write_file(path, "here");
    auto jit = jit_k(R"SRC(
        module __fc_excl__;
        test() : int {
            p : k::io::file::Path("/tmp/klang_fc_excl.bin");
            opts : int = k::io::file::OPEN_WRITE | k::io::file::OPEN_CREATE | k::io::file::OPEN_EXCLUSIVE;
            try {
                c : k::io::file::FileChannel! = k::io::file::FileChannel::open(p, opts);
                delete c;
                return 0;
            } catch (e: k::io::IOException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
    REQUIRE(read_file(path) == "here");
    std::filesystem::remove(path);
}

TEST_CASE("FileChannel: concurrent positional reads from several threads",
          "[libk][io][file-channel]") {
    const std::string path = "/tmp/klang_fc_concurrent.bin";
    write_file(path, std::string(4096, 'Z'));
    auto jit = jit_k(R"SRC(
        module __fc_concurrent__;

        class Reader : public k::Runnable {
            _chan : k::io::file::FileChannel*;
            _from : long;
            _ok   : int;
        public:
            Reader(chan: k::io::file::FileChannel*, from: long)
                : _chan(chan), _from(from), _ok(0) {}
            override run() : void {
                b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(512u);
                _chan->readFullySync(b, _from);
                b->flip();
                good : int = 1;
                i : unsigned int = 0u;
                while (i < 512u) {
                    if (b->get(i) != (byte) 90) { good = 0; }
                    ++i;
                }
                _ok = good;
                delete b;
            }
            const ok() : int { return _ok; }
        }

        test() : int throws(k::ThreadInterruptionException) {
            p : k::io::file::Path("/tmp/klang_fc_concurrent.bin");
            c : k::io::file::FileChannel! = k::io::file::FileChannel::open(p, k::io::file::OPEN_READ);

            r0 : Reader! = new Reader(c, 0L);
            r1 : Reader! = new Reader(c, 1024L);
            r2 : Reader! = new Reader(c, 2048L);
            r3 : Reader! = new Reader(c, 3072L);

            t0 : k::Thread! = new k::Thread(r0);
            t1 : k::Thread! = new k::Thread(r1);
            t2 : k::Thread! = new k::Thread(r2);
            t3 : k::Thread! = new k::Thread(r3);
            t0->start();
            t1->start();
            t2->start();
            t3->start();
            t0->join();
            t1->join();
            t2->join();
            t3->join();

            res : int = r0->ok() + r1->ok() + r2->ok() + r3->ok();
            // The channel position must be untouched by positional reads.
            if (c->position() == 0L) { res += 8; }

            delete t0; delete t1; delete t2; delete t3;
            delete r0; delete r1; delete r2; delete r3;
            c->close();
            delete c;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 12);
    std::filesystem::remove(path);
}
