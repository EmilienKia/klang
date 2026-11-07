/*
 * K Language standard library -- symmetric authenticated encryption
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */
module k::crypto;

public class CryptoInvalidInputException : CryptoException {
    CryptoInvalidInputException() : CryptoException(602) {}
}
public class CryptoAuthenticationException : CryptoException {
    CryptoAuthenticationException() : CryptoException(603) {}
}
public class CryptoOperationException : CryptoException {
    CryptoOperationException() : CryptoException(604) {}
}
public class CryptoStateException : CryptoException {
    CryptoStateException() : CryptoException(605) {}
}

private struct NativeSymmetricKey {}
private struct NativeSymmetricSession {}

@ffi::Extern("C") private __k_symmetric_available(size: unsigned int) : int;
@ffi::Extern("C") private __k_symmetric_random(out: unsigned byte*, length: unsigned int) : int;
@ffi::Extern("C") private __k_symmetric_key_new(size: unsigned int, data: const unsigned byte*,
    generate: int, status: int*) : NativeSymmetricKey*;
@ffi::Extern("C") private __k_symmetric_key_free(key: NativeSymmetricKey*);
@ffi::Extern("C") private __k_symmetric_session_new(size: unsigned int, key: const NativeSymmetricKey*,
    nonce: const unsigned byte*, encrypt: int, fileStaging: int, status: int*) : NativeSymmetricSession*;
@ffi::Extern("C") private __k_symmetric_session_free(session: NativeSymmetricSession*);
@ffi::Extern("C") private __k_symmetric_close(session: NativeSymmetricSession*) : int;
@ffi::Extern("C") private __k_symmetric_nonce(session: NativeSymmetricSession*, out: unsigned byte*) : int;
@ffi::Extern("C") private __k_symmetric_aad(session: NativeSymmetricSession*,
    data: const unsigned byte*, length: unsigned int) : int;
@ffi::Extern("C") private __k_symmetric_update(session: NativeSymmetricSession*,
    data: const unsigned byte*, length: unsigned int, out: unsigned byte*) : int;
@ffi::Extern("C") private __k_symmetric_finish(session: NativeSymmetricSession*, tag: unsigned byte*) : int;
@ffi::Extern("C") private __k_symmetric_read(session: NativeSymmetricSession*,
    out: unsigned byte*, length: unsigned int, count: unsigned int*) : int;

private symmetricCheck(status: int) throws(CryptoException) {
    if (status == 0) return;
    if (status == -1) throw CryptoInvalidInputException();
    if (status == -2) throw CryptoAuthenticationException();
    if (status == -4) throw CryptoStateException();
    throw CryptoOperationException();
}

private symmetricRange(size: unsigned int, off: unsigned int, len: unsigned int)
        throws(CryptoException) {
    if (off > size || len > size - off) throw CryptoInvalidInputException();
}

public enum DecryptionStaging {
    Memory;
    TemporaryFile;
}

/**
 * An AES key whose bytes are held privately and cleared on destruction.
 * It must not be copied or used concurrently with destruction.
 */
public final class SymmetricKey : Key {
    friend SymmetricCipher;
private:
    _native: NativeSymmetricKey*;
    _size: unsigned int;

    SymmetricKey(size: unsigned int, data: const unsigned byte*, generate: int)
            : _native(null), _size(size) throws(CryptoException) {
        status: int = 0;
        _native = __k_symmetric_key_new(size, data, generate, &status);
        symmetricCheck(status);
    }
    SymmetricKey(other: const SymmetricKey&) -> delete;
    operator =(other: const SymmetricKey&) -> delete;
public:
    ~SymmetricKey() {
        __k_symmetric_key_free(_native);
        _native = null;
    }

    const keySize() : unsigned int { return _size; }
}

/**
 * An immutable AES-GCM descriptor. Sizes are in bytes.
 *
 * A nonce must never be reused with the same key. Random nonces are convenient
 * but callers remain responsible for their collision and usage limits.
 */
