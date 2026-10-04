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
 * NativeByteBuffer — opaque handle representing a reference-counted native buffer
 * managing heap, direct (page-aligned), or memory-mapped backing storage.
 */
public struct NativeByteBuffer {
}

// ── Native Buffer FFI Declarations ─────────────────────────────────────────

@ffi::Extern("C") private __k_io_buffer_allocate(capacity: unsigned int) : NativeByteBuffer*;
@ffi::Extern("C") private __k_io_buffer_allocate_direct(capacity: unsigned int) : NativeByteBuffer*;
@ffi::Extern("C") private __k_io_buffer_map(k_path: const char*, fd: int, offset: long, length: long, mode: int) : NativeByteBuffer*;
@ffi::Extern("C") private __k_io_buffer_retain(native: NativeByteBuffer*) : void;
@ffi::Extern("C") private __k_io_buffer_release(native: NativeByteBuffer*) : void;
@ffi::Extern("C") private __k_io_buffer_acquire_lease(native: NativeByteBuffer*) : void;
@ffi::Extern("C") private __k_io_buffer_release_lease(native: NativeByteBuffer*) : void;
@ffi::Extern("C") private __k_io_buffer_has_lease(native: const NativeByteBuffer*) : int;
@ffi::Extern("C") private __k_io_buffer_get_lease_count(native: const NativeByteBuffer*) : unsigned int;
@ffi::Extern("C") private __k_io_buffer_get_capacity(native: const NativeByteBuffer*) : unsigned int;
@ffi::Extern("C") private __k_io_buffer_get_position(native: const NativeByteBuffer*) : unsigned int;
@ffi::Extern("C") private __k_io_buffer_get_limit(native: const NativeByteBuffer*) : unsigned int;
@ffi::Extern("C") private __k_io_buffer_set_position(native: NativeByteBuffer*, new_pos: unsigned int) : int;
@ffi::Extern("C") private __k_io_buffer_set_limit(native: NativeByteBuffer*, new_limit: unsigned int) : int;
@ffi::Extern("C") private __k_io_buffer_flip(native: NativeByteBuffer*) : int;
@ffi::Extern("C") private __k_io_buffer_clear(native: NativeByteBuffer*) : int;
@ffi::Extern("C") private __k_io_buffer_rewind(native: NativeByteBuffer*) : int;
@ffi::Extern("C") private __k_io_buffer_compact(native: NativeByteBuffer*) : int;
@ffi::Extern("C") private __k_io_buffer_data_at(native: NativeByteBuffer*, index: unsigned int) : byte*;
@ffi::Extern("C") private __k_io_buffer_get_byte(native: const NativeByteBuffer*, index: unsigned int, out_byte: byte*) : int;
@ffi::Extern("C") private __k_io_buffer_put_byte(native: NativeByteBuffer*, index: unsigned int, b: byte) : int;
@ffi::Extern("C") private __k_io_buffer_copy_from(native: NativeByteBuffer*, pos: unsigned int, src: const byte*, len: unsigned int) : int;
@ffi::Extern("C") private __k_io_buffer_copy_to(native: const NativeByteBuffer*, pos: unsigned int, dst: byte*, len: unsigned int) : int;
@ffi::Extern("C") private __k_io_buffer_is_registered(native: const NativeByteBuffer*) : int;
@ffi::Extern("C") private __k_io_buffer_register(native: NativeByteBuffer*) : int;
@ffi::Extern("C") private __k_io_buffer_unregister(native: NativeByteBuffer*) : int;
@ffi::Extern("C") private __k_io_buffer_force(native: NativeByteBuffer*) : int;
@ffi::Extern("C") private __k_io_buffer_load(native: NativeByteBuffer*) : int;
@ffi::Extern("C") private __k_io_buffer_is_loaded(native: const NativeByteBuffer*) : int;
@ffi::Extern("C") private __k_io_buffer_relative(native: NativeByteBuffer*, bytes: byte*, length: unsigned int, write: int) : int;

/**
 * ByteBuffer — a fixed-capacity byte container with an explicit cursor backed
 * by a reference-counted NativeByteBuffer.
 *
 * Backed by native storage (heap, direct, or memory-mapped).
 * Supports in-flight lease tracking: any attempt to mutate position, limit,
 * content, compact or remap while leases are active throws BufferInUseException.
 */
