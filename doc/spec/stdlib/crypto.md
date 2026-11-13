# `k::crypto`

The separately built `libk-crypto` library provides incremental message
digests, HMAC, HKDF, and symmetric encryption. Programs using it must import `k::crypto`
and link the crypto library.

## `MessageDigest`

`MessageDigest` accepts byte-array updates and returns the digest bytes from
`finalize()`. Its named static factories return a digest when initialization
succeeds and `null` otherwise.

`MessageDigestAlgorithm` is a public alias of `String`. The
`MessageDigestAlgorithms` enum is backed by that alias, and each standard entry
contains its canonical OpenSSL algorithm name. An enum value can be passed
directly to `fromName`, which also accepts raw names, including other names or
aliases recognized by the linked OpenSSL library.

| `MessageDigestAlgorithms` entry | Canonical OpenSSL name |
|---|---|
| `MD5` | `MD5` |
| `SHA1` | `SHA-1` |
| `SHA224` | `SHA-224` |
| `SHA256` | `SHA-256` |
| `SHA384` | `SHA-384` |
| `SHA512` | `SHA-512` |
| `SHA512_224` | `SHA-512/224` |
| `SHA512_256` | `SHA-512/256` |
| `SHA3_224` | `SHA3-224` |
| `SHA3_256` | `SHA3-256` |
| `SHA3_384` | `SHA3-384` |
| `SHA3_512` | `SHA3-512` |

```k
public alias MessageDigestAlgorithm : String;
public enum MessageDigestAlgorithms : MessageDigestAlgorithm { ... }
public static fromName(name: const MessageDigestAlgorithm&) : MessageDigest!
```

`fromName` borrows the algorithm-name string and returns an initialized digest
when the algorithm is available through the linked OpenSSL library. It returns
`null` when the name is unsupported or the backend cannot initialize that
algorithm. Names and aliases are interpreted by OpenSSL.

`update()` and `finalize()` report digest-operation failures with
`MessageDigestException`.

The whole-array `update(data)` API remains available. Slice overloads accept
`const byte[]` or `const unsigned byte[]` with an offset and length; they check
the range and update OpenSSL directly from the selected memory without making
a temporary copy.

## `k::io::crypto::MessageDigestOutputStream`

This decorator is provided by the crypto library as the separate
`k::io::crypto` module. Import `k::io::crypto` and link the `libk-crypto`
package to use it. Its named factories and `fromName(name, output)` create an
owned digest and wrap the supplied `k::io::OutputStream<byte>&`. The wrapped
output is borrowed.

Writes are forwarded unchanged and update the digest with the same bytes,
including only the requested range for a bulk write. `close()` follows filter
stream close behavior, finalizes the digest once, and freezes the stream.
`getDigest()` returns a read-only view of the cached result after successful
close-time finalization, or null before then. Digest failures remain
`MessageDigestException`; writing after close throws `IllegalStateException`.

The digest covers bytes that pass through the stream's position in a decorator
chain. Place it next to the sink, downstream of encoders or other transforms,
when the desired digest is over the final bytes written to that sink.

## HMAC

`HmacKey::fromBytes` copies the supplied unsigned bytes into an owned,
non-copyable key. `keySize()` returns its length in bytes. Internal key storage
is cleared on destruction; the caller remains responsible for its input buffer.
The key is independent of symmetric cipher keys and is not tied to a digest.
An empty key may be imported but does not provide authentication security.
There is no HMAC key-generation API.

`Hmac::create(algorithm, key)` initializes a one-message HMAC using a
`MessageDigestAlgorithm`. The string alias accepts standard enum values and
provider-supported extensions. `SHA256(key)` and `SHA512(key)` are convenience
factories. A factory returns null when the requested combination is
unavailable; invalid keys and operation failures throw crypto exceptions. The
initialized HMAC owns its state independently of the supplied key.

`algorithm()` returns the selected digest name and `tagSize()` the full tag
length in bytes. `update` accepts whole `byte[]` or `unsigned byte[]` arrays,
or a slice given by offset and length. An empty message is valid. `finalize()`
returns an owned full tag; `verify(expected)` finalizes and compares the full
tag, returning false for a mismatch or wrong length. Equal-length comparisons
are constant-time. A successful or failed verification consumes the HMAC;
subsequent updates and finalization throw `CryptoStateException`.

