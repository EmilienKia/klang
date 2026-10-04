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

- [x] **Application singleton test now handles the checked `Thread.join()` interruption.**
      Direct reproduction: run `libk-tests-types --reporter compact [singleton]`.
      The test case `Application singleton — activation, inheritance casts,
      duplicate rejection and teardown` fails its `REQUIRE(jit)` with
      `01207`: `Thread.join()` may throw `ThreadInterruptionException`, which
      is neither caught nor declared by the generated `main`; diagnostic
      location is `__app_singleton_lifecycle__.k:94:16` (the existing
      `thread.join()` in `libk/libk/tests/test-application.cpp`). The other
      `[singleton]` test passes. This is exposed by the current KDI importer
      change that preserves imported method `throws_spec`; `k::Thread.join()`
      correctly declares `throws(ThreadInterruptionException)` in
      `libk/libk/src/thread.k:174`. The fixture now catches that exception and
      explicitly asserts no interruption occurred, preserving its former
      success expectation. Retest with `./libk-tests-types --reporter compact
      [singleton]` and the full direct types executable.

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

- [x] **Imported `Shared` member templates were re-exported from concrete hosts.**
      The initial full module run reproduced four `00502: Undefined symbol
      '_block'` failures at `test-module.cpp:167`, `:428`, `:785`, and `:898`.
      The generated KDI separately emitted member-template recipes hosted by
      concrete instantiations such as `k::Shared__k_NModule`; re-materializing
      those methods against an imported aggregate stub lost the host's private
      layout and friend context. `kdi_exporter` now leaves member templates in
      their template aggregate's source/signature and emits standalone hosted
      method recipes only for non-template, non-instantiated aggregates. This
      preserves private access without adding private fields to public KDI
      layout. The focused imported-template regression and the original
      `Application::applicationModule` reproducer pass. The full module
      executable now has 19/20 passing cases; its remaining, unrelated
      `Thread.join()` checked-exception failure is tracked below.

- [x] **Concurrent module-load JIT fixture handles checked interruption.**
      Direct reproduction:
      `cmake-build-debug/libk/libk/libk-tests-module --reporter console
      --success "Application::loadModule — concurrent loads of one file share one Module"`.
      The full module executable now reports 20 cases / 90 assertions, with
      only this case failing at `test-module.cpp:898` (`REQUIRE(jit)`). The
      generated module reports `01207` at line 37 on `th0.join()`: the call may
      throw `ThreadInterruptionException`, but generated `main` neither catches
      it nor declares it. This is independent of the imported `Shared` KDI
      regression. The fixture now catches interruption explicitly and treats
      it as a failure rather than skipping the concurrent-load checks.
      The full module suite passes with the intended identity assertions.

- [ ] **Several full CTest executables exceed the 120-second test budget.**
      The CTest registrations for `klang-tests-gen-virtuality` and
      `klang-tests-gen-functions` timed out at 120.37s and 120.41s respectively;
      direct serial runs completed successfully (660 assertions/144 cases and
      379 assertions/166 cases). The earlier broad CTest run also timed out
      `libk-tests-rtti-exceptions` and `libk-tests-collections-sequential`;
      direct serial reruns completed (537 assertions/141 cases and 337
      assertions/115 cases). Rebalance/shard these registrations so CTest's
      timeout remains meaningful.

- [ ] **Abstract base constructors clear the inherited vptr during derived construction.**
      `implementation_generator::emit_constructor_pre_block()` in
      `klang/src/gen/gen_function.cpp` zero-initializes the entire base subobject.
      Abstract classes have no vtable global (`klang/src/gen/gen_class.cpp`,
      `declaration_generator::visit_class` returns before creating one), so
      `emit_vptr_store()` returns without replacing the cleared vptr.
      A concrete derived constructor installs secondary vptrs before calling
      its base constructor, but restores them only after its entire body.
      Consequently, an inherited virtual call from that body can dispatch
      through null even when the base fields are fully initialized.
      Reproduction: an abstract base with a concrete non-final method and a
      concrete derived constructor calling that inherited method.
      Confirmed by the Application decorator construction IR and debugger:
      the Application base constructor runs before slot 13 dispatch, whose
      receiver vptr is null (`call *0x68(%rax)`, `rax == 0`).
      Stream accessors and installation hooks are intentionally non-virtual;
      fixing their API does not fix this general constructor-dispatch defect.
      Add a compiler virtuality regression before changing constructor vptr
      restoration or abstract construction vtable emission.

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
- [ ] **Intermittent I/O test allocator diagnostic — UNCONFIRMED, NOT FIXED.**
  A detailed run of the former 141-case `libk-tests-io` executable emitted
  glibc's `sysmalloc` assertion after `"Uri: percent decoding accessors"`
  (`libk/libk/tests/test-io-uri.cpp:83`), before the QueryParams case at line 104.
  The process also exceeded its 120-second test budget. A whole-group debugger
  replay passed all 141 cases / 455 assertions, three independent `[uri]`
  runs passed all eight cases / 24 assertions, and an uninstrumented whole-group
  replay passed 141 cases / 455 assertions in 123.77 seconds. The same whole
  group passed with `MALLOC_PERTURB_=165` in 124.84 seconds; both runs had zero
  stderr. The current byte/text split passes 69/223 in 58.18 seconds and 72/232
  in 59.22 seconds, preserving 141/455 under each 120-second budget.
  No responsible allocation or source change is established; do not treat
  timeout budgeting as a memory
  fix. Reproduce with declaration ordering and seed 318440856, retaining
  stdout/stderr separately; use `MALLOC_PERTURB_=165` only as a diagnostic and
  capture the first allocator-failure stack if it recurs.

- [x] **Legacy Unix datagram pathname hazard — resolved and accepted in Lot H.**
  The retired version of `libk/libk/tests/test-io-unix-socket-native.cpp`, case
  `"Unix socket backend: datagram sendTo/connect"`, removed the fixed names
  `/tmp/klang_unix_dgram_recv.sock` and `/tmp/klang_unix_dgram_send.sock`.
  The removed `runtime/network_ffi.c::__k_net_unix_dgram_bind()` also
  unconditionally unlinked its input pathname. A pre-existing file/socket at
  either fixture name was therefore removed before the test owned it. This was
  a source finding, not an executed destructive reproduction. It motivated
  unique fixture directories, removal of only owned paths and safe bind
  semantics. Stream LOT G did not use these paths or auto-unlink.
  Lot H removes the blocking bridge and fixed-path fixture, uses unique
  directories, and assigns pathname ownership only after successful native
  bind. Close/final release checks the created socket identity before unlink.
  `[datagram-channel][ownership]` native/API fixtures preserve existing files
  and verify remote-address release never unlinks a socket. The parent-supplied
  H gate passed 15 native datagram cases and 7 API cases; the latest API replay
  is 7 cases / 35 assertions. This supersedes the historical source-only
  handoff, without claiming a destructive reproduction of the retired code.

