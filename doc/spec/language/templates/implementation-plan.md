# Templates — Implementation & Test Plan

> **Scope:** Phase 1 — full explicit instantiation, no specialization.
> **Date:** 2026-04-09

---

## Implementation Steps

The implementation is organized in **8 milestones**, each building on the previous one.
Each milestone ends with a set of passing tests that validates the work done so far.

---

### Milestone 1 — Lexer & Grammar Foundation ✅ DONE

**Goal:** The lexer recognizes `template` and `typename` as keywords. The grammar spec
is updated. No functional change in the compiler yet.

**Steps:**
1. Add `TEMPLATE` and `TYPENAME` to `keyword::type_t` in `klang/src/lex/lexemes.hpp`.
2. Add `"template"` and `"typename"` to `keyword_set` in `klang/src/lex/lexemes.cpp`.
3. Update `doc/spec/language/grammar.ebnf`:
   - Add `'template'` and `'typename'` to the `Keyword` rule.
   - Add `TemplateDeclaration`, `TemplateParameterList`, `TemplateParameter`,
     `TemplateParameterKind` rules.
   - Add `TemplateArgList`, `TemplateArg` rules.
   - Update `AggregateDecl` and `FunctionDecl` with optional `TemplateDeclaration`.
   - Update `QualifiedIdentifier` to support per-segment template arguments
     (`IdentifierSegment`).

**Tests (Milestone 1):**
- `test-lexer.cpp`: verify `template` and `typename` are tokenized as keywords.
- Ensure existing test suite still passes (no regressions).

**Estimated effort:** Small (1-2 hours).

---

### Milestone 2 — AST Nodes & Parser ✅ DONE

**Goal:** The parser can parse template declarations and template argument lists.
Templates are represented in the AST but have no semantic effect yet.

**Steps:**
1. **New AST nodes** in `klang/src/parse/ast.hpp`:
   - `template_parameter`: kind_kw, name, constraint_type, default_expr.
   - `template_arg`: type_arg or value_arg.
2. **Modify existing AST nodes**:
   - `aggregate_decl`: add `template_params` vector.
   - `function_decl`: add `template_params` vector.
   - `identified_type_specifier`: add `template_args` vector.
   - `qualified_identifier`: add `template_args_per_segment` (parallel to `names`).
3. **Update `ast_visitor`** and `default_ast_visitor` with new `visit_*` methods.
4. **Parser** (`klang/src/parse/parser.hpp` and `parser.cpp`):
   - Implement `parse_template_declaration()`.
   - Implement `parse_template_parameter()`.
   - Implement `parse_template_arg_list()` with angle-bracket disambiguation
     (tentative parse with save/restore).
   - Modify `parse_aggregate_decl()` to check for `template` keyword before specifiers.
   - Modify `parse_function_decl()` similarly.
   - Modify `parse_type_specifier()` / `parse_identified_type_specifier()` to parse
     template arguments after qualified identifiers.
5. **Add to `klang/CMakeLists.txt`** any new `.cpp` files.

**Tests (Milestone 2):**
- `test-parser.cpp` (or new `test-parser-templates.cpp`):
  - Parse `template<typename T> struct Foo {}` and verify AST.
  - Parse `template<typename T> swap(a: T+, b: T+) {}` and verify AST.
  - Parse `Pair<int>` as a type specifier.
  - Parse `template<typename T, N : unsigned int = 10> struct Arr {}`.
  - Parse `template<class T : Base> doSomething(t: T&) : int {}`.
  - Verify `<` is correctly disambiguated as comparison vs template args.
  - Verify `>>` splitting for nested templates: `Pair<Pair<int>>`.

**Estimated effort:** Medium-Large (6-10 hours). Parser disambiguation is the trickiest part.

---

### Milestone 3 — Model Layer & Template Definitions ✅ DONE

**Status:** Completed. All tests passing (14 test cases, 77 assertions).

**Goal:** The model builder creates `tpl_info` for template declarations. Template
aggregates and functions are recognized in the model but not yet instantiable.

**Steps:**
1. **New header** `klang/src/model/template.hpp`:
   - `template_param_descriptor` (kind, name, constraint, defaults).
   - `template_argument` (type_arg or value_arg).
   - `template_info` (params, original_ast, instantiations map).
   - `instantiation_info` (template_def, args).
