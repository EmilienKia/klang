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

@ffi::Extern("C") private __k_net_connect(host: const char*, port: int, timeoutNanos: long) : long;
@ffi::Extern("C") private __k_net_bind_listen(host: const char*, port: int, backlog: int, reuseAddr: int) : int;
@ffi::Extern("C") private __k_net_local_port(fd: int) : long;
@ffi::Extern("C") private __k_net_accept(serverFd: int, timeoutNanos: long) : long;
@ffi::Extern("C") private __k_net_recv(fd: int, buf: byte*, len: int, timeoutNanos: long) : long;
@ffi::Extern("C") private __k_net_send(fd: int, buf: const byte*, len: int, timeoutNanos: long) : long;
@ffi::Extern("C") private __k_net_is_open(fd: int) : int;
@ffi::Extern("C") private __k_net_close(fd: int) : int;
@ffi::Extern("C") private __k_net_dgram_bind(host: const char*, port: int, reuseAddr: int) : int;
@ffi::Extern("C") private __k_net_dgram_connect(fd: int, host: const char*, port: int, timeoutNanos: long) : long;
@ffi::Extern("C") private __k_net_dgram_sendto(fd: int, host: const char*, port: int, buf: const byte*, len: int, timeoutNanos: long) : long;
@ffi::Extern("C") private __k_net_unix_connect(path: const char*, timeoutNanos: long) : long;
@ffi::Extern("C") private __k_net_unix_bind_listen(path: const char*, backlog: int, reuseAddr: int) : int;
@ffi::Extern("C") private __k_net_unix_accept(serverFd: int, timeoutNanos: long) : long;
@ffi::Extern("C") private __k_net_unix_dgram_bind(path: const char*, reuseAddr: int) : int;
@ffi::Extern("C") private __k_net_unix_dgram_connect(fd: int, path: const char*, timeoutNanos: long) : long;
@ffi::Extern("C") private __k_net_unix_dgram_sendto(fd: int, path: const char*, buf: const byte*, len: int, timeoutNanos: long) : long;

/**
 * TCP byte channel.
 */
public class SocketChannel : public ReadableChannel, public WritableChannel {
protected:
    _fd : int;

public:
    SocketChannel() : _fd(-1) {}

    SocketChannel(fd: int) : _fd(fd) {}

    static connect(addr: const NetworkAddress&, timeoutNanos: long) : SocketChannel! {
        fd: long = __k_net_connect(addr.nativeHost(), addr.port(), timeoutNanos);
        if (fd >= 0L) {
            return new SocketChannel((int) fd);
        }
        SocketChannel::checkResult(fd, 0L);
        throw IOException(0);
    }

    static connect(path: const Path&, timeoutNanos: long) : SocketChannel! {
        fd: long = __k_net_unix_connect(path.nativePath(), timeoutNanos);
        if (fd >= 0L) {
            return new SocketChannel((int) fd);
        }
        SocketChannel::checkResult(fd, 0L);
        throw IOException(0);
    }

    static connect(path: const char[], timeoutNanos: long) : SocketChannel! {
        fd: long = __k_net_unix_connect(&path[0], timeoutNanos);
        if (fd >= 0L) {
            return new SocketChannel((int) fd);
        }
        SocketChannel::checkResult(fd, 0L);
        throw IOException(0);
    }

    static connect(addr: const NetworkAddress&) : SocketChannel! {
        return connect(addr, IO_NO_TIMEOUT);
    }

    static connect(path: const Path&) : SocketChannel! {
        return connect(path, IO_NO_TIMEOUT);
    }

    static connect(path: const char[]) : SocketChannel! {
        return connect(path, IO_NO_TIMEOUT);
    }

    override const isOpen() : bool {
        if (_fd < 0) {
            return false;
        }
        return __k_net_is_open(_fd) != 0;
    }

    override close() : void {
        if (_fd >= 0) {
            __k_net_close(_fd);
            _fd = -1;
        }
    }

    const nativeDescriptor() : int {
        return _fd;
    }

