# K Language — Templates

> **Version:** Working Draft — 2026  
> **Formal grammar:** see [`../grammar.ebnf`](../grammar.ebnf)

---

## Table of Contents

1. [Overview](#1-overview)
2. [Template Declaration Syntax](#2-template-declaration-syntax)
3. [Template Parameters](#3-template-parameters)
4. [Applicable Declarations](#4-applicable-declarations)
5. [Template Instantiation](#5-template-instantiation)
6. [Instantiation in Type Specifiers](#6-instantiation-in-type-specifiers)
7. [Name Mangling](#7-name-mangling)
8. [Interaction with Other Features](#8-interaction-with-other-features)
9. [Error Model](#9-error-model)
10. [Examples](#10-examples)

---

## 1. Overview

Templates provide compile-time parametric polymorphism in K. A **template declaration**
introduces one or more compile-time parameters — either **type parameters** or **value
parameters** — that are substituted when the template is **instantiated** with concrete
arguments.

K templates follow a **monomorphization** model: each unique set of template arguments
produces a distinct, fully-independent concrete entity (function or aggregate). There is
no type erasure or runtime dispatch on template parameters.

### 1.1 Currently Implemented

The following template features are fully implemented, tested, and usable:
- Template function declarations (free and member functions).
- Template aggregate declarations (`struct`, `class`, `interface`).
- Type parameters (`typename`, `struct`, `class`, `interface`).
- Value parameters (compile-time constant integers, booleans, etc.).
- Type constraints (base-type requirement).
- Default parameter values and default types.
- Full explicit instantiation: `MyTemplate<int, 42>`.
- Cross-module templates: define a template in one module, import and instantiate it in
  another (via KDI export/import with reconstructed source fragments).
- Name mangling of template instantiations.
- Constructor member-initializer lists in template aggregates.

### 1.2 Planned (Not Yet Implemented)

The following features are planned for future development:
- Template specialization (partial or full).
- Template template parameters.
- Variadic template parameters (parameter packs).
- Deduction of template arguments from function call arguments.
- Concepts or type traits beyond simple base-type constraints.
- Templates on constructors, destructors, operators, or enumerations independently.
- `extern template` declarations.

---

## 2. Template Declaration Syntax

A template declaration is a prefix clause placed **after** any annotation definitions and
**before** the entity declaration (function or aggregate).

```
TemplateDeclaration:
    'template' '<' TemplateParameterList '>'
```

```
TemplateParameterList:
    TemplateParameter { ',' TemplateParameter }
```

The `template` keyword introduces the parameter list enclosed in angle brackets.

### 2.1 Grammar Integration

The template declaration modifies `AggregateDecl` and `FunctionDecl` as follows:

```ebnf
AggregateDecl
    = { AnnotationDef } , [ TemplateDeclaration ] , { Specifier } ,
      ( 'struct' | 'class' | 'interface' | 'annotation' ) ,
      Identifier ,
      [ ':' , BaseClause ] ,
      '{' , { Declaration } , '}'
    ;

FunctionDecl
    = { AnnotationDef } , [ TemplateDeclaration ] , { Specifier } , [ 'fun' ] ,
      ( FunctionHead | OperatorFunctionHead | DestructorHead ) ,
      '(' , [ ParameterList ] , ')' ,
      [ NamedReturnVar ] ,
      [ ':' , ( ReturnTypeOrMemberInitList ) ] ,
      FunctionBody
    ;
```

> **Placement rule:** The `TemplateDeclaration` always appears between annotations and
> specifiers. Specifiers (`public`, `static`, `abstract`, etc.) apply to the resulting
> instantiation, not to the template itself.

---

## 3. Template Parameters

```
TemplateParameter:
    [ TemplateParameterKind ] Identifier [ ':' QualifiedIdentifier ] [ '=' Expression ]

TemplateParameterKind:
    'typename' | 'struct' | 'interface' | 'class'
```

A template parameter is either a **type parameter** or a **value parameter**, determined
by the presence or absence of a `TemplateParameterKind` prefix.

### 3.1 Type Parameters

A type parameter is introduced by one of the four keywords:

| Keyword     | Constraint                                                        |
|-------------|-------------------------------------------------------------------|
| `typename`  | Any type (primitive, struct, class, interface, enum, array, etc.) |
| `struct`    | Must be a `struct` type (or derived from one)                    |
| `class`     | Must be a `class` type (or derived from one)                     |
| `interface` | Must be an `interface` type (or derived from one)                |

#### 3.1.1 Type Constraint (Optional)

After the identifier, an optional `: QualifiedIdentifier` specifies a **base-type
constraint**. When instantiating, the supplied type argument must either:
- Be exactly the constraining type, or
- Derive from the constraining type (following the language's inheritance rules for the
  appropriate aggregate kind).

The constraint refines but does not replace the `TemplateParameterKind` filter:
- `class T : Animal` — T must be a class that is or derives from `Animal`.
- `typename T : Comparable` — T must be any type that is or derives from `Comparable`.

#### 3.1.2 Default Type (Optional)

After the identifier and optional constraint, `= QualifiedIdentifier` provides a default
type. When the user does not supply an argument for this parameter at instantiation:
1. The default type is used.
2. If no default type is specified but a constraint type exists, the constraint type is
   used as the default.
3. If neither is specified, it is an instantiation error.

> **Note:** The default expression for a type parameter must be a `QualifiedIdentifier`
> (a type name), not an arbitrary expression.

### 3.2 Value Parameters

A value parameter has **no** `TemplateParameterKind` keyword prefix. Instead, a mandatory
`: TypeSpec` specifies the type of the expected compile-time constant value.

```
count : unsigned int = 16
```

- The type must be a **compile-time-evaluable type**: any primitive type (`bool`, `char`,
  `byte`, `short`, `int`, `long`, `float`, `double`, and their `unsigned` variants), or
  an `enum` type. Aggregates, indirections, arrays, and function references are not
  permitted as value parameter types.
- The supplied value expression (at instantiation) must be a **constant expression**
  resolvable at compile time (literal values).
- An optional `= Expression` provides a default value.
- All primitive types are stored as `k::value_type`, a variant covering `bool`, `char`,
  `unsigned char`, `short`, `unsigned short`, `int`, `unsigned int`, `long`,
  `unsigned long`, `long long`, `unsigned long long`, `float`, `double`.

### 3.3 Parameter Ordering

There is no ordering restriction between type and value parameters. However, once a
parameter has a default value or default type, all subsequent parameters must also have
defaults (same rule as function default parameters).

---

## 4. Applicable Declarations

### 4.1 Template Functions

A template function is a function declaration preceded by a `TemplateDeclaration`.

```k
template<typename T>
swap(a: T+, b: T+) {
    tmp : T = *a;
    *a = *b;
    *b = tmp;
}
```

Within the function body, the template type parameter `T` is usable wherever a type
specifier is expected: parameter types, return type, local variable types, cast targets,
`new` expressions, etc.

Template member functions (inside an aggregate) are supported. However, a template
member function inside a non-template aggregate generates one concrete function per
instantiation.

### 4.2 Template Aggregates

A template aggregate is a `struct`, `class`, or `interface` declaration preceded by a
`TemplateDeclaration`.

```k
template<typename T>
struct Pair {
    first  : T;
    second : T;
}
```

All members (fields, methods, constructors, destructors, nested types) of a template
aggregate are implicitly parameterized by the enclosing template parameters. They are
instantiated together with the aggregate.

### 4.3 Restrictions

The following declarations cannot currently appear as the subject of a
`TemplateDeclaration`:
- Namespaces
- `enum` declarations (standalone)
- `using` declarations
- Module and import declarations
- Annotation types

---

## 5. Template Instantiation

A template is instantiated by providing concrete arguments for all its parameters,
enclosed in angle brackets, at the point of use.

```
TemplateArgList:
    '<' TemplateArg { ',' TemplateArg } '>'

TemplateArg:
    TypeSpec
    | ConditionalExpr
```

### 5.1 Explicit Instantiation

In Phase 1, all template arguments must be supplied explicitly. There is no deduction.

```k
p : Pair<int>;
swap<float>(&a, &b);
```

### 5.2 Instantiation Semantics

When the compiler encounters a template instantiation `Name<Args...>`:
1. Look up the template declaration for `Name` in the current scope.
2. Match each argument to the corresponding parameter:
   - For type parameters: the argument must be a valid type satisfying any kind filter
     and constraint.
   - For value parameters: the argument must be a constant expression of the declared
     type (or implicitly convertible to it).
3. Apply defaults for any trailing parameters not explicitly supplied.
4. Validate type constraints (kind filter and base-type constraint).
5. Check whether this exact combination has already been instantiated (cache lookup).
6. If not, perform **monomorphization**: clone the template's model-level members
   (fields, methods, parameters, expressions, statements), substitute all type
   placeholders with concrete types and value placeholders with concrete values,
   then proceed through the full compilation pipeline (type resolution, code
   generation, etc.). No AST cloning or re-parsing is involved.
7. Register the resulting concrete entity so that future uses of the same arguments
   reuse it.

### 5.3 Instantiation Context

Template instantiation occurs lazily (on first use). The instantiated entity is placed in
the same namespace/scope as the template definition. Multiple translation units
instantiating the same template with the same arguments will produce duplicate symbols
that the linker must deduplicate (weak/COMDAT linkage).

---

## 6. Instantiation in Type Specifiers

Template aggregate names may appear in type specifiers wherever a `QualifiedIdentifier`
is accepted:

```k
v : Pair<int>*;                     // pointer to Pair<int>
arr : Pair<float>[10];              // array of 10 Pair<float>
process(p: Pair<int>&) : bool;      // parameter type
```

The `QualifiedIdentifier` grammar is extended:

```ebnf
QualifiedIdentifier
    = [ '::' ] , IdentifierSegment , { '::' , IdentifierSegment }
    ;

IdentifierSegment
    = Identifier [ TemplateArgList ]
    ;

TemplateArgList
    = '<' , TemplateArg , { ',' , TemplateArg } , '>'
    ;

TemplateArg
    = TypeSpec
    | ConditionalExpr
    ;
```

### 6.1 Parsing Ambiguity with `<` and `>`

The angle brackets `<` and `>` are also comparison operators. The parser resolves
ambiguity as follows:

- When the parser has just consumed an `Identifier` and the next token is `<`, it
  checks whether a template declaration with that name exists in the current scope.
  If yes, `<` opens a template argument list. If no, `<` is a comparison operator.
- Inside a template argument list, `>` closes the list. Nested `<`/`>` for nested
  template arguments are balanced by counting.
- Shift operators `>>` must be split by the parser when occurring inside nested template
  argument lists (e.g., `Pair<Pair<int>>` — the `>>` is two closing `>`).

---

## 7. Name Mangling

Template instantiations are mangled by appending the mangled template argument list to
the entity's qualified name, inside the `N…E` scope block.

### 7.1 Mangling Scheme

```
Template arguments are encoded between 'I' and 'E' markers after the template name:

  N <name> I <args> E E

Each template argument is mangled as:
  - Type argument:  the mangled type (same encoding as parameter types)
  - Value argument: 'L' <type> <value> 'E'
    - Integer values: decimal encoding (negative with 'n' prefix)
    - Boolean values: 'b' '0' or 'b' '1'
```

Examples:
- `Pair<int>` → `N4PairIiEE`
- `Pair<unsigned long>` → `N4PairIyEE`
- `Array<int, 10>` → `N5ArrayIiLi10EEE`
- `swap<float>` (free function) → `_KFN4swapIfEE…` (with function params after)

### 7.2 Mangling of Nested Templates

```
Container<Pair<int>>  →  N9ContainerIN4PairIiEEEE
```

---

## 8. Interaction with Other Features

### 8.1 Inheritance

A template aggregate may inherit from:
- A non-template aggregate.
- A specific instantiation of a template aggregate.
- A template parameter (if it is a type parameter with an appropriate kind constraint).

```k
template<class T : Animal>
struct Cage : Container<T> {
    occupant : T*;
}
```

### 8.2 Virtual Dispatch

Template classes support virtual dispatch. Each instantiation has its own vtable.

### 8.3 Annotations

Annotations may be applied to template declarations. The annotations are propagated to
each instantiation.

### 8.4 Visibility

Visibility specifiers apply to the template definition and are inherited by all
instantiations.

### 8.5 Constructors and Destructors

Constructors and destructors of template aggregates are instantiated with the aggregate.
They cannot have independent template parameter lists in Phase 1.

### 8.6 KDI Export

Template definitions are exported in `.kdi` files as `kdi_template_def` entries, which
contain the template parameter descriptors and a reconstructed K source fragment.  The
source is emitted from the semantic model (using `k_source_emitter`) with all
`using`-aliases resolved to fully-qualified names so that the importing module can
re-parse and re-instantiate the template locally with new type arguments.

**Concrete instantiations** that are part of the module's public/protected API are also
exported as regular aggregates or functions with their mangled names. A `template_origin`
metadata field in the KDI records that the entity was produced by template instantiation,
including the base template name, its fully-qualified name, and the concrete arguments
used.

This enables two cross-module usage patterns:
1. **Consumer calls a wrapper:** The library instantiates the template internally and
   exports the concrete entity.  The consumer calls the wrapper function that exercises
   the concrete entity.
2. **Consumer instantiates directly:** The library exports the template definition. The
   consumer imports the library and instantiates the template with its own arguments.
   The KDI importer re-parses the template source fragment and builds the template in
   the consumer's model, enabling local instantiation.

### 8.7 Operator Overloading

Template functions may be operator overloads:

```k
template<typename T>
operator +(a: Pair<T>&, b: Pair<T>&) : Pair<T> {
    return Pair<T>{.first = a.first + b.first, .second = a.second + b.second};
}
```

### 8.8 `using` Directives

A `using` alias may refer to a specific template instantiation:

```k
using IntPair = Pair<int>;
```

Template aliases (parameterized `using`) are not supported in Phase 1.

---

## 9. Error Model

### 9.1 Instantiation Errors (Implemented)

| Code     | Name                               | Description                                                   |
|----------|------------------------------------|---------------------------------------------------------------|
| `0x0180` | `ERR_TPL_NO_MATCHING`              | No matching template found                                    |
| `0x0181` | `ERR_TPL_ARG_COUNT_MISMATCH`       | Wrong number of template arguments                            |
| `0x0182` | `ERR_TPL_ARG_WRONG_KIND`           | Type argument is wrong aggregate kind (e.g., class instead of struct) |
| `0x0183` | `ERR_TPL_ARG_CONSTRAINT_VIOLATED`  | Type argument does not satisfy base-type constraint            |
| `0x0184` | `ERR_TPL_ARG_NOT_AGGREGATE`        | Type argument is not an aggregate (for struct/class/interface filter) |
| `0x0185` | `ERR_TPL_VALUE_ARG_TYPE_MISMATCH`  | Value argument type mismatch                                  |
| `0x0186` | `ERR_TPL_VALUE_ARG_NOT_CONST`      | Value argument is not a compile-time constant                 |

### 9.2 Error Propagation

- **Aggregate template constraint violations**: throw `resolution_error` (fatal) via
  `throw_error()` in `aggregate_type_resolver` and `type_reference_resolver`.
- **Function template constraint violations**: log error via `logger_relay::error()` and
  set `args_ok = false` (non-fatal) in `gen_expressions`, allowing overload resolution
  to continue and produce a "no matching overload" diagnostic.

### 9.3 Error Message Format

Error messages include:
- The template name (e.g., `"Container"`)
- The parameter name and index (e.g., `"parameter 'T' (index 0)"`)
- The expected kind or constraint type
- The actual type provided

Example: `"template 'Container' parameter 'T' (index 0): expected a struct, but got class 'MyClass'"`

### 9.4 Body Errors

Errors inside a template body are reported **at instantiation time** against the
concrete types. The error message includes the template name and the arguments that
triggered the error, plus the source location in the template definition.

---

## 10. Examples

### 10.1 Template Function

```k
module example;

template<typename T>
max(a: T&, b: T&) : T {
    if(a > b) {
        return a;
    }
    return b;
}

main() : int {
    x : int = 10;
    y : int = 20;
    result : int = max<int>(x, y);
    return result - 20;
}
```

### 10.2 Template Struct

```k
module containers;

template<typename T>
struct Pair {
    first  : T;
    second : T;

    Pair(f: T, s: T) : first(f), second(s) {}

    getFirst() : T& {
        return first;
    }

    getSecond() : T& {
        return second;
    }
}

main() : int {
    p : Pair<int>(3, 7);
    return p.getFirst() + p.getSecond() - 10;
}
```

### 10.3 Template with Value Parameter

```k
module arrays;

template<typename T, N : unsigned int>
struct FixedArray {
    data : T[N];
    
    get(index: unsigned int) : T& {
        return data[index];
    }

    const size() : unsigned int {
        return N;
    }
}

main() : int {
    arr : FixedArray<int, 5>;
    arr.data[0] = 42;
    return arr.get(0) - 42;
}
```

### 10.4 Template with Type Constraint

```k
module zoo;

class Animal {
    name : const char[];

    Animal(n: const char[]) : name(n) {}
    
    speak() : int { return 0; }
}

template<class T : Animal>
feed(animal: T+) : int {
    return animal->speak();
}
```

### 10.5 Template Class with Inheritance

```k
module collections;

template<typename T>
class Container {
    count : int = 0;

    Container() {}

    getCount() : int { return count; }
}

template<typename T>
class Stack : Container<T> {
    // ...members using T...
}
```

---

*For the complete EBNF grammar integrating template syntax, see [`../grammar.ebnf`](../grammar.ebnf).*

