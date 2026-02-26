# Types

[← Index](../index.md)

K is a statically-typed language. Every expression has a type determined at compile time.

---

## Contents

1. [Primitive types](#1-primitive-types)
2. [Reference types](#2-reference-types)
3. [Pointer types](#3-pointer-types)
4. [Array types](#4-array-types)
5. [Struct types](#5-struct-types)
6. [Type specifiers — grammar](#6-type-specifiers--grammar)
7. [Implicit conversions](#7-implicit-conversions)

---

## 1. Primitive types

Primitive types are built-in types that represent scalar values.

### Integer types

| Keyword           | Bits | Signed | Range |
|-------------------|------|--------|-------|
| `bool`            | 1    | —      | `false` / `true` |
| `byte`            | 8    | yes    | −128 … 127 |
| `char`            | 8    | yes    | −128 … 127 (character alias) |
| `unsigned byte`   | 8    | no     | 0 … 255 |
| `short`           | 16   | yes    | −32 768 … 32 767 |
| `unsigned short`  | 16   | no     | 0 … 65 535 |
| `int`             | 32   | yes    | −2 147 483 648 … 2 147 483 647 |
| `unsigned int`    | 32   | no     | 0 … 4 294 967 295 |
| `long`            | 64   | yes    | −2⁶³ … 2⁶³−1 |
| `unsigned long`   | 64   | no     | 0 … 2⁶⁴−1 |

`byte` and `char` refer to the same underlying 8-bit signed integer type.
`unsigned` is a modifier that makes the following integer type unsigned.

### Floating-point types

| Keyword  | Bits | Standard          |
|----------|------|-------------------|
| `float`  | 32   | IEEE 754 single   |
| `double` | 64   | IEEE 754 double   |

### Boolean type

`bool` represents a boolean value: `true` or `false`.
`bool` is a distinct type from integer types.

---

## 2. Reference types

A reference is an alias for an existing object. It behaves as a non-nullable, non-rebindable pointer that is always initialised.

A reference type is formed by appending `&` to a type.

**Grammar:**

```
ReferenceTypeSuffix:
    '&'
```

**Examples:**

```k
// Reference as a function parameter (pass by reference)
set(var: int&, val: int) {
    var = val;          // modifies the referenced object
}

// Reference as a local variable (alias)
test() : int {
    x : int = 10;
    r : int& = x;       // r is bound to x
    r = 42;             // assigns 42 to x through r
    return x;           // returns 42
}
```

References can be used as function parameters to pass objects by reference (allowing modification or avoiding copies), and as local variable aliases.

**Constraints:**

1. **Mandatory initialisation** — A reference variable must be initialised at its declaration.
   The following is a compile-time error:
   ```k
   r : int&;           // ERROR: reference without initialiser
   ```

2. **Initialiser must be addressable (lvalue)** — The initialiser must denote an existing object
   (a variable, parameter, or struct member). A temporary value or arithmetic result is not
   accepted:
   ```k
   r : int& = 42;      // ERROR: 42 is not addressable
   r : int& = x + 1;   // ERROR: x+1 is a temporary
   ```

3. **Type must match exactly** — The type of the initialiser must be the same as the referenced
   type; implicit conversions are not applied when binding a reference:
   ```k
   d : double = 3.14d;
   r : int& = d;       // ERROR: int& cannot refer to a double
   ```

4. **Binding is permanent (no rebind)** — Once bound, a reference always refers to the same
   object. Assigning to a reference modifies the value of the referred-to object, not the
   binding itself:
   ```k
   x : int = 10;
   y : int = 20;
   r : int& = x;
   r = 99;             // sets x to 99; r is still bound to x, NOT rebound to y
   ```

5. **No null reference** — A reference is always bound to a valid object; there is no null
   reference.

6. **References to references** — Reference-to-reference types are used internally by the
   compiler (e.g. to model variable access) but have restricted user-facing uses.

---

## 3. Pointer types

A pointer holds the memory address of an object. Pointers may be null.

A pointer type is formed by appending `*` to a type.

**Grammar:**

```
PointerTypeSuffix:
    '*'
```

**Examples:**

```k
p : int*;               // pointer to int
p = &a;                 // take address of 'a'
*p += 1;                // dereference and modify
```

**Pointer operators:**

| Operator | Meaning |
|----------|---------|
| `&expr`  | Address-of: yields a pointer to `expr` |
| `*expr`  | Dereference: yields the object pointed to by `expr` |
| `p->m`   | Member access through pointer: equivalent to `(*p).m` |

---

## 4. Array types

An array type represents a fixed-size sequence of elements of the same type.

### Sized array

A sized array has a fixed number of elements specified at the type level.

```
SizedArrayTypeSuffix:
    '[' IntegerLiteral ']'
```

**Example:**

```k
arr : int[4];           // array of 4 ints
set(p: int[4]&, i: int, v: int) {
    p[i] = v;
}
```

### Unsized array (pointer decay)

An array type may omit the size (`[]`), which is treated as a pointer-like view.

```
UnsizedArrayTypeSuffix:
    '[' ']'
```

**Subscript access:**
Elements are accessed via the subscript operator `[ ]`.

```k
a[0] = 1;
x = a[2];
```

**Constraints:**
- Array indices are zero-based.
- Bounds checking is not performed at runtime in the current implementation.

---

## 5. Struct types

A struct type is a user-defined composite type. See the [Structures](../structs/structs.md) reference for full details.

A struct type is referenced by name (simple or qualified).

```k
p : plop;               // variable of struct type 'plop'
r : plop&;              // reference to 'plop'
ptr : plop*;            // pointer to 'plop'
```

---

## 6. Type specifiers — grammar

Types appear in variable declarations, parameter declarations, and return type annotations.

### Grammar

```
TypeSpec:
    FundamentalTypeSpec { TypeSuffix }
    | QualifiedIdentifier { TypeSuffix }

FundamentalTypeSpec:
    [ 'unsigned' ] ( 'byte' | 'char' | 'short' | 'int' | 'long' | 'float' | 'double' )
    | 'bool'

TypeSuffix:
    '[' [ IntegerLiteral ] ']'     -- array suffix (sized or unsized)
    | '*'                          -- pointer suffix
    | '&'                          -- reference suffix
```

Suffixes may be chained: `int*` is a pointer to int; `int[4]` is a 4-element int array; `int[4]&` is a reference to a 4-element int array.

**Examples:**

```k
int
double
unsigned int
short*
int[4]
int[4]&
plop&
plop*
```

---

## 7. Implicit conversions

The compiler performs implicit type conversions in certain contexts (e.g., function call arguments, assignments):

### Widening conversions (no data loss)

A narrower integer or float type is widened to a broader one automatically.

| From        | To                     |
|-------------|------------------------|
| `byte`/`char` | `short`, `int`, `long` |
| `short`     | `int`, `long`          |
| `int`       | `long`                 |
| `float`     | `double`               |
| integer     | `float` or `double`    |

### Narrowing conversions (possible data loss)

Narrowing conversions are also accepted implicitly by the current compiler (e.g., passing an `int` where a `short` is expected). The programmer is responsible for ensuring correctness.

> **Note:** This behaviour may be tightened in future versions to require an explicit cast for narrowing conversions.

### Explicit cast

A C-style cast converts an expression to a named type:

```k
x : int = (int) someDouble;
```

**Grammar:**

```
CastExpr:
    '(' TypeSpec ')' CastExpr
```

---

*See also:* [Literals](../expressions/literals.md) · [Expressions](../expressions/expressions.md) · [Structures](../structs/structs.md)
