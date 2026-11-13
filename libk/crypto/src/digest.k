/*
 * K Language standard library -- crypto module
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

/** A string naming an algorithm accepted by the OpenSSL digest provider. */
public alias MessageDigestAlgorithm : String;

/**
 * Standard digest algorithms supported by this library.
 */
public enum MessageDigestAlgorithms : MessageDigestAlgorithm {
    MD5("MD5");
    SHA1("SHA-1");
    SHA224("SHA-224");
    SHA256("SHA-256");
    SHA384("SHA-384");
    SHA512("SHA-512");
    SHA512_224("SHA-512/224");
    SHA512_256("SHA-512/256");
    SHA3_224("SHA3-224");
    SHA3_256("SHA3-256");
    SHA3_384("SHA3-384");
    SHA3_512("SHA3-512");
}

public interface MessageDigest {

    /**
     * Creates a digest using an algorithm name recognized by OpenSSL.
     *
     * @param name the algorithm name, borrowed for this call
     * @return a digest when the algorithm is available, or null otherwise
     */
    public static fromName(name: const MessageDigestAlgorithm&) : MessageDigest! {
        encodedName : unsigned byte[]! = name.toUtf8();
        try {
            length : unsigned int = encodedName.size - 1u;
            i : unsigned int = 0u;
            while (i < length) {
                if (encodedName[i] == 0) {
                    return null;
                }
                ++i;
            }

            md : EVP_MD* = __k_EVP_MD_fetch(&encodedName[0]);
            if (md == null) {
                return null;
            }

            transferred : bool = false;
            try {
                result : MessageDigest! = new OpenSSLMessageDigest(md, true);
                transferred = true;
                return result;
            } catch (e: OpenSSLBackendException&) {
                return null;
            } finally {
                if (!transferred) {
                    EVP_MD_free(md);
                }
            }
        } finally {
            delete encodedName;
        }
    }

    public static MD5() : MessageDigest! {
        try {
            return new OpenSSLMessageDigest(EVP_md5());
        } catch (e: OpenSSLBackendException&) {
            return null;
        }
    }

    public static SHA1() : MessageDigest! {
        try {
            return new OpenSSLMessageDigest(EVP_sha1());
        } catch (e: OpenSSLBackendException&) {
            return null;
        }
    }

    public static SHA224() : MessageDigest! {
        try {
            return new OpenSSLMessageDigest(EVP_sha224());
        } catch (e: OpenSSLBackendException&) {
            return null;
        }
    }

    public static SHA256() : MessageDigest! {
        try {
            return new OpenSSLMessageDigest(EVP_sha256());
        } catch (e: OpenSSLBackendException&) {
            return null;
        }
    }

    public static SHA384() : MessageDigest! {
        try {
            return new OpenSSLMessageDigest(EVP_sha384());
        } catch (e: OpenSSLBackendException&) {
            return null;
        }
    }

    public static SHA512() : MessageDigest! {
        try {
            return new OpenSSLMessageDigest(EVP_sha512());
        } catch (e: OpenSSLBackendException&) {
            return null;
        }
    }

    public static SHA512_224() : MessageDigest! {
        try {
            return new OpenSSLMessageDigest(EVP_sha512_224());
        } catch (e: OpenSSLBackendException&) {
            return null;
        }
    }

    public static SHA512_256() : MessageDigest! {
        try {
            return new OpenSSLMessageDigest(EVP_sha512_256());
        } catch (e: OpenSSLBackendException&) {
            return null;
        }
    }

    public static SHA3_224() : MessageDigest! {
        try {
            return new OpenSSLMessageDigest(EVP_sha3_224());
        } catch (e: OpenSSLBackendException&) {
            return null;
        }
    }

    public static SHA3_256() : MessageDigest! {
        try {
            return new OpenSSLMessageDigest(EVP_sha3_256());
        } catch (e: OpenSSLBackendException&) {
            return null;
        }
    }

    public static SHA3_384() : MessageDigest! {
        try {
            return new OpenSSLMessageDigest(EVP_sha3_384());
        } catch (e: OpenSSLBackendException&) {
            return null;
        }
    }

    public static SHA3_512() : MessageDigest! {
        try {
            return new OpenSSLMessageDigest(EVP_sha3_512());
        } catch (e: OpenSSLBackendException&) {
            return null;
        }
    }

    /**
     * Updates the digest with the specified data.
     *
     * @param data the data to update the digest with
     */
    update(data : unsigned byte[]&) throws(MessageDigestException);

    /**
     * Updates the digest with a range of unsigned bytes.
     *
     * @param data source byte array
     * @param off start offset in the source array
     * @param len number of bytes to update
     * @throws IndexOutOfBoundsError when the requested range is outside `data`
     * @throws MessageDigestException when the digest operation fails
     */
    update(data : const unsigned byte[], off: unsigned int, len: unsigned int)
        throws(MessageDigestException);

