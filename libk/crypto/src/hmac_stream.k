/*
 * K Language standard library -- crypto I/O module
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

module k::io::crypto;
import k::crypto;

private hmacVerifyTruncated(tag: const unsigned byte[], expected: const unsigned byte[],
                            tagSize: unsigned int) : bool throws(k::crypto::CryptoException) {
    if (tagSize == 0u || tagSize > tag.size) throw k::crypto::CryptoInvalidInputException();
    prefix : unsigned byte[]! = new unsigned byte[tagSize];
    i : unsigned int = 0u;
    while (i < tagSize) {
        prefix[i] = tag[i];
        ++i;
    }
    return k::crypto::Hmac::constantTimeEquals(prefix, expected);
}

/**
 * MACs bytes forwarded to a borrowed output. The HMAC is consumed.
 * A tag is published only after the output flushes and closes successfully.
 */
public class HmacOutputStream : public k::io::FilterOutputStream<byte> {
private:
    _hmac : k::crypto::Hmac!;
    _tag : unsigned byte[]!;
    _closed : bool;
    _failed : bool;
    _complete : bool;

    _ensureWritable() throws(k::crypto::CryptoException) {
        if (_closed || _failed) throw k::crypto::CryptoStateException();
    }

public:
    HmacOutputStream(output: k::io::OutputStream<byte>&, hmac: k::crypto::Hmac!)
        : FilterOutputStream(&output), _hmac(hmac), _tag(null),
          _closed(false), _failed(false), _complete(false)
            throws(k::crypto::CryptoException) {
        if (_hmac == null) throw k::crypto::CryptoInvalidInputException();
    }

    /** A borrowed tag view, or null until successful completion. */
    public const getTag() : const unsigned byte[]? {
        if (!_complete || _failed) return null;
        return _tag;
    }

    public const isComplete() : bool { return _complete && !_failed; }

    /** Compare a complete tag; a shorter expected tag is never accepted. */
    public const verify(expected: const unsigned byte[]) : bool throws(k::crypto::CryptoException) {
        if (!_complete || _failed) throw k::crypto::CryptoStateException();
        return k::crypto::Hmac::constantTimeEquals(_tag, expected);
    }

    /** Compare exactly the trusted, explicitly selected tag prefix length. */
    public const verifyTruncated(expected: const unsigned byte[], tagSize: unsigned int)
            : bool throws(k::crypto::CryptoException) {
        if (!_complete || _failed) throw k::crypto::CryptoStateException();
        return hmacVerifyTruncated(_tag, expected, tagSize);
    }

    override write(b: byte) : k::io::OutputStream<byte>& throws(k::crypto::CryptoException) {
        one : byte[1];
        one[0] = b;
        return write(one, 0u, 1u);
    }

    override write(buf: const byte[], off: unsigned int, len: unsigned int)
            : k::io::OutputStream<byte>& throws(k::crypto::CryptoException) {
        _ensureWritable();
        _failed = true;
        if (off > buf.size || len > buf.size - off) throw IndexOutOfBoundsError();
        _out->write(buf, off, len);
        _hmac->update(buf, off, len);
        _failed = false;
        return this;
    }

    override write(buf: const byte[]) : k::io::OutputStream<byte>& throws(k::crypto::CryptoException) {
        return write(buf, 0u, buf.size);
    }

    override flush() : k::io::OutputStream<byte>& throws(k::crypto::CryptoException) {
        _ensureWritable();
        _failed = true;
        _out->flush();
        _failed = false;
        return this;
    }

    override close() : k::io::OutputStream<byte>& throws(Throwable) {
        if (_closed) return this;
        _closed = true;
        if (_failed) {
            _out->close();
            return this;
        }
        _failed = true;
        _out->close();
        _tag = _hmac->finalize();
        _complete = true;
        _failed = false;
        return this;
    }
}

/**
 * MACs bytes as they are read from a borrowed input. The HMAC is consumed.
 * Bytes are released immediately, not held pending verification. A tag is
 * available only after a successful EOF; closing early leaves it incomplete.
 */
