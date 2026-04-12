# Templates — Implementation & Test Plan

> **Scope:** Phase 1 — full explicit instantiation, no specialization.
> **Date:** 2026-04-09 (initial), 2026-04-12 (updated)

---

## Implementation Steps

The implementation is organized in **11 milestones**, each building on the previous one.
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

**Tests:** `test-lexer.cpp`: verify `template` and `typename` are tokenized as keywords.

---

### Milestone 2 — AST Nodes & Parser ✅ DONE

**Goal:** The parser can parse template declarations and template argument lists.
Templates are represented in the AST but have no semantic effect yet.

**Steps:**
1. New AST nodes: `template_parameter`, `template_arg`.
2. Modified AST nodes: `aggregate_decl`, `function_decl`, `identified_type_specifier`,
   `qualified_identifier` with template args.
3. Parser: `parse_template_declaration()`, `parse_template_parameter()`,
   `parse_template_arg_list()` with angle-bracket disambiguation.

**Tests:** Parser tests for template declarations, arg lists, disambiguation.

---

### Milestone 3 — Model Layer & Template Definitions ✅ DONE

**Goal:** The model builder creates `tpl_info` for template declarations. Template
aggregates and functions are recognized in the model but not yet instantiable.

**Steps:**
1. `klang/src/model/template.hpp`: `template_param_descriptor`, `template_argument`, `tpl_info`.
2. `_tpl_info` member in `aggregate` and `function` with `is_template()` helpers.
3. Model builder: detect template declarations, build `tpl_info`, process members
   with `unresolved_type` for template param references.
4. All resolution passes and generators skip template definitions.

**Tests:** 14 test cases, 77 assertions for template model definitions.

---

### Milestone 4 — Template Instantiator (model-level) ✅ DONE

**Goal:** Core instantiation engine at the model level. Given a template definition
and concrete arguments, clones model nodes and substitutes types — no AST cloning.

**Steps:**
1. `template_instantiator.hpp/.cpp`: `instantiate_aggregate()`, `instantiate_function()`.
2. Type substitution map: `unordered_map<string, shared_ptr<type>>`.
3. `substitute_type()` recursive rewriting through wrapper type chains.
4. Instantiation cache in `tpl_info::instantiations`.
5. Name helpers: `build_instantiation_key`, `build_instantiated_name`.

**Tests:** 7 test cases (61 assertions) for instantiation, caching, name generation,
member type substitution, function body cloning.

---

### Milestone 5 — Symbol Resolution Integration ✅ DONE

**Goal:** Symbol resolver can find templates and trigger instantiation.

**Steps:**
1. `aggregate_type_resolver`: handle `QualifiedIdentifier` with template args, trigger
   `instantiate_aggregate()`.
2. `type_reference_resolver`: same for type references in expressions.
3. Instantiated entities are registered in the parent namespace.

**Tests:** Gen-JIT integration tests for template struct instantiation (basic, member
type, distinct types, caching, function params/returns, multi-params).

---

### Milestone 6 — Suppress Cosmetic Diagnostics ✅ DONE

**Goal:** Template parameter placeholders (`unresolved_type` for `T`) do not produce
spurious "cannot resolve type" messages.

**Steps:**
1. Mark template param `unresolved_type` as placeholder in model builder.
2. Suppress diagnostics for placeholder types in `context::resolve_type`.
3. Reorder `resolve_one_type` for template-arg types.

**Tests:** Stderr capture test verifying no cosmetic error messages.

---

### Milestone 7 — Default Template Parameters ✅ DONE

**Goal:** Trailing template parameters with defaults can be omitted. `<>` syntax for
all-defaulted templates.

**Steps:**
1. Default type/value storage in `template_param_descriptor`.
2. Apply defaults when fewer arguments than parameters are provided.
3. Support `<>` syntax via `explicit_template_args` flag.

**Tests:** Gen-JIT tests for default type params, default value params, `<>` syntax.

---

### Milestone 8 — Name Mangling ✅ DONE

**Goal:** Template instantiations have correct Itanium-style mangled names with `I…E`
encoding.

**Steps:**
1. `mangle_template_args()`, `mangle_template_short_name()` in mangler.
2. Integration into `mangle_function`, `mangle_constructor`, `mangle_destructor`,
   `mangle_structure`, `mangle_type` for struct types.
3. `_tpl_base_name` and `_tpl_args` storage in aggregate and function models.

**Tests:** 7 test cases (41 assertions) for mangled name correctness.

---

### Milestone 9 — Function Template Instantiation ✅ DONE

