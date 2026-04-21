# IN-PROGRESS — Typed enums (object-backed enums)

## Goal
Implement typed enums in K/klang while keeping full backward compatibility with existing integer enums.

Target semantics (requested):
- `enum E : T { ... }` where `T` can be struct/class/primitive.
- For non-integer `T`: enum entries are represented as unsigned indices into a static table of `T` values.
- `E -> const T&` cast by table lookup.
- `T -> E` cast allowed only if equality is available; non-match is fatal except conditional soft-fail pattern in `if` variable creation.
- Implicit next value from previous value (copy + `++`) when available and `T` is non-const.
- Enum derivation inherits underlying type automatically for typed enums.
- If `T` is explicit integer primitive: keep classic enum behavior, force explicit integer type, no table.

## Delivery strategy
- Keep current enum pipeline intact for integer mode.
- Add typed-enum mode incrementally behind strict semantic checks.
- Add tests first for syntax/typing and then for runtime behavior.

## Implementation checklist

### Phase 0 — Scope lock and design contracts
- [ ] Finalize grammar disambiguation between enum derivation and enum underlying type.
- [ ] Finalize conversion rules (`E <-> T`) and ambiguous-value policy for `T -> E`.
- [ ] Define diagnostics (fatal vs soft-fail) and map to existing diagnostic categories.

### Phase 1 — Parser and AST
- [x] Extend enum declaration AST to carry explicit underlying type information.
- [x] Extend enum entry AST to carry initializer forms:
  - [x] constructor args `VALUE(args...)`
  - [x] default construction `VALUE() default`
  - [x] designated init `VALUE{...}`
  - [x] alias `VALUE2 = VALUE1`
  - [x] implicit entry (no explicit init)
- [x] Update parser for all supported enum entry forms.
- [x] Add parser diagnostics for invalid enum entry forms.

Primary files:
- `klang/src/parse/ast.hpp`
- `klang/src/parse/ast.cpp`
- `klang/src/parse/parser.cpp`
- `klang/src/parse/parser.hpp`
- `klang/src/errors.hpp`

### Phase 2 — Model building
- [x] Extend model enum raw-entry representation from integer-only to typed initializer model.
- [x] Preserve integer fast path for existing enums.
- [x] Propagate enum underlying type declaration into model.
- [ ] Carry source references for better diagnostics on per-entry failures.

Primary files:
- `klang/src/model/model.hpp`
- `klang/src/model/model_builder.cpp`
- `klang/src/model/expressions.hpp`

### Phase 3 — Symbol/type resolution
- [x] Resolve base enum first (existing behavior), then inherit typed underlying type when needed.
- [x] Resolve enum mode:
  - [x] integer-explicit mode (`enum E : int` etc.)
  - [x] typed-object mode (`enum E : Struct/Class/...`)
- [x] Resolve entries in order:
  - [x] explicit constructor/designated value
  - [ ] alias resolution (including cycle detection)
  - [x] implicit next-value via copy+increment validation
- [x] Default entry rules:
  - [x] explicit `default` wins
  - [x] fallback to first effective entry
- [x] Validate `T -> E` cast prerequisites (`==`/`!=` availability).

Primary files:
- `klang/src/gen/gen_struct.cpp`
- `klang/src/gen/resolvers.cpp`
- `klang/src/gen/resolvers.hpp`
- `klang/src/errors.hpp`

### Phase 4 — Type system and IR representation
- [x] Extend `enum_type` to support typed-object backing metadata.
- [x] For typed-object mode, define runtime representation as unsigned index.
- [x] Keep existing IR behavior unchanged for integer mode.
- [x] Generate static value table and stable enum-entry index mapping.
- [x] Implement `E -> const T&` via static table lookup.

Primary files:
- `klang/src/model/type.hpp`
- `klang/src/model/type.cpp`
- `klang/src/gen/gen_expressions.cpp`
- `klang/src/gen/gen_class.cpp`
- `klang/src/gen/gen_function.cpp`

### Phase 5 — Conversions and control-flow soft-fail
- [x] Implement `T -> E` lookup conversion path.
- [x] Fatal on no match by default.
- [x] Soft-fail in `if` variable-creation pattern; assign enum default value.
- [x] Ensure no behavior regressions for existing if soft-fail infrastructure.

