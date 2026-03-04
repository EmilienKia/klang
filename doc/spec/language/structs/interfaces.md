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
- All methods in an interface are implicitly abstract — they **must not** have a body `{ … }`.
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
| Method bodies                   | ✓                    | ✓                        | ✗ (methods are abstract) |
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
| `0x30039`  | Symbol resolver| Class inherits unimplemented interface methods but is not `abstract`       |
| `0x40032`  | Type resolver  | Direct instantiation of an interface                                       |

### Warnings

| Code       | Phase          | Condition                                                                  |
|------------|----------------|----------------------------------------------------------------------------|
| `0x2002A`  | Model builder  | `abstract` specifier used on an `interface` declaration (redundant)        |
| `0x2002B`  | Model builder  | `abstract` specifier used on an interface method (redundant)               |

---

*See also:* [Classes and Virtuality](classes.md) · [Inheritance](inheritance.md) · [Structures](structs.md)

