/*
 * Proactor TCP and Unix stream channels. Copyright 2026 Emilien Kia.
 * Licensed under the Apache License, Version 2.0.
 */
module k;
namespace io {

/** Copyable TCP/IPv4 or Unix byte-stream channel backed by the proactor.
 *
 * All copies share one native descriptor, closed state and direction gates;
 * they are not independent connections. The default value is closed. Closing
 * any copy closes the shared connection, whereas destroying a copy only
 * releases its native reference.
 *
 * ## Partial transfers and framing
 *
 * TCP and Unix streams have no packet or application-message boundaries.
 * Primitive read/write methods may transfer fewer bytes than the buffer's
 * remaining range. A short positive read is not EOF; a nonempty primitive read
 * returns zero on orderly EOF, not minus one. Define framing in your protocol
 * instead of assuming one write matches one read.
 *
 * readFully/writeFully attempt the initial remaining range and produce one
 * cumulative result. Fully success returns that requested count. An early EOF
 * in readFully fails with EndOfStream and the number already received; Sync
 * raises EndOfStreamException. A nonempty send making zero progress fails with
 * System/EIO rather than spinning. Empty buffers complete with count zero.
 * Native completion advances buffer position by actual bytes before result
 * publication; flip the buffer after a read to drain only its filled prefix.
 *
 * One read and one write may run concurrently on a shared handle, using
 * distinct buffers. A competing request in the same direction fails with
 * ResourceExhausted/EBUSY, even through another copy. A fully transfer reserves
 * its direction through all partial transfers, with one continuous buffer
 * lease and one positive I/O deadline. Nonpositive I/O timeouts disable the
 * deadline; Future.get timeouts only limit the observer's wait.
 *
 * ## Delivery and lifetime
 *
 * Callback forms return an IoOperation and deliver IoResult on the selected
 * executor (the system executor by default). Async forms publish a Future
 * independently of that executor; Sync waits on the same proactor. Avoid
 * blocking a serial event loop, even though publication needs no loop task.
 * Linux kernel transport support is required; no blocking socket fallback is
 * selected when it is unavailable.
 *
 * Submitted operations retain native descriptor and buffer storage, not K
 * wrapper addresses. A callback that dereferences a buffer/channel or other
 * borrowed state must keep that K object alive through delivery and cleanup.
 * Cancellation before callback entry suppresses delivery; during a callback it
 * requests cooperative interruption. Neither cancellation nor a timed-out
 * Future wait grants access to a still-leased buffer. Sync interruption and
 * cancellation drain first and preserve actual partial-byte counts.
 *
 * Close rejects new work, cancels/drains active directions, then closes the
 * descriptor. It has no I/O deadline and cannot be cancelled. It is a whole
 * connection close, not a write-half shutdown; this API has no half-close
 * method. Native drain/close does not join unrelated user callback cleanup.
 *
 * ## Example: read a fixed-size frame
 *
 * The helper borrows a connected channel whose peer sends two bytes within
 * five seconds. It returns a copied frame, or throws on early EOF, timeout or
 * other failure. It leaves connection ownership and close to the caller.
 *
 * ```k
 * module stream_frame_usage;
 *
 * readPair(channel: k::io::SocketChannel&) : byte[]! throws(Throwable) {
 *     buffer : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(2u);
 *     deadline : Duration = Duration::ofSeconds(5L);
 *     channel.readFullySync(buffer, deadline);
 *     buffer->flip();
 *     frame : byte[]! = buffer->toArray();
 *     delete buffer;
 *     return frame;
 * }
 * ```
 */
public class SocketChannel : public ReadableChannel, public WritableChannel {
private:
    _native : NativeIoHandle*;
    @ffi::Extern("C") private static __k_io_stream_handle_prepare(fd: int) : NativeIoHandle*;
    @ffi::Extern("C") private static __k_io_stream_handle_create(fd: int) : NativeIoHandle*;
    @ffi::Extern("C") private static __k_io_stream_configure(fd: int) : int;
    @ffi::Extern("C") private static __k_io_file_handle_claim(handle: NativeIoHandle*, result: byte*);
    @ffi::Extern("C") private static __k_io_handle_retain(handle: NativeIoHandle*);
    @ffi::Extern("C") private static __k_io_handle_release(handle: NativeIoHandle*);
    @ffi::Extern("C") private static __k_io_handle_is_open(handle: const NativeIoHandle*) : int;
    @ffi::Extern("C") private static __k_io_handle_fd(handle: const NativeIoHandle*) : int;
    @ffi::Extern("C") private static __k_io_op_set_stream_endpoint(operation: NativeIoOperation*,
        endpoint: const char*, port: int, unixSocket: int) : int;
    @ffi::Extern("C") private static __k_io_op_set_fully(operation: NativeIoOperation*) : int;

