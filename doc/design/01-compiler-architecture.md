# Klang Compiler (`klangc`) — Architecture & Design

This document provides a semi-detailed architectural and design overview of `klangc`, the native reference compiler for the **K programming language**. It describes the compilation pipeline, subsystem decomposition, core data structures, driver orchestration, and code generation workflow.

---

## 1. High-Level Architecture & Principles

`klangc` is an ahead-of-time (AOT) and Just-In-Time (JIT) compiler built on **LLVM**. It translates K source code (`.k`) into LLVM Intermediate Representation (IR), native machine code objects (`.o`), static archives (`.a`), shared libraries (`.so`), executables, or K Declarative Interface files (`.kdi`).

```
                    ┌──────────────────────────────────────────┐
                    │               K Source Files             │
                    └─────────────────────┬────────────────────┘
                                          │
                                          ▼
                    ┌──────────────────────────────────────────┐
                    │       Frontend: Lexer & Parser           │
                    │         (AST: parse::ast::unit)          │
                    └─────────────────────┬────────────────────┘
                                          │
                                          ▼
                    ┌──────────────────────────────────────────┐
                    │      Semantic Model Construction         │
                    │         (Model: k::model::unit)          │
                    └─────────────────────┬────────────────────┘
                                          │
                                          ▼
                    ┌──────────────────────────────────────────┐
                    │      Multi-Pass Semantic Resolvers       │
                    │   (Symbols, Types, Classes, Overloads)   │
                    └─────────────────────┬────────────────────┘
                                          │
                                          ▼
                    ┌──────────────────────────────────────────┐
                    │      Code Generation (LLVM IR)           │
                    │   (Declaration + Implementation Passes)  │
                    └─────────────────────┬────────────────────┘
                                          │
                                          ▼
                    ┌──────────────────────────────────────────┐
                    │     LLVM Optimization & Verification     │
                    └─────────────────────┬────────────────────┘
                                          │
                     ┌────────────────────┴────────────────────┐
                     ▼                                         ▼
         ┌───────────────────────┐                 ┌───────────────────────┐
         │ Binary Emission & Link│                 │     KDI Metadata      │
         │ (Executable/Lib/JIT)  │                 │    (.kdi / .json)     │
         └───────────────────────┘                 └───────────────────────┘
```

### 1.1 Core Design Invariants

1. **AST vs. Model Separation**:
   - The **AST** (`k::parse::ast`) is a concrete syntax tree capturing syntactic structure and raw source lexemes without semantic validation.
   - The **Model** (`k::model`) represents the semantic graph (namespaces, aggregates, functions, parameters, typed statements, and typed expressions). It contains no LLVM types and is independent of code generation.
2. **Strict Multi-Pass Semantic Resolution**:
   - Cyclic references, forward declarations, template instantiations, and inheritance hierarchies prevent single-pass compilation.
   - Semantic resolution is partitioned into distinct passes: symbol binding, aggregate structure layout, virtual materialization, and full type inference.
3. **Single LLVM Context Ownership**:
   - A single `k::model::context` instance owns the underlying `llvm::LLVMContext` throughout compilation.
4. **Zero-Copy Lexeme Lifetimes**:
   - All loaded source buffers are allocated and locked in a non-reallocating container (`std::vector<k::source>`) before lexing begins, guaranteeing that `std::string_view` tokens remain valid across the entire compilation pipeline.
5. **Modular Dependency Model (KDI)**:
   - Separate compilation and cross-module imports are powered by **KDI** files, containing binary CBOR or JSON representations of exported modules, including aggregate layouts, function signatures, and serialized ASTs of templates.

---

## 2. Compiler Pipeline & Execution Flow

The compilation process is managed by `k::compiler::parse_sources()`. The execution sequence is strictly ordered:

```
[Phase 0] Source Ingestion & Buffer Pinning
    │
[Phase 1] Pre-Lookup: Module Discovery (lookup_module_name)
    │
[Phase 2] Frontend: Lexing, Parsing & AST Merging
    │
[Phase 3] Semantic Model Building & Import Resolution (model_builder, kdi_importer)
    │
[Phase 4] Semantic Resolver Chain:
    ├── Pass A: Symbol Resolution & Vtable Layout (symbol_resolver)
    ├── Pass B: Aggregate & Signature Resolution (aggregate_type_resolver)
    ├── Pass C: Model Materialization (model_materializer)
    └── Pass D: Type Reference Resolution & Type Checking (type_reference_resolver)
    │
[Phase 5] Global Model Sanity Verification (verify_mangled_names)
    │
[Phase 6] LLVM IR Code Generation:
    ├── Declaration Generator (declaration_generator)
    └── Implementation Generator (implementation_generator)
    │
[Phase 7] LLVM Verification & Optimization Pipeline
    │
[Phase 8] Target Emission & Linkage (compiler_linker)
```

