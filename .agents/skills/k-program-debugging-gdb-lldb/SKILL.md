---
description: Debug a compiled K program with gdb/lldb, including build flags, breakpoints, run control, and backtraces.
---

# K Program Debugging with gdb/lldb

## Goal
Debug a K source program after compilation to a native executable.

## Prerequisites
- Build `klangc` in debug environment:
  ```bash
  cd cmake-build-debug && ninja -j3 klangc
  ```
- Have `gdb` or `lldb` installed on the host.

## 1) Compile a K program with debug-friendly settings

```bash
./cmake-build-debug/klang/klangc -g path/to/program.k -o /tmp/program_dbg
```

Notes:
- Always pass `-g` (or `--debug`) to emit full DWARF metadata (variable locations, nested lexical blocks, scopes).
- Use `--gdwarf-4` or `--gdwarf-5` to target specific DWARF versions if required.
- If stepping only is required without variable metadata, use `--gline-tables-only`.
- If needed, add compiler trace during diagnosis:
  ```bash
  ./cmake-build-debug/klang/klangc -g --log-level trace path/to/program.k -o /tmp/program_dbg
  ```

## 2) Debug with gdb

### Launch
```bash
gdb /tmp/program_dbg
```

### Core commands
```gdb
set pagination off
break main
run
next
step
finish
continue
bt
frame 0
info locals
info args
print <expr>
```

### Useful variants
```gdb
run arg1 arg2
break <symbol_name>
watch <expr>
catch throw
```

## 3) Debug with lldb

### Launch
```bash
lldb /tmp/program_dbg
```

### Core commands
```lldb
breakpoint set --name main
run
next
step
finish
continue
bt
frame select 0
frame variable
expr <expr>
```

### Useful variants
```lldb
run -- arg1 arg2
breakpoint set --name <symbol_name>
watchpoint set variable <var_name>
```

## 4) Crash-first workflow (recommended)

1. Reproduce crash under debugger (`run`).
2. Capture full backtrace (`bt`).
3. Inspect current frame locals/args.
4. Move up stack frame-by-frame to identify bad state propagation.
5. Re-run with targeted breakpoints before the failing site.

## 5) Symbol and name tips

- If source-level names are hard to find, list symbols from binary:
  ```bash
  nm -C /tmp/program_dbg | head -200
  ```
- When debugging generated/linked behavior involving imported modules, break on nearby known runtime symbols (`main`, helper functions, throw paths) and step into calls.

## Deliverable
- Repro command line.
- Exact failing frame/backtrace.
- Suspected root-cause function and variable state at failure point.