public class ByteBuffer {
protected:
    _native : NativeByteBuffer*;

    protected checkNotLeased() : void {
        if (_native != null && __k_io_buffer_has_lease(_native) != 0) {
            throw BufferInUseException();
        }
    }

public:
    /** Construct an empty buffer with zero capacity. */
    ByteBuffer() : _native(null) {
        _native = __k_io_buffer_allocate(0u);
    }

    /**
     * Construct a buffer with the given capacity, in fill mode.
     *
     * @param capacity Number of bytes the buffer can hold.
     */
    ByteBuffer(capacity: unsigned int) : _native(null) {
        _native = __k_io_buffer_allocate(capacity);
        if (_native == null) {
            throw OutOfMemory();
        }
    }

    /**
     * Internal constructor wrapping an already allocated native buffer.
     */
    protected ByteBuffer(nat: NativeByteBuffer*) : _native(nat) {}

    ~ByteBuffer() {
        if (_native != null) {
            __k_io_buffer_release(_native);
            _native = null;
        }
    }

    /**
     * Allocate a zero-filled heap buffer in fill mode.
     *
     * @param capacity Number of bytes the buffer can hold.
     * @return A newly allocated buffer with position 0 and limit equal to capacity.
     */
    static allocate(capacity: unsigned int) : ByteBuffer! {
        return new ByteBuffer(capacity);
    }

    /**
     * Allocate a page-aligned direct buffer in fill mode.
     *
     * @param capacity Number of bytes the buffer can hold.
     * @return A newly allocated direct buffer.
     */
    static allocateDirect(capacity: unsigned int) : ByteBuffer! {
        nat : NativeByteBuffer* = __k_io_buffer_allocate_direct(capacity);
        if (nat == null) {
            throw OutOfMemory();
        }
        return new ByteBuffer(nat);
    }

    /**
     * Allocate a buffer holding a copy of array, ready to be drained.
     *
     * @param array Source bytes; copied into the new buffer.
     * @return A buffer with position 0 and limit equal to array size.
     */
    static wrap(array: const byte[]) : ByteBuffer! {
        n : unsigned int = (unsigned int) array.size;
        buf : ByteBuffer! = ByteBuffer::allocate(n);
        buf->put(array);
        buf->flip();
        return buf;
    }

    // ── Geometry ─────────────────────────────────────────────────────────

    /** @return The index of the next byte to be read or written. */
    const position() : unsigned int {
        if (_native == null) {
            return 0u;
        }
        return __k_io_buffer_get_position(_native);
    }

    /**
     * Move the cursor.
     *
     * @param newPosition New cursor value; must not exceed limit.
     * @return This buffer.
     */
    position(newPosition: unsigned int) : ByteBuffer& {
        checkNotLeased();
        lim : unsigned int = limit();
        if (newPosition > lim) {
            throw IndexOutOfBoundsError();
        }
        if (_native != null) {
            rc : int = __k_io_buffer_set_position(_native, newPosition);
            if (rc == -16) {
                throw BufferInUseException();
            } else if (rc < 0) {
                throw IndexOutOfBoundsError();
            }
        }
        return this;
    }

    /** @return The first index that must not be read or written. */
    const limit() : unsigned int {
        if (_native == null) {
            return 0u;
        }
        return __k_io_buffer_get_limit(_native);
    }

    /**
     * Move the limit, clamping the cursor when it would fall outside.
     *
     * @param newLimit New limit; must not exceed capacity.
     * @return This buffer.
     */
    limit(newLimit: unsigned int) : ByteBuffer& {
        checkNotLeased();
        cap : unsigned int = capacity();
        if (newLimit > cap) {
            throw IndexOutOfBoundsError();
        }
        if (_native != null) {
            rc : int = __k_io_buffer_set_limit(_native, newLimit);
            if (rc == -16) {
                throw BufferInUseException();
            } else if (rc < 0) {
                throw IndexOutOfBoundsError();
            }
        }
        return this;
    }

    /** @return The fixed number of bytes the backing storage holds. */
    const capacity() : unsigned int {
        if (_native == null) {
            return 0u;
        }
        return __k_io_buffer_get_capacity(_native);
    }

