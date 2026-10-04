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
| Application return/unwind shutdown ordering | `src/gen/gen_unit.cpp` | `libk/libk/src/runtime/application_ffi.c`, `libk/libk/src/runtime/io_runtime.c`, `libk/libk/tests/test-application-lifecycle.cpp` |
| Private constructor/static helper friend access | `src/gen/resolvers_scope_lookup.cpp` | `src/gen/gen_expr_construct.cpp`, `tests/test-gen-friend.cpp` |

Relative friend targets use the declaring aggregate's lexical scope, not the
consumer root. Direct friend members include constructor/default member
initializers and static helpers; inherited and nested members do not acquire
friendship. Keep private dependency declarations private and preserve negative
access checks.

The generated standard-library entry point invokes `__k_application_shutdown`
before the concrete Application destructor on normal return and unwind.
The FFI owns the completed barrier and must leave an unused runtime virgin.
Bootstrap entry points without `::k::Application` must not acquire this hook.

KDI export runs after the frontend exception boundary. Its private-dependency
lookup logger throws structured diagnostics without printing them; `gen_kdi`
must report these through the compiler logger before propagating the exception.
Dependency collection is not a semantic pass: never resolve a deferred `this`
in an uninstantiated template body or latch an error for that speculative probe.
Native declarations with unavailable LLVM ABI types must diagnose the function,
symbol and affected parameter/return type instead of silently skipping emission.
Imported callable signatures are completed after all KDI namespaces have been
materialised: an aggregate can refer to a union or alias declared in a later
namespace. Pointer return subtypes also need lexical signature resolution before
any function body uses them; matching LLVM pointer shapes does not establish K
type identity.

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

Aggregate instantiations created after Pass A receive a body-only symbol-resolution
catch-up before their parameter expressions and bodies are type-resolved. This uses
the canonical symbol resolver after completing missing nested union declarations,
preserves already-bound cloned targets, and must not repeat aggregate layout or
constructor-initializer synthesis.
Full body typing is idempotent per concrete function within the type-reference pass.
Template-qualified static calls use that pass's instantiation path, not a fresh
declaration-only aggregate resolver. Explicit function template calls instantiate
every candidate declaration; argument caches are declaration-owned, not name-owned.

Private FFI dependencies of imported templates remain private declarations in a
definition-scope namespace carried by template source (not exported KDI functions).
Cloned functions must retain that scope for symbol lookup, overload lookup, and
visibility checks, including across re-export. Do not widen FFI visibility.
Unresolved-type declaration origins belong only to re-parsed imported declarations.
Do not attach them to ordinary local types or substituted caller arguments. Origin
references use lexical resolver lookup, not a parallel context-registry lookup;
enum return signatures and cast targets must share their declaration's enum type.
Private dependency signatures retain rooted type references. Concrete aggregate
types are emitted with source template arguments, never only their internal
instantiation-cache names.

Nullable class upcasts must preserve null before adjusting a base-subobject
address or loading a virtual-base pointer. Owner cleanup must never receive a
base offset manufactured from null.

Namespace-level lambda declarations synthesized inside free template functions
must retain the function's template parameters until instantiation. Bind their
concrete declarations and capture arguments before resolving closure constructors.
Find the enclosing function through the lexical context stack, not only its top
entry (normally a block). Original dependent closures must remain unvisited.
Member overload collection uses `scope_lookup::lookup_member_functions`, whose
signature comparison must work both before and after callable normalization.
Hiding must be independent of base traversal order. Same-owner equal-parameter
redeclarations preserve the first declaration for lookup (including Writer's
covariant adapters); virtual dispatch still uses the final implementation in
that slot. Unrelated inherited equal-signature declarations remain ambiguous.

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
| `klang-tests-gen-core` | general codegen + literals + variable init |
| `klang-tests-gen-resolution` | symbol resolution, visibility, multi-source |
| `klang-tests-gen-extern-intrinsics` | extern C, compiler intrinsics, varargs |
| `klang-tests-gen-functions` | callables/lambda/func-ref; CTest shards 0–2 |
| `klang-tests-gen-control` | statements/control-flow/init-order/static ctor-dtor |
| `klang-tests-gen-exceptions` | throw/catch/contracts/rethrow/unwinding |
| `klang-tests-gen-ternary` | ternary expression behavior matrix |
| `klang-tests-gen-arithmetic-int8-16` | 8/16-bit integer arithmetic (byte..uint16) |
| `klang-tests-gen-arithmetic-int32-64` | 32/64-bit integer arithmetic (int32..uint64) |
| `klang-tests-gen-arithmetic-float` | float/double/bool/incr-decr arithmetic |
| `klang-tests-gen-operators` | operator overload/comparison/spaceship/assign |
| `klang-tests-gen-casts` | scalar casts and indirection casts |
| `klang-tests-gen-types-enums-unions` | enums, typed enums, and union hierarchies |
| `klang-tests-gen-types-const-null` | const and null semantics (`test-gen-const.cpp`, `test-gen-null-semantics.cpp`) |
| `klang-tests-gen-types-meta-designated` | annotations and designated init (`test-gen-annotation-meta.cpp`, `test-gen-designated-init.cpp`) |
| `klang-tests-gen-aliases` | alias/typedef/callable-alias |
| `klang-tests-gen-classes` | class basics/inheritance/upcasts |
| `klang-tests-gen-virtuality` | virtual/interface/override/diamond |
| `klang-tests-gen-memory-ownership` | owner/drain/indirection |
| `klang-tests-gen-arrays` | fixed/dynamic arrays and foreach |
| `klang-tests-gen-oop-lifecycle` | lifecycle and RVO |
| `klang-tests-gen-oop-temporary` | named return + temporary construction |
| `klang-tests-gen-scoping` | using/friend |
| `klang-tests-gen-templates-core` | template functions/instantiation/mangling/members |
| `klang-tests-gen-templates-deduction` | template deduction/constraints/packs/value params |
| `klang-tests-gen-generics` | generic (type-erased) declarations, restrictions, single synthesis |
| `klang-tests-import-core` | KDI import core, namespaces, enums, transitive |
| `klang-tests-import-templates` | KDI cross-module template instantiation; CTest shards 0–2 |
| `klang-tests-import-interop` | KDI aliases, typedefs, callables, prod-lib |
| `klangc-tests` | CLI/integration behavior |

Grouping policy:
- keep thematic coherence,
- keep per-executable runtime under timeout budget,
- register new/redistributed files with `klang_gen_test(<target> 120 ...)`.
- `SHARD_COUNT 3` keeps heavy-suite executable names and build dependencies
  unchanged, but replaces their unsharded CTest entry with
  `<target>-shard-0`, `<target>-shard-1`, and `<target>-shard-2`.
  All shards use identical lexical ordering and RNG seed, run serially, and
  retain the 120-second timeout; every normally enabled Catch2 test case runs
  exactly once. Hidden (`[.]`) cases remain opt-in as before.
- Use `ctest -R "^klang-tests-gen-functions-shard-" --output-on-failure` or
  `ctest -R "^klang-tests-import-templates-shard-" --output-on-failure` to run
  a full heavy suite within the per-entry budget. Direct executable invocation
  remains available for focused selectors or standalone whole-suite runs.

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
