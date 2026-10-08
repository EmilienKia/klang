## TODO and wish list
### K Language

#### Features to add

- **Template-qualified type references in non-expression contexts**
  - Ensure support (with the same diagnostics quality) in every context that accepts
    type names and deferred symbol references.

- Add templates advanced features (partial specialization, variadic templates, template template parameters, etc.)
  - [ ] Partial and full template specialization
  - [ ] Template template parameters (`template<template<typename> class C>`)
  - [ ] Variadic template parameters (parameter packs, fold expressions)
  - [ ] `extern template` (explicit instantiation declarations)
  - [ ] Value template parameters in parameterised aliases (`template<int N> alias Buf : byte[N];`)
  - [ ] Concepts / type traits / static_if on template parameters
  - [ ] Standalone template enum declarations
  - [ ] Template constructors (independent of aggregate template)
  - [ ] SFINAE-like overload filtering based on template constraints
  - [ ] Variable templates (`template<typename T> const size : int = ...`)
  - [x] Non-primitive value template arguments (enums, expressions, aggregates, propagation, mangling, KDI roundtrip)
  - export templates (Phase 3+ — separate compilation of template definitions and instantiations)
- Add unions, typed unions (discriminated/tagged unions à la std::variant)
    - [ ] Pattern matching / `match` expression on union alternatives
    - [ ] RTTI-only discriminant optimization (all-class alternatives)
- Add state classes
- Better private visibility support
- Improve log and debug messages
- Add constant values expression computation at compile time, enhance compile-time evaluation capabilities
  - [x] Compile time evaluation of in-line structure initialization
  - [x] Compile time evaluation of in-line array initialization
  - [x] Compile time evaluation of in-line union and enum initialization
  - [ ] Compile time evaluation of in-line complex object initialization
  - [ ] Compile time evaluation of in-line function calls, including constructors
- Add static conditional statements and static compiler value definitions
- Add traits and compile-time type introspection capabilities
- Add support for separate compilation and module interfaces (e.g. `export` keyword, module partitions)
- Add concepts (and template constraints) for more expressive template programming and better error messages
- Add traits (Rust like)
- Exceptions: core support already implemented (`throw`/`try`/`catch`/`finally`/`rethrow`, `throws` clauses with
  checked-exception enforcement, KDI import/export of `throws` clauses, stdlib exception hierarchy, cause
  chaining). Remaining gaps:
    - [ ] `noexcept` conditional expression (`noexcept(expr)`)
    - [ ] Exception handling in static constructors/destructors
    - [ ] Unhandled FatalError diagnostic: when an uncaught FatalError propagates past `main()`, the runtime should print a diagnostic message (exception type, code, optional stack trace) before terminating the process
      Status: the generated C entry point now intercepts every exception reaching it (catch-all landing
      pad, K `Throwable` or foreign) and calls `abort()`, so the process dies with SIGABRT
      (`klang/tests/test-klangc-main-abort.cpp`). Still missing: the diagnostic message itself.
      Tests that assert "the program traps" through `exit_code != 0` (e.g. `test-gen-callable-variance.cpp`,
      null-callable cases) should now be tightened to check for SIGABRT.
    - [x] Abort on uncaught exception inside the JIT: the generated `main` guard calls the handler stored in
      the internal global `__k_main_uncaught_handler` (defaults to `abort()`); `jit::add_module()` replaces it
      with a host callback, so `jit::run_main()` returns `{status, uncaught_exception}` instead of killing the
      host. `klangc --jit-exec` reports the error and re-raises SIGABRT. Remaining: after an uncaught exception
      the JIT finalisation still runs global destructors on a possibly inconsistent state; the host callback
      does not receive any information about the thrown type/code yet (see the diagnostic item above).
    - [ ] Stack not available for debugging after an uncaught-exception abort
      The abort guard uses a catch-all landing pad in the entry point: the unwinder's search phase finds a
      handler there, so phase 2 unwinds (and runs cleanups of) every frame between the `throw` and `main`
      *before* `abort()` is called. The core dump / debugger backtrace then only shows the entry-point
      frame, not the frames that were active at the `throw`. Options to investigate:
        - no handler at all, letting the unwinder fail the search phase so the stack is left intact
          (needs a runtime that calls `abort()` / `std::terminate` itself, see `__cxa_throw`), or install a
          terminate handler that calls `abort()`;
        - keep the catch-all but capture a backtrace (e.g. `backtrace()`/`libunwind`) at `__cxa_throw` time
          and print it from the guard, in line with the "optional stack trace" of the diagnostic above;
        - a `--debug`-only mode that omits the guard.
    - [ ] `catch(...)` catch-all clause (catch any `Throwable`)