    SocketChannel(handle: NativeIoHandle*) : _native(handle) {}
    static prepareConnect(endpoint: const char*, port: int, unixSocket: int,
        completion: IoCompletion<SocketChannel>, executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = prepareOperation<SocketChannel>(6, null, null,
            0, 0L, 0, completion, executor, SocketChannel::decodeSocket, SocketChannel::claimSocket);
        setOperationError(*operation, __k_io_op_set_stream_endpoint(
            operation->native(), endpoint, port, unixSocket));
        return operation;
    }
    static connectOperation(endpoint: const char*, port: int, unixSocket: int, timeoutNanos: long,
        completion: IoCompletion<SocketChannel>, executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = SocketChannel::prepareConnect(endpoint, port, unixSocket, completion, executor);
        submitOperation(*operation, timeoutNanos);
        return operation;
    }
    static connectFuture(endpoint: const char*, port: int, unixSocket: int, timeoutNanos: long,
        executor: ExecutorLease&) : Future<SocketChannel>! {
        operation : IoOperation! = SocketChannel::prepareConnect(endpoint, port, unixSocket, null, executor);
        future : Future<SocketChannel>! = futureFor<SocketChannel>(*operation,
            SocketChannel::decodeSocket, SocketChannel::claimSocket);
        submitOperation(*operation, timeoutNanos);
        return future;
    }
    static connectWait(endpoint: const char*, port: int, unixSocket: int, timeoutNanos: long) : SocketChannel {
        checkIoSyncThread();
        executor : ExecutorLease! = ExecutorLease::system();
        operation : IoOperation! = SocketChannel::prepareConnect(endpoint, port, unixSocket, null, *executor);
        future : Future<SocketChannel>! = futureFor<SocketChannel>(*operation,
            SocketChannel::decodeSocket, SocketChannel::claimSocket);
        submitOperation(*operation, timeoutNanos);
        return getIoSync<SocketChannel>(*future, *operation);
    }
    static decodeCount(value: long) : int { return (int) value; }
    static decodeDone(value: long) : IoDone { return IoDone(); }
    prepareTransfer(opcode: int, buffer: ByteBuffer+, fully: bool,
        completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        length : unsigned int = buffer->remaining();
        count : int = 0;
        if (length <= 2147483647u) count = (int) length;
        operation : IoOperation! = prepareOperation<int>(opcode, _native, buffer->nativeHandle(),
            count, 0L, 0, completion, executor, SocketChannel::decodeCount);
        if (length > 2147483647u) setOperationError(*operation, 22);
        if (fully) setOperationError(*operation, __k_io_op_set_fully(operation->native()));
        return operation;
    }
    transfer(opcode: int, buffer: ByteBuffer+, fully: bool, timeoutNanos: long,
        completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = prepareTransfer(opcode, buffer, fully, completion, executor);
        submitOperation(*operation, timeoutNanos);
        return operation;
    }
    transferFuture(opcode: int, buffer: ByteBuffer+, fully: bool, timeoutNanos: long,
        executor: ExecutorLease&) : Future<int>! {
        operation : IoOperation! = prepareTransfer(opcode, buffer, fully, null, executor);
        future : Future<int>! = futureFor<int>(*operation, SocketChannel::decodeCount);
        submitOperation(*operation, timeoutNanos);
        return future;
    }
    transferWait(opcode: int, buffer: ByteBuffer+, fully: bool, timeoutNanos: long) : int {
        checkIoSyncThread();
        executor : ExecutorLease! = ExecutorLease::system();
        operation : IoOperation! = prepareTransfer(opcode, buffer, fully, null, *executor);
        future : Future<int>! = futureFor<int>(*operation, SocketChannel::decodeCount);
        submitOperation(*operation, timeoutNanos);
        return getIoSync<int>(*future, *operation);
    }

public:
    SocketChannel() : _native(null) {}
    /** Transitional descriptor adoption, with checked nonblocking/cloexec setup. */
    SocketChannel(fd: int) : _native(null) {
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
    SocketChannel(other: const SocketChannel&) : _native(other._native) { __k_io_handle_retain(_native); }
    operator =(other: const SocketChannel&) : SocketChannel& {
        handle : NativeIoHandle* = other._native;
        __k_io_handle_retain(handle);
        __k_io_handle_release(_native);
        _native = handle;
        return this;
    }
    ~SocketChannel() { __k_io_handle_release(_native); _native = null; }

    private static decodeSocket(value: long) : SocketChannel {
        handle : NativeIoHandle* = __k_io_stream_handle_prepare((int) value);
        if (handle == null) throw OutOfMemory();
        return SocketChannel(handle);
    }
    private static claimSocket(value: SocketChannel&, result: byte*) {
        __k_io_file_handle_claim(value._native, result);
    }
    friend ServerSocket;

    static connect(address: const NetworkAddress&, timeoutNanos: long,
        completion: IoCompletion<SocketChannel>, executor: ExecutorLease&) : IoOperation! {
        return SocketChannel::connectOperation(address.nativeHost(), address.port(), 0, timeoutNanos, completion, executor);
    }
    static connect(path: const file::Path&, timeoutNanos: long,
        completion: IoCompletion<SocketChannel>, executor: ExecutorLease&) : IoOperation! {
        return SocketChannel::connectOperation(path.nativePath(), 0, 1, timeoutNanos, completion, executor);
    }
    static connect(address: const NetworkAddress&, completion: IoCompletion<SocketChannel>,
        executor: ExecutorLease&) : IoOperation! { return SocketChannel::connect(address, IO_NO_TIMEOUT, completion, executor); }
    static connect(path: const file::Path&, completion: IoCompletion<SocketChannel>,
        executor: ExecutorLease&) : IoOperation! { return SocketChannel::connect(path, IO_NO_TIMEOUT, completion, executor); }
    static connect(address: const NetworkAddress&, timeoutNanos: long,
        completion: IoCompletion<SocketChannel>) : IoOperation! {
        executor : ExecutorLease! = ExecutorLease::system();
        return SocketChannel::connect(address, timeoutNanos, completion, *executor);
    }
    static connect(path: const file::Path&, timeoutNanos: long,
        completion: IoCompletion<SocketChannel>) : IoOperation! {
        executor : ExecutorLease! = ExecutorLease::system();
        return SocketChannel::connect(path, timeoutNanos, completion, *executor);
    }
    static connect(address: const NetworkAddress&, completion: IoCompletion<SocketChannel>) : IoOperation! {
        return SocketChannel::connect(address, IO_NO_TIMEOUT, completion);
    }
    static connect(path: const file::Path&, completion: IoCompletion<SocketChannel>) : IoOperation! {
        return SocketChannel::connect(path, IO_NO_TIMEOUT, completion);
    }
    static connect(address: const NetworkAddress&, timeout: const Duration&,
        completion: IoCompletion<SocketChannel>, executor: ExecutorLease&) : IoOperation! {
        return SocketChannel::connect(address, timeout.toNanos(), completion, executor);
    }
    static connect(path: const file::Path&, timeout: const Duration&,
        completion: IoCompletion<SocketChannel>, executor: ExecutorLease&) : IoOperation! {
        return SocketChannel::connect(path, timeout.toNanos(), completion, executor);
    }
    static connect(address: const NetworkAddress&, timeout: const Duration&,
        completion: IoCompletion<SocketChannel>) : IoOperation! {
        return SocketChannel::connect(address, timeout.toNanos(), completion);
    }
    static connect(path: const file::Path&, timeout: const Duration&,
        completion: IoCompletion<SocketChannel>) : IoOperation! {
        return SocketChannel::connect(path, timeout.toNanos(), completion);
    }
    static connectAsync(address: const NetworkAddress&, timeoutNanos: long,
        executor: ExecutorLease&) : Future<SocketChannel>! {
        return SocketChannel::connectFuture(address.nativeHost(), address.port(), 0, timeoutNanos, executor);
    }
    static connectAsync(path: const file::Path&, timeoutNanos: long,
        executor: ExecutorLease&) : Future<SocketChannel>! {
        return SocketChannel::connectFuture(path.nativePath(), 0, 1, timeoutNanos, executor);
    }
    static connectAsync(address: const NetworkAddress&, timeoutNanos: long) : Future<SocketChannel>! {
        executor : ExecutorLease! = ExecutorLease::system();
        return SocketChannel::connectAsync(address, timeoutNanos, *executor);
    }
    static connectAsync(path: const file::Path&, timeoutNanos: long) : Future<SocketChannel>! {
        executor : ExecutorLease! = ExecutorLease::system();
        return SocketChannel::connectAsync(path, timeoutNanos, *executor);
    }
    static connectAsync(address: const NetworkAddress&) : Future<SocketChannel>! {
        return SocketChannel::connectAsync(address, IO_NO_TIMEOUT);
    }
    static connectAsync(path: const file::Path&) : Future<SocketChannel>! {
        return SocketChannel::connectAsync(path, IO_NO_TIMEOUT);
    }
    static connectAsync(address: const NetworkAddress&, executor: ExecutorLease&) : Future<SocketChannel>! {
        return SocketChannel::connectAsync(address, IO_NO_TIMEOUT, executor);
    }
    static connectAsync(path: const file::Path&, executor: ExecutorLease&) : Future<SocketChannel>! {
        return SocketChannel::connectAsync(path, IO_NO_TIMEOUT, executor);
    }
    static connectAsync(address: const NetworkAddress&, timeout: const Duration&,
        executor: ExecutorLease&) : Future<SocketChannel>! {
        return SocketChannel::connectAsync(address, timeout.toNanos(), executor);
    }
    static connectAsync(path: const file::Path&, timeout: const Duration&,
        executor: ExecutorLease&) : Future<SocketChannel>! {
        return SocketChannel::connectAsync(path, timeout.toNanos(), executor);
    }
    static connectAsync(address: const NetworkAddress&, timeout: const Duration&) : Future<SocketChannel>! {
        return SocketChannel::connectAsync(address, timeout.toNanos());
    }
    static connectAsync(path: const file::Path&, timeout: const Duration&) : Future<SocketChannel>! {
        return SocketChannel::connectAsync(path, timeout.toNanos());
    }
    static connectSync(address: const NetworkAddress&, timeoutNanos: long) : SocketChannel {
        return SocketChannel::connectWait(address.nativeHost(), address.port(), 0, timeoutNanos);
    }
    static connectSync(path: const file::Path&, timeoutNanos: long) : SocketChannel {
        return SocketChannel::connectWait(path.nativePath(), 0, 1, timeoutNanos);
    }
    static connectSync(address: const NetworkAddress&) : SocketChannel { return SocketChannel::connectSync(address, IO_NO_TIMEOUT); }
    static connectSync(path: const file::Path&) : SocketChannel { return SocketChannel::connectSync(path, IO_NO_TIMEOUT); }
    static connectSync(address: const NetworkAddress&, timeout: const Duration&) : SocketChannel {
        return SocketChannel::connectSync(address, timeout.toNanos());
    }
    static connectSync(path: const file::Path&, timeout: const Duration&) : SocketChannel {
        return SocketChannel::connectSync(path, timeout.toNanos());
    }

    override const isOpen() : bool { return __k_io_handle_is_open(_native) != 0; }
    const nativeDescriptor() : int { return __k_io_handle_fd(_native); }
    override close(completion: IoCompletion<IoDone>, executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = prepareOperation<IoDone>(7, _native, null, 0, 0L, 0,
            completion, executor, SocketChannel::decodeDone);
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
            null, executor, SocketChannel::decodeDone);
        future : Future<IoDone>! = futureFor<IoDone>(*operation, SocketChannel::decodeDone);
        submitOperation(*operation, IO_NO_TIMEOUT);
        return future;
    }
    override closeSync() {
        checkIoSyncThread();
        executor : ExecutorLease! = ExecutorLease::system();
        operation : IoOperation! = prepareOperation<IoDone>(7, _native, null, 0, 0L, 0,
            null, *executor, SocketChannel::decodeDone);
        future : Future<IoDone>! = futureFor<IoDone>(*operation, SocketChannel::decodeDone);
        submitOperation(*operation, IO_NO_TIMEOUT);
        getIoSync<IoDone>(*future, *operation);
    }
    override close() { closeSync(); }

