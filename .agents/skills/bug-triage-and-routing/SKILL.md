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

## Deliverable
- One-sentence classification + exact first files to inspect + first test command.

