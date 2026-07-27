## TODO and wish list
### K Language

#### Features to add

- **Template-qualified type references in non-expression contexts**
  - Ensure support (with the same diagnostics quality) in every context that accepts
    type names and deferred symbol references.

- Add templates advanced features (Phase 2+ — partial specialization, variadic templates, template template parameters, etc.)
  - [ ] Tests: name mangling tests for template entities
  - [ ] Partial and full template specialization
  - [ ] Template template parameters (`template<template<typename> class C>`)
  - [ ] Variadic template parameters (parameter packs, fold expressions)
  - [ ] `extern template` (explicit instantiation declarations)
  - [ ] Template aliases (`template<typename T> using Vec = Array<T, 16>`)
  - [ ] Concepts / type traits / static_if on template parameters
  - [ ] Standalone template enum declarations
  - [ ] Template constructors (independent of aggregate template)
  - [ ] SFINAE-like overload filtering based on template constraints
  - [ ] Variable templates (`template<typename T> const size : int = ...`)
  - [ ] Non-primitive value template arguments (enum constants, compile-time constant
        expressions, aggregates) — currently limited to `int`/`long`/`float`/`double`/
        `bool`/`char`/`string`/`nullptr`
  - export templates (Phase 3+ — separate compilation of template definitions and instantiations)
- Add unions, typed unions (discriminated/tagged unions à la std::variant)
    - [ ] Enum-based discriminant interrogation (`u.type()` → enum)
    - [ ] Union extension / inheritance (derive union from another union)
    - [ ] Polymorphic access when all alternatives share a common base class/interface
    - [ ] Cast union to alternative type (`(int) myUnion`)
    - [ ] Pattern matching / `match` expression on union alternatives
    - [ ] Template unions (`template<T> union Optional { ... }`)
    - [ ] RTTI-only discriminant optimization (all-class alternatives)
- Add state classes
- Better private visibility support
- Improve log and debug messages
- Add constant values expression computation at compile time, enhance compile-time evaluation capabilities
- Add static conditional statements and static compiler value definitions
- Add traits and compile-time type introspection capabilities
- Add support for separate compilation and module interfaces (e.g. `export` keyword, module partitions)
- Add concepts (and template constraints) for more expressive template programming and better error messages
- Add traits (Rust like)
- Exceptions: core support already implemented (`throw`/`try`/`catch`/`finally`/`rethrow`, `throws` clauses with
  checked-exception enforcement, KDI import/export of `throws` clauses, stdlib exception hierarchy, cause
  chaining). Remaining gaps:
    - [ ] Exception specifications on function pointer/reference types
    - [ ] `noexcept` conditional expression (`noexcept(expr)`)
    - [ ] Exception handling in static constructors/destructors
    - [ ] Unhandled FatalError diagnostic: when an uncaught FatalError propagates past `main()`, the runtime should print a diagnostic message (exception type, code, optional stack trace) before terminating the process
    - [ ] Destructor invocation during stack unwinding (RAII cleanup on throw)
    - [ ] `catch(...)` catch-all clause (catch any `Throwable`)
- Switch/case statements and expression
- With-block - temporary change 'this' scope for a block of code (e.g. `with (obj) { ... }` to access members directly)
- Add static code decoration and constraint (usage example : units of measurement)
- member reordering optimization
- boolean member bitfield optimization
- lambda expressions and closures, variable capture, and functional interfaces

#### Current bugs and gaps to fix

