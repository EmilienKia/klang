# Classes and Virtuality

[← Structs](structs.md) · [← Index](../index.md)

A **class** is declared with the `class` keyword instead of `struct`.  
The most important distinction between `struct` and `class` is *virtuality*:

| Feature                               | `struct`                | `class`                          |
|---------------------------------------|-------------------------|----------------------------------|
| Virtual dispatch (vtable)             | ✗ None                  | ✓ Automatic for all methods      |
| Virtual base classes                  | ✗ Forbidden (error)     | ✓ Allowed (`virtual` in base clause) |
| Cross-inheritance (struct ↔ class)    | ✗ Error                 | ✗ Error                          |
| Default variable visibility           | `public`                | `protected`                      |
| Default function visibility           | `public`                | `public`                         |
| `abstract` specifier                  | ✗ Forbidden (error)     | ✓ On class and eligible methods  |

---

## Contents

1. [The `class` keyword](#1-the-class-keyword)
2. [Automatic virtual dispatch](#2-automatic-virtual-dispatch)
3. [Virtual method rules](#3-virtual-method-rules)
4. [The `final` specifier on methods](#4-the-final-specifier-on-methods)
5. [Private methods — not virtual](#5-private-methods--not-virtual)
6. [Static methods — not virtual](#6-static-methods--not-virtual)
7. [Constructors and destructors — not virtual](#7-constructors-and-destructors--not-virtual)
8. [Class vs struct — the virtuality contract](#8-class-vs-struct--the-virtuality-contract)
9. [Virtual base classes and diamond inheritance](#9-virtual-base-classes-and-diamond-inheritance)
10. [Default visibility in classes](#10-default-visibility-in-classes)
11. [Non-virtual qualified calls](#11-non-virtual-qualified-calls)
12. [Abstract classes and methods](#12-abstract-classes-and-methods)
13. [Const member functions in classes](#13-const-member-functions-in-classes)
14. [RTTI and dynamic downcast](#14-rtti-and-dynamic-downcast)

---

## 1. The `class` keyword

A class is declared exactly like a struct except that the `class` keyword is used:

```k
class Animal {
    sound() : int { return 0; }
}
```

All syntax valid for `struct` is valid for `class` as well (fields, member functions, constructors, destructors, specifiers, visibility, inheritance, etc.), with the virtuality rules described below.

---

## 2. Automatic virtual dispatch

**Every non-static, non-private member function of a class is automatically virtual.**  
You do not need to write `virtual` — it is the default for classes.

```k
class Shape {
    area() : int { return 0; }
    name() : int { return 1; }   // also automatically virtual
}

class Circle : public Shape {
    area() : int { return 314; }
    name() : int { return 2; }
}

get_area(s: Shape&) : int {
    return s.area();             // virtual dispatch
}

test_circle() : int {
    c: Circle;
    return get_area(c);          // → Circle::area → 314
}
```

The vtable is built automatically by the compiler.  
Derived classes **override** inherited virtual methods by declaring a member function with the same name and parameter types.

### Only `class` objects dispatch virtually

`struct` objects have **no vtable**. Calling a member function through a `Base&` always calls `Base`'s own implementation, regardless of the dynamic type of the object.

```k
struct Base { value() : int { return 1; } }
struct Derived : public Base { value() : int { return 2; } }

call_value(b: Base&) : int { return b.value(); }

test() : int {
    d: Derived;
    return call_value(d);   // → 1 (NO dispatch — struct has no vtable)
}
```

---

## 3. Virtual method rules

For a method in a **class**:

| Method kind                                    | Virtual? | Vtable slot |
|------------------------------------------------|----------|-------------|
| `public` non-static member function            | ✓        | Own slot or inherited slot |
| `protected` non-static member function         | ✓        | Own slot or inherited slot |
| `private` non-static member function           | ✗        | None        |
| `static` member function                       | ✗        | None        |
| Constructor                                    | ✗        | None        |
| Destructor                                     | ✗        | None (but see note) |
| New `final` member function                    | ✗        | None        |
| `final` override of an inherited virtual       | ✓        | Takes the inherited slot; seals it |

> **Note on destructors:** Destructors are not dispatched virtually themselves, but the compiler-generated destructor call sequence correctly chains base-class destructors. Full virtual destructor dispatch may be added in a future version.

---

## 4. The `final` specifier on methods

The `final` specifier can be placed before a member function name inside a class.

### New `final` function (not an override)

If the function does not match any inherited virtual function, `final` means the function is **NOT placed in the vtable at all** — it has no vtable slot and cannot be dispatched virtually.  
The function is still callable directly (non-virtually) by name.

```k
class Base {
    final compute() : int { return 10; }
}

class Derived : public Base {
    extra() : int { return 20; }
    // Cannot override Base::compute — it is final-non-virtual.
}

test() : int {
    b: Base;
    d: Derived;
    return b.compute() + d.compute();  // 10 + 10 — direct calls, no dispatch
}
```

### Overriding `final` function (seals the slot)

If the function **overrides** an inherited virtual slot and is marked `final`, the override is applied to the vtable slot (dispatch still works through the inherited slot) but the slot is sealed: **no further subclass can override it**.  

Attempting to override a sealed final generates a **warning** (not an error) in the current K implementation; the override is applied with a warning.

```k
class A {
    val() : int { return 1; }
}
class B : public A {
    final val() : int { return 2; }   // overrides A::val, seals the slot
}
class C : public B {
    val() : int { return 3; }         // Warning: overriding a 'final' virtual
}

call_via_a(a: A&) : int { return a.val(); }

test() : int {
    a: A;  b: B;  c: C;
    call_via_a(a);  // → 1
    call_via_a(b);  // → 2
    call_via_a(c);  // → 3  (override proceeded with warning)
    return 0;
}
```

---

## 5. Private methods — not virtual

**Private methods are never virtual.** They do not get a vtable slot.

A private method in a derived class with the same name and signature as a (virtual) public/protected method in the base class is **an error**: a private function cannot silently override a virtual function.

```k
class Base {
    foo() : int { return 1; }    // virtual (public by default)
}
class Derived : public Base {
    private foo() : int { return 2; }  // ERROR: private cannot override virtual
}
```

Private methods are entirely internal to the class; they are called only from within the same class body (they bypass virtual dispatch):

```k
class Base {
    private helper() : int { return 1; }
    public run() : int { return this.helper(); }  // non-virtual call to helper
}
class Derived : public Base {
    private helper() : int { return 2; }   // new private, not an override
}
// run() always calls Base::helper (no dispatch), even on Derived objects.
```

---

## 6. Static methods — not virtual

Static methods belong to the **class type**, not to any instance.  
They have no `this` parameter and are never virtual.

```k
class Counter {
    static count: int;
    static reset()     { count = 0; }
    static increment() { count = count + 1; }
    static get() : int { return count; }
}

test() : int {
    Counter::reset();
    Counter::increment();
    Counter::increment();
    return Counter::get();  // 2
}
```

---

## 7. Constructors and destructors — not virtual

Constructors and destructors are not dispatched through the vtable.  
They are called directly by the compiler as part of object construction and destruction sequences.

```k
class Base {
    Base() { /* not virtual */ }
    ~Base() { /* not virtual */ }
}
```

---

## 8. Class vs struct — the virtuality contract

The golden rule:

> **`struct` = pure aggregation (no virtuality)**  
> **`class` = full virtuality (automatic virtual dispatch)**

### Enforcement rules

| Rule                                                   | Error code |
|--------------------------------------------------------|------------|
| `struct` uses `virtual` in base clause                 | `0x30034`  |
| `class` inherits from `struct` (or vice-versa)         | `0x30035`  |
| `private` function in class overrides a virtual method | `0x30037`  |

### Examples of errors

```k
struct A { A() {} }
struct B : virtual public A { B() {} }
// Error 30034: struct 'B' cannot use 'virtual' in base clause for 'A':
//              virtual inheritance is only allowed in class declarations
```

```k
struct S { S() {} }
class C : public S { C() {} }
// Error 30035: class 'C' cannot inherit from struct 'S':
//              cross-inheritance between class and struct is not allowed
```

```k
struct S { S() {} }
class C { C() {} }
struct D : public C { D() {} }
// Error 30035: struct 'D' cannot inherit from class 'C':
//              cross-inheritance between class and struct is not allowed
```

---

## 9. Virtual base classes and diamond inheritance

In a **class** hierarchy, a base class can be declared **virtual** in the base clause using the `virtual` keyword.  
A virtual base is shared: in a diamond hierarchy, only **one** copy of the virtual base sub-object exists in the most-derived class.

> Virtual bases are only supported by **classes**, not structs.

### Grammar

```
ClassDecl:
    { Specifier } 'class' Identifier [ ':' BaseClause ] '{' { ClassMember } '}'

BaseClause:
    BaseSpec { ',' BaseSpec }

BaseSpec:
    [ 'virtual' ] [ Visibility ] Identifier
  | [ Visibility ] [ 'virtual' ] Identifier

Visibility: (one of)
    'public'  'protected'  'private'
```

### Single virtual base

```k
class A {
    public x: int;
    A() : x(10) {}
}
class B : virtual public A {
    public y: int;
    B() : y(20) {}
}

test() : int {
    b: B;
    return b.x + b.y;   // 30
}
```

`B` holds a pointer (`__vbptr_A__`) to its shared copy of `A`.  
When `B` is the most-derived class, it also holds `A`'s sub-object directly (`__vbase_A__`).

### Diamond pattern

The classic use of virtual inheritance is to avoid the diamond ambiguity:

```k
class A {
    public x: int;
    A() : x(0) {}
}
class B : virtual public A { B() {} }
class C : virtual public A { C() {} }
class D : public B, public C { D() {} }

test_shared() : int {
    d: D;
    d.x = 42;
    // B and C both point to the same A — only one x in D
    return d.x;   // 42
}
```

Without `virtual`, B and C each embed their own copy of A, giving D **two** independent `x` fields (non-virtual diamond).

### Virtual base constructor

The **most-derived class** (the one being constructed, not B or C) is responsible for initialising the virtual base.  
Specify its constructor explicitly in the most-derived class's constructor initialiser list:

```k
class A {
    x: int;
    A() : x(-1) {}
    A(v: int) : x(v) {}
}
class B : virtual public A { B() {} }
class C : virtual public A { C() {} }
class D : public B, public C {
    D() : A(99) {}   // D constructs A; B and C do NOT re-construct A
}

test() : int {
    d: D;
    return d.x;   // 99
}
```

### Virtual base destructor

The virtual base destructor is called **exactly once**, by the most-derived class.  
Intermediate classes (B, C) do not call A's destructor — the compiler handles this automatically.

---

## 10. Default visibility in classes

Classes have slightly different **default** visibility than structs:

| Entity               | `struct` default | `class` default |
|----------------------|------------------|-----------------|
| Member variable      | `public`         | `protected`     |
| Member function      | `public`         | `public`        |

Use explicit visibility specifiers (`public:`, `protected:`, `private:`) or per-element specifiers (`public x: int`) to override the defaults.

```k
class Example {
    x: int;          // protected by default — not accessible from free functions
    public y: int;   // explicitly public — accessible from everywhere

    get_x() : int { return x; }   // public by default, auto-virtual
}

test() : int {
    e: Example;
    // e.x   // ERROR: x is protected
    e.y = 5;        // OK: y is public
    return e.get_x(); // OK: get_x is public
}
```

---

## 11. Non-virtual qualified calls

A qualified call bypasses virtual dispatch and **always** calls the specified class's implementation, regardless of the dynamic type of the object.

### 11.1 From outside a class — explicit object argument

```k
class Base  { value() : int { return 10; } }
class Derived : public Base { value() : int { return 20; } }

test() : int {
    d: Derived;
    d.value();              // virtual dispatch → 20
    Base::value(d);         // non-virtual, qualified → always 10
    return 0;
}
```

### 11.2 From inside an override — calling the parent implementation

Inside a method body, three equivalent syntaxes are available to call the parent class's version of the current (or any ancestor) method.

#### Form 1 — `Base::method(this)` (free-function style, explicit this)

```k
class Derived : public Base {
    value() : int {
        return Base::value(this) + 1;   // calls Base::value non-virtually
    }
}
```

#### Form 2 — `this.Base::method()` (dot-qualified on this)

```k
class Derived : public Base {
    value() : int {
        return this.Base::value() + 1;  // equivalent to Form 1
    }
}
```

#### Form 3 — `Base::method()` (implicit this injection)

When called from inside a member function with no explicit object, `this` is automatically injected:

```k
class Derived : public Base {
    value() : int {
        return Base::value() + 1;       // equivalent to Forms 1 and 2
    }
}
```

All three forms produce identical code: a direct (non-virtual) call to `Base::value`, bypassing the vtable.

### 11.3 Multi-level chains

Qualifications can chain across multiple levels of inheritance:

```k
class A { value() : int { return 1;   } }
class B : public A { value() : int { return A::value() + 10;  } }   // 11
class C : public B { value() : int { return B::value() + 100; } }   // 111

test() : int {
    c: C;
    return c.value();   // → C::value → B::value + 100 → (A::value + 10) + 100 = 111
}
```

### 11.4 With arguments

All three forms work with methods that take parameters:

```k
class Calc {
    add(a: int, b: int) : int { return a + b; }
}
class ExtCalc : public Calc {
    add(a: int, b: int) : int {
        return Calc::add(a, b) * 2;   // 3+4=7, *2 = 14
    }
}
```

### 11.5 Grammar

The dot-qualified form follows the normal member-access grammar, with a qualified identifier after the `.`:

```
QualifiedMemberCall:
    Expr '.' QualifiedIdentifier '(' [ ExpressionList ] ')'
```

`QualifiedIdentifier` contains `::` to name a specific base class:

```
this.Base::method(args)
obj.Base::method(args)
```

---

*See also:* [Structs](structs.md) · [Interfaces](interfaces.md) · [Inheritance](inheritance.md) · [Constructors](constructors.md) · [Destructors](destructors.md) · [Nested Structures](nested.md)

---

## 12. Abstract classes and methods

The `abstract` specifier can be placed on a **class** declaration, on an **eligible method** declaration, or on both.

### 12.1 Abstract methods

An **abstract method** is a virtual method that declares an interface contract but provides no implementation.  
It uses the `abstract` specifier and ends with a bare `;` — no body `{ … }`.

```k
abstract class Shape {
    Shape() {}
    abstract area() : int;          // no body — must be overridden
    abstract perimeter() : int;     // same
}
```

**Rules for abstract methods:**

| Constraint | Effect on violation |
|---|---|
| Must NOT have a body `{ … }` | Error `0x20026` |
| Must NOT be `static` | Error `0x20024` |
| Must NOT be `final` | Error `0x20025` |
| Must NOT be `private` | Error `0x20029` |
| Only inside a `class`, not a `struct` | Error `0x20027` |
| Owning class must also be `abstract` | Error `0x30038` |

**Semantics:**
- The abstract method is assigned a vtable slot like any other virtual method.
- No LLVM declaration or definition is emitted for an abstract method.
- Calling through a concrete derived class dispatches correctly via the vtable.

### 12.2 Abstract classes

An **abstract class** cannot be directly instantiated.

A class **must** be declared `abstract` if:
- It declares at least one `abstract` method directly, OR
- It inherits at least one abstract method that is not overridden with a concrete implementation.

A class **may** also be declared `abstract` explicitly, even with no abstract methods, to prevent direct instantiation for design reasons (e.g. a singleton base, a registry base, etc.).

```k
abstract class Logger {          // explicitly abstract, no abstract methods
    Logger() {}
    log(n: int) : int { return n; }
}
```

Attempting to instantiate an abstract class is a compile-time error:

```k
test() : int {
    s: Shape;       // error 0x40032: cannot instantiate abstract class 'Shape'
    return 0;
}
```

### 12.3 Deriving from an abstract class

A derived class becomes **concrete** (instantiable) once it provides concrete (non-abstract) implementations for **all** abstract methods, directly or transitively.

```k
abstract class Shape {
    Shape() {}
    abstract area() : int;
}

class Circle : public Shape {
    Circle() {}
    area() : int { return 314; }   // implements the abstract method
}

test() : int {
    c: Circle;             // OK — Circle is concrete
    s: Shape&;             // OK — reference (no instantiation)
    // call virtual dispatch
    return c.area();       // → 314
}
```

A derived class that does **not** implement all inherited abstract methods must itself be declared `abstract`:

```k
abstract class Mid : public Shape {    // still abstract; area() not implemented
    Mid() {}
}

class Leaf : public Mid {
    Leaf() {}
    area() : int { return 1; }        // fully concrete now
}
```

If a derived class omits the `abstract` specifier but still has unimplemented abstract methods, it is a compile-time error (`0x30039`).

### 12.4 Virtual dispatch through an abstract base reference

Virtual dispatch through a base class reference works correctly even when the static type is abstract:

```k
call_area(s: Shape&) : int {
    return s.area();           // dispatches through vtable at runtime
}

test() : int {
    c: Circle;
    return call_area(c);       // → 314
}
```

---

*See also:* [Structs](structs.md) · [Interfaces](interfaces.md) · [Inheritance](inheritance.md) · [Constructors](constructors.md) · [Destructors](destructors.md) · [Nested Structures](nested.md)

---

## 13. Const member functions in classes

All rules described for const member functions on structs (see [Structures — §12](structs.md#12-const-member-functions)) apply equally to classes.  
This section documents the additional interactions that are specific to classes: **virtual dispatch**, **const/mutable overloading**, and **inheritance**.

---

### 13.1 Basic rules (same as structs)

A class method declared with the `const` specifier receives a `const ClassName&` implicit `this`.  
Inside a const method:

- All fields (direct and inherited) are **read-only**.
- Only other **const** member functions may be called on `this`.
- `++` / `--` on any field is a compile-time error.
- A const method **may** be called on both mutable and const objects.
- A mutable method may **only** be called on mutable objects.

```k
class Box {
    public size : int;
    Box(s : int) : size(s) {}
    const area() : int { return this.size * this.size; }  // OK: reads only
}

test() : int {
    b : Box(5);
    return b.area();   // OK — mutable object, calling const method → 25
}

read_box(b : const Box&) : int {
    return b.area();   // OK — const reference, const method
    // b.size = 1;     // ERROR: field of const object is read-only
}
```

---

### 13.2 Const/mutable overloading

A class may provide **two overloads** of the same method: one `const` and one mutable.  
The compiler selects between them based on the constness of the receiver:

- On a **mutable** object or reference, the **mutable** overload is preferred.
- On a **const** object or reference, only the **const** overload is viable.

```k
class C {
    public x : int;
    C() : x(5) {}
    get() : int       { return this.x; }          // mutable overload
    const get() : int { return this.x + 100; }    // const overload
}

test_mutable() : int {
    c : C;
    return c.get();       // mutable object → mutable overload → 5
}

test_const() : int {
    const c : C;
    return c.get();       // const object   → const overload  → 105
}
```

This is the standard pattern for paired accessors (e.g. `operator[]`, `begin()`/`end()`).

---

### 13.3 Const methods and virtual dispatch

In a class, **const and mutable methods with the same name occupy distinct vtable slots** — their `this`-constness is part of their signature.  
A mutable method in a derived class does **not** override a const method in the base class.

#### Const override

```k
class Base {
    public x : int;
    Base() : x(1) {}
    const get() : int { return this.x; }       // vtable slot A (const)
}

class Derived : public Base {
    Derived() {}
    const get() : int { return this.x + 10; }  // overrides slot A
}

call_const(b : const Base&) : int { return b.get(); }

test() : int {
    d : Derived;
    return call_const(d);   // virtual dispatch → Derived::get → 11
}
```

#### Mutable method does NOT override a const base method

```k
class Base {
    public x : int;
    Base() : x(1) {}
    const get() : int { return this.x; }   // slot A (const)
}

class Derived : public Base {
    Derived() {}
    get() : int { return this.x + 10; }    // slot B (mutable) — NOT an override of slot A
}

call_const(b : const Base&) : int { return b.get(); }  // uses slot A

test_const_path() : int {
    d : Derived;
    return call_const(d);   // slot A → Base::get (not overridden) → 1
}

test_mutable_path() : int {
    d : Derived;
    return d.get();         // slot B → Derived::get → 11
}
```

---

### 13.4 Const methods and inheritance

A const method in a derived class may call any inherited const method on `this`.  
It may **not** call a mutable method (base or own) on `this`.

```k
class B {
    public x : int;
    B() : x(3) {}
    const get() : int { return this.x; }    // const
    mut() { this.x = 1; }                  // mutable
}

class D : public B {
    D() {}

    const get2() : int {
        return this.get() * 2;   // OK: calling inherited const method → 3 * 2 = 6
        // this.mut();           // ERROR: mutable method called from const context
    }
}

test() : int {
    d : D;
    return d.get2();   // 6
}
```

---

### 13.5 Error codes

| Code     | Phase         | Condition |
|----------|---------------|-----------|
| `0x40034`| Type resolver | Mutable member function called on a `const` object or `const T&` parameter |
| `0x40080`| Type resolver | Assignment to a field inside a const member function |
| `0x20009`| Model builder | `const` and `static` combined on a member function |

---

*See also:* [Structs](structs.md) · [Interfaces](interfaces.md) · [Inheritance](inheritance.md) · [Constructors](constructors.md) · [Destructors](destructors.md) · [Nested Structures](nested.md) · [Types — Const-ness](../basic/types.md#12-const-ness)

---

## 14. RTTI and dynamic downcast

### 14.1 Runtime Type Information (RTTI)

Every **concrete** (non-abstract) `class` and every `class` or `interface` that has virtual
functions automatically receives an **RTTI descriptor** at compile time.

The RTTI descriptor is a global constant struct with the layout:

```
{ ptr self_rtti_ptr, ptr mangled_name_cstr, ptr null_introspection }
```

| Field | Content |
|-------|---------|
| `self_rtti_ptr` | Pointer to this RTTI global itself (used as the unique *typeid*) |
| `mangled_name_cstr` | Null-terminated C string of the mangled class name |
| `null_introspection` | Reserved — currently null; reserved for future introspection data |

The RTTI global is stored at **vtable slot 0** (before the first virtual function pointer).
Every vtable — primary and secondary — carries the RTTI of the **concrete** (most-derived) class
of the complete object, so loading vtable[0] from any base sub-object of a `Derived` object
always yields `Derived`'s RTTI.

`struct` types have **no vtable and no RTTI**.

### 14.2 Dynamic downcast semantics

A **dynamic downcast** assigns a `Base*` (or `Base~`, `Base^`, `Base&`) to a `Derived*`
(or `Derived~`, `Derived^`, `Derived&`) with a runtime type check:

1. Load the vptr from field 0 of the pointed-at object (via the Base sub-object layout).
2. Load vtable[0] — the actual RTTI pointer of the concrete object.
3. Compare with Derived's RTTI global address.
4. **Match** → subtract the byte offset of the Base sub-object inside Derived from the source
   pointer to obtain the start of Derived; assign the adjusted pointer.
5. **Mismatch** → assign **null**.
6. If null is assigned to a non-null target (`~` link or `&` reference) → call
   `__fatal_null_dyncast()`.

The operation is emitted implicitly whenever the compiler detects that a `Base` indirection is
being assigned to a `Derived` indirection and `Derived` is a class/interface derived from `Base`.

### 14.3 Binding rules

| Target type | Allowed when | On RTTI mismatch |
|-------------|--------------|-----------------|
| `Derived&`  | Init only (immutable binding) | `__fatal_null_dyncast()` |
| `Derived~`  | Init only (non-null link) | `__fatal_null_dyncast()` |
| `Derived^`  | Init only (nullable pin) | null assigned |
| `Derived*`  | Init and rebind (nullable ptr) | null assigned |

### 14.4 Applicability

| Type | Dynamic downcast? |
|------|-------------------|
| `class` | ✓ Yes |
| `interface` | ✓ Yes |
| `struct` | ✗ No — compile-time error |
| Primitives | ✗ No |

### 14.5 Examples

```k
class Base {
    public val : int;
    public Base(v : int) : val(v) {}
    public dummy() : int { return 0; }
}
class Derived : public Base {
    public extra : int;
    public Derived(v : int) : Base(v), extra(99) {}
    public get_extra() : int { return extra; }
}

get_extra_fn(d : Derived&) : int { return d.get_extra(); }

test_ptr() : int {
    d  : Derived(42);
    bp : Base*    = &d;       // static upcast Derived→Base
    dp : Derived* = bp;       // dynamic downcast — dp non-null (RTTI matches)
    return get_extra_fn(*dp); // → 99
}

test_lnk() : int {
    d   : Derived(7);
    bl  : Base~   = &d;
    dl  : Derived~ = bl;      // dynamic downcast — dl non-null; fatal if RTTI mismatches
    return get_extra_fn(*dl); // → 99
}

test_ref() : int {
    d  : Derived(3);
    br : Base&    = d;
    dr : Derived& = br;       // dynamic downcast — fatal if RTTI mismatches
    return get_extra_fn(dr);  // → 99
}
```

**Transitive hierarchy:**

```k
class A { public x : int; public A(v:int):x(v){} public dummy():int{return 0;} }
class B : public A { public B(v:int):A(v){} }
class C : public B { public z : int; public C(v:int):B(v),z(99){} public get_z():int{return z;} }

get_z_fn(c : C&) : int { return c.get_z(); }

test() : int {
    c  : C(5);
    ap : A*  = &c;   // static upcast through B→A
    cp : C*  = ap;   // dynamic downcast A→C via RTTI
    return get_z_fn(*cp); // → 99
}
```

**Interface downcast:**

```k
interface IBase { get_val() : int; }
class Derived : public IBase {
    public val : int;
    public Derived(v : int) : val(v) {}
    public get_val() : int { return val; }
    public get_extra() : int { return val * 2; }
}

get_extra_fn(d : Derived&) : int { return d.get_extra(); }

test() : int {
    d  : Derived(21);
    ip : IBase*   = &d;      // static upcast to interface
    dp : Derived* = ip;      // dynamic downcast via RTTI → non-null
    return get_extra_fn(*dp); // → 42
}
```

### 14.6 Error reference

| Code | Condition |
|------|-----------|
| `0x4700` | Source and target have no inheritance relationship — compile-time error |
| *(runtime)* | RTTI mismatch on non-null target → `__fatal_null_dyncast()` |

*See also:* [Types — §11.4](../basic/types.md#114-dynamic-indirection-downcast-classinterface) · [Inheritance — Dynamic downcast](inheritance.md#dynamic-indirection-downcast-classinterface-only)

---

## 15. Classes and library export

When a class is compiled into a library, its visibility rules determine what
is exported in the **KDI** description file:

| Member visibility | Exported to `.kdi` | Accessible by consumers |
|---|---|---|
| `public` | ✓ Full layout + mangled name | ✓ Yes |
| `protected` | ✓ Full layout + mangled name | Sub-classes only |
| `private` | Opaque size-block only | ✗ No |

The vtable layout, base-class offsets, and RTTI information are always
exported (they are required to inherit from or dispatch through the class).

**A consumer class may:**
- Inherit from the exported class (virtual dispatch is preserved).
- Instantiate it (if constructors are public).
- Call public virtual and non-virtual methods.
- Override virtual methods.

**A consumer class may not:**
- Access private members by name (they are hidden as opaque blocks).
- Rely on the private block layout across library versions.

See [Libraries — Export and Import](../basic/libraries.md) for worked
examples, including cross-library inheritance and interface implementation.
