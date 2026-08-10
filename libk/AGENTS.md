# K Standard Library (`libk/`) — AI Agent Guide

Scope: K stdlib modules, runtime C substrate, FFI bridges, and libk test executables.

---

## 1. What lives here

- `libk/src/*.k`: standard library modules (`String`, `Future`, sync, I/O, threading...).
- `libk/src/runtime/*.c`: low-level runtime substrate (threads, async I/O, synchronization, futures).
- `libk/src/runtime/*_ffi.c`: C<->K bridges.
- `libk/tests/*.cpp`: libk functional and stress tests.

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

1. **K API layer** (`libk/src/*.k`)
   - user-visible abstractions (`Thread`, `Future`, `Mutex`, `FileChannel`, `Socket`, `String`, collections).
2. **FFI bridge layer** (`libk/src/runtime/*_ffi.c`)
   - thin translation between K ABI shapes and runtime C calls.
   - converts data representations (paths, buffers, result codes, handles).
3. **Runtime substrate** (`libk/src/runtime/*.c`)
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
- **Async file I/O substrate**: `async_io.c` + `async_ffi.c`
- **Network substrate**: `network_ffi.c`

### Typical data/control flow patterns

- **Future completion path**:
  `Future/Promise` K API -> `future_ffi.c` -> `future_state.c` state transition -> waiting thread wakeup via runtime sync primitives.
- **Interruptible wait path**:
  K-level wait API -> runtime blocking primitive -> park-lot wait -> interrupt/cancel signal -> wakeup and status mapping.
- **Async file read/write path**:
  `FileChannel`/`FileStream` K call -> `async_ffi.c` bridge -> `async_io.c` operation registry/dispatch -> completion -> result decode in K wrapper.

### Coupling points with compiler/codegen

- Thrown-type state consumed by compiler-generated throw/catch path (`klang/src/gen/gen_statements.cpp`).
- ABI-shape assumptions (array/header layout, pointer-like addressers) must remain consistent with compiler lowering.

---

## 4. Fast file map

- Core: `libk/src/object.k`, `libk/src/string.k`, `libk/src/rtti.c`, `libk/src/fatal.c`
- Thread/time: `thread.k`, `thread_exceptions.k`, `time.k`, `runtime/runtime_thread.c`, `runtime/thread_ffi.c`
- Futures: `future.k`, `runtime/future_state.c`, `runtime/future_ffi.c`
- Sync: `sync/*.k`, `runtime/sync_primitives.c`, `runtime/sync_ffi.c`
- Async file I/O: `io/file_channel.k`, `io/file_stream.k`, `runtime/async_io.c`, `runtime/async_ffi.c`
- Network I/O: `io/socket.k`, `io/network_address.k`, `runtime/network_ffi.c`

## 4.1 Investigation map (by symptom)

| Symptom | Start with | Then check |
|---------|------------|------------|
| Thread lifecycle/interrupt/join bug | `libk/src/thread.k` | `runtime/runtime_thread.c`, `runtime/thread_ffi.c` |
| Future/promise state transition bug | `libk/src/future.k` | `runtime/future_state.c`, `runtime/future_ffi.c` |
| Mutex/condition/semaphore/rwlock deadlock | `libk/src/sync/*.k` | `runtime/sync_primitives.c`, `runtime/sync_ffi.c` |
| Async file operation behavior mismatch | `libk/src/io/file_channel.k`, `file_stream.k` | `runtime/async_io.c`, `runtime/async_ffi.c`, `runtime/park_lot.c` |
| Socket connect/accept/read/write issue | `libk/src/io/socket.k` | `runtime/network_ffi.c` |
| String/collection semantic issue | `libk/src/string.k` and related collection modules | matching tests under `libk/tests/` |
| Exception dispatch mismatch | `libk/src/rtti.c`, `libk/src/fatal.c` | compiler throw/catch path in `klang/src/gen/gen_statements.cpp` |

---

## 5. Tests in this subtree

- `libk-tests`: functional stdlib tests.
- `libk-thread-io-tests`: thread/future/sync/file/network tests.
- `libk-perf-tests` (optional): stress/perf suite (`-DLIBK_PERF_TESTS=ON`).

```bash
# Build libk tests
cd cmake-build-debug && ninja -j3 libk-tests libk-thread-io-tests

# Run libk tests
cd cmake-build-debug && ctest -R "libk-tests|libk-thread-io-tests" --output-on-failure
```

Focused examples:
```bash
# Sync/thread-related only
cd cmake-build-debug && ./libk/libk-thread-io-tests "[sync]"

# Futures only
cd cmake-build-debug && ./libk/libk-thread-io-tests "[future]"
```
