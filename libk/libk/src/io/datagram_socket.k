/*
 * Proactor IPv4 and Unix datagrams. Copyright 2026 Emilien Kia.
 * Licensed under the Apache License, Version 2.0.
 */
module k;
namespace io {

/** One consumed packet. bytesTransferred counts copied bytes, not packet length. */
public class DatagramReceive {
private:
    _bytes : int;
    _source : NetworkAddress;
    _truncated : bool;
    _length : long;
    @ffi::Extern("C") private static __k_io_datagram_source(value: long) : NativeNetworkAddress*;
    @ffi::Extern("C") private static __k_io_datagram_copied(value: long) : int;
    @ffi::Extern("C") private static __k_io_datagram_truncated(value: long) : int;
    @ffi::Extern("C") private static __k_io_datagram_length(value: long) : long;
    DatagramReceive(bytes: int, source: const NetworkAddress&, truncated: bool, length: long)
        : _bytes(bytes), _source(source), _truncated(truncated), _length(length) {}
    static decode(value: long) : DatagramReceive {
        source : NetworkAddress(__k_io_datagram_source(value));
        return DatagramReceive(__k_io_datagram_copied(value), source,
            __k_io_datagram_truncated(value) != 0, __k_io_datagram_length(value));
    }
    friend DatagramSocket;
public:
    DatagramReceive() : _bytes(0), _source(), _truncated(false), _length(-1L) {}
    DatagramReceive(other: const DatagramReceive&)
        : _bytes(other._bytes), _source(other._source),
          _truncated(other._truncated), _length(other._length) {}
    operator =(other: const DatagramReceive&) : DatagramReceive& {
        _bytes = other._bytes; _source = other._source;
        _truncated = other._truncated; _length = other._length;
        return this;
    }
    const bytesTransferred() : int { return _bytes; }
    const source() : NetworkAddress { return _source; }
    const isTruncated() : bool { return _truncated; }
    /** Full packet length from MSG_TRUNC, or -1 if unavailable. */
    const datagramLength() : long { return _length; }
}

/** Copyable value sharing one fd, one receive gate, and one send/connect gate. */
public class DatagramSocket : public Channel {
private:
    _native : NativeIoHandle*;
    @ffi::Extern("C") private static __k_io_datagram_bind(endpoint: const char*, port: int,
        unixSocket: int) : NativeIoHandle*;
    @ffi::Extern("C") private static __k_io_datagram_handle_create(fd: int) : NativeIoHandle*;
    @ffi::Extern("C") private static __k_io_datagram_bound_address(handle: NativeIoHandle*) : NativeNetworkAddress*;
    @ffi::Extern("C") private static __k_io_op_set_datagram_endpoint(operation: NativeIoOperation*,
        address: const NativeNetworkAddress*) : int;
    @ffi::Extern("C") private static __k_io_op_set_datagram_message(operation: NativeIoOperation*) : int;
    @ffi::Extern("C") private static __k_io_handle_retain(handle: NativeIoHandle*);
    @ffi::Extern("C") private static __k_io_handle_release(handle: NativeIoHandle*);
    @ffi::Extern("C") private static __k_io_handle_is_open(handle: const NativeIoHandle*) : int;
    @ffi::Extern("C") private static __k_io_handle_fd(handle: const NativeIoHandle*) : int;
    @ffi::Extern("C") private static __k_io_setup_errno() : int;
    DatagramSocket(handle: NativeIoHandle*) : _native(handle) {}
    static decodeDone(value: long) : IoDone { return IoDone(); }
    static decodeCount(value: long) : int { return (int) value; }
    static bindEndpoint(endpoint: const char*, port: int, unixSocket: int) : DatagramSocket! {
        handle : NativeIoHandle* = __k_io_datagram_bind(endpoint, port, unixSocket);
        if (handle == null) {
            error : IoError = IoError::fromErrno(__k_io_setup_errno(), 0L);
            error.raiseSync();
        }
        try { return new DatagramSocket(handle); }
        catch (e: Throwable&) { __k_io_handle_release(handle); throw; }
    }
    prepareConnect(remote: const NetworkAddress&, completion: IoCompletion<IoDone>,
        executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = prepareOperation<IoDone>(6, _native, null, 0, 0L, 0,
            completion, executor, DatagramSocket::decodeDone);
        setOperationError(*operation, __k_io_op_set_datagram_endpoint(operation->native(), remote.native()));
        return operation;
    }
    prepareSend(buffer: ByteBuffer+, remote: NativeNetworkAddress*,
        completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        remaining : unsigned int = buffer->remaining();
        length : int = remaining <= 2147483647u ? (int) remaining : 0;
        operation : IoOperation! = prepareOperation<int>(15, _native, buffer->nativeHandle(),
            length, 0L, 0, completion, executor, DatagramSocket::decodeCount);
        if (remaining > 2147483647u) setOperationError(*operation, 22);
        setOperationError(*operation, __k_io_op_set_datagram_message(operation->native()));
        if (remote != null)
            setOperationError(*operation, __k_io_op_set_datagram_endpoint(operation->native(), remote));
        return operation;
    }
    prepareReceive(buffer: ByteBuffer+, completion: IoCompletion<DatagramReceive>,
        executor: ExecutorLease&) : IoOperation! {
        remaining : unsigned int = buffer->remaining();
        length : int = remaining <= 2147483647u ? (int) remaining : 0;
        operation : IoOperation! = prepareOperation<DatagramReceive>(14, _native, buffer->nativeHandle(),
            length, 0L, 0, completion, executor, DatagramReceive::decode);
        if (remaining > 2147483647u) setOperationError(*operation, 22);
        setOperationError(*operation, __k_io_op_set_datagram_message(operation->native()));
        return operation;
    }
    sendOperation(buffer: ByteBuffer+, remote: NativeNetworkAddress*, timeoutNanos: long,
        completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = prepareSend(buffer, remote, completion, executor);
        submitOperation(*operation, timeoutNanos); return operation;
    }
    sendFuture(buffer: ByteBuffer+, remote: NativeNetworkAddress*, timeoutNanos: long,
        executor: ExecutorLease&) : Future<int>! {
        operation : IoOperation! = prepareSend(buffer, remote, null, executor);
        future : Future<int>! = futureFor<int>(*operation, DatagramSocket::decodeCount);
        submitOperation(*operation, timeoutNanos); return future;
    }
    sendWait(buffer: ByteBuffer+, remote: NativeNetworkAddress*, timeoutNanos: long) : int {
        checkIoSyncThread();
        executor : ExecutorLease! = ExecutorLease::system();
        operation : IoOperation! = prepareSend(buffer, remote, null, *executor);
        future : Future<int>! = futureFor<int>(*operation, DatagramSocket::decodeCount);
        submitOperation(*operation, timeoutNanos);
        return getIoSync<int>(*future, *operation);
    }
public:
    DatagramSocket() : _native(null) {}
    DatagramSocket(fd: int) : _native(null) {
        if (fd >= 0) {
            _native = __k_io_datagram_handle_create(fd);
            if (_native == null) {
                error : IoError = IoError::fromErrno(__k_io_setup_errno(), 0L);
                error.raiseSync();
            }
        }
    }
    DatagramSocket(other: const DatagramSocket&) : _native(other._native) { __k_io_handle_retain(_native); }
    operator =(other: const DatagramSocket&) : DatagramSocket& {
        handle : NativeIoHandle* = other._native;
        __k_io_handle_retain(handle); __k_io_handle_release(_native);
        _native = handle; return this;
    }
    ~DatagramSocket() { __k_io_handle_release(_native); _native = null; }
    static open() : DatagramSocket! {
        address : NetworkAddress("*", 0);
        return DatagramSocket::bind(address);
    }
    static bind(address: const NetworkAddress&) : DatagramSocket! {
        return DatagramSocket::bindEndpoint(address.nativeHost(), address.port(), address.family() == 1 ? 1 : 0);
    }
    static bind(path: const file::Path&) : DatagramSocket! {
        return DatagramSocket::bindEndpoint(path.nativePath(), 0, 1);
    }
    static bind(path: const char[]) : DatagramSocket! {
        endpoint : file::Path(path); return DatagramSocket::bind(endpoint);
    }
    const boundAddress() : NetworkAddress { return NetworkAddress(__k_io_datagram_bound_address(_native)); }
    const localPort() : int {
        address : NetworkAddress = boundAddress();
        if (address.family() != 4) {
            error : IoError = IoError::fromErrno(97, 0L); error.raiseSync();
        }
        return address.port();
    }
    const nativeDescriptor() : int { return __k_io_handle_fd(_native); }
    override const isOpen() : bool { return __k_io_handle_is_open(_native) != 0; }

