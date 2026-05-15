# UniSlot<T> — Intrinsic Memory Primitive Implementation

## Status: IMPLEMENTED (Phase 1-5 complete)

## Summary

Introduced `UniSlot<T>` — a template struct in the K standard library that separates
memory allocation from object construction/destruction. The compiler recognises
`@annotations::Intrinsic` annotations and generates specialised IR for annotated
constructors/destructors/methods.

---

## What was implemented

### Phase 1 — `@Intrinsic` annotation (`libk/libk/src/annotations.k`)
- Added `@annotations::Intrinsic { name : const char[]; }` with `@Retention(Policy::SOURCE)`.

### Phase 2 — `UniSlot<T>` K source (`libk/libk/src/memory.k`)
- Template struct with `_slot : T` storage.
- Constructor/destructor annotated `@Intrinsic(...)` → no member init/destroy.
- `construct()` and `destruct()` methods annotated `@Intrinsic(...)` → placement ctor/dtor.
- `get() : T&` accessor for the stored object.

### Phase 3 — Intrinsic detection (`klang/src/gen/gen_intrinsics.hpp/.cpp`)
- `get_intrinsic_name(fn)`: extracts intrinsic name from `@Intrinsic` annotations.
- Falls back to raw annotation name matching (handles pre-resolution and template instantiation).
- Falls back to function-context-based intrinsic name derivation.

### Phase 4 — Suppression of auto init/destroy
- `gen_constructor.cpp`: `type_reference_resolver::visit_constructor` skips member-init
  injection for intrinsic constructors.
- `gen_constructor.cpp`: `type_reference_resolver::visit_destructor` skips member-dtor
  injection for intrinsic destructors.
- `gen_function.cpp`: `emit_destructor_cleanup` skips member/base dtor calls for intrinsic dtors.
- `gen_function.cpp`: `declaration_generator::visit_function` allows bodyless intrinsic functions.
- `gen_function.cpp`: `implementation_generator::visit_function` allows bodyless intrinsic
  functions through the late-materialisation path.

### Phase 5 — Intrinsic IR generation
- `gen_function.cpp`: Intrinsic dispatch in `implementation_generator::visit_function`:
  - `UniSlot::constructor` / `UniSlot::destructor` → empty body (`ret void`).
  - `UniSlot::construct` → GEP to `_slot`, find T's default ctor, call it.
  - `UniSlot::destruct` → GEP to `_slot`, find T's dtor, call it.
- `gen_intrinsics.cpp`: `emit_intrinsic_unislot_construct` and `emit_intrinsic_unislot_destruct`.

### Phase 6 — Template instantiation fix
- `template_instantiator.cpp`: `populate_function_from_template` now copies annotations
  to cloned functions (preserves `@Intrinsic` through template instantiation).

### Phase 7 — Tests (`klang/tests/test-gen-intrinsic.cpp`)
6 test cases, all passing:
1. UniSlot with primitive type (int) — construct and get
2. UniSlot does NOT auto-construct T
3. UniSlot::construct invokes T's constructor
4. UniSlot does NOT auto-destruct T
5. UniSlot::destruct invokes T's destructor
6. Full lifecycle: construct + use + destruct

---

## Open items (deferred)

1. **Variadic construct** — `construct(args...)` with argument forwarding to T's constructor.
   Currently only zero-arg `construct()` is supported.
2. **`operator() : T&`** — Deferred pending grammar verification.
3. **MultiSlot<T>** — Array-based variant for collections.
4. **KDI export** — Template intrinsics are not yet exportable through KDI.
5. **Stdlib integration** — `memory.k` is in LIBK_SOURCES but the template won't be
   usable from imported KDI until template import is fully functional.

---

## Files modified/created

| File | Action |
|------|--------|
| `libk/libk/src/annotations.k` | Added `@Intrinsic` annotation |
| `libk/libk/src/memory.k` | Replaced placeholder with `UniSlot<T>` |
| `klang/src/gen/gen_intrinsics.hpp` | **New** — `get_intrinsic_name()` declaration |
| `klang/src/gen/gen_intrinsics.cpp` | **New** — Intrinsic detection + IR generation |
| `klang/src/gen/gen_constructor.cpp` | Skip init injection for intrinsic ctors/dtors |
| `klang/src/gen/gen_function.cpp` | Intrinsic dispatch + bodyless function support |
| `klang/src/gen/generators.hpp` | Declared `emit_intrinsic_unislot_*` methods |
| `klang/src/model/template_instantiator.cpp` | Copy annotations during instantiation |
| `klang/CMakeLists.txt` | Added `gen_intrinsics.cpp` + test file |
| `klang/tests/test-gen-intrinsic.cpp` | **New** — 6 test cases |
