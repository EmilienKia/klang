# K Language — Symbol Resolution

_Reference document for compiler engineers, IDE implementors, and debugger authors._

---

## Table of Contents

1. [Overview](#1-overview)
2. [Core Concepts](#2-core-concepts)
   - 2.1 [The `k::name` type — Absolute vs. Relative names](#21-the-kname-type--absolute-vs-relative-names)
   - 2.2 [Scope hierarchy and holder interfaces](#22-scope-hierarchy-and-holder-interfaces)
   - 2.3 [Compiler phases that perform resolution](#23-compiler-phases-that-perform-resolution)
3. [Phase A — Symbol Resolver](#3-phase-a--symbol-resolver)
   - 3.1 [Entry point](#31-entry-point)
   - 3.2 [Pre-passes performed before resolution](#32-pre-passes-performed-before-resolution)
4. [Core Resolution Algorithm — `resolve_symbol()`](#4-core-resolution-algorithm--resolve_symbol)
   - 4.1 [Step 1 — `this` keyword](#41-step-1--this-keyword)
   - 4.2 [Step 2 — Root-prefixed (absolute) names](#42-step-2--root-prefixed-absolute-names)
   - 4.3 [Step 3 — Qualified names (multi-part)](#43-step-3--qualified-names-multi-part)
   - 4.4 [Step 4 — Simple (unqualified) names](#44-step-4--simple-unqualified-names)
   - 4.5 [Step 5 — Using directives](#45-step-5--using-directives)
   - 4.6 [Step 6 — Recurse to parent scope](#46-step-6--recurse-to-parent-scope)
   - 4.7 [Step 7 — Imported module fallback (at root)](#47-step-7--imported-module-fallback-at-root)
5. [Downward Resolution — `resolve_qualified_from()`](#5-downward-resolution--resolve_qualified_from)
6. [Absolute Resolution — `resolve_symbol_from_root()`](#6-absolute-resolution--resolve_symbol_from_root)
7. [Resolution via Using Directives — `resolve_via_using()`](#7-resolution-via-using-directives--resolve_via_using)
   - 7.1 [Anonymous namespace using](#71-anonymous-namespace-using)
   - 7.2 [Aliased namespace using](#72-aliased-namespace-using)
   - 7.3 [Specific element using (with or without alias)](#73-specific-element-using-with-or-without-alias)
   - 7.4 [Ambiguity detection](#74-ambiguity-detection)
8. [Scope-Chain Lookup Utilities — `scope_lookup`](#8-scope-chain-lookup-utilities--scope_lookup)
   - 8.1 [`lookup_variable()`](#81-lookup_variable)
   - 8.2 [`lookup_function()` and `lookup_functions()`](#82-lookup_function-and-lookup_functions)
   - 8.3 [`lookup_structure()`](#83-lookup_structure)
   - 8.4 [`lookup_enumeration()`](#84-lookup_enumeration)
   - 8.5 [`lookup_union()`](#85-lookup_union)
9. [Special Symbol Forms](#9-special-symbol-forms)
   - 9.1 [Enum entries — `MyEnum::entryName`](#91-enum-entries--myenumentryname)
   - 9.2 [Union discriminator entries — `MyUnion::Kind::entryName`](#92-union-discriminator-entries--myunionkindentryname)
   - 9.3 [Annotation RTTI — `MyAnnotation::annotation`](#93-annotation-rtti--myannotationannotation)
10. [Member Resolution — `expr.member`](#10-member-resolution--exprmember)
    - 10.1 [Field access on a struct reference](#101-field-access-on-a-struct-reference)
    - 10.2 [Method lookup](#102-method-lookup)
    - 10.3 [Unified call syntax](#103-unified-call-syntax)
    - 10.4 [Member access through inheritance](#104-member-access-through-inheritance)
    - 10.5 [Member access on enum-typed expressions](#105-member-access-on-enum-typed-expressions)
11. [Visibility Enforcement](#11-visibility-enforcement)
    - 11.1 [Member variable visibility](#111-member-variable-visibility)
    - 11.2 [Global variable visibility](#112-global-variable-visibility)
    - 11.3 [Friendship](#113-friendship)
12. [Redirect Chain Resolution](#12-redirect-chain-resolution)
13. [Type Reference Resolver — Phase D](#13-type-reference-resolver--phase-d)
    - 13.1 [Variable type assignment](#131-variable-type-assignment)
    - 13.2 [Function type assignment](#132-function-type-assignment)
    - 13.3 [Enum entry type assignment](#133-enum-entry-type-assignment)
14. [Error Codes Reference](#14-error-codes-reference)
15. [Complete Examples](#15-complete-examples)

---

## 1. Overview

Symbol resolution is the process of **binding an identifier** (a name written in
source code) to a concrete declaration in the model — a variable, a function, an
aggregate type, an enumeration entry, or a special RTTI descriptor.

The K compiler performs symbol resolution in **two passes**:

| Pass | Class | Pipeline stage |
|---|---|---|
| **Pass A** | `k::model::gen::symbol_resolver` | After model building, before type resolution |
| **Pass D** | `k::model::gen::type_reference_resolver` | After aggregate type resolution; finalises types on resolved symbols |

Pass A is the central pass described in this document. Pass D builds on top of it to
assign LLVM-ready types to every resolved symbol expression.

After both passes every `symbol_expression` node in the AST carries a fully-resolved
target (variable definition, function, enum entry, or RTTI descriptor) and a
resolved K type.

---

## 2. Core Concepts

### 2.1 The `k::name` type — Absolute vs. Relative names

Every name in the K source is represented by a `k::name` value, defined in
`klang/src/common/common.hpp`:

```cpp
class name {
    bool _root_prefix;                // true  → absolute name (starts with ::)
    std::vector<std::string> _identifiers; // ordered list of name components
};
```

**Relative name** (`_root_prefix == false`):  
The name is resolved starting from the current scope and climbing toward the root.

```k
// Inside module my::module:
foo()       // relative, resolves to the first 'foo' found climbing from here
ns::foo()   // relative qualified, starts lookup at the ns child of the current scope
```

**Absolute name** (`_root_prefix == true`):  
The name is resolved starting from the root namespace of the compilation unit.
The `::` prefix is written in source to force this.

```k
::ns::foo()           // absolute — skips any local 'ns' definitions
::my::module::foo()   // absolute with explicit module path
```

**Useful predicates and operations on `k::name`**:

| Method | Meaning |
|---|---|
| `has_root_prefix()` | `true` for names starting with `::` |
| `size()` | number of components |
| `front()` / `back()` | first / last component |
| `without_front()` | name without its first component |
| `without_back()` | name without its last component |
| `with_back(s)` | new name with additional component appended |
| `without_root_prefix()` | same name with `_root_prefix` cleared |
| `with_root_prefix()` | same name with `_root_prefix` set |
| `to_string()` | `"a::b::c"` (no leading `::` for relative names) |

### 2.2 Scope hierarchy and holder interfaces

The model is a tree of `k::model::element` nodes linked by a `_parent` pointer.
Several **holder mixin interfaces** mark elements as scopes that can contain
particular kinds of declarations:

| Mixin | Elements that implement it | What it holds |
|---|---|---|
| `variable_holder` | `ns`, `aggregate`, `block`, `for_statement` | Local variables, global variables, member variables |
| `function_holder` | `ns`, `aggregate` | Free functions, member methods |
| `aggregate_holder` | `ns`, `aggregate` | Nested structs, classes, interfaces, annotation types |
| `enum_holder` | `ns`, `aggregate` | Enumeration type definitions |
| `union_holder` | `ns`, `aggregate` | Discriminated union type definitions |
| `using_holder` | `ns`, `aggregate`, `block`, `for_statement` | `using` directives |
| `friend_holder` | `aggregate` | `friend` directives |

The scope chain is formed by climbing `element::parent<element>()` starting from any
node in the tree, up to the root namespace (`unit::get_root_namespace()`), which has no
parent.

**Key traversal helpers on `element`**:

| Method | Meaning |
|---|---|
| `parent<T>()` | Direct parent cast to T, or null |
| `ancestor<T>()` | First ancestor that is a T, or null |
| `shared_as<T>()` | `shared_from_this()` cast to T |

### 2.3 Compiler phases that perform resolution

```
Phase A  (symbol_resolver::resolve())
   └─ visit_unit
       ├─ Pre-pass 0a: implicit Object inheritance injection
       ├─ Pre-pass 0b: implicit Annotation inheritance injection
       ├─ Pre-pass 1:  base-name resolution + virtual-base detection
       ├─ visit_namespace (recursive)
       │   └─ for each child:
       │       ├─ visit_aggregate / visit_klass / visit_structure …
       │       ├─ visit_function
       │       │   └─ visit all statements / expressions
       │       │       └─ visit_symbol_expression  ← main resolution
       │       └─ visit_global_variable_definition
       └─ resolve_redirect_chains()

Phase D  (type_reference_resolver)
   └─ visit_symbol_expression  ← assigns K types to targets resolved in Phase A
```

---

## 3. Phase A — Symbol Resolver

### 3.1 Entry point

```cpp
// klang/src/gen/resolvers_symbol.cpp
void symbol_resolver::resolve() {
    visit_unit(_unit);
    resolve_redirect_chains(_unit);
}
```

`visit_unit` is invoked once on the compilation unit. It drives the recursive
visitor descent through the entire model tree.  When `visit_symbol_expression` is
reached for each `symbol_expression` node, the core resolution algorithm is invoked.

The resolver maintains a **function stack** (`_function_stack`) — a
`std::vector<std::shared_ptr<function>>` whose last element is always the
innermost function currently being visited. This stack is used for visibility
access-site checking.

### 3.2 Pre-passes performed before resolution

Before visiting the namespace tree the unit-level visitor performs three pre-passes:

1. **Implicit Object inheritance** (`Pre-pass 0a`): every `klass` that has no
   declared base classes (and is not `::k::Object` itself) automatically acquires
   `::k::Object` as a public base. This injection only occurs when `k::Object` is
   reachable (the current module is `k`, or `import k;` is present).

2. **Implicit Annotation inheritance** (`Pre-pass 0b`): every `annotation_type`
   that has no declared base classes (and is not `::k::Annotation` itself)
   automatically acquires `::k::Annotation` as a public base.

3. **Base-name pre-resolution and virtual-base detection** (`Pre-pass 1`): a
   depth-first walk resolves all non-template base names in `base_spec::base`
   pointers so that `klass::compute_virtual_bases()` can correctly identify
   diamond patterns before membership injection begins.

---

## 4. Core Resolution Algorithm — `resolve_symbol()`

```cpp
// Signature
std::variant<std::monostate,
             std::shared_ptr<variable_definition>,
             std::shared_ptr<function>>
symbol_resolver::resolve_symbol(const element& elem, const name& name);
```

`elem` is the **current scope node** where resolution starts; `name` is the name
to resolve. The function returns one of three alternatives:

- `std::monostate{}` — not found
- `std::shared_ptr<variable_definition>` — resolved to a variable/parameter
- `std::shared_ptr<function>` — resolved to a function

The algorithm is a **recursive upward walk with embedded downward descent for
qualified names**. Below is the complete step-by-step description.

---

### 4.1 Step 1 — `this` keyword

```
if name == "this" (simple, size == 1):
    walk ancestor<function> chain:
        if function is_member() and has_this_parameter():
            return this_parameter
    error: 'this' outside a non-static member function
```

`this` is the only magic keyword resolved before any scope traversal. It finds
the `this` parameter of the nearest enclosing non-static member function.

```k
// Example
struct Counter {
    value: int;
    increment() {
        this.value = this.value + 1;  // 'this' → parameter of increment()
    }
}
```

---

### 4.2 Step 2 — Root-prefixed (absolute) names

```
if name.has_root_prefix():
    return resolve_symbol_from_root(name.without_root_prefix())
    (see §6 for the full algorithm)
```

An absolute name (`::...`) bypasses the current scope and goes straight to the root
namespace of the unit. See [§6](#6-absolute-resolution--resolve_symbol_from_root)
for the full algorithm.

```k
// Example — inside namespace a::b, force resolution at root
foo := ::a::b::helper();   // absolute lookup: ::a::b::helper
bar := ::globalFunc();     // absolute: free function at module root
```

---

### 4.3 Step 3 — Qualified names (multi-part)

A qualified name has `size() > 1` and no root prefix. It requires **descending into
a named child scope** before continuing resolution. Checked in order:

1. **Aggregate (struct/class)**: if `elem` is an `aggregate_holder` and its first
   component matches a nested aggregate, recurse into that aggregate.

   ```
   if elem is aggregate_holder:
       agg = elem.get_aggregate(name.front())
       if agg:
           result = resolve_symbol(*agg, name.without_front())
           if found: return result
   ```

2. **Enumeration** (`size == 2`): enum entry lookup is *not* done here for
   variable/function results. Enum entries are handled as a special case after
   the main resolve call fails (see [§9.1](#91-enum-entries--myenumentryname)).

3. **Namespace**: if `elem` is an `ns`, try stepping into the matching child
   namespace.

   ```
   if elem is ns:
       child = elem.get_child_namespace(name.front())
       if child:
           result = resolve_symbol(*child, name.without_front())
           if found: return result
   ```

After these three checks, if still not found, fall through to Step 5 (using
directives) and Step 6 (parent climbing).

```k
// Example — qualified lookup
module my;
namespace util {
    helper() : int { return 42; }
}
main() : int {
    return util::helper();  // qualified: descend into util then find helper
}
```

---

### 4.4 Step 4 — Simple (unqualified) names

A simple name has `size() == 1` and no root prefix. Checked in order:

1. **Variable holder**: if `elem` is a `variable_holder`, look up the name in its
   variable map.

   ```
   if elem is variable_holder:
       var = elem.get_variable(name)
       if var: return var
   ```

   Covers: local variables in a `block`, member variables in an `aggregate`,
   global variables in an `ns`.

2. **Function holder**: if `elem` is a `function_holder`, look up the name.

   ```
   if elem is function_holder:
       func = elem.get_function(name.to_string())
       if func: return func
   ```

   Covers: free functions in `ns`, member methods in aggregates.
   `get_function()` returns the **first** overload; for overload resolution see
   [§8.2](#82-lookup_function-and-lookup_functions).

3. **Inherited members (BFS)**: if `elem` is an `aggregate`, search base classes in
   breadth-first order.

   ```
   if elem is aggregate:
       queue = all direct bases of elem
       while queue not empty:
           base = queue.pop_front()
           if base.get_variable(name): return it
           if base.get_function(name): return it
           queue.push_back(all bases of base)
   ```

   This allows member access inherited from base classes.

4. **Function parameters** (workaround): if `elem` is a `block` with a direct
   enclosing function, look up the name in that function's parameter list.

   ```
   if elem is block:
       func = block.get_direct_function()
       if func:
           param = func.get_parameter(name)
           if param: return param
   ```

   > _Note_: This is a workaround — in a future version parameters will be held as
   > a proper `variable_holder` inside the function node.

```k
// Example
foo(x: int) : int {
    y: int = x + 1;   // block lookup finds 'y' (local),
                       // parameter lookup finds 'x'
    return y;
}
```

---

### 4.5 Step 5 — Using directives

After all direct members of `elem` are tried, the resolver checks the `using`
directives attached to the current scope element. This happens **before** climbing
to the parent. See [§7](#7-resolution-via-using-directives--resolve_via_using) for
the full algorithm.

```
using_result = resolve_via_using(elem, name)
if using_result is not monostate: return using_result
```

---

### 4.6 Step 6 — Recurse to parent scope

If the name was not found at the current element, the resolver climbs one level up
the scope chain and retries from the top:

```
if elem.parent() exists:
    return resolve_symbol(*elem.parent(), name)
```

The recursion continues up through `block → function → aggregate → ns → ...` until
the root namespace is reached (its parent is null).

This is the **upward traversal** that gives inner scopes priority over outer ones.
Names defined in inner scopes shadow names of the same identifier in outer scopes.

```k
// Example — shadowing
x: int = 10;           // global x

foo() : int {
    x: int = 20;       // local x shadows global x
    return x;          // resolves to local x (= 20)
}

bar() : int {
    return x;          // no local x → climbs to global x (= 10)
}
```

---

### 4.7 Step 7 — Imported module fallback (at root)

When the root namespace has been reached and the name is still not found, the
resolver attempts resolution through **KDI-imported modules** (external libraries):

```
// Attempted at root level (parent is null):

1. _unit.find_imported_function(name)          → imported function
2. _unit.find_imported_variable(name)          → imported variable
3. if name.size() >= 2:
       agg_name = name.without_back()
       func_name = name.back()
       imp_agg = _unit.get_or_create_imported_aggregate(agg_name, _context)
       if imp_agg:
           func = imp_agg.get_function(func_name)  → imported method/static
```

This fallback is also attempted symmetrically inside
`resolve_symbol_from_root()` (steps 3 and 4 of that function).

---

## 5. Downward Resolution — `resolve_qualified_from()`

```cpp
static std::variant<std::monostate,
                    std::shared_ptr<variable_definition>,
                    std::shared_ptr<function>>
symbol_resolver::resolve_qualified_from(const element& elem, const name& name);
```

Pure **downward descent** from `elem`; does **not** climb to parents. Used for:

- The recursive descent steps in `resolve_symbol()` (qualified name handling).
- Resolution of `using` directive targets.
- Resolution from the root namespace in `resolve_symbol_from_root()`.

**Algorithm**:

```
if name.size() == 1:
    if elem is variable_holder: look up in variable map
    if elem is function_holder: look up in function list
    return result (or monostate)

if name.size() > 1:
    first = name.front()
    rest  = name.without_front()

    // Try child namespace
    if elem is ns:
        child = elem.get_child_namespace(first)
        if child: return resolve_qualified_from(*child, rest)

    // Try aggregate
    if elem is aggregate_holder:
        agg = elem.get_aggregate(first)
        if agg: return resolve_qualified_from(*agg, rest)

    return monostate
```

This is a strict downward search — it never climbs. It is safe to use when the
starting element has already been precisely identified (e.g., after a `::` prefix
routes to the root, or after a namespace/aggregate has been positively identified).

---

## 6. Absolute Resolution — `resolve_symbol_from_root()`

Called when the source name has a `::` prefix. The algorithm:

```
root_ns = _unit.get_root_namespace()

Step 1: Module-prefix shorthand
    unit_name = _unit.get_unit_name()
    if name.front() == unit_name.back():
        rest = name.without_front()
        result = resolve_qualified_from(*root_ns, rest)
        if found: return result
        // fall through (name.front() may coincidentally match a child ns)

Step 2: Direct resolve from root
    result = resolve_qualified_from(*root_ns, name)
    if found: return result

Step 3: Imported function or variable
    if _unit.find_imported_function(name): return it
    if _unit.find_imported_variable(name): return it

Step 4: Imported aggregate method
    if name.size() >= 2:
        agg_name = name.without_back()
        func_name = name.back()
        imp_agg = _unit.get_or_create_imported_aggregate(agg_name)
        if imp_agg:
            func = imp_agg.get_function(func_name)
            if found: return func
```

**Step 1 rationale**: In K the module name is the root namespace. Writing
`::my::module::foo` and `::foo` (when inside module `my::module`) are both valid
ways to address the same function `foo` defined at the top of the module. Step 1
handles the explicit module-prefix form by entering the root namespace through its
last name component and resolving the rest.

```k
module the::test;

greet() : void { /* ... */ }

main() : void {
    ::the::test::greet();  // Step 1 matches "test" == unit_name.back(), then descends
    ::greet();             // Step 2 resolves directly from root
}
```

---

## 7. Resolution via Using Directives — `resolve_via_using()`

A `using` directive at a scope level injects names from another scope, making them
visible as if declared locally. Resolution through `using` directives is checked
**after** direct members but **before** climbing to the parent.

Three kinds of `using` directives are recognised:

### 7.1 Anonymous namespace using

```k
using namespace k::math;

result := abs(-5);  // resolved as k::math::abs without prefix
```

All members of the target namespace are injected into the current scope. The
resolver calls `resolve_qualified_from(target_element, name)` to search within the
target.

### 7.2 Aliased namespace using

```k
using M = namespace k::math;

result := M::abs(-5);  // M acts as an alias for k::math
```

The first component of the looked-up name must match the alias. The resolver strips
that first component and then calls `resolve_qualified_from(target_element, rest)`.
When the target namespace is imported (lives in a KDI), fallback tries
`find_imported_function/variable` with the fully-qualified name.

### 7.3 Specific element using (with or without alias)

```k
using k::math::abs;         // inject a single function
using myAbs = k::math::abs; // inject with alias
```

The resolver checks if `name.front()` matches the alias (or the real last
component of the target). If it matches and `name.size() == 1`, it resolves
directly the target symbol. If `name` has more components (the target is a
namespace/aggregate and further descent is needed), the resolver navigates into
the target.

### 7.4 Ambiguity detection

If two different `using` directives both produce a candidate for the same name,
the first match wins. A TODO in the code notes that this should eventually produce
a proper ambiguity error. For tools and IDEs: report the first match and consider
all additional matches as candidates for an ambiguity warning.

---

## 8. Scope-Chain Lookup Utilities — `scope_lookup`

`k::model::gen::scope_lookup` (in `resolvers_scope_lookup.hpp/cpp`) is a
**static utility class** with convenience wrappers that walk the scope chain
starting from a given element.

Unlike `resolve_symbol()` these functions are **type-specific**: each searches only
one kind of declaration.

### 8.1 `lookup_variable()`

```cpp
static std::shared_ptr<variable_definition>
scope_lookup::lookup_variable(std::shared_ptr<element> elem, const std::string& name);
```

Climbs the parent chain. At each element:
1. If the element is a `variable_holder`, look up in its variable map.
2. If the element is a `block` with a direct enclosing function, look up in that
   function's parameter list.

```k
foo(a: int) : int {
    b: int = 2;
    {
        // inner block
        c: int = scope_lookup::lookup_variable here starts on the inner block:
        //  inner block → finds 'c'
        //  inner block (variable_holder) → not found
        //  inner block (block, direct func = foo) → finds 'a' (parameter)
        //  outer block (variable_holder) → finds 'b'
        //  function → ...
        return a + b + c;
    }
}
```

### 8.2 `lookup_function()` and `lookup_functions()`

```cpp
static std::shared_ptr<function>
scope_lookup::lookup_function(std::shared_ptr<element> elem, const std::string& name);

static std::vector<std::shared_ptr<function>>
scope_lookup::lookup_functions(std::shared_ptr<element> elem, const std::string& name);
```

- `lookup_function` returns the **first** overload found (stops at the first
  `function_holder` scope that has at least one match).
- `lookup_functions` returns **all** overloads from **all** enclosing scopes,
  collecting from every `function_holder` element up the chain.

`lookup_functions` is used for overload resolution: the full candidate set is
assembled before the best-match selection phase.

### 8.3 `lookup_structure()`

```cpp
static std::shared_ptr<aggregate>
scope_lookup::lookup_structure(std::shared_ptr<element> elem, const std::string& name);
```

Climbs the parent chain. At each `aggregate_holder` element, looks up by name.
Used for base-class resolution and type-name resolution.

### 8.4 `lookup_enumeration()`

```cpp
static std::shared_ptr<enumeration>
scope_lookup::lookup_enumeration(std::shared_ptr<element> elem, const std::string& name);
```

Climbs the parent chain. At each `enum_holder` element, looks up by name.

### 8.5 `lookup_union()`

```cpp
static std::shared_ptr<union_type_def>
scope_lookup::lookup_union(std::shared_ptr<element> elem, const std::string& name);
```

For **simple names**: climbs the parent chain, looking up in each `union_holder`.

For **qualified names** (containing `::`): the name is split on `::` and the root
namespace is used as the starting point. The algorithm walks down the namespace
chain for all components except the last, then looks for the union in the final
namespace's `union_holder`.

---

## 9. Special Symbol Forms

Some expression forms look syntactically like qualified name references but are
resolved in special ways that do not fit the standard variable/function lookup.

### 9.1 Enum entries — `MyEnum::entryName`

After the standard `resolve_symbol()` fails to find a variable or function, the
resolver checks whether the name has exactly **2 components** and the first matches
an `enumeration` in scope.

**Algorithm**:

```
if name.size() == 2 and not root-prefixed:
    entry_name = name.back()

    for each element up the scope chain:
        if element is enum_holder:
            if enum = element.get_enum(name.front()):
                found_enum = enum
                break

    if found_enum not found:         // try imported enum
        enum_parts = name.without_back()
        found_enum = _unit.get_or_create_imported_enum(enum_parts)

    if found_enum:
        entry = found_enum.get_entry_by_name(entry_name)
        if entry found:
            symbol.set_target(enum_entry_target{ found_enum, entry_index })
        else:
            error: "Enum '...' has no entry named '...'"
```

```k
enum Color { Red; Green; Blue; }

getColor() : Color {
    return Color::Blue;  // resolved as enum_entry_target for Color, entry 2
}
```

The resulting type assigned by Pass D is the `enum_type` of that enumeration (not
a reference — it is an r-value constant).

### 9.2 Union discriminator entries — `MyUnion::Kind::entryName`

Discriminated unions synthesize an internal enumeration named `Kind` that lists all
alternative names. Resolution uses a 3-component name.

```
if name.size() == 3 and name[1] == "Kind":
    union_name = name.front()
    entry_name  = name.back()

    for each element up the scope chain:
        if element is union_holder:
            if union = element.get_union(union_name):
                kind_enum = union.get_kind_enum()
                set_target(enum_entry_target{ kind_enum, entry_index })
                break
```

```k
union Value {
    i: int;
    d: double;
    s: String;
}

checkKind(v: Value&) : bool {
    return v.kind == Value::Kind::i;  // 3-part: Value → Kind → i
}
```

### 9.3 Annotation RTTI — `MyAnnotation::annotation`

The special trailing component `annotation` resolves to the runtime type descriptor
of an annotation type.

```
if name.size() >= 2 and name.back() == "annotation":
    ann_name = name.without_back()

    // Try local scope first (single-part name)
    if ann_name.size() == 1:
        agg = scope_lookup::lookup_structure(symbol, ann_name.front())
        if agg and agg.is_annotation(): found_ann = agg

    // Try imported annotation types
    if not found:
        imp_agg = _unit.get_or_create_imported_aggregate(ann_name)
        if imp_agg and imp_agg.is_annotation(): found_ann = imp_agg

    if found_ann:
        symbol.set_target(annotation_type_rtti_target{ found_ann })
```

```k
@interface Deprecated {}

getAnnotationType() : AnnotationType& {
    return Deprecated::annotation;  // resolves to RTTI descriptor of Deprecated
}
```

The resulting type (assigned by Pass D) is `const k::AnnotationType&`.

---

## 10. Member Resolution — `expr.member`

Member access (`expr.member`) is handled by `member_of_object_expression` (`.`
operator) and `member_of_pointer_expression` (`->` operator). The sub-expression
(left-hand side) is resolved first by `symbol_resolver`; the member symbol is
resolved by `type_reference_resolver` in Pass D, once the LHS type is known.

### 10.1 Field access on a struct reference

```
lhs_type = sub_expr.get_type()     // e.g. ref<Point>
subtype  = lhs_type.get_subtype()  // e.g. Point (struct_type)
bare     = remove_const(subtype)

if field = struct_type.get_member(member_name):
    expr.set_type(field->field_type->get_reference())
    record field index for codegen
```

The type of a field access is always a **reference** to the field type, allowing
it to be used on either side of an assignment.

```k
struct Point { x: int; y: int; }
p: Point;
p.x = 3;      // member access → ref<int> → assignable
val := p.x;   // member access → ref<int> → loaded by codegen
```

### 10.2 Method lookup

When the member name resolves to a function (not a field), the expression produces a
**method reference**. Actual binding of the call happens in
`visit_function_invocation_expression` which is responsible for overload selection.

```
if method = struct_type.find_method(member_name):
    // type is left unset at member level;
    // function_invocation_expression binds the call
```

### 10.3 Unified call syntax

If the member name was not found as a direct or inherited method, the type resolver
looks for a **free function** whose first parameter is `ref<Struct>` (or
`const ref<Struct>`). This is the K unified call syntax — free functions can be
called as if they were methods.

```k
struct Vec2 { x: float; y: float; }

length(v: Vec2&) : float { return sqrt(v.x*v.x + v.y*v.y); }

main() {
    v: Vec2 = {1.0, 0.0};
    l := v.length();   // unified call syntax → resolved to free function length(v)
}
```

### 10.4 Member access through inheritance

When a member is not found on the direct struct type, the resolver searches the
inheritance hierarchy using a BFS traversal of base classes. The `base_spec::base`
pointers (resolved during `aggregate_type_resolver`, Pass B) are followed.

At the LLVM codegen level, inherited member access navigates the `__base_Name__`
sub-object field chain injected into the struct layout by the model materializer.

### 10.5 Member access on enum-typed expressions

When the LHS of a `.` access has an `enum_type`, the resolver treats the member
name as an **enum entry** and resolves it directly, without performing struct member
lookup.

---

## 11. Visibility Enforcement

Visibility is enforced during Phase A inside `check_variable_visibility()` and
implicitly by the resolver for functions (visibility is checked at call-site by
`type_reference_resolver`).

### 11.1 Member variable visibility

```
vis = member_variable.get_visibility()
owner_agg = member_variable.parent<aggregate>()

if vis == PUBLIC: allowed

if is_struct_member_accessible(vis, owner_agg, function_stack): allowed

if vis == PROTECTED and is_friend_of(owner_agg, function_stack): allowed

otherwise: error ERR_AGGREGATE_VISIBILITY_DENIED
```

`is_struct_member_accessible` uses the **function stack** (not the static scope
chain) to determine the access site:

- **PRIVATE**: allowed only if the access site is a direct member function of
  `owner_agg` (or a struct nested inside it via `get_enclosing_aggregate()`).
- **PROTECTED**: allowed if the access site is a member function of `owner_agg` or
  of any class that derives from `owner_agg` (`is_derived_from()`).

### 11.2 Global variable visibility

```
vis = global_variable.get_visibility()
owner_ns = enclosing_namespace(global_variable)

if vis == PUBLIC: allowed

if vis == PROTECTED (= module-level):
    owner_root = root_namespace(owner_ns)
    if access_site is in the same root namespace: allowed
    otherwise: error ERR_VISIBILITY_ACCESS_DENIED ("same module only")

if vis == PRIVATE (= namespace-level):
    if access_site is in owner_ns or a descendant namespace: allowed
    otherwise: error ERR_VISIBILITY_ACCESS_DENIED ("private to namespace")
```

### 11.3 Friendship

`friend` declarations in an aggregate grant access to protected members from an
external function or all member functions of an external aggregate.

```
for each friend_directive in owner_agg:
    resolve target_name from root namespace (ns → aggregate → function chain)

    if target is aggregate:
        if current_fn.get_owner() == target_agg: THIS FUNCTION IS A FRIEND → allowed
    if target is function:
        if current_fn == target_fn: allow
```

Friendship is **not transitive** and **does not propagate through inheritance**.

```k
class Secret {
    private:
    value: int;
    friend class Inspector;
}

class Inspector {
    read(s: Secret&) : int {
        return s.value;  // allowed: Inspector is a friend of Secret
    }
}
```

---

## 12. Redirect Chain Resolution

Functions declared with `-> default` or `-> delete` syntax (and user-defined
redirects) carry a redirect target. After Phase A's main visitor completes, the
resolver follows **transitive redirect chains**:

```
for each function fn in all functions:
    if fn.is_redirected() and fn.has_redirect_target():
        fn.set_redirect_target(resolve_redirect_chain(fn))

resolve_redirect_chain(fn, visited):
    if fn in visited: error ERR_REDIRECT_CHAIN_CYCLE
    add fn to visited
    target = fn.get_redirect_target()
    if target.is_redirected():
        return resolve_redirect_chain(target, visited)   // follow chain
    if target.is_abstract(): error (cannot redirect to abstract)
    if target.is_deleted():  error (cannot redirect to deleted)
    return target
```

After this step every redirected function points **directly** to the final concrete
implementation (no chain walking needed at codegen time).

---

## 13. Type Reference Resolver — Phase D

After Phase A resolves all names to their declarations, Phase D
(`type_reference_resolver`) assigns K types to every resolved `symbol_expression`.

### 13.1 Variable type assignment

```
var_type = var_def.get_type()

if var_def.is_const() and not type::is_const(var_type):
    if type is indirection (link/pointer/view/ref):
        apply const to the subtype of the indirection
    else:
        var_type = var_type.get_const()

if type::is_reference(var_type) or type::is_drain(var_type):
    // Variable IS already an indirection: symbol type = the reference/drain itself
    symbol.set_type(var_type)
else:
    // Non-indirection variable: symbol type = reference<var_type>
    symbol.set_type(var_type.get_reference())
```

The result is that, from the expression perspective, every variable symbol
yields a **reference** (the address of the variable). Codegen loads the actual
value when an r-value is needed.

### 13.2 Function type assignment

When a function name is used without a call (as a function pointer):

```
fn_ref_type = function_reference_type_builder
    .ref_kind(link)
    .return_type(func.get_return_type())
    .parameter_types(...)
    .member_of(struct if non-static member)
    .build()

symbol.set_type(fn_ref_type.get_reference())
```

The type of a function symbol is `ref<fn_ref_type>` (a non-null reference to a
function reference type). This can be stored in variables of type `*`, `+` or `&`
to the corresponding function reference type.

### 13.3 Enum entry type assignment

```
en = target.enum_def
if en.get_enum_type() == null:
    uint_type = from_type(UNSIGNED_INT)
    en.set_underlying_type(uint_type)
    et = new enum_type(en, uint_type)
    en.set_enum_type(et)

symbol.set_type(en.get_enum_type())
```

Enum entries are r-value constants; their type is the `enum_type` itself (not
wrapped in a reference).

---

## 14. Error Codes Reference

All error codes are defined in `klang/src/errors_model.hpp` under
`namespace k::diag`.

| Error code (enum entry) | Class | Meaning |
|---|---|---|
| `symbol_diag::ERR_SYMBOL_NOT_FOUND` | `symbol_diag` | Name not found in any scope |
| `symbol_diag::ERR_UNRESOLVED_IDENTIFIER` | `symbol_diag` | Identifier still unresolved at end of Phase A |
| `symbol_diag::ERR_AGGREGATE_VISIBILITY_DENIED` | `symbol_diag` | Private/protected member access denied |
| `symbol_diag::ERR_VISIBILITY_ACCESS_DENIED` | `symbol_diag` | Global variable visibility violation |
| `symbol_diag::ERR_REDIRECT_CHAIN_CYCLE` | `symbol_diag` | Circular redirect chain detected |
| `symbol_diag::ERR_REDIRECT_TARGET_NOT_FOUND` | `symbol_diag` | Redirector has no resolved target |
| `symbol_diag::ERR_REDIRECT_AMBIGUOUS` | `symbol_diag` | Redirector targets an unresolved redirector |
| `symbol_diag::ERR_REDIRECT_SELF_REF` | `symbol_diag` | Redirector targets an abstract function |
| `symbol_diag::ERR_REDIRECT_INCOMPATIBLE_SIG` | `symbol_diag` | Redirector targets a deleted function |
| `function_diag::ERR_FUNC_ANNOTATION_MISMATCH` | `function_diag` | Enum entry name not found in enum |
| `codegen_diag::INTERNAL_ERR_F003` | `codegen_diag` | Symbol reached type-resolution without being resolved (internal error) |
| `type_diag::ERR_MEMBER_NOT_FOUND_ON_OBJECT` | `type_diag` | Dot-access on non-reference type |

---

## 15. Complete Examples

### Example 1 — Upward scope walk and shadowing

```k
module demo;

x: int = 1;            // global variable in root ns

namespace helpers {
    x: int = 2;        // global variable in helpers ns
    
    compute() : int {
        x: int = 3;    // local variable in compute()
        return x;      // resolves: block → finds local x (= 3)
                       // global helpers::x (= 2) is shadowed
                       // global ::x (= 1) is shadowed further
    }
    
    computeGlobal() : int {
        return x;      // resolves: block (empty) → function params (none)
                       // → namespace helpers → finds helpers::x (= 2)
    }
    
    computeRoot() : int {
        return ::x;    // absolute → root namespace → finds x (= 1)
    }
}
```

### Example 2 — Qualified name resolution

```k
module geo;

namespace shapes {
    struct Point {
        x: float;
        y: float;
        
        distance(other: Point&) : float {
            dx := other.x - this.x;   // 'other' → parameter,
                                       // 'x' → this.x (member)
            dy := other.y - this.y;
            return sqrt(dx*dx + dy*dy);
        }
    }
    
    makePt(x: float, y: float) : Point {
        return {x, y};
    }
}

main() : int {
    p1 := shapes::makePt(0.0, 0.0);  // qualified: resolve 'shapes' ns → 'makePt' fn
    p2 := shapes::makePt(3.0, 4.0);
    d  := p1.distance(p2);            // member access on Point
    return 0;
}
```

**Walk for `shapes::makePt`** (inside `main`):

1. `resolve_symbol(main's block, "shapes::makePt")` — qualified name, size 2
2. `block` is not an `aggregate_holder` with "shapes" → miss
3. `block` is not a `ns` with child "shapes" → miss
4. Check `using` directives of `block` → none
5. Climb to `function main` → not a `function_holder`, not `aggregate_holder`
6. Climb to root `ns` (module `geo`):
   - `ns` has child namespace "shapes" → enter
   - `resolve_symbol(shapes ns, "makePt")` — simple name
   - `shapes ns` is a `function_holder` → `shapes.get_function("makePt")` → **found**

### Example 3 — Using directives

```k
module app;

import k::math;

compute(x: float) : float {
    using namespace k::math;   // anonymous using inside function block

    return abs(x) + sqrt(x);   // 'abs' → resolved via using → k::math::abs
                                // 'sqrt' → resolved via using → k::math::sqrt
}
```

**Walk for `abs`** (inside `compute`'s block):

1. `resolve_symbol(block, "abs")` — simple name
2. `block` variable holder → not found
3. `block` is not a `function_holder`
4. Check `using` directives on `block`:
   - directive `using namespace k::math` (anonymous)
   - `resolve_using_target({k, math}, unit)` → resolves to imported aggregate
   - `resolve_qualified_from(*k_math_elem, "abs")` → **found** (imported function)

### Example 4 — Enum entry resolution

```k
module status;

enum State { Idle; Running; Stopped; }

describe(s: State) : void {
    if s == State::Running {      // State::Running → enum entry target
        // ...
    }
}
```

**Walk for `State::Running`**:

1. `resolve_symbol(block, "State::Running")` — qualified name, size 2
2. At block level: no aggregate named "State" → miss
3. No namespace "State" in block → miss
4. Using → none
5. Climb to function → no aggregate/ns named "State" → miss
6. Climb to root ns → no aggregate named "State" → miss (it is an `enum`)
7. Standard resolution returns monostate

**Special fallback** in `visit_symbol_expression`:
- `sym_name.size() == 2`, not root-prefixed
- Walk scope chain looking for `enum_holder` with `get_enum("State")` → found in root ns
- `found_enum.get_entry_by_name("Running")` → index 1
- `symbol.set_target(enum_entry_target{ State enum, index 1 })`

### Example 5 — Visibility and friend access

```k
module vault;

class Safe {
    private:
    secret: int = 42;

    friend class Auditor;

    public:
    locked() : bool { return true; }
}

class Auditor {
    inspect(s: Safe&) : int {
        return s.secret;  // allowed: Auditor is a friend of Safe
    }
}

class Hacker {
    steal(s: Safe&) : int {
        return s.secret;  // ERROR: ERR_AGGREGATE_VISIBILITY_DENIED
                          // private member, Hacker is not a friend
    }
}
```

---

## Summary — Resolution decision tree

```
resolve_symbol(elem, name)
│
├─ name == "this"  →  find nearest non-static member function's this param
│
├─ name.has_root_prefix()  →  resolve_symbol_from_root(name.without_root_prefix())
│                               │
│                               ├─ Step 1: unit-name prefix shorthand  →  resolve_qualified_from(root_ns, rest)
│                               ├─ Step 2: direct from root            →  resolve_qualified_from(root_ns, name)
│                               ├─ Step 3: imported function/variable
│                               └─ Step 4: imported aggregate method
│
├─ name.size() > 1 (qualified, relative)
│   │
│   ├─ elem is aggregate_holder  →  descend into named aggregate, then resolve rest
│   └─ elem is ns                →  descend into named child ns, then resolve rest
│
├─ name.size() == 1 (simple)
│   │
│   ├─ elem is variable_holder   →  look up in variable map
│   ├─ elem is function_holder   →  look up in function list
│   ├─ elem is aggregate         →  BFS over base classes for inherited members
│   └─ elem is block             →  look up in enclosing function's parameter list
│
├─ resolve_via_using(elem, name)  →  try using directives at current scope
│
└─ elem.parent() exists
    │
    └─ recurse: resolve_symbol(*parent, name)
        │
        └─ at root (no parent):
            ├─ find_imported_function(name)
            ├─ find_imported_variable(name)
            └─ find imported aggregate method (peel back as agg_name::func_name)
```

After standard resolution fails (monostate), `visit_symbol_expression` attempts
special forms:

```
if name.size() >= 2:
    if name.back() == "annotation"   →  annotation RTTI resolution
    if name.size() == 2              →  enum entry resolution (scope walk for enum type)
    if name.size() == 3 and [1]=="Kind"  →  union Kind enum entry
    fallback: imported enum entry
```

---

_End of symbol resolution specification._

