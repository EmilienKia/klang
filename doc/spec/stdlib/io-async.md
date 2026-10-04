# Asynchronous I/O — `k::io`

> **Status:** Working Draft — 2026  
> **Source:** `libk/libk/src/io/byte_buffer.k`, `mapped_byte_buffer.k`,
> `channel.k`, `io_operation.k`, `io_result.k`, `file/file_channel.k`,
> `socket_channel.k`, `server_socket.k`, `datagram_socket.k`
> **Runtime:** `libk/libk/src/runtime/io_runtime.c`, `io_operation.c`,
> `io_uring_backend.c`, `io_ffi.c`

This document describes the asynchronous channel I/O layer of the K standard
library. It builds on the threading and synchronisation layers documented in
[threading.md](threading.md) and [synchronization.md](synchronization.md), and
complements the synchronous stream framework described in [io.md](io.md).

---

## 1. Overview

FileChannel, stream socket and datagram primitives use three forms: a proactive callback method returning
`IoOperation!`, an `Async` method returning `Future<T>!`, and an interruptible
`Sync` bridge built on the same prepared native operation and Future publisher.
The callback and Async forms must not block the caller on filesystem I/O.
Future publication must not depend on the callback executor: Sync and Future
`get()` may be used inside an EventLoop task without waiting for that loop to
dispatch an I/O callback.

The layer separates native buffers and proactive channels from synchronous streams:

| Level | Types | Role |
|-------|-------|------|
| Buffer | `ByteBuffer`, `MappedByteBuffer` | Native byte storage with position/limit/capacity cursors and in-flight leases. |
| Channel | `Channel`, `ReadableChannel`, `WritableChannel`, `SocketChannel`, `ServerSocket`, `DatagramSocket` (in `k::io`), `FileChannel` (in `k::io::file`) | Proactive primitives, Futures and Sync bridges. |
| Stream | `FileInputStream`, `FileOutputStream` (in `k::io::file`) | Separate synchronous framework; not proactive adapters. |

`Path` and file-specific facilities live in [`k::io::file`](io-file.md).

### Platform backend

Building this layer requires Linux and `liburing` 2.5 or newer. A missing
dependency is a configuration error, not a request to compile a POSIX fallback.
The proactor uses a process-wide **io_uring** instance served by a dedicated
completion thread and probes required kernel operations during initialization.

File transfers must use the kernel data plane; there is no blocking POSIX
read/write or socket transport fallback. Missing required operations fail runtime
initialization explicitly. If an optional control opcode is unavailable, open,
statx, fsync and ftruncate may run on a bounded internal control pool, never on
the caller. This control path does not perform channel data transfers.
Close cancels and drains target requests before submitting the descriptor close.

FileChannel includes native parent-operation fully transfers. The obsolete
async file stream adapters and their separate file backend are removed.
TCP/IPv4 and Unix stream channels use the same prepared operations and native
fully parents, with whole-direction rather than file-cursor reservations.
Hostname connects compose bounded resolver work with kernel connect under one
deadline. UDP/IPv4 and Unix datagrams use persistent SENDMSG/RECVMSG contexts
and structured packet results, never fully loops. See [io-network.md](io-network.md)
for both APIs, packet truncation and ownership rules. The transitional
blocking `network_ffi.c` bridge is retired. RECVMSG and SENDMSG are required
kernel data-plane opcodes along with the stream/file operations.

`IoRuntime::initialize()` probes the required kernel data-plane opcodes.
The old FileChannel backend-selection helper is removed: this implementation
does not select a synchronous data-plane fallback.

Interruption and timeout are honoured by submitting an asynchronous cancel and
then waiting **uninterruptibly** for the real completion, so the kernel never
writes into a buffer the caller has already released.
The Sync bridge must also drain kernel activity before propagating Future
cancellation. Cancellation with no transferred bytes raises
`CancellationException`; cancellation after a partial transfer raises
`InterruptedIOException` with the actual byte count. A forbidden completion-thread
wait or an inherited post-fork Future raises `IllegalStateException` without
attempting another blocking wait.

