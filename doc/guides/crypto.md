# Cryptography with libk

This guide covers the current `k::crypto` and `k::io::crypto` APIs: message
digests, HMAC, capability discovery, symmetric keys and ciphers, encryption
sessions, and stream adapters. See the [crypto reference](../spec/stdlib/crypto.md)
for the complete API and parameter rules.

The examples are module-level functions. Place them in a K module with this
import; `k` itself is imported automatically:

```k
module crypto_guide;
import k::io::crypto;
```

The I/O crypto module brings in its `k::crypto` dependency, so both namespaces
are available. A module that does not use crypto stream adapters can instead
import `k::crypto` directly. Do not import both modules in the same source:
their shared root causes an import collision.

`libk-crypto` is built separately from the base library. Make the crypto KDI
descriptors discoverable to the compiler, and link `libk.crypto` and
`libk.io.crypto` when using their modules. The build produces a
`libk.crypto.pc` package describing those libraries; static consumers also
need the dependencies reported by `pkg-config --static`. Make the shared
libraries available to the runtime loader when using dynamic linking. See the
[CMake integration guide](../howtos/cmake-integration-k-projects.md) for general
KDI and library search-path setup.

## 1. Choosing a cryptographic operation

- Use a **digest** such as SHA-256 to calculate a checksum. A digest alone does
  not authenticate data: an attacker who can replace the data can also replace
  its digest. It is not a password-hashing scheme.
- Use **HMAC** to authenticate data shared between parties that hold a secret
  key. HMAC does not encrypt data or provide replay protection.
- Prefer an **authenticated encryption** mode such as AES-GCM or
  ChaCha20-Poly1305 when confidentiality and integrity are both needed.
  Supply relevant unencrypted metadata as additional authenticated data (AAD).
- CBC, CTR, CFB, OFB, XTS, and raw ChaCha20/STREAM do **not** authenticate
  their output. Do not treat successful decryption or valid padding as proof
  that a message is authentic.

Availability depends on the active provider configuration. Factories return
`null` for unavailable or invalid selections. Check the result; do not silently
fall back to a weaker algorithm. Discovery and use can observe different
provider configurations if they change concurrently.

## 2. Message digests

Create one `MessageDigest` per message, update it with the bytes in order, and
finalize it once. The result is an owned array of raw digest bytes, not a
hexadecimal string. The slice overload accepts `byte[]` or `unsigned byte[]`
and avoids copying the selected input range.

```k
sha256(data: const unsigned byte[]) : unsigned byte[]!
        throws(k::crypto::MessageDigestException) {
    digest: k::crypto::MessageDigest! = k::crypto::MessageDigest::SHA256();
    if (digest == null) return null;

    digest.update(data, 0u, data.size);
    return digest.finalize();
}
```

The caller must handle a null result if SHA-256 is unavailable. For multiple
chunks, call `update` for each one before `finalize`; the result is the digest
of their concatenation. `MessageDigest::fromName` accepts a
`MessageDigestAlgorithm`, which is a `String` alias. Standard names are
available in `MessageDigestAlgorithms`, and additional provider names can be
used directly:

```k
namedDigest() : k::crypto::MessageDigest! {
    return k::crypto::MessageDigest::fromName(
        k::crypto::MessageDigestAlgorithms::SHA256);
}
```

