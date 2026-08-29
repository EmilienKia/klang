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
9. [Exception chaining (cause)](#9-exception-chaining-cause)
10. [Known limitations](#10-known-limitations)

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
    'throw' ';'
```

The first form throws a new exception. The expression must evaluate to a class
type derived from `::k::Throwable`.

The second form (`throw;`) is a **rethrow** — it re-throws the exception
currently being handled. It is only valid inside a `catch` block.

### Semantics

At runtime, the compiler:
1. Allocates exception storage via `__cxa_allocate_exception`.
2. Copies the value into the exception storage.
3. Calls `__cxa_throw` to initiate stack unwinding.

Stack unwinding destroys all local objects with destructors, all owner values
(`T!`), and all sized arrays whose elements require cleanup, in reverse
declaration order within each scope and in reverse frame order between the
throw point and the matching catch handler.

For destructible locals, the compiler ensures that only fully constructed
objects are destroyed during unwinding. Owner values are released only when the
owned value is non-null; releasing an owner destroys the owned object and then
deallocates the storage. Sized arrays are cleaned up element-by-element in
reverse element order.

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

### Rethrow semantics

A bare `throw;` re-throws the currently handled exception without allocating or
copying. At runtime it calls `__cxa_rethrow()`, which resumes stack unwinding
from the current catch handler.

**Constraints:**
- `throw;` is only valid inside the body of a `catch` clause.
  Using it outside a catch block is a compile-time error (`0x01C9`).
- The rethrown exception type (from the enclosing catch) is subject to the same
  exception contract rules as a normal throw: the type must be declared in the
  function's `throws` clause or caught by an outer enclosing `try-catch`.
- `FatalError`-derived types are unchecked and always allowed.

### Rethrow examples

```k
class NetError : public Exception {
    public:
    NetError() : Exception(42) { }
}

// Rethrow to an outer try-catch in the same function:
retry(attempts: int) : int {
    result : int = 0;
    try {
        try {
            throw NetError();
        } catch (e: NetError&) {
            // Log, then rethrow
            result = 1;
            throw;
        }
    } catch (e: NetError&) {
        result = result + e.getCode();
    }
    return result;  // returns 43 (1 + 42)
}

// Rethrow to the caller:
relay() : void throws(NetError) {
    try {
        throw NetError();
    } catch (e: NetError&) {
        throw;  // caller must handle NetError
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

### Finally blocks

`try` blocks may optionally be followed by a `finally` block.

- A `finally` block executes after the `try` body on normal flow.
- A `finally` block also executes when control leaves the `try` or `catch`
  body early via `return`, `break`, `continue`, or a rethrow.
- A `finally` block runs after the relevant scope cleanup for the exit path
  that triggered it.

`finally` blocks do not replace exception cleanup; they are executed in addition
to the normal RAII cleanup performed for locals, owners, and arrays.

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

### Example with `finally`

```k
class LogError : public Exception {
    public:
    LogError() : Exception(12) { }
}

trace() : int {
    result : int = 0;
    try {
        result = 1;
        throw LogError();
    } catch (e: LogError&) {
        result = result + e.getCode();
    } finally {
        result = result + 100;
    }
    return result;
}
```

---

## 4. Throws clause

### Grammar

```
ThrowsClause:
    'throws' '(' [ TypeSpec { ',' TypeSpec } ] ')'
```

The `throws` clause appears after the return type and before the function body
(or `-> default` / `-> delete`):

```k
myFunc(a: int) : int throws(IOException, ParseException) {
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
    Sensor(v: int) throws(InitError) {
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

### Template throws propagation

When a template function or method declares a `throws` clause, the clause is
propagated to all instantiated functions. This means callers of template
methods like `UniSlot<T>::construct()` are subject to the same contract
enforcement as any other throwing function.

```k
template<typename T>
struct Box {
    _slot : UniSlot<T>;

    fill(value : T&) {
        _slot.construct(value);
    }
}

// ConstructionException is a FatalError — no throws clause needed:
test() : int {
    box : Box<int>;
    v : int = 42;
    box.fill(v);   // OK — ConstructionException propagates as FatalError
    return 0;
}
```

### Example

```k
class NetworkError : public Exception {
    public:
    NetworkError() : Exception(30) { }
}

// This function declares its exception contract
fetchData() : int throws(NetworkError) {
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
proxy() : int throws(NetworkError) {
    return fetchData();  // OK — NetworkError is declared in our throws clause
}
```

---

## 6. Stack unwinding and cleanup

When an exception propagates through a stack frame:

- All local variables with destructors are destroyed in reverse declaration
  order.
- Owner variables (`T!`) are auto-deleted when non-null: the owned object is
  destroyed and then the owner storage is released.
- Sized arrays are cleaned up element-by-element in reverse element order when
  their element type is destructible.
- Cleanup is performed both on normal scope exit and on exception unwinding.
- The cleanup is implemented via LLVM landing pads with cleanup clauses.
- Nested `try-catch` blocks within the same function use direct CFG branching
  and still run the appropriate cleanup before propagating to an outer handler.

### Example

```k
struct Resource {
    id: int;
    ~Resource() { cleanup(id); }
}

riskyWork() : int {
    a : Resource(1);   // destroyed during unwinding (second)
    b : Resource(2);   // destroyed during unwinding (first — reverse order)
    owned : Resource! = new Resource(3);  // destroyed, then released on unwind
    items : Resource(4)[2];                // elements destroyed in reverse order
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
| `0x01C9` | `ERR_RETHROW_OUTSIDE_CATCH` | Bare `throw;` (rethrow) used outside a catch block |

---

## 9. Exception chaining (cause)

An exception can reference another exception as its **cause**. This enables
wrapping lower-level errors while preserving the original diagnostic context.

### Mechanism

Exception chaining is constructor-based — no dedicated syntax is needed.
All stdlib exception classes (`Throwable`, `Exception`, `FatalError`) provide
constructors that accept an optional `Throwable?` cause parameter:

```k
class MyAppError : public Exception {
    public:
    MyAppError(code: int) : Exception(code) { }
    MyAppError(code: int, cause: Throwable?) : Exception(code, cause) { }
}
```

### Usage

```k
class LowLevelError : public Exception {
    public:
    LowLevelError() : Exception(10) { }
}

class HighLevelError : public Exception {
    public:
    HighLevelError(code: int, cause: Throwable?) : Exception(code, cause) { }
}

process() : int {
    result : int = 0;
    try {
        try {
            throw LowLevelError();
        } catch (ex: LowLevelError&) {
            // Wrap the original error with context
            throw HighLevelError(99, ex);
        }
    } catch (w: HighLevelError&) {
        result = w.getCode();       // 99
        if (w.hasCause()) {
            cause : Throwable? = w.getCause();
            result = result + cause.getCode();  // 99 + 10 = 109
        }
    }
    return result;
}
```

### Null cause

Passing `null` as the cause is valid and equivalent to having no cause:

```k
throw MyAppError(42, null);  // hasCause() returns false
```

### Lifetime management

The compiler automatically manages the cause exception's lifetime:

- At `throw` time, if the `_cause` field is non-null, the runtime retains
  (ref-count increments) the currently active exception's ABI storage.
- When the wrapping exception is destroyed, the retained cause is released.
- This is transparent to the programmer — no manual memory management is needed.

This lifetime management is independent from stack unwinding cleanup for local
variables, owners, and arrays in the frames crossed by the exception.

### Chaining depth

Chains can be arbitrarily deep. Each wrapper stores a view to its immediate
cause; walking the full chain requires repeated `getCause()` calls:

```k
catch (outer: OuterError&) {
    if (outer.hasCause()) {
        c1 : Throwable? = outer.getCause();
        if (c1.hasCause()) {
            c2 : Throwable? = c1.getCause();  // original root cause
        }
    }
}
```

---

## 10. Known limitations

- No `generic` catch-all clause (planned).

---

## See Also

- [K Standard Library — Exception Types](../../stdlib/exceptions.md) — `Throwable`, `Exception`, `FatalError`, and derived types
- [Grammar — ThrowStatement, TryCatchStatement](../grammar.md)
- [Language Summary §27](../summary.md#27-exception-handling)
- [Functions](../functions/functions.md) — throws clause in function signatures
- [Constructors](../structs/constructors.md) — throws clause on constructors