Thread interruption follows the same drain rule for primitive and fully
operations. If kernel completion races with interruption, the exception must
report all transferred bytes, even when the entire requested transfer completed.
The buffer position must match that count and its in-flight lease must be released
before control returns to the caller.

Stress test coverage exercises concurrent positional reads, repeated
cancellation, and runtime teardown while async channels are active.

### Proactor result types

The foundations expose two transparent template aliases in `k::io`:

```k
public template<typename T>
alias IoResult : Expected<T, IoError>;

public template<typename T>
alias IoCompletion : !(IoResult<T>);
```

`IoResult<T>` holds either a successful value or an `IoError`.
`IoError::fromErrno(error, transferred)` uses the same native error-kind table
as operation completion; immediate setup methods do not maintain another errno
taxonomy.
`IoCompletion<T>` is an owning callable receiving that result by value.
Operations without a result value use `IoDone` rather than `void`.
For alias declarations, visibility specifiers precede the `template` clause.

### Execution contexts

`ExecutionContext::current()` returns an owning observer of the current I/O
callback's execution context, or `null` outside a callback. The
`const isCancellationRequested() : bool` method dispatches to the operation's
cancellation/interruption state even through a const `ExecutionContext&`.
Destroying that observer only releases its native reference; it does not cancel
the operation.

### Operation observers and drain

`IoOperation::share()` creates another owning observer of the same operation.
`stage()`, `isIoDone()`, `isCallbackRunning()`, `isDone()`, `isCancelled()`,
`isInterruptionRequested()`, `bytesTransferred()` and `error()` expose its state.
`cancel()` returns whether cancellation or cooperative interruption was accepted;
destroying an observer does not cancel.

