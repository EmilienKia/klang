# Operator Overloading

[← Index](../index.md) · [Functions](functions.md)

*Operator overloading* allows user-defined types (structs, classes, interfaces) to provide custom behaviour for built-in operators.
When an expression uses an operator whose left-hand operand (or sole operand, for unary operators) is of an aggregate type, the compiler looks for a matching **operator function** and dispatches the call to it.

---

## Contents

1. [Overview](#1-overview)
2. [Syntax — declaring an operator function](#2-syntax--declaring-an-operator-function)
3. [Overloadable operators](#3-overloadable-operators)
4. [Member operator functions](#4-member-operator-functions)
5. [Non-member operator functions](#5-non-member-operator-functions)
6. [Prefix and postfix increment / decrement](#6-prefix-and-postfix-increment--decrement)
7. [Cast operator](#7-cast-operator)
8. [Overload resolution](#8-overload-resolution)
9. [Const operator functions](#9-const-operator-functions)
10. [Operators and inheritance](#10-operators-and-inheritance)
11. [Operators and interfaces](#11-operators-and-interfaces)
12. [Operators across module boundaries](#12-operators-across-module-boundaries)
13. [Operator chaining](#13-operator-chaining)
14. [Restrictions](#14-restrictions)

---

## 1. Overview

When the compiler encounters an expression such as `a + b` and `a` is of struct/class type `T`, it performs the following lookup:

1. Search for a **member operator function** named `operator +` in `T` (and its base classes).
2. Search for a **non-member operator function** named `operator +` visible in the enclosing scope, whose first parameter matches `T`.
3. If both member and non-member candidates exist, apply [overload resolution](#8-overload-resolution) to select the best match.
4. If no operator function is found, emit a compile-time error.

For unary operators (`-a`, `!a`, `~a`, `++a`, `a++`, etc.), the same process applies with the sole operand as `a`.

---

## 2. Syntax — declaring an operator function

An operator function is declared with the `operator` keyword followed by the operator symbol, in place of a regular function name.

### Grammar

```
OperatorFunctionDecl:
    { Specifier } 'operator' OperatorSymbol '(' [ ParameterList ] ')' [ ':' TypeSpec ] BlockStatement
    | { Specifier } 'operator' '(' ')' ':' TypeSpec BlockStatement          // cast operator
```

```
OperatorSymbol: (one of)
    +   -   *   /   %                          // arithmetic
    &   |   ^   ~                              // bitwise
    <<  >>                                     // shift
    &&  ||  !                                  // logical
    ==  !=  <  >  <=  >=                       // comparison
    =   +=  -=  *=  /=  %=  &=  |=  ^=  <<=  >>=    // assignment
    ++_   --_                                  // prefix increment / decrement
    _++   _--                                  // postfix increment / decrement
    ()                                         // cast (empty parentheses — see § 7)
```

**Example:**

```k
struct Vec2 {
    x: int;
    y: int;
    Vec2(ax: int, ay: int) : x(ax), y(ay) {}

    operator +(other: Vec2&) : int {
        return x + other.x + y + other.y;
    }
}
```

---

## 3. Overloadable operators

### Binary operators

| Category | Operators |
|----------|-----------|
| Arithmetic | `+` `-` `*` `/` `%` |
| Bitwise | `&` `\|` `^` |
| Shift | `<<` `>>` |
| Logical | `&&` `\|\|` |
| Comparison | `==` `!=` `<` `>` `<=` `>=` |
| Assignment | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` |

A member binary operator takes **one explicit parameter** (the right-hand operand); `this` is the left-hand operand.

A non-member binary operator takes **two parameters**: the left-hand operand first, then the right-hand operand.

### Unary operators

| Category | Operators |
|----------|-----------|
| Arithmetic | `+` (unary plus), `-` (unary minus) |
| Bitwise | `~` (bitwise NOT) |
| Logical | `!` (logical NOT) |
| Increment/Decrement | `++_` (prefix `++`), `--_` (prefix `--`), `_++` (postfix `++`), `_--` (postfix `--`) |

A member unary operator takes **no explicit parameter**; `this` is the sole operand.

A non-member unary operator takes **one parameter** — the operand.

### Cast operator

The cast operator `operator()` has no explicit parameter and no identifier parameter list. It takes the return type as the target type. See [§ 7](#7-cast-operator).

---

## 4. Member operator functions

A member operator function is declared inside a struct, class, or interface body.
The left-hand operand (or sole operand for unary operators) is the implicit `this` parameter.

```k
struct Num {
    v: int;
    Num(av: int) : v(av) {}

    // Binary: this + other
    operator +(other: Num&) : int { return v + other.v; }

    // Binary with a primitive right-hand side
    operator *(n: int) : int { return v * n; }

    // Unary minus
    operator -() : int { return -v; }

    // Comparison
    operator ==(other: Num&) : bool { return v == other.v; }
}

test() : int {
    a: Num(3);
    b: Num(4);
    return a + b;      // calls a.operator+(b) → 7
}
```

### Member binary operators

For `a OP b` where `a` is of aggregate type `T`:

```k
struct T {
    operator OP(rhs: RType) : RetType { ... }
}
```

The call is equivalent to `a.operator_OP(b)`.

### Member unary operators

For `OP a` (or `a OP` for postfix), where `a` is of aggregate type `T`:

```k
struct T {
    operator OP() : RetType { ... }
}
```

---

## 5. Non-member operator functions

A non-member (free) operator function is declared at namespace/module level, outside any struct body.

### Binary non-member operator

```k
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}
}

operator +(a: Vec&, b: Vec&) : int { return a.v + b.v; }

test() : int {
    a: Vec(10);
    b: Vec(32);
    return a + b;   // calls operator+(a, b) → 42
}
```

### Unary non-member operator

```k
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}
}

operator -(a: Vec&) : int { return -a.v; }

test() : int {
    a: Vec(42);
    return -a;      // calls operator-(a) → -42
}
```

### Non-member vs member priority

When both a member and a non-member operator are candidates, [overload resolution](#8-overload-resolution) selects the best match.
At equal cast weight, **member operators take priority** over non-member operators.

```k
struct Val {
    v: int;
    Val(av: int) : v(av) {}
    operator +(other: Val&) : int { return v + other.v + 100; }
}
operator +(a: Val&, b: Val&) : int { return a.v + b.v; }

test() : int {
    a: Val(1);
    b: Val(2);
    return a + b;   // member wins → 103 (not 3)
}
```

However, if the non-member operator has a better (more exact) parameter match, it wins:

```k
struct Val {
    v: int;
    Val(av: int) : v(av) {}
    operator +(n: long) : int { return v + 1000; }   // requires int→long widening
}
operator +(a: Val&, n: int) : int { return a.v + n; } // exact match for int

test() : int {
    a: Val(10);
    i: int = 5;
    return a + i;   // non-member wins (exact match) → 15
}
```

---

## 6. Prefix and postfix increment / decrement

Prefix and postfix increment/decrement are distinct operators, declared with special syntax:

| Syntax | Meaning | Invocation |
|--------|---------|------------|
| `operator ++_()` | Prefix increment | `++obj` |
| `operator --_()` | Prefix decrement | `--obj` |
| `operator _++()` | Postfix increment | `obj++` |
| `operator _--()` | Postfix decrement | `obj--` |

The underscore `_` represents the operand's position relative to the operator symbol.

### Member declaration

```k
struct Counter {
    v: int;
    Counter(av: int) : v(av) {}

    // Prefix: increment then return new value
    operator ++_() : int {
        this.v = this.v + 1;
        return this.v;
    }

    // Postfix: return old value then increment
    operator _++() : int {
        r : int = this.v;
        this.v = this.v + 1;
        return r;
    }
}

test_prefix() : int {
    c: Counter(10);
    return ++c;    // 11
}
test_postfix() : int {
    c: Counter(10);
    return c++;    // 10 (c.v is now 11)
}
```

### Non-member declaration

```k
struct Counter {
    v: int;
    Counter(av: int) : v(av) {}
}

operator ++_(c: Counter&) : int {
    c.v = c.v + 1;
    return c.v;
}

test() : int {
    c: Counter(10);
    return ++c;    // 11
}
```

---

## 7. Cast operator

A **cast operator** (conversion operator) allows a struct/class instance to be implicitly or explicitly converted to another type via the `(Type) expr` cast syntax.

### Declaration

The cast operator uses empty parentheses after `operator` and specifies the target type as the return type:

```k
struct Num {
    v: int;
    Num(av: int) : v(av) {}

    operator() : int { return v; }        // converts Num → int
    operator() : double { return v; }     // converts Num → double (overloaded)
}
```

### Grammar

```
CastOperatorDecl:
    { Specifier } 'operator' '(' ')' ':' TypeSpec BlockStatement
```

### Invocation

A cast operator is invoked when an explicit cast is applied to an object of the aggregate type:

```k
n : Num(42);
i : int = (int) n;        // calls operator() : int → 42
d : double = (double) n;  // calls operator() : double → 42.0
```

---

## 8. Overload resolution

When multiple candidate operator functions exist (from the aggregate, its base classes, and non-member declarations), the compiler selects the best match using a scoring system.

### Resolution steps

1. **Collect candidates:**
   - Member operators from the aggregate and its inheritance hierarchy (with [name-hiding](#10-operators-and-inheritance)).
   - Non-member operators visible in the enclosing scope whose first parameter matches the left-hand operand's type.

2. **Score each candidate** by computing a *cast weight* for the right-hand operand (binary) or sole operand (non-member unary):
   - **Exact match** (type matches perfectly): weight 0 (best).
   - **Widening conversion** (e.g., `short` → `int`, `Derived&` → `Base&`): weight proportional to the conversion distance.
   - **No possible conversion**: candidate is eliminated.

3. **Select the winner:**
   - The candidate with the **lowest cast weight** wins.
   - At equal weight, **member operators take priority** over non-member operators.
   - If two or more candidates remain tied after these rules, the call is **ambiguous** and a compile-time error is reported.

### Implicit conversions in operator resolution

The same implicit widening conversions that apply to regular function calls also apply to operator operands:

```k
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}
    operator +(n: int) : int { return v + n; }
}

test() : int {
    a: Vec(40);
    s: short = 2;
    return a + s;    // s is widened from short → int; calls operator+(int)
}
```

### Ambiguity example

```k
struct Val {
    v: int;
    Val(av: int) : v(av) {}
    operator +(n: int) : int { return v + n; }
    operator +(n: long) : long { return v + n; }
}

test() {
    a: Val(10);
    s: short = 5;
    a + s;    // ERROR: ambiguous — short→int and short→long have equal cast weight
}
```

---

## 9. Const operator functions

A member operator function may be declared `const`, meaning it can be called on const objects.

### Semantics

- A `const` operator function receives `this` as `const T&`.
- It can be called on both mutable and const objects.
- A **mutable** (non-const) operator function can only be called on mutable objects.
- If a mutable operator is invoked on a const object, a compile-time error is reported.

### Declaration

```k
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}

    // Const operator: callable on both mutable and const objects
    const operator +(n: int) : int { return v + n; }

    // Mutable operator: only callable on mutable objects
    operator -() : int { return -v; }
}

compute(a: const Vec&) : int {
    return a + 5;       // OK: operator+ is const
    // return -a;       // ERROR: operator-() is not const
}
```

### Const filtering

When the left-hand operand is const, only `const` member operators are candidates.
If no const member operator matches but a mutable one exists, the compiler emits an error:

> Cannot call mutable member operator 'OP' on a const object of type 'T': only const member operators can be called on const objects; declare the operator as 'const' to allow calls on const objects

### Non-member operators and constness

Non-member operators are not affected by the constness of `this` (they have no `this`).
A non-member operator taking `const T&` as its first parameter can serve as a fallback when no const member operator exists:

```k
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}
}
// Non-member operator with const parameter
operator +(a: const Vec&, n: int) : int { return a.v + n; }

compute(a: const Vec&) : int {
    return a + 5;   // OK: non-member operator takes const Vec&
}
```

### Const/mutable overload resolution

When both a `const` and a mutable operator with the same signature exist:
- On a **mutable** object, the mutable version is preferred.
- On a **const** object, only the `const` version is a candidate.

```k
struct Val {
    v: int;
    Val(av: int) : v(av) {}
    const operator +(n: int) : int { return v + n; }
    operator +(n: int) : int { return v + n + 100; }
}

test_mutable() : int {
    a: Val(10);
    return a + 5;           // mutable version → 115
}
compute(a: const Val&) : int {
    return a + 5;           // const version → 15
}
```

### Const structs

If a struct is declared `const`, all its non-static member functions (including operators) are implicitly promoted to `const`.  See [Structures — Const structs](../structs/structs.md#13-const-structs).

---

## 10. Operators and inheritance

### Inherited operators

Operator functions are inherited following the same rules as regular member functions.
If a derived struct/class does not declare its own version of an operator, the base class's operator is used.

### Name hiding

If a derived struct/class declares **any** operator function with a given name (e.g., `operator +`), it **hides all** base-class versions of that operator.
This follows the C++-style name-hiding semantics.

```k
class Base {
    v: int;
    Base(av: int) : v(av) {}
    operator +(other: Base&) : int { return this.v + other.v; }
}
class Derived : public Base {
    Derived(av: int) : Base(av) {}
    operator +(other: Base&) : int { return (this.v + other.v) * 10; }
}

call_add(a: Base&, b: Base&) : int { return a + b; }

test_base() : int {
    a: Base(3);
    b: Base(4);
    return call_add(a, b);    // Base::operator+ → 7
}
test_derived() : int {
    a: Derived(3);
    b: Base(4);
    return call_add(a, b);    // Derived::operator+ via virtual dispatch → 70
}
```

### Virtual dispatch

For `class` types (not plain `struct`), operator functions participate in virtual dispatch.
When `a + b` is called through a base-class reference and `a`'s dynamic type has overridden `operator +`, the derived version is called.

---

## 11. Operators and interfaces

An `interface` may declare abstract operator functions.
A class implementing the interface must provide a concrete implementation.

```k
interface Addable {
    operator +(other: Addable&) : int;   // abstract operator
}

class MyVal : public Addable {
    v: int;
    MyVal(av: int) : v(av) {}
    operator +(other: Addable&) : int { return this.v + 1000; }
}

call_add(a: Addable&, b: Addable&) : int { return a + b; }

test() : int {
    a: MyVal(42);
    b: MyVal(0);
    return call_add(a, b);   // dispatched via vtable → 1042
}
```

---

## 12. Operators across module boundaries

Operator functions are exported as part of a library and can be used in importing modules.
Both member and non-member operators are exported.

```k
// ── Library: oplib ──
module oplib;
struct Vec2 {
    x: int;
    y: int;
    Vec2(ax: int, ay: int) : x(ax), y(ay) {}
    operator +(other: Vec2&) : int {
        return x + other.x + y + other.y;
    }
}
```

```k
// ── Executable ──
module opexec;
import oplib;

main() : int {
    a : oplib::Vec2(10, 20);
    b : oplib::Vec2(5, 7);
    return a + b;    // calls oplib::Vec2::operator+
}
```

---

## 13. Operator chaining

Operators on aggregate types can be chained in the same way as built-in operators.
Each sub-expression is evaluated according to operator precedence and the result is used as an operand for the next operation.

```k
struct Val {
    v: int;
    Val(av: int) : v(av) {}
    operator +(other: Val&) : int { return v + other.v; }
}

test() : int {
    a: Val(1);
    b: Val(2);
    c: Val(3);
    return (a + b) + (a + c);   // (1+2) + (1+3) = 7
}
```

> **Note:** chaining requires that the return type of the first operator call matches an operand type expected by the second operator. If `operator +` returns `int` (a primitive), the second `+` uses built-in integer addition. To chain with aggregate semantics at every step, the operator should return the struct type itself.

---

## 14. Restrictions

- **Only aggregate types:** operator overloading is only available when at least one operand is of a user-defined aggregate type (struct, class, or interface). Operators between two primitive types always use the built-in semantics.
- **No new operators:** only existing K operators can be overloaded. No new operator symbols can be introduced.
- **No arity change:** a binary operator must remain binary; a unary operator must remain unary.
- **Return type freedom:** operator functions may return any type — there is no constraint that `operator +` must return the same type as its operands.
- **No `operator []` or `operator ()`** (function call): subscript and call operators are not currently overloadable. Subscript accesses and function calls always use the built-in semantics.

---

## Summary table — operator canonical names

The compiler maps each operator declaration to an internal canonical function name. These names follow the [Itanium C++ ABI](https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling-operator) 2-letter convention.

| Operator | Declaration syntax | Internal name |
|----------|-------------------|---------------|
| `+` (binary/unary) | `operator +` | `__operator_pl_` |
| `-` (binary/unary) | `operator -` | `__operator_mi_` |
| `*` | `operator *` | `__operator_ml_` |
| `/` | `operator /` | `__operator_dv_` |
| `%` | `operator %` | `__operator_rm_` |
| `&` (bitwise) | `operator &` | `__operator_an_` |
| `\|` | `operator \|` | `__operator_or_` |
| `^` (bitwise XOR) | `operator ^` | `__operator_eo_` |
| `~` | `operator ~` | `__operator_co_` |
| `<<` | `operator <<` | `__operator_ls_` |
| `>>` | `operator >>` | `__operator_rs_` |
| `&&` | `operator &&` | `__operator_aa_` |
| `\|\|` | `operator \|\|` | `__operator_oo_` |
| `!` | `operator !` | `__operator_nt_` |
| `==` | `operator ==` | `__operator_eq_` |
| `!=` | `operator !=` | `__operator_ne_` |
| `<` | `operator <` | `__operator_lt_` |
| `>` | `operator >` | `__operator_gt_` |
| `<=` | `operator <=` | `__operator_le_` |
| `>=` | `operator >=` | `__operator_ge_` |
| `=` | `operator =` | `__operator_aS_` |
| `+=` | `operator +=` | `__operator_pL_` |
| `-=` | `operator -=` | `__operator_mI_` |
| `*=` | `operator *=` | `__operator_mL_` |
| `/=` | `operator /=` | `__operator_dV_` |
| `%=` | `operator %=` | `__operator_rM_` |
| `&=` | `operator &=` | `__operator_aN_` |
| `\|=` | `operator \|=` | `__operator_oR_` |
| `^=` | `operator ^=` | `__operator_eO_` |
| `<<=` | `operator <<=` | `__operator_lS_` |
| `>>=` | `operator >>=` | `__operator_rS_` |
| `++` (prefix) | `operator ++_` | `__operator_pp_` |
| `--` (prefix) | `operator --_` | `__operator_mm_` |
| `++` (postfix) | `operator _++` | `__operator_PP_` |
| `--` (postfix) | `operator _--` | `__operator_MM_` |
| cast | `operator() : T` | `__operator_cv_T` |

---

*See also:* [Functions](functions.md) · [Function Overloading](overloading.md) · [Expressions](../expressions/expressions.md) · [Binary Operators](../expressions/binary.md) · [Unary Operators](../expressions/unary.md) · [Structures](../structs/structs.md) · [Classes and Virtuality](../structs/classes.md) · [Interfaces](../structs/interfaces.md)

