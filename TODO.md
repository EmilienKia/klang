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

- [ ] **ARRAY-variant `foreach` re-evaluates its source expression on every iteration
      instead of exactly once.** `type_reference_resolver::visit_foreach_statement`
      (`gen/gen_statements.cpp`) builds the `.size` test expression and the
      per-iteration `source[$index]` subscript from two *separate* `source_expr->clone()`
      calls, and the generated test expression lives in the loop's condition basic
      block, so it is re-evaluated at runtime on every iteration (standard loop-condition
      codegen — see `implementation_generator::visit_foreach_statement`). For a plain
      variable source (`for (x : int = arr)`) this is harmless (re-reading the same
      variable's address is idempotent and cheap), which is why the existing test suite
      never caught it. It becomes a real correctness/perf smell once the source is a
      non-trivial expression — most notably a temporary array literal built directly in
      the init expression (`for (x : int = int[]{1, 2, 30})`, added to support the
      primitive-array-literal foreach feature): the temporary array is reconstructed
      (all element stores re-executed) once per `.size` check *and* once more per
      successful iteration, i.e. `O(iterations × size)` redundant work instead of `O(size)`.
      Confirmed by manual stress-testing (not a permanent test, to avoid slow CI): a
      20 000-element `int[]{...}` foreach source took ~30s to JIT-compile/run vs ~1.6s
      for a 5 000-element one (~20× for 4× the size — the expected quadratic blow-up),
      though results stayed numerically correct in all cases tried (no crash, since LLVM
      keeps the temporary's `alloca` in the function's entry block — no per-iteration
      stack growth). Recommended fix: synthesize a hidden `$source` variable (mirroring
      the existing `$index` pattern) bound *once* to `source_expr` (as a reference to the
      array — `array_init_expression`'s resolved type is always `<array>&`, so this works
      uniformly for both lvalue arrays and temporary literals), then rewrite the `.size`
      test and the per-iteration subscript to read from `$source` instead of re-cloning
      `source_expr`. Not fixed yet: doing so safely requires auditing whether re-visiting
      an already-resolved `source_expr` via the generic `variable_definition` visit path
      is idempotent, or whether the hidden variable must be constructed *before* the
      first (and only) resolution pass of `source_expr` instead. Add a permanent
      regression test (e.g. a ctor-counting struct element or a large literal with a
      tight iteration/compile-time budget) once fixed.
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
- [ ] **`DataStream round-trip long` fails on a negative value round-trip — root-caused,
      fix plan proposed, not yet fixed (compiler bug, needs discussion before fixing).**
      `[libk][io][data]` in `libk/libk/tests/test-io-data-streams.cpp`: writing/reading
      back `-1L` through `DataOutputStream`/`DataInputStream` returns `4294967295`
      instead of `-1` (high 32 bits of the `long` get zeroed).
      - **Root cause**: this is a genuine **compiler bug** (KDI import/export +
        template instantiation), not a `libk` API/semantics bug, and reproduces even in
        a plain non-JIT compiled executable. `Expected<R,E>`
        (`libk/libk/src/expected.k`) has a private nested `union Storage { result: R;
        error: E; }`. When `k::io` (containing `Expected<byte,SOD>`,
        `Expected<int,SOD>`, `Expected<long,SOD>`, … sibling instantiations with
        different-sized `R`) is compiled into `libk.so`/`k.kdi` and then imported via
        KDI into a consuming translation unit, all sibling `Storage` union
        instantiations collapse onto a single (too-small) LLVM struct type/layout —
        concretely, `Expected<long,StreamOutOfData>`'s union ends up using the 4-byte
        layout computed for a smaller `R` (verified with `gdb`/`--emit-raw-ir`: the
        high 4 bytes of the stored `long` are silently zeroed). Contributing factors
        identified in the compiler:
        1. `template_instantiator::clone_nested_union()`
           (`klang/src/model/template_instantiator.cpp`, ~L1153-1200) reuses the
           **template definition's** `struct_type` across all outer-template
           instantiations instead of creating a fresh LLVM struct type per
           instantiation.
        2. `mangler::mangle_union()` (`klang/src/model/mangler.cpp`, ~L380) does not
           qualify a nested (non-template-parameterized) union's mangled name by the
           enclosing instantiation's template arguments, so `Expected<byte,E>::Storage`
           and `Expected<long,E>::Storage` can mangle to the same LLVM type name.
        3. `kdi_importer.cpp`'s `collect_llvm_defs_from_namespace()` (~L477-535)
           explicitly deduplicates LLVM struct definitions **by type name string**
           across the whole KDI, with a comment assuming shared inner types like
           `Expected<T,E>`'s private union are always interchangeable across
           specializations — false when `T`'s size varies.
      - **Proposed fix plan** (needs go-ahead before implementing, since it touches
        template instantiation, name mangling, and the KDI format handling — all core
        compiler internals):
        1. Make nested-union mangled names instantiation-qualified (fix mangling so
           `Storage` inside `Expected<byte,E>` vs `Expected<long,E>` produce distinct
           mangled/LLVM type names), removing the false name collision at the root.
        2. Stop reusing the template definition's `struct_type` in
           `clone_nested_union()`; create a fresh `llvm::StructType` per instantiation
           and let `declaration_generator::visit_union()` compute its own `max_size`
           independently for each.
        3. Re-audit `kdi_importer.cpp`'s type-name-based LLVM def deduplication: once
           names are correctly unique per instantiation (step 1), the existing dedup-by-
           name logic becomes safe again — but add a regression test that would have
           caught silent incorrect dedup (e.g. two sibling `Expected<T,E>` KDI-imported
           instantiations with different-sized `T`, asserting on both instantiations'
           values simultaneously in one process).
        4. Add a permanent regression test in `klang/tests/test-gen-*.cpp` compiling two
           sibling template instantiations with differently-sized nested-union payloads
           through an actual KDI export/import round-trip (not just in one translation
           unit — the bug does not reproduce without the KDI round-trip).
      - No `libk` API changes needed for this fix — it is entirely internal to the
        `klang` compiler (mangling, template instantiation, KDI import).

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
