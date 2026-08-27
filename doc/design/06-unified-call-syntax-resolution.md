# 06. Unified Call Syntax (UCS) & Member Invocation Resolution

This document describes the technical architecture, candidate collection pipeline, filtering semantics, template deduction integration, and overload resolution mechanics for **Unified Call Syntax (UCS)** and member function invocation in the Klang reference compiler (`klangc`).

---

## 1. Overview and Core Design Principles

Unified Call Syntax (UCS) allows functions declared outside a structure or class (such as free functions in namespaces or imported modules), as well as **static member methods** declared on an aggregate or its base hierarchy, to be invoked using member call syntax:

```k
// Invoking static member template Sequence<T>::accumulate<R> via Unified Call Syntax
result = vec.accumulate(0, [](acc: int, v: const int&) { return acc + v; });

// Unified Call Syntax with explicit template arguments
result = vec.accumulate<int>(0, [](acc: int, v: const int&) { return acc + v; });

// Traditional static member method invocation
result = Sequence<int>::accumulate<int>(vec, 0, [](acc: int, v: const int&) { return acc + v; });
```

### 1.1. Motivation
1. **Fluent API & Method Chaining**: Enables ergonomic pipelining of transformations (e.g., sequences, collections, views) without polluting core aggregate definitions.
2. **Open Extension without Subclassing**: Allows standard libraries and external packages to extend types (including fundamental and generic aggregates like `Vector<T>`, `LinkedList<T>`, `Span<T>`) without intrusive modifications.
3. **Uniform Member vs. Free Syntax**: Call sites can evolve naturally between internal member methods and external helper functions without breaking calling code.

---

## 2. Invocation Resolution Pipeline in `type_reference_resolver`

During **Pass D** (`type_reference_resolver::visit_function_invocation_expression`), member invocation expressions (`obj.func(...)` or `obj.func<...>(...)`) follow a multi-stage pipeline:

```
              ┌──────────────────────────────────────────────┐
              │  1. Receiver & Argument Expression Typing   │
              │     - Resolve receiver type (st)             │
              │     - Unpack references / owners / qualifiers │
              │     - Type all call-site arguments           │
              └──────────────────────┬───────────────────────┘
                                     │
                                     ▼
              ┌──────────────────────────────────────────────┐
              │  2. Comprehensive Candidate Collection      │
              │     - Direct & inherited member methods      │
              │     - Caller scope free / static functions   │
              │     - `using namespace` imported functions   │
              │     - Module-level imported functions        │
              └──────────────────────┬───────────────────────┘
                                     │
                                     ▼
              ┌──────────────────────────────────────────────┐
              │  3. Filtering & Scope Isolation              │
              │     - Const-receiver validation              │
              │     - Isolation: exclude unrelated members   │
              └──────────────────────┬───────────────────────┘
                                     │
                                     ▼
              ┌──────────────────────────────────────────────┐
              │  4. Template Instantiation / Deduction       │
              │     - Explicit template argument matching    │
              │     - Implicit template argument deduction   │
              │     - Tri-state deduction (SUCCESS/NO_MATCH) │
              └──────────────────────┬───────────────────────┘
                                     │
                                     ▼
              ┌──────────────────────────────────────────────┐
              │  5. Overload Resolution & Argument Adaptation│
              │     - get_best_matching_function() scoring   │
              │     - Implicit conversions & lvalue checks   │
              │     - Virtual dispatch / direct call wiring  │
              └──────────────────────────────────────────────┘
```

---

## 3. Candidate Collection and Scope Rules

When evaluating `receiver.name(args...)`, candidates are gathered in a unified pool from four primary sources:

### 3.1. Primary Aggregate Candidates
The receiver expression's underlying aggregate type (`st`) is inspected:
- Member functions and static methods declared directly in `st`.
- Inherited methods from base structures, classes, and interfaces (via breadth-first search across the inheritance DAG).

### 3.2. Caller Scope Free and Static Functions (UCS)
The enclosing lexical scope of the call site (`_function_stack.back()` or caller block) is searched for functions named `name`.

### 3.3. `using namespace` Directives
All `using namespace <target>;` directives active in the caller's lexical hierarchy and the current translation unit's root namespace are queried:
- Functions in the target namespace matching `name`.
- Imported functions qualified with the namespace prefix (e.g. `::k::accumulate`).

### 3.4. Module-Level Imported Free Functions
Directly imported functions matching `name` from any imported module (`import k;`).

---

## 4. Scope Isolation and Candidate Filtering

To preserve encapsulation and prevent erroneous overload bindings, candidates undergo strict filtering before overload resolution:

### 4.1. The UCS Isolation Rule
```cpp
auto append_ucs_candidate = [&](const std::shared_ptr<function>& fn) {
    if (!fn) return;
    if (fn->is_member() && !fn->is_static()) return;
    append_unique_candidate(fn);
};
```
- **Rule**: When collecting UCS candidates from caller or outer enclosing scopes, non-static member functions belonging to enclosing classes **must not** be collected as UCS candidates on an unrelated receiver object.
- **Rationale**: If a method `B::foo()` is called inside method `A::bar()`, `A`'s member functions must not accidentally bind to `B` via UCS unless declared `static`.