    read(dst: ByteBuffer+, timeoutNanos: long) : int {
        if (_fd < 0) {
            throw ClosedChannelException();
        }
        len : unsigned int = dst->remaining();
        if (len == 0u) {
            return 0;
        }
        rc : long = __k_net_recv(_fd, dst->dataAt(dst->position()), (int) len, timeoutNanos);
        SocketChannel::checkResult(rc, 0L);
        n : int = (int) rc;
        dst->position(dst->position() + (unsigned int) n);
        return n;
    }

    override read(dst: ByteBuffer+) : int {
        return read(dst, IO_NO_TIMEOUT);
    }

    write(src: ByteBuffer+, timeoutNanos: long) : int {
        if (_fd < 0) {
            throw ClosedChannelException();
        }
        len : unsigned int = src->remaining();
        if (len == 0u) {
            return 0;
        }
        rc : long = __k_net_send(_fd, src->dataAt(src->position()), (int) len, timeoutNanos);
        SocketChannel::checkResult(rc, 0L);
        n : int = (int) rc;
        src->position(src->position() + (unsigned int) n);
        return n;
    }

    override write(src: ByteBuffer+) : int {
        return write(src, IO_NO_TIMEOUT);
    }

    writeFully(src: ByteBuffer+, timeoutNanos: long) : void {
        done : long = 0L;
        while (src->hasRemaining()) {
            n : int = 0;
            try {
                n = write(src, timeoutNanos);
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
}

/**
 * Convenience wrapper over SocketChannel.
 */
public class Socket : public SocketChannel {
public:
    Socket() : SocketChannel(-1) {}
    Socket(fd: int) : SocketChannel(fd) {}

    static connect(addr: const NetworkAddress&, timeoutNanos: long) : Socket! {
        fd: long = __k_net_connect(addr.nativeHost(), addr.port(), timeoutNanos);
        if (fd >= 0L) {
            return new Socket((int) fd);
        }
        SocketChannel::checkResult(fd, 0L);
        throw IOException(0);
    }

    static connect(path: const Path&, timeoutNanos: long) : Socket! {
        fd: long = __k_net_unix_connect(path.nativePath(), timeoutNanos);
        if (fd >= 0L) {
            return new Socket((int) fd);
        }
        SocketChannel::checkResult(fd, 0L);
        throw IOException(0);
    }

    static connect(path: const char[], timeoutNanos: long) : Socket! {
        fd: long = __k_net_unix_connect(&path[0], timeoutNanos);
        if (fd >= 0L) {
            return new Socket((int) fd);
        }
        SocketChannel::checkResult(fd, 0L);
        throw IOException(0);
    }

    static connect(addr: const NetworkAddress&) : Socket! {
        return connect(addr, IO_NO_TIMEOUT);
    }

    static connect(path: const Path&) : Socket! {
        return connect(path, IO_NO_TIMEOUT);
    }

    static connect(path: const char[]) : Socket! {
        return connect(path, IO_NO_TIMEOUT);
    }
}

/**
 * Listening TCP server socket.
 */
public class ServerSocket : public Channel {
    _fd : int;

public:
    ServerSocket() : _fd(-1) {}

    ServerSocket(fd: int) : _fd(fd) {}

    static bind(addr: const NetworkAddress&, backlog: int) : ServerSocket! {
        fd : int = __k_net_bind_listen(addr.nativeHost(), addr.port(), backlog, 1);
        if (fd >= 0) {
            return new ServerSocket(fd);
        }
        throw IOException((int) (IO_ERROR_BASE - (long) fd));
    }

    static bind(path: const Path&, backlog: int) : ServerSocket! {
        fd : int = __k_net_unix_bind_listen(path.nativePath(), backlog, 1);
        if (fd >= 0) {
            return new ServerSocket(fd);
        }
        throw IOException((int) (IO_ERROR_BASE - (long) fd));
    }

    static bind(path: const char[], backlog: int) : ServerSocket! {
        fd : int = __k_net_unix_bind_listen(&path[0], backlog, 1);
        if (fd >= 0) {
            return new ServerSocket(fd);
        }
        throw IOException((int) (IO_ERROR_BASE - (long) fd));
    }

    static bind(addr: const NetworkAddress&) : ServerSocket! {
        return bind(addr, 64);
    }

    static bind(path: const Path&) : ServerSocket! {
        return bind(path, 64);
    }

    static bind(path: const char[]) : ServerSocket! {
        return bind(path, 64);
    }

    const localPort() : int {
        if (_fd < 0) {
            throw ClosedChannelException();
        }
        p : long = __k_net_local_port(_fd);
        if (p < 0L) {
            throw IOException(0);
        }
        return (int) p;
    }

    accept(timeoutNanos: long) : SocketChannel! {
        if (_fd < 0) {
            throw ClosedChannelException();
        }
        fd : long = __k_net_accept(_fd, timeoutNanos);
        if (fd >= 0L) {
            return new SocketChannel((int) fd);
        }
        SocketChannel::checkResult(fd, 0L);
        throw IOException(0);
    }

    accept() : SocketChannel! {
        return accept(IO_NO_TIMEOUT);
    }

    override const isOpen() : bool {
        if (_fd < 0) {
            return false;
        }
        return __k_net_is_open(_fd) != 0;
    }

    override close() : void {
        if (_fd >= 0) {
            __k_net_close(_fd);
            _fd = -1;
        }
    }
}

/**
 * UDP datagram socket.
 *
 * Supports both connected mode (`connect`) and one-shot destination sends
 * (`sendTo`).
 */
public class DatagramSocket : public Channel {
    _fd : int;

public:
    DatagramSocket() : _fd(-1) {}

    DatagramSocket(fd: int) : _fd(fd) {}

    static bind(addr: const NetworkAddress&) : DatagramSocket! {
        fd : int = __k_net_dgram_bind(addr.nativeHost(), addr.port(), 1);
        if (fd >= 0) {
            return new DatagramSocket(fd);
        }
        throw IOException((int) (IO_ERROR_BASE - (long) fd));
    }

    static bind(path: const Path&) : DatagramSocket! {
        fd : int = __k_net_unix_dgram_bind(path.nativePath(), 1);
        if (fd >= 0) {
            return new DatagramSocket(fd);
        }
        throw IOException((int) (IO_ERROR_BASE - (long) fd));
    }

    static bind(path: const char[]) : DatagramSocket! {
        fd : int = __k_net_unix_dgram_bind(&path[0], 1);
        if (fd >= 0) {
            return new DatagramSocket(fd);
        }
        throw IOException((int) (IO_ERROR_BASE - (long) fd));
    }

    static open() : DatagramSocket! {
        any : NetworkAddress! = NetworkAddress::any(0);
        s : DatagramSocket! = DatagramSocket::bind(any);
        delete any;
        return s;
    }

    connect(remote: const NetworkAddress&, timeoutNanos: long) : void {
        if (_fd < 0) {
            throw ClosedChannelException();
        }
        rc : long = __k_net_dgram_connect(_fd, remote.nativeHost(), remote.port(), timeoutNanos);
        SocketChannel::checkResult(rc, 0L);
    }

    connect(remote: const Path&, timeoutNanos: long) : void {
        if (_fd < 0) {
            throw ClosedChannelException();
        }
        rc : long = __k_net_unix_dgram_connect(_fd, remote.nativePath(), timeoutNanos);
        SocketChannel::checkResult(rc, 0L);
    }

    connect(remote: const char[], timeoutNanos: long) : void {
        if (_fd < 0) {
            throw ClosedChannelException();
        }
        rc : long = __k_net_unix_dgram_connect(_fd, &remote[0], timeoutNanos);
        SocketChannel::checkResult(rc, 0L);
    }

    connect(remote: const NetworkAddress&) : void {
        connect(remote, IO_NO_TIMEOUT);
    }

    connect(remote: const Path&) : void {
        connect(remote, IO_NO_TIMEOUT);
    }

    connect(remote: const char[]) : void {
        connect(remote, IO_NO_TIMEOUT);
    }

    receive(dst: ByteBuffer+, timeoutNanos: long) : int {
        if (_fd < 0) {
            throw ClosedChannelException();
        }
        len : unsigned int = dst->remaining();
        if (len == 0u) {
            return 0;
        }
        rc : long = __k_net_recv(_fd, dst->dataAt(dst->position()), (int) len, timeoutNanos);
        SocketChannel::checkResult(rc, 0L);
        n : int = (int) rc;
        dst->position(dst->position() + (unsigned int) n);
        return n;
    }

    receive(dst: ByteBuffer+) : int {
        return receive(dst, IO_NO_TIMEOUT);
    }

    send(src: ByteBuffer+, timeoutNanos: long) : int {
        if (_fd < 0) {
            throw ClosedChannelException();
        }
        len : unsigned int = src->remaining();
        if (len == 0u) {
            return 0;
        }
        rc : long = __k_net_send(_fd, src->dataAt(src->position()), (int) len, timeoutNanos);
        SocketChannel::checkResult(rc, 0L);
        n : int = (int) rc;
        src->position(src->position() + (unsigned int) n);
        return n;
    }

    send(src: ByteBuffer+) : int {
        return send(src, IO_NO_TIMEOUT);
    }

    sendTo(src: ByteBuffer+, remote: const NetworkAddress&, timeoutNanos: long) : int {
        if (_fd < 0) {
            throw ClosedChannelException();
        }
        len : unsigned int = src->remaining();
        if (len == 0u) {
            return 0;
        }
        rc : long = __k_net_dgram_sendto(_fd, remote.nativeHost(), remote.port(),
                                         src->dataAt(src->position()), (int) len,
                                         timeoutNanos);
        SocketChannel::checkResult(rc, 0L);
        n : int = (int) rc;
        src->position(src->position() + (unsigned int) n);
        return n;
    }

    sendTo(src: ByteBuffer+, remote: const Path&, timeoutNanos: long) : int {
        if (_fd < 0) {
            throw ClosedChannelException();
        }
        len : unsigned int = src->remaining();
        if (len == 0u) {
            return 0;
        }
        rc : long = __k_net_unix_dgram_sendto(_fd, remote.nativePath(),
                                              src->dataAt(src->position()), (int) len,
                                              timeoutNanos);
        SocketChannel::checkResult(rc, 0L);
        n : int = (int) rc;
        src->position(src->position() + (unsigned int) n);
        return n;
    }

    sendTo(src: ByteBuffer+, remote: const char[], timeoutNanos: long) : int {
        if (_fd < 0) {
            throw ClosedChannelException();
        }
        len : unsigned int = src->remaining();
        if (len == 0u) {
            return 0;
        }
        rc : long = __k_net_unix_dgram_sendto(_fd, &remote[0],
                                              src->dataAt(src->position()), (int) len,
                                              timeoutNanos);
        SocketChannel::checkResult(rc, 0L);
        n : int = (int) rc;
        src->position(src->position() + (unsigned int) n);
        return n;
    }

    sendTo(src: ByteBuffer+, remote: const NetworkAddress&) : int {
        return sendTo(src, remote, IO_NO_TIMEOUT);
    }

    sendTo(src: ByteBuffer+, remote: const Path&) : int {
        return sendTo(src, remote, IO_NO_TIMEOUT);
    }

    sendTo(src: ByteBuffer+, remote: const char[]) : int {
        return sendTo(src, remote, IO_NO_TIMEOUT);
    }

    const localPort() : int {
        if (_fd < 0) {
            throw ClosedChannelException();
        }
        p : long = __k_net_local_port(_fd);
        if (p < 0L) {
            throw IOException(0);
        }
        return (int) p;
    }

    override const isOpen() : bool {
        if (_fd < 0) {
            return false;
        }
        return __k_net_is_open(_fd) != 0;
    }

    override close() : void {
        if (_fd >= 0) {
            __k_net_close(_fd);
            _fd = -1;
        }
    }
}

} // namespace io
