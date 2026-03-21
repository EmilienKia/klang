# String Types
**Module:** `k`  
**Source:** `libk/libk/src/string.k`
---
## CharHelpers
`public final class CharHelpers` — static utility methods for `char[]` buffer
operations.
All methods are public and static.  Parameters use `char[]` (reference) so
that any addresser (owner, pointer, link, pin) can be passed without
transferring ownership.
### Static Methods
| Method | Description |
|--------|-------------|
| `copy(dst: char[], dst_off: unsigned int, const src: char[], src_off: unsigned int, count: unsigned int)` | Copy `count` chars from `src[src_off..]` to `dst[dst_off..]`. |
| `equals(a: char[], b: char[], len: unsigned int) : bool` | Compare `len` chars starting from position 0 in both arrays. |
| `fill(dst: char[], dst_off: unsigned int, c: char, count: unsigned int)` | Fill `count` positions in `dst` starting at `dst_off` with char `c`. |
---
## String
`const final class String` — immutable string wrapping a null-terminated
`char[]!` (owner) buffer.
### Invariants
- `_size >= 0`
- `_buf` is either `null` (empty string) or a heap-allocated `char[]!` whose
  element at index `_size` is `'\0'`.
### Constructors
| Signature | Description |
|-----------|-------------|
| `String()` | Default: empty string. |
| `String(buf: char[]!, sz: unsigned int)` | Takes ownership of a null-terminated buffer. |
| `String(other: const String&)` | Copy constructor — allocates a new buffer. |
| `String(sb: const StringBuilder&)` | Construct from a `StringBuilder` (implicit conversion). |
### Const Methods
| Method | Returns | Description |
|--------|---------|-------------|
| `size()` | `unsigned int` | Number of characters (excluding `'\0'`). |
| `empty()` | `bool` | `true` when the string is empty. |
| `at(index: int)` | `char` | Character at position `index` (no bounds check). |
| `data()` | `char[]^` | Non-owning pin to the internal buffer. |
### Operators
| Operator | Description |
|----------|-------------|
| `==(other: const String&) : bool` | Equality comparison. |
| `!=(other: const String&) : bool` | Inequality comparison. |
| `+(other: const String&) : StringBuilder` | Concatenation — returns a new `StringBuilder`. |
---
## StringBuilder
`class StringBuilder` — mutable, growable string builder backed by a single
contiguous `char[]!` buffer.
### Invariants
- `_buf` is either `null` (empty builder) or a heap-allocated `char[]!` that
  is null-terminated at position `_size`.
- `_size <= _capacity`.
### Constructors
| Signature | Description |
|-----------|-------------|
| `StringBuilder()` | Default: empty builder. |
| `StringBuilder(s: const String&)` | Construct from a `String`. |
| `StringBuilder(other: const StringBuilder&)` | Copy constructor. |
### Const Methods
| Method | Returns | Description |
|--------|---------|-------------|
| `size()` | `unsigned int` | Total number of characters. |
| `empty()` | `bool` | `true` when the builder is empty. |
| `char_at(index: int)` | `char` | Character at position `index` (no bounds check). |
| `data()` | `char[]*` | Non-owning pointer to the internal buffer. |
### Mutating Methods
| Method | Returns | Description |
|--------|---------|-------------|
| `append(s: const String&)` | `StringBuilder&` | Append a `String`. Fluent. |
| `append_char(c: char)` | `StringBuilder&` | Append a single character. Fluent. |
| `clear()` | `StringBuilder&` | Clear all content. Fluent. |
### Operators
| Operator | Description |
|----------|-------------|
| `+=(s: const String&) : StringBuilder&` | Append (fluent). |
| `+(s: const String&) : StringBuilder` | Concatenation — returns a new `StringBuilder`. |
