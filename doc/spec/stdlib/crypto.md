# `k::crypto` Message Digests

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