    /** Submit one possibly short stream read into the remaining buffer range.
     *
     * For a nonempty buffer, success zero denotes orderly EOF. Use readFully
     * for a fixed-size frame whose short EOF must be reported as an error.
     */
    override read(buffer: ByteBuffer+, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return read(buffer, IO_NO_TIMEOUT, completion, executor);
    }
    override read(buffer: ByteBuffer+, completion: IoCompletion<int>) : IoOperation! {
        return read(buffer, IO_NO_TIMEOUT, completion);
    }
    read(buffer: ByteBuffer+, timeoutNanos: long, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return transfer(3, buffer, false, timeoutNanos, completion, executor);
    }
    read(buffer: ByteBuffer+, timeoutNanos: long, completion: IoCompletion<int>) : IoOperation! {
        executor : ExecutorLease! = ExecutorLease::system();
        return read(buffer, timeoutNanos, completion, *executor);
    }
    read(buffer: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return read(buffer, timeout.toNanos(), completion, executor);
    }
    read(buffer: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<int>) : IoOperation! {
        return read(buffer, timeout.toNanos(), completion);
    }
    readAsync(buffer: ByteBuffer+, timeoutNanos: long, executor: ExecutorLease&) : Future<int>! {
        return transferFuture(3, buffer, false, timeoutNanos, executor);
    }
    readAsync(buffer: ByteBuffer+, timeoutNanos: long) : Future<int>! {
        executor : ExecutorLease! = ExecutorLease::system();
        return readAsync(buffer, timeoutNanos, *executor);
    }
    override readAsync(buffer: ByteBuffer+) : Future<int>! { return readAsync(buffer, IO_NO_TIMEOUT); }
    readAsync(buffer: ByteBuffer+, executor: ExecutorLease&) : Future<int>! { return readAsync(buffer, IO_NO_TIMEOUT, executor); }
    readAsync(buffer: ByteBuffer+, timeout: const Duration&, executor: ExecutorLease&) : Future<int>! {
        return readAsync(buffer, timeout.toNanos(), executor);
    }
    readAsync(buffer: ByteBuffer+, timeout: const Duration&) : Future<int>! { return readAsync(buffer, timeout.toNanos()); }
    readSync(buffer: ByteBuffer+, timeoutNanos: long) : int { return transferWait(3, buffer, false, timeoutNanos); }
    override readSync(buffer: ByteBuffer+) : int { return readSync(buffer, IO_NO_TIMEOUT); }
    readSync(buffer: ByteBuffer+, timeout: const Duration&) : int { return readSync(buffer, timeout.toNanos()); }
    override read(buffer: ByteBuffer+) : int { return readSync(buffer); }
    read(buffer: ByteBuffer+, timeoutNanos: long) : int { return readSync(buffer, timeoutNanos); }

