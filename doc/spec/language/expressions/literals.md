# Literals

[← Index](../index.md) · [Expressions](expressions.md)

A *literal* is the source-code representation of a fixed value.

---

## Contents

1. [Integer literals](#1-integer-literals)
2. [Floating-point literals](#2-floating-point-literals)
3. [Boolean literals](#3-boolean-literals)
4. [Character literals](#4-character-literals)
5. [String literals](#5-string-literals)
6. [Null literal](#6-null-literal)

---

## 1. Integer literals

Integer literals represent integer constant values.
They may be written in decimal, hexadecimal, octal, or binary notation.

### Grammar

```
IntegerLiteral:
    DecimalLiteral [ IntegerSuffix ]
    | HexLiteral [ IntegerSuffix ]
    | OctalLiteral [ IntegerSuffix ]
    | BinaryLiteral [ IntegerSuffix ]

DecimalLiteral:
    NonZeroDigit { Digit }
    | '0'

HexLiteral:
    '0x' HexDigit { HexDigit }
    | '0X' HexDigit { HexDigit }

OctalLiteral:
    '0' OctalDigit { OctalDigit }

BinaryLiteral:
    '0b' BinaryDigit { BinaryDigit }
    | '0B' BinaryDigit { BinaryDigit }

IntegerSuffix:
    'u'                  -- unsigned int (32-bit)
    | 'l'               -- signed long (64-bit)
    | 'ul' | 'lu'       -- unsigned long (64-bit)
    | 's'               -- signed short (16-bit)
    | 'b'               -- signed byte (8-bit)
```

The default type of an unsuffixed integer literal is `int` (32-bit signed).

### Type of integer literals

| Suffix | Type |
|--------|------|
| (none) | `int` |
| `u`    | `unsigned int` |
| `s`    | `short` |
| `l`    | `long` |
| `ul`/`lu` | `unsigned long` |
| `b`    | `byte` |

### Examples

```k
42          // int decimal
0xFF        // int hexadecimal (255)
0b1010      // int binary (10)
0777        // int octal (511)
100u        // unsigned int
10s         // short
64l         // long
255ul       // unsigned long
```

---

## 2. Floating-point literals

Floating-point literals represent IEEE 754 values.

### Grammar

```
FloatLiteral:
    FloatContent [ FloatSuffix ]

FloatContent:
    Digit { Digit } '.' { Digit } [ Exponent ]
    | '.' Digit { Digit } [ Exponent ]
    | Digit { Digit } Exponent

Exponent:
    ( 'e' | 'E' ) [ '+' | '-' ] Digit { Digit }

FloatSuffix:
    'f'   -- float (32-bit)
    | 'd' -- double (64-bit)
```

The default type of an unsuffixed floating-point literal is `float` (32-bit).
Use the `d` suffix for a `double` (64-bit) literal.

### Examples

```k
3.14          // float
3.14d         // double
1.0e10        // float, scientific notation
1.0e10d       // double, scientific notation
.5            // float (0.5)
2.0d          // double literal (important: int 2 would not match a double parameter without this)
```

---

## 3. Boolean literals

### Grammar

```
BooleanLiteral:
    'true'
    | 'false'
```

`true` and `false` are the two values of type `bool`.

### Examples

```k
flag : bool = true;
done : bool = false;
```

---

## 4. Character literals

A character literal represents a single character constant of type `char`
(8-bit signed integer, range −128 to 127).

At the LLVM IR level a character literal compiles to a constant `i8` value.

> **Current limitation:** Only ASCII character values (0x00–0x7F) are
> supported. Full escape-sequence decoding and Unicode support will be added
> in a future language revision.

### Grammar

```
CharacterLiteral:
    "'" CharacterValue "'"

CharacterValue:
    AsciiCharacter but not "'" or '\'
    | EscapeSequence
```

### Escape sequences

| Sequence | Character | Value |
|----------|-----------|-------|
| `\n`     | newline (LF)             | 0x0A |
| `\r`     | carriage return (CR)     | 0x0D |
| `\t`     | horizontal tab (HT)      | 0x09 |
| `\\`     | backslash                | 0x5C |
| `\'`     | single quote             | 0x27 |
| `\"`     | double quote             | 0x22 |
| `\0`     | null character           | 0x00 |
| `\xNN`   | hex byte (1–2 hex digits) | NN  |
| `\NNN`   | octal byte (1–3 octal digits) | (value) |

### Type

A character literal has type `char` (8-bit signed integer). It is assignable
to or comparable with any `char` expression.

### Examples

```k
c   : char = 'A';           // 65
nul : char = '\0';          // null terminator
tab : char = '\t';          // horizontal tab (9)

// Comparison with a literal
if (c == 'Z') { ... }

// Appending to a StringBuilder
sb : k::StringBuilder = "Hello";
sb.append_char('!');        // "Hello!"

// Filling a buffer manually
buf : char[]! = new char[4u];
buf[0] = 'H';
buf[1] = 'i';
buf[2] = '\0';
```

---

## 5. String literals

A string literal is a sequence of ASCII characters enclosed in double quotes.
It produces a **static, pre-initialized, read-only** character array that is
allocated once at program load time and shared across uses of the same literal
in the same compilation unit.

> **Current limitation:** String literals are treated as raw ASCII byte
> sequences; escape-sequence decoding is not yet implemented. Full escape
> processing and Unicode support will be added in a future language revision.

### Grammar

```
StringLiteral:
    '"' { StringCharacter } '"'

StringCharacter:
    AsciiCharacter but not '"' or '\'
    | EscapeSequence
```

### Type

A string literal of *n* characters has type **`const char[N]&`**, where
`N = n + 1` — the extra byte is the implicit null terminator.

```
"hello"  →  const char[6]&   (size == 6: 'h','e','l','l','o','\0')
""       →  const char[1]&   (size == 1: '\0')
```

The `size` field of the underlying array struct equals `N` and therefore
**includes the null terminator**. To get only the text length, use `size - 1`.

The internal LLVM layout is `{ i32 size, [N x i8] data }`, identical to the
layout of every K sized array (`T[N]`).

### Deduplication

Identical string literals within the same compilation unit share a single
global constant. No storage is allocated twice for the same content.

### Implicit null terminator

Every string literal automatically includes a `\0` byte after the last
character. Consumers that interpret the array as a C-style null-terminated
string can therefore rely on this terminator being present.

### Passing to functions

A `const char[N]&` literal widens implicitly to `const char[]` (unsized
reference) wherever a `const char[]` parameter is expected. This is the
standard sized-to-unsized array widening conversion and has zero runtime cost.

```k
display(s: const char[]) { ... }   // parameter: const char[] (unsized)
display("hello");                   // OK — const char[6]& widens to const char[]
```

Non-const `char[]` parameters do **not** accept string literals because
literals are read-only.

### Constructing standard library types

String literals integrate directly with `k::String` and `k::StringBuilder`
through constructors that accept `const char[]`:

```k
// Variable initialisation with '= expr' syntax (implicit best-constructor match)
s  : k::String = "hello";
sb : k::StringBuilder = "world";

// Explicit constructor call syntax
s2 : k::String("hello world");

// As a function argument
greet(name: const k::String&) { ... }
greet(k::String("Alice"));

// Appending a literal to a builder
builder : k::StringBuilder;
builder.append("Hello").append(", ").append("world");
```

The constructors copy the literal content into a new heap-allocated buffer;
the static global literal is never modified.

### Examples

```k
// Reading the size (includes null terminator)
read_size(s: const char[]) : unsigned int { return s.size; }
read_size("hi");    // returns 3

// Subscript access
first(s: const char[]) : char { return s[0]; }
first("ABC");       // returns 'A'

// Building a String from a literal
name : k::String = "Alice";
assert(name.size() == 5u);      // text length, not array size

// Building and extending a StringBuilder
sb : k::StringBuilder("Hello");
sb.append_char(',').append(" world");
result : k::String(sb);         // "Hello, world"
```

---

## 6. Null literal

`null` represents the null pointer value.  It has a dedicated type (`null`) that
is distinct from every other type.  `null` is implicitly convertible to any
nullable indirection type: pointer (`T*`), pinned (`T^`), and owner (`T!`).

### Grammar

```
NullLiteral:
    'null'
```

### Type rules

| Context | Behaviour |
|---|---|
| Initialise `T*`, `T^`, `T!` | `null` is accepted; stored as a null pointer |
| Assign `T*`, `T!` | `null` is accepted; stored as a null pointer |
| Assign `T^` | **Compile-time error** — pinned cannot be re-assigned after initialisation |
| Compare with `==` / `!=` against any indirection (`T*`, `T~`, `T^`, `T!`) | Address comparison; result is `bool` |
| Boolean context (`if`, `while`, `&&`, `\|\|`, `!`) | `null` converts to `false` |
| Initialise/assign `T&` or `T~` | **Compile-time error** — references and links are non-null |

### Examples

```k
p : int* = null;
o : Foo! = null;
pin : Bar^ = null;

if (p == null) { /* ... */ }
```

---

*See also:* [Types](../basic/types.md) · [Expressions](expressions.md)