    connect(remote: const NetworkAddress&, timeoutNanos: long, completion: IoCompletion<IoDone>,
        executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = prepareConnect(remote, completion, executor);
        submitOperation(*operation, timeoutNanos); return operation;
    }
    connect(remote: const NetworkAddress&, completion: IoCompletion<IoDone>, executor: ExecutorLease&) : IoOperation! {
        return connect(remote, IO_NO_TIMEOUT, completion, executor);
    }
    connect(remote: const NetworkAddress&, timeoutNanos: long, completion: IoCompletion<IoDone>) : IoOperation! {
        executor : ExecutorLease! = ExecutorLease::system(); return connect(remote, timeoutNanos, completion, *executor);
    }
    connect(remote: const NetworkAddress&, completion: IoCompletion<IoDone>) : IoOperation! {
        return connect(remote, IO_NO_TIMEOUT, completion);
    }
    connect(remote: const NetworkAddress&, timeout: const Duration&, completion: IoCompletion<IoDone>,
        executor: ExecutorLease&) : IoOperation! { return connect(remote, timeout.toNanos(), completion, executor); }
    connect(remote: const NetworkAddress&, timeout: const Duration&, completion: IoCompletion<IoDone>) : IoOperation! {
        return connect(remote, timeout.toNanos(), completion);
    }
    connectAsync(remote: const NetworkAddress&, timeoutNanos: long, executor: ExecutorLease&) : Future<IoDone>! {
        operation : IoOperation! = prepareConnect(remote, null, executor);
        future : Future<IoDone>! = futureFor<IoDone>(*operation, DatagramSocket::decodeDone);
        submitOperation(*operation, timeoutNanos); return future;
    }
    connectAsync(remote: const NetworkAddress&, timeoutNanos: long) : Future<IoDone>! {
        executor : ExecutorLease! = ExecutorLease::system(); return connectAsync(remote, timeoutNanos, *executor);
    }
    connectAsync(remote: const NetworkAddress&) : Future<IoDone>! { return connectAsync(remote, IO_NO_TIMEOUT); }
    connectAsync(remote: const NetworkAddress&, executor: ExecutorLease&) : Future<IoDone>! {
        return connectAsync(remote, IO_NO_TIMEOUT, executor);
    }
    connectAsync(remote: const NetworkAddress&, timeout: const Duration&, executor: ExecutorLease&) : Future<IoDone>! {
        return connectAsync(remote, timeout.toNanos(), executor);
    }
    connectAsync(remote: const NetworkAddress&, timeout: const Duration&) : Future<IoDone>! {
        return connectAsync(remote, timeout.toNanos());
    }
    connectSync(remote: const NetworkAddress&, timeoutNanos: long) {
        checkIoSyncThread();
        executor : ExecutorLease! = ExecutorLease::system();
        operation : IoOperation! = prepareConnect(remote, null, *executor);
        future : Future<IoDone>! = futureFor<IoDone>(*operation, DatagramSocket::decodeDone);
        submitOperation(*operation, timeoutNanos); getIoSync<IoDone>(*future, *operation);
    }
    connectSync(remote: const NetworkAddress&) { connectSync(remote, IO_NO_TIMEOUT); }
    connectSync(remote: const NetworkAddress&, timeout: const Duration&) { connectSync(remote, timeout.toNanos()); }
    connect(remote: const file::Path&, timeoutNanos: long, completion: IoCompletion<IoDone>,
        executor: ExecutorLease&) : IoOperation! {
        address : NetworkAddress = NetworkAddress::unix(remote);
        return connect(address, timeoutNanos, completion, executor);
    }
    connect(remote: const file::Path&, completion: IoCompletion<IoDone>, executor: ExecutorLease&) : IoOperation! {
        return connect(remote, IO_NO_TIMEOUT, completion, executor);
    }
    connect(remote: const file::Path&, timeoutNanos: long, completion: IoCompletion<IoDone>) : IoOperation! {
        executor : ExecutorLease! = ExecutorLease::system(); return connect(remote, timeoutNanos, completion, *executor);
    }
    connect(remote: const file::Path&, completion: IoCompletion<IoDone>) : IoOperation! {
        return connect(remote, IO_NO_TIMEOUT, completion);
    }
    connect(remote: const file::Path&, timeout: const Duration&, completion: IoCompletion<IoDone>,
        executor: ExecutorLease&) : IoOperation! { return connect(remote, timeout.toNanos(), completion, executor); }
    connect(remote: const file::Path&, timeout: const Duration&, completion: IoCompletion<IoDone>) : IoOperation! {
        return connect(remote, timeout.toNanos(), completion);
    }
    connectAsync(remote: const file::Path&, timeoutNanos: long, executor: ExecutorLease&) : Future<IoDone>! {
        address : NetworkAddress = NetworkAddress::unix(remote);
        return connectAsync(address, timeoutNanos, executor);
    }
    connectAsync(remote: const file::Path&, timeoutNanos: long) : Future<IoDone>! {
        executor : ExecutorLease! = ExecutorLease::system(); return connectAsync(remote, timeoutNanos, *executor);
    }
    connectAsync(remote: const file::Path&) : Future<IoDone>! { return connectAsync(remote, IO_NO_TIMEOUT); }
    connectAsync(remote: const file::Path&, executor: ExecutorLease&) : Future<IoDone>! {
        return connectAsync(remote, IO_NO_TIMEOUT, executor);
    }
    connectAsync(remote: const file::Path&, timeout: const Duration&, executor: ExecutorLease&) : Future<IoDone>! {
        return connectAsync(remote, timeout.toNanos(), executor);
    }
    connectAsync(remote: const file::Path&, timeout: const Duration&) : Future<IoDone>! {
        return connectAsync(remote, timeout.toNanos());
    }
    connectSync(remote: const file::Path&, timeoutNanos: long) {
        address : NetworkAddress = NetworkAddress::unix(remote); connectSync(address, timeoutNanos);
    }
    connectSync(remote: const file::Path&) { connectSync(remote, IO_NO_TIMEOUT); }
    connectSync(remote: const file::Path&, timeout: const Duration&) { connectSync(remote, timeout.toNanos()); }

