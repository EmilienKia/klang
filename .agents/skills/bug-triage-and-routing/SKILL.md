---
description: Route a bug quickly to the correct subsystem and first investigation files.
---

# Bug Triage and Routing

## Goal
Route a bug to the right subsystem quickly before deep investigation.

## Procedure
1. Classify symptom: parse, resolution, codegen, runtime, interop, docs.
2. Map to owning subtree:
   - compiler -> `klang/`
   - stdlib/runtime -> `libk/`
   - KDI -> `libkdi/`
   - specification/docs -> `doc/`
3. Pick smallest relevant test executable for reproduction.
4. Record likely boundary crossing (e.g., compiler <-> libk, compiler <-> libkdi).

## Fast-path for fatal crashes

- If failure is `SIGSEGV` in compiler tests:
  1. Re-run the single failing test case by name/wildcard.
  2. Capture a debugger backtrace (`gdb -batch -ex run -ex bt --args ...`).
  3. If the backtrace shows thousands of repeated parser frames, suspect a parser branch that returns without consuming input (infinite recursion / stack overflow).
  4. Inspect parser lookahead heuristics first (especially in `parse_primary_expr` helpers).

## Deliverable
- One-sentence classification + exact first files to inspect + first test command.
