/*
 * Proactor stream listeners. Copyright 2026 Emilien Kia.
 * Licensed under the Apache License, Version 2.0.
 */
module k;
namespace io {

/** Copyable listener. Bind/listen are immediate; accepts and close are proactive. */
public class ServerSocket : public Channel {
private:
    _native : NativeIoHandle*;
    @ffi::Extern("C") private static __k_io_stream_bind(endpoint: const char*, port: int,
        unixSocket: int, backlog: int) : int;
    @ffi::Extern("C") private static __k_io_stream_handle_create(fd: int) : NativeIoHandle*;
    @ffi::Extern("C") private static __k_io_stream_configure(fd: int) : int;
    @ffi::Extern("C") private static __k_io_stream_local_port(handle: NativeIoHandle*) : int;
    @ffi::Extern("C") private static __k_io_handle_retain(handle: NativeIoHandle*);
    @ffi::Extern("C") private static __k_io_handle_release(handle: NativeIoHandle*);
    @ffi::Extern("C") private static __k_io_handle_is_open(handle: const NativeIoHandle*) : int;
    static decodeDone(value: long) : IoDone { return IoDone(); }
    ServerSocket(handle: NativeIoHandle*) : _native(handle) {}
    prepareAccept(completion: IoCompletion<SocketChannel>, executor: ExecutorLease&) : IoOperation! {
        return prepareOperation<SocketChannel>(5, _native, null, 0, 0L, 0,
            completion, executor, SocketChannel::decodeSocket, SocketChannel::claimSocket);
    }
    static bindEndpoint(endpoint: const char*, port: int, unixSocket: int, backlog: int) : ServerSocket! {
        fd : int = __k_io_stream_bind(endpoint, port, unixSocket, backlog);
        if (fd < 0) {
            error : IoError = IoError::fromErrno(-fd, 0L);
            error.raiseSync();
        }
        handle : NativeIoHandle* = __k_io_stream_handle_create(fd);
        if (handle == null) throw OutOfMemory();
        try {
            return new ServerSocket(handle);
        } catch (e: Throwable&) {
            __k_io_handle_release(handle);
            throw;
        }
    }

public:
    ServerSocket() : _native(null) {}
    ServerSocket(fd: int) : _native(null) {
        if (fd >= 0) {
            rc : int = __k_io_stream_configure(fd);
            if (rc < 0) {
                error : IoError = IoError::fromErrno(-rc, 0L);
                error.raiseSync();
            }
            _native = __k_io_stream_handle_create(fd);
            if (_native == null) throw OutOfMemory();
        }
    }
    ServerSocket(other: const ServerSocket&) : _native(other._native) { __k_io_handle_retain(_native); }
    operator =(other: const ServerSocket&) : ServerSocket& {
        handle : NativeIoHandle* = other._native;
        __k_io_handle_retain(handle);
        __k_io_handle_release(_native);
        _native = handle;
        return this;
    }
    ~ServerSocket() { __k_io_handle_release(_native); _native = null; }

    static bind(address: const NetworkAddress&, backlog: int) : ServerSocket! {
        return ServerSocket::bindEndpoint(address.nativeHost(), address.port(), 0, backlog);
    }
    static bind(path: const file::Path&, backlog: int) : ServerSocket! {
        return ServerSocket::bindEndpoint(path.nativePath(), 0, 1, backlog);
    }
    static bind(address: const NetworkAddress&) : ServerSocket! { return ServerSocket::bind(address, 64); }
    static bind(path: const file::Path&) : ServerSocket! { return ServerSocket::bind(path, 64); }
    static bind(path: const char[], backlog: int) : ServerSocket! {
        address : file::Path(path);
        return ServerSocket::bind(address, backlog);
    }
    static bind(path: const char[]) : ServerSocket! { return ServerSocket::bind(path, 64); }
    const localPort() : int {
        port : int = __k_io_stream_local_port(_native);
        if (port < 0) {
            error : IoError = IoError::fromErrno(-port, 0L);
            error.raiseSync();
        }
        return port;
    }
    override const isOpen() : bool { return __k_io_handle_is_open(_native) != 0; }

