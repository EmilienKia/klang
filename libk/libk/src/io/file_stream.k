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
 * AsyncFileInputStream — a byte input stream backed by ::k::io::FileChannel.
 *
 * Unlike ::k::io::FileInputStream, which sits on top of the C standard
 * library, this stream uses the asynchronous substrate: a blocked read can be
 * cancelled by ::k::Thread::interrupt(), and closing the stream from another
 * thread aborts the pending operation.
 *
 * The stream owns its channel and closes it on `close()`.
 */
public class AsyncFileInputStream : public InputStream<byte> {
    _chan : FileChannel!;

public:
    /**
     * Open a file for reading.
     *
     * @param path Location to read from.
     */
    AsyncFileInputStream(path: const Path&) : _chan(null) {
        _chan = FileChannel::open(path, OPEN_READ);
    }

    /**
     * Adopt an already open channel.
     * The stream takes ownership and closes the channel on `close()`.
     *
     * @param chan Channel to adopt.
     */
    AsyncFileInputStream(chan: FileChannel!) : _chan(chan) {}

    ~AsyncFileInputStream() {
        close();
    }

    /**
     * Tell whether the underlying channel is still usable.
     *
     * @return `true` while the channel is open.
     */
    const isOpen() : bool {
        if (_chan == null) {
            return false;
        }
        return _chan->isOpen();
    }

    /**
     * Return the underlying channel, for positional access.
     *
     * @return The channel, or null once the stream is closed.
     */
    const channel() : FileChannel* {
        return _chan;
    }

    // ── InputStream<byte> ────────────────────────────────────────────────

    override read() : ::k::Optional<byte> {
        if (_chan == null) {
            return ::k::Optional<byte>();
        }
        one : byte[1];
        buf : ByteBuffer! = ByteBuffer::allocate(1u);
        n : int = 0;
        try {
            n = _chan->read(buf);
        } catch (e: IOException&) {
            delete buf;
            throw;
        }
        if (n <= 0) {
            delete buf;
            return ::k::Optional<byte>();
        }
        buf->flip();
        one[0] = buf->get();
        delete buf;
        result : ::k::Optional<byte>;
        result.set(one[0]);
        return result;
    }

    override read(buf: byte[], off: unsigned int, len: unsigned int)
            : Expected<unsigned int, ::k::io::StreamOutOfData> {
        if (_chan == null) {
            err : ::k::io::StreamOutOfData = ::k::io::StreamOutOfData::Closed;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::error(err);
        }
        if (len == 0u) {
            zero : unsigned int = 0u;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(zero);
        }
        tmp : ByteBuffer! = ByteBuffer::allocate(len);
        n : int = 0;
        try {
            n = _chan->read(tmp);
        } catch (e: IOException&) {
            delete tmp;
            throw;
        }
        if (n > 0) {
            tmp->flip();
            got : unsigned int = (unsigned int) n;
            i : unsigned int = 0u;
            while (i < got) {
                buf[off + i] = tmp->get(i);
                ++i;
            }
        }
        delete tmp;
        actual : unsigned int = (unsigned int) n;
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(actual);
    }

    override read(buf: byte[]) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return read(buf, 0u, (unsigned int) buf.size);
    }

    override skip(n: unsigned long) : unsigned long {
        if (_chan == null) {
            return 0;
        }
        size : long = _chan->size();
        pos : long = _chan->position();
        remaining : long = size - pos;
        if (remaining < 0L) {
            remaining = 0L;
        }
        wanted : long = (long) n;
        if (wanted > remaining) {
            wanted = remaining;
        }
        _chan->position(pos + wanted);
        return (unsigned long) wanted;
    }

    override available() : Expected<unsigned int, ::k::io::StreamOutOfData> {
        if (_chan == null) {
            err : ::k::io::StreamOutOfData = ::k::io::StreamOutOfData::Closed;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::error(err);
        }
        remaining : long = _chan->size() - _chan->position();
        if (remaining < 0L) {
            remaining = 0L;
        }
        actual : unsigned int = (unsigned int) remaining;
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(actual);
    }

    override close() {
        if (_chan != null) {
            _chan->close();
            delete _chan;
            _chan = null;
        }
    }
}


