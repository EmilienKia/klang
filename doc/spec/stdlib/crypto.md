# `k::crypto`

The separately built `libk-crypto` library provides `MessageDigest` for
incremental digest computation. Programs using it must import `k::crypto` and
link the crypto library.

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

`k::crypto` provides AES-GCM authenticated encryption through
`SymmetricCipher`. Its `AES128GCM()`, `AES192GCM()`, and `AES256GCM()` named
factories, and its `fromName(String)` factory, return owned nullable cipher
results. `fromName` recognizes the canonical names `AES-128-GCM`,
`AES-192-GCM`, and `AES-256-GCM`; other names return null. `name()` returns
the corresponding canonical name. `keySize()`, `nonceSize()`, and
`tagSize()` report lengths in bytes. These AES variants use 16-, 24-, and
32-byte keys respectively, a 12-byte nonce, and a 16-byte authentication tag.

`SymmetricKey` derives from `Key` and contains private key bytes. Its
`keySize()` reports the key length in bytes. A cipher's `generateKey()` creates
a key, and `importKey(const unsigned byte[])` imports key bytes; both validate
that the key has the required length.

`randomNonce()` generates a nonce for the cipher. A caller must never reuse a
12-byte nonce with the same key. Random generation makes reuse unlikely, not
impossible: collision risk grows with the number of nonces generated under a
key, so applications must account for that limit. The caller is responsible
for transporting the nonce and any other required message metadata alongside
the ciphertext; the cipher streams do not include the nonce in their byte
stream. Obtain the session nonce before transferring its owner to a stream.
Metadata that must be bound to the ciphertext should be authenticated as
additional authenticated data (AAD).

`encrypt(key, nonce)` creates an encryption session using the supplied nonce;
`encrypt(key)` creates one with a generated nonce. `decrypt(key, nonce,
staging)` creates a decryption session using the supplied nonce and the
selected `DecryptionStaging` mode. Both session types expose `nonce()` and
`tagSize()`. They accept AAD through `updateAad()` for byte or unsigned-byte
slices, and data through `update()` for byte or unsigned-byte arrays and
slices. AAD must be supplied before data.
Encryption `finalize()` returns the authentication tag; decryption
`finalize(tag)` verifies the supplied tag. Decryption reads expose plaintext
only after tag verification succeeds. `close()` releases the session; it does
not replace successful finalization or, for decryption, tag verification.

`DecryptionStaging::Memory` stages decrypted data in memory.
`DecryptionStaging::TemporaryFile` explicitly selects staging in a private
temporary file. With the latter choice plaintext is stored on disk, and
physical erasure of that data is not guaranteed.

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

Every stream exposes `isComplete(): bool`. For
`EncryptCipherInputStream`, it becomes true only after the complete tag has
been delivered; source EOF triggers finalization and the tag follows the
ciphertext. Closing this stream before source EOF aborts without finalizing.
If closed before the full tag is delivered, the message remains incomplete
and the tag may be only partially emitted. For
`EncryptCipherOutputStream`, successful close finalizes the session and
appends the 16-byte tag after the ciphertext.

Decrypting streams also expose `isVerified(): bool`, which reports whether
the message tag has been authenticated; it is distinct from
`isComplete(): bool`. A decrypting input stream reads through source EOF,
interprets the final 16 bytes as the tag, and verifies before making staged
plaintext readable. Its `isVerified()` becomes true after successful
verification; `isComplete()` becomes true only after verified plaintext has
been drained and its EOF observed. Closing this input stream before source EOF
aborts without verification, leaving both flags false. Closing it after
verification but before plaintext EOF discards any unread staged plaintext
and leaves `isComplete()` false. A decrypting output
stream takes the message through writes and verifies the trailing 16-byte tag
on close before writing staged plaintext to its destination. Its
`isVerified()` becomes true as soon as tag verification succeeds, whereas
`isComplete()` becomes true only after all plaintext is written and close
finishes successfully.

An input stream therefore delimits a message at source EOF; an output stream
delimits it at close. A missing or invalid tag fails authentication. If a
stream operation has already failed, a later close cannot complete the
message. In particular, output encryption does not finalize after a prior
write failure. A failure after decryption has been verified can leave
`isVerified()` true while `isComplete()` remains false; a destination failure
may also occur after some verified plaintext has been written.

Crypto API errors derive from `CryptoException`. `CryptoInvalidInputException`
reports invalid inputs, `CryptoAuthenticationException` reports failed
authentication, `CryptoOperationException` reports operation failures, and
`CryptoStateException` reports use in an invalid state.
