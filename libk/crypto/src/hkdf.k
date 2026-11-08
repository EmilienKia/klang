/*
 * K Language standard library -- HKDF
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */
module k::crypto;

private struct NativeHkdf {}
private struct NativeHkdfPrk {}

@ffi::Extern("C") private __k_hkdf_new(name: const unsigned byte*, status: int*) : NativeHkdf*;
@ffi::Extern("C") private __k_hkdf_free(h: NativeHkdf*);
@ffi::Extern("C") private __k_hkdf_size(h: const NativeHkdf*) : unsigned int;
@ffi::Extern("C") private __k_hkdf_import(h: const NativeHkdf*, data: const unsigned byte*,
    length: unsigned int, status: int*) : NativeHkdfPrk*;
@ffi::Extern("C") private __k_hkdf_extract(h: const NativeHkdf*, data: const unsigned byte*,
    length: unsigned int, salt: const unsigned byte*, saltLength: unsigned int,
    absent: int, status: int*) : NativeHkdfPrk*;
@ffi::Extern("C") private __k_hkdf_derive(h: const NativeHkdf*, mode: int,
    data: const unsigned byte*, length: unsigned int, salt: const unsigned byte*,
    saltLength: unsigned int, absent: int, info: const unsigned byte*,
    infoLength: unsigned int, out: unsigned byte*, outLength: unsigned int) : int;
@ffi::Extern("C") private __k_hkdf_prk_free(p: NativeHkdfPrk*);
@ffi::Extern("C") private __k_hkdf_prk_size(p: const NativeHkdfPrk*) : unsigned int;
@ffi::Extern("C") private __k_hkdf_prk_hash_size(p: const NativeHkdfPrk*) : unsigned int;
@ffi::Extern("C") private __k_hkdf_prk_export(p: const NativeHkdfPrk*, out: unsigned byte*);
@ffi::Extern("C") private __k_hkdf_expand(p: const NativeHkdfPrk*, info: const unsigned byte*,
    infoLength: unsigned int, out: unsigned byte*, length: unsigned int) : int;
@ffi::Extern("C") private __k_hkdf_clear(data: unsigned byte*, length: unsigned int);
@ffi::Extern("C") private __k_hmac_key_borrow(key: const NativeHmacKey*) : const unsigned byte*;
@ffi::Extern("C") private __k_symmetric_key_borrow(key: const NativeSymmetricKey*) : const unsigned byte*;

private hkdfLength(data: const unsigned byte[]) : unsigned int throws(CryptoException) {
    if (data.size > 0xffffffffuL) throw CryptoInvalidInputException();
    return (unsigned int) data.size;
}

private hkdfData(data: const unsigned byte[]) : const unsigned byte* {
    if (data.size == 0u) return null;
    return &data[0u];
}

private hkdfClear(data: unsigned byte[]) {
    if (data.size != 0u) __k_hkdf_clear(&data[0u], (unsigned int) data.size);
}

private hkdfCheck(status: int) throws(CryptoException) {
    if (status == 0) return;
    if (status == -1) throw CryptoInvalidInputException();
    throw CryptoOperationException();
}

/**
 * An immutable provider-backed HKDF descriptor. Empty salt is distinct from
 * the no-salt overload, which supplies the RFC 5869 HashLen zero bytes.
 * OpenSSL's standard HKDF provider limits info to 1024 bytes; provider
 * rejection is reported as an operation failure, never silently truncated.
 * The OpenSSL library context must outlive this descriptor, its PRKs and
 * their operations. Threads using a nondefault context must exit before it is
 * freed, or perform OpenSSL's per-thread context cleanup before it is freed.
 * Existing caller OpenSSL errors may cause provider execution on a worker
 * thread to preserve the error queue, which can affect thread-affine providers.
 * Operations refetch the digest and HKDF under current context properties.
 * They fail if the selected provider or canonical algorithm changes, or the
 * digest size changes. Same-provider implementations selected by current
 * properties may differ from those selected when the descriptor was created.
 * Valid factory, PRK import, extract, derive and expand calls fail with
 * CryptoOperationException inside an active OpenSSL ASYNC job.
 */
public final class Hkdf : CryptoObject {
private:
    _native: NativeHkdf*;
    _algorithm: MessageDigestAlgorithm;
    _size: unsigned int;

    Hkdf(native: NativeHkdf*, algorithm: const MessageDigestAlgorithm&)
        : _native(native), _algorithm(algorithm), _size(__k_hkdf_size(native)) {}
    Hkdf(other: const Hkdf&) -> delete;
    operator =(other: const Hkdf&) -> delete;

