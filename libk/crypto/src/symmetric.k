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

private struct NativeSymmetricCipher {}
private struct NativeSymmetricKey {}
private struct NativeSymmetricSession {}

@ffi::Extern("C") private __k_symmetric_cipher_create(algorithm: const unsigned byte*, bits: unsigned int,
    mode: const unsigned byte*, nonceSize: unsigned int, tagSize: unsigned int,
    padding: const unsigned byte*, defaults: int) : NativeSymmetricCipher*;
@ffi::Extern("C") private __k_symmetric_cipher_from_name(name: const unsigned byte*) : NativeSymmetricCipher*;
@ffi::Extern("C") private __k_symmetric_cipher_free(cipher: NativeSymmetricCipher*);
@ffi::Extern("C") private __k_symmetric_cipher_info(cipher: const NativeSymmetricCipher*, field: int) : unsigned long;
@ffi::Extern("C") private __k_symmetric_cipher_name(cipher: const NativeSymmetricCipher*, out: unsigned byte*);
@ffi::Extern("C") private __k_symmetric_cipher_field_size(cipher: const NativeSymmetricCipher*, field: int) : unsigned int;
@ffi::Extern("C") private __k_symmetric_cipher_field_copy(cipher: const NativeSymmetricCipher*, field: int, out: unsigned byte*);
@ffi::Extern("C") private __k_symmetric_random(out: unsigned byte*, length: unsigned int) : int;
@ffi::Extern("C") private __k_symmetric_key_new(cipher: const NativeSymmetricCipher*, data: const unsigned byte*,
    generate: int, status: int*) : NativeSymmetricKey*;
@ffi::Extern("C") private __k_symmetric_key_free(key: NativeSymmetricKey*);
@ffi::Extern("C") private __k_symmetric_session_new(cipher: const NativeSymmetricCipher*, key: const NativeSymmetricKey*,
    nonce: const unsigned byte*, encrypt: int, fileStaging: int, knownLength: int,
    messageLength: unsigned long, status: int*) : NativeSymmetricSession*;
@ffi::Extern("C") private __k_symmetric_session_free(session: NativeSymmetricSession*);
@ffi::Extern("C") private __k_symmetric_close(session: NativeSymmetricSession*) : int;
@ffi::Extern("C") private __k_symmetric_nonce(session: NativeSymmetricSession*, out: unsigned byte*) : int;
@ffi::Extern("C") private __k_symmetric_capacity(session: NativeSymmetricSession*,
    length: unsigned int, capacity: unsigned int*) : int;
@ffi::Extern("C") private __k_symmetric_incremental_output(session: const NativeSymmetricSession*) : int;
@ffi::Extern("C") private __k_symmetric_session_max_data(session: const NativeSymmetricSession*) : unsigned long;
@ffi::Extern("C") private __k_symmetric_aad(session: NativeSymmetricSession*,
    data: const unsigned byte*, length: unsigned int) : int;
@ffi::Extern("C") private __k_symmetric_update(session: NativeSymmetricSession*,
    data: const unsigned byte*, length: unsigned int, out: unsigned byte*,
    capacity: unsigned int, count: unsigned int*) : int;
@ffi::Extern("C") private __k_symmetric_finish(session: NativeSymmetricSession*,
    tag: const unsigned byte*, tagLength: unsigned int) : int;
@ffi::Extern("C") private __k_symmetric_tag(session: NativeSymmetricSession*, out: unsigned byte*) : int;
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

/** A case-sensitive symmetric cipher algorithm name. */
public alias SymmetricAlgorithm : String;
/** A case-sensitive symmetric cipher mode name. */
public alias SymmetricMode : String;
/** A case-sensitive symmetric cipher padding name. */
public alias SymmetricPadding : String;

public enum SymmetricAlgorithms : SymmetricAlgorithm {
    AES("AES");
    ARIA("ARIA");
    Camellia("CAMELLIA");
    SM4("SM4");
    ChaCha20("CHACHA20");
    AsconAead128("ASCON-AEAD128");
}

