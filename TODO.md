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

  - [ ] **Template throws clause propagation**: the `throws` clause on a template
    function definition is not propagated to instantiated functions. This prevents
    compile-time contract enforcement for template methods that declare `throws`
    (e.g. `UniSlot<T>::construct() throws ConstructionException`).
    Test: `test-gen-exceptions.cpp` — "ConstructionException: contract enforcement"
    (marked `[!shouldfail]`).

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
- Add temporary object explicit construction (incl in return expr) — **struct form done**, **struct designated init done** (`S{.x=val}`), array temporary `T[]{init}` pending
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
- Exceptions — **in progress** (see `IN-PROGRESS-EXCEPTIONS.md`)
  - Deferred sub-features:
    - [ ] `finally` block (guaranteed execution regardless of exception path)
    - [ ] Exception masking/chaining (`throw new E2() from e1`)
    - [ ] `rethrow` / `throw;` (re-throw current exception in catch block)
    - [ ] Exception specifications on function pointer/reference types
    - [ ] `noexcept` conditional expression (`noexcept(expr)`)
    - [ ] Exception handling in static constructors/destructors
    - [ ] `MemoryException` integration with `new` operator and `MultiSlot<T>` — **in progress** (see `IN-PROGRESS.md`)
    - [ ] Fatal exception types for internal runtime errors (second wave of stdlib exceptions)
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
- [ ] Explicit template type arguments on intrinsic variadic methods (`_slot.construct<T>(value)`) fail in nested template contexts — workaround: omit explicit type args, rely on argument deduction (`_slot.construct(value)`)
- [ ] `if(var1; var2; ...; test)` still hard-fails during condition-variable initialization on union alternative mismatch / nullable addressor soft-fail cases; extend it to pattern-like semantics so a failed binding makes the whole condition `false` and skips evaluation of the trailing `test`

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