- [x] **F2 adopting return prvalue leaks a native handle — resolved with regression acceptance.**
  The supplied Luna trace `f2-count-limit-ref-trace.log` records native creation
  at reference count **1**, the first retain in `FileChannel::decodeOpen()`
  (**1 -> 2**), then publication retains through `IoPromiseCarrier::invoke()`
  and `trySuccess()` (**2 -> 3 -> 4**). Only two FileChannel drops follow
  publication; final wrapper destruction leaves reference **1**, so the fd
  stays open. The native operation ownership accounting is balanced.
  The exact compiler path is
  `visit_return_statement()` setting `_sret_destination`, followed by
  `visit_temporary_construction_expression()` constructing directly in that
  slot and intentionally omitting expression-temporary registration.
  `visit_load_value_expression()` previously recognised a constructor prvalue
  only when `is_expression_temporary(src_ptr)` was true. The untracked sret
  object was consequently treated as an lvalue: its copy constructor retained
  into `lval_copy`, and return lowering overwrote the original sret object
  with the copied aggregate, stranding the adoption reference.
  The source fix recognises a direct constructor expression that actually
  wrote to the saved construction destination, preserving existing copy
  elision rather than registering/destroying the caller's result. Constructor
  calls still use `create_call_or_invoke()`; existing successful-construction
  and argument guards remain responsible for exceptions, with no destructor
  of an incomplete whole object. Aggregate classification is canonicalised
  for by-value loads, copy/move dispatch, constructor temporaries and sret
  result cleanup, including resolved name wrappers in imported template bodies.
  No FileChannel implementation, count assertion, explicit close or native
  reference-count workaround was changed.
  Permanent `[return-prvalue]` regressions cover free/static factories,
  class/struct and template variants, normal lvalue retains, later carrier
  getters, intermediate receiver cleanup, optimised/unoptimised IR, failed
  constructors/copies and cleanup of earlier arguments. The import regression
  exports KDI and instantiates its templates in a separate consumer, using
  project-local artifacts only. Expected final native-style reference count
  is **0**, with no excessive releases; direct factories require **0** copies.
  The source-only implementation task did not run agents/builds/tests/debuggers
  or commits; subsequent Luna validation is separate. The native `[count-limit]`
  fd-closure regression passed **1 case / 28 assertions**, local prvalue coverage
  **1 / 128**, import coverage **1 / 68**, and exceptional/copy-argument coverage
  **3 / 48**. The final serialized F2 gate passed **8/8 compiler CTests,
  752 / 3,050**, and **7/7 libk CTests, 307 / 36,778**; FileFully passed **3 / 24**.
  The original fd-closed assertion is unchanged. Fresh full compiler replay after
  the later Application/friend changes is accepted by the final J coverage
  recorded below; it is separate from these historical F2 measurements.
  Historical focused validation selection (now executed):
  ```
  ninja -C cmake-build-debug -j3 klang-tests-gen-oop-temporary klang-tests-gen-exceptions klang-tests-import-templates
  cmake-build-debug/klang/klang-tests-gen-oop-temporary "[return-prvalue]"
  cmake-build-debug/klang/klang-tests-gen-exceptions "[return-prvalue],[copy-arguments]"
  cmake-build-debug/klang/klang-tests-import-templates "[return-prvalue]"
  ```
  Related compiler gates include all three function shards and the existing
  exception, union, virtuality, template-core and memory-ownership suites
  (**8 CTests**), plus the newly affected OOP temporary/lifecycle suites and
  all three import-template shards:
  ```
  ninja -C cmake-build-debug -j3 klang-tests-gen-functions klang-tests-gen-exceptions klang-tests-gen-types-enums-unions klang-tests-gen-virtuality klang-tests-gen-templates-core klang-tests-gen-memory-ownership klang-tests-gen-oop-temporary klang-tests-gen-oop-lifecycle klang-tests-import-templates
  ctest --test-dir cmake-build-debug -j1 -R '^klang-tests-(gen-functions-shard-[0-2]|gen-exceptions|gen-types-enums-unions|gen-virtuality|gen-templates-core|gen-memory-ownership|gen-oop-temporary|gen-oop-lifecycle|import-templates-shard-[0-2])$' --output-on-failure
  ninja -C cmake-build-debug -j3 libk-tests-io-proactor libk-tests-file-channel-fully
  cmake-build-debug/libk/libk/libk-tests-io-proactor "[count-limit]"
  cmake-build-debug/libk/libk/libk-tests-io-proactor "[file-fully]"
  ctest --test-dir cmake-build-debug -j1 -R '^libk-tests-file-channel-fully$' --output-on-failure
  ```
  Re-run any failing focused selector with `-s` before reporting detailed
  errors. The original `[count-limit]` fd-closed assertion is the integration
  acceptance criterion and must remain unchanged.

- [ ] **Prepared native operation wrapper allocation cleanup gap - source finding, not verified.**
  `libk/libk/src/io/io_operation.k::prepareOperation<T>()` obtains a retained
  native request from `__k_io_prepare_request()`, then returns
  `new IoOperation(native)` without a cleanup guard for wrapper allocation
  failure. If that allocation throws, the raw pointer has no K owner and its
  native observer reference, buffer lease, handle and callback carrier can be
  stranded. `IoOperation::current()` and `unsupportedOperation<T>()` have the
  same retain/create-then-wrap shape. This predates F2 and is nonblocking for
  its normal preparation/error paths; it was not changed in the source-only lot.
  Bounded reproduction: inject failure into the K allocation backing
  `new IoOperation` immediately after native preparation succeeds, catch
  OutOfMemory, then inspect the buffer lease and native callback drop counter.
  Expected cleanup is no remaining lease and exactly one carrier drop.
  No allocation-failure injection, build or test was run.
  Preparation also allocates a callback carrier before calling the native
  factory, then throws OutOfMemory on a null native result. Carrier ownership
  on that native-null path needs the same failure-injection audit; neither
  path is claimed repaired by ordinary successful-operation gates.

