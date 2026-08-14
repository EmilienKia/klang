# How-To: Debugging Compiled K Programs with GDB / LLDB

This guide explains how to compile a **K language program** with native debug symbols and debug the resulting executable using **GDB** or **LLDB**.

---

## 1. Compiling a K Program with Debug Symbols

To inspect variables, view accurate source lines, and step through lexical blocks in a debugger, compile your K source files with the debug flags enabled.

### 1.1 Full DWARF Debug Information (`-g`)

```bash
# Compile and link with full DWARF metadata (default DWARF 5)
klangc -g myprog.k -o myprog
```

Or using the long option:

```bash
klangc --debug myprog.k -o myprog
```

### 1.2 DWARF Version Selection

Depending on your toolchain or debugger version, you can specify the target DWARF version:

```bash
# Force DWARF 4
klangc -g --gdwarf-4 myprog.k -o myprog

# Force DWARF 5 (default)
klangc -g --gdwarf-5 myprog.k -o myprog
```

### 1.3 Stepping Only (Line Tables)

If you only need line numbers for stack traces and stepping without variable inspection overhead:

```bash
klangc --gline-tables-only myprog.k -o myprog
```

### 1.4 Compiling Multi-File Programs and Dependencies

When compiling programs that import other modules:

```bash
klangc -g -I /path/to/kdi -L /path/to/libs main.k helper.k -l mylib -o myprog
```

Ensure the runtime shared library directory is present in `LD_LIBRARY_PATH` when running dynamically linked programs:

```bash
export LD_LIBRARY_PATH="/path/to/libs:$LD_LIBRARY_PATH"
```

---

## 2. Debugging with GDB

### 2.1 Starting GDB

```bash
gdb ./myprog
```

To run with command-line arguments:

```bash
gdb --args ./myprog arg1 arg2
```

### 2.2 Setting Breakpoints

You can set breakpoints on the `main` entry point, on specific functions, or on source file lines:

```gdb
# Break at main function
(gdb) break main

# Break at a specific line in K source
(gdb) break myprog.k:24

# Break on a specific function
(gdb) break calculate_total

# Break on exception throws
(gdb) catch throw
```

### 2.3 Running and Stepping

```gdb
(gdb) run                  # Start the program
(gdb) next                 # Step over (source line)
(gdb) step                 # Step into (source line)
(gdb) finish               # Run until current function returns
(gdb) continue             # Continue execution until next breakpoint
```

### 2.4 Inspecting Variables and Stack Frames

```gdb
(gdb) bt                   # Print backtrace
(gdb) frame 0              # Select stack frame 0
(gdb) info locals          # Print all local variables in current scope
(gdb) info args            # Print function parameters
(gdb) print x              # Print variable x
(gdb) print *obj           # Dereference and print object structure
(gdb) whatis x             # Display type of variable
```

---

## 3. Debugging with LLDB

### 3.1 Starting LLDB

```bash
lldb ./myprog
```

To pass arguments inside LLDB:

```bash
lldb -- ./myprog arg1 arg2
```

Or configure arguments interactively:

```lldb
(lldb) settings set target.run-args arg1 arg2
```

### 3.2 Setting Breakpoints in LLDB

```lldb
# Break at main function
(lldb) breakpoint set --name main

# Break at source line
(lldb) breakpoint set --file myprog.k --line 24

# Break on specific function name
(lldb) breakpoint set --name calculate_total

# List breakpoints
(lldb) breakpoint list
```

### 3.3 Running and Stepping

```lldb
(lldb) run                 # Start execution
(lldb) next                # Step over
(lldb) step                # Step into
(lldb) finish              # Step out
(lldb) continue            # Continue execution
```

### 3.4 Inspecting Variables and Frames in LLDB

```lldb
(lldb) bt                  # Show stack trace
(lldb) frame select 0      # Switch to frame 0
(lldb) frame variable      # List all locals and parameters in scope
(lldb) frame variable x    # Print specific variable
(lldb) expr x + 10         # Evaluate expression
```

---

## 4. Symbols, Mangling, and Native Names

Klang generates standard C/C++ ABI symbols for native interop and debugging.

### 4.1 Inspecting Symbols with `nm`

You can inspect the symbol table of your compiled executable:

```bash
# Demangled symbols
nm -C ./myprog | grep -v " __"

# Dynamic symbols in a shared library
nm -CD ./libmylib.so
```

### 4.2 Mangling Conventions

- **Global functions:** mangled based on namespace and parameter types.
- **Constructors / Destructors:** standard Itanium ABI markers (`C1`/`C2` for constructors, `D1`/`D2` for destructors).
- **Virtual methods:** accessible via the class vtable (`vptr`).

---

## 5. Crash-First Investigation Workflow

When diagnosing a segmentation fault (`SIGSEGV`) or unhandled exception crash:

1. **Launch under debugger without initial breakpoints:**
   ```bash
   gdb -batch -ex run -ex bt --args ./myprog
   ```
2. **Examine the crashing frame:**
   ```gdb
   (gdb) frame <crash_frame_number>
   (gdb) info locals
   (gdb) info args
   ```
3. **Verify pointer validity and bounds:**
   Check whether pointers are null, dangling, or if array indexes exceeded allocated buffers.
4. **Re-run with targeted breakpoints:**
   Set a breakpoint immediately before the failing call to inspect variable mutations step by step.

