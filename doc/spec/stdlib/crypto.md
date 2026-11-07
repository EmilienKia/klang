# `k::crypto`

The separately built `libk-crypto` library provides incremental message
digests and symmetric encryption. Programs using it must import `k::crypto`
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