- [x] **LOT F1 acceptance green - superseded by the parent gate supplied on 2026-10-04.**
  The parent reports **8 compiler CTests + 5 libk CTests** green, plus the
  original callback **10**, EventLoop/Future **27**, resource adoption **47**
  and Expected class lifecycle **14** assertions. F2 may proceed. The notes
  and former pending commands below are historical, not current blockers.
  F2 is also accepted; see its stdlib tracking entry. Current resource/callback
  gates retain **47** adoption, **14** Expected lifecycle, **10** callback and
  **27** EventLoop/Future assertions. The real-carrier replay passes **38**
  assertions, and owner-delete/capture-free/union/ABI regressions are green.
  The following source diagnoses and failed counters are historical, not
  unresolved F1 blockers. The saved
  `f1-nested-lambda-gate-*` Luna logs validate the declaration-owner ABI repair:
  the original EventLoop Future/FileChannel case now passes **27/27** assertions.
  Both real-carrier callback modes report state **1**, stage **4**, native
  claim **1**, and no exception. The captured carrier mode passes; only the
  capture-free mode returns **-3** at its strict final lifecycle gate
  (`13/14` assertions overall). Resource adoption remains **47**, Expected
  class lifecycle **14**, copy arguments **28**, polymorphic union destruction
  **11**, and the original callbacks **10**, all passing. These results
  supersede the earlier `25/26` EventLoop failure and pending ABI validation.
  Two newly added fixture blockers received bounded source-only corrections:
  - The global `[capture-free-owner]` initializer failed with diagnostic
    **00312** on `const[]`. It now uses the already accepted fresh capture-free
    `[](value : int):int` function pointer. Namespace/free-function, static
    member, constructor, and nested member-block coverage is unchanged.
    The separate deferred aggregate-argument regression already passed.
  - The carrier's false-mode lifecycle failure has a concrete saved IR cause:
    `f1-iodone-investigation-carrier-rerun.log:24410-24418` clears `probe`,
    calls only `free`, and skips `~Probe`; captured-field cleanup instead calls
    `ProbeD1Ev_destroy_and_free`. `emit_owner_object_destroy()` classified raw
    allocation types, so a resolved name wrapper could miss the struct branch.
    It now canonicalizes the allocation type before destructor dispatch,
    reusing the existing destroy-and-free/unwind path. `[nested-owner-delete]`
    isolates a local struct owner inside an owned lambda, immediate destructor
    count **1**, source clearing, null re-delete, and no second drop at scope
    exit. Carrier getters and INFO now expose pre-delivery/final closure drops,
    live/drop/invalid payload counts, callback count, the saved validity flag,
    and the actual K boolean mode. Both modes still require exactly one Probe
    drop, zero live payloads, two payload drops, and zero invalid payloads.
  No agent, build, test, debugger, or commit was run for these corrections.
  Their later acceptance is green; these failed counters are retained as
  historical evidence only. Earlier `f1-layout-gate-*`,
  `f1-void-union-gate-*`, and investigation logs document the historical causes
  and passing fixes below, not validation of this new cleanup correction.
  - `gen_class.cpp::fill_secondary_vtables()` now resolves virtual/transitive
    collector offsets from the final LLVM DataLayout instead of retaining the
    materializer's field-size estimates. The saved lifecycle GDB trace reaches
    `FileChannel::close(..., ExecutorLease&)` through `_thunk_adj64`; this path
    previously corrected only `__base_X__`, not `__vbase_Channel__`. The final
    offsets also replace the existing exported secondary-vtable metadata.
    Static decoding of the existing `libk.kdi` confirms the mismatch:
    `Object={ptr}` is 8 bytes, `Channel={ptr,Object}` is 16, and
    `ReadableChannel`/`WritableChannel={ptr,ptr,Channel}` are 32 each.
    `FileChannel={ptr,ReadableChannel,WritableChannel,ptr,Channel}` therefore
    places the live Channel at **80**, while its metadata and crashing thunk
    use **64** (each intermediate was estimated as three pointers / 24 bytes).
    `[interface-owner-dispatch]` checks both overloads, owned callbacks/results,
    a copied class value, and shared state through all three diamond interfaces.
    The real `Channel&.close(completion, lease)` fixture remains unchanged.
  - Union assignment now uses the existing null-vptr lifetime guard before
    treating a tagged, zero-filled class alternative as constructed. The extra
    `ResA` destructor came from destroying default zero storage on the first
    assignment. The polymorphic union regression still requires **11**, not 12.
  - Constructor unwinding tracks successful base/member initialization and
    destroys only completed subobjects in reverse initialization order, without
    invoking the incomplete enclosing destructor. `[constructor-members]`
    requires catch codes 922/923, zero live tokens, five token drops, final
    destruction order 1, and only one complete Payload destructor.
  - The original `[copy-arguments]` fixture now explicitly deletes its owned
    token in `~Payload`; normal owner-member deletion is not implicit. All
    original intermediate/final counts remain unchanged. Getter diagnostics
    expose catch-time and final counters if either function still returns 0.
    Expected's class-resource fixture reads `other.state` into a mutable local
    pointer; neither const-pointer semantics nor access visibility was relaxed.
  - The EventLoop Future fixture now reports stages 1-13, caught/cause codes,
    and task publication result. Stage 3 is open/get, 5 force/quiescence,
    7 write/get, 9 read/get, 11 EOF/get, 12 closeSync, and 13 publication.
    Stage 0 distinguishes failure before task entry. Its existing return/file/
    callback assertions are unchanged; native snapshots establish success
    before delivery, and the later Luna evidence identifies the callback ABI
    mismatch below.
  Historical source diagnoses (latest acceptance status is recorded above):
  - **Diamond owner dispatch — valid void callable signature, not another offset fix.**
    `!(int)` means an owned callback taking an int and returning nothing
    (`doc/spec/language/functions/callables.md`, section 3). The saved
    `f1-final-probe-diamond.log` pre/post-optimization IR emits both void lambdas
    as `define ptr ... ret ptr undef`. In the permit overload, `callable.join`
    frees that undefined pointer before allocating the real four-byte Result.
    The newer GDB memory trace stops at free `0x7c04bca2f3a0`, after the callback
    observed 176; the copied Middle is stack allocated and its Channel adjustment
    is 88. `gen_expr_invocation.cpp` was borrowing the enclosing function's
    `Result!` return whenever an indirect callable's return was null, then
    mutating the interned signature. Both ordinary and pointer-to-member
    invocation now consume the declared return unchanged. This keeps the
    diamond fixture, owners, cleanup, and prior secondary-vtable fix intact.
    `[void-callable-return]` adds a minimal owned callback and a void member
    function pointer called from owner-returning functions; both returned
    objects must still be destroyed exactly once.
  - **Expected class lifecycle — embedded union members were omitted from cleanup.**
    `f1-final-probe-expected-rerun.log` reports first stage 11, mask 7168
    (stages 11–13), refs(result/error)=3/1, releases=5/2, copies=7,
    assignments=2, drops=5 and invalid=0. Stages 7–10 passed: getters each
    increment refs once and the first getter's local is cleaned up. Expected's
    copy constructor makes two copies (getter local `r` and stored result);
    destroying `r` removes one reference, leaving exactly its stored reference
    legitimately live until scope exit.
    Storage is a union `struct_type` with no aggregate owner. Previously,
    `finalize_aggregate_destructor()` only inspected aggregate-owned fields,
    and `emit_destructor_cleanup()` likewise skipped union members. Thus
    neither default nor copy construction made Expected clean up its Storage.
    The finalizer now detects non-trivial union alternatives, and enclosing
    member cleanup calls the existing discriminant/null-vptr guarded union
    cleanup. `Expected.k`, all 13 invariant stages, and their counts are unchanged.
    `[union-member-cleanup]` isolates empty/copy-empty, an active copied template
    holder, a by-value getter, and final zero references / four drops.
  - **Future publication inside an EventLoop task — capture-free callback has a phantom `this`.**
    `f1-final-probe-eventloop-rerun.log` records task stage 13, publication=1,
    caught=0, transfer=1, host stage 5 and runOnce=1. Return -1 comes from
    the force callback's false `IoResult<IoDone>.hasResult()`, not the outer
    progress gate or an exception. No native error kind/errno was saved.
    Static ordering in `io_operation.c::k_io_operation_on_kernel_completion()`
    unlinks a handle operation before publishing QUIESCENT; therefore, after
    successful waitQuiescent, the later close cannot find that force operation
    in its inflight cancellation list. Its observer destructor does not cancel.
    Do not reorder close or assume a broken Expected discriminant without data.
    The fixture now snapshots fd/isOpen before force; operation stage/error kind/
    errno/bytes after quiescence; callback hasResult/hasError, error kind/errno/
    bytes; and `IoOperation::current()`'s native stage/error/bytes at delivery.
    Callback state 1 is success, 0 is an error, -1 is neither, -2 is not invoked.
    `f1-resumed-completion-20261004-filechannel-future-eventloop-focused.log`
    and `...-rerun-detailed.log` now record task stage 13, publication=1,
    caught=0, host stage 5, transferred=1, and runOnce=1. Force fd/open is 4/1;
    quiescent stage/kind/errno/bytes is 2/0/0/0; callback native snapshot is
    3/0/0/0. Callback state/kind/errno/bytes is -1/-1/-1/-1: neither result nor
    error, not a filesystem failure or cancellation.
    Source inspection does **not** prove missing empty-payload activation:
    `gen_operators_assign.cpp::visit_simple_assignation_expression()` stores
    the new discriminant after constructing a struct alternative independently
    of its byte size. Its null-vptr guard applies only to vtable-bearing types;
    `IoDone` is an empty struct without one. Scalar/overload fallback paths
    likewise update the discriminant. `IoCallbackCarrier<T>::invoke()` calls
    `setResult(value)` before claim and completion; a contained decode exception
    instead calls `setError(error)`. Do not change these paths on speculation.
    `[io-done-result]` in `test-expected.cpp` isolates default/copy-empty,
    setResult, explicit copy, getter round-trip, owned by-value completion,
    error copy/switch, repeated activation, and two empty alternatives.
    `[io-done-carrier]` in `test-io-foundations-api.cpp` invokes the real carrier
    after an enclosing owned EventLoop task, native Future operation, and
    guarded class-union subobjects have been destroyed. It checks both free
    and captured completions, native resource claim, strict hasResult, copied
    result/getter, and once-only closure/payload destruction. Native claim
    separates producer progress from the callback's by-value copy.
    Luna's `f1-iodone-investigation-carrier-rerun.log` reproduces the failure
    through the real carrier: the capture-free branch fails, the captured
    branch passes, and 13 of 14 assertions pass overall. The saved model/IR
    declares the inner capture-free `__lambda_3` as an instance member of the
    outer owned Task closure. Its target accepts `(ptr this, Expected value)`,
    but its callable is `{lambda_fn, null}` and the free dispatch branch calls
    it with only `(Expected value)`.
    `f1-iodone-investigation-filechannel-args.log` confirms this on the original
    native path through `libk.so::IoCallbackCarrier<IoDone>::invoke`: the carrier
    contains a non-null function and null context; callback entry has `rdi=1`,
    `rsi=0`. The success tag is consumed as the unintended `this`, so the
    callback reads an empty Expected. The original fixture still passes
    25 of 26 assertions, reaches task stage 13 without an exception, and records
    native force SUCCESS. Captured callbacks correctly bind and pass a
    non-null context; no Expected activation or carrier workaround is needed.
    The exact source error is in
    `model_builder.cpp::visit_lambda_expression()`: static classification used
    `current_context_content<aggregate>()`, whose definition in
    `model_builder.hpp` examines only the top lexical context (a function or
    block here). The existing `decl_owner` search already finds the aggregate
    through arbitrary intervening contexts. The repair tests that actual
    declaration owner when adding `static`, leaving namespace targets free,
    all capture-free bindings context-null, and capturing targets bound.
    Neither the context helper's semantics nor `gen_callable`'s ABI changed.
    `[capture-free-owner]` in `klang/tests/test-gen-lambda.cpp` adds permanent
    model/runtime coverage for namespace/static-member initializers, free
    functions, constructors, deeply nested member blocks, and an owned Task
    that creates a deferred owned callback. The latter checks by-value
    `Expected<Done,int>` and two-int aggregate arguments after the outer closure
    has dropped, with genuinely captured and capture-free callbacks, non-void
    returns, source-slot clearing, once-only drops, and exception cleanup.
    The latest nested-lambda gate validates the deferred callback case and the
    declaration-owner repair on the original FileChannel path.     The global initializer correction and `[nested-owner-delete]` case
    subsequently passed their regression gate.
    Both existing fixtures opt into `KLANG_F1_DUMP_IR=1`, as does the unchanged
    original FileChannel fixture.
    Original publication, deferred callback, file content, and result assertions
    remain strict. No empty Expected may be treated as fabricated success.
  Historical focused Luna validation selection (subsequently accepted):
  ```bash
  ninja -C cmake-build-debug -j3 klang-tests-gen-functions libk-tests-io-foundations-api libk-thread-io-tests
  cmake-build-debug/klang/klang-tests-gen-functions "[capture-free-owner],[nested-owner-delete]"
  cmake-build-debug/libk/libk/libk-tests-io-foundations-api "[io-done-carrier]"
  cmake-build-debug/libk/libk/libk-thread-io-tests "FileChannel primitives: Future publication progresses while EventLoop waits inside a task"
  ```
  Every failing selector must be rerun by exact test name with `-s`.
  The prior passing union activation/destruction, diamond, Expected class,
  void-callable, copy-argument, resource-adoption and callback fixes are unchanged.
  Preserve all carrier and FileChannel assertions; do not add phantom captures
  or treat an empty Expected as success.
  KDI impact checklist for this F1 cleanup patch only: DTO updated **no**;
  JSON updated **no**; CBOR updated
  **no** (unchanged fields); exporter alignment checked **yes**; converter and
  tooling execution validated **no**; regression added **yes**; format spec
  update **no** (no format change, schema version remains 0.1).

