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