    override write(buffer: ByteBuffer+, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return write(buffer, IO_NO_TIMEOUT, completion, executor);
    }
    override write(buffer: ByteBuffer+, completion: IoCompletion<int>) : IoOperation! {
        return write(buffer, IO_NO_TIMEOUT, completion);
    }
    write(buffer: ByteBuffer+, timeoutNanos: long, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return transfer(4, buffer, false, timeoutNanos, completion, executor);
    }
    write(buffer: ByteBuffer+, timeoutNanos: long, completion: IoCompletion<int>) : IoOperation! {
        executor : ExecutorLease! = ExecutorLease::system();
        return write(buffer, timeoutNanos, completion, *executor);
    }
    write(buffer: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return write(buffer, timeout.toNanos(), completion, executor);
    }
    write(buffer: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<int>) : IoOperation! {
        return write(buffer, timeout.toNanos(), completion);
    }
    writeAsync(buffer: ByteBuffer+, timeoutNanos: long, executor: ExecutorLease&) : Future<int>! {
        return transferFuture(4, buffer, false, timeoutNanos, executor);
    }
    writeAsync(buffer: ByteBuffer+, timeoutNanos: long) : Future<int>! {
        executor : ExecutorLease! = ExecutorLease::system();
        return writeAsync(buffer, timeoutNanos, *executor);
    }
    override writeAsync(buffer: ByteBuffer+) : Future<int>! { return writeAsync(buffer, IO_NO_TIMEOUT); }
    writeAsync(buffer: ByteBuffer+, executor: ExecutorLease&) : Future<int>! { return writeAsync(buffer, IO_NO_TIMEOUT, executor); }
    writeAsync(buffer: ByteBuffer+, timeout: const Duration&, executor: ExecutorLease&) : Future<int>! {
        return writeAsync(buffer, timeout.toNanos(), executor);
    }
    writeAsync(buffer: ByteBuffer+, timeout: const Duration&) : Future<int>! { return writeAsync(buffer, timeout.toNanos()); }
    writeSync(buffer: ByteBuffer+, timeoutNanos: long) : int { return transferWait(4, buffer, false, timeoutNanos); }
    override writeSync(buffer: ByteBuffer+) : int { return writeSync(buffer, IO_NO_TIMEOUT); }
    writeSync(buffer: ByteBuffer+, timeout: const Duration&) : int { return writeSync(buffer, timeout.toNanos()); }
    override write(buffer: ByteBuffer+) : int { return writeSync(buffer); }
    write(buffer: ByteBuffer+, timeoutNanos: long) : int { return writeSync(buffer, timeoutNanos); }