- [ ] **Member destructor order — separate nonblocking static finding.**
  `gen_function.cpp::emit_destructor_cleanup()` collects members from
  `st->variables()`, a name-sorted `std::map` (`model_element.hpp`), then reverses
  that list instead of declaration order. Repro: declare destructible member
  `z` before `a`; cleanup emits z then a instead of a then z. This predates the
  F1 union-member repair and does not affect its one-member Expected/Holder
  fixtures. No execution or unrelated ordering change was made here.

- [x] **Targeted import/resolution follow-up — repaired and regression gates accepted.**
  Later enum/Writer/member-main/source-emitter/private-prelude regressions
  passed; all six function/import-template shards passed within the unchanged
  120-second budgets. The source-only handoff and its commands below are
  historical, not current pending acceptance.
  The saved `compiler-import-kdi-import-templates-shard-0-rerun.log` and
  `compiler-import-kdi-verifier-gdb-ir.log` show synthesized
  `Application::main():int` allocating `Box<int>` but emitting neither `set(42)`
  nor `get()`, then `ret void`. This is not an entry-point return-type override:
  `resolve_instantiated_aggregate()` called `visit_aggregate()` directly,
  bypassing `visit_klass()`'s LLVM vtable typing; virtual call emission returned
  no value when the vtable type was missing. It now uses the aggregate visitor.
  The original consumer fixture is unchanged; `[member-main]` additionally
  covers a typed ordinary member `main` and a derived template-base call chain.
  Rooted member-reference owners now retain the root token in callable-parser
  lookahead. The source-emitter roundtrip checks owner identity and all existing
  ABI/privacy/throws assertions remain; the already-parsed private prelude's
  expected exception spelling is updated to canonical `::k::Exception`.
  Writer's two inherited return contracts have distinct slots, not a same-owner
  `narrow -> wide` chain. The fixture now checks both base contracts and first
  declaration lookup, plus real dispatch to a `FinalWriter` override of the
  broad slot through a `FilterWriter&` receiver and bound callables. Production
  Writer lookup, covariance checks and vtable construction are unchanged.
  No build, test or debugger was run in that implementation task. Its handoff
  requested Luna build
  `klang-tests-import-templates`, `klang-tests-gen-resolution` and
  `klang-tests-gen-functions`, run `[member-main]`, `[consumer-inst]`,
  `[source-emitter]`, the private callback-prelude case and `[writer-overrides]`,
  then rerun all six function/import-template CTest shards before closing.
  Historical follow-up commands (Luna only, serial):
  ```bash
  ninja -C cmake-build-debug -j3 klang-tests-import-templates klang-tests-gen-resolution klang-tests-gen-functions
  cmake-build-debug/klang/klang-tests-import-templates "[member-main],cross-module template — class template consumer instantiation,[source-emitter],import template — private callback preludes preserve native ABI and privacy"
  cmake-build-debug/klang/klang-tests-gen-resolution "Member lookup:*,Reference variable bound to wrong type must be rejected"
  /usr/bin/ctest --test-dir cmake-build-debug -j1 -R '^klang-tests-(gen-functions|import-templates)-shard-[0-2]$' --output-on-failure
  ```
  If a focused selector fails, rerun the failing test name with `-s`; if a shard
  fails, rerun only that CTest entry with `-V --output-on-failure` and then run
  its failing Catch2 test name with `-s`. The six-shard command must be green
  after repairs; earlier runs are not validation of this patch. Use the system
  CTest executable because the saved Python-wrapper invocation failed to import
  its `cmake` package.

