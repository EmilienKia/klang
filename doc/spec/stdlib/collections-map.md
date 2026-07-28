# Maps — `Map<K,V>`, `MutableMap<K,V>`, `OrderedMap<K,V>`, `SortedMap<K,V>`, `MutableOrderedMap<K,V>`, `MutableSortedMap<K,V>`, `ListMap<K,V>`, `TreeMap<K,V>`, `HashMap<K,V>`

> **Module:** `k`
> **Source:** `libk/libk/src/map.k`
> **Status:** Working Draft — 2026

---

## Overview

The K standard library provides a generic map (dictionary) framework built on
six abstract interfaces, a shared entry type, and three concrete
implementations. A *map* associates at most one value with each key.

Unlike `Set<T>`/`List<T>`, `Map<K,V>` is **not** a `Collection<T>`: iteration
walks read-only `Entry<K,V>` associations (`Map<K,V>` extends
`Sequence<Entry<K,V>>`), and membership testing is split between
`containsKey` (indexed, typically fast) and `containsValue` (always an O(n)
linear scan of the values, since values are not indexed).

`OrderedMap<K,V>` only guarantees that iteration order is stable and
well-defined — it does not say *what* that order is (it may, for instance, be
insertion order). `SortedMap<K,V>` narrows that guarantee: the order is a
genuine sort order (entries are arranged by the key's `<` operator).

| Type | Description | Backing |
|------|-------------|---------|
| `Entry<K,V>` | Read-only key/value association returned by traversal/lookup | (abstract) |
| `MutableEntry<K,V>` | An `Entry<K,V>` whose value can be updated in place (key is immutable) | (abstract) |
| `Map<K,V>` | Common read-only interface for all maps | (abstract) |
| `MutableMap<K,V>` | Adds insertion/removal/in-place mutation | (abstract) |
| `OrderedMap<K,V>` | A `Map<K,V>` with a well-defined (but unspecified) iteration order and `first()`/`last()` | (abstract) |
| `MutableOrderedMap<K,V>` | Combination of `MutableMap<K,V>` and `OrderedMap<K,V>` | (abstract) |
| `SortedMap<K,V>` | An `OrderedMap<K,V>` whose order is a sort order (by key's `<`) | (abstract) |
| `MutableSortedMap<K,V>` | Combination of `MutableOrderedMap<K,V>` and `SortedMap<K,V>` | (abstract) |
| `ListMap<K,V>` | Ordered map (insertion order), keyed by linear scan | `DoubleLinkedList<MapEntry<K,V>>` |
| `TreeMap<K,V>` | Sorted map (sorted by key's `<`) | AVL self-balancing binary search tree |
| `HashMap<K,V>` | Unordered map, keyed by hashing | Separate-chaining hash table |

All three concrete maps store both keys and values **by value** (each entry
owns its own key/value pair via a `UniSlot<K>`/`UniSlot<V>` pair inside a
shared `MapEntry<K,V>`), implement `MutableMap<K,V>` (const iteration over
`Entry<K,V>` via `Sequence<Entry<K,V>>`, plus a separate mutable
`Iterator<MutableEntry<K,V>>!`), and own their contents (entries are
destroyed when removed or when the map itself is destroyed).

---

## `Entry<K,V>` — Interface

```k
template<typename K, typename V>
interface Entry {
    const key() : const K&;
    const value() : const V&;
}
```

A read-only key/value association, returned (via `OptionalConstRef<Entry<K,V>>`
or `ConstIterator<Entry<K,V>>`) by read-only map traversal and lookup.
Concrete map implementations back this interface with a node that stores both
the key and the value (`MapEntry<K,V>`).

| Method | Description |
|--------|-------------|
| `key() : const K&` | Read-only reference to the entry's key. |
| `value() : const V&` | Read-only reference to the entry's value. |

## `MutableEntry<K,V>` — Interface

```k
template<typename K, typename V>
interface MutableEntry : public Entry<K,V> {
    value(val: const V&);
}
```

Adds an in-place value setter on top of `Entry<K,V>`. Only the value can be
changed: the key is immutable once an entry has been created, so structural
invariants of ordered/hashed maps (which index by key) are never at risk of
being broken through this interface. Returned by `MutableMap<K,V>::iterator()`.

| Method | Description |
|--------|-------------|
| `value(val: const V&)` | Overwrite the entry's value in place; the key is unchanged. |

---

## `Map<K,V>` — Interface

```k
template<typename K, typename V>
interface Map : public Sequence<Entry<K,V>>, public Sized {
    const get(key: const K&) : OptionalConstRef<V>;
    const containsKey(key: const K&) : bool;
    const containsValue(value: const V&) : bool;
}
```

### Methods

| Method | Description |
|--------|-------------|
| `size() : unsigned int` | Number of entries (inherited from `Sized`). |
| `isEmpty() : bool` | `true` if the map contains no entries. |
| `get(key) : OptionalConstRef<V>` | Read-only reference to the value associated with `key`, or empty if `key` is not present. |
| `containsKey(key) : bool` | `true` if some entry has a key equal to `key` (via `==`). |
| `containsValue(value) : bool` | `true` if some entry has a value equal to `value` (via `==`). Always O(n): scans every entry. |
| `constIterator() : ConstIterator<Entry<K,V>>!` | Read-only iterator over the entries (inherited from `Sequence<Entry<K,V>>`). |

### Exceptions

None declared explicitly. Insertion methods on `MutableMap<K,V>` implementations
may throw `ConstructionException` (a `FatalError`) if the underlying storage's
key/value construction fails; as a `FatalError` it does not need to be declared
in `throws` clauses.

---

## `MutableMap<K,V>` — Interface

```k
template<typename K, typename V>
interface MutableMap : public Map<K,V> {
    get(key: const K&) : OptionalRef<V>;
    put(key: const K&, value: const V&);
    putIfAbsent(key: const K&, value: const V&) : OptionalRef<V>;
    replace(key: const K&, value: const V&, insertIfAbsent: bool = true) : Optional<V>;
    remove(key: const K&) : bool;
    clear();
    iterator() : Iterator<MutableEntry<K,V>>!;
}
```

| Method | Description |
|--------|-------------|
| `get(key) : OptionalRef<V>` | Mutable reference to the value associated with `key`, or empty if `key` is not present. |
| `put(key, value)` | Associate `value` with `key`, overwriting any previous association. Returns nothing: use `replace()` if the previous value is needed. |
| `putIfAbsent(key, value) : OptionalRef<V>` | Associate `value` with `key` only if `key` is not already present. Returns a reference to the existing value if `key` was already present (map unchanged), or empty if `key` was absent (`value` was inserted). |
| `replace(key, value, insertIfAbsent = true) : Optional<V>` | Associate `value` with `key` and return a **copy** of the previous value, if any. If `insertIfAbsent` is `true` (default), `key` is inserted even if it was absent; if `false`, the map is left unchanged when `key` is absent. Returns empty if `key` had no previous value (whether or not it was just inserted). |
| `remove(key) : bool` | Remove the entry for `key`. Returns `true` if `key` was found and removed. |
| `clear()` | Remove every entry from the map. |
| `iterator() : Iterator<MutableEntry<K,V>>!` | Mutable iterator over the entries: `value()` can be updated in place, `key()` cannot. |

> **Design note — why `replace()` returns `Optional<V>` (a copy) rather than
> `OptionalRef<V>` (a reference):** a reference to the *previous* value would
> be dangling the instant the map overwrites it in place to store the new
> value, since all three concrete maps hold exactly one value slot per entry
> (there is no separate "old value" storage). Returning a copy avoids this
> dangling-reference hazard entirely, at the cost of one extra copy of `V`.

---

## `OrderedMap<K,V>` — Interface

```k
template<typename K, typename V>
interface OrderedMap : public Map<K,V> {
    const first() : OptionalConstRef<Entry<K,V>>;
    const last() : OptionalConstRef<Entry<K,V>>;
}
```

| Method | Description |
|--------|-------------|
| `first() : OptionalConstRef<Entry<K,V>>` | Read-only reference to the first entry in the map's iteration order, or empty if the map is empty. |
| `last() : OptionalConstRef<Entry<K,V>>` | Read-only reference to the last entry in the map's iteration order, or empty if the map is empty. |

`OrderedMap<K,V>` only promises that the iteration/`first()`/`last()` order is
stable and well-defined; it says nothing about *what* that order is. It may,
for example, be insertion order. Use `SortedMap<K,V>` when the order must
specifically be a sort order (by the key's `<`).

## `MutableOrderedMap<K,V>` — Interface

```k
template<typename K, typename V>
interface MutableOrderedMap : public OrderedMap<K,V>, public MutableMap<K,V> {
    mutableFirst() : OptionalRef<MutableEntry<K,V>>;
    mutableLast() : OptionalRef<MutableEntry<K,V>>;
}
```

Combines `put`/`get`/`remove`/`clear`/`iterator` (from `MutableMap<K,V>`) with
`first()`/`last()` (from `OrderedMap<K,V>`), and adds mutable equivalents:

| Method | Description |
|--------|-------------|
| `mutableFirst() : OptionalRef<MutableEntry<K,V>>` | Mutable reference to the first entry in iteration order, or empty if the map is empty. |
| `mutableLast() : OptionalRef<MutableEntry<K,V>>` | Mutable reference to the last entry in iteration order, or empty if the map is empty. |

Implemented by `ListMap<K,V>` (insertion order).

## `SortedMap<K,V>` — Interface

```k
template<typename K, typename V>
interface SortedMap : public OrderedMap<K,V> {
}
```

A `SortedMap<K,V>` is an `OrderedMap<K,V>` whose iteration order is
specifically a sort order: entries are arranged according to the key's `<`
operator, so `first()`/`last()` return the entries with the smallest/largest
key and iteration visits entries in ascending key order.

## `MutableSortedMap<K,V>` — Interface

```k
template<typename K, typename V>
interface MutableSortedMap : public SortedMap<K,V>, public MutableOrderedMap<K,V> {
}
```

Combines `put`/`get`/`remove`/`clear`/`iterator` (from `MutableMap<K,V>`) with
the sorted `first()`/`last()`/iteration contract (from `SortedMap<K,V>`, via
`MutableOrderedMap<K,V>`). Implemented by `TreeMap<K,V>`.

---

## `MapEntry<K,V>`

```k
template<typename K, typename V>
class MapEntry : public MutableEntry<K,V> { ... }
```

Concrete key/value pair storage implementing `MutableEntry<K,V>`, shared by
every concrete `Map<K,V>` implementation (`ListMap`, `TreeMap`, `HashMap`) as
the embedded per-node entry: the key and the value are each stored **by
value** in their own `UniSlot`. Only `key()`/`value()`/`value(val)` (declared
by `Entry<K,V>`/`MutableEntry<K,V>`) are part of the public map API surface;
`getValue()`/`setKey()` are internal helpers used by the concrete map
implementations (which hold a typed `MapEntry<K,V>`, not just an
`Entry<K,V>`) to build and relocate entries.

---

## `ListMap<K,V>`

```k
template<typename K, typename V>
class ListMap : public MutableOrderedMap<K, V> { ... }
```

### Description

An ordered map backed by a `DoubleLinkedList<MapEntry<K,V>>`, maintaining
**insertion order**: `first()`/`mutableFirst()` return the oldest (first
inserted, still-present) entry and `last()`/`mutableLast()` return the most
recently inserted entry. `put()`-ing an already-present key updates its value
in place without moving it in the iteration order. Keys are located by a
linear scan (`==` operator) before every lookup/insertion, so `get`, `put`,
`putIfAbsent`, `replace`, `containsKey` and `remove` are all O(n). Simplest
and lowest-overhead ordered map — a good default for small maps or key types
that do not implement `hash()`.

Iterators (forward, const and mutable) simply delegate to the backing
`DoubleLinkedList<MapEntry<K,V>>`'s own iterators — iteration order is
insertion order.

### Complexity

| Operation | Time |
|-----------|------|
| `get`/`put`/`putIfAbsent`/`replace` | O(n) |
| `containsKey` | O(n) |
| `containsValue` | O(n) |
| `remove` | O(n) |
| `first`/`last`/`mutableFirst`/`mutableLast` | O(1) |
| `size`/`isEmpty` | O(1) |
| `clear` | O(n) |
| Iteration (full) | O(n) |

### Usage

```k
m : ListMap<int, int>;
m.put(1, 10);
m.put(2, 20);
m.put(1, 11);          // updates 1's value in place, does not move it
m.containsKey(2);       // true
m.first();              // Entry (1, 11) — first inserted key
m.last();               // Entry (2, 20) — last inserted key
m.remove(1);
```

---

## `TreeMap<K,V>`

```k
template<typename K, typename V>
class TreeMap : public MutableSortedMap<K, V> { ... }
```

### Description

A sorted map implemented as an AVL self-balancing binary search tree, ordered
by the key's `<` operator (`K` must support `<` and `==`/`!=`). Mirrors
`TreeSet<T>`'s algorithm (see `set.k`): every insertion and removal rebalances
the tree so its height stays O(log n), bounding `get`, `put`, `putIfAbsent`,
`replace`, `containsKey` and `remove` to O(log n).

`first()`/`last()`/`mutableFirst()`/`mutableLast()` return the entry with the
smallest/largest key in O(log n) (leftmost/rightmost node). Forward iterators
perform an in-order (ascending) traversal via an explicit stack over the left
spine, giving O(1) amortized time per step and O(log n) auxiliary space.

### Complexity

| Operation | Time |
|-----------|------|
| `get`/`put`/`putIfAbsent`/`replace` | O(log n) |
| `containsKey` | O(log n) |
| `containsValue` | O(n) |
| `remove` | O(log n) |
| `first`/`last`/`mutableFirst`/`mutableLast` | O(log n) |
| `size`/`isEmpty` | O(1) |
| `clear` | O(n) |
| Iteration (full) | O(n) |

### Usage

```k
m : TreeMap<int, int>;
m.put(5, 50);
m.put(1, 10);
m.put(3, 30);
m.first();     // Entry (1, 10)
m.last();      // Entry (5, 50)

it : ConstIterator<Entry<int,int>>! = m.constIterator();
cur : OptionalConstRef<Entry<int,int>> = it.next();
while (cur.hasValue()) {   // visits keys 1, 3, 5 in ascending order
    cur = it.next();
}
```

---

## `HashMap<K,V>`

```k
template<typename K, typename V>
class HashMap : public MutableMap<K, V> { ... }
```

### Description

An unordered map implemented with separate chaining, mirroring `HashSet<T>`
(see `set.k`): an array of buckets, each holding a singly-linked chain of
colliding entries. The bucket array starts at 16 buckets and doubles whenever
the load factor would exceed 0.75, rehashing (redistributing) every existing
entry.

**Requirement on `K`:** the key type must provide a `const hash() : int`
method (inherited from `Object` or overridden) and support the `==` operator,
used to resolve collisions within a bucket.

`HashMap<K,V>` implements `MutableMap<K,V>` only — it is neither an
`OrderedMap<K,V>` nor a `SortedMap<K,V>`, so it has no `first()`/`last()`.
Iteration order is **unspecified** and unrelated to insertion order: it walks
the bucket array from first to last, and within each bucket walks the
collision chain from head to tail.

### Complexity

| Operation | Time |
|-----------|------|
| `get`/`put`/`putIfAbsent`/`replace` | Average O(1) (amortized; may trigger an O(n) rehash) |
| `containsKey` | Average O(1) |
| `containsValue` | O(n) |
| `remove` | Average O(1) |
| `size`/`isEmpty` | O(1) |
| `clear` | O(buckets) — chains are cleared, bucket capacity is left unchanged |
| Iteration (full) | O(buckets + n) |

### Usage

```k
m : HashMap<int, int>;
m.put(1, 10);
m.put(2, 20);
m.containsKey(1);   // true
m.remove(2);
```

Using a custom type as key requires overriding `hash()` and `==`:

```k
class Id {
    private:
    _value : int;

    public:
    Id(v : int) { _value = v; }
    const hash() : int { return _value; }
    const operator ==(other : const Id&) : bool { return _value == other._value; }
}

ids : HashMap<Id, int>;
ids.put(Id(1), 100);
ids.put(Id(2), 200);
ids.containsKey(Id(1));   // true
```

---

## Iterators

Each concrete map provides two entry iterator classes returned by
`constIterator()` and `iterator()` (there is no reverse iteration):

| Map | Const forward (`Entry<K,V>`) | Mutable forward (`MutableEntry<K,V>`) |
|-----|-------------------------------|------------------------------------------|
| `ListMap<K,V>` | `ListMapConstIterator<K,V>` (delegates to `DoubleLinkedList<MapEntry<K,V>>::constIterator()`) | `ListMapIterator<K,V>` (delegates to `DoubleLinkedList<MapEntry<K,V>>::iterator()`) |
| `TreeMap<K,V>` | `TreeMapConstIterator<K,V>` | `TreeMapIterator<K,V>` |
| `HashMap<K,V>` | `HashMapConstIterator<K,V>` | `HashMapIterator<K,V>` |

All iterators implement `ConstIterator<Entry<K,V>>`/`Iterator<MutableEntry<K,V>>`:
`next()` returns an `OptionalConstRef<Entry<K,V>>` (const iterators) or
`OptionalRef<MutableEntry<K,V>>` (mutable iterators), empty once the traversal
is exhausted. Iterators are single-use and consumable — call `next()` in a
loop until `hasValue()` is `false`. On a mutable iterator, calling
`value(val)` on the yielded `MutableEntry<K,V>` updates the value in place;
the key cannot be changed.

`TreeMap<K,V>`'s iterators are a true forward in-order traversal (ascending
key order). `HashMap<K,V>`'s iterators traverse buckets/chains in storage
order, which is unspecified from the caller's perspective and may change
across `rehash()` calls; do not rely on any particular order or on order
stability across mutations.

---

## Memory Model

All three map classes store keys and values **by value**, each pair held in a
shared `MapEntry<K,V>` (itself holding a `UniSlot<K>` and a `UniSlot<V>`):

- **`ListMap<K,V>`** delegates entirely to `DoubleLinkedList<MapEntry<K,V>>`'s
  own node storage.
- **`TreeMap<K,V>`** allocates one `TreeMapNode<K,V>` per entry via `new`;
  each node embeds a `MapEntry<K,V>` and owns its two children
  (`_left`/`_right`), forming a tree of ownership rooted at an internal
  pointer. Removing the whole tree (`clear()`/destructor) walks it
  recursively to free every node.
- **`HashMap<K,V>`** allocates one `HashMapChain<K,V>` per bucket and one
  `HashMapNode<K,V>` per entry (each embedding a `MapEntry<K,V>`). Bucket
  chain heads are tracked via a raw `Vector<HashMapChain<K,V>*>` for O(1)
  indexed lookup, while every chain is also independently kept alive via an
  intrusive singly-linked list rooted internally, ensuring every chain is
  freed exactly once regardless of how buckets are indexed or replaced during
  a rehash.

### Ownership

Maps **own** their entries: destroying a map (or calling `clear()`) destroys
every contained key and value. There is no shared ownership or external
reference tracking. `replace()` returns a **copy** of the previous value
(never a reference to internal storage) precisely to avoid dangling
references when the map overwrites a value in place (see the design note
under `MutableMap<K,V>` above).

---

## Choosing a Map

| Need | Best choice |
|------|-------------|
| Sorted iteration / smallest-largest key `first()`/`last()` | `TreeMap<K,V>` |
| Insertion-order iteration / oldest-newest `first()`/`last()` | `ListMap<K,V>` |
| Fastest average-case `get`/`put`/`remove` | `HashMap<K,V>` (requires `hash()` on `K`) |
| Key type has no `hash()` and map is small/rarely searched | `ListMap<K,V>` |
| Guaranteed O(log n) worst case (no hash collisions to worry about) | `TreeMap<K,V>` |
| Key type supports `<` but not `hash()` | `TreeMap<K,V>` |
| General purpose, best average performance | `HashMap<K,V>` |
