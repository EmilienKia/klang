## TODO and wish list
### K Language

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
    - [x] **Nested-node template collection runtime under JIT** — **DONE**. `LinkedList<T>` /
      `DoubleLinkedList<T>` (nested-node templates) now allocate, link, index and destroy
      correctly under JIT for non-trivial patterns: structs stored by value, owners
      (`Object!`), enums, insert/remove at both ends, indexed access and `emplace`.
      Regression suite: `[libk][list]` in `libk/libk/tests/test-list.cpp` (63 cases).
      Resolved by unifying imported vs locally-synthesised instantiations into a single
      `struct_type` (commit "Unify imported and locally-synthesised template instantiations")
      and the value-semantics copy/move wiring for owning aggregates.
    - [x] **Imported template aggregate methods materialise executable bodies in consumer
      modules** — **DONE**. Template stdlib collections and streams (`Vector<T>`,
      `LinkedList<T>`, `Optional<T>`, `Expected<R,E>` and the `k::io` stream classes) now
      instantiate, materialise their method bodies and run end-to-end when imported into a
      consumer module. Verified by the full `libk-tests` suite (417 cases, each JIT-importing
      the compiled `k` module). Resolved by the single-`struct_type` unification above and by
      adding imported-enum resolution for root-prefixed enum template arguments in
      `aggregate_type_resolver::resolve_type_from_root`, so a static factory call such as
      `Expected<unsigned int, ::k::io::StreamOutOfData>::expected(...)` inside an imported
      stream method resolves instead of falling back to the un-instantiated template.
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
    - [x] **Static-link diamond of a libk template instantiation** (cross-module COMDAT,
      base-erased generic RTTI) — **DONE**. Two libs A and B plus an executable C all
      instantiate the same libk template (`::k::Optional<int>`, `::k::Expected<int,int>`):
      the *shared-library* diamond works (origin-absolute names + `linkonce_odr` + COMDAT,
      loader interposes one copy — tests `[import][e2e][instantiation-diamond-shared]`), and
      the *static-archive* diamond now links and runs too. Every consumer of a libk template
      re-emits that template's RTTI / vtable / reflection descriptors; they are now emitted
      with merge-friendly linkage so a static link resolves them to a single definition
      instead of failing on duplicate strong symbols:
        - vtable / RTTI globals (`_KTV` / `_KTRI`) of any template / generic / instantiation
          aggregate → `linkonce_odr` + COMDAT (`gen_class.cpp` `visit_klass`, guarded by
          `should_merge_aggregate_symbols` = `is_instantiation() || is_template()`);
        - **secondary (base/interface) vtable globals** (named `<vtable>_for_<BaseName>`,
          created for base-interface thunks and virtual-base thunks in `collect_all_bases`)
          were still missed by the above and kept plain `ExternalLinkage` — this was the
          actual remaining cause of "multiple definition" errors whenever a statically-linked
          template instantiation implemented a base interface or reached a shared virtual
          base (e.g. `Vector<T>` implementing `Collection`/`Sized`/…). Fixed by applying the
          same `should_merge_aggregate_symbols` + `apply_instantiation_linkage` treatment to
          both `sec_gv` creation sites in `gen_class.cpp`;
        - reflection function descriptors (`_KTRF`, member + free) → module-local
          (`PrivateLinkage`) since they are referenced only by baked pointers, never by name
          (`gen_class.cpp`, `gen_unit.cpp`) — matching the already-private ctor/param descriptors.
      Regression test: `[klangc][instantiation-diamond-static]` in `test-klangc-static-diamond.cpp`
      (builds two `.a` archives + statically links an executable + runs → 42).
      Remaining (minor, non-blocking): a consumer still *over-emits* RTTI for imported generic
      templates it never instantiates (e.g. a lib using only `Optional<int>` emits Collection/
      Vector/LinkedList RTTI); now harmless (weak/private) but wasteful — curbing it belongs to
      the KDI recipe-only rework (Phase 4).
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
- Add in-comment documentation support (e.g. for doc generation)
- Varargs
- Add constant values expression computation at compile time, enhance compile-time evaluation capabilities
- Add static conditional statements and static compiler value definitions
- Add traits and compile-time type introspection capabilities
- Add support for separate compilation and module interfaces (e.g. `export` keyword, module partitions)
- Add concepts (and template constraints) for more expressive template programming and better error messages
- Add traits (Rust like)
- Exceptions:
    - [x] `throw` statement — throws an expression deriving from `Throwable` (local var or temporary construction)
    - [x] `try`/`catch` blocks — type-based dispatch via RTTI typeinfo chain, polymorphic base-class matching
    - [x] Multiple `catch` clauses with first-match semantics
    - [x] Nested `try`/`catch` — unmatched exceptions resume unwinding to outer handler
    - [x] `finally` block (guaranteed execution regardless of exception path) — **Phase 1 done** (basic flow); **Phase 2 done** (`return`/`break`/`continue` inside try/catch bodies emit finally before exiting)
    - [x] `try`-`finally` without `catch` clauses
    - [x] `rethrow` / `throw;` (re-throw current exception in catch block) — compile-time error if used outside a `catch` block
    - [x] `throws` clause on functions/constructors — exception specification with type list
    - [x] Checked exception contract enforcement: functions with `throws` must declare or catch all checked exceptions from callees
    - [x] `throws` clause type resolution and KDI import/export
    - [x] Compile-time rejection of `throw` on non-`Throwable` types (struct or class not deriving from `Throwable`)
    - [x] `FatalError` unchecked exception semantics — propagates freely without `throws` declaration
    - [x] Stdlib exception hierarchy (`Throwable` → `Exception` / `FatalError`, `OutOfMemory`, `NullPointerException`, `IndexOutOfBoundsException`, `IllegalArgumentException`, `IllegalStateException`, `ConstructionException`, `NullPointerError` → `NullDereferenceError`/`NullAssignationError`/`NullCastError`, `IndexOutOfBoundsError`)
    - [x] Runtime fatal helpers in libk (`__k_fatal_null_dereference`, `__k_fatal_null_assignation`, `__k_fatal_null_dyncast`, `__k_fatal_array_bounds_check_failed`, `__k_fatal_memory_allocation`) — throw `FatalError`-derived exceptions via Itanium C++ ABI
    - [x] `ConstructionException` wrapping in `UniSlot<T>::construct` / `MultiSlot<T>::construct` — catches constructor exceptions and rethrows as `FatalError`
    - [x] `invoke` instruction usage inside `try` bodies (instead of `call`) for proper exception unwinding
    - [x] Cross-module constructor throws (import library with throwing ctor, catch in consumer)
    - [x] Exception chaining/cause — `Throwable._cause` field + constructors accepting a `Throwable?` cause parameter, `getCause()`/`hasCause()` methods, ABI ref-counted retention via `__k_exception_retain_current()`/`__k_exception_release()`, compiler-generated per-type destructor for cleanup
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
- comparison operator (spaceship operator <=>)
- lambda expressions and closures, variable capture, and functional interfaces

