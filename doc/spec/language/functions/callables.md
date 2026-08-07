# Callables

[← Index](../index.md) · [Functions](functions.md)

A *callable* is a **fat pointer** — a pair `{function pointer, context pointer}` — that represents any invocable entity with a fixed prototype. Unlike a bare [function reference](function_references.md), a callable can wrap a free function, a bound method, a functor object, or a lambda, while exposing a uniform call interface to its users.

| Property | Value |
|---|---|
| Runtime size | 2 × pointer (function ptr + context ptr) |
| Invocation | `f(args)` — same as an ordinary function call |
| Context | `null` for free functions and static members; object address for bound methods and functors |

---

## Contents

1. [Callable types — syntax and addressers](#1-callable-types--syntax-and-addressers)
2. [Prototype-only form](#2-prototype-only-form)
3. [Return type — void and non-void callables](#3-return-type--void-and-non-void-callables)
4. [Checked exceptions — the `throws` clause](#4-checked-exceptions--the-throws-clause)
5. [Addresser semantics](#5-addresser-semantics)
6. [What can be assigned to a callable](#6-what-can-be-assigned-to-a-callable)
   - 6.1 [null](#61-null)
   - 6.2 [Free function name](#62-free-function-name)
   - 6.3 [Static member function](#63-static-member-function)
   - 6.4 [Bound member function](#64-bound-member-function)
   - 6.5 [Functor — object with `operator()`](#65-functor--object-with-operator)
   - 6.6 [Functional interface](#66-functional-interface)
   - 6.7 [Another callable](#67-another-callable)
   - 6.8 [Lambda expression](#68-lambda-expression)
7. [Calling a callable](#7-calling-a-callable)
8. [Null and boolean semantics](#8-null-and-boolean-semantics)
9. [Co/contravariance rules](#9-co-contravariance-rules)
10. [Aliases and templates](#10-aliases-and-templates)
11. [Overload disambiguation](#11-overload-disambiguation)
12. [Breaking changes from function references](#12-breaking-changes-from-function-references)
13. [Grammar summary](#13-grammar-summary)

---

## 1. Callable types — syntax and addressers

A callable type is written by placing an **addresser** directly before the parameter list:

```
addresser '(' [ TypeList ] ')' [ ':' ReturnType ] [ 'throws' ThrowsList ]
```

The four permitted addressers are:

| Addresser | Name | Nullable | Rebindable |
|---|---|---|---|
| `*` | pointer | yes | yes |
| `?` | view | yes | no |
| `+` | link | no | yes |
| `&` | reference | no | no |

> **Forbidden addressers:** `!` (owner) and `#` (drain) are **not** permitted on callable types.

**Examples:**

```k
fp  : *(int) : int;          // pointer — nullable, rebindable
vw  : ?(int, double) : bool; // view    — nullable, non-rebindable
lnk : +(int) : int;          // link    — non-null,  rebindable
ref : &() : int;             // reference — non-null, non-rebindable
```

The `const` prefix makes the binding non-rebindable (same as for object indirections):

```k
cfp : const *(int) : int;    // pointer to callable, binding is const
```

`const` here means the variable itself cannot be reassigned; it does not affect the callable's body.

---

## 2. Prototype-only form

A callable type written **without** a leading addresser — `(Params):ReturnType` — is called the *prototype form*. It is **not** directly usable as a variable type. Its only valid uses are:

- as the target of an `alias` or `typedef` declaration;
- as a template type argument.

```k
alias Transform : (int) : int;        // prototype alias — valid
typedef Predicate : (bool) : bool;    // prototype typedef — valid

f : (int) : int;   // ERROR — prototype form cannot be used as a variable type
```

To declare a variable, wrap the prototype in an addresser:

```k
f : *(int) : int;   // OK — pointer to a callable with prototype (int):int
```

---

## 3. Return type — void and non-void callables

K has **no `void` keyword**. A callable that returns nothing simply omits the `: ReturnType` portion of the type:

| Type | Meaning |
|---|---|
| `*(int) : int` | callable taking `int`, returning `int` |
| `*(int)` | callable taking `int`, returning nothing (void) |
| `*()` | callable taking no parameters, returning nothing |
| `*() : int` | callable taking no parameters, returning `int` |

Writing `: void` is a **parse error**.

```k
fp  : *(int) : int;   // returns int
fp2 : *(int);         // returns nothing (void)
fp3 : *(int) : void;  // ERROR — 'void' is not a type in K
```

---

## 4. Checked exceptions — the `throws` clause

A callable type may declare which checked exceptions its implementation is allowed to raise, using the same `throws` syntax as function declarations:

```
'*' '(' TypeList ')' ':' ReturnType 'throws' ThrowableType { ',' ThrowableType }
```

```k
fp  : *(int) : int throws IOException;                  // may throw IOException
fp2 : *(int) : int throws IOException, NetworkException; // may throw either
fp3 : *(int) : int;                                     // throws nothing (checked)
```

**Assignment rules** (covariance — see also [§9](#9-co-contravariance-rules)):

- A function/callable that throws a **subset** of the declared `throws` set may be bound to the callable.
- A function that throws exceptions **not** listed in the callable's `throws` clause **cannot** be bound — this is a compile-time error.

```k
readInt(fd : int) : int throws IOException { ... }

fp  : *(int) : int throws IOException = readInt;   // OK
fp2 : *(int) : int                    = readInt;   // ERROR — readInt throws IOException
                                                   // but fp2 declares no throws clause
```

---

## 5. Addresser semantics

The addresser controls nullability and rebindability of the callable variable, exactly as for object indirections.

### `*` — pointer (nullable, rebindable)

```k
fp : *(int) : int;      // default-initialized to null
fp = add_one;           // rebind at any time
fp = null;              // explicit null assignment — valid
```

### `?` — view (nullable, non-rebindable)

A `?` callable must be initialized at declaration and cannot be reassigned:

```k
vw : ?(int) : int = add_one;   // bound at declaration
vw = add_two;                  // ERROR — view is non-rebindable
```

### `+` — link (non-null, rebindable)

A `+` callable must be initialized at declaration (no implicit null):

```k
lnk : +(int) : int = add_one;  // must be initialized
lnk = add_two;                  // rebind is allowed
// lnk = null;                  // ERROR — link is non-null
```

When assigned from a nullable source, a runtime null-check is inserted:

```k
fp  : *(int) : int = add_one;
lnk : +(int) : int = fp;       // runtime null-check on fp
```

### `&` — reference (non-null, non-rebindable)

A `&` callable must be initialized at declaration and cannot be reassigned:

```k
ref : &(int) : int = add_one;  // bound at declaration, never rebound
// ref = add_two;               // ERROR — reference is non-rebindable
// ref = null;                  // ERROR — reference is non-null
```

---

## 6. What can be assigned to a callable

### 6.1 null

Only `*` (pointer) and `?` (view) callable types accept `null`. `+` and `&` do not.

```k
fp : *(int) : int = null;   // OK
vw : ?(int) : int = null;   // OK
lnk : +(int) : int = null;  // ERROR — link cannot be null
ref : &(int) : int = null;  // ERROR — reference cannot be null
```

### 6.2 Free function name

Any free function whose parameter list and return type match the callable's prototype may be assigned directly:

```k
add_one(x : int) : int { return x + 1; }

fp : *(int) : int = add_one;   // context pointer = null
```

### 6.3 Static member function

A `static` member function is treated identically to a free function — no receiver is needed:

```k
struct Math {
    static double_it(x : int) : int { return x * 2; }
}

fp : *(int) : int = Math::double_it;   // context pointer = null
```

### 6.4 Bound member function

A member function can be bound to a specific receiver object using the `.method` (on an object/reference) or `->method` (through an indirection) syntax. The resulting callable stores both the function pointer and the receiver address as the context pointer:

```k
struct Counter {
    value : int;
    add(x : int) : int { return value + x; }
}

c   : Counter;
c.value = 40;

fp  : *(int) : int = c.add;    // ctx = address of c; calls c.add(x)
fp2 : *(int) : int = ptr->add; // ptr is Counter* or Counter+; ctx = ptr
```

The bound receiver is captured **by address**, not by value. The lifetime of the receiver object must outlive every call through the callable.

### 6.5 Functor — object with `operator()`

A class or struct that defines `operator()(params) : ret` can be bound to a callable whose prototype matches the `operator()` signature. The object itself becomes the context pointer:

```k
struct Adder {
    delta : int;
    operator()(x : int) : int { return x + delta; }
}

a   : Adder;
a.delta = 5;

fp  : *(int) : int = a;        // context = address of a; calls a.operator()(x)
```

The same lifetime caveat applies: the functor object must outlive all calls through the callable.

Invocation on an aggregate type (`a(42)`) also triggers `operator()` lookup, so functors can be called directly without going through a callable variable:

```k
result : int = a(37);   // calls a.operator()(37) → 42
```

### 6.6 Functional interface

An *interface* (or abstract class) whose vtable layout holds **exactly one** abstract virtual slot — the universal destructor slot excluded — is *functional* and may be bound to a callable. The slot's signature must match the callable's prototype. The object's address becomes the context pointer, and dispatch goes through the vtable:

```k
interface Transformer {
    transform(x : int) : int;   // the single abstract method
}

class Doubler : public Transformer {
    override transform(x : int) : int { return x * 2; }
}

d  : Doubler;
fp : *(int) : int = d;      // binds Transformer::transform through Doubler's vtable

use(t : Transformer&) : int {
    g : &(int) : int = t;   // same binding through an interface reference
    return g(21);
}
```

Every addresser is accepted on the receiver: `I&`, `I*`, `I+`, `I?` and `I!`. As for any other bound callable, a `*`/`?` receiver bound to a nullable callable propagates the null (`{null, null}`) instead of trapping, whereas binding it to a `+`/`&` callable dereferences the receiver and raises a `FatalError` when it is null.

The slot count is computed on the **vtable layout**, not on the declarations: an abstract method inherited from a base interface — or re-declared several times along a diamond — counts exactly once. *Default* methods (§ [interfaces](../structs/interfaces.md)) are concrete and are never counted.

A concrete class that implements exactly one abstract slot inherited from a functional base is bindable too, as in the `Doubler` example above.

Errors:

| Condition | Diagnostic |
|---|---|
| The interface / abstract class has zero or more than one abstract slot | `ERR_CALLABLE_NOT_FUNCTIONAL_IFACE` (0x01D7) |
| The single abstract method does not match the callable prototype | `ERR_CALLABLE_IFACE_SIGNATURE_MISMATCH` (0x01D8) |

When the receiver also declares an `operator()`, the functor rule of [§6.5](#65-functor--object-with-operator) wins: `operator()` is looked up first.

### 6.7 Another compatible callable

A callable may be assigned from another callable with a compatible prototype. Compatibility is governed by the co/contravariance rules in [§9](#9-co-contravariance-rules).

```k
fp  : *(int) : int = add_one;
fp2 : *(int) : int = fp;       // copy of fat pointer — OK
```

### 6.8 Lambda expression

A lambda expression (see [Lambdas](lambdas.md)) may be assigned to a callable. If the lambda captures variables, those captures are stored in a compiler-generated closure object whose address becomes the context pointer:

```k
base  : int = 10;
fp    : *(int) : int = (x : int) => x + base;   // closure captures base by reference
result : int = fp(32);   // → 42
```

Non-capturing lambdas have a null context pointer and are therefore freely assignable to any compatible callable.

---

## 7. Calling a callable

A callable is called using ordinary call syntax, identical to a direct function call:

```k
fp : *(int) : int = add_one;
result : int = fp(41);   // → 42
```

Internally the compiler lowers the call to a conditional dispatch:

1. If the context pointer is `null` → call the function pointer with only the explicit arguments.
2. If the context pointer is non-`null` → call the function pointer prepending the context pointer as the hidden first argument.

This dispatch is transparent to the caller. The compiler elides the branch when it can prove the context at compile time (e.g. for `+` and `&` callables bound at initialisation only once).

**Null pointer call:** Calling a `*` or `?` callable that holds `null` is undefined behaviour. No automatic null-check is emitted. Use an explicit null-check (see [§8](#8-null-and-boolean-semantics)) before calling through a nullable callable.

---

## 8. Null and boolean semantics

### Null comparison

```k
fp : *(int) : int;       // null by default
if (fp == null) { ... }  // valid
if (fp != null) { ... }  // valid
```

### Boolean conversion

A callable converts to `bool` as `true` when non-null, `false` when null:

```k
fp : *(int) : int;
if (fp) { ... }          // true when fp != null
b : bool = (bool)fp;     // explicit cast — valid
```

### Equality between two callables

Comparing two callable values with `==` or `!=` is **rejected** in v1. Use null-checks to determine liveness; use identity tokens or wrapper objects to distinguish callables in data structures.

```k
fp1 : *(int) : int = add_one;
fp2 : *(int) : int = add_one;
fp1 == fp2;   // ERROR — callable equality is not supported
```

### Arithmetic operators

All arithmetic operators (`+`, `-`, `*`, `/`, …) are **rejected** on callable types.

---

## 9. Co/contravariance rules

These rules govern when a callable (or function) with prototype `(P1…Pn):R` may be assigned to a callable variable declared with prototype `(Q1…Qm):S`.

The rules are **strict**: the compiler does not insert adapter functions or implicit conversions between incompatible signatures.

### Return type — covariant

The source return type `R` must be *covariant* with respect to the declared return type `S`:

- **Same type:** always valid.
- **Nominal subtype:** `R` is a class or interface that inherits from `S` — valid, but only when `R`'s base `S` is at offset 0 in `R`'s layout (i.e. `S` is the first or only base). If `S`'s sub-object is at a non-zero offset, the assignment is rejected.
- **Primitive widening:** rejected. `int` → `long` is not a valid covariant return substitution.

### Parameters — contravariant

Each source parameter type `Pi` must be *contravariant* with respect to the declared parameter type `Qi`:

- **Same type:** always valid.
- **Nominal supertype:** `Pi` is a base of `Qi` — valid under the same zero-offset rule as for return types.
- **Primitive narrowing:** rejected. `long` → `int` as a parameter is not a valid contravariant substitution.

### Parameter count

The source and target must have the same number of parameters.

### `throws` set — covariant

The source's checked-exception set must be a **subset** of the target's declared `throws` set:

```k
interface Failure {}
interface IOError : public Failure {}

read(fd : int) : int throws IOError { ... }

fp : *(int) : int throws Failure = read;    // OK — IOError ⊆ {Failure}
fp2 : *(int) : int               = read;    // ERROR — read may throw IOError,
                                            // fp2 declares no throws clause
```

### Summary table

| Relationship | Return (covariant) | Params (contravariant) |
|---|---|---|
| Same type | ✓ | ✓ |
| Aggregate derivation, offset 0 | ✓ | ✓ |
| Aggregate derivation, non-zero offset | ✗ | ✗ |
| Primitive widening/narrowing | ✗ | ✗ |

### Consequence of the K object layout

The offset-0 requirement is a *layout* rule, not a nominal one, and the K object
layout makes it strict:

- a **class** (or interface) stores its vptr at field 0, so **no base sub-object of
  a class is ever at offset 0** — class-to-base callable covariance is always
  rejected with `ERR_CALLABLE_COVARIANCE_NEEDS_ADJUSTMENT`;
- a **struct** lays its bases out first, so its **first** base *is* at offset 0 —
  covariance to that base is accepted, but not to any later base;
- a **virtual base** is reached through a `__vbptr_X__` indirection and therefore
  never qualifies.

Identity is decided **nominally first**: a `typedef` never collapses into the type
it renames, so `*(int):Meters` is not satisfied by an `int`-returning function even
though `Meters` is a `typedef` of `int`. A soft `alias` is a name, not a type, and
is transparent.

The addresser of an indirection must also match exactly: a `Base+` return does not
satisfy a `Base*` return, in either direction.

---

## 10. Aliases and templates

### Soft alias (`alias`)

A soft alias introduces an alternative name for a callable prototype without creating a new nominal type:

```k
alias IntMapper : (int) : int;                 // prototype alias
alias Predicate : (bool) : bool;               // prototype alias, void-param version
```

Variables are then declared with an addresser on the alias name:

```k
f : *IntMapper = add_one;    // same as: f : *(int) : int = add_one
```

### Strong alias (`typedef`)

A `typedef` creates a distinct nominal type. Assignments between the original prototype and the `typedef` type require an explicit cast:

```k
typedef Filter : *(int) : bool;     // strong alias of pointer-to-callable

f : Filter = is_positive;   // OK — Filter is the addressed form
```

### Template alias

Callable prototypes are frequently used as template arguments to express higher-order abstractions:

```k
template<typename T, typename R>
alias Function : (T) : R;

template<typename T, typename R>
transform(src : T, fn : *Function<T, R>) : R {
    return fn(src);
}
```

---

## 11. Overload disambiguation

When a function has multiple overloads, the **declared callable type** selects the correct one. The parameter list and return type together identify the overload:

```k
compute(x : int)    : int    { return x + 1; }
compute(x : double) : double { return x + 1.0d; }

fp_i : *(int)    : int    = compute;   // selects (int):int
fp_d : *(double) : double = compute;   // selects (double):double

result : int = fp_i(41);    // → 42
```

If the declared type is ambiguous (e.g. only the return type differs between overloads), the assignment is a compile-time error.

---

## 12. Breaking changes from function references

Callables supersede the old *function reference* types in `*(Params)` notation. There are two **breaking changes**:

### Return type is now required to express non-void return

In the old function reference system, `*(int)` meant "a reference to a function taking `int`, with unspecified (inferred) return type". In the callable system:

> `*(int)` means "a callable taking `int` and returning **nothing** (void)".

Old code that used `*(int)` to point to a function returning, say, `int`, must be updated to `*(int) : int`.

```k
// Old (function reference — inferred return):
fp : *(int) = add_one;   // add_one returns int

// New (callable — explicit return type required):
fp : *(int) : int = add_one;   // correct
fp2 : *(int) = add_one;        // now means void return — type mismatch if add_one returns int
```

### `&` is a new addresser for callables

In the old function reference system, `&` was not used for function types. It is now a valid callable addresser meaning "non-null, non-rebindable callable reference". Old code that happened to use `&` in this context (which would have been a parse error before) is now valid but carries new semantics.

### Member function references are unchanged

The `T::*(Params)` / `T::?(Params)` / `T::+(Params)` member-function-reference syntax is unchanged and continues to work as before. Member function references are distinct from callables: they do not carry a bound receiver and must still be called via `.*` / `->*`. See [Function References](function_references.md).

---

## 13. Grammar summary

```
-- Addressed callable type (variable type, parameter type, return type):
CallableType:
    CallableAdresser '(' [ TypeList ] ')' [ ':' ReturnTypeSpec ] [ ThrowsClause ]
  | 'const' CallableAdresser '(' [ TypeList ] ')' [ ':' ReturnTypeSpec ] [ ThrowsClause ]

CallableAdresser:
    '*' | '?' | '+' | '&'

-- Prototype-only form (alias / typedef / template argument only):
CallablePrototype:
    '(' [ TypeList ] ')' [ ':' ReturnTypeSpec ] [ ThrowsClause ]

-- Return type (void = omitted):
ReturnTypeSpec:
    TypeSpec                      -- any resolved type except void

-- Throws clause:
ThrowsClause:
    'throws' TypeSpec { ',' TypeSpec }

-- Binding expressions:
CallableExpr:
    FunctionName                  -- free function or static member (§6.2, §6.3)
  | ObjExpr '.' MethodName       -- bound member, object on stack/ref (§6.4)
  | IndirExpr '->' MethodName    -- bound member, through indirection (§6.4)
  | AggregateExpr                -- functor or functional interface (§6.5, §6.6)
  | LambdaExpr                   -- lambda (§6.8)
  | 'null'                       -- only for * and ? (§6.1)

-- Call through a callable:
CallableCallExpr:
    PostfixExpr '(' [ ExpressionList ] ')'

-- Null / boolean checks:
NullCheckExpr:
    CallableExpr '==' 'null'
  | CallableExpr '!=' 'null'
  | '(' 'bool' ')' CallableExpr
  | CallableExpr                 -- in boolean context (if / while / …)
```

---

*See also:* [Types](../basic/types.md) · [Functions](functions.md) · [Function References](function_references.md) · [Lambdas](lambdas.md) · [Expressions — Calls](../expressions/call.md) · [Grammar (EBNF)](../grammar.ebnf)
