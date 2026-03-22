# String Types

[← Standard Library](index.md)

**Module:** `k`  
**Source:** `libk/libk/src/string.k`  
**Auto-imported:** yes (the base library is injected automatically into every module)

This page documents the string-related types of the base standard library:
`CharHelpers`, `String`, and `StringBuilder`.

---

## Contents

1. [CharHelpers](#1-charhelpers)
2. [String](#2-string)
3. [StringBuilder](#3-stringbuilder)
4. [Usage with string and character literals](#4-usage-with-string-and-character-literals)

---

## 1. CharHelpers

```k
public final class CharHelpers
```

Static utility class providing low-level operations on `char[]` buffers.
All methods are `public static`. No instances are ever created.

The `src` and other read-only buffer parameters are typed `const char[]` so
that owners (`char[]!`), pointers (`char[]*`), links (`char[]+`), view
(`char[]?`), and raw references (`char[]`) can all be passed without
ownership transfer or an explicit cast. String literals (`const char[N]&`)
also widen to `const char[]` automatically.

### Static Methods

| Signature | Returns | Description |
|-----------|---------|-------------|
| `copy(dst: char[], dst_off: unsigned int, src: const char[], src_off: unsigned int, count: unsigned int)` | *(void)* | Copy `count` chars from `src[src_off..]` into `dst[dst_off..]`. Buffers must not overlap. |
| `equals(a: const char[], b: const char[], len: unsigned int) : bool` | `bool` | Return `true` if the first `len` characters of `a` and `b` are identical. |
| `fill(dst: char[], dst_off: unsigned int, c: char, count: unsigned int)` | *(void)* | Write `c` into `count` consecutive positions of `dst` starting at `dst_off`. |

### Example

```k
buf : char[]! = new char[8u];
CharHelpers::fill(buf, 0, 'x', 6u);
CharHelpers::fill(buf, 6, '\0', 2u);
// buf == "xxxxxx\0\0"
```

---

## 2. String

```k
const final class String
```

An **immutable**, heap-backed string. The content is stored in a
null-terminated owner buffer (`char[]!`); the `size` field holds the
character count **excluding** the null terminator.

Because the class is declared `const`, all methods are implicitly `const`
(they never modify the object).

### Constructors

| Signature | Description |
|-----------|-------------|
| `String()` | Default constructor — creates an empty string. |
| `String(buf: char[]!, sz: unsigned int)` | Takes **ownership** of a pre-allocated, null-terminated buffer. `sz` must equal the number of text characters (not counting `\0`). |
| `String(other: const String&)` | Copy constructor — allocates a new buffer and copies the content. |
| `String(sb: const StringBuilder&)` | Constructs from a `StringBuilder`, copying its content into a new buffer. |
| `String(src: const char[])` | Constructs from a `const char[]` — typically a string literal. Allocates a new buffer and copies `src.size` bytes (the null terminator is included; the internal `_size` is set to `src.size - 1`). See [§4](#4-usage-with-string-and-character-literals). |

### Const Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `size()` | `unsigned int` | Number of characters (excluding the null terminator `'\0'`). |
| `empty()` | `bool` | `true` when the string has zero characters. |
| `at(index: int)` | `char` | Character at position `index`. No bounds check — behaviour is undefined if `index < 0` or `index >= size()`. |
| `data()` | `char[]?` | Non-owning view to the internal null-terminated buffer. Valid only as long as the `String` object is alive. Returns `null` for an empty string. |

### Operators

| Operator | Returns | Description |
|----------|---------|-------------|
| `==(other: const String&)` | `bool` | Equality: same length and identical characters. |
| `!=(other: const String&)` | `bool` | Inequality. |
| `+(other: const String&)` | `StringBuilder` | Concatenation — returns a new `StringBuilder` containing the concatenated content. |

### Example

```k
// From a string literal
greeting : k::String = "Hello, world";
assert(greeting.size() == 12u);
assert(greeting.at(0) == 'H');

// Copy
copy : k::String(greeting);
assert(copy == greeting);

// From a StringBuilder
sb : k::StringBuilder("foo");
sb.append("bar");
s : k::String(sb);   // "foobar"
assert(s.size() == 6u);
```

---

## 3. StringBuilder

```k
class StringBuilder
```

A **mutable**, growable string builder backed by a single contiguous
heap-allocated buffer. The buffer is automatically resized when more capacity
is needed.

Most mutating methods return `StringBuilder&` to support **fluent chaining**:

```k
sb.append("Hello").append(", ").append_char('W').append("orld!");
```

### Constructors

| Signature | Description |
|-----------|-------------|
| `StringBuilder()` | Default constructor — creates an empty builder. |
| `StringBuilder(s: const String&)` | Constructs from a `String`, copying its content into a new buffer. |
| `StringBuilder(src: const char[])` | Constructs from a `const char[]` — typically a string literal. Allocates a new buffer and copies `src.size - 1` text characters (the null terminator from `src` is **not** counted in `_size` but the buffer is null-terminated). See [§4](#4-usage-with-string-and-character-literals). |
| `StringBuilder(other: const StringBuilder&)` | Copy constructor — allocates a new buffer and copies the content. |

### Const Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `size()` | `unsigned int` | Current number of characters. |
| `empty()` | `bool` | `true` when the builder has zero characters. |
| `char_at(index: int)` | `char&` | Reference to the character at position `index`. No bounds check. |
| `data()` | `char[]*` | Non-owning pointer to the internal null-terminated buffer. Valid only while this `StringBuilder` is alive and not reallocated. Returns `null` for an empty builder. |

### Mutating Methods

All mutating methods return `StringBuilder&` for fluent chaining.

| Method | Returns | Description |
|--------|---------|-------------|
| `append(s: const String&)` | `StringBuilder&` | Append the content of a `String`. |
| `append(src: const char[])` | `StringBuilder&` | Append a `const char[]` (e.g. a string literal). Appends `src.size - 1` characters; the source null terminator is not included in the builder content. |
| `append_char(c: char)` | `StringBuilder&` | Append a single character literal or `char` value. |
| `clear()` | `StringBuilder&` | Remove all content; capacity is retained. |

### Operators

| Operator | Returns | Description |
|----------|---------|-------------|
| `+=(s: const String&)` | `StringBuilder&` | Append in place (fluent). |
| `+(s: const String&)` | `StringBuilder` | Concatenation — returns a new `StringBuilder`. |

### Example

```k
// From a string literal
sb : k::StringBuilder = "Hello";
assert(sb.size() == 5u);

// Fluent append
sb.append(", ").append_char('W').append("orld!");
assert(sb.size() == 12u);

// Convert to String
result : k::String(sb);
assert(result == k::String("Hello, World!"));

// Build from scratch with literals
builder : k::StringBuilder;
builder.append("foo").append("bar").append_char('!');
// builder contains "foobar!"
```

---

## 4. Usage with string and character literals

K string and character literals are first-class sources for constructing and
populating `String` and `StringBuilder` objects.

### 4.1 Character literals

A character literal (e.g. `'A'`) has type `char` (8-bit signed integer) and
can be passed anywhere a `char` value is expected, including `append_char`:

```k
sb : k::StringBuilder = "Hello";
sb.append_char(',');
sb.append_char(' ');
sb.append_char('!');
// sb == "Hello, !"
```

### 4.2 String literals — type recap

A string literal of *n* characters has type `const char[N]&` with
`N = n + 1` (including the implicit null terminator). Its `size` field equals
`N` — to obtain the text length alone, compute `size - 1`:

```k
read_size(s: const char[]) : unsigned int { return s.size; }
read_size("hi");    // returns 3  (2 text chars + '\0')
read_size("");      // returns 1  ('\0' only)
```

String literals are **read-only static globals**. A `const char[N]&` widens
automatically to `const char[]` (unsized) when passed to a function that
expects `const char[]`; the conversion has zero runtime cost.

Passing a string literal to a non-`const` `char[]` parameter is a
**compile-time error** because literals are immutable.

### 4.3 Constructing String from a literal

Use either the `= expr` form (implicit best-constructor selection) or the
explicit constructor-call syntax:

```k
// Implicit '= expr' form
s1 : k::String = "Hello, world";
assert(s1.size() == 12u);

// Explicit constructor call
s2 : k::String("Hello, world");
assert(s1 == s2);

// Empty literal
empty : k::String = "";
assert(empty.empty());
assert(empty.size() == 0u);
```

The constructor copies the literal into a fresh heap buffer; the static
global literal is never mutated.

### 4.4 Constructing StringBuilder from a literal

```k
// Implicit '= expr' form
sb : k::StringBuilder = "Building";
assert(sb.size() == 8u);

// Explicit constructor call
sb2 : k::StringBuilder("Building");
```

### 4.5 Appending literals to a StringBuilder

`append(src: const char[])` appends `src.size - 1` characters (the source
null terminator is stripped from the appended content):

```k
sb : k::StringBuilder;
sb.append("Hello").append(", ").append("world").append_char('!');
assert(sb.size() == 13u);   // "Hello, world!"
```

Mixing `append` (for multi-character sequences) with `append_char` (for
individual characters) is a common pattern:

```k
sb : k::StringBuilder("Key");
sb.append_char(':').append_char(' ').append("value");
// "Key: value"
```

### 4.6 Equality comparison after literal construction

`String` objects built from literals can be compared with `==` and `!=`:

```k
s1 : k::String = "hello";
s2 : k::String = "hello";
assert(s1 == s2);

s3 : k::String = "world";
assert(s1 != s3);
```

### 4.7 Converting a StringBuilder back to String

Use the `String(sb: const StringBuilder&)` constructor to obtain an immutable
snapshot at any point:

```k
sb : k::StringBuilder("Hello");
sb.append_char('!');
snap : k::String(sb);
assert(snap == k::String("Hello!"));
assert(sb.size() == 6u);   // builder is unchanged
```

---

*See also:*
[Literals](../language/expressions/literals.md) ·
[Types](../language/basic/types.md) ·
[Object](object.md)
