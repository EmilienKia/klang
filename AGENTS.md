# Klang Compiler — AI Agent Instructions

This document is the primary reference for AI agents working on this codebase.
Read it **before** exploring any source file to minimise unnecessary file reads.

---

## 1. Project Overview

**Klang** is a compiled, statically typed, C++-inspired language targeting native
code via LLVM. The repository contains:

| Sub-project | Path | Purpose |
|-------------|------|---------|
| `klang/` | `klang/` | Compiler (library `klang` + executable `klangc`) |
| `libk/` | `libk/` | K standard library (written in K, compiled by klangc) |
| `libkdi/` | `libkdi/` | KDI (K Description Interface) file format library |

Build system: **CMake + Ninja**. Build directory: `cmake-build-debug/`.  
Build command: `cd cmake-build-debug && ninja -j3`.  
Test command: `cd cmake-build-debug && ctest -j3`.

### Technology Stack

| Layer | Technology |
|---|---|
| Implementation language | C++20 |
| Build system | CMake ≥ 3.17 + Ninja |
| IR / code-gen backend | LLVM (all targets + OrcJIT) |
| CLI argument parsing | Boost.ProgramOptions |
| Utility libraries | Boost.System, Boost.Filesystem, fmtlib |
| Test framework | Catch2 v3 (`Catch2::Catch2WithMain`) |
| KDI serialisation | libcbor (CBOR), nlohmann/json (JSON) |

---

## 2. Agent Behavioural Rules

These rules apply to **every AI agent** working on this repository.

### Build
- Always use the build directory `cmake-build-debug/` — do not create alternate
  build directories.

### Git / VCS
- **Never commit** changes without explicit user confirmation.
- **Never push** to any remote without explicit user confirmation.
- Prefer small, focused changesets; describe what changed and why before asking
  for commit confirmation.

### Language
- Use **English** for everything written in code: identifiers, comments,
  documentation strings, log messages, test descriptions, commit messages.
- Never use another language in source files, even if the user writes in French.

---

## 3. Compilation Pipeline

The `compiler` class (`klang/src/compiler.hpp/cpp`) orchestrates the full pipeline.
Entry point: `compiler::parse_sources()`, which runs these phases **in order**:

```
Phase 0   Load source files into _sources vector (reserve() before lexing)
Phase 1   Module name discovery (lightweight pre-parse via lookup_module_name)
Phase 2   Lexing + Parsing → AST (parse::ast::unit)
Phase 3   Model building (AST → k::model::unit via model_builder)
          KDI import resolution (kdi_importer)
          ↓
Pass A    symbol_resolver::resolve()           — resolve names/symbols
          context::resolve_types()             — LLVM type materialization
Pass B    aggregate_type_resolver::resolve()   — struct/class/enum type refs
          context::resolve_types()             — re-run after template instantiation
Pass C    model_materializer::materialize()    — resolve init order etc.
Pass D    type_reference_resolver::resolve()   — all other type refs + overloads
          importer.check_unused_imports()
          ↓
Code gen  process_generation():
            declaration_generator (LLVM type + function declarations)
            implementation_generator (LLVM function bodies)
            optimize_gen_code() (optional)
```

Output: LLVM IR → object file → executable / shared lib / static lib / KDI.

### K Standard Library (libk)

The K standard library is compiled from K source files using `klangc`.

- **Base library** (module `k`, directory `libk/libk/`): compiled during the build via
  `add_custom_command`. Produces `libk.so`, `libk.a`, `libk.kdi`, and a `k.kdi` symlink.
- **Auto-imported**: the compiler injects `import k;` for every module except `k` itself
  (in `compiler.cpp`, after model building). Do **not** write `import k;` in K sources.
- **Auto-linked**: `build_import_link_args()` always adds `-lk` unless the current module is `k`.
- `config.h` defines `KLANG_STDLIB_KDI_DIR` and `KLANG_STDLIB_LIB_DIR` so `klangc` can locate
  stdlib artifacts at build time.
- **Optional libraries** (e.g. `k::math`): not auto-imported; the programmer adds
  `import k::math;` explicitly. Each optional library depends on the base `libk_stdlib` target.

### KDI import and transitive dependencies

