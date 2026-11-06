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

public interface MessageDigest {

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

    ~OpenSSLMessageDigest() {
        if (ctx != null) {
            EVP_MD_CTX_free(ctx);
            ctx = null;
        }
    }

    override update(data : unsigned byte[]&) throws(MessageDigestException) {
        if (ctx == null) {
            throw MessageDigestException();
        }
        if (data.size != 0u && EVP_DigestUpdate(ctx, &data[0], data.size) != 1) {
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