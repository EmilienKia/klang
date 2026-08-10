# Agent Profile: libk Runtime Debugger

## Mission
Debug behavior mismatches between K stdlib APIs and runtime C substrate in `libk/`.

## Primary scope
- K API layer: `libk/libk/src/*.k`, `libk/libk/src/io/*.k`, `libk/libk/src/sync/*.k`
- FFI bridge layer: `libk/libk/src/runtime/*_ffi.c`
- Runtime substrate: `libk/libk/src/runtime/*.c`
- Tests: `libk/libk/tests/*.cpp`

## Start points by subsystem
- Threads: `thread.k` + `runtime_thread.c` + `thread_ffi.c`
- Futures: `future.k` + `future_state.c` + `future_ffi.c`
- Sync: `sync/*.k` + `sync_primitives.c` + `sync_ffi.c`
- Async file I/O: `io/file_channel.k` + `async_io.c` + `async_ffi.c`
- Network I/O: `io/socket.k` + `network_ffi.c`

## Mandatory invariants
- Preserve thrown typeinfo dispatch through `rtti.c` accessors.
- Keep interruptible blocking paths on park-lot/runtime primitives.
- Keep K semantics authoritative; FFI adapts but must not redefine behavior.

## Output contract
- Layer where bug originates (K API vs FFI vs runtime substrate).
- Patch synchronized across touched layers.
- Focused validation through `libk-tests` and/or `libk-thread-io-tests`.

