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
    - [ ] Nested-node template collection runtime remains unstable under JIT for non-trivial list patterns (allocation/link/destruction path)
    - [ ] Imported template aggregate methods from KDI/signature-only metadata do not yet materialize executable bodies in consumer modules; this currently blocks re-enabling template stdlib collections end-to-end
  - Covariance for generics (invariant only in Phase 1; co/contra-variance is a future feature)

- Review casting algorithm and implicit casting strategy (char[]! -> const char[]?  ou  char[]! -> const char[], etc.)
- Add support of foreach loops
- Add placement new operator and support for custom allocators
- Add non-construct memory allocation and deallocation intrinsics (e.g. `alloc(size)`, `dealloc(ptr, size)`) for manual memory management
- Add FFI memcopy/memmove intrinsics for efficient raw memory manipulation
- Add return type covariance
- Add "virtual" symbols (parent, self, etc.)
- Enumerations
    - [ ] Enum methods (functions declared inside enum body)
    - [ ] Standalone template enum declarations (`template<T> enum ...`)
    - [ ] Pattern matching / `match` expression on enum values (depends on switch/case)
    - [ ] Bitflags / combined enum values (bitwise operations on enums)
    - [ ] `values()` / `count()` / `name()` intrinsics on enums
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

### K compiler and language specifics for compiler capabilities
- Add support for producing inline documentation (e.g. via `///` comments) and generating API reference docs from it
- Add support for generating language bindings (e.g. C header generation) from K code
- Add iterative compilation mode for faster edit-compile-test cycles (e.g. via an interactive REPL or watch mode)
- Add support for incremental compilation and caching of intermediate results to speed up subsequent builds
- Add support for cross-compilation to different target architectures and platforms

### K language limitations (compiler bugs / missing features)
- [ ] Calling a member method on `this` from within a constructor body crashes at
      runtime: methods are virtual by default and dispatch through the vtable, but the
      vtable pointer is only initialised *after* the constructor body runs
      (`gen_function.cpp`, `emit_constructor_post_block`; see the note at the
      `// NOTE: vptr initialization ... is deferred to AFTER the block` line). The vptr
      should be stored after base-constructor calls but before the user body, like C++.
      Workaround: inline the logic instead of calling helper methods from constructors
      (done in `k::StringBuilder`'s constructors). Minimal repro:
      `class T { _n:unsigned int; public: T():_n(0){ doit(); } doit(){ _n=5u; } }` — the
      `doit()` call from the ctor segfaults.
- [ ] Implicit user-defined cast-operator conversions are not applied: a class with
      `operator() : T` is not implicitly converted in an initialisation/argument context
      (e.g. `x : int = wrapper;` yields garbage). Only explicit casts work, and only for
      a cast operator defined in the *same* module — `(T) imported_value` reports
      "casting between non-primitive types is not yet supported" for an imported class.
      Affects the ergonomic read path of `StringBuilder`'s `CharRef` proxy (use
      `charAt(i)` to read; `sb[i] = c` to write).
- [ ] Explicit template type arguments on intrinsic variadic methods (`_slot.construct<T>(value)`) fail in nested template contexts — workaround: omit explicit type args, rely on argument deduction (`_slot.construct(value)`)
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
