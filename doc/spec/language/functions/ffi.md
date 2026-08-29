# Foreign Function Interface (FFI)

[← Functions](functions.md) · [Annotations](../annotations/annotations.md)

The K language provides a **Foreign Function Interface** (FFI) that allows
K code to call functions written in other languages — currently **C only**.
FFI is implemented through annotations in the `k::ffi` namespace, declared
in the standard library module `k` (source file `libk/libk/src/ffi.k`).

---

## Contents

1. [`@ffi::Extern` — declaring external functions](#1-ffiextern--declaring-external-functions)
2. [`@ffi::CString` — C string parameter annotation](#2-fficstring--c-string-parameter-annotation)
3. [Type compatibility matrix](#3-type-compatibility-matrix)
4. [Error and warning reference](#4-error-and-warning-reference)
5. [Examples](#5-examples)

---

## 1. `@ffi::Extern` — declaring external functions

The `@ffi::Extern` annotation marks a function as externally defined —
its body is resolved at link time from an external (typically C) library.

### Declaration

```k
@annotations::Retention(Policy::SOURCE)
@annotations::Target({ElementType::FUNCTION})
annotation Extern {
    language : const char[];
    library  : const char[]? = null;
    symbol   : const char[]? = null;
}
```

### Members

| Member | Type | Required | Description |
|--------|------|----------|-------------|
| `language` | `const char[]` | Yes | The foreign language. Currently only `"C"` is supported (case-insensitive). |
| `library` | `const char[]?` | No | The shared library to resolve the symbol from. *Not yet implemented — accepted with a warning.* |
| `symbol` | `const char[]?` | No | The exact C symbol name. If omitted, the function's short name is used as the symbol name. |

### Rules

| Rule | Description |
|------|-------------|
| **No body** | An `@Extern` function must not have a body — the implementation is provided by the external library. |
| **Not abstract** | An `@Extern` function cannot also be `abstract`. |
| **Static members only** | When used on a class member function, the function must be `static`. Non-static member functions have a hidden `this` parameter that has no C equivalent. |
| **Language** | The `language` parameter is mandatory and must be `"C"` (case-insensitive). Other languages are not yet supported. |
| **Mangling** | The mangled name of an `@Extern("C")` function is its C symbol name (no K name mangling). |

### Syntax

```k
// Positional form
@ffi::Extern("C") my_c_func(x : int) : int;

// Positional form with explicit symbol
@ffi::Extern("C", null, "actual_c_symbol") my_func(x : int) : int;

// Designated form
@ffi::Extern{.language = "C", .symbol = "actual_c_symbol"} my_func(x : int) : int;
```

---

## 2. `@ffi::CString` — C string parameter annotation

The `@ffi::CString` annotation is a **parameter-only** annotation that
instructs the compiler to treat the annotated parameter as a C-style
null-terminated string (`char*` or `const char*`) when generating FFI
call code.

### Declaration

```k
@annotations::Retention(Policy::SOURCE)
@annotations::Target({ElementType::PARAMETER})
annotation CString {}
```

### Purpose

In K, string data is represented as arrays of characters (`char[]` or
`const char[]`) or as addressers (pointers, references, views, links,
owners) to characters. In C, strings are represented as `char*` —
a pointer to a null-terminated sequence of characters.

Since all K addresser types (`&`, `*`, `?`, `+`, `!`) are internally
represented as LLVM opaque pointers (`ptr`), and C's `char*` is also
an opaque pointer, the conversion is **direct** — no runtime
transformation is needed. The `@CString` annotation serves as:

1. A **semantic marker** — documenting that the parameter is expected to
   point to a null-terminated C string.
2. A **compile-time validator** — the compiler verifies that the parameter
   type is a valid addresser to `char` (or `unsigned char` with a warning).
3. A **model flag** — the parameter's `is_ffi_cstring()` flag is set to
   `true`, which can be used by downstream compiler passes and tooling.

### Rules

| Rule | Description |
|------|-------------|
| **Parameter only** | `@CString` can only be applied to parameters, not to functions, classes, or other elements. |
| **Extern only** | The owning function must be annotated with `@ffi::Extern("C")`. Using `@CString` on a parameter of a non-extern function is an error. |
| **Addresser required** | The parameter type must be an addresser (reference `&`, pointer `*`, view `?`, link `+`, or owner `!`) to `char`. A bare value type (e.g. `char`) is rejected. |
| **Drain warning** | If the parameter type is a drain (`#`), a warning is emitted (drain semantics are not meaningful for C FFI) and the parameter is treated as a reference. |
| **Unsigned char warning** | If the addressed type is `unsigned char` (a.k.a. `byte`), a warning is emitted. The parameter is accepted and treated as `char` for FFI purposes. |
| **Non-char rejected** | If the addressed type is not `char` or `unsigned char` (e.g. `int`, `short`, `long`), a compile-time error is raised. |
| **Const transparency** | `const` wrappers are stripped before checking the addresser and leaf type, so `const char*`, `const char&`, etc. are all valid. |

### Syntax

```k
// Pointer to char
@ffi::Extern("C") strlen(@ffi::CString s : const char*) : int;

// Reference to char
@ffi::Extern("C") putchar(@ffi::CString c : char&) : int;

// View to char (nullable)
@ffi::Extern("C") optional_str(@ffi::CString s : char?) : int;

// Multiple CString parameters
@ffi::Extern("C") compare(
    @ffi::CString a : const char*,
    @ffi::CString b : const char*
) : int;

// Mixed CString and non-CString parameters
@ffi::Extern("C") write(@ffi::CString buf : const char*, len : int) : int;
```

---

## 3. Type compatibility matrix

The following table lists all K parameter types and their compatibility
with the `@ffi::CString` annotation:

| K parameter type | Result | C equivalent | Notes |
|------------------|--------|-------------|-------|
| `char*` | ✅ OK | `char*` | Direct mapping. |
| `char&` | ✅ OK | `char*` | Reference is a pointer internally. |
| `const char*` | ✅ OK | `const char*` | Direct mapping. |
| `const char&` | ✅ OK | `const char*` | Reference is a pointer internally. |
| `char?` (view) | ✅ OK | `char*` | View is a nullable pointer internally. |
| `char+` (link) | ✅ OK | `char*` | Link is a pointer internally. |
| `char!` (owner) | ✅ OK | `char*` | Owner is a pointer internally. Ownership is **not** transferred across FFI. |
| `char#` (drain) | ⚠️ Warning 0x0804 | `char*` | Drain is not meaningful for C FFI; treated as reference. |
| `byte&` / `byte*` / etc. | ⚠️ Warning 0x0807 | `char*` | `byte` = `unsigned char`; accepted but warned. |
| `char` (bare value) | ❌ Error 0x0808 | — | Not an addresser; cannot represent a C string. |
| `int&`, `short*`, etc. | ❌ Error 0x0803 | — | Addressed type must be `char`. |
| Non-extern function | ❌ Error 0x0801 | — | `@CString` requires `@ffi::Extern("C")` on the function. |

---

## 4. Error and warning reference

### Errors

| Code | Phase | Condition | Message |
|------|-------|-----------|---------|
| `0x0802` | Symbol resolver | `@Extern` function has a body, or language parameter is missing/empty/unsupported, or function is both `@Extern` and abstract. | Various messages depending on sub-condition. |
| `0x0801` | Symbol resolver | `@CString` used on a parameter of a non-`@Extern("C")` function. | `@ffi::CString on parameter '…' of function '…': @ffi::CString is only valid on parameters of @ffi::Extern("C") functions` |
| `0x0808` | Symbol resolver | `@CString` parameter type is not an addresser (e.g. bare `char`). | `@ffi::CString on parameter '…': the parameter type must be an addresser (reference, pointer, view, link or owner) to char, but got '…'` |
| `0x0803` | Symbol resolver | `@CString` addressed type is not `char` (e.g. `int&`, `short*`). | `@ffi::CString on parameter '…': the addressed type must be char, but got '…'` |

### Warnings

| Code | Phase | Condition | Message |
|------|-------|-----------|---------|
| `0x0804` | Symbol resolver | `@CString` parameter uses drain (`#`) indirection. | `@ffi::CString on parameter '…': drain indirection (#) is not meaningful for C FFI; treating as reference` |
| `0x0807` | Symbol resolver | `@CString` addressed type is `unsigned char` / `byte`. | `@ffi::CString on parameter '…': unsigned char will be treated as char for C FFI` |

---

## 5. Examples

### Calling C's `strlen`

```k
import k;

@ffi::Extern("C") strlen(@ffi::CString s : const char*) : long;

test() : long {
    str : const char[] = "hello, world";
    p : const char* = &str[0];
    return strlen(p);  // returns 12
}
```

### Calling C's `puts`

```k
import k;

@ffi::Extern("C") puts(@ffi::CString s : const char*) : int;

greet() : int {
    msg : const char[] = "Hello from K!\0";
    return puts(&msg[0]);
}
```

### Static member extern with `@CString`

```k
import k;

class FileUtils {
    public FileUtils() {}

    @ffi::Extern("C")
    static fopen(@ffi::CString path : const char*, @ffi::CString mode : const char*) : void*;
}
```

### Mixed parameters

```k
import k;

@ffi::Extern("C") write(fd : int, @ffi::CString buf : const char*, count : long) : long;

send_message(fd : int) : long {
    msg : const char[] = "OK\n";
    return write(fd, &msg[0], 3);
}
```

---

*See also:* [Functions](functions.md) · [Annotations](../annotations/annotations.md)


