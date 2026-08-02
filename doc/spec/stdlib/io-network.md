# Asynchronous Network I/O (`k::io`) — Phase 5

This module adds interruptible TCP networking primitives on top of the same
thread interruption model used by `FileChannel`.

## Types

- `NetworkAddress`: immutable endpoint (`host`, `port`), with helpers
  `loopback(port)` and `any(port)`.
- `SocketChannel`: readable/writable TCP byte channel.
- `Socket`: convenience subtype of `SocketChannel`.
- `ServerSocket`: listening endpoint (`bind`, `accept`, `localPort`).
- `DatagramSocket`: UDP endpoint with `bind`, `sendTo`, `receive`, and
  optional connected mode (`connect` + `send`).

## Blocking semantics

Operations that block (`connect`, `accept`, `read`, `write`) support:

- interruption → `ThreadInterruptionException`
- timeout → `TimeoutException`
- closed endpoint → `ClosedChannelException`
- platform failure → `IOException(errno)`

`read` returns `0` on orderly peer shutdown (EOF).

## Current scope

Phase 5 currently targets IPv4 and supports numeric addresses and
`localhost`, with:

- TCP (`Socket*`, `ServerSocket`) on interruptible/timeout-aware waits.
- UDP (`DatagramSocket`) for loopback-style datagram send/receive.

## Phase 6 foundation

A minimal `EventLoop` is available (`k::io::EventLoop`) with:

- `submit(Runnable*)`
- `scheduleAfter(Duration, Runnable*)`
- `run()` / `stop()`

This is the base for upcoming selector/reactor APIs.
