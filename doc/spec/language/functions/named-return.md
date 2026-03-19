# Named Return Variables

[← Index](../index.md) · [Functions](functions.md) · [Return Statement](../statements/return.md)

A function may **name its return variable** in its declaration.  The named variable is
declared at function entry, is usable throughout the body, and is implicitly returned
when the function exits.

---

## Contents
1. [Syntax](#1-syntax)
2. [Semantics](#2-semantics)
3. [Initialisation](#3-initialisation)
4. [Return behaviour](#4-return-behaviour)
5. [NRVO guarantee](#5-nrvo-guarantee)
6. [Restrictions](#6-restrictions)
7. [Examples](#7-examples)

---

## 1. Syntax

### Grammar
```
FunctionDecl:
    { Specifier } Identifier '(' [ ParameterList ] ')'
    ReturnVarName ':' TypeSpec [ Initialiser ]
    BlockStatement

ReturnVarName:
    Identifier
```

The named return variable appears between the closing parenthesis `)` of the parameter
list and the colon `:` of the return type.  It follows the standard K variable declaration
syntax: `name : Type [ Initialiser ]`.

### Disambiguation
After `)`, if the next token is an identifier followed by `:`, it is a named return
variable.  If the next token is directly `:`, `{`, `->`, or `;`, the classic syntax is
used.

---

## 2. Semantics

The named return variable is semantically equivalent to a local variable declared as the
first statement of the function body.  It can be read, written, passed by reference, and
have member functions called on it — just like any other local.

The named return variable is **not destroyed** during scope cleanup at function exit.  It
lives in the caller's storage and its lifetime is managed by the caller.

---

## 3. Initialisation

The initialiser follows the same rules as any variable declaration:

| Syntax | Meaning |
|--------|---------|
| `r : int = 42` | Assignment-style initialisation |
| `r : Obj(42)` | Constructor-style initialisation |
| `r : Obj` | Default construction (no initialiser) |

---

## 4. Return behaviour

| Statement | Behaviour with named return |
|-----------|---------------------------|
| *end of `}`* | Implicitly returns the named variable |
| `return;` | Returns the named variable (early exit) |
| `return expr;` | **Warning 0x6001**: assigns `expr` to the named variable, then returns |

A function with a named return variable does not require an explicit `return` statement.
Reaching the closing `}` of the function body is sufficient.

---

## 5. NRVO guarantee

When the return type is an aggregate (struct, class) returned by value, the named return
variable benefits from **guaranteed NRVO** (Named Return Value Optimisation).  This means:

- The named variable is constructed directly into the caller's destination memory.
- No copy or move occurs.
- Exactly **1 constructor call** and **1 destructor call** are emitted for the object.

This is stronger than heuristic NRVO (which analyses `return` statements and may fail when
multiple return paths exist).

---

## 6. Restrictions

| Restriction | Reason |
|-------------|--------|
| Not allowed on constructors | Constructors build `this`, not a return value |
| Not allowed on destructors | Destructors do not return values |
| Not allowed on `abstract` functions | Abstract functions have no body |
| Not allowed on `void` functions | No return type to name |
| Not allowed with `-> default` / `-> delete` / `-> target` | Aliased functions have no body |
| Cannot be `const` | The variable must be mutable in the body |

---

## 7. Examples

### Primitive return
```k
increment(n : int) r : int = n {
    r = r + 1;
}
// increment(41) → 42
```

### Struct with guaranteed NRVO
```k
struct Point {
    x : int;
    y : int;
    Point(a : int, b : int) : x(a), y(b) {}
    ~Point() { /* ... */ }
}

make(v : int) p : Point(v, v * 2) {
    p.x = p.x + 1;
}
// make(10) → Point{11, 20}, exactly 1 ctor + 1 dtor
```

### Conditional early return
```k
clamp(val : int, lo : int, hi : int) r : int = val {
    if (r < lo) { r = lo; return; }
    if (r > hi) { r = hi; return; }
}
// clamp(5, 0, 10) → 5
// clamp(-1, 0, 10) → 0
// clamp(99, 0, 10) → 10
```

### Member function
```k
struct Adder {
    base : int;
    Adder(b : int) : base(b) {}
    add(x : int) r : int = base {
        r = r + x;
    }
}
```

### Operator overload
```k
struct Vec {
    x : int;
    y : int;
    Vec(a : int, b : int) : x(a), y(b) {}
    operator +(other : Vec) r : Vec(x + other.x, y + other.y) { }
}
```

---

*See also:* [Functions](functions.md) · [Return Statement](../statements/return.md) · [Destructors](../structs/destructors.md)

