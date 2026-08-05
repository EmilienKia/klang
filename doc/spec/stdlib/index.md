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
| [`Set<T>`, `MutableSet<T>`, `OrderedSet<T>`, `MutableOrderedSet<T>`, `SortedSet<T>`, `MutableSortedSet<T>`, `ListSet<T>`, `TreeSet<T>`, `HashSet<T>`](collections-set.md) | Generic set framework: interfaces + three concrete implementations (list-backed insertion-ordered set, AVL-tree-backed sorted set, hash-table-backed set). | `src/set.k` |
| [`Entry<K,V>`, `MutableEntry<K,V>`, `Map<K,V>`, `MutableMap<K,V>`, `OrderedMap<K,V>`, `MutableOrderedMap<K,V>`, `SortedMap<K,V>`, `MutableSortedMap<K,V>`, `ListMap<K,V>`, `TreeMap<K,V>`, `HashMap<K,V>`](collections-map.md) | Generic map (dictionary) framework: interfaces + three concrete implementations (list-backed insertion-ordered map, AVL-tree-backed sorted map, hash-table-backed map). | `src/map.k` |
| [`Expected<R,E>`](expected.md) | Discriminated-union wrapper holding either a successful result of type `R` or an error of type `E`. Static factories: `Expected<R,E>::expected()`, `::unexpected()`, `::error()`. | `src/expected.k` |
| [`RTTI Types`](rtti.md) | Runtime type information: `Visibility`, `TypeInfo`, `AggregateType`, `Class`, `Interface`, `AnnotationType`, `Annotation`, `Function`, `Unit`. | `src/rtti.k` |
| [`Exception Types`](exceptions.md) | Exception hierarchy for error handling: `Throwable`, `Exception` (checked), `FatalError` (unchecked), `OutOfMemory`, `NullPointerException`, `IndexOutOfBoundsException`, `IllegalArgumentException`, `IllegalStateException`, `ConstructionException`, `NullPointerError`, `NullDereferenceError`, `NullAssignationError`, `NullCastError`, `IndexOutOfBoundsError`. | `src/exception.k` |
| [`Meta-Annotations`](rtti.md#11-meta-annotation-types) | Meta-annotation types: `Retention` (with `Policy` enum), `Inherited`, `Target` (with `ElementType` enum). Control annotation retention, inheritance, and applicability. | `src/annotations.k` |
| [`I/O Streams`](io.md) | Byte-oriented I/O stream abstractions: `InputStream`, `OutputStream`, `Array*Stream`, `Filter*Stream`, `Buffered*Stream`, `DataInput`/`DataOutput`, `Data*Stream`, `PrintStream`, `File*Stream`, and standard I/O handles. | `src/io/` |
| [`Threading and Time`](threading.md) | Portable runtime threading layer: `Duration`, `Instant`, `Runnable`, `Thread`, and the `ThreadInterruptionException` / `TimeoutException` / `CancellationException` / `ExecutionException` hierarchy. | `src/time.k`, `src/thread.k`, `src/thread_exceptions.k` |
| [`Futures and Promises`](futures.md) | One-shot asynchronous results: `Future<T>` (read side, interruptible and timed `get()`, cancellation) and `Promise<T>` (write-once producer side). | `src/future.k` |
| [`Synchronisation Primitives`](synchronization.md) | Mutual exclusion and coordination: `Lock`, `Mutex`, `ReentrantLock`, `Condition`, `Semaphore`, `CountDownLatch`, `CyclicBarrier`, `ReadWriteLock`, plus `IllegalMonitorStateException` / `BrokenBarrierException`. | `src/sync/` |
| [`Asynchronous I/O`](io-async.md) | Interruptible, cancellable file I/O: `ByteBuffer`, `Path`, `Channel`/`ReadableChannel`/`WritableChannel`, `FileChannel`, `AsyncFileInputStream`/`AsyncFileOutputStream`, and the `IOException` hierarchy. Backed by io_uring with a portable POSIX fallback. | `src/io/` |
| [`Asynchronous Network I/O`](io-network.md) | Interruptible TCP networking: `NetworkAddress`, `SocketChannel`, `Socket`, `ServerSocket`, with timeout/interruption semantics aligned with other blocking APIs. | `src/io/` |
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