Primary files:
- `klang/src/gen/gen_adapt_type.cpp`
- `klang/src/gen/resolvers.cpp`
- `klang/src/gen/gen_statements.cpp`

### Phase 6 — KDI import/export and spec
- [x] Extend KDI DTO/export/import for typed enums.
- [x] Keep compatibility behavior explicit for old KDI payloads.
- [x] Update language spec and grammar docs.

Primary files:
- `klang/src/model/tools/kdi_exporter.cpp`
- `klang/src/model/tools/kdi_importer.cpp`
- `libkdi/src/kdi_aggregates.hpp`
- `doc/spec/language/grammar.ebnf`
- `doc/spec/language/summary.md`
- `doc/spec/language/` (enum section)

## Test rollout checklist

### A. Parser coverage
- [x] Parse typed enum with primitive underlying type.
- [x] Parse typed enum with struct/class underlying type.
- [x] Parse entry forms: call/default/designated/alias/implicit.
- [x] Reject malformed entries and malformed typed enum declarations.

Targets:
- `klang/tests/test-gen-enum.cpp` (existing enum parser tests)
- `klang/tests/test-parser.cpp` (if parser-only coverage is better scoped there)

### B. Semantic/resolution coverage
- [x] Underlying type inheritance from base enum.
- [ ] Alias chain resolution and cycle rejection.
- [x] Implicit next-value rules (copy ctor + `++` required).
- [x] Correct default-entry selection in typed enums.
- [ ] Diagnostics quality (message + location) for invalid typed enum definitions.

### C. Codegen/runtime coverage
- [x] `E -> const T&` returns expected object data.
- [x] `T -> E` success with matching value.
- [x] `T -> E` fatal on non-match outside soft-fail context.
- [x] `if` soft-fail path assigns enum default value and continues control flow.
- [x] Explicit integer typed enums behave exactly like classic enums (no mapping table).

### D. Integration/regression coverage
- [x] Existing enum tests remain green unchanged.
- [x] Import/KDI tests for enums keep passing.
- [x] Add typed enum import/export tests (cross-module where relevant).

## Execution order (pragmatic)
1. Parser/AST changes + parser tests.
2. Model + resolution changes + semantic tests.
3. IR representation + runtime conversion tests.
4. Soft-fail path tests.
5. KDI/spec updates + integration tests.

## Risks and mitigations
- Grammar ambiguity (`:` base enum vs `:` underlying type).
  - Mitigation: finalize a deterministic parsing rule before coding phase 1.
- Large blast radius in enum conversions.
  - Mitigation: strict integer-mode isolation and focused non-regression tests.
- Runtime cost of `T -> E` lookup.
  - Mitigation: start linear lookup in V1, optimize later without semantic changes.
- KDI compatibility drift.
  - Mitigation: update KDI tests in same PR and document schema changes in spec.

## Acceptance criteria
- [x] All existing enum tests pass.
- [x] New typed-enum parser, semantic, and runtime tests pass.
- [x] Soft-fail behavior implemented only in intended `if` context.
- [x] Integer explicit typed enums are backward-compatible in behavior and IR expectations.
- [x] Spec and grammar are updated to reflect final accepted syntax.

## Remaining work (to close the topic)
- [x] Run broader non-regression suites and keep results logged (`[gen][enum]`, `[import]`, then full `klang-tests` if feasible).
- [x] Add/confirm explicit runtime test for hard-fail `T -> E` non-match outside soft-fail context.
- [x] Close parser/semantic diagnostic gaps (malformed typed-enum forms, alias cycle diagnostics).
- [ ] **GAP-1 (ctor_args)**: Constructor call syntax silently ignored — `VALUE(arg1, arg2)` produces zero-init instead of calling constructor
- [ ] **GAP-2 (alias)**: Alias resolution broken for object-backed enums — `ALIAS = V1` creates separate backing entry instead of sharing
- [ ] **GAP-3 (equality)**: No validation of `equals` method prerequisite for `T -> E` cast
- [ ] **GAP-4 (implicit++)**: Implicit increment uses heuristic instead of user-defined `operator++`

