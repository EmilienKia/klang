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
 * Base class for streams that transform values from an input stream of type `I`
 * into an output stream of type `O`.
 *
 * The wrapped input stream is owned by the caller.
 */
template<typename I, typename O>
public abstract class TransformInputStream : public InputStream<O> {
protected:
    _in : InputStream<I>*;

public:
    /** Construct a transform stream wrapping the given input stream. */
    TransformInputStream(input: InputStream<I>*) : _in(input) {}

    /** Default constructor used by compiler-generated subclass constructors. */
    TransformInputStream() : _in(null) {}

    close() {
        _in->close();
    }
}


/**
 * Transform stream that maps each input element to zero or one output element.
 */
template<typename I, typename O>
public abstract class OneToOneTransformInputStream : public TransformInputStream<I, O> {
public:
    OneToOneTransformInputStream(input: InputStream<I>*) : TransformInputStream(input) {}
    OneToOneTransformInputStream() : TransformInputStream() {}

    read() : ::k::Optional<O> {
        current : ::k::Optional<I> = _in->read();
        while (current.hasValue()) {
            result : ::k::Optional<O> = transform(current.get());
            if (result.hasValue()) return result;
            current = _in->read();
        }
        return ::k::Optional<O>();
    }

    read(buf: O[], off: unsigned int, len: unsigned int) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        actual : unsigned int = len;
        if (off + actual > buf.size) {
            actual = buf.size - off;
        }
        if (actual == 0) {
            zero : unsigned int = 0;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(zero);
        }

        written : unsigned int = 0;
        while (written < actual) {
            next : ::k::Optional<O> = read();
            if (!next.hasValue()) {
                break;
            }
            buf[off + written] = next.get();
            written = written + 1;
        }
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(written);
    }

    read(buf: O[]) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return read(buf, 0, (unsigned int) buf.size);
    }

    skip(n: unsigned long) : unsigned long {
        skipped : unsigned long = 0;
        while (skipped < n) {
            next : ::k::Optional<O> = read();
            if (!next.hasValue()) {
                break;
            }
            skipped = skipped + 1;
        }
        return skipped;
    }

    available() : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return _in->available();
    }

protected:
    abstract transform(in : const I&) : ::k::Optional<O>;
}


/**
 * Transform stream that maps each input element to zero, one or many output elements.
 */
template<typename I, typename O>
public abstract class OneToManyTransformInputStream : public TransformInputStream<I, O> {
protected:
    buffer : ::k::Vector<O>;

public:
    OneToManyTransformInputStream(input: InputStream<I>*) : TransformInputStream(input) {}
    OneToManyTransformInputStream() : TransformInputStream() {}

    read() : ::k::Optional<O> {
        if (!buffer.isEmpty()) {
            result : O = buffer[0];
            buffer.removeFront();
            return ::k::Optional<O>(result);
        }

        current : ::k::Optional<I> = _in->read();
        while (current.hasValue()) {
            doTransform(current.get());
            if (!buffer.isEmpty()) {
                result : O = buffer[0];
                buffer.removeFront();
                return ::k::Optional<O>(result);
            }
            current = _in->read();
        }
        return ::k::Optional<O>();
    }

    read(buf: O[], off: unsigned int, len: unsigned int) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        actual : unsigned int = len;
        if (off + actual > buf.size) {
            actual = buf.size - off;
        }
        if (actual == 0) {
            zero : unsigned int = 0;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(zero);
        }

