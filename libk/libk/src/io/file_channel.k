/*
 * K Language standard library — base module
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

module k;

namespace io {

/**
 * Opaque wrapper type representing a native asynchronous I/O handle.
 */
public struct NativeAsyncHandle {
}

// ── Open options ─────────────────────────────────────────────────────────

/** Open for reading. */
public const OPEN_READ : int = 1;
/** Open for writing. */
public const OPEN_WRITE : int = 2;
/** Every write goes to the end of the file. */
public const OPEN_APPEND : int = 4;
/** Create the file when it does not exist. */
public const OPEN_CREATE : int = 8;
/** Truncate the file to zero length when opening it. */
public const OPEN_TRUNCATE : int = 16;
/** Fail when the file already exists; only meaningful with OPEN_CREATE. */
public const OPEN_EXCLUSIVE : int = 32;

// ── Encoded operation outcomes (returned by the substrate) ───────────────

/** The calling thread was interrupted while waiting. */
public const IO_INTERRUPTED : long = -1L;
/** The deadline elapsed before the operation completed. */
public const IO_TIMEOUT : long = -2L;
/** The channel was closed, possibly by another thread. */
public const IO_CLOSED : long = -3L;
/** Any value at or below this one encodes `-(1000 + errno)`. */
public const IO_ERROR_BASE : long = -1000L;

/** Wait without any deadline. */
public const IO_NO_TIMEOUT : long = -1L;

/**
 * FileChannel — a seekable, interruptible byte channel over a regular file.
 *
 * Every operation goes through the asynchronous substrate, which means that
 *
 *  - a blocked read or write can be interrupted (::k::ThreadInterruptionException),
 *  - a blocked read or write can be given a deadline (::k::TimeoutException),
 *  - closing the channel from another thread aborts the pending operation and
 *    raises ::k::io::ClosedChannelException on the blocked thread,
 *
 * and in every one of those cases the kernel has released the caller's buffer
 * before the exception propagates.
 *
 * A channel keeps its own file position, used by the `read`/`write` overloads
 * that do not take one; the positional overloads leave it untouched, so
 * several threads can safely read disjoint regions of the same channel.
 */
public class FileChannel : public ReadableChannel, public WritableChannel {
    _native   : NativeAsyncHandle*;
    _position : long;
    @ffi::Extern("C") private static __k_async_is_uring() : int;
    @ffi::Extern("C") private static __k_async_open(path: const char*, flags: int, mode: int) : NativeAsyncHandle*;
    @ffi::Extern("C") private static __k_async_last_errno() : int;
    @ffi::Extern("C") private static __k_async_retain(h: NativeAsyncHandle*) : void;
    @ffi::Extern("C") private static __k_async_release(h: NativeAsyncHandle*) : void;
    @ffi::Extern("C") private static __k_async_close(h: NativeAsyncHandle*) : int;
    @ffi::Extern("C") private static __k_async_is_open(h: const NativeAsyncHandle*) : int;
    @ffi::Extern("C") private static __k_async_fd(h: const NativeAsyncHandle*) : int;
    @ffi::Extern("C") private static __k_async_read_bytes(h: NativeAsyncHandle*, buf: byte*, len: int, offset: long, timeoutNanos: long) : long;
    @ffi::Extern("C") private static __k_async_write_bytes(h: NativeAsyncHandle*, buf: const byte*, len: int, offset: long, timeoutNanos: long) : long;
    @ffi::Extern("C") private static __k_async_fsync(h: NativeAsyncHandle*, timeoutNanos: long) : long;
    @ffi::Extern("C") private static __k_async_size(h: const NativeAsyncHandle*) : long;
    @ffi::Extern("C") private static __k_async_truncate(h: NativeAsyncHandle*, size: long) : int;

public:
    /** Construct a closed channel. */
    FileChannel() : _native(null), _position(0L) {}

    /**
     * Adopt an already-open native handle.
     * The channel takes ownership and releases it on close.
     *
     * @param handle Native handle produced by the substrate.
     */
    FileChannel(handle: NativeAsyncHandle*) : _native(handle), _position(0L) {}

