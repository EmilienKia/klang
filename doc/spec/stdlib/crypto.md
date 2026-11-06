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