When the compiler processes `import <module>;`, `kdi_importer` locates the `.kdi` file via a
chain of `file_resolver`s (local dir → `-I` paths → `KLANG_LIB_PATH` env → system dirs).
The `header.dependencies` field of each loaded KDI lists all direct imports; these are loaded
recursively and their `imported_*` nodes are materialised alongside the direct imports.
A missing transitive KDI is a **fatal error**.

---

## 4. Source Tree Map

### `klang/src/` — flat files

| File | Purpose |
|------|---------|
| `compiler.hpp / .cpp` | Core pipeline, parse/codegen orchestration, IR/obj/exe output |
| `compiler_linker.cpp` | `gen_executable`, `gen_shared_library`, `gen_static_library`, `gen_libraries`, `gen_kdi`, `build_import_link_args` |
| `compiler_diagnostic.cpp` | `report()`, `print_logs()`, `log_source_line*()`, coordinate helpers |
| `errors.hpp` | **Umbrella** → `errors_lex_parse.hpp` → `errors_model.hpp` → `errors_gen.hpp` |
| `errors_lex_parse.hpp` | `compiler_diag`, `lexer_diag`, `parser_diag` |
| `errors_model.hpp` | `model_diag`, `symbol_diag`, `structure_diag`, `function_diag`, `type_diag` |
| `errors_gen.hpp` | `operator_diag`, `variable_diag`, `statement_diag`, `codegen_diag`, `template_diag` |
| `klang.cpp` | `klangc` executable entry point |
| `config.h.in` | CMake-generated version header |

### `klang/src/common/`

| File | Purpose |
|------|---------|
| `common.hpp / .cpp` | `name`, `source`, `char_coord`, `char_pos`, `file_resolver` base types |
| `logger.hpp / .cpp` | `diagnostic`, `logger`, `logger_relay`, logging infrastructure |
| `any_of.hpp` | Variadic `any_of<T...>` helper for lexeme matching |
| `operator_names.hpp` | Canonical operator name strings (`operator_add`, …) ↔ symbols (`+`, …) |
| `path_lookup_file_resolver.hpp / .cpp` | File resolver that searches a list of directories |
| `process.hpp / .cpp` | `run_process()` — shell out to clang/ar |
| `file_resolver.hpp / .cpp` | Abstract `file_resolver` interface |
| `tools.hpp` | Small utilities |

### `klang/src/lex/`

| File | Purpose |
|------|---------|
| `lexemes.hpp / .cpp` | All token types: `keyword`, `punctuator`, `identifier`, `integer_literal`, etc. `lex_holder` for save/restore |
| `lexer.hpp / .cpp` | `lexer` — tokenizes a `source` into a token queue; `lex_holder` for tentative parsing |

### `klang/src/parse/`

| File | Purpose |
|------|---------|
| `ast.hpp / .cpp` | All AST node types (`unit`, `module_name`, `import`, `function_decl`, `block_statement`, `expression`, …) |
| `ast_dump.hpp` | Pretty-printer for AST nodes |
| `parser.hpp` | `parser` class declaration (all 50+ public methods) |
| `parser.cpp` | **Stub** — see split files below |
| `parser_declarations.cpp` | Constructors, `parse_unit`, module/import, declarations, aggregates, enums, templates, functions, parameters, type specs |
| `parser_statements.cpp` | Statement block, return/break/continue, if-else, while, for, statements, variable decls |
| `parser_expressions.cpp` | All expression parsers (assignment → primary) + brace init list |

### `klang/src/model/`

The semantic model — all classes after parsing.

**Umbrella headers** (include these, not the sub-headers directly unless you need just one part):

| Umbrella | Chain | What it pulls in |
|----------|-------|-----------------|
| `model.hpp` | → `model_ns.hpp` | Entire model hierarchy |
| `expressions.hpp` | → `expressions_init.hpp` | All expression classes |

**Model sub-headers** (include individually for targeted edits):

