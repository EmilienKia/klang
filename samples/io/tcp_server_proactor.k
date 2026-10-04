/*
 * Callback-only accept/readFully/writeFully/close chain.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
module io_samples;

struct ServerState {
    loop : k::io::EventLoop!;
    lease : ExecutorLease!;
    operation : k::io::IoOperation!;
    server : k::io::ServerSocket!;
    peer : k::io::SocketChannel;
    buffer : k::io::ByteBuffer!;
    deadline : Duration = Duration::ofSeconds(30L);
    done : bool = false;
    closingPeer : bool = false;
    closingServer : bool = false;
    status : int = 0;
}

serverCloseListener(state: ServerState*) {
    if (state->closingServer) return;
    state->closingServer = true;
    try {
        if (state->server == null || !state->server->isOpen()) {
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
        state->operation = state->server->close(completion, *state->lease);
    } catch (error: Throwable&) {
        state->status = failureStatus(error);
        state->done = true;
    }
}

serverClosePeer(state: ServerState*) {
    if (state->closingPeer) return;
    state->closingPeer = true;
    try {
        if (!state->peer.isOpen()) {
            serverCloseListener(state);
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
                serverCloseListener(state);
            };
        state->operation = state->peer.close(completion, *state->lease);
    } catch (error: Throwable&) {
        state->status = failureStatus(error);
        serverCloseListener(state);
    }
}

serverFail(state: ServerState*, error: Throwable&) {
    state->status = failureStatus(error);
    serverClosePeer(state);
}

serverWrite(state: ServerState*) throws(Throwable) {
    state->buffer->flip();
    completion : k::io::IoCompletion<int> = [state](result: k::io::IoResult<int>) {
        try {
            if (result.hasError()) {
                state->status = 1;
                reportIoError(result.getError());
            } else if (result.getResult() != 4) {
                throw IllegalStateException();
            }
            serverClosePeer(state);
        } catch (error: Throwable&) {
            serverFail(state, error);
        }
    };
    state->operation = state->peer.writeFully(
        state->buffer, state->deadline, completion, *state->lease);
}

serverRead(state: ServerState*) throws(Throwable) {
    completion : k::io::IoCompletion<int> = [state](result: k::io::IoResult<int>) {
        try {
            if (result.hasError()) {
                state->status = 1;
                reportIoError(result.getError());
                serverClosePeer(state);
                return;
            }
            checkPing(state->buffer, result.getResult());
            serverWrite(state);
        } catch (error: Throwable&) {
            serverFail(state, error);
        }
    };
    state->operation = state->peer.readFully(
        state->buffer, state->deadline, completion, *state->lease);
}

class Application : public k::Application {
    private state : ServerState;

    public main(args: const String[]) : int {
        current : ServerState* = &state;
        try {
            if (args.size > 2) return usage(args[0], "[port] (port: 0..65535)");
            port : int = 0;
            if (args.size == 2) port = parsePort(args[1], 0);
            if (port < 0) return usage(args[0], "[port] (port: 0..65535)");
            address : k::io::NetworkAddress("127.0.0.1", port);
            state.loop = new k::io::EventLoop();
            state.lease = state.loop->acquireLease();
            state.buffer = k::io::ByteBuffer::allocateDirect(4u);
            state.server = k::io::ServerSocket::bind(address);
            announcePort(state.server->localPort());
            completion : k::io::IoCompletion<k::io::SocketChannel> =
                [current](result: k::io::IoResult<k::io::SocketChannel>) {
                    try {
                        if (result.hasError()) {
                            current->status = 1;
                            reportIoError(result.getError());
                            serverClosePeer(current);
                            return;
                        }
                        current->peer = result.getResult();
                        serverRead(current);
                    } catch (error: Throwable&) {
                        serverFail(current, error);
                    }
                };
            state.operation = state.server->accept(
                state.deadline, completion, *state.lease);
        } catch (error: Throwable&) {
            serverFail(current, error);
        }
        try {
            while (!state.done) state.loop->runOnce();
        } catch (error: Throwable&) {
            state.status = failureStatus(error);
            serverClosePeer(current);
            serverCloseListener(current);
        }
        return state.status;
    }
}