`finalizeTruncated(size)` and `verifyTruncated(expected, size)` use exactly
the specified prefix of the full tag. The size must be from one byte through
`tagSize()`; a different expected-tag length fails verification. The size must
come from trusted protocol configuration, not from a received tag. Full tags
are preferred; for new protocols use at least 128 bits if truncation is needed.
`Hmac::constantTimeEquals` compares equal-length byte arrays without
data-dependent early exit; it does not enforce a protocol's expected tag size.

### HMAC stream adapters

`k::io::crypto::HmacOutputStream` and `HmacInputStream` each borrow an
underlying byte stream and consume an `Hmac` owner. They close the wrapped
stream and expose `getTag()` as a read-only view owned by the adapter.
`isComplete()` identifies a successfully completed message. `verify(expected)`
and `verifyTruncated(expected, size)` require completion and compare the cached
tag in constant time; an incorrect expected length returns false.
Bytes supplied to the HMAC before constructing the adapter are included as
the message prefix. Use a fresh HMAC to authenticate only the stream's bytes.

The output adapter forwards each write before updating the HMAC. It completes
only after successful flush, close, and finalization. A failure during any of
these operations leaves it incomplete, including when a destination accepted
only part of a write.

The input adapter authenticates bytes read or skipped through it and finalizes
only at successful EOF. A read failure or early close leaves it incomplete.
It releases bytes before verification: applications must not act on untrusted
data until its tag has been verified. Neither adapter adds a tag or frames
messages; protocols must carry the tag and delimit messages separately.

## HKDF

`Hkdf::create(algorithm)` selects provider-backed RFC 5869 HKDF using a
`MessageDigestAlgorithm`. `SHA256()` and `SHA512()` are convenience
factories. A factory returns null when the digest or HKDF is unavailable;
operation failures throw crypto exceptions. `algorithm()`, `hashSize()`, and
`maxOutputSize()` report the digest name, digest length, and maximum output
length in bytes.

Any fixed-output digest the selected provider offers can be used with `create`,
for example SHA-1, SHA-224, SHA-384, SHA-512/224, SHA-512/256 and SHA-3;
extendable-output digests such as SHAKE are reported as unavailable (null).
`CryptoCapabilities::hkdfDigestAlgorithms()` lists the digests on which a mode
was witnessed. Results match RFC 5869 Appendix A, including the SHA-1 cases.

A `Hkdf` and an `HkdfPrk` are immutable after construction. Their operations
may run concurrently from several threads on the same object; the object must
not be destroyed while another thread is using it.

`extract(ikm, salt)` returns an owned `HkdfPrk`; `extract(ikm)` supplies the
RFC default salt of `hashSize()` zero bytes. IKM can be an unsigned byte array,
an `HmacKey`, or a `SymmetricKey`. The operation borrows its inputs and does
not expose an existing key's contents. Empty byte arrays and embedded zero
bytes are permitted, subject to provider restrictions. Pass an empty array to
represent omitted `info`; there is no separate no-info overload.

An `HkdfPrk` owns secret material independently of the original descriptor and
is non-copyable: its copy constructor and assignment are deleted, so copies
and drain-to-value conversions are compile errors, and copying a union whose
active alternative is a PRK raises a `FatalError`. `algorithm()` and
`keySize()` describe it. `expand(info, length)` derives an owned byte array.
`expandSymmetricKey(cipher, info)` derives
exactly `cipher.keySize()` bytes and applies the cipher's key validation;
`expandHmacKey(info, length)` derives an `HmacKey`. A PRK can expand multiple
outputs. Internal secret buffers are cleared on destruction or failure.

`importPrk(bytes)` imports an externally supplied PRK of at least
`hashSize()` bytes and binds it to the selected digest. `exportBytes()` makes
an explicit caller-owned copy of a PRK. The caller must protect and clear
exported bytes and any other caller-owned secret buffers.

`derive(ikm, salt, info, length)` performs extract and expand in one operation;
the overload without salt supplies the RFC default. `deriveSymmetricKey` and
`deriveHmacKey` provide equivalent combined derivations into key objects.
Output length must not exceed `255 * hashSize()`; invalid lengths throw
`CryptoInvalidInputException`. A provider may reject otherwise valid inputs,
including zero-length output, according to its policy.

`info` is binary context chosen by the protocol, not a string or a secret
generator. Use distinct, unambiguously encoded values for different purposes
and key lengths. Output with identical PRK and `info` shares a prefix across
different requested lengths. HKDF does not authenticate data, increase weak
input entropy, or replace a password-based KDF.

