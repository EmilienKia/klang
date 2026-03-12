# Uniform Array Initialization

[← Index](../index.md) · [Dynamic Allocation — `new` and `delete`](new-delete.md) · [Array Types](../basic/types.md#9-array-types)

Uniform array initialization allows **all elements** of an array — stack-allocated or
heap-allocated — to be initialized with the **same constructor arguments**.  Instead of
listing individual values in a brace initializer, a single constructor call is specified
and applied to every element.

---

## Contents

1. [Syntax](#1-syntax)
2. [Stack-allocated arrays](#2-stack-allocated-arrays)
3. [Heap-allocated arrays (`new`)](#3-heap-allocated-arrays-new)
4. [Semantics](#4-semantics)
5. [Interaction with existing forms](#5-interaction-with-existing-forms)
6. [Grammar changes](#6-grammar-changes)
7. [Error and warning codes](#7-error-and-warning-codes)

---

## 1. Syntax

**Stack-allocated array:**

```
Name ':' TypeName '(' [ ExpressionList ] ')' '[' Expression ']' ';'
```

**Heap-allocated array:**

```
'new' TypeName '(' [ ExpressionList ] ')' '[' Expression ']'
```

In both forms:
- `TypeName` is the element type (primitive, struct, or class — not abstract).
- `( ExpressionList )` are the **constructor arguments** applied to every element.
- `[ Expression ]` is the **array size** — a compile-time integer literal (static) or a
  runtime expression (dynamic, heap only).

---

## 2. Stack-allocated arrays

**Examples:**

```k
// Every element initialized with value 42
arr : int(42)[5];                 // int[5], all elements = 42

// Every struct element constructed with the same arguments
items : Point(3, 4)[10];         // Point[10], each Point(3, 4)

// Default constructor for all elements (no arguments)
zeros : int(0)[100];             // int[100], all elements = 0
things : Widget()[8];            // Widget[8], each default-constructed
```

Stack arrays always have a **compile-time constant** size (integer literal).

**Return type:** The variable has type `T[N]` (sized array, same as existing stack arrays).

**Comparison with existing brace init:**

| Form | Meaning |
|------|---------|
| `arr : int[3]{1, 2, 3};` | Each element gets a different value |
| `arr : int(42)[3];` | All elements get the same value (42) |
| `items : Point[2]{Point(1,2), Point(3,4)};` | Different ctor args per element |
| `items : Point(1, 2)[2];` | Same ctor args for all elements |

---

## 3. Heap-allocated arrays (`new`)

**Examples:**

```k
// Static-sized: all elements initialized with value 42
arr : int[5]! = new int(42)[5];

// Dynamic-sized: all elements constructed with same args
n : unsigned int = get_count();
items : Point[]! = new Point(3, 4)[n];

// Default constructor for all (equivalent to existing new T[n])
widgets : Widget[]! = new Widget()[n];
```

**Return type:**

| Form | Return type |
|------|-------------|
| `new T(args)[N]` (N = compile-time constant) | `T[N]!` |
| `new T(args)[expr]` (expr = runtime expression) | `T[]!` |

---

## 4. Semantics

### 4.1 Construction order

Elements are constructed **in order** from index 0 to index N−1.  The constructor argument
expressions are **evaluated once per element** — this allows side effects (e.g., calling a
function that returns different values each time).

For simple constant arguments (literals), the compiler may optimize by evaluating once and
replicating, but the observable semantics must remain as if evaluated per-element.

### 4.2 Primitive types

For primitive element types, the "constructor argument" is simply the value to store:

```k
arr : int(42)[5];          // each arr[i] = 42
arr : float(3.14)[3];     // each arr[i] = 3.14f
```

The value expression must be implicitly convertible to the element type.

### 4.3 Struct / class types

For struct or class element types, the constructor matching the provided arguments is
resolved at compile time:

```k
struct Point {
    x : int; y : int;
    Point(a : int, b : int) { x = a; y = b; }
}

pts : Point(1, 2)[5];     // calls Point(1, 2) for each of the 5 elements
```

The element type must not be abstract.  A matching constructor must exist.

### 4.4 Destruction

On `delete` (heap) or scope exit (stack), destructors are called in **reverse order**
(last element first), exactly as for brace-initialized arrays.

### 4.5 Memory layout

The memory layout is identical to existing arrays:

```
{ uint32 count; T[N] data; }
```

For heap dynamic-sized arrays: `{ uint32 count; T[0] data; }` (data extends past the struct).

---

## 5. Interaction with existing forms

### No ambiguity with single-object `new`

The existing `new T(args)` allocates a **single object**.  The new `new T(args)[N]` allocates
an **array**.  The presence of `[N]` after the `)` unambiguously distinguishes the two forms:

| Form | Meaning |
|------|---------|
| `new Point(1, 2)` | Single Point object → `Point!` |
| `new Point(1, 2)[5]` | Array of 5 Points → `Point[5]!` |

### No ambiguity with variable constructor init

The existing `var : T(args);` initializes a **single variable** via constructor.  The new
`var : T(args)[N];` initializes an **array**.  The presence of `[N]` after the `)` is the
disambiguator:

| Form | Meaning |
|------|---------|
| `p : Point(1, 2);` | Single Point variable |
| `pts : Point(1, 2)[5];` | Array of 5 Points |

### Coexistence with brace init

Both forms remain available.  Brace init provides per-element values; uniform init provides
a single value for all elements:

```k
a : int[3]{1, 2, 3};      // per-element: [1, 2, 3]
b : int(42)[3];            // uniform:    [42, 42, 42]
```

---

## 6. Grammar changes

### VariableDecl (updated)

```
VariableDecl:
    { Specifier } Identifier ':' TypeSpec [ Initialiser ] ';'

Initialiser:
    '=' ConditionalExpr
    | '(' [ ExpressionList ] ')'                                -- constructor init
    | '(' [ ExpressionList ] ')' '[' Expression ']'             -- uniform array init (NEW)
    | BraceInitList
```

The parser disambiguates: after parsing `( args )`, if the next token is `[`, it is a
uniform array init; otherwise, it is a single-variable constructor init.

### UnaryExpr (updated — new expression)

```
UnaryExpr:
    'new' TypeName '(' [ ExpressionList ] ')'                        -- NewExpr (single object)
    'new' TypeName '(' [ ExpressionList ] ')' '[' Expression ']'     -- NewUniformArrayExpr (NEW)
    | 'new' TypeName '[' [ Expression ] ']' [ BraceInitList ]        -- NewArrayExpr
    | 'new' TypeName BraceInitList                                   -- NewBareArrayExpr
    | 'delete' CastExpr                                              -- DeleteExpr
    | ...
```

The parser disambiguates: after parsing `new TypeName ( args )`, if the next token is `[`,
it is a uniform array allocation; otherwise, it is a single-object allocation.

---

## 7. Error and warning codes

| Code | Severity | Condition |
|------|----------|-----------|
| 0x4230 | Error | Uniform array init: element type is abstract. |
| 0x4231 | Error | Uniform array init: no matching constructor for the provided arguments. |
| 0x4232 | Error | Uniform array init: cannot convert value to primitive element type. |
| 0x4233 | Error | Uniform array init: array size must be a compile-time constant (stack) or convertible to `unsigned int` (heap). |
| 0x4234 | Error | Uniform array init: array size must be non-negative. |

---

*See also:* [Dynamic Allocation — `new` and `delete`](new-delete.md) · [Array Types](../basic/types.md#9-array-types) · [Constructors](../structs/constructors.md)