- [ ] **KDI validator/exporter contract mismatch — OPEN, nonblocking, not repaired.**
  Reproduce with
  `cmake-build-debug/libkdi/kditool validate cmake-build-debug/libk/libk/libk.kdi`.
  Current final J original and round-tripped descriptors both validate with
  **exit 1** and identical **1,034 errors: 247 empty `llvm_def` fields + 787
  unresolved aggregate references**. Canonical JSON is byte-identical and the
  original descriptor is unchanged. The increase of **62** stays within the
  same known validator categories; it is not a fix or schema-validation success.
  Codecs/tooling and consumer acceptance do not close this nonblocking backlog.
  Historical `compiler-import-kdi-kditool-validate.log` reports **972** issues:
  247 empty `llvm_def` fields and 725 type-table reference misses; the saved
  historical roundtrip validation reports the same count and JSON dumps are
  byte-identical. These earlier counts are retained as historical evidence.
  Example: `unit.root_ns.aggregates[1]` is abstract interface `::k::TypeInfo`;
  abstract `getName` and its non-emitted abstract vtable have empty `llvm_def`,
  which `libkdi/src/kdi_validate.cpp::validate_aggregate()` rejects unconditionally.
  `kdi_exporter.cpp::register_aggregate_type()` retains `::k::String` in
  `types.aggregates`, while `to_kdi_type()` emits references as `k::String`.
  `validate_type()` compares these strings exactly and reports a missing entry.
  Git blame identifies the type-table registration and exact validator comparison
  as unchanged code from `fe5b1bc5`, and rooted-reference stripping from
  `e787c894` (both March 8, 2026): this normalization mismatch predates the
  current callback changes.
  This routes to existing producer/validator normalization and abstract-metadata
  rules, not JSON/CBOR loss or callback-private-dependency serialization. Do not
  normalize the entire format in this callback fix. Consumers use
  `kdi_type_converter.cpp::convert_aggregate_ref()`'s context/namespace lookup,
  not the validator's exact type-table comparison, and the LLVM importer skips
  empty definition fragments while reconstructing method signatures from DTOs.
  Thus successful consumers do not prove that validator/exporter contracts agree.
  A baseline artifact comparison remains necessary before attributing every issue
  to pre-existing behavior; this triage does not claim such a comparison was run.

- [x] **Template lifecycle/overload/late-body repairs — accepted by later foundation gates.**
  Local/imported overloads, free-template closures and late carrier enum
  comparisons passed their regressions. The foundation API's later gate is
  **19 / 216** and executor **16 / 48**; six function/import shards and the
  F2 compiler replay are green. Historical source-only handoff follows.
  `gen_function.cpp` and `gen_constructor.cpp` now keep body typing and fallback
  initialization single-shot within one type-reference pass. Preserve the lifecycle
  regressions' exact expectations: `220`/`222`, and one construction/drop per nested
  payload (historical results were `420`/`422` and four constructions).
  `gen_expr_invocation.cpp` instantiates every explicit function-template overload
  from its own declaration-owned cache; the old free-call branch selected the first
  template and discarded the rest, rather than sharing a function registry entry.
  Type-qualified static calls now finish body typing in the current resolver.
  `template_instantiator.cpp` roots free instance names at creation, including
  dependent free lambda targets. `resolvers_aggregate.cpp` consults imported type
  provenance without discarding a concrete declaration context in the same namespace.
  `k_source_emitter.cpp` emits rooted dependency types and reconstructs concrete
  aggregate template arguments instead of emitting unmaterialized `Result__...`
  names. Private declaration visibility is unchanged. Existing regression assertions
  remain intact; overload declaration-order, rooted lambda-name, canonical native-local
  type, and emitted template-argument assertions were added.
  Minimal combined validation commands (one process at a time, Luna only):
  ```bash
  ninja -C cmake-build-debug -j3 klang-tests-gen-templates-core klang-tests-gen-functions klang-tests-import-templates
  cmake-build-debug/klang/klang-tests-gen-templates-core "Template value members are constructed and destroyed exactly once,Nested template value destruction propagates after releasing owned state,Template carrier resolves enum comparisons before body types,Template lifecycle body symbols preserve synthesized initializers,Nested and base template bodies resolve qualified enum symbols,Template union Kind is declared before body symbol catch-up,Invalid enum entry in instantiated body reports a symbol diagnostic,*Template-qualified static call Type<T>::fn(args)*"
  cmake-build-debug/klang/klang-tests-gen-functions "[free-template-capture],Lambda: explicit generic execute selects owning free and UCS overloads"
  cmake-build-debug/klang/klang-tests-import-templates "import template execute preserves every same-named overload,import template private native handles and nested result callables use lexical types,private FFI source — callable addressers and contracts reparse"
  ```
  Re-run failing selectors with `-s` before diagnosing further. No build/test/debugger
  was run for this batch; a clean whitespace check is not executable validation.

- [x] **Historical deferred foundation failures — resolved by subsequent scoped repairs.**
  Enum qualification, legitimate Writer contracts and imported class-template
  vtable typing passed their regressions. Callable-sequence corruption was
  traced to native fatal-exception underallocation and repaired using complete
  ABI layouts; imported layout **25** and repeated JIT **104** assertions,
  the five-case callable sequence, and full functions **182 / 669** passed.
  This evidence does not resolve the separate unconfirmed I/O allocator
  diagnostic above. Historical failures:
  Writer fixture: `test-gen-resolution.cpp`, "Member lookup: generic Writer
  redeclarations select the final covariant override", rejects binding
  `FilterWriter::write` to `&(const char[]&):Writer&` with `01306` because
  `OutputStream__char` does not derive from `Writer`.
  Enum homonyms: `test-gen-enum.cpp`, "Enum return casts resolve lexical homonyms
  instead of a global Visibility", reports `00F03` for
  `::k::reflection::nested::Visibility` and `::k::Visibility` both mangling to
  `10Visibility`. Compiler-host corruption: the callable non-null argument sequence
  in `test-gen-callable-basic.cpp` aborted with `double free or corruption (!prev)`,
  recorded in `resumed-foundations-functions-import-full-ctest-20261003T0958.log`
  and its existing single/sequence debugger logs. Investigate these separately;
  do not weaken fixtures or infer a host-allocator fix from the lifecycle guards.

- [x] **Free-template owning closure mangling — accepted by the later foundation replay.**
  Local/imported free-template owner closure regressions and all six
  function/import-template CTest shards subsequently passed. The failure
  description below records the earlier implementation-only handoff.
  The existing case "Lambda: free template owner captures use concrete closure
  constructor types" in `klang/tests/test-gen-lambda.cpp`
  (`[free-template-capture]`) previously failed with an empty mangled name for
  `__lambda_44__int`. It constructs a returned owned closure that captures an
  owner inside a free template function. The imported overload-set correction
  does not establish a fix to that closure declaration/mangling path. Keep both
  the local and imported `[free-template-capture]` cases in Luna's follow-up;
  neither was run or rewritten in this implementation-only batch.

- [ ] **Exceptional deallocation through polymorphic base owners needs ABI evidence.**
  `emit_owner_object_destroy()` in `klang/src/gen/gen_helpers.hpp` obtains the
  complete allocation pointer from the virtual destructor's normal return.
  On a destructor exception that return value is unavailable. This batch makes
  the call unwind-aware and clears the owner before it, but cannot safely free
  a nonzero-offset base pointer on the exceptional path without additional ABI
  information. Candidate repro: own a multiply-derived object through a
  secondary base, let its destructor throw inside `try { delete owner; }`,
  then check both catch continuation and allocation release. Non-virtual struct
  destroy/free and closure environment release now have exceptional cleanup;
  do not infer the same allocation guarantee for polymorphic base owners or
  arrays whose element destructor throws.
  The shared exceptional exit in `get_or_create_closure_drop_function()` also
  frees the environment without walking remaining captures. Follow-up repro:
  capture two owners with counted destructors, make the first capture destroyed
  in reverse field order throw, and verify whether the other capture is
  destroyed. The single-owner carrier regression does not establish complete
  multi-capture exceptional cleanup.

