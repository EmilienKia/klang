# Expressions

[← Index](../index.md)

An *expression* is a syntactic construct that is evaluated to produce a value and/or a side effect.
Every expression has a type determined at compile time.

---

## Contents

1. [Grammar overview](#1-grammar-overview)
2. [Expression categories](#2-expression-categories)
3. [Operator precedence and associativity](#3-operator-precedence-and-associativity)
4. [Expression list (comma expression)](#4-expression-list-comma-expression)
5. [Primary expressions](#5-primary-expressions)

---

## 1. Grammar overview
```
Expression:
    AssignmentExpr { ',' AssignmentExpr }
AssignmentExpr:
    ConditionalExpr [ AssignmentOperator AssignmentExpr ]
AssignmentOperator: (one of)
    =   *=   /=   %=   +=   -=   >>=   <<=   &=   ^=   |=
ConditionalExpr:
    LogicalOrExpr [ '?' ConditionalExpr ':' ConditionalExpr ]
LogicalOrExpr:
    LogicalAndExpr { '||' LogicalAndExpr }
LogicalAndExpr:
    InclusiveBinOrExpr { '&&' InclusiveBinOrExpr }
InclusiveBinOrExpr:
    ExclusiveBinOrExpr { '|' ExclusiveBinOrExpr }
ExclusiveBinOrExpr:
    BinAndExpr { '?' BinAndExpr }
BinAndExpr:
    EqualityExpr { '&' EqualityExpr }
EqualityExpr:
    RelationalExpr { ( '==' | '!=' ) RelationalExpr }
RelationalExpr:
    ShiftingExpr { ( '<' | '>' | '<=' | '>=' ) ShiftingExpr }
ShiftingExpr:
    AdditiveExpr { ( '<<' | '>>' ) AdditiveExpr }
AdditiveExpr:
    MultiplicativeExpr { ( '+' | '-' ) MultiplicativeExpr }
MultiplicativeExpr:
    PmExpr { ( '*' | '/' | '%' ) PmExpr }
PmExpr:
    CastExpr { ( '.*' | '->*' ) CastExpr }
CastExpr:
    '(' TypeSpec ')' CastExpr
    | UnaryExpr
UnaryExpr:
    'new' TypeName '(' [ ExpressionList ] ')'
    | 'delete' CastExpr
    | ( '++' | '--' | '*' | '&' | '+' | '-' | '!' | '+' ) CastExpr
    | PostfixExpr
PostfixExpr:
    PrimaryExpr { PostfixOp }
PostfixOp:
    '++'
    | '--'
    | '[' Expression ']'
    | '(' [ ExpressionList ] ')'
    | ( '.' | '->' ) IdentifierExpr
PrimaryExpr:
    Literal
    | 'this'
    | '(' Expression ')'
    | IdentifierExpr
IdentifierExpr:
    QualifiedIdentifier
```
---
## 2. Expression categories
### Value expressions
A value expression holds a concrete value of a primitive or composite type.  
This includes literals and the result of arithmetic or comparison operations.
### Symbol expressions
A symbol expression names a declared entity: a variable, a function, or a parameter.  
When evaluated in a value context, the referenced object's value is loaded.
### Unary expressions
Expressions with a single operand.  
See [Unary Operators](unary.md).
### Binary expressions
Expressions with two operands.  
See [Binary Operators](binary.md) and [Assignment Operators](assignment.md).
### Function call expressions
Apply a function to a list of arguments.  
See [Function Call](call.md).

> **Expression temporaries:** When a function call returns a struct by value, the result is a
> temporary that is materialised in compiler-managed storage.  All temporaries created within a
> full expression are destroyed at the end of the enclosing statement, in reverse creation
> order.  See [Destructors — §4](../structs/destructors.md#4-return-values-and-expression-temporaries).
### Constructor invocation expressions
Construct a struct-typed variable with explicit arguments.  
See [Constructors](../structs/constructors.md).
### Dynamic allocation expressions
`new T(args)` allocates and constructs an object of type `T` and returns a `T!` owner.
`delete owner` destroys and frees the object held by a `T!` variable.
See [Dynamic Allocation](../memory/new-delete.md).

### Member access expressions
Access a field or member function of a struct.  
See [Function Call and Member Access](call.md).
---
## 3. Operator precedence and associativity
Higher rows bind more tightly (higher precedence).
| Precedence | Operators | Associativity |
|------------|-----------|---------------|
| 1 (highest) | `()` (call), `[]` (subscript), `.`, `->`, `++` (post), `--` (post) | Left-to-right |
| 2 | `++` (pre), `--` (pre), unary `+`, unary `-`, `!`, `+`, `&` (address-of), `*` (dereference), `(type)` (cast), `new`, `delete` | Right-to-left |
| 3 | `.*`, `->*` | Left-to-right |
| 4 | `*`, `/`, `%` | Left-to-right |
| 5 | `+`, `-` | Left-to-right |
| 6 | `<<`, `>>` | Left-to-right |
| 7 | `<`, `>`, `<=`, `>=` | Left-to-right |
| 8 | `==`, `!=` | Left-to-right |
| 9 | `&` (bitwise AND) | Left-to-right |
| 10 | `?` (bitwise XOR) | Left-to-right |
| 11 | `\|` (bitwise OR) | Left-to-right |
| 12 | `&&` (logical AND) | Left-to-right |
| 13 | `\|\|` (logical OR) | Left-to-right |
| 14 | `?:` (conditional) | Right-to-left |
| 15 (lowest) | `=`, `*=`, `/=`, `%=`, `+=`, `-=`, `>>=`, `<<=`, `&=`, `^=`, `\|=` | Right-to-left |
---
## 4. Expression list (comma expression)
A comma expression evaluates each sub-expression in order and yields the value of the last one.
```
ExpressionList:
    AssignmentExpr { ',' AssignmentExpr }
```
Comma expressions are primarily used in function call argument lists (where commas separate arguments, not form a comma expression).  
In a top-level expression context, consecutive assignments separated by commas are evaluated in order.
---
## 5. Primary expressions
### Literals
Integer, floating-point, boolean, character, string, or null literals.  
See [Literals](literals.md).
### `this`
Inside a non-static member function, `this` refers to the current object.  
Its type is a reference to the enclosing struct type.
```k
struct Point {
    x : int;
    get() : int {
        return this.x;   // 'this' is a reference to the Point
    }
}
```
### Parenthesised expression
An expression enclosed in parentheses `( expr )` evaluates to the same value and type as `expr`.  
Parentheses are used to override precedence.
```k
result : int = (a + b) * c;
```
### Identifier expression
A bare name or qualified name refers to a declared variable or function.  
See [Identifiers and Name Expressions](identifiers.md).
---
*See also:* [Literals](literals.md) · [Unary Operators](unary.md) · [Binary Operators](binary.md) · [Assignment Operators](assignment.md) · [Function Call](call.md) · [Dynamic Allocation](../memory/new-delete.md) · [Operator Overloading](../functions/operators.md)