    /**
     * Updates the digest with a range of bytes without copying the source.
     * This overload allows byte output streams to pass their input slice
     * directly to the digest.
     *
     * @param data source byte array
     * @param off start offset in the source array
     * @param len number of bytes to update
     * @throws IndexOutOfBoundsError when the requested range is outside `data`
     * @throws MessageDigestException when the digest operation fails
     */
    update(data : const byte[], off: unsigned int, len: unsigned int)
        throws(MessageDigestException);

    /**
     * Finalizes the digest computation and returns the resulting hash.
     *
     * @return the computed hash as a byte array
     */
    finalize() : unsigned byte[]! throws(MessageDigestException);
}

private class OpenSSLBackendException : Exception {
    OpenSSLBackendException() : Exception() {}
}

private class OpenSSLMessageDigest : MessageDigest {
    ctx : EVP_MD_CTX* = null;
    ownedMd : EVP_MD* = null;

    OpenSSLMessageDigest(md: EVP_MD*) throws(OpenSSLBackendException) {
        if (md == null) {
            throw OpenSSLBackendException();
        }
        ctx = EVP_MD_CTX_new();
        if (ctx == null) {
            throw OpenSSLBackendException();
        }
        if (EVP_DigestInit_ex(ctx, md, null) != 1) {
            EVP_MD_CTX_free(ctx);
            ctx = null;
            throw OpenSSLBackendException();
        }
    }

    OpenSSLMessageDigest(md: EVP_MD*, ownsMd: bool) throws(OpenSSLBackendException) {
        if (md == null) {
            throw OpenSSLBackendException();
        }
        ctx = EVP_MD_CTX_new();
        if (ctx == null) {
            throw OpenSSLBackendException();
        }
        if (EVP_DigestInit_ex(ctx, md, null) != 1) {
            EVP_MD_CTX_free(ctx);
            ctx = null;
            throw OpenSSLBackendException();
        }
        if (ownsMd) {
            ownedMd = md;
        }
    }

    ~OpenSSLMessageDigest() {
        if (ctx != null) {
            EVP_MD_CTX_free(ctx);
            ctx = null;
        }
        if (ownedMd != null) {
            EVP_MD_free(ownedMd);
            ownedMd = null;
        }
    }

    override update(data : unsigned byte[]&) throws(MessageDigestException) {
        update(data, 0u, (unsigned int) data.size);
    }

    override update(data : const unsigned byte[], off: unsigned int, len: unsigned int)
            throws(MessageDigestException) {
        if (off > data.size || len > data.size - off) {
            throw IndexOutOfBoundsError();
        }
        dataPtr : const unsigned byte* = null;
        if (len > 0u) {
            dataPtr = &data[off];
        }
        _updateAddress(dataPtr, len);
    }

    override update(data : const byte[], off: unsigned int, len: unsigned int)
            throws(MessageDigestException) {
        if (off > data.size || len > data.size - off) {
            throw IndexOutOfBoundsError();
        }
        dataPtr : const unsigned byte* = null;
        if (len > 0u) {
            dataPtr = (const unsigned byte*) &data[off];
        }
        _updateAddress(dataPtr, len);
    }

    _updateAddress(data : const unsigned byte*, len: unsigned int)
            throws(MessageDigestException) {
        if (ctx == null) {
            throw MessageDigestException();
        }
        if (len != 0u && EVP_DigestUpdate(ctx, data, len) != 1) {
            throw MessageDigestException();
        }
    }

    override finalize() : unsigned byte[]! throws(MessageDigestException) {
        if (ctx == null) {
            throw MessageDigestException();
        }
        size : int = EVP_MD_CTX_get_size(ctx);
        if (size <= 0) {
            throw MessageDigestException();
        }
        result : unsigned byte[]! = new unsigned byte[size];
        if (EVP_DigestFinal_ex(ctx, &result[0], null) != 1) {
            throw MessageDigestException();
        }
        return result;
    }
}

// KLANG_CRYPTO_IO_DIGEST_SOURCE
/**
 * Forwards bytes to an output stream while computing their message digest.
 *
 * The wrapped output is borrowed; the digest is owned by this stream. Closing
 * the stream closes the filter chain and finalizes the digest. The read-only
 * digest result is available from `getDigest()` after close.
 */
public class MessageDigestOutputStream : public k::io::FilterOutputStream<byte> {
private:
    _digest : k::crypto::MessageDigest!;
    _result : unsigned byte[]!;
    _finalized : bool;

    MessageDigestOutputStream(output: k::io::OutputStream<byte>*,
                              digest: k::crypto::MessageDigest!)
        : FilterOutputStream(output), _digest(digest), _result(null), _finalized(false) {}

    private static _create(output: k::io::OutputStream<byte>&,
                           digest: k::crypto::MessageDigest!) : MessageDigestOutputStream! {
        if (digest == null) {
            return null;
        }
        return new MessageDigestOutputStream(&output, digest);
    }

