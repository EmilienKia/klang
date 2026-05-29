# Optional<T> — Type-Safe Optional Value

> **Module:** `k`  
> **Source:** `libk/libk/src/optional.k`  
> **Status:** Working Draft — 2026

---

## Overview

`Optional<T>` is a type-safe wrapper that either contains a value of type `T` or
is empty. It avoids heap allocation by storing the value inline using
`UniSlot<T>` as backing storage together with a boolean flag.

This is the K equivalent of C++17's `std::optional<T>`.

---

## Definition

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

---

## Constructors & Destructor

| Method | Description |
|--------|-------------|
| `Optional()` | Create an empty optional (no value). |
| `Optional(value : T&)` | Create an optional holding a copy of `value`. May throw if `T`'s copy constructor throws. |
| `Optional(other : Optional<T>&)` | Copy constructor. If `other` has a value, copy it; otherwise create empty. May throw if `T`'s copy constructor throws. |
| `~Optional()` | Destructor. If a value is present, destroys it via `UniSlot::destruct()`. |

---

## Methods

| Method | Description |
|--------|-------------|
| `hasValue() : bool` | Return `true` if this optional holds a value. |
| `get() : T&` | Return a reference to the contained value. **Precondition:** `hasValue()` is `true`. |
| `getOr(defaultValue : T&) : T` | Return the contained value if present, or `defaultValue` otherwise. |
| `set(value : T&)` | Set the optional to hold a copy of `value`. If a value was already present, it is destroyed first. May throw if `T`'s copy constructor throws. |
| `reset()` | Clear the optional. If a value is present, it is destroyed. After `reset()`, `hasValue()` returns `false`. |

---

## Static Factory

| Method | Description |
|--------|-------------|
| `static empty() : Optional<T>` | Create an empty `Optional<T>`. Equivalent to the default constructor; provided for expressiveness at call sites. |

---

## Usage

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

---

## Undefined Behaviour

- Calling `get()` on an empty optional (when `hasValue()` is `false`).

---

## Design Rationale

`Optional<T>` uses `UniSlot<T>` as the underlying storage mechanism. This
provides:

1. **No heap allocation** — the value is stored inline within the struct layout.
2. **Explicit lifetime control** — construction and destruction of `T` happen
   only when the optional transitions between empty and value-holding states.
3. **Zero overhead when empty** — an empty optional is just a `UniSlot` (zeroed
   storage) plus a `false` boolean.

The boolean `_hasValue` flag tracks whether the slot contains a live object.
All mutating operations (`set`, `reset`, destructor) check this flag to ensure
correct construction/destruction sequencing.



