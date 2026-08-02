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
 * ByteBuffer — a fixed-capacity byte container with an explicit cursor.
 *
 * A buffer carries three invariants, always ordered
 * `0 <= position <= limit <= capacity`:
 *
 *  - `capacity` — the fixed number of bytes the backing storage holds;
 *  - `limit`    — the first index that must not be read or written;
 *  - `position` — the index of the next byte to be read or written.
 *
 * Buffers are used in two alternating modes.  In *fill* mode (`clear()`)
 * `limit` equals `capacity` and `put()` advances `position`.  Calling
 * `flip()` switches to *drain* mode: `limit` takes the current `position`
 * and `position` returns to 0, so `get()` walks exactly the bytes that were
 * written.  `compact()` returns to fill mode while preserving the bytes that
 * were not consumed.
 *
 * Channel and stream operations use the very same convention: a read fills
 * the buffer from `position` up to `limit`, a write drains it over the same
 * range, and both advance `position` by the number of bytes transferred.
 *
 * ByteBuffer is not thread-safe; a buffer handed to an in-flight
 * asynchronous operation must not be touched until that operation completes.
 */
public class ByteBuffer {
    _data     : byte[]!;
    _position : unsigned int;
    _limit    : unsigned int;
    _capacity : unsigned int;

public:
    /** Construct an empty buffer with no capacity. */
    ByteBuffer() : _data(null), _position(0), _limit(0), _capacity(0) {
        _data = new byte[0];
    }

    /**
     * Construct a buffer with the given capacity, in fill mode.
     *
     * @param capacity Number of bytes the buffer can hold.
     */
    ByteBuffer(capacity: unsigned int) : _data(null), _position(0),
                                         _limit(capacity), _capacity(capacity) {
        _data = new byte[capacity];
    }

    /**
     * Allocate a zero-filled buffer in fill mode.
     *
     * @param capacity Number of bytes the buffer can hold.
     * @return A newly allocated buffer with `position` 0 and `limit` equal to
     *         `capacity`.
     */
    static allocate(capacity: unsigned int) : ByteBuffer! {
        return new ByteBuffer(capacity);
    }

    /**
     * Allocate a buffer holding a copy of `array`, ready to be drained.
     *
     * @param array Source bytes; copied into the new buffer.
     * @return A buffer with `position` 0 and `limit` equal to the array size.
     */
    static wrap(array: const byte[]) : ByteBuffer! {
        n : unsigned int = (unsigned int) array.size;
        buf : ByteBuffer! = new ByteBuffer(n);
        buf->put(array);
        buf->flip();
        return buf;
    }

    // ── Geometry ─────────────────────────────────────────────────────────

    /** @return The index of the next byte to be read or written. */
    const position() : unsigned int {
        return _position;
    }

    /**
     * Move the cursor.
     *
     * @param newPosition New cursor value; must not exceed `limit`.
     * @return This buffer.
     */
    position(newPosition: unsigned int) : ByteBuffer& {
        if (newPosition > _limit) {
            throw IndexOutOfBoundsError();
        }
        _position = newPosition;
        return this;
    }

    /** @return The first index that must not be read or written. */
    const limit() : unsigned int {
        return _limit;
    }

    /**
     * Move the limit, clamping the cursor when it would fall outside.
     *
     * @param newLimit New limit; must not exceed `capacity`.
     * @return This buffer.
     */
    limit(newLimit: unsigned int) : ByteBuffer& {
        if (newLimit > _capacity) {
            throw IndexOutOfBoundsError();
        }
        _limit = newLimit;
        if (_position > _limit) {
            _position = _limit;
        }
        return this;
    }

    /** @return The fixed number of bytes the backing storage holds. */
    const capacity() : unsigned int {
        return _capacity;
    }

    /** @return The number of bytes between `position` and `limit`. */
    const remaining() : unsigned int {
        if (_position >= _limit) {
            return 0;
        }
        return _limit - _position;
    }

    /** @return `true` when at least one byte lies between cursor and limit. */
    const hasRemaining() : bool {
        return _position < _limit;
    }

    // ── Mode switching ───────────────────────────────────────────────────

    /**
     * Switch to drain mode: `limit` becomes the current `position`, and the
     * cursor returns to 0.
     *
     * @return This buffer.
     */
    flip() : ByteBuffer& {
        _limit = _position;
        _position = 0;
        return this;
    }

    /**
     * Switch to fill mode over the whole capacity, discarding any content.
     *
     * @return This buffer.
     */
    clear() : ByteBuffer& {
        _position = 0;
        _limit = _capacity;
        return this;
    }