- [ ] **Dynamic downcasts to an intermediate class require hierarchy matching.**
  Static inspection of `implementation_generator::emit_dynamic_cast()` in
  `klang/src/gen/gen_expr_cast.cpp` shows that it compares the object's
  most-derived RTTI pointer directly with the target RTTI pointer. For
  `class A {}; class B : public A {}; class C : public B {};`, an `A*` pointing
  to a `C` cannot consequently be recovered as `(B*) a`: the two RTTI pointers
  differ even though `C` derives from `B`. A concrete example is an
  `ExecutionException::getCause()` referring to `ClosedChannelException`,
  cast to `IOException*`. The I/O taxonomy regression now downcasts to each
  concrete exception before reading its metadata; lazy imported RTTI declaration
  does not fix hierarchy matching or virtual-base address adjustment.
  Also, the existing nullable-source path still loads a vptr from null for a
  non-fatal pointer/view target (see its in-code TODO). Neither runtime case
  has been re-run in this implementation-only batch.

- [ ] **Imported-template sibling friend lookup — independent coverage still unconfirmed.**
  Historically `scope_lookup::is_friend_of()` in
  `klang/src/gen/resolvers_scope_lookup.cpp` started friend lookup at the
  consumer root; `friend OptionalConstRef<T>;` in an origin-scoped
  `k::OptionalRef<T>` could not find its sibling declaration there. The current
  lexical friend repair is accepted for local/later-source constructors,
  static helpers and negative checks (**35 / 92**), but that focused selector
  does not establish imported sibling-template coverage. Reproduce
  with a producer template granting a relative sibling-template friend access
  to a private field, then instantiate both templates from a consumer module.
  `OptionalConstRef` conversion now uses `hasValue()` and `get()` instead, so
  its nullable reference semantics do not depend on this unconfirmed extension.
  Preserve private visibility; do not describe the already accepted local
  lexical friend repair as pending.

- [ ] **KDI function prototypes still contain definition-only LLVM syntax.**
  Static inspection of the current `cmake-build-debug/libk/libk/libk.kdi` with
  `kditool json-dump` shows `Vector<k::io::file::AnyFileSystemEntry>` constructors
  exported as `define linkonce_odr void @...(ptr %this, ptr %other) comdat`,
  without a body or COMDAT declaration. `llvm_fn_prototype()` in
  `klang/src/model/tools/kdi_exporter.cpp` trims a printed definition's first
  line rather than producing a standalone `declare`; parsing it in
  `context::declare_llvm_function_from_def()` fails and import falls back to
  deriving the ABI from K metadata. Completing forward-referenced signature
  metadata fixes the current null-parameter blocker, but does not restore
  fidelity to the stored LLVM prototype. Reproduce by exporting a concrete
  template constructor, inspecting its `llvm_def`, and passing that snippet to
  LLVM's IR parser. No new build or executable reproduction has been run.

- [ ] **Imported LLVM nominal short-name collisions (separate from alias binding).**
  `symbol_resolver::visit_aggregate` in `klang/src/gen/gen_struct.cpp` creates
  `struct_type` with `get_short_name()`; distinct producers can consequently export
  incompatible `%Error` or `%IoError` layouts. The importer correctly rejects these
  with Error `00F05`, before declaration-origin/alias resolution can be exercised.
  Reproduce with two libraries declaring top-level `struct Error { code: int; }`
  and `struct Error { padding: long; code: int; }`, then import both; alternatively
  import a producer's `IoError { code: int; }` alongside auto-imported `k::io::IoError`.
  Detailed existing evidence is in `import-templates-rerun.log` (Error `00F05` for
  `aliasconsumer.k` and `tplaliashomonyms.k`). The scoped KDI alias regressions now
  use distinct producer DTO names while retaining identically named consumer
  shadows and homonymous aliases. Do not interpret those fixtures as a fix to
  LLVM nominal interning; that requires separate ABI/name-compatibility work.

- [ ] **Function `throws` aliases need lexical resolution (static finding, not run).**
  The raw-name branch in `type_reference_resolver::visit_function`
  (`klang/src/gen/gen_function.cpp`, `get_throws_spec_raw()` handling) creates a
  context unresolved type and then falls back only to aggregate/import lookups.
  Unlike callable-component resolution, it does not use the lexical alias lookup.
  Candidate repro: `alias Failure: k::Exception; fail(): int throws(Failure);`
  with the stdlib resolver enabled. Confirm the expected `ERR_THROWS_TYPE_NOT_FOUND`
  before changing that independent path. Native-prelude coverage keeps the function
  contract `throws(k::Exception)` and tests alias canonicalization in its callable
  component instead, so this does not block the scoped dependency export changes.

- [ ] **Visibility of ordinary function addresses (static finding, not run).**
  `type_reference_resolver::visit_symbol_expression` in
  `klang/src/gen/gen_expressions.cpp` builds callable types for resolved functions
  without checking ordinary namespace/member visibility. Candidate repro in one
  module: `namespace detail { private hidden(x: int): int { return x; } }`
  followed outside that namespace by `test(): int { fn: *(int):int =
  detail::hidden; return fn(42); }`. Direct calls already use
  `check_function_visibility`, but taking an address can bypass that path.
  The current KDI fix closes addresses/binds/redirectors for generated private
  dependency namespaces only; extending the ordinary address check needs its
  own overload/member visibility regression coverage.

- [ ] **Explicit `this` captures look only at the top model-builder context.**
  Statically identified in `klang/src/model/model_builder.cpp`,
  `visit_lambda_expression()`: the `cap.is_this` branch calls
  `current_context_content<aggregate>()`, although a lambda in a member body
  normally has a block at the top of `_contexts`. Repro:
  `struct Holder { value: int; make(): !():int {`
  `return [this]():int { return value; }; } }`.
  The builder reports `Cannot capture 'this' outside of a member function`
  instead of finding the lexical owning aggregate. Executable reproduction
  has not been run; unrelated to the free-template `runner` capture fix.

- [x] **Parameterised alias declaration-scope correction — accepted by later alias/import gates.**
  Full aliases passed **83 / 356**, imported callable aliases/typedefs **11 / 11**
  and native-alias import regressions **6 / 207**. Declaration/default/use-site
  provenance and private ABI coverage are green. This does not close the
  separately tracked hidden imported-template homonym extension below.
  Implemented in `klang/src/gen/resolvers_scope_lookup.cpp`,
  `scope_lookup::resolve_alias_template()`: explicit template arguments retain
  use-site resolution, while defaults and the substituted target resolve in the
  declaration scope. Arguments
  belong to the use site, but non-parameter names in the target must resolve in
  the alias declaration's scope, as `materialize_alias_type()` already does.
  Repro: `template<typename T, typename E> struct Pair { value:T; error:E; }`
  followed by `namespace definitions { struct Error {} template<typename T>
  alias Result : Pair<T, Error>; } namespace consumer { struct Error {}
  result : definitions::Result<int>; }`. The result must use
  `definitions::Error`, not `consumer::Error`. Imported aliases additionally
  need their original declaration scope preserved across KDI re-parsing.
  Permanent regressions are in `test-gen-alias-template.cpp` and
  `test-import-templates.cpp`, including defaults, consumer shadows, canonical
  callable parameters, re-export, and two homonymous imported alias families.
  Builds and reproductions were not run in that source-only correction task.
  Its historical carrier `00924` failures subsequently passed the alias gate.

