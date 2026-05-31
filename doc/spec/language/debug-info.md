# Debug Information and DWARF Emission

[← Index](index.md)

Klang can emit native debug information in **DWARF** format alongside the generated LLVM IR, object files, shared libraries and executables.

The debug metadata is designed to let a debugger recover:

- the compilation unit and source file table,
- function boundaries,
- parameter and local variable locations,
- nested lexical scopes,
- source locations for control-flow instructions.

---

## 1. Enabling debug emission

Debug information is emitted only when the compiler is configured for it.

### Full debug metadata

The `-g` / `--debug` mode enables full DWARF emission.

In this mode, Klang emits:

- `llvm.dbg.declare` entries for parameters and local variables,
- lexical block metadata for nested scopes,
- DWARF sections in the produced native artifact.

### Line tables only

The `--gline-tables-only` mode emits only line tables.

This mode keeps file/line/column mappings for stepping, but omits the richer variable and lexical-scope metadata that full debug mode provides.

### DWARF version

The DWARF version can be selected explicitly:

- `--gdwarf-4`
- `--gdwarf-5`

When no version is forced, the compiler defaults to DWARF 5.

---

## 2. Source location model

Klang associates debug locations with the original lexical tokens produced by the parser.

The debug location attached to an instruction is resolved from:

1. the lexeme that introduced the construct,
2. the source file recorded by that lexeme,
3. the line and column stored in the source map.

When a construct does not have a direct source lexeme, the compiler falls back to the enclosing statement or to the module fallback file used for generated code.

### File entries

Each distinct source path referenced by the compilation unit is materialised as a DWARF file entry.

This includes:

- regular source files,
- sources read from standard input,
- fallback files for generated or implicit code.

---

## 3. Lexical scopes

Klang emits lexical scopes so that nested blocks remain visible to debuggers.

### Blocks

Every statement block creates a nested lexical block when debug info is enabled and the block contains generated code.

This means that normal block nesting is reflected directly in the DWARF scope tree.

### Nested loops

Loop bodies inherit the surrounding scope, and a block-form body introduces its own nested lexical block.

As a result, nested loops are represented cleanly in debug metadata whenever their bodies are blocks:

- a `while` inside another `while` gets a distinct nested scope for each block body,
- a `for` inside a `while`, or vice versa, remains visible as a separate scope level,
- variables declared inside the inner loop body are represented in the inner lexical block, not in the outer one.

### Exception blocks

Each `catch` clause introduces its own lexical block.

Nested `try-catch` constructs therefore produce nested exception scopes, and each catch parameter is emitted as a local variable in the corresponding catch block.

The compiler also preserves the surrounding function scope so that catch blocks remain attached to the correct parent lexical context.

---

## 4. Variables and parameters

### Function parameters

Function parameters are emitted as debug variables in the function subprogram scope.

Their debug declaration records:

- the parameter name,
- the parameter index,
- the parameter type,
- the source location of the parameter declaration when available.

### Local variables

Local variables are emitted with `llvm.dbg.declare` once their storage is materialised.

The compiler tracks the variable in the current lexical scope so that a debugger can inspect it at the point where the declaration becomes live.

This applies to:

- plain local variables,
- variables declared in a block,
- condition variables in control-flow statements,
- catch parameters.

---

## 5. Control-flow locations

Klang assigns meaningful source locations to the IR instructions that control execution flow, not only to the high-level statements themselves.

### `if`

The branch generated for an `if` statement uses the location of the `if` keyword or of the active `else` keyword when appropriate.

### `while`

The loop condition and the back-edge branch use the `while` statement location.

This keeps debugger stepping aligned with the loop header instead of the internal basic blocks that implement the condition test.

### `for`

The `for` statement uses the loop header location for the condition branch and the back-edge branch.

The step expression keeps its own source location, so stepping through a `for` loop can distinguish between:

- the loop test,
- the loop body,
- the step expression.

### `break` and `continue`

`break` and `continue` are lowered using the innermost active loop context.

The emitted branch instructions inherit the control-flow statement location, and any required cleanup runs before the exit is taken.

This ensures that nested loops remain debuggable even when the compiler inserts cleanup edges or temporary control-flow blocks.

### `try-catch`

The control-flow plumbing for exception handling keeps the location of the surrounding `try` or `catch` header.

In particular:

- the `try` entry branch is associated with the `try` statement,
- each `catch` body starts in its own catch scope,
- nested catch propagation preserves the most specific enclosing catch location,
- landing-pad plumbing does not displace the source location of the user-visible construct.

---

## 6. Emission summary

In full debug mode, Klang currently emits the following DWARF-visible information:

- compile unit metadata,
- source file entries,
- function subprogram metadata,
- parameters and locals,
- nested lexical blocks,
- source locations for loop, branch, and catch plumbing.

In line-tables-only mode, only the source location part of this list is retained.

---

## 7. Practical debugging behaviour

A debugger should therefore observe:

- function entry at the function declaration location,
- locals becoming visible at their declaration point,
- nested loop bodies as distinct scopes,
- nested catch blocks as distinct scopes,
- control-flow stepping anchored on the source statement that generated the branch.

This makes stepping through nested loops and nested exception handlers follow the structure of the K source rather than the compiler's internal basic blocks.


