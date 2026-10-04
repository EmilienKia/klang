# Documentation (`doc/`) — AI Agent Guide

Scope: language spec, KDI spec, stdlib references, and man pages.

---

## 1. What lives here

- `spec/language/`: grammar and language semantics.
- `spec/kdi/`: KDI format specification.
- `spec/stdlib/`: stdlib public/protected API docs.
- `man/`: `klangc` and `kditool` user manuals.
- `howtos/`: practical guides (compiler debugging, K program debugging, CMake integration).

## 1.1 Investigation map

| Need | Start with |
|------|------------|
| Language syntax update | `spec/language/grammar.ebnf` |
| Language semantics update | `spec/language/summary.md` + related section pages |
| KDI format/update | `spec/kdi/` |
| stdlib public API docs | `spec/stdlib/` |
| CLI options/behavior docs | `man/klangc.md`, `man/kdi.md` |
| Compiler debugging guide | `howtos/debug-klangc-compiler.md` |
| K program debugging guide | `howtos/debug-compiled-k-programs.md` |
| CMake build integration guide | `howtos/cmake-integration-k-projects.md` |
| Practical callback/Future/Sync file and TCP examples | `guides/io.md`, `samples/io/` |
| Proactive file/buffer API | `spec/stdlib/io-async.md`, `io-file.md`, `guides/file-system.md` |
| TCP/Unix stream and UDP/Unix datagram API | `spec/stdlib/io-network.md` |
| Executor leases and serial EventLoop | `spec/stdlib/executors.md` |
| Application shutdown before field destruction | `spec/stdlib/application.md`, `spec/language/basic/main.md` |

The obsolete Selector and async file stream adapters have no active API.
Synchronous generic/file streams remain separate. Distinguish optional
metadata/control worker fallback from the prohibited blocking transport fallback,
and buffer registration from actual fixed-opcode use. Source preparation or old
sample compilation must never be presented as a fresh acceptance run.

---

## 2. Mandatory sync rules

- If lexical/syntax/semantic language behavior changes, update:
  - `doc/spec/language/grammar.ebnf`
  - relevant pages under `doc/spec/language/`
- If KDI format behavior changes, update `doc/spec/kdi/`.
- If stdlib API changes (public/protected), update `doc/spec/stdlib/`.
- Keep implementation details/private members out of public stdlib spec pages.

---

## 3. Writing style

- Keep docs technical, concise, and version-consistent.
- Prefer normative wording for spec rules (`must`, `must not`, `may`).
- Keep examples valid against current compiler behavior.
- **Never expose local environment/agent constraints in documentation files (`doc/`):**
  - Do not mention machine-local resource caps (e.g., `-j3`, VM memory limits, agent concurrency budgets) in user-facing manuals, specs, or how-tos.
  - Document generic, standard commands (e.g. `cmake --build cmake-build-debug` or `ninja <target>`) in `doc/`.
  - Local execution constraints remain mandatory operational guardrails for AI agents when running builds/tests, but belong solely in agent guides (`AGENTS.md`, `.agents/`).
- When adding a new language feature, include at least:
  - grammar change (if applicable),
  - semantic rule text,
  - short positive and negative examples.

## 4. Update checklist

1. Update the relevant spec/man/howto page.
2. Verify examples still compile/run with current behavior.
3. Ensure no local machine/agent constraints (e.g. `-j3`, VM limits) leak into `doc/`.
4. If compiler/runtime behavior changed, ensure matching tests exist.
5. Keep wording normative and avoid implementation-only details in public docs.