        written : unsigned int = 0;
        while (written < actual) {
            if (!buffer.isEmpty()) {
                buf[off + written] = buffer[0];
                buffer.removeFront();
                written = written + 1;
                continue;
            }

            current : ::k::Optional<I> = _in->read();
            if (!current.hasValue()) {
                break;
            }
            doTransform(current.get());
            if (!buffer.isEmpty()) {
                buf[off + written] = buffer[0];
                buffer.removeFront();
                written = written + 1;
            }
        }
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(written);
    }

    read(buf: O[]) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return read(buf, 0, (unsigned int) buf.size);
    }

    skip(n: unsigned long) : unsigned long {
        skipped : unsigned long = 0;
        while (skipped < n) {
            if (!buffer.isEmpty()) {
                buffer.removeFront();
                skipped = skipped + 1;
                continue;
            }

            current : ::k::Optional<I> = _in->read();
            if (!current.hasValue()) {
                break;
            }
            doTransform(current.get());
            if (!buffer.isEmpty()) {
                buffer.removeFront();
                skipped = skipped + 1;
            }
        }
        return skipped;
    }

    available() : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return _in->available();
    }

protected:
    abstract transform(in : const I&) : ::k::Vector<O>;

    doTransform(in : const I&) {
        results : ::k::Vector<O> = transform(in);
        n : int = results.size();
        for (j : int = 0; j < n; j = j + 1) {
            buffer.append(results[j]);
        }
    }
}


/**
 * Transform stream that groups many input elements into a single output element.
 */
template<typename I, typename O>
public abstract class ManyToOneTransformInputStream : public TransformInputStream<I, O> {
protected:
    buffer : ::k::Vector<I>;

public:
    ManyToOneTransformInputStream(input: InputStream<I>*) : TransformInputStream(input) {}
    ManyToOneTransformInputStream() : TransformInputStream() {}

    read() : ::k::Optional<O> {
        while (true) {
            current : ::k::Optional<I> = _in->read();
            if (!current.hasValue()) {
                break;
            }

            buffer.append(current.get());
            result : ::k::Optional<O> = transform(buffer);
            if (result.hasValue()) {
                buffer.clear();
                return result;
            }
        }
        return ::k::Optional<O>();
    }

    read(buf: O[], off: unsigned int, len: unsigned int) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        actual : unsigned int = len;
        if (off + actual > buf.size) {
            actual = buf.size - off;
        }
        if (actual == 0) {
            zero : unsigned int = 0;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(zero);
        }

        written : unsigned int = 0;
        while (written < actual) {
            next : ::k::Optional<O> = read();
            if (!next.hasValue()) {
                break;
            }
            buf[off + written] = next.get();
            written = written + 1;
        }
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(written);
    }

    read(buf: O[]) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return read(buf, 0, (unsigned int) buf.size);
    }

    skip(n: unsigned long) : unsigned long {
        skipped : unsigned long = 0;
        while (skipped < n) {
            next : ::k::Optional<O> = read();
            if (!next.hasValue()) {
                break;
            }
            skipped = skipped + 1;
        }
        return skipped;
    }

    available() : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return _in->available();
    }

protected:
    abstract transform(in : const ::k::Vector<I>&) : ::k::Optional<O>;
}


/**
 * Transform stream that groups many input elements into many output elements.
 */
template<typename I, typename O>
public abstract class ManyToManyTransformInputStream : public TransformInputStream<I, O> {
protected:
    inputBuffer : ::k::Vector<I>;
    outputBuffer : ::k::Vector<O>;

public:
    ManyToManyTransformInputStream(input: InputStream<I>*) : TransformInputStream(input) {}
    ManyToManyTransformInputStream() : TransformInputStream() {}

    read() : ::k::Optional<O> {
        if (!outputBuffer.isEmpty()) {
            result : O = outputBuffer[0];
            outputBuffer.removeFront();
            return ::k::Optional<O>(result);
        }

        current : ::k::Optional<I> = _in->read();
        while (current.hasValue()) {
            inputBuffer.append(current.get());
            doTransform(inputBuffer);
            if (!outputBuffer.isEmpty()) {
                result : O = outputBuffer[0];
                outputBuffer.removeFront();
                return ::k::Optional<O>(result);
            }
            current = _in->read();
        }
        return ::k::Optional<O>();
    }

