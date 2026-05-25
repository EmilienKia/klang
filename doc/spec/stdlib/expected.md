# Expected<R, E> — Type-Safe Result or Error

> **Module:** `k`  
> **Source:** `libk/libk/src/expected.k`  
> **Status:** Working Draft — 2026

---

## Overview

`Expected<R, E>` is a discriminated-union wrapper that holds either a successful
result of type `R` or an error of type `E`.  It avoids heap allocation by storing
both alternatives inline using a nested `union Storage`.

This is the K equivalent of C++23's `std::expected<T, E>` and Rust's
`Result<T, E>`.

---

## Definition

```k
template<typename R, typename E>
struct Expected {
    Expected();
    Expected(other : Expected<R, E>&);

    // Static factory methods (preferred entry points)
    public static expected(value : R&)   : Expected<R, E>;
    public static unexpected(value : E&) : Expected<R, E>;
    public static error(value : E&)      : Expected<R, E>;   // alias for unexpected()

    // Query
    const hasResult() : bool;
    const hasError()  : bool;

    // Mutators
    setResult(value : R&);
    setError(value : E&);

    // Accessors
    const getResult()                        : R;
    const getError()                         : E;
    const getResultOr(defaultValue : R&)     : R;
    const getErrorOr(defaultValue : E&)      : E;
}
```

---

## Constructors

| Method | Description |
|--------|-------------|
| `Expected()` | Default constructor. Creates an `Expected` in an undefined state. Must be followed immediately by a call to `setResult()` or `setError()`. |
| `Expected(other : Expected<R,E>&)` | Copy constructor. If `other` holds a result, copies the result value. If `other` holds an error, copies the error value. If `other` is undefined, this is also left undefined. |

---

## Static Factory Methods

These are the **preferred** way to construct an `Expected<R, E>`.

| Method | Description |
|--------|-------------|
| `Expected<R,E>::expected(value)` | Create an `Expected<R,E>` that holds a successful result copy of `value`. |
| `Expected<R,E>::unexpected(value)` | Create an `Expected<R,E>` that holds an error copy of `value`. |
| `Expected<R,E>::error(value)` | Alias for `unexpected(value)`. Useful when the error type is an error code or exception-like object. |

The same calls are valid with explicit namespace qualification when desired:

```k
ok  : ::k::Expected<int, int> = ::k::Expected<int, int>::expected(42);
err : ::k::Expected<int, int> = ::k::Expected<int, int>::unexpected(-1);
```

---

## Query Methods

| Method | Description |
|--------|-------------|
| `hasResult() : bool` | Return `true` if this `Expected` holds a successful result. |
| `hasError() : bool` | Return `true` if this `Expected` holds an error. |

---

## Mutator Methods

| Method | Description |
|--------|-------------|
| `setResult(value : R&)` | Store a successful result (copy of `value`). Overwrites any previously stored value. |
| `setError(value : E&)` | Store an error (copy of `value`). Overwrites any previously stored value. |

---

## Accessor Methods

| Method | Description |
|--------|-------------|
| `getResult() : R` | Return the stored result. **Precondition:** `hasResult()` must be `true`. |
| `getError() : E` | Return the stored error. **Precondition:** `hasError()` must be `true`. |
| `getResultOr(defaultValue : R&) : R` | Return the stored result if present, otherwise return `defaultValue`. |
| `getErrorOr(defaultValue : E&) : E` | Return the stored error if present, otherwise return `defaultValue`. |

---

## Usage

```k
// --- Static factory methods (preferred) ---

ok  : Expected<int, int> = Expected<int, int>::expected(42);
err : Expected<int, int> = Expected<int, int>::unexpected(-1);
// error() is an alias for unexpected():
e2  : Expected<int, int> = Expected<int, int>::error(-1);

// Explicit namespace-qualified form is also valid:
ok2 : ::k::Expected<int, int> = ::k::Expected<int, int>::expected(42);

// --- Querying the state ---

if (ok.hasResult()) {
    val : int = ok.getResult();   // 42
}

if (err.hasError()) {
    code : int = err.getError();  // -1
}

// --- Safe access with a default ---

val  : int = ok.getResultOr(0);   // 42
code : int = ok.getErrorOr(-1);   // -1  (ok holds a result, so default is returned)

// --- Copy ---

copy : Expected<int, int>(ok);
// copy.hasResult() == true, copy.getResult() == 42
```

---

## Undefined Behaviour

- Calling `getResult()` when `hasResult()` is `false`.
- Calling `getError()` when `hasError()` is `false`.
- Using an `Expected` that was default-constructed but never initialised with
  `setResult()`, `setError()`, or a factory method.

---

## Design Notes

### Storage

`Expected<R, E>` uses a nested `union Storage` with two alternatives:

```k
union Storage {
    result: R;   // alternative index 0  (Storage::Kind::result)
    error:  E;   // alternative index 1  (Storage::Kind::error)
}
```

The active alternative is tracked by the union's built-in discriminant, queried
via `_storage.index()`.

### Factory Methods vs Direct Construction

The static factory methods `expected()`, `unexpected()`, and `error()` are
preferred over direct construction + manual `setResult()`/`setError()` calls
because they express intent clearly and make code easier to read.

Compatibility free functions (`makeExpected` and `makeUnexpected`) are retained
for legacy code, but new code should prefer the static factories.

`error()` is a pure alias for `unexpected()` — it exists so that call-sites
using error codes or error objects can write more domain-appropriate code:

```k
parseNumber(s : String&) : Expected<int, int> {
    // ...
    return Expected<int, int>::error(-1);   // reads like "return an error"
}
```
