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
// FilterInputStream — A wrapper that delegates all reads to an underlying
// InputStream. Subclasses override specific methods to add behaviour
// (buffering, data decoding, etc.).
//
// The wrapped stream is held by link (non-owning, non-null, rebindable).
// The caller is responsible for the lifetime of the underlying stream.
//
// Inspired by java.io.FilterInputStream.
// ═════════════════════════════════════════════════════════════════════════════
public class FilterInputStream : public InputStream {
protected:
    _in : InputStream*;

public:
    /**
     * Construct a FilterInputStream wrapping the given stream.
     * The stream is held by pointer — the caller retains ownership.
     */
    FilterInputStream(input: InputStream*) : _in(input) {}

    /** Default constructor (null inner stream — for compiler-generated subclass constructors). */
    FilterInputStream() : _in(null) {}

    // ── InputStream implementation (all delegated) ───────────────────────

    read() : int {
        return _in->read();
    }

    read(buf: byte[], off: unsigned int, len: unsigned int) : int {
        return _in->read(buf, off, len);
    }

    read(buf: byte[]) : int {
        return _in->read(buf);
    }

    skip(n: unsigned long) : unsigned long {
        return _in->skip(n);
    }

    available() : int {
        return _in->available();
    }

    close() {
        _in->close();
    }
}


// ═════════════════════════════════════════════════════════════════════════════
// FilterOutputStream — A wrapper that delegates all writes to an underlying
// OutputStream. Subclasses override specific methods to add behaviour
// (buffering, data encoding, etc.).
//
// The wrapped stream is held by link (non-owning, non-null, rebindable).
// The caller is responsible for the lifetime of the underlying stream.
//
// Inspired by java.io.FilterOutputStream.
// ═════════════════════════════════════════════════════════════════════════════
public class FilterOutputStream : public OutputStream {
protected:
    _out : OutputStream*;

public:
    /**
     * Construct a FilterOutputStream wrapping the given stream.
     * The stream is held by pointer — the caller retains ownership.
     */
    FilterOutputStream(output: OutputStream*) : _out(output) {}

    /** Default constructor (null inner stream — for compiler-generated subclass constructors). */
    FilterOutputStream() : _out(null) {}

    // ── OutputStream implementation (all delegated) ──────────────────────

    write(b: int) : OutputStream& {
        _out->write(b);
        return this;
    }

    write(buf: const byte[], off: unsigned int, len: unsigned int) : OutputStream& {
        _out->write(buf, off, len);
        return this;
    }

    write(buf: const byte[]) : OutputStream& {
        _out->write(buf);
        return this;
    }

    flush() : OutputStream& {
        _out->flush();
        return this;
    }

    close() : OutputStream& {
        flush();
        _out->close();
        return this;
    }
}

} // namespace io