The standard OpenSSL HKDF provider limits `info` to 1024 bytes. Other
providers may differ. Inputs are never silently truncated, hashed, or
processed by a different implementation to bypass this limit. A provider
failure is reported explicitly. The OpenSSL library context used to create a
descriptor must outlive it, its PRKs, and their operations; incompatible
provider changes cause operations to fail rather than silently changing the
selected digest.
Valid factory, PRK import, extract, derive, and expand calls inside an active
OpenSSL `ASYNC_JOB` throw `CryptoOperationException` before provider execution.
For a nondefault context, every thread using it must either exit before the
context is freed or call `OPENSSL_thread_stop_ex` on that thread after its last
use and before the context is freed. OpenSSL also cleans up the thread that
frees the context. Provider operations normally execute on the calling thread
when its OpenSSL error queue is empty. Existing caller errors may force
execution on a worker to preserve the queue and marks; a thread-affine provider
may then reject the operation.

## Symmetric encryption

`SymmetricCipher` selects an algorithm family and an operation mode
independently. `SymmetricAlgorithm`, `SymmetricMode`, and `SymmetricPadding`
are aliases of `String`. Their corresponding enums provide standard names:

| Enum | Entries and underlying names |
|---|---|
| `SymmetricAlgorithms` | `AES`, `ARIA`, `Camellia` (`CAMELLIA`), `SM4`, `ChaCha20` (`CHACHA20`), `AsconAead128` (`ASCON-AEAD128`) |
| `SymmetricModes` | `GCM`, `CCM`, `OCB`, `CBC`, `CTR`, `XTS`, `Poly1305` (`POLY1305`), `Ascon` (`ASCON`), `CFB`, `OFB`, `CFB1`, `CFB8` |
| `SymmetricPaddings` | `None` (`NONE`), `PKCS7` |

Entries without an explicit underlying name use the entry name. Factories
accept enum values or alias strings; names are case-sensitive.

```k
public static create(algorithm: const SymmetricAlgorithm&, keyBits: unsigned int,
                     mode: const SymmetricMode&) : SymmetricCipher!;
public static create(algorithm: const SymmetricAlgorithm&, keyBits: unsigned int,
                     mode: const SymmetricMode&, nonceSize: unsigned int,
                     tagSize: unsigned int, padding: const SymmetricPadding&) : SymmetricCipher!;
```

Sizes other than `keyBits` are in bytes. The three-argument factory defaults
CBC to PKCS7 padding and all other modes to no padding. Its defaults are a
12-byte nonce and 16-byte tag for GCM, CCM, OCB and Poly1305; Ascon uses
16 bytes for each, and other modes use a 16-byte IV and no tag. PKCS7 is
restricted to CBC. Without padding, CBC input must be block-aligned.

Not every family, key size, mode, or parameter combination exists. Factories
return null for an invalid or unavailable combination, including when the
configured OpenSSL providers cannot supply or initialize the required cipher.
For example, `ChaCha20` uses `Poly1305` with a 256-bit key.
`AsconAead128` uses `Ascon` with a 128-bit key, nonce and tag. It requires a
provider implementing `ASCON-AEAD128` with encryption and decryption
capability and matching standardized empty-message known-answer checks, both
without AAD and with AAD, plus a nonempty payload check. An unavailable or
mismatching provider yields null.
`fromName(name)` accepts recognized canonical cipher names with default
parameters, not arbitrary OpenSSL aliases; it also returns null when
unavailable.

For CCM, configurable nonce lengths are 7–13 bytes and tag lengths are
even values from 4–16 bytes; the nonce length also limits the message size.
ChaCha20-Poly1305 requires a 12-byte nonce and a 16-byte tag. Configurable
parameters remain subject to provider availability.

`AES128GCM()`, `AES192GCM()`, and `AES256GCM()` and the canonical
`AES-128-GCM`, `AES-192-GCM`, and `AES-256-GCM` names remain available when
the provider supports them. These defaults use 16-, 24-, and 32-byte keys,
a 12-byte nonce and a 16-byte tag. `name()`, `algorithm()`, `mode()`,
`keyBits()`, `padding()`, `authenticated()`, `keySize()`, `nonceSize()`,
`tagSize()`, `maxDataSize()` and `maxAadSize()` describe a cipher; sizes and
limits are in bytes except `keyBits()`.

