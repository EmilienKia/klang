# Symbol & Type Resolution — Architecture & Process

This document describes the detailed architecture, data structures, algorithms, and multi-pass pipeline used by `klangc` for **symbol resolution**, **scope lookup**, **type materialization**, and **type checking**.

---

## 1. Why Multi-Pass Resolution?

In the K language, symbols may be used before they are declared, types may refer to one another cyclically (e.g., mutually recursive structures or pointers), classes may inherit from templates that require on-demand instantiation, and function bodies may rely on type inference.

A single-pass compiler cannot resolve these patterns without arbitrary lookahead or forward-declaration restrictions. `klangc` solves this by decoupling resolution into four strictly ordered passes:

```
[AST Unit]
    │
    ▼
┌──────────────────────────────────────────────────────────────┐
│ Phase 3: Model Builder                                       │
│ Constructs empty/unresolved model graph nodes.               │
└──────────────────────────────┬───────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────┐
│ Pass A: Symbol Resolver (symbol_resolver)                    │
│ - Walks the model hierarchy to resolve identifiers.          │
│ - Binds variables, functions, parameters, and redirects.     │
│ - Constructs initial vtable layouts and destructor slot 0.   │
│ - Validates member visibility, friends, and annotations.     │
└──────────────────────────────┬───────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────┐
│ Pass B: Aggregate Type Resolver (aggregate_type_resolver)    │
│ - Resolves type references in aggregate headers/signatures.  │
│ - Does NOT visit function bodies or expressions.             │
│ - Discovers template types & triggers instantiations.        │
│ - Calls context::resolve_types() to build LLVM StructTypes.  │
└──────────────────────────────┬───────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────┐
│ Pass C: Model Materializer (model_materializer)              │
│ - Validates abstract vtable slots in non-abstract classes.   │
│ - Computes secondary vtable thunk specs & byte offsets.      │
└──────────────────────────────┬───────────────────────────────┘
                               │
                               ▼
┌──────────────────────────────────────────────────────────────┐
│ Pass D: Type Reference Resolver (type_reference_resolver)    │
│ - Visits all expressions and statements.                     │
│ - Infer types, performs overload selection, adapts types.    │
│ - Verifies exception contracts (throws specifications).      │
│ - Verifies variable initialization order & unused imports.   │
└──────────────────────────────────────────────────────────────┘
```

---

## 2. Scope Hierarchy & Lookup Algorithms (`scope_lookup`)

Scope lookup is encapsulated in `k::model::gen::scope_lookup`. The model itself is purely structural; all scoping heuristics, visibility checks, and lookup algorithms reside in the resolver layer.

### 2.1 The Scope Tree

The scope hierarchy is walked upward starting from an innermost `k::model::element` up to the root namespace:

```
┌────────────────────────────────────────────────────────┐
│                   k::model::unit                       │
│  (Root Namespace: unit::get_root_namespace())          │
│  └── Imported Modules (k::model::imported_module)      │
└───────────────────────────▲────────────────────────────┘
                            │
┌───────────────────────────┴────────────────────────────┐
│              k::model::ns (Namespace)                  │
│  - Member Variables, Functions, Aggregates, Enums      │
│  - Using Directives (using_holder)                     │
└───────────────────────────▲────────────────────────────┘
                            │
┌───────────────────────────┴────────────────────────────┐
│           k::model::aggregate (Class/Struct)           │
│  - Member Variables, Member Functions, Nested Types    │
│  - Base Class Hierarchy (get_bases())                  │
│  - Friend Declarations (friend_holder)                 │
└───────────────────────────▲────────────────────────────┘
                            │
┌───────────────────────────┴────────────────────────────┐
│            k::model::function (Function)               │
│  - Parameter List (_parameters)                        │
│  - Implicit 'this' Parameter (_this_param)             │
└───────────────────────────▲────────────────────────────┘
                            │
┌───────────────────────────┴────────────────────────────┐
│    k::model::block / for_statement / catch_clause      │
│  - Local Variable Definitions                          │
└────────────────────────────────────────────────────────┘
```

