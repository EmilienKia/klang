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

// ═════════════════════════════════════════════════════════════════════════════
// IOException
// ═════════════════════════════════════════════════════════════════════════════

/**
 * IOException — base class for every recoverable input/output failure.
 *
 * Carries the platform `errno` value that caused the failure when one is
 * available, so that callers can discriminate between conditions such as
 * `ENOENT` and `EACCES` without parsing a message.
 */
public class IOException : public Exception {
    _errno : int;

public:
    /** Construct with default error code 200 and no platform error. */
    IOException() : Exception(200), _errno(0) {}

    /**
     * Construct with a platform error number.
     * @param err Platform `errno` value, or 0 when not applicable.
     */
    IOException(err: int) : Exception(200), _errno(err) {}

    /**
     * Construct with an explicit exception code and a platform error number.
     * @param code Exception subcode.
     * @param err Platform `errno` value, or 0 when not applicable.
     */
    IOException(code: int, err: int) : Exception(code), _errno(err) {}

    /**
     * Return the platform error number that caused this failure.
     * @return The `errno` value, or 0 when none was reported.
     */
    const getErrno() : int {
        return _errno;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// FileNotFoundException
// ═════════════════════════════════════════════════════════════════════════════

/**
 * FileNotFoundException — thrown when a file cannot be opened because it does
 * not exist, or because a component of its path is not a directory.
 */
public class FileNotFoundException : public IOException {
public:
    /** Construct with default error code 201. */
    FileNotFoundException() : IOException(201, 0) {}

    /**
     * Construct with a platform error number.
     * @param err Platform `errno` value.
     */
    FileNotFoundException(err: int) : IOException(201, err) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// ClosedChannelException
// ═════════════════════════════════════════════════════════════════════════════

/**
 * ClosedChannelException — thrown when an operation is attempted on a channel
 * that is closed, or when a channel is closed by another thread while an
 * operation is in flight.
 *
 * The pending operation is cancelled at the kernel level before this exception
 * is raised, so no buffer handed to the runtime is written after the throw.
 */
public class ClosedChannelException : public IOException {
public:
    /** Construct with default error code 202. */
    ClosedChannelException() : IOException(202, 0) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// EndOfStreamException
// ═════════════════════════════════════════════════════════════════════════════

/**
 * EndOfStreamException — thrown when end of stream is reached before a
 * "fully" operation could transfer the requested number of bytes.
 *
 * `getBytesTransferred()` reports how many bytes were transferred before the
 * end of stream was hit, so the caller can resume or report partial progress.
 */
public class EndOfStreamException : public IOException {
    _transferred : long;

public:
    /** Construct with default error code 203 and no transferred bytes. */
    EndOfStreamException() : IOException(203, 0), _transferred(0) {}

    /**
     * Construct reporting a partial transfer.
     * @param transferred Number of bytes transferred before end of stream.
     */
    EndOfStreamException(transferred: long) : IOException(203, 0),
                                              _transferred(transferred) {}

    /**
     * Return the number of bytes transferred before end of stream.
     * @return Byte count, possibly 0.
     */
    const getBytesTransferred() : long {
        return _transferred;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// InterruptedIOException
// ═════════════════════════════════════════════════════════════════════════════

/**
 * InterruptedIOException — thrown when a multi-step I/O operation is
 * interrupted after having already transferred some bytes.
 *
 * A "fully" operation that is interrupted before transferring anything raises
 * ::k::ThreadInterruptionException instead: there is no partial state to
 * report.  Once bytes have moved, the caller must be told how many, hence this
 * distinct exception.
 *
 * Catching this exception does not clear the calling thread's interrupted
 * flag; call ::k::Thread::interrupted() when re-entering a blocking API must
 * not throw again.
 */
public class InterruptedIOException : public IOException {
    _transferred : long;

public:
    /** Construct with default error code 204 and no transferred bytes. */
    InterruptedIOException() : IOException(204, 0), _transferred(0) {}

    /**
     * Construct reporting a partial transfer.
     * @param transferred Number of bytes transferred before the interruption.
     */
    InterruptedIOException(transferred: long) : IOException(204, 0),
                                                _transferred(transferred) {}

    /**
     * Return the number of bytes transferred before the interruption.
     * @return Byte count, possibly 0.
     */
    const getBytesTransferred() : long {
        return _transferred;
    }
}

} // namespace io
