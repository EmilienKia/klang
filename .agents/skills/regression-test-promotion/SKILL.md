---
description: Convert temporary repros into permanent, categorized regression tests.
---

# Regression Test Promotion

## Goal
Convert temporary repros into permanent, categorized regression tests.

## Procedure
1. Identify the closest existing test group/file.
2. Promote minimal repro into an expressive named test case.
3. Keep test deterministic and isolated.
4. Register new file in CMake only when no suitable file exists.
5. Remove temporary repro artifacts once promoted.

## Rules
- Every non-trivial bug fix should land with a reproducing test.
- Prefer strengthening tests to catch deeper regressions, not only surface symptoms.

## Deliverable
- Permanent test location + short mapping from original repro to final test.

