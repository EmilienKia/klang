# Optional<T>, OptionalConstRef<T>, OptionalRef<T> — Type-Safe Optional Wrappers

> **Module:** `k`  
> **Source:** `libk/libk/src/optional.k`  
> **Status:** Working Draft — 2026

---

## Overview

This file provides three related types for expressing optional/nullable values and
references in K:

| Type | Ownership | Mutability | Storage |
|------|-----------|------------|---------|
| `Optional<T>` | Owning — holds a copy of `T` | `T` is mutable via `set()` | Stack-allocated via `UniSlot<T>` |
| `OptionalRef<T>` | Non-owning — points to an existing `T` | `T` is mutable through `get()` | Nullable pointer `T*` |
| `OptionalConstRef<T>` | Non-owning — points to an existing `T` | Read-only access via `get()` | Nullable pointer `T*` |

`Optional<T>` avoids heap allocation by storing the value inline using `UniSlot<T>`
and a boolean flag. It is the K equivalent of C++17's `std::optional<T>`.

`OptionalRef<T>` and `OptionalConstRef<T>` are lightweight non-owning wrappers —
they store a nullable pointer to an externally owned object. They are the K
equivalent of a nullable reference or pointer.

An `OptionalRef<T>` can be widened to an `OptionalConstRef<T>` (mutable-to-read-only).

---

## Optional<T>

### Definition

```k
template<typename T>
struct Optional {
    Optional();
    Optional(value : T&);
    Optional(other : Optional<T>&);
    ~Optional();

    const hasValue() : bool;
    const get() : T&;
    const getOr(defaultValue : T&) : T;

    set(value : T&);
    reset();

    static empty() : Optional<T>;
}
```

### Constructors & Destructor

| Method | Description |
|--------|-------------|
| `Optional()` | Create an empty optional (no value). |
| `Optional(value : T&)` | Create an optional holding a copy of `value`. May throw if `T`'s copy constructor throws. |
| `Optional(other : Optional<T>&)` | Copy constructor. If `other` has a value, copy it; otherwise create empty. May throw if `T`'s copy constructor throws. |
| `~Optional()` | Destructor. If a value is present, destroys it via `UniSlot::destruct()`. |

### Methods

| Method | Description |
|--------|-------------|
| `hasValue() : bool` | Return `true` if this optional holds a value. |
| `get() : T&` | Return a reference to the contained value. **Precondition:** `hasValue()` is `true`. |
| `getOr(defaultValue : T&) : T` | Return the contained value if present, or `defaultValue` otherwise. |
| `set(value : T&)` | Set the optional to hold a copy of `value`. If a value was already present, it is destroyed first. |
| `reset()` | Clear the optional. If a value is present, it is destroyed. After `reset()`, `hasValue()` returns `false`. |

### Static Factory

| Method | Description |
|--------|-------------|
| `static empty() : Optional<T>` | Create an empty `Optional<T>`. Equivalent to the default constructor; provided for expressiveness at call sites. |

### Usage

```k
// Create an empty optional
opt : Optional<int>;
// opt.hasValue() == false

// Create with a value
opt2 : Optional<int>(42);
// opt2.hasValue() == true, opt2.get() == 42

// Create an empty optional via static factory (explicit form)
opt3 : Optional<int> = Optional<int>::empty();
// opt3.hasValue() == false

// Set a value on an empty optional
opt.set(10);
// opt.hasValue() == true, opt.get() == 10

// Replace existing value
opt.set(20);
// opt.get() == 20

// Clear the value
opt.reset();
// opt.hasValue() == false

// Safe access with default
val : int = opt.getOr(0);  // returns 0 since opt is empty
```

### Design Rationale

`Optional<T>` uses `UniSlot<T>` as the underlying storage mechanism. This provides:

1. **No heap allocation** — the value is stored inline within the struct layout.
2. **Explicit lifetime control** — construction and destruction of `T` happen
   only when the optional transitions between empty and value-holding states.
3. **Zero overhead when empty** — an empty optional is just a `UniSlot` (zeroed
   storage) plus a `false` boolean.

---

## OptionalRef<T>

A non-owning, mutable reference wrapper. Stores a nullable mutable pointer to an
externally managed `T`. Rebind with `set()`; read and write through `get()` / `operator*()`.

### Definition

```k
template<typename T>
struct OptionalRef {
    OptionalRef();
    OptionalRef(value : T&);
    OptionalRef(other : const OptionalRef<T>&);

    const hasValue() : bool;
    const get() : T&;
    const operator*() : T&;
    const getOr(defaultValue : T&) : T&;

    set(value : T&);
}
```

