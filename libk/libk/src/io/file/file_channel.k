/*
 * K Language standard library - proactor file channels
 * Copyright 2026 Emilien Kia
 * Licensed under the Apache License, Version 2.0.
 */
module k;

namespace io::file {

public const OPEN_READ : int = 1;
public const OPEN_WRITE : int = 2;
public const OPEN_APPEND : int = 4;
public const OPEN_CREATE : int = 8;
public const OPEN_TRUNCATE : int = 16;
public const OPEN_EXCLUSIVE : int = 32;

/** Copyable proactor file descriptor with a synchronized shared position.
 *
 * Copies retain the same native handle and share
 * its synchronized position and closed state. A default value is closed.
 * Native operations own handles and buffer leases, never this K wrapper.
 * Implicit transfers and truncate use a bounded native FIFO; positional
 * transfers do not change its cursor. Implicit completion advances both cursors
 * before publishing a Future or dispatching a callback, even after cancellation.
 *
 * ## Selecting an operation form
 *
 * Callback methods return an IoOperation and later deliver IoResult on the
 * selected ExecutorLease, or on the system executor when omitted. Async
 * methods publish a Future independently of callback execution. Sync methods
 * wait interruptibly on the same proactor, not on a separate blocking data
 * path. Keep blocking joins out of a serial event loop when other tasks must
 * remain responsive; waits on the native completion thread are forbidden.
 * Linux kernel proactor availability is required; there is no blocking
 * read/write fallback.
 *
 * ## File position and buffer position
 *
 * An implicit transfer takes its file offset when it reaches the FIFO head,
 * not when called. Its actual transferred count advances both the shared file
 * position and buffer position. Channel copies therefore cannot be used as
 * independent sequential readers. Repositioning while implicit work is queued
 * raises BufferInUseException.
 *
 * An explicit offset is independent of the shared file position and does not
 * advance it, but still advances the buffer position. Use distinct unleased
 * buffers for concurrent positional work. OPEN_APPEND writes land at file end;
 * an explicit write offset does not override that open option.
 *
 * Primitive reads/writes may complete short. A positive short read is not EOF;
 * a nonempty primitive read returns zero at EOF. readFully/writeFully instead
 * attempt the entire remaining buffer range. Fully success returns its initial
 * remaining count; early EOF fails with EndOfStream and cumulative progress.
 * A nonempty fully write making zero progress fails rather than retrying forever.
 *
 * An implicit fully transfer reserves the FIFO for the whole operation, so
 * other implicit transfers and truncate cannot interleave between its partial
 * transfers. Positional fully operations leave that FIFO cursor untouched.
 * One positive I/O deadline covers queue wait and every partial transfer; it
 * is not renewed after progress. Nonpositive I/O timeouts disable the deadline.
 * A Future.get timeout only bounds the observer's wait.
 *
 * ## Ownership and cancellation
 *
 * Buffer arguments are borrowed, not consumed. Native operations retain the
 * backing storage and descriptor through drain, even if their K wrappers are
 * destroyed. They do not keep arbitrary K objects referenced by a callback
 * alive: retain those objects through callback execution and capture cleanup.
 * Do not access a leased buffer or reuse it merely because cancel returned.
 * Cancellation can leave irreversible bytes and cursor movement. Fully
 * Futures defer their cancellation result until drain; Sync cancellation and
 * interruption drain before propagating, reporting partial bytes through
 * InterruptedIOException when appropriate.
 *
 * Closing any channel copy closes the shared handle after cancelling/draining
 * its requests. Close has no I/O deadline and is not cancellable; its completion
 * does not join queued callback cleanup. Destroying one copy only releases its
 * reference, leaving other copies and in-flight native operations valid.
 *
 * ## Example: a positional header read
 *
 * This helper borrows an already open readable channel whose file has at least
 * two bytes. It returns a copy of those bytes without changing the channel's
 * shared position and does not close the caller's channel. An early EOF throws.
 *
 * ```k
 * module file_header_usage;
 *
 * readHeader(channel: k::io::file::FileChannel&) : byte[]! throws(Throwable) {
 *     buffer : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(2u);
 *     deadline : Duration = Duration::ofSeconds(5L);
 *     channel.readFullySync(buffer, 0L, deadline);
 *     buffer->flip();
 *     header : byte[]! = buffer->toArray();
 *     delete buffer;
 *     return header;
 * }
 * ```
 */
public class FileChannel : public ReadableChannel, public WritableChannel {
private:
    _native : NativeIoHandle*;

    @ffi::Extern("C") private static __k_io_file_handle_prepare(fd: int) : NativeIoHandle*;
    @ffi::Extern("C") private static __k_io_file_handle_claim(handle: NativeIoHandle*, result: byte*);
    @ffi::Extern("C") private static __k_io_handle_retain(handle: NativeIoHandle*);
    @ffi::Extern("C") private static __k_io_handle_release(handle: NativeIoHandle*);
    @ffi::Extern("C") private static __k_io_handle_is_open(handle: const NativeIoHandle*) : int;
    @ffi::Extern("C") private static __k_io_handle_fd(handle: const NativeIoHandle*) : int;
    @ffi::Extern("C") private static __k_io_handle_position(handle: const NativeIoHandle*) : long;
    @ffi::Extern("C") private static __k_io_handle_set_position(handle: NativeIoHandle*, position: long) : int;
    @ffi::Extern("C") private static __k_io_op_set_path(operation: NativeIoOperation*, path: const char*) : int;
    @ffi::Extern("C") private static __k_io_op_set_file_options(operation: NativeIoOperation*, options: int) : int;
    @ffi::Extern("C") private static __k_io_op_set_implicit_position(operation: NativeIoOperation*) : int;
    @ffi::Extern("C") private static __k_io_op_set_fully(operation: NativeIoOperation*) : int;
    @ffi::Extern("C") private static __k_io_statx_size(value: long) : long;