| File | Contents |
|------|---------|
| `model_fwd.hpp` | STL/LLVM includes, all `namespace k::model` forward decls, `visibility` enum, vtable structs |
| `model_element.hpp` | `element`, `named_element`, `variable_definition`, holder mixins (`variable_holder`, `function_holder`, `aggregate_holder`, `enum_holder`, `union_holder`, `using_holder`, `friend_holder`, `annotation_holder`) |
| `model_enum.hpp` | `enum_entry_def`, `enum_raw_entry_def`, `enumeration` |
| `model_union.hpp` | `union_alternative`, `union_type_def` — discriminated union model class |
| `model_aggregate.hpp` | `member_variable_definition`, `base_spec`, `aggregate`, `structure`, `klass`, `interface`, `annotation_type` |
| `model_function.hpp` | `parameter`, `function`, `constructor`, `destructor`, `static_constructor`, `static_destructor`, `init_item`, `global_tool_function`, `global_constructor/destructor/main_function` |
| `model_ns.hpp` | `global_variable_definition`, `ns`, `unit` |

**Expression sub-headers**:

| File | Contents |
|------|---------|
| `expressions_base.hpp` | `expression`, `value_expression`, `symbol_expression` |
| `expressions_unary.hpp` | `unary_expression`, `binary_expression`, `load_value_expression`, `owner_move_expression`, `address_of_expression`, `drain_expression`, `dereference_expression`, `member_of_expression`, `member_of_object_expression`, `member_of_pointer_expression`, `pm_expression`, `cast_expression`, `subscript_expression` |
| `expressions_invocation.hpp` | `function_invocation_expression`, `constructor_invocation_expression`, `temporary_construction_expression`, `new_expression`, `delete_expression` |
| `expressions_init.hpp` | `array_init_expression`, `designated_struct_init_expression` |

**Other model files**:

| File | Purpose |
|------|---------|
| `expressions.cpp` | Non-inline method bodies for expression classes |
| `statements.hpp / .cpp` | `statement`, `block_statement`, `if_else_statement`, `while_statement`, `for_statement`, `return_statement`, `variable_statement`, `using_statement`, … |
| `operators.hpp / .cpp` | `arithmetic_binary_expression`, `logical_binary_expression`, `comparison_expression`, `assignation_expression`, `arithmetic_unary_expression`, `prefix/postfix_increment/decrement_expression`, etc. (inherits expression classes) |
| `type.hpp / .cpp` | `type` hierarchy: `primitive_type`, `struct_type`, `array_type`, `pointer_type`, `reference_type`, `owner_type`, `link_type`, `view_type`, `function_ref_type`, `unresolved_type`, … |
| `context.hpp / .cpp` | `context` — wraps `llvm::LLVMContext` + `llvm::Module`; type resolution cache |
| `model.cpp` | Non-inline `model.hpp` method bodies |
| `model_builder.hpp / .cpp` | `model_builder` — visitor that walks AST and constructs the `k::model::unit` |
| `model_visitor.hpp / .cpp` | `model_visitor` — double-dispatch visitor base for all model nodes |
| `model_dump.hpp` | `unit_dump` — debug printer for the model tree |
| `imported.hpp / .cpp` | `imported_function`, `imported_constructor`, `imported_aggregate`, etc. — model nodes for KDI-imported symbols |
| `import.hpp` | `imported_module` descriptor |
| `mangler.hpp / .cpp` | Name mangling / demangling |
| `template.hpp / .cpp` | `template_parameter`, `template_arg`, `template_instantiation_request` |
| `template_instantiator.hpp / .cpp` | `template_instantiator` — clones and specializes template aggregates |
| `tools/kdi_exporter.hpp / .cpp` | Serialize `k::model::unit` → `.kdi` file |
| `tools/kdi_importer.hpp / .cpp` | Load `.kdi` files → imported model nodes |
| `tools/kdi_type_converter.hpp / .cpp` | KDI ↔ model type mapping |
| `tools/k_source_emitter.hpp / .cpp` | Re-emit K source from model (for debugging) |

### `klang/src/gen/`

Code generation and resolution passes. All live in `namespace k::model::gen`.

**Resolution passes** (run before codegen, see pipeline above):