    /** Fill the initial remaining range, reserving the read direction until drain.
     *
     * One final completion reports the requested count or an error with
     * cumulative bytes. A positive timeout covers the entire operation, not
     * each partial read separately.
     */
    readFully(buffer: ByteBuffer+, timeoutNanos: long, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return transfer(3, buffer, true, timeoutNanos, completion, executor);
    }
    readFully(buffer: ByteBuffer+, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return readFully(buffer, IO_NO_TIMEOUT, completion, executor);
    }
    readFully(buffer: ByteBuffer+, timeoutNanos: long, completion: IoCompletion<int>) : IoOperation! {
        executor : ExecutorLease! = ExecutorLease::system();
        return readFully(buffer, timeoutNanos, completion, *executor);
    }
    readFully(buffer: ByteBuffer+, completion: IoCompletion<int>) : IoOperation! {
        return readFully(buffer, IO_NO_TIMEOUT, completion);
    }
    readFully(buffer: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return readFully(buffer, timeout.toNanos(), completion, executor);
    }
    readFully(buffer: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<int>) : IoOperation! {
        return readFully(buffer, timeout.toNanos(), completion);
    }
    readFullyAsync(buffer: ByteBuffer+, timeoutNanos: long, executor: ExecutorLease&) : Future<int>! {
        return transferFuture(3, buffer, true, timeoutNanos, executor);
    }
    readFullyAsync(buffer: ByteBuffer+, timeoutNanos: long) : Future<int>! {
        executor : ExecutorLease! = ExecutorLease::system();
        return readFullyAsync(buffer, timeoutNanos, *executor);
    }
    readFullyAsync(buffer: ByteBuffer+) : Future<int>! { return readFullyAsync(buffer, IO_NO_TIMEOUT); }
    readFullyAsync(buffer: ByteBuffer+, executor: ExecutorLease&) : Future<int>! {
        return readFullyAsync(buffer, IO_NO_TIMEOUT, executor);
    }
    readFullyAsync(buffer: ByteBuffer+, timeout: const Duration&, executor: ExecutorLease&) : Future<int>! {
        return readFullyAsync(buffer, timeout.toNanos(), executor);
    }
    readFullyAsync(buffer: ByteBuffer+, timeout: const Duration&) : Future<int>! {
        return readFullyAsync(buffer, timeout.toNanos());
    }
    readFullySync(buffer: ByteBuffer+, timeoutNanos: long) : int { return transferWait(3, buffer, true, timeoutNanos); }
    readFullySync(buffer: ByteBuffer+) : int { return readFullySync(buffer, IO_NO_TIMEOUT); }
    readFullySync(buffer: ByteBuffer+, timeout: const Duration&) : int { return readFullySync(buffer, timeout.toNanos()); }