    read(buf: O[], off: unsigned int, len: unsigned int) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        actual : unsigned int = len;
        if (off + actual > buf.size) {
            actual = buf.size - off;
        }
        if (actual == 0) {
            zero : unsigned int = 0;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(zero);
        }

        written : unsigned int = 0;
        while (written < actual) {
            if (!outputBuffer.isEmpty()) {
                buf[off + written] = outputBuffer[0];
                outputBuffer.removeFront();
                written = written + 1;
                continue;
            }

            current : ::k::Optional<I> = _in->read();
            if (!current.hasValue()) {
                break;
            }
            inputBuffer.append(current.get());
            doTransform(inputBuffer);
            if (!outputBuffer.isEmpty()) {
                buf[off + written] = outputBuffer[0];
                outputBuffer.removeFront();
                written = written + 1;
            }
        }
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(written);
    }

    read(buf: O[]) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return read(buf, 0, (unsigned int) buf.size);
    }

    skip(n: unsigned long) : unsigned long {
        skipped : unsigned long = 0;
        while (skipped < n) {
            if (!outputBuffer.isEmpty()) {
                outputBuffer.removeFront();
                skipped = skipped + 1;
                continue;
            }

            current : ::k::Optional<I> = _in->read();
            if (!current.hasValue()) {
                break;
            }
            inputBuffer.append(current.get());
            doTransform(inputBuffer);
            if (!outputBuffer.isEmpty()) {
                outputBuffer.removeFront();
                skipped = skipped + 1;
            }
        }
        return skipped;
    }

    available() : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return _in->available();
    }

protected:
    abstract transform(in : const ::k::Vector<I>&) : ::k::Vector<O>;

    doTransform(in : const ::k::Vector<I>&) {
        result : ::k::Vector<O> = transform(in);
        if (!result.isEmpty()) {
            n : int = result.size();
            for (j : int = 0; j < n; j = j + 1) {
                outputBuffer.append(result[j]);
            }
            inputBuffer.clear();
        }
    }
}


/**
 * Base class for streams that transform values written to an output stream of
 * type `O` from values of type `I`.
 */
template<typename I, typename O>
public abstract class TransformOutputStream : public OutputStream<I> {
protected:
    _out : OutputStream<O>*;

public:
    TransformOutputStream(output: OutputStream<O>*) : _out(output) {}
    TransformOutputStream() : _out(null) {}

    flush() : OutputStream<I>& {
        _out->flush();
        return this;
    }

    close() : OutputStream<I>& {
        flush();
        _out->close();
        return this;
    }
}


/**
 * Transform stream that maps each written element to zero or one output element.
 */
template<typename I, typename O>
public abstract class OneToOneTransformOutputStream : public TransformOutputStream<I, O> {
public:
    OneToOneTransformOutputStream(output: OutputStream<O>*) : TransformOutputStream(output) {}
    OneToOneTransformOutputStream() : TransformOutputStream() {}

    write(value: I) : OutputStream<I>& {
        result : ::k::Optional<O> = transform(value);
        if (result.hasValue()) {
            _out->write(result.get());
        }
        return this;
    }

    write(buf: const I[], off: unsigned int, len: unsigned int) : OutputStream<I>& {
        actual : unsigned int = len;
        if (off + actual > buf.size) {
            actual = buf.size - off;
        }
        i : unsigned int = 0;
        while (i < actual) {
            write(buf[off + i]);
            i = i + 1;
        }
        return this;
    }

    write(buf: const I[]) : OutputStream<I>& {
        return write(buf, 0, (unsigned int) buf.size);
    }

protected:
    abstract transform(in : const I&) : ::k::Optional<O>;
}


/**
 * Transform stream that maps each written element to zero, one or many output elements.
 */
template<typename I, typename O>
public abstract class OneToManyTransformOutputStream : public TransformOutputStream<I, O> {
public:
    OneToManyTransformOutputStream(output: OutputStream<O>*) : TransformOutputStream(output) {}
    OneToManyTransformOutputStream() : TransformOutputStream() {}

