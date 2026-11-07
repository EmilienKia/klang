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

// Keep only the final GCM tag; all earlier bytes go directly to the session.
private class CipherTrailer {
private:
    _tag : byte[]!;
    _tagSize : unsigned int;
    _count : unsigned int;

public:
    CipherTrailer() : _tag(null), _tagSize(0u), _count(0u) {}

    initialize(tagSize: unsigned int) throws(k::crypto::CryptoException) {
        if (tagSize == 0u) throw k::crypto::CryptoInvalidInputException();
        _tag = new byte[tagSize];
        _tagSize = tagSize;
    }

    accept(session: k::crypto::DecryptSession*, data: const byte[],
           off: unsigned int, len: unsigned int) throws(k::crypto::CryptoException) {
        space : unsigned int = _tagSize - _count;
        if (len <= space) {
            i : unsigned int = 0u;
            while (i < len) {
                _tag[_count + i] = data[off + i];
                ++i;
            }
            _count += len;
            return;
        }

        excess : unsigned int = len - space;
        oldBytes : unsigned int = excess < _count ? excess : _count;
        if (oldBytes != 0u) {
            session->update(_tag, 0u, oldBytes);
        }
        newBytes : unsigned int = excess - oldBytes;
        if (newBytes != 0u) {
            session->update(data, off, newBytes);
        }
        kept : unsigned int = _count - oldBytes;
        i : unsigned int = 0u;
        while (i < kept) {
            _tag[i] = _tag[oldBytes + i];
            ++i;
        }
        i = 0u;
        while (i < len - newBytes) {
            _tag[kept + i] = data[off + newBytes + i];
            ++i;
        }
        _count = _tagSize;
    }

    finish(session: k::crypto::DecryptSession*) throws(k::crypto::CryptoException) {
        if (_count != _tagSize) throw k::crypto::CryptoAuthenticationException();
        session->finalize(_tag, 0u, _count);
    }
}

/**
 * Encrypts a borrowed source as a single ciphertext-and-tag message.
 * The session is consumed; configure its AAD before transferring it.
 */
public class EncryptCipherInputStream : public k::io::FilterInputStream<byte> {
private:
    _session : k::crypto::EncryptSession!;
    _pending : byte[]!;
    _pos : unsigned int;
    _count : unsigned int;
    _finalized : bool;
    _complete : bool;
    _closed : bool;
    _failed : bool;

public:
    EncryptCipherInputStream(input: k::io::InputStream<byte>&,
                             session: k::crypto::EncryptSession!)
        : FilterInputStream(&input), _session(session), _pending(null), _pos(0u), _count(0u),
          _finalized(false), _complete(false), _closed(false), _failed(false)
            throws(k::crypto::CryptoException) {
        if (_session == null) throw k::crypto::CryptoInvalidInputException();
        tagSize : unsigned int = _session->tagSize();
        if (tagSize == 0u) throw k::crypto::CryptoInvalidInputException();
        capacity : unsigned int = tagSize > 4096u ? tagSize : 4096u;
        _pending = new byte[capacity];
    }

    /** True once the entire tag has been delivered. */
    public const isComplete() : bool { return _complete; }

    override read() : k::Optional<byte> throws(k::crypto::CryptoException) {
        one : byte[1];
        result : Expected<unsigned int, k::io::StreamOutOfData> = read(one, 0u, 1u);
        if (result.hasError()) throw k::crypto::CryptoStateException();
        if (result.getResult() == 0u) return k::Optional<byte>();
        return k::Optional<byte>(one[0]);
    }

    override read(buf: byte[], off: unsigned int, len: unsigned int)
            : Expected<unsigned int, k::io::StreamOutOfData>
            throws(k::crypto::CryptoException) {
        if (off > buf.size || len > buf.size - off) throw IndexOutOfBoundsError();
        if (_closed || _failed) {
            err : k::io::StreamOutOfData = k::io::StreamOutOfData::Closed;
            return Expected<unsigned int, k::io::StreamOutOfData>::error(err);
        }
        if (len == 0u || _complete) {
            zero : unsigned int = 0u;
            return Expected<unsigned int, k::io::StreamOutOfData>::expected(zero);
        }
        if (_pos == _count) {
            input : byte[4096];
            _failed = true;
            source : Expected<unsigned int, k::io::StreamOutOfData> = _in->read(input, 0u, 4096u);
            if (source.hasError()) return source;
            n : unsigned int = source.getResult();
            _pos = 0u;
            if (n == 0u) {
                _count = _session->finalizeTo(_pending, 0u);
                _finalized = true;
            } else {
                _count = _session->updateTo(input, 0u, n, _pending, 0u);
            }
            _failed = false;
        }
        n : unsigned int = _count - _pos;
        if (n > len) n = len;
        i : unsigned int = 0u;
        while (i < n) {
            buf[off + i] = _pending[_pos + i];
            ++i;
        }
        _pos += n;
        if (_finalized && _pos == _count) _complete = true;
        return Expected<unsigned int, k::io::StreamOutOfData>::expected(n);
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
        count : unsigned int = _count - _pos;
        return Expected<unsigned int, k::io::StreamOutOfData>::expected(count);
    }