| Resolver | Header | Implementation | Role |
|----------|--------|----------------|------|
| `scope_lookup` | `resolvers_scope_lookup.hpp` | `resolvers_scope_lookup.cpp` | Visibility checks, namespace/member lookup, `is_friend_of`, `is_struct_member_accessible` |
| `symbol_resolver` | `resolvers_symbol.hpp` | `resolvers_symbol.cpp` + visitors in `gen_unit.cpp`, `gen_struct.cpp` | Resolve identifier → model element; redirect chains |
| `aggregate_type_resolver` | `resolvers_aggregate.hpp` | `resolvers_aggregate.cpp` | Resolve base classes, member types, enum underlying types; trigger template instantiation |
| `model_materializer` | `resolvers_materializer.hpp` | `resolvers_materializer.cpp` | Post-aggregate materialize pass |
| `signature_resolver` | `resolvers_signature.hpp` | Bodies in `gen_function.cpp`, `gen_constructor.cpp`, `gen_class.cpp`, `gen_unit.cpp` | Resolve function parameter and return types; LLVM function type creation |
| `type_reference_resolver` | `resolvers_type_ref.hpp` | `resolvers_type_ref.cpp` + visitor bodies spread across `gen_expr_*.cpp`, `gen_operators_*.cpp`, `gen_statements.cpp`, `gen_variable_definition.cpp`, `gen_class.cpp`, `gen_function.cpp`, `gen_constructor.cpp` | Full type resolution of all expressions, overload resolution, implicit casts |
| `init_order_resolver` | `resolvers_init_order.hpp` | `resolvers_init_order.cpp` | Compute global/static constructor and variable initialization order |

**Umbrella / common**:

| File | Purpose |
|------|---------|
| `resolvers.hpp` | **Umbrella** — includes all `resolvers_*.hpp` |
| `resolvers.cpp` | **Stub** — split into 6 files above |
| `resolvers_common.hpp` | Shared includes (`model.hpp`, `model_visitor.hpp`, `context.hpp`, `logger.hpp`) used by all resolver headers |
| `gen_helpers.hpp` | LLVM IR builder helpers, `destroy_owner()`, common LLVM patterns |
| `generators.hpp / .cpp` | `declaration_generator`, `implementation_generator` class declarations |

**Code generation** (`implementation_generator` visitor bodies):

| File | Visitor methods |
|------|----------------|
| `gen_unit.cpp` | `visit_unit`, `visit_namespace`, `visit_member_variable_definition`, `visit_global_variable_definition`; also `symbol_resolver` + `signature_resolver` visitor bodies |
| `gen_class.cpp` | `visit_klass`, `visit_interface`, vtable construction, virtual dispatch; also `signature_resolver` for classes |
| `gen_struct.cpp` | `visit_aggregate`, `visit_structure`; also `symbol_resolver::resolve_enumeration` |
| `gen_function.cpp` | `visit_function`, parameter handling, named return vars; also `signature_resolver::visit_function/parameter` |
| `gen_constructor.cpp` | `visit_constructor`, `visit_destructor`, `visit_static_constructor`, `visit_static_destructor`; also `signature_resolver` for constructors |
| `gen_variable_definition.cpp` | `visit_global_variable_definition` codegen |
| `gen_statements.cpp` | All statement visitors (block, if/else, while, for, return, break, continue, …); also `type_reference_resolver` statement visitors |
| `gen_expressions.cpp` | Base `visit_symbol_expression`, `visit_value_expression`, cast/load/drain/etc. |
| `gen_expr_unary.cpp` | Unary expression codegen (address-of, dereference, load, owner-move, drain) |
| `gen_expr_member.cpp` | `visit_member_of_object/pointer_expression`; also `type_reference_resolver` member visitors |
| `gen_expr_invocation.cpp` | `visit_function_invocation_expression`, `visit_constructor_invocation_expression`, `visit_temporary_construction_expression`; also `type_reference_resolver` invocation visitors |
| `gen_expr_memory.cpp` | `visit_new_expression`, `visit_delete_expression`, array memory ops; also `type_reference_resolver` memory visitors |
| `gen_expr_construct.cpp` | `visit_array_init_expression`, `visit_designated_struct_init_expression`; also `type_reference_resolver` construct visitors |
| `gen_expr_cast.cpp` | `visit_cast_expression`; also `type_reference_resolver` cast visitors |
| `gen_adapt_type.cpp` | `adapt_type()` and `adapt_reference_load_value()` — implicit type adaptation |
| `gen_operators.cpp` | **Stub** — split below |
| `gen_operators_helpers.hpp` | Anonymous-namespace helpers: `encode_type_for_cast_operator`, `get_binary_operator_name`, `get_unary_operator_name`, `get_operator_symbol`, `collect_member_operators_from_hierarchy` |
| `gen_operators_overload.cpp` | `resolve/generate_binary/unary/cast_operator_overload` |
| `gen_operators_arithmetic.cpp` | `process_arithmetic`, binary arithmetic visitors (+, -, *, /, %, &, \|, ^, <<, >>) |
| `gen_operators_assign.cpp` | `visit_assignation_expression` + all compound-assignment visitors |
| `gen_operators_unary.cpp` | `visit_arithmetic_unary_expression`, prefix/postfix inc/dec, unary +/−, `~` visitors |
| `gen_operators_logical.cpp` | `visit_logical_binary/unary_expression`, all comparison visitors (==, !=, <, >, <=, >=) |

