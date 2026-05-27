# K Standard Library — Exception Types

> **Source:** `libk/libk/src/exception.k`  
> **Module:** `k` (auto-imported)

---

## Overview

The K standard library provides a hierarchy of exception classes for structured
error handling. All exception types reside in the `::k` namespace and are
available without an explicit import.

Exception types are classes that can be thrown with the `throw` statement and
caught with `try-catch`. They use integer error codes to identify the error
category.

---

## Class Hierarchy

```
Exception                         (base, code = 0)
└── RuntimeException              (programming errors)
    ├── MemoryException           (code = 1, allocation failures)
    ├── NullPointerException      (code = 2, null dereference)
    ├── IndexOutOfBoundsException (code = 3, invalid index)
    ├── IllegalArgumentException  (code = 4, invalid argument)
    └── IllegalStateException     (code = 5, invalid object state)
```

---

## Exception

Root base class for all K exceptions.

### Fields

| Field | Type | Description |
|-------|------|-------------|
| `_code` | `int` | Integer error code (default: 0) |

### Constructors

| Signature | Description |
|-----------|-------------|
| `Exception()` | Construct with code 0 |
| `Exception(code: int)` | Construct with the given error code |

### Methods

| Signature | Description |
|-----------|-------------|
| `const getCode() : int` | Return the error code |

---

## RuntimeException

Base class for exceptions indicating programming errors or unexpected runtime
conditions. Extends `Exception`.

### Constructors

| Signature | Description |
|-----------|-------------|
| `RuntimeException()` | Construct with default code (0) |
| `RuntimeException(code: int)` | Construct with the given error code |

---

## MemoryException

Signals a memory allocation failure. Extends `RuntimeException`.

Default error code: **1**.

### Constructors

| Signature | Description |
|-----------|-------------|
| `MemoryException()` | Construct with code 1 |
| `MemoryException(code: int)` | Construct with a custom code |

---

## NullPointerException

Signals an attempt to dereference a null pointer. Extends `RuntimeException`.

Default error code: **2**.

### Constructors

| Signature | Description |
|-----------|-------------|
| `NullPointerException()` | Construct with code 2 |
| `NullPointerException(code: int)` | Construct with a custom code |

---

## IndexOutOfBoundsException

Signals an index outside the valid range for an array or collection.
Extends `RuntimeException`.

Default error code: **3**.

### Constructors

| Signature | Description |
|-----------|-------------|
| `IndexOutOfBoundsException()` | Construct with code 3 |
| `IndexOutOfBoundsException(code: int)` | Construct with a custom code |

---

## IllegalArgumentException

Signals that a function received an inappropriate or invalid argument.
Extends `RuntimeException`.

Default error code: **4**.

### Constructors

| Signature | Description |
|-----------|-------------|
| `IllegalArgumentException()` | Construct with code 4 |
| `IllegalArgumentException(code: int)` | Construct with a custom code |

---

## IllegalStateException

Signals that an object is not in an appropriate state for the requested
operation. Extends `RuntimeException`.

Default error code: **5**.

### Constructors

| Signature | Description |
|-----------|-------------|
| `IllegalStateException()` | Construct with code 5 |
| `IllegalStateException(code: int)` | Construct with a custom code |

---

## Usage Example

```k
import k;  // (auto-imported, shown for clarity)

riskyAlloc() : int throws MemoryException {
    // ... some allocation that may fail ...
    throw MemoryException();
}

safeWrapper() : int {
    result : int = 0;
    try {
        result = riskyAlloc();
    } catch (e: MemoryException&) {
        result = -1;
    }
    return result;
}
```

---

## See Also

- [Exception Handling (Language Spec)](../language/statements/exceptions.md)
- [Language Summary §27](../language/summary.md#27-exception-handling)
- [Grammar — ThrowStatement, TryCatchStatement](../language/grammar.ebnf)

