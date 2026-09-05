# Collections — `Sequence<T>`, `MutableSequence<T>`, `Collection<T>`, `Vector<T>`, `LinkedList<T>`, `DoubleLinkedList<T>`

> **Module:** `k`  
> **Source:** `libk/libk/src/collections.k`  
> **Status:** Working Draft — 2026

---

## Overview

The K standard library provides a generic collection framework built on interfaces and
concrete collection classes.

| Type | Description | Backing |
|------|-------------|---------|
| `Sequence<T>` | Read-only sequence interface (`forEach`) | (abstract) |
| `MutableSequence<T>` | Mutable sequence interface (`forEach`) | (abstract) |
| `Collection<T>` | Common interface for all collections | (abstract) |
| `LinkedList<T>` | Singly-linked list | `UniSlot<T>` per node |
| `DoubleLinkedList<T>` | Doubly-linked list | `UniSlot<T>` per node |
| `Vector<T>` | Dynamic array | `MultiSlot<T>` |

All collections store elements **by value** and provide ownership of their
contents (elements are destroyed when removed or when the collection is
destroyed).

---

## `Sequence<T>` and `MutableSequence<T>` — Interfaces

```k
template<typename T>
interface Sequence {
    const constIterator() : ConstIterator<T>!;
    default const forEach(consumer : functional::Consumer<const T&>);
    default const filter(predicate : functional::Predicate<const T&>) : Sequence<T>!;
    template<typename U>
    default const map(f : functional::Function<const T&, U>) : Sequence<U>!;
    template<typename U>
    default const flatMap(f : functional::Function<const T&, Sequence<U>!>) : Sequence<U>!;
    template<typename U>
    default const flatten() : Sequence<U>!;
    template<typename U>
    default const flatMap() : Sequence<U>!;
    template<typename C = unsigned long>
    default const count() : SequenceCount<T, C>!;
    template<typename C>
    default const count(c : C&) : SequenceCount<T, C>!;
    default const skip(skip : unsigned long, after : unsigned long = 0) : Sequence<T>!;
    default const getFirst() : Optional<T>;
    default const collect(collection : Appendable<T>&);
    default const collect(collector : functional::Consumer<const T&>);
    template<typename R>
    default const accumulate(initial : const R&, accumulator : functional::BiFunction<R, const T&, R>) : R;
}

template<typename T>
interface MutableSequence : public Sequence<T> {
    iterator() : Iterator<T>!;
    default forEach(consumer : functional::Consumer<T&>);
}
```

### Methods

| Method | Description |
|--------|-------------|
| `const constIterator() : ConstIterator<T>!` | Return a read-only iterator over elements. |
| `const forEach(consumer : Consumer<const T&>)` | Invoke `consumer` with a `const T&` reference for each element in sequence order. |
| `const filter(predicate : Predicate<const T&>) : Sequence<T>!` | Return a lazy filtered sequence containing elements satisfying `predicate`. |
| `const map<U>(f : Function<const T&, U>) : Sequence<U>!` | Return a lazy mapped sequence containing elements transformed by `f`. |
| `const flatMap<U>(f : Function<const T&, Sequence<U>!>) : Sequence<U>!` | Return a lazy sequence containing flattened results of mapping each element to a `Sequence<U>!`. |
| `const flatten<U>() : Sequence<U>!` | Flatten a sequence of sequences into a single sequence. |
| `const flatMap<U>() : Sequence<U>!` | Convenience alias for `flatten<U>()`. |
| `const count<C = unsigned long>() : SequenceCount<T, C>!` | Return a sequence tracking traversed elements in an internal counter (`unsigned long` by default, accessible via `.getCount()`). |
| `const count<C>(c : C&) : SequenceCount<T, C>!` | Return a sequence incrementing external accumulator `c` via `operator++_()` without resetting it. |
| `const skip(skip : unsigned long, after : unsigned long = 0) : Sequence<T>!` | Return a lazy sequence passing `after` elements, skipping the next `skip` elements, and passing all subsequent elements. |
| `const getFirst() : Optional<T>` | Return an `Optional<T>` containing a copy of the first element, or empty if the sequence is empty. |
| `const collect(collection : Appendable<T>&)` | Append all elements into an `Appendable<T>` collection. |
| `const collect(collector : Consumer<const T&>)` | Feed all elements to a consumer callback. |
| `const accumulate<R>(initial : const R&, accumulator : BiFunction<R, const T&, R>) : R` | Left-fold reduction combining elements with an accumulator. |
| `iterator() : Iterator<T>!` | Return a mutable iterator over elements. |
| `forEach(consumer : Consumer<T&>)` | Invoke `consumer` with a mutable `T&` reference for each element in sequence order. |