- Known generic call-site limitations (found by Phase 12, tracked for future fix):
    - [ ] **Generic constructor call with owner `T!` argument.** Symptom: instantiating a
      generic class whose constructor takes `T!` (owner) with a concrete `ConcreteType!`
      argument at the call site fails to compile. Root cause: the "uniform synthesis" model
      erases every generic type parameter `T` to `byte*` in the synthesized body, so a
      constructor parameter declared `v : T!` becomes `v : byte*!` in the emitted IR. The
      call-site overload resolver then tries to implicitly convert the caller's
      `ConcreteType!` (owner of a concrete aggregate) to `byte*!` (owner of an opaque byte
      pointer) and there is currently no adaptation path for owner-to-opaque-owner
      conversion (unlike plain pointers, where `ConcreteType* → byte*` already has an
      adaptation rule). Fix direction: extend `adapt_from_owner` / `gen_adapt_type.cpp`
      with an owner-erasure conversion symmetric to the existing pointer-erasure one, and
      make sure the destructor/move semantics of the erased `byte*!` correctly delegate to
      the concrete type's real destructor (needed because owners run cleanup code, unlike
      raw pointers). Repro: `test-gen-generic.cpp`, "Known-limitation: generic constructor
      with owner arg at call site" (`[.][generic][known-limitation]`).
    - [ ] **Member access on `T*` inside a generic body.** Symptom: inside a generic
      class/function body, writing `v.field` where `v : T*` fails to compile — this is a
      deliberate by-design restriction, not a regression. Root cause: the "uniform
      synthesis" model compiles the generic body exactly once, with every type parameter
      erased to `byte*` (an opaque pointer with no known fields), so the generated IR has no
      layout information to resolve `.field` against. Concrete field access can only happen
      where the concrete type is known, i.e. at the call site, outside the generic body.
      This is an architectural trade-off (single compiled body reused for every
      instantiation, vs. per-instantiation monomorphization like C++ templates) rather than
      a bug — proper support would require either (a) monomorphizing generic bodies per
      concrete type argument (large change: multiplies codegen work and drops the "compile
      generic body once" invariant relied upon elsewhere), or (b) a constrained-generics /
      concept system that lets the body describe the subset of `T`'s layout it needs (e.g.
      "T has field `x : int`") and synthesizes per-field accessor thunks — effectively a
      lightweight vtable-of-accessors passed alongside the erased pointer. Both are
      substantial, cross-cutting designs; recommend scoping as a dedicated future phase
      (concepts / type traits, already listed above) rather than a point fix. Repro:
      `test-gen-generic.cpp`, "Known-limitation: member access on generic T* in generic
      body" (`[.][generic][known-limitation]`).
    - [ ] **Explicit generic type args in a member method call on a non-generic host
      class** (`obj.method<Dog>(arg)`). Symptom: calling a generic *method* of an
      otherwise non-generic host class/struct with an explicit type argument list fails to
      resolve `Dog` as a type argument. Root cause (to be confirmed by deeper
      investigation, not yet root-caused to the same depth as the two items above):
      overload resolution for member-function calls in `resolvers_type_ref.cpp` /
      `gen_expr_invocation.cpp` appears to special-case explicit template argument lists
      primarily for calls where the *enclosing aggregate itself* is generic (so the
      argument-list parsing/binding path is wired through the aggregate's own template
      parameter substitution machinery); when the host class is a plain non-generic
      class but only the *method* is a generic (`generic<class T> method(...)`), the
      explicit `<Dog>` argument list is not threaded through the same substitution path
      and the call fails to bind. Fix direction: audit the explicit-template-argument
      parsing/binding path for member-function invocation expressions and ensure it does
      not assume the host aggregate is itself generic before accepting explicit type
      arguments for a per-method generic. Repro: `test-gen-generic.cpp`,
      "Known-limitation: explicit generic type args in member method call"
      (`[.][generic][known-limitation]`).
    - [ ] `ConcreteType! → byte*` implicit cast at generic setter sites (runtime returns 0 instead of value)
    - [ ] **Homonymous imported templates from different modules** (origin-aware homing
      of imported template definitions). Symptom: a consumer module imports two libraries
      that each export a top-level template with the same short name (e.g. both `boxa` and
      `boxb` export a template `Box<T>`); instantiating both as `boxa::Box<int>` and
      `boxb::Box<int>` should behave as two distinct types with independent layouts, but
      the two colliding template *definitions* are merged and the second import silently
      wins, giving wrong behaviour instead of a compile error. Root cause: re-injected
      imported templates are flattened into the consumer module's **root** namespace (via
      the `module <ns>;`-rename trick in `kdi_importer::materialise_template_def`), so two
      *imported* templates with the same short name from different modules (`a::Box`,
      `b::Box`) clash at the model/symbol level (the flatten dedups by short name in root).
      The instantiation `struct_type` registry is already collision-safe (keyed by an
      origin-qualified name via `unit::make_instantiation_registry_key`; see test
      `[template][instantiation][ns-collision]`), but the *model-level* symbol clash
      remains. **Why the naive fix is blocked**: simply homing the re-parsed template under
      `root::<origin>` (nested-`namespace` wrapping instead of the module-rename trick)
      breaks **unqualified access** to imported top-level symbols — `import mylib;`
      currently behaves like `using namespace mylib;`, so imported function templates must
      be reachable as bare `sum_pair<int>(...)`. Homing them under
      `::consumer::mylib::sum_pair` makes unqualified lookup fail (proven: it regresses the
      4 `[cross-tpl][consumer-inst]` function-template tests in `test-import.cpp`). The real
      fix is therefore **deeper than homing**: scope lookup (`resolvers_scope_lookup.cpp`)
      must resolve unqualified imported symbols from their origin namespaces (an
      `import`-as-`using-namespace` mechanism) so the flatten can be dropped; homonymous
      imports then naturally require qualification once that mechanism exists. This is a
      structural change to the import/scope-resolution model (affects every unqualified
      symbol lookup path), not a local fix — recommend scoping as a dedicated phase.
      Repro/target-behaviour test: `test-import.cpp`, "Known-limitation: homonymous imported
      templates from different modules" (`[.][import][template][homonym-imports]`).

