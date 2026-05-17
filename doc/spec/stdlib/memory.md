# Memory Primitives — `UniSlot<T>` & `MultiSlot<T>`

> **Module:** `k`  
> **Source:** `libk/libk/src/memory.k`  
> **Status:** Working Draft — 2026

---

## Overview

The K standard library provides two low-level memory primitive templates for
**explicit lifetime management** of objects. These are building blocks for
higher-level containers (such as `Vector<T>` and the linked-list types).

| Type | Purpose |
|------|---------|
| `UniSlot<T>` | Raw storage for a **single** object of type `T` |
| `MultiSlot<T>` | Raw storage for a **dynamically-sized array** of `T` objects |

Both types manage raw memory **without** automatically calling constructors or
destructors on the contained objects. The user is responsible for explicitly
invoking `construct()` / `destruct()` to control object lifetimes.

---

## `UniSlot<T>`

```k
template<typename T>
struct UniSlot {
    UniSlot();
    ~UniSlot();

    template<typename...Args>
    construct(Args...args);

    destruct();

    get() : T&;
}
```

### Description

`UniSlot<T>` embeds storage for exactly one `T` directly in its own struct
layout (no heap allocation). The `UniSlot` constructor/destructor only manage
the storage region — they do **not** call `T`'s constructor or destructor.

This is the K equivalent of C++'s `std::aligned_storage` + placement new.

### Methods

| Method | Description |
|--------|-------------|
| `UniSlot()` | Initialize the storage region (no `T` construction). |
| `~UniSlot()` | Finalize the storage region (no `T` destruction). |
| `construct(Args...args)` | Construct a `T` in the storage, forwarding `args` to `T`'s constructor. |
| `destruct()` | Destroy the `T` in the storage (calls `T`'s destructor). |
| `get() : T&` | Return a mutable reference to the stored `T`. |

### Intrinsics

All lifecycle methods are compiler intrinsics (`@annotations::Intrinsic`):
- `UniSlot::constructor` — zero-initializes the raw storage
- `UniSlot::destructor` — no-op (does not destroy the contained object)
- `UniSlot::construct` — placement-constructs `T` with forwarded arguments
- `UniSlot::destruct` — calls `T`'s destructor in-place

### Usage

```k
slot : UniSlot<Point>;
slot.construct<int, int>(10, 20);   // construct Point(10, 20)
slot.get().x = 42;                  // access the stored Point
slot.destruct();                    // destroy the Point
// slot goes out of scope — no double-destruct
```

### Undefined Behaviour

- Calling `destruct()` without a prior `construct()`
- Calling `construct()` twice without an intervening `destruct()`
- Calling `get()` on unconstructed storage

---

## `MultiSlot<T>`

```k
template<typename T>
struct MultiSlot {
    MultiSlot();
    ~MultiSlot();

    allocate(capacity : int);
    reallocate(newCapacity : int);
    deallocate();

    template<typename...Args>
    construct(index : int, Args...args);

    destruct(index : int);

    get(index : int) : T&;

    const getCapacity() : int;
}
```

### Description

`MultiSlot<T>` manages a heap-allocated buffer of uninitialized storage that
can hold up to `capacity` objects of type `T`. Memory is managed via
`malloc`/`realloc`/`free`, but individual objects must be explicitly
constructed and destroyed by the user.

This is the array counterpart to `UniSlot<T>` and is the storage backing for
`Vector<T>`.

### Methods

| Method | Description |
|--------|-------------|
| `MultiSlot()` | Initialize (null buffer, capacity = 0). |
| `~MultiSlot()` | Finalize (does **not** free buffer or destroy elements). |
| `allocate(capacity)` | Allocate a buffer for `capacity` elements. |
| `reallocate(newCapacity)` | Grow/shrink the buffer. Preserves raw content up to the old capacity. |
| `deallocate()` | Free the buffer. |
| `construct(index, Args...args)` | Placement-construct a `T` at `index`, forwarding `args`. |
| `destruct(index)` | Call `T`'s destructor on the element at `index`. |
| `get(index) : T&` | Return a mutable reference to the element at `index`. |
| `getCapacity() : int` | Return the current buffer capacity. |

### Intrinsics

All lifecycle and access methods are compiler intrinsics:
- `MultiSlot::constructor` — initializes `_data = null`, `_capacity = 0`
- `MultiSlot::destructor` — no-op
- `MultiSlot::allocate` — `malloc(capacity * sizeof(T))`
- `MultiSlot::reallocate` — `realloc(_data, newCapacity * sizeof(T))`
- `MultiSlot::deallocate` — `free(_data)`
- `MultiSlot::construct` — placement-constructs at `_data + index`
- `MultiSlot::destruct` — calls destructor at `_data + index`
- `MultiSlot::get` — returns `_data[index]`

### Usage

```k
slots : MultiSlot<Point>;
slots.allocate(10);                     // allocate buffer for 10 Points
slots.construct<int, int>(0, 1, 2);     // construct Point(1,2) at index 0
slots.get(0).x = 99;                    // access element
slots.destruct(0);                      // destroy Point at index 0
slots.deallocate();                     // free the buffer
```

### Reallocation

`reallocate()` preserves raw memory content but does **not** guarantee object
validity beyond the old capacity. The caller must track which indices contain
live objects.

```k
slots.reallocate(20);   // grow buffer to 20 elements
// indices 0..9 still contain their raw bytes; 10..19 are uninitialized
```

### Undefined Behaviour

- Calling `destruct(i)` on an unconstructed index
- Calling `construct(i, ...)` on an already-constructed index without intervening `destruct(i)`
- Calling `get(i)` on an unconstructed index
- Accessing indices outside `[0, capacity)`

---

## Design Rationale

These primitives exist because K does not (yet) have a built-in placement-new
mechanism. They provide the building blocks for:

- **`Vector<T>`** — uses `MultiSlot<T>` for contiguous growable storage
- **`LinkedList<T>`** / **`DoubleLinkedList<T>`** — use `UniSlot<T>` inside each node to store
  elements by value without triggering automatic construction at node allocation time

By separating allocation from construction, collection types can manage object
lifetimes precisely — constructing elements only when inserted and destructing
them only when removed.

