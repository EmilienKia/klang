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
 * Channel — a connection to an entity capable of I/O.
 *
 * A channel is open from the moment it is created until it is closed.  Closing
 * a channel cancels every operation still in flight on it: the blocked threads
 * observe ::k::io::ClosedChannelException, and the kernel is guaranteed to have
 * released the buffers they handed over before the exception is raised.
 *
 * Closing an already-closed channel is a no-op.
 */
public interface Channel {
    /**
     * Tell whether this channel is still usable.
     *
     * @return `true` until the channel is closed.
     */
    const isOpen() : bool;

    /**
     * Close this channel and release the underlying resources.
     * Idempotent.
     */
    close() : void;
}

/**
 * ReadableChannel — a channel bytes can be read from.
 *
 * Reads fill a ::k::io::ByteBuffer between its `position` and its `limit` and
 * advance `position` by the number of bytes transferred.  A read returning 0
 * with a non-empty buffer means end of stream.
 *
 * Reads block interruptibly: a thread waiting on a read throws
 * ::k::ThreadInterruptionException when it is interrupted, after the pending
 * kernel operation has been cancelled.
 */
public interface ReadableChannel : public Channel {
    /**
     * Read into the remaining space of `dst`.
     *
     * @param dst Destination buffer.
     * @return The number of bytes read; 0 at end of stream.
     */
    read(dst: ByteBuffer+) : int;
}

/**
 * WritableChannel — a channel bytes can be written to.
 *
 * Writes drain a ::k::io::ByteBuffer between its `position` and its `limit`
 * and advance `position` by the number of bytes transferred.  A single write
 * may transfer fewer bytes than requested.
 */
public interface WritableChannel : public Channel {
    /**
     * Write the remaining bytes of `src`.
     *
     * @param src Source buffer.
     * @return The number of bytes written.
     */
    write(src: ByteBuffer+) : int;
}

} // namespace io
