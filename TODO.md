## TODO and wish list

### K Language

- Review casting algorithm and implicit casting strategy (char[]! -> const char[]?  ou  char[]! -> const char[], etc.)
- Add temporary object explicit construction (incl in return expr) — **struct form done**, **struct designated init done** (`S{.x=val}`), array temporary `T[]{init}` pending
- Add return type covariance
- Add "virtual" symbols (parent, self, etc.)
- Add typed enums
- Add unions, typed unions
- Add state classes
- Add templates (Phase 1 — full instantiation — in progress, see below)

### Templates — Phase 1 (Full Instantiation Only)

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

**To do — Phase 1:**
- [x] Symbol resolver: resolve template names, trigger monomorphization on `Name<Args>` usage (Milestone 5)
- [x] Aggregate type resolver: handle template type parameter substitution (Milestone 5)
- [x] Type reference resolver: resolve template type arguments in expressions (Milestone 5)
- [x] Suppress cosmetic "cannot resolve type: T" messages for template param unresolved_type entries (Milestone 6)
- [x] Value parameter support: compile-time constant expression evaluation for value parameters (Milestone 11)
- [x] Type constraint checking: validate kind filter (`struct`/`class`/`interface`) and base-type constraint (Milestone 10)
- [x] Default template parameters: apply defaults when trailing arguments are omitted (incl. `<>` syntax)
- [x] Name mangling: encode template arguments (`I…E` markers) in mangled names
- [x] Template function call syntax: `func<type_args>(args)` disambiguation and instantiation (Milestone 9)
- [ ] Declaration generator: emit LLVM declarations for each concrete instantiation (with weak/COMDAT linkage)
- [ ] Implementation generator: emit LLVM IR bodies for each concrete instantiation
- [ ] KDI exporter: export concrete instantiations as regular entities with `template_origin` metadata
- [ ] KDI importer: import concrete instantiations (recognise `template_origin`)
- [ ] libkdi: extend KDI DTOs/CBOR/JSON for `template_origin` field, bump schema version
- [ ] kdi-tool: display template origin info in `dump` and `json-dump` commands
- [ ] Grammar: update `grammar.ebnf` with `TemplateDeclaration`, `TemplateArgList`, updated `QualifiedIdentifier`
- [ ] Spec: update `summary.md` with §25 Templates
- [x] Tests: gen-jit tests for template functions (primitives, multi-type-params, cache) (Milestone 9)
- [ ] Tests: gen-jit tests for template aggregates (struct, class, interface)
- [x] Tests: gen-jit tests for template value parameters (Milestone 11)
- [ ] Tests: gen-jit tests for template type constraints
- [x] Tests: gen-jit tests for template default parameters (Milestone 7)
- [ ] Tests: error tests for invalid instantiations (wrong kind, missing args, etc.)
- [ ] Tests: import tests for template instantiations across modules
- [x] Tests: name mangling tests for template entities

**Deferred — Phase 2+ (not in scope):**
- [ ] Template argument deduction for function templates
- [ ] Partial and full template specialization
- [ ] Template template parameters (`template<template<typename> class C>`)
- [ ] Variadic template parameters (parameter packs, fold expressions)
- [ ] `extern template` (explicit instantiation declarations)
- [ ] Template aliases (`template<typename T> using Vec = Array<T, 16>`)
- [ ] Concepts / type traits / static_if on template parameters
- [ ] Standalone template enum declarations
- [ ] Template constructors (independent of aggregate template)
- [ ] SFINAE-like overload filtering based on template constraints

### K Language (continued)
- Better private visibility support
- Improve log and debug messages
- Add in-comment documentation support (e.g. for doc generation)
- Varargs
- Add constant values expression computation at compile time, enhance compile-time evaluation capabilities
- Add static conditional statements and static compiler value definitions

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