2. **Modify** `klang/src/model/model.hpp`:
   - Add `std::unique_ptr<template_info> _template_info` to `aggregate` and `function`.
   - Add `std::unique_ptr<instantiation_info> _instantiation_info` to both.
   - Add `bool is_template()` / `bool is_template_instantiation()` helpers.
   - Add instantiation registry to `unit`.
3. **Model builder** (`model_builder.cpp`):
   - When `aggregate_decl::is_template()`, build `template_info`, store AST,
     skip member processing.
   - Same for `function_decl`.
4. **Resolution passes**: Skip template definitions (not yet instantiated) in
   `symbol_resolver`, `aggregate_type_resolver`, `type_reference_resolver`,
   `declaration_generator`, `implementation_generator`.

**Tests (Milestone 3):**
- `test-gen-template-functions.cpp` (skeleton):
  - Verify that a template function definition is parsed and model-built without error.
  - Verify it is marked `is_template()`.
  - Verify it is NOT emitted as LLVM IR (no instantiation yet).
- `test-gen-template-aggregates.cpp` (skeleton):
  - Same for template struct.

**Estimated effort:** Medium (4-6 hours).

---

### Milestone 4 — Template Instantiator (model-level) ✅ DONE

**Status:** Completed. All tests passing.

**Goal:** The core instantiation engine works at the model level. Given a template
definition (whose members are already built with unresolved_type placeholders) and
concrete arguments, it clones model nodes and substitutes types directly — no AST
cloning or re-parsing is needed.

**Steps:**
1. **Template Instantiator** (`klang/src/model/template_instantiator.hpp/.cpp`):
   - `instantiate_aggregate(tpl_def, args, parent_ns, unit, ctx, logger)`:
     a. Build a type substitution map from template params → concrete types.
     b. Create a new concrete aggregate in the parent namespace.
     c. Clone member variables, methods, constructors, destructors from the
        template definition, substituting types.
   - `instantiate_function(tpl_def, args, parent_ns, unit, ctx, logger)`:
     a. Same substitution approach for free functions.
     b. Clone parameters, return type, and body with type substitution.
   - Expression and statement cloning with recursive type substitution.
2. **Instantiation cache**: `tpl_info::instantiations` map ensures the same
   argument combination is not instantiated twice.
3. **Name generation**: Concrete instantiations get a name that encodes the arguments
   (e.g., `Box__int` internally).

**Tests (Milestone 4):**
- Instantiate a template struct with `T=int`, verify concrete aggregate and members.
- Instantiate a template function with `T=int`, verify concrete function, params, body.
- Cache hit: same args → same instance; different args → different instances.
- Name helpers: verify key and name generation.

---

### Milestone 5 — Name Mangling & Symbol Resolution Integration

**Goal:** Template instantiations have correct mangled names. The symbol resolver can
find templates and trigger instantiation.

**Steps:**
1. **Mangler** (`mangler.hpp/.cpp`):
   - Implement `mangle_template_args()`.
   - Integrate into `mangle_function()`, `mangle_constructor()`, `mangle_structure()`.
   - For instantiated entities, the mangled name includes `I...E`.
2. **Symbol resolver** (`resolvers.cpp`):
   - When resolving a name with template arguments, look up the template definition.
   - Validate arguments against parameters (kind, constraint, type).
   - Call `unit::instantiate_*_template()`.
   - Return the concrete entity.
3. **scope_lookup** additions:
   - `resolve_template_aggregate()`, `resolve_template_function()`.
4. **Aggregate type resolver**: When resolving a `QualifiedIdentifier` with template
   args (e.g., field type `Pair<int>`), trigger instantiation.
5. **Type reference resolver**: When encountering `swap<int>(a, b)`, trigger function
   template instantiation.

**Tests (Milestone 5):**
- `test-gen-template-mangling.cpp`:
  - Verify mangled names: `Pair<int>`, `Pair<unsigned long>`, `Array<int, 10>`,
    `swap<float>`.
- `test-gen-template-functions.cpp`:
  - Template function `max<int>` called with int args -> correct result via JIT.
  - Template function `swap<int>` modifying values -> verify via JIT.
  - Two instantiations of the same template with different types: `max<int>` and
    `max<float>`.
- `test-gen-template-aggregates.cpp`:
  - Template struct `Pair<int>` instantiated, fields accessed.
  - Template struct with methods: `Pair<int>.getFirst()`.