    /**
     * Open a file channel.
     *
     * @param path Location to open.
     * @param options Bitwise combination of the OPEN_* constants.
     * @return A newly allocated open channel.
     */
    static open(path: const Path&, options: int) : FileChannel! {
        h : NativeAsyncHandle* = __k_async_open(path.nativePath(), options, 438);
        if (h == null) {
            err : int = __k_async_last_errno();
            // ENOENT (2) and ENOTDIR (20) both mean "no such file".
            if (err == 2 || err == 20) {
                throw FileNotFoundException(err);
            }
            throw IOException(err);
        }
        return new FileChannel(h);
    }

    /**
     * Open a file channel for reading.
     *
     * @param path Location to open.
     * @return A newly allocated open channel.
     */
    static open(path: const Path&) : FileChannel! {
        return FileChannel::open(path, OPEN_READ);
    }

    // ── Channel ──────────────────────────────────────────────────────────

    override const isOpen() : bool {
        if (_native == null) {
            return false;
        }
        return __k_async_is_open(_native) != 0;
    }

    override close() : void {
        if (_native != null) {
            __k_async_close(_native);
            __k_async_release(_native);
            _native = null;
        }
    }

    /**
     * Return the native file descriptor backing this channel.
     *
     * @return The descriptor, or -1 when the channel is closed.
     */
    const nativeDescriptor() : int {
        if (_native == null) {
            return -1;
        }
        return __k_async_fd(_native);
    }

    // ── Position ─────────────────────────────────────────────────────────

    /** @return The channel's own file position. */
    const position() : long {
        return _position;
    }

    /**
     * Move the channel's own file position.
     *
     * @param newPosition Byte offset from the start of the file.
     * @return This channel.
     */
    position(newPosition: long) : FileChannel& {
        _position = newPosition;
        return this;
    }

    /**
     * Return the current size of the file.
     *
     * @return Size in bytes.
     */
    const size() : long {
        if (_native == null) {
            throw ClosedChannelException();
        }
        n : long = __k_async_size(_native);
        if (n < 0L) {
            throw IOException(0);
        }
        return n;
    }

    /**
     * Truncate or extend the file.
     *
     * @param newSize Requested size in bytes.
     * @return This channel.
     */
    truncate(newSize: long) : FileChannel& {
        if (_native == null) {
            throw ClosedChannelException();
        }
        rc : int = __k_async_truncate(_native, newSize);
        if (rc != 0) {
            throw IOException(rc);
        }
        if (_position > newSize) {
            _position = newSize;
        }
        return this;
    }

    /**
     * Flush every pending write to stable storage.
     * Blocks interruptibly until the platform reports completion.
     */
    force() : void {
        if (_native == null) {
            throw ClosedChannelException();
        }
        rc : long = __k_async_fsync(_native, IO_NO_TIMEOUT);
        FileChannel::checkResult(rc, 0L);
    }

    // ── Reading ──────────────────────────────────────────────────────────

    /**
     * Read into the remaining space of `dst` at an explicit file offset,
     * without touching the channel position.
     *
     * @param dst Destination buffer.
     * @param position Byte offset to read from.
     * @param timeoutNanos Deadline in nanoseconds, or IO_NO_TIMEOUT.
     * @return The number of bytes read; 0 at end of stream.
     */
    read(dst: ByteBuffer+, position: long, timeoutNanos: long) : int {
        if (_native == null) {
            throw ClosedChannelException();
        }
        len : unsigned int = dst->remaining();
        if (len == 0u) {
            return 0;
        }
        rc : long = __k_async_read_bytes(_native, dst->dataAt(dst->position()),
                                         (int) len, position, timeoutNanos);
        FileChannel::checkResult(rc, 0L);
        n : int = (int) rc;
        dst->position(dst->position() + (unsigned int) n);
        return n;
    }

    /**
     * Read into the remaining space of `dst` at an explicit file offset.
     *
     * @param dst Destination buffer.
     * @param position Byte offset to read from.
     * @return The number of bytes read; 0 at end of stream.
     */
    read(dst: ByteBuffer+, position: long) : int {
        return read(dst, position, IO_NO_TIMEOUT);
    }

    /**
     * Read into the remaining space of `dst` at the channel position, then
     * advance that position by the number of bytes read.
     *
     * @param dst Destination buffer.
     * @return The number of bytes read; 0 at end of stream.
     */
    override read(dst: ByteBuffer+) : int {
        n : int = read(dst, _position, IO_NO_TIMEOUT);
        _position = _position + (long) n;
        return n;
    }