### `libk/`

| Path | Purpose |
|------|---------|
| `libk/libk/src/object.k` | `Object` root base class |
| `libk/libk/src/string.k` | `CharHelpers`, `String`, `StringBuilder` |
| `libk/libk/src/io/` | I/O stream abstractions (`InputStream`, `OutputStream`, buffered, data, filter streams) |
| `libk/libk/src/io/io_helpers.c` | C runtime helpers (float/double bitcast for FFI) |
| `libk/libkmath/src/math.k` | `abs`, `min`, `max` utility functions (module `k::math`) |

### `doc/`

| Path | Purpose |
|------|---------|
| `doc/spec/language/grammar.ebnf` | **Authoritative EBNF grammar** for the K language |
| `doc/spec/language/summary.md` | Summary of language rules |
| `doc/spec/kdi/` | KDI file format specification (schema, CBOR layout) |
| `doc/spec/stdlib/` | Public K standard library reference (Object, String, I/O…) |
| `doc/man/klangc.md` | `klangc` man page (options, imports, library naming, transitive deps) |
| `doc/man/kdi.md` | `kditool` man page |

---

## 5. Key Design Patterns

### Umbrella Headers
Several large headers have been split into sub-headers. The umbrella header
preserves full backward compatibility — **always `#include` the umbrella** in
production code. Include sub-headers only when editing a specific class.

| Umbrella | Sub-header chain |
|----------|-----------------|
| `model.hpp` | `model_fwd` → `model_element` → `model_enum` → `model_union` → `model_aggregate` → `model_function` → `model_ns` |
| `expressions.hpp` | `expressions_base` → `expressions_unary` → `expressions_invocation` → `expressions_init` |
| `resolvers.hpp` | All `resolvers_*.hpp` individually |
| `errors.hpp` | `errors_lex_parse` → `errors_model` → `errors_gen` |

### Visitor Pattern
The model uses a double-dispatch visitor (`model_visitor`). Every model node has
an `accept(model_visitor&)` method. The resolution and code generation passes are
`model_visitor` subclasses. Visitor method bodies are **split across multiple
`.cpp` files** for the same visitor class — this is intentional.

For example, `type_reference_resolver` visitor bodies live in:
`resolvers_type_ref.cpp`, `gen_expr_*.cpp`, `gen_statements.cpp`,
`gen_operators_*.cpp`, `gen_variable_definition.cpp`, `gen_class.cpp`,
`gen_function.cpp`, `gen_constructor.cpp`.

### Tentative Parsing
The lexer supports `lex_holder` for save/restore of the token stream position.
Parser methods use it to implement backtracking without throwing.

### Error Reporting
Each resolver/generator inherits `log::logger_relay` and uses:
```cpp
throw_error(static_cast<unsigned int>(k::diag::some_diag::ERR_CODE),
            lexeme, "message template {}", {arg1, arg2});
```
Diagnostic codes live in `errors.hpp` (and sub-headers). Codes are hexadecimal
unsigned ints; each category has its own `enum class` in `namespace k::diag`.

### KDI (K Description Interface)
The `.kdi` file format describes the public interface of a compiled K library
(like a C header). When importing a module, the compiler loads its `.kdi` file
(via `kdi_importer`) and creates `imported_*` model nodes.