    override close() throws(Throwable) {
        if (_closed) return;
        _closed = true;
        try {
            _session->close();
        } catch (e: Throwable&) {
            try {
                _in->close();
            } catch (ignored: Throwable&) {}
            throw;
        }
        _in->close();
    }
}

/**
 * Authenticates a borrowed source before releasing any staged plaintext.
 * Closing before source EOF aborts verification.
 */
public class DecryptCipherInputStream : public k::io::FilterInputStream<byte> {
private:
    _session : k::crypto::DecryptSession!;
    _trailer : CipherTrailer;
    _verified : bool;
    _complete : bool;
    _closed : bool;
    _failed : bool;

public:
    DecryptCipherInputStream(input: k::io::InputStream<byte>&,
                             session: k::crypto::DecryptSession!)
        : FilterInputStream(&input), _session(session), _verified(false),
          _complete(false), _closed(false), _failed(false)
            throws(k::crypto::CryptoException) {
        if (_session == null) throw k::crypto::CryptoInvalidInputException();
        _trailer.initialize(_session->tagSize());
    }

    public const isVerified() : bool { return _verified; }
    /** True once verified plaintext EOF has been observed. */
    public const isComplete() : bool { return _complete; }

    override read() : k::Optional<byte> throws(k::crypto::CryptoException) {
        one : byte[1];
        result : Expected<unsigned int, k::io::StreamOutOfData> = read(one, 0u, 1u);
        if (result.hasError()) throw k::crypto::CryptoStateException();
        if (result.getResult() == 0u) return k::Optional<byte>();
        return k::Optional<byte>(one[0]);
    }

    override read(buf: byte[], off: unsigned int, len: unsigned int)
            : Expected<unsigned int, k::io::StreamOutOfData>
            throws(k::crypto::CryptoException) {
        if (off > buf.size || len > buf.size - off) throw IndexOutOfBoundsError();
        if (_closed || _failed) {
            err : k::io::StreamOutOfData = k::io::StreamOutOfData::Closed;
            return Expected<unsigned int, k::io::StreamOutOfData>::error(err);
        }
        if (len == 0u || _complete) {
            zero : unsigned int = 0u;
            return Expected<unsigned int, k::io::StreamOutOfData>::expected(zero);
        }
        _failed = true;
        if (!_verified) {
            input : byte[4096];
            while (true) {
                source : Expected<unsigned int, k::io::StreamOutOfData> = _in->read(input, 0u, 4096u);
                if (source.hasError()) return source;
                n : unsigned int = source.getResult();
                if (n == 0u) {
                    _trailer.finish(_session);
                    _verified = true;
                    break;
                }
                _trailer.accept(_session, input, 0u, n);
            }
        }
        n : unsigned int = _session->read(buf, off, len);
        if (n == 0u) _complete = true;
        _failed = false;
        return Expected<unsigned int, k::io::StreamOutOfData>::expected(n);
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
        // The staging API has no non-blocking availability query.
        zero : unsigned int = 0u;
        return Expected<unsigned int, k::io::StreamOutOfData>::expected(zero);
    }

    override close() throws(Throwable) {
        if (_closed) return;
        _closed = true;
        try {
            _session->close();
        } catch (e: Throwable&) {
            try {
                _in->close();
            } catch (ignored: Throwable&) {}
            throw;
        }
        _in->close();
    }
}

/** Encrypts writes to a borrowed destination; close appends the GCM tag. */
public class EncryptCipherOutputStream : public k::io::FilterOutputStream<byte> {
private:
    _session : k::crypto::EncryptSession!;
    _complete : bool;
    _closed : bool;
    _failed : bool;
    _ensureWritable() throws(k::crypto::CryptoException) {
        if (_closed || _failed) throw k::crypto::CryptoStateException();
    }