/**
 * AsyncFileOutputStream — a byte output stream backed by
 * ::k::io::FileChannel.
 *
 * Writes are issued through the asynchronous substrate, so a blocked write
 * can be cancelled by ::k::Thread::interrupt(). The stream owns its channel
 * and closes it on `close()`.
 */
public class AsyncFileOutputStream : public OutputStream<byte> {
    _chan : FileChannel!;

public:
    /**
     * Open a file for writing, creating and truncating it as needed.
     *
     * @param path Location to write to.
     */
    AsyncFileOutputStream(path: const Path&) : _chan(null) {
        _chan = FileChannel::open(path, OPEN_WRITE | OPEN_CREATE | OPEN_TRUNCATE);
    }

    /**
     * Open a file for writing.
     *
     * @param path Location to write to.
     * @param append When `true`, keep the existing content and write past it.
     */
    AsyncFileOutputStream(path: const Path&, append: bool) : _chan(null) {
        opts : int = OPEN_WRITE | OPEN_CREATE;
        if (append) {
            opts |= OPEN_APPEND;
        } else {
            opts |= OPEN_TRUNCATE;
        }
        _chan = FileChannel::open(path, opts);
        if (append) {
            _chan->position(_chan->size());
        }
    }

    /**
     * Adopt an already open channel.
     * The stream takes ownership and closes the channel on `close()`.
     *
     * @param chan Channel to adopt.
     */
    AsyncFileOutputStream(chan: FileChannel!) : _chan(chan) {}

    ~AsyncFileOutputStream() {
        if (_chan != null) {
            _chan->close();
            delete _chan;
            _chan = null;
        }
    }

    /**
     * Tell whether the underlying channel is still usable.
     *
     * @return `true` while the channel is open.
     */
    const isOpen() : bool {
        if (_chan == null) {
            return false;
        }
        return _chan->isOpen();
    }

    /**
     * Return the underlying channel, for positional access.
     *
     * @return The channel, or null once the stream is closed.
     */
    const channel() : FileChannel* {
        return _chan;
    }

    // ── OutputStream<byte> ───────────────────────────────────────────────

    /** Raise ClosedChannelException; kept out of the fluent writers. */
    static requireOpen(chan: const FileChannel*) : void {
        if (chan == null) {
            throw ClosedChannelException();
        }
    }

    override write(b: byte) : OutputStream<byte>& {
        AsyncFileOutputStream::requireOpen(_chan);
        buf : ByteBuffer! = ByteBuffer::allocate(1u);
        buf->put(b);
        buf->flip();
        try {
            _chan->writeFully(buf, _chan->position());
        } catch (e: IOException&) {
            delete buf;
            throw;
        }
        _chan->position(_chan->position() + 1L);
        delete buf;
        return this;
    }

    override write(buf: const byte[], off: unsigned int, len: unsigned int)
            : OutputStream<byte>& {
        AsyncFileOutputStream::requireOpen(_chan);
        if (len == 0u) {
            return this;
        }
        tmp : ByteBuffer! = ByteBuffer::allocate(len);
        i : unsigned int = 0u;
        while (i < len) {
            tmp->put(buf[off + i]);
            ++i;
        }
        tmp->flip();
        try {
            _chan->writeFully(tmp, _chan->position());
        } catch (e: IOException&) {
            delete tmp;
            throw;
        }
        _chan->position(_chan->position() + (long) len);
        delete tmp;
        return this;
    }

    override write(buf: const byte[]) : OutputStream<byte>& {
        return write(buf, 0u, (unsigned int) buf.size);
    }

    override flush() : OutputStream<byte>& {
        if (_chan != null) {
            _chan->force();
        }
        return this;
    }

    override close() : OutputStream<byte>& {
        if (_chan != null) {
            _chan->close();
            delete _chan;
            _chan = null;
        }
        return this;
    }
}

} // namespace io
