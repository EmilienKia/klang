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

A character literal represents a single character as a `char` (8-bit signed integer).

### Grammar

```
CharacterLiteral:
    "'" CharacterValue "'"

CharacterValue:
    InputCharacter but not "'" or '\'
    | EscapeSequence
```

### Escape sequences

| Sequence | Character |
|----------|-----------|
| `\n`     | newline (LF, 0x0A) |
| `\r`     | carriage return (CR, 0x0D) |
| `\t`     | horizontal tab (HT, 0x09) |
| `\\`     | backslash |
| `\'`     | single quote |
| `\"`     | double quote |
| `\0`     | null character (0x00) |
| `\xNN`   | hex escape (byte value NN) |
| `\NNN`   | octal escape |

### Examples

```k
c : char = 'A';
nl : char = '\n';
```

---

## 5. String literals

A string literal is a sequence of characters enclosed in double quotes.
It is represented as a pointer to a null-terminated character array.

### Grammar

```
StringLiteral:
    '"' { StringCharacter } '"'

StringCharacter:
    InputCharacter but not '"' or '\'
    | EscapeSequence
```

### Examples

```k
s : char* = "hello";
msg : char* = "line1\nline2";
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
