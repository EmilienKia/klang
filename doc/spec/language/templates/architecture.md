# Templates — Internal Architecture

> **Version:** Working Draft — 2026
> **Status:** Phase 1 implementation complete (except KDI export/import)

---

## Table of Contents

1. [Design Principles](#1-design-principles)
2. [Data Structures](#2-data-structures)
3. [Compilation Pipeline Integration](#3-compilation-pipeline-integration)
4. [Template Instantiation Engine](#4-template-instantiation-engine)
5. [Type and Value Substitution](#5-type-and-value-substitution)
6. [Error Handling](#6-error-handling)
7. [Name Mangling](#7-name-mangling)
8. [File Map](#8-file-map)

---

## 1. Design Principles

### 1.1 Model-Level Instantiation

Template instantiation operates entirely at the **model level**. When a template is
instantiated with concrete arguments, the instantiator clones model nodes (members,
fields, parameters, return types, expressions, statements) and substitutes type/value
placeholders with concrete types/values. There is **no AST cloning or re-parsing**.

This design enables:
- Programmatic model construction (no source code required).
- Future JIT compilation from model-only inputs.
- Simpler instantiation logic (one representation to clone).

AST nodes are retained optionally in model elements for diagnostic purposes (source
location in error messages) but are not required for instantiation.

### 1.2 Monomorphization

Each unique set of template arguments produces a distinct, fully-independent concrete
entity. There is no type erasure, runtime dispatch, or shared code between different
instantiations of the same template.

### 1.3 Lazy Instantiation with Caching

Templates are instantiated on first use. The `tpl_info::instantiations` map (keyed by
a canonical string encoding of the arguments) ensures that the same argument combination
is never instantiated twice.

---

## 2. Data Structures

### 2.1 `template_param_descriptor` (`template.hpp`)

Describes one template parameter:

```cpp
struct template_param_descriptor {
    template_param_kind kind;          // TYPENAME, STRUCT, CLASS, INTERFACE, VALUE
    std::string name;                  // Parameter name (e.g., "T", "N")
    std::shared_ptr<type> constraint_type;  // Optional base-type constraint
    std::shared_ptr<type> default_type;     // Optional default type (type params)
    k::value_type default_value;       // Optional default value (value params)
    std::shared_ptr<type> value_type;  // Type of value param (e.g., int, bool)
};
```

### 2.2 `template_argument` (`template.hpp`)

Represents one concrete argument at instantiation:

```cpp
struct template_argument {
    std::shared_ptr<type> type_arg;    // Non-null for type arguments
    k::value_type value_arg;           // Non-monostate for value arguments

    static template_argument make_type(std::shared_ptr<type>);
    static template_argument make_value(k::value_type);
};
```

`k::value_type` is a variant covering all primitive types:
`std::variant<std::monostate, nullptr_t, bool, char, unsigned char, short,
unsigned short, int, unsigned int, long, unsigned long, long long,
unsigned long long, float, double, string>`.

### 2.3 `tpl_info` (`template.hpp`)

Attached to template `aggregate` and `function` model nodes:

```cpp
struct tpl_info {
    std::vector<template_param_descriptor> params;
    std::unordered_map<std::string, std::shared_ptr<aggregate>> instantiations;  // for aggregates
    // or std::unordered_map<std::string, std::shared_ptr<function>> instantiations;  // for functions
};
```

The `instantiations` map is keyed by `build_instantiation_key(args)` — a canonical
string like `"int"`, `"int,10"`, `"float,MyClass"`.

### 2.4 Model Integration

- `aggregate::_tpl_info` / `function::_tpl_info`: `unique_ptr<tpl_info>`, non-null for
  template definitions.
- `aggregate::is_template()` / `function::is_template()`: returns `true` if `_tpl_info`
  is set.
- `aggregate::_tpl_base_name` / `function::_tpl_base_name`: original name before
  instantiation (e.g., `"Pair"`).
- `aggregate::_tpl_args` / `function::_tpl_args`: concrete argument list for
  instantiated entities.

---

## 3. Compilation Pipeline Integration

Template definitions and instantiations interact with the pipeline as follows:

| Phase | Template Definitions | Concrete Instantiations |
|---|---|---|
| **Lexer** | `template` and `typename` recognized as keywords | — |
| **Parser** | `TemplateDeclaration` parsed, AST nodes created | `TemplateArgList` parsed in `QualifiedIdentifier` |
| **Model Builder** | `tpl_info` created, members built with `unresolved_type` placeholders | — |
| **Symbol Resolver** | Skipped (`is_template()` guard) | Triggered on `Name<Args>` usage, creates concrete entity |
| **Aggregate Type Resolver** | Skipped (except constraint type resolution) | Processes normally (types are concrete) |
| **Type Reference Resolver** | Skipped | Processes normally |
| **Signature Resolver** | Skipped | Processes normally |
| **Model Materializer** | Skipped | Processes normally |
| **Declaration Generator** | Skipped | Emits LLVM declarations (regular entity) |
| **Implementation Generator** | Skipped | Emits LLVM IR bodies (regular entity) |

Concrete instantiations (produced by `template_instantiator`) have `is_template() == false`
and are treated as regular entities by all phases after the resolver that triggered their
creation.

---

## 4. Template Instantiation Engine

### 4.1 `template_instantiator` (`template_instantiator.hpp/.cpp`)

Two main entry points:
- `instantiate_aggregate(tpl_def, args, parent_ns, unit, ctx, logger)` → `shared_ptr<aggregate>`
- `instantiate_function(tpl_def, args, parent_ns, unit, ctx, logger)` → `shared_ptr<function>`

### 4.2 Instantiation Flow

1. **Build substitution maps**:
   - Type substitution map: `unordered_map<string, shared_ptr<type>>` mapping param
     names to concrete types.
   - Value substitution map: `unordered_map<string, k::value_type>` mapping param
     names to concrete values.

2. **Create concrete entity**:
   - New aggregate/function in the parent namespace.
   - Name: `build_instantiated_name(base_name, args)` (e.g., `"Pair__int"`).

3. **Clone and substitute members**:
   - Fields: clone with type substitution.
   - Methods: clone signature + body with type and value substitution.
   - Constructors, destructors: clone with substitution.
   - Expressions and statements: recursive cloning via `clone()` virtual methods
     with substitution maps.

4. **Register in cache**: `tpl_info::instantiations[key] = concrete_entity`.

### 4.3 Instantiation Triggers

Three resolver paths trigger instantiation:

1. **`aggregate_type_resolver::try_instantiate_template_type`** — when resolving a
   field type, base class, or parameter type like `Pair<int>`.

2. **`type_reference_resolver::try_instantiate_template_type`** — when resolving a
   type reference in an expression context.

3. **`gen_expressions::visit_function_invocation_expression`** — when resolving a
   function call like `max<int>(a, b)`.

---

## 5. Type and Value Substitution

### 5.1 Type Substitution

`substitute_type(type, substitution_map)` recursively rewrites types:

- `unresolved_type` with name matching a map key → replaced with concrete type.
- Wrapper types (`pointer_type`, `reference_type`, `owner_type`, `array_type`, etc.)
  → inner type substituted recursively.
- Other types → returned unchanged.

### 5.2 Value Substitution

`substitute_value_params(expression, value_substitution_map)` recursively walks
expression trees:

- `identifier_expression` matching a value param name → replaced with a
  `literal_expression` holding the concrete value.
- Compound expressions → children substituted recursively.

---

## 6. Error Handling

### 6.1 Constraint Validation

`validate_template_arg_constraints(params, args, err_idx, err_reason)` checks:

1. **Kind filter**: if param kind is `STRUCT`/`CLASS`/`INTERFACE`, the argument must
   be an aggregate of the correct kind.
2. **Base-type constraint**: if param has a `constraint_type`, the argument must be
   or derive from that type.

### 6.2 Error Propagation

- **Aggregate template errors** (resolvers): `throw_error()` with `resolution_error`
  — fatal, stops compilation for that entity.
- **Function template errors** (`gen_expressions`): `logger_relay::error()` +
  `args_ok = false` — non-fatal, allows overload resolution to continue and produce
  a "no matching overload" error.

### 6.3 Diagnostic Codes

| Code | Name | Description |
|------|------|-------------|
| `0x0180` | `ERR_TPL_NO_MATCHING` | No matching template found |
| `0x0181` | `ERR_TPL_ARG_COUNT_MISMATCH` | Wrong number of template arguments |
| `0x0182` | `ERR_TPL_ARG_WRONG_KIND` | Type argument is wrong aggregate kind |
| `0x0183` | `ERR_TPL_ARG_CONSTRAINT_VIOLATED` | Type argument doesn't satisfy constraint |
| `0x0184` | `ERR_TPL_ARG_NOT_AGGREGATE` | Type argument is not an aggregate |
| `0x0185` | `ERR_TPL_VALUE_ARG_TYPE_MISMATCH` | Value argument type mismatch |
| `0x0186` | `ERR_TPL_VALUE_ARG_NOT_CONST` | Value argument is not a constant |

---

## 7. Name Mangling

Template instantiations use Itanium-style `I…E` encoding:

```
N <length><name> I <args> E E

Type arg:   mangled type code (i, j, f, d, etc.)
Value arg:  L <type> <value> E
Boolean:    Lb0E or Lb1E
Negative:   n prefix (e.g., Lin5E for -5)
```

The mangler integrates template args into:
- `mangle_structure()` — aggregate names
- `mangle_function()` — function names
- `mangle_constructor()`, `mangle_destructor()` — member functions
- `mangle_type()` — when a `struct_type` refers to a template instantiation

---

## 8. File Map

| File | Role |
|------|------|
| `klang/src/model/template.hpp` | Core data structures: `template_param_descriptor`, `template_argument`, `tpl_info`, validation, error formatting |
| `klang/src/model/template.cpp` | Validation logic: `validate_template_arg_constraints()`, `format_constraint_error()` |
| `klang/src/model/template_instantiator.hpp` | Instantiator interface |
| `klang/src/model/template_instantiator.cpp` | Instantiation engine: cloning, substitution, caching |
| `klang/src/model/type.hpp` | `substitute_type()`, `type_substitution_map`, `value_substitution_map` |
| `klang/src/model/type.cpp` | `substitute_type()` implementation |
| `klang/src/model/model_builder.cpp` | Build `tpl_info` from AST, create `unresolved_type` placeholders |
| `klang/src/model/mangler.cpp` | `mangle_template_args()`, template-aware mangling |
| `klang/src/gen/resolvers.cpp` | Instantiation triggers in `aggregate_type_resolver`, `type_reference_resolver` |
| `klang/src/gen/gen_expressions.cpp` | Function template instantiation in `visit_function_invocation_expression` |
| `klang/src/gen/gen_struct.cpp` | `is_template()` guards in declaration/implementation generators |
| `klang/src/gen/gen_function.cpp` | `is_template()` guard in implementation generator |
| `klang/src/errors.hpp` | `template_diag` error codes (0x0180–0x0186) |

---

*This document describes the internal architecture as implemented. It is intended for
compiler developers working on the template subsystem.*