### K compiler and language specifics for compiler capabilities
- Add support for producing inline documentation (e.g. via `///` comments) and generating API reference docs from it
- Add support for generating language bindings (e.g. C header generation) from K code
- Add iterative compilation mode for faster edit-compile-test cycles (e.g. via an interactive REPL or watch mode)
- Add support for incremental compilation and caching of intermediate results to speed up subsequent builds
- Add support for cross-compilation to different target architectures and platforms

### K language limitations (compiler bugs / missing features)
- [x] Calling a member method on `this` from within a constructor body — **FIXED**.
      The constructor now stores the class vptr before user-body execution
      (`gen/gen_function.cpp`, `emit_constructor_pre_block`) and still re-applies
      post-block vptr/vbptr fixups after base-constructor execution
      (`emit_constructor_post_block`). This matches C++ constructor dispatch timing
      and prevents constructor-body virtual-call crashes.
      Regression test: `test-gen-lifecycle.cpp`
      (`[gen][lifecycle][cat1][vptr]`).
- [x] **Sized→unsized array implicit conversion broken for most indirection kinds — FIXED.**
      Discovered while writing the `foreach` array-variant test suite
      (`klang/tests/test-gen-foreach.cpp`, "unsized array reference parameter").
      Root causes and fixes:
      - `context::from_type_specifier` (`model/context.cpp`): an explicit `T[]&`
        double-wrapped the reference (`ref<ref<array<T>>>`) instead of producing the
        single-level `ref<array<T>>` that bare `T[]` and overload resolution expect
        (the `AMPERSAND` branch called `subtype->get_reference()` on a subtype that
        was *already* `ref<array<T>>`, unlike the `*`/`+`/`?`/`!` branches which
        correctly unwrap first). Fixed by unwrapping before re-wrapping, matching the
        other indirection kinds.
      - `check_and_insert_inheritance_cast` (`gen/gen_variable_definition.cpp`) and
        `adapt_from_{pointer,link,view,owner}` (`gen/gen_adapt_type.cpp`) did not
        special-case sized→unsized array widening (unlike `validate_owner_variable`,
        which had its own ad-hoc check). Fixed by extending
        `types_match_array_const_compatible` to also accept a sized source vs. an
        unsized target, and using it (or an equivalent direct check) in all of the
        above.
      Registered `klang/tests/test-gen-array-unsized-conv.cpp` in `klang/CMakeLists.txt`
      (all 14 cases now pass) and un-skipped
      `Foreach over an unsized array reference parameter (sized→unsized widening)`
      (`[gen][foreach][array]`) in `klang/tests/test-gen-foreach.cpp`.
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
- [x] ✅ **FIXED — Virtual destructor dispatch through a secondary (non-primary)
      vtable base, plus a real bug found while verifying it: template classes with
      no explicit base never got their implicit `::k::Object` base resolved.**
      An earlier defensive code comment in `gen/gen_class.cpp` speculated that
      destroying an object through a reference/owner statically typed as a
      *secondary* base (any base other than the primary one sharing the object's
      main `__vptr__`) might not route to the most-derived destructor override,
      reasoning that the name-based matching helper `have_same_virtual_signature()`
      can't recognise `~Base` and `~Derived` as "the same slot". Investigation
      showed this specific reasoning does not apply in practice: secondary vtable
      slots are filled by `compute_secondary_vtable_specs()`/`build_spec()` in
      `gen/resolvers_materializer.cpp`, which matches slots primarily via the
      `overrides` chain (`overrides_base_func()`), not by name. Every class's
      destructor `set_overrides()` call chains back to `::k::Object::~Object` as
      the common `introducing_func`, so the overrides-chain match always succeeds
      for destructors — the broken name-based fallback is never reached. Verified
      (manual `klangc` repros) across a plain 2-interface/2-level hierarchy, a
      3-level chain destroyed via a secondary base two hops removed, and a local
      class extending the real imported `::k::Collection<T>` (which itself extends
      `::k::Sequence<T>` and `::k::Sized`), destroyed via the secondary
      `::k::Sized!` reference.
      However, the FIRST attempt at a template-instantiated-hierarchy repro
      uncovered a genuine, more severe bug (not the one originally documented):
      `ensure_klass_vtable_built()` (`gen/resolvers_aggregate.cpp`, the
      template-instantiation-specific vtable-building path) silently left
      `has_vtable()` **false** for a template class with no *explicit* base — i.e.
      the common case, since every K class/interface with no declared base
      implicitly extends `::k::Object` (injected by `symbol_resolver::visit_unit`'s
      pre-pass in `gen/gen_unit.cpp`, running once on the template *definition*
      only). Root cause: `template_instantiator::instantiate_aggregate()`'s
      base-resolution loop cloned this implicit `"Object"` base_spec into every
      instantiation but only knew how to resolve *explicit* bases (written in the
      template source) — the implicit one has no namespace qualification and isn't
      tied to the template's origin module, so it was silently left with
      `bs.base == nullptr`. With no resolvable base, `ensure_klass_vtable_built()`
      never built a vtable at all, and `emit_owner_object_destroy()`
      (`gen/gen_helpers.hpp`) silently fell back to a **static** (non-virtual)
      destructor call for `delete`/scope-exit on ANY reference typed as that base
      class — not just secondary bases: even destroying through the class's own
      *direct, non-template* base (e.g. `GrandParent<int>!` holding a
      `Child<int>`) called only `~GrandParent()`, never `~Child()`. Fixed by adding
      a final fallback in `instantiate_aggregate()`'s base-resolution loop: search
      every module actually imported by the compilation unit for the unqualified
      base name (mirrors `scope_lookup::lookup_structure_or_import()`'s import
      fallback, used by the equivalent non-template path in `gen/gen_struct.cpp`) —
      this generically covers `"Object"`, `"Annotation"`, and any other
      implicitly-injected or otherwise-reachable base name, not just
      `::k::Object` specifically. Regression tests added:
      `klang/tests/test-gen-virtual-destructor.cpp`
      (`[gen][class][virtual-destructor][secondary-base]`). The stale comment in
      `gen/gen_class.cpp` was corrected to reflect this.
