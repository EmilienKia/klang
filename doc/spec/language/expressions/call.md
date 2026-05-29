# Function Call, Subscript and Member Access

[← Index](../index.md) · [Expressions](expressions.md)

This page covers postfix expression forms: function invocation, array subscript, and member access through `.` and `->`.

---

## Contents
1. [Function call](#1-function-call)
2. [Constructor invocation](#2-constructor-invocation)
3. [Subscript operator](#3-subscript-operator)
4. [Member access — dot operator `.`](#4-member-access--dot-operator-)
5. [Member access through pointer — arrow operator `->`](#5-member-access-through-pointer--arrow-operator--)
6. [Pointer-to-member call — operators `.*` and `->*`](#6-pointer-to-member-call--operators--and--)
---
## 1. Function call
A function call evaluates the callee expression (resolving it to a function), evaluates the arguments, and invokes the function.
### Grammar
```
PostfixExpr:
    PrimaryExpr { PostfixOp }
PostfixOp (call):
    '(' [ ExpressionList ] ')'
ExpressionList:
    AssignmentExpr { ',' AssignmentExpr }
```
**Free function call:**
```k
result : int = increment(41);
multiply(2, 3)
fibo(8u)
```
**Member function call:**
Called on an object with `.` or through a pointer with `->`.
```k
p.add(8)           // call member function 'add' on object 'p'
r.add()            // call through reference
ptr->getValue()    // call through pointer
```
**Static member function call:**
Static member functions are called via the struct name and `::`:
```k
plop::sub(43)
titi::b = 13;   // static field access (not a call)
```
### Argument passing
- Arguments are matched to parameters left-to-right.
- Each argument is implicitly converted to the corresponding parameter type (see [Types](../basic/types.md#7-implicit-conversions)).
- Pass-by-value for primitive types and struct types (a copy is made).
- Pass-by-reference if the parameter type is a reference type (`T&`): the argument must be an lvalue.

**Struct pass-by-value:**  
When a struct is passed by value, a bitwise copy of the argument is placed in the callee's
parameter storage.  If the struct has a destructor, the destructor is called on the parameter
copy at function exit, in reverse declaration order together with other locals.
See [Destructors — By-value parameters](../structs/destructors.md#3-by-value-parameters).
### Default parameter values
Parameters may have default values. If a call omits trailing arguments that have defaults, the default values are substituted.
```k
// Declaration:
increment(n: int, step: int = 1) : int { return n + step; }
// Calls:
increment(10)       // step defaults to 1 → result: 11
increment(10, 5)    // step is 5 → result: 15
```
**Constraint:** Only trailing parameters may have default values.
### Overload resolution
When multiple functions share the same name, the compiler selects the best match based on the argument types.  
See [Function Overloading](../functions/overloading.md).
---
## 2. Constructor invocation
A constructor invocation initialises a struct-typed variable using an explicit constructor call.
The syntax uses the variable declaration form with constructor arguments after the type name:
```k
p : plop(5);        // declare 'p' of type 'plop' and call plop(int) constructor
i : Outer::Inner(42);
```
This is equivalent to a variable declaration with constructor arguments (not a standalone expression).  
See [Constructors](../structs/constructors.md) for the full constructor syntax.

To create an **anonymous temporary** of a struct type within an expression (without declaring
a variable), use the same syntax in expression context: `T(args…)`.  
See [Temporary Object Construction](temporary-construction.md).
---
## 3. Subscript operator
The subscript operator accesses an element of an array through any supported type.
### Grammar
```
PostfixOp (subscript):
    '[' Expression ']'
```
The left operand must be an array or an indirection to an array; the right operand must be
an integer.  The result is a reference (`&`) to the element at the given index.

**Supported left operand types:**

| Left operand type | Description |
|-------------------|-------------|
| `T[N]` | Sized array value |
| `T[N]&` / `T[]&` | Reference to an array |
| `T[N]!` / `T[]!` | Owner of an array |
| `T[N]*` / `T[]*` | Pointer to an array |
| `T[N]+` / `T[]+` | Link to an array |
| `T[N]?` / `T[]?` | View to an array |

For all indirection types, the subscript operator transparently dereferences the indirection
to access the underlying array element.

```k
arr[0] = 1;
x : int = arr[2];
p : int[3]* = &arr;
p[1] = 42;           // subscript through pointer
```
Array indices are zero-based.

**Arrays of indirections:**

When the element type of the array is itself an indirection (see [Types — §9.7](../basic/types.md#97-arrays-of-indirection-types)),
the subscript returns a reference to the element slot, which holds an address.
Apply `*` to dereference the pointed-to value:

```k
a : int = 10;
b : int = 20;
arr : int+[] {&a, &b};
val : int = *arr[0];     // dereference the link at index 0 → 10
*arr[1] = 99;            // write-through: modifies 'b' to 99
```

**Runtime bounds checking:** every subscript access on an array (through any indirection)
is checked at runtime. The index is compared (unsigned) against the element count stored
in the array header. If the index is out of bounds, an `IndexOutOfBoundsError` (a
`FatalError`) is thrown. This exception can be caught but does not require a `throws`
declaration.

> **Note:** the bounds check uses an unsigned comparison, so negative indices
> (which wrap to large unsigned values) are caught as well.
---
## 4. Member access — dot operator `.`
Access a field or call a member function of a struct object (by value or by reference).
### Grammar
```
PostfixOp (member access):
    '.' IdentifierExpr
```
**Field access:**
```k
p.a = 10;           // assign to field 'a' of object 'p'
q.b = p.a + 20;
x : int = p.add(8); // call member function
```
Inside a member function, `this.field` and bare `field` are equivalent:
```k
struct Counter {
    n : int;
    get() : int { return n; }          // implicit this.n
    getEx() : int { return this.n; }   // explicit this.n
}
```

**Member access on temporaries (rvalues):**

The `.` operator also works on struct-typed rvalues — for example, the result of a function
call that returns a struct by value.  The temporary is materialised into compiler-managed
storage and remains alive until the end of the enclosing full expression statement:

```k
struct Obj {
    val : int;
    get() : int { return val; }
}
make(v: int) : Obj { o : Obj; o.val = v; return o; }

test() : int {
    return make(42).get();   // member call on temporary — valid
}
```

Chained method calls on temporaries are also valid; all intermediate temporaries survive
until the end of the statement:

```k
make(1).add(10).add(100).get();
// Three temporaries created; all destroyed at ';' in reverse order.
```

See [Destructors — Return values and expression temporaries](../structs/destructors.md#4-return-values-and-expression-temporaries)
for the full lifetime rules.

**Array virtual member — `size`:**

The `.` operator also provides access to the virtual `size` member on arrays.
This returns the element count as an `unsigned int`:

```k
arr : int[5]{10, 20, 30, 40, 50};
sz : unsigned int = arr.size;   // 5
```

See [Types — §9.8](../basic/types.md#98-virtual-member-size) for full details.

---
## 5. Member access through pointer — arrow operator `->`
Access a field or call a member function of a struct through a pointer.
### Grammar
```
PostfixOp (pointer member access):
    '->' IdentifierExpr
```
`ptr->member` is equivalent to `(*ptr).member`.
```k
struct Node {
    value : int;
    next  : Node*;
}
p : Node*;
p->value = 42;          // equivalent to (*p).value = 42
p->next = null;
```

**Array virtual member — `size`:**

The `->` operator also provides access to the virtual `size` member on arrays
accessed through any indirection type (pointer, link, view, owner):

```k
o : int[3]! = new int[]{1, 2, 3};
sz : unsigned int = o->size;    // 3
```

See [Types — §9.8](../basic/types.md#98-virtual-member-size) for full details.
---
## 6. Pointer-to-member call — operators `.*` and `->*`
These operators call a *member function reference* (a variable of type `T::*(Params)`, `T::?(Params)` or `T::+(Params)`) on a specific receiver object.
### Grammar
```
MemberRefCallExpr:
    '(' ObjExpr '.*'   MfpExpr ')' '(' [ ExpressionList ] ')'
  | '(' IndirExpr '->*' MfpExpr ')' '(' [ ExpressionList ] ')'
```
The outer parentheses around `(obj.*mfp)` are **required** — `.*` and `->*` have lower precedence than the function-call operator `()`.
### `.*` — call on an object value or reference
`ObjExpr` must be of struct type `T` (a variable, parameter, or reference `T&`).
```k
struct Counter {
    value : int;
    add(x : int) : int { return value + x; }
}
test() : int {
    mfp : Counter::*(int) = Counter::add;
    c   : Counter;
    c.value = 40;
    return (c.*mfp)(2);     // calls c.add(2) -> 42
}
```
### `->*` — call through a pointer, link, or view
`IndirExpr` must be of type `T*`, `T?`, or `T+`.
```k
test_link() : int {
    mfp : Counter::*(int) = Counter::add;
    c   : Counter;
    c.value = 40;
    lnk : Counter+ = c;
    return (lnk->*mfp)(2);  // -> 42  (via link)
}
test_ptr() : int {
    mfp : Counter::*(int) = Counter::add;
    c   : Counter;
    c.value = 40;
    ptr : Counter* = c;
    return (ptr->*mfp)(2);  // -> 42  (via pointer)
}
```
For the full specification of member function reference types and call semantics, see [Function References](../functions/function_references.md).
---
*See also:* [Expressions](expressions.md) · [Functions](../functions/functions.md) · [Function References](../functions/function_references.md) · [Structures](../structs/structs.md) · [Overloading](../functions/overloading.md)