- Switch/case statements and expression
- With-block - temporary change 'this' scope for a block of code (e.g. `with (obj) { ... }` to access members directly)
- Add static code decoration and constraint (usage example : units of measurement)
- member reordering optimization
- boolean member bitfield optimization
- [x] Lambda expressions and closures, variable capture, and functional interfaces — **Phase B.1-B.11 done**: 
  Core callable type system, binding (free/static/member/functor/interface), variance checking, KDI 
  round-trip, lambda parsing, capture-free lowering, return-type deduction. **Phases B.12-B.13 done**: 
  Template instantiation support, k::functional stdlib aliases. **Remaining future items** (Phase C+):
  - [x] Déduction du type de retour des fonctions — in the short term this only needs to
        propagate the destination callable context into capture-free lambdas; a broader
        function/lambda return inference pass would collect `return` expressions first and
        resolve the final type afterwards.
  - [x] Lambda return-type inference from callable destination context (capture-free lambdas still need an explicit return type in some paths)
  - [ ] Full capture semantics: reference vs value capture, capture-by-binding-time, mutable/const inference
  - [ ] Dynamic closure allocation and environment storage
  - [ ] Capture-with-escaping warnings/errors (scope lifetime analysis)
  - [ ] Bracket-less lambda syntax (`[]` omission when no captures)
  - [ ] Higher-order composition helpers (`compose`, `andThen`) on callables
  - [ ] Callables in generic bodies (currently unsupported due to fat-pointer size)
  - [ ] Callables as non-type template parameters (value-template instantiation)
  - [ ] Thread-local callable state (for thread factories, event handlers)

#### Current bugs and gaps to fix

- [x] **ListMap/TreeMap imported return specializations retain their lexical origin.**
      Direct reproduction: from `cmake-build-debug/libk/libk`, run
      `./libk-tests-collections-associative --reporter compact`. The executable
      reports 43 cases, 99 assertions, with 15 failed `REQUIRE(j)` assertions.
      Failures are the nine `ListMap<int,int>` tests and six `TreeMap<int,int>`
      tests in `libk/libk/tests/test-map.cpp` (lines 50, 76, 99, 116, 138, 165,
      188, 214, 237, 255, 278, 313, 336, 374, and 403); each generated module reports
      `00E01: Return expression type must be compatible to the expected
      function return type`. HashMap cases pass. Test sources and expectations
      are unchanged. GDB at the return checker confirms incompatible specialized
      return types: the declaration is
      `::k::OptionalConstRef___u_ulmap_uempty_u_u_NEntry_u_uint_u_uint`, while
      the expression is `::k::OptionalConstRef__k_NEntry_u_uint_u_uint&`.
      Both contain `::k::Entry__int__int` as the template argument, but the
      `OptionalConstRef` specializations have different names. This
      was coupled to canonical imported-template/return-type resolution.
      Specializations now retain their defining template, both resolution
      phases use that lexical origin, and canonical identity is assigned before
      resolving children rather than renaming a cached specialization later.
      Producer/consumer and ListMap regressions cover nested return identities.
      The full associative suite passes: 43 cases and 129 assertions.

- [ ] **Specialized `Expected` static factories reject matching values.**
      In `libk/libk/src/application.k`, calling
      `Expected<Shared<Module>, LoadModuleError>::unexpected(failure)` where
      `failure : LoadModuleError` reports that `LoadModuleError&` cannot be
      converted to the candidate parameter `E&`, even though the receiver
      specializes `E` as `LoadModuleError` and `Expected::unexpected(value : E&)`
      declares that signature. The corresponding `expected(found)` call should
      also be checked. Constructing an `Expected` and calling `setError()` or
      `setResult()` directly is a workaround. Investigate generic static member
      overload resolution/substitution.