    const _extract(data: const unsigned byte*, length: unsigned int,
                   salt: const unsigned byte*, saltLength: unsigned int, absent: int)
            : HkdfPrk! throws(CryptoException) {
        status: int = 0;
        native: NativeHkdfPrk* = __k_hkdf_extract(_native, data, length, salt,
                                                   saltLength, absent, &status);
        hkdfCheck(status);
        if (native == null) throw CryptoOperationException();
        transferred: bool = false;
        try {
            result: HkdfPrk! = new HkdfPrk(native, _algorithm);
            transferred = true;
            return result;
        } finally {
            if (!transferred) __k_hkdf_prk_free(native);
        }
    }

    const _derive(data: const unsigned byte*, length: unsigned int,
                  salt: const unsigned byte*, saltLength: unsigned int, absent: int,
                  info: const unsigned byte[], outputLength: unsigned int)
            : unsigned byte[]! throws(CryptoException) {
        infoLength: unsigned int = hkdfLength(info);
        if (outputLength > maxOutputSize()) throw CryptoInvalidInputException();
        result: unsigned byte[]! = new unsigned byte[outputLength];
        success: bool = false;
        try {
            out: unsigned byte* = null;
            if (outputLength != 0u) out = &result[0u];
            hkdfCheck(__k_hkdf_derive(_native, 0, data, length, salt, saltLength,
                                      absent, hkdfData(info), infoLength, out, outputLength));
            success = true;
            return result;
        } finally {
            if (!success) {
                hkdfClear(result);
                delete result;
            }
        }
    }
public:
    ~Hkdf() {
        __k_hkdf_free(_native);
        _native = null;
    }

    /** Returns null for an unavailable digest or KDF, including XOF digests. */
    static create(algorithm: const MessageDigestAlgorithm&) : Hkdf! throws(CryptoException) {
        encoded: unsigned byte[]! = algorithm.toUtf8();
        native: NativeHkdf* = null;
        status: int = 0;
        valid: bool = true;
        try {
            i: unsigned int = 0u;
            while (i + 1u < encoded.size) {
                if (encoded[i] == 0) {
                    valid = false;
                    break;
                }
                ++i;
            }
            if (valid) native = __k_hkdf_new(&encoded[0u], &status);
        } finally {
            delete encoded;
        }
        if (!valid || status == 1) return null;
        hkdfCheck(status);
        if (native == null) throw CryptoOperationException();
        transferred: bool = false;
        try {
            result: Hkdf! = new Hkdf(native, algorithm);
            transferred = true;
            return result;
        } finally {
            if (!transferred) __k_hkdf_free(native);
        }
    }
    static SHA256() : Hkdf! throws(CryptoException) {
        return create(MessageDigestAlgorithms::SHA256);
    }
    static SHA512() : Hkdf! throws(CryptoException) {
        return create(MessageDigestAlgorithms::SHA512);
    }
    const algorithm() : MessageDigestAlgorithm { return String(_algorithm); }
    const hashSize() : unsigned int { return _size; }
    const maxOutputSize() : unsigned int { return _size * 255u; }

    const importPrk(bytes: const unsigned byte[]) : HkdfPrk! throws(CryptoException) {
        length: unsigned int = hkdfLength(bytes);
        status: int = 0;
        native: NativeHkdfPrk* = __k_hkdf_import(_native, hkdfData(bytes), length, &status);
        hkdfCheck(status);
        if (native == null) throw CryptoOperationException();
        transferred: bool = false;
        try {
            result: HkdfPrk! = new HkdfPrk(native, _algorithm);
            transferred = true;
            return result;
        } finally {
            if (!transferred) __k_hkdf_prk_free(native);
        }
    }

    const extract(ikm: const unsigned byte[], salt: const unsigned byte[])
            : HkdfPrk! throws(CryptoException) {
        return _extract(hkdfData(ikm), hkdfLength(ikm), hkdfData(salt), hkdfLength(salt), 0);
    }
    const extract(ikm: const unsigned byte[]) : HkdfPrk! throws(CryptoException) {
        return _extract(hkdfData(ikm), hkdfLength(ikm), null, 0u, 1);
    }
    const extract(ikm: const HmacKey&, salt: const unsigned byte[])
            : HkdfPrk! throws(CryptoException) {
        return _extract(__k_hmac_key_borrow(ikm._native), ikm.keySize(),
                        hkdfData(salt), hkdfLength(salt), 0);
    }
    const extract(ikm: const HmacKey&) : HkdfPrk! throws(CryptoException) {
        return _extract(__k_hmac_key_borrow(ikm._native), ikm.keySize(), null, 0u, 1);
    }
    const extract(ikm: const SymmetricKey&, salt: const unsigned byte[])
            : HkdfPrk! throws(CryptoException) {
        return _extract(__k_symmetric_key_borrow(ikm._native), ikm.keySize(),
                        hkdfData(salt), hkdfLength(salt), 0);
    }
    const extract(ikm: const SymmetricKey&) : HkdfPrk! throws(CryptoException) {
        return _extract(__k_symmetric_key_borrow(ikm._native), ikm.keySize(), null, 0u, 1);
    }

