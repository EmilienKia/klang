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
 * Fast dynamic circular byte buffer for codec streaming.
 */
public class CodecBuffer {
private:
    _data     : byte[]!;
    _head     : unsigned int = 0u;
    _tail     : unsigned int = 0u;
    _count    : unsigned int = 0u;
    _capacity : unsigned int = 0u;

    grow(minCapacity: unsigned int) {
        newCap : unsigned int = _capacity * 2u;
        if (newCap < minCapacity) {
            newCap = minCapacity;
        }
        if (newCap < 64u) {
            newCap = 64u;
        }

        newData : byte[]! = new byte[newCap];
        if (_count > 0u && _data != null) {
            i : unsigned int = 0u;
            while (i < _count) {
                newData[i] = _data[(_head + i) % _capacity];
                ++i;
            }
        }
        if (_data != null) {
            delete _data;
        }
        _data = newData;
        _head = 0u;
        _tail = _count;
        _capacity = newCap;
    }

public:
    CodecBuffer() : _data(null), _head(0u), _tail(0u), _count(0u), _capacity(0u) {
        grow(64u);
    }

    CodecBuffer(initialCapacity: unsigned int) : _data(null), _head(0u), _tail(0u), _count(0u), _capacity(0u) {
        grow(initialCapacity);
    }

    ~CodecBuffer() {
        if (_data != null) {
            delete _data;
            _data = null;
        }
    }

    const size() : unsigned int {
        return _count;
    }

    const isEmpty() : bool {
        return _count == 0u;
    }

    clear() {
        _head = 0u;
        _tail = 0u;
        _count = 0u;
    }

    push(b: byte) {
        if (_count == _capacity) {
            grow(_capacity + 1u);
        }
        _data[_tail] = b;
        _tail = (_tail + 1u) % _capacity;
        ++_count;
    }

    pop() : ::k::Optional<byte> {
        if (_count == 0u) {
            return ::k::Optional<byte>();
        }
        val : byte = _data[_head];
        _head = (_head + 1u) % _capacity;
        --_count;
        return ::k::Optional<byte>(val);
    }

    read(buf: byte[], off: unsigned int, len: unsigned int) : unsigned int {
        if (_count == 0u || len == 0u) {
            return 0u;
        }
        toRead : unsigned int = len < _count ? len : _count;
        i : unsigned int = 0u;
        while (i < toRead) {
            buf[off + i] = _data[_head];
            _head = (_head + 1u) % _capacity;
            ++i;
        }
        _count -= toRead;
        return toRead;
    }
}


/**
 * Base abstract class for stateful stream codecs (encoders/decoders).
 */
public abstract class Codec {
protected:
    _eof   : bool = false;
    _state : unsigned int = 0u;
    _work  : unsigned long = 0uL;
    _buf   : CodecBuffer;

public:
    Codec() {}

    const isEof() : bool {
        return _eof;
    }

    const available() : unsigned int {
        return _buf.size();
    }

    abstract process(buff: const byte[], off: unsigned int, len: unsigned int);

    finalize() {
        _eof = true;
    }

    read(buff: byte[], off: unsigned int, len: unsigned int) : int {
        if (_eof && _buf.isEmpty()) {
            return -1;
        }
        if (len == 0u) {
            return 0;
        }
        sz : unsigned int = _buf.read(buff, off, len);
        return (int) sz;
    }
}


/**
 * Base codec input stream decorator.
 *
 * Reads raw or encoded bytes from an underlying InputStream and passes them
 * through a Codec to produce transformed output.
 */
public class CodecInputStream : public FilterInputStream<byte> {
protected:
    _codec   : Codec*;
    _bufSize : unsigned int = 4096u;
    _tmpBuf  : byte[]!;

public:
    CodecInputStream(input: InputStream<byte>*, codec: Codec*, bufSize: unsigned int)
        : FilterInputStream(input), _codec(codec), _bufSize(bufSize), _tmpBuf(null) {
        if (_bufSize < 64u) _bufSize = 64u;
        _tmpBuf = new byte[_bufSize];
    }

    CodecInputStream(input: InputStream<byte>*, codec: Codec*)
        : CodecInputStream(input, codec, 4096u) {}

    CodecInputStream()
        : FilterInputStream(null), _codec(null), _bufSize(4096u), _tmpBuf(null) {
        _tmpBuf = new byte[4096];
    }

    ~CodecInputStream() {
        close();
        if (_tmpBuf != null) {
            delete _tmpBuf;
            _tmpBuf = null;
        }
    }

    override read() : ::k::Optional<byte> {
        b : byte[1];
        res : Expected<unsigned int, ::k::io::StreamOutOfData> = read(b, 0u, 1u);
        if (res.hasError() || res.getResult() == 0u) {
            return ::k::Optional<byte>();
        }
        return ::k::Optional<byte>(b[0]);
    }

