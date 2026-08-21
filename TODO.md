## TODO and wish list
### K Language

#### Features to add

- **Template-qualified type references in non-expression contexts**
  - Ensure support (with the same diagnostics quality) in every context that accepts
    type names and deferred symbol references.

- Add templates advanced features (partial specialization, variadic templates, template template parameters, etc.)
  - [ ] Tests: name mangling tests for template entities
  - [ ] Partial and full template specialization
  - [ ] Template template parameters (`template<template<typename> class C>`)
  - [ ] Variadic template parameters (parameter packs, fold expressions)
  - [ ] `extern template` (explicit instantiation declarations)
  - [ ] Value template parameters in parameterised aliases
        (`template<int N> alias Buf : byte[N];`) — parameterised aliases over
        *type* parameters are implemented (see
        `doc/spec/language/basic/aliases.md` §9); a value parameter is rejected
        because it would have to be evaluated at the use site, where it is not
        in scope. Tests: `klang/tests/test-gen-alias-template.cpp`.
  - [ ] Concepts / type traits / static_if on template parameters
  - [ ] Standalone template enum declarations
  - [ ] Template constructors (independent of aggregate template)
  - [ ] SFINAE-like overload filtering based on template constraints
  - [ ] Variable templates (`template<typename T> const size : int = ...`)
  - [x] Non-primitive value template arguments — **Phase 1+2 done**: enum constants,
        dependent value-param propagation, compile-time-constant expressions (arithmetic,
        logical, ternary, casts). **Phase 3 still open**: aggregate-typed value params
        (e.g. `template<Point P>`). See detailed entry under "Current bugs and gaps to
        fix" below.
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
  - [ ] Compile time evaluation of in-line structure initialization
  - [ ] Compile time evaluation of in-line array initialization
  - [ ] Compile time evaluation of in-line union and enum initialization
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
    - [x] Exception specifications on function pointer/reference types — **Done in Phase B.2**: Callable types 
      now carry explicit throws clauses like free functions; KDI import/export and variance checking all 
      respect the throws set.
    - [ ] `noexcept` conditional expression (`noexcept(expr)`)
    - [ ] Exception handling in static constructors/destructors
    - [ ] Unhandled FatalError diagnostic: when an uncaught FatalError propagates past `main()`, the runtime should print a diagnostic message (exception type, code, optional stack trace) before terminating the process
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
  - [ ] Déduction du type de retour des fonctions — in the short term this only needs to
        propagate the destination callable context into capture-free lambdas; a broader
        function/lambda return inference pass would collect `return` expressions first and
        resolve the final type afterwards.
  - [ ] Lambda return-type inference from callable destination context (capture-free lambdas still need an explicit return type in some paths)
  - [ ] Full capture semantics: reference vs value capture, capture-by-binding-time, mutable/const inference
  - [ ] Dynamic closure allocation and environment storage
  - [ ] Capture-with-escaping warnings/errors (scope lifetime analysis)
  - [ ] Bracket-less lambda syntax (`[]` omission when no captures)
  - [ ] Higher-order composition helpers (`compose`, `andThen`) on callables
  - [ ] Callables in generic bodies (currently unsupported due to fat-pointer size)
  - [ ] Callables as non-type template parameters (value-template instantiation)
  - [ ] Thread-local callable state (for thread factories, event handlers)

#### Latest full test-suite backlog (2026-08-11, updated 2026-08-11)

Full baseline run (HEAD `75d3feb`) executed with:

- `cd cmake-build-debug && ninja -j3 && ctest --output-on-failure`

Result: **92% tests passed, 2 tests failed out of 26**.

Timeout follow-up and mitigation (same day):

- `klang-tests-gen-core` was close to the 120s budget and timed out under `ctest`
  despite passing standalone; timeout increased to **180s**.
- `libk-tests` was too large for a stable 120s budget under `ctest`; it was split
  into two functional targets:
    - `libk-tests-core` (exceptions/RTTI/strings/value types)
    - `libk-tests-collections-io` (collections + stream/file/path I/O)
  Both received a **180s** timeout budget.
- Targeted verification run:
  - `ctest -R "^(klang-tests-gen-core|libk-tests-core|libk-tests-collections-io)$" --output-on-failure`
  - **100% passed (3/3)**.


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

- **Callable `throws` clause in a parameter list is greedy.** The type specification
  `*(int):int throws A, B` reads its exception list until a token that cannot start a type,
  so inside a parameter list the comma separating the parameters is swallowed by the throws
  clause: `f(p: *(int):int throws Boom, v: int)` fails to parse. A callable parameter that
  declares a `throws` clause must therefore currently be the **last** parameter. Options:
  parenthesise the throws list, terminate it with an explicit token, or restrict the greedy
  scan when the callable type is parsed inside a parameter list. Repro/target-behaviour test:
  `test-gen-callable-alias.cpp`, "callable throws clause in a parameter list is greedy"
  (`[.][gen][callable][throws][parser-limitation]`).

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
- [ ] `if(var1; var2; ...; test)` still hard-fails during condition-variable initialization on union alternative mismatch / nullable addressor soft-fail cases;
      extend it to pattern-like semantics so a failed binding makes the whole condition `false` and skips evaluation of the trailing `test`
- [ ] **Broaden compile-time constant-expression support for template values and
      compile-time configuration values.**
      Current behavior still has a parser/evaluator gap in template value-argument
      contexts: unparenthesized binary defaults such as `template<int N = 1 + 2>`
      are rejected because `parse_template_arg_value_expr()` currently accepts only
      unary-prefix + primary forms. Workaround today: `template<int N = (1 + 2)>`.
      This should be addressed as part of a larger compile-time constant-expression
      initiative, not as an isolated parser tweak.
      - Scope expected for this workstream:
        1. Full expression parsing in template value-arg/default contexts
           (without breaking `<...>` disambiguation).
        2. Shared/centralized constexpr evaluation usable beyond template args
           (e.g. global initialization/instantiation optimization opportunities).
        3. First-class compile-time parameter values exposed by the compiler
           (for example target/platform-dependent constants).
      - Goal: make compile-time values a coherent subsystem that can drive both
        semantic correctness and optimization decisions (including global
        instantiation paths).
- [ ] **Exception cause chaining is unavailable for pointer-shaped throws.**
  `throw new MyError(...)` copies only the *pointer* into the
  `__cxa_allocate_exception` block, so the `_cause` / `_cause_handle` fields of
  `Throwable` cannot be written (they would land outside the 8-byte allocation).
  Cause chaining is therefore skipped for that form
  (`implementation_generator::visit_throw_statement`, guard
  `exception_stored_by_value` in `gen/gen_statements.cpp`). The by-value idiom
  `throw MyError(...)` — used throughout `libk` — is unaffected.
  Proper support requires boxing the pointed-to object (or storing the cause out
  of line) and adjusting the catch-side base-offset arithmetic, which currently
  assumes the exception storage *is* the object.

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
  - [x] threading and synchronisation (`Thread`, `Mutex`, `Semaphore`, `Future<T>`, …)
  - [x] asynchronous file I/O (`FileChannel`, `AsyncFile*Stream`, io_uring backend)
  - networking
- Add sub libraries for:
  - security (crypto, authn, authz)
  - net (http)
  - serialization
  - database client
  - message-bus client
