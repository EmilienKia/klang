# 05. Klang Template Architecture, Instantiation & Argument Deduction

This document describes the technical design and internal implementation of the **template subsystem** in the Klang compiler (`klangc`), covering definition modeling, instantiation mechanics, multi-pass resolution, automated argument deduction, and cross-module KDI serialization.

---

## 1. Overview and Core Design Principles

Klang provides a dual-model template system:

1. **Monomorphized Templates (`template<...>` keyword)**:
   - Evaluated and specialized per distinct combination of concrete template arguments.
   - Supports type parameters (`typename`, `struct`, `class`, `interface`), non-type value parameters (primitive compile-time constants, enum constants, constant expressions), and variadic parameter packs (`typename... Ts`).
   - Generates specialized LLVM declarations and bodies with `linkonce_odr` linkage and COMDAT grouping.
2. **Uniform Synthesis Generics (`generic<...>` keyword)**:
   - Single compilation pass: all type parameters are erased to opaque pointer types (`byte*` / `%ptr`) in LLVM IR.
   - Reuses a single synthesized body across all call sites, eliminating binary bloat.

---

## 2. Template Data Model

```
       ┌────────────────────────┐
       │   aggregate / function │
       └───────────┬────────────┘
                   │ owns
                   ▼
       ┌────────────────────────┐
       │        tpl_info        │
       ├────────────────────────┤
       │ params: vector<param>  │
       │ source_text: string    │
       │ is_generic: bool       │
       │ instantiations: map    │
       └────────────────────────┘
```

### 2.1. Parameter Descriptors (`template_param_descriptor`)
Each parameter in `tpl_info::params` records:
- **`kind`**: `TYPENAME`, `STRUCT`, `CLASS`, `INTERFACE`, or `VALUE`.
- **`name`**: Parameter identifier (`T`, `U`, `N`, `Ts`).
- **`is_pack`**: Boolean flag indicating a parameter pack (`typename... Ts`).
- **`constraint_type`**: Optional base class/interface constraint (`T : BaseClass`).
- **`default_type`**: Optional default type for type parameters.
- **`value_type`**: Declared type for value parameters (`int`, `unsigned long`, `Color`).
- **`default_value`**: Optional evaluated compile-time constant (`k::value_type`).
- **`default_value_expr`**: Un-evaluated AST expression for deferred constexpr evaluation.

### 2.2. Concrete Template Arguments (`template_argument`)
Provided at instantiation sites or deduced from call arguments:
- `type_arg`: Concrete `std::shared_ptr<type>` (e.g. `int`, `String`, `Vector<int>`).
- `value_arg`: Concrete constant `k::value_type` (e.g. `10`, `true`, `Color::RED`).
- `pack_types`: `std::vector<std::shared_ptr<type>>` for pack arguments.

### 2.3. Model Representation of Uninstantiated Templates
When the compiler builds the initial semantic model (`model_builder`):
- Template parameter references inside definitions are created as placeholder `unresolved_type` nodes with `is_template_param_placeholder() == true`.
- Type resolution passes skip uninstantiated template definitions, preserving the original parameter placeholders until concrete instantiation.

---

## 3. Template Instantiation Engine (`template_instantiator`)

Specialization occurs at the **semantic model level**, rather than by re-lexing or AST text substitution:

```
 [Template Definition] ──(Clone + Substitute)──> [Concrete Model Node]
                                                        │
                                                        ▼
                                             [Resolver Mini-Pipeline]
                                             1. symbol_resolver
                                             2. aggregate_type_resolver
                                             3. signature_resolver
                                             4. type_reference_resolver
                                                        │
                                                        ▼
                                             [Ready for Codegen]
```

### 3.1. Substitution Maps
- **`type_substitution_map`**: Maps placeholder type names (`"T"`) to concrete types (`int`).
- **`value_substitution_map`**: Maps value parameter names (`"N"`) to constant expressions / values.
- **`pack_substitution_map`**: Maps pack names (`"Ts"`) to concrete type slices.