    receive(buffer: ByteBuffer+, timeoutNanos: long, completion: IoCompletion<DatagramReceive>,
        executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = prepareReceive(buffer, completion, executor);
        submitOperation(*operation, timeoutNanos); return operation;
    }
    receive(buffer: ByteBuffer+, completion: IoCompletion<DatagramReceive>, executor: ExecutorLease&) : IoOperation! {
        return receive(buffer, IO_NO_TIMEOUT, completion, executor);
    }
    receive(buffer: ByteBuffer+, timeoutNanos: long, completion: IoCompletion<DatagramReceive>) : IoOperation! {
        executor : ExecutorLease! = ExecutorLease::system(); return receive(buffer, timeoutNanos, completion, *executor);
    }
    receive(buffer: ByteBuffer+, completion: IoCompletion<DatagramReceive>) : IoOperation! {
        return receive(buffer, IO_NO_TIMEOUT, completion);
    }
    receive(buffer: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<DatagramReceive>,
        executor: ExecutorLease&) : IoOperation! { return receive(buffer, timeout.toNanos(), completion, executor); }
    receive(buffer: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<DatagramReceive>) : IoOperation! {
        return receive(buffer, timeout.toNanos(), completion);
    }
    receiveAsync(buffer: ByteBuffer+, timeoutNanos: long, executor: ExecutorLease&) : Future<DatagramReceive>! {
        operation : IoOperation! = prepareReceive(buffer, null, executor);
        future : Future<DatagramReceive>! = futureFor<DatagramReceive>(*operation, DatagramReceive::decode);
        submitOperation(*operation, timeoutNanos); return future;
    }
    receiveAsync(buffer: ByteBuffer+, timeoutNanos: long) : Future<DatagramReceive>! {
        executor : ExecutorLease! = ExecutorLease::system(); return receiveAsync(buffer, timeoutNanos, *executor);
    }
    receiveAsync(buffer: ByteBuffer+) : Future<DatagramReceive>! { return receiveAsync(buffer, IO_NO_TIMEOUT); }
    receiveAsync(buffer: ByteBuffer+, executor: ExecutorLease&) : Future<DatagramReceive>! {
        return receiveAsync(buffer, IO_NO_TIMEOUT, executor);
    }
    receiveAsync(buffer: ByteBuffer+, timeout: const Duration&, executor: ExecutorLease&) : Future<DatagramReceive>! {
        return receiveAsync(buffer, timeout.toNanos(), executor);
    }
    receiveAsync(buffer: ByteBuffer+, timeout: const Duration&) : Future<DatagramReceive>! {
        return receiveAsync(buffer, timeout.toNanos());
    }
    receiveSync(buffer: ByteBuffer+, timeoutNanos: long) : DatagramReceive {
        checkIoSyncThread();
        executor : ExecutorLease! = ExecutorLease::system();
        operation : IoOperation! = prepareReceive(buffer, null, *executor);
        future : Future<DatagramReceive>! = futureFor<DatagramReceive>(*operation, DatagramReceive::decode);
        submitOperation(*operation, timeoutNanos); return getIoSync<DatagramReceive>(*future, *operation);
    }
    receiveSync(buffer: ByteBuffer+) : DatagramReceive { return receiveSync(buffer, IO_NO_TIMEOUT); }
    receiveSync(buffer: ByteBuffer+, timeout: const Duration&) : DatagramReceive {
        return receiveSync(buffer, timeout.toNanos());
    }

