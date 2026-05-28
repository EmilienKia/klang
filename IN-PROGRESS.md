# IN-PROGRESS: Exception Hierarchy Restructuring

**Feature:** Restructure the K exception model with `Throwable` as root, separating
checked exceptions (`Exception`) from unchecked fatal errors (`FatalError`).

**Status:** ✅ Complete

---

## 1. New Class Hierarchy

```
Object
└── Throwable (_code: int, getCode())       ← NEW root of all throwable types
    ├── Exception                            ← Checked: must be declared with `throws`
    │   ├── NullPointerException (code=2)
    │   ├── IndexOutOfBoundsException (code=3)
    │   ├── IllegalArgumentException (code=4)
    │   └── IllegalStateException (code=5)
    └── FatalError                           ← Unchecked: no `throws` declaration needed
        └── OutOfMemory (code=1)             ← Renamed from MemoryException
```

**Removed:** `RuntimeException` — no longer exists in K.

### Semantics

| Base class       | Throwable? | Needs `throws` declaration? | Caught by `catch`? |
|------------------|------------|-----------------------------|--------------------|
| `Exception`      | Yes        | **Yes** (checked)           | Yes                |
| `FatalError`     | Yes        | **No** (unchecked)          | Yes                |
| Other (non-Throwable) | No  | Cannot be thrown            | N/A                |

- **`throw` validation**: only types derived from `Throwable` can be thrown.
- **`throws` clause contract**: only applies to `Exception`-derived types.
  Functions that throw `FatalError` subtypes do NOT need to declare them.
- **`catch` dispatch**: polymorphic — `catch(e: Throwable&)` catches everything,
  `catch(e: FatalError&)` catches all fatal errors, etc.

---

## 2. Implementation Steps

### Step 1: K source — class hierarchy (`libk/libk/src/exception.k`)

- Add `class Throwable` inheriting from `Object`:
  - Move `_code` field and `getCode()` from `Exception` to `Throwable`
  - Add constructors `Throwable()` and `Throwable(code: int)`
- Change `Exception` to inherit from `Throwable`:
  - Remove `_code` field (now in `Throwable`)
  - Constructors delegate to `Throwable(code)`
- Add `class FatalError` inheriting from `Throwable`:
  - Constructors delegate to `Throwable(code)`
- Rename `MemoryException` → `OutOfMemory`:
  - Inherit from `FatalError` instead of `RuntimeException`
- **Remove `RuntimeException` entirely**
- Change `NullPointerException` to inherit from `Exception` directly
- Change `IndexOutOfBoundsException` to inherit from `Exception` directly
- Change `IllegalArgumentException` to inherit from `Exception` directly
- Change `IllegalStateException` to inherit from `Exception` directly

### Step 2: C runtime — `fatal.c` (throw mechanics)

- Update mangled symbol references:
  - Constructor: `_KFMN1k15MemoryExceptionC1Ev` → `_KFMN1k11OutOfMemoryC1Ev`
  - Typeinfo: `_KTRIN1k15MemoryExceptionE` → `_KTRIN1k11OutOfMemoryE`
  - Add `_KTRIN1k10FatalErrorE` and `_KTRIN1k9ThrowableE` to the chain
  - Remove `_KTRIN1k16RuntimeExceptionE` and `_KTRIN1k9ExceptionE` from chain
- Update typeinfo chain for new hierarchy:
  ```
  [OutOfMemory@0, FatalError@offset, Throwable@offset, Object@offset, {null,0}]
  ```
- Recalculate `K_MEMORY_EXCEPTION_SIZE` (object size with new hierarchy depth):
  - OutOfMemory { vptr, FatalError { vptr, Throwable { vptr, Object { vptr }, _code, pad } } }
  - = 4 vptrs × 8 + 1 int + pad = 40 bytes on LP64 (same depth as before)

### Step 3: Compiler — throw validation (`gen_statements.cpp`)

- **`throw` type check** (line ~740-770): change from checking `::k::Exception` to
  checking `::k::Throwable`. Any `Throwable`-derived type can be thrown.