- [x] **The aggregate weak-ownership selector exits without a Catch2 report.**
      The native fixture now calls `ensure_libk_loaded()` before its scoped
      `dlopen`/`dlclose` of `libk.so`. Without that pin, the fixture could unload
      libk before the JIT helpers and process teardown finished; preloading
      `libk.so` reproduces the fix. The `[weak]` selector passes with the
      fixture change.

- [ ] **Dependent constructor-call expressions in template functions.**
      Calling a bound type parameter as `T(args)` is unresolved in instantiated
      function bodies: the NR-21 named-return regression's `return T(second);`
      produced `00910: No function named 'T' found` for both struct and class
      arguments, with and without optimisation. A concrete
      `return Handle(second);` works in the same fixture. Investigate
      constructor-name lookup through template type bindings separately from
      named-result replacement. `klang/src/model/template_instantiator.cpp`
      substitutes expression types but leaves the callee name unchanged;
      `klang/src/gen/gen_expr_invocation.cpp` subsequently looks up `T`.
      Minimal analogous repro (not independently executed):
      ```k
      module dependent_constructor_repro;
      struct Value {
          number : int;
          Value(n : int) : number(n) {}
      }
      template<typename T>
      make() : T { return T(42); }
      test() : int {
          value : Value = make<Value>();
          return value.number;
      }
      ```

- [ ] **Implicit struct assignment bypasses resource lifetime operations.**
      `klang/src/gen/gen_struct.cpp` generates implicit assignment, but
      `implementation_generator::emit_copy_assignment_operator()` in
      `klang/src/gen/gen_function.cpp` implements it with bare memcpy, without
      releasing the destination's previous contents or invoking the copy
      constructor. Source-inspection finding; this repro has not been run.
      Expected result: `0`; inferred result: `9` (`a.strong == 1`,
      `b.strong == -1`). Explicit user-defined assignment is unaffected.
      ```k
      module ordinary_assignment_repro;
      struct Resource { strong : int = 0; }
      struct Handle {
          r : Resource*;
          Handle(x : Resource&) : r(&x) { ++r->strong; }
          Handle(x : Handle&) : r(x.r) { ++r->strong; }
          ~Handle() { --r->strong; }
      }
      test() : int {
          a : Resource;
          b : Resource;
          { x : Handle(a); y : Handle(b); x = y; }
          return a.strong * 10 + b.strong;
      }
      ```

