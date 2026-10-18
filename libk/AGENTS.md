# K Standard Library (`libk/`) — AI Agent Guide

Scope: K stdlib modules, runtime C substrate, FFI bridges, and libk test executables.

---

## 1. What lives here

- `libk/libk/src/*.k`: standard library modules (`String`, `Future`, sync, I/O, threading...).
- `libk/libk/src/runtime/*.c`: low-level runtime substrate (threads, proactor I/O, synchronization, futures).
- `libk/libk/src/runtime/*_ffi.c`: C<->K bridges.
- `libk/libk/tests/*.cpp`: libk functional and stress tests.

Unless fully qualified, paths below are relative to `libk/libk/src/`.

---

## 2. K-language rules for this subtree

- Naming:
  - types: `PascalCase`
  - functions/variables: `camelCase`
  - constants: `ALL_CAPS`
- 4-space indentation, same-line braces.
- Use the narrowest correct addresser:
  - `&` reference (immutable, non-null)
  - `?` view (immutable, nullable)
  - `+` link (mutable, non-null)
  - `*` pointer (mutable, nullable)
  - `!` owner (ownership transfer)
  - `#` drain (resource-acquisition semantics)
- Prefer `const` whenever possible.
- Never add `import k;` manually (auto-imported by compiler).
- FFI functions should remain `private`.

---

## 3. Runtime and ABI invariants

- Thrown typeinfo dispatch state is per-thread and must go through:
  - `__k_thrown_typeinfo_chain_addr()`
  - `__k_thrown_typeinfo_addr()`
- Exception throwing semantics are by-value centric for chaining correctness.
- Interruptible blocking/wakeup logic relies on park-lot and futex-based runtime paths; do not bypass them with ad-hoc synchronization.
- Keep ownership/lifecycle semantics aligned between K-level APIs and runtime C implementations.

## 3.1 libk architecture (K layer -> FFI -> runtime layer)

### End-to-end execution model

1. **K API layer** (`libk/libk/src/*.k`)
   - user-visible abstractions (`Thread`, `Future`, `Mutex`, `FileChannel`, `Socket`, `String`, collections).
2. **FFI bridge layer** (`libk/libk/src/runtime/*_ffi.c`)
   - thin translation between K ABI shapes and runtime C calls.
   - converts data representations (paths, buffers, result codes, handles).
3. **Runtime substrate** (`libk/libk/src/runtime/*.c`)
   - owns low-level state machines, blocking/wakeup, cancellation/interrupt, and OS calls.
4. **Error/exception boundary**
   - runtime outcomes mapped to K exceptions/status codes.
   - thrown-type dispatch uses per-thread slots from `rtti.c`.

### Architectural boundaries

| Boundary | Rule |
|---------|------|
| K API -> FFI | keep K semantics authoritative; FFI adapts, does not redefine behavior |
| FFI -> runtime substrate | keep bridge thin and explicit (encoding/decoding + call forwarding) |
| runtime -> OS syscalls | centralize in runtime modules (no direct syscall scattering in K-facing code) |
| exception dispatch | always route through `rtti.c` accessors; no duplicate dispatch state |

### Concurrency architecture blocks

- **Threading**: `thread.k` + `runtime_thread.c` + `thread_ffi.c`
- **Futures/promises**: `future.k` + `future_state.c` + `future_ffi.c`
- **Sync primitives**: `sync/*.k` + `sync_primitives.c` + `sync_ffi.c`
- **Shared park/wakeup substrate**: `park_lot.c` (used by sync and async paths)
- **Proactor file I/O**: `io_runtime.c`, `io_operation.c`, `io_buffer.c`, `io_uring_backend.c` + `io_ffi.c`
- **Application exit barrier**: compiler `klang/src/gen/gen_unit.cpp` ->
  private `application.k` declaration / `runtime/application_ffi.c` ->
  `runtime/io_runtime.c` completed shutdown. The barrier precedes the concrete
  Application destructor on normal return and unwind; `atexit` is only fallback.
- **Executor/EventLoop endpoints**: `executor.k`, `io/event_loop.k`,
  `runtime/executor_runtime.c`, `runtime/io_operation.c`; native leases retain
  endpoints, not K wrapper addresses.
- **Stream network substrate**: prepared `io_ffi.c` requests, `io_runtime.c`,
  `io_operation.c` and bounded `io_resolver.c`; RECV/SEND fully parents reserve
  one whole native direction, with children bypassing only their parent's gate.
  The bounded pending-delivery list retains accept/connect callbacks after
  registry retirement so shutdown can rollback/drop them before executor entry.
- **Datagram substrate**: `io/datagram_socket.k`, shared value addresses in
  `network_address.k`, `io_datagram_ffi.h` / `io_ffi.c`, and the same native
  registry/handles/resolver. SENDMSG/RECVMSG own persistent message contexts;
  datagrams never use fully loops, blocking transport bridges or executor I/O.
  Kernel quiescence releases buffer leases, not deferred callback metadata.
  Only successful Unix binds own pathname cleanup; destination/source address
  values never own paths.

