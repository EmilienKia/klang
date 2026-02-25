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
---
## 3. Subscript operator
The subscript operator accesses an element of an array (or pointer).
### Grammar
```
PostfixOp (subscript):
    '[' Expression ']'
```
The left operand must be of array or pointer type; the right operand must be an integer.  
The result is a reference to the element at the given index.
```k
arr[0] = 1;
x : int = arr[2];
p[i] = v;
```
Array indices are zero-based. No runtime bounds check is performed.
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
---
*See also:* [Expressions](expressions.md) · [Functions](../functions/functions.md) · [Structures](../structs/structs.md) · [Overloading](../functions/overloading.md)