    override read(buf: byte[], off: unsigned int, len: unsigned int) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        if (_codec == null || _in == null) {
            err : ::k::io::StreamOutOfData = ::k::io::StreamOutOfData::Closed;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::error(err);
        }
        if (len == 0u) {
            zero : unsigned int = 0u;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(zero);
        }

        totalRead : unsigned int = 0u;
        while (totalRead < len) {
            if (_codec->available() == 0u) {
                if (_codec->isEof()) {
                    break;
                }
                cRes : Expected<unsigned int, ::k::io::StreamOutOfData> = _in->read(_tmpBuf, 0u, _bufSize);
                if (cRes.hasError()) {
                    _codec->finalize();
                } else {
                    c : unsigned int = cRes.getResult();
                    if (c == 0u) {
                        _codec->finalize();
                    } else {
                        _codec->process(_tmpBuf, 0u, c);
                    }
                }
                if (_codec->available() == 0u) {
                    if (_codec->isEof()) {
                        break;
                    }
                    continue;
                }
            }
            toRead : unsigned int = len - totalRead;
            r : int = _codec->read(buf, off + totalRead, toRead);
            if (r <= 0) {
                break;
            }
            totalRead += (unsigned int) r;
        }
        return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(totalRead);
    }

    override read(buf: byte[]) : Expected<unsigned int, ::k::io::StreamOutOfData> {
        return read(buf, 0u, (unsigned int) buf.size);
    }

    override skip(n: unsigned long) : unsigned long {
        skipped : unsigned long = 0uL;
        skipBuf : byte[256];
        while (skipped < n) {
            toRead : unsigned int = (unsigned int) ((n - skipped) < 256uL ? (n - skipped) : 256uL);
            r : Expected<unsigned int, ::k::io::StreamOutOfData> = read(skipBuf, 0u, toRead);
            if (r.hasError() || r.getResult() == 0u) {
                break;
            }
            skipped += (unsigned long) r.getResult();
        }
        return skipped;
    }

    override available() : Expected<unsigned int, ::k::io::StreamOutOfData> {
        if (_codec == null || _in == null) {
            err : ::k::io::StreamOutOfData = ::k::io::StreamOutOfData::Closed;
            return Expected<unsigned int, ::k::io::StreamOutOfData>::error(err);
        }
        if (_codec->available() > 0u) {
            avail : unsigned int = _codec->available();
            return Expected<unsigned int, ::k::io::StreamOutOfData>::expected(avail);
        }
        return _in->available();
    }

    override close() {
        if (_in != null) {
            _in->close();
        }
    }
}


/**
 * Base codec output stream decorator.
 *
 * Encodes or decodes data written through it via a Codec before forwarding
 * to the underlying OutputStream.
 */
public class CodecOutputStream : public FilterOutputStream<byte> {
protected:
    _codec     : Codec*;
    _drainBuf  : byte[]!;

    _drain(propagate: bool) {
        if (_codec == null || _out == null) return;
        avail : unsigned int = _codec->available();
        while (avail > 0u) {
            toRead : unsigned int = avail < 4096u ? avail : 4096u;
            r : int = _codec->read(_drainBuf, 0u, toRead);
            if (r <= 0) break;
            _out->write(_drainBuf, 0u, (unsigned int) r);
            avail = _codec->available();
        }
        if (propagate) {
            _out->flush();
        }
    }

public:
    CodecOutputStream(output: OutputStream<byte>*, codec: Codec*)
        : FilterOutputStream(output), _codec(codec), _drainBuf(null) {
        _drainBuf = new byte[4096];
    }

    CodecOutputStream()
        : FilterOutputStream(null), _codec(null), _drainBuf(null) {
        _drainBuf = new byte[4096];
    }

    ~CodecOutputStream() {
        close();
        if (_drainBuf != null) {
            delete _drainBuf;
            _drainBuf = null;
        }
    }

    override write(b: byte) : OutputStream<byte>& {
        buf : byte[1];
        buf[0] = b;
        return write(buf, 0u, 1u);
    }

    override write(buf: const byte[], off: unsigned int, len: unsigned int) : OutputStream<byte>& {
        if (_out == null || _codec == null || len == 0u) {
            return this;
        }
        _codec->process(buf, off, len);
        _drain(false);
        return this;
    }

    override write(buf: const byte[]) : OutputStream<byte>& {
        return write(buf, 0u, (unsigned int) buf.size);
    }

    override flush() : OutputStream<byte>& {
        _drain(true);
        return this;
    }

    override close() : OutputStream<byte>& {
        if (_codec != null) {
            _codec->finalize();
        }
        _drain(true);
        if (_out != null) {
            _out->close();
        }
        return this;
    }
}

} // namespace io