### Phase 0: Source Loading & Buffer Pinning
All source files (`path` and `content`) are loaded into `compiler::_sources`. A single `.reserve()` guarantees pointer stability. The boolean flag `_sources_locked` is set to `true`, preventing subsequent source additions that could invalidate string views.

### Phase 1: Module Name Discovery
Before full parsing, `k::parse::lookup_module_name()` performs a fast scan of the initial tokens of each source file to detect `module <name>;`.
- If a CLI `--module-name` is passed, it overrides in-source declarations.
- All sources in a compilation unit must declare the identical module name; conflicting declarations trigger `ERR_CONFLICTING_MODULE_DECL`.
- If no file declares a module, a fallback unit name is generated with a warning (`WARN_NO_MODULE_DECL`).

### Phase 2: Lexing & Parsing
Each source is tokenized by `k::lex::lexer` and parsed by `k::parse::parser` into a `k::parse::ast::unit`.
- The parser employs recursive descent with operator-precedence climbing for expressions.
- Doc comments (`/** ... */`) are parsed and attached to declaration AST nodes.
- Per-file ASTs are merged into a single composite `_ast_unit`: imports, namespace contents, and top-level declarations are concatenated.

### Phase 3: Semantic Model Building & Import Materialization
1. **Model Building**: `k::model::model_builder` visits the composite AST and constructs the semantic model tree rooted at `k::model::unit`.
2. **Implicit Stdlib Injection**: Unless the module being compiled is named `k` (the standard library base), the compiler automatically injects an implicit `import k;` into the unit.
3. **KDI Import Processing**: `k::model::kdi_importer` resolves imported modules using `k::file_resolver`:
   - Searches local directories, `-I` search paths, `KLANG_LIB_PATH` environment paths, and system directories.
   - Loads `.kdi` metadata transitively.
   - Materializes imported symbols (`imported_aggregate`, `imported_function`, `imported_variable`) into the compilation context.

### Phase 4: Semantic Resolution Passes
The semantic analysis pipeline consists of four main passes:
1. **Pass A (`symbol_resolver`)**:
   - Resolves lexical scopes, binds symbol expressions (`symbol_expression`) to definitions (`variable_definition`, `function`, `parameter`).
   - Builds initial vtable layouts and universal destructor slot assignment (Slot 0).
   - Validates visibility (`public`, `protected`, `private`), friendship, and annotations (`@Target`, `@Retention`).
   - Resolves redirector chains (`-> target;`) and detects circular redirections.
2. **Pass B (`aggregate_type_resolver`)**:
   - Resolves struct, class, interface, and union structural definitions and function signatures (parameters and return types).
   - Performs on-demand template instantiations (`template_instantiator`), generating new concrete model aggregates.
   - Invokes `context::resolve_types()` to construct LLVM struct types.
3. **Pass C (`model_materializer`)**:
   - Validates class abstract methods and vtable slot consistency.
   - Computes secondary vtable thunk specifications (calculating `this`-pointer adjustment byte offsets via LLVM DataLayout).
4. **Pass D (`type_reference_resolver`)**:
   - Performs full expression-level type inference and type checking.
   - Checks assignment compatibility, operator overloading, and callable conversions.
   - Validates exception propagation (`throws` specifications) against enclosing `try-catch` scopes.
   - Enforces variable initialization ordering (`resolvers_init_order.cpp`).
   - Verifies that all declared imports were utilized, issuing `WARN_UNUSED_IMPORT` diagnostics when appropriate.

### Phase 5: Mangled Name Verification
`compiler::verify_mangled_names()` scans all named elements in the model to guarantee:
- No element produces an empty mangled name.
- No two distinct elements produce identical mangled names (detecting mangler collisions early before COMDAT/link-time symbol corruption).

### Phase 6: Code Generation (LLVM IR)
Code generation is cleanly partitioned into two visitor passes over `k::model::unit`:
1. **Declaration Generator (`declaration_generator`)**:
   - Creates `llvm::Function` declarations for all functions, methods, constructors, destructors, and synthetic thunks.
   - Creates `llvm::GlobalVariable` definitions for global variables, RTTI structures, vtables, and constant string literals.
