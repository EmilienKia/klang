# IN-PROGRESS: MemoryException integration with `new`/`delete` and `MultiSlot<T>`

**Feature:** The `new` operator (single and array forms) and `MultiSlot<T>::allocate`/`reallocate`
must abort with a diagnostic when the underlying `malloc`/`realloc` returns null.

**Status:** ✅ Complete

---

## Implementation Summary

### What was done

1. **`fatal.c`**: Added `__k_fatal_memory_allocation()` — aborts with "fatal: memory
   allocation failed (out of memory)" message when malloc/realloc returns NULL.

2. **`generators.hpp`**: Added `get_or_declare_fatal_memory_function()` and
   `emit_alloc_null_check()` helper methods to `implementation_generator`.

3. **`gen_expr_memory.cpp`**: Inserted null-checks after all 5 `malloc` calls in
   `visit_new_expression` (single object, static array, dynamic array, static uniform,
   dynamic uniform).

4. **`gen_intrinsics.cpp`**: Inserted null-checks after `malloc` in
   `emit_intrinsic_multislot_allocate` and after `realloc` in
   `emit_intrinsic_multislot_reallocate`.

5. **`compiler_linker.cpp`**: Always link `-lk` (with `-L` and `-rpath` for the stdlib
   lib directory) for any module that isn't `k` itself. This ensures
   `__k_fatal_memory_allocation` is available at link time.

### Design decisions

- **Abort (not throw)**: The initial plan was to throw `MemoryException`, but this
  requires `__cxa_allocate_exception`/`__cxa_throw` which pull in `libstdc++`/`libc++abi`
  as a link dependency for every K executable. This is deferred to a future step that
  will add proper `-lstdc++` linking.

- **Always link libk**: Previously `-lk` was only added when the KDI import resolved.
  Now it's always added (except for module `k` itself) because the compiler emits
  references to libk symbols in generated code.

### All 13 test suites pass (100%)

---

*Delete this file when feature is complete.*
