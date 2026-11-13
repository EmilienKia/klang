/*
 * K Language standard library -- HMAC
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */
module k::crypto;

private struct NativeHmacKey {}
private struct NativeHmac {}

@ffi::Extern("C") private __k_hmac_key_new(data: const unsigned byte*, length: unsigned int)
    : NativeHmacKey*;
@ffi::Extern("C") private __k_hmac_key_free(key: NativeHmacKey*);
@ffi::Extern("C") private __k_hmac_new(name: const unsigned byte*, key: const NativeHmacKey*,
                                       status: int*) : NativeHmac*;
@ffi::Extern("C") private __k_hmac_free(hmac: NativeHmac*);
@ffi::Extern("C") private __k_hmac_size(hmac: const NativeHmac*) : unsigned int;
@ffi::Extern("C") private __k_hmac_update(hmac: NativeHmac*, data: const unsigned byte*,
                                          length: unsigned int) : int;
@ffi::Extern("C") private __k_hmac_final(hmac: NativeHmac*, out: unsigned byte*) : int;
@ffi::Extern("C") private __k_hmac_equal(a: const unsigned byte*, b: const unsigned byte*,
                                         length: unsigned int) : int;

/** Imported HMAC key material is copied and cleared when the key is destroyed. */
public final class HmacKey : Key {
    friend Hmac;
    friend Hkdf;
private:
    _native: NativeHmacKey*;
    _size: unsigned int;

    HmacKey(data: const unsigned byte*, size: unsigned int)
            : _native(null), _size(size) throws(CryptoException) {
        _native = __k_hmac_key_new(data, size);
        if (_native == null) throw CryptoOperationException();
    }
    HmacKey(other: const HmacKey&) -> delete;
    operator =(other: const HmacKey&) -> delete;
public:
    static fromBytes(data: const unsigned byte[]) : HmacKey! throws(CryptoException) {
        ptr: const unsigned byte* = null;
        if (data.size != 0u) ptr = &data[0u];
        return new HmacKey(ptr, (unsigned int) data.size);
    }

    ~HmacKey() {
        __k_hmac_key_free(_native);
        _native = null;
    }

    const keySize() : unsigned int { return _size; }
}

/** A one-message HMAC; finalization and verification consume the instance. */
public final class Hmac : CryptoObject {
private:
    _native: NativeHmac*;
    _algorithm: MessageDigestAlgorithm;
    _size: unsigned int;
    _active: bool;

    Hmac(native: NativeHmac*, algorithm: const MessageDigestAlgorithm&)
            : _native(native), _algorithm(algorithm), _size(__k_hmac_size(native)), _active(true) {}
    Hmac(other: const Hmac&) -> delete;
    operator =(other: const Hmac&) -> delete;

    _ensureActive() throws(CryptoException) {
        if (!_active) throw CryptoStateException();
    }

    _update(data: const unsigned byte*, length: unsigned int) throws(CryptoException) {
        _ensureActive();
        status: int = __k_hmac_update(_native, data, length);
        if (status != 0) {
            _active = false;
            if (status == -4) throw CryptoStateException();
            throw CryptoOperationException();
        }
    }

    _checkTruncation(size: unsigned int) throws(CryptoException) {
        _ensureActive();
        if (size == 0u || size > _size) throw CryptoInvalidInputException();
    }
public:
    ~Hmac() {
        __k_hmac_free(_native);
        _native = null;
    }

    /** Returns null when the named digest or HMAC is unavailable. */
    static create(algorithm: const MessageDigestAlgorithm&, key: const HmacKey&)
            : Hmac! throws(CryptoException) {
        encoded: unsigned byte[]! = algorithm.toUtf8();
        validName: bool = true;
        status: int = 0;
        native: NativeHmac* = null;
        try {
            i: unsigned int = 0u;
            while (i + 1u < encoded.size) {
                if (encoded[i] == 0) {
                    validName = false;
                    break;
                }
                ++i;
            }
            if (validName) native = __k_hmac_new(&encoded[0u], key._native, &status);
        } finally {
            delete encoded;
        }
        if (!validName || status == 1) return null;
        if (status == -1) throw CryptoInvalidInputException();
        if (status != 0 || native == null) throw CryptoOperationException();
        transferred: bool = false;
        try {
            result: Hmac! = new Hmac(native, algorithm);
            transferred = true;
            return result;
        } finally {
            if (!transferred) __k_hmac_free(native);
        }
    }

    static SHA256(key: const HmacKey&) : Hmac! throws(CryptoException) {
        return create(MessageDigestAlgorithms::SHA256, key);
    }
    static SHA512(key: const HmacKey&) : Hmac! throws(CryptoException) {
        return create(MessageDigestAlgorithms::SHA512, key);
    }

    const algorithm() : MessageDigestAlgorithm { return String(_algorithm); }
    const tagSize() : unsigned int { return _size; }

    update(data: const unsigned byte[]) : Hmac& throws(CryptoException) {
        return update(data, 0u, (unsigned int) data.size);
    }
    update(data: const byte[]) : Hmac& throws(CryptoException) {
        return update(data, 0u, (unsigned int) data.size);
    }
    update(data: const unsigned byte[], off: unsigned int, len: unsigned int)
            : Hmac& throws(CryptoException) {
        _ensureActive();
        if (off > data.size || len > data.size - off) throw CryptoInvalidInputException();
        ptr: const unsigned byte* = null;
        if (len != 0u) ptr = &data[off];
        _update(ptr, len);
        return this;
    }
    update(data: const byte[], off: unsigned int, len: unsigned int)
            : Hmac& throws(CryptoException) {
        _ensureActive();
        if (off > data.size || len > data.size - off) throw CryptoInvalidInputException();
        ptr: const unsigned byte* = null;
        if (len != 0u) ptr = (const unsigned byte*) &data[off];
        _update(ptr, len);
        return this;
    }

    finalize() : unsigned byte[]! throws(CryptoException) {
        _ensureActive();
        result: unsigned byte[]! = new unsigned byte[_size];
        _active = false;
        status: int = __k_hmac_final(_native, &result[0u]);
        if (status != 0) {
            delete result;
            if (status == -4) throw CryptoStateException();
            throw CryptoOperationException();
        }
        return result;
    }

    finalizeTruncated(size: unsigned int) : unsigned byte[]! throws(CryptoException) {
        _checkTruncation(size);
        full: unsigned byte[]! = finalize();
        try {
            result: unsigned byte[]! = new unsigned byte[size];
            i: unsigned int = 0u;
            while (i < size) {
                result[i] = full[i];
                ++i;
            }
            return result;
        } finally {
            delete full;
        }
    }

    verify(expected: const unsigned byte[]) : bool throws(CryptoException) {
        actual: unsigned byte[]! = finalize();
        try {
            return constantTimeEquals(actual, expected);
        } finally {
            delete actual;
        }
    }

    verifyTruncated(expected: const unsigned byte[], size: unsigned int)
            : bool throws(CryptoException) {
        actual: unsigned byte[]! = finalizeTruncated(size);
        try {
            return constantTimeEquals(actual, expected);
        } finally {
            delete actual;
        }
    }

    /** Equal-length contents are compared without data-dependent early exit. */
    static constantTimeEquals(a: const unsigned byte[], b: const unsigned byte[]) : bool {
        if (a.size != b.size) return false;
        if (a.size == 0u) return true;
        return __k_hmac_equal(&a[0u], &b[0u], (unsigned int) a.size) != 0;
    }
}