    /**
     * Rewind the cursor to 0, leaving `limit` untouched, so the same range
     * can be walked again.
     *
     * @return This buffer.
     */
    rewind() : ByteBuffer& {
        _position = 0;
        return this;
    }

    /**
     * Move the unread bytes to the front of the buffer and switch back to
     * fill mode, so a partially consumed buffer can be topped up.
     *
     * @return This buffer.
     */
    compact() : ByteBuffer& {
        n : unsigned int = remaining();
        i : unsigned int = 0;
        while (i < n) {
            _data[i] = _data[_position + i];
            i = i + 1;
        }
        _position = n;
        _limit = _capacity;
        return this;
    }

    // ── Relative access ──────────────────────────────────────────────────

    /**
     * Read the byte at the cursor and advance it.
     *
     * @return The byte read.
     */
    get() : byte {
        if (_position >= _limit) {
            throw IndexOutOfBoundsError();
        }
        b : byte = _data[_position];
        _position = _position + 1;
        return b;
    }

    /**
     * Write a byte at the cursor and advance it.
     *
     * @param b Byte to write.
     * @return This buffer.
     */
    put(b: byte) : ByteBuffer& {
        if (_position >= _limit) {
            throw IndexOutOfBoundsError();
        }
        _data[_position] = b;
        _position = _position + 1;
        return this;
    }

    /**
     * Copy `src` at the cursor and advance it by the array size.
     *
     * @param src Source bytes.
     * @return This buffer.
     */
    put(src: const byte[]) : ByteBuffer& {
        n : unsigned int = (unsigned int) src.size;
        if (n > remaining()) {
            throw IndexOutOfBoundsError();
        }
        i : unsigned int = 0;
        while (i < n) {
            _data[_position + i] = src[i];
            i = i + 1;
        }
        _position = _position + n;
        return this;
    }

    // ── Absolute access ──────────────────────────────────────────────────

    /**
     * Read the byte at `index` without moving the cursor.
     *
     * @param index Absolute index, below `limit`.
     * @return The byte read.
     */
    const get(index: unsigned int) : byte {
        if (index >= _limit) {
            throw IndexOutOfBoundsError();
        }
        return _data[index];
    }

    /**
     * Write a byte at `index` without moving the cursor.
     *
     * @param index Absolute index, below `limit`.
     * @param b Byte to write.
     * @return This buffer.
     */
    put(index: unsigned int, b: byte) : ByteBuffer& {
        if (index >= _limit) {
            throw IndexOutOfBoundsError();
        }
        _data[index] = b;
        return this;
    }

    // ── Bulk transfer ────────────────────────────────────────────────────

    /**
     * Copy up to `len` remaining bytes into `dst` starting at `off`, and
     * advance the cursor by the number of bytes copied.
     *
     * @param dst Destination array.
     * @param off Destination start offset.
     * @param len Maximum number of bytes to copy.
     * @return The number of bytes actually copied.
     */
    get(dst: byte[], off: unsigned int, len: unsigned int) : unsigned int {
        n : unsigned int = remaining();
        if (n > len) {
            n = len;
        }
        i : unsigned int = 0;
        while (i < n) {
            dst[off + i] = _data[_position + i];
            i = i + 1;
        }
        _position = _position + n;
        return n;
    }

    /**
     * Copy up to `len` bytes from `src` starting at `off` into the remaining
     * space, and advance the cursor by the number of bytes copied.
     *
     * @param src Source array.
     * @param off Source start offset.
     * @param len Maximum number of bytes to copy.
     * @return The number of bytes actually copied.
     */
    put(src: const byte[], off: unsigned int, len: unsigned int) : unsigned int {
        n : unsigned int = remaining();
        if (n > len) {
            n = len;
        }
        i : unsigned int = 0;
        while (i < n) {
            _data[_position + i] = src[off + i];
            i = i + 1;
        }
        _position = _position + n;
        return n;
    }

    /**
     * Return a copy of the bytes between `position` and `limit`, leaving the
     * cursor untouched.
     *
     * @return A newly allocated array holding the remaining bytes.
     */
    const toArray() : byte[]! {
        n : unsigned int = remaining();
        out : byte[]! = new byte[n];
        i : unsigned int = 0;
        while (i < n) {
            out[i] = _data[_position + i];
            i = i + 1;
        }
        return out;
    }

    // ── Raw access (internal, used by channels) ──────────────────────────

    /**
     * Return a pointer to the byte at `index` in the backing storage.
     * Used by the asynchronous I/O layer to hand a destination to the kernel.
     *
     * @param index Absolute index into the backing storage.
     * @return Pointer to the requested byte.
     */
    dataAt(index: unsigned int) : byte* {
        return &_data[index];
    }
}

} // namespace io