### Typical data/control flow patterns

- **Future completion path**:
  `Future/Promise` K API -> `future_ffi.c` -> `future_state.c` state transition -> waiting thread wakeup via runtime sync primitives.
- **Interruptible wait path**:
  K-level wait API -> runtime blocking primitive -> park-lot wait -> interrupt/cancel signal -> wakeup and status mapping.
- **Async file read/write path**:
  `FileChannel` K call -> prepared `io_ffi.c` request -> native registry/CQE drain -> internal Future publication -> selected Executor callback. Fully transfers own a native parent and reserve the whole implicit FIFO operation; children never use a user executor.

### Coupling points with compiler/codegen

- Thrown-type state consumed by compiler-generated throw/catch path (`klang/src/gen/gen_statements.cpp`).
- ABI-shape assumptions (array/header layout, pointer-like addressers) must remain consistent with compiler lowering.

---

## 4. Fast file map

- Dynamic modules: `application.k` (`loadModule`), `module.k`, `runtime/module_ffi.c`
- Core: `libk/src/object.k`, `libk/src/string.k`, `libk/src/rtti.c`, `libk/src/fatal.c`
- Core: `object.k`, `string.k`, `rtti.c`, `fatal.c`
- Thread/time: `thread.k`, `thread_exceptions.k`, `time.k`, `runtime/runtime_thread.c`, `runtime/thread_ffi.c`
- Futures: `future.k`, `runtime/future_state.c`, `runtime/future_ffi.c`
- Sync: `sync/*.k`, `runtime/sync_primitives.c`, `runtime/sync_ffi.c`
- Buffers: `io/byte_buffer.k`, `io/mapped_byte_buffer.k`, `runtime/io_buffer.[ch]`
- Runtime lifecycle: `io/io_operation.k` (`IoRuntime`), `application.k`,
  `runtime/application_ffi.c`, `runtime/io_runtime.[ch]`
- Proactor file I/O: `io/file/file_channel.k`, `runtime/io_operation.[ch]`, `runtime/io_runtime.[ch]`, `runtime/io_ffi.c`, `runtime/io_file_ffi.h`
- Synchronous file streams: `io/file/file.k`, `io/file/file.c` (unchanged by the proactor migration)
- Stream I/O: `io/socket_channel.k`, `io/server_socket.k`, `io/network_address.k`,
  `runtime/io_stream_ffi.h`, `runtime/io_ffi.c`, `runtime/io_resolver.c`
- Datagram I/O: `io/datagram_socket.k`, `io/network_address.k`,
  `runtime/io_datagram_ffi.h`, `runtime/io_ffi.c`. `socket.k` and the blocking
  `network_ffi.c` transport engine are retired.

## 4.1 Investigation map (by symptom)

| Symptom | Start with | Then check |
|---------|------------|------------|
| Thread lifecycle/interrupt/join bug | `thread.k` | `runtime/runtime_thread.c`, `runtime/thread_ffi.c` |
| Future/promise state transition bug | `future.k` | `runtime/future_state.c`, `runtime/future_ffi.c` |
| Mutex/condition/semaphore/rwlock deadlock | `sync/*.k` | `runtime/sync_primitives.c`, `runtime/sync_ffi.c` |
| Async file operation behavior mismatch | `io/file/file_channel.k` | `runtime/io_operation.c`, `runtime/io_runtime.c`, `runtime/io_ffi.c`, `runtime/io_buffer.c` |
| Stream connect/accept/read/write issue | `io/socket_channel.k`, `io/server_socket.k` | `runtime/io_ffi.c`, `runtime/io_operation.c`, `runtime/io_runtime.c`, `runtime/io_resolver.c` |
| Datagram source/truncation/lifetime issue | `io/datagram_socket.k`, `io/network_address.k` | `runtime/io_datagram_ffi.h`, `runtime/io_ffi.c`, `runtime/io_operation.c`, `runtime/io_runtime.c`, `runtime/io_resolver.c` |
| Shutdown before Application/global destruction | `runtime/application_ffi.c`, `runtime/io_runtime.c` | `io/io_operation.k`, `klang/src/gen/gen_unit.cpp`, `libk/libk/tests/test-application-lifecycle.cpp` |
| String/collection semantic issue | `string.k` and related collection modules | matching tests under `libk/libk/tests/` |
| Exception dispatch mismatch | `rtti.c`, `fatal.c` | compiler throw/catch path in `klang/src/gen/gen_statements.cpp` |

---

## 5. Tests in this subtree