2. **Implementation Generator (`implementation_generator`)**:
   - Emits LLVM BasicBlocks and instructions for function bodies, control flow (`if`, `while`, `for`, `foreach`, `break`, `continue`), exception throwing/catching (`invoke`, `landingpad`, `resume`), and member access.
   - Lowers constructors (C1 complete object vs. C2 base subobject) and destructors (D1 complete object vs. D2 base subobject).
   - Generates DWARF debug info (`k::model::gen::debug_info_generator`).

### Phase 7: Optimization & Verification
- `llvm::verifyModule()` is invoked to guarantee IR structural validity.
- The optimization pipeline runs standard LLVM transform passes (InstCombine, GVN, Scalar optimizations) when optimization is enabled.

### Phase 8: Artifact Generation & Linking
`compiler_linker.cpp` handles final output generation:
- **JIT Execution**: `compiler::to_jit()` initializes LLVM ORC / LLJIT, resolves symbols, executes runtime initialization, and invokes entry points.
- **Object Emission**: `compiler::gen_object_file()` emits native object code (`.o`) via LLVM target machine.
- **Executable Linking**: `compiler::gen_executable()` drives `clang++` with `-pie`, linking the generated object, extra objects (`-o`), stdlib (`-lk`), and imported libraries (`-L`, `-l`).
- **Library Generation**: Emits shared libraries (`.so`) via `clang++ -shared` or static archives (`.a`) via `llvm-ar` / `ar rcs`.
- **KDI Export**: `k::model::kdi_exporter` serializes the module's public declarations, layouts, and template ASTs into `.kdi` (CBOR) and optional `.kdi.json`.

---

## 3. Subsystem Decomposition

| Subsystem | Namespace / Paths | Primary Responsibilities |
|-----------|-------------------|--------------------------|
| **Driver & CLI** | `k`, `src/compiler*.cpp`, `src/klang.cpp` | CLI parsing, pipeline orchestration, diagnostics rendering, linker invocation. |
| **Lexer** | `k::lex`, `src/lex/` | Unicode handling, tokenization (`lex::lexeme`), indentation/newlines, doc-comment extraction. |
| **Parser** | `k::parse`, `src/parse/` | Syntax analysis, AST node construction (`parse::ast::*`), operator precedence. |
| **Semantic Model** | `k::model`, `src/model/` | OOP semantic graph (`ns`, `aggregate`, `klass`, `function`, `statement`, `expression`), Type hierarchy (`k::model::type`), Mangler (`k::model::mangler`). |
| **Template Engine** | `k::model`, `src/model/template*` | Template deduction, AST cloning, parameter substitution, instantiation caching. |
| **Resolvers** | `k::model::gen`, `src/gen/resolvers*` | Multi-pass name resolution, scope traversal, overload selection, layout synthesis, type inference. |
| **Code Generators** | `k::model::gen`, `src/gen/gen_*` | LLVM IR lowering, vtable/RTTI generation, constructor/destructor lowering, callable adapters, DWARF debug info. |
| **KDI Subsystem** | `k::model`, `src/model/tools/kdi*`, `libkdi/` | Metadata serialization/deserialization (CBOR/JSON), header validation, cross-module type mapping. |

---

## 4. Diagnostics & Error Handling

`klangc` uses a structured diagnostic architecture centered on `k::log::logger` and `k::log::diagnostic`:
- Diagnostics carry a **severity** (`trace`, `debug`, `info`, `warning`, `error`, `fatal`), a numeric **code** (e.g. `k::diag::compiler_diag`, `k::diag::callable_diag`), a formatted message, and optional source locations (`k::lex::lexeme` or `k::char_pos`).
- Source snippets with line, column, and underlining are rendered by `compiler_diagnostic.cpp`.
- Recoverable errors allow the compiler to continue analyzing later declarations to collect multiple diagnostics before halting.
- Unrecoverable semantic errors throw `k::model::gen::resolution_error` or `k::log::compiler_error`, which are caught at phase boundaries to safely abort code generation.

---

## 5. Architectural Boundaries & Invariants Summary

1. **AST is Read-Only during Model Building**: AST nodes are treated as immutable input once parsed; all semantic augmentations reside solely in the Model graph.
2. **Model Elements Own No LLVM Pointers**: Model classes (`aggregate`, `function`, `variable_definition`) never store `llvm::Value*` or `llvm::Type*` fields directly. All LLVM entities are indexed in `k::model::context` maps.
3. **Template Instantiations use `linkonce_odr` + COMDAT**: Concrete template instantiations emitted in multiple compilation units are marked `LinkOnceODR` in matching COMDAT groups to ensure duplicate definitions are safely deduplicated by the linker.
4. **Exceptions Use Itanium C++ ABI**: K exceptions lower to standard LLVM landing pads, personality functions, and runtime unwinding routines compatible with the system C++ runtime.