---

## 6. Coding Conventions

### C++ (compiler, tools, libraries)

- **Namespaces**: `k::` top-level; sub-namespaces mirror the directory (`k::lex`, `k::parse`,
  `k::model`, `k::model::gen`, `k::log`). KDI library code lives under `kdi::`.
- **Smart pointers**: `std::shared_ptr<T>` for model nodes (`enable_shared_from_this`);
  `std::unique_ptr<T>` for exclusive ownership (e.g., JIT instances).
- **Visitor pattern**: new AST/model node types must add a `visit()` override and a
  `visit_*()` method in the relevant `*_visitor` base class.
- **Error reporting**: use `k::log::diagnostic` + `k::log::logger::report()`. Throw
  `k::log::compiler_error` (or a subclass: `resolution_error`, `generation_error`) for fatal
  errors. Never use bare `throw std::runtime_error`.
- **Header guards**: `#ifndef KLANG_<FILENAME_UPPER>_HPP` (no `#pragma once`).
- **Copyright header**: every new `.cpp` / `.hpp` file must start with the standard
  Apache-2.0 block with `Copyright 2023-2026 Emilien Kia`.
- **C++ standard**: C++20 (`std::variant`, concepts, ranges are available).
- **Formatting**: 4-space indent; braces on the same line for control flow, on a new line for
  class/struct bodies — follow the surrounding file's style.
- **Naming**: standard C++ `snake_case` for all identifiers.
- **Unsigned integers**: use for counts, sizes, indices, offsets, and other non-negative values.
- **No raw debug output**: never add `printf` / `std::cout` inside library code — use the
  `k::log::logger` infrastructure.
- Do **not** put `find_package`, `CMAKE_CXX_STANDARD`, `enable_testing()`, or `KLANG_*`
  configuration variables in sub-project `CMakeLists.txt` — they belong in the root only.

### K source code

- **Naming**: `camelCase` for variables/functions, `PascalCase` for types, `ALL_CAPS` for
  constants (Java convention).
- **Formatting**: 4-space indent, braces on the same line (Java convention).
- **Addressers** — use the most restricted one that satisfies the semantics:
  - `&` reference — immutable, non-null
  - `?` view — immutable, nullable
  - `+` link — mutable, non-null
  - `*` pointer — mutable, nullable
  - `!` owner — mutable, nullable, with ownership-transfer semantics
  - `#` drain — like reference but with optional internal resource acquisition
- **Constness**: always use `const` whenever possible.
- **Imports**: never write `import k;` — the base stdlib is auto-imported.
  Direct members of `::k` are available without a `using` directive. Nested sub-namespaces
  (e.g. `k::math`) require an explicit name or `using k::math;`.
- **FFI**: K arrays are `{uint32_t count, data[]}` — not directly compatible with C arrays.
  All K addressers map to a pointer at the ABI level. Flag FFI functions as `private`.

---

## 7. Where to Find Things

| Task | Files to read |
|------|--------------|
| Add a new AST node | `parse/ast.hpp`, `parse/ast.cpp`, `parse/parser_declarations.cpp` or `parser_statements.cpp` |
| Add a new model class | Appropriate `model_*.hpp` sub-header, `model_visitor.hpp`, `model_visitor.cpp`, `model_dump.hpp` |
| Add a new expression class | Appropriate `expressions_*.hpp` sub-header, `model_visitor.hpp` |
| Add a new statement | `model/statements.hpp`, `gen/gen_statements.cpp` |
| Add a new binary operator | `model/operators.hpp`, `gen/gen_operators_arithmetic.cpp` or `gen_operators_assign.cpp` |
| Add a new unary operator | `model/operators.hpp`, `gen/gen_operators_unary.cpp` |
| Add a new diagnostic code | `errors_lex_parse.hpp` (parser) / `errors_model.hpp` (model) / `errors_gen.hpp` (codegen) |
| Fix a type resolution bug | `gen/resolvers_type_ref.cpp` + the relevant `gen_expr_*.cpp` |
| Fix a symbol lookup bug | `gen/resolvers_symbol.cpp`, `gen/resolvers_scope_lookup.cpp` |
| Fix an aggregate resolution bug | `gen/resolvers_aggregate.cpp` |
| Fix a code generation bug | The relevant `gen/gen_*.cpp` file |
| Add linking logic | `compiler_linker.cpp` |
| Add a diagnostic message | `compiler_diagnostic.cpp` |
| Understand template instantiation | `model/template_instantiator.cpp`, called from `resolvers_aggregate.cpp` |
| Understand name mangling | `model/mangler.cpp` |
| Understand import system | `model/tools/kdi_importer.cpp`, `model/imported.hpp` |
| Understand union types | `model/model_union.hpp`, `gen/gen_struct.cpp` (visit_union), `gen/gen_expr_member.cpp` (union access), `gen/gen_statements.cpp` (emit_union_cleanup) |
| Add a test | `klang/tests/test-gen-*.cpp` (follow existing pattern with `helpers.cpp`) |