    writeFully(buffer: ByteBuffer+, timeoutNanos: long, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return transfer(4, buffer, true, timeoutNanos, completion, executor);
    }
    writeFully(buffer: ByteBuffer+, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return writeFully(buffer, IO_NO_TIMEOUT, completion, executor);
    }
    writeFully(buffer: ByteBuffer+, timeoutNanos: long, completion: IoCompletion<int>) : IoOperation! {
        executor : ExecutorLease! = ExecutorLease::system();
        return writeFully(buffer, timeoutNanos, completion, *executor);
    }
    writeFully(buffer: ByteBuffer+, completion: IoCompletion<int>) : IoOperation! {
        return writeFully(buffer, IO_NO_TIMEOUT, completion);
    }
    writeFully(buffer: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return writeFully(buffer, timeout.toNanos(), completion, executor);
    }
    writeFully(buffer: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<int>) : IoOperation! {
        return writeFully(buffer, timeout.toNanos(), completion);
    }
    writeFullyAsync(buffer: ByteBuffer+, timeoutNanos: long, executor: ExecutorLease&) : Future<int>! {
        return transferFuture(4, buffer, true, timeoutNanos, executor);
    }
    writeFullyAsync(buffer: ByteBuffer+, timeoutNanos: long) : Future<int>! {
        executor : ExecutorLease! = ExecutorLease::system();
        return writeFullyAsync(buffer, timeoutNanos, *executor);
    }
    writeFullyAsync(buffer: ByteBuffer+) : Future<int>! { return writeFullyAsync(buffer, IO_NO_TIMEOUT); }
    writeFullyAsync(buffer: ByteBuffer+, executor: ExecutorLease&) : Future<int>! {
        return writeFullyAsync(buffer, IO_NO_TIMEOUT, executor);
    }
    writeFullyAsync(buffer: ByteBuffer+, timeout: const Duration&, executor: ExecutorLease&) : Future<int>! {
        return writeFullyAsync(buffer, timeout.toNanos(), executor);
    }
    writeFullyAsync(buffer: ByteBuffer+, timeout: const Duration&) : Future<int>! {
        return writeFullyAsync(buffer, timeout.toNanos());
    }
    writeFullySync(buffer: ByteBuffer+, timeoutNanos: long) : int { return transferWait(4, buffer, true, timeoutNanos); }
    writeFullySync(buffer: ByteBuffer+) : int { return writeFullySync(buffer, IO_NO_TIMEOUT); }
    writeFullySync(buffer: ByteBuffer+, timeout: const Duration&) : int { return writeFullySync(buffer, timeout.toNanos()); }
    writeFully(buffer: ByteBuffer+, timeoutNanos: long) { writeFullySync(buffer, timeoutNanos); }

