# How-To: Debugging the `klangc` Compiler with GDB / LLDB

This guide explains how to build, run, and step through the **`klangc` compiler** itself using native debuggers (**GDB** or **LLDB**). Use this workflow when diagnosing compiler crashes (`SIGSEGV`, infinite recursion, assertion failures), miscompilations, type resolution issues, or unexpected diagnostic errors.

---

## 1. Prerequisites and Debug Build

Ensure the compiler is compiled in `Debug` configuration (producing full unoptimized DWARF symbols).

```bash
# Configure the workspace for Debug build
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug

# Build the klangc binary
cmake --build cmake-build-debug --target klangc
```

The resulting debug executable is located at `cmake-build-debug/klang/klangc`.

---

## 2. Preparing a Minimal Reproduction File

Create a minimal K source file (e.g., `/tmp/repro.k`) that triggers the compiler behavior or crash:

```k
// /tmp/repro.k
module repro;

class MyTest {
    var value: int;

    public create(v: int) {
        this.value = v;
    }
}

public main() : int {
    var t: MyTest(42);
    return t.value;
}
```

---

## 3. Running `klangc` Under GDB

### 3.1 Launching GDB

Run GDB passing `klangc` and the compilation arguments:

```bash
gdb --args ./cmake-build-debug/klang/klangc /tmp/repro.k -o /tmp/repro
```

Or start GDB and set arguments inside:

```bash
gdb ./cmake-build-debug/klang/klangc
(gdb) set args /tmp/repro.k -o /tmp/repro
```

### 3.2 Useful `klangc` Diagnostic Flags During Debugging

When passing arguments, consider these flags to gain visibility into compiler internals:

```gdb
(gdb) set args --log-level trace --emit-raw-ir --raw-ir-file=/tmp/repro.raw.ll /tmp/repro.k -o /tmp/repro
```

- `--log-level trace` : prints detailed diagnostic and phase transitions to stdout/log.
- `--emit-raw-ir` : dumps LLVM IR prior to optimization.
- `--emit-opt-ir` : dumps LLVM IR post-optimization.
- `-I <dir>` / `-L <dir>` : sets KDI / library lookup paths.
- `--module-name <name>` : forces explicit module name.

### 3.3 Setting Breakpoints by Compiler Phase

The compilation pipeline executes in strict chronological phases inside `k::compiler::parse_sources()` (`src/compiler.cpp`):

| Phase | Target File / Function | Breakpoint Example |
|---|---|---|
| **Pipeline Entry** | `compiler::parse_sources` | `break k::compiler::parse_sources` |
| **Lexer** | `lex::lexer::next_lexeme` | `break k::lex::lexer::next_lexeme` |
| **Parser AST Construction** | `parse::parser::parse_unit` | `break k::parse::parser::parse_unit` |
| **Parser Declarations** | `parse::parser::parse_declaration` | `break k::parse::parser::parse_declaration` |
| **Parser Statements** | `parse::parser::parse_statement` | `break k::parse::parser::parse_statement` |
| **Model Building** | `model_builder::build_unit` | `break k::model_builder::build_unit` |
| **KDI Import Resolution** | `kdi_importer::import_module` | `break k::kdi_importer::import_module` |
| **Symbol Resolver (Pass A)** | `symbol_resolver::resolve` | `break k::symbol_resolver::resolve` |
| **Aggregate Resolver (Pass B)** | `aggregate_type_resolver::resolve` | `break k::aggregate_type_resolver::resolve` |
| **Model Materializer (Pass C)** | `model_materializer::materialize` | `break k::model_materializer::materialize` |
| **Type Reference Resolver (Pass D)** | `type_reference_resolver::resolve` | `break k::type_reference_resolver::resolve` |
| **Declaration Codegen** | `declaration_generator::generate` | `break k::gen::declaration_generator::generate` |
| **Implementation Codegen** | `implementation_generator::generate` | `break k::gen::implementation_generator::generate` |
| **Statement Codegen** | `implementation_generator::visit(ast::stmt_*)` | `break k::gen::implementation_generator::visit(k::model::stmt_block const&)` |
| **Expression Codegen** | `implementation_generator::visit(ast::expr_*)` | `break k::gen::implementation_generator::visit(k::model::expr_cast const&)` |
| **Object / Binary Linking** | `compiler_linker::link_executable` | `break k::compiler_linker::link_executable` |