    FileChannel(handle: NativeIoHandle*) : _native(handle) {}

    static decodeOpen(value: long) : FileChannel {
        handle : NativeIoHandle* = __k_io_file_handle_prepare((int) value);
        if (handle == null) throw OutOfMemory();
        return FileChannel(handle);
    }
    static claimOpen(value: FileChannel&, result: byte*) {
        __k_io_file_handle_claim(value._native, result);
    }
    static decodeCount(value: long) : int { return (int) value; }
    static decodeDone(value: long) : IoDone { return IoDone(); }
    static defaultExecutor() : ExecutorLease! {
        return ExecutorLease::system();
    }
    static decodeSize(value: long) : long {
        size : long = __k_io_statx_size(value);
        if (size < 0L) throw IOException((int) -size);
        return size;
    }
    static prepareOpen(path: const char*, options: int,
        completion: IoCompletion<FileChannel>, executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = prepareOperation<FileChannel>(10, null, null,
            0, 438L, 0, completion, executor, FileChannel::decodeOpen, FileChannel::claimOpen);
        setOperationError(*operation, __k_io_op_set_file_options(operation->native(), options));
        setOperationError(*operation, __k_io_op_set_path(operation->native(), path));
        return operation;
    }
    prepareTransfer(opcode: int, buffer: ByteBuffer+, offset: long, implicit: bool,
        completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        length : unsigned int = buffer->remaining();
        count : int = 0;
        if (length <= 2147483647u) count = (int) length;
        operation : IoOperation! = prepareOperation<int>(opcode, _native,
            buffer->nativeHandle(), count, offset, 0, completion, executor, FileChannel::decodeCount);
        if (length > 2147483647u || (!implicit && offset < 0L) ||
            (!implicit && offset >= 0L && (long) length > 9223372036854775807L - offset))
            setOperationError(*operation, 22);
        if (implicit) setOperationError(*operation, __k_io_op_set_implicit_position(operation->native()));
        return operation;
    }
    transfer(opcode: int, buffer: ByteBuffer+, offset: long, implicit: bool,
        timeoutNanos: long, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = prepareTransfer(opcode, buffer, offset, implicit, completion, executor);
        submitOperation(*operation, timeoutNanos);
        return operation;
    }
    transferAsync(opcode: int, buffer: ByteBuffer+, offset: long, implicit: bool,
        timeoutNanos: long, executor: ExecutorLease&) : Future<int>! {
        operation : IoOperation! = prepareTransfer(opcode, buffer, offset, implicit, null, executor);
        future : Future<int>! = futureFor<int>(*operation, FileChannel::decodeCount);
        submitOperation(*operation, timeoutNanos);
        return future;
    }
    transferSync(opcode: int, buffer: ByteBuffer+, offset: long, implicit: bool,
        timeoutNanos: long) : int {
        checkIoSyncThread();
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        operation : IoOperation! = prepareTransfer(opcode, buffer, offset, implicit, null, *executor);
        future : Future<int>! = futureFor<int>(*operation, FileChannel::decodeCount);
        submitOperation(*operation, timeoutNanos);
        return getIoSync<int>(*future, *operation);
    }
    prepareFully(opcode: int, buffer: ByteBuffer+, offset: long, implicit: bool,
        completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = prepareTransfer(opcode, buffer, offset, implicit, completion, executor);
        setOperationError(*operation, __k_io_op_set_fully(operation->native()));
        return operation;
    }
    fullyTransfer(opcode: int, buffer: ByteBuffer+, offset: long, implicit: bool,
        timeoutNanos: long, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = prepareFully(opcode, buffer, offset, implicit, completion, executor);
        submitOperation(*operation, timeoutNanos);
        return operation;
    }
    fullyAsync(opcode: int, buffer: ByteBuffer+, offset: long, implicit: bool,
        timeoutNanos: long, executor: ExecutorLease&) : Future<int>! {
        operation : IoOperation! = prepareFully(opcode, buffer, offset, implicit, null, executor);
        future : Future<int>! = futureFor<int>(*operation, FileChannel::decodeCount);
        submitOperation(*operation, timeoutNanos);
        return future;
    }
    fullySync(opcode: int, buffer: ByteBuffer+, offset: long, implicit: bool,
        timeoutNanos: long) : int {
        checkIoSyncThread();
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        operation : IoOperation! = prepareFully(opcode, buffer, offset, implicit, null, *executor);
        future : Future<int>! = futureFor<int>(*operation, FileChannel::decodeCount);
        submitOperation(*operation, timeoutNanos);
        return getIoSync<int>(*future, *operation);
    }
    const prepareDone(opcode: int, argument: long, completion: IoCompletion<IoDone>,
        executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = prepareOperation<IoDone>(opcode, _native, null,
            0, argument, 0, completion, executor, FileChannel::decodeDone);
        if (opcode == 13 && argument < 0L) setOperationError(*operation, 22);
        return operation;
    }
    const doneOperation(opcode: int, argument: long, timeoutNanos: long,
        completion: IoCompletion<IoDone>, executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = prepareDone(opcode, argument, completion, executor);
        submitOperation(*operation, timeoutNanos);
        return operation;
    }
    const doneAsync(opcode: int, argument: long, timeoutNanos: long) : Future<IoDone>! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        operation : IoOperation! = prepareDone(opcode, argument, null, *executor);
        future : Future<IoDone>! = futureFor<IoDone>(*operation, FileChannel::decodeDone);
        submitOperation(*operation, timeoutNanos);
        return future;
    }
    const doneSync(opcode: int, argument: long, timeoutNanos: long) {
        checkIoSyncThread();
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        operation : IoOperation! = prepareDone(opcode, argument, null, *executor);
        future : Future<IoDone>! = futureFor<IoDone>(*operation, FileChannel::decodeDone);
        submitOperation(*operation, timeoutNanos);
        getIoSync<IoDone>(*future, *operation);
    }
    const prepareSize(completion: IoCompletion<long>, executor: ExecutorLease&) : IoOperation! {
        return prepareOperation<long>(11, _native, null, 0, 0L, 0,
            completion, executor, FileChannel::decodeSize);
    }

public:
    FileChannel() : _native(null) {}
    FileChannel(other: const FileChannel&) : _native(other._native) {
        __k_io_handle_retain(_native);
    }
    operator =(other: const FileChannel&) : FileChannel& {
        handle : NativeIoHandle* = other._native;
        __k_io_handle_retain(handle);
        __k_io_handle_release(_native);
        _native = handle;
        return this;
    }
    ~FileChannel() {
        __k_io_handle_release(_native);
        _native = null;
    }

