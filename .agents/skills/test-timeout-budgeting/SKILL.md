---
description: Rebalance test executables to stay within timeout budgets by category.
---

# Test Timeout Budgeting

## Goal
Maintain category-based test executables that respect runtime limits on constrained environments.

## Procedure
1. Measure current runtime of candidate executables.
2. Detect groups approaching/exceeding budget.
3. Split by coherent category (not random distribution).
4. If one source file remains too heavy, shard execution at test-run level instead of forcing artificial file splits.
5. Keep naming stable and explicit.
6. Set/adjust per-target timeout in CMake.

## Sharding strategy (Catch2)

When a single executable cannot be cleanly split by source files:
- register multiple CTest entries for the same binary using Catch2 sharding:
  - `--shard-count N`
  - `--shard-index i`
- example:
  - `add_test(NAME my-tests-shard-0 COMMAND my-tests --shard-count 2 --shard-index 0)`
  - `add_test(NAME my-tests-shard-1 COMMAND my-tests --shard-count 2 --shard-index 1)`

Use this for large suites such as arithmetic/import/type matrices.

## Heuristics
- Prefer 40-90s target runtime per executable.
- Keep hard limit at current policy (120s).
- Do not run multiple heavy test suites concurrently on this VM.

## Deliverable
- Updated grouping plan + CMake changes + measured impact summary.
