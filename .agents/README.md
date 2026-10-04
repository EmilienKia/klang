# `.agents` Catalog

This directory stores reusable, project-specific operational definitions:

- `agents/`: long-lived **agent profiles** (who does what, where to start, output contract).
- `skills/`: focused **task skills** (repeatable procedures/checklists).

## Layout

| Path | Role |
|------|------|
| `.agents/agents/*.md` | broad investigation/execution profiles |
| `.agents/skills/<skill-name>/SKILL.md` | Claude-style skills with metadata frontmatter |

## Conventions for new definitions

- Keep scope explicit (what is in/out).
- Include start files/commands first.
- Include invariants/guardrails that must not be violated.
- End with an output contract (what the result must contain).
- Prefer references to existing `AGENTS.md` and spec docs instead of duplicating long prose.

## Continuous improvement policy

- Agent and skill descriptions are intentionally evolutive.
- Update existing definitions whenever clearer wording, better routing, or lower token-cost guidance is identified.
- Add new agents/skills whenever recurring tasks are not well covered by the current catalog.
- When modifying a definition, keep backward-friendly naming unless a rename has strong justification.

## Bootstrapped definitions

### Agents
- `compiler-architecture-investigator.md`
- `libk-runtime-debugger.md`
- `kdi-interop-guardian.md`
- `test-distribution-optimizer.md`

### Skills
- `bug-triage-and-routing/SKILL.md`
- `regression-test-promotion/SKILL.md`
- `language-change-sync/SKILL.md`
- `test-timeout-budgeting/SKILL.md`
- `kdi-change-impact-checklist/SKILL.md`
- `klangc-compiler-debugging-gdb-lldb/SKILL.md`
- `k-program-debugging-gdb-lldb/SKILL.md`

## Current I/O routing

Use `libk/AGENTS.md` for the active proactor, buffer, executor and lifecycle
targets. File channels live in `libk/libk/src/io/file/file_channel.k`; stream
and datagram APIs live in `io/socket_channel.k`, `io/server_socket.k`,
`io/datagram_socket.k` and `io/network_address.k`. Their shared native engine
is `runtime/io_runtime.c` / `io_operation.c` / `io_ffi.c`, with bounded
resolution in `io_resolver.c`. Do not route new work to retired `socket.k`,
`network_ffi.c`, `async_io.[ch]`, `async_ffi.c`, Selector or async file streams.

Application exit ordering crosses `klang/src/gen/gen_unit.cpp` and
`libk/libk/src/runtime/application_ffi.c`; start there for normal/unwind
shutdown. Keep source-only restrictions scoped to their task and acceptance
separate from preparation. Runtime shutdown checks require fresh subprocesses.
