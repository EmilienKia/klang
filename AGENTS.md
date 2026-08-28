# Klang Repository — Root AI Agent Guide

This root file contains only cross-repository rules and routing.
For technical details, always read the closest local `AGENTS.md` first.

---

## 1. Local AGENTS routing (first-level)

| Area | Local guide | Scope |
|------|-------------|-------|
| `klang/` | `klang/AGENTS.md` | Compiler (AST/model/resolvers/codegen/tests) |
| `libk/` | `libk/AGENTS.md` | K standard library + runtime C bridges |
| `libkdi/` | `libkdi/AGENTS.md` | KDI DTO/JSON/CBOR + tooling tests |
| `doc/` | `doc/AGENTS.md` | Language/KDI/stdlib/man documentation |
| `samples/` | `samples/AGENTS.md` | Example K programs |
| `.agents/` | `.agents/README.md` | Reusable project-specific agent/skill definitions |

Rule: if a local guide exists in your working subtree, it is authoritative for that subtree.

---

## 1.1 Quick language brief (grammar + semantics)

For a fast understanding of K language rules **without reading the full spec**, start here:

- **Language summary (brief):** [`doc/spec/language/summary.md`](doc/spec/language/summary.md)
- **Authoritative grammar:** [`doc/spec/language/grammar.ebnf`](doc/spec/language/grammar.ebnf)

Usage guidance:
- Read `summary.md` first for the semantic overview and idioms.
- Open `grammar.ebnf` only when syntax precision is needed.

---

## 1.2 Reusable agent/skill catalog

Project-local reusable definitions live in:

- [`/.agents/README.md`](.agents/README.md)
- [`/.agents/agents/`](.agents/agents/)
- [`/.agents/skills/`](.agents/skills/)

Use them as ready-made operating profiles for common tasks (compiler investigation, libk runtime debugging, KDI interop checks, regression-test promotion, test timeout budgeting, spec synchronization).

---

## 2. Global hard rules

- Build directory is fixed: `cmake-build-debug/` only.
- VM resources are limited: use at most `-j3`, and run only one build/test process at a time.
- Never commit or push without explicit user confirmation.
- Keep source code content in English (identifiers, comments, diagnostics, test names).
- Do not add root-level CMake configuration in sub-project `CMakeLists.txt`:
  `find_package`, `CMAKE_CXX_STANDARD`, `enable_testing()`, `KLANG_*`.
- Do not duplicate LLVM contexts: `k::model::context` owns the unique `llvm::LLVMContext`.
- Keep the declaration pass -> implementation pass resolution flow intact.
- Base stdlib import is compiler-managed (`import k;` auto-injected outside module `k`).

---

## 3. Cross-cutting build/test commands

```bash
# Configure
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug

# Build all (resource-safe)
cd cmake-build-debug && ninja -j3

# Run all tests (resource-safe)
cd cmake-build-debug && ctest --output-on-failure
```

### 3.1 Scoped build/test policy

- If in a working session, only `libk` changes (`.c` and/or `.k`), then only recompile `libk` and run `libk` tests each time. No need to run all project tests (the compiler and kdi tools are unaffected).
```bash
# Build libk tests
cd cmake-build-debug && ninja -j3 libk-tests-rtti-exceptions libk-tests-strings libk-tests-types libk-tests-io libk-tests-collections-sequential libk-tests-collections-associative libk-thread-io-tests

# Run libk tests
cd cmake-build-debug && ctest -R "libk-tests|libk-thread-io-tests" --output-on-failure
```

---

## 4. Cross-cutting quality policy

- Every compiler/runtime bug fix must include a reproducing regression test.
- Any temporary repro snippet that covers regression-prone behavior must be promoted to a permanent test (unless already covered).
- Language semantics changes require spec updates under `doc/spec/language/`.
- KDI format changes require spec updates under `doc/spec/kdi/` (schema version remains `0.1` until formal stabilization).
