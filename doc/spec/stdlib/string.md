# String Types

[← Standard Library](index.md)

**Module:** `k`  
**Source:** `libk/libk/src/string.k`  
**Auto-imported:** yes (the base library is injected automatically into every module)

This page documents the string-related types of the base standard library:
`CharHelpers`, `String`, and `StringBuilder`.

> **Note:** `char` is a 32-bit **unsigned** Unicode scalar value (UTF-32), so a
> `char[]` is a sequence of Unicode code points. The `Encoding` enum
> (`UTF8`, `UTF16`, `UTF32`) describes the encoding used by the array-conversion
> helpers. Raw byte buffers use `unsigned byte[]` and UTF-16 code-unit buffers
> use `unsigned short[]`.

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
| `copy(dst, dstOff, src, srcOff, count)` | *(void)* | Copy `count` chars from `src[srcOff..]` into `dst[dstOff..]`. Buffers must not overlap. |
| `copyBackward(dst, dstOff, src, srcOff, count)` | *(void)* | Copy `count` chars in reverse order — safe for overlapping regions where `dstOff > srcOff`. |
| `equals(a, b, len) : bool` | `bool` | Return `true` if the first `len` characters of `a` and `b` are identical. |
| `equalsAt(a, aOff, b, bOff, len) : bool` | `bool` | Compare `len` chars at the given offsets. |
| `fill(dst, dstOff, c, count)` | *(void)* | Write `c` into `count` consecutive positions of `dst` starting at `dstOff`. |
| `findChar(buf, off, len, c) : int` | `int` | First occurrence of `c` in `buf[off..off+len)`. Returns index or `-1`. |
| `rfindChar(buf, off, len, c) : int` | `int` | Last occurrence of `c` in `buf[off..off+len)`. Returns index or `-1`. |
| `find(haystack, hOff, hLen, needle, nOff, nLen) : int` | `int` | First occurrence of `needle[nOff..nOff+nLen)` in `haystack[hOff..hOff+hLen)`. Returns index or `-1`. |
| `rfind(haystack, hOff, hLen, needle, nOff, nLen) : int` | `int` | Last occurrence of `needle` in `haystack`. Returns index or `-1`. |
| `compare(a, aOff, b, bOff, len) : int` | `int` | Lexicographic compare of `len` chars. Returns `<0`, `0`, or `>0`. |
| `isWhitespace(c) : bool` | `bool` | `true` if `c` is a space, tab, newline, or carriage return. |
| `utf8Len(c) : unsigned int` | `unsigned int` | UTF-8 byte count (1–4) for code point `c`. |
| `utf16Len(c) : unsigned int` | `unsigned int` | UTF-16 code-unit count (1 or 2) for code point `c`. |
| `utf8Size(src, len) : unsigned int` | `unsigned int` | Total UTF-8 byte count for the first `len` code points of `src`. |
| `utf16Size(src, len) : unsigned int` | `unsigned int` | Total UTF-16 code-unit count for the first `len` code points of `src`. |
| `toUtf8(src, len) : unsigned byte[]!` | `unsigned byte[]!` | Encode `len` UTF-32 code points to a new null-terminated UTF-8 buffer. |
| `toUtf16(src, len) : unsigned short[]!` | `unsigned short[]!` | Encode `len` UTF-32 code points to a new null-terminated UTF-16 buffer. |
| `utf8CodePointCount(src, len) : unsigned int` | `unsigned int` | Number of code points in the first `len` bytes of UTF-8 `src`. |
| `utf16CodePointCount(src, len) : unsigned int` | `unsigned int` | Number of code points in the first `len` UTF-16 code units. |
| `utf8ToUtf32(src, len) : char[]!` | `char[]!` | Decode `len` UTF-8 bytes to a new null-terminated UTF-32 `char[]`. |
| `utf16ToUtf32(src, len) : char[]!` | `char[]!` | Decode `len` UTF-16 code units to a new null-terminated UTF-32 `char[]`. |

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

An **immutable**, heap-backed string. The content is stored compactly in one
of three Unicode encodings (UTF-8, UTF-16, or UTF-32) chosen at construction
from the argument type; the storage encoding is reported by `rawEncoding()`.
The public API is always **code-point (`char`) based** regardless of the
underlying storage. `size()` is the number of code points.