    send(buffer: ByteBuffer+, timeoutNanos: long, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return sendOperation(buffer, null, timeoutNanos, completion, executor);
    }
    send(buffer: ByteBuffer+, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return send(buffer, IO_NO_TIMEOUT, completion, executor);
    }
    send(buffer: ByteBuffer+, timeoutNanos: long, completion: IoCompletion<int>) : IoOperation! {
        executor : ExecutorLease! = ExecutorLease::system(); return send(buffer, timeoutNanos, completion, *executor);
    }
    send(buffer: ByteBuffer+, completion: IoCompletion<int>) : IoOperation! { return send(buffer, IO_NO_TIMEOUT, completion); }
    send(buffer: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return send(buffer, timeout.toNanos(), completion, executor);
    }
    send(buffer: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<int>) : IoOperation! {
        return send(buffer, timeout.toNanos(), completion);
    }
    sendAsync(buffer: ByteBuffer+, timeoutNanos: long, executor: ExecutorLease&) : Future<int>! {
        return sendFuture(buffer, null, timeoutNanos, executor);
    }
    sendAsync(buffer: ByteBuffer+, timeoutNanos: long) : Future<int>! {
        executor : ExecutorLease! = ExecutorLease::system(); return sendAsync(buffer, timeoutNanos, *executor);
    }
    sendAsync(buffer: ByteBuffer+) : Future<int>! { return sendAsync(buffer, IO_NO_TIMEOUT); }
    sendAsync(buffer: ByteBuffer+, executor: ExecutorLease&) : Future<int>! { return sendAsync(buffer, IO_NO_TIMEOUT, executor); }
    sendAsync(buffer: ByteBuffer+, timeout: const Duration&, executor: ExecutorLease&) : Future<int>! {
        return sendAsync(buffer, timeout.toNanos(), executor);
    }
    sendAsync(buffer: ByteBuffer+, timeout: const Duration&) : Future<int>! { return sendAsync(buffer, timeout.toNanos()); }
    sendSync(buffer: ByteBuffer+, timeoutNanos: long) : int { return sendWait(buffer, null, timeoutNanos); }
    sendSync(buffer: ByteBuffer+) : int { return sendSync(buffer, IO_NO_TIMEOUT); }
    sendSync(buffer: ByteBuffer+, timeout: const Duration&) : int { return sendSync(buffer, timeout.toNanos()); }