- [ ] **Class/interface inheritance cycles can recurse before cycle diagnostics.**
  Statically identified in `klang/src/model/model.cpp`:
  `count_diamond_bases()` recursively visits class/interface bases without an
  active-path guard. `symbol_resolver::visit_unit()` calls
  `klass::compute_virtual_bases()` after resolving base names but before
  `visit_aggregate()` checks inheritance cycles. Repro input:
  `module inheritance_cycle; class A : public B {} class B : public A {}`.
  The diamond pre-pass can therefore exhaust the stack instead of reporting
  `ERR_STRUCT_SELF_INHERIT`. Apply equivalent cycle protection to interface
  graphs and retain diamond path counts for acyclic graphs. Executable
  reproduction has not been run; unrelated to the forward-base executor fix.

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
      **Implementation pending validation:** `kdi_importer` now builds declarations
      in origin-named namespaces without replacing the consumer AST/module.
      Lookup-only references and import-as-using exposure preserve canonical names
      and bare imported calls; both aggregate and full type-resolution phases stop
      at foreign lexical boundaries. Scoped unresolved types carry provenance so
      short-name caches cannot choose consumer shadows. Alias source/origin metadata
      is preserved by DTO, JSON, CBOR, and re-export (schema remains `0.1`).
      Existing hidden homonym coverage and the unhidden alias homonym regression
      must both be exercised before declaring this structural gap fixed.

- [x] **Normally enabled foundation stabilization gate — accepted in subsequent validation.**
  The implementation agent did not run builds/tests; separate Luna acceptance
  passed the enabled alias/import/function/template/foundation regressions.
  Historical aliases **83 / 356**, functions **182 / 669**, foundation API
  **15 / 104** and executor **16 / 48** are recorded in the session plan;
  latest foundation API is **19 / 216**. Hidden homonym and unconfirmed sibling
  private-dependency extensions retain their own open entries.
  `resolvers_type_ref.cpp` scores fresh lambda literals by callable signature;
  contextual argument/return materialization remains in the existing adaptation
  paths. Stored borrowed variables still cannot become owners, and existing
  owner-move cleanup remains responsible for clearing a source exactly once.
  `test-gen-lambda.cpp` covers direct owned arguments/returns, owner-capture
  destruction, explicit free-template calls, and borrowed/reference-capture rejection.
  Private FFI fixture helpers opt into the stdlib resolver; emitter callback owners
  omit empty names and void return suffixes. Native-handle provenance regressions
  are in `test-import-templates.cpp`.
  The historical handoff requested build/run of `klang-tests-gen-aliases`,
  `klang-tests-import-templates`,
  `klang-tests-import-core` (including hidden homonym imports),
  `klang-tests-gen-functions`, `klang-tests-gen-resolution`,
  `klang-tests-gen-templates-core`, and `kdi-tests`, then foundations/executor
  cases and the previously green Future/EventLoop/null-upcast baseline.
  KDI impact: DTO/JSON/CBOR changes **yes**, codec/compiler regression additions
  **yes**, spec changes **yes** (version `0.1`); type-converter changes **no**
  (existing canonical alias/type-reference conversion retained), tooling changes
  **no** (existing DTO-backed dump/conversion paths reviewed).
  Historical unresolved outcomes: `test-gen-template-instantiation.cpp`,
  "Template carrier resolves enum comparisons before body types"
  (`[gen][template][instantiation][enum][symbol-order]`), failed with `0F00D`
  at `err == 0`. This local test has no imports, so import-origin correction
  alone does not establish a fix. The codegen diagnostic now identifies both
  operand types and the enclosing function instead of merely reporting
  "non-primitive";
  the IoCompletion carrier/lifecycle cases in `test-gen-alias-template.cpp`
  failed with `00924`.   These normally enabled carrier/enum failures subsequently passed. The
  original source-only gate text is retained as history, not a current blocker.

- [ ] **Private FFI source dependencies from a different lexical namespace.**
  Awaiting executable sibling-scope coverage. `kdi_exporter.cpp`,
  `build_template_def()` now emits fully qualified, grammar-valid namespace
  fragments for `collect_private_types`; `kdi_importer.cpp` rebuilds those
  private-only fragments at the unit root instead of the template's namespace.
  Repro shape: `namespace native { private struct Handle {} }`,
  `namespace io { @k::ffi::Extern("C", null, "__handle") private nativeCall(
  h: native::Handle*): native::Handle*; template<typename T> call(): int {
  nativeCall(null); return 0; } }`. The emitted signature names
  `producer::native::Handle*`; verify that its prelude declares the handle
  under `producer::native`, not `producer::io`. No executable sibling-scope
  reproduction has been run.
  This does not block the same-namespace `k::io` handle foundation regressions;
  do not claim sibling-namespace private dependencies supported.

- [ ] **HashMap<String,String> heap corruption — pre-existing EnvironmentMap workaround.**
  `libk/libk/src/application.k` documents corruption while constructing or
  populating `HashMapNode<K,V>` / `MapEntry<K,V>` when both template arguments
  are the same class type, such as `String`. Its EnvironmentMap instead uses
  two parallel `Vector<String>` buffers and linear lookup. Bounded reproduction:
  construct and populate `HashMap<String,String>` with several distinct keys,
  retaining allocator stderr and the first failure stack. The source records
  glibc invalid-allocation/free diagnostics, but this docs task neither
  reproduced nor repaired the map. Application lifecycle acceptance does not
  establish a HashMap fix.

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

- [x] **Compiler const/meta CTest budget — resolved and accepted, 2026-10-04.**
  The historical full compiler replay passed **33/34** entries; the original
  `klang-tests-gen-types-const-meta` timed out twice at **120 seconds**.
  A full direct run of that original executable passed all **217 cases / 787
  assertions** in **145.72 seconds**, with no skipped or failing cases.
  The source split retains all four files exactly once, using `klang_gen_test`
  with the unchanged **120-second** timeout and shared setup:
  - `klang-tests-gen-types-const-null`: `test-gen-const.cpp` (**54 cases,
    38.941s**) + `test-gen-null-semantics.cpp` (**53 cases, 40.053s**);
    **107 cases**, **78.994s** summed original per-source duration.
  - `klang-tests-gen-types-meta-designated`: `test-gen-annotation-meta.cpp`
    (**70 cases, 35.627s**) + `test-gen-designated-init.cpp` (**40 cases,
    29.979s**); **110 cases**, **65.606s** summed original per-source duration.
  These sums are historical measurements, not measured split runtimes.
  Current regenerated category routes both pass within the unchanged
  **120-second** limit: const-null **107 cases / 253 assertions, 80.35 s**;
  meta-designated **110 / 534, 66.22 s**. They preserve **217 / 787** exactly.
  The original passing 33 entries contain **2,735 / 13,701**; together with
  the two replacement entries they cover all **35 current compiler CTest
  entries**, totaling **2,952 / 14,488**. This is combined coverage across
  replays, **not one single 35-entry CTest run**. The source-only preparation
  preceded this accepted validation; budgeting is not a compiler correctness
  or performance bug fix.

- [x] **Native ABI fatal-diagnostics fixture build blocker — resolved and accepted, 2026-10-04.**
  Historical `archive-final-build.log` records compilation failure at
  `klang/tests/test-compiler-fatal-diagnostics.cpp:211`: the fixture called the
  protected `function::update_mangled_name()`. After `set_extern_c_symbol()`,
  it now reassigns the existing qualified name through public `assign_name()`,
  refreshing the C symbol without changing production visibility. All three
  invalid-ABI sections, `INTERNAL_ERR_F01B`, symbol/type diagnostics and output
  suppression assertions remain unchanged. The source-only correction was
  subsequently validated: `[fatal-diagnostics]` passed **7 cases / 213
  assertions in 0.244 s**, and full regeneration succeeded. The protected
  production API was not widened.