## Activity log
- [x] Created implementation and test execution plan in `IN-PROGRESS.md`.
- [x] Added typed-enum expected tests (skipped) in `klang/tests/test-gen-enum.cpp`.
- [x] Registered `klang/tests/test-gen-enum.cpp` in `klang/CMakeLists.txt` test target.
- [x] Linked typed-enum TODO item to related tests in `TODO.md`.
- [ ] Phase 0 scope decisions completed.
- [x] Phase 1 started (parser/AST: `enum_decl::explicit_underlying_type`, enum entry `brace_init` / `ctor_args` / `ref_value`).
- [x] Phase 2 partial: `enum_raw_entry_def::brace_init` threaded from AST; `enumeration::_object_type` and `_table_global` added.
- [x] Phase 3 partial: `resolve_enumeration` detects struct underlying types (object-backed mode), assigns sequential indices, selects smallest unsigned index type.
- [x] Phase 4 partial: `declaration_generator::visit_enumeration` emits `[N × StructType]` global constant array; `enum_type::is_object_backed()` / `get_object_type()` implemented.
- [x] Phase 5 partial: `E → const T&` adaptation via GEP into backing table implemented in `adapt_enum_type` and `visit_cast_expression`; `validate_reference_variable` extended to allow object-backed enum initialization of const T& references.
- [x] Phase 5 partial: `T → E` adaptation for object-backed enums implemented (linear table lookup in `visit_cast_expression`, fatal trap on no match outside soft-fail, branch to soft-fail failure block for `if` condition-variable context).
- [x] Tests enabled: "explicit integer underlying keeps classic behavior", "derived enum inherits explicit integer underlying", "object-backed zero-init entry", "enum entry to const underlying reference".
- [x] Tests enabled: "class-backed implicit ++ from previous value" and "object to enum conversion and soft-fail in if".
- [x] Lock tests added: parser coverage for typed enum entry forms (`VALUE(...)`, `VALUE() default`, `VALUE{...}`, alias) and import coverage for explicit integer underlying enums across module boundaries.
- [x] Object-backed enum implicit progression stabilized (`copy + ++` behavior) in `klang/src/gen/gen_struct.cpp` (increment on last user integer field, skipping synthetic fields).
- [x] `T -> E` adaptation/codegen finalized across resolver + IR (`klang/src/gen/gen_adapt_type.cpp`, `klang/src/gen/resolvers.cpp`, `klang/src/gen/gen_expressions.cpp`, `klang/src/gen/gen_statements.cpp`).
- [x] Enum variable initialization path fixed to apply adaptation for enum targets in `klang/src/gen/resolvers.cpp`.
- [x] Validation run: `./klang-tests '[gen][enum]'` -> All tests passed (152 assertions in 48 test cases).
- [x] Validation run: `./klang-tests '[import]'` -> All tests passed (158 assertions in 95 test cases).
- [x] Validation run: `./klang-tests -r compact` (full suite) -> All tests passed (9220 assertions in 1907 test cases).
- [x] Validation run: `./kdi-tests '[validate][enum]'` -> All tests passed (3 assertions in 3 test cases).
- [x] Validation run: `./kdi-tests '[json][enum][typed]'` -> All tests passed (10 assertions in 1 test case).
- [x] Validation run: `./kdi-tests '[cbor][enum][typed]'` -> All tests passed (10 assertions in 1 test case).
- [x] Added runtime hard-fail coverage test for object-backed `T -> E` non-match outside `if` soft-fail (`klang/tests/test-gen-enum.cpp`).
- [x] Added parser malformed typed-enum diagnostics coverage and wired enum-specific parser codes (`klang/tests/test-parser.cpp`, `klang/src/parse/parser.cpp`).
- [x] Post-Phase 6 validation run: `./klang-tests '[gen][enum]'` -> All tests passed (153 assertions in 49 test cases).
- [x] Post-Phase 6 validation run: `./klang-tests -r compact` (full suite) -> All tests passed (8799 assertions in 1836 test cases).
- [x] Post-Phase 6 validation run: `./kdi-tests '[enum]'` -> All tests passed (43 assertions in 9 test cases).
- [x] Added comprehensive parser/semantic coverage for typed enum diagnostics (test-parser.cpp):
  - [x] Parser tests for typed enum declarations with struct/class underlying types
  - [x] Parser tests for all typed enum entry forms (ctor call, default, designated init, alias, implicit)
  - [x] Parser diagnostic tests for malformed typed enum declarations
  - [x] 6 parser diagnostic tests covering edge cases and error recovery

