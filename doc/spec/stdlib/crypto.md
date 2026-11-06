# `k::crypto` Message Digests

The separately built `libk-crypto` library provides `MessageDigest` for
incremental digest computation. Programs using it must import `k::crypto` and
link the crypto library.

## `MessageDigest`

`MessageDigest` accepts byte-array updates and returns the digest bytes from
`finalize()`. Its named static factories return a digest when initialization
succeeds and `null` otherwise.

```k
public static fromName(name: const String&) : MessageDigest!
```

`fromName` borrows the algorithm-name string and returns an initialized digest
when the algorithm is available through the linked OpenSSL library. It returns
`null` when the name is unsupported or the backend cannot initialize that
algorithm. Names and aliases are interpreted by OpenSSL.

`update()` and `finalize()` report digest-operation failures with
`MessageDigestException`.
