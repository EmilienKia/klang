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
 * Define the base contract for byte-oriented input streams.
 *
 * `read()` returns the next byte as an `Optional<byte>`, set when a byte was
 * successfully read, or empty at end of stream. Bulk reads fill
 * caller-provided buffers and report the number of bytes actually read.
 */
template<typename T>
public interface InputStream {
    /**
     * Read a single byte.
     *
     * @return An `Optional<byte>` containing the next byte if one is available,
     *         or an empty Optional at end of stream.
     */
    read() : Optional<T>;

    /**
     * Read up to `len` bytes into `buf` starting at `off`.
     *
     * @param buf Destination byte buffer.
     * @param off Destination start offset.
     * @param len Maximum number of bytes to read.
     * @return Number of bytes read, or `-1` if end of stream is reached first.
     */
    read(buf: T[], off: unsigned int, len: unsigned int) : int;

    /**
     * Read up to `buf.size` bytes into `buf`.
     *
     * @param buf Destination byte buffer.
     * @return Number of bytes read, or `-1` if end of stream is reached first.
     */
    read(buf: T[]) : int;

    /**
     * Skip and discard up to `n` bytes.
     *
     * @param n Requested number of bytes to skip.
     * @return Actual number of bytes skipped.
     */
    skip(n: unsigned long) : unsigned long;

    /**
     * Return a non-blocking availability estimate.
     *
     * @return Number of bytes that can be read without blocking.
     */
    available() : int;

    /**
     * Close the stream and release any associated resources.
     */
    close();
}


/**
 * Define the base contract for byte-oriented output streams.
 *
 * Single-byte writes use the low 8 bits of the provided integer value. Bulk
 * writes consume caller-provided buffers.
 */
template<typename T>
public interface OutputStream {
    /**
     * Write a single byte.
     *
     * @param b Source value; low 8 bits are written.
     * @return This stream.
     */
    write(b: T) : OutputStream<T>&;

    /**
     * Write `len` bytes from `buf` starting at `off`.
     *
     * @param buf Source byte buffer.
     * @param off Source start offset.
     * @param len Number of bytes to write.
     * @return This stream.
     */
    write(buf: const T[], off: unsigned int, len: unsigned int) : OutputStream<T>&;

    /**
     * Write all bytes from `buf`.
     *
     * @param buf Source byte buffer.
     * @return This stream.
     */
    write(buf: const T[]) : OutputStream<T>&;

    /**
     * Flush any buffered output to the underlying destination.
     * Returns this stream for fluent chaining.
     */
    flush() : OutputStream<T>&;

    /**
     * Flush and close the stream, releasing any associated resources.
     * Returns this stream for fluent chaining.
     */
    close() : OutputStream<T>&;
}


} // namespace io


