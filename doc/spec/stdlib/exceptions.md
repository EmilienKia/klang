# K Standard Library — Exception Types

> **Source:** `libk/libk/src/exception.k`  
> **Module:** `k` (auto-imported)

---

## Overview

The K standard library provides a hierarchy of throwable types for structured
error handling. All types reside in the `::k` namespace and are available
without an explicit import.

The hierarchy separates **checked exceptions** (`Exception` subtypes) that must
be declared in a function's `throws` clause, from **unchecked fatal errors**
(`FatalError` subtypes) that propagate freely without declaration.

---

## Class Hierarchy

```
Throwable (_code: int)                    root of all throwable types
├── Exception                             checked — must declare `throws`
│   ├── NullPointerException (code=2)
│   ├── IndexOutOfBoundsException (code=3)
│   ├── IllegalArgumentException (code=4)
│   └── IllegalStateException (code=5)
└── FatalError                            unchecked — no `throws` needed
    ├── OutOfMemory (code=1)
    ├── ConstructionException (code=6)
    ├── NullPointerError (code=7)
    │   ├── NullDereferenceError (code=8)
    │   ├── NullAssignationError (code=9)
    │   └── NullCastError (code=10)
    └── IndexOutOfBoundsError (code=11)
```

---

## Throwable

Root base class for all types that can be thrown in K.

### Fields

| Field | Type | Description |
|-------|------|-------------|
| `_code` | `int` | Integer error code (default: 0) |

### Constructors

| Signature | Description |
|-----------|-------------|
| `Throwable()` | Construct with code 0 |
| `Throwable(code: int)` | Construct with the given error code |

### Methods

| Signature | Description |
|-----------|-------------|
| `const getCode() : int` | Return the error code |

---

## Exception

Base class for **checked** exceptions. Functions that throw `Exception`-derived
types must declare them in their `throws` clause. Extends `Throwable`.

### Constructors

| Signature | Description |
|-----------|-------------|
| `Exception()` | Construct with default code (0) |
| `Exception(code: int)` | Construct with the given error code |

---

## FatalError

Base class for **unchecked** fatal errors. Functions that throw `FatalError`-derived
types do NOT need to declare them. They represent conditions that any code may
encounter at any time. Extends `Throwable`.

### Constructors

| Signature | Description |
|-----------|-------------|
| `FatalError()` | Construct with default code (0) |
| `FatalError(code: int)` | Construct with the given error code |

---

## OutOfMemory

Signals a memory allocation failure. Extends `FatalError`.

Default error code: **1**.

Thrown by the runtime when `new` or `MultiSlot<T>::allocate/reallocate` cannot
satisfy the allocation request.

### Constructors

| Signature | Description |
|-----------|-------------|
| `OutOfMemory()` | Construct with code 1 |
| `OutOfMemory(code: int)` | Construct with a custom code |

---

## NullPointerException

Signals an attempt to dereference a null pointer. Extends `Exception`.

Default error code: **2**.

### Constructors

| Signature | Description |
|-----------|-------------|
| `NullPointerException()` | Construct with code 2 |
| `NullPointerException(code: int)` | Construct with a custom code |

---

## IndexOutOfBoundsException

Signals an index outside the valid range for an array or collection.
Extends `Exception`.

Default error code: **3**.

### Constructors

| Signature | Description |
|-----------|-------------|
| `IndexOutOfBoundsException()` | Construct with code 3 |
| `IndexOutOfBoundsException(code: int)` | Construct with a custom code |

---

## IllegalArgumentException

Signals that a function received an inappropriate or invalid argument.
Extends `Exception`.

Default error code: **4**.

### Constructors

| Signature | Description |
|-----------|-------------|
| `IllegalArgumentException()` | Construct with code 4 |
| `IllegalArgumentException(code: int)` | Construct with a custom code |

---

## IllegalStateException

Signals that an object is not in an appropriate state for the requested
operation. Extends `Exception`.

Default error code: **5**.

### Constructors

| Signature | Description |
|-----------|-------------|
| `IllegalStateException()` | Construct with code 5 |
| `IllegalStateException(code: int)` | Construct with a custom code |