Named factories and enum entries include `SHA224`, `SHA256`, `SHA384`,
`SHA512`, `SHA512_224`, `SHA512_256`, `SHA3_224`, `SHA3_256`, `SHA3_384`,
and `SHA3_512`, as well as the compatibility algorithms `MD5` and `SHA1`.
Check the [reference](../spec/stdlib/crypto.md#messagedigest) for canonical names.
For new designs, choose a collision-resistant digest such as SHA-256 or SHA-3.
MD5 and SHA-1 are present for compatibility, not collision-resistant security.
Do not use a fast digest directly for passwords; use a suitable password
hashing scheme supplied by another library or service.

## 3. Digesting an output stream

`MessageDigestOutputStream` forwards bytes to a borrowed
`OutputStream<byte>` and hashes exactly the bytes written through the
decorator. Closing it closes the wrapped stream and finalizes the digest. Its
`getDigest()` result is a read-only view owned by the decorator, not a
separately owned array.

```k
digestWrite(sink: k::io::OutputStream<byte>&, data: const byte[]) : bool
        throws(Throwable) {
    stream: k::io::crypto::MessageDigestOutputStream! =
        k::io::crypto::MessageDigestOutputStream::SHA256(sink);
    if (stream == null) return false;

    stream.write(data, 0u, data.size);
    stream.close();
    digest: const unsigned byte[]? = stream.getDigest();
    return digest != null;
}
```

Here `false` means that the digest implementation was unavailable or no digest
was produced. The caller must handle that result. Read or copy the digest
while the decorator is alive; it is null before successful finalization. Treat
an exception from write or close as failure, even if a digest view exists.
Place the decorator at the point in a pipeline whose bytes you want to hash:
before encoding to hash the original bytes, or after encoding to hash the
encoded bytes.

## HMAC: authenticating messages

HMAC combines a secret key and a digest. Obtain a sufficiently strong random
key through an appropriate key-management system; do not use a password
directly. `HmacKey::fromBytes` copies the key, and its internal copy is cleared
on destruction. Protect and eventually clear any caller-owned copies too.
This API does not generate keys.

Create a separate HMAC for each message. The receiver must use the same key,
digest, and message bytes, and must verify the tag before trusting the message:

```k
verifyHmacSha256(keyBytes: const unsigned byte[], message: const unsigned byte[],
                 tag: const unsigned byte[]) : bool throws(k::crypto::CryptoException) {
    key: k::crypto::HmacKey! = k::crypto::HmacKey::fromBytes(keyBytes);
    mac: k::crypto::Hmac! = k::crypto::Hmac::SHA256(key);
    if (mac == null) return false;
    mac.update(message);
    return mac.verify(tag);
}
```

For a sender, call `finalize()` instead of `verify()` to obtain an owned full
tag. For several chunks, update in order before finalizing. Use `create` with a
`MessageDigestAlgorithm` to select another available digest. Verification uses
constant-time comparison for equal-length tags; do not replace it with an
ordinary byte-by-byte comparison. A mismatch returns false, whereas operation
failures throw exceptions.

Full tags are recommended. If a protocol requires truncation, use
`finalizeTruncated(size)` and `verifyTruncated(tag, size)` with the size fixed
by trusted protocol configuration. Do not select the size from the received
tag; use at least 128 bits for new protocols when truncation is necessary.

`HmacOutputStream` computes a tag over bytes written to a borrowed sink and
exposes it after a successful close. `HmacInputStream` computes it over bytes
read or skipped and completes only at EOF. Both consume an `Hmac` owner and
provide `getTag()`, `isComplete()`, and constant-time `verify` methods. They
neither transmit tags nor delimit messages. Input bytes are available before
verification: stage untrusted data and do not use it until verification
succeeds. The adapter owns the tag view, which must not outlive it. Any bytes
already supplied to the HMAC form a prefix; pass a fresh HMAC to authenticate
only the stream bytes.

For example, this function verifies and discards a single input message:

```k
verifyHmacInput(source: k::io::InputStream<byte>&,
                keyBytes: const unsigned byte[], tag: const unsigned byte[]) : bool
        throws(Throwable) {
    key: k::crypto::HmacKey! = k::crypto::HmacKey::fromBytes(keyBytes);
    mac: k::crypto::Hmac! = k::crypto::Hmac::SHA256(key);
    if (mac == null) return false;
    stream: k::io::crypto::HmacInputStream! =
        new k::io::crypto::HmacInputStream(source, mac);
    buffer: byte[4096];
    while (true) {
        result: Expected<unsigned int, k::io::StreamOutOfData> =
            stream.read(buffer, 0u, 4096u);
        if (result.hasError()) {
            stream.close();
            return false;
        }
        if (result.getResult() == 0u) break;
    }
    verified: bool = stream.verify(tag);
    stream.close();
    return verified;
}
```

In an application that needs the message, stage the bytes instead of discarding
them, and release them only after verification. Arrange cleanup if an
exception interrupts the example before its close call.

## 4. Discovering supported algorithms

`CryptoCapabilities::snapshot()` produces an eager snapshot. The four global
sequences are `digestAlgorithms()`, `hmacDigestAlgorithms()`,
`symmetricAlgorithms()`, `symmetricModes()`, and `symmetricPaddings()`.
Their values are string aliases:
they can include supported extensions absent from the standard enums, and
standard enum entries need not be available on every installation.

The global symmetric lists are **unions**, not a list of compatible
combinations. Use `symmetricModes(algorithm, keyBits)` and
`symmetricPaddings(algorithm, keyBits, mode)` for contextual projections.
`symmetricConfigurations()` supplies representative usable tuples with
`algorithm()`, `keyBits()`, `mode()`, `padding()`, `nonceSize()`, and
`tagSize()` accessors. The tuples do not enumerate every possible parameter
length. For XTS, `keyBits()` is the size of one component key.

This example finds a witnessed AES-256-GCM configuration and constructs it:

```k
findAes256Gcm() : k::crypto::SymmetricCipher! throws(k::crypto::CryptoException) {
    caps: k::crypto::CryptoCapabilities! = k::crypto::CryptoCapabilities::snapshot();
    configs: Sequence<k::crypto::SymmetricCipherConfiguration>! =
        caps.symmetricConfigurations();
    it: ConstIterator<k::crypto::SymmetricCipherConfiguration>! = configs.constIterator();
    item: OptionalConstRef<k::crypto::SymmetricCipherConfiguration> = it.next();
    while (item.hasValue()) {
        config: const k::crypto::SymmetricCipherConfiguration& = item.get();
        if (config.algorithm() == k::crypto::SymmetricAlgorithms::AES &&
            config.keyBits() == 256u && config.mode() == k::crypto::SymmetricModes::GCM) {
            return k::crypto::SymmetricCipher::create(
                config.algorithm(), config.keyBits(), config.mode(),
                config.nonceSize(), config.tagSize(), config.padding());
        }
        item = it.next();
    }
    return null;
}
```

The snapshot and each returned sequence own their data; an iterator and its
element references borrow the sequence and must not outlive it. An empty
filtered sequence is a valid result. A snapshot is stable but is not a promise
that a later factory call will succeed after provider settings change. Digest
and cipher candidates come from the provider; padding choices are libk
policies validated with ciphers. HMAC digest names are separately probed with
a fixed test key; a provider may impose different restrictions on other keys.

## 5. Selecting a symmetric cipher

Use the convenience factories, a recognized canonical name, or specify an
algorithm family, component key size in bits, and mode. The full `create`
overload also accepts a nonce/IV size, tag size, and padding:

```k
selectCiphers() : bool {
    gcm: k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::AES256GCM();
    cbc: k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::create(
        k::crypto::SymmetricAlgorithms::AES, 128u, k::crypto::SymmetricModes::CBC,
        16u, 0u, k::crypto::SymmetricPaddings::PKCS7);
    return gcm != null && cbc != null;
}
```

This checks availability only; it does not recommend CBC for a new protocol.
`fromName(String("AES-256-GCM"))` selects a recognized canonical cipher name
with default parameters; unlike digest lookup, it does not accept arbitrary
provider aliases. Names are case-sensitive.

The three-argument `create` defaults CBC to PKCS7 and other modes to no
padding. GCM, CCM, OCB, and Poly1305 default to a 12-byte nonce and 16-byte
tag; Ascon uses 16 bytes for each. The other modes use a 16-byte IV and no
tag. `PKCS7` is available only for CBC; unpadded CBC input must be
block-aligned. Check `name()`, `algorithm()`, `mode()`, `padding()`,
`authenticated()`, `keySize()`, `nonceSize()`, `tagSize()`, `maxDataSize()`,
and `maxAadSize()` on the selected descriptor.

| Family or mode | Practical guidance |
|---|---|
| AES, ARIA, Camellia, SM4 | A family, size, and mode are selected independently, but only supported combinations succeed. |
| GCM, CCM, OCB | Authenticated modes where supported. Never reuse a nonce under the same key. |
| ChaCha20 / Poly1305 | Authenticated ChaCha20-Poly1305 with a 256-bit key, 12-byte nonce, and 16-byte tag. |
| Ascon-AEAD128 / Ascon | Authenticated 128-bit Ascon when a compatible provider is available. |
| CBC, CTR, CFB variants, OFB | Unauthenticated modes. CBC and CFB need unpredictable IVs; CTR counter ranges and OFB IVs must not repeat under a key. |
| XTS | Unauthenticated storage encryption. Each session processes one data unit of 16 bytes to 16 MiB and needs its own tweak. |
| ChaCha20 / STREAM | Provider-dependent raw ChaCha20 extension. It is unauthenticated and is not ChaCha20-Poly1305. |

Use independent keys for different algorithms, modes, and purposes. For XTS,
`keyBits` denotes the size of **each** component: the imported or generated
key has twice that many bytes as one component, and its two components must
differ. Rewriting a data unit with the same XTS tweak does not provide fresh
authenticated encryption.

Raw ChaCha20/STREAM uses a 16-byte IV: an eight-byte little-endian initial
block counter followed by an eight-byte nonce. This is not the 96-bit-nonce
format of ChaCha20-Poly1305. Never reuse or overlap counter ranges for the
same key, use a separate key from ChaCha20-Poly1305, and supply independent
authentication when integrity is needed. A session's `maxDataSize()` accounts
for its initial counter and the API's representable size limit.

## 6. Creating and handling symmetric keys

A `SymmetricKey` holds private key material and is generated or imported
through a cipher descriptor. Generation uses the backend's secure random
generator. Import requires exactly `cipher.keySize()` bytes:

```k
makeKey(cipher: const k::crypto::SymmetricCipher&)
        : k::crypto::SymmetricKey! throws(k::crypto::CryptoException) {
    return cipher.generateKey();
}

loadKey(cipher: const k::crypto::SymmetricCipher&, bytes: const unsigned byte[])
        : k::crypto::SymmetricKey! throws(k::crypto::CryptoException) {
    return cipher.importKey(bytes);
}
```

Use a key-management system or other protected storage for long-lived keys;
do not hard-code them in a program or treat a password as a cipher key. The
current API does not provide a password-based key derivation facility. Keep
import buffers and key owners alive only as long as needed, avoid logging
them, and arrange secure storage and rotation separately. The key object
clears its internal bytes on destruction; that does not erase every copy an
application, allocator, or storage system may have made.

## 7. Nonces, AAD, and authenticated sessions

`cipher.encrypt(key)` creates a session with a generated nonce;
`cipher.encrypt(key, nonce)` uses the supplied nonce. Obtain `nonce()` before
transferring the session to a stream and store or transmit it with the message.
Nonce generation is random, not a uniqueness guarantee. Account for collision
risk and per-key usage limits, or manage unique nonces explicitly. Never
repeat an AEAD nonce under the same key.

AAD authenticates metadata without encrypting it. Both parties must supply
the exact same bytes in the same order **before** any data update. Include
metadata such as a protocol version, message type, or record sequence number
when the protocol needs to bind it to the ciphertext. Design replay protection
separately; a valid authentication tag alone does not prevent replay.

This in-memory example passes the encrypted chunks directly to the decryptor
to demonstrate ordering. In a real protocol, send the nonce and the cipher
parameters separately, followed by all ciphertext chunks and the tag. A
receiver must use the same key, nonce, parameters, and AAD.

```k
gcmRoundTrip(plain: const unsigned byte[], aad: const unsigned byte[]) : bool
        throws(k::crypto::CryptoException) {
    cipher: k::crypto::SymmetricCipher! = k::crypto::SymmetricCipher::AES256GCM();
    if (cipher == null) return false;
    key: k::crypto::SymmetricKey! = cipher.generateKey();
    encrypt: k::crypto::EncryptSession! = cipher.encrypt(key);
    nonce: unsigned byte[]! = encrypt.nonce();
    decrypt: k::crypto::DecryptSession! = cipher.decrypt(
        key, nonce, k::crypto::DecryptionStaging::Memory);

    encrypt.updateAad(aad);
    decrypt.updateAad(aad);
    chunk: unsigned byte[]! = encrypt.update(plain);
    decrypt.update(chunk);
    encrypt.finish();
    finalBytes: unsigned byte[4096];
    count: unsigned int = encrypt.readFinal(finalBytes, 0u, 4096u);
    while (count != 0u) {
        decrypt.update(finalBytes, 0u, count);
        count = encrypt.readFinal(finalBytes, 0u, 4096u);
    }
    tag: unsigned byte[]! = encrypt.tag();
    decrypt.finalize(tag);

    recovered: unsigned byte[]! = new unsigned byte[plain.size];
    used: unsigned int = 0u;
    while (used < recovered.size) {
        count = decrypt.read(recovered, used, recovered.size - used);
        if (count == 0u) return false;
        used += count;
    }
    i: unsigned int = 0u;
    while (i < plain.size) {
        if (recovered[i] != plain[i]) return false;
        ++i;
    }
    return true;
}
```

Handle a `false` result as failure, including when AES-256-GCM is unavailable.
Authentication and other operation failures propagate as exceptions. The
decryptor withholds AEAD plaintext until `finalize(tag)` succeeds. Never
release or act on a failed message. Use a fresh session per message; sessions
are not concurrent shared objects.

Encryption updates may return fewer bytes than supplied, or none. Always call
`finish()`, drain `readFinal()` to zero, then obtain `tag()`. The compatibility
`finalize()` method finishes and returns the tag but does **not** return final
ciphertext, so generalized code must still drain `readFinal()`. If using
`updateTo`, allocate at least `updateOutputCapacity(inputLength)` bytes at the
output offset and use the returned count rather than the input length.

For tagless modes call `decrypt.finalize()` without a tag. Their
`readableBeforeFinalize()` result can be true and plaintext may be released
before finalization. That plaintext has no integrity guarantee; CBC can still
fail padding after earlier bytes were read.

### CCM and staging

CCM needs the message length to produce incremental encryption output. The
four-argument session overload declares the total plaintext length for
encryption or ciphertext length for decryption, excluding the tag:

```k
startCcm(cipher: const k::crypto::SymmetricCipher&,
         key: const k::crypto::SymmetricKey&, nonce: const unsigned byte[],
         messageLength: unsigned long) : k::crypto::EncryptSession!
        throws(k::crypto::CryptoException) {
    return cipher.encrypt(key, nonce, k::crypto::DecryptionStaging::Memory,
                          messageLength);
}
```

Use this with a CCM descriptor, supply AAD before updates, and provide exactly
the declared number of bytes. CCM accepts nonce lengths of 7–13 bytes and even
tag lengths of 4–16 bytes; the nonce length also limits the message size.
Without a declared length CCM stages the message until finalization.
`DecryptionStaging::Memory` keeps staged data in memory;
`TemporaryFile` explicitly permits a private temporary file, potentially
including plaintext on disk without guaranteed physical erasure. AEAD
decryption still withholds plaintext until verification even when a length is
declared. CCM's streaming composition uses backend block-cipher primitives and
is not automatically FIPS-validated.

## 8. Encrypting and decrypting streams

The four adapters each process **one message**. Their constructors borrow the
underlying stream and consume the session owner, so configure AAD and retain
the nonce before construction. Closing an adapter also closes its wrapped
stream. Encryption emits ciphertext followed by any authentication tag;
the nonce and cipher parameters are not included. Decryption treats the last
`tagSize()` bytes as the tag for authenticated modes.

### `EncryptCipherOutputStream`

This function processes one caller-supplied chunk. In an application, write
all chunks in order before closing. It assumes an authenticated descriptor,
and the receiver must obtain the nonce and AAD separately.

```k
encryptOutput(sink: k::io::OutputStream<byte>&,
              cipher: const k::crypto::SymmetricCipher&,
              key: const k::crypto::SymmetricKey&, nonce: const unsigned byte[],
              aad: const unsigned byte[], plain: const byte[]) : bool throws(Throwable) {
    session: k::crypto::EncryptSession! = cipher.encrypt(key, nonce);
    session.updateAad(aad);
    stream: k::io::crypto::EncryptCipherOutputStream! =
        new k::io::crypto::EncryptCipherOutputStream(sink, session);
    stream.write(plain, 0u, plain.size);
    stream.close();
    return stream.isComplete();
}
```

### `DecryptCipherOutputStream`

```k
decryptOutput(sink: k::io::OutputStream<byte>&,
              cipher: const k::crypto::SymmetricCipher&,
              key: const k::crypto::SymmetricKey&, nonce: const unsigned byte[],
              aad: const unsigned byte[], message: const byte[]) : bool throws(Throwable) {
    session: k::crypto::DecryptSession! = cipher.decrypt(
        key, nonce, k::crypto::DecryptionStaging::Memory);
    session.updateAad(aad);
    stream: k::io::crypto::DecryptCipherOutputStream! =
        new k::io::crypto::DecryptCipherOutputStream(sink, session);
    stream.write(message, 0u, message.size);
    stream.close();
    return stream.isComplete() && stream.isVerified();
}
```

Successful encryption close writes final ciphertext and the tag. For AEAD,
decryption verifies on close before writing staged plaintext to the sink.
Propagate a failure from write or close, and treat `false` as incomplete or
unverified. A sink failure can occur after some verified plaintext was
written; it is not an atomic output transaction. Arrange cleanup if an
exception interrupts the example before its close call.

### `EncryptCipherInputStream`

These input examples copy one message from a wrapped source to a sink. They return
`false` on a stream read error or incomplete/unverified message, and propagate
exceptions. A zero successful read marks EOF; an error is not EOF.

```k
encryptInput(source: k::io::InputStream<byte>&, sink: k::io::OutputStream<byte>&,
             cipher: const k::crypto::SymmetricCipher&,
             key: const k::crypto::SymmetricKey&, nonce: const unsigned byte[],
             aad: const unsigned byte[]) : bool throws(Throwable) {
    session: k::crypto::EncryptSession! = cipher.encrypt(key, nonce);
    session.updateAad(aad);
    stream: k::io::crypto::EncryptCipherInputStream! =
        new k::io::crypto::EncryptCipherInputStream(source, session);
    buffer: byte[4096];
    while (true) {
        result: Expected<unsigned int, k::io::StreamOutOfData> =
            stream.read(buffer, 0u, 4096u);
        if (result.hasError()) {
            stream.close();
            return false;
        }
        count: unsigned int = result.getResult();
        if (count == 0u) break;
        sink.write(buffer, 0u, count);
    }
    complete: bool = stream.isComplete();
    stream.close();
    return complete;
}
```

### `DecryptCipherInputStream`

```k
decryptInput(source: k::io::InputStream<byte>&, sink: k::io::OutputStream<byte>&,
             cipher: const k::crypto::SymmetricCipher&,
             key: const k::crypto::SymmetricKey&, nonce: const unsigned byte[],
             aad: const unsigned byte[]) : bool throws(Throwable) {
    session: k::crypto::DecryptSession! = cipher.decrypt(
        key, nonce, k::crypto::DecryptionStaging::Memory);
    session.updateAad(aad);
    stream: k::io::crypto::DecryptCipherInputStream! =
        new k::io::crypto::DecryptCipherInputStream(source, session);
    buffer: byte[4096];
    while (true) {
        result: Expected<unsigned int, k::io::StreamOutOfData> =
            stream.read(buffer, 0u, 4096u);
        if (result.hasError()) {
            stream.close();
            return false;
        }
        count: unsigned int = result.getResult();
        if (count == 0u) break;
        sink.write(buffer, 0u, count);
    }
    complete: bool = stream.isComplete() && stream.isVerified();
    stream.close();
    return complete;
}
```

The caller owns and closes the sink. These functions close the wrapped source
on normal completion or a reported read error; arrange cleanup if an exception
interrupts them earlier.
Encryption finalizes at source EOF and is complete only after the final bytes
and tag have been delivered. AEAD input decryption reads to EOF and verifies
before exposing staged plaintext. Read through plaintext EOF before checking
completion. Closing early aborts or discards pending output.

For tagless ciphers there is no tag or verification: `isVerified()` remains
false and plaintext may be released incrementally. The authenticated examples
above intentionally require verification and must not be used unchanged for
tagless ciphers. A source must delimit exactly one encrypted message at EOF;
an output stream delimits one message at close. Do not concatenate messages
without an external framing protocol.

## 9. Errors and operational checklist

`CryptoException` is the base checked crypto exception.
`MessageDigestException` reports digest failures;
`CryptoInvalidInputException` reports invalid lengths or parameters;
`CryptoAuthenticationException` reports a failed authentication check;
`CryptoOperationException` reports backend failures; and
`CryptoStateException` reports an invalid operation order or state.
Stream operations can also report I/O errors or throw other exceptions. Do not
convert these failures into successful empty messages.

For a deployed protocol:

1. Select an available authenticated cipher and enforce its parameters.
2. Obtain and protect an appropriate key; use separate keys for separate purposes.
3. Ensure nonce uniqueness under each key, including across restarts and hosts.
4. Define message framing and authenticate relevant metadata as AAD.
5. Send the nonce, ciphertext, and tag in the protocol's specified format.
6. Release AEAD plaintext only after successful verification; handle failures explicitly.
7. Respect per-key, per-message, storage, and staging limits, and rotate keys.
