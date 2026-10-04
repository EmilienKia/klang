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
 * Channel — a connection to an entity capable of asynchronous I/O.
 *
 * A channel is open from the moment it is created until it is closed. Closing
 * a channel cancels every operation still in flight on it and drains its kernel
 * buffers before completion. Active Sync requests observe CancellationException
 * (or InterruptedIOException after transfer); new requests observe Closed.
 *
 * Closing an already-closed channel is a safe no-op.
 *
 * FileChannel, stream channels and DatagramSocket override all three proactive
 * forms. Legacy aliases delegate to Sync, not vice versa. Interface defaults
 * report Unsupported for implementations which do not provide a proactor.
 */
public interface Channel {
    /**
     * Tell whether this channel is still usable.
     *
     * @return `true` until the channel is closed.
     */
    const isOpen() : bool;

    /**
     * Asynchronously close this channel, dispatching completion on the given executor lease.
     *
     * @param completion Callback receiving IoResult<IoDone>.
     * @param executor Refcounted lease to the executor that will run the callback.
     * @return An IoOperation tracking this request.
     */
    default close(completion: IoCompletion<IoDone>, executor: ExecutorLease&) : IoOperation! {
        return unsupportedOperation<IoDone>(completion, executor);
    }

    /**
     * Asynchronously close this channel, dispatching completion on the system default executor lease.
     *
     * @param completion Callback receiving IoResult<IoDone>.
     * @return An IoOperation tracking this request.
     */
    default close(completion: IoCompletion<IoDone>) : IoOperation! {
        lease : ExecutorLease! = ExecutorLease::system();
        return close(completion, *lease);
    }

    /**
     * Asynchronously close this channel and return a Future.
     *
     * @return A Future observing completion of the close operation.
     */
    default closeAsync() : Future<IoDone>! {
        p : Promise<IoDone>;
        error : Throwable! = new IOException(95);
        p.tryFailure(error);
        return p.future();
    }

    /**
     * Synchronously close this channel, blocking until kernel release is quiescent.
     */
    default closeSync() : void {
        close();
    }

    /**
     * Transitional legacy primitive. Migration F/G/H replaces every new
     * callback/Future/Sync adapter; no proactive adapter may call this method.
     */
    close() : void;
}

/**
 * ReadableChannel — a channel bytes can be read from asynchronously.
 *
 * Reads transfer data into a ::k::io::ByteBuffer between its `position` and `limit`.
 * The primitive proactive method renders immediately an IoOperation, while
 * readAsync() provides a Future, and readSync() offers synchronous execution.
 */
public interface ReadableChannel : public Channel {
    /**
     * Proactive read primitive into the remaining space of `dst`.
     *
     * @param dst Destination buffer.
     * @param completion Callback receiving IoResult<int> with transferred byte count.
     * @param executor Refcounted lease to the executor that will run the callback.
     * @return An IoOperation tracking this request.
     */
    default read(dst: ByteBuffer+, completion: IoCompletion<int>,
                 executor: ExecutorLease&) : IoOperation! {
        return unsupportedOperation<int>(completion, executor);
    }

    /**
     * Proactive read primitive using the system default executor lease.
     *
     * @param dst Destination buffer.
     * @param completion Callback receiving IoResult<int>.
     * @return An IoOperation tracking this request.
     */
    default read(dst: ByteBuffer+, completion: IoCompletion<int>) : IoOperation! {
        lease : ExecutorLease! = ExecutorLease::system();
        return read(dst, completion, *lease);
    }

    /**
     * Asynchronous read returning a Future.
     *
     * @param dst Destination buffer.
     * @return A Future observing the transferred byte count.
     */
    default readAsync(dst: ByteBuffer+) : Future<int>! {
        p : Promise<int>;
        error : Throwable! = new IOException(95);
        p.tryFailure(error);
        return p.future();
    }

    /**
     * Synchronous read, blocking until completion or interrupt.
     *
     * @param dst Destination buffer.
     * @return The number of bytes read; 0 at end of stream.
     */
    default readSync(dst: ByteBuffer+) : int {
        return read(dst);
    }

    read(dst: ByteBuffer+) : int;
}

/**
 * WritableChannel — a channel bytes can be written to asynchronously.
 *
 * Writes transfer data from a ::k::io::ByteBuffer between its `position` and `limit`.
 */
public interface WritableChannel : public Channel {
    /**
     * Proactive write primitive from the remaining bytes of `src`.
     *
     * @param src Source buffer.
     * @param completion Callback receiving IoResult<int> with transferred byte count.
     * @param executor Refcounted lease to the executor that will run the callback.
     * @return An IoOperation tracking this request.
     */
    default write(src: ByteBuffer+, completion: IoCompletion<int>,
                  executor: ExecutorLease&) : IoOperation! {
        return unsupportedOperation<int>(completion, executor);
    }

    /**
     * Proactive write primitive using the system default executor lease.
     *
     * @param src Source buffer.
     * @param completion Callback receiving IoResult<int>.
     * @return An IoOperation tracking this request.
     */
    default write(src: ByteBuffer+, completion: IoCompletion<int>) : IoOperation! {
        lease : ExecutorLease! = ExecutorLease::system();
        return write(src, completion, *lease);
    }

    /**
     * Asynchronous write returning a Future.
     *
     * @param src Source buffer.
     * @return A Future observing the transferred byte count.
     */
    default writeAsync(src: ByteBuffer+) : Future<int>! {
        p : Promise<int>;
        error : Throwable! = new IOException(95);
        p.tryFailure(error);
        return p.future();
    }

    /**
     * Synchronous write, blocking until completion or interrupt.
     *
     * @param src Source buffer.
     * @return The number of bytes written.
     */
    default writeSync(src: ByteBuffer+) : int {
        return write(src);
    }

    write(src: ByteBuffer+) : int;
}

} // namespace io
