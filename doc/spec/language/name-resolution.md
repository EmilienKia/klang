# K Language — Name Resolution Specification

_Language-level specification intended for IDE implementors, language server authors,_
_AI agents building context resolvers, and anyone implementing name lookup for K._

This document is **compiler-agnostic**: it describes the resolution rules as part of
the K language specification, independently of how the reference compiler (klangc)
implements them internally.

---

## Table of Contents

1. [Definitions](#1-definitions)
2. [Name Forms](#2-name-forms)
   - 2.1 [Simple names](#21-simple-names)
   - 2.2 [Qualified names](#22-qualified-names)
   - 2.3 [Absolute names](#23-absolute-names)
3. [Scope Types and Declaration Spaces](#3-scope-types-and-declaration-spaces)
   - 3.1 [Namespace scope](#31-namespace-scope)
   - 3.2 [Aggregate scope](#32-aggregate-scope)
   - 3.3 [Function scope](#33-function-scope)
   - 3.4 [Block scope](#34-block-scope)
   - 3.5 [For-statement scope](#35-for-statement-scope)
4. [Declarations and Their Kinds](#4-declarations-and-their-kinds)
5. [General Resolution Algorithm](#5-general-resolution-algorithm)
   - 5.1 [Overall lookup procedure](#51-overall-lookup-procedure)
   - 5.2 [The `this` keyword](#52-the-this-keyword)
   - 5.3 [Absolute names](#53-absolute-names)
   - 5.4 [Qualified names — downward descent](#54-qualified-names--downward-descent)
   - 5.5 [Simple names — local search](#55-simple-names--local-search)
   - 5.6 [Using directives](#56-using-directives)
   - 5.7 [Upward scope climb](#57-upward-scope-climb)
   - 5.8 [External module fallback](#58-external-module-fallback)
6. [Absolute Name Lookup from Module Root](#6-absolute-name-lookup-from-module-root)
7. [Pure Downward Lookup](#7-pure-downward-lookup)
8. [Inherited Member Lookup](#8-inherited-member-lookup)
9. [Using Directives in Detail](#9-using-directives-in-detail)
   - 9.1 [Anonymous namespace using](#91-anonymous-namespace-using)
   - 9.2 [Aliased namespace using](#92-aliased-namespace-using)
   - 9.3 [Specific element using](#93-specific-element-using)
   - 9.4 [Ambiguity](#94-ambiguity)
10. [Member Access — Dot Operator](#10-member-access--dot-operator)
    - 10.1 [Field access](#101-field-access)
    - 10.2 [Method lookup](#102-method-lookup)
    - 10.3 [Unified call syntax](#103-unified-call-syntax)
    - 10.4 [Member access through inheritance](#104-member-access-through-inheritance)
11. [Special Name Forms](#11-special-name-forms)
    - 11.1 [Enum entries](#111-enum-entries)
    - 11.2 [Union discriminator entries](#112-union-discriminator-entries)
    - 11.3 [Annotation type descriptors](#113-annotation-type-descriptors)
12. [Overload Resolution](#12-overload-resolution)
13. [Visibility Rules](#13-visibility-rules)
    - 13.1 [Member visibility](#131-member-visibility)
    - 13.2 [Namespace-level visibility](#132-namespace-level-visibility)
    - 13.3 [Friendship](#133-friendship)
14. [Function Redirect Resolution](#14-function-redirect-resolution)
15. [Shadowing and Name Hiding](#15-shadowing-and-name-hiding)
16. [Resolution Errors](#16-resolution-errors)
17. [Complete Annotated Examples](#17-complete-annotated-examples)

---

## 1. Definitions

| Term | Meaning |
|---|---|
| **Name** | A sequence of one or more dot-free identifiers separated by `::`, optionally prefixed with `::`. |
| **Simple name** | A name consisting of a single identifier (e.g. `foo`). |
| **Qualified name** | A name consisting of two or more identifiers (e.g. `util::foo`), without a leading `::`. |
| **Absolute name** | Any name beginning with `::` (e.g. `::util::foo`). Resolved from the module root. |
| **Scope** | A syntactic region that introduces a declaration space (namespace body, aggregate body, function body, block, for-init). |
| **Declaration** | Any named entity introduced in a scope: variable, function, aggregate, enumeration, union, using-directive. |
| **Resolution** | The process of determining which declaration a name refers to. |
| **Scope chain** | The ordered sequence of enclosing scopes from the current position up to the module root. |
| **Lookup** | A single search attempt in a given scope (without climbing). |
| **Module root** | The top-level namespace that corresponds to the `module` declaration of the current compilation unit. |

---

## 2. Name Forms

### 2.1 Simple names

A **simple name** is a single identifier used in an expression or type position.

```k
x           // simple name
foo         // simple name
MyStruct    // simple name (type position)
```

Simple names are resolved by searching the current scope first, then climbing the
scope chain (see [§5.5](#55-simple-names--local-search)).

### 2.2 Qualified names

A **qualified name** contains `::` separators but does **not** begin with `::`.

```k
util::helper        // 2 components: namespace util, then helper
shapes::Point::x    // 3 components: namespace shapes, type Point, field x
```

Resolution starts from the **current scope** and descends into each component in
order (see [§5.4](#54-qualified-names--downward-descent)).

### 2.3 Absolute names

An **absolute name** begins with `::` and is anchored at the module root.

```k
::util::helper      // always refers to util::helper in the current module root
::greet             // refers to greet at the top of the current module
```

Absolute names bypass the current scope entirely and go straight to the module root
(see [§6](#6-absolute-name-lookup-from-module-root)).

**Special case — explicit module prefix**: writing the module name as the first
component after `::` is allowed and equivalent to omitting it:

```k
// In module the::game:
::the::game::run()   // explicit module prefix form
::run()              // short form — identical result
```

---

## 3. Scope Types and Declaration Spaces

Every scope has a **declaration space** — a collection of names it directly
introduces. The scopes that exist in K, from outermost to innermost:

### 3.1 Namespace scope

Introduced by `namespace Name { … }` or implicitly by the `module` declaration.

**Declares**: global variables, free functions, aggregates (struct/class/interface/
annotation types), enumerations, union types, child namespaces, using directives.

The **module root** is the implicit top-level namespace created by the `module`
declaration. It is the starting point for all absolute name lookups.

```k
module myapp;           // creates the "myapp" root namespace

namespace util {        // child namespace
    count: int = 0;     // global variable in util
    reset() : void {}   // free function in util
}
```

### 3.2 Aggregate scope

Introduced by `struct`, `class`, `interface`, or `annotation` bodies.

**Declares**: member variables, member functions (methods), nested aggregates,
nested enumerations, nested unions, using directives, friend directives.

```k
struct Point {
    x: float;           // member variable
    y: float;
    norm() : float {}   // member function
}
```

### 3.3 Function scope

Introduced by a function or method body.

**Declares**: parameters (as if they were variables declared at the top of the
function body).

Parameters are accessible throughout the entire function body and any nested blocks.

```k
add(a: int, b: int) : int {
    // 'a' and 'b' are in scope here
    return a + b;
}
```

### 3.4 Block scope

Introduced by `{ … }` inside a function body or control-flow statement.

**Declares**: local variables (via variable declaration statements).

A variable declared in a block is visible from the point of its declaration until
the closing `}` of the block that introduces it.

```k
foo() : void {
    x: int = 1;   // in scope from here onwards in this block
    {
        y: int = 2;   // in scope only in this inner block
        // x is also accessible here (outer block)
    }
    // y is NOT accessible here
}
```

### 3.5 For-statement scope

A `for` statement implicitly creates a scope for the init-statement:

```k
for (i: int = 0; i < 10; i++) {
    // 'i' is in scope here
}
// 'i' is NOT in scope here
```

---

## 4. Declarations and Their Kinds

The K resolution algorithm must distinguish between the following declaration kinds,
because they exist in different declaration sub-spaces within a scope:

| Kind | Where declared | Accessible as |
|---|---|---|
| **Variable** | Namespace, aggregate (member), block, for-init | Value / reference |
| **Parameter** | Function | Value / reference (like a variable) |
| **Function** | Namespace, aggregate (method) | Callable, or first-class reference |
| **Aggregate type** | Namespace, nested in aggregate | Type name |
| **Enumeration type** | Namespace, nested in aggregate | Type name; entries via `Enum::entry` |
| **Union type** | Namespace, nested in aggregate | Type name; discriminator via `Union::Kind::entry` |
| **Child namespace** | Namespace | Scope qualifier (`ns::...`) |
| **Enum entry** | Inside enumeration body | Via qualified access `Enum::entry` |

---

## 5. General Resolution Algorithm

Given a **name** and a **starting scope** (the scope that syntactically contains the
name occurrence), the resolution algorithm produces either:

- A single matched declaration, **or**
- An ordered set of overloaded functions (when the name is used as a callee), **or**
- A resolution failure (error).

### 5.1 Overall lookup procedure

```
RESOLVE(name, current_scope):

  1. If name == "this":
       return THIS_LOOKUP(current_scope)                    // §5.2

  2. If name is absolute (starts with ::):
       return ROOT_LOOKUP(name.without_prefix, module_root) // §6

  3. If name is qualified (has :: inside, no leading ::):
       return QUALIFIED_LOOKUP(name, current_scope)         // §5.4

  4. // Simple name:
       result = SIMPLE_LOOKUP(name, current_scope)          // §5.5
       if result found: return result

  5. result = USING_LOOKUP(name, current_scope)             // §5.6 / §9
       if result found: return result

  6. if current_scope has a parent scope:
       return RESOLVE(name, parent_scope)                   // §5.7 (upward climb)

  7. // Reached module root, still not found:
       return EXTERNAL_LOOKUP(name)                         // §5.8
```

### 5.2 The `this` keyword

`this` is only valid inside a **non-static member function** body (or a block
nested inside one).

```
THIS_LOOKUP(current_scope):
  walk outward through enclosing scopes:
    for each enclosing function F:
      if F is a non-static member function:
        return the implicit 'this' parameter of F   // type: ref<OwnerStruct>
  ERROR: 'this' used outside a non-static member function
```

`this` evaluates to a reference to the current instance of the enclosing struct/class.

```k
struct Counter {
    value: int;
    inc() {
        this.value++;   // 'this' → ref<Counter> (parameter of inc)
    }
}
```

### 5.3 Absolute names

Delegated to [§6](#6-absolute-name-lookup-from-module-root).

### 5.4 Qualified names — downward descent

A qualified name `A::B::C` is resolved by first identifying scope `A` from the
current position, then descending into it to find `B`, and so on.  The descent is
**purely downward** — it never climbs back up once a component has been matched.

```
QUALIFIED_LOOKUP(name, current_scope):
  first = name.first_component
  rest  = name.remaining_components

  // Search for 'first' among aggregates at this scope level
  if current_scope declares an aggregate named first:
    return DOWN_LOOKUP(rest, that_aggregate)

  // Search for 'first' among child namespaces at this scope level
  if current_scope is a namespace and has child namespace named first:
    return DOWN_LOOKUP(rest, that_child_namespace)

  // Not found at this level — try using directives, then climb
  result = USING_LOOKUP(name, current_scope)
  if result found: return result

  if current_scope has a parent:
    return QUALIFIED_LOOKUP(name, parent_scope)

  return EXTERNAL_LOOKUP(name)
```

```k
module geo;
namespace shapes {
    struct Circle { radius: float; }
    area(c: Circle&) : float { return 3.14 * c.radius * c.radius; }
}

main() {
    c: shapes::Circle;          // qualified: descend into 'shapes' → 'Circle'
    a := shapes::area(c);       // qualified: descend into 'shapes' → 'area'
}
```

### 5.5 Simple names — local search

A simple name is searched in the **current scope only**, without climbing.

```
SIMPLE_LOOKUP(name, scope):
  1. Search variables declared in scope (including parameters if scope is a function)
  2. Search functions declared in scope
  3. If scope is an aggregate: also search inherited members (§8)
  4. If scope is a block: also search the enclosing function's parameters

  return first match, or "not found"
```

> **Priority**: local variables and parameters shadow inherited members and outer
> scope declarations of the same name.

### 5.6 Using directives

After local search fails, the resolver checks **using directives** declared in the
current scope. See [§9](#9-using-directives-in-detail) for the complete rules.

Using directives are checked **before** climbing to the parent scope — they can
supply names that shadow the outer scope.

### 5.7 Upward scope climb

If neither local search nor using directives produced a result, the resolver climbs
to the **immediately enclosing scope** and retries the full procedure from Step 4
(§5.5). This continues until the module root is reached.

The climb follows the static (lexical) scope chain:

```
inner block → outer block → function (parameters) → aggregate → namespace → … → module root
```

**Important**: the upward climb is **lexical**, not dynamic. It follows the nesting
structure in the source code, not the call stack at runtime.

### 5.8 External module fallback

When the module root is reached and the name is still unresolved, the resolver tries
to find the declaration in **imported modules** (loaded through `import` statements):

```
EXTERNAL_LOOKUP(name):
  1. Search all imported module symbols for a function matching name
  2. Search all imported module symbols for a variable matching name
  3. If name has >= 2 components:
       agg_name  = name.without_last_component
       func_name = name.last_component
       Search imported aggregates for agg_name,
         then search that aggregate's functions for func_name
```

---

## 6. Absolute Name Lookup from Module Root

```
ROOT_LOOKUP(name_without_prefix, module_root):

  1. // Module-name shorthand: '::MyModule::rest' is the same as '::rest'
     // when the first component equals the module's own name.
     if name.first_component == module_root.name:
       result = DOWN_LOOKUP(name.without_first, module_root)
       if found: return result
       // fall through in case a child ns has the same name as the module

  2. result = DOWN_LOOKUP(name, module_root)
     if found: return result

  3. // Search imported modules (see §5.8)
     return EXTERNAL_LOOKUP(name)
```

The rationale for step 1 is that the module name and its root namespace are the
same entity. Both `::myapp::foo` and `::foo` (when in module `myapp`) refer to
the same top-level function `foo`.

---

## 7. Pure Downward Lookup

`DOWN_LOOKUP` is used by both absolute and qualified resolution. It **never climbs**.

```
DOWN_LOOKUP(name, scope):

  if name has one component:
    search scope's variables for name     → return if found
    search scope's functions for name     → return if found
    return "not found"

  // name has 2+ components:
  first = name.first_component
  rest  = name.without_first

  if scope is a namespace and has child namespace named first:
    return DOWN_LOOKUP(rest, that_child_namespace)

  if scope declares an aggregate named first:
    return DOWN_LOOKUP(rest, that_aggregate)

  return "not found"
```

---

## 8. Inherited Member Lookup

When performing a simple name lookup inside an **aggregate scope**, the resolver
also searches the aggregate's base classes using **breadth-first search (BFS)**:

```
INHERITED_LOOKUP(name, aggregate):
  queue = [all direct base aggregates of aggregate]
  while queue is not empty:
    base = queue.dequeue()
    search base's variables for name  → return if found
    search base's functions for name  → return if found
    queue.enqueue(all direct bases of base)
  return "not found"
```

BFS gives precedence to closer ancestors over more distant ones.

If the **same name** is found in two or more bases at the same BFS depth (diamond
inheritance), the result is **ambiguous** — the programmer must qualify the name.

```k
class Animal {
    name: String;
    sound() : String { return "..."; }
}

class Dog : public Animal {
    fetch() : void { }
}

class GoldenRetriever : public Dog {
    greet() : void {
        sound();      // found in Animal via Dog via inherited lookup
        this.name;    // found in Animal via Dog
    }
}
```

---

## 9. Using Directives in Detail

A `using` directive appears inside a scope (namespace, aggregate, block) and
virtually injects declarations from another scope, making them accessible without
full qualification.

**Syntax forms**:

```k
using namespace some::ns;              // (1) anonymous namespace using
using Alias = namespace some::ns;      // (2) aliased namespace using
using some::ns::foo;                   // (3) specific element using
using Bar = some::ns::foo;             // (4) specific element using with alias
```

Using directives affect **name lookup only** — they do not create new declarations
and are local to the scope where they appear.

### 9.1 Anonymous namespace using

```k
using namespace k::math;
```

All members of `k::math` are injected into the current scope.

```
USING_ANONYMOUS(name, target_ns):
  return DOWN_LOOKUP(name, target_ns)
```

```k
foo() : float {
    using namespace k::math;
    return abs(-3.5) + sqrt(2.0);   // resolved as k::math::abs, k::math::sqrt
}
```

### 9.2 Aliased namespace using

```k
using M = namespace k::math;
```

The namespace becomes accessible under the alias `M`.

```
USING_ALIASED_NS(name, alias, target_ns):
  if name.first_component != alias: return "not found"
  rest = name.without_first
  return DOWN_LOOKUP(rest, target_ns)
  // fallback: try EXTERNAL_LOOKUP on fully-qualified name built from target_ns path + rest
```

```k
foo() : float {
    using M = namespace k::math;
    return M::abs(-3.5);   // → k::math::abs
}
```

### 9.3 Specific element using

```k
using k::math::abs;           // inject 'abs' from k::math
using myAbs = k::math::abs;   // inject 'abs' as 'myAbs'
```

Only the targeted element is injected.

```
USING_SPECIFIC(name, target_path, opt_alias):
  lookup_name = opt_alias if present, else target_path.last_component
  if name.first_component != lookup_name: return "not found"

  target_parent = DOWN_LOOKUP(target_path.without_last, module_root)
  if name.size() == 1:
    return DOWN_LOOKUP([target_path.last], target_parent)
  else:
    // name has more components after the alias/element name
    descend further: return DOWN_LOOKUP(name.without_first, target_parent::target_element)
```

```k
compute() : float {
    using k::math::abs;
    return abs(-1.5);      // → k::math::abs
}
```

### 9.4 Ambiguity

If two using directives in the same scope both match the same name, the result is
**ambiguous**. An IDE should flag this as a warning and highlight all matching
candidates. The first directive that matched wins for compilation purposes.

---

## 10. Member Access — Dot Operator

The dot operator (`expr.member`) is a two-step process:

1. Resolve `expr` (the left-hand side) and determine its type.
2. Once the type is known, look up `member` **within that type's scope**.

This second step is performed **after** the LHS expression is fully typed, which
means it cannot use the general scope-chain algorithm — it targets a specific aggregate.

### 10.1 Field access

```
MEMBER_FIELD_LOOKUP(member_name, struct_type):
  if struct_type has a field named member_name:
    return that field
  return "not found"
```

The type of a field access expression is a **reference** to the field type,
making the field usable as both an l-value and an r-value.

```k
p: Point;
p.x = 3;        // l-value use — assign through reference
val := p.x;     // r-value use — load through reference
```

### 10.2 Method lookup

```
MEMBER_METHOD_LOOKUP(method_name, struct_type):
  if struct_type has a method named method_name:
    return that method (or the set of overloads — see §12)
  search inherited methods (§8)
  return "not found"
```

### 10.3 Unified call syntax

If neither a field nor a method is found, K supports the **unified call syntax**:
a free function whose first parameter is a (reference to the) same struct type can
be called as if it were a method.

```
UNIFIED_CALL_LOOKUP(method_name, struct_type, current_scope):
  // Collect all free functions named method_name visible from current_scope
  candidates = all_functions_named(method_name, current_scope)
  for each candidate F:
    if F has >= 1 parameter and first_param_type is ref<struct_type>
       or const ref<struct_type>:
      include F in result set
  return result set (may be overloaded)
```

```k
struct Vec2 { x: float; y: float; }

length(v: Vec2&) : float { return sqrt(v.x*v.x + v.y*v.y); }

demo() {
    v: Vec2 = {3.0, 4.0};
    l := v.length();    // unified call: resolves to free function length(v)
}
```

### 10.4 Member access through inheritance

If a field or method is not found directly on the struct, the resolver searches
inherited members using the same BFS algorithm described in [§8](#8-inherited-member-lookup).

At the LLVM/codegen level the path is navigated through `__base_Name__` sub-object
fields; from the language-specification point of view, the inherited member simply
behaves as if it were declared directly on the derived type.

---

## 11. Special Name Forms

Some name forms in K look syntactically like qualified identifiers but have
dedicated resolution rules.

### 11.1 Enum entries

The form `EnumName::entryName` (exactly 2 components, relative) resolves to a
**compile-time constant** of the enumeration type. It is **not** a variable or
function reference.

```
ENUM_ENTRY_LOOKUP(name):                 // name.size() == 2, not absolute
  enum_name  = name.first_component
  entry_name = name.second_component

  // Walk up the scope chain looking for an enumeration named enum_name
  for each scope S in scope chain:
    if S declares an enumeration named enum_name:
      if that enumeration has an entry named entry_name:
        return enum_entry_constant(enumeration, entry_index)
      else:
        ERROR: "Enumeration '...' has no entry named '...'"

  // Fallback: search imported modules for an enum matching enum_name::entry_name
  if found_in_imports:
    return imported_enum_entry_constant(...)

  ERROR: "Name '...' does not resolve to an enumeration"
```

```k
enum Color { Red; Green; Blue; }

paint(c: Color) : void { /* ... */ }

main() {
    paint(Color::Blue);   // Color::Blue → enum constant, index 2
}
```

**Note**: Enum entries are **r-value constants** (not l-values). Their type is the
`enum_type` itself, not a reference to it.

### 11.2 Union discriminator entries

Discriminated unions automatically synthesize an internal enumeration named `Kind`.
The form `UnionName::Kind::entryName` (exactly 3 components, middle == `"Kind"`)
resolves to a constant of that synthesized enumeration.

```
UNION_KIND_ENTRY_LOOKUP(name):           // name.size() == 3, name[1] == "Kind"
  union_name = name.first_component
  entry_name = name.third_component

  for each scope S in scope chain:
    if S declares a union type named union_name:
      kind_enum = union_name.Kind_enumeration   // synthesized
      if kind_enum has entry named entry_name:
        return enum_entry_constant(kind_enum, entry_index)

  ERROR: union not found, or entry not found
```

```k
union Shape { circle: Circle; rect: Rectangle; }

isCircle(s: Shape&) : bool {
    return s.kind == Shape::Kind::circle;   // 3-part: Shape → Kind → circle
}
```

### 11.3 Annotation type descriptors

The form `AnnotationTypeName::annotation` (last component == `"annotation"`)
resolves to the **runtime type descriptor** (RTTI) of an annotation type. It yields
a `const k::AnnotationType&` value.

```
ANNOTATION_RTTI_LOOKUP(name):
  ann_name = name.without_last_component

  // Try local scope first (single-component ann_name)
  if ann_name.size() == 1:
    for each scope S in scope chain:
      if S declares an annotation type named ann_name:
        return annotation_rtti_descriptor(that_annotation_type)

  // Fallback: search imported annotation types
  if found_in_imports:
    return imported_annotation_rtti_descriptor(...)

  ERROR: annotation type not found, or not an annotation type
```

```k
annotation Deprecated {}

getType() : const AnnotationType& {
    return Deprecated::annotation;      // → RTTI descriptor of Deprecated
}
```

---

## 12. Overload Resolution

When a name lookup produces **multiple functions** (overloads), the best match is
selected based on the argument types at the call site.

### Candidate collection

All overloads visible at the call site are collected from every scope level
traversed during lookup (both local and parent scopes). A function in an inner
scope does **not** automatically hide identically-named functions in outer scopes —
all are candidates.

```
OVERLOAD_CANDIDATES(name, call_site_scope):
  candidates = []
  for each scope S in scope chain from call_site_scope up to module root:
    candidates.add_all(functions named name declared in S)
  candidates.add_all(imported functions named name)
  return candidates
```

### Candidate filtering

1. **Arity**: candidates with the wrong number of parameters are eliminated.
2. **Type compatibility**: each argument type must be compatible with the
   corresponding parameter type (either exact match or implicit conversion allowed).
3. **Const correctness**: a `const` reference argument cannot bind to a non-const
   reference parameter.

### Best-match selection

Among the remaining candidates, the **most specific** match is selected:
- An exact-type match beats an implicit-conversion match.
- If two candidates are equally specific, the call is **ambiguous** → error.

### Unified call syntax candidates

When resolving `expr.method(args)`, the candidate set also includes free functions
whose first parameter matches `ref<typeof(expr)>` (or `const ref<typeof(expr)>`),
with the remaining parameters matched against `args`.

---

## 13. Visibility Rules

Visibility restricts which scopes can access a given declaration.

### Visibility levels in K

| Keyword | Scope | Meaning |
|---|---|---|
| `public` | Members and global decls | Accessible everywhere |
| `protected` | Members | Accessible from the declaring aggregate, its derived types, and friends |
| `private` | Members | Accessible only from within the declaring aggregate (and friends) |
| `protected` | Namespace-level decls | Accessible only within the same **module** (compilation unit) |
| `private` | Namespace-level decls | Accessible only within the same **namespace** |

> The same keyword has different meanings depending on whether it annotates a
> **member** of an aggregate or a **declaration** in a namespace.

### 13.1 Member visibility

```
IS_MEMBER_ACCESSIBLE(member, access_site):
  vis = member.declared_visibility
  owner = aggregate that declares member

  if vis == PUBLIC:   return ACCESSIBLE

  if vis == PRIVATE:
    return access_site is inside a member function of owner
           OR inside a member function of a type nested inside owner

  if vis == PROTECTED:
    return access_site is inside a member function of owner
           OR inside a member function of a type that directly or transitively
              inherits from owner
           OR access_site is in a friend of owner (§13.3)
```

```k
class Box {
    private:
    secret: int;

    protected:
    side: float;

    public:
    volume() : float { return side * side * side; }   // can access 'side'
}

class BigBox : public Box {
    describe() : void {
        // side is accessible (PROTECTED, BigBox inherits from Box)
        // secret is NOT accessible (PRIVATE to Box)
    }
}
```

### 13.2 Namespace-level visibility

```
IS_GLOBAL_VAR_ACCESSIBLE(variable, access_site):
  vis = variable.declared_visibility
  owner_ns = namespace that declares variable

  if vis == PUBLIC:     return ACCESSIBLE

  if vis == PROTECTED:  // same-module restriction
    return access_site is in the same module (same root namespace)

  if vis == PRIVATE:    // same-namespace restriction
    return access_site is inside owner_ns or a namespace nested inside owner_ns
```

The same rules apply to free functions:

```k
module mymod;

private:                      // module-private section
helperFunc() : void { }       // only visible inside mymod

protected:                    // same-module section
internalFunc() : void { }     // same module only

public:
api() : void { helperFunc(); }  // ok: within same module
```

### 13.3 Friendship

A `friend` declaration inside an aggregate grants a named external entity access to
the aggregate's **protected** members (it does not grant access to private members
beyond the normal private access rule).

```k
class Safe {
    private: key: int;
    friend class Inspector;
}
```

**Friendship rules**:

- If the friend target is an **aggregate**, all direct member functions of that
  aggregate (not inherited, not in nested types) are friends.
- If the friend target is a **function**, only that specific function is a friend.
- Friendship is **not inherited**: subclasses of `Inspector` are not friends of `Safe`.
- Friendship is **not transitive**: friends of `Inspector` are not friends of `Safe`.
- Friendship is **not symmetric**: `Safe` being a friend of `X` does not make `X` a
  friend of `Safe`.

```
IS_FRIEND(owner_aggregate, access_site_function):
  for each friend_directive F in owner_aggregate:
    target = RESOLVE_FROM_ROOT(F.target_name)
    if target is an aggregate:
      if access_site_function.owner == target: return IS_FRIEND
    if target is a function:
      if access_site_function == target: return IS_FRIEND
  return NOT_FRIEND
```

---

## 14. Function Redirect Resolution

K allows functions to be declared as redirectors using `-> target_expr` syntax
(e.g., `-> default` or `-> delete` or `-> otherFunc`). A redirector function has
no body of its own; calls to it are forwarded to the target.

**Redirect chain flattening**: if A redirects to B and B redirects to C, then the
effective target of A is C (chains are resolved transitively at compile time).

```
RESOLVE_REDIRECT_CHAIN(fn, visited):
  if fn already in visited: ERROR — circular redirect chain
  add fn to visited
  target = fn.redirect_target

  if target is itself a redirector:
    return RESOLVE_REDIRECT_CHAIN(target, visited)

  if target is abstract:  ERROR — cannot redirect to an abstract function
  if target is deleted:   ERROR — cannot redirect to a deleted function
  return target
```

After chain resolution, every redirector has a **direct pointer** to its concrete
final target. Callers need not traverse the chain at call time.

---

## 15. Shadowing and Name Hiding

### Shadowing

A declaration in an inner scope **shadows** a declaration with the same name in an
outer scope. The inner declaration completely hides the outer one.

```k
x: int = 10;               // outer x

foo() : int {
    x: int = 20;           // inner x shadows outer x
    return x;              // returns 20, outer x is invisible here
}
```

The outer declaration is not erased — it is accessible via **absolute** or
**qualified** names if a path to it exists.

### Name hiding in aggregates

An overriding method in a derived class hides **all** base-class members with the
same simple name, even those with different signatures:

```k
class Base {
    foo(x: int) : void {}
    foo(x: float) : void {}
}

class Derived : public Base {
    foo() : void {}  // hides BOTH base foo(int) and foo(float)
}
```

To access a hidden base-class member, use an explicit type qualifier or a using
directive:

```k
class Derived : public Base {
    using Base::foo;         // re-introduces both Base::foo overloads
    foo() : void {}          // adds a third overload
}
```

---

## 16. Resolution Errors

An IDE or language server should report the following categories of errors during
name resolution:

| Situation | Error kind |
|---|---|
| Name not found in any accessible scope | `Undefined symbol` |
| Ambiguous name (two or more equally-specific matches) | `Ambiguous reference` |
| Ambiguous access through multiple using directives | `Ambiguous using import` |
| Private or protected member accessed from disallowed scope | `Visibility violation` |
| `this` used outside a non-static member function | `Invalid use of 'this'` |
| Enum entry name not found in the enumeration | `Unknown enum entry` |
| Union Kind entry name not found in the union | `Unknown union alternative` |
| Annotation RTTI used without importing the `k` stdlib | `Missing 'k' import for RTTI` |
| Circular redirect chain | `Circular redirect` |
| Redirect target is abstract | `Abstract redirect target` |
| Redirect target is deleted | `Deleted redirect target` |

**Deferred resolution**: a name that is the callee of a function-invocation
expression may legitimately remain unresolved through the first name-resolution
pass (when `symbol_resolver` runs) if it could be a unified-call-syntax candidate.
The IDE should not report an error if the name is the callee of a call expression
and overload resolution is still pending.

---

## 17. Complete Annotated Examples

### Example 1 — Upward scope walk and shadowing

```k
module demo;

x: int = 1;                  // (A) global x in module root

namespace helpers {
    x: int = 2;              // (B) global x in helpers

    compute() : int {
        x: int = 3;          // (C) local x in compute
        return x;            // lookup chain: block → finds (C), returns 3
    }

    computeNs() : int {
        return x;            // lookup chain: block (empty) →
                             //   function params (none) →
                             //   helpers ns → finds (B), returns 2
    }

    computeRoot() : int {
        return ::x;          // absolute lookup → module root → finds (A), returns 1
    }
}
```

Scope walk for `x` inside `compute()`:

```
Block of compute        → finds local variable x (C) ✓ STOP
```

Scope walk for `x` inside `computeNs()`:

```
Block of computeNs      → no variable named x
Using directives        → none
↑ Function computeNs    → no parameter named x
↑ Namespace helpers     → finds global variable x (B) ✓ STOP
```

### Example 2 — Qualified lookup across namespaces and aggregates

```k
module geo;

namespace shapes {
    struct Point {
        x: float;
        y: float;

        distance(other: Point&) : float {
            dx := other.x - this.x;    // 'other' → parameter (function scope)
                                        // '.x'   → field lookup on Point
                                        // 'this' → implicit parameter of distance
            dy := other.y - this.y;
            return sqrt(dx*dx + dy*dy);
        }
    }

    makePoint(x: float, y: float) : Point {
        return {x, y};
    }
}

main() : int {
    // Qualified lookup: shapes → namespace, then makePoint → function
    p1 := shapes::makePoint(0.0, 0.0);

    // Qualified lookup: shapes → namespace, Point → aggregate, distance → method
    // But method calls use member access syntax, not qualified names:
    p2 := shapes::makePoint(3.0, 4.0);
    d  := p1.distance(p2);   // member access, not qualified name
    return 0;
}
```

Scope walk for `shapes::makePoint` inside `main`:

```
Block of main           → no aggregate 'shapes', no ns 'shapes'
Using directives        → none
↑ Function main         → no aggregate 'shapes', no ns 'shapes'
↑ Module root (geo)     → has child namespace 'shapes'
  Descend into shapes:
    DOWN_LOOKUP("makePoint", shapes::ns)
      shapes ns has function 'makePoint' ✓ STOP
```

### Example 3 — Inherited member lookup (BFS)

```k
class A { val: int; }
class B : public A { extra: float; }
class C : public B {
    show() : void {
        // Resolution of 'val' inside show():
        //   SIMPLE_LOOKUP('val', C's method scope) → not in C
        //   INHERITED_LOOKUP('val', C):
        //     BFS queue: [B]
        //     Check B: no 'val' → enqueue [A]
        //     Check A: 'val' found ✓
        print(val);
    }
}
```

### Example 4 — Using directives with qualified alias

```k
module app;
import k::math;

compute() : float {
    using M = namespace k::math;    // alias 'M' for k::math

    // Resolution of 'M::abs':
    //   SIMPLE_LOOKUP('M::abs', block) → not a variable or function
    //   USING_LOOKUP('M::abs', block):
    //     aliased namespace using, alias='M', target=k::math
    //     name.first_component == 'M' ✓
    //     rest = ['abs']
    //     DOWN_LOOKUP(['abs'], k::math) → finds k::math::abs ✓
    return M::abs(-3.5);
}
```

### Example 5 — Visibility and friendship

```k
module vault;

class Safe {
    private:
    passcode: int = 1234;

    protected:
    hint: String = "four digits";

    friend class Auditor;

    public:
    isLocked() : bool { return true; }
}

class Auditor {
    inspect(s: Safe&) : int {
        // passcode:
        //   IS_MEMBER_ACCESSIBLE(passcode, Auditor::inspect)?
        //   vis = PRIVATE → is Auditor::inspect inside a member fn of Safe? NO
        //   IS_FRIEND(Safe, Auditor::inspect)? YES (Auditor is a friend of Safe)
        //   → ACCESSIBLE ✓
        return s.passcode;
    }
}

class Hacker {
    steal(s: Safe&) : int {
        return s.passcode;
        // IS_MEMBER_ACCESSIBLE(passcode, Hacker::steal)?
        // vis = PRIVATE → is Hacker::steal inside Safe? NO
        // IS_FRIEND(Safe, Hacker::steal)? NO
        // → ERROR: visibility violation
    }
}

class SubSafe : public Safe {
    getHint() : String {
        // IS_MEMBER_ACCESSIBLE(hint, SubSafe::getHint)?
        // vis = PROTECTED → is SubSafe::getHint inside Safe or a derived type? YES (SubSafe inherits)
        // → ACCESSIBLE ✓
        return hint;
    }
}
```

### Example 6 — Overload resolution

```k
module math;

// Three overloads of 'format':
format(x: int) : String { return "int"; }
format(x: float) : String { return "float"; }
format(x: double) : String { return "double"; }

main() {
    s1 := format(42);      // 42 : int → exact match → format(int)
    s2 := format(3.14);    // 3.14 : double → exact match → format(double)
    s3 := format(1.0f);    // 1.0f : float → exact match → format(float)
}
```

Overload resolution for `format(42)`:

```
OVERLOAD_CANDIDATES('format', main's block):
  module root: format(int), format(float), format(double)

Filter by arity(1): all three remain
Filter by argument type (int → int): exact match = format(int), others need conversion
Best match: format(int) ✓
```

---

## Summary — Name Resolution Decision Tree

```
Given (name, current_scope):

name == "this"
  └─ find nearest non-static member function → return its this parameter

name has leading "::" (absolute)
  └─ ROOT_LOOKUP(name.without_prefix)
      ├─ [Step 1] if name.first == module_name → DOWN_LOOKUP(rest, root_ns)
      ├─ [Step 2] DOWN_LOOKUP(name, root_ns)
      └─ [Step 3] EXTERNAL_LOOKUP(name)

name has "::" separator (qualified, relative)
  └─ QUALIFIED_LOOKUP(name, current_scope)
      ├─ aggregate named name.first at current_scope → DOWN_LOOKUP(rest, aggregate)
      ├─ child namespace named name.first → DOWN_LOOKUP(rest, child_ns)
      ├─ USING_LOOKUP(name, current_scope)
      └─ recurse with parent_scope

name is simple (no ":")
  ├─ SIMPLE_LOOKUP(name, current_scope)
  │   ├─ variables in scope
  │   ├─ functions in scope
  │   ├─ if aggregate: inherited members (BFS over bases)
  │   └─ if block: enclosing function parameters
  ├─ USING_LOOKUP(name, current_scope)
  └─ recurse with parent_scope
      └─ at module root with no further parent:
          └─ EXTERNAL_LOOKUP(name)
              ├─ imported functions
              ├─ imported variables
              └─ imported aggregate methods (peel as agg_name::func_name)

After standard resolution yields nothing:
  name.size() >= 2 and name.back() == "annotation"
    └─ ANNOTATION_RTTI_LOOKUP(name)
  name.size() == 2 (relative)
    └─ ENUM_ENTRY_LOOKUP(name)
  name.size() == 3 and name[1] == "Kind" (relative)
    └─ UNION_KIND_ENTRY_LOOKUP(name)
```

---

_End of K language name resolution specification._