### Constructors

| Method | Description |
|--------|-------------|
| `OptionalRef()` | Create an empty `OptionalRef` (no reference). |
| `OptionalRef(value : T&)` | Store a pointer to `value`. Does not copy. |
| `OptionalRef(other : const OptionalRef<T>&)` | Copy constructor — both instances point to the same object. |

### Methods

| Method | Description |
|--------|-------------|
| `hasValue() : bool` | Return `true` if a reference is held. |
| `get() : T&` | Return a mutable reference to the referenced object. **Precondition:** `hasValue()` is `true`. |
| `operator*() : T&` | Equivalent to `get()`. Note: currently only callable via `opt.get()` — K's built-in `*` works on pointer types only, not struct values. |
| `getOr(defaultValue : T&) : T&` | Return the referenced value if present, or `defaultValue` otherwise. |
| `set(value : T&)` | Rebind to a different object. |

### Usage

```k
x : int = 42;
y : int = 99;

opt : OptionalRef<int>(x);   // points to x
opt.get() == 42;             // read
opt.get() = 7;               // write (modifies x)
x == 7;                      // true

opt.set(y);                  // rebind to y
opt.get() == 99;             // true

opt2 : OptionalRef<int>;     // empty
opt2.hasValue() == false;

def : int = 0;
opt2.getOr(def) == 0;        // returns defaultValue
```

### Notes

- **Does not own** the referenced object. The referenced object must outlive the `OptionalRef`.
- Mutations through `get()` directly modify the original variable.
- `OptionalRef<T>` can be implicitly widened to `OptionalConstRef<T>` via the
  `OptionalConstRef(other : const OptionalRef<T>&)` constructor.

---

## OptionalConstRef<T>

A non-owning, read-only reference wrapper. Stores a nullable pointer to an
externally managed `T` and exposes only immutable (`const T&`) access.

### Definition

```k
template<typename T>
struct OptionalConstRef {
    OptionalConstRef();
    OptionalConstRef(value : T&);
    OptionalConstRef(other : const OptionalConstRef<T>&);
    OptionalConstRef(other : const OptionalRef<T>&);   // mutable-to-read-only widening

    const hasValue() : bool;
    const get() : const T&;
    const operator*() : const T&;
    const getOr(defaultValue : const T&) : const T&;
}
```

### Constructors

| Method | Description |
|--------|-------------|
| `OptionalConstRef()` | Create an empty `OptionalConstRef` (no reference). |
| `OptionalConstRef(value : T&)` | Store a pointer to `value`. Does not copy. Exposes read-only access only. |
| `OptionalConstRef(other : const OptionalConstRef<T>&)` | Copy constructor — both instances point to the same object. |
| `OptionalConstRef(other : const OptionalRef<T>&)` | Widening constructor — creates a read-only view from a mutable `OptionalRef<T>`. |

### Methods

| Method | Description |
|--------|-------------|
| `hasValue() : bool` | Return `true` if a reference is held. |
| `get() : const T&` | Return a read-only reference to the referenced object. **Precondition:** `hasValue()` is `true`. |
| `operator*() : const T&` | Equivalent to `get()`. Note: currently only callable via `opt.get()` — K's built-in `*` works on pointer types only, not struct values. |
| `getOr(defaultValue : const T&) : const T&` | Return the referenced value if present, or `defaultValue` otherwise. |

### Usage

```k
x : int = 10;
y : int = 20;

// Construct from a mutable reference
cr : OptionalConstRef<int>(x);
cr.hasValue() == true;
cr.get() == 10;               // read-only access

// Reflects external mutations
x = 99;
cr.get() == 99;               // true

// Construct from OptionalRef (widening)
ref : OptionalRef<int>(y);
cr2 : OptionalConstRef<int>(ref);
cr2.get() == 20;

// Empty form
empty : OptionalConstRef<int>;
empty.hasValue() == false;
def : int = 0;
empty.getOr(def) == 0;
```

### Design Note

K has no mutable-pointer-to-const type (there is no `const T*` in K). Immutability
in `OptionalConstRef<T>` is enforced at the API level: all accessors return
`const T&`, so the caller cannot modify the referenced object through this wrapper.
The stored pointer is a `T*` internally, but the wrapper provides no method to
write through it.

---

## Undefined Behaviour

- Calling `get()` or `operator*()` on an empty optional/ref/constref (when `hasValue()` is `false`).
- Letting the referenced object go out of scope while an `OptionalRef` or `OptionalConstRef` still holds a pointer to it.