**Goal:** Function templates are instantiated via `func<Args>(...)` call syntax.

**Steps:**
1. `visit_function_invocation_expression` in `gen_expressions.cpp` handles explicit
   template args.
2. Template function lookup and instantiation via `template_instantiator`.
3. Concrete function is used for overload resolution.

**Tests:** Gen-JIT tests for function template calls (primitives, multi-type-params,
structs, cache).

---

### Milestone 10 — Type Constraint Validation ✅ DONE

**Goal:** Kind filter (`struct`/`class`/`interface`) and base-type constraints are
validated at instantiation time with proper error diagnostics.

**Steps:**
1. `validate_template_arg_constraints()` in `template.cpp`.
2. `format_constraint_error()` generates `{diagnostic_code, message}` for 3 error types.
3. Three error codes: `ERR_TPL_ARG_NOT_AGGREGATE` (0x184), `ERR_TPL_ARG_WRONG_KIND`
   (0x182), `ERR_TPL_ARG_CONSTRAINT_VIOLATED` (0x183).
4. Aggregate/type resolvers throw `resolution_error` on constraint violations.
5. Function template instantiation logs error and prevents instantiation.

**Tests:** 16 integration tests: kind filter, base-type constraint, function templates,
error message content verification. 25 total test cases (89 assertions).

---

### Milestone 11 — Value Template Parameters ✅ DONE

**Goal:** Compile-time constant value parameters for templates, supporting all
primitive types.

**Steps:**
1. `value_substitution_map`: `unordered_map<string, k::value_type>`.
2. `k::value_type` = `std::variant<monostate, nullptr_t, bool, char, unsigned char,
   short, unsigned short, int, unsigned int, long, unsigned long, long long,
   unsigned long long, float, double, string>`.
3. `substitute_value_params()` recursive substitution in template instantiator.
4. Value arg extraction from AST literal expressions.
5. Parser disambiguation for value args in template arg lists.
6. Name mangling: `Li<n>E` encoding for value args.

**Tests:** 10 JIT tests for value parameter scenarios (int, bool, default values,
multi-value params, mixed type+value params).

---

## Remaining Work — Phase 1

### KDI Export/Import (not started)

**Goal:** Template instantiations are correctly exported to KDI and can be imported
from other modules.

**Steps:**
1. Add `template_origin` and `template_args_mangled` to KDI DTOs.
2. Update CBOR/JSON serialization.
3. Update KDI exporter/importer.
4. Update kdi-tool dump/json-dump.
5. Bump schema version.

**Tests:** Cross-module template instantiation via KDI import/export.

---

## Test Plan Summary

| Test File | Status | What It Tests |
|-----------|--------|---------------|
| `test-lexer.cpp` | ✅ | `template`/`typename` keyword tokenization |
| `test-parser-templates.cpp` (in parser tests) | ✅ | Template declaration & arg list parsing, disambiguation |
| `test-gen-template-instantiation.cpp` | ✅ | Model-level instantiation, caching, name generation |
| `test-gen-template-aggregates.cpp` | ✅ | Template struct instantiation, fields, members |
| `test-gen-template-functions.cpp` | ✅ | Template function instantiation, model-level |
| `test-gen-template-func-calls.cpp` | ✅ | Template function JIT call syntax |
| `test-gen-template-value-params.cpp` | ✅ | Value parameters, sized arrays, defaults |
| `test-gen-template-constraints.cpp` | ✅ | Kind filters, base-type constraints, error diagnostics |
| `test-gen-template-mangling.cpp` | ✅ | Mangled name correctness (Itanium I…E encoding) |
| `test-gen-template-comprehensive.cpp` | ✅ | Full matrix: functions, structs, classes, interfaces, derived classes, member methods, all arg types |
| `test-import-template.cpp` | ⬜ | Cross-module template instantiation via KDI |

**Total test coverage:** 8635+ assertions across 1725+ test cases (full suite).

---

## Risk Analysis

| Risk | Impact | Mitigation |
|------|--------|------------|
| Angle-bracket disambiguation complexity | High | Tentative parse with save/restore; well-tested edge cases |
| Pipeline ordering (cascade instantiations) | Medium | Fixed-point queue; test with chain/diamond templates |
| Interaction with existing features (virtual dispatch, annotations, etc.) | Medium | Progressive integration tests |
| Performance (many instantiations) | Low | Registry dedup; lazy instantiation; acceptable for Phase 1 |

---

*Each milestone should end with a green test suite. Milestones can be submitted as
individual PRs for review.*