---

## ConstructionException

Signals that an object constructor threw a checked exception during
`UniSlot<T>::construct()` or `MultiSlot<T>::construct()`. The original
exception is intercepted and replaced by this `ConstructionException`.
Other `FatalError`-derived exceptions are **not** intercepted and propagate normally.
Extends `FatalError` — does not need to be declared in `throws` clauses.

Default error code: **6**.

### Constructors

| Signature | Description |
|-----------|-------------|
| `ConstructionException()` | Construct with code 6 |
| `ConstructionException(code: int)` | Construct with a custom code |

---

## NullPointerError

Base class for all null-pointer related fatal errors. Thrown by the runtime
when a null pointer is used in an invalid context.
Extends `FatalError` — does not need to be declared in `throws` clauses.

Default error code: **7**.

### Constructors

| Signature | Description |
|-----------|-------------|
| `NullPointerError()` | Construct with code 7 |
| `NullPointerError(code: int)` | Construct with a custom code |

---

## NullDereferenceError

Signals a dereference of a null pointer. Thrown by the runtime when code
attempts to dereference a null owner, pointer, or link.
Extends `NullPointerError`.

Default error code: **8**.

### Constructors

| Signature | Description |
|-----------|-------------|
| `NullDereferenceError()` | Construct with code 8 |
| `NullDereferenceError(code: int)` | Construct with a custom code |

---

## NullAssignationError

Signals an assignment through a null pointer. Thrown by the runtime when
code attempts to bind a null value to a non-nullable link or reference.
Extends `NullPointerError`.

Default error code: **9**.

### Constructors

| Signature | Description |
|-----------|-------------|
| `NullAssignationError()` | Construct with code 9 |
| `NullAssignationError(code: int)` | Construct with a custom code |

---

## NullCastError

Signals that a dynamic cast produced null for a non-nullable target type
(reference or link). Thrown by the runtime when a dynamic cast to a
non-nullable addressor fails.
Extends `NullPointerError`.

Default error code: **10**.

### Constructors

| Signature | Description |
|-----------|-------------|
| `NullCastError()` | Construct with code 10 |
| `NullCastError(code: int)` | Construct with a custom code |

---

## IndexOutOfBoundsError

Signals an array index outside the valid range. Thrown by the runtime when
an array subscript operation exceeds the array bounds.
Extends `FatalError` — does not need to be declared in `throws` clauses.

Default error code: **11**.

### Constructors

| Signature | Description |
|-----------|-------------|
| `IndexOutOfBoundsError()` | Construct with code 11 |
| `IndexOutOfBoundsError(code: int)` | Construct with a custom code |

---

## Checked vs Unchecked Semantics

| Category | Base class | `throws` declaration | Example |
|----------|-----------|---------------------|---------|
| **Checked** | `Exception` | Required | `IllegalArgumentException` |
| **Unchecked** | `FatalError` | Not required | `OutOfMemory` |

- A `throw` statement accepts any `Throwable`-derived type.
- A `catch` clause accepts any `Throwable`-derived type.
- The `throws` clause on a function only needs to list `Exception`-derived types.
- `FatalError`-derived types propagate freely without `throws` declarations.

---

## Usage Example

```k
// Checked exception: must be declared in throws clause
riskyOperation() : int throws IllegalArgumentException {
    throw IllegalArgumentException();
}

safeWrapper() : int {
    result : int = 0;
    try {
        result = riskyOperation();
    } catch (e: IllegalArgumentException&) {
        result = -1;
    }
    return result;
}

// FatalError (OutOfMemory): no throws declaration needed —
// it's thrown by the runtime on allocation failure and can be
// caught if desired, but doesn't require explicit propagation.
allocator() : int* {
    return new int;  // may throw OutOfMemory on failure
}
```

---

## See Also

- [Exception Handling (Language Spec)](../language/statements/exceptions.md)
- [Language Summary §27](../language/summary.md#27-exception-handling)
- [Grammar — ThrowStatement, TryCatchStatement](../language/grammar.ebnf)
