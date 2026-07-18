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
    - [ ] Generic constructor call with owner `T!` argument: synthesized ctor takes `byte*!`, call site `ConcreteType!` implicit cast not supported
    - [ ] Member access on `T*` inside generic body (opaque pointer — by design; workaround: access at call site)
    - [ ] Explicit generic type args in generic member method call on non-generic host class (`obj.method<Dog>(arg)`)
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
    - [ ] Origin-aware homing of imported template definitions: re-injected imported templates are flattened into the consumer module's **root** namespace (via the `module <ns>;`-rename trick in `kdi_importer::materialise_template_def`), so two *imported* templates with the same short name from different modules (`a::Box`, `b::Box`) clash at the model/symbol level (the flatten dedups by short name in root). The instantiation `struct_type` registry is already collision-safe (keyed by an origin-qualified name via `unit::make_instantiation_registry_key`; see test `[template][instantiation][ns-collision]`), but the *model-level* symbol clash remains. **Why the naive fix is blocked**: simply homing the re-parsed template under `root::<origin>` (nested-`namespace` wrapping instead of the module-rename trick) breaks **unqualified access** to imported top-level symbols — `import mylib;` currently behaves like `using namespace mylib;`, so imported function templates must be reachable as bare `sum_pair<int>(...)`. Homing them under `::consumer::mylib::sum_pair` makes unqualified lookup fail (proven: it regresses the 4 `[cross-tpl][consumer-inst]` function-template tests in `test-import.cpp`). The real fix is therefore **deeper than homing**: scope lookup (`resolvers_scope_lookup.cpp`) must resolve unqualified imported symbols from their origin namespaces (an `import`-as-`using-namespace` mechanism) so the flatten can be dropped; homonymous imports then naturally require qualification. See skipped test `[import][template][homonym-imports][.]` documenting the target behaviour.
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
- [ ] **Sized→unsized array implicit conversion broken for most indirection kinds.**
      Discovered while writing the `foreach` array-variant test suite
      (`klang/tests/test-gen-foreach.cpp`, "unsized array reference parameter").
      A sized array `T[N]` is supposed to implicitly widen to the corresponding
      unsized array indirection `T[]&` / `T[]+` / `T[]*` / `T[]?` when passed as an
      argument (see `doc/spec/language/summary.md` and the pre-existing, currently
      **unregistered** test file `klang/tests/test-gen-array-unsized-conv.cpp`, which
      is not wired into `klang/CMakeLists.txt`). In practice:
      - `T[]&` (explicit reference) and `T[]+`/`T[]*`/`T[]?` (link/pointer/view)
        parameters fail to bind at all: `validate_reference_variable` /
        `validate_link_variable` / `validate_pointer_variable` / `validate_view_variable`
        (`gen/gen_variable_definition.cpp`) reject the sized-array argument with
        "... cannot be bound to an expression of type '...': the referenced/linked/
        view types are incompatible (no inheritance relationship)" — they never special-case
        array widening (unlike the owner (`!`) validator, which does handle it correctly).
      - `T[]` (no explicit addresser, i.e. bare unsized-array parameter) and `T[]!`
        (owner) *compile* successfully, but the resulting parameter is unusable at
        runtime: even a plain `while` loop indexing `a[i]` on such a parameter
        **segfaults** (ABI/codegen bug, not caught by the type-resolution passes).
      Register `test-gen-array-unsized-conv.cpp` in `klang/CMakeLists.txt` once fixed
      (9 of its 14 cases currently fail) and un-skip
      `Known-limitation: foreach over an unsized array reference parameter`
      (`[.][foreach][known-limitation]`) in `klang/tests/test-gen-foreach.cpp`.
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
