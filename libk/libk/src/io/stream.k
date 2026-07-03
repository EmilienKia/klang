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
 * Error codes for stream read/available operations.
 *
 * Represents possible error conditions returned by InputStream methods
 * via Expected<T, StreamOutOfData>.
 */
public enum StreamOutOfData {
    /**
     * Stream reached end-of-data while open.
     * Returned when a read request cannot be fully satisfied because there
     * is no more data, but the stream itself is still open.
     */
    OutOfData;

    /**
     * Stream is closed or in a broken state.
     * Operations on a closed stream will fail with this error.
     */
    Closed;
}


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
    read() : ::k::Optional<T>;

     /**
      * Read up to `len` values into `buf` starting at `off`.
      *
      * @param buf Destination buffer.
      * @param off Destination start offset.
      * @param len Maximum number of values to read.
      * @return Expected containing the number of values read (0 = EOS while open),
      *         or a ::k::io::StreamOutOfData when the stream is closed/broken.
      */
     read(buf: T[], off: unsigned int, len: unsigned int) : Expected<unsigned int, ::k::io::StreamOutOfData>;

     /**
      * Read up to `buf.size` values into `buf`.
      *
      * @param buf Destination buffer.
      * @return Expected containing the number of values read.
      */
     read(buf: T[]) : Expected<unsigned int, ::k::io::StreamOutOfData>;

    /**
     * Skip and discard up to `n` values.
     *
     * @param n Requested number of values to skip.
     * @return Actual number of values skipped.
     */
    skip(n: unsigned long) : unsigned long;

     /**
      * Return a non-blocking availability estimate.
      *
      * @return Expected containing the number of values readable without blocking,
      *         or a ::k::io::StreamOutOfData when the stream is closed/broken.
      */
     available() : Expected<unsigned int, ::k::io::StreamOutOfData>;

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