    sendTo(buffer: ByteBuffer+, remote: const NetworkAddress&, timeoutNanos: long,
        completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return sendOperation(buffer, remote.native(), timeoutNanos, completion, executor);
    }
    sendTo(buffer: ByteBuffer+, remote: const NetworkAddress&, completion: IoCompletion<int>,
        executor: ExecutorLease&) : IoOperation! { return sendTo(buffer, remote, IO_NO_TIMEOUT, completion, executor); }
    sendTo(buffer: ByteBuffer+, remote: const NetworkAddress&, timeoutNanos: long, completion: IoCompletion<int>) : IoOperation! {
        executor : ExecutorLease! = ExecutorLease::system(); return sendTo(buffer, remote, timeoutNanos, completion, *executor);
    }
    sendTo(buffer: ByteBuffer+, remote: const NetworkAddress&, completion: IoCompletion<int>) : IoOperation! {
        return sendTo(buffer, remote, IO_NO_TIMEOUT, completion);
    }
    sendTo(buffer: ByteBuffer+, remote: const NetworkAddress&, timeout: const Duration&,
        completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return sendTo(buffer, remote, timeout.toNanos(), completion, executor);
    }
    sendTo(buffer: ByteBuffer+, remote: const NetworkAddress&, timeout: const Duration&,
        completion: IoCompletion<int>) : IoOperation! { return sendTo(buffer, remote, timeout.toNanos(), completion); }
    sendToAsync(buffer: ByteBuffer+, remote: const NetworkAddress&, timeoutNanos: long,
        executor: ExecutorLease&) : Future<int>! { return sendFuture(buffer, remote.native(), timeoutNanos, executor); }
    sendToAsync(buffer: ByteBuffer+, remote: const NetworkAddress&, timeoutNanos: long) : Future<int>! {
        executor : ExecutorLease! = ExecutorLease::system(); return sendToAsync(buffer, remote, timeoutNanos, *executor);
    }
    sendToAsync(buffer: ByteBuffer+, remote: const NetworkAddress&) : Future<int>! {
        return sendToAsync(buffer, remote, IO_NO_TIMEOUT);
    }
    sendToAsync(buffer: ByteBuffer+, remote: const NetworkAddress&, executor: ExecutorLease&) : Future<int>! {
        return sendToAsync(buffer, remote, IO_NO_TIMEOUT, executor);
    }
    sendToAsync(buffer: ByteBuffer+, remote: const NetworkAddress&, timeout: const Duration&,
        executor: ExecutorLease&) : Future<int>! { return sendToAsync(buffer, remote, timeout.toNanos(), executor); }
    sendToAsync(buffer: ByteBuffer+, remote: const NetworkAddress&, timeout: const Duration&) : Future<int>! {
        return sendToAsync(buffer, remote, timeout.toNanos());
    }
    sendToSync(buffer: ByteBuffer+, remote: const NetworkAddress&, timeoutNanos: long) : int {
        return sendWait(buffer, remote.native(), timeoutNanos);
    }
    sendToSync(buffer: ByteBuffer+, remote: const NetworkAddress&) : int { return sendToSync(buffer, remote, IO_NO_TIMEOUT); }
    sendToSync(buffer: ByteBuffer+, remote: const NetworkAddress&, timeout: const Duration&) : int {
        return sendToSync(buffer, remote, timeout.toNanos());
    }
    sendTo(buffer: ByteBuffer+, remote: const file::Path&, timeoutNanos: long,
        completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        address : NetworkAddress = NetworkAddress::unix(remote);
        return sendTo(buffer, address, timeoutNanos, completion, executor);
    }
    sendTo(buffer: ByteBuffer+, remote: const file::Path&, completion: IoCompletion<int>,
        executor: ExecutorLease&) : IoOperation! { return sendTo(buffer, remote, IO_NO_TIMEOUT, completion, executor); }
    sendTo(buffer: ByteBuffer+, remote: const file::Path&, timeoutNanos: long, completion: IoCompletion<int>) : IoOperation! {
        executor : ExecutorLease! = ExecutorLease::system(); return sendTo(buffer, remote, timeoutNanos, completion, *executor);
    }
    sendTo(buffer: ByteBuffer+, remote: const file::Path&, completion: IoCompletion<int>) : IoOperation! {
        return sendTo(buffer, remote, IO_NO_TIMEOUT, completion);
    }
    sendTo(buffer: ByteBuffer+, remote: const file::Path&, timeout: const Duration&,
        completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return sendTo(buffer, remote, timeout.toNanos(), completion, executor);
    }
    sendTo(buffer: ByteBuffer+, remote: const file::Path&, timeout: const Duration&,
        completion: IoCompletion<int>) : IoOperation! { return sendTo(buffer, remote, timeout.toNanos(), completion); }
    sendToAsync(buffer: ByteBuffer+, remote: const file::Path&, timeoutNanos: long,
        executor: ExecutorLease&) : Future<int>! {
        address : NetworkAddress = NetworkAddress::unix(remote);
        return sendToAsync(buffer, address, timeoutNanos, executor);
    }
    sendToAsync(buffer: ByteBuffer+, remote: const file::Path&, timeoutNanos: long) : Future<int>! {
        executor : ExecutorLease! = ExecutorLease::system(); return sendToAsync(buffer, remote, timeoutNanos, *executor);
    }
    sendToAsync(buffer: ByteBuffer+, remote: const file::Path&) : Future<int>! {
        return sendToAsync(buffer, remote, IO_NO_TIMEOUT);
    }
    sendToAsync(buffer: ByteBuffer+, remote: const file::Path&, executor: ExecutorLease&) : Future<int>! {
        return sendToAsync(buffer, remote, IO_NO_TIMEOUT, executor);
    }
    sendToAsync(buffer: ByteBuffer+, remote: const file::Path&, timeout: const Duration&,
        executor: ExecutorLease&) : Future<int>! { return sendToAsync(buffer, remote, timeout.toNanos(), executor); }
    sendToAsync(buffer: ByteBuffer+, remote: const file::Path&, timeout: const Duration&) : Future<int>! {
        return sendToAsync(buffer, remote, timeout.toNanos());
    }
    sendToSync(buffer: ByteBuffer+, remote: const file::Path&, timeoutNanos: long) : int {
        address : NetworkAddress = NetworkAddress::unix(remote); return sendToSync(buffer, address, timeoutNanos);
    }
    sendToSync(buffer: ByteBuffer+, remote: const file::Path&) : int { return sendToSync(buffer, remote, IO_NO_TIMEOUT); }
    sendToSync(buffer: ByteBuffer+, remote: const file::Path&, timeout: const Duration&) : int {
        return sendToSync(buffer, remote, timeout.toNanos());
    }

