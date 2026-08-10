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
4. Keep naming stable and explicit.
5. Set/adjust per-target timeout in CMake.

## Heuristics
- Prefer 40-90s target runtime per executable.
- Keep hard limit at current policy (120s).
- Do not run multiple heavy test suites concurrently on this VM.

## Deliverable
- Updated grouping plan + CMake changes + measured impact summary.