### 3.2. Recursive Type Substitution (`substitute_type`)
`substitute_type(type, subst)` traverses the type tree:
- **`unresolved_type`**: Replaces bare placeholders (`T` $\rightarrow$ `int`) or substitutes nested template arguments (`Vector<T>` $\rightarrow$ `Vector<int>`).
- **Indirection wrappers**: Unwraps and reconstructs pointer (`*`), reference (`&`), link (`+`), view (`?`), owner (`!`), drain (`#`), const (`const`), or array (`[]`).
- **Callable types**: Recursively substitutes return types, parameter types, and `throws` exception specifications.
- **Pinned wrappers**: When freshly substituted types lack an owning scope, `type::make_pinned_wrapper` creates strong lifetime ownership to prevent dangling references.

### 3.3. Instantiation Caching and Naming
- **Key generation**: `build_instantiation_key(args)` formats `<arg1,arg2,...>`.
- **Mangled name**: Encoded according to K ABI mangling specification (`I...E` envelope, e.g. `_KFN4main8identityIiET_T_`).
- **Linkage**: Instantiations are marked with `mark_instantiation()`, instructing LLVM emission to apply `linkonce_odr` linkage with matching COMDAT sections for cross-translation-unit deduplication.

---

## 4. Automated Template Argument Deduction

The deduction engine (`template_deduction.cpp`) deduces concrete template arguments from call-site argument expressions for global functions, static member functions, instance methods, and Unified Call Syntax (UCS).

### 4.1. Pattern Matching Algorithm (`deduce_from_types`)

Deduction performs structural pattern matching between the declared parameter type pattern $P$ and the call-site argument type $A$:

```
                                  deduce_from_types(P, A)
                                             │
      ┌──────────────────┬───────────────────┼───────────────────┬──────────────────┐
      ▼                  ▼                   ▼                   ▼                  ▼
[Placeholder T]    [Wrappers]           [Composite]          [Callable]          [Array]
P is bare "T"      P: T*, T&, const T   P: Vector<T>         P: *(T):R           P: T[N]
A decayed → T=A    Recurse subtypes     A: struct_type(Vec)  Recurse ret/params  Recurse T, N=size
```

#### 1. Bare Type Placeholders (`T`)
- Matches any argument type $A$.
- Decays top-level lvalue references and drain qualifiers when the parameter is passed by value.
- **Consistency Enforcement**: If $T$ was previously deduced to $T_{prev}$, requires $T_{prev} == A$ (via `type::are_equal` or `type::are_layout_equal`). Conflicting deductions cause candidate rejection.

#### 2. Indirection and Qualifier Matching
- Matches symmetric addressers: `T*` vs `int*`, `T&` vs `int&`, `T!` vs `MyClass!`.
- **Indirection Compatibility**:
  - `T*` accepts pointer (`*`), link (`+`), view (`?`), and reference (`&`).
  - `T?` accepts view (`?`), pointer (`*`), link (`+`), and reference (`&`).
  - `T&` accepts reference (`&`), link (`+`), and drain (`#`).
  - `const T&` accepts const reference, non-const reference, or rvalue expressions.

#### 3. Composite Template Types (`Vector<T>`, `Map<K, V>`)
- Parameter pattern $P$ is an `unresolved_type` with template arguments.
- Argument $A$ is unwrapped to its underlying `struct_type`.
- Validates that $A$'s aggregate base template matches $P$'s identifier.
- Recursively matches each template argument:
  - Type argument: matches AST type specifier against $A$'s concrete template argument.
  - Value argument: matches identifier against $A$'s concrete constant value.
- Supports arbitrary recursion depth (e.g. `Vector<Vector<T>>` vs `Vector<Vector<int>>`).

#### 4. Callable Types (`*(T):R`, `Class::*(T):R`)
- Unwraps outer references from argument expressions.
- Recursively deduces the return type ($R$).
- Recursively deduces each parameter type ($T_1, T_2, \dots$).
- For member function pointers, matches the owning class type.