- `libk-tests-rtti-exceptions`: functional RTTI and exception handling tests.
- `libk-tests-strings`: string and string builder tests.
- `libk-tests-types`: foundational types, optional, expected, shared, math, and application tests.
- `libk-tests-big-integer-native`: GMP bridge arithmetic, ownership, checked conversions, and formatting; public K API cases run in `libk-tests-types`.
- `libk-tests-module`: dynamic module loading (`Application::loadModule`, `Module`); builds K/C fixture libraries from `libk/tests/modules/`.
- `libk-tests-io`: synchronous stream/file/path I/O tests (2 Catch2 shards: `-shard-0/1`).
- `libk-tests-collections-sequential`: sequential collection tests (List, Vector, Sequences).
- `libk-tests-collections-associative`: associative collection tests (Set, Map).
- `libk-thread-io-tests`: thread/future/sync/file/network tests (2 Catch2 shards).
- `libk-tests-time`: time API tests (2 Catch2 shards).
- `libk-tests-application-lifecycle`: isolated native/mock and compiled K exit
  barriers; every actual runtime shutdown executes in a fresh subprocess
  (120-second budget).
- `libk-tests-io`: synchronous stream/file/path I/O tests.
- `libk-tests-io-text`: print, text, codec, reader/scanner and UTF-8 pipeline tests.
- `libk-tests-collections-sequential`: sequential collection tests (List, Vector, Sequences).
- `libk-tests-collections-associative`: associative collection tests (Set, Map).
- `libk-thread-io-tests`: thread/future/sync/file/network tests.
- `libk-tests-file-channel-fully`: isolated FileChannel fully callback/Future/Sync API cases (120-second budget).
- `libk-tests-stream-channels`: isolated TCP/Unix proactive API cases (120-second
  ceiling, target under 60 seconds); `[stream-channel]` native mocks remain in
  `libk-tests-io-proactor`.
- `libk-tests-datagram-channels`: isolated IPv4/Unix packet, callback/Future/Sync,
  address-copy, DNS, direction, cancel/close and pathname cases (120 seconds).
  Native `[datagram-channel]` mocks remain in `libk-tests-io-proactor`.
- `libk-tests-io-proactor`: deterministic native/mock lifecycle cases, including `[file-fully]`; no kernel availability dependency.
- `libk-tests-io-proactor-kernel`: real io_uring pipe/timeout integration.
- `libk-tests-io-foundations-api`: K buffers, operation carriers and publication.
- `libk-tests-executor`: leases, queues, task cleanup and executor contracts.
- `libk-tests-uuid`, `libk-tests-time`: UUID and temporal APIs.
- `libk-perf-tests` (optional): stress/perf suite (`-DLIBK_PERF_TESTS=ON`).

### Scoped session rule

If in a working session, only `libk` changes (`.c` and/or `.k`), only recompile `libk` and run `libk` tests each time. Do not run the full project test suite (compiler and kdi tools are unaffected).

```bash
# Build libk tests
cd cmake-build-debug && ninja -j3 libk-tests-rtti-exceptions libk-tests-strings libk-tests-types libk-tests-io libk-tests-collections-sequential libk-tests-collections-associative libk-thread-io-tests libk-tests-module
ninja -C cmake-build-debug -j3 libk_stdlib libk-tests-rtti-exceptions libk-tests-strings libk-tests-types libk-tests-big-integer-native libk-tests-io libk-tests-io-text libk-tests-collections-sequential libk-tests-collections-associative libk-thread-io-tests libk-tests-io-proactor libk-tests-io-proactor-kernel libk-tests-io-foundations-api libk-tests-file-channel-fully libk-tests-stream-channels libk-tests-datagram-channels libk-tests-application-lifecycle libk-tests-executor libk-tests-uuid libk-tests-time

# Run libk tests
/usr/bin/ctest --test-dir cmake-build-debug -j1 -R "^(libk-tests-.*|libk-thread-io-tests)$" --output-on-failure
```

Keep every current target in a scoped rebuild/replay. The byte/file and
text/codec targets together preserve the original I/O cases; budget splitting
is not evidence of an allocator fix. Report process-local Catch2 totals and
hidden fresh-executable lifecycle totals separately.

Lifecycle TLS tests must distinguish guarded access from construction: a local
`threadlocal` declaration emits an access at the declaration point, and later
uses call the same helper again. Count successful construction/destruction with
runtime hooks; the single-access IR fixture uses namespace TLS first accessed
inside `try`, with an outer local cleanup scope. An expected-abort fork child
must restore `SIGABRT` to `SIG_DFL` before the fatal hook so the inherited Catch2
reporter does not turn the intentional refusal into a test failure. Capture its
diagnostic and verify that the parent's backend and owned descriptor survive.

Focused examples:
```bash
# Sync/thread-related only
cd cmake-build-debug && ./libk/libk/libk-thread-io-tests "[sync]"

# Futures only
cd cmake-build-debug && ./libk/libk/libk-thread-io-tests "[future]"
```
