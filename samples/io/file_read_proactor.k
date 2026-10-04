/*
 * Callback-only open/read/close chain on a serial EventLoop.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
module io_samples;

struct FileReadState {
    loop : k::io::EventLoop!;
    lease : ExecutorLease!;
    operation : k::io::IoOperation!;
    channel : k::io::file::FileChannel;
    buffer : k::io::ByteBuffer!;
    scratch : byte[4096];
    deadline : Duration = Duration::ofSeconds(30L);
    done : bool = false;
    closing : bool = false;
    status : int = 0;
}

fileClose(state: FileReadState*) {
    if (state->closing) return;
    state->closing = true;
    try {
        if (!state->channel.isOpen()) {
            state->done = true;
            return;
        }
        completion : k::io::IoCompletion<k::io::IoDone> =
            [state](result: k::io::IoResult<k::io::IoDone>) {
                try {
                    if (result.hasError()) {
                        state->status = 1;
                        reportIoError(result.getError());
                    }
                } catch (error: Throwable&) {
                    state->status = failureStatus(error);
                }
                state->done = true;
            };
        state->operation = state->channel.close(completion, *state->lease);
    } catch (error: Throwable&) {
        state->status = failureStatus(error);
        state->done = true;
    }
}

fileFail(state: FileReadState*, error: Throwable&) {
    state->status = failureStatus(error);
    fileClose(state);
}

fileRead(state: FileReadState*) throws(Throwable) {
    completion : k::io::IoCompletion<int> = [state](result: k::io::IoResult<int>) {
        try {
            if (result.hasError()) {
                state->status = 1;
                reportIoError(result.getError());
                fileClose(state);
                return;
            }
            count : int = result.getResult();
            if (count == 0) {
                k::io::stdoutBytes.flush();
                fileClose(state);
                return;
            }
            emitBytes(state->buffer, state->scratch, count);
            state->buffer->clear();
            fileRead(state);
        } catch (error: Throwable&) {
            fileFail(state, error);
        }
    };
    state->operation = state->channel.read(state->buffer, state->deadline, completion, *state->lease);
}

class Application : public k::Application {
    private state : FileReadState;

    public main(args: const String[]) : int {
        // Captures point to an Application field, not a main-local resource.
        current : FileReadState* = &state;
        try {
            if (args.size != 2 || args[1].size() == 0u) return usage(args[0], "filepath");
            path : k::io::file::Path(args[1]);
            state.loop = new k::io::EventLoop();
            state.lease = state.loop->acquireLease();
            state.buffer = k::io::ByteBuffer::allocateDirect(4096u);
            completion : k::io::IoCompletion<k::io::file::FileChannel> =
                [current](result: k::io::IoResult<k::io::file::FileChannel>) {
                    try {
                        if (result.hasError()) {
                            current->status = 1;
                            reportIoError(result.getError());
                            fileClose(current);
                            return;
                        }
                        current->channel = result.getResult();
                        fileRead(current);
                    } catch (error: Throwable&) {
                        fileFail(current, error);
                    }
                };
            state.operation = k::io::file::FileChannel::open(
                path, k::io::file::OPEN_READ, state.deadline, completion, *state.lease);
        } catch (error: Throwable&) {
            fileFail(current, error);
        }
        try {
            while (!state.done) state.loop->runOnce();
        } catch (error: Throwable&) {
            state.status = failureStatus(error);
            fileClose(current);
        }
        return state.status;
    }
}