#### 5. Arrays and Sized Arrays (`T[N]`)
- Matches element types recursively.
- For sized arrays with value parameters, deduces the array length $N$.

#### 6. Variadic Parameter Packs (`Ts...`)
- A pack parameter at the end of the signature collects all remaining call-site arguments into a `template_argument::make_pack(...)`.
- Top-level references are stripped from individual elements.

### 4.2. Return-Type & Contextual Target-Type Deduction

When a function template has template parameters that do not appear in its parameter list (or are only partially deducible from argument expressions), Klang utilizes **bidirectional contextual type inference**:

```
[ Surrounding Context ] (val x: TargetType =, return, (TargetType)...)
         │
         ▼
[ type_reference_resolver ] ─── Pushes expected TargetType via target_scope
         │
         ▼
[ deduce_template_arguments ]
         │  1. Deduce from arguments (params vs args)
         │  2. If un-deduced params remain: match tpl_return_type vs expected_target_type
         │  3. Reject candidate if deduction conflicts with argument deduction
         │  4. Fill remaining defaulted parameters
         ▼
[ Concrete Instantiation & Overload Selection ]
```

#### 1. Contextual Deduction Algorithm
1. `deduce_template_arguments` first processes all call arguments against declared parameter types.
2. If any type/value parameters remain un-deduced, and an `expected_target_type` is provided by the enclosing resolver context:
   - Top-level references/drains are unwrapped unless the template return type is explicitly a reference.
   - `deduce_from_types(tpl_return_type, clean_target, ...)` recursively matches the declared return pattern against the target type.
   - Deduces bare types (`T`), wrapped pointers/owners (`T*`, `T!`), composites (`Box<T>`, `Pair<T, U>`), and sized arrays (`T[N]`).
3. If deduction from the return type produces a conflict with a parameter already deduced from call arguments, the candidate fails deduction (`match_status::CONFLICT`) and is discarded from the overload set.

#### 2. Supported Contextual Producers
Target types are propagated down the AST during Pass D (`type_reference_resolver`) across:
- **Variable Definitions**: `val x: int = make_default();`
- **Return Statements**: `fun get(): int { return make_default(); }`
- **Assignments**: `x = make_default();`, `obj.field = make_default();`
- **Explicit Casts**: `(int) make_default()`
- **Ternary Expressions**: `cond ? make_default() : fallback`
- **Array Initializers**: `arr : int[2] { make_default(), make_default() };`
- **Designated Struct Initializers**: `Point{ .x = make_default(), .y = make_default() }`
- **Function Arguments**: `consume(make_default())` when `consume(int)` parameter type is known.

### 4.3. Class Template Argument Deduction (CTAD)

Klang supports automated template argument deduction for class/struct constructors (CTAD), allowing instantiation without explicit `<...>` template arguments:

```k
template<typename T, typename U>
struct Pair {
    first  : T;
    second : U;
    Pair(first : T, second : U) : first(first), second(second) {}
}

p1 = Pair(10, 20);            // Deduced as Pair<int, int> (temporary construction)
p2 : Pair = Pair(p1);         // Deduced as Pair<int, int> (implicit copy guide)
p3 : Pair(30, 40);            // Deduced as Pair<int, int> (variable constructor syntax)
p4 : Pair! = new Pair(50, 60);// Deduced as Pair<int, int>! (dynamic allocation)
arr : Pair(1, 2)[5];          // Deduced as Pair<int, int>[5] (uniform array init)
```

#### 1. Deduction Guides & Candidate Generation
During CTAD (`deduce_aggregate_ctad_candidates`), the compiler gathers candidates:
1. **Explicit Constructors**: Every constructor defined on the template aggregate participates as a deduction candidate. Parameter patterns $P$ are matched against call-site argument types $A$ via `deduce_template_arguments`.
2. **Implicit Copy/Move Guide**: `S(const S<Params...>&) -> S<Params...>` is synthesized to ensure constructing from an existing specialization `S<T...>` preserves the specialization rather than deducing from individual fields.
3. **Implicit Default Guide**: When invoked with 0 arguments and all template parameters have defaults, default arguments are populated automatically.