    const derive(ikm: const unsigned byte[], salt: const unsigned byte[],
                 info: const unsigned byte[], length: unsigned int)
            : unsigned byte[]! throws(CryptoException) {
        return _derive(hkdfData(ikm), hkdfLength(ikm), hkdfData(salt), hkdfLength(salt),
                       0, info, length);
    }
    const derive(ikm: const unsigned byte[], info: const unsigned byte[], length: unsigned int)
            : unsigned byte[]! throws(CryptoException) {
        return _derive(hkdfData(ikm), hkdfLength(ikm), null, 0u, 1, info, length);
    }
    const derive(ikm: const HmacKey&, salt: const unsigned byte[],
                 info: const unsigned byte[], length: unsigned int)
            : unsigned byte[]! throws(CryptoException) {
        return _derive(__k_hmac_key_borrow(ikm._native), ikm.keySize(),
                       hkdfData(salt), hkdfLength(salt), 0, info, length);
    }
    const derive(ikm: const HmacKey&, info: const unsigned byte[], length: unsigned int)
            : unsigned byte[]! throws(CryptoException) {
        return _derive(__k_hmac_key_borrow(ikm._native), ikm.keySize(), null, 0u, 1, info, length);
    }
    const derive(ikm: const SymmetricKey&, salt: const unsigned byte[],
                 info: const unsigned byte[], length: unsigned int)
            : unsigned byte[]! throws(CryptoException) {
        return _derive(__k_symmetric_key_borrow(ikm._native), ikm.keySize(),
                       hkdfData(salt), hkdfLength(salt), 0, info, length);
    }
    const derive(ikm: const SymmetricKey&, info: const unsigned byte[], length: unsigned int)
            : unsigned byte[]! throws(CryptoException) {
        return _derive(__k_symmetric_key_borrow(ikm._native), ikm.keySize(), null, 0u, 1, info, length);
    }

    const deriveSymmetricKey(ikm: const unsigned byte[], salt: const unsigned byte[],
                             cipher: const SymmetricCipher&, info: const unsigned byte[])
            : SymmetricKey! throws(CryptoException) {
        bytes: unsigned byte[]! = derive(ikm, salt, info, cipher.keySize());
        try { return cipher.importKey(bytes); }
        finally { hkdfClear(bytes); delete bytes; }
    }
    const deriveSymmetricKey(ikm: const unsigned byte[], cipher: const SymmetricCipher&,
                             info: const unsigned byte[]) : SymmetricKey! throws(CryptoException) {
        bytes: unsigned byte[]! = derive(ikm, info, cipher.keySize());
        try { return cipher.importKey(bytes); }
        finally { hkdfClear(bytes); delete bytes; }
    }
    const deriveSymmetricKey(ikm: const HmacKey&, salt: const unsigned byte[],
                             cipher: const SymmetricCipher&, info: const unsigned byte[])
            : SymmetricKey! throws(CryptoException) {
        bytes: unsigned byte[]! = derive(ikm, salt, info, cipher.keySize());
        try { return cipher.importKey(bytes); }
        finally { hkdfClear(bytes); delete bytes; }
    }
    const deriveSymmetricKey(ikm: const HmacKey&, cipher: const SymmetricCipher&,
                             info: const unsigned byte[]) : SymmetricKey! throws(CryptoException) {
        bytes: unsigned byte[]! = derive(ikm, info, cipher.keySize());
        try { return cipher.importKey(bytes); }
        finally { hkdfClear(bytes); delete bytes; }
    }
    const deriveSymmetricKey(ikm: const SymmetricKey&, salt: const unsigned byte[],
                             cipher: const SymmetricCipher&, info: const unsigned byte[])
            : SymmetricKey! throws(CryptoException) {
        bytes: unsigned byte[]! = derive(ikm, salt, info, cipher.keySize());
        try { return cipher.importKey(bytes); }
        finally { hkdfClear(bytes); delete bytes; }
    }
    const deriveSymmetricKey(ikm: const SymmetricKey&, cipher: const SymmetricCipher&,
                             info: const unsigned byte[]) : SymmetricKey! throws(CryptoException) {
        bytes: unsigned byte[]! = derive(ikm, info, cipher.keySize());
        try { return cipher.importKey(bytes); }
        finally { hkdfClear(bytes); delete bytes; }
    }