    _ensureWritable() {
        if (_finalized) {
            throw IllegalStateException();
        }
    }

public:

    /** Create a stream using the MD5 digest implementation. */
    public static MD5(output: k::io::OutputStream<byte>&) : MessageDigestOutputStream! {
        return _create(output, k::crypto::MessageDigest::MD5());
    }

    /** Create a stream using the SHA-1 digest implementation. */
    public static SHA1(output: k::io::OutputStream<byte>&) : MessageDigestOutputStream! {
        return _create(output, k::crypto::MessageDigest::SHA1());
    }

    /** Create a stream using the SHA-224 digest implementation. */
    public static SHA224(output: k::io::OutputStream<byte>&) : MessageDigestOutputStream! {
        return _create(output, k::crypto::MessageDigest::SHA224());
    }

    /** Create a stream using the SHA-256 digest implementation. */
    public static SHA256(output: k::io::OutputStream<byte>&) : MessageDigestOutputStream! {
        return _create(output, k::crypto::MessageDigest::SHA256());
    }

    /** Create a stream using the SHA-384 digest implementation. */
    public static SHA384(output: k::io::OutputStream<byte>&) : MessageDigestOutputStream! {
        return _create(output, k::crypto::MessageDigest::SHA384());
    }

    /** Create a stream using the SHA-512 digest implementation. */
    public static SHA512(output: k::io::OutputStream<byte>&) : MessageDigestOutputStream! {
        return _create(output, k::crypto::MessageDigest::SHA512());
    }

    /** Create a stream using the SHA-512/224 digest implementation. */
    public static SHA512_224(output: k::io::OutputStream<byte>&) : MessageDigestOutputStream! {
        return _create(output, k::crypto::MessageDigest::SHA512_224());
    }

    /** Create a stream using the SHA-512/256 digest implementation. */
    public static SHA512_256(output: k::io::OutputStream<byte>&) : MessageDigestOutputStream! {
        return _create(output, k::crypto::MessageDigest::SHA512_256());
    }

    /** Create a stream using the SHA3-224 digest implementation. */
    public static SHA3_224(output: k::io::OutputStream<byte>&) : MessageDigestOutputStream! {
        return _create(output, k::crypto::MessageDigest::SHA3_224());
    }

    /** Create a stream using the SHA3-256 digest implementation. */
    public static SHA3_256(output: k::io::OutputStream<byte>&) : MessageDigestOutputStream! {
        return _create(output, k::crypto::MessageDigest::SHA3_256());
    }

    /** Create a stream using the SHA3-384 digest implementation. */
    public static SHA3_384(output: k::io::OutputStream<byte>&) : MessageDigestOutputStream! {
        return _create(output, k::crypto::MessageDigest::SHA3_384());
    }

    /** Create a stream using the SHA3-512 digest implementation. */
    public static SHA3_512(output: k::io::OutputStream<byte>&) : MessageDigestOutputStream! {
        return _create(output, k::crypto::MessageDigest::SHA3_512());
    }

    /**
     * Create a stream using an algorithm name recognized by OpenSSL.
     *
     * @param name algorithm name, borrowed for this call
     * @param output wrapped output stream, borrowed by the returned stream
     * @return the stream, or null when the digest is unavailable
     */
    public static fromName(name: const k::crypto::MessageDigestAlgorithm&,
                           output: k::io::OutputStream<byte>&) : MessageDigestOutputStream! {
        return _create(output, k::crypto::MessageDigest::fromName(name));
    }

    /**
     * Return a read-only view of the finalized digest, or null before a
     * successful close-time finalization.
     */
    public const getDigest() : const unsigned byte[]? {
        return _result;
    }

    override write(b: byte) : k::io::OutputStream<byte>&
            throws(k::crypto::MessageDigestException) {
        _ensureWritable();
        _out->write(b);
        bytes : byte[1];
        bytes[0] = b;
        _digest->update(bytes, 0u, 1u);
        return this;
    }

    override write(buf: const byte[], off: unsigned int, len: unsigned int)
            : k::io::OutputStream<byte>& throws(k::crypto::MessageDigestException) {
        _ensureWritable();
        if (off > buf.size || len > buf.size - off) {
            throw IndexOutOfBoundsError();
        }
        _out->write(buf, off, len);
        _digest->update(buf, off, len);
        return this;
    }

    override write(buf: const byte[]) : k::io::OutputStream<byte>&
            throws(k::crypto::MessageDigestException) {
        return write(buf, 0u, (unsigned int) buf.size);
    }

    override flush() : k::io::OutputStream<byte>& {
        _out->flush();
        return this;
    }

    override close() : k::io::OutputStream<byte>&
            throws(k::crypto::MessageDigestException) {
        if (_finalized) {
            return this;
        }

        // Freeze before closing so failures cannot make a second close
        // re-finalize the digest or allow further writes.
        _finalized = true;
        try {
            flush();
            _out->close();
        } finally {
            _result = _digest->finalize();
        }
        return this;
    }
}
