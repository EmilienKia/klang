# Exception Handling

[← Statements](statements.md) · [← Index](../index.md)

K provides structured exception handling via the `throw` statement, `try-catch`
blocks, and the `throws` clause on function signatures.

All throwable types must belong to the `::k::Throwable` class hierarchy defined
in the standard library. The hierarchy separates **checked exceptions**
(`Exception` subtypes) that must be declared in `throws` clauses, from
**unchecked fatal errors** (`FatalError` subtypes) that propagate freely.

---

## Contents

1. [Throwable type constraint](#1-throwable-type-constraint)
2. [Throw statement](#2-throw-statement)
3. [Try-catch statement](#3-try-catch-statement)
4. [Throws clause](#4-throws-clause)
5. [Exception contract rules](#5-exception-contract-rules)
6. [Stack unwinding and cleanup](#6-stack-unwinding-and-cleanup)
7. [Implementation details](#7-implementation-details)
8. [Diagnostic codes](#8-diagnostic-codes)
9. [Known limitations](#9-known-limitations)

---

## 1. Throwable type constraint

**Only classes derived from `::k::Throwable` (or `Throwable` itself) may be thrown.**

- Throwing a struct or class that does not derive from `::k::Throwable` →
  compile-time error `0x01C0`.
- Throwing a non-class type (primitive, array, etc.) → compile-time error `0x01C0`.
- This constraint is enforced whenever the stdlib is available (i.e. for all
  modules except `k` itself).

The throwable hierarchy has two branches:

- **`Exception`** (checked) — must be declared in `throws` clauses.
- **`FatalError`** (unchecked) — propagates freely without declaration.

User-defined exception types should inherit from `Exception` (checked) or
`FatalError` (unchecked):

```k
class MyError : public Exception {
    public:
    MyError() : Exception(100) { }
}

class CriticalFailure : public FatalError {
    public:
    CriticalFailure() : FatalError(50) { }
}
```

See also: [K Standard Library — Exception Types](../../stdlib/exceptions.md)

---

## 2. Throw statement

### Grammar

```
ThrowStatement:
    'throw' Expression ';'
```

The expression must evaluate to a class type derived from `::k::Throwable`.

### Semantics

At runtime, the compiler:
1. Allocates exception storage via `__cxa_allocate_exception`.
2. Copies the value into the exception storage.
3. Calls `__cxa_throw` to initiate stack unwinding.

Stack unwinding destroys all local objects with destructors (in reverse
declaration order) in each stack frame between the throw point and the matching
catch handler.

### Examples

```k
class ParseError : public Exception {
    public:
    ParseError() : Exception(10) { }
}

validate(x: int) : void {
    if (x < 0) {
        e : ParseError;
        throw e;
    }
}

// Throw via temporary construction (no local variable needed):
validate2(x: int) : void {
    if (x < 0) {
        throw ParseError();
    }
}
```

---

## 3. Try-catch statement

### Grammar

```
TryCatchStatement:
    'try' BlockStatement { CatchClause }

CatchClause:
    'catch' '(' CatchParameterDecl ')' BlockStatement

CatchParameterDecl:
    Identifier ':' TypeSpec
```

### Rules

- Multiple catch clauses are evaluated in order; the **first matching type wins**.
- Match is by exact type or base class (if the thrown type derives from the
  caught type).
- Unmatched exceptions propagate to the next enclosing try-catch or out of the
  function.
- The catch parameter receives a **reference** (`T&`) to the exception object.
- Catch parameter types must derive from `::k::Throwable` (error `0x01C1`
  otherwise).
- Catch parameter must use a reference addresser (`T&`) (error `0x01C2` otherwise).
  References guarantee that the caught exception is never null and its address
  is non-reassignable.
- All function calls within a try block are compiled as LLVM `invoke`
  instructions (instead of `call`) to enable unwinding through the landing pad.

### Examples

```k
class IOError : public Exception {
    public:
    IOError() : Exception(20) { }
}

class FileNotFound : public IOError {
    public:
    FileNotFound() : IOError() { }
}

safeRead() : int {
    result : int = 0;
    try {
        result = readFile();
    } catch (e: FileNotFound&) {
        // Catches FileNotFound specifically
        result = -1;
    } catch (e: IOError&) {
        // Catches any other IOError subclass
        result = -2;
    } catch (e: Exception&) {
        // Catches anything else
        result = -99;
    }
    return result;
}
```

### Catch ordering

Catch clauses are tested in declaration order. More specific types should appear
before more general base types. The compiler does **not** warn about unreachable
catch clauses (this may change in future versions).

---

## 4. Throws clause

### Grammar

```
ThrowsClause:
    'throws' TypeSpec { ',' TypeSpec }
```

The `throws` clause appears after the return type and before the function body
(or `-> default` / `-> delete`):

```k
myFunc(a: int) : int throws IOException, ParseException {
    // ...
}
```

### Rules

- All types in the `throws` clause must derive from `::k::Throwable` (error
  `0x01C4` otherwise).
- A type in the `throws` clause that cannot be resolved → error `0x01C3`.
- The throws clause is part of the function's public interface and exported in
  `.kdi` files.
- Constructors can also have a `throws` clause (see below).

---

## 5. Exception contract rules

When a function declares a `throws` clause, the compiler enforces static
contract verification **for checked exceptions only** (types derived from
`::k::Exception`):

### Throw check

Any `throw` statement in the function body that throws an `Exception`-derived
type must throw a type that is either:
- Declared in the function's `throws` clause, **or**
- Caught by an enclosing `try-catch` within the same function.

Violation → error `0x01C5`.

### Call check

Any call to a function (or constructor) that itself has a `throws` clause must
have all its declared exception types either:
- Caught by an enclosing `try-catch`, **or**
- Declared in the caller's own `throws` clause (propagation).

Violation → error `0x01C6`.

### Unchecked types

Types derived from `::k::FatalError` are **exempt** from contract enforcement —
they may be thrown from any function without declaration. This allows runtime
errors like `OutOfMemory` to propagate without polluting every function
signature.

### Unchecked functions

Functions **without** a `throws` clause are **not checked** — they may throw
freely and are not required to handle exceptions from called functions. This
allows gradual adoption and FFI interop.

### Constructor throws clause

Constructors (for both `struct` and `class`) may declare a `throws` clause.
The same contract rules apply: callers that construct an object (via local
variable declaration or `new`) must handle or propagate the constructor's
declared exceptions.

```k
class InitError : public Exception {
    public:
    InitError(code: int) : Exception(code) { }
}

class Sensor {
    value : int;
    public:
    Sensor(v: int) throws InitError {
        if (v < 0) {
            throw InitError(v);
        }
        value = v;
    }
}

// Caller must handle InitError:
test() : int {
    result : int = 0;
    try {
        s : Sensor(10);
        result = s.value;
    } catch (e: InitError&) {
        result = -1;
    }
    return result;
}
```

### Example

```k
class NetworkError : public Exception {
    public:
    NetworkError() : Exception(30) { }
}

// This function declares its exception contract
fetchData() : int throws NetworkError {
    throw NetworkError();
}

// Caller handles the exception
process() : int {
    result : int = 0;
    try {
        result = fetchData();
    } catch (e: NetworkError&) {
        result = -1;
    }
    return result;
}

// Caller propagates the exception
proxy() : int throws NetworkError {
    return fetchData();  // OK — NetworkError is declared in our throws clause
}
```

---

## 6. Stack unwinding and cleanup

When an exception propagates through a stack frame:

- All local variables with destructors are destroyed in reverse declaration
  order.
- Owner variables (`T!`) are auto-deleted.
- The cleanup is implemented via LLVM landing pads with cleanup clauses.
- Nested try-catch blocks within the same function use direct CFG branching
  (resume to outer handler if inner handlers don't match).

### Example

```k
struct Resource {
    id: int;
    ~Resource() { cleanup(id); }
}

riskyWork() : int {
    a : Resource(1);   // destroyed during unwinding (second)
    b : Resource(2);   // destroyed during unwinding (first — reverse order)
    mayThrow();        // if this throws, both a and b are properly destroyed
    return 0;
}
```

---

## 7. Implementation details

- Uses the **Itanium C++ ABI** unwinding mechanism (`__cxa_allocate_exception`,
  `__cxa_throw`, `__cxa_begin_catch`, `__cxa_end_catch`).
- Type matching uses pointer equality on module-level typeinfo globals
  (`_KTI<mangled_name>`). Before throwing, the compiler stores the typeinfo
  pointer in a per-module `_k_thrown_typeinfo` thread-local global.
- Each catch clause in a landing pad compares the caught typeinfo against the
  expected typeinfo for its declared type.
- Stack unwinding properly destroys local objects via cleanup landing pads.
- Nested try-catch uses direct CFG branching (no re-throw) for intra-function
  propagation.

---

## 8. Diagnostic codes

| Code | Identifier | Description |
|------|-----------|-------------|
| `0x01C0` | `ERR_THROW_NOT_EXCEPTION_TYPE` | Thrown type does not derive from `::k::Throwable` |
| `0x01C1` | `ERR_CATCH_NOT_EXCEPTION_TYPE` | Catch clause type does not derive from `::k::Throwable` |
| `0x01C2` | `ERR_CATCH_MUST_BE_REFERENCE` | Catch clause must use reference addresser (`&`) |
| `0x01C3` | `ERR_THROWS_TYPE_NOT_FOUND` | Type in throws clause cannot be resolved |
| `0x01C4` | `ERR_THROWS_NOT_EXCEPTION_TYPE` | Type in throws clause does not derive from `::k::Throwable` |
| `0x01C5` | `ERR_THROW_NOT_IN_THROWS_SPEC` | Throw of undeclared checked exception in function with throws clause |
| `0x01C6` | `ERR_CALL_UNHANDLED_EXCEPTION` | Call to throwing function without handling/declaring its checked exceptions |

---

## 9. Known limitations

- No `finally` clause.
- No rethrow (`throw;` without expression).
- No `generic` catch-all clause (planned).
- Template instantiation does not currently propagate the `throws` clause from
  the template definition to the instantiated function (affects `UniSlot<T>::construct()`
  and `MultiSlot<T>::construct()` contract enforcement).

---

## See Also

- [K Standard Library — Exception Types](../../stdlib/exceptions.md) — `Throwable`, `Exception`, `FatalError`, and derived types
- [Grammar — ThrowStatement, TryCatchStatement](../grammar.md)
- [Language Summary §27](../summary.md#27-exception-handling)
- [Functions](../functions/functions.md) — throws clause in function signatures
- [Constructors](../structs/constructors.md) — throws clause on constructors