    const deriveHmacKey(ikm: const unsigned byte[], salt: const unsigned byte[],
                        info: const unsigned byte[], length: unsigned int)
            : HmacKey! throws(CryptoException) {
        bytes: unsigned byte[]! = derive(ikm, salt, info, length);
        try { return HmacKey::fromBytes(bytes); }
        finally { hkdfClear(bytes); delete bytes; }
    }
    const deriveHmacKey(ikm: const unsigned byte[], info: const unsigned byte[], length: unsigned int)
            : HmacKey! throws(CryptoException) {
        bytes: unsigned byte[]! = derive(ikm, info, length);
        try { return HmacKey::fromBytes(bytes); }
        finally { hkdfClear(bytes); delete bytes; }
    }
    const deriveHmacKey(ikm: const HmacKey&, salt: const unsigned byte[],
                        info: const unsigned byte[], length: unsigned int)
            : HmacKey! throws(CryptoException) {
        bytes: unsigned byte[]! = derive(ikm, salt, info, length);
        try { return HmacKey::fromBytes(bytes); }
        finally { hkdfClear(bytes); delete bytes; }
    }
    const deriveHmacKey(ikm: const HmacKey&, info: const unsigned byte[], length: unsigned int)
            : HmacKey! throws(CryptoException) {
        bytes: unsigned byte[]! = derive(ikm, info, length);
        try { return HmacKey::fromBytes(bytes); }
        finally { hkdfClear(bytes); delete bytes; }
    }
    const deriveHmacKey(ikm: const SymmetricKey&, salt: const unsigned byte[],
                        info: const unsigned byte[], length: unsigned int)
            : HmacKey! throws(CryptoException) {
        bytes: unsigned byte[]! = derive(ikm, salt, info, length);
        try { return HmacKey::fromBytes(bytes); }
        finally { hkdfClear(bytes); delete bytes; }
    }
    const deriveHmacKey(ikm: const SymmetricKey&, info: const unsigned byte[], length: unsigned int)
            : HmacKey! throws(CryptoException) {
        bytes: unsigned byte[]! = derive(ikm, info, length);
        try { return HmacKey::fromBytes(bytes); }
        finally { hkdfClear(bytes); delete bytes; }
    }
}

/** A noncopyable, independently owned pseudorandom key, cleared on destruction. */
public final class HkdfPrk : Key {
    friend Hkdf;
private:
    _native: NativeHkdfPrk*;
    _algorithm: MessageDigestAlgorithm;

    HkdfPrk(native: NativeHkdfPrk*, algorithm: const MessageDigestAlgorithm&)
        : _native(native), _algorithm(algorithm) {}
    HkdfPrk(other: const HkdfPrk&) -> delete;
    operator =(other: const HkdfPrk&) -> delete;
public:
    ~HkdfPrk() {
        __k_hkdf_prk_free(_native);
        _native = null;
    }
    const keySize() : unsigned int { return __k_hkdf_prk_size(_native); }
    const algorithm() : MessageDigestAlgorithm { return String(_algorithm); }

    /** Explicitly exports sensitive key material; the caller owns the returned bytes. */
    const exportBytes() : unsigned byte[]! {
        result: unsigned byte[]! = new unsigned byte[keySize()];
        __k_hkdf_prk_export(_native, &result[0u]);
        return result;
    }

    const expand(info: const unsigned byte[], length: unsigned int)
            : unsigned byte[]! throws(CryptoException) {
        infoLength: unsigned int = hkdfLength(info);
        if (length > __k_hkdf_prk_hash_size(_native) * 255u)
            throw CryptoInvalidInputException();
        result: unsigned byte[]! = new unsigned byte[length];
        success: bool = false;
        try {
            out: unsigned byte* = null;
            if (length != 0u) out = &result[0u];
            hkdfCheck(__k_hkdf_expand(_native, hkdfData(info), infoLength, out, length));
            success = true;
            return result;
        } finally {
            if (!success) {
                hkdfClear(result);
                delete result;
            }
        }
    }
    const expandSymmetricKey(cipher: const SymmetricCipher&, info: const unsigned byte[])
            : SymmetricKey! throws(CryptoException) {
        bytes: unsigned byte[]! = expand(info, cipher.keySize());
        try { return cipher.importKey(bytes); }
        finally { hkdfClear(bytes); delete bytes; }
    }
    const expandHmacKey(info: const unsigned byte[], length: unsigned int)
            : HmacKey! throws(CryptoException) {
        bytes: unsigned byte[]! = expand(info, length);
        try { return HmacKey::fromBytes(bytes); }
        finally { hkdfClear(bytes); delete bytes; }
    }
}
