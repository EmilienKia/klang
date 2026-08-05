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

Every blocking operation in this layer is **interruptible**: a thread parked on
a read, a write or an `fsync` wakes up when another thread calls
`Thread::interrupt()` on it, when the deadline given to the operation elapses,
or when the channel is closed by another thread.

The layer is organised in three levels:

| Level | Types | Role |
|-------|-------|------|
| Buffer | `ByteBuffer` | Byte container with position/limit/capacity cursors. |
| Channel | `Channel`, `ReadableChannel`, `WritableChannel`, `FileChannel` | Positional, interruptible transfers. |
| Stream | `AsyncFileInputStream`, `AsyncFileOutputStream` | `InputStream<byte>` / `OutputStream<byte>` adapters over a channel. |

`Path` sits beside them as a pure value type describing a filesystem location.

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

Phase 7 stress coverage exercises concurrent positional reads, repeated
cancellation, and runtime teardown while async channels are active.

---

## 2. `ByteBuffer`

`ByteBuffer` owns a `byte[]` and exposes the classic three-cursor model.

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

Relative accesses past the limit raise `IndexOutOfBoundsException`.

---

## 3. `Path`

`Path` is an immutable, null-terminated copy of a filesystem location. It does
no I/O beyond the inspection helpers below.

| Method | Description |
|--------|-------------|
| `Path()` / `Path(path: const char[])` | Construct empty, or from path text. |
| `static of(path: const char[]) : Path!` | Heap-allocated path. |
| `length() : int` | Number of characters, excluding the terminator. |
| `toString() : const char[]?` | View on the stored text. |
| `nativePath() : const char*` | Null-terminated pointer for platform calls. |
| `fileName() : char[]!` | Last component. |
| `parent() : Path!` | Parent directory, or `null` when there is none. |
| `resolve(other) : Path!` | Append a component; an absolute `other` replaces the receiver. |
| `isAbsolute() : bool` | Whether the path starts at the filesystem root. |
| `exists()`, `isFile()`, `isDirectory()` | Inspection. |
| `size() : long` | File size in bytes, `-1` when unavailable. |
| `remove() : bool`, `mkdir() : bool` | Removal and directory creation. |

> **Note:** K `char` is a 32-bit code point. Path text handed to the platform is
> transcoded to UTF-8 by the runtime bridge; K code never sees the encoding.

---

## 4. Channels

```
interface Channel                 // isOpen(), close()
interface ReadableChannel : Channel   // read(dst: ByteBuffer+) : int
interface WritableChannel : Channel   // write(src: ByteBuffer+) : int
class FileChannel : ReadableChannel, WritableChannel
```

### Opening

| Signature | Description |
|-----------|-------------|
| `static open(path: const Path&, options: int) : FileChannel!` | Open with explicit options. |
| `static open(path: const Path&) : FileChannel!` | Open for reading. |

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

copyFile(from: const Path&, to: const Path&) : void {
    src : k::io::AsyncFileInputStream! = new k::io::AsyncFileInputStream(from);
    dst : k::io::AsyncFileOutputStream! = new k::io::AsyncFileOutputStream(to);
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
