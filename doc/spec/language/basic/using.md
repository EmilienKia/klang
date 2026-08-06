# Using Directives

[← Index](../index.md)

A `using` directive makes elements from another scope resolvable as if they were
declared in the enclosing scope.  It affects **name lookup only** — it does not
create or materialise new symbols.  Exports, imports and mangled names always use
the real (de-aliased) names.

A `using` directive is always **scope-local**: it is never exported, so an
importing module does not see it.  To publish a renaming through the module
interface, use an [alias declaration](aliases.md) (`alias` or `typedef`)
instead.

---

## Contents

1. [Overview](#1-overview)
2. [Grammar](#2-grammar)
3. [Anonymous namespace using](#3-anonymous-namespace-using)
4. [Specific element using](#4-specific-element-using)
5. [Aliased using](#5-aliased-using)
6. [Namespace alias](#6-namespace-alias)
7. [Scope and placement](#7-scope-and-placement)
8. [Interaction with imports](#8-interaction-with-imports)
9. [Name lookup priority](#9-name-lookup-priority)
10. [Restrictions](#10-restrictions)

---

## 1. Overview

K requires every symbol from another namespace or imported module to be
referenced by its fully qualified name.  The `using` directive relaxes this
requirement by injecting names or creating local aliases, without modifying
the actual namespace hierarchy.

There are four forms:

| Form | Effect |
|------|--------|
| `using namespace X::Y;` | All members of `X::Y` are directly accessible |
| `using X::Y::foo;` | Only `foo` is injected into the current scope |
| `using Alias = X::Y::foo;` | `foo` is accessible as `Alias` |
| `using namespace NS = X::Y;` | `X::Y` is accessible as `NS::member` |

---

## 2. Grammar

```
UsingDecl:
    'using' [ UsingFilter ] [ Identifier '=' ] QualifiedIdentifier ';'

UsingFilter:
    'namespace' | 'struct' | 'interface' | 'class'
```

The optional *UsingFilter* keyword constrains the kind of target element.
When `namespace` is specified, the directive imports the entire content of the
target namespace (anonymous form) or creates a namespace alias (named form).

The optional `Identifier '='` introduces a local alias for the target.

A `using` directive may appear wherever a [declaration](#) or a
[statement](#) is valid: at namespace scope, inside an aggregate body
(`struct`, `class`, `interface`), or inside a function body / block.

---

## 3. Anonymous namespace using

```k
using namespace X::Y;
```

All public members (functions, variables, types, nested namespaces) of `X::Y`
become resolvable from the enclosing scope as if they were direct members.

**Example:**

```k
module myapp;

namespace math {
    add(a : int, b : int) : int { return a + b; }
    sub(a : int, b : int) : int { return a - b; }
}

using namespace math;

test() : int {
    return add(10, 32);   // resolves to math::add
}
```

This also applies to types:

```k
namespace geom {
    struct Point { x : int; y : int; }
}

using namespace geom;

test() : int {
    p : Point;            // resolves to geom::Point
    p.x = 10;
    p.y = 32;
    return p.x + p.y;
}
```

---

## 4. Specific element using

```k
using X::Y::foo;
```

Only the element `foo` from scope `X::Y` is injected.  Other members of
`X::Y` remain inaccessible without full qualification.

**Example:**

```k
namespace math {
    mul(a : int, b : int) : int { return a * b; }
    div(a : int, b : int) : int { return a / b; }
}

using math::mul;

test() : int {
    return mul(6, 7);     // OK: resolves to math::mul
    // div(10, 2);        // ERROR: 'div' not found — not injected
}
```

This works for types as well:

```k
namespace geom {
    struct Vec2 { x : int; y : int; }
}

using geom::Vec2;

test() : int {
    v : Vec2;             // resolves to geom::Vec2
    v.x = 20;
    v.y = 22;
    return v.x + v.y;
}
```

---

## 5. Aliased using

```k
using Alias = X::Y::foo;
```

The target element is accessible under the alias name **only** in the enclosing
scope.  The original qualified name remains valid.  Aliases are purely local —
they never appear in exports or mangled names.

**Function alias:**

```k
namespace math {
    add(a : int, b : int) : int { return a + b; }
}

using sum = math::add;

test() : int {
    r1 : int = sum(10, 12);       // OK: 'sum' is an alias for math::add
    r2 : int = math::add(10, 10); // OK: original name still works
    return r1 + r2;               // 42
}
```

**Variable alias:**

```k
namespace config {
    answer : int = 42;
}

using the_answer = config::answer;

test() : int {
    return the_answer;    // resolves to config::answer
}
```

**Type alias:**

```k
namespace shapes {
    struct Rect { w : int; h : int; area() : int { return w * h; } }
}

using Box = shapes::Rect;

test() : int {
    b : Box;              // resolves to shapes::Rect
    b.w = 6;
    b.h = 7;
    return b.area();      // 42
}
```

---

## 6. Namespace alias

```k
using namespace NS = X::Y;
```

The namespace `X::Y` becomes accessible under the short name `NS`.
Members are **not** injected directly — they must be accessed via the
`NS::member` prefix.  This is the key difference with the anonymous form.

**Example — function access:**

```k
namespace very_long_namespace_name {
    compute(x : int) : int { return x * 2; }
}

using namespace ns = very_long_namespace_name;

test() : int {
    return ns::compute(21);   // resolves to very_long_namespace_name::compute
}
```

**Example — type access:**

```k
namespace geo {
    struct Point { x : int; y : int; }
}

using namespace g = geo;

test() : int {
    p : g::Point;         // resolves to geo::Point
    p.x = 10;
    p.y = 32;
    return p.x + p.y;
}
```

**Deeply nested namespaces:**

```k
namespace a { namespace b { namespace c {
    get_val() : int { return 42; }
}}}

using namespace deep = a::b::c;

test() : int {
    return deep::get_val();
}
```

### Comparison: anonymous vs aliased namespace using

| Directive | `add(1, 2)` | `ns::add(1, 2)` |
|-----------|:-----------:|:----------------:|
| `using namespace math;` | ✓ direct access | ✗ |
| `using namespace ns = math;` | ✗ | ✓ prefixed access |

---

## 7. Scope and placement

A `using` directive is valid in any scope that accepts declarations or
statements:

**Namespace scope:**

```k
module mymod;
namespace math { add(a: int, b: int) : int { return a + b; } }

using namespace math;           // namespace-level using
```

**Aggregate body:**

```k
struct Wrapper {
    using namespace math;       // using inside a struct body
    compute() : int { return add(1, 2); }
}
```

**Function body / block:**

```k
test() : int {
    using namespace math;       // statement-level using
    return add(10, 32);
}
```

**For-loop body:**

```k
test() : int {
    s : int = 0;
    for (i : int = 0; i < 3; i = i + 1) {
        using namespace math;
        s = s + add(i, 1);
    }
    return s;
}
```

The using directive affects lookup from the point of its declaration to the
end of its enclosing scope.

---

## 8. Interaction with imports

A `using` directive works seamlessly with imported modules.  After an `import`
declaration, the imported module's namespace can be used as the target of a
`using` directive:

**Anonymous using with import:**

```k
module consumer;
import mathlib;

using namespace mathlib;

main() : int {
    return add(10, 32);         // resolves to mathlib::add
}
```

**Specific using with import:**

```k
module consumer;
import arithlib;

using arithlib::mul;

main() : int {
    return mul(6, 7);           // resolves to arithlib::mul
}
```

**Alias with import:**

```k
module consumer;
import arithlib;

using sum = arithlib::add;

main() : int {
    return sum(10, 32);         // resolves to arithlib::add
}
```

**Namespace alias with import:**

```k
module consumer;
import very_long_module_name;

using namespace m = very_long_module_name;

main() : int {
    return m::compute(21);      // resolves to very_long_module_name::compute
}
```

Aliases are purely local to the consumer module — they are never exported
to the `.kdi` file.

---

## 9. Name lookup priority

Using directives are consulted **after** direct members of the current scope
and **before** the parent scope:

1. Local variables and function parameters
2. Direct members of the current scope (namespace, struct, block)
3. **Using directives at the current scope level**
4. Parent scope (recurse to step 1)
5. Imported modules (fully qualified fallback)

If two using directives inject the same name, the first match wins
(future: ambiguity error).

---

## 10. Restrictions

- A `using` directive does **not** create a new symbol.  It only affects
  name lookup.
- Aliases are **never exported**.  The `.kdi` file and mangled names always
  use the real (de-aliased) names.
- A `using namespace` directive without an alias injects all members of the
  target.  A `using namespace` directive **with** an alias does **not** inject
  members — they must be accessed via the alias prefix.
- Type-filter keywords (`struct`, `interface`, `class`) may be used to constrain
  the directive, but this is not currently enforced beyond `namespace`.
- `using` directives are not transitive: a `using` in namespace `A` does not
  affect lookup in namespace `B`, even if `B` uses `A`.

---

*See also:* [Names and Lookup](names.md) · [Module System](modules.md) · [Libraries — Export and Import](libraries.md)

