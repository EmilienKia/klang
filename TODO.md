## TODO and wish list

### Current task : Add templates (Phase 1 — full instantiation only — in progress, see below)

Phase 1 implements the core template infrastructure with explicit-only instantiation.
See [doc/spec/language/templates/templates.md](doc/spec/language/templates/templates.md) for the full specification.

**Done:**
- [x] Lexer: add `template` and `typename` keywords (Milestone 1)
- [x] Parser: parse `TemplateDeclaration` clause before aggregates and functions (Milestone 2)
- [x] Parser: parse `TemplateArgList` in `QualifiedIdentifier` / `TypeSpec` (angle-bracket disambiguation) (Milestone 2)
- [x] AST: add `template_parameter` node, `template_arg_list` in `identified_type_specifier`, optional `template_params` in `aggregate_decl` and `function_decl` (Milestone 2)
- [x] Model: `template.hpp` with `template_param_descriptor`, `template_argument`, `tpl_info` (Milestone 3)
- [x] Model: `_tpl_info` in `aggregate` and `function`, `is_template()` helpers (Milestone 3)
- [x] Model builder: detect template declarations, build `tpl_info` model nodes, **continue processing members** with `unresolved_type` for template param references (Milestone 3–4)
- [x] Resolution passes: skip template definitions in all passes (symbol_resolver, aggregate_type_resolver, type_reference_resolver, signature_resolver, model_materializer, declaration_generator, implementation_generator) (Milestone 3)
- [x] Tests: lexer tests for `template`/`typename` keywords (Milestone 1)
- [x] Tests: parser tests for template declarations (Milestone 2)
- [x] Tests: model tests for template aggregate definitions (Milestone 3)
- [x] Tests: model tests for template function definitions (Milestone 3)
- [x] AST Cloner: deep copy of AST subtree (retained for general use, no longer required for instantiation) (Milestone 4)
- [x] Template Instantiator: **model-level** — clone model members, substitute types via `substitute_type()`, no AST dependency (Milestone 4)
- [x] Type substitution: `substitute_type()` utility recursively substitutes `unresolved_type` through wrapper type chains (Milestone 4)
- [x] Name generation: `build_instantiation_key` and `build_instantiated_name` helpers (Milestone 4)
- [x] Instantiation cache: same args return same instance, different args produce different instances (Milestone 4)
- [x] Tests: template instantiator model-level tests (aggregate + function + cache + names + member type verification + body cloning) (Milestone 4)
- [x] Symbol resolver: resolve template names, trigger monomorphization on `Name<Args>` usage (Milestone 5)
- [x] Aggregate type resolver: handle template type parameter substitution (Milestone 5)
- [x] Type reference resolver: resolve template type arguments in expressions (Milestone 5)
- [x] Tests: gen-jit integration tests for template struct instantiation (basic, member type, distinct types, caching, function params/returns, multi-params) (Milestone 5)
- [x] Suppress cosmetic "cannot resolve type: T" messages: mark template param unresolved_type as placeholder, suppress diagnostics in context::resolve_type, reorder resolve_one_type for template-arg types (Milestone 6)
- [x] Tests: stderr capture test verifying no cosmetic error messages during template compilation (Milestone 6)
- [x] Default template parameters: apply defaults when trailing arguments are omitted (incl. `<>` syntax) (Milestone 7)
- [x] Name mangling: encode template arguments (`I…E` markers) in mangled names (Milestone 8)
- [x] Function template call syntax and resolver integration (Milestone 9)
- [x] Type constraint checking: validate kind filter (`struct`/`class`/`interface`) and base-type constraint, with proper error diagnostics (Milestone 10)
- [x] Value parameter support: compile-time constant expression evaluation for value parameters, all primitive types (Milestone 11)
- [x] Declaration generator: template definitions skipped, concrete instantiations emitted as regular entities (Milestone 3+5)
- [x] Implementation generator: template definitions skipped, concrete instantiations emitted as regular entities (Milestone 3+5)
- [x] Tests: gen-jit tests for template functions (primitives, multi-type-params, cache) (Milestone 9)
- [x] Tests: gen-jit tests for template functions (primitives, structs)
- [x] Tests: gen-jit tests for template value parameters
- [x] Tests: gen-jit tests for template type constraints
- [x] Tests: gen-jit tests for template default parameters (Milestone 7)
- [x] Tests: error tests for invalid instantiations (wrong kind, missing args, etc.)
- [x] Tests: name mangling tests for template entities
- [x] Tests: comprehensive template tests (functions, structs, classes, interfaces, derived classes, member methods, primitive/aggregate type arguments, indirections)
- [x] Grammar: `grammar.ebnf` updated with `TemplateDeclaration`, `TemplateArgList`, updated `QualifiedIdentifier` (Milestone 1)
- [x] Spec: template specification in `doc/spec/language/templates/templates.md`
- [x] Spec: update `summary.md` with §25 Templates
- [x] Spec: update `grammar.md` with template rules
- [x] Spec: update `index.md` with Templates section

**To do — Phase 1 (remaining):**
- [ ] KDI exporter: export concrete instantiations as regular entities with `template_origin` metadata
- [ ] KDI importer: import concrete instantiations (recognise `template_origin`)
- [ ] libkdi: extend KDI DTOs/CBOR/JSON for `template_origin` field, bump schema version
- [ ] kdi-tool: display template origin info in `dump` and `json-dump` commands
- [ ] Tests: import tests for template instantiations across modules

**Deferred — Phase 2+ (not in scope):**
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





### K Language

- Add templates (Phase 1 — full instantiation — in progress, see above)
- Add templates (Phase 2+ — partial specialization, variadic templates, template template parameters, etc.)
- Review casting algorithm and implicit casting strategy (char[]! -> const char[]?  ou  char[]! -> const char[], etc.)
- Add temporary object explicit construction (incl in return expr) — **struct form done**, **struct designated init done** (`S{.x=val}`), array temporary `T[]{init}` pending
- Add return type covariance
- Add "virtual" symbols (parent, self, etc.)
- Add typed enums
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