### 2.2 Lookup Algorithms

#### 1. Variable Lookup (`scope_lookup::lookup_variable`)
Given a starting `element` and a simple `std::string` identifier:
1. Walk up the parent chain:
   - If the element is a `block`, check its local variables.
   - If the element is a `for_statement` or `catch_clause`, check its loop/catch variable.
   - If the element is a `function`, check its parameters and `_this_param`.
   - If the element is an `aggregate`, check its member variables. If not found, recursively search base classes in depth-first / BFS order.
   - If the element is a `ns` (namespace), check its namespace-level global variables.
2. If not found in the lexical chain, evaluate `using` directives in the active scopes.
3. Fall back to imported module exports.

#### 2. Function & Overload Lookup (`scope_lookup::lookup_functions`)
Because K supports function overloading, name resolution collects **all** matching overloads:
1. When starting inside an `aggregate`:
   - Search the aggregate's own function table.
   - Search base class hierarchies for inherited functions.
2. If no matching member function is found, search enclosing namespaces and outer scopes upward.
3. Search imported namespaces made visible by `using namespace` or `using <target>`.
4. Return a `std::vector<std::shared_ptr<function>>` representing the viable overload candidate set.

#### 3. Aggregate & Type Lookup (`scope_lookup::lookup_structure_or_import`)
1. Handles simple names by walking the lexical scope chain for `structure`, `klass`, `interface`, or `annotation_type`.
2. Handles qualified names (e.g. `k::io::FileStream`):
   - If the name has a root prefix (`::`), start at `unit->get_root_namespace()`.
   - Otherwise, resolve the first path component from the current scope, then descend children.
3. If not found locally, look up imported KDI modules (`unit::get_or_create_imported_aggregate()`).

#### 4. Alias & Typedef Lookup (`scope_lookup::lookup_alias`)
- Traverses `alias_holder` scopes (blocks, aggregates, namespaces).
- Resolves soft aliases (`alias NewName : Target;`) and strong typedefs (`typedef NewType : Target;`).
- Parameterized aliases (`template<typename T> alias Vec : Array<T, 16>;`) are substituted on the fly without generating new nominal entities.
- Cyclic alias chains are detected and rejected (`ERR_ALIAS_CYCLE`).

### 2.3 Visibility & Access Control

Access validation is performed during symbol lookup via `scope_lookup::is_struct_member_accessible()`:
- `PUBLIC`: Accessible from any scope.
- `PROTECTED`: Accessible from member functions of the declaring aggregate and any transitively derived aggregate (`aggregate::is_derived_from()`). Also accessible from nested aggregates within the declaring class.
- `PRIVATE`: Accessible only from member functions of the declaring aggregate or types nested within it.
- `friend`: Friendship allows specific functions or whole aggregates to access `private` and `protected` members. Friendship is non-transitive and not inherited.

---

## 3. Pass A: Symbol Resolution (`symbol_resolver`)

The primary goal of Pass A is to bind textual symbol expressions to concrete semantic model definitions.

### Key Responsibilities

1. **Symbol Binding**:
   - Replaces unresolved `symbol_expression` names with explicit targets: `variable_definition`, `function`, or `parameter`.
   - Automatically injects the implicit `this` parameter for non-static member functions (`function::create_this_parameter()`).
2. **Vtable & Destructor Seeding (`build_vtable_layout`)**:
   - Identifies virtual functions (`virtual`, `override`, `abstract`, interface methods).
   - Inherits vtable slot layouts from the primary base class.
   - **Universal Destructor Slot 0**: `::k::Object` establishes the universal virtual destructor at Vtable Slot 0. Every derived class destructor automatically overrides Slot 0.
   - Validates override contracts: checks that functions marked `override` match an existing base slot, warns if a virtual method is overridden without `override`, and rejects attempts to override `final` methods or `private` base functions.