- [ ] `if(var1; var2; ...; test)` still hard-fails during condition-variable initialization on union alternative mismatch / nullable addressor soft-fail cases; extend it to pattern-like semantics so a failed binding makes the whole condition `false` and skips evaluation of the trailing `test`
- [ ] Inline method call on a template-aggregate value chained from a one-liner is
      now supported. Bugs (a), (b) and (c) below are all **fixed**. Repro tests:
      `[libk][optional][inline]` in `test-optional.cpp`.
  - (a) ✅ **FIXED — Nested-template member unresolved on return-by-value.** A template
        aggregate whose member has a nested template type (e.g. `Optional<T>` holds
        `_slot : UniSlot<T>`) used to be instantiated with that nested member left as
        `<<unresolved:UniSlot>>` when the enclosing template was first materialised as
        a by-value function **return type** (diagnostic `000F4`). Fixed in
        `gen/resolvers_aggregate.cpp`: `aggregate_type_resolver::try_instantiate_template_type`
        now (1) transitively resolves nested-template member-variable types after
        instantiation, and (2) resolves a template-parameter argument (e.g. `T`) via the
        enclosing concrete aggregate's `tpl_args` / concrete function's substitution map
        when it is no longer in scope — mirroring the `type_reference_resolver` path.
        Regression tests: `Optional<int>/<byte> getOr on function-returned struct rvalue
        (inline)` (`[libk][optional][inline]`) in `test-optional.cpp`.
  - (b) ✅ **FIXED — Inline temporary construction of a template type** in expression
        position (`Optional<byte>(value)`, e.g. chained `.getOr(0)`). It used to be
        looked up as a free function and reported as `000FD` ("No function named
        'Optional' found"). Two fixes in `gen/`:
        (1) `type_reference_resolver::visit_function_invocation_expression` now, when the
        callee carries explicit template args and no template function matched, synthesises
        an `unresolved_type` from the callee name + AST template args and calls
        `try_instantiate_template_type`, producing a `temporary_construction_expression`
        of the instantiated aggregate (`gen_expr_invocation.cpp`); a public
        `unresolved_type::set_ast_template_args` was added (`model/type.hpp`).
        (2) `type_reference_resolver::visit_member_of_object_expression` now picks up the
        sub-expression's `_replacement_expr` so member access (`.getOr(...)`) operates on
        the rewritten temporary — this also enables `S(args).method()` for non-template
        types (`gen_expr_member.cpp`). Regression test:
        `Optional inline-constructed temporary getOr (without named variable)`
        (`[libk][optional][inline]`) in `test-optional.cpp`.
  - (c) ✅ **FIXED — Static-factory call through a template-qualified type** in
        expression position, chained (`Optional<byte>::empty().getOr(0)`). Two fixes:
        (1) Parsing: `parse/parser_expressions.cpp` (`parse_postfix_expr`) now accepts
        `.`/`->` (in addition to `(`) after a template argument list on a primary
        identifier, so `Type<args>.member` no longer mis-parses the `<` as a relational
        operator (was diagnostic `00034`). Note K uses `::` for static member access
        (like non-template types); the `.` form now produces a clean "undefined symbol"
        rather than a parse error.
        (2) Semantics: `type_reference_resolver::visit_function_invocation_expression`
        now instantiates an unresolved *template return type* of a resolved call (e.g.
        the static factory's declared `Optional<T>` → `Optional<byte>`), so a chained
        member access sees a concrete struct type instead of `<<unresolved:Optional>>`
        (was diagnostic `000F2`) (`gen_expr_invocation.cpp`). Regression test:
        `Optional template-qualified static factory call (inline)`
        (`[libk][optional][inline]`) in `test-optional.cpp`.

- [x] **`test-gen-foreach.cpp` used a stale `Vector<T>::pushBack` method name — FIXED.**
      5 test cases (e.g. "Foreach sequence — sum Vector<int> via copy",
      `[gen][foreach][sequence]`) called `vec.pushBack(...)`, but `Vector<T>`
      (`libk/libk/src/vector.k`) only exposes `append(...)` — `pushBack` was presumably
      renamed at some point and these tests were never updated. Fixed by updating the
      call sites to `append(...)` in `klang/tests/test-gen-foreach.cpp` (no API change).
- [x] **`Transform streams one-to-one and buffering` failed — FIXED.**
      `[libk][io][transform]` in `libk/libk/tests/test-io-transform-streams.cpp`. Root
      cause: commit `cce3f7b` ("Remove legacy compatibility method names for collections")
      renamed `Vector<T>::removeFront()` → `removeFirst()` and updated most call sites in
      `libk/libk/src/io/transform_stream.k` (`pushBack`→`append`, `getSize`→`size`), but
      missed the 13 `removeFront()` call sites in that same file — a genuine bug in
      shipped `libk` production code (not just the test), since `removeFront` no longer
      exists on `Vector<T>`. The test itself also still used the pre-rename
      `pushBack`/`getSize` names. Fixed by updating all `removeFront()` call sites in
      `transform_stream.k` to `removeFirst()`, and updating the test to use
      `append`/`size`. No API change — `removeFirst` already existed; this only fixes
      stale call sites left over from an incomplete mechanical rename.
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

### libk
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
