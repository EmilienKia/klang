# K Standard Library Reference

This section documents the **K Standard Library** — the set of types and
functions shipped with the K compiler and available to all K programs.

> **Status:** Working Draft — 2026

---

## Organisation

The K standard library is contained in a single module — `k` — compiled into
`libk.so` / `libk.a`.  It is automatically linked with every K program; no
explicit `import` is ever needed to use its types and namespaces.

### Base standard library (module `k`)

The base library provides the complete common infrastructure that every K
program can rely on.  The compiler injects `import k;` automatically (unless
the module being compiled *is* `k`), and `-lk` is added to the link command.

Contents:

| Type / Namespace | Description | Source |
|------------------|-------------|--------|
| [`Object`](object.md) | Root base class for all K classes. | `src/object.k` |
| [`CharHelpers`](string.md#charhelpers) | Static utility methods for `char[]` buffer operations. | `src/string.k` |
| [`String`](string.md#string) | Immutable, final string class wrapping a null-terminated `char[]!` buffer. | `src/string.k` |
| [`StringBuilder`](string.md#stringbuilder) | Mutable, growable string builder backed by a single contiguous buffer. | `src/string.k` |
| [`UniSlot<T>`, `MultiSlot<T>`](memory.md) | Low-level memory primitives for explicit lifetime management of objects. | `src/memory.k` |
| [`Collection<T>`, `Vector<T>`, `LinkedList<T>`, `DoubleLinkedList<T>`](collections.md) | Generic collection framework: interface + three concrete implementations (dynamic array, singly-linked list, doubly-linked list). | `src/collections.k` |
| [`RTTI Types`](rtti.md) | Runtime type information: `Visibility`, `TypeInfo`, `AggregateType`, `Class`, `Interface`, `AnnotationType`, `Annotation`, `Function`, `Unit`. | `src/rtti.k` |
| [`Meta-Annotations`](rtti.md#11-meta-annotation-types) | Meta-annotation types: `Retention` (with `Policy` enum), `Inherited`, `Target` (with `ElementType` enum). Control annotation retention, inheritance, and applicability. | `src/annotations.k` |
| [`I/O Streams`](io.md) | Byte-oriented I/O stream abstractions: `InputStream`, `OutputStream`, `ByteArray*Stream`, `Filter*Stream`, `Buffered*Stream`, `DataInput`/`DataOutput`, `Data*Stream`. | `src/io/` |
| [`k::math::Math`](math.md) | Static utility class for integer and long mathematical operations (`abs`, `min`, `max`, `clamp`, …). | `src/math/math.k` |

*(More types and namespaces will be added as the language evolves.)*

---

## Conventions

### Naming

All standard library types follow the Java naming conventions mandated by the
K language:

- **Types**: `PascalCase` (e.g. `String`, `StringBuilder`, `Object`)
- **Methods / functions**: `camelCase` (e.g. `size()`, `append()`, `hash()`)
- **Constants**: `ALL_CAPS`
- **Private fields**: prefixed with `_` (e.g. `_buf`, `_size`)

### Parameter passing

- Array parameters that are **read-only** use `const char[]`; those that are
  mutated in-place (without reallocation) use `char[]`.
- Owners (`char[]!`), pointers (`char[]*`), links (`char[]+`), and views
  (`char[]?`) widen implicitly to `char[]` or `const char[]` without
  ownership transfer.
- **String literals** have type `const char[N]&` and widen automatically to
  `const char[]` — they can therefore be passed directly to any `const char[]`
  parameter and used to construct `String` or `StringBuilder` objects.
- **Constness** is applied whenever the parameter is not modified.

### Ownership

- Heap-allocated buffers are held as owners (`T!`).
- Public accessors that expose internal data without transferring ownership
  return a view (`T?`) or pointer (`T*`), never an owner.
- Copy constructors allocate fresh buffers; the original is unmodified.

---

## See Also

- [Module System](../language/basic/modules.md)
- [Libraries — Export and Import](../language/basic/libraries.md)
- [Types — Indirection Types](../language/basic/types.md)