    write(value: I) : OutputStream<I>& {
        emit(transform(value));
        return this;
    }

    write(buf: const I[], off: unsigned int, len: unsigned int) : OutputStream<I>& {
        actual : unsigned int = len;
        if (off + actual > buf.size) {
            actual = buf.size - off;
        }
        i : unsigned int = 0;
        while (i < actual) {
            write(buf[off + i]);
            i = i + 1;
        }
        return this;
    }

    write(buf: const I[]) : OutputStream<I>& {
        return write(buf, 0, (unsigned int) buf.size);
    }

protected:
    abstract transform(in : const I&) : ::k::Vector<O>;

    emit(values : const ::k::Vector<O>&) {
        i : int = 0;
        while (i < values.size()) {
            _out->write(values[i]);
            i = i + 1;
        }
    }
}


/**
 * Transform stream that groups many written values into a single output value.
 */
template<typename I, typename O>
public abstract class ManyToOneTransformOutputStream : public TransformOutputStream<I, O> {
protected:
    buffer : ::k::Vector<I>;

public:
    ManyToOneTransformOutputStream(output: OutputStream<O>*) : TransformOutputStream(output) {}
    ManyToOneTransformOutputStream() : TransformOutputStream() {}

    write(value: I) : OutputStream<I>& {
        buffer.append(value);
        result : ::k::Optional<O> = transform(buffer);
        if (result.hasValue()) {
            buffer.clear();
            _out->write(result.get());
        }
        return this;
    }

    write(buf: const I[], off: unsigned int, len: unsigned int) : OutputStream<I>& {
        actual : unsigned int = len;
        if (off + actual > buf.size) {
            actual = buf.size - off;
        }
        i : unsigned int = 0;
        while (i < actual) {
            write(buf[off + i]);
            i = i + 1;
        }
        return this;
    }

    write(buf: const I[]) : OutputStream<I>& {
        return write(buf, 0, (unsigned int) buf.size);
    }

protected:
    abstract transform(in : const ::k::Vector<I>&) : ::k::Optional<O>;
}


/**
 * Transform stream that groups many written values into many output values.
 */
template<typename I, typename O>
public abstract class ManyToManyTransformOutputStream : public TransformOutputStream<I, O> {
protected:
    inputBuffer : ::k::Vector<I>;
    outputBuffer : ::k::Vector<O>;

public:
    ManyToManyTransformOutputStream(output: OutputStream<O>*) : TransformOutputStream(output) {}
    ManyToManyTransformOutputStream() : TransformOutputStream() {}

    write(value: I) : OutputStream<I>& {
        inputBuffer.append(value);
        transformAndEmit();
        return this;
    }

    write(buf: const I[], off: unsigned int, len: unsigned int) : OutputStream<I>& {
        actual : unsigned int = len;
        if (off + actual > buf.size) {
            actual = buf.size - off;
        }
        i : unsigned int = 0;
        while (i < actual) {
            write(buf[off + i]);
            i = i + 1;
        }
        return this;
    }

    write(buf: const I[]) : OutputStream<I>& {
        return write(buf, 0, (unsigned int) buf.size);
    }

    flush() : OutputStream<I>& {
        flushBufferedOutputs();
        _out->flush();
        return this;
    }

protected:
    abstract transform(in : const ::k::Vector<I>&) : ::k::Vector<O>;

    transformAndEmit() {
        result : ::k::Vector<O> = transform(inputBuffer);
        if (!result.isEmpty()) {
            n : int = result.size();
            for (j : int = 0; j < n; j = j + 1) {
                outputBuffer.append(result[j]);
            }
            inputBuffer.clear();
            flushBufferedOutputs();
        }
    }

    flushBufferedOutputs() {
        while (!outputBuffer.isEmpty()) {
            _out->write(outputBuffer[0]);
            outputBuffer.removeFront();
        }
    }
}

} // namespace io






