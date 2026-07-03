# Interfaces

[← Classes and Virtuality](classes.md) · [← Index](../index.md)

An **interface** is a named contract declared with the `interface` keyword.  
It defines a set of methods that any implementing class must provide.

| Feature                              | `interface`                                      |
|--------------------------------------|--------------------------------------------------|
| Virtual dispatch (vtable)            | ✓ All methods (implicitly virtual)               |
| Can be instantiated directly         | ✗ Never (implicitly abstract)                    |
| Fields                               | ✗ Not allowed                                    |
| Constructors / destructors           | ✗ Not allowed                                    |
| Method bodies                        | ✗ Not allowed (methods are implicitly abstract)  |
| Default methods (`default`)          | ✓ Concrete virtual method with a body (see §5b)  |
| Inherits from                        | Other interfaces only                            |
| `abstract` specifier                 | ✓ Accepted but redundant (warning)               |
| Default member visibility            | `public`                                         |

---

## Contents

1. [Declaring an interface](#1-declaring-an-interface)
2. [Implementing an interface](#2-implementing-an-interface)
3. [Virtual dispatch through an interface reference](#3-virtual-dispatch-through-an-interface-reference)
4. [Interface extending another interface](#4-interface-extending-another-interface)
5. [Multiple interface implementation](#5-multiple-interface-implementation)
5b. [Default methods (`default`)](#5b-default-methods-default)
6. [Partial implementation — abstract class](#6-partial-implementation--abstract-class)
7. [Nested interfaces](#7-nested-interfaces)
8. [Implicit `abstract` and redundancy warnings](#8-implicit-abstract-and-redundancy-warnings)
9. [Interface vs class vs struct — comparison](#9-interface-vs-class-vs-struct--comparison)
10. [Error reference](#10-error-reference)

---

## 1. Declaring an interface

An interface is declared with the `interface` keyword, a name, and a body containing **method declarations** (no bodies).

### Grammar

```
InterfaceDecl:
    { Specifier } 'interface' Identifier [ ':' BaseClause ] '{' { Declaration } '}'
```

The same `Specifier` list as `struct`/`class` is accepted syntactically, but only `abstract` and `final` are meaningful.  
`abstract` is **redundant** (see §8) and `final` prevents further inheritance.

**Example:**

```k
module shapes;

interface Shape {
    area()      : int;
    perimeter() : int;
    sides()     : int;
}
```

**Rules:**
- All methods in an interface are implicitly abstract — they **must not** have a body `{ … }`, unless declared `default` (see §5b).
- All methods are implicitly public.  Explicit visibility sections are allowed, but `private` abstract methods are an error (§10).
- No fields, constructors, or destructors are allowed.

---

## 2. Implementing an interface

A **class** implements an interface by listing it in its inheritance clause and providing a concrete (non-abstract) body for every interface method.

```k
module shapes;

interface Shape {
    area()      : int;
    perimeter() : int;
    sides()     : int;
}

class Triangle : public Shape {
    Triangle() {}
    area()      : int { return 6;  }
    perimeter() : int { return 12; }
    sides()     : int { return 3;  }
}

test() : int {
    t: Triangle;
    return t.area();   // → 6
}
```

**Rules:**
- The implementing type **must** be a `class` (not a `struct`): interfaces use virtual dispatch, which only `class` supports.
- A class that does not implement all inherited interface methods must be declared `abstract`; otherwise it is a compile-time error (§10).
- A class that implements all interface methods becomes **concrete** and can be instantiated normally.

---

## 3. Virtual dispatch through an interface reference

Because all interface methods are virtual, calling a method through an `Interface&` reference always dispatches to the most-derived implementation at runtime.

```k
module disp;

interface Processor {
    process() : int;
}

class Fast : public Processor {
    Fast() {}
    process() : int { return 100; }
}

class Slow : public Processor {
    Slow() {}
    process() : int { return 1; }
}

run(p: Processor&) : int {
    return p.process();   // virtual dispatch
}

test_fast() : int { f: Fast; return run(f); }   // → 100
test_slow() : int { s: Slow; return run(s); }   // → 1
```

---

## 4. Interface extending another interface

An interface may inherit from one or more other interfaces.  
The result is still an interface: it is implicitly abstract and cannot be instantiated.

A class implementing the sub-interface must provide concrete implementations for **all** methods declared in the sub-interface **and** all its ancestor interfaces.

```k
module naming;

interface Identifiable {
    id() : int;
}

interface Named : public Identifiable {
    name_hash() : int;
}

class Entity : public Named {
    Entity() {}
    id()        : int { return 7;  }   // from Identifiable
    name_hash() : int { return 99; }   // from Named
}

call_id(x: Identifiable&) : int { return x.id(); }

test_id()   : int { e: Entity; return call_id(e); }   // → 7
test_name() : int { e: Entity; return e.name_hash(); } // → 99
```

---

## 5. Multiple interface implementation

A class may implement more than one interface by listing them all in its base clause.

```k
module rw;

interface Readable {
    read() : int;
}

interface Writable {
    write() : int;
}

class Buffer : public Readable, public Writable {
    Buffer() {}
    read()  : int { return 1; }
    write() : int { return 2; }
}

test_read()  : int { b: Buffer; return b.read();  }   // → 1
test_write() : int { b: Buffer; return b.write(); }   // → 2
```

> **Note — secondary base dispatch:**  
> Dispatching through a reference to a *secondary* interface base (the second or later interface in the base clause) requires per-base vtable thunks, which are not yet implemented in the current compiler.  
> Direct calls and dispatch through the **primary** (first) interface reference work correctly.

---

## 5b. Default methods (`default`)

A member function declared with the **`default`** prefix specifier and a body is
a *default method*: a **concrete, virtual** method (mangled and emitted like any
other method). A class that implements the interface but does **not** override
the method inherits the default implementation through its vtable slot — and
therefore does not need to be declared `abstract`.

```k
module greet;

interface Greeter {
    base() : int;
    default greeting() : int {          // default implementation
        return this.base() + 5;         // may call abstract or default methods
    }
}

class Hello : public Greeter {
    Hello() {}
    base() : int { return 37; }         // greeting() is inherited from Greeter
}

test() : int { h: Hello; return h.greeting(); }   // → 42
```

**Rules:**
- `default` is only valid on interface member functions, and a **body is required**.
- It is incompatible with `static`, `final`, `abstract`, `private`,
  constructors, destructors and `-> default/delete/redirect`.
- A default body may call other (abstract or default) methods of the interface;
  those calls dispatch dynamically to the most-derived implementation.
- A class **may** override a default method (with or without the `override`
  specifier); the override then wins.
- A sub-interface may provide (or replace) a default for a method declared in a
  parent interface.

### Template interfaces

For a `template<…>` interface, a default method is **not** synthesised at the
definition site. It is synthesised — with `linkonce_odr` linkage — for each
concrete instantiation, exactly like every other template member method. This
covers the case where the default method's prototype or body depends on a
template parameter:

```k
template<typename T>
interface Box {
    get() : T;
    default getOrTwice() : T { return this.get() + this.get(); }
}

class IntBox : public Box<int> {
    IntBox() {}
    get() : int { return 21; }
}

test() : int { b: IntBox; return b.getOrTwice(); }   // → 42
```

### Cross-module

For a non-template interface the default method symbol lives in the interface's
library. An implementing class in another module references it through the
imported vtable slot and links against that library automatically.

---

## 6. Partial implementation — abstract class

A class may implement *some* of the interface methods and leave the rest to its subclasses, as long as it is itself declared `abstract`.

```k
module vehicles;

interface Vehicle {
    speed()  : int;
    wheels() : int;
}

abstract class MotorVehicle : public Vehicle {
    MotorVehicle() {}
    wheels() : int { return 4; }   // provided
    // speed() not implemented — class stays abstract
}

class Car : public MotorVehicle {
    Car() {}
    speed() : int { return 120; }  // completes the contract
}

call_speed(v: Vehicle&)  : int { return v.speed();  }
call_wheels(v: Vehicle&) : int { return v.wheels(); }

test_speed()  : int { c: Car; return call_speed(c);  }  // → 120
test_wheels() : int { c: Car; return call_wheels(c); }  // → 4
```

---

## 7. Nested interfaces

An interface may be declared inside a class body, forming a *nested interface*.

```k
module containers;

class Outer {
    Outer() {}

    interface Inner {
        compute() : int;
    }
}
```

Nested interfaces follow the same rules as top-level interfaces.

> **Note — qualified names in base clauses:**  
> Using a nested interface as a base class via its qualified name (`Outer::Inner`) is not yet supported by the parser.

---

## 8. Implicit `abstract` and redundancy warnings

### Interface is always abstract

An interface is **implicitly and unconditionally abstract** — it can never be instantiated.  
Writing `abstract interface …` is accepted syntactically but **redundant**.

```k
abstract interface Marker {   // Warning 0x2002A: 'abstract' is implicit on interfaces
    check() : int;
}
```

### Interface methods are always abstract

Every method declared in an interface (without `final` or `static`) is **implicitly abstract** — no body allowed.  
Writing `abstract method() : …;` is also redundant.

```k
interface Printer {
    abstract print() : int;   // Warning 0x2002B: 'abstract' is implicit on interface methods
}
```

The code still compiles and the semantics are identical to the non-redundant form.

### `final` methods — not abstract

A method marked `final` inside an interface is **not** implicitly abstract (the `final` modifier suppresses the implicit abstraction).  
Such a method is a *non-virtual new final method*, and it still requires a body.  
Without a body it is an error (error `0x2002C`).

### `static` methods — not abstract

A method marked `static` inside an interface is also **not** implicitly abstract.  
Explicitly combining `abstract` and `static` is an error (`0x20024`).

---

## 9. Interface vs class vs struct — comparison

| Feature                         | `struct`             | `class`                  | `interface`              |
|---------------------------------|----------------------|--------------------------|--------------------------|
| Virtual dispatch                | ✗                    | ✓ Automatic              | ✓ Automatic              |
| Can be instantiated             | ✓                    | ✓ (if not abstract)      | ✗ Never                  |
| Fields                          | ✓                    | ✓                        | ✗                        |
| Constructors / destructors      | ✓                    | ✓                        | ✗                        |
| Method bodies                   | ✓                    | ✓                        | Only `default` methods   |
| `abstract` specifier            | ✗ Error              | ✓                        | Redundant (warning)      |
| May inherit from                | `struct` only        | `class` only             | `interface` only         |
| May be used as base by          | `struct`             | `class`                  | `class`, `interface`     |
| Default variable visibility     | `public`             | `protected`              | `public`                 |
| Default function visibility     | `public`             | `public`                 | `public`                 |

---

## 10. Error reference

### Compile-time errors

| Code       | Phase          | Condition                                                                  |
|------------|----------------|----------------------------------------------------------------------------|
| `0x20024`  | Model builder  | Interface method is `abstract` and `static`                                |
| `0x20025`  | Model builder  | Interface method is `abstract` and `final`                                 |
| `0x20026`  | Model builder  | Interface method has a body `{ … }` (implicit or explicit abstract)        |
| `0x20029`  | Model builder  | `abstract` interface method has `private` visibility                       |
| `0x2002C`  | Model builder  | Non-abstract interface method (`final` or `static`) has no body            |
| `0x0018B`  | Model builder  | `default` specifier used outside an interface                              |
| `0x0018C`  | Model builder  | `default` method has no body                                               |
| `0x0018D`  | Model builder  | `default` combined with `static`, `final`, `abstract` or an aliasing `->`  |
| `0x0018E`  | Model builder  | `default` method has `private` visibility                                  |
| `0x0018F`  | Model builder  | `default` used on a constructor or destructor                              |
| `0x30039`  | Symbol resolver| Class inherits unimplemented interface methods but is not `abstract`       |
| `0x40032`  | Type resolver  | Direct instantiation of an interface                                       |

### Warnings

| Code       | Phase          | Condition                                                                  |
|------------|----------------|----------------------------------------------------------------------------|
| `0x2002A`  | Model builder  | `abstract` specifier used on an `interface` declaration (redundant)        |
| `0x2002B`  | Model builder  | `abstract` specifier used on an interface method (redundant)               |

---

## 11. Interfaces and library export

When a module containing an interface is compiled into a library, the interface
is exported in full in the **KDI** description file.  Interface members are
always `public` (the only meaningful visibility), so every method signature and
its vtable slot index are exported.

Default methods (§5b) are exported as **concrete** (non-abstract) vtable slots
carrying the mangled symbol of their implementation. A consumer that inherits
the interface without overriding a default method references that symbol through
the imported vtable slot and links against the interface's library.

**A consumer may:**
- Implement the interface (`class MyClass : public mylib::IFoo { … }`).
- Hold references / pointers to the interface and dispatch virtual calls.
- Extend the interface (`interface MyIFoo : public mylib::IFoo { … }`).

**A consumer may not:**
- Instantiate an interface directly.
- Provide a body for an interface method without implementing the full interface.

See [Libraries — Export and Import](../basic/libraries.md#5-inheriting-from-imported-aggregates)
for a worked example of implementing an imported interface.

---

*See also:* [Classes and Virtuality](classes.md) · [Inheritance](inheritance.md) · [Structures](structs.md) · [Libraries — Export and Import](../basic/libraries.md) · [Types — §11.4 Dynamic downcast](../basic/types.md#114-dynamic-indirection-downcast-classinterface)

