# Klang Compiler (`klang/`) — AI Agent Guide

Scope: compiler implementation (`lex`/`parse`/`model`/`gen`), `klangc`, and compiler tests.

---

## 1. Directory map (where to start)

| Path | Purpose | Typical use |
|------|---------|-------------|
| `src/compiler*.cpp` | orchestration, linker wiring, diagnostics | pipeline/debug/link bugs |
| `src/lex/` | lexer + lexemes | tokenization issues |
| `src/parse/` | AST + parser | syntax/grammar/AST construction issues |
| `src/model/` | semantic model + templates + mangling + KDI model tools | type/model/template/name issues |
| `src/gen/` | resolver passes + code generation | resolution/codegen/runtime behavior issues |
| `tests/` | Catch2 suites | regressions and behavior checks |
| `CMakeLists.txt` | test grouping/targets | timeout and test architecture |

---

## 2. Investigation playbook (by symptom)

| Symptom | Start with | Then check |
|---------|------------|------------|
| Parse error / wrong AST shape | `src/parse/parser_*.cpp`, `src/parse/ast.hpp` | `src/lex/lexemes.*`, `src/parse/doc_comment_parser.*` |
| Symbol not found / wrong lookup | `src/gen/resolvers_symbol.cpp` | `src/gen/resolvers_scope_lookup.cpp`, `src/model/model_ns.hpp` |
| Type mismatch in semantic pass | `src/gen/resolvers_type_ref.cpp` | `src/model/type.*`, relevant `src/gen/gen_expr_*.cpp` |
| Wrong overload/operator selected | `src/gen/gen_operators_overload.cpp` | `src/gen/gen_callable*.cpp`, `src/gen/gen_operators_logical.cpp` |
| Wrong cast/upcast/downcast behavior | `src/gen/gen_expr_cast.cpp` | `src/gen/gen_expr_member.cpp`, `src/model/type.*` |
| Exception not caught / unwind broken | `src/gen/gen_statements.cpp` | `src/gen/gen_helpers.hpp`, `libk/libk/src/rtti.c` |
| Template instantiation loses code/symbols | `src/model/template_instantiator.cpp` | `src/gen/gen_expr_invocation.cpp`, `src/gen/gen_expressions.cpp` |
| Imported symbol/link mismatch | `src/model/tools/kdi_importer.cpp` | `src/compiler_linker.cpp`, `src/model/imported.*` |
| Mangling collision / link ambiguity | `src/model/mangler.cpp` | `src/compiler.cpp` (`verify_mangled_names`) |

---

## 3. Compiler architecture (must preserve order)

### 3.1 End-to-end flow (`compiler::parse_sources()`)

1. **Source load**
   - load input files into internal source list.
2. **Module name discovery**
   - lightweight pre-parse (`lookup_module_name`) to build module map early.
3. **Frontend**
   - lex/parse each source -> `parse::ast::unit`.
4. **Model build**
   - AST -> `k::model::unit` via `model_builder`.
   - resolve KDI imports (`kdi_importer`) and materialize imported model nodes.
5. **Resolver pass chain**
   - Pass A: `symbol_resolver::resolve()` + `context::resolve_types()`
   - Pass B: `aggregate_type_resolver::resolve()` + `context::resolve_types()` (re-run after template instantiation)
   - Pass C: `model_materializer::materialize()`
   - Pass D: `type_reference_resolver::resolve()` + `importer.check_unused_imports()`
6. **Code generation**
   - declaration pass (`declaration_generator`)
   - implementation pass (`implementation_generator`)
   - optional LLVM optimization
7. **Output and link**
   - emit IR/object
   - generate executable/shared/static library or KDI (`compiler_linker.cpp`).

Do not collapse/reorder phases.

### 3.2 Architectural layering

| Layer | Primary types | Main files |
|------|----------------|------------|
| Frontend lexing | token stream (`lex::lexeme` etc.) | `src/lex/lexemes.*`, `src/lex/lexer.*` |
| Frontend parsing | AST (`parse::ast::*`) | `src/parse/ast.*`, `src/parse/parser_*.cpp` |
| Semantic model | model graph (`k::model::*`) | `src/model/model_*.hpp`, `src/model/model.cpp` |
| Semantic resolution | resolver visitors | `src/gen/resolvers_*.cpp` |
| IR generation | LLVM IR builders/visitors | `src/gen/gen_*.cpp` |
| Artifact generation | link/library/kdi emission | `src/compiler_linker.cpp`, `src/model/tools/kdi_exporter.*` |

### 3.3 Critical architectural boundaries

- **AST vs Model boundary**
  - syntax-only decisions stay in parser/AST;
  - semantic/type decisions happen in model/resolver passes.
- **Resolver vs Codegen boundary**
  - resolver passes establish symbols/types/overloads;
  - codegen should consume resolved model, not re-derive semantics ad hoc.
- **Declaration vs Implementation codegen**
  - declaration pass must create LLVM declarations/types first;
  - implementation pass emits bodies and executable IR.
- **Local vs Imported entities**
  - imported entities are represented via `imported_*` model nodes;
  - cross-module behavior must not assume local concrete definitions.

### 3.4 Auto-import/auto-link architecture

- For every module except `k`, compiler injects base stdlib import (`import k;`).
- Link arguments include `-lk` by default except when compiling module `k`.
- Optional stdlib modules (e.g. `k::math`) remain explicit imports.

### 3.5 KDI dependency architecture