- [x] **Static archive replacement acceptance — resolved and accepted, 2026-10-04.**
  Both `compiler::gen_static_library()` and `gen_libraries()` previously ran
  `ar rcs` on the existing destination. `ar` retained removed input objects,
  including obsolete `async_io.o`, `async_ffi.o` and `network_ffi.o`, and the
  generated K root object's changing temporary name accumulated old module
  definitions that could win static linking. The shared
  `compiler_linker.cpp::create_static_archive()` now creates a unique sibling
  directory with LLVM's filesystem helper, archives only current inputs there,
  and atomically renames the successful result over the destination. Tool,
  archiver and filesystem failures are explicit diagnostics; failed archive
  creation preserves the previous `.a` and KDI. Only owned temporaries are
  cleaned; KDI generation still follows binary publication.
  Four permanent `klangc-tests` cases in `test-klangc-object-input.cpp`,
  selector `[archive-replacement]`, separately cover static-only and combined
  generation, changed function results **1 -> 2**, removed C/K symbols, exact
  member counts, spaced fixture paths, failed `ar` preserving archive/KDI bytes,
  and a consumer forced to the fixture `.a` by removing only its owned `.so`.
  The historical source-only handoff is superseded by final acceptance:
  **4 cases / 141 assertions passed in 11.810 s** and full regeneration
  succeeded. The actual current `.a` contains **31 members**, exactly one
  PID-generated root (`libk_266532_0.o`), no duplicate global definitions and
  no legacy engines. The staged archive hash matches current; `libk.so` has
  `DT_NEEDED liburing.so.2`. A forced-current static-only consumer explicitly
  linked liburing and ICU, has **no `libk.so` DT_NEEDED**, and runs loopback.
  Copied staging tools/libraries/descriptors compile all three channels;
  syscall traces prove staged KDI and runtime opens with no build-default
  artifact opens. This is driver replacement acceptance, not deletion of the
  old archive to conceal the bug.

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

- [ ] Automated toolchain/runtime installation and packaging.
  The repository had no CMake `install()` rules before the proactor migration.
  Adding an installation framework is a future packaging feature, not a
  migration bug or current I/O acceptance prerequisite. Current acceptance
  covers generated shared/static/KDI artifacts and manually copied toolchain
  staging under `cmake-build-debug/`; an empty `cmake --install` success is
  not evidence of installed artifacts.
- Refactor libk C functions wrapping to reduce intermediate method counts
- Add following to base libk:
  - containers 
  - maths (to be completed)
  - time/date
  - log facade
  - filesystem
  - process
  - [ ] threading and synchronisation (`Thread`, `Mutex`, `Semaphore`, `Future<T>`, …)
  - [x] asynchronous file I/O (`FileChannel`, native fully parents, io_uring backend)
    - **F1/F2 ACCEPTED:** callback/Future/Sync fully forms use a
      native parent, continuous buffer lease, cumulative partial errors,
      atomic child replacement/cancel, one absolute deadline, and a
      whole-operation implicit FIFO reservation. Futures defer cancellation
      publication until child drain to preserve the final partial count.
      EOF is EndOfStream/ENODATA; nonempty zero-progress writes fail System/EIO.
      Ambiguous legacy blocking fully signatures are removed; primitive Sync
      aliases still serve existing file-entry/channel migration users.
    - Historical `[file-fully]` acceptance: **15 cases / 34,700 assertions**;
      current combined proactor **66 / 37,904**. Real-file API target:
      **3 / 24**, including direct/default executors. Five obsolete async
      stream tests are removed; the two channel lifecycle tests in
      `test-io-file-stream.cpp` and synchronous FileInput/OutputStream coverage remain.
    - Removed the obsolete async stream source and unused
      `async_io.[ch]`/`async_ffi.c` backend/build inputs after source dependency
      checks. G/H also removed `socket.k` and `network_ffi.c`; shared address
      values remain in `network_address.k`. No blocking transport fallback remains.
  - [x] proactive TCP/Unix stream and UDP/Unix datagram channels
    - G/H accepted; latest stream API **5 / 34**, datagram API **7 / 35**.
      Callback/Future/Sync, DNS deadlines, whole-direction gates, packet
      metadata and successful-bind-only Unix pathname ownership are covered.
  - [x] Application normal/unwind I/O shutdown before concrete field destruction
    - Lifecycle driver **2 / 78** plus **12 isolated child cases / 402**;
      stream shutdown adds **1 / 64** child case.
  - [x] final proactor packaging/regression/documentation closure — accepted, 2026-10-04
    - Fresh selected libk gate: **11/11 CTests in 243.720 s**,
      **349 cases / 39,139 assertions**,
      plus **13 child cases / 466 assertions**. Byte/file **69 / 223** and
      text/codec **72 / 232** preserve all **141 / 455** original I/O coverage.
    - Compiler model **141 / 1,137**, scoping **81 / 204**, and main/Phase4/friend
      focus are green; friend **35 / 92** retains private and negative checks,
      with unchanged grammar. The older **8/8 F2 compiler CTests, 752 / 3,050**
      precede the latest entry-point/friend changes. Final compiler coverage is
      the original **33** passing entries plus **2** category replacements:
      **35 current entries, 2,952 / 14,488**, **not a single 35-entry run**;
      exact budget results and historical timeout evidence are recorded above.
    - Docs/routing, full regeneration, shared/static/KDI artifacts and linkage,
      copied toolchain staging, fresh samples and compiler/KDI replay are
      accepted. Archive/fatal-fixture regressions and staged/static linkage
      evidence are recorded in their resolved entries above.
      Thread I/O file/socket/server/EventLoop/Future replay passed
      **58 cases / 245 assertions in 49.005 s**, including **6 UDP + 2 Unix**
      cases.
      Automated installation remains unchanged and out of scope; no new
      `install()` rules are required. Staging includes copied `klangc`,
      `kditool`, `libk.so`, `libk.a`, `libk.kdi` and `k.kdi`; all staged and
      generated outputs remain under `cmake-build-debug/`.
    - Current file fully, stream loopback, datagram loopback and fibo samples
      all compiled. Current/staged bounded stream/datagram loopbacks passed.
      The fixed-`/tmp` file sample was **not executed**.
    - KDI migration checklist: DTO updated **yes**; JSON updated **yes**, last
      validated **33 / 162**; CBOR updated **yes**, last validated **21 / 183**;
      converter changed **no**, alignment review **yes**; tooling dump/to-json/
      to-cbor byte-identical **yes**; tests added **yes**; spec updated **yes**.
      KDI CTest **1/1** passed. Schema remains **0.1**; H/J add no format fields.
      Fresh original/roundtrip canonical JSON is byte-identical; original
      descriptor unchanged. Both validator runs still fail with **exit 1,
      1,034 errors (247 empty llvm_def + 787 unresolved aggregate references)**.
      Historical **972 (247 + 725)** remains prior evidence; the additional
      **62** are in the same known categories, not schema-validation success.
    - Remaining nonblocking backlog stays OPEN: validator/exporter mismatch,
      unconfirmed allocator diagnostic, raw-native wrapper/carrier OOM cleanup
      source finding, and pre-existing `HashMap<String,String>` corruption.
      Successful final gates do not establish fixes for these entries.
- Add sub libraries for:
  - security (crypto, authn, authz)
  - net (http)
  - serialization
  - database client
  - message-bus client
