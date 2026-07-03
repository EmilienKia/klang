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
 * Delegate input operations to an underlying stream.
 *
 * `FilterInputStream` is intended as a base class for input decorators such as
 * buffered or typed readers.
 */
template<typename T>
public class FilterInputStream : public InputStream<T> {
protected:
    _in : InputStream<T>*;

public:
    /**
     * Construct a FilterInputStream wrapping the given stream.
     * The stream is held by pointer — the caller retains ownership.
     *
     * @param input Wrapped input stream.
     */
    FilterInputStream(input: InputStream<T>*) : _in(input) {}

    /** Default constructor (null inner stream — for compiler-generated subclass constructors). */
    FilterInputStream() : _in(null) {}

    // ── InputStream implementation (all delegated) ───────────────────────

    read() : ::k::Optional<T> {
        return _in->read();
    }

    read(buf: T[], off: unsigned int, len: unsigned int) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return _in->read(buf, off, len);
    }

    read(buf: T[]) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return _in->read(buf);
    }

    skip(n: unsigned long) : unsigned long {
        return _in->skip(n);
    }

    available() : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return _in->available();
    }

    close() {
        _in->close();
    }
}


/**
 * Delegate output operations to an underlying stream.
 *
 * `FilterOutputStream` is intended as a base class for output decorators such
 * as buffered, data, or printing streams.
 */
template<typename T>
public class FilterOutputStream : public OutputStream<T> {
protected:
    _out : OutputStream<T>*;

public:
    /**
     * Construct a FilterOutputStream wrapping the given stream.
     * The stream is held by pointer — the caller retains ownership.
     *
     * @param output Wrapped output stream.
     */
    FilterOutputStream(output: OutputStream<T>*) : _out(output) {}

    /** Default constructor (null inner stream — for compiler-generated subclass constructors). */
    FilterOutputStream() : _out(null) {}

    // ── OutputStream implementation (all delegated) ──────────────────────

    write(b: T) : OutputStream<T>& {
        _out->write(b);
        return this;
    }

    write(buf: const T[], off: unsigned int, len: unsigned int) : OutputStream<T>& {
        _out->write(buf, off, len);
        return this;
    }

    write(buf: const T[]) : OutputStream<T>& {
        _out->write(buf);
        return this;
    }

    flush() : OutputStream<T>& {
        _out->flush();
        return this;
    }

    close() : OutputStream<T>& {
        flush();
        _out->close();
        return this;
    }
}

} // namespace io






