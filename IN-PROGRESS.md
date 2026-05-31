# IN-PROGRESS — DWARF debug support for `klangc`

This file is a temporary implementation log. It must be kept in sync with the current phase and removed once the DWARF debug feature is fully implemented, tested, and stable.

## Goal

Add end-to-end DWARF debug information support to `klangc`, from CLI options to LLVM IR metadata, linking, and regression tests.

## Current status

- Phase 1: completed
- Phase 2: completed
- Phase 3: completed
- Phase 4: in progress

## Phase plan

### Phase 1 — CLI and compiler configuration plumbing

Status: completed

Scope:
- Add `-g` / `--debug` CLI switches.
- Add `--gline-tables-only`.
- Add `--gdwarf-4` and `--gdwarf-5`.
- Validate mutually exclusive DWARF version options.
- Store debug options in `k::compiler`.
- Expose source location lookup from lexemes for later LLVM metadata generation.

Validation:
- Build `klangc` and `klangc-tests`.
- Confirm the CLI accepts the new flags.

### Phase 2 — LLVM DWARF bootstrap

Status: completed

Scope:
- Introduce a debug metadata emitter around LLVM `DIBuilder`.
- Create a compile unit and file metadata.
- Attach subprogram metadata to generated LLVM functions.
- Emit instruction debug locations from source lexemes.
- Finalize metadata after code generation.

Validation:
- Build the compiler and tests.
- Run focused `klangc` debug tests that verify DWARF sections are present in produced binaries.

### Phase 3 — Link pipeline propagation and integration tests

Status: completed

Scope:
- Propagate debug mode to the final link step.
- Ensure executables and shared libraries keep debug sections.
- Add end-to-end tests for both executable and shared-library outputs.

Validation:
- Compile a sample executable with `-g`.
- Compile a shared library with `--dyn-lib -g`.
- Verify `.debug_info` and `.debug_line` are present in the produced ELF files.

### Phase 4 — Lexical scopes and local variable debug info

Status: in progress

Scope:
- Emit nested lexical scopes for statement blocks.
- Track source locations for statement boundaries more precisely.
- Emit parameter debug declarations in function bodies.
- Emit local variable debug declarations with `llvm.dbg.declare`.
- Keep debug scope state aligned with nested blocks, loops, and exception bodies.

Validation:
- Build `klangc` and `klangc-tests`.
- Run regression tests for functions with local variables, loops, and nested blocks.
- Inspect emitted DWARF with `readelf` or `llvm-dwarfdump` to confirm local variables and scopes are visible.

### Phase 5 — Cleanup and final verification

Status: pending

Scope:
- Fix any remaining DWARF gaps or incorrect locations.
- Remove this temporary file.
- Confirm the entire feature is stable across the existing test suite.

Validation:
- Run the relevant `klangc-tests` categories.
- Run the full test suite if practical.
- Remove `IN-PROGRESS.md` only after the implementation is complete.