### 3.4 Essential GDB Commands

```gdb
(gdb) set pagination off
(gdb) run
(gdb) bt                      # Backtrace of all stack frames
(gdb) frame 2                 # Select stack frame 2
(gdb) info locals             # Display local variables
(gdb) info args               # Display function arguments
(gdb) step                    # Step into
(gdb) next                    # Step over
(gdb) finish                  # Run until current function returns
(gdb) continue                # Continue execution
(gdb) print <variable>        # Print variable value
```

---

## 4. Running `klangc` Under LLDB

### 4.1 Launching LLDB

```bash
lldb -- ./cmake-build-debug/klang/klangc /tmp/repro.k -o /tmp/repro
```

Or inside LLDB:

```bash
lldb ./cmake-build-debug/klang/klangc
(lldb) settings set target.run-args /tmp/repro.k -o /tmp/repro
```

### 4.2 Setting Breakpoints in LLDB

```lldb
(lldb) breakpoint set --name "k::compiler::parse_sources"
(lldb) breakpoint set --name "k::symbol_resolver::resolve"
(lldb) breakpoint set --name "k::gen::implementation_generator::generate"
(lldb) breakpoint set --file src/gen/gen_expr_cast.cpp --line 45
```

### 4.3 Essential LLDB Commands

```lldb
(lldb) run
(lldb) bt                     # Print backtrace
(lldb) frame select 1         # Select frame 1
(lldb) frame variable         # Print all arguments and locals in current frame
(lldb) expr <expression>      # Evaluate C++ expression
(lldb) step                   # Step into
(lldb) next                   # Step over
(lldb) finish                 # Finish current frame
(lldb) continue               # Resume execution
```

---

## 5. Inspecting Compiler Data Structures

### 5.1 LLVM Module and Value Inspection

Klang uses LLVM C++ objects for intermediate representation. Within the debugger, you can invoke LLVM dump methods:

```gdb
# In GDB:
(gdb) call llvm_module->dump()
(gdb) call llvm_value->dump()
(gdb) call llvm_type->dump()

# In LLDB:
(lldb) expr llvm_module->dump()
(lldb) expr llvm_value->dump()
```

### 5.2 Inspecting Model Elements

You can print AST and semantic model structures:

```gdb
(gdb) print node->get_name()
(gdb) print node->get_type()->to_string()
(gdb) print unit->get_root_namespace()->get_name()
```

---

## 6. Diagnosing Common Compiler Bugs

### 6.1 Crash on `SIGSEGV` or Null Dereference

1. Launch `gdb -batch -ex run -ex bt --args ./cmake-build-debug/klang/klangc /tmp/repro.k -o /tmp/repro`.
2. Find the topmost frame in `k::` code (ignore system libraries).
3. Inspect pointers in that frame (`info locals`, `print ptr`).
4. Trace if an unresolved symbol, missing type reference, or missing parent scope returned `nullptr`.

### 6.2 Infinite Recursion / Stack Overflow in Parser

If the backtrace contains thousands of frames repeating `parse_primary_expr` or `parse_expression`:
- The parser hit a branch where a sub-expression helper did not consume any lexeme before recurring.
- Place a breakpoint at the lookahead helper and observe `lexer.current()` index progression.

### 6.3 Linker Errors / Mangling Mismatches

- Pass `--emit-raw-ir` to inspect the exact mangled names emitted in LLVM IR.
- Inspect `src/model/mangler.cpp` by placing a breakpoint on `k::model::mangler::mangle_function` or `k::model::mangler::mangle_type`.


