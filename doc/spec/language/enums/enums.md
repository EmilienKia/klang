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
    { Specifier } 'enum' Identifier '{' { EnumEntry } '}' ';'

EnumEntry:
    Identifier [ '=' ( IntegerLiteral | Identifier ) ] [ 'default' ] ';'
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
| **Underlying type** | The compiler picks the smallest primitive integer type that can represent all values (unsigned if all values ≥ 0, signed otherwise). |

---

## 2. Usage

### 2.1 Qualified access

```k
c : Color = Color::BLUE;
```

The fully qualified form `EnumName::entryName` is always available and
follows the standard symbol lookup / scope resolution rules.

### 2.2 Constructor-style initialization

```k
c : Color(GREEN);     // entry name resolved relative to Color
c : Color(2);         // construction from numeric value
c : Color;            // default construction → uses the default entry
```

When using the constructor form `EnumName(arg)`:

- If `arg` is an unqualified identifier, it is resolved as an entry name
  **relative to the enum** (no scope lookup for variables or functions).
- If `arg` is a numeric literal or an already-resolved expression, it is
  implicitly converted to the enum type.
- If no argument is given, the default entry value is used.

### 2.3 Assignment from numeric value

```k
c : Color = 2;        // implicit int → Color conversion
```

---

## 3. Implicit conversions

| Source | Target | Weight | Notes |
|--------|--------|--------|-------|
| `E` | same `E` | identity | No conversion needed |
| `E` | different `F` | widening | Allowed; a warning may be emitted |
| `E` | primitive int | widening | Implicit |
| primitive int | `E` | widening | Implicit |
| `ref<E>` | `E` | ref-load | Load + identity |
| `ref<E>` | primitive int | ref-load + widening | Load then convert |

---

## 4. Comparison operators

All six comparison operators (`==`, `!=`, `<`, `>`, `<=`, `>=`) are
supported on enum values.  Both operands are implicitly converted to
the wider underlying primitive type before the comparison is performed.

```k
if (c == Color::RED) { /* ... */ }
if (c < Color::BLUE) { /* ... */ }
if (c == 1) { /* ... */ }
```

---

## 5. Underlying type selection

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

---

## 6. LLVM IR representation

An enum value is represented as a plain integer of the underlying type.
There is no distinct LLVM type — the enum_type is a compiler-level
abstraction.  Casts between enum and integer are either no-ops (same bit
width) or integer truncation / extension.

---

## 7. Future extensions

The following features are **not** currently supported but are planned
for future versions:

- **Enum inheritance / extension**: extending an existing enum with new
  entries.
- **Non-numeric backing types**: using types other than primitive integers
  as the underlying representation.
- **Methods on enums**: defining member functions associated with an enum
  type.
- **`switch` / pattern matching**: exhaustive matching on enum entries.

---

*See also:* [Keywords](../basic/keywords.md) · [Types](../basic/types.md) · [Statements](../statements/statements.md)