`waitQuiescent() : int` waits indefinitely for native drain.
`waitQuiescent(timeout: Duration&) : int` limits that wait, with negative
durations treated as zero. Zero means drain completed; nonzero means it did
not complete successfully and must not be ignored before reusing a buffer.
Quiescence releases kernel leases but does not imply that a queued user callback
has run or that a running callback has finished. The process shutdown barrier
also waits for user delivery cleanup, as described in [section 9](#9-runtime-and-application-lifecycle).

---

## 2. `ByteBuffer`

`ByteBuffer` owns reference-counted native storage and exposes the classic
three-cursor model. Preparing an operation snapshots its buffer range and takes
a native lease. Destroying the K buffer wrapper or channel wrapper after
submission must not invalidate the native request. Mutating a leased buffer
raises `BufferInUseException`. A read into a read-only mapped buffer fails.

| Cursor | Meaning |
|--------|---------|
| `position()` | Index of the next byte to read or write. |
| `limit()` | First index that must not be touched. |
| `capacity()` | Total size of the backing array. |

Invariant: `0 <= position <= limit <= capacity`.

### Construction

| Signature | Description |
|-----------|-------------|
| `ByteBuffer()` | Empty buffer of capacity 0. |
| `ByteBuffer(capacity: unsigned int)` | Buffer with the given capacity, `position = 0`, `limit = capacity`. |
| `static allocate(capacity: unsigned int) : ByteBuffer!` | Heap-allocated buffer. |
| `static allocateDirect(capacity: unsigned int) : ByteBuffer!` | Page-aligned native storage; alignment does not promise registration. |
| `static wrap(array: const byte[]) : ByteBuffer!` | Buffer holding a copy of `array`, ready for reading (`position = 0`, `limit = size`). |

### Cursor management

| Method | Description |
|--------|-------------|
| `position()` / `position(newPosition)` | Read / set the position. |
| `limit()` / `limit(newLimit)` | Read / set the limit. |
| `capacity()` | Backing array size. |
| `remaining()` | `limit - position`. |
| `hasRemaining()` | `remaining() != 0`. |
| `flip()` | `limit = position; position = 0` — switch from filling to draining. |
| `clear()` | `position = 0; limit = capacity` — switch from draining to filling. |
| `rewind()` | `position = 0`, limit unchanged. |
| `compact()` | Move the remaining bytes to the front, then set up for filling. |

### Transfers

| Method | Description |
|--------|-------------|
| `get() : byte` | Relative read; advances the position. |
| `put(b: byte) : ByteBuffer&` | Relative write; advances the position. |
| `get(index: unsigned int) : byte` | Absolute read; position unchanged. |
| `put(index: unsigned int, b: byte) : ByteBuffer&` | Absolute write; position unchanged. |
| `get(dst: byte[], off, len) : unsigned int` | Bulk relative read; returns the number of bytes transferred. |
| `put(src: const byte[], off, len) : unsigned int` | Bulk relative write; returns the number of bytes transferred. |
| `put(src: const byte[]) : ByteBuffer&` | Bulk relative write of the whole array. |
| `toArray() : byte[]!` | Copy of the bytes between position and limit. |
| `dataAt(index: unsigned int) : byte*` | Raw pointer into the backing array, for channel transfers. |

Relative accesses past the limit raise `IndexOutOfBoundsError`.

### Leases and mapped storage

`hasLease()` and `leaseCount()` expose in-flight retention.
`isRegistered()` reports sparse io_uring registration, not whether a particular
transfer uses a fixed-buffer opcode. Application code must not access the
leased range or mutate its cursors while an operation owns it. Native completion
advances the cursor before releasing the lease and publishing results.

`MappedByteBuffer` extends `ByteBuffer` with deterministic mapping cleanup:

| Signature | Description |
|-----------|-------------|
| `static map(fd: int, offset: long, length: long, mode: MapMode) : MappedByteBuffer!` | Map an open descriptor; `fd == -1` requests anonymous storage. Offset need not be page-aligned. |
| `static map(path, offset: long, length: long, mode: MapMode) : MappedByteBuffer!` | Map a `const file::Path&`, `const char[]` or zero-terminated `const char*` path. |
| `force() : void` | Flush mapped changes; forbidden while leased. |
| `load() : void` / `const isLoaded() : bool` | Prefault pages / inspect current residency; `load` is forbidden while leased. |
| `registerBuffer() : bool` | Attempt sparse registration; `false` leaves a valid non-fixed mapping. |
| `unregisterBuffer() : bool` | Unregister now, or defer until the final lease drains; `true` includes successful deferral. |

`MapMode` is `ReadOnly`, `ReadWrite` or `Private`. Offset must be nonnegative
and length must be in `1..4294967295`. Mapped storage attempts registration
automatically; unavailable fixed opcodes, table exhaustion or memory-lock
limits must not invalidate a successful mapping. `isRegistered()` makes this
fallback observable. The registration survives active leases and is released
before the final unmap. Only file READ/WRITE requests can select fixed-buffer
opcodes here; stream RECV/SEND and packet RECVMSG/SENDMSG remain ordinary
kernel operations even when their buffer is registered.

---

## 3. `Path`

Filesystem paths live in the dedicated sub-namespace [`k::io::file`](io-file.md)
as `k::io::file::Path`. `Path` is an immutable, multi-segment representation of a
filesystem location. See [`io-file.md`](io-file.md) for complete details on the
three-tier filesystem architecture, fluent operations, URI mappings, and providers.

---

## 4. Channels

```
interface Channel                 // isOpen(), close / closeAsync / closeSync
interface ReadableChannel : Channel   // read / readAsync / readSync
interface WritableChannel : Channel   // write / writeAsync / writeSync
class FileChannel : ReadableChannel, WritableChannel
```

Stream channels implement the same readable/writable contracts, and datagram
sockets implement `Channel` with packet-specific methods. The interface defaults
report Unsupported for missing proactive implementations; they must not fake
success. File, stream and datagram implementations provide their own proactive
methods, with legacy blocking aliases delegating to Sync, not the reverse.

### Opening

| Signature | Description |
|-----------|-------------|
| `static open(path, options: int, completion: IoCompletion<FileChannel>, executor: ExecutorLease&) : IoOperation!` | Proactive open; callback receives a copyable channel value. |
| `static open(path, options: int, completion: IoCompletion<FileChannel>) : IoOperation!` | Open with the system callback executor. |
| `static openAsync(path, options: int) : Future<FileChannel>!` | Open with executor-independent Future publication. |
| `static openSync(path, options: int) : FileChannel` | Interruptible bridge returning a channel value. |
| `static openAsync(path) : Future<FileChannel>!` / `static openSync(path) : FileChannel` | Open for reading. |
| `static open(path, options: int) : FileChannel!` / `static open(path) : FileChannel!` | Temporary legacy aliases calling `openSync` and allocating a wrapper. |

Here `path` may be `const file::Path&` or `const char*`. The path is copied to
native UTF-8 storage during preparation. Timeout overloads accept `long`
nanoseconds or `const Duration&`; omitted timeouts use `IO_NO_TIMEOUT`.
String literals are `const char[]` values, not native pointers: construct a
`file::Path` from a literal or `String` and pass that path (or its
`nativePath()` pointer) to these factories.

Options are a bitwise combination of:

| Constant | Effect |
|----------|--------|
| `OPEN_READ` | Open for reading. |
| `OPEN_WRITE` | Open for writing. |
| `OPEN_APPEND` | Every write lands at the end of the file. |
| `OPEN_CREATE` | Create the file when it does not exist. |
| `OPEN_TRUNCATE` | Truncate an existing file to zero length. |
| `OPEN_EXCLUSIVE` | With `OPEN_CREATE`, fail when the file already exists. |

Unknown flags, missing access mode, exclusive without create, and append or
truncate without write fail with `IoErrorKind::InvalidArgument`.
Callback errors are `IoResult` values, not caller exceptions. Allocation failure
may prevent construction of an observable operation. Overloads acquiring the
system executor raise `RejectedExecutionException` when no system lease can be
acquired; they must not substitute an inactive lease or another dispatch path.
An operation already constructed with a lease retains observable callback
rejection if that executor subsequently rejects dispatch. Sync open preserves
`FileNotFoundException` for ENOENT and ENOTDIR; Future failures wrap the same
cause in `ExecutionException`.

FileChannel is a value with a native atomic reference count. Its default
constructor makes a closed value. Copies, assignment, repeated `Future.get()`,
and shared Future observers must all retain the same descriptor state. Destroying
one copy must not close other copies. Closing any copy closes the shared handle.
A newly produced descriptor must be owned by its channel before successful
Future publication becomes observable or callback delivery begins. If decoding
fails before adoption, the operation closes the unclaimed descriptor. If
publication fails or loses to cancellation after adoption, the owning channel
value releases it. These paths must close the descriptor exactly once; operation
rollback must not close a descriptor already adopted by a channel.

### Position and size

| Method | Description |
|--------|-------------|
| `position()` / `position(newPosition)` | Shared, synchronized native cursor. A setter with queued implicit requests raises `BufferInUseException`; negative positions are invalid. |
| `size(completion: IoCompletion<long>, executor: ExecutorLease&) : IoOperation!` | Query size using fd-based statx. |
| `sizeAsync() : Future<long>!` / `sizeSync() : long` | Future and Sync size forms. |
| `truncate(newSize: long, completion: IoCompletion<IoDone>, executor: ExecutorLease&) : IoOperation!` | Truncate or extend, then clamp the shared cursor after successful completion. |
| `truncateAsync(newSize: long) : Future<IoDone>!` / `truncateSync(newSize: long) : FileChannel&` | Future and Sync truncate forms. |
| `force(completion: IoCompletion<IoDone>, executor: ExecutorLease&) : IoOperation!` | Flush writes to stable storage. |
| `forceAsync() : Future<IoDone>!` / `forceSync()` | Future and Sync fsync forms. |
| `close(completion: IoCompletion<IoDone>, executor: ExecutorLease&) : IoOperation!` | Cancel and drain file requests, then close. |
| `closeAsync() : Future<IoDone>!` / `closeSync()` | Future and Sync close forms. |
| `nativeDescriptor() : int` | Underlying descriptor, `-1` once closed. |

With an available executor, closing a default or already closed FileChannel
succeeds without closing another descriptor. A concurrent close while the first
close is still pending may fail with `ResourceExhausted`; it must not take
ownership of the pending close.

### Transfers

| Method | Description |
|--------|-------------|
| `read(dst: ByteBuffer+, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation!` | Queue an implicit-position read. |
| `read(dst: ByteBuffer+, offset: long, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation!` | Positional read, independent of the shared cursor. |
| `readAsync(dst: ByteBuffer+) : Future<int>!` / `readSync(dst: ByteBuffer+) : int` | Implicit read Future and Sync forms. |
| `readAsync(dst: ByteBuffer+, offset: long) : Future<int>!` / `readSync(dst: ByteBuffer+, offset: long) : int` | Positional read Future and Sync forms. |
| `write(src: ByteBuffer+, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation!` | Queue an implicit-position write. |
| `write(src: ByteBuffer+, offset: long, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation!` | Positional write. |
| `writeAsync(src: ByteBuffer+) : Future<int>!` / `writeSync(src: ByteBuffer+) : int` | Implicit write Future and Sync forms. |
| `writeAsync(src: ByteBuffer+, offset: long) : Future<int>!` / `writeSync(src: ByteBuffer+, offset: long) : int` | Positional write Future and Sync forms. |
| `readFully(dst: ByteBuffer+, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation!` | Native implicit-position composite read. |
| `writeFully(src: ByteBuffer+, offset: long, completion: IoCompletion<int>, executor: ExecutorLease&) : IoOperation!` | Native positional composite write; read/write both provide implicit and positional forms. |
| `readFullyAsync(dst: ByteBuffer+) : Future<int>!` / `writeFullyAsync(src: ByteBuffer+, offset: long) : Future<int>!` | Future publication from the same native parent, independent of callback execution. |
| `readFullySync(dst: ByteBuffer+) : int` / `writeFullySync(src: ByteBuffer+, offset: long) : int` | Thin Future/getIoSync bridges, not transfer loops. |

Callback overloads without an executor use the system executor. Timeout
overloads retain the same semantics. Positive nanosecond durations establish an
operation deadline; non-positive values disable it. Negative explicit offsets
and lengths exceeding the signed native transfer range are invalid.

Implicit transfers and truncate are serialized in a bounded FIFO shared by all
channel copies. Offsets are selected when a request reaches the head, and the
cursor advances by actual completed bytes, not requested bytes. Up to 128
requests, including the active request, may be queued; excess requests fail
with ResourceExhausted. Queue wait time counts toward the deadline. Positional
overloads do not use or change this cursor and may run concurrently.

Buffer and channel positions must advance before Future publication or user
callback dispatch, including irreversible bytes from cancelled requests.
Zero-length transfers remain valid asynchronous operations. A closed-channel
request returns an observable failed operation, not a null handle.

Legacy `read`/`write` overloads returning `int`, `size()`, `truncate(size)`,
`force()`, and `close()` explicitly call the new Sync bridges. They are
compatibility aliases, not proactive primitives, and are pending removal.

### Fully transfer contract

Each fully operation snapshots the buffer's remaining bytes during preparation,
retains one native buffer lease continuously across all children, and returns one
parent `IoOperation`. The caller's buffer owner is borrowed, not consumed.
Dropping either wrapper does not cancel the operation or release kernel storage.
Each drained child advances the buffer position by its actual completed bytes,
even after cancellation; a subsequent explicit offset is the initial offset plus
the cumulative count. The parent exposes that count while pending and on every
terminal outcome. Neither a failed nor a cancelled child resets it.

Implicit fully operations reserve one FIFO slot for the **whole** composite.
No implicit primitive, truncate or other fully request may interleave between
children. The shared cursor advances per completed child; the slot is released
only at final drain. Positional fully operations leave the shared cursor alone.
Every native child passes normal bounded registry/backend admission.

A positive timeout establishes one absolute monotonic deadline, including FIFO
wait. Children receive only the remaining duration; a short transfer must not
renew the deadline. Internal chaining runs on native completion/control paths,
never on a callback worker, and never waits on a user's executor.

Success produces one `Expected<int, IoError>` containing the total requested
count; an empty buffer produces observable success `0`. Invalid offsets,
offset-plus-count overflow and counts above `INT_MAX` produce InvalidArgument,
not an empty success. `read` itself still returns `0` at EOF. In contrast,
`readFully` ending early produces EndOfStream (native ENODATA) with the cumulative
partial count. A nonempty `writeFully` child returning zero produces System/EIO
with the partial count, using the existing IOException convention, and must
not spin. Other child errors retain their classification and cumulative count.

Only one final user callback is dispatched on the selected/default executor.
Cancelling the parent atomically selects/cancels its current child and prevents
the next child, including cancellation before registration. A suppressed callback
does not undo bytes or cursor movement. Cancelling a running callback is
cooperative through ExecutionContext, never Thread::interrupt.
`waitQuiescent` must wait for the child's target **and linked-timeout** CQEs;
a cancel CQE or a target alone cannot release the parent's lease.

Fully Futures attach both parent hooks before submission, including immediate
preparation failures. Their cancellation acknowledges the request but defers
terminal publication until drain: zero bytes yield CancellationException; partial
bytes yield ExecutionException with an InterruptedIOException cause containing
the final count. Sync maps the same outcomes through getIoSync. EOF maps to
EndOfStreamException with the partial count. Thread interruption requests
cancellation, drains uninterruptibly, then reports InterruptedIOException after
any transfer, otherwise ThreadInterruptionException. An observation timeout on
Future.get does not change the I/O deadline.

Both readFully/writeFully families provide selected/default callback executors,
implicit/positional Future forms, and Sync forms. Duration overloads place the
timeout before the completion or executor. Positional raw-nanosecond overloads
take `(buffer, offset, timeoutNanos, ...)`. To avoid conflating a `long` offset
with an implicit `long` timeout, implicit callbacks take
`(buffer, completion, timeoutNanos, executor)` (or omit executor); implicit
Futures take `(buffer, executor, timeoutNanos)`. The default-executor raw form is
`readFullyAsyncWithTimeout(buffer, timeoutNanos)` (likewise write), and implicit
Sync uses `readFullySyncWithTimeout(buffer, timeoutNanos)` (likewise write).
The old blocking `readFully(buffer, offset)` / `writeFully(buffer, offset)`
signatures are removed; use their explicitly named Sync counterparts.

Use `IO_NO_TIMEOUT` to wait indefinitely.

---

## 5. Synchronous streams remain separate

The previous async file stream adapters are removed. Use FileChannel's
callback/Future/Sync forms for cancellable channel I/O, or the unchanged
FileInputStream/FileOutputStream and InputStream/OutputStream framework for
synchronous stream semantics. No proactive stream replacement is provided.

---

## 6. Exceptions

All I/O exceptions derive from `IOException`, itself an `Exception`.

| Type | Code | Raised when |
|------|------|-------------|
| `IOException` | 200 | Generic failure; `getErrno()` carries the platform code. |
| `FileNotFoundException` | 201 | The path does not exist, or a component is not a directory. |
| `ClosedChannelException` | 202 | The channel was closed, possibly by another thread. |
| `EndOfStreamException` | 203 | The file ended before the request was satisfied; `getBytesTransferred()` reports the partial transfer. |
| `InterruptedIOException` | 204 | The thread was interrupted after a partial transfer; `getBytesTransferred()` reports it. |

A pure interruption with no bytes transferred surfaces as
`::k::ThreadInterruptionException`, and an elapsed deadline as
`::k::TimeoutException`, so interruption handling stays uniform across the
threading, synchronisation and I/O layers.

---

## 7. Example

```k
module sample;

copyFile(from: const k::io::file::Path&, to: const k::io::file::Path&) : void throws(Throwable) {
    src : k::io::file::FileChannel = k::io::file::FileChannel::openSync(from);
    dst : k::io::file::FileChannel = k::io::file::FileChannel::openSync(to,
        k::io::file::OPEN_WRITE | k::io::file::OPEN_CREATE | k::io::file::OPEN_TRUNCATE);
    chunk : k::io::ByteBuffer! = k::io::ByteBuffer::allocateDirect(4096u);
    while (true) {
        chunk->clear();
        if (src.readSync(chunk) == 0) break;
        chunk->flip();
        dst.writeFullySync(chunk);
    }
    dst.forceSync();
    dst.closeSync();
    src.closeSync();
    delete chunk;
}
```

---

## 8. Threading contract

- Distinct FileChannel copies may submit concurrently. Implicit requests are
  serialized natively; positional requests remain independent.
- A wrapper must not be assigned or destroyed concurrently with a method call
  on that same wrapper. Once preparation returns, native resources, not the
  wrapper, own the operation's lifetime.
- Close cancels and drains outstanding requests before fd close. Submission
  on a closing or closed handle fails with Closed. Cancellation suppresses a
  queued user callback; irreversible transferred bytes remain observable.
- Closing an already closed value is harmless. A second close while the first
  is still draining reports ResourceExhausted (EBUSY), without changing the
  first close's ownership or reopening the handle.
- Sync waits are forbidden on the native completion thread. Interruption
  requests cancellation and waits uninterruptibly for kernel resource drain
  before throwing, with any partial-byte count preserved.
- Interrupting a thread that is not blocked leaves the flag set; the next
  blocking I/O call observes it. Clear it with `Thread::interrupted()`.

## 9. Runtime and Application lifecycle

`IoRuntime` lives in `k::io`:

| Method | Description |
|--------|-------------|
| `static initialize()` | Initialize the process proactor and probe required kernel operations; failure raises `IOException`. |
| `static shutdown()` | Complete the permanent process shutdown barrier; forbidden execution contexts raise `IllegalStateException`. |

The process owns one I/O runtime. `IoRuntime::shutdown()` is an idempotent,
permanent stop, not a restartable reset. It must reject new submissions and DNS
work, cancel accepted requests, drain target **and** linked-timeout CQEs, and
finish already accepted DNS/control jobs. Kernel buffer and handle leases must
be released before backend destruction; sparse buffer registrations must be
removed while the backend is still available.
An unregister failure must be diagnosed; destroying the drained backend must
then release any remaining kernel registrations before Application destruction.

Shutdown must also finish **user delivery**, not only kernel quiescence.
Every queued I/O callback, including file and datagram callbacks, must be
suppressed and have its owned carrier dropped exactly once. An accepted native
task may remain on an external executor after shutdown, but it must no longer
contain a live user capture or invoke that callback. Running callbacks on either
the system pool or a custom executor must receive cooperative cancellation via
`ExecutionContext`, return, and finish carrier/lease cleanup before shutdown
returns. Shutdown must not interrupt the executor's `Thread`.
Close I/O itself remains non-cancellable; queued or running close callbacks
must nevertheless be suppressed or cooperatively interrupted like other user
deliveries.

Callbacks must cooperate with cancellation; shutdown may wait for one that
does not return. Application code must arrange for its own external workers
and non-I/O tasks to stop as appropriate. The runtime drains and joins its
system workers, resolver/control workers, and completion thread.

Shutdown from the completion thread, a system or resolver/control worker, an I/O callback, or
callback capture cleanup must fail with `IllegalStateException` rather than
self-join. This includes running custom-executor callbacks. Concurrent shutdown
callers must await the same completed barrier, not return merely because another
caller has requested it. An inherited post-fork runtime is unsupported and must
be rejected before accessing inherited mutexes or descriptor state.
Runtime-owned worker TLS finalizers retain the same self-shutdown restriction.

The generated [Application entry point](application.md#exit-lifecycle) invokes
this completed barrier before the concrete Application destructor on normal
main return and on unwinding. An unused runtime/system executor is left virgin:
automatic exit must not initialize it or install a permanent-stop marker.
Explicit `IoRuntime::shutdown()` retains its permanent-stop semantics even when
called before initialization. `atexit` is only a safety fallback. Application
destructors and module-global finalizers may release surviving handles, buffers
and observers, but must not start new asynchronous work after the barrier.