---

## `Collection<T>` — Interface

```k
template<typename T>
interface Collection : public MutableSequence<T> {
    getSize() : int;
    isEmpty() : bool;
    pushFront(value : T&);
    pushBack(value : T&);
    insert(index : int, value : T&);
    peekFront() : T&;
    peekBack() : T&;
    get(index : int) : T&;
    removeFront() : bool;
    removeBack() : bool;
    clear();
}
```

### Methods

| Method | Description |
|--------|-------------|
| `getSize() : int` | Return the number of elements. |
| `isEmpty() : bool` | Return `true` if the collection is empty. |
| `pushFront(value : T&)` | Insert a copy of `value` at the front. |
| `pushBack(value : T&)` | Insert a copy of `value` at the back. |
| `insert(index, value)` | Insert a copy at the given index (clamped to `[0, size]`). |
| `peekFront() : T&` | Mutable reference to the front element. Undefined if empty. |
| `peekBack() : T&` | Mutable reference to the back element. Undefined if empty. |
| `get(index) : T&` | Mutable reference to element at `index`. Undefined if out of bounds. |
| `removeFront() : bool` | Remove the front element. Returns `true` if removed. |
| `removeBack() : bool` | Remove the back element. Returns `true` if removed. |
| `clear()` | Remove all elements. |

### Exceptions

All insertion methods (`pushFront`, `pushBack`, `insert`) may throw
`ConstructionException` (a `FatalError`) when the underlying
`UniSlot<T>::construct()` or `MultiSlot<T>::construct()` call fails.
As a `FatalError`, it does not need to be declared in `throws` clauses.

### Polymorphism

Any function accepting a `Collection<T>&` parameter can operate on any
concrete collection type:

```k
fillAndSum(coll : Collection<int>&) : int {
    a : int = 10;
    b : int = 20;
    coll.pushBack(a);
    coll.pushBack(b);
    return coll.peekFront() + coll.peekBack();
}

test() : int {
    vec : Vector<int>;
    return fillAndSum(vec);   // 30
}
```

---

## `LinkedList<T>`

```k
template<typename T>
class LinkedList : public Collection<T> { ... }
```

### Description

A singly-linked list storing elements by value. Each element is embedded in a
heap-allocated `LinkedListNode` via a `UniSlot<T>`. Ownership of nodes is
chained through owner pointers: the list owns the head, each node owns its
successor.

### Complexity

| Operation | Time |
|-----------|------|
| `pushFront` | O(1) |
| `pushBack` | O(1) |
| `insert(index)` | O(n) |
| `peekFront` | O(1) |
| `peekBack` | O(1) |
| `get(index)` | O(n) |
| `removeFront` | O(1) |
| `removeBack` | O(n) |
| `clear` | O(n) |

### Additional Methods

Beyond the `Collection<T>` interface, `LinkedList<T>` provides:

| Method | Description |
|--------|-------------|
| `emplaceFront(Args...args)` | Construct element in-place at the front. |
| `emplaceBack(Args...args)` | Construct element in-place at the back. |
| `emplace(index, Args...args)` | Construct element in-place at index. |
| `operator [](index) : T&` | Subscript access (O(n)). |

### Usage

```k
lst : LinkedList<int>;
a : int = 10;
b : int = 20;
try {
    lst.pushBack(a);
    lst.pushFront(b);
} catch (e: ConstructionException&) {
    // handle fatal construction failure (optional — FatalError propagates by default)
}
front : int& = lst.peekFront();   // 20
lst.removeFront();
lst.clear();
```

---

## `DoubleLinkedList<T>`

```k
template<typename T>
class DoubleLinkedList : public Collection<T> { ... }
```

### Description

