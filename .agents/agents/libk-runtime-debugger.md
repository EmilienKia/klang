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
- Proactor file I/O: `io/file/file_channel.k` + `io_operation.c` + `io_runtime.c` + `io_ffi.c`
- Buffers/mapping: `io/byte_buffer.k`, `io/mapped_byte_buffer.k` + `io_buffer.c`
- Stream I/O: `io/socket_channel.k`, `io/server_socket.k` + `io_ffi.c`,
  `io_operation.c`, `io_runtime.c`, `io_resolver.c`, `io_stream_ffi.h`
- Datagram I/O: `io/datagram_socket.k`, `io/network_address.k` + `io_ffi.c`,
  `io_datagram_ffi.h` and the same operation/runtime/resolver engine
- Executors/EventLoop: `executor.k`, `io/event_loop.k` + native endpoints
- Application exit: `klang/src/gen/gen_unit.cpp` + `application_ffi.c`,
  `io_runtime.c`; normal and unwind barriers precede concrete field destruction

Native names above are under `libk/libk/src/runtime/`. The old blocking file
and network engines, Selector and async file stream adapters are retired.

## Mandatory invariants
- Preserve thrown typeinfo dispatch through `rtti.c` accessors.
- Keep interruptible blocking paths on park-lot/runtime primitives.
- Keep K semantics authoritative; FFI adapts but must not redefine behavior.
- Future publication is independent of callback executors; Sync is a thin
  Future/drain bridge. Never restore a blocking POSIX transport fallback.
- Drain target and linked-timeout CQEs before releasing buffer/handle leases.
- Callback carriers drop exactly once, including suppression and rejection.
- Kernel quiescence is not the completed Application/user-delivery barrier.

## Output contract
- Layer where bug originates (K API vs FFI vs runtime substrate).
- Patch synchronized across touched layers.
- Focused validation through `libk-tests` and/or `libk-thread-io-tests`.
- Select native mocks/kernel, foundations, fully file, stream, datagram or
  application-lifecycle targets from `libk/AGENTS.md`; include both byte and
  text I/O targets for a full scoped replay. Lifecycle shutdown uses fresh
  child executables, with their counts reported separately.