    override close(completion: IoCompletion<IoDone>, executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = prepareOperation<IoDone>(7, _native, null, 0, 0L, 0,
            completion, executor, DatagramSocket::decodeDone);
        submitOperation(*operation, IO_NO_TIMEOUT); return operation;
    }
    override close(completion: IoCompletion<IoDone>) : IoOperation! {
        executor : ExecutorLease! = ExecutorLease::system(); return close(completion, *executor);
    }
    closeAsync(executor: ExecutorLease&) : Future<IoDone>! {
        operation : IoOperation! = prepareOperation<IoDone>(7, _native, null, 0, 0L, 0,
            null, executor, DatagramSocket::decodeDone);
        future : Future<IoDone>! = futureFor<IoDone>(*operation, DatagramSocket::decodeDone);
        submitOperation(*operation, IO_NO_TIMEOUT); return future;
    }
    override closeAsync() : Future<IoDone>! {
        executor : ExecutorLease! = ExecutorLease::system(); return closeAsync(*executor);
    }
    override closeSync() {
        checkIoSyncThread();
        executor : ExecutorLease! = ExecutorLease::system();
        operation : IoOperation! = prepareOperation<IoDone>(7, _native, null, 0, 0L, 0,
            null, *executor, DatagramSocket::decodeDone);
        future : Future<IoDone>! = futureFor<IoDone>(*operation, DatagramSocket::decodeDone);
        submitOperation(*operation, IO_NO_TIMEOUT); getIoSync<IoDone>(*future, *operation);
    }
    override close() { closeSync(); }

