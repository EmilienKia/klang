# Sets — `Set<T>`, `MutableSet<T>`, `OrderedSet<T>`, `MutableOrderedSet<T>`, `ListSet<T>`, `TreeSet<T>`, `HashSet<T>`

> **Module:** `k`
> **Source:** `libk/libk/src/set.k`
> **Status:** Working Draft — 2026

---

## Overview

The K standard library provides a generic set framework built on four
abstract interfaces and three concrete implementations. A *set* is a
`Collection<T>` that guarantees every contained element is unique.

| Type | Description | Backing |
|------|-------------|---------|
| `Set<T>` | Common read-only interface for all sets | (abstract) |
| `MutableSet<T>` | Adds `add`/`remove` mutation | (abstract) |
| `OrderedSet<T>` | A `Set<T>` that also exposes `first()`/`last()` | (abstract) |
| `MutableOrderedSet<T>` | Combination of `MutableSet<T>` and `OrderedSet<T>` | (abstract) |
| `ListSet<T>` | Unordered set, uniqueness enforced by linear scan | `DoubleLinkedList<T>` |
| `TreeSet<T>` | Ordered set (sorted by `<`) | AVL self-balancing binary search tree |
| `HashSet<T>` | Unordered set, uniqueness enforced by hashing | Separate-chaining hash table |

All three concrete sets store elements **by value**, implement
`MutableReversibleSequence<T>` (forward + reverse, const + mutable
iterators), and own their contents (elements are destroyed when removed
or when the set itself is destroyed).

---

## `Set<T>` — Interface

```k
template<typename T>
interface Set : public Collection<T> {
    default const isSubsetOf(other: const Set<T>&) : bool;
    default const isSupersetOf(other: const Set<T>&) : bool;
}
```

### Methods

| Method | Description |
|--------|-------------|
| `size() : unsigned int` | Number of elements (inherited from `Sized` via `Collection<T>`). |
| `isEmpty() : bool` | `true` if the set contains no elements. |
| `contains(value) : bool` | `true` if an element equal to `value` (via `==`) is present. |
| `constIterator() : ConstIterator<T>!` | Read-only iterator over the elements. |
| `isSubsetOf(other) : bool` | Default implementation: `true` iff every element of `this` is found in `other`. |
| `isSupersetOf(other) : bool` | Default implementation: delegates to `other.isSubsetOf(this)`. |

### Exceptions

None declared explicitly. Insertion methods on `MutableSet<T>` implementations
may throw `ConstructionException` (a `FatalError`) if the underlying storage's
element construction fails; as a `FatalError` it does not need to be declared
in `throws` clauses.

---

## `MutableSet<T>` — Interface

```k
template<typename T>
interface MutableSet : public Set<T>, public MutableCollection<T> {
    add(element: const T&) : bool;
    remove(element: const T&) : bool;
}
```

| Method | Description |
|--------|-------------|
| `add(element) : bool` | Insert a copy of `element` if not already present. Returns `true` if added, `false` if it was already present. |
| `remove(element) : bool` | Remove the element equal to `element`. Returns `true` if it was found and removed. |
| `clear()` | Remove all elements (inherited from `MutableCollection<T>`). |
| `iterator() : Iterator<T>!` | Mutable iterator over the elements (inherited from `MutableSequence<T>`). |

---

## `OrderedSet<T>` — Interface

```k
template<typename T>
interface OrderedSet : public Set<T>, public OrderedCollection<T> {
}
```

Adds, from `OrderedCollection<T>`:

| Method | Description |
|--------|-------------|
| `first() : OptionalConstRef<T>` | Read-only reference to the smallest/first element, or empty if the set is empty. |
| `last() : OptionalConstRef<T>` | Read-only reference to the largest/last element, or empty if the set is empty. |

## `MutableOrderedSet<T>` — Interface

```k
template<typename T>
interface MutableOrderedSet : public OrderedSet<T>, public MutableSet<T> {
}
```

Combines `add`/`remove`/`clear` (from `MutableSet<T>`) with
`first()`/`last()` (from `OrderedSet<T>`). Implemented by `TreeSet<T>`.

---

## `ListSet<T>`

```k
template<typename T>
class ListSet : public MutableSet<T>, public MutableReversibleSequence<T> { ... }
```

### Description

An unordered set backed by a `DoubleLinkedList<T>`. Uniqueness is enforced by
a linear scan (`==` operator) before every insertion, so `add`, `remove` and
`contains` are all O(n). Simplest and lowest-overhead set — a good default for
small sets or sets whose element type does not implement `hash()`.

Iterators (forward, reverse, const and mutable) simply delegate to the
backing `DoubleLinkedList<T>`'s own iterators — iteration order is insertion
order.

### Complexity

| Operation | Time |
|-----------|------|
| `add` | O(n) |
| `remove` | O(n) |
| `contains` | O(n) |
| `size`/`isEmpty` | O(1) |
| `clear` | O(n) |
| Iteration (full) | O(n) |

### Usage

```k
s : ListSet<int>;
s.add(1);
s.add(2);
s.add(1);            // false, 1 already present
s.contains(2);        // true
s.remove(1);
```

---

## `TreeSet<T>`

```k
template<typename T>
class TreeSet : public MutableOrderedSet<T>, public MutableReversibleSequence<T> { ... }
```

### Description

An ordered set implemented as an AVL self-balancing binary search tree,
ordered by the `<` operator (elements must support `<` and `==`/`!=`). Every
insertion and removal rebalances the tree so its height stays O(log n),
bounding `add`, `remove` and `contains` to O(log n).

`first()`/`last()` return the smallest/largest element in O(log n) (leftmost/
rightmost node). Forward iterators perform an in-order (ascending) traversal;
reverse iterators perform a reverse-in-order (descending) traversal — both
via an explicit stack over the left/right spine, giving O(1) amortized time
per step and O(log n) auxiliary space.