    /** @return The number of bytes between position and limit. */
    const remaining() : unsigned int {
        pos : unsigned int = position();
        lim : unsigned int = limit();
        if (pos >= lim) {
            return 0u;
        }
        return lim - pos;
    }

    /** @return `true` when at least one byte lies between cursor and limit. */
    const hasRemaining() : bool {
        return position() < limit();
    }

    // ── Mode switching ───────────────────────────────────────────────────

    /**
     * Switch to drain mode: limit becomes the current position, and the
     * cursor returns to 0.
     *
     * @return This buffer.
     */
    flip() : ByteBuffer& {
        checkNotLeased();
        if (_native != null) {
            rc : int = __k_io_buffer_flip(_native);
            if (rc == -16) {
                throw BufferInUseException();
            }
        }
        return this;
    }

    /**
     * Switch to fill mode over the whole capacity, discarding any content.
     *
     * @return This buffer.
     */
    clear() : ByteBuffer& {
        checkNotLeased();
        if (_native != null) {
            rc : int = __k_io_buffer_clear(_native);
            if (rc == -16) {
                throw BufferInUseException();
            }
        }
        return this;
    }

    /**
     * Rewind the cursor to 0, leaving limit untouched.
     *
     * @return This buffer.
     */
    rewind() : ByteBuffer& {
        checkNotLeased();
        if (_native != null) {
            rc : int = __k_io_buffer_rewind(_native);
            if (rc == -16) {
                throw BufferInUseException();
            }
        }
        return this;
    }

    /**
     * Move unread bytes to the front of the buffer and switch back to fill mode.
     *
     * @return This buffer.
     */
    compact() : ByteBuffer& {
        checkNotLeased();
        if (_native != null) {
            rc : int = __k_io_buffer_compact(_native);
            if (rc == -16) {
                throw BufferInUseException();
            }
        }
        return this;
    }

    // ── Relative access ──────────────────────────────────────────────────

    /**
     * Read the byte at the cursor and advance it.
     *
     * @return The byte read.
     */
    get() : byte {
        b : byte = (byte) 0;
        rc : int = __k_io_buffer_relative(_native, &b, 1u, 0);
        if (rc == -16) {
            throw BufferInUseException();
        } else if (rc < 0) {
            throw IndexOutOfBoundsError();
        }
        return b;
    }

    /**
     * Write a byte at the cursor and advance it.
     *
     * @param b Byte to write.
     * @return This buffer.
     */
    put(b: byte) : ByteBuffer& {
        rc : int = __k_io_buffer_relative(_native, &b, 1u, 1);
        if (rc == -16) {
            throw BufferInUseException();
        } else if (rc < 0) {
            throw IndexOutOfBoundsError();
        }
        return this;
    }

    /**
     * Copy src at the cursor and advance it by array size.
     *
     * @param src Source bytes.
     * @return This buffer.
     */
    put(src: const byte[]) : ByteBuffer& {
        checkNotLeased();
        n : unsigned int = (unsigned int) src.size;
        if (n > remaining()) {
            throw IndexOutOfBoundsError();
        }
        if (n > 0u && _native != null) {
            rc : int = __k_io_buffer_relative(_native, (byte*) &src[0], n, 1);
            if (rc == -16) {
                throw BufferInUseException();
            } else if (rc < 0) {
                throw IndexOutOfBoundsError();
            }
        }
        return this;
    }

    // ── Absolute access ──────────────────────────────────────────────────

    /**
     * Read the byte at index without moving the cursor.
     *
     * @param index Absolute index, below limit.
     * @return The byte read.
     */
    const get(index: unsigned int) : byte {
        lim : unsigned int = limit();
        if (index >= lim) {
            throw IndexOutOfBoundsError();
        }
        b : byte = (byte) 0;
        rc : int = __k_io_buffer_get_byte(_native, index, &b);
        if (rc == -16) throw BufferInUseException();
        if (rc < 0) {
            throw IndexOutOfBoundsError();
        }
        return b;
    }

