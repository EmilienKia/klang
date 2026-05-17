# Collections — `Collection<T>`, `Vector<T>`, `LinkedList<T>`, `DoubleLinkedList<T>`

> **Module:** `k`  
> **Source:** `libk/libk/src/collections.k`  
> **Status:** Working Draft — 2026

---

## Overview

The K standard library provides a generic collection framework built on three
concrete collection classes that all implement the `Collection<T>` interface.

| Type | Description | Backing |
|------|-------------|---------|
| `Collection<T>` | Common interface for all collections | (abstract) |
| `LinkedList<T>` | Singly-linked list | `UniSlot<T>` per node |
| `DoubleLinkedList<T>` | Doubly-linked list | `UniSlot<T>` per node |
| `Vector<T>` | Dynamic array | `MultiSlot<T>` |

All collections store elements **by value** and provide ownership of their
contents (elements are destroyed when removed or when the collection is
destroyed).

---

## `Collection<T>` — Interface

```k
template<typename T>
interface Collection {
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
lst.pushBack(a);
lst.pushFront(b);
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

