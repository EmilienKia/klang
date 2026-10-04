/*
 * Callback-only connect/writeFully/readFully/close chain.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
module io_samples;

struct ClientState {
    loop : k::io::EventLoop!;
    lease : ExecutorLease!;
    operation : k::io::IoOperation!;
    channel : k::io::SocketChannel;
    buffer : k::io::ByteBuffer!;
    deadline : Duration = Duration::ofSeconds(30L);
    done : bool = false;
    closing : bool = false;
    status : int = 0;
}

clientClose(state: ClientState*) {
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
                    if (state->status == 0) announcePing();
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

clientFail(state: ClientState*, error: Throwable&) {
    state->status = failureStatus(error);
    clientClose(state);
}

clientRead(state: ClientState*) throws(Throwable) {
    state->buffer->clear();
    completion : k::io::IoCompletion<int> = [state](result: k::io::IoResult<int>) {
        try {
            if (result.hasError()) {
                state->status = 1;
                reportIoError(result.getError());
            } else {
                checkPing(state->buffer, result.getResult());
            }
            clientClose(state);
        } catch (error: Throwable&) {
            clientFail(state, error);
        }
    };
    state->operation = state->channel.readFully(
        state->buffer, state->deadline, completion, *state->lease);
}

clientWrite(state: ClientState*) throws(Throwable) {
    fillPing(state->buffer);
    completion : k::io::IoCompletion<int> = [state](result: k::io::IoResult<int>) {
        try {
            if (result.hasError()) {
                state->status = 1;
                reportIoError(result.getError());
                clientClose(state);
                return;
            }
            if (result.getResult() != 4) throw IllegalStateException();
            clientRead(state);
        } catch (error: Throwable&) {
            clientFail(state, error);
        }
    };
    state->operation = state->channel.writeFully(
        state->buffer, state->deadline, completion, *state->lease);
}

class Application : public k::Application {
    private state : ClientState;

    public main(args: const String[]) : int {
        current : ClientState* = &state;
        try {
            if (args.size < 2 || args.size > 3) return usage(args[0], "port [host]");
            port : int = parsePort(args[1], 1);
            if (port < 0 || (args.size == 3 && args[2].size() == 0u))
                return usage(args[0], "port [host] (port: 1..65535)");
            address : k::io::NetworkAddress = clientAddress(args, port);
            state.loop = new k::io::EventLoop();
            state.lease = state.loop->acquireLease();
            state.buffer = k::io::ByteBuffer::allocateDirect(4u);
            completion : k::io::IoCompletion<k::io::SocketChannel> =
                [current](result: k::io::IoResult<k::io::SocketChannel>) {
                    try {
                        if (result.hasError()) {
                            current->status = 1;
                            reportIoError(result.getError());
                            clientClose(current);
                            return;
                        }
                        current->channel = result.getResult();
                        clientWrite(current);
                    } catch (error: Throwable&) {
                        clientFail(current, error);
                    }
                };
            state.operation = k::io::SocketChannel::connect(
                address, state.deadline, completion, *state.lease);
        } catch (error: Throwable&) {
            clientFail(current, error);
        }
        try {
            while (!state.done) state.loop->runOnce();
        } catch (error: Throwable&) {
            state.status = failureStatus(error);
            clientClose(current);
        }
        return state.status;
    }
}