- **`check_throw_contract`** (line ~1520-1545): only enforce contract for
  `Exception`-derived types. If the thrown type derives from `FatalError`,
  skip the contract entirely (no `throws` declaration needed).
- **`check_call_contract`** (line ~1547-1576): same — only propagate contract
  checking for `Exception`-derived throws specs.

### Step 4: Compiler — throws clause validation

- **`ERR_THROWS_NOT_EXCEPTION_TYPE`** (throws clause resolver): keep as-is —
  only `Exception`-derived types may appear in a `throws` clause (since FatalError
  doesn't need declaration, it makes no sense to declare it).
- **`ERR_THROW_NOT_EXCEPTION_TYPE`**: rename to `ERR_THROW_NOT_THROWABLE_TYPE`,
  update message to reference `Throwable`.
- **`ERR_CATCH_NOT_EXCEPTION_TYPE`**: rename to `ERR_CATCH_NOT_THROWABLE_TYPE`,
  update message to reference `Throwable`. Any `Throwable` subtype can be caught.

### Step 5: Compiler — catch clause validation

- Update catch type checking: a catch clause should accept any `Throwable`-derived
  type (not just `Exception`), since we want `catch(e: OutOfMemory&)` to work.

### Step 6: KDI — serialisation

- `kdi_aggregates.hpp`: `throws_spec` remains unchanged (stores type references)
- No structural change needed — just ensure the new class names export correctly

### Step 7: Tests

- Update `libk/libk/tests/test-exceptions.cpp`:
  - Remove all `RuntimeException` references
  - Rename `MemoryException` references → `OutOfMemory`
  - Add tests for `Throwable` and `FatalError` construction
  - Add test: FatalError can be thrown without `throws` declaration
  - Add test: Exception requires `throws` declaration
- Update `klang/tests/test-gen-exceptions.cpp`:
  - Update existing tests to use new class names
  - Remove RuntimeException references
  - Add tests for checked vs unchecked enforcement
- Comments in `gen_expr_memory.cpp`, `gen_expr_cast.cpp`, `gen_intrinsics.cpp`:
  update "MemoryException" → "OutOfMemory"

### Step 8: Documentation

- Update `doc/spec/stdlib/exceptions.md` with new hierarchy
- Update `doc/spec/language/summary.md` exception section
- Update error messages in `errors_gen.hpp`

---

## 3. Files Affected

| File | Change type |
|------|-------------|
| `libk/libk/src/exception.k` | Major rewrite: hierarchy restructuring |
| `libk/libk/src/fatal.c` | Mangled names, typeinfo chain, size |
| `klang/src/gen/gen_statements.cpp` | Throw/catch validation logic |
| `klang/src/errors_gen.hpp` | Diagnostic code renames + messages |
| `klang/src/gen/gen_expr_cast.cpp` | Comments only |
| `klang/src/gen/gen_expr_memory.cpp` | Comments only |
| `klang/src/gen/gen_intrinsics.cpp` | Comments only |
| `libk/libk/tests/test-exceptions.cpp` | Test updates |
| `klang/tests/test-gen-exceptions.cpp` | Test updates |
| `doc/spec/stdlib/exceptions.md` | Documentation |
| `doc/spec/language/summary.md` | Documentation |

---

## 4. Risks and Notes

1. **Object size of OutOfMemory**: With the new hierarchy, `OutOfMemory` has 4 levels of
   inheritance (Object → Throwable → FatalError → OutOfMemory), each adding a vptr.
   This means the object size = 4×8 + 4(int) + 4(pad) = 40 bytes. Same as before
   (Object → Exception → RuntimeException → MemoryException). ✓ No size change.

2. **Backward compatibility**: Existing user code that catches `Exception&` will still
   catch `NullPointerException`, `IndexOutOfBoundsException`, etc. (they inherit from
   Exception). However it will NOT catch `OutOfMemory` anymore (now under FatalError).

3. **RuntimeException removed**: All references to `RuntimeException` in the codebase
   (tests, fatal.c, docs) must be purged.

---

*Delete this file when feature is complete.*