    /**
     * Fill the remaining space of `dst` entirely, issuing as many reads as
     * needed.
     *
     * Raises ::k::io::EndOfStreamException when the file ends first, and
     * ::k::io::InterruptedIOException when the thread is interrupted after
     * some bytes have already been transferred.
     *
     * @param dst Destination buffer.
     * @param position Byte offset to read from.
     */
    readFully(dst: ByteBuffer+, position: long) : void {
        done : long = 0L;
        while (dst->hasRemaining()) {
            n : int = 0;
            try {
                n = read(dst, position + done, IO_NO_TIMEOUT);
            } catch (e: ::k::ThreadInterruptionException&) {
                if (done > 0L) {
                    throw InterruptedIOException(done);
                }
                throw;
            }
            if (n == 0) {
                throw EndOfStreamException(done);
            }
            done = done + (long) n;
        }
    }

    // ── Writing ──────────────────────────────────────────────────────────

    /**
     * Write the remaining bytes of `src` at an explicit file offset, without
     * touching the channel position.
     *
     * @param src Source buffer.
     * @param position Byte offset to write at.
     * @param timeoutNanos Deadline in nanoseconds, or IO_NO_TIMEOUT.
     * @return The number of bytes written.
     */
    write(src: ByteBuffer+, position: long, timeoutNanos: long) : int {
        if (_native == null) {
            throw ClosedChannelException();
        }
        len : unsigned int = src->remaining();
        if (len == 0u) {
            return 0;
        }
        rc : long = __k_async_write_bytes(_native, src->dataAt(src->position()),
                                          (int) len, position, timeoutNanos);
        FileChannel::checkResult(rc, 0L);
        n : int = (int) rc;
        src->position(src->position() + (unsigned int) n);
        return n;
    }

    /**
     * Write the remaining bytes of `src` at an explicit file offset.
     *
     * @param src Source buffer.
     * @param position Byte offset to write at.
     * @return The number of bytes written.
     */
    write(src: ByteBuffer+, position: long) : int {
        return write(src, position, IO_NO_TIMEOUT);
    }

    /**
     * Write the remaining bytes of `src` at the channel position, then
     * advance that position by the number of bytes written.
     *
     * @param src Source buffer.
     * @return The number of bytes written.
     */
    override write(src: ByteBuffer+) : int {
        n : int = write(src, _position, IO_NO_TIMEOUT);
        _position = _position + (long) n;
        return n;
    }

    /**
     * Write every remaining byte of `src`, issuing as many writes as needed.
     *
     * Raises ::k::io::InterruptedIOException when the thread is interrupted
     * after some bytes have already been transferred.
     *
     * @param src Source buffer.
     * @param position Byte offset to write at.
     */
    writeFully(src: ByteBuffer+, position: long) : void {
        done : long = 0L;
        while (src->hasRemaining()) {
            n : int = 0;
            try {
                n = write(src, position + done, IO_NO_TIMEOUT);
            } catch (e: ::k::ThreadInterruptionException&) {
                if (done > 0L) {
                    throw InterruptedIOException(done);
                }
                throw;
            }
            if (n == 0) {
                throw IOException(0);
            }
            done = done + (long) n;
        }
    }

    // ── Result decoding ──────────────────────────────────────────────────

    /**
     * Translate an encoded substrate result into the matching exception.
     *
     * @param rc Encoded result returned by the substrate.
     * @param transferred Bytes already transferred by the caller, reported
     *        when an interruption must be turned into an
     *        ::k::io::InterruptedIOException.
     */
    static checkResult(rc: long, transferred: long) : void {
        if (rc >= 0L) {
            return;
        }
        if (rc == IO_INTERRUPTED) {
            if (transferred > 0L) {
                throw InterruptedIOException(transferred);
            }
            throw ::k::ThreadInterruptionException();
        }
        if (rc == IO_TIMEOUT) {
            throw ::k::TimeoutException();
        }
        if (rc == IO_CLOSED) {
            throw ClosedChannelException();
        }
        throw IOException((int) (IO_ERROR_BASE - rc));
    }

    /**
     * Tell whether the asynchronous substrate uses the io_uring backend.
     * When it does not, operations are neither interruptible nor cancellable.
     *
     * @return `true` when io_uring is in use.
     */
    static isAsyncBackendActive() : bool {
        return __k_async_is_uring() != 0;
    }
}

} // namespace io