- KDI lookup chain: local dir -> `-I` paths -> `KLANG_LIB_PATH` env -> system dirs.
- `header.dependencies` are loaded transitively.
- Missing transitive KDI is fatal and must fail compilation early.

---

## 4. High-risk invariants

- `import k;` is auto-injected for non-`k` modules.
- `build_import_link_args()` auto-links `-lk` except for module `k`.
- `mangler::mangle_type()` must be total and injective.
- `template_instantiator::clone_statement()` must handle every statement kind.
- Indirection cast path must handle `* & + ? ! #`.
- Calls that may throw must use unwind-aware emission (`invoke` path).
- Stub files are documentation only (do not add logic):
  - `src/parse/parser.cpp`
  - `src/gen/resolvers.cpp`
  - `src/gen/gen_operators.cpp`

---

## 5. Deeper file groups for targeted work

### 5.1 Frontend and AST
- Lexer:
  - `src/lex/lexemes.hpp/.cpp`
  - `src/lex/lexer.hpp/.cpp`
- Parser and AST:
  - `src/parse/ast.hpp/.cpp`
  - `src/parse/parser.hpp`
  - `src/parse/parser_declarations.cpp`
  - `src/parse/parser_statements.cpp`
  - `src/parse/parser_expressions.cpp`
  - `src/parse/doc_comment_parser.hpp/.cpp`

### 5.2 Model and template system
- Core model:
  - `src/model/model_*.hpp`, `src/model/model.cpp`
  - `src/model/statements.*`, `src/model/operators.*`, `src/model/type.*`
- Context/type materialization:
  - `src/model/context.hpp/.cpp`
- Template machinery:
  - `src/model/template.hpp/.cpp`
  - `src/model/template_instantiator.hpp/.cpp`
- Naming:
  - `src/model/mangler.hpp/.cpp`

### 5.3 Resolver passes
- `src/gen/resolvers_scope_lookup.*`
- `src/gen/resolvers_symbol.*`
- `src/gen/resolvers_aggregate.*`
- `src/gen/resolvers_materializer.*`
- `src/gen/resolvers_type_ref.*`
- `src/gen/resolvers_init_order.*`

### 5.4 Codegen visitors
- Units/functions/classes:
  - `src/gen/gen_unit.cpp`, `gen_function.cpp`, `gen_class.cpp`, `gen_struct.cpp`, `gen_constructor.cpp`
- Statements/variables:
  - `src/gen/gen_statements.cpp`, `gen_variable_definition.cpp`
- Expressions:
  - `src/gen/gen_expressions.cpp`, `gen_expr_unary.cpp`, `gen_expr_member.cpp`, `gen_expr_invocation.cpp`, `gen_expr_memory.cpp`, `gen_expr_construct.cpp`, `gen_expr_cast.cpp`
- Callables:
  - `src/gen/gen_callable.cpp`, `gen_callable_compat.cpp`, `gen_callable_helpers.hpp`, `gen_adapt_type.cpp`
- Operators:
  - `src/gen/gen_operators_overload.cpp`, `gen_operators_arithmetic.cpp`, `gen_operators_assign.cpp`, `gen_operators_unary.cpp`, `gen_operators_logical.cpp`, `gen_operators_spaceship.cpp`

### 5.5 Compiler entry/link/diagnostics
- Pipeline + global checks:
  - `src/compiler.hpp/.cpp`
- Link and library generation:
  - `src/compiler_linker.cpp`
- Diagnostic rendering:
  - `src/compiler_diagnostic.cpp`
- CLI executable:
  - `src/klang.cpp`

---

## 6. Test architecture and search strategy

Use this mapping to choose the smallest relevant executable first:

| Executable | Focus |
|------------|-------|
| `klang-tests-frontend` | lexer/parser/file-resolver/process |
| `klang-tests-model` | model/documentation/materializer/phase checks |
| `klang-tests-gen-core` | general codegen + resolution baseline |
| `klang-tests-gen-functions` | callables/lambda/func-ref/varargs |
| `klang-tests-gen-control` | statements/control-flow/exceptions/init-order |
| `klang-tests-gen-arithmetic` | primitive arithmetic behavior |
| `klang-tests-gen-operators` | operator overload/comparison/spaceship/casts |
| `klang-tests-gen-types` | enum/union/const/null/annotations |
| `klang-tests-gen-aliases` | alias/typedef/callable-alias |
| `klang-tests-gen-classes` | class basics/inheritance/upcasts |
| `klang-tests-gen-virtuality` | virtual/interface/override/diamond |
| `klang-tests-gen-memory` | owner/indirection/arrays/drain |
| `klang-tests-gen-oop` | lifecycle/RVO/temporary/named-return |
| `klang-tests-gen-scoping` | using/friend |
| `klang-tests-gen-templates` | template-heavy behavior |
| `klang-tests-import` | KDI import/transitive/prod-lib |
| `klangc-tests` | CLI/integration behavior |

Grouping policy:
- keep thematic coherence,
- keep per-executable runtime under timeout budget,
- register new/redistributed files with `klang_gen_test(<target> 120 ...)`.

---

## 7. Practical commands

```bash
# Build one test executable
cd cmake-build-debug && ninja -j3 klang-tests-gen-operators

# Run one executable
cd cmake-build-debug && ./klang/klang-tests-gen-operators

# Run compiler-related suites
cd cmake-build-debug && ctest -R "klang-tests|klangc-tests" --output-on-failure
```