public final class SymmetricCipher {
private:
    _size: unsigned int;
    SymmetricCipher(size: unsigned int) : _size(size) {}
    operator =(other: const SymmetricCipher&) -> delete;
    static create(size: unsigned int) : SymmetricCipher! {
        if (__k_symmetric_available(size) == 0) return null;
        return new SymmetricCipher(size);
    }
public:
    static AES128GCM() : SymmetricCipher! { return create(16u); }
    static AES192GCM() : SymmetricCipher! { return create(24u); }
    static AES256GCM() : SymmetricCipher! { return create(32u); }

    static fromName(name: const String&) : SymmetricCipher! {
        if (name == String("AES-128-GCM")) return AES128GCM();
        if (name == String("AES-192-GCM")) return AES192GCM();
        if (name == String("AES-256-GCM")) return AES256GCM();
        return null;
    }

    const name() : String {
        if (_size == 16u) return String("AES-128-GCM");
        if (_size == 24u) return String("AES-192-GCM");
        return String("AES-256-GCM");
    }
    const keySize() : unsigned int { return _size; }
    const nonceSize() : unsigned int { return 12u; }
    const tagSize() : unsigned int { return 16u; }

    const generateKey() : SymmetricKey! throws(CryptoException) {
        return new SymmetricKey(_size, null, 1);
    }

    const importKey(data: const unsigned byte[]) : SymmetricKey! throws(CryptoException) {
        if (data.size != _size) throw CryptoInvalidInputException();
        return new SymmetricKey(_size, &data[0u], 0);
    }

    const randomNonce() : unsigned byte[]! throws(CryptoException) {
        result: unsigned byte[]! = new unsigned byte[12u];
        symmetricCheck(__k_symmetric_random(&result[0u], 12u));
        return result;
    }

    const encrypt(key: const SymmetricKey&, nonce: const unsigned byte[]) : EncryptSession!
            throws(CryptoException) {
        if (key._size != _size || nonce.size != 12u) throw CryptoInvalidInputException();
        return new EncryptSession(_size, key._native, &nonce[0u]);
    }

    const encrypt(key: const SymmetricKey&) : EncryptSession! throws(CryptoException) {
        if (key._size != _size) throw CryptoInvalidInputException();
        nonce: unsigned byte[]! = randomNonce();
        return encrypt(key, nonce);
    }

    const decrypt(key: const SymmetricKey&, nonce: const unsigned byte[],
                  staging: DecryptionStaging) : DecryptSession! throws(CryptoException) {
        if (key._size != _size || nonce.size != 12u) throw CryptoInvalidInputException();
        fileStaging: int = 0;
        if (staging == DecryptionStaging::TemporaryFile) fileStaging = 1;
        else if (staging != DecryptionStaging::Memory) throw CryptoInvalidInputException();
        return new DecryptSession(_size, key._native, &nonce[0u], fileStaging);
    }
}

/**
 * One AES-GCM encryption operation. AAD precedes every data update.
 * Sessions may not be reused, shared concurrently, or destroyed during a call.
 */
public final class EncryptSession {
    friend SymmetricCipher;
private:
    _native: NativeSymmetricSession*;
    EncryptSession(size: unsigned int, key: const NativeSymmetricKey*, nonce: const unsigned byte*)
            : _native(null) throws(CryptoException) {
        status: int = 0;
        _native = __k_symmetric_session_new(size, key, nonce, 1, 0, &status);
        symmetricCheck(status);
    }
    EncryptSession(other: const EncryptSession&) -> delete;
    operator =(other: const EncryptSession&) -> delete;

    updateAddress(data: const unsigned byte*, len: unsigned int) : unsigned byte[]!
            throws(CryptoException) {
        result: unsigned byte[]! = new unsigned byte[len];
        output: unsigned byte* = null;
        if (len != 0u) output = &result[0u];
        symmetricCheck(__k_symmetric_update(_native, data, len, output));
        return result;
    }
public:
    ~EncryptSession() {
        __k_symmetric_session_free(_native);
        _native = null;
    }