    /** Source-only aliases; receive intentionally returns the structured value. */
    connect(remote: const NetworkAddress&, timeoutNanos: long) { connectSync(remote, timeoutNanos); }
    connect(remote: const NetworkAddress&) { connectSync(remote); }
    connect(remote: const file::Path&, timeoutNanos: long) {
        address : NetworkAddress = NetworkAddress::unix(remote); connectSync(address, timeoutNanos);
    }
    connect(remote: const file::Path&) { connect(remote, IO_NO_TIMEOUT); }
    connect(remote: const char[], timeoutNanos: long) {
        address : NetworkAddress = NetworkAddress::unix(remote); connectSync(address, timeoutNanos);
    }
    connect(remote: const char[]) { connect(remote, IO_NO_TIMEOUT); }
    receive(buffer: ByteBuffer+, timeoutNanos: long) : DatagramReceive { return receiveSync(buffer, timeoutNanos); }
    receive(buffer: ByteBuffer+) : DatagramReceive { return receiveSync(buffer); }
    send(buffer: ByteBuffer+, timeoutNanos: long) : int { return sendSync(buffer, timeoutNanos); }
    send(buffer: ByteBuffer+) : int { return sendSync(buffer); }
    sendTo(buffer: ByteBuffer+, remote: const NetworkAddress&, timeoutNanos: long) : int {
        return sendToSync(buffer, remote, timeoutNanos);
    }
    sendTo(buffer: ByteBuffer+, remote: const NetworkAddress&) : int { return sendToSync(buffer, remote); }
    sendTo(buffer: ByteBuffer+, remote: const file::Path&, timeoutNanos: long) : int {
        address : NetworkAddress = NetworkAddress::unix(remote); return sendToSync(buffer, address, timeoutNanos);
    }
    sendTo(buffer: ByteBuffer+, remote: const file::Path&) : int { return sendTo(buffer, remote, IO_NO_TIMEOUT); }
    sendTo(buffer: ByteBuffer+, remote: const char[], timeoutNanos: long) : int {
        address : NetworkAddress = NetworkAddress::unix(remote); return sendToSync(buffer, address, timeoutNanos);
    }
    sendTo(buffer: ByteBuffer+, remote: const char[]) : int { return sendTo(buffer, remote, IO_NO_TIMEOUT); }
}
}
