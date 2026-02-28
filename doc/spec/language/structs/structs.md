# Structures

[← Index](../index.md)

A *struct* (structure) is a user-defined composite type that aggregates fields and member functions.

---

## Contents
1. [Struct declaration](#1-struct-declaration)
2. [Fields](#2-fields)
3. [Member functions](#3-member-functions)
4. [Using a struct — declaring variables](#4-using-a-struct--declaring-variables)
5. [Field access and member function calls](#5-field-access-and-member-function-calls)
6. [Default field initialisation](#6-default-field-initialisation)
7. [Struct type in function parameters](#7-struct-type-in-function-parameters)
8. [Aggregated structs](#8-aggregated-structs)
9. [Struct specifiers](#9-struct-specifiers)
10. [Member visibility](#10-member-visibility)
11. [Final structs](#11-final-structs)
12. [Const member functions](#12-const-member-functions)
13. [Const structs](#13-const-structs)
---
## 1. Struct declaration
A struct is declared with the `struct` keyword, a name, and a body containing field and member function declarations.
### Grammar
```
StructDecl:
    { Specifier } 'struct' Identifier '{' { Declaration } '}'
Specifier: (one of)
    'static'
```
`static` before `struct` declares a *static nested struct* (see [Nested Structures](nested.md)).
**Example:**
```k
module myapp;
struct plop {
    a : int;
    b : int;
    add(c: int) : int {
        return a + b + c;
    }
}
```
Struct declarations may appear at module level or inside another struct body.
---
## 2. Fields
Fields are the data members of a struct.  
They are declared with the same syntax as local variables (name `:` type, optional initialiser).
### Grammar
```
FieldDecl:
    { Specifier } Identifier ':' TypeSpec [ '=' ConditionalExpr ] ';'
```
**Examples:**
```k
struct Point {
    x : double;
    y : double;
}
struct Config {
    timeout : int = 30;      // default value 30
    enabled : bool = true;
    name    : char*;         // pointer field, no default
}
```
**Default values:**  
A field may have a default constant expression (evaluated once for the type, used when the field is zero/value-initialised or when a compiler-generated default constructor runs).
**Static fields:**  
A field declared `static` is shared across all instances.  
See [Static Functions — Static member variables](../functions/static.md#4-static-member-variables).
---
## 3. Member functions
Member functions are declared inside the struct body with the same syntax as free functions.  
They receive an implicit `this` parameter giving access to the current instance.
### Grammar
```
MemberFunctionDecl:
    { Specifier } Identifier '(' [ ParameterList ] ')' [ ':' TypeSpec ] BlockStatement
```
See [Functions](../functions/functions.md) for the full function syntax.
Inside a member function:
- Fields and other member functions are accessible by name (implicit `this.`).
- `this` is also available explicitly: `this.field`.
```k
struct Counter {
    n : int;
    increment() { n = n + 1; }
    get() : int { return n; }
    getExplicit() : int { return this.n; }  // same as get()
}
```
---
## 4. Using a struct — declaring variables
A struct variable is declared like any other variable, using the struct name as the type specifier.
```k
p : plop;              // default construction
q : plop(5);           // explicit constructor
g : plop;              // global struct variable
r : plop&;             // reference to plop (function parameter or binding)
ptr : plop*;           // pointer to plop
```
---
## 5. Field access and member function calls
Fields and member functions are accessed with `.` (object or reference) or `->` (pointer):
```k
p.a = 10;
p.b = 32;
result : int = p.add(8);     // calls member function
ptr->a = 5;
result2 : int = ptr->add(3);
```
---
## 6. Default field initialisation
Fields are zero-initialised by default:
- Integer fields → 0
- Float/double fields → 0.0
- Bool fields → false
- Pointer fields → null (0)
- Struct fields → recursively default-initialised
If a field has an explicit default expression (e.g., `a : int = 5`), that value is used instead of zero when the struct is default-constructed.
```k
struct plop {
    a : int = 5;     // default 5
    b : int = 12;    // default 12
    c : int;         // default 0
}
test_local() : int {
    p : plop;
    return p.sum();  // returns 5 + 12 + 0 = 17
}
```
---
## 7. Struct type in function parameters
Struct objects can be passed to functions by value, by reference, or by pointer:
```k
// Pass by reference (modifiable)
test_ref_call(r: plop&) : int {
    r.b = 72;
    return r.add();
}
// Pass by pointer
update(p: plop*) {
    p->a = 100;
}
```
Passing by value copies the struct; passing by reference or pointer avoids the copy and allows modification.
---
## 8. Aggregated structs
Structs may contain fields of other struct types (aggregation):
```k
struct plop {
    a : int = 3;
    add(c: int) : int { return a + c; }
}
struct titi {
    b : int = 5;
    p : plop;         // nested struct by value
    add() : int { return p.add(b); }
}
test() : int {
    t : titi;
    t.p.a = 7;
    return t.add();   // 7 + 5 = 12
}
```
---
## 9. Struct specifiers

Specifiers appear before a declaration inside a struct body (or before the `struct` keyword itself for the struct's own visibility).

| Specifier   | Applies to            | Meaning |
|-------------|-----------------------|---------|
| `static`    | Nested struct         | Declares a static nested struct (see [Nested Structures](nested.md)). |
| `final`     | Struct declaration    | Declares a final struct (cannot be used as a base class; see [§ 11](#11-final-structs)). |
| `const`     | Struct declaration    | Declares a const struct (see [§ 13](#13-const-structs)); all non-static member functions must be `const`. |
| `const`     | Non-static member function | Declares a const member function (see [§ 12](#12-const-member-functions)); `this` is `const`. |
| `public`    | Any member            | Per-element visibility: public (see [§ 10](#10-member-visibility)). |
| `protected` | Any member            | Per-element visibility: protected (see [§ 10](#10-member-visibility)). |
| `private`   | Any member            | Per-element visibility: private (see [§ 10](#10-member-visibility)). |

### Grammar

```
StructDecl:
    { Specifier } 'struct' Identifier '{' { StructMember } '}'

StructMember:
    VisibilityDecl
  | { Specifier } FieldDecl
  | { Specifier } MemberFunctionDecl
  | { Specifier } StructDecl

VisibilityDecl:
    ( 'public' | 'protected' | 'private' ) ':'

Specifier: (one of)
    'static'   'final'   'const'   'public'   'protected'   'private'
```

---

## 10. Member visibility

Every field and member function inside a struct has a *visibility* that determines which code may access it.

### Visibility levels

| Keyword     | Accessible from …                                                       |
|-------------|-------------------------------------------------------------------------|
| `public`    | Everywhere. This is the **default**.                                    |
| `protected` | Member functions of this struct and of its nested structs.              |
| `private`   | Member functions of this struct only.                                   |

> **Note:** For struct members, `protected` and `private` currently have the same access rules. The distinction will become meaningful when a friendship mechanism is introduced in a future language version.

### Ways to specify visibility

**Per-element specifier** — placed directly before a single declaration; takes precedence over any group setting:

```k
struct S {
private:
    x : int;          // private  (group)
    public y : int;   // public   (per-element override)
    z : int;          // private  (back to group)
}
```

**Group visibility specifier** — a visibility keyword followed by `:` sets the current default for all subsequent declarations until the next group specifier or end of body:

```k
struct BankAccount {
private:
    balance : int = 0;
    log(amount: int) { /* … */ }

public:
    BankAccount() : balance(0) {}
    deposit(n: int)  { balance = balance + n; }
    withdraw(n: int) { balance = balance - n; }
    get() : int      { return balance; }
}
```

Without any specifier the default visibility is **public**.

### Access from member functions

Private and protected members are accessible from any (non-static) member function of the same struct.
When calling another member function of the same struct from within a method, use explicit `this.`:

```k
struct Helper {
private:
    compute(p: int, q: int) : int { return p + q; }
public:
    sum(a: int, b: int) : int { return this.compute(a, b); }
}
```

### Access enforcement

Accessing a private or protected member from outside its struct is a compile-time error:

```
Error 40030 : private member variable 'balance' of struct 'BankAccount' is not accessible here;
              it can only be accessed from member functions of 'BankAccount'
Error 4002F : private member function 'log' of struct 'BankAccount' is not accessible here;
              it can only be called from member functions of 'BankAccount'
```

### Static constructors and visibility

Static constructors (`static StructName() { … }`) are insensitive to per-element visibility specifiers: they inherit the visibility of the struct itself.
If a struct is visible from another context, it may be declared as a static-initialization dependency regardless of the visibility of its individual members.

---

*See also:* [Constructors](constructors.md) · [Destructors](destructors.md) · [Nested Structures](nested.md) · [Namespace visibility](../basic/names.md#5-visibility-of-namespace-members) · [Functions](../functions/functions.md) · [Types](../basic/types.md)

---

## 11. Final structs

A struct declared with the `final` specifier **cannot be used as a base class**.  
Any attempt to inherit from a final struct is a compile-time error.

A final struct is otherwise a fully normal struct: it can have fields, member functions, constructors, a destructor, and it can itself inherit from other (non-final or final) structs.  
It can also be used freely as a **field type** (aggregation) in other structs, or as a function parameter type.

### Grammar

```
FinalStructDecl:
    'final' { OtherSpecifier } 'struct' Identifier [ ':' BaseClause ] '{' { StructMember } '}'
```

`final` may appear in any position among the specifiers, combined with visibility specifiers if needed.

### Examples

**Declaring a final struct:**

```k
final struct Coord {
    x : int;
    y : int;
    Coord(a: int, b: int) : x(a), y(b) {}
}
```

**A final struct can be inherited from another (non-final) struct:**

```k
struct Base { v : int; Base() : v(10) {} }

final struct Leaf : public Base {
    w : int;
    Leaf() : w(5) {}
}

test() : int {
    l : Leaf;
    return l.v + l.w;   // 10 + 5 = 15
}
```

**A final struct can be used as a member (aggregation):**

```k
struct Shape {
    pos : Coord;          // aggregation — OK
    Shape(a: int, b: int) : pos(a, b) {}
    get_x() : int { return pos.x; }
}
```

**Inheriting from a final struct is a compile-time error:**

```k
// ERROR: cannot inherit from 'Coord', it is declared final
struct Extended : public Coord { }
```

```
Error: Cannot inherit from 'Coord' in struct 'Extended': 'Coord' is declared final and cannot be used as a base class
```

### Rationale

Marking a struct `final` communicates the design intent that the type is a leaf in the type hierarchy.  
It also allows the compiler (in future optimisation passes) to devirtualise calls and make stronger layout assumptions.

---

## 12. Const member functions

A **const member function** is declared by placing the `const` specifier before the member function declaration.  
Its implicit `this` parameter is typed as `const Struct&` (a reference to a const instance) instead of the usual mutable `Struct&`.

### Semantics

Inside a const member function:
- All fields of the struct (and inherited fields) are **implicitly const** — they can be read but not assigned or mutated.
- Only other **const** member functions (direct or inherited) may be invoked on `this`.
- The function may still call non-member functions or static member functions freely.
- `++` / `--` on any field is forbidden.

### Grammar

```
ConstMemberFunctionDecl:
    'const' { OtherSpecifier } Identifier '(' [ ParameterList ] ')' [ ':' TypeSpec ] BlockStatement
```

`const` may appear in any position among the specifiers of a non-static member function.

> **Note:** `const` and `static` cannot be combined on a member function: a static function has no `this` parameter, so `const` on `this` is meaningless. This is a compile-time error.

### Examples

```k
struct Counter {
    value : int;

    Counter(v : int) : value(v) {}

    // Const member function: can read fields, cannot modify them
    const get() : int { return this.value; }

    // Mutable member function: can modify fields
    increment() { this.value = this.value + 1; }
}

test() : int {
    c : Counter(10);
    c.increment();         // OK: c is mutable
    return c.get();        // OK: calling const function on mutable object — always allowed
}

test_const() : int {
    const c : Counter(10);
    // c.increment();      // ERROR: cannot call mutable function on const object
    return c.get();        // OK: const function on const object
}
```

### Const member function on a const variable

When a struct variable is declared `const` (or accessed via a `const T&` reference/parameter), only const member functions may be called on it:

```k
use_counter(const c : Counter&) : int {
    // c.increment();  // ERROR: cannot call mutable member function on const Counter
    return c.get();    // OK
}
```

### Overloading with const

`const` is part of the function signature for mangling but overloading between a `const` and a mutable version of the same member function is not currently supported.

---

## 13. Const structs

A **const struct** is a struct declared with the `const` specifier.  
All non-static member functions of a const struct are implicitly const: a non-static member function that is not explicitly declared `const` inside a const struct is **automatically promoted to const** at compile time, and a **warning** is emitted to signal the implicit promotion.

### Semantics

- All fields of a const struct are effectively const in every context — even if not explicitly declared `const`.
- Constructors and destructors are **exempt** from the const requirement (they always have mutable `this`).
- Static member functions are **exempt** (they have no `this` parameter).
- A const struct instance (whether declared `const` or not) behaves as fully immutable after construction.
- A non-static member function that is **not** declared `const` inside a const struct is silently promoted to const. The compiler emits a `Warning 30010` to alert the developer. The promoted function is treated as const in all contexts: its generated prototype, its invocation, and inheritance.

### Grammar

```
ConstStructDecl:
    'const' { OtherSpecifier } 'struct' Identifier [ ':' BaseClause ] '{' { StructMember } '}'
```

### Warning on implicit promotion

```
Warning 30010 : member function 'foo' of const struct 'Bar' is not declared 'const';
                it is implicitly promoted to const
```

This warning helps catch accidental omissions. To silence it, add the `const` specifier explicitly.

### Inheritance rules

| Situation | Allowed? | Notes |
|-----------|----------|-------|
| `const` struct inherits from `const` struct | ✓ | All inherited methods remain const. |
| `const` struct inherits from mutable struct | ✗ | Compile-time error. |
| Mutable struct inherits from `const` struct | ✓ | Inherited const methods remain const; inherited fields become mutable in the derived context. |

### Examples

```k
const struct Immutable {
    x : int;
    Immutable(v : int) : x(v) {}

    const get() : int { return this.x; }   // OK: explicitly const — no warning

    // The function below is not declared const but is inside a const struct.
    // It is automatically promoted to const (Warning 30010 emitted).
    // To suppress the warning, add 'const' explicitly.
    also_get() : int { return this.x; }    // Warning 30010 → promoted to const
}

test() : int {
    i : Immutable(7);
    return i.also_get();   // OK: works as a const member function
}
```

```k
// A mutable struct may inherit from a const struct.
// Inherited const methods are kept const; the derived struct may add mutable methods.
struct Mutable : public Immutable {
    Mutable(v : int) : Immutable(v) {}
    set(v : int) { this.x = v; }           // OK: Mutable is not a const struct
}

test() : int {
    m : Mutable(5);
    m.set(42);
    return m.get();   // 42
}
```

### Const struct instance via const variable

If a mutable struct is instantiated as a `const` variable (or passed as `const Struct&`), all its fields are treated as const and only const member functions may be called:

```k
test_const_instance() : int {
    const p : Counter(10);
    // p.increment();   // ERROR: mutable method on const instance
    return p.get();     // OK
}
```

This applies regardless of whether the struct itself is declared `const`.

---

*See also:* [Constructors](constructors.md) · [Destructors](destructors.md) · [Nested Structures](nested.md) · [Inheritance](../structs/structs.md) · [Namespace visibility](../basic/names.md#5-visibility-of-namespace-members) · [Types — Const-ness](../basic/types.md#12-const-ness)