A doubly-linked list storing elements by value. Similar to `LinkedList<T>` but
each node also has a back-pointer to the predecessor, enabling O(1)
`removeBack` and indexed access from the nearest end.

### Complexity

| Operation | Time |
|-----------|------|
| `pushFront` | O(1) |
| `pushBack` | O(1) |
| `insert(index)` | O(n) — searches from nearest end |
| `peekFront` | O(1) |
| `peekBack` | O(1) |
| `get(index)` | O(n/2) — searches from nearest end |
| `removeFront` | O(1) |
| `removeBack` | O(1) |
| `clear` | O(n) |

### Additional Methods

Beyond the `Collection<T>` interface, `DoubleLinkedList<T>` provides:

| Method | Description |
|--------|-------------|
| `emplaceFront(Args...args)` | Construct element in-place at the front. |
| `emplaceBack(Args...args)` | Construct element in-place at the back. |
| `emplace(index, Args...args)` | Construct element in-place at index. |
| `operator [](index) : T&` | Subscript access (O(n/2), searches from nearest end). |

### Usage

```k
lst : DoubleLinkedList<int>;
a : int = 42;
b : int = 10;
lst.pushBack(a);
lst.pushFront(b);
back : int& = lst.peekBack();    // 42
lst.removeBack();                // O(1) thanks to double linking
lst.clear();
```

---

## `Vector<T>`

```k
template<typename T>
class Vector : public Collection<T> { ... }
```

### Description

A dynamic array storing elements contiguously in memory via a `MultiSlot<T>`.
Provides O(1) indexed access and amortized O(1) `pushBack`. The buffer grows
with a factor of 2 when capacity is exceeded.

### Complexity

| Operation | Time |
|-----------|------|
| `pushFront` | O(n) |
| `pushBack` | O(1) amortized |
| `insert(index)` | O(n) |
| `peekFront` | O(1) |
| `peekBack` | O(1) |
| `get(index)` | O(1) |
| `removeFront` | O(n) |
| `removeBack` | O(1) |
| `clear` | O(n) |

### Additional Methods

Beyond the `Collection<T>` interface, `Vector<T>` provides:

| Method | Description |
|--------|-------------|
| `getCapacity() : int` | Return the current buffer capacity. |
| `reserve(minCapacity)` | Ensure capacity for at least `minCapacity` elements. |
| `emplaceBack(Args...args)` | Construct element in-place at the back. |
| `emplace(index, Args...args)` | Construct element in-place at index. |
| `removeAt(index) : bool` | Remove element at a given index (shifts left). |
| `operator [](index) : T&` | Subscript access (O(1)). |

### Usage

```k
vec : Vector<int>;
a : int = 1;
b : int = 2;
c : int = 3;
vec.pushBack(a);
vec.pushBack(b);
vec.pushBack(c);
vec.get(1) = 99;               // direct indexed access
vec.removeAt(0);               // remove first element, shift left
vec.reserve(100);              // pre-allocate buffer
vec.clear();
```

---

## Memory Model

All three collection classes store elements **by value** using the memory
primitives from `memory.k`:

- **Linked lists** allocate a node per element; each node contains a
  `UniSlot<T>` that provides explicit lifetime control over the stored value.
- **Vector** uses a single `MultiSlot<T>` buffer with explicit per-index
  `construct`/`destruct` calls.

Elements are:
- **Constructed** (copied from the `T&` argument) when inserted via `push*`/`insert`
- **Constructed in-place** (forwarding arguments to `T`'s constructor) via `emplace*`
- **Destroyed** when removed via `remove*`/`clear`, or when the collection itself is destroyed

### Ownership

Collections **own** their elements. There is no shared ownership or external
reference tracking. Destroying a collection destroys all contained elements.

---

## Choosing a Collection

| Need | Best choice |
|------|-------------|
| O(1) random access by index | `Vector<T>` |
| O(1) push/pop at both ends | `DoubleLinkedList<T>` |
| Minimal memory per element | `Vector<T>` |
| Frequent front insertions/removals | `LinkedList<T>` or `DoubleLinkedList<T>` |
| Stable element addresses (no reallocation) | `LinkedList<T>` or `DoubleLinkedList<T>` |
| General purpose, best average performance | `Vector<T>` |

