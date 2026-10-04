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
 * MapMode — memory mapping protection and sharing mode.
 */
public enum MapMode {
    ReadOnly;
    ReadWrite;
    Private;
}

/**
 * MappedByteBuffer — a direct byte buffer backed by a memory-mapped file region.
 *
 * Created via MappedByteBuffer::map(...).
 * Supports automatic sparse registration with io_uring with transparent fallback
 * to non-fixed I/O upon table exhaustion or memory limits.
 */
public class MappedByteBuffer : public ByteBuffer {
protected:
    MappedByteBuffer(nat: NativeByteBuffer*) : ByteBuffer(nat) {}

public:
    /**
     * Map a region of an open file descriptor.
     *
     * @param fd File descriptor to map (-1 for anonymous mapping).
     * @param offset Byte offset in the file; need not be page-aligned.
     * @param length Number of bytes to map.
     * @param mode Mapping mode (ReadOnly, ReadWrite, Private).
     * @return Newly created mapped byte buffer.
     */
    static map(fd: int, offset: long, length: long, mode: MapMode) : MappedByteBuffer! {
        if (length <= 0L || length > 4294967295L || offset < 0L) {
            throw IllegalArgumentException();
        }
        m : int = 0;
        if (mode == MapMode::ReadOnly) {
            m = 0;
        } else if (mode == MapMode::ReadWrite) {
            m = 1;
        } else if (mode == MapMode::Private) {
            m = 2;
        }
        nat : NativeByteBuffer* = __k_io_buffer_map(null, fd, offset, length, m);
        if (nat == null) {
            throw IOException();
        }
        return new MappedByteBuffer(nat);
    }

    /**
     * Map a file region given by a character array path.
     *
     * @param path File system path as character array.
     * @param offset Byte offset in the file; need not be page-aligned.
     * @param length Number of bytes to map.
     * @param mode Mapping mode (ReadOnly, ReadWrite, Private).
     * @return Newly created mapped byte buffer.
     */
    static map(path: const char[], offset: long, length: long, mode: MapMode) : MappedByteBuffer! {
        if (path.size == 0u || length <= 0L || length > 4294967295L || offset < 0L) {
            throw IllegalArgumentException();
        }
        m : int = 0;
        if (mode == MapMode::ReadOnly) {
            m = 0;
        } else if (mode == MapMode::ReadWrite) {
            m = 1;
        } else if (mode == MapMode::Private) {
            m = 2;
        }
        terminated : char[]! = new char[path.size + 1u];
        i : unsigned int = 0u;
        while (i < path.size) {
            terminated[i] = path[i];
            ++i;
        }
        terminated[path.size] = (char) 0;
        nat : NativeByteBuffer* = __k_io_buffer_map(&terminated[0], -1, offset, length, m);
        if (nat == null) {
            throw IOException();
        }
        return new MappedByteBuffer(nat);
    }

    /**
     * Map a file region given by a null-terminated character pointer.
     *
     * @param path File system path as character pointer.
     * @param offset Byte offset in the file; need not be page-aligned.
     * @param length Number of bytes to map.
     * @param mode Mapping mode (ReadOnly, ReadWrite, Private).
     * @return Newly created mapped byte buffer.
     */
    static map(path: const char*, offset: long, length: long, mode: MapMode) : MappedByteBuffer! {
        if (path == null || length <= 0L || length > 4294967295L || offset < 0L) {
            throw IllegalArgumentException();
        }
        m : int = 0;
        if (mode == MapMode::ReadOnly) {
            m = 0;
        } else if (mode == MapMode::ReadWrite) {
            m = 1;
        } else if (mode == MapMode::Private) {
            m = 2;
        }
        nat : NativeByteBuffer* = __k_io_buffer_map(path, -1, offset, length, m);
        if (nat == null) {
            throw IOException();
        }
        return new MappedByteBuffer(nat);
    }

    /**
     * Map a file region given by a Path object.
     *
     * @param path File system Path.
     * @param offset Byte offset in the file; need not be page-aligned.
     * @param length Number of bytes to map.
     * @param mode Mapping mode (ReadOnly, ReadWrite, Private).
     * @return Newly created mapped byte buffer.
     */
    static map(path: const file::Path&, offset: long, length: long, mode: MapMode) : MappedByteBuffer! {
        return MappedByteBuffer::map(path.nativePath(), offset, length, mode);
    }

    /**
     * Flush modifications made to this buffer to the underlying storage device.
     */
    force() : void {
        checkNotLeased();
        if (_native != null) {
            rc : int = __k_io_buffer_force(_native);
            if (rc < 0) {
                throw IOException(rc);
            }
        }
    }

    /**
     * Prefault pages of this buffer into physical memory.
     */
    load() : void {
        checkNotLeased();
        if (_native != null) {
            rc : int = __k_io_buffer_load(_native);
            if (rc < 0) throw IOException(-rc);
        }
    }

    /**
     * Tell whether this buffer's content is completely resident in physical memory.
     *
     * @return `true` if all pages are resident in RAM.
     */
    const isLoaded() : bool {
        if (_native == null) {
            return false;
        }
        return __k_io_buffer_is_loaded(_native) != 0;
    }

    /**
     * Manually attempt registration in the io_uring sparse buffer table.
     *
     * @return `true` if registration succeeded, `false` on fallback non-fixed.
     */
    registerBuffer() : bool {
        if (_native == null) {
            return false;
        }
        return __k_io_buffer_register(_native) != 0;
    }

    /**
     * Unregister buffer from io_uring sparse buffer table.
     * If an I/O lease is currently active, unregistration is deferred until
     * lease count drops to zero.
     *
     * @return `true` if unregistration was performed or successfully deferred.
     */
    unregisterBuffer() : bool {
        if (_native == null) {
            return false;
        }
        return __k_io_buffer_unregister(_native) != 0;
    }
}

} // namespace io