---

## 8. Testing

83 test files in `klang/tests/`. All follow this pattern:

```cpp
// test-gen-my-feature.cpp
#include "helpers.hpp"

TEST_CASE("my feature", "[gen][my-feature]") {
    klang_test_context ctx;
    ctx.compile(R"(
        module test;
        // ... K source code ...
    )");
    // Use JIT to call functions and assert results
    auto fn = ctx.get_function<int()>("::my_function");
    REQUIRE(fn() == 42);
}
```

Key helpers: `klang_test_context` (in `helpers.hpp/cpp`) wraps `compiler::create()`
and provides `compile()`, `get_function<T>()`, `get_mangled_name()`.

Additional helpers for integration tests:

| Helper | Purpose |
|--------|---------|
| `gen_jit(src)` | Compile K snippet via JIT, returns function pointer |
| `gen_jit_throws(src)` | Expects a `k::log::compiler_error` subclass to be thrown |
| `build_and_exec(src)` | Full compile + link + run pipeline; returns `exec_result` |
| `build_shared_library(src)` | Compile K source to `.so` + `.kdi` in `/tmp` |
| `build_exec_with_lib(lib_src, exec_src)` | Compile one library + one executable, run it |
| `build_exec_with_libs(libs, exec_src)` | Compile several libraries + one executable |
| `build_exec_with_libs_direct_only(libs, exec_src, direct_imports)` | Like above but only listed modules are registered as explicit KDI paths (tests transitive KDI via search dirs) |
| `compile_should_fail(src, resolver)` | Returns `true` if compilation throws `k::log::compiler_error` |
| `make_pic_target_machine()` | PIC-mode LLVM `TargetMachine` for shared-library compilation |

Common test tags: `[gen]`, `[lex]`, `[parse]`, `[run]`, `[resolution]`, `[statements]`,
`[structs]`, `[import]`, `[transitive]`, `[prod-lib]`.  
Import sub-tags: `[phase4]`, `[e2e]`, `[import-class]`, `[import-transitive-chain-searchdir]`,
`[import-transitive-deep]`, `[import-transitive-diamond-searchdir]`, `[import-transitive-function]`.

New tests should go in the most specific existing file (e.g. arithmetic →
`test-gen-arithmetic.cpp`) or a new `test-gen-<feature>.cpp` registered in
`klang/CMakeLists.txt`.

Run a single test category:

```bash
cd cmake-build-debug && ./klang/klang-tests "[gen][arithmetic]"
```

---

## 9. Build Notes

- Always rebuild after editing CMakeLists.txt: CMake re-runs automatically on
  the next `ninja` invocation.
- The `compile_flags.txt` in the root is for clangd (editor tooling) only.
- Log level is `info` by default; add `--log-level trace` to `klangc`
  for verbose pipeline tracing.

### Build & run cheat-sheet

