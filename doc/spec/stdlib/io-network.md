# Asynchronous Network I/O (`k::io`) — Phase 5

This module adds interruptible TCP networking primitives on top of the same
thread interruption model used by `FileChannel`.

## Types

- `NetworkAddress`: immutable endpoint (`host`, `port`), with helpers
  `loopback(port)` and `any(port)`.
- `SocketChannel`: readable/writable TCP byte channel.
- `Socket`: convenience subtype of `SocketChannel`.
- `ServerSocket`: listening endpoint (`bind`, `accept`, `localPort`).

## Blocking semantics

Operations that block (`connect`, `accept`, `read`, `write`) support:

- interruption → `ThreadInterruptionException`
- timeout → `TimeoutException`
- closed endpoint → `ClosedChannelException`
- platform failure → `IOException(errno)`

`read` returns `0` on orderly peer shutdown (EOF).

## Current scope

Phase 5 currently targets TCP over IPv4 and supports numeric addresses and
`localhost`.