Because the class is declared `const`, all methods are implicitly `const`
(they never modify the object).

### Constructors

| Signature | Description |
|-----------|-------------|
| `String()` | Default constructor — creates an empty string. |
| `String(buf: char[]!, sz: unsigned int)` | Takes **ownership** of a pre-allocated, null-terminated buffer. `sz` must equal the number of text characters (not counting `\0`). |
| `String(other: const String&)` | Copy constructor — allocates a new buffer and copies the content. |
| `String(other: String#)` | Drain constructor — steals the buffer from `other`, leaving it empty. |
| `String(sb: const StringBuilder&)` | Constructs from a `StringBuilder`, copying its content into a new buffer. |
| `String(src: const char[])` | Constructs from a `const char[]` — typically a string literal. See [§4](#4-usage-with-string-and-character-literals). |
| `String(src: const unsigned byte[])` | Constructs from a UTF-8 byte buffer (e.g. a `u8"…"` literal), stored natively as UTF-8. |
| `String(src: const unsigned short[])` | Constructs from a UTF-16 code-unit buffer (e.g. a `u"…"` literal), stored natively as UTF-16. |
| `String(src: const char[], enc: Encoding)` | Constructs from a UTF-32 `char[]`, transcoded to the requested storage encoding `enc`. |

### Accessors

| Method | Returns | Description |
|--------|---------|-------------|
| `size()` | `unsigned int` | Number of characters (code points). |
| `empty()` | `bool` | `true` when the string has zero characters. |
| `at(index: unsigned int)` | `char` | Code point at position `index`. No bounds check. O(index) for UTF-8/16 storage, O(1) for UTF-32. |
| `operator [](index: unsigned int)` | `char` | Read-only subscript — same as `at(index)`. |
| `rawEncoding()` | `Encoding` | Underlying storage encoding (`UTF8`, `UTF16` or `UTF32`). An empty string reports `UTF32`. |
| `toUtf32()` | `char[]!` | New null-terminated UTF-32 copy of the content. |
| `toUtf8()` | `unsigned byte[]!` | New null-terminated UTF-8 encoding of the content. |
| `toUtf16()` | `unsigned short[]!` | New null-terminated UTF-16 encoding of the content. |

### Search Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `find(c: char)` | `int` | First index of character `c`, or `-1`. |
| `find(needle: const String&)` | `int` | First index of substring `needle`, or `-1`. |
| `rfind(c: char)` | `int` | Last index of character `c`, or `-1`. |
| `rfind(needle: const String&)` | `int` | Last index of substring `needle`, or `-1`. |
| `contains(c: char)` | `bool` | `true` if the string contains `c`. |
| `contains(needle: const String&)` | `bool` | `true` if the string contains `needle`. |
| `indexOf(c: char)` | `int` | Alias for `find(c)`. |
| `indexOf(needle: const String&)` | `int` | Alias for `find(needle)`. |
| `count(c: char)` | `unsigned int` | Number of occurrences of character `c`. |
| `count(needle: const String&)` | `unsigned int` | Number of non-overlapping occurrences of `needle`. Returns `0` for an empty needle. |
| `beginsWith(prefix: const String&)` | `bool` | `true` if this string starts with `prefix`. |
| `endsWith(suffix: const String&)` | `bool` | `true` if this string ends with `suffix`. |

### Extraction Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `substr(start: unsigned int, len: unsigned int)` | `String` | New string with `len` chars starting at `start`. Truncated if out-of-range. |
| `first(count: unsigned int)` | `String` | First `count` characters. |
| `last(count: unsigned int)` | `String` | Last `count` characters. |

### Comparison

| Method | Returns | Description |
|--------|---------|-------------|
| `compareTo(other: const String&)` | `int` | Lexicographic comparison: `<0`, `0`, or `>0`. |

### Operators

| Operator | Returns | Description |
|----------|---------|-------------|
| `==(other: const String&)` | `bool` | Equality: same length and identical characters. |
| `!=(other: const String&)` | `bool` | Inequality. |
| `<(other: const String&)` | `bool` | Lexicographically less than. |
| `<=(other: const String&)` | `bool` | Lexicographically less than or equal. |
| `>(other: const String&)` | `bool` | Lexicographically greater than. |
| `>=(other: const String&)` | `bool` | Lexicographically greater than or equal. |
| `+(other: const String&)` | `StringBuilder` | Concatenation — returns a new `StringBuilder`. |

### Example

```k
s : k::String("Hello, world");
assert(s.size() == 12u);
assert(s.at(0u) == 'H');
assert(s.find('o') == 4);
assert(s.contains('w'));
assert(s.beginsWith(k::String("Hello")));
assert(s.endsWith(k::String("world")));

sub : k::String = s.substr(7u, 5u);  // "world"
assert(sub == k::String("world"));

f : k::String = s.first(5u);  // "Hello"
l : k::String = s.last(5u);   // "world"

a : k::String("abc");
b : k::String("abd");
assert(a < b);
assert(a.compareTo(b) < 0);
```

---

## 3. StringBuilder

```k
class StringBuilder
```

A **mutable**, growable string builder with **heterogeneous, multi-encoding
fragment storage**. Each appended fragment is kept in its **native encoding**
(UTF-8, UTF-16 or UTF-32) with no transcoding on append — three growable pools
(`unsigned byte[]`, `unsigned short[]`, `char[]`) hold the raw units and four
parallel metadata arrays describe each fragment in logical order. The public
API is **code-point (`char`) based**; `size()` counts code points.

Random reads decode the target fragment on demand; bulk reads decode all
fragments to UTF-32. Any in-place **mutation** (`set`, `remove`, `insert`,
`reverse`, `trim`, `replace`, …) first **consolidates** all fragments into a
single contiguous UTF-32 fragment, so mutation logic always works on a
contiguous buffer.

Most mutating methods return `StringBuilder&` to support **fluent chaining**:

```k
sb.append("Hello").append(", ").appendChar('W').append("orld!");
```

### Constructors

| Signature | Description |
|-----------|-------------|
| `StringBuilder()` | Default constructor — creates an empty builder. |
| `StringBuilder(s: const String&)` | Constructs from a `String`, mirroring its native storage encoding. |
| `StringBuilder(src: const char[])` | Constructs from a `const char[]` (e.g. a string literal) — stored as UTF-32. |
| `StringBuilder(src: const unsigned byte[])` | Constructs from a UTF-8 buffer (e.g. a `u8"…"` literal) — stored natively as UTF-8. |
| `StringBuilder(src: const unsigned short[])` | Constructs from a UTF-16 buffer (e.g. a `u"…"` literal) — stored natively as UTF-16. |
| `StringBuilder(other: const StringBuilder&)` | Copy constructor — deep copies all pools and fragment metadata. |
| `StringBuilder(other: StringBuilder#)` | Drain constructor — steals the buffers from `other`, leaving it empty. |

### Accessors

| Method | Returns | Description |
|--------|---------|-------------|
| `size()` | `unsigned int` | Current number of characters (code points). |
| `empty()` | `bool` | `true` when the builder has zero characters. |
| `charAt(index: unsigned int)` | `char` | Code point at position `index`. Decodes the target fragment — O(fragCount + intra-fragment offset). No bounds check. |
| `at(index: unsigned int)` (const) | `char` | Read-only code point at `index`. |
| `operator [](index: unsigned int)` (const) | `char` | Read-only subscript — same as `at(index)`. |
| `at(index: unsigned int)` (non-const) | `CharRef` | Mutable element proxy (see below). |
| `operator [](index: unsigned int)` (non-const) | `CharRef` | Mutable element proxy — supports `sb[i] = c`. |
| `rawEncoding()` | `Encoding` | Native encoding of the **first** fragment, or `UTF32` if empty. (A builder may hold fragments of mixed encodings.) |
| `copyTo(dst: char[], dstOff: unsigned int)` | *(void)* | Decode all fragment content (as UTF-32 code points) into `dst` starting at `dstOff`. |

> **`CharRef` write-proxy** — the non-const `at()` / `operator[]` return a
> `CharRef` that holds a link to the builder and a logical index. `sb[i] = c`
> stores the code point `c` (equivalent to `set(i, c)`). For reading a single
> code point use `charAt(i)` (always returns `char`). A `CharRef` is invalidated
> by any structural mutation of the builder.

### Conversion

| Method | Returns | Description |
|--------|---------|-------------|
| `operator() : String` | `String` | Conversion operator — builds an immutable `String` using the default (`MostCompact`) consolidation policy. |
| `toString()` | `String` | Builds an immutable `String` using the default (`MostCompact`) policy. |
| `toString(policy: ConsolidationPolicy)` | `String` | Builds an immutable `String`, choosing the storage encoding per `policy`. |
| `toString(enc: Encoding)` | `String` | Builds an immutable `String` stored in the given encoding `enc`. |
| `rawEncoding()` | `Encoding` | First-fragment encoding (see Accessors). |
| `toUtf32()` | `char[]!` | New null-terminated UTF-32 copy of the content. |
| `toUtf8()` | `unsigned byte[]!` | New null-terminated UTF-8 encoding of the content. |
| `toUtf16()` | `unsigned short[]!` | New null-terminated UTF-16 encoding of the content. |

**`ConsolidationPolicy`** (chooses the storage encoding of the produced `String`):

| Value | Description |
|-------|-------------|
| `MostCompact` | Pick the encoding with the smallest byte footprint (default). |
| `FirstFragment` | Use the encoding of the builder's first fragment. |
| `ForceUtf8` / `ForceUtf16` / `ForceUtf32` | Always use the named encoding. |

### Search Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `find(c: char)` | `int` | First index of character `c`, or `-1`. |
| `find(needle: const String&)` | `int` | First index of substring `needle`, or `-1`. |
| `rfind(c: char)` | `int` | Last index of character `c`, or `-1`. |
| `rfind(needle: const String&)` | `int` | Last index of substring `needle`, or `-1`. |
| `contains(c: char)` | `bool` | `true` if the content contains `c`. |
| `contains(needle: const String&)` | `bool` | `true` if the content contains `needle`. |
| `indexOf(c: char)` | `int` | Alias for `find(c)`. |
| `indexOf(needle: const String&)` | `int` | Alias for `find(needle)`. |
| `count(c: char)` | `unsigned int` | Number of occurrences of character `c`. |
| `count(needle: const String&)` | `unsigned int` | Number of non-overlapping occurrences of `needle`. Returns `0` for an empty needle. |
| `beginsWith(prefix: const String&)` | `bool` | `true` if the content starts with `prefix`. |
| `endsWith(suffix: const String&)` | `bool` | `true` if the content ends with `suffix`. |

### Extraction Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `substr(start: unsigned int, len: unsigned int)` | `String` | Extract a substring as a new `String`. Truncated if out-of-range. |
| `first(count: unsigned int)` | `String` | First `count` characters. |
| `last(count: unsigned int)` | `String` | Last `count` characters. |

### Mutating Methods

All mutating methods return `StringBuilder&` for fluent chaining.

| Method | Returns | Description |
|--------|---------|-------------|
| `consolidate()` | `StringBuilder&` | Merge all fragments into a single contiguous UTF-32 fragment. Safe to call multiple times. |
| `append(s: const String&)` | `StringBuilder&` | Append a `String`, mirroring its native storage encoding. |
| `append(src: const char[])` | `StringBuilder&` | Append a `const char[]` (e.g. a string literal) as a UTF-32 fragment. |
| `append(src: const unsigned byte[])` | `StringBuilder&` | Append a UTF-8 fragment (e.g. a `u8"…"` literal), stored natively as UTF-8. |
| `append(src: const unsigned short[])` | `StringBuilder&` | Append a UTF-16 fragment (e.g. a `u"…"` literal), stored natively as UTF-16. |
| `append(other: const StringBuilder&)` | `StringBuilder&` | Append another `StringBuilder`'s content, mirroring each of its fragments. |
| `appendChar(c: char)` | `StringBuilder&` | Append a single character (UTF-32 fragment). |
| `set(index: unsigned int, c: char)` | `StringBuilder&` | Set the code point at `index` (consolidates to UTF-32 first). No-op if out of range. |
| `prepend(s: const String&)` | `StringBuilder&` | Prepend a `String` at the beginning. |
| `prepend(src: const char[])` | `StringBuilder&` | Prepend a `const char[]` at the beginning. |
| `insert(pos: unsigned int, s: const String&)` | `StringBuilder&` | Insert a `String` at the given position. |
| `insert(pos: unsigned int, src: const char[])` | `StringBuilder&` | Insert a `const char[]` at the given position. |
| `remove(start: unsigned int, count: unsigned int)` | `StringBuilder&` | Remove `count` characters starting at `start`. |
| `replace(start, count, s: const String&)` | `StringBuilder&` | Replace `count` characters at `start` with `s`. |
| `replace(start, count, src: const char[])` | `StringBuilder&` | Replace with a `const char[]`. |
| `replaceAll(needle: const String&, replacement: const String&)` | `StringBuilder&` | Replace all non-overlapping occurrences of `needle` with `replacement`. |
| `reverse()` | `StringBuilder&` | Reverse the entire content in place. |
| `trimLeft()` | `StringBuilder&` | Remove leading whitespace (space, tab, newline, CR). |
| `trimRight()` | `StringBuilder&` | Remove trailing whitespace. |
| `trim()` | `StringBuilder&` | Remove leading and trailing whitespace. |
| `clear()` | `StringBuilder&` | Remove all content; capacity is retained. |

### Operators

| Operator | Returns | Description |
|----------|---------|-------------|
| `==(other: const StringBuilder&)` | `bool` | Equality: same length and identical content. |
| `!=(other: const StringBuilder&)` | `bool` | Inequality. |
| `+=(s: const String&)` | `StringBuilder&` | Append in place (fluent). |
| `+(s: const String&)` | `StringBuilder` | Concatenation — returns a new `StringBuilder`. |

### Example

```k
// Build a string
sb : k::StringBuilder("Hello");
sb.append(", ").append("world").appendChar('!');
assert(sb.size() == 13u);

// Search
assert(sb.find('o') == 4);
assert(sb.contains('!'));
assert(sb.beginsWith(k::String("Hello")));

// Mutation
sb.insert(5u, k::String(" beautiful"));
// "Hello beautiful, world!"
sb.remove(0u, 6u);
// "beautiful, world!"
sb.replace(0u, 9u, k::String("Wonderful"));
// "Wonderful, world!"

// Trim
ws : k::StringBuilder("  spaced  ");
ws.trim();
// "spaced"

// Reverse
rev : k::StringBuilder("abc");
rev.reverse();
// "cba"

// Convert to String
result : k::String(sb);
assert(result == k::String("Wonderful, world!"));

// Fluent chaining
sb2 : k::StringBuilder;
sb2.append("A").append("B").appendChar('C').reverse();
// "CBA"
```

---

## 4. Usage with string and character literals

K string and character literals are first-class sources for constructing and
populating `String` and `StringBuilder` objects.

### 4.1 Character literals

A character literal (e.g. `'A'`) has type `char` (8-bit signed integer) and
can be passed anywhere a `char` value is expected, including `appendChar`:

```k
sb : k::StringBuilder = "Hello";
sb.appendChar(',');
sb.appendChar(' ');
sb.appendChar('!');
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
sb.append("Hello").append(", ").append("world").appendChar('!');
assert(sb.size() == 13u);   // "Hello, world!"
```

Mixing `append` (for multi-character sequences) with `appendChar` (for
individual characters) is a common pattern:

```k
sb : k::StringBuilder("Key");
sb.appendChar(':').appendChar(' ').append("value");
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

Use the `String(sb: const StringBuilder&)` constructor or `StringBuilder.toString()`
to obtain an immutable snapshot at any point:

```k
sb : k::StringBuilder("Hello");
sb.appendChar('!');

snap1 : k::String(sb);
snap2 : k::String = sb.toString();
assert(snap1 == k::String("Hello!"));
assert(snap1 == snap2);
assert(sb.size() == 6u);   // builder is unchanged
```

---

*See also:*
[Literals](../language/expressions/literals.md) ·
[Types](../language/basic/types.md) ·
[Object](object.md)