    /** Transitional owner-returning names; every wait delegates to the proactor. */
    static connect(address: const NetworkAddress&, timeoutNanos: long) : SocketChannel! {
        return new SocketChannel(SocketChannel::connectSync(address, timeoutNanos));
    }
    static connect(path: const file::Path&, timeoutNanos: long) : SocketChannel! {
        return new SocketChannel(SocketChannel::connectSync(path, timeoutNanos));
    }
    static connect(address: const NetworkAddress&) : SocketChannel! { return SocketChannel::connect(address, IO_NO_TIMEOUT); }
    static connect(path: const file::Path&) : SocketChannel! { return SocketChannel::connect(path, IO_NO_TIMEOUT); }
    static connect(path: const char[], timeoutNanos: long) : SocketChannel! {
        address : file::Path(path);
        return SocketChannel::connect(address, timeoutNanos);
    }
    static connect(path: const char[]) : SocketChannel! { return SocketChannel::connect(path, IO_NO_TIMEOUT); }

}

/** Transitional convenience subtype; all networking uses SocketChannel's proactor. */
public class Socket : public SocketChannel {
public:
    Socket() : SocketChannel() {}
    Socket(fd: int) : SocketChannel(fd) {}
    Socket(channel: const SocketChannel&) : SocketChannel(channel) {}
    static connect(address: const NetworkAddress&, timeoutNanos: long) : Socket! {
        return new Socket(SocketChannel::connectSync(address, timeoutNanos));
    }
    static connect(path: const file::Path&, timeoutNanos: long) : Socket! {
        return new Socket(SocketChannel::connectSync(path, timeoutNanos));
    }
    static connect(address: const NetworkAddress&) : Socket! { return Socket::connect(address, IO_NO_TIMEOUT); }
    static connect(path: const file::Path&) : Socket! { return Socket::connect(path, IO_NO_TIMEOUT); }
    static connect(path: const char[], timeoutNanos: long) : Socket! {
        address : file::Path(path);
        return Socket::connect(address, timeoutNanos);
    }
    static connect(path: const char[]) : Socket! { return Socket::connect(path, IO_NO_TIMEOUT); }
}
}
