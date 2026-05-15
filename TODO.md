## TODO and wish list
### K Language

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
  - Generics (template with uniform materialization whatever the arguments)
    See IN-PROGRESS.md for the completed implementation plan (Phases 1-10, 12).
    - [x] Phase 6: Generic constraint validator (direct usage of type param, owner constraint)
    - [x] Phase 7: Generic synthesis (single LLVM IR for all type-arg combinations)
    - [x] Phase 8: Type tracking at usage sites (generic_aggregate_instance)
    - [x] Phase 9: Mangling for generic synthesis (single symbol, no arg suffix)
    - [x] Phase 10: KDI export/import of generics (signature only, no source text)
    - [ ] Phase 11: libk template collections (`UniSlot<T>`, `MultiSlot<T>`, `LinkedList<T>`, `DoubleLinkedList<T>`, `Vector<T>`) (deferred — pending libk stabilisation)
    - [x] Phase 12: Full test suite for generics (test-gen-generic.cpp, 57 pass + 3 documented skip)
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
- Add temporary object explicit construction (incl in return expr) — **struct form done**, **struct designated init done** (`S{.x=val}`), array temporary `T[]{init}` pending
- Add return type covariance
- Add "virtual" symbols (parent, self, etc.)
- Add typed enums
  - Motivation: support enum entries backed by non-integer constant objects with deterministic index-based runtime representation.
  - Related tests: `klang/tests/test-gen-enum.cpp` (tag `[gen][enum][typed][expected]`)
- Add unions, typed unions
- Add state classes
- Better private visibility support
- Improve log and debug messages
- Add in-comment documentation support (e.g. for doc generation)
- Varargs
- Add constant values expression computation at compile time, enhance compile-time evaluation capabilities
- Add static conditional statements and static compiler value definitions
- Add traits and compile-time type introspection capabilities
- Add support for separate compilation and module interfaces (e.g. `export` keyword, module partitions)
- Add concepts
- Add traits (Rust like)
- Exceptions
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
