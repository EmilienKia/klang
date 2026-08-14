---
description: Debug the klangc compiler itself with gdb/lldb, including phase breakpoints, AST/model inspection, and crash diagnosis.
---

# `klangc` Compiler Debugging with gdb/lldb

## Goal
Debug the `klangc` compiler binary itself when investigating compiler crashes (`SIGSEGV`, stack overflows), unexpected compiler diagnostics, symbol resolution failures, or IR codegen issues.

## Prerequisites
- Build `klangc` in the debug build environment:
  ```bash
  cd cmake-build-debug && ninja -j3 klangc
  ```
- Have `gdb` or `lldb` installed on the host.
- Prepare a minimal reproducer K source file (e.g. `/tmp/repro.k`).

## 1) Launch `klangc` Under Debugger

### With GDB
```bash
gdb --args ./cmake-build-debug/klang/klangc --log-level trace /tmp/repro.k -o /tmp/repro
```

### With LLDB
```bash
lldb -- ./cmake-build-debug/klang/klangc --log-level trace /tmp/repro.k -o /tmp/repro
```

## 2) Strategic Breakpoints by Compiler Phase

The compilation pipeline executes in strict sequential order inside `k::compiler::parse_sources()` (`klang/src/compiler.cpp`):

| Pipeline Phase | Primary Source File | GDB / LLDB Breakpoint Target |
|---|---|---|
| **Frontend: Lexing** | `klang/src/lex/lexer.cpp` | `break k::lex::lexer::next_lexeme` |
| **Frontend: Parsing** | `klang/src/parse/parser.hpp` / `parser_*.cpp` | `break k::parse::parser::parse_unit` |
| **Model Building** | `klang/src/model/model.cpp` | `break k::model_builder::build_unit` |
| **KDI Imports** | `klang/src/model/tools/kdi_importer.cpp` | `break k::kdi_importer::import_module` |
| **Resolver Pass A (Symbols)** | `klang/src/gen/resolvers_symbol.cpp` | `break k::symbol_resolver::resolve` |
| **Resolver Pass B (Aggregates)** | `klang/src/gen/resolvers_aggregate.cpp` | `break k::aggregate_type_resolver::resolve` |
| **Resolver Pass C (Materializer)** | `klang/src/gen/resolvers_materializer.cpp` | `break k::model_materializer::materialize` |
| **Resolver Pass D (Type Refs)** | `klang/src/gen/resolvers_type_ref.cpp` | `break k::type_reference_resolver::resolve` |
| **Codegen: Declarations** | `klang/src/gen/gen_unit.cpp` | `break k::gen::declaration_generator::generate` |
| **Codegen: Implementation** | `klang/src/gen/gen_unit.cpp` | `break k::gen::implementation_generator::generate` |
| **Codegen: Expressions** | `klang/src/gen/gen_expressions.cpp` | `break k::gen::implementation_generator::visit(k::model::expr_cast const&)` |
| **Linking & Artifacts** | `klang/src/compiler_linker.cpp` | `break k::compiler_linker::link_executable` |

## 3) Compiler Data Inspection Commands

### Inspecting LLVM State
```gdb
# GDB:
(gdb) call llvm_module->dump()
(gdb) call llvm_value->dump()

# LLDB:
(lldb) expr llvm_module->dump()
(lldb) expr llvm_value->dump()
```

### Inspecting Model AST / Types
```gdb
(gdb) print node->get_name()
(gdb) print node->get_type()->to_string()
(gdb) info locals
```

## 4) Fast Diagnostic Heuristics for Compiler Crashes

1. **Stack overflow during parsing:**
   - Symptom: Repeated calls to `parse_primary_expr` or `parse_expression` in stack backtrace.
   - Root cause: Lookahead helper failing to advance lexer stream before recurring.
2. **SIGSEGV / Null pointer in resolver or codegen:**
   - Symptom: Crash on `get_type()`, `get_scope()`, or `get_llvm_type()`.
   - Inspection: Check if symbol resolution failed silently or if type reference pass did not materialize the type.
3. **Mangled Name Collision / Duplicate Symbol:**
   - Check `klang/src/model/mangler.cpp` and compare with `--emit-raw-ir`.

## Deliverable
- Exact failing phase and stack frame backtrace.
- Target AST/model node or LLVM IR instruction triggering the failure.
- Root cause diagnosis and suggested fix location in `klang/src/`.