### K Compiler

#### Features to add

- Add support for generating language bindings (e.g. C header generation) from K code
- Add iterative compilation mode for faster edit-compile-test cycles (e.g. via an interactive REPL or watch mode)
- Add support for incremental compilation and caching of intermediate results to speed up subsequent builds
- Add support for cross-compilation to different target architectures and platforms. Partial: `klangc
  --target <triple>` already selects the LLVM codegen target triple (see `klang/src/klang.cpp`); still
  missing is end-to-end cross-toolchain support (cross-linker/sysroot selection in
  `compiler_linker.cpp`, which currently always shells out to the host `clang`/`ar`).

#### Current bugs and gaps to fix

- [x] **FIXED — ARRAY-variant `foreach` re-evaluated its source expression on every
      iteration instead of exactly once.** `type_reference_resolver::visit_foreach_statement`
      (`gen/gen_statements.cpp`) built the `.size` test expression and the
      per-iteration `source[$index]` subscript from two *separate* `source_expr->clone()`
      calls, and the generated test expression lives in the loop's condition basic
      block, so it was re-evaluated at runtime on every iteration (standard loop-condition
      codegen — see `implementation_generator::visit_foreach_statement`). For a plain
      variable source (`for (x : int = arr)`) this was harmless (re-reading the same
      variable's address is idempotent and cheap), which is why the existing test suite
      never caught it. It became a real correctness/perf smell once the source was a
      non-trivial expression — most notably a temporary array literal built directly in
      the init expression (`for (x : int = int[]{1, 2, 30})`, added to support the
      primitive-array-literal foreach feature): the temporary array was reconstructed
      (all element stores re-executed) once per `.size` check *and* once more per
      successful iteration, i.e. `O(iterations × size)` redundant work instead of `O(size)`.
      Confirmed by manual stress-testing (not a permanent test, to avoid slow CI): a
      20 000-element `int[]{...}` foreach source took ~30s to JIT-compile/run vs ~1.6s
      for a 5 000-element one (~20× for 4× the size — the expected quadratic blow-up),
      though results stayed numerically correct in all cases tried (no crash, since LLVM
      keeps the temporary's `alloca` in the function's entry block — no per-iteration
      stack growth).
      - Fix applied: a hidden `$source` variable (mirroring the existing `$index`
        pattern) is now synthesized once in `type_reference_resolver::visit_foreach_statement`,
        bound to a single clone of `source_expr` via `constructor_invocation_expression`
        and resolved through `accept(*this)` (safe: it visits a fresh clone, not the
        original `source_expr` node). The `.size` test and the per-iteration subscript
        now read `$source` via `symbol_expression::from_variable()` instead of re-cloning
        `source_expr`. Routing support added to `foreach_statement` (`model/statements.hpp`
        / `.cpp`): a new `_source_var` field, `get_source_var()`/`set_source_var()`, and an
        `on_variable_defined()` branch routing the first ARRAY-kind hidden variable to it.
      - Follow-up codegen wrinkle: declaring a variable of type "reference to a *sized*
        array" (`T[N]&`) — exactly `$source`'s type, needed to keep `.size`/subscript
        resolution unchanged — unconditionally triggers a copy-initialisation codegen path
        (`gen_expr_construct.cpp`, "int[N]& : copy-initialise" case) that allocates a
        *fresh* local array buffer and copies elements into it, rather than aliasing the
        original array's storage. That is correct/intentional for user-facing `T[N]&`
        declarations (it supports size-mismatched copy/pad semantics) but would silently
        break mutation-through-reference semantics for this hidden alias variable.
        `implementation_generator::visit_foreach_statement` therefore constructs `$source`
        manually — direct `alloca` + address-store of the evaluated source, bypassing the
        generic copy-semantics codegen path entirely — instead of dispatching through the
        generic `variable_statement`/`constructor_invocation_expression` codegen used for
        ordinary reference variables.
      - Regression test: `[gen][foreach][array][temporary][regression]` in
        `klang/tests/test-gen-foreach.cpp` ("array literal source expression is evaluated
        exactly once") — a temporary array literal whose elements call a side-effecting
        counting function; asserts the function is called exactly once per element
        (`call_count == 3`), not once per iteration/condition-check.
      - **Follow-up investigation (not a standing bug)**: while designing the fix above, an
        alternative approach using an *unsized* array reference/link type for `$source`
        appeared to segfault at runtime in manual `.k` snippets tried at the time. A
        dedicated follow-up investigation tried to reproduce this with a plain local
        variable of type `int[]&` / `int[]+` bound to a sized array (read, write,
        mutation-through-reference, `.size` access, global vs. local source, via
        `klangc --jit-exec`, a native compiled executable, and the `gen_jit` Catch2 test
        harness) and **could not reproduce any crash** — all variants read/wrote/propagated
        correctly. The earlier segfault was most likely an artifact of the author's own
        in-progress experimental code at that point in the design process (a half-finished
        manual codegen attempt for the abandoned `$source`-as-unsized-type approach), not a
        defect reachable from valid K source in the committed codebase. No fix needed; no
        regression test added (nothing to regress against).
- [ ] Implicit user-defined cast-operator conversions are not applied: a class with
      `operator() : T` is not implicitly converted in an initialisation/argument context
      (e.g. `x : int = wrapper;` yields garbage). Only explicit casts work, and only for
      a cast operator defined in the *same* module — `(T) imported_value` reports
      "casting between non-primitive types is not yet supported" for an imported class.
      Affects the ergonomic read path of `StringBuilder`'s `CharRef` proxy (use
      `charAt(i)` to read; `sb[i] = c` to write).
- [ ] **Value semantics for owning aggregates — incomplete wiring (deferred from IN-PROGRESS phase F).**
      The unified copy/move routine `implementation_generator::emit_value_copy_or_move()`
      (`gen/gen_operators_assign.cpp`, declared in `gen/generators.hpp`) correctly handles
      value semantics for owning aggregates such as `Vector<T>` / `MultiSlot<T>`:
      trivially-copyable → `memcpy`; non-trivial prvalue temporary → **MOVE** (`memcpy` +
      `cancel_temporary_cleanup()` so the source is not double-freed); non-trivial lvalue →
      **COPY** via the copy constructor when present. It is currently wired at only **2 of
      the 4** value-semantics sites:
      - [x] Site 1 — assignment `a = b` (`gen_operators_assign.cpp`,
            `visit_simple_assignation_expression`)
      - [x] Root-cause site — temporary construction `S(expr)` /
            `emit(transform(value))` (`gen_expr_construct.cpp`,
            `visit_temporary_construction_expression`)
      - [ ] Site 2 — variable initialisation `x : T = expr`
            (`gen/gen_variable_definition.cpp`)
      - [ ] Site 3 — by-value argument passing (`gen/gen_expr_invocation.cpp`)
      - [ ] Site 4 — return by value (sret / NRVO) (`gen/gen_function.cpp`); keep NRVO
            elision where the returned object is a named local.
      Consequence: outside the scenarios exercised by `[libk][io][transform]`, sites 2–4 can
      still perform a *shallow* `memcpy` of an owning aggregate, aliasing its heap buffer and
      risking a double free / use-after-free.
      Regression coverage (added — migrated from the `/tmp` `vbyval.k` / `vsret.k` repros):
      - `[gen][lifecycle][cat8][value-semantics]` in `klang/tests/test-gen-lifecycle.cpp` —
        prvalue `struct` (ctor/dtor counters) passed and returned by value is moved, not
        double-destroyed.
      - `[libk][vector][value-semantics]` in `libk/libk/tests/test-vector.cpp` — the same two
        move scenarios strengthened to a real owning `Vector<int>` (heap-buffer integrity, plus
        an `[run]` e2e variant that would crash on a shallow-copy double free).
      When wiring the remaining lvalue *copy* paths for sites 2–4, extend these tests with
      by-value argument / return-by-value of an **lvalue** `Vector<T>` asserting deep-copy
      independence (mutating one copy must not affect the other).
- [ ] **`type-not-copyable` diagnostic — not implemented (deferred from IN-PROGRESS phase F6).**
      In `emit_value_copy_or_move()` the non-trivial *lvalue* copy path falls back to a
      shallow `memcpy` when the type has no copy constructor (see the `TODO` comment near
      `gen/gen_operators_assign.cpp:147`). This is unsafe for owning types. Add a dedicated
      `type-not-copyable` diagnostic in `errors_gen.hpp` (`codegen_diag`) and raise it here
      instead of the silent memcpy fallback, so copying an owning aggregate that has no copy
      constructor becomes a compile-time error. Add a `compile_should_fail` regression test.
- [ ] Explicit template type arguments on intrinsic variadic methods (`_slot.construct<T>(value)`) fail in nested template contexts — workaround: omit explicit type args, rely on argument deduction (`_slot.construct(value)`)
- [ ] `if(var1; var2; ...; test)` still hard-fails during condition-variable initialization on union alternative mismatch / nullable addressor soft-fail cases; extend it to pattern-like semantics so a failed binding makes the whole condition `false` and skips evaluation of the trailing `test`
- [x] **FIXED — `DataStream round-trip long` failed on a negative value round-trip.**
      `[libk][io][data]` in `libk/libk/tests/test-io-data-streams.cpp`: writing/reading back
      `-1L` through `DataOutputStream`/`DataInputStream` returned `4294967295` instead of
      `-1` (high 32 bits of the `long` were zeroed).
      - **Root cause**: a compiler bug in template instantiation, not a `libk` API bug.
        `Expected<R,E>` (`libk/libk/src/expected.k`) has a private nested
        `union Storage { result: R; error: E; }`. All sibling instantiations
        (`Expected<byte,SOD>`, `Expected<int,SOD>`, `Expected<long,SOD>`, …) **shared a
        single `llvm::StructType` for `Storage`**, so only the first instantiation whose
        layout was finalised decided the payload size for all of them. It reproduced in a
        plain single translation unit, without any KDI round-trip, and was an
        **instantiation-order race** (instantiating the large payload first hid it).
      - Fixes applied:
        1. `template_instantiator::clone_nested_union()` no longer reuses the template
           definition's `struct_type`; it creates a fresh `struct_type` + opaque
           `llvm::StructType` per instantiation, named after the enclosing instantiation
           (`nested_type_name()`).
        2. `aggregate_type_resolver::visit_union()` skips unions nested in a template
           *definition* — a blueprint must never get a materialised type.
        3. `resolvers_aggregate.cpp` / `resolvers_type_ref.cpp` now assign fully-qualified
           names (and recompute mangled names) to the nested unions, nested aggregates and
           synthesised `Kind` enums of an instantiation, so no entity is emitted anonymously.
        4. `kdi_importer.cpp`'s dedup-by-type-name is now conflict-checking (see below).
      - Regression tests: `[gen][union][template][nested-layout]` in
        `klang/tests/test-gen-union-template.cpp` (single-TU sibling layouts, three payload
        sizes, and a full KDI export/import round-trip).
- [x] **FIXED — `mangler::mangle_type()` was not exhaustive and silently returned `""`
      for several type kinds, producing colliding symbols (cross-module miscompilation).**
      It had no branch for `enum_type`, and none for `primitive_type::LONG_LONG` /
      `UNSIGNED_LONG_LONG`, `null_type` or `unresolved_function_ref_type`.
      - Confirmed damage: a library exporting `f(x: ErrA)` and `f(x: ErrB)` (two enums)
        recorded **the same** `mangled_name` for both in its `.kdi`, while the `.so`
        exported `<sym>` and `<sym>.1` (LLVM auto-uniquification, invisible to the KDI); a
        consumer then called the first overload for both. The same erasure dropped enum
        *template arguments*: `Expected<long, StreamOutOfData>` mangled as
        `_KFN1k8ExpectedIxE…`, making `Expected<long, EnumA>` and `Expected<long, EnumB>`
        indistinguishable at link time under `linkonce_odr` + `Comdat::Any`.
      - Fixes applied: `mangle_type()` is now **total** — enums encode as `Te` + qualified
        name, the missing primitives and `null_type` have codes, unions without an owning
        aggregate mangle by their qualified name instead of their short name, unresolved
        types get a deterministic placeholder (recomputed once resolution completes), and
        any remaining fall-through raises `INTERNAL_ERR_MANGLE_TYPE` instead of returning
        `""`. `TYPE_CHAR` changed from `"Di"` to `"c"` to remove its ambiguity with the
        drain modifier `D` applied to `int`. `type_reference_resolver::visit_function()`
        recomputes the mangled name after full type resolution (enums are only resolved in
        that pass), and `compute_cast_weight()` gained an enumeration identity rule so two
        unrelated enums are never implicitly interconvertible.
      - Regression tests: `[gen][mangling][exhaustive]` in
        `klang/tests/test-gen-template-mangling.cpp`, `[import][mangling][enum-param]` in
        `klang/tests/test-import.cpp`.
- [x] **FIXED — `build_instantiated_name()` was not injective, so distinct template
      instantiations collapsed onto a single model aggregate / LLVM type.**
      It built an instantiation's short name by mapping **every** non-alphanumeric character
      to `_`: `Box<int*>`, `Box<int&>` and `Box<int!>` all became `Box__int_` and produced a
      single `%Box__int_ = type { ptr }`, while the symbol mangler still gave their methods
      distinct names (a split brain between the two naming systems). Realistic damage:
      `Vector<String!>` (owning, runs the destructor) and `Vector<String*>` (raw) became the
      same aggregate.
      - Fix applied: every component is now escaped with a prefix-free, injective scheme
        (`_u` for `_`, `_p` `*`, `_r` `&`, `_o` `!`, `_l` `+`, `_v` `?`, `_d` `#`, `_N` for
        `::`, `_x<hex>` otherwise). Since no escape can produce `__`, `__` is used as the
        unambiguous argument separator. Common names are unchanged (`Box__int`,
        `get_n__42`); multi-argument names now read `Pair__int__float`. `type_display_name()`
        additionally uses the **fully-qualified** name of struct/enum arguments (recursing
        through addresser wrappers), because `struct_type::to_string()` yields the short name
        and `a::S` / `b::S` would otherwise share an instantiation key and name.
      - Residual, documented ambiguity: a template whose *own* name contains `__` (e.g.
        `A__B<x>` vs `A<B, x>`). It is caught by `compiler::verify_mangled_names()` instead
        of miscompiling silently.
      - Regression tests: `[template][instantiation][addresser-distinct]` in
        `klang/tests/test-gen-template-instantiation.cpp`.
- [x] **FIXED — nested types of a template instantiation got unqualified LLVM type names,
      leaking LLVM's `.N` auto-uniquification into the KDI.**
      A nested `struct Inner` inside `Outer<T>` was emitted as `%Inner` for `Outer<int>` and
      `%Inner.1` for `Outer<long>`; the suffix is assigned by LLVM purely from *compilation
      order*, so it was neither deterministic nor reproducible across builds. The exported
      `.kdi` then mapped both `::…::Outer__int::Inner` and `::…::Outer__long::Inner` to the
      same `mangled_name` `Inner` while their `llvm_def`s differed.
      - Fix applied: `template_instantiator::clone_nested_aggregate()` (and
        `clone_nested_union()`) now create the nested `struct_type` up front with a name
        qualified by the enclosing instantiation (`Outer__long::Inner`), and the resolvers
        assign matching fully-qualified K names. `libk.kdi` no longer contains a single
        `.N`-suffixed LLVM type name.
- [ ] **Unqualified calls to imported functions bypass overload resolution.**
      A library exporting two overloads `f(x: ErrA)` / `f(x: ErrB)` (or any two overloads
      differing only in an imported parameter type) is called from a consumer module as
      `f(ovllib::ErrB::b1)`; the call is bound by `symbol_resolver` to the **first**
      matching imported function and never reaches
      `type_reference_resolver::get_best_matching_function` (verified: no
      `[get_best_matching_function] selected …` trace is emitted for the call, and the
      emitted IR calls the `ErrA` overload with the `ErrB` argument value).
      Distinct from the mangling gap that used to mask it: since `mangle_type()` was made
      exhaustive the two overloads now have distinct symbols and are both declared in the
      consumer, but the wrong one is still selected.
      Fix direction: route imported-function call sites through the same overload-resolution
      path as local ones instead of binding eagerly during symbol resolution.
- [x] **FIXED — `compute_cast_weight()` had no enumeration identity rule.** Unrelated
      enumerations sharing an underlying integer type scored identically, so overload
      resolution silently picked the first candidate. Derived → base enum conversion stays
      allowed.
- [ ] **`enum X : long` ignores the explicit underlying type.**
      `enum ErrA : byte { … }` and `enum ErrB : long { … }` both produce `i8` fields:
      `struct Holder { ea : ErrA; eb : ErrB; }` emits `%Holder = type { i8, i8 }`.
      Independent of templates, but it currently *masks* layout collisions caused by the
      mangling gaps above (every enum happens to be the same size), so it must be fixed
      together with them to get meaningful regression coverage.
      The grammar already specifies this (`doc/spec/language/grammar.ebnf`, ~L458-468:
      "if TypeSpec does not resolve to an enum type, it is the explicit underlying type").
- [x] **FIXED — there was no diagnostic for an empty or duplicated mangled name.**
      `update_mangled_name()` silently yielded `""` for elements whose name had no root
      prefix, and nothing checked that two distinct elements of the same unit produced the
      same mangled name. Because template instantiations are emitted `linkonce_odr` in a
      `Comdat::Any` group keyed by the mangled name, **any mangling collision became a
      silent miscompilation at link time** — which is what let all the bugs above go
      unnoticed.
      - Fix applied: `compiler::verify_mangled_names()` runs at the start of
        `process_generation()` and raises `ERR_MANGLED_NAME_EMPTY` (0x0200) or
        `ERR_DUPLICATE_MANGLED_NAME` (0x0201) over every emitted function, aggregate, union
        and enumeration. Deleted functions and template blueprints are exempt (never
        emitted). Regression coverage: `[gen][mangling][exhaustive]`.
- [x] **FIXED — `update_mangled_name()` was called before the fully-qualified name was
      assigned in the template-cloning paths**, so nested unions/aggregates of an
      instantiation kept an empty mangled name (hence the anonymous `%_union`).
      `resolvers_aggregate.cpp` and `resolvers_type_ref.cpp` now assign the FQ name and
      recompute the mangled name for every nested union, nested aggregate and synthesised
      `Kind` enum of an instantiation.
- [x] **FIXED (by construction) — a member variable typed by a nested type of a template
      was not re-substituted to the concrete nested type.**
      `substitute_type()` only rewrites `unresolved_type` leaves, so a member declared
      `_storage : Storage` kept the template definition's `struct_type`. This is now moot:
      unions nested in a template *definition* are never resolved, so such a member stays an
      `unresolved_type` and is resolved later within the concrete aggregate's own scope —
      exactly the path nested *structs* already used. Should a nested type ever be resolved
      before instantiation again, `compiler::verify_mangled_names()` plus the KDI layout
      conflict check will catch the resulting collision instead of miscompiling.
- [x] **FIXED — silent failure when re-parsing an imported template definition.**
      `kdi_importer::materialise_template_def()` re-parses the template source text carried
      by the KDI; if parsing/model-building failed, or produced no usable declaration, the
      template simply became unavailable for cross-module instantiation with no diagnostic
      at all.
      - Fix applied: both failure paths (parse/model-build exception, and empty/no
        declaration produced) now report `compiler_diag::ERR_KDI_TEMPLATE_REPARSE_FAILED`
        (0x01FC) via the logger — non-fatal (the current compilation may not even reference
        the broken template), but no longer silent.
      - Regression test: `[import][template][kdi][reparse-diagnostic]` in
        `klang/tests/test-import-template-reparse-diagnostic.cpp` (corrupts a real KDI's
        stored template source via CBOR read/write and asserts the diagnostic is emitted).
- [x] **FIXED — no recursion-depth limit on template instantiation.**
      `template_instantiator::instantiate_aggregate()` recursed without any depth guard
      (e.g. via its "resolve template base classes immediately" step), so a recursive
      template (accidental or malicious, such as mutually-recursive bases `A<T> : B<T>` /
      `B<T> : A<T>`) overflowed the compiler's own call stack instead of reporting a
      diagnostic.
      - Fix applied: a thread-local recursion counter (`MAX_TEMPLATE_INSTANTIATION_DEPTH =
        256`) guards every call to `instantiate_aggregate()`; exceeding it raises
        `template_diag::ERR_TPL_INSTANTIATION_DEPTH_EXCEEDED` (0x0187) instead of crashing.
      - Regression test: `[template][instantiation][recursion-guard]` in
        `klang/tests/test-gen-template-recursion-guard.cpp`.
- [ ] **Value template arguments are limited to primitive types.**
      `build_value_substitution_map()` only accepts `int`, `long`, `float`, `double`,
      `bool`, `char`, `string` and `nullptr` values; no enum constants, no
      compile-time-constant expressions, no aggregates.

### Auxiliary Tools (libkdi / kditool)

#### Features to add

- Add support for generating language bindings (e.g. C header generation) from K code

### libk (Standard Library)

#### Features to add

- Refactor libk C functions wrapping to reduce intermediate method counts
- Add following to base libk:
  - containers 
  - maths (to be completed)
  - time/date
  - log facade
  - filesystem
  - process
  - threading
  - networking
- Add sub libraries for:
  - security (crypto, authn, authz)
  - net (http)
  - serialization
  - database client
  - message-bus client
