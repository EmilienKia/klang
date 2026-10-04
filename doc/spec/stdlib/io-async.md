# Asynchronous I/O — `k::io`

> **Status:** Working Draft — 2026  
> **Source:** `libk/libk/src/io/byte_buffer.k`, `channel.k`, `path.k`,
> `file_channel.k`, `file_stream.k`, `io_exceptions.k`  
> **Runtime:** `libk/libk/src/runtime/async_io.c`, `async_ffi.c`, `park_lot.c`

This document describes the asynchronous file I/O layer of the K standard
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

On Linux the runtime uses a single process-wide **io_uring** instance served by
a dedicated reaper thread. When `liburing` is not available at build time, the
exact same API is provided by a synchronous POSIX fallback (`pread`/`pwrite`
/`fsync`). `FileChannel` behaves identically either way; only latency under
load differs.

The `FileChannel::isAsyncBackendActive()` helper reports which backend is
currently active. It is useful for diagnostics only.



Interruption and timeout are honoured by submitting an asynchronous cancel and
then waiting **uninterruptibly** for the real completion, so the kernel never
writes into a buffer the caller has already released.

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

A missing file raises `FileNotFoundException`; any other failure raises
`IOException` carrying the platform `errno`.

### Position and size

| Method | Description |
|--------|-------------|
| `position()` / `position(newPosition)` | The channel's own cursor, used only by the non-positional overloads. |
| `size() : long` | Current file size. |
| `truncate(newSize: long) : FileChannel&` | Truncate or extend the file. |
| `force() : void` | Flush pending writes to stable storage. |
| `nativeDescriptor() : int` | Underlying descriptor, `-1` once closed. |

### Transfers

| Method | Description |
|--------|-------------|
| `read(dst: ByteBuffer+, position: long, timeoutNanos: long) : int` | Positional read with a deadline. |
| `read(dst: ByteBuffer+, position: long) : int` | Positional read. |
| `read(dst: ByteBuffer+) : int` | Read at the channel position, then advance it. |
| `readFully(dst: ByteBuffer+, position: long) : void` | Fill the buffer entirely. |
| `write(src: ByteBuffer+, position: long, timeoutNanos: long) : int` | Positional write with a deadline. |
| `write(src: ByteBuffer+, position: long) : int` | Positional write. |
| `write(src: ByteBuffer+) : int` | Write at the channel position, then advance it. |
| `writeFully(src: ByteBuffer+, position: long) : void` | Write every remaining byte. |

Positional overloads never touch the channel position, so several threads may
use one channel concurrently without coordination.

`read` returns `0` at end of stream. `readFully` raises `EndOfStreamException`
when the file ends first; both `readFully` and `writeFully` raise
`InterruptedIOException` when the thread is interrupted **after** some bytes
have already been transferred, so no transfer is ever silently lost.

Use `IO_NO_TIMEOUT` to wait indefinitely.

---

## 5. Asynchronous file streams

`AsyncFileInputStream` and `AsyncFileOutputStream` adapt a `FileChannel` to the
stream framework of [io.md](io.md). They own their channel and close it on
`close()`.

They differ from `FileInputStream` / `FileOutputStream` — which sit on the C
standard library — only in that their blocking points are interruptible and
cancellable.

| Type | Implements | Construction |
|------|-----------|--------------|
| `AsyncFileInputStream` | `InputStream<byte>` | `AsyncFileInputStream(path: const Path&)`, or adopt a `FileChannel!`. |
| `AsyncFileOutputStream` | `OutputStream<byte>` | `AsyncFileOutputStream(path: const Path&)` (create + truncate), `AsyncFileOutputStream(path, append: bool)`, or adopt a `FileChannel!`. |

Both expose `isOpen()` and `channel()` for positional access to the underlying
channel.

`AsyncFileInputStream::skip()` and `available()` are computed from the file
size and the channel position, so they never block.

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

copyFile(from: const k::io::file::Path&, to: const k::io::file::Path&) : void {
    src : k::io::file::AsyncFileInputStream! = new k::io::file::AsyncFileInputStream(from);
    dst : k::io::file::AsyncFileOutputStream! = new k::io::file::AsyncFileOutputStream(to);
    chunk : byte[4096];
    while (true) {
        n : k::Expected<unsigned int, k::io::StreamOutOfData> = src->read(chunk);
        if (n.hasError() || n.getResult() == 0u) {
            break;
        }
        dst->write(chunk, 0u, n.getResult());
    }
    dst->flush();
    dst->close();
    src->close();
    delete dst;
    delete src;
}
```

---

## 8. Threading contract

- A `FileChannel` may be shared between threads as long as only the
  **positional** overloads are used; the non-positional ones mutate the shared
  channel position.
- `close()` is safe to call from any thread and at any time: in-flight
  operations are cancelled and their callers observe `ClosedChannelException`.
- Closing twice is harmless.
- Interrupting a thread that is not blocked leaves the flag set; the next
  blocking I/O call observes it. Clear it with `Thread::interrupted()`.
