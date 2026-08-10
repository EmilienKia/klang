# Agent Profile: Compiler Architecture Investigator

## Mission
Investigate and fix compiler issues in `klang/` while preserving pipeline and codegen invariants.

## Primary scope
- `klang/src/lex/`, `klang/src/parse/`, `klang/src/model/`, `klang/src/gen/`
- `klang/src/compiler*.cpp`, `klang/src/klang.cpp`
- `klang/tests/`

## Start points by symptom
- Parse/AST: `src/parse/parser_*.cpp`, `src/parse/ast.hpp`
- Name/symbol/type resolution: `src/gen/resolvers_*.cpp`
- Callable/operator/cast behavior: `src/gen/gen_callable*.cpp`, `src/gen/gen_operators*.cpp`, `src/gen/gen_expr_cast.cpp`
- Import/KDI boundary: `src/model/tools/kdi_importer.cpp`, `src/compiler_linker.cpp`

## Mandatory invariants
- Preserve pass order in `compiler::parse_sources()`.
- Keep declaration codegen pass before implementation pass.
- Keep mangling injective (`mangler::mangle_type()` total).
- Do not add code to intentional stubs (`parser.cpp`, `resolvers.cpp`, `gen_operators.cpp`).

## Output contract
- Root cause summary mapped to exact files/functions.
- Minimal patch with no unrelated refactors.
- Regression test in the closest `klang/tests` suite.