- Generics (type-erased, single synthesis). Feature overview:
  `doc/spec/language/templates/generic.md`. Model recap: a generic is a template with
  restrictions; its body is synthesised once with every type parameter erased to an opaque
  pointer (today `byte*`), only non-owning addressers (`& * ? +`) may be applied to a
  parameter, and users of the generic keep the concrete types and perform the checks/casts.
    - [x] **Owners and drains as generic parameters/arguments**: not supported, by design
      (`T!`, `T#`, `Box<Dog!>` are rejected with `0x1102` / `0x1106` / `0x1107`). Possible
      future support through an owned destruction callable passed to the generic (under study).
      History: the call `Holder<Dog>(d)` with `d : Dog!` and `Holder(v : T!)` already
      compiled (owner erasure adaptation rules exist in `gen_adapt_type.cpp` and
      `resolvers_type_ref.cpp`); the real defect was that the erased `byte*!` was released with
      a bare `free` (`emit_owner_object_destroy` in `gen_helpers.hpp` only handles struct,
      array and primitive pointees), so the destructor of the owned object never ran.
      Rejected options: a virtual-destructor branch for opaque owners (needs a marker type to
      tell it from a user `byte*!`, and `k::Object`), erasing `class T` to `::k::Object*`, or
      a per-instantiation drop glue (changes layout/ABI).
    - [x] **Member access through an erased parameter** (`v->field`, `v.method()` with
      `v : T*`) is a compile error by design (`0x1108`), in generic aggregates, functions and
      methods. Rejected options: erasing `T` to its `: Base` bound (Java-like), monomorphizing
      generic aggregates (breaks the single-synthesis/KDI signature-only model), structural
      constraints with accessor thunks (needs concepts). The early check in
      `generic_constraint_validator` (`resolvers_generic.cpp`) is based on declared types; the
      remaining cases (e.g. the result of a call, `ident(v)->n`) are rejected by the resolver
      in `gen_expr_member.cpp` with the same diagnostic.
    - [x] **Single synthesis for generic functions and methods** (free functions and methods of
      non-generic classes). Done: `template_instantiator::synthesize_generic_function` builds the
      single erased body (cached under `"<generic_synthesis>"`, named `f<generic>`, weak linkage);
      each use site with concrete arguments gets a body-less *facade* (`signature_only` clone,
      `function::set_erased_target`) that carries the concrete signature for overload resolution
      and type checking, wired by `type_reference_resolver::link_generic_facade` at the four
      instantiation sites of `gen_expr_invocation.cpp`. Invocation codegen redirects a facade to
      its target (same LLVM signature); the facade is skipped by the declaration and
      implementation generators, the KDI exporter and `compiler::verify_mangled_names`. A generic
      body calling a generic with an erased argument works through deduction; the kind/bound
      validation skips erased arguments. Also fixed: deducing `T*` from a pointer *variable*
      (`ident(p)` with `p : Dog*`) used to deduce `T = Dog*` (implicit reference not stripped in
      `deduce_from_types`). Tests: `test-gen-generic.cpp`, `[generic][synthesis]`.
      Still open: an explicit `f<T>(...)` naming a parameter of the enclosing generic is rejected
      as a bare `T` (`0x1101`, validator); a generic method declared inside a generic class still
      instantiates its own parameters like a template.
    - [x] Erased-parameter marker type: `erased_param_type` (a `pointer_type` subclass built by
      `type::get_erased_param()`, tested with `type::is_erased_param`) replaces `pointer<byte>`;
      the 5 copies of `is_generic_opaque_ptr` now use it. It mangles and compares exactly like
      `byte*`, so symbols are unchanged. Dedicated resolver diagnostic for member access on an
      erased type (`gen_expr_member.cpp`, `0x1108`) done.
    - [x] **Cross-module use of generic aggregates.** Done: every generic aggregate is synthesised
      once in its defining module (`type_reference_resolver::visit_namespace`, step 1c, through
      `instantiate_template_aggregate` with erased arguments), so the erased class, its symbols
      and its layout always exist and are exported as a regular KDI aggregate; a new KDI type
      `erased_param` (`kdi_erased_param_type`) carries the erased parameter through the
      signatures (KDI DTO, JSON/CBOR codecs, dump/query/docgen, converter, exporter, specs);
      `instantiate_template_aggregate` resolves an imported signature-only generic to that
      exported synthesis (`get_or_create_imported_aggregate`) and only records the concrete
      arguments. Also added the missing erasure rules for link/view sources (`&d` passed to a
      `T*` parameter) in `compute_cast_weight` and `adapt_from_link/view`. Tests: `test-import-
      templates.cpp` (`[generic]`), `libkdi` JSON/CBOR round trips.
      Also done afterwards: (1) the arguments of a call on a generic aggregate are type-checked
      against the concrete type arguments (`check_generic_call_arguments`; `Box<Dog>::set(&cat)`
      is rejected); (2) no backward compatibility: an imported generic aggregate or method whose
      library exports no synthesis is an error (`0x1109`), the old re-instantiation path is gone;
      (3) generic methods of non-generic classes are exported as signature-only member templates
      (`kdi_template_def.host_fq_name`) plus the erased synthesis `<name><generic>` (regular
      method), synthesised at definition time; the importer attaches the template to the imported
      host and `link_generic_facade` targets the exported synthesis.
      Still open: non-generic member templates (`template<...>` methods of a non-template class)
      are not exported through KDI (`h.method<T>()` on an imported class finds no function).
    - [x] Rebinding a non-null callable from a nullable one (`lnk = fp` with `lnk : +(...)`,
      `fp : *(...)` null) did not trap: the null-check in `operator=` codegen was dead code for
      callables (the callable branch returned before reaching it, and the adapted right-hand side
      hid the source nullability). Fixed in `gen_operators_assign.cpp`; this was the cause of the
      intermittent `klang-tests-gen-functions` failure (`[callable][variance][null]`, test 19).
    - [x] Test time budget: `libk-tests-io`, `libk-thread-io-tests` and `libk-tests-time` were close to
      the 120 s CTest timeout and timed out under VM load; each is now two Catch2 shards
      (`libk_add_test_sharded`), like `klang-tests-import-templates`.
    - [x] Owned members of an aggregate are destroyed automatically in reverse member
      declaration order; null members are skipped after explicit deletion. Regression:
      `klang/tests/test-gen-owner.cpp`, `template owner members are destroyed once in reverse
      declaration order`.
    - [x] **Explicit generic type args in a member method call on a non-generic host
      class** (`obj.method<zoo::Dog>(arg)`). Fixed: the member-call path only used
      `context::resolve_type`, so namespace-qualified types, aliases and enclosing template
      parameters (`pass<U>`) were not resolved. It now shares
      `type_reference_resolver::resolve_explicit_template_arg_type` with the free-function
      path. Tests: `test-gen-generic.cpp`, `[generic][explicit-args]`. Generic
      methods of a non-template host are exported/imported through KDI; non-generic member
      templates (`template<...>` methods) are still not.
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