public enum SymmetricModes : SymmetricMode {
    GCM("GCM");
    CCM("CCM");
    OCB("OCB");
    CBC("CBC");
    CTR("CTR");
    XTS("XTS");
    Poly1305("POLY1305");
    Ascon("ASCON");
    CFB("CFB");
    OFB("OFB");
    CFB1("CFB1");
    CFB8("CFB8");
}

public enum SymmetricPaddings : SymmetricPadding {
    None("NONE");
    PKCS7("PKCS7");
}

private symmetricValidName(encoded: const unsigned byte[]) : bool {
    i: unsigned int = 0u;
    while (i + 1u < encoded.size) {
        if (encoded[i] == 0) return false;
        ++i;
    }
    return true;
}

private symmetricStaging(staging: DecryptionStaging) : int throws(CryptoException) {
    if (staging == DecryptionStaging::Memory) return 0;
    if (staging == DecryptionStaging::TemporaryFile) return 1;
    throw CryptoInvalidInputException();
}

/**
 * A symmetric key whose bytes are held privately and cleared on destruction.
 * It must not be copied or used concurrently with destruction.
 */
public final class SymmetricKey : Key {
    friend SymmetricCipher;
    friend Hkdf;
private:
    _native: NativeSymmetricKey*;
    _size: unsigned int;

