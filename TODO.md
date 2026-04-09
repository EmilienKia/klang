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
- (nothing yet)

**To do — Phase 1:**
- [ ] Lexer: add `template` and `typename` keywords
- [ ] Parser: parse `TemplateDeclaration` clause before aggregates and functions
- [ ] Parser: parse `TemplateArgList` in `QualifiedIdentifier` / `TypeSpec` (angle-bracket disambiguation)
- [ ] AST: add `template_parameter` node, `template_arg_list` in `identified_type_specifier`, optional `template_params` in `aggregate_decl` and `function_decl`
- [ ] Model: `template_parameter` (type or value), `template_definition<T>` wrapper for function/aggregate, `template_instantiation_registry` in `unit`
- [ ] Model builder: detect template declarations, build `template_definition` model nodes
- [ ] Symbol resolver: resolve template names, trigger monomorphization on `Name<Args>` usage
- [ ] Aggregate type resolver: handle template type parameter substitution
- [ ] Type reference resolver: resolve template type arguments in expressions
- [ ] Value parameter support: compile-time constant expression evaluation for value parameters
- [ ] Type constraint checking: validate kind filter (`struct`/`class`/`interface`) and base-type constraint
- [ ] Default template parameters: apply defaults when trailing arguments are omitted
- [ ] Name mangling: encode template arguments (`I…E` markers) in mangled names
- [ ] Declaration generator: emit LLVM declarations for each concrete instantiation (with weak/COMDAT linkage)
- [ ] Implementation generator: emit LLVM IR bodies for each concrete instantiation
- [ ] KDI exporter: export concrete instantiations as regular entities with `template_origin` metadata
- [ ] KDI importer: import concrete instantiations (recognise `template_origin`)
- [ ] libkdi: extend KDI DTOs/CBOR/JSON for `template_origin` field, bump schema version
- [ ] kdi-tool: display template origin info in `dump` and `json-dump` commands
- [ ] Grammar: update `grammar.ebnf` with `TemplateDeclaration`, `TemplateArgList`, updated `QualifiedIdentifier`
- [ ] Spec: update `summary.md` with §25 Templates
- [ ] Tests: lexer tests for `template`/`typename` keywords
- [ ] Tests: parser tests for template declarations and instantiations
- [ ] Tests: gen-jit tests for template functions (primitives, structs)
- [ ] Tests: gen-jit tests for template aggregates (struct, class, interface)
- [ ] Tests: gen-jit tests for template value parameters
- [ ] Tests: gen-jit tests for template type constraints
- [ ] Tests: gen-jit tests for template default parameters
- [ ] Tests: error tests for invalid instantiations (wrong kind, missing args, etc.)
- [ ] Tests: import tests for template instantiations across modules
- [ ] Tests: name mangling tests for template entities

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