`SymmetricKey` derives from `Key` and contains private key bytes. Its
`keySize()` reports the key length in bytes. `generateKey()` creates a key;
`importKey(const unsigned byte[])` imports exactly `keySize()` bytes. Keys
are checked for matching algorithm family, component size and XTS/non-XTS
use when starting a session. For XTS, `keyBits` is the size of *each*
component key, so `keySize()` and the imported key material are twice
`keyBits / 8`; the two components must differ.
Each XTS session processes one data unit of at least 16 bytes and at most
16 MiB. Its 16-byte IV identifies the data-unit tweak. Different data units
under a key need distinct tweaks; rewriting a data unit with the same tweak
does not provide fresh authenticated encryption. XTS provides no integrity.

An IV or nonce is not included in the cipher streams: callers must transport
it and the required cipher parameters separately. Obtain `nonce()` before
transferring a session owner to a stream. Nonces for AEAD modes must not be
reused with the same key. CTR counter ranges must not overlap under a key;
CBC and CFB require unpredictable IVs, and OFB IVs must not be reused.
`randomNonce()` generates an IV or nonce, but random generation does not
guarantee uniqueness: applications must account for collision risk and
mode-specific usage limits, including CCM's per-key block-operation limit.
Metadata that must be bound to an AEAD ciphertext should be supplied as
additional authenticated data (AAD).

### Sessions and finalization

`encrypt(key, nonce)` creates an encryption session, while `encrypt(key)`
generates its nonce. `decrypt(key, nonce, staging)` creates a decryption
session. Both expose `nonce()`, `tagSize()` and `authenticated()`. AEAD modes
accept byte or unsigned-byte AAD through `updateAad()` before any data update;
unauthenticated modes do not accept AAD. `update()` accepts whole arrays or
slices. Sessions are single-use; `close()` releases resources and does not
replace finalization or authentication.

Encryption `update()` returns exactly the ciphertext produced by that call,
which may be empty or differ in length from the input. `updateTo()` returns
the actual number of bytes written. Before `updateTo()`, reserve at least
`updateOutputCapacity(inputLength)` bytes at the output offset: the safe
bound may include previously buffered input and may exceed the input length.
After the last update, call `finish()`, drain `readFinal()` until it returns
zero, then obtain `tag()`. Transmit update ciphertext followed by final
ciphertext, followed by the tag; tagless modes return an empty tag. A zero
update count does not mean the message is complete.

For compatibility, encryption `finalize()` calls `finish()` and returns the
tag, and `finalizeTo()` writes that tag. Neither returns final ciphertext:
generalized callers must also drain `readFinal()`. Existing default AES-GCM
usage remains compatible.

For authenticated modes (GCM, CCM, OCB, ChaCha20-Poly1305 and Ascon-AEAD128
where available), decryption `finalize(tag)` verifies the tag before `read()`
exposes any plaintext. A missing or invalid tag fails authentication. For
unauthenticated CBC, CTR, XTS, CFB variants and OFB, `finalize()` takes no
tag and `read()` can release plaintext before finalization. A pre-final read
returning zero means no plaintext is currently available, not end of message.
Such plaintext has not been authenticated; in particular, CBC padding may fail
after earlier plaintext has already been released. `updatesProduceOutput()`
indicates whether a session can produce incremental output; XTS may instead
stage the data unit if the selected provider cannot stream it correctly.

`DecryptionStaging::Memory` selects memory staging.
`DecryptionStaging::TemporaryFile` explicitly selects a private temporary file
for staged data; plaintext may be stored on disk, and physical erasure is not
guaranteed. The staging selection is also available on the encryption overload
`encrypt(key, nonce, staging)`.

CCM has a custom streaming implementation using OpenSSL ECB block-cipher
primitives; this composition is not automatically FIPS-validated. An overload
of `encrypt` or `decrypt` taking `(key, nonce, staging, messageLength)`
declares the total plaintext or ciphertext byte length, respectively, excluding
the tag. The supplied length must match the updates and allows incremental
CCM processing, although AEAD decryption still withholds plaintext until
verification. Without a declared length, CCM stages the input until
finalization: `encrypt(key, nonce)` selects memory; the overloads accepting
`staging` allow an explicit temporary file instead. AAD is supplied before
data in either case.

The `k::io::crypto` module provides `EncryptCipherInputStream`,
`DecryptCipherInputStream`, `EncryptCipherOutputStream`, and
`DecryptCipherOutputStream`. These streams adapt cipher sessions to input and
output streams. Their constructors borrow the wrapped stream and consume the
session owner. Closing a cipher stream also closes its wrapped stream:

```k
EncryptCipherInputStream(InputStream<byte>&, EncryptSession!)
DecryptCipherInputStream(InputStream<byte>&, DecryptSession!)
EncryptCipherOutputStream(OutputStream<byte>&, EncryptSession!)
DecryptCipherOutputStream(OutputStream<byte>&, DecryptSession!)
```