    static open(path: const char*, options: int, timeoutNanos: long,
        completion: IoCompletion<FileChannel>, executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = FileChannel::prepareOpen(path, options, completion, executor);
        submitOperation(*operation, timeoutNanos);
        return operation;
    }
    static open(path: const char*, options: int, completion: IoCompletion<FileChannel>,
        executor: ExecutorLease&) : IoOperation! {
        return FileChannel::open(path, options, IO_NO_TIMEOUT, completion, executor);
    }
    static open(path: const char*, options: int, completion: IoCompletion<FileChannel>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return FileChannel::open(path, options, completion, *executor);
    }
    static open(path: const char*, options: int, timeout: const Duration&,
        completion: IoCompletion<FileChannel>, executor: ExecutorLease&) : IoOperation! {
        return FileChannel::open(path, options, timeout.toNanos(), completion, executor);
    }
    static open(path: const char*, options: int, timeoutNanos: long,
        completion: IoCompletion<FileChannel>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return FileChannel::open(path, options, timeoutNanos, completion, *executor);
    }
    static open(path: const char*, options: int, timeout: const Duration&,
        completion: IoCompletion<FileChannel>) : IoOperation! {
        return FileChannel::open(path, options, timeout.toNanos(), completion);
    }
    static open(path: const char*, completion: IoCompletion<FileChannel>, executor: ExecutorLease&) : IoOperation! {
        return FileChannel::open(path, OPEN_READ, completion, executor);
    }
    static open(path: const char*, completion: IoCompletion<FileChannel>) : IoOperation! {
        return FileChannel::open(path, OPEN_READ, completion);
    }
    static open(path: const Path&, options: int, timeoutNanos: long,
        completion: IoCompletion<FileChannel>, executor: ExecutorLease&) : IoOperation! {
        return FileChannel::open(path.nativePath(), options, timeoutNanos, completion, executor);
    }
    static open(path: const Path&, options: int, completion: IoCompletion<FileChannel>,
        executor: ExecutorLease&) : IoOperation! {
        return FileChannel::open(path.nativePath(), options, completion, executor);
    }
    static open(path: const Path&, options: int, completion: IoCompletion<FileChannel>) : IoOperation! {
        return FileChannel::open(path.nativePath(), options, completion);
    }
    static open(path: const Path&, options: int, timeout: const Duration&,
        completion: IoCompletion<FileChannel>, executor: ExecutorLease&) : IoOperation! {
        return FileChannel::open(path.nativePath(), options, timeout, completion, executor);
    }
    static open(path: const Path&, options: int, timeoutNanos: long,
        completion: IoCompletion<FileChannel>) : IoOperation! {
        return FileChannel::open(path.nativePath(), options, timeoutNanos, completion);
    }
    static open(path: const Path&, options: int, timeout: const Duration&,
        completion: IoCompletion<FileChannel>) : IoOperation! {
        return FileChannel::open(path.nativePath(), options, timeout, completion);
    }
    static open(path: const Path&, completion: IoCompletion<FileChannel>, executor: ExecutorLease&) : IoOperation! {
        return FileChannel::open(path.nativePath(), OPEN_READ, completion, executor);
    }
    static open(path: const Path&, completion: IoCompletion<FileChannel>) : IoOperation! {
        return FileChannel::open(path.nativePath(), OPEN_READ, completion);
    }
    static openAsync(path: const char*, options: int, timeoutNanos: long,
        executor: ExecutorLease&) : Future<FileChannel>! {
        operation : IoOperation! = FileChannel::prepareOpen(path, options, null, executor);
        future : Future<FileChannel>! = futureFor<FileChannel>(*operation,
            FileChannel::decodeOpen, FileChannel::claimOpen);
        submitOperation(*operation, timeoutNanos);
        return future;
    }
    static openAsync(path: const char*, options: int, timeoutNanos: long) : Future<FileChannel>! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return FileChannel::openAsync(path, options, timeoutNanos, *executor);
    }
    static openAsync(path: const char*, options: int) : Future<FileChannel>! {
        return FileChannel::openAsync(path, options, IO_NO_TIMEOUT);
    }
    static openAsync(path: const char*) : Future<FileChannel>! {
        return FileChannel::openAsync(path, OPEN_READ);
    }
    static openAsync(path: const char*, options: int, timeout: const Duration&) : Future<FileChannel>! {
        return FileChannel::openAsync(path, options, timeout.toNanos());
    }
    static openAsync(path: const Path&, options: int, timeoutNanos: long) : Future<FileChannel>! {
        return FileChannel::openAsync(path.nativePath(), options, timeoutNanos);
    }
    static openAsync(path: const Path&, options: int, timeoutNanos: long,
        executor: ExecutorLease&) : Future<FileChannel>! {
        return FileChannel::openAsync(path.nativePath(), options, timeoutNanos, executor);
    }
    static openAsync(path: const Path&, options: int) : Future<FileChannel>! {
        return FileChannel::openAsync(path.nativePath(), options);
    }
    static openAsync(path: const Path&) : Future<FileChannel>! {
        return FileChannel::openAsync(path.nativePath());
    }
    static openAsync(path: const Path&, options: int, timeout: const Duration&) : Future<FileChannel>! {
        return FileChannel::openAsync(path.nativePath(), options, timeout);
    }
    static openSync(path: const char*, options: int, timeoutNanos: long) : FileChannel {
        checkIoSyncThread();
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        operation : IoOperation! = FileChannel::prepareOpen(path, options, null, *executor);
        future : Future<FileChannel>! = futureFor<FileChannel>(*operation,
            FileChannel::decodeOpen, FileChannel::claimOpen);
        submitOperation(*operation, timeoutNanos);
        return getIoSync<FileChannel>(*future, *operation);
    }
    static openSync(path: const char*, options: int) : FileChannel {
        return FileChannel::openSync(path, options, IO_NO_TIMEOUT);
    }
    static openSync(path: const char*) : FileChannel {
        return FileChannel::openSync(path, OPEN_READ);
    }
    static openSync(path: const char*, options: int, timeout: const Duration&) : FileChannel {
        return FileChannel::openSync(path, options, timeout.toNanos());
    }
    static openSync(path: const Path&, options: int, timeoutNanos: long) : FileChannel {
        return FileChannel::openSync(path.nativePath(), options, timeoutNanos);
    }
    static openSync(path: const Path&, options: int) : FileChannel {
        return FileChannel::openSync(path.nativePath(), options);
    }
    static openSync(path: const Path&) : FileChannel { return FileChannel::openSync(path.nativePath()); }
    static openSync(path: const Path&, options: int, timeout: const Duration&) : FileChannel {
        return FileChannel::openSync(path.nativePath(), options, timeout);
    }

    override const isOpen() : bool { return __k_io_handle_is_open(_native) != 0; }
    const nativeDescriptor() : int { return __k_io_handle_fd(_native); }
    const position() : long { return __k_io_handle_position(_native); }
    /** Set the position shared by all copies of this channel.
     *
     * Setting the cursor while implicit requests are queued raises
     * BufferInUseException. Negative positions are invalid. This does not move
     * a ByteBuffer's cursor or change offsets of positional requests.
     */
    position(newPosition: long) : FileChannel& {
        rc : int = __k_io_handle_set_position(_native, newPosition);
        if (rc == -16) throw BufferInUseException(16);
        if (rc == -9) throw ClosedChannelException(9);
        if (rc != 0) throw IllegalArgumentException();
        return this;
    }

    override close(completion: IoCompletion<IoDone>, executor: ExecutorLease&) : IoOperation! {
        return doneOperation(7, 0L, IO_NO_TIMEOUT, completion, executor);
    }
    override close(completion: IoCompletion<IoDone>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return close(completion, *executor);
    }
    override closeAsync() : Future<IoDone>! { return doneAsync(7, 0L, IO_NO_TIMEOUT); }
    override closeSync() { doneSync(7, 0L, IO_NO_TIMEOUT); }

    const size(timeoutNanos: long, completion: IoCompletion<long>, executor: ExecutorLease&) : IoOperation! {
        operation : IoOperation! = prepareSize(completion, executor);
        submitOperation(*operation, timeoutNanos);
        return operation;
    }
    const size(completion: IoCompletion<long>, executor: ExecutorLease&) : IoOperation! {
        return size(IO_NO_TIMEOUT, completion, executor);
    }
    const size(completion: IoCompletion<long>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return size(completion, *executor);
    }
    const size(timeout: const Duration&, completion: IoCompletion<long>, executor: ExecutorLease&) : IoOperation! {
        return size(timeout.toNanos(), completion, executor);
    }
    const size(timeoutNanos: long, completion: IoCompletion<long>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return size(timeoutNanos, completion, *executor);
    }
    const size(timeout: const Duration&, completion: IoCompletion<long>) : IoOperation! {
        return size(timeout.toNanos(), completion);
    }
    const sizeAsync(timeoutNanos: long) : Future<long>! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        operation : IoOperation! = prepareSize(null, *executor);
        future : Future<long>! = futureFor<long>(*operation, FileChannel::decodeSize);
        submitOperation(*operation, timeoutNanos);
        return future;
    }
    const sizeAsync() : Future<long>! { return sizeAsync(IO_NO_TIMEOUT); }
    const sizeAsync(timeout: const Duration&) : Future<long>! { return sizeAsync(timeout.toNanos()); }
    const sizeSync(timeoutNanos: long) : long {
        checkIoSyncThread();
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        operation : IoOperation! = prepareSize(null, *executor);
        future : Future<long>! = futureFor<long>(*operation, FileChannel::decodeSize);
        submitOperation(*operation, timeoutNanos);
        return getIoSync<long>(*future, *operation);
    }
    const sizeSync() : long { return sizeSync(IO_NO_TIMEOUT); }
    const sizeSync(timeout: const Duration&) : long { return sizeSync(timeout.toNanos()); }

    truncate(newSize: long, timeoutNanos: long, completion: IoCompletion<IoDone>,
        executor: ExecutorLease&) : IoOperation! {
        return doneOperation(13, newSize, timeoutNanos, completion, executor);
    }
    truncate(newSize: long, completion: IoCompletion<IoDone>, executor: ExecutorLease&) : IoOperation! {
        return truncate(newSize, IO_NO_TIMEOUT, completion, executor);
    }
    truncate(newSize: long, completion: IoCompletion<IoDone>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return truncate(newSize, completion, *executor);
    }
    truncate(newSize: long, timeout: const Duration&, completion: IoCompletion<IoDone>,
        executor: ExecutorLease&) : IoOperation! {
        return truncate(newSize, timeout.toNanos(), completion, executor);
    }
    truncate(newSize: long, timeoutNanos: long, completion: IoCompletion<IoDone>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return truncate(newSize, timeoutNanos, completion, *executor);
    }
    truncate(newSize: long, timeout: const Duration&, completion: IoCompletion<IoDone>) : IoOperation! {
        return truncate(newSize, timeout.toNanos(), completion);
    }
    truncateAsync(newSize: long, timeoutNanos: long) : Future<IoDone>! {
        return doneAsync(13, newSize, timeoutNanos);
    }
    truncateAsync(newSize: long) : Future<IoDone>! { return truncateAsync(newSize, IO_NO_TIMEOUT); }
    truncateAsync(newSize: long, timeout: const Duration&) : Future<IoDone>! {
        return truncateAsync(newSize, timeout.toNanos());
    }
    truncateSync(newSize: long, timeoutNanos: long) : FileChannel& {
        doneSync(13, newSize, timeoutNanos);
        return this;
    }
    truncateSync(newSize: long) : FileChannel& { return truncateSync(newSize, IO_NO_TIMEOUT); }
    truncateSync(newSize: long, timeout: const Duration&) : FileChannel& {
        return truncateSync(newSize, timeout.toNanos());
    }

    force(timeoutNanos: long, completion: IoCompletion<IoDone>, executor: ExecutorLease&) : IoOperation! {
        return doneOperation(12, 0L, timeoutNanos, completion, executor);
    }
    force(completion: IoCompletion<IoDone>, executor: ExecutorLease&) : IoOperation! {
        return force(IO_NO_TIMEOUT, completion, executor);
    }
    force(completion: IoCompletion<IoDone>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return force(completion, *executor);
    }
    force(timeout: const Duration&, completion: IoCompletion<IoDone>, executor: ExecutorLease&) : IoOperation! {
        return force(timeout.toNanos(), completion, executor);
    }
    force(timeoutNanos: long, completion: IoCompletion<IoDone>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return force(timeoutNanos, completion, *executor);
    }
    force(timeout: const Duration&, completion: IoCompletion<IoDone>) : IoOperation! {
        return force(timeout.toNanos(), completion);
    }
    forceAsync(timeoutNanos: long) : Future<IoDone>! { return doneAsync(12, 0L, timeoutNanos); }
    forceAsync() : Future<IoDone>! { return forceAsync(IO_NO_TIMEOUT); }
    forceAsync(timeout: const Duration&) : Future<IoDone>! { return forceAsync(timeout.toNanos()); }
    forceSync(timeoutNanos: long) { doneSync(12, 0L, timeoutNanos); }
    forceSync() { forceSync(IO_NO_TIMEOUT); }
    forceSync(timeout: const Duration&) { forceSync(timeout.toNanos()); }

    read(dst: ByteBuffer+, offset: long, timeoutNanos: long,
        completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return transfer(1, dst, offset, false, timeoutNanos, completion, executor);
    }
    read(dst: ByteBuffer+, offset: long, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return read(dst, offset, IO_NO_TIMEOUT, completion, executor);
    }
    read(dst: ByteBuffer+, offset: long, completion: IoCompletion<int>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return read(dst, offset, completion, *executor);
    }
    read(dst: ByteBuffer+, offset: long, timeout: const Duration&,
        completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return read(dst, offset, timeout.toNanos(), completion, executor);
    }
    read(dst: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<int>,
        executor: ExecutorLease&) : IoOperation! {
        return transfer(1, dst, 0L, true, timeout.toNanos(), completion, executor);
    }
    read(dst: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<int>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return read(dst, timeout, completion, *executor);
    }
    read(dst: ByteBuffer+, offset: long, timeoutNanos: long, completion: IoCompletion<int>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return read(dst, offset, timeoutNanos, completion, *executor);
    }
    read(dst: ByteBuffer+, offset: long, timeout: const Duration&, completion: IoCompletion<int>) : IoOperation! {
        return read(dst, offset, timeout.toNanos(), completion);
    }
    override read(dst: ByteBuffer+, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return transfer(1, dst, 0L, true, IO_NO_TIMEOUT, completion, executor);
    }
    override read(dst: ByteBuffer+, completion: IoCompletion<int>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return read(dst, completion, *executor);
    }
    readAsync(dst: ByteBuffer+, offset: long, timeoutNanos: long, executor: ExecutorLease&) : Future<int>! {
        return transferAsync(1, dst, offset, false, timeoutNanos, executor);
    }
    readAsync(dst: ByteBuffer+, offset: long, timeoutNanos: long) : Future<int>! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return readAsync(dst, offset, timeoutNanos, *executor);
    }
    readAsync(dst: ByteBuffer+, offset: long) : Future<int>! { return readAsync(dst, offset, IO_NO_TIMEOUT); }
    readAsync(dst: ByteBuffer+, offset: long, timeout: const Duration&) : Future<int>! {
        return readAsync(dst, offset, timeout.toNanos());
    }
    readAsync(dst: ByteBuffer+, timeout: const Duration&) : Future<int>! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return transferAsync(1, dst, 0L, true, timeout.toNanos(), *executor);
    }
    override readAsync(dst: ByteBuffer+) : Future<int>! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return transferAsync(1, dst, 0L, true, IO_NO_TIMEOUT, *executor);
    }
    readSync(dst: ByteBuffer+, offset: long, timeoutNanos: long) : int {
        return transferSync(1, dst, offset, false, timeoutNanos);
    }
    readSync(dst: ByteBuffer+, offset: long) : int { return readSync(dst, offset, IO_NO_TIMEOUT); }
    readSync(dst: ByteBuffer+, offset: long, timeout: const Duration&) : int {
        return readSync(dst, offset, timeout.toNanos());
    }
    readSync(dst: ByteBuffer+, timeout: const Duration&) : int {
        return transferSync(1, dst, 0L, true, timeout.toNanos());
    }
    override readSync(dst: ByteBuffer+) : int { return transferSync(1, dst, 0L, true, IO_NO_TIMEOUT); }

    write(src: ByteBuffer+, offset: long, timeoutNanos: long,
        completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return transfer(2, src, offset, false, timeoutNanos, completion, executor);
    }
    write(src: ByteBuffer+, offset: long, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return write(src, offset, IO_NO_TIMEOUT, completion, executor);
    }
    write(src: ByteBuffer+, offset: long, completion: IoCompletion<int>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return write(src, offset, completion, *executor);
    }
    write(src: ByteBuffer+, offset: long, timeout: const Duration&,
        completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return write(src, offset, timeout.toNanos(), completion, executor);
    }
    write(src: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<int>,
        executor: ExecutorLease&) : IoOperation! {
        return transfer(2, src, 0L, true, timeout.toNanos(), completion, executor);
    }
    write(src: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<int>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return write(src, timeout, completion, *executor);
    }
    write(src: ByteBuffer+, offset: long, timeoutNanos: long, completion: IoCompletion<int>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return write(src, offset, timeoutNanos, completion, *executor);
    }
    write(src: ByteBuffer+, offset: long, timeout: const Duration&, completion: IoCompletion<int>) : IoOperation! {
        return write(src, offset, timeout.toNanos(), completion);
    }
    override write(src: ByteBuffer+, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return transfer(2, src, 0L, true, IO_NO_TIMEOUT, completion, executor);
    }
    override write(src: ByteBuffer+, completion: IoCompletion<int>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return write(src, completion, *executor);
    }
    writeAsync(src: ByteBuffer+, offset: long, timeoutNanos: long, executor: ExecutorLease&) : Future<int>! {
        return transferAsync(2, src, offset, false, timeoutNanos, executor);
    }
    writeAsync(src: ByteBuffer+, offset: long, timeoutNanos: long) : Future<int>! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return writeAsync(src, offset, timeoutNanos, *executor);
    }
    writeAsync(src: ByteBuffer+, offset: long) : Future<int>! { return writeAsync(src, offset, IO_NO_TIMEOUT); }
    writeAsync(src: ByteBuffer+, offset: long, timeout: const Duration&) : Future<int>! {
        return writeAsync(src, offset, timeout.toNanos());
    }
    writeAsync(src: ByteBuffer+, timeout: const Duration&) : Future<int>! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return transferAsync(2, src, 0L, true, timeout.toNanos(), *executor);
    }
    override writeAsync(src: ByteBuffer+) : Future<int>! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return transferAsync(2, src, 0L, true, IO_NO_TIMEOUT, *executor);
    }
    writeSync(src: ByteBuffer+, offset: long, timeoutNanos: long) : int {
        return transferSync(2, src, offset, false, timeoutNanos);
    }
    writeSync(src: ByteBuffer+, offset: long) : int { return writeSync(src, offset, IO_NO_TIMEOUT); }
    writeSync(src: ByteBuffer+, offset: long, timeout: const Duration&) : int {
        return writeSync(src, offset, timeout.toNanos());
    }
    writeSync(src: ByteBuffer+, timeout: const Duration&) : int {
        return transferSync(2, src, 0L, true, timeout.toNanos());
    }
    override writeSync(src: ByteBuffer+) : int { return transferSync(2, src, 0L, true, IO_NO_TIMEOUT); }

    /** Read the entire remaining range starting at an explicit file offset.
     *
     * All fully overloads report one cumulative result and retain one continuous
     * buffer lease. An early EOF is an error with partial progress, not short
     * success. The explicit offset does not alter the shared file position.
     * Implicit variants reserve one FIFO slot for the whole composite.
     *
     * For implicit operations prefer Duration overloads: a single long after
     * the buffer denotes an offset, not a timeout. Raw implicit callback
     * timeouts follow completion; raw implicit Future/Sync convenience names
     * are readFullyAsyncWithTimeout and readFullySyncWithTimeout.
     */
    readFully(dst: ByteBuffer+, offset: long, timeoutNanos: long,
        completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return fullyTransfer(1, dst, offset, false, timeoutNanos, completion, executor);
    }
    readFully(dst: ByteBuffer+, offset: long, completion: IoCompletion<int>,
        executor: ExecutorLease&) : IoOperation! {
        return readFully(dst, offset, IO_NO_TIMEOUT, completion, executor);
    }
    readFully(dst: ByteBuffer+, offset: long, timeout: const Duration&,
        completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return readFully(dst, offset, timeout.toNanos(), completion, executor);
    }
    readFully(dst: ByteBuffer+, offset: long, timeoutNanos: long,
        completion: IoCompletion<int>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return readFully(dst, offset, timeoutNanos, completion, *executor);
    }
    readFully(dst: ByteBuffer+, offset: long, completion: IoCompletion<int>) : IoOperation! {
        return readFully(dst, offset, IO_NO_TIMEOUT, completion);
    }
    readFully(dst: ByteBuffer+, offset: long, timeout: const Duration&,
        completion: IoCompletion<int>) : IoOperation! {
        return readFully(dst, offset, timeout.toNanos(), completion);
    }
    readFully(dst: ByteBuffer+, completion: IoCompletion<int>, timeoutNanos: long,
        executor: ExecutorLease&) : IoOperation! {
        return fullyTransfer(1, dst, 0L, true, timeoutNanos, completion, executor);
    }
    readFully(dst: ByteBuffer+, completion: IoCompletion<int>, timeoutNanos: long) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return readFully(dst, completion, timeoutNanos, *executor);
    }
    readFully(dst: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<int>,
        executor: ExecutorLease&) : IoOperation! {
        return readFully(dst, completion, timeout.toNanos(), executor);
    }
    readFully(dst: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<int>) : IoOperation! {
        return readFully(dst, completion, timeout.toNanos());
    }
    readFully(dst: ByteBuffer+, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return readFully(dst, completion, IO_NO_TIMEOUT, executor);
    }
    readFully(dst: ByteBuffer+, completion: IoCompletion<int>) : IoOperation! {
        return readFully(dst, completion, IO_NO_TIMEOUT);
    }
    readFullyAsync(dst: ByteBuffer+, offset: long, timeoutNanos: long,
        executor: ExecutorLease&) : Future<int>! {
        return fullyAsync(1, dst, offset, false, timeoutNanos, executor);
    }
    readFullyAsync(dst: ByteBuffer+, offset: long, timeoutNanos: long) : Future<int>! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return readFullyAsync(dst, offset, timeoutNanos, *executor);
    }
    readFullyAsync(dst: ByteBuffer+, offset: long) : Future<int>! {
        return readFullyAsync(dst, offset, IO_NO_TIMEOUT);
    }
    readFullyAsync(dst: ByteBuffer+, offset: long, executor: ExecutorLease&) : Future<int>! {
        return readFullyAsync(dst, offset, IO_NO_TIMEOUT, executor);
    }
    readFullyAsync(dst: ByteBuffer+, offset: long, timeout: const Duration&,
        executor: ExecutorLease&) : Future<int>! {
        return readFullyAsync(dst, offset, timeout.toNanos(), executor);
    }
    readFullyAsync(dst: ByteBuffer+, offset: long, timeout: const Duration&) : Future<int>! {
        return readFullyAsync(dst, offset, timeout.toNanos());
    }
    readFullyAsync(dst: ByteBuffer+, executor: ExecutorLease&, timeoutNanos: long) : Future<int>! {
        return fullyAsync(1, dst, 0L, true, timeoutNanos, executor);
    }
    readFullyAsyncWithTimeout(dst: ByteBuffer+, timeoutNanos: long) : Future<int>! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return readFullyAsync(dst, *executor, timeoutNanos);
    }
    readFullyAsync(dst: ByteBuffer+, timeout: const Duration&, executor: ExecutorLease&) : Future<int>! {
        return readFullyAsync(dst, executor, timeout.toNanos());
    }
    readFullyAsync(dst: ByteBuffer+, timeout: const Duration&) : Future<int>! {
        return readFullyAsyncWithTimeout(dst, timeout.toNanos());
    }
    readFullyAsync(dst: ByteBuffer+, executor: ExecutorLease&) : Future<int>! {
        return readFullyAsync(dst, executor, IO_NO_TIMEOUT);
    }
    readFullyAsync(dst: ByteBuffer+) : Future<int>! {
        return readFullyAsyncWithTimeout(dst, IO_NO_TIMEOUT);
    }
    readFullySync(dst: ByteBuffer+, offset: long, timeoutNanos: long) : int {
        return fullySync(1, dst, offset, false, timeoutNanos);
    }
    readFullySync(dst: ByteBuffer+, offset: long) : int {
        return readFullySync(dst, offset, IO_NO_TIMEOUT);
    }
    readFullySync(dst: ByteBuffer+, offset: long, timeout: const Duration&) : int {
        return readFullySync(dst, offset, timeout.toNanos());
    }
    readFullySyncWithTimeout(dst: ByteBuffer+, timeoutNanos: long) : int {
        return fullySync(1, dst, 0L, true, timeoutNanos);
    }
    readFullySync(dst: ByteBuffer+, timeout: const Duration&) : int {
        return readFullySyncWithTimeout(dst, timeout.toNanos());
    }
    readFullySync(dst: ByteBuffer+) : int {
        return readFullySyncWithTimeout(dst, IO_NO_TIMEOUT);
    }

    /** Write the entire remaining range as one cumulative operation.
     *
     * Explicit-offset overloads leave the shared cursor unchanged. Implicit
     * overloads reserve it through all partial transfers. The buffer position
     * advances by actual bytes even on failure or cancellation; no rollback is
     * implied. Raw implicit timeout placement follows the readFully convention,
     * with writeFullyAsyncWithTimeout and writeFullySyncWithTimeout helpers.
     */
    writeFully(src: ByteBuffer+, offset: long, timeoutNanos: long,
        completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return fullyTransfer(2, src, offset, false, timeoutNanos, completion, executor);
    }
    writeFully(src: ByteBuffer+, offset: long, completion: IoCompletion<int>,
        executor: ExecutorLease&) : IoOperation! {
        return writeFully(src, offset, IO_NO_TIMEOUT, completion, executor);
    }
    writeFully(src: ByteBuffer+, offset: long, timeout: const Duration&,
        completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return writeFully(src, offset, timeout.toNanos(), completion, executor);
    }
    writeFully(src: ByteBuffer+, offset: long, timeoutNanos: long,
        completion: IoCompletion<int>) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return writeFully(src, offset, timeoutNanos, completion, *executor);
    }
    writeFully(src: ByteBuffer+, offset: long, completion: IoCompletion<int>) : IoOperation! {
        return writeFully(src, offset, IO_NO_TIMEOUT, completion);
    }
    writeFully(src: ByteBuffer+, offset: long, timeout: const Duration&,
        completion: IoCompletion<int>) : IoOperation! {
        return writeFully(src, offset, timeout.toNanos(), completion);
    }
    writeFully(src: ByteBuffer+, completion: IoCompletion<int>, timeoutNanos: long,
        executor: ExecutorLease&) : IoOperation! {
        return fullyTransfer(2, src, 0L, true, timeoutNanos, completion, executor);
    }
    writeFully(src: ByteBuffer+, completion: IoCompletion<int>, timeoutNanos: long) : IoOperation! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return writeFully(src, completion, timeoutNanos, *executor);
    }
    writeFully(src: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<int>,
        executor: ExecutorLease&) : IoOperation! {
        return writeFully(src, completion, timeout.toNanos(), executor);
    }
    writeFully(src: ByteBuffer+, timeout: const Duration&, completion: IoCompletion<int>) : IoOperation! {
        return writeFully(src, completion, timeout.toNanos());
    }
    writeFully(src: ByteBuffer+, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation! {
        return writeFully(src, completion, IO_NO_TIMEOUT, executor);
    }
    writeFully(src: ByteBuffer+, completion: IoCompletion<int>) : IoOperation! {
        return writeFully(src, completion, IO_NO_TIMEOUT);
    }
    writeFullyAsync(src: ByteBuffer+, offset: long, timeoutNanos: long,
        executor: ExecutorLease&) : Future<int>! {
        return fullyAsync(2, src, offset, false, timeoutNanos, executor);
    }
    writeFullyAsync(src: ByteBuffer+, offset: long, timeoutNanos: long) : Future<int>! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return writeFullyAsync(src, offset, timeoutNanos, *executor);
    }
    writeFullyAsync(src: ByteBuffer+, offset: long) : Future<int>! {
        return writeFullyAsync(src, offset, IO_NO_TIMEOUT);
    }
    writeFullyAsync(src: ByteBuffer+, offset: long, executor: ExecutorLease&) : Future<int>! {
        return writeFullyAsync(src, offset, IO_NO_TIMEOUT, executor);
    }
    writeFullyAsync(src: ByteBuffer+, offset: long, timeout: const Duration&,
        executor: ExecutorLease&) : Future<int>! {
        return writeFullyAsync(src, offset, timeout.toNanos(), executor);
    }
    writeFullyAsync(src: ByteBuffer+, offset: long, timeout: const Duration&) : Future<int>! {
        return writeFullyAsync(src, offset, timeout.toNanos());
    }
    writeFullyAsync(src: ByteBuffer+, executor: ExecutorLease&, timeoutNanos: long) : Future<int>! {
        return fullyAsync(2, src, 0L, true, timeoutNanos, executor);
    }
    writeFullyAsyncWithTimeout(src: ByteBuffer+, timeoutNanos: long) : Future<int>! {
        executor : ExecutorLease! = FileChannel::defaultExecutor();
        return writeFullyAsync(src, *executor, timeoutNanos);
    }
    writeFullyAsync(src: ByteBuffer+, timeout: const Duration&, executor: ExecutorLease&) : Future<int>! {
        return writeFullyAsync(src, executor, timeout.toNanos());
    }
    writeFullyAsync(src: ByteBuffer+, timeout: const Duration&) : Future<int>! {
        return writeFullyAsyncWithTimeout(src, timeout.toNanos());
    }
    writeFullyAsync(src: ByteBuffer+, executor: ExecutorLease&) : Future<int>! {
        return writeFullyAsync(src, executor, IO_NO_TIMEOUT);
    }
    writeFullyAsync(src: ByteBuffer+) : Future<int>! {
        return writeFullyAsyncWithTimeout(src, IO_NO_TIMEOUT);
    }
    writeFullySync(src: ByteBuffer+, offset: long, timeoutNanos: long) : int {
        return fullySync(2, src, offset, false, timeoutNanos);
    }
    writeFullySync(src: ByteBuffer+, offset: long) : int {
        return writeFullySync(src, offset, IO_NO_TIMEOUT);
    }
    writeFullySync(src: ByteBuffer+, offset: long, timeout: const Duration&) : int {
        return writeFullySync(src, offset, timeout.toNanos());
    }
    writeFullySyncWithTimeout(src: ByteBuffer+, timeoutNanos: long) : int {
        return fullySync(2, src, 0L, true, timeoutNanos);
    }
    writeFullySync(src: ByteBuffer+, timeout: const Duration&) : int {
        return writeFullySyncWithTimeout(src, timeout.toNanos());
    }
    writeFullySync(src: ByteBuffer+) : int {
        return writeFullySyncWithTimeout(src, IO_NO_TIMEOUT);
    }

    /** Temporary primitive Sync aliases for existing channel migration users. */
    static open(path: const char*, options: int) : FileChannel! {
        value : FileChannel = FileChannel::openSync(path, options);
        return new FileChannel(value);
    }
    static open(path: const char*) : FileChannel! { return FileChannel::open(path, OPEN_READ); }
    static open(path: const Path&, options: int) : FileChannel! {
        return FileChannel::open(path.nativePath(), options);
    }
    static open(path: const Path&) : FileChannel! { return FileChannel::open(path, OPEN_READ); }
    override close() { closeSync(); }
    const size() : long { return sizeSync(); }
    truncate(newSize: long) : FileChannel& { return truncateSync(newSize); }
    force() { forceSync(); }
    read(dst: ByteBuffer+, offset: long, timeoutNanos: long) : int { return readSync(dst, offset, timeoutNanos); }
    read(dst: ByteBuffer+, offset: long) : int { return readSync(dst, offset); }
    override read(dst: ByteBuffer+) : int { return readSync(dst); }
    write(src: ByteBuffer+, offset: long, timeoutNanos: long) : int { return writeSync(src, offset, timeoutNanos); }
    write(src: ByteBuffer+, offset: long) : int { return writeSync(src, offset); }
    override write(src: ByteBuffer+) : int { return writeSync(src); }

}
}
