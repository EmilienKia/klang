# Function References

[← Index](../index.md) · [Functions](functions.md)

A *function reference* is a value that holds the address of a function. It can be stored in a variable, passed as an argument, returned from a function, and later *called* through the reference.

K distinguishes two flavours:

| Flavour | Syntax | Holds |
|---------|--------|-------|
| **Free function reference** | `*(Params)`, `?(Params)`, `+(Params)` | Address of a free (or static member) function |
| **Member function reference** | `T::*(Params)`, `T::?(Params)`, `T::+(Params)` | Address of a non-static member function of struct `T` |

The three reference-qualifier variants (`*`, `?`, `+`) share the same runtime representation (an opaque pointer) but carry compile-time nullability and rebindability information, exactly as for object indirections (see [Types §2](../basic/types.md#2-indirection-types--overview)).

---

## Contents

1. [Free function reference types](#1-free-function-reference-types)
2. [Member function reference types](#2-member-function-reference-types)
3. [Obtaining a function address](#3-obtaining-a-function-address)
4. [Calling through a free function reference](#4-calling-through-a-free-function-reference)
5. [Calling through a member function reference — operators `.*` and `->*`](#5-calling-through-a-member-function-reference--operators--and--)
6. [Passing and returning function references](#6-passing-and-returning-function-references)
7. [Overload disambiguation](#7-overload-disambiguation)
8. [Nullability and null-checks](#8-nullability-and-null-checks)
9. [Grammar summary](#9-grammar-summary)

---

## 1. Free function reference types

A *free function reference type* describes the type of a variable that holds the address of a free function (or a `static` member function).

**Syntax:**

```
FreeRefQualifier '(' [ TypeList ] ')'
FreeRefQualifier:
    '*'   -- pointer  (nullable,     rebindable)
    '?'   -- view   (nullable,     immutable binding)
    '+'   -- link     (non-null,     rebindable)
```

The parameter types are listed inside the parentheses, separated by commas. The return type is **not** written — it is inferred from the target function. This avoids ambiguity, since K does not support overloading on return type alone.

**Examples:**

```k
fp  : *(int)           // pointer to a function (int) → ?  (nullable, rebindable)
view : ?(int, double)   // view  to a function (int, double) → ?  (nullable, immutable)
lnk : +(int)           // link    to a function (int) → ?  (non-null, rebindable)
```

**No-parameter functions** use an empty parameter list:

```k
fp0 : *()   // pointer to a function with no parameters
```

---

## 2. Member function reference types

A *member function reference type* describes the type of a variable that holds the address of a non-static member function of a specific struct type.

**Syntax:**

```
StructName '::' FreeRefQualifier '(' [ TypeList ] ')'
```

The `StructName::` prefix qualifies the struct whose method is referenced. Qualified names (e.g. `myns::Counter`) are allowed.

**Examples:**

```k
mfp  : Counter::*(int)           // pointer to a Counter member function (int) → ?
mpin : Counter::?(int, double)   // view   to a Counter member function
mlnk : Counter::+(int)           // link     to a Counter member function (non-null)
```

The implicit `this` parameter is **not** listed — it is supplied by the `.*` / `->*` call syntax.

---

## 3. Obtaining a function address

A *symbol expression that resolves to a function but is not followed by a call* evaluates to the address of the function:

```k
struct Counter {
    value : int;
    add(x : int) : int { return value + x; }
}

// Free function
add_one(x : int) : int { return x + 1; }
fp  : *(int) = add_one;         // address of add_one

// Member function — qualified with struct name
mfp : Counter::*(int) = Counter::add;   // address of Counter::add
```

The compiler verifies that the type of the referenced function matches the declared reference type (parameter list). If multiple overloads share the same name, the declared type is used to select the correct one (see [§7 — Overload disambiguation](#7-overload-disambiguation)).

**Assignment after declaration** follows the same rules:

```k
fp = add_one;           // fp is re-pointed at add_one (fp is a * or +)
mfp = Counter::add;     // mfp is re-pointed at Counter::add
```

---

## 4. Calling through a free function reference

A free function reference is called using ordinary call syntax:

```k
add_one(x : int) : int { return x + 1; }

test() : int {
    fp : *(int) = add_one;
    return fp(41);          // calls add_one(41) → 42
}
```

The call site must provide exactly the arguments required by the parameter list. The compiler adapts the argument types according to the usual implicit conversion rules.

**Null pointer call:** Calling a `*` or `?` reference that holds null results in undefined behaviour (no automatic null-check is emitted at call sites; the caller is responsible).

---

## 5. Calling through a member function reference — operators `.*` and `->*`

To call a member function reference, you must supply both a *receiver object* and a *member function reference* using the pointer-to-member call operators.

### `.*` — call on an object or reference

```
'(' ObjExpr '.*' MfpExpr ')' '(' [ ArgumentList ] ')'
```

`ObjExpr` must be of struct type `T` (value or reference `T&`).  
`MfpExpr` must be of member function reference type `T::qualifier(Params)`.

```k
struct Counter {
    value : int;
    add(x : int) : int { return value + x; }
}

test() : int {
    mfp : Counter::*(int) = Counter::add;
    c   : Counter;
    c.value = 40;
    return (c.*mfp)(2);     // calls c.add(2) → 42
}
```

### `->*` — call through an indirection

```
'(' IndirExpr '->*' MfpExpr ')' '(' [ ArgumentList ] ')'
```

`IndirExpr` must be of type `T*`, `T?`, or `T+` — any of the three indirection types for `T`.

```k
test_link() : int {
    mfp : Counter::*(int) = Counter::add;
    c   : Counter;
    c.value = 40;
    lnk : Counter+ = c;
    return (lnk->*mfp)(2);  // calls c.add(2) via link → 42
}

test_ptr() : int {
    mfp : Counter::*(int) = Counter::add;
    c   : Counter;
    c.value = 40;
    ptr : Counter* = c;
    return (ptr->*mfp)(2);  // calls c.add(2) via pointer → 42
}

test_pin() : int {
    mfp : Counter::*(int) = Counter::add;
    c   : Counter;
    c.value = 40;
    view : Counter? = c;
    return (view->*mfp)(2);  // calls c.add(2) via view → 42
}
```

### Parenthesisation

The outer parentheses around `(obj.*mfp)` or `(ptr->*mfp)` are **required**: `.*` and `->*` have lower precedence than the function call operator `()`.

```k
c.*mfp(2)       // ERROR: parsed as c.*(mfp(2)) — mfp(2) is a call, not a member ref
(c.*mfp)(2)     // CORRECT
```

### Using a member function reference as a parameter

A member function reference may be passed to and called from inside another function:

```k
invoke(mfp : Counter::*(int), c : Counter, x : int) : int {
    return (c.*mfp)(x);
}

invoke_via_link(mfp : Counter::*(int), lnk : Counter+, x : int) : int {
    return (lnk->*mfp)(x);
}

test() : int {
    mfp : Counter::*(int) = Counter::add;
    c   : Counter;
    c.value = 40;
    return invoke(mfp, c, 2);           // → 42
}
```

---

## 6. Passing and returning function references

Function references are first-class values: they can be passed as parameters, returned from functions, and stored in variables.

### Passing as a parameter

The parameter type uses the same `*(Params)` / `T::*(Params)` notation:

```k
apply(fp : *(int), x : int) : int {
    return fp(x);
}

test() : int {
    add_one(x : int) : int { return x + 1; }
    return apply(add_one, 41);   // → 42
}
```

### Returning from a function

The return type uses the same notation:

```k
struct Counter {
    value : int;
    add(x : int) : int { return value + x; }
}

get_add() : Counter::*(int) {
    return Counter::add;
}

test() : int {
    mfp : Counter::*(int) = get_add();
    c   : Counter;
    c.value = 40;
    return (c.*mfp)(2);     // → 42
}
```

---

## 7. Overload disambiguation

When a function is overloaded (multiple functions share the same name), the compiler uses the **declared type** of the function reference variable to choose the correct overload.

The parameter list in the type declaration acts as the disambiguator:

```k
struct Counter {
    value : int;
    add(x : int) : int { return value + x; }         // overload 1: (int)
    add(x : double) : double { return value + x; }   // overload 2: (double)
}

test() : int {
    mfp_i : Counter::*(int)    = Counter::add;   // selects overload 1
    mfp_d : Counter::*(double) = Counter::add;   // selects overload 2

    c : Counter;
    c.value = 40;
    r1 : int    = (c.*mfp_i)(2);    // → 42
    r2 : double = (c.*mfp_d)(2.0d); // → 42.0
    return r1;
}
```

The same mechanism applies to free function references:

```k
double_it(x : int)    : int    { return x * 2; }
double_it(x : double) : double { return x * 2.0d; }

test() : int {
    fp_i : *(int)    = double_it;   // selects the (int) overload
    fp_d : *(double) = double_it;   // selects the (double) overload
    return fp_i(21);                // → 42
}
```

---

## 8. Nullability and null-checks

The reference qualifier controls nullability, mirroring object indirections:

| Qualifier | Nullable | Rebindable | Notes |
|-----------|----------|------------|-------|
| `*` (pointer) | Yes | Yes | May be `null`; no automatic null-check at call site |
| `?` (view)  | Yes | No  | May be `null`; no automatic null-check at call site |
| `+` (link)    | No  | Yes | Initialisation from nullable source triggers runtime null-check |

**Assigning null:**

```k
fp : *(int) = null;    // valid — pointer may be null
fp(42);                // undefined behaviour if fp is null (no auto null-check)
```

**Link initialisation from nullable source:**

```k
fp_ptr : *(int) = add_one;
fp_lnk : +(int) = fp_ptr;   // runtime null-check inserted: fp_ptr must not be null
```

---

## 9. Grammar summary

```
FunctionReferenceType:
    FreeRefQualifier '(' [ TypeList ] ')'
  | StructQualifiedName '::' FreeRefQualifier '(' [ TypeList ] ')'

FreeRefQualifier:
    '*' | '?' | '+'

StructQualifiedName:
    Identifier { '::' Identifier }

TypeList:
    TypeSpec { ',' TypeSpec }

-- Obtaining an address (no call follows):
FunctionAddressExpr:
    QualifiedFunctionName          -- symbol resolved to a function, without '()'

-- Free function call through reference:
FreeRefCallExpr:
    PostfixExpr '(' [ ExpressionList ] ')'

-- Member function call through reference:
MemberRefCallExpr:
    '(' ObjExpr '.*'  MfpExpr ')' '(' [ ExpressionList ] ')'
  | '(' IndirExpr '->*' MfpExpr ')' '(' [ ExpressionList ] ')'

ObjExpr:
    -- expression of struct type T (value or T&)

IndirExpr:
    -- expression of type T*, T?, or T+
```

---

*See also:* [Types](../basic/types.md) · [Functions](functions.md) · [Function Overloading](overloading.md) · [Expressions](../expressions/call.md)