    SymmetricKey(cipher: const NativeSymmetricCipher*, size: unsigned int,
                 data: const unsigned byte*, generate: int)
            : _native(null), _size(size) throws(CryptoException) {
        status: int = 0;
        _native = __k_symmetric_key_new(cipher, data, generate, &status);
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
 * An immutable symmetric cipher descriptor. Sizes are in bytes except keyBits,
 * which is the component-key size for XTS. XTS keySize is twice that size.
 *
 * A nonce must never be reused with the same key. Random nonces are convenient
 * but callers remain responsible for their collision and usage limits.
 * CBC needs an unpredictable IV; CTR counter ranges and XTS data-unit tweaks
 * must not be reused with the same key. CBC, CTR, XTS, CFB and OFB do not
 * authenticate their output.
 * Raw CHACHA20/STREAM is unauthenticated. Its 16-byte IV is an eight-byte
 * little-endian initial block counter followed by an eight-byte nonce, not
 * the IETF 96-bit-nonce format. Counter ranges must never overlap for a key.
 * Use separate keys for raw ChaCha20 and ChaCha20-Poly1305.
 * CCM uses a private streaming composition and is not provider-validated or
 * FIPS-validated merely because its underlying block cipher is.
 */
public final class SymmetricCipher {
private:
    _native: NativeSymmetricCipher*;
    SymmetricCipher(native: NativeSymmetricCipher*) : _native(native) {}
    SymmetricCipher(other: const SymmetricCipher&) -> delete;
    operator =(other: const SymmetricCipher&) -> delete;
public:
    ~SymmetricCipher() {
        __k_symmetric_cipher_free(_native);
        _native = null;
    }

    /** Returns null for unknown names, invalid parameters, or unavailable ciphers. */
    static create(algorithm: const SymmetricAlgorithm&, keyBits: unsigned int,
                  mode: const SymmetricMode&, nonceSize: unsigned int, tagSize: unsigned int,
                  padding: const SymmetricPadding&)
            : SymmetricCipher! {
        a: unsigned byte[]! = algorithm.toUtf8();
        m: unsigned byte[]! = mode.toUtf8();
        p: unsigned byte[]! = padding.toUtf8();
        try {
            if (!symmetricValidName(a) || !symmetricValidName(m) || !symmetricValidName(p))
                return null;
            native: NativeSymmetricCipher* = __k_symmetric_cipher_create(
                &a[0u], keyBits, &m[0u], nonceSize, tagSize, &p[0u], 0);
            if (native == null) return null;
            return new SymmetricCipher(native);
        } finally {
            delete p;
            delete m;
            delete a;
        }
    }

    static create(algorithm: const SymmetricAlgorithm&, keyBits: unsigned int,
                  mode: const SymmetricMode&)
            : SymmetricCipher! {
        a: unsigned byte[]! = algorithm.toUtf8();
        m: unsigned byte[]! = mode.toUtf8();
        try {
            if (!symmetricValidName(a) || !symmetricValidName(m)) return null;
            native: NativeSymmetricCipher* = __k_symmetric_cipher_create(
                &a[0u], keyBits, &m[0u], 0u, 0u, null, 1);
            if (native == null) return null;
            return new SymmetricCipher(native);
        } finally {
            delete m;
            delete a;
        }
    }

    static AES128GCM() : SymmetricCipher! {
        return create(SymmetricAlgorithms::AES, 128u, SymmetricModes::GCM);
    }
    static AES192GCM() : SymmetricCipher! {
        return create(SymmetricAlgorithms::AES, 192u, SymmetricModes::GCM);
    }
    static AES256GCM() : SymmetricCipher! {
        return create(SymmetricAlgorithms::AES, 256u, SymmetricModes::GCM);
    }

    static fromName(name: const String&) : SymmetricCipher! {
        encoded: unsigned byte[]! = name.toUtf8();
        i: unsigned int = 0u;
        while (i + 1u < encoded.size) {
            if (encoded[i] == 0) return null;
            ++i;
        }
        native: NativeSymmetricCipher* = __k_symmetric_cipher_from_name(&encoded[0u]);
        if (native == null) return null;
        return new SymmetricCipher(native);
    }

    const name() : String {
        size: unsigned int = (unsigned int) __k_symmetric_cipher_info(_native, 11);
        bytes: unsigned byte[]! = new unsigned byte[size + 1u];
        __k_symmetric_cipher_name(_native, &bytes[0u]);
        return String(bytes);
    }
    const keySize() : unsigned int { return (unsigned int) __k_symmetric_cipher_info(_native, 0); }
    const nonceSize() : unsigned int { return (unsigned int) __k_symmetric_cipher_info(_native, 1); }
    const tagSize() : unsigned int { return (unsigned int) __k_symmetric_cipher_info(_native, 2); }
    const authenticated() : bool { return __k_symmetric_cipher_info(_native, 3) != 0uL; }
    const maxDataSize() : unsigned long { return __k_symmetric_cipher_info(_native, 4); }
    const maxAadSize() : unsigned long { return __k_symmetric_cipher_info(_native, 5); }
    private const descriptorField(field: int) : String {
        size: unsigned int = __k_symmetric_cipher_field_size(_native, field);
        bytes: unsigned byte[]! = new unsigned byte[size + 1u];
        __k_symmetric_cipher_field_copy(_native, field, &bytes[0u]);
        return String(bytes);
    }
    const algorithm() : SymmetricAlgorithm { return descriptorField(0); }
    const mode() : SymmetricMode { return descriptorField(1); }
    const keyBits() : unsigned int { return (unsigned int) __k_symmetric_cipher_info(_native, 8); }
    const padding() : SymmetricPadding { return descriptorField(2); }

    const generateKey() : SymmetricKey! throws(CryptoException) {
        return new SymmetricKey(_native, keySize(), null, 1);
    }

    const importKey(data: const unsigned byte[]) : SymmetricKey! throws(CryptoException) {
        if (data.size != keySize()) throw CryptoInvalidInputException();
        return new SymmetricKey(_native, keySize(), &data[0u], 0);
    }

    const randomNonce() : unsigned byte[]! throws(CryptoException) {
        result: unsigned byte[]! = new unsigned byte[nonceSize()];
        symmetricCheck(__k_symmetric_random(&result[0u], nonceSize()));
        return result;
    }

    const encrypt(key: const SymmetricKey&, nonce: const unsigned byte[]) : EncryptSession!
            throws(CryptoException) {
        return encrypt(key, nonce, DecryptionStaging::Memory);
    }

    const encrypt(key: const SymmetricKey&) : EncryptSession! throws(CryptoException) {
        nonce: unsigned byte[]! = randomNonce();
        return encrypt(key, nonce);
    }

    const encrypt(key: const SymmetricKey&, nonce: const unsigned byte[],
                  staging: DecryptionStaging) : EncryptSession! throws(CryptoException) {
        if (key._size != keySize() || nonce.size != nonceSize()) throw CryptoInvalidInputException();
        return new EncryptSession(_native, key._native, &nonce[0u], symmetricStaging(staging),
                                  0, 0uL, nonceSize(), tagSize(), authenticated());
    }

    /** Declares the total plaintext length, enabling incremental CCM output. */
    const encrypt(key: const SymmetricKey&, nonce: const unsigned byte[],
                  staging: DecryptionStaging, messageLength: unsigned long)
            : EncryptSession! throws(CryptoException) {
        if (key._size != keySize() || nonce.size != nonceSize()) throw CryptoInvalidInputException();
        return new EncryptSession(_native, key._native, &nonce[0u], symmetricStaging(staging),
                                  1, messageLength, nonceSize(), tagSize(), authenticated());
    }

    const decrypt(key: const SymmetricKey&, nonce: const unsigned byte[],
                  staging: DecryptionStaging) : DecryptSession! throws(CryptoException) {
        if (key._size != keySize() || nonce.size != nonceSize()) throw CryptoInvalidInputException();
        return new DecryptSession(_native, key._native, &nonce[0u], symmetricStaging(staging),
                                  0, 0uL, nonceSize(), tagSize(), authenticated());
    }

    /** Declares the total ciphertext length before decryption. */
    const decrypt(key: const SymmetricKey&, nonce: const unsigned byte[], staging: DecryptionStaging,
                  messageLength: unsigned long) : DecryptSession! throws(CryptoException) {
        if (key._size != keySize() || nonce.size != nonceSize()) throw CryptoInvalidInputException();
        return new DecryptSession(_native, key._native, &nonce[0u], symmetricStaging(staging),
                                  1, messageLength, nonceSize(), tagSize(), authenticated());
    }
}

/**
 * One encryption operation. AAD precedes every data update.
 * Sessions may not be reused, shared concurrently, or destroyed during a call.
 * finish() retains any final ciphertext for readFinal(); the separate tag
 * follows that ciphertext. Unknown-length CCM stages the message until finish.
 * XTS stages one bounded data unit if the selected provider cannot stream it
 * correctly; updatesProduceOutput() exposes that choice.
 */
public final class EncryptSession {
    friend SymmetricCipher;
private:
    _native: NativeSymmetricSession*;
    _nonceSize: unsigned int;
    _tagSize: unsigned int;
    _authenticated: bool;
    EncryptSession(cipher: const NativeSymmetricCipher*, key: const NativeSymmetricKey*,
                   nonce: const unsigned byte*, fileStaging: int, knownLength: int,
                   messageLength: unsigned long, nonceSize: unsigned int, tagSize: unsigned int,
                   authenticated: bool)
            : _native(null), _nonceSize(nonceSize), _tagSize(tagSize), _authenticated(authenticated)
            throws(CryptoException) {
        status: int = 0;
        _native = __k_symmetric_session_new(cipher, key, nonce, 1, fileStaging,
                                             knownLength, messageLength, &status);
        symmetricCheck(status);
    }
    EncryptSession(other: const EncryptSession&) -> delete;
    operator =(other: const EncryptSession&) -> delete;

    updateAddress(data: const unsigned byte*, len: unsigned int) : unsigned byte[]!
            throws(CryptoException) {
        capacity: unsigned int = updateOutputCapacity(len);
        result: unsigned byte[]! = new unsigned byte[capacity];
        output: unsigned byte* = null;
        if (capacity != 0u) output = &result[0u];
        count: unsigned int = 0u;
        symmetricCheck(__k_symmetric_update(_native, data, len, output, capacity, &count));
        if (count == capacity) return result;
        exact: unsigned byte[]! = new unsigned byte[count];
        i: unsigned int = 0u;
        while (i < count) {
            exact[i] = result[i];
            ++i;
        }
        return exact;
    }
public:
    ~EncryptSession() {
        __k_symmetric_session_free(_native);
        _native = null;
    }

    const tagSize() : unsigned int { return _tagSize; }
    const authenticated() : bool { return _authenticated; }
    const updatesProduceOutput() : bool { return __k_symmetric_incremental_output(_native) != 0; }
    const maxDataSize() : unsigned long { return __k_symmetric_session_max_data(_native); }
    /** Safe output bound including previously buffered input; may exceed len. */
    const updateOutputCapacity(len: unsigned int) : unsigned int throws(CryptoException) {
        capacity: unsigned int = 0u;
        symmetricCheck(__k_symmetric_capacity(_native, len, &capacity));
        return capacity;
    }

    close() throws(CryptoException) {
        symmetricCheck(__k_symmetric_close(_native));
    }

    nonce() : unsigned byte[]! throws(CryptoException) {
        result: unsigned byte[]! = new unsigned byte[_nonceSize];
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
        if (len != 0u) ptr = (const unsigned byte*) &data[off];
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
        capacity: unsigned int = updateOutputCapacity(len);
        symmetricRange(output.size, outputOff, capacity);
        result: unsigned byte[]! = update(data, off, len);
        i: unsigned int = 0u;
        while (i < result.size) {
            output[outputOff + i] = (byte) result[i];
            ++i;
        }
        return result.size;
    }

    updateTo(data: const unsigned byte[], off: unsigned int, len: unsigned int,
             output: unsigned byte[], outputOff: unsigned int) : unsigned int throws(CryptoException) {
        symmetricRange(data.size, off, len);
        capacity: unsigned int = updateOutputCapacity(len);
        symmetricRange(output.size, outputOff, capacity);
        result: unsigned byte[]! = update(data, off, len);
        i: unsigned int = 0u;
        while (i < result.size) {
            output[outputOff + i] = result[i];
            ++i;
        }
        return result.size;
    }

    finish() throws(CryptoException) {
        symmetricCheck(__k_symmetric_finish(_native, null, 0u));
    }

    tag() : unsigned byte[]! throws(CryptoException) {
        result: unsigned byte[]! = new unsigned byte[_tagSize];
        ptr: unsigned byte* = null;
        if (_tagSize != 0u) ptr = &result[0u];
        symmetricCheck(__k_symmetric_tag(_native, ptr));
        return result;
    }

    readFinal(output: unsigned byte[], off: unsigned int, len: unsigned int) : unsigned int
            throws(CryptoException) {
        symmetricRange(output.size, off, len);
        ptr: unsigned byte* = null;
        if (len != 0u) ptr = &output[off];
        count: unsigned int = 0u;
        symmetricCheck(__k_symmetric_read(_native, ptr, len, &count));
        return count;
    }
    readFinal(output: byte[], off: unsigned int, len: unsigned int) : unsigned int
            throws(CryptoException) {
        symmetricRange(output.size, off, len);
        ptr: unsigned byte* = null;
        if (len != 0u) ptr = (unsigned byte*) &output[off];
        count: unsigned int = 0u;
        symmetricCheck(__k_symmetric_read(_native, ptr, len, &count));
        return count;
    }

    finalize() : unsigned byte[]! throws(CryptoException) {
        finish();
        return tag();
    }

    finalizeTo(output: byte[], off: unsigned int) : unsigned int throws(CryptoException) {
        symmetricRange(output.size, off, _tagSize);
        result: unsigned byte[]! = finalize();
        i: unsigned int = 0u;
        while (i < _tagSize) {
            output[off + i] = (byte) result[i];
            ++i;
        }
        return _tagSize;
    }
}

/**
 * A single decryption. Authenticated plaintext is withheld until verification.
 * Plaintext from unauthenticated modes is not verified, even if it is readable.
 * A pre-final read returning zero means no output is currently available.
 * CBC may report invalid padding after earlier unauthenticated reads.
 * File staging is explicit and does not guarantee physical erasure.
 */
public final class DecryptSession {
    friend SymmetricCipher;
private:
    _native: NativeSymmetricSession*;
    _nonceSize: unsigned int;
    _tagSize: unsigned int;
    _authenticated: bool;
    DecryptSession(cipher: const NativeSymmetricCipher*, key: const NativeSymmetricKey*,
                   nonce: const unsigned byte*, fileStaging: int, knownLength: int,
                   messageLength: unsigned long, nonceSize: unsigned int, tagSize: unsigned int,
                   authenticated: bool)
            : _native(null), _nonceSize(nonceSize), _tagSize(tagSize), _authenticated(authenticated)
            throws(CryptoException) {
        status: int = 0;
        _native = __k_symmetric_session_new(cipher, key, nonce, 0, fileStaging,
                                             knownLength, messageLength, &status);
        symmetricCheck(status);
    }
    DecryptSession(other: const DecryptSession&) -> delete;
    operator =(other: const DecryptSession&) -> delete;
public:
    ~DecryptSession() {
        __k_symmetric_session_free(_native);
        _native = null;
    }

    const tagSize() : unsigned int { return _tagSize; }
    const authenticated() : bool { return _authenticated; }
    const readableBeforeFinalize() : bool { return !_authenticated; }
    const updatesProduceOutput() : bool { return __k_symmetric_incremental_output(_native) != 0; }
    const maxDataSize() : unsigned long { return __k_symmetric_session_max_data(_native); }

    close() throws(CryptoException) {
        symmetricCheck(__k_symmetric_close(_native));
    }

    nonce() : unsigned byte[]! throws(CryptoException) {
        result: unsigned byte[]! = new unsigned byte[_nonceSize];
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
        count: unsigned int = 0u;
        symmetricCheck(__k_symmetric_update(_native, ptr, len, null, 0u, &count));
    }
    update(data: const byte[], off: unsigned int, len: unsigned int)
            throws(CryptoException) {
        symmetricRange(data.size, off, len);
        ptr: const unsigned byte* = null;
        if (len != 0u) ptr = (const unsigned byte*) &data[off];
        count: unsigned int = 0u;
        symmetricCheck(__k_symmetric_update(_native, ptr, len, null, 0u, &count));
    }
    update(data: const unsigned byte[]) throws(CryptoException) {
        update(data, 0u, data.size);
    }
    update(data: const byte[]) throws(CryptoException) {
        update(data, 0u, data.size);
    }

    finalize(tag: const unsigned byte[]) throws(CryptoException) {
        if (tag.size != _tagSize) throw CryptoInvalidInputException();
        ptr: const unsigned byte* = null;
        if (_tagSize != 0u) ptr = &tag[0u];
        symmetricCheck(__k_symmetric_finish(_native, ptr, _tagSize));
    }

    finalize(tag: const byte[], off: unsigned int, len: unsigned int) throws(CryptoException) {
        symmetricRange(tag.size, off, len);
        if (len != _tagSize) throw CryptoInvalidInputException();
        ptr: const unsigned byte* = null;
        if (len != 0u) ptr = (const unsigned byte*) &tag[off];
        symmetricCheck(__k_symmetric_finish(_native, ptr, len));
    }

    finalize() throws(CryptoException) {
        if (_tagSize != 0u) throw CryptoInvalidInputException();
        symmetricCheck(__k_symmetric_finish(_native, null, 0u));
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