    const tagSize() : unsigned int { return 16u; }

    close() throws(CryptoException) {
        symmetricCheck(__k_symmetric_close(_native));
    }

    nonce() : unsigned byte[]! throws(CryptoException) {
        result: unsigned byte[]! = new unsigned byte[12u];
        symmetricCheck(__k_symmetric_nonce(_native, &result[0u]));
        return result;
    }

    updateAad(data: const unsigned byte[], off: unsigned int, len: unsigned int)
            throws(CryptoException) {
        symmetricRange(data.size, off, len);
        ptr: const unsigned byte* = null;
        if (len != 0u) ptr = &data[off];
        symmetricCheck(__k_symmetric_aad(_native, ptr, len));
    }
    updateAad(data: const byte[], off: unsigned int, len: unsigned int)
            throws(CryptoException) {
        symmetricRange(data.size, off, len);
        ptr: const unsigned byte* = null;
        if (len != 0u) ptr = (const unsigned byte*) &data[off];
        symmetricCheck(__k_symmetric_aad(_native, ptr, len));
    }
    updateAad(data: const unsigned byte[]) throws(CryptoException) {
        updateAad(data, 0u, data.size);
    }
    updateAad(data: const byte[]) throws(CryptoException) {
        updateAad(data, 0u, data.size);
    }

    update(data: const unsigned byte[], off: unsigned int, len: unsigned int) : unsigned byte[]!
            throws(CryptoException) {
        symmetricRange(data.size, off, len);
        ptr: const unsigned byte* = null;
        if (len != 0u) ptr = &data[off];
        return updateAddress(ptr, len);
    }
    update(data: const byte[], off: unsigned int, len: unsigned int) : unsigned byte[]!
            throws(CryptoException) {
        symmetricRange(data.size, off, len);
        ptr: const unsigned byte* = null;
        if (len != 0u) ptr = (const unsigned byte*) &data[off];
        return updateAddress(ptr, len);
    }
    update(data: const unsigned byte[]) : unsigned byte[]! throws(CryptoException) {
        return update(data, 0u, data.size);
    }
    update(data: const byte[]) : unsigned byte[]! throws(CryptoException) {
        return update(data, 0u, data.size);
    }

    updateTo(data: const byte[], off: unsigned int, len: unsigned int,
             output: byte[], outputOff: unsigned int) : unsigned int throws(CryptoException) {
        symmetricRange(data.size, off, len);
        symmetricRange(output.size, outputOff, len);
        result: unsigned byte[]! = update(data, off, len);
        i: unsigned int = 0u;
        while (i < len) {
            output[outputOff + i] = (byte) result[i];
            ++i;
        }
        return len;
    }

    finalize() : unsigned byte[]! throws(CryptoException) {
        tag: unsigned byte[]! = new unsigned byte[16u];
        symmetricCheck(__k_symmetric_finish(_native, &tag[0u]));
        return tag;
    }

    finalizeTo(output: byte[], off: unsigned int) : unsigned int throws(CryptoException) {
        symmetricRange(output.size, off, 16u);
        tag: unsigned byte[]! = finalize();
        i: unsigned int = 0u;
        while (i < 16u) {
            output[off + i] = (byte) tag[i];
            ++i;
        }
        return 16u;
    }
}

/**
 * A single authenticated decryption. No staged plaintext is readable until
 * finalize verifies the complete tag. File staging is explicit and private;
 * deleting its temporary file does not guarantee erasure from storage.
 */
public final class DecryptSession {
    friend SymmetricCipher;
private:
    _native: NativeSymmetricSession*;
    DecryptSession(size: unsigned int, key: const NativeSymmetricKey*,
                   nonce: const unsigned byte*, fileStaging: int)
            : _native(null) throws(CryptoException) {
        status: int = 0;
        _native = __k_symmetric_session_new(size, key, nonce, 0, fileStaging, &status);
        symmetricCheck(status);
    }
    DecryptSession(other: const DecryptSession&) -> delete;
    operator =(other: const DecryptSession&) -> delete;
public:
    ~DecryptSession() {
        __k_symmetric_session_free(_native);
        _native = null;
    }