    _closeResources() throws(Throwable) {
        try {
            _session->close();
        } catch (e: Throwable&) {
            try {
                _out->close();
            } catch (ignored: Throwable&) {}
            throw;
        }
        _out->close();
    }

public:
    EncryptCipherOutputStream(output: k::io::OutputStream<byte>&,
                              session: k::crypto::EncryptSession!)
        : FilterOutputStream(&output), _session(session), _complete(false),
          _closed(false), _failed(false) throws(k::crypto::CryptoException) {
        if (_session == null) throw k::crypto::CryptoInvalidInputException();
    }

    public const isComplete() : bool { return _complete; }

    override write(b: byte) : k::io::OutputStream<byte>& throws(k::crypto::CryptoException) {
        one : byte[1];
        one[0] = b;
        return write(one, 0u, 1u);
    }

    override write(buf: const byte[], off: unsigned int, len: unsigned int)
            : k::io::OutputStream<byte>& throws(k::crypto::CryptoException) {
        _ensureWritable();
        if (off > buf.size || len > buf.size - off) throw IndexOutOfBoundsError();
        if (len == 0u) return this;
        _failed = true;
        encrypted : byte[4096];
        pos : unsigned int = 0u;
        while (pos < len) {
            count : unsigned int = len - pos;
            if (count > 4096u) count = 4096u;
            written : unsigned int = _session->updateTo(buf, off + pos, count, encrypted, 0u);
            _out->write(encrypted, 0u, written);
            pos += count;
        }
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
        if (_closed) {
            if (!_complete) throw k::crypto::CryptoStateException();
            return this;
        }
        _closed = true;
        if (_failed) {
            _closeResources();
            throw k::crypto::CryptoStateException();
        }
        _failed = true;
        try {
            tagSize : unsigned int = _session->tagSize();
            if (tagSize == 0u) throw k::crypto::CryptoInvalidInputException();
            tag : byte[]! = new byte[tagSize];
            count : unsigned int = _session->finalizeTo(tag, 0u);
            _out->write(tag, 0u, count);
            _out->flush();
        } catch (e: Throwable&) {
            try {
                _closeResources();
            } catch (ignored: Throwable&) {}
            throw;
        }
        _closeResources();
        _failed = false;
        _complete = true;
        return this;
    }
}

/** Stages ciphertext and writes plaintext only after close-time authentication. */
public class DecryptCipherOutputStream : public k::io::FilterOutputStream<byte> {
private:
    _session : k::crypto::DecryptSession!;
    _trailer : CipherTrailer;
    _verified : bool;
    _complete : bool;
    _closed : bool;
    _failed : bool;
    _ensureWritable() throws(k::crypto::CryptoException) {
        if (_closed || _failed) throw k::crypto::CryptoStateException();
    }

    _closeResources() throws(Throwable) {
        try {
            _session->close();
        } catch (e: Throwable&) {
            try {
                _out->close();
            } catch (ignored: Throwable&) {}
            throw;
        }
        _out->close();
    }

public:
    DecryptCipherOutputStream(output: k::io::OutputStream<byte>&,
                              session: k::crypto::DecryptSession!)
        : FilterOutputStream(&output), _session(session), _verified(false),
          _complete(false), _closed(false), _failed(false)
            throws(k::crypto::CryptoException) {
        if (_session == null) throw k::crypto::CryptoInvalidInputException();
        _trailer.initialize(_session->tagSize());
    }

    public const isVerified() : bool { return _verified; }
    public const isComplete() : bool { return _complete; }

    override write(b: byte) : k::io::OutputStream<byte>& throws(k::crypto::CryptoException) {
        one : byte[1];
        one[0] = b;
        return write(one, 0u, 1u);
    }

    override write(buf: const byte[], off: unsigned int, len: unsigned int)
            : k::io::OutputStream<byte>& throws(k::crypto::CryptoException) {
        _ensureWritable();
        if (off > buf.size || len > buf.size - off) throw IndexOutOfBoundsError();
        if (len == 0u) return this;
        _failed = true;
        _trailer.accept(_session, buf, off, len);
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
        if (_closed) {
            if (!_complete) throw k::crypto::CryptoStateException();
            return this;
        }
        _closed = true;
        if (_failed) {
            _closeResources();
            throw k::crypto::CryptoStateException();
        }
        _failed = true;
        try {
            _trailer.finish(_session);
            _verified = true;
            plain : byte[4096];
            while (true) {
                count : unsigned int = _session->read(plain, 0u, 4096u);
                if (count == 0u) break;
                _out->write(plain, 0u, count);
            }
            _out->flush();
        } catch (e: Throwable&) {
            try {
                _closeResources();
            } catch (ignored: Throwable&) {}
            throw;
        }
        _closeResources();
        _failed = false;
        _complete = true;
        return this;
    }
}