**Estimated effort:** Large (8-12 hours). Integration across multiple passes.

---

### Milestone 6 — Code Generation & Advanced Features

**Goal:** Template instantiations produce correct LLVM IR. Value parameters,
constraints, defaults work.

**Steps:**
1. **Declaration generator**: Emit template instantiations with `WeakODRLinkage`.
2. **Implementation generator**: No special handling needed (concrete entities).
3. **Pipeline ordering**: Implement pending instantiation queue. After each phase,
   drain the queue through all phases up to current.
4. **Value parameters**: During substitution, replace identifier_expr matching a value
   parameter with a literal_expr of the concrete value. The value must be a constant
   expression evaluable at compile time. (For Phase 1, only literal constants and
   simple constant expressions are supported.)
5. **Type constraints**: During argument validation, check:
   - Kind filter: `struct` -> must be a structure, `class` -> must be a class, etc.
   - Base-type constraint: the argument type must be or derive from the constraint type.
6. **Default parameters**: When fewer arguments than parameters are provided, fill
   from defaults (right to left). Error if a non-defaulted parameter is missing.
7. **Error reporting**: Implement error codes 0x7001-0x7017.

**Tests (Milestone 6):**
- `test-gen-template-value-params.cpp`:
  - `FixedArray<int, 5>`: struct with sized array field parameterized by N.
  - `FixedArray<int, 5>.size()` returns 5.
  - Value param with default: `template<typename T, N : unsigned int = 3>`.
- `test-gen-template-constraints.cpp`:
  - `template<struct S> process(s: S&)` — accepts structs, rejects classes.
  - `template<class T : Animal> feed(t: T+)` — accepts Animal subclasses.
  - Error case: constraint violation.
- `test-gen-template-defaults.cpp`:
  - Default type parameter: `template<typename T = int>`.
  - Instantiation with omitted args uses default.
  - Default falls back to constraint type if no explicit default.
- `test-gen-template-errors.cpp`:
  - Too many arguments -> error 0x7010.
  - Too few arguments (no default) -> error 0x7011.
  - Wrong kind -> error 0x7012.
  - Constraint violation -> error 0x7013.
  - Non-constant value arg -> error 0x7014.
  - Type mismatch on value arg -> error 0x7015.

**Estimated effort:** Large (10-14 hours).

---

### Milestone 7 — KDI Export/Import & Cross-Module Templates

**Goal:** Template instantiations are correctly exported to KDI and can be imported
from other modules.

**Steps:**
1. **libkdi changes**:
   - Add `template_origin` and `template_args_mangled` to `kdi_aggregate` and
     `kdi_function` in `kdi_aggregates.hpp` / `kdi_file.hpp`.
   - Update CBOR serialization/deserialization (`kdi_cbor.cpp`).
   - Update JSON serialization/deserialization (`kdi_json.cpp`).
   - Update validation (`kdi_validate.cpp`): accept schema 0.2, validate field pairs.
   - Update dump (`kdi_dump.cpp`): display template origin.
   - Bump schema version to 0.2 in the header constants.
2. **KDI exporter** (`kdi_exporter.cpp`):
   - When exporting an aggregate/function with `_instantiation_info`, set the
     `template_origin` and `template_args_mangled` fields.
3. **KDI importer** (`kdi_importer.cpp`):
   - Read the new fields. The imported entity is a regular imported_aggregate/function.
   - The `template_origin` info is informational (for tooling); the imported entity
     works like any other imported entity.
4. **kdi-tool**: Update `dump` and `json-dump` to display template info.

**Tests (Milestone 7):**
- `test-import-template.cpp`:
  - Build a library with a template struct, instantiate it in the library's public API,
    export to KDI.
  - Import the KDI in a second module, use the instantiated struct.
  - Verify the imported struct works correctly.
- `libkdi/tests/test_cbor.cpp`: Add tests for CBOR round-trip with template_origin.
- `libkdi/tests/test_json.cpp`: Add tests for JSON round-trip with template_origin.
- `libkdi/tests/test_validate.cpp`: Schema 0.2 validation tests.
- kdi-tool dump test: verify template origin appears in output.

**Estimated effort:** Medium (6-8 hours).

---

### Milestone 8 — Documentation, Polish & Edge Cases