### Complexity

| Operation | Time |
|-----------|------|
| `add` | O(log n) |
| `remove` | O(log n) |
| `contains` | O(log n) |
| `first`/`last` | O(log n) |
| `size`/`isEmpty` | O(1) |
| `clear` | O(n) |
| Iteration (full) | O(n) |

### Usage

```k
s : TreeSet<int>;
s.add(5);
s.add(1);
s.add(3);
s.first();     // OptionalConstRef holding 1
s.last();      // OptionalConstRef holding 5

it : ConstIterator<int>! = s.constIterator();
cur : OptionalConstRef<int> = it.next();
while (cur.hasValue()) {   // visits 1, 3, 5 in ascending order
    cur = it.next();
}
```

---

## `HashSet<T>`

```k
template<typename T>
class HashSet : public MutableSet<T>, public MutableReversibleSequence<T> { ... }
```

### Description

An unordered set implemented with separate chaining: an array of buckets,
each holding a singly-linked chain of colliding elements. The bucket array
starts at 16 buckets and doubles whenever the load factor would exceed 0.75,
rehashing (redistributing) every existing element.

**Requirement on `T`:** the element type must provide a `const hash() : int`
method (inherited from `Object` or overridden) and support the `==` operator,
used to resolve collisions within a bucket.

Iteration order is **unspecified** and unrelated to insertion order: it walks
the bucket array from first to last, and within each bucket walks the
collision chain from head to tail.

### Complexity

| Operation | Time |
|-----------|------|
| `add` | Average O(1) (amortized; may trigger an O(n) rehash) |
| `remove` | Average O(1) |
| `contains` | Average O(1) |
| `size`/`isEmpty` | O(1) |
| `clear` | O(buckets) — chains are cleared, bucket capacity is left unchanged |
| Iteration (full) | O(buckets + n) |

### Usage

```k
s : HashSet<int>;
s.add(1);
s.add(2);
s.contains(1);   // true
s.remove(2);
```

Using a custom type as element requires overriding `hash()` and `==`:

```k
class Id {
    private:
    _value : int;

    public:
    Id(v : int) { _value = v; }
    const hash() : int { return _value; }
    const operator ==(other : const Id&) : bool { return _value == other._value; }
}

ids : HashSet<Id>;
ids.add(Id(1));
ids.add(Id(2));
ids.contains(Id(1));   // true
```

---

## Iterators

Each concrete set provides four iterator classes returned by `constIterator()`,
`iterator()`, `constReverseIterator()` and `reverseIterator()`:

| Set | Const forward | Mutable forward | Const reverse | Mutable reverse |
|-----|---------------|------------------|----------------|-------------------|
| `ListSet<T>` | delegates to `DoubleLinkedList<T>::constIterator()` | delegates to `DoubleLinkedList<T>::iterator()` | delegates to `DoubleLinkedList<T>::constReverseIterator()` | delegates to `DoubleLinkedList<T>::reverseIterator()` |
| `TreeSet<T>` | `TreeSetConstIterator<T>` | `TreeSetIterator<T>` | `TreeSetReverseConstIterator<T>` | `TreeSetReverseIterator<T>` |
| `HashSet<T>` | `HashSetConstIterator<T>` | `HashSetIterator<T>` | `HashSetReverseConstIterator<T>` | `HashSetReverseIterator<T>` |

All iterators implement `ConstIterator<T>`/`Iterator<T>`: `next()` returns an
`OptionalConstRef<T>` (const iterators) or `OptionalRef<T>` (mutable
iterators), empty once the traversal is exhausted. Iterators are single-use
and consumable — call `next()` in a loop until `hasValue()` is `false`.

`TreeSet<T>`'s iterators are true forward/reverse in-order traversals (sorted
order). `HashSet<T>`'s iterators traverse buckets/chains in storage order,
which is unspecified from the caller's perspective and may change across
`rehash()` calls; do not rely on any particular order or on order stability
across mutations.

---

## Memory Model

All three set classes store elements **by value**:

- **`ListSet<T>`** delegates entirely to `DoubleLinkedList<T>`'s own node
  storage (a `UniSlot<T>` per node).
- **`TreeSet<T>`** allocates one `TreeSetNode<T>` per element via `new`; each
  node stores its value in a `UniSlot<T>` and owns its two children
  (`_left`/`_right`), forming a tree of ownership rooted at an internal
  pointer. Removing the whole tree (`clear()`/destructor) walks it
  recursively to free every node.
- **`HashSet<T>`** allocates one `HashSetChain<T>` per bucket and one
  `HashSetNode<T>` per element (each with a `UniSlot<T>`). Bucket chain heads
  are tracked via a raw `Vector<HashSetChain<T>*>` for O(1) indexed lookup,
  while every chain is also independently kept alive via an intrusive
  singly-linked list rooted internally, ensuring every chain is freed exactly
  once regardless of how buckets are indexed or replaced during a rehash.

### Ownership

Sets **own** their elements: destroying a set (or calling `clear()`) destroys
every contained element. There is no shared ownership or external reference
tracking.

---

## Choosing a Set

| Need | Best choice |
|------|-------------|
| Sorted iteration / `first()`/`last()` | `TreeSet<T>` |
| Fastest average-case `add`/`remove`/`contains` | `HashSet<T>` (requires `hash()`) |
| Element type has no `hash()` and is small/rarely searched | `ListSet<T>` |
| Guaranteed O(log n) worst case (no hash collisions to worry about) | `TreeSet<T>` |
| Element type supports `<` but not `hash()` | `TreeSet<T>` |
| General purpose, best average performance | `HashSet<T>` |
