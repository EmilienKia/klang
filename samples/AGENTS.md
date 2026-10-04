# Samples (`samples/`) — AI Agent Guide

Scope: example K programs used for demos and quick manual checks.

---

## 1. Goals

- Keep samples small, readable, and representative.
- Prefer showcasing one concept per sample (or one coherent theme).
- Samples should compile with default local debug build tools.

---

## 2. Style and constraints

- Follow K source conventions:
  - `camelCase` functions/variables,
  - `PascalCase` types,
  - 4-space indentation.
- Do not add `import k;` (auto-imported).
- Avoid overly synthetic micro-optimizations; optimize for clarity.
- Keep dependencies explicit for optional modules (`import k::math;` etc.).

---

## 3. Useful check commands

```bash
# Build compiler
ninja -C cmake-build-debug -j3 klangc libk_stdlib

# Enable optional sample compilation (never execution)
cmake -S . -B cmake-build-debug -DKLANG_BUILD_SAMPLES=ON -DKLANG_BUILD_IO_SAMPLES=ON
ninja -C cmake-build-debug -j3 klang-samples
ninja -C cmake-build-debug -j3 klang-io-samples

# Compile sample executable
./cmake-build-debug/klang/klangc samples/fibo.k -o cmake-build-debug/fibo-sample

# Compile existing channel executables
./cmake-build-debug/klang/klangc samples/file_channel_fully.k -o cmake-build-debug/file-channel-fully-sample
./cmake-build-debug/klang/klangc samples/stream_channel_loopback.k -o cmake-build-debug/stream-channel-loopback-sample
./cmake-build-debug/klang/klangc samples/datagram_loopback.k -o cmake-build-debug/datagram-loopback-sample
```

The three channel samples use the current Future/Sync API. Recompile them
after generated-entry-point changes; older compilation is not a final replay.
The stream/datagram samples use ephemeral loopback endpoints. The file sample
still writes the fixed `/tmp/klang-file-channel-fully.bin` pathname: compile it
only, never execute it as an automated acceptance check. Safe execution would
require a separate sample change to use a caller-owned path under the build
subtree. Do not create another build directory for sample or packaging checks.

The master sample option defaults to OFF. Its IO sub-option defaults to ON and
is effective only when the master is ON; set IO OFF to build the four root
examples alone. `klang-samples` is an ALL target only when samples are enabled.
Root targets are `sample-fibo`, `sample-file-channel-fully`,
`sample-stream-channel-loopback` and `sample-datagram-loopback`.

`samples/io/` contains nine standalone read-only file/TCP programs:
`file_read_{proactor,future,sync}.k`,
`tcp_client_{proactor,future,sync}.k` and
`tcp_server_{proactor,future,sync}.k`. Compile each with `common.k`, in the same
`io_samples` module, never multiple main sources together. Individual targets
are `sample-io-file-read-<mode>`, `sample-io-tcp-client-<mode>` and
`sample-io-tcp-server-<mode>`; binaries are in `cmake-build-debug/samples/io/`.
Both native libk and KDI file dependencies must remain explicit in the custom
commands, as must the shared source dependency for every IO sample.

Use the [I/O guide](../doc/guides/io.md) for CLI contracts and safe execution
recipes. File readers require a caller-owned filepath and emit raw bytes.
Servers default to ephemeral loopback ports and announce `PORT=` before accept;
clients require that port. Each pair exchanges exactly four bytes once, with
bounded transport deadlines. Callback state must remain Application-owned
through the generated exit barrier; do not capture main-local resources or
replace the callback chain with Future/Sync waits.

## 4. Investigation map

| Goal | Typical files |
|------|---------------|
| Arithmetic/control-flow demo | `samples/fibo.k`-like files |
| Library build/import demo | sample modules compiled with `--dyn-lib` / `--static-lib` |
| CLI behavior smoke check | minimal single-file sample used with `klangc` flags |
| File fully Future/Sync API | `samples/file_channel_fully.k` |
| TCP loopback Future/Sync API | `samples/stream_channel_loopback.k` |
| UDP packet metadata and source address | `samples/datagram_loopback.k` |
| Callback/Future/Sync file reader and TCP pairs | `samples/io/*.k`, `doc/guides/io.md` |

When adding a new sample:
- keep it focused on one language/library concept,
- keep it short enough for fast manual compilation,
- prefer deterministic output for easy quick checks.
