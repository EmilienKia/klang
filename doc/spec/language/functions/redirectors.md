# Function Redirectors

[← Functions](functions.md) · [← Index](../index.md)

A **function redirector** declares a function name as an alias for another function's implementation,
without duplicating code. At the binary level, the redirector symbol points directly to the target
function's machine code.

---

## Contents

1. [Syntax](#1-syntax)
2. [Semantics](#2-semantics)
3. [Resolution rules](#3-resolution-rules)
4. [Type compatibility](#4-type-compatibility)
5. [Visibility rules](#5-visibility-rules)
6. [Chained redirections](#6-chained-redirections)
7. [Free / member function mixing](#7-free--member-function-mixing)
8. [Virtual functions and vtable interaction](#8-virtual-functions-and-vtable-interaction)
9. [Code generation](#9-code-generation)
10. [Error conditions](#10-error-conditions)

---

## 1. Syntax

### Grammar

```
FunctionRedirectDecl:
    { Specifier } Identifier '(' [ ParameterList ] ')' [ ':' TypeSpec ] '->' TargetRef ';'

TargetRef:
    QualifiedIdentifier [ '(' [ TypeSpecList ] ')' ]

TypeSpecList:
    TypeSpec { ',' TypeSpec }
```

### Examples

```k
// Simple redirect — foo is an alias for bar
foo() -> bar;

// Redirect with return type
foo() : int -> bar;

// Redirect with parameters
foo(a: int, b: int) : int -> add;

// Redirect to a scoped target
foo(a: int) : int -> Base::method;

// Redirect with target disambiguation (overloaded target)
foo(a: int) -> bar(int);

// Redirect with fully qualified target and disambiguation
foo(a: int) -> ns::helper(int);
```

---

## 2. Semantics

A function redirector:

- Declares a new source **symbol**, with its own name, visibility and signature.
- Does **not** generate a new function body; it shares the exact same machine code as the target.
- For a locally defined implementation, a `GlobalAlias` with the redirector's
  mangled name points to the target; its ELF symbol is a regular function symbol.
- A redirect to an imported declaration retains the existing imported linkage
  entry, while its independent source declaration is preserved in KDI.
- The redirector has **no body**, **no member initializer list**, and **no block statement**.

---

## 3. Resolution rules

The target of a redirect is resolved by qualified name lookup starting from the scope of the redirector declaration:

1. The target name is resolved using the same scope-chain rules as any other symbol reference.
2. If multiple overloads match, the optional parameter type list `(types...)` after the target name is used for disambiguation.
3. If the target cannot be found, a compile-time error is emitted.
4. If the target is abstract (has no implementation), a compile-time error is emitted.
5. If the target is deleted (`-> delete`), a compile-time error is emitted.

---

## 4. Type compatibility

The redirector's full prototype (including the implicit `this` parameter for member functions)
must be **strictly compatible** with the target's prototype:

- The number of parameters must match (including implicit `this`).
- Each explicit parameter type must be identical (exact semantic match), including
  signedness, const qualification, addressers, ownership, nominal types, and
  callable signatures. Matching machine representations is not sufficient.
- K typed-vararg packing is caller-local: `values... : T` and `values : T[]`
  may redirect to each other when their resolved parameter types are identical.
- The return type must be identical.
- For member functions: const-qualification of the `this` parameter must be compatible
  (a const redirector can only target a const function).
- Every exception the target declares must be covered by the redirector's `throws`
  clause, either by the same exception type or by one of its ancestors. A noexcept
  redirector must not target a throwing implementation.
- Parameter names and default argument expressions belong to the redirector and
  need not match those of the target. Transparent type aliases remain transparent.

K typed varargs pass one array parameter; they are not native C ellipsis.
Native variadic calling-convention constraints are separate from this packing sugar.

Compatibility is checked after lexical type resolution, before code generation.
Each edge of a redirect chain must satisfy the contract; a compatible final
implementation does not make an incompatible intermediate declaration valid.

```k
alias Count : int;
impl(value : int) : int { return value; }
valid(renamed : Count = 7) : int -> impl;
invalid(value : unsigned int) : int -> impl; // error: different semantic type
```

The implicit receiver has one additional rule for inherited member re-exports:
the redirector's owner may derive from the implementation's owner. The call must
project the receiver to the implementation's base subobject, including when that
subobject has a nonzero offset or is reached through a virtual-base pointer.
This exception does not apply to explicit parameter or return types.

> **Future extension:** Covariance on return types and contravariance on parameter types
> may be relaxed in a future version.

---

## 5. Visibility rules

The **visibility of the redirector** (public, protected, private) is independent of the target's visibility.
This is by design: a redirector can re-export a protected or private function under a different name or
wider visibility.

- A `public` redirector may point to a `protected` parent method — effectively re-exporting it.
- A `private` redirector may point to a `public` function — restricting access.
- **No visibility check is performed on the target** at the redirector's definition site.

This allows intentional visibility changes:

```k
class Base {
    protected impl() : int { return 42; }
}
class Derived : public Base {
    // Re-export the protected method as public under a new name
    public compute() : int -> Base::impl;
}
```

---

## 6. Chained redirections

Chained redirections are **allowed**. The compiler resolves the chain transitively:
the redirector ultimately points to the final concrete implementation.

```k
impl() : int { return 55; }
mid() : int -> impl;
top() : int -> mid;
// top() → mid() → impl() : all three symbols point to the same code
```

- Circular redirections are detected and produce a compile-time error.
- Chains are validated and flattened after their individual signatures and targets
  have been resolved. Overload disambiguation precedes cycle detection.
- A receiver binding introduced by a member edge is retained when later edges
  use free functions. Thus a free alias taking `Derived&` may reach a `Base`
  implementation through a valid `Derived` member redirect; its direct calls
  still project argument zero to the actual `Base` subobject.
- Imported redirectors preserve their source signature and symbolic provider
  receiver contract in KDI. Calls to their exported alias symbols apply the same
  projection, without executable redirect adapters.
- An unbound callable cannot insert a receiver adjustment. Member references
  therefore name the provider's receiver owner; a free alias whose first
  parameter has a retained receiver binding exposes that actual receiver type
  when bound as a bare callable. Its direct named calls still use the source
  logical signature.

---

## 7. Free / member function mixing

A free function may redirect to a member function and vice versa, as long as the **underlying
calling convention prototypes are compatible**. The implicit `this` parameter of a member function
counts as a regular parameter for compatibility purposes (unified calling convention).

```k
// A static member can redirect to a free function
struct Foo {
    static compute(a: int) : int -> helper;
}
helper(a: int) : int { return a * 2; }
```

Since member functions use the same calling convention as free functions (with `this`
as the first parameter), any free function whose first parameter matches the `this`
type of a member function can serve as its redirect target, and vice versa.
The explicit receiver parameter must match the implicit receiver semantically:
for example, `Foo&` is not interchangeable with `Foo*` or `Foo!`.

---

## 8. Virtual functions and vtable interaction

A redirector declared on a virtual method participates in vtable slot assignment normally:

- The vtable slot stores a pointer to the **target**'s implementation (not a wrapper).
- A derived class can override a redirected virtual method with a concrete implementation.
- Redirectors do **not** replace or interact with virtual dispatch thunks; thunks remain separate.
- Receiver adjustment in an ordinary virtual dispatch thunk uses the actual
  implementation's receiver, not merely the aggregate declaring the redirector.

```k
class Base {
    speak() : int { return 42; }
}

class Derived : public Base {
    speak() : int -> Base::speak;   // vtable slot points directly to Base::speak impl
}

class Final : public Derived {
    speak() : int { return 99; }    // normal override, replaces the redirect
}
```

Virtual dispatch works correctly through the full hierarchy:

```k
dispatch(b: Base&) : int { return b.speak(); }

main() : int {
    d : Derived;
    f : Final;
    dispatch(d);  // → 42 (via Base::speak, through redirect)
    dispatch(f);  // → 99 (via Final::speak, normal override)
}
```

---

## 9. Code generation

For an implementation defined in the current module, a redirector is compiled as:

```llvm
@_KFN...redirectorE... = alias i32 (...)*, @_KFN...targetE...
```

This `GlobalAlias` ensures:
- Both symbols resolve to the same function pointer in the final binary.
- No extra indirection at call sites.
- The alias is visible in the ELF symbol table as a regular function symbol.
- The context function map registers the redirector pointing to the target's LLVM function
  so that call sites can resolve the alias transparently.

LLVM and ELF aliases cannot define another module's external declaration.
An imported re-export therefore uses the existing callable entry symbol in its
KDI linkage fields, while retaining its own logical name, defaults and receiver
contract. Consumers must not conflate source declarations merely because their
native linkage entries are identical.

No executable redirect wrapper is generated. A direct call or bound callable
projects an inherited receiver before entering the implementation; virtual calls
use the existing vtable receiver-adjustment machinery. An unbound member reference
is a bare function pointer and must use the implementation's receiver type when
the declaring and implementation receivers differ.

---

## 10. Error conditions

| Condition | Error |
|-----------|-------|
| Target name not found | `Function redirector 'X' targets 'Y', which could not be resolved to a function` |
| Circular redirect chain | `Circular redirect chain detected involving function 'X'` |
| Target is abstract | `Function redirector 'X' targets abstract function 'Y', which has no implementation` |
| Target is deleted | `Function redirector 'X' targets deleted function 'Y'` |
| Incompatible semantic prototype or exception contract | `Function redirector 'X' has an incompatible signature with target 'Y': ...` |
| Missing target after `->` | Parse error: `Function redirect declaration expects a target function name` |
| Missing `;` after target | Parse error: `Function redirect declaration expects ';' after target` |
