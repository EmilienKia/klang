# Nested Structures

[← Index](../index.md) · [Structures](structs.md)

A *nested struct* is a struct declared inside another struct's body.
K supports two kinds of nested structs: *static nested structs* and *non-static inner structs*.

---

## Contents
1. [Static nested structs](#1-static-nested-structs)
2. [Non-static inner structs](#2-non-static-inner-structs)
3. [Implicit parent reference `__parent__`](#3-implicit-parent-reference-__parent__)
4. [Constructing inner structs](#4-constructing-inner-structs)
5. [Access to outer struct fields from an inner struct](#5-access-to-outer-struct-fields-from-an-inner-struct)
6. [Name shadowing](#6-name-shadowing)
7. [Multi-level nesting](#7-multi-level-nesting)
8. [Examples](#8-examples)
---
## 1. Static nested structs
A *static nested struct* is declared with the `static` keyword.  
It behaves exactly like a top-level struct: there is no implicit parent reference.
### Grammar
```
StaticNestedStructDecl:
    'static' 'struct' Identifier '{' { Declaration } '}'
```
**Example:**
```k
struct Outer {
    value : int = 10;
    static struct Inner {
        x : int = 0;
        Inner(v: int) : x(v) {}
        get() : int { return x; }
    }
}
test() : int {
    // Static nested struct can be instantiated directly:
    i : Outer::Inner(42);
    return i.get();   // 42
}
```
**The type name** of a static nested struct is `OuterStruct::InnerStruct`.
---
## 2. Non-static inner structs
A *non-static inner struct* (simply written `struct` without `static`) carries an implicit reference to an instance of the enclosing struct.  
This reference is stored in a hidden field named `__parent__` (implementation detail).
### Grammar
```
InnerStructDecl:
    'struct' Identifier '{' { Declaration } '}'
```
An inner struct can only be instantiated in the context of an enclosing struct instance (either directly inside a member function of the outer struct, or by passing an outer reference explicitly when constructing from outside).
**Example:**
```k
struct Outer {
    outer_val : int = 100;
    struct Inner {
        inner_val : int = 0;
        Inner(v: int) : inner_val(v) {}
        get() : int { return inner_val; }
    }
    make_and_get(v: int) : int {
        i : Inner(v);      // implicit parent = this
        return i.get();
    }
}
```
---
## 3. Implicit parent reference `__parent__`
For a non-static inner struct, the compiler synthesizes a hidden `__parent__` field that is a pointer to the enclosing struct instance.
- When constructing an inner struct **inside a method of the outer struct**, `__parent__` is automatically set to `this`.
- When constructing an inner struct **outside** the outer struct (if needed), an explicit outer instance reference must be provided.
This field is an implementation detail; it is not directly accessible in user code. The compiler uses it to resolve outer field references automatically.
---
## 4. Constructing inner structs
### From within an outer struct method
```k
struct Outer {
    struct Inner {
        Inner(v: int) : inner_val(v) {}
        inner_val : int;
    }
    make(v: int) : int {
        i : Inner(v);   // __parent__ set to 'this' automatically
        return i.inner_val;
    }
}
```
### Default construction
If the inner struct has no user-defined constructor, the compiler generates a default constructor that also sets `__parent__`.
---
## 5. Access to outer struct fields from an inner struct
A non-static inner struct's methods can access fields and methods of the enclosing outer struct directly (via the implicit `__parent__` pointer).
```k
struct Outer {
    outer_val : int = 55;
    struct Inner {
        inner_val : int = 0;
        Inner(v: int) : inner_val(v) {}
        get_outer() : int { return outer_val; }   // accesses Outer::outer_val
        sum() : int { return inner_val + outer_val; }
    }
}
```
When there is a name conflict (inner field shadows outer field), use the qualified name to access the outer field explicitly:
```k
struct Outer {
    value : int = 10;
    struct Inner {
        value : int = 0;
        get_inner_value() : int { return value; }              // Inner::value
        get_outer_value() : int { return Outer::value; }       // Outer::value
    }
}
```
---
## 6. Name shadowing
If an inner struct declares a field with the same name as an outer struct field:
- The inner field takes precedence for bare name resolution inside the inner struct.
- The outer field can be accessed explicitly with `OuterStruct::fieldName`.
---
## 7. Multi-level nesting
Structs may be nested at multiple levels.  
Each level adds an implicit `__parent__` reference to the immediately enclosing struct.  
Methods of an innermost struct can access fields at all levels by traversing the parent chain.
```k
struct Outer {
    outer_val : int = 1;
    struct Middle {
        middle_val : int = 2;
        struct Inner {
            inner_val : int = 3;
            Inner(v: int) : inner_val(v) {}
            sum_all() : int {
                return inner_val + middle_val + outer_val;
            }
        }
        Middle(mv: int) : middle_val(mv) {}
        make_and_sum(iv: int) : int {
            i : Inner(iv);
            return i.sum_all();
        }
    }
    test_all(mv: int, iv: int) : int {
        m : Middle(mv);
        return m.make_and_sum(iv);
    }
}
test_multilevel() : int {
    o : Outer;
    return o.test_all(20, 300);   // 300 + 20 + 1 = 321
}
```
---
## 8. Examples
### Static nested struct
```k
struct Outer {
    value : int = 10;
    static struct Inner {
        x : int = 0;
        Inner(v: int) : x(v) {}
        get() : int { return x; }
    }
}
test_static_nested() : int {
    i : Outer::Inner(42);
    return i.get();   // 42
}
```
### Non-static inner struct accessing outer field
```k
struct Outer {
    outer_val : int = 55;
    struct Inner {
        inner_val : int = 0;
        Inner(v: int) : inner_val(v) {}
        get_outer() : int { return outer_val; }
        sum() : int { return inner_val + outer_val; }
    }
    make_and_sum(v: int) : int {
        i : Inner(v);
        return i.sum();
    }
}
test_inner_sum() : int {
    o : Outer;
    return o.make_and_sum(5);   // 5 + 55 = 60
}
```
---
*See also:* [Structures](structs.md) · [Constructors](constructors.md) · [Names and Lookup](../basic/names.md)