3. **Function Redirect Chains (`resolve_redirect_chains`)**:
   - Resolves function redirection syntax (`fn foo() : int -> bar;`).
   - Follows redirect chains transitively to their ultimate target and detects circular redirects.
4. **Enum Resolution (`resolve_enumeration`)**:
   - Resolves enum entries, base enumerations, and underlying integral types (`byte`, `short`, `int`, `long`).
5. **Annotation Validation (`resolve_and_validate_annotations`)**:
   - Validates annotation targets against `@Target` constraints (`CLASS`, `INTERFACE`, `FUNCTION`, `VARIABLE`, etc.).
   - Checks retention policy (`@Retention(Policy::SOURCE)` vs `RUNTIME`).

---

## 4. Pass B: Aggregate & Signature Resolution (`aggregate_type_resolver`)

Pass B resolves the structural skeleton of the module without entering function bodies.

```
For each Aggregate in Unit:
  1. Resolve Base Class Type References (populate aggregate::_bases)
  2. Compute Virtual Base Flags for Diamonds (klass::compute_virtual_bases_single)
  3. Resolve Member Variable Types
  4. Resolve Constructor & Destructor Signatures
  5. For each Member Function:
       Resolve Parameter Types & Return Type
       (Do NOT visit function body statements or expressions)

For each Global Function in Unit:
  Resolve Parameter Types & Return Type
```

### Template Type Discovery & Instantiation

When `aggregate_type_resolver` encounters an `unresolved_type` that carries template arguments (e.g., `Vector<int>` or `Map<String, User>`):
1. Looks up the primary template definition (`aggregate::get_tpl_info()`).
2. Evaluates template arguments (type arguments, constant value arguments).
3. Invokes `k::model::template_instantiator::instantiate_aggregate()`:
   - Checks instantiation cache to avoid duplicate work.
   - Clones AST declaration nodes with template parameters substituted.
   - Creates a new concrete `klass` or `structure` with a unique mangled name.
   - Injects the instantiated aggregate into the model unit.
4. Calls `_context->resolve_types()` to generate LLVM struct types for newly instantiated aggregates.

---

## 5. Pass C: Model Materializer (`model_materializer`)

Pass C runs after all aggregate types and signatures are stable, preparing metadata required for code generation.

### Key Responsibilities

1. **Abstract Class Validation (`validate_vtable`)**:
   - For every concrete (non-abstract) class, verifies that all inherited abstract vtable slots have a concrete implementation.
   - Rejects instantiation of abstract types.
2. **Secondary Vtable Thunk Specification (`compute_secondary_vtable_specs`)**:
   - In multiple and interface inheritance, secondary base subobjects sit at non-zero byte offsets within the derived class layout.
   - Uses the target machine's `llvm::DataLayout` to calculate the exact byte offset of each secondary base subobject.
   - Builds `thunk_info` descriptors for each secondary vtable slot, specifying the necessary negative `this`-adjustment offset so the runtime can adjust the receiver pointer before jumping to the derived implementation.

---

## 6. Pass D: Type Reference Resolution (`type_reference_resolver`)

Pass D performs full type inference, expression type decoration, type checking, and statement validation.

### 6.1 Expression Type Deduction & Adaptation

Every expression node is visited to deduce its `_type`:
- **Literal Expressions**: Mapped to primitive types (`int`, `double`, `bool`, `char[]`, `null`).
- **Binary & Arithmetic Expressions**: Evaluates operand type compatibility, applies promotion rules (e.g. `int` + `long` -> `long`), or selects user-defined operator overloads.
- **Member Access (`.` and `->`)**: Resolves field offsets, handles implicit dereferencing, and performs automatic struct upcasting when accessing base members.
- **Type Adaptation (`adapt_type`)**:
  - Injects `cast_expression` nodes where implicit conversions are allowed (e.g. derived pointer to base pointer, integer widening, reference loading).
  - Handles addresser conversions (`*`, `?`, `+`, `&`, `!`, `#`).
  - Wraps lvalue references in `load_value_expression` when rvalues are expected (`adapt_reference_load_value`).