    /**
     * Write a byte at index without moving the cursor.
     *
     * @param index Absolute index, below limit.
     * @param b Byte to write.
     * @return This buffer.
     */
    put(index: unsigned int, b: byte) : ByteBuffer& {
        checkNotLeased();
        lim : unsigned int = limit();
        if (index >= lim) {
            throw IndexOutOfBoundsError();
        }
        rc : int = __k_io_buffer_put_byte(_native, index, b);
        if (rc == -16) {
            throw BufferInUseException();
        } else if (rc < 0) {
            throw IndexOutOfBoundsError();
        }
        return this;
    }

    // ── Bulk transfer ────────────────────────────────────────────────────

    /**
     * Copy up to len remaining bytes into dst starting at off, and advance cursor.
     *
     * @param dst Destination array.
     * @param off Destination start offset.
     * @param len Maximum number of bytes to copy.
     * @return Number of bytes actually copied.
     */
    get(dst: byte[], off: unsigned int, len: unsigned int) : unsigned int {
        checkNotLeased();
        if (off > dst.size || len > dst.size - off) throw IndexOutOfBoundsError();
        n : unsigned int = remaining();
        if (n > len) {
            n = len;
        }
        if (n > 0u && _native != null) {
            rc : int = __k_io_buffer_relative(_native, &dst[off], n, 0);
            if (rc == -16) {
                throw BufferInUseException();
            } else if (rc < 0) {
                throw IndexOutOfBoundsError();
            }
        }
        return n;
    }

    /**
     * Copy up to len bytes from src starting at off into remaining space, and advance cursor.
     *
     * @param src Source array.
     * @param off Source start offset.
     * @param len Maximum number of bytes to copy.
     * @return Number of bytes actually copied.
     */
    put(src: const byte[], off: unsigned int, len: unsigned int) : unsigned int {
        checkNotLeased();
        if (off > src.size || len > src.size - off) throw IndexOutOfBoundsError();
        n : unsigned int = remaining();
        if (n > len) {
            n = len;
        }
        if (n > 0u && _native != null) {
            rc : int = __k_io_buffer_relative(_native, (byte*) &src[off], n, 1);
            if (rc == -16) {
                throw BufferInUseException();
            } else if (rc < 0) {
                throw IndexOutOfBoundsError();
            }
        }
        return n;
    }

    /**
     * Return a copy of bytes between position and limit, leaving cursor untouched.
     *
     * @return Newly allocated array holding remaining bytes.
     */
    const toArray() : byte[]! {
        n : unsigned int = remaining();
        out : byte[]! = new byte[n];
        if (n > 0u && _native != null) {
            pos : unsigned int = position();
            rc : int = __k_io_buffer_copy_to(_native, pos, &out[0], n);
            if (rc == -16) throw BufferInUseException();
            if (rc < 0) throw IndexOutOfBoundsError();
        }
        return out;
    }

    // ── Raw & Internal access ────────────────────────────────────────────

    /**
     * Return pointer to byte at index in backing storage.
     *
     * @param index Absolute index.
     * @return Pointer to byte.
     */
    dataAt(index: unsigned int) : byte* {
        checkNotLeased();
        if (_native == null) {
            return null;
        }
        return __k_io_buffer_data_at(_native, index);
    }

    /**
     * Return underlying native buffer handle.
     */
    nativeHandle() : NativeByteBuffer* {
        return _native;
    }

    // ── Leases & Registration ────────────────────────────────────────────

    /**
     * Tell whether this buffer is currently registered in io_uring sparse buffer table.
     */
    const isRegistered() : bool {
        if (_native == null) {
            return false;
        }
        return __k_io_buffer_is_registered(_native) != 0;
    }

    /**
     * Tell whether an in-flight I/O lease is active on this buffer.
     */
    const hasLease() : bool {
        if (_native == null) {
            return false;
        }
        return __k_io_buffer_has_lease(_native) != 0;
    }

    /**
     * Return current in-flight lease counter.
     */
    const leaseCount() : unsigned int {
        if (_native == null) {
            return 0u;
        }
        return __k_io_buffer_get_lease_count(_native);
    }

    /**
     * Manually acquire a lease on this buffer.
     */
    acquireLease() : void {
        if (_native != null) {
            __k_io_buffer_acquire_lease(_native);
        }
    }

    /**
     * Manually release a lease on this buffer.
     */
    releaseLease() : void {
        if (_native != null) {
            __k_io_buffer_release_lease(_native);
        }
    }
}

} // namespace io