#### 2. CTAD Resolution Pipeline
In `type_reference_resolver::resolve_ctad`:
1. Viable candidates are instantiated and their internal types resolved via `instantiate_template_aggregate`.
2. Candidates are evaluated against argument expressions with `get_best_matching_constructor`.
3. Overload resolution ranks candidates by lowest conversion weight (`cast_weight`), fewest defaulted parameters, and copy guide preference.
4. Ambiguous deductions with equal score but distinct concrete specializations are rejected with `ERR_CTAD_AMBIGUOUS` (0x0189).

---

## 5. Overload Resolution Integration

Template candidate deduction integrates seamlessly with Klang's overload resolution pipeline in `gen_expr_invocation.cpp` and `resolvers_type_ref.cpp`:

```
                             [Candidate Function Overloads]
                                             │
                       ┌─────────────────────┴─────────────────────┐
                       ▼                                           ▼
             [Non-Template Overloads]                    [Template Candidates]
                       │                                           │
                       │                                 deduce_template_arguments()
                       │                                           │
                       │                             ┌─────────────┴─────────────┐
                       │                             ▼ (Success)                 ▼ (Failure)
                       │                      Instantiate Candidate         Silent Rejection
                       │                             │                          (SFINAE)
                       └─────────────────────┬───────┘
                                             ▼
                                [get_best_matching_function]
                                1. Lowest Cast Weight (score)
                                2. Fewest Default Parameters
                                3. Member > Free > Unified
                                4. Non-Template > Template (Tie-breaker)
                                             │
                                             ▼
                                [Selected Best Function]
```

### 5.1. SFINAE (Substitution Failure Is Not An Error)
When template argument deduction or constraint validation fails for a candidate function template:
- The candidate is silently discarded from the overload set.
- Compilation fails only if no viable candidates remain across the entire set.

### 5.2. Non-Template Preference Rule
In `type_reference_resolver::get_best_matching_function`:
- Candidates are ranked by conversion distance (`cast_weight`).
- If an exact match exists in a template specialization (e.g. `f<int>(42)` with `CAST_NONE`) and a non-template requires promotion (e.g. `f(long)` with `CAST_PROMOTION`), the exact template specialization wins.
- If a non-template function and a deduced template specialization both provide an **exact match** (`CAST_NONE`), the non-template function is preferred as a tie-breaker.

### 5.3. Dependent Return Type Materialization
When a function template has a return type dependent on deduced arguments:
- `substitute_type` computes the concrete return type (`T` $\rightarrow$ `int`, `Vector<T>` $\rightarrow$ `Vector<int>`).
- If the return type is a composite or wrapped template type (`Vector<int>&`, `Pair<int, float>`), `instantiate_wrapped_return_type` recursively materializes and resolves the underlying `struct_type`.
- The resolved concrete type is assigned directly to `function_invocation_expression::set_type(...)`.

---

## 6. Cross-Module Serialization & KDI

Template declarations and concrete instantiations are preserved across compilation boundaries using **KDI (K Description Interface)**:

1. **Source Preservation**:
   - Template bodies are preserved as raw K source text in `kdi_template_def::source`.
   - `kdi_importer::materialise_template_def` re-parses and injects the template definition into the consumer module's compilation unit upon `import`.
2. **Origin Tracking**:
   - `kdi_template_def::origin_module` records the originating module (e.g. `k` for stdlib templates).
   - Prevents duplicate symbol synthesis and ensures identical COMDAT symbol mangling across separate shared libraries.
3. **Instantiated Aggregates**:
   - Pre-instantiated structs in compiled libraries export their complete LLVM layout and RTTI symbols via `kdi_aggregate`, enabling zero-cost layout reuse in importing modules.
