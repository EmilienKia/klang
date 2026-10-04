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

# Compile sample executable
./cmake-build-debug/klang/klangc samples/fibo.k -o cmake-build-debug/fibo-sample

# Compile sample shared library
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

## 4. Investigation map

| Goal | Typical files |
|------|---------------|
| Arithmetic/control-flow demo | `samples/fibo.k`-like files |
| Library build/import demo | sample modules compiled with `--dyn-lib` / `--static-lib` |
| CLI behavior smoke check | minimal single-file sample used with `klangc` flags |
| File fully Future/Sync API | `samples/file_channel_fully.k` |
| TCP loopback Future/Sync API | `samples/stream_channel_loopback.k` |
| UDP packet metadata and source address | `samples/datagram_loopback.k` |

When adding a new sample:
- keep it focused on one language/library concept,
- keep it short enough for fast manual compilation,
- prefer deterministic output for easy quick checks.