Every stream exposes `isComplete(): bool`. Encryption input finalizes at
source EOF, then delivers all final ciphertext and any tag; it is complete
only once those bytes have been delivered. Closing before source EOF aborts
finalization; closing before the final bytes are delivered leaves the message
incomplete. Encryption output finalizes on successful close and writes final
ciphertext before any tag.

Decrypting streams expose `authenticated()` and `isVerified()`. For AEAD,
the final session `tagSize()` bytes are the tag. A decrypting input stream
reads to source EOF and verifies before releasing staged plaintext; a
decrypting output stream verifies on close before writing that plaintext.
`isVerified()` becomes true only after successful tag verification. In tagless
modes there is no trailer or verification, `isVerified()` remains false, and
plaintext can be released incrementally before finalization.

For decrypting input, `isComplete()` becomes true after finalization and
plaintext EOF is observed. Closing before source EOF aborts finalization;
closing after verification but before plaintext EOF discards unread staged
plaintext and leaves the stream incomplete. For decrypting output,
`isComplete()` becomes true only after all plaintext is written and close
finishes successfully. A failure after verification can leave `isVerified()`
true and `isComplete()` false; a destination failure can occur after some
verified plaintext was written.

An input stream delimits one message at source EOF; an output stream delimits
it at close. If a stream operation has already failed, a later close cannot
complete the message. In particular, output encryption does not finalize
after a prior write failure.

Crypto API errors derive from `CryptoException`. `CryptoInvalidInputException`
reports invalid inputs, `CryptoAuthenticationException` reports failed
authentication, `CryptoOperationException` reports operation failures, and
`CryptoStateException` reports use in an invalid state.

## Capability discovery

`CryptoCapabilities::snapshot()` discovers available digests, HMAC and HKDF
digests, and symmetric ciphers and returns an owned snapshot. Its `digestAlgorithms()`,
`symmetricAlgorithms()`, `symmetricModes()` and `symmetricPaddings()` methods
return owned `Sequence` values containing the corresponding string aliases.
Names may include supported extensions that have no entry in the standard enums.
`hmacDigestAlgorithms()` returns digest names actually witnessed with HMAC and
a fixed 32-byte probe key. Other keys can be subject to provider restrictions.
`hkdfDigestAlgorithms()` returns digest names for which at least one HKDF mode
was witnessed. `hkdfConfigurations()` returns `HkdfConfiguration` values with
`algorithm()`, `hashSize()`, `supportsExtract()`, `supportsExpand()`, and
`supportsDerive()` accessors. Each mode is independently probed with fixed
non-secret inputs; other inputs and subsequent provider settings can differ.
With an empty caller OpenSSL error queue, discovery normally uses the calling
thread. Existing errors may force worker execution to preserve the queue and
its marks, which can cause discovery to fail for thread-affine providers.
Applications using nondefault contexts must also observe OpenSSL's per-thread
context cleanup requirements for discovery.

The global symmetric mode and padding sequences are unions, not a Cartesian
product. `symmetricModes(algorithm, keyBits)` and
`symmetricPaddings(algorithm, keyBits, mode)` restrict the results to matching
configurations. `symmetricConfigurations()` returns representative
`SymmetricCipherConfiguration` values with `algorithm()`, `keyBits()`, `mode()`,
`padding()`, `nonceSize()` and `tagSize()` accessors. Each tuple is validated
with the cipher implementation; it is not an exhaustive list of every permitted
nonce and tag length. For XTS, `keyBits()` is the component-key size.

Discovery uses the active provider configuration and the same selection policy
as the factories; provider candidates unsupported by the library are not
advertised. OpenSSL enumerates digest and cipher candidates; padding names are
libk policies validated with each cipher, not a provider enumeration. A snapshot
and its returned sequences own their data and remain unchanged if providers are
subsequently reconfigured. An iterator borrows its sequence. Availability may
change between discovery and use, so a later factory call can still return
null. Discovery failures are reported as crypto errors.

When available, the `CHACHA20` / `STREAM` extension is unauthenticated. Its
16-byte IV consists of a 64-bit little-endian initial block counter followed by
an 8-byte nonce; this is not the 96-bit nonce format of ChaCha20-Poly1305.
Counter ranges must not overlap, and a session rejects counter wrap. Use a
separate key from ChaCha20-Poly1305, and provide independent authentication
when integrity is required. A session's `maxDataSize()` accounts for its
initial counter and is capped at the largest `unsigned long` value.
