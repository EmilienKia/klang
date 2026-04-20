# Enumerations

[← Index](../index.md) · [Keywords](../basic/keywords.md)

An **enumeration** (`enum`) is a named type representing a fixed set of
named integer constants.  Each constant is called an **entry**.  The compiler
automatically selects the smallest primitive integer type capable of holding
all declared values as the *underlying type*.

---

## 1. Declaration syntax

```
EnumDecl:
    { Specifier } 'enum' Identifier [ ':' TypeSpec ] '{' { EnumEntry } '}' ';'

EnumEntry:
    Identifier
    [ '=' ( IntegerLiteral | Identifier )
    | BraceInitList
    | '(' [ ExpressionList ] ')' ]
    [ 'default' ] ';'
```

### Example

```k
enum Color {
    RED = 0;
    GREEN = 1;
    BLUE = 2 default;
};
```

### Rules

| Rule | Description |
|------|-------------|
| **Explicit value** | An entry may be assigned an integer literal (`= 1`) or the name of a previously (or forward-) declared entry (`= RED`). |
| **Auto-increment** | When no value is specified, the entry takes the value of the preceding entry plus one.  The first entry defaults to `0`. |
| **Duplicate values** | Two entries may have the same numeric value; they are treated as aliases with no distinction. |
| **Default entry** | Appending `default` after the value marks the entry as the default.  At most **one** entry per enum may carry this attribute.  If no entry is explicitly marked, the first entry is the default. |
| **Typed enum clause** | `enum E : T` accepts either an enum base (`T` is an enum) or an explicit backing type (`T` is a primitive or aggregate type). |
| **Underlying type** | Integer enums use the smallest primitive integer type that can represent all values (unsigned if all values ≥ 0, signed otherwise), unless an explicit primitive type is provided. |

---

## 1.1 Typed enums (object-backed)

When `T` is an aggregate type (`struct`/`class`), the enum is **object-backed**:

- The enum runtime value is still an integer index.
- Each entry maps to one object in a static backing table.
- `E -> const T&` conversion returns a reference to the corresponding table element.

```k
struct Vec2 {
    x : int;
    y : int;
}

enum Dir : Vec2 {
    UP{.x = 0, .y = 1} default;
    RIGHT{.x = 1, .y = 0};
    DOWN;  // zero-init object
};
```

Entry initializer forms for object-backed enums:

- `NAME{ ... }` designated object initializer.
- `NAME(args...)` constructor-style initializer.
- `NAME()` default-construction form.
- `NAME` implicit entry value (derived from previous entry when possible, otherwise zero-init).

Additional conversion rules for object-backed enums:

- `E -> const T&` is always supported via backing-table lookup.
- `T -> E` is supported when equality is available for `T`.
- `T -> E` non-match is fatal outside soft-fail contexts.
- In `if (e : E = value)` condition-variable form, `T -> E` non-match follows
  soft-fail control flow and selects the `else` path.

---

## 2. Enum derivation

An enum may **derive** from another enum using single-inheritance syntax.
The derived enum inherits all entries from its base and may add new ones.

```k
enum Shape {
    CIRCLE = 0;
    SQUARE = 1;
};

enum ExtShape : Shape {
    TRIANGLE = 2;
    HEXAGON;          // auto-incremented from last base entry → 3 if no gap
};
```

### Rules

| Rule | Description |
|------|-------------|
| **Single inheritance** | Only one base enum is allowed (`enum D : B { … }`).  Multiple inheritance is not supported. |
| **Multi-level** | Derivation chains are supported: `enum C : B` where `enum B : A`.  `C` inherits entries from both `A` and `B`. |
| **Entry visibility** | All entries from the base are accessible via the derived name: `ExtShape::CIRCLE` works. |
| **Auto-increment** | The first entry without an explicit value in the derived enum continues from the last entry value of the base (base + local entries form a single sequence). |
| **References** | A new entry may reference a base entry by name: `ALIAS = CIRCLE;`. |
| **Default inheritance** | If the derived enum does not declare a new `default`, the base's default is inherited.  If the derived declares a `default`, it overrides the inherited one. |
| **Name shadowing** | A derived entry may have the same name as a base entry (a compiler **warning** is emitted).  The derived entry takes precedence for the derived enum's entry list. |
| **Cycle detection** | Circular derivation (`A : B`, `B : A`) is detected and rejected with an error. |
| **Declaration order** | The base enum does not need to be declared before the derived enum — the compiler resolves forward references. |
| **Underlying type** | The underlying type of the derived enum is the smallest type that fits **all** entries (inherited + local). |
| **Typed derivation** | If the base enum is object-backed, the derived enum inherits the same underlying object type. |

