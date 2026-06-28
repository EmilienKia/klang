# Exception stack unwinding finalization

Temporary implementation plan for finalizing exception stack unwinding and RAII cleanup.

## Goals

- Ensure local destructors run during exception stack unwinding.
- Ensure owner variables and destructible parameters are cleaned up exactly once.
- Align normal-exit and exceptional-exit cleanup paths.
- Strengthen tests for unwind, finally, rethrow, and cross-frame cleanup.

## Planned steps

1. Baseline validation
   - Run targeted exception/unwinding tests.
   - Confirm the current failure surface before refactoring.

2. Cleanup design consolidation
   - Factor common cleanup emission helpers in `klang/src/gen/generators.hpp` and `klang/src/gen/gen_statements.cpp`.
   - Reuse the same cleanup logic for normal block exit, `return`, `break`, `continue`, and EH unwind.
   - Keep NRVO / named return / construction-flag behavior intact.

3. Function-level cleanup alignment
   - Reuse the same helpers for owner and by-value struct parameters.
   - Ensure function-level unwind cleanup matches fallthrough and explicit return cleanup.

4. Exception/finally edge cases
   - Verify `finally` emission remains ordered and single-shot on early exits.
   - Verify nested cleanup vs catch dispatch precedence stays correct.

5. Tests
   - Extend `klang/tests/test-gen-exceptions.cpp` with missing RAII/unwind cases.
   - Run targeted test binaries, then broader exception-related coverage.

6. Documentation / wrap-up
   - Update this file as steps complete.
   - Remove `IN-PROGRESS.md` once the work is done.

## Status

- [x] Create temporary progress file.
- [ ] Validate current exception/unwind baseline.
- [ ] Implement cleanup factorization.
- [ ] Align function-level cleanup.
- [ ] Add/adjust unwind regression tests.
- [ ] Run targeted and broader validation.
- [ ] Remove temporary progress file.