### 6.2 Overload Selection Algorithm

When a function call is resolved against a set of candidates:
1. Filter candidates by argument count (taking default parameter values and varargs into account).
2. Rank candidates by conversion cost:
   - **Exact Match**: 0 cost.
   - **Const-qualification widening**: low cost.
   - **Derived-to-Base Upcast**: proportional to inheritance depth.
   - **Primitive Widening**: integer promotion cost.
3. If a single candidate has strictly lower cost than all others, it is selected.
4. If multiple candidates tie for the lowest cost, compilation halts with `ERR_AMBIGUOUS_FUNCTION_CALL`.
5. If no candidate is viable, compilation halts with `ERR_NO_MATCHING_FUNCTION`.

### 6.3 Exception Contract Checking

K enforces strict exception contracts:
- Functions declare thrown types via `throws Type1, Type2`.
- A function without a `throws` clause is `noexcept`.
- Inside function bodies, `type_reference_resolver` maintains a `_try_catch_stack`:
  - Every `throw <expr>` or call to a throwing function must either be handled by an enclosing `try-catch` matching the exception type (or a superclass), or be listed in the enclosing function's `throws` specification.
  - Unhandled exceptions trigger `ERR_UNHANDLED_EXCEPTION`.
  - Bare `throw;` (rethrow) statements are validated to ensure they only appear inside a `catch` block.

### 6.4 Initialization Order & Unused Import Checks

1. **Initialization Order (`resolvers_init_order.cpp`)**:
   - Analyzes local variable definitions and constructors.
   - Verifies that variables are not read before assignment and that member fields are initialized according to declaration order.
2. **Unused Imports (`check_unused_imports`)**:
   - Throughout Passes A-D, whenever an imported symbol is resolved, its containing `k::model::imported_module` is marked as used.
   - At the conclusion of Pass D, any explicitly imported module that was never referenced triggers `WARN_UNUSED_IMPORT`.

---

## 7. Resolver Diagnostic Codes Summary

| Subsystem / Pass | Code Range | Typical Diagnostics |
|------------------|------------|---------------------|
| **Symbol Resolution** | `0x0100 – 0x0150` | `ERR_SYMBOL_NOT_FOUND`, `ERR_DUPLICATE_SYMBOL`, `ERR_PRIVATE_MEMBER_ACCESS`, `ERR_FRIEND_ACCESS_DENIED`, `ERR_OVERRIDE_NOT_OVERRIDING`, `ERR_REDIRECTION_CYCLE`. |
| **Aggregate & Vtables** | `0x0151 – 0x0190` | `ERR_ABSTRACT_MEMBER_NOT_IMPLEMENTED`, `ERR_INSTANTIATE_ABSTRACT_CLASS`, `ERR_CIRCULAR_INHERITANCE`, `ERR_FINAL_CLASS_INHERITANCE`. |
| **Type Checking** | `0x0191 – 0x01E0` | `ERR_TYPE_MISMATCH`, `ERR_NO_MATCHING_FUNCTION`, `ERR_AMBIGUOUS_FUNCTION_CALL`, `ERR_UNHANDLED_EXCEPTION`, `ERR_INVALID_CAST`. |
| **Callables & Lambdas** | `0x01E1 – 0x0210` | `ERR_CALLABLE_INCOMPATIBLE_SIGNATURE`, `ERR_CALLABLE_COVARIANCE_NEEDS_ADJUSTMENT`, `ERR_CALLABLE_THROWS_NOT_SUBSET`, `ERR_CALLABLE_NULL_TO_NONNULL`. |