```bash
# Configure (debug) — run from the workspace root
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug

# Build everything (compiler + libkdi + kditool + all tests)
cd cmake-build-debug && ninja -j3

# Run all tests
cd cmake-build-debug && ctest -j3 --output-on-failure

# Run only klang tests
cd cmake-build-debug && ctest --output-on-failure -R klang-tests

# Run only libkdi tests
cd cmake-build-debug && ctest --output-on-failure -R kdi-tests

# Run a single test category
cd cmake-build-debug && ./klang/klang-tests "[gen][arithmetic]"

# Compile a K file to a shared library
./cmake-build-debug/klang/klangc --dyn-lib samples/mylib.k

# Compile a K file to a static library
./cmake-build-debug/klang/klangc --static-lib samples/mylib.k

# Compile a K executable
./cmake-build-debug/klang/klangc samples/fibo.k -o fibo

# Dump a .kdi file
./cmake-build-debug/libkdi/kditool dump path/to/lib.kdi

# Verbose pipeline tracing
./cmake-build-debug/klang/klangc --log-level trace samples/fibo.k
```

---

## 10. Language Quick Reference

The **authoritative** K language specification lives in `doc/spec/language/`:
- `grammar.ebnf` — full EBNF grammar
- `summary.md` — condensed language rules reference

Quick syntax summary:

```
module my.module;
import other.module;

// Types: int, long, double, bool, char, byte, short, float
// Qualifiers: *, &, !, ?, +, #  (pointer, ref, owner, optional, link/view, array-size)
// Visibility: public, protected, private (block-level or member-level)

struct Point {
    x: int;
    y: int;
}

class Animal {
    public:
    fun name() : string -> default;  // default implementation
    fun sound() : string -> delete;  // deleted
}

class Dog : public Animal {
    fun sound() override : string { return "woof"; }
}

fun add(a: int, b: int) : int { return a + b; }

// Annotations
@Deprecated
fun old_func() : void { }

// Templates
template<T>
fun identity(x: T) : T { return x; }

// Enums
enum Color { Red; Green; Blue; }

// Unions (discriminated/tagged)
union Value {
    i: int;
    d: double;
    s: String;
}

// Interfaces
interface Drawable {
    fun draw() : void -> delete;
}
```

---

## 11. Guidelines and Watch-outs

- **Language spec**: when adding/modifying lexical, syntax, or semantic rules, update
  `doc/spec/language/` (including `grammar.ebnf`), the tests, and all inline doc/comments.
- **KDI format**: when changing the KDI format, update `doc/spec/kdi/` and the schema version
  field. Version stays `0.1` until formally stabilised — do **not** bump it even for breaking changes.
- **K stdlib doc**: public/protected API only in `doc/spec/stdlib/`. Implementation details and
  private members stay in source comments only.
- **Compiler bugs**: every newly found compiler bug must get a reproducing test (with
  documentation) before or alongside the fix.
- **Language design issues**: document in `doc/spec/language/` and `TODO.md`; add a reproducing
  test. If fixed with a breaking change, update the spec and the test.
- **Missing features**: create a *skipped* test with documentation, add an entry to `TODO.md`
  describing the feature, its motivation, and the link to the related test.
- **Complex tasks**: create a temporary `IN-PROGRESS.md` at the repository root with a
  step-by-step implementation plan; update it during the work; delete it on completion.
- **No LLVM context duplication**: `k::model::context` owns the `llvm::LLVMContext` —
  never create additional `llvm::LLVMContext` instances.
- **Two-pass resolution**: never shortcut the declaration pass → implementation pass sequence;
  doing so breaks forward references.
- **`find_package` / CMake config**: root `CMakeLists.txt` only — never in sub-project files.
- **`AGENTS.md`**: this file is the single source of truth for all AI-agent conventions; keep
  it in sync with every structural change.

---

## 12. Stub Files

The following files are intentional stubs pointing to their split counterparts:

| Stub | Split into |
|------|-----------|
| `parse/parser.cpp` | `parser_declarations.cpp`, `parser_statements.cpp`, `parser_expressions.cpp` |
| `gen/resolvers.cpp` | `resolvers_scope_lookup.cpp`, `resolvers_symbol.cpp`, `resolvers_aggregate.cpp`, `resolvers_materializer.cpp`, `resolvers_type_ref.cpp`, `resolvers_init_order.cpp` |
| `gen/gen_operators.cpp` | `gen_operators_overload.cpp`, `gen_operators_arithmetic.cpp`, `gen_operators_assign.cpp`, `gen_operators_unary.cpp`, `gen_operators_logical.cpp` |

**Do not add code to stub files** — they exist only for documentation.



