public class HmacInputStream : public k::io::FilterInputStream<byte> {
private:
    _hmac : k::crypto::Hmac!;
    _tag : unsigned byte[]!;
    _closed : bool;
    _failed : bool;
    _complete : bool;

public:
    HmacInputStream(input: k::io::InputStream<byte>&, hmac: k::crypto::Hmac!)
        : FilterInputStream(&input), _hmac(hmac), _tag(null),
          _closed(false), _failed(false), _complete(false)
            throws(k::crypto::CryptoException) {
        if (_hmac == null) throw k::crypto::CryptoInvalidInputException();
    }

    /** A borrowed tag view, or null until successful EOF. */
    public const getTag() : const unsigned byte[]? {
        if (!_complete || _failed) return null;
        return _tag;
    }

    public const isComplete() : bool { return _complete && !_failed; }

    public const verify(expected: const unsigned byte[]) : bool throws(k::crypto::CryptoException) {
        if (!_complete || _failed) throw k::crypto::CryptoStateException();
        return k::crypto::Hmac::constantTimeEquals(_tag, expected);
    }

    public const verifyTruncated(expected: const unsigned byte[], tagSize: unsigned int)
            : bool throws(k::crypto::CryptoException) {
        if (!_complete || _failed) throw k::crypto::CryptoStateException();
        return hmacVerifyTruncated(_tag, expected, tagSize);
    }

    override read() : k::Optional<byte> throws(k::crypto::CryptoException) {
        one : byte[1];
        result : Expected<unsigned int, k::io::StreamOutOfData> = read(one, 0u, 1u);
        if (result.hasError()) throw k::crypto::CryptoStateException();
        if (result.getResult() == 0u) return k::Optional<byte>();
        return k::Optional<byte>(one[0]);
    }

    override read(buf: byte[], off: unsigned int, len: unsigned int)
            : Expected<unsigned int, k::io::StreamOutOfData> throws(k::crypto::CryptoException) {
        if (off > buf.size || len > buf.size - off) {
            _failed = true;
            throw IndexOutOfBoundsError();
        }
        if (_closed || _failed) {
            err : k::io::StreamOutOfData = k::io::StreamOutOfData::Closed;
            return Expected<unsigned int, k::io::StreamOutOfData>::error(err);
        }
        if (len == 0u || _complete) {
            zero : unsigned int = 0u;
            return Expected<unsigned int, k::io::StreamOutOfData>::expected(zero);
        }
        _failed = true;
        result : Expected<unsigned int, k::io::StreamOutOfData> = _in->read(buf, off, len);
        if (result.hasError()) return result;
        count : unsigned int = result.getResult();
        if (count == 0u) {
            _tag = _hmac->finalize();
            _complete = true;
        } else {
            _hmac->update(buf, off, count);
        }
        _failed = false;
        return result;
    }

    override read(buf: byte[]) : Expected<unsigned int, k::io::StreamOutOfData>
            throws(k::crypto::CryptoException) {
        return read(buf, 0u, buf.size);
    }

    override skip(n: unsigned long) : unsigned long throws(k::crypto::CryptoException) {
        skipped : unsigned long = 0uL;
        buf : byte[256];
        while (skipped < n) {
            count : unsigned int = (unsigned int) ((n - skipped) < 256uL ? (n - skipped) : 256uL);
            result : Expected<unsigned int, k::io::StreamOutOfData> = read(buf, 0u, count);
            if (result.hasError()) throw k::crypto::CryptoStateException();
            if (result.getResult() == 0u) break;
            skipped += (unsigned long) result.getResult();
        }
        return skipped;
    }

    override available() : Expected<unsigned int, k::io::StreamOutOfData> {
        if (_closed || _failed) {
            err : k::io::StreamOutOfData = k::io::StreamOutOfData::Closed;
            return Expected<unsigned int, k::io::StreamOutOfData>::error(err);
        }
        if (_complete) {
            zero : unsigned int = 0u;
            return Expected<unsigned int, k::io::StreamOutOfData>::expected(zero);
        }
        _failed = true;
        result : Expected<unsigned int, k::io::StreamOutOfData> = _in->available();
        if (result.hasError()) return result;
        _failed = false;
        return result;
    }

    override close() throws(Throwable) {
        if (_closed) return;
        _closed = true;
        wasFailed : bool = _failed;
        _failed = true;
        _in->close();
        _failed = wasFailed;
    }
}
