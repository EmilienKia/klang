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
cd cmake-build-debug && ninja -j3 klangc

# Compile sample executable
./cmake-build-debug/klang/klangc samples/fibo.k -o fibo

# Compile sample shared library
./cmake-build-debug/klang/klangc --dyn-lib samples/mylib.k
```

## 4. Investigation map

| Goal | Typical files |
|------|---------------|
| Arithmetic/control-flow demo | `samples/fibo.k`-like files |
| Library build/import demo | sample modules compiled with `--dyn-lib` / `--static-lib` |
| CLI behavior smoke check | minimal single-file sample used with `klangc` flags |

When adding a new sample:
- keep it focused on one language/library concept,
- keep it short enough for fast manual compilation,
- prefer deterministic output for easy quick checks.