**Goal:** Complete documentation, handle edge cases, ensure all tests pass.

**Steps:**
1. **Update `doc/spec/language/summary.md`**: Add section 25 (Templates).
2. **Update `doc/spec/language/grammar.ebnf`** (if not done in M1): finalize grammar.
3. **Update `doc/spec/kdi/kdi-schema-abstract.md`** and `kdi-cbor-schema.md`.
4. **Update `.copilot-instructions.md`**: Mention templates in the overview.
5. **Edge cases and integration tests**:
   - Template struct as a base class for another struct.
   - Template class with virtual methods.
   - Template function as an operator overload.
   - Nested template instantiation: `Pair<Pair<int>>`.
   - Template used with `new` / `delete`: `new Pair<int>(1, 2)`.
   - Template used with arrays: `Pair<int>[5]`.
   - Template with `const` modifier: `const Pair<int>&`.
   - Template and `using` alias: `using IntPair = Pair<int>;`.
   - Template and friend declarations.
   - Template and visibility specifiers.
   - Template in non-module (root namespace) context.
6. **Full regression run**: Ensure all existing tests pass.

**Tests (Milestone 8):**
- Various edge case tests in existing test files (or new
  `test-gen-template-advanced.cpp`).
- Full CTest run with `--output-on-failure`.

**Estimated effort:** Medium (4-8 hours).

---

## Test Plan Summary

| Test File | Tags | What It Tests |
|-----------|------|---------------|
| `test-lexer.cpp` | `[lex]` | `template`/`typename` keyword tokenization |
| `test-parser.cpp` (or new) | `[parse]`, `[template]` | Template declaration & arg list parsing, disambiguation |
| `test-gen-template-functions.cpp` | `[gen]`, `[template]`, `[template-func]` | Template function instantiation, multiple types, return values |
| `test-gen-template-aggregates.cpp` | `[gen]`, `[template]`, `[template-struct]` | Template struct/class instantiation, fields, methods, ctors |
| `test-gen-template-value-params.cpp` | `[gen]`, `[template]`, `[template-value]` | Value parameters, sized arrays, constant expressions |
| `test-gen-template-constraints.cpp` | `[gen]`, `[template]`, `[template-constraint]` | Kind filters, base-type constraints |
| `test-gen-template-defaults.cpp` | `[gen]`, `[template]`, `[template-default]` | Default type/value parameters |
| `test-gen-template-errors.cpp` | `[gen]`, `[template]`, `[template-error]` | All error codes 0x7001-0x7017 |
| `test-gen-template-mangling.cpp` | `[gen]`, `[template]`, `[mangling]` | Mangled name correctness |
| `test-import-template.cpp` | `[import]`, `[template]` | Cross-module template instantiation via KDI |
| `libkdi/tests/test_cbor.cpp` | `[kdi]`, `[cbor]` | CBOR round-trip with template_origin |
| `libkdi/tests/test_json.cpp` | `[kdi]`, `[json]` | JSON round-trip with template_origin |
| `libkdi/tests/test_validate.cpp` | `[kdi]`, `[validate]` | Schema 0.2 acceptance |

---

## Risk Analysis

| Risk | Impact | Mitigation |
|------|--------|------------|
| Angle-bracket disambiguation complexity | High | Tentative parse with save/restore; well-tested edge cases |
| AST cloning correctness | High | Comprehensive cloner tests; structural comparison |
| Pipeline ordering (cascade instantiations) | Medium | Fixed-point queue; test with chain/diamond templates |
| Interaction with existing features (virtual dispatch, annotations, etc.) | Medium | Progressive integration tests in Milestone 8 |
| Performance (many instantiations) | Low | Registry dedup; lazy instantiation; acceptable for Phase 1 |

---

## Total Estimated Effort

| Milestone | Hours |
|-----------|-------|
| M1 — Lexer & Grammar | 1-2 |
| M2 — AST & Parser | 6-10 |
| M3 — Model Layer | 4-6 |
| M4 — Cloner & Instantiator | 8-12 |
| M5 — Mangling & Resolution | 8-12 |
| M6 — CodeGen & Advanced | 10-14 |
| M7 — KDI & Import | 6-8 |
| M8 — Documentation & Polish | 4-8 |
| **Total** | **47-72** |

---

*Each milestone should end with a green test suite. Milestones can be submitted as
individual PRs for review.*