    const tagSize() : unsigned int { return 16u; }

    close() throws(CryptoException) {
        symmetricCheck(__k_symmetric_close(_native));
    }

    nonce() : unsigned byte[]! throws(CryptoException) {
        result: unsigned byte[]! = new unsigned byte[12u];
        symmetricCheck(__k_symmetric_nonce(_native, &result[0u]));
        return result;
    }
    updateAad(data: const unsigned byte[], off: unsigned int, len: unsigned int)
            throws(CryptoException) {
        symmetricRange(data.size, off, len);
        ptr: const unsigned byte* = null;
        if (len != 0u) ptr = &data[off];
        symmetricCheck(__k_symmetric_aad(_native, ptr, len));
    }
    updateAad(data: const byte[], off: unsigned int, len: unsigned int)
            throws(CryptoException) {
        symmetricRange(data.size, off, len);
        ptr: const unsigned byte* = null;
        if (len != 0u) ptr = (const unsigned byte*) &data[off];
        symmetricCheck(__k_symmetric_aad(_native, ptr, len));
    }
    updateAad(data: const unsigned byte[]) throws(CryptoException) {
        updateAad(data, 0u, data.size);
    }
    updateAad(data: const byte[]) throws(CryptoException) {
        updateAad(data, 0u, data.size);
    }

    update(data: const unsigned byte[], off: unsigned int, len: unsigned int)
            throws(CryptoException) {
        symmetricRange(data.size, off, len);
        ptr: const unsigned byte* = null;
        if (len != 0u) ptr = &data[off];
        symmetricCheck(__k_symmetric_update(_native, ptr, len, null));
    }
    update(data: const byte[], off: unsigned int, len: unsigned int)
            throws(CryptoException) {
        symmetricRange(data.size, off, len);
        ptr: const unsigned byte* = null;
        if (len != 0u) ptr = (const unsigned byte*) &data[off];
        symmetricCheck(__k_symmetric_update(_native, ptr, len, null));
    }
    update(data: const unsigned byte[]) throws(CryptoException) {
        update(data, 0u, data.size);
    }
    update(data: const byte[]) throws(CryptoException) {
        update(data, 0u, data.size);
    }

    finalize(tag: const unsigned byte[]) throws(CryptoException) {
        if (tag.size != 16u) throw CryptoInvalidInputException();
        copied: unsigned byte[16];
        i: unsigned int = 0u;
        while (i < 16u) {
            copied[i] = tag[i];
            ++i;
        }
        symmetricCheck(__k_symmetric_finish(_native, &copied[0u]));
    }

    finalize(tag: const byte[], off: unsigned int, len: unsigned int) throws(CryptoException) {
        symmetricRange(tag.size, off, len);
        if (len != 16u) throw CryptoInvalidInputException();
        copied: unsigned byte[16];
        i: unsigned int = 0u;
        while (i < 16u) {
            copied[i] = (unsigned byte) tag[off + i];
            ++i;
        }
        symmetricCheck(__k_symmetric_finish(_native, &copied[0u]));
    }

    read(output: unsigned byte[], off: unsigned int, len: unsigned int) : unsigned int
            throws(CryptoException) {
        symmetricRange(output.size, off, len);
        ptr: unsigned byte* = null;
        if (len != 0u) ptr = &output[off];
        count: unsigned int = 0u;
        symmetricCheck(__k_symmetric_read(_native, ptr, len, &count));
        return count;
    }
    read(output: byte[], off: unsigned int, len: unsigned int) : unsigned int
            throws(CryptoException) {
        symmetricRange(output.size, off, len);
        ptr: unsigned byte* = null;
        if (len != 0u) ptr = (unsigned byte*) &output[off];
        count: unsigned int = 0u;
        symmetricCheck(__k_symmetric_read(_native, ptr, len, &count));
        return count;
    }
}