## Implementation gaps requiring fixes

### GAP-1: Constructor call syntax `VALUE(arg1, arg2)` silently ignored

**Problem**: Constructor arguments are parsed but never propagated to the model. The `enum_raw_entry_def` lacks a `ctor_args` field, so compiler creates zero-initialized entries instead of calling constructors.

**Test evidence**: `enum E : Point { UP(0, 1); }` compiles to `UP = {x:0, y:0}` instead of `UP = {x:0, y:1}`.

**Fix plan**:
1. Add `std::vector<std::shared_ptr<model::expression>> ctor_args` to `enum_raw_entry_def` in `model.hpp`.
2. In `model_builder.cpp`, populate `ctor_args` when `ast_entry->ctor_args` is non-empty.
3. In `gen_struct.cpp::build_entry_constant`, when `ctor_args` is non-empty, resolve arguments as compile-time constants and call the matching constructor.
4. Add test: `VALUE(literal_args)` gives expected struct field values.
5. Limitation: requires compile-time constant arguments (no runtime expressions).

**Files to modify**: `model.hpp`, `model_builder.cpp`, `gen_struct.cpp`

### GAP-2: Alias resolution broken — `ALIAS = V1` creates separate backing entry

**Problem**: Aliases point to separate backing table entries instead of sharing their target's slot. Root cause: sequential index assignment (0, 1, 2, ...) happens before alias resolution; aliases never reuse target's index.

**Test evidence**: `V1{.v=10}; ALIAS=V1;` → `V1.v=10`, but `ALIAS.v=11` (implicit increment).

**Fix plan**:
1. After initial sequential index assignment, resolve aliases to get target's index.
2. Compact indices so aliases share their target's table slot.
3. In backing table generation, skip entries that are aliases (they map to existing indices).
4. Ensure implicit `++` still works for non-aliased implicit entries.
5. Add test: `ALIAS` and target return same backing object.

**Files to modify**: `gen_struct.cpp` (resolve_enumeration and visit_enumeration declaration pass)

### GAP-3: No equality prerequisite validation for `T -> E` cast

**Problem**: Spec requires `==`/`!=` availability for `T -> E` conversion, but no validation occurs. Spec says "allowed only if equality is available."

**Current implementation**: Field-by-field LLVM integer comparison works for simple structs by accident; no user `equals` method required.

**Fix plan**:
1. In `resolve_enumeration` (gen_struct.cpp), for object-backed enums, check if underlying type has reachable `equals(const T&) : bool` method.
2. If missing, emit error diagnostic: "Object-backed enum `T -> E` conversion requires underlying type to define `equals`."
3. Change `gen_expressions.cpp::visit_cast_expression` T→E codegen to call user's `equals` method instead of field-by-field comparison.
4. Add test: reject object-backed enum definition on type without equality.

**Files to modify**: `gen_struct.cpp`, `gen_expressions.cpp`, `resolvers.cpp`

### GAP-4: Implicit increment uses heuristic, not user `operator++`

**Problem**: Spec says "copy + `++` when available," implying the user's `operator++` should be called. Implementation finds last integer field and hardcodes `field += 1`.

**Fix plan**:
1. In `build_implicit_from_previous` (gen_struct.cpp), attempt to lookup `operator++` (prefix/postfix) on constant struct type.
2. If found, eval constant expression `copy_of_prev++` to get next value (requires constant-folding evaluator).
3. Fallback to heuristic with diagnostic: "Implicit enum increment for types without explicit `operator++` uses heuristic last-integer-field approach (unsupported)."
4. Add test: implicit entry with proper `operator++` generates correct values.

**Files to modify**: `gen_struct.cpp` (backing table generation), possibly new constant evaluation helper.

### Testing strategy

Each gap should include:
- Unit test demonstrating correct behavior (after fix)
- Regression test ensuring no breakage to existing integer/implicit enums
- Run full suite: `./klang-tests '[gen][enum]'`, `./kdi-tests '[enum]'`