    accept(timeoutNanos: long, completion: IoCompletion<SocketChannel>, executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = prepareAccept(completion, executor);
        submitOperation(*operation, timeoutNanos);
        return operation;
    }
    accept(completion: IoCompletion<SocketChannel>, executor: ExecutorLease&) : IoOperation! {
        return accept(IO_NO_TIMEOUT, completion, executor);
    }
    accept(timeoutNanos: long, completion: IoCompletion<SocketChannel>) : IoOperation! {
        executor : ExecutorLease! = ExecutorLease::system();
        return accept(timeoutNanos, completion, *executor);
    }
    accept(completion: IoCompletion<SocketChannel>) : IoOperation! { return accept(IO_NO_TIMEOUT, completion); }
    accept(timeout: const Duration&, completion: IoCompletion<SocketChannel>, executor: ExecutorLease&) : IoOperation! {
        return accept(timeout.toNanos(), completion, executor);
    }
    accept(timeout: const Duration&, completion: IoCompletion<SocketChannel>) : IoOperation! {
        return accept(timeout.toNanos(), completion);
    }
    acceptAsync(timeoutNanos: long, executor: ExecutorLease&) : Future<SocketChannel>! {
        operation : IoOperation! = prepareAccept(null, executor);
        future : Future<SocketChannel>! = futureFor<SocketChannel>(*operation,
            SocketChannel::decodeSocket, SocketChannel::claimSocket);
        submitOperation(*operation, timeoutNanos);
        return future;
    }
    acceptAsync(timeoutNanos: long) : Future<SocketChannel>! {
        executor : ExecutorLease! = ExecutorLease::system();
        return acceptAsync(timeoutNanos, *executor);
    }
    acceptAsync() : Future<SocketChannel>! { return acceptAsync(IO_NO_TIMEOUT); }
    acceptAsync(executor: ExecutorLease&) : Future<SocketChannel>! { return acceptAsync(IO_NO_TIMEOUT, executor); }
    acceptAsync(timeout: const Duration&, executor: ExecutorLease&) : Future<SocketChannel>! {
        return acceptAsync(timeout.toNanos(), executor);
    }
    acceptAsync(timeout: const Duration&) : Future<SocketChannel>! { return acceptAsync(timeout.toNanos()); }
    acceptSync(timeoutNanos: long) : SocketChannel {
        checkIoSyncThread();
        executor : ExecutorLease! = ExecutorLease::system();
        operation : IoOperation! = prepareAccept(null, *executor);
        future : Future<SocketChannel>! = futureFor<SocketChannel>(*operation,
            SocketChannel::decodeSocket, SocketChannel::claimSocket);
        submitOperation(*operation, timeoutNanos);
        return getIoSync<SocketChannel>(*future, *operation);
    }
    acceptSync() : SocketChannel { return acceptSync(IO_NO_TIMEOUT); }
    acceptSync(timeout: const Duration&) : SocketChannel { return acceptSync(timeout.toNanos()); }

    override close(completion: IoCompletion<IoDone>, executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = prepareOperation<IoDone>(7, _native, null, 0, 0L, 0,
            completion, executor, ServerSocket::decodeDone);
        submitOperation(*operation, IO_NO_TIMEOUT);
        return operation;
    }
    override close(completion: IoCompletion<IoDone>) : IoOperation! {
        executor : ExecutorLease! = ExecutorLease::system();
        return close(completion, *executor);
    }
    override closeAsync() : Future<IoDone>! {
        executor : ExecutorLease! = ExecutorLease::system();
        return closeAsync(*executor);
    }
    closeAsync(executor: ExecutorLease&) : Future<IoDone>! {
        operation : IoOperation! = prepareOperation<IoDone>(7, _native, null, 0, 0L, 0,
            null, executor, ServerSocket::decodeDone);
        future : Future<IoDone>! = futureFor<IoDone>(*operation, ServerSocket::decodeDone);
        submitOperation(*operation, IO_NO_TIMEOUT);
        return future;
    }
    override closeSync() {
        checkIoSyncThread();
        executor : ExecutorLease! = ExecutorLease::system();
        operation : IoOperation! = prepareOperation<IoDone>(7, _native, null, 0, 0L, 0,
            null, *executor, ServerSocket::decodeDone);
        future : Future<IoDone>! = futureFor<IoDone>(*operation, ServerSocket::decodeDone);
        submitOperation(*operation, IO_NO_TIMEOUT);
        getIoSync<IoDone>(*future, *operation);
    }
    override close() { closeSync(); }
    /** Transitional owner-returning aliases. */
    accept(timeoutNanos: long) : SocketChannel! { return new SocketChannel(acceptSync(timeoutNanos)); }
    accept() : SocketChannel! { return accept(IO_NO_TIMEOUT); }
}
}