- [ ] Explicit template type arguments on intrinsic variadic methods (`_slot.construct<T>(value)`) fail in nested template contexts. 
      Workaround: omit explicit type args, rely on argument deduction (`_slot.construct(value)`)
- [ ] KDI export omits module-private helper templates referenced by public templates.
      Observed while extending `Shared<T>`/`Weak<T>`: declaring their helper as
      `template<class T> private final struct SharedBlock` allows libk to build,
      but importing its KDI and instantiating `Shared<Payload>` fails with
      `0F003: Internal error: variable '<generated Weak<T>::doRelease::block>'
      has an unresolvable type that is not an unresolved_type instance`.
      `Weak<T>::doRelease()` declares `block : SharedBlock<T>* = _block`.
      The exporter (`klang/src/model/tools/kdi_exporter.cpp`) skips private
      template definitions, and KDI currently permits only public/protected
      template visibility. Preserve referenced implementation templates or
      diagnose this unsupported dependency instead of reporting an internal
      compiler error. Current workaround: export the helper type but keep all
      its operations private, accessible only to the public templates as friends.
- [ ] Binding a drain to a reference variable contradicts the documented
      drain-to-observer conversions. For example, in a template drain assigner,
      `source : Weak<T>& = other` with `other : Weak<T>#` is rejected with
      `00D06: the initializer ... has type Weak<T>#`, although drains are
      specified as implicitly convertible to references. A pointer observer
      is sufficient for the address-only self-drain guard in `Shared`/`Weak`;
      reference initialisation should consistently accept a drain without
      transferring resources or copying the referred object.
- [ ] KDI does not preserve annotation-type retention metadata. Imported
      `ffi::Extern` therefore appears to have RUNTIME rather than SOURCE
      retention and emits spurious `00506` warnings for private declarations.
      Minimal consumer repro: `module retention_repro; struct Host {
      @ffi::Extern("C") private static native(x : int) : int; }`.
      `libk/libk/src/ffi.k` declares SOURCE retention, but `kdi_aggregate` and
      `kdi_builder::begin_aggregate()` export no retention metadata;
      `imported_annotation_type` inherits the default false
      `aggregate::is_source_retention()`. Preserve this metadata across the
      DTO/codecs/export/import boundary rather than suppressing warnings or
      hardcoding standard-library annotation names.

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
  - [ ] threading and synchronisation (`Thread`, `Mutex`, `Semaphore`, `Future<T>`, …)
  - [ ] asynchronous file I/O (`FileChannel`, `AsyncFile*Stream`, io_uring backend)
  - networking
- Add sub libraries for:
  - security (crypto, authn, authz)
  - net (http)
  - serialization
  - database client
  - message-bus client
