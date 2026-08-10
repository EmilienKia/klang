---
description: Keep compiler behavior, tests, and language spec/docs synchronized.
---

# Language Change Sync

## Goal
Keep compiler behavior, tests, and language documentation synchronized when language semantics/syntax evolve.

## Procedure
1. Implement behavior change in compiler (`klang/`).
2. Add/adjust tests for positive and negative paths.
3. Update language brief/spec:
   - `doc/spec/language/summary.md`
   - `doc/spec/language/grammar.ebnf` (if grammar changed)
4. Verify examples remain valid (or update them).

## Deliverable
- Patch set including compiler change + tests + matching docs update.