### Conversion rules for derived enums

| Source | Target | Allowed | Notes |
|--------|--------|---------|-------|
| `Derived` | `Base` | ✔ (widening) | Upcast: implicit, always safe |
| `Base` | `Derived` | ✘ (error) | Downcast: not allowed |
| Unrelated enums | | ✘ (error) | No implicit conversion between unrelated enums |

---

## 3. Usage

### 3.1 Qualified access

```k
c : Color = Color::BLUE;
d : ExtShape = ExtShape::CIRCLE;   // inherited entry
```

The fully qualified form `EnumName::entryName` is always available and
follows the standard symbol lookup / scope resolution rules.

### 3.2 Constructor-style initialization

```k
c : Color(GREEN);     // entry name resolved relative to Color
c : Color(2);         // construction from numeric value
c : Color;            // default construction → uses the default entry
```

When using the constructor form `EnumName(arg)`:

- If `arg` is an unqualified identifier, it is resolved as an entry name
  **relative to the enum** (no scope lookup for variables or functions).
  For derived enums, inherited entry names are also searched.
- If `arg` is a numeric literal or an already-resolved expression, it is
  implicitly converted to the enum type.
- If no argument is given, the default entry value is used.

### 3.3 Assignment from numeric value

```k
c : Color = 2;        // implicit int → Color conversion
```

---

## 4. Implicit conversions

| Source | Target | Weight | Notes |
|--------|--------|--------|-------|
| `E` | same `E` | identity | No conversion needed |
| `Derived` | `Base` | widening | Upcast (only for derived→base relationship) |
| `E` | primitive int | widening | Implicit |
| primitive int | `E` | widening | Implicit |
| `ref<E>` | `E` | ref-load | Load + identity |
| `ref<E>` | primitive int | ref-load + widening | Load then convert |
| `T` (typed enum object type) | `E` | widening | Runtime table lookup; fatal on non-match except soft-fail in `if` cond-var form |
| `ref<T>` (typed enum object type) | `E` | widening | Runtime table lookup on loaded value |

> **Note:** Implicit conversions between unrelated enums are **not** allowed.
> Only `Derived → Base` (upcast) is permitted.

---

## 5. Comparison operators

All six comparison operators (`==`, `!=`, `<`, `>`, `<=`, `>=`) are
supported on enum values.  Both operands are implicitly converted to
the wider underlying primitive type before the comparison is performed.

```k
if (c == Color::RED) { /* ... */ }
if (c < Color::BLUE) { /* ... */ }
if (c == 1) { /* ... */ }
```

---

## 6. Underlying type selection

| All values ≥ 0 | Range | Underlying type |
|:-:|-------------|-----------------|
| ✔ | 0 … 255 | `byte` (unsigned 8-bit) |
| ✔ | 0 … 65 535 | `unsigned short` |
| ✔ | 0 … 4 294 967 295 | `unsigned int` |
| ✔ | 0 … 2⁶⁴−1 | `unsigned long` |
| ✘ | −128 … 127 | `char` (signed 8-bit) |
| ✘ | −32 768 … 32 767 | `short` |
| ✘ | −2³¹ … 2³¹−1 | `int` |
| ✘ | everything else | `long` |

For derived enums, the range includes **all** entries (inherited + local).

---

## 7. LLVM IR representation

An enum value is represented as a plain integer of the underlying type.
There is no distinct LLVM type — the enum_type is a compiler-level
abstraction.  Casts between enum and integer are either no-ops (same bit
width) or integer truncation / extension.

Derived enums are represented identically to their base — the derivation
relationship exists only at the compiler's type-system level.

For object-backed enums:

- The enum value remains an integer index type.
- A static global table stores the concrete object values for entries.
- `E -> const T&` computes a pointer into that table.
- `T -> E` performs a runtime table scan to recover the matching index.

---

## 8. Future extensions

The following features are **not** currently supported but are planned
for future versions:

- **Methods on enums**: defining member functions associated with an enum
  type.
- **`switch` / pattern matching**: exhaustive matching on enum entries.

---

*See also:* [Keywords](../basic/keywords.md) · [Types](../basic/types.md) · [Statements](../statements/statements.md)