### 4.2. Const-Receiver Filtering
If `receiver` is a `const` object (e.g., `const Vector<int>&`):
- Non-const member functions are filtered out.
- Free functions taking `const Receiver&` or `Receiver` by value remain valid candidates.

### 4.3. KDI Member Template Export Boundary
In `kdi_exporter`, member template methods declared inside aggregates (e.g., `LinkedList<T>::emplaceBack<Args...>`) are strictly contained within their aggregate descriptor. They are guarded with `_agg_stack.empty()` so that member templates are never erroneously emitted into top-level namespace `template_defs` as free functions.

---

## 5. Template Functions in UCS

Both explicitly parameterized calls (`obj.func<T>(...)`) and deduced calls (`obj.func(...)`) fully participate in Unified Call Syntax.

### 5.1. Receiver Adaptation for Deduction
When deducing template arguments for a candidate function:
- **For member methods**: The expected arguments are `(arg1, arg2, ...)`.
- **For free / static UCS functions**: The receiver type `typeof(receiver)` is prepended as parameter 0: `(receiver_type, arg1, arg2, ...)`.

```cpp
bool is_member_cand = tpl_func->is_member() && !tpl_func->is_static();
std::vector<std::shared_ptr<type>> call_types;
if (!is_member_cand) {
    call_types.push_back(this_expr ? this_expr->get_type() : nullptr);
}
call_types.insert(call_types.end(), expr_arg_types.begin(), expr_arg_types.end());
```

### 5.2. Tri-State Template Argument Matching
Template argument deduction uses a **tri-state** match status (`template_deduction.hpp`):

| Match Status | Meaning | Action in Deduction Engine |
|--------------|---------|----------------------------|
| `SUCCESS`    | Type or pattern matched cleanly; template parameter deduced. | Record deduced argument in binding table. |
| `NO_MATCH`   | Parameter does not depend on target template param, or context is non-deducible (e.g., alias/typedef). | Continue matching other parameters. Allowed if deduced elsewhere or defaulted. |
| `CONFLICT`   | Incompatible types or conflicting deductions across multiple parameters. | SFINAE rejection of the candidate overload. |

This ensures that alias types (e.g. `Predicate<T>` or `UnaryOp<T>`) and non-deducible positions do not cause spurious deduction failures when `T` is unambiguously deduced from another parameter (such as `const Sequence<T>&`).

### 5.3. Static Member Template Instantiation Scope
When a member template is declared static within an aggregate or interface (such as `Sequence<T>::accumulate<R>`), its instantiation via UCS on a concrete derived receiver (e.g., `Vector<int>`) must target the corresponding aggregate in the receiver's inheritance hierarchy rather than the top-level namespace:
- If `tpl_func` belongs to a generic aggregate template (e.g., uninstantiated `Sequence`), the receiver's base hierarchy is traversed to locate the matching instantiated base (e.g., `Sequence<int>`).
- The concrete method is defined directly on that aggregate with identical visibility, constness, aliasing, and compiler-generated flags.
- This ensures proper encapsulation, symbol mangling (`Sequence<int>::accumulate__int`), and type resolution of aggregate-level template parameters (`T = int`).

---

## 6. Overload Selection and Call Generation

Once candidates are gathered, specialized, and instantiated, `get_best_matching_function` performs overload scoring using the following preference hierarchy (lower preference score wins):

| Preference Level | Candidate Category | Description |
|:----------------:|:-------------------|:------------|
| **0** | Non-const instance method | Exact receiver match on non-const object (or const method on const object). |
| **1** | Const instance method / Direct call | Const instance method on non-const receiver; or direct non-member call. |
| **2** | **Receiver Aggregate Static Method (UCS)** | Static member method declared in the receiver's aggregate or anywhere in its base hierarchy. |
| **3** | **Global / Namespace Free Function (UCS)** | Free function or static method from caller scope, imported namespaces, or module imports. |

### 6.1. Prioritization Rationale
Static methods declared within the receiver's aggregate (or base interfaces/classes) are logically bound to the type contract (e.g. `Sequence<T>::accumulate`). They take priority over arbitrary global or imported free functions of the same name.

### 6.2. Argument Adaptation
- For **UCS static member methods and free functions**, the receiver (`this_expr`) is prepended as parameter 0, with implicit conversions and upcasts applied (supporting value, reference `&`, owner `!`, view `?`, and pointer `*` categories).
- For **instance member methods**, `this_expr` is maintained as the object expression in `member_of_object_expression` and annotated with direct or virtual dispatch information.

---

## 7. Diagnostics and Error Handling

| Scenario | Diagnostic Code | Behavior |
|----------|-----------------|----------|
| No matching member or UCS function | `ERR_INVOKE_NO_MATCHING_OVERLOAD` | Reports function name and searched aggregate / scope context. |
| Ambiguous overloads between member and UCS | `ERR_INVOKE_AMBIGUOUS_CALL` | Emits candidates with signatures. |
| Calling non-const member on const receiver | `ERR_CAST_UNSUPPORTED` | Explains const-correctness violation. |
| Template constraint violation | `ERR_TPL_ARG_CONSTRAINT_VIOLATED` | Identifies parameter index and unmet constraint. |
