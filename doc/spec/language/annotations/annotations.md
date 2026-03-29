# Annotations

[← Index](../index.md) · [Classes](../structs/classes.md)

An **annotation** is a user-defined metadata type that can be attached to
aggregate declarations (classes, interfaces, and other annotations).
At runtime, the annotation instances are accessible through the RTTI system
and can be inspected by user code.

Annotation types support **member variables** and **methods**, follow the
same construction rules as structs, and are **implicitly `const`** — their
content is immutable after construction.

---

## Contents

1. [Declaring an annotation type](#1-declaring-an-annotation-type)
2. [Member variables](#2-member-variables)
3. [Methods](#3-methods)
4. [Applying annotations](#4-applying-annotations)
5. [Construction rules](#5-construction-rules)
6. [Constness and immutability](#6-constness-and-immutability)
7. [Multiple annotations](#7-multiple-annotations)
8. [Annotation inheritance from `k::Annotation`](#8-annotation-inheritance-from-kannotation)
9. [Annotation type RTTI — `k::AnnotationType`](#9-annotation-type-rtti--kannotationtype)
10. [Reading annotations at runtime](#10-reading-annotations-at-runtime)
11. [Annotation visibility and scope](#11-annotation-visibility-and-scope)
12. [Allowed member types](#12-allowed-member-types)
13. [Inner enums](#13-inner-enums)
14. [Meta-annotations](#14-meta-annotations)
15. [Meta-annotation reference](#15-meta-annotation-reference)
16. [Library export](#16-library-export)
17. [Examples](#17-examples)
18. [Grammar](#18-grammar)
19. [Error reference](#19-error-reference)

---

## 1. Declaring an annotation type

An annotation type is declared with the `annotation` keyword, followed by a
name and a body delimited by braces:

```k
annotation Deprecated {}

annotation Version {
    major : int;
    minor : int;
}
```

### Rules

| Rule | Description |
|------|-------------|
| **Keyword** | `annotation` introduces the declaration. |
| **Body** | May contain member variables and methods. |
| **Implicit base** | Every annotation type implicitly inherits from `k::Annotation`, the root annotation class. |
| **Implicit `const`** | Annotation types are implicitly `const` — all member variables are immutable after construction, and all non-static methods are implicitly `const`. |
| **Vtable** | Annotation types have a vtable with a single RTTI slot (slot 0), used for type identification at runtime. Member functions are **not** virtual. |
| **Default variable visibility** | `public` (like structs, unlike classes which default to `protected`). |
| **Default function visibility** | `public`. |

---

## 2. Member variables

Annotation types can declare member variables. These variables are
**public by default** and can be accessed from outside the annotation type.

```k
annotation Author {
    name : const char[];
    year : int;
}
```

### Allowed types

Member variables of annotation types must have one of the following types:

| Type category | Examples | Notes |
|---------------|----------|-------|
| Primitive types | `int`, `bool`, `char`, `byte`, `short`, `long`, `float`, `double`, `unsigned int`, etc. | All primitive types. |
| Inner enum types | `Level`, `Policy` | Enums declared inside the annotation body (see [§13](#13-inner-enums)). |
| Sized arrays of primitives | `int[3]`, `bool[4]`, `char[10]` | Fixed-size arrays. |
| `const char[]` | `const char[]` | Unsized char array — compatible with string literals. |
| Other annotation types | `Inner` (where `annotation Inner {}`) | By value — the inner annotation is embedded. |
| Arrays of annotation views | `const Base?[]` | Nullable views to allow polymorphic (derived) annotations. |
| Arrays of inner enum types | `ElementType[]` | Unsized arrays of an inner enum (see `@Target`). |

Types that are **not** allowed as annotation member variables:

| Disallowed | Reason |
|------------|--------|
| Class or struct references | `MyClass&`, `MyStruct+` — annotations are not object containers. |
| Pointers and owners | `int*`, `T!` — annotations are compile-time constants, no heap allocation. |
| Mutable types | Fields are always implicitly const. |

### Default values

Member variables may have default initializers, following the same rules as
struct member variable defaults:

```k
annotation Config {
    level : int = 1;
    verbose : bool = false;
}
```

When an annotation is applied without specifying a value for a field, the
default initializer is used. If no default is specified, the field is
zero-initialized.

---

## 3. Methods

Annotation types may declare methods. All methods are **implicitly `const`**
(since the annotation type itself is implicitly `const`). Methods are
**never virtual** — they are direct calls only.

```k
annotation Range {
    min : int;
    max : int;

    span() : int {
        return max - min;
    }
}
```

Methods serve as computed accessors and can be called on annotation instances.

---

## 4. Applying annotations

An annotation is applied to an aggregate declaration by prefixing it with
`@AnnotationName`. The annotation definition appears **before** any specifiers
and the aggregate keyword.

### Syntax

```
'@' QualifiedIdentifier [ '(' [ ExpressionList ] ')' | DesignatedBraceInitList | BraceInitList ]
```

Three initialization forms are supported:

| Form | Syntax | Example | Meaning |
|------|--------|---------|---------|
| **Default** | `@Name` | `@Deprecated` | Default-constructed: all fields get default values or zero. |
| **Positional** | `@Name(args…)` | `@Version(2, 1)` | Constructor-style initialization matching fields by position. |
| **Designated** | `@Name{.f = v, …}` | `@Author{.name="Alice", .year=2026}` | Named-field initialization, unmentioned fields get defaults. |

### Applicable targets

Annotations can be applied to:

| Target | Supported |
|--------|-----------|
| `class` | ✓ |
| `interface` | ✓ |
| `annotation` | ✓ |
| `struct` | ✗ (error — structs have no vtable/RTTI) |
| Function | ✗ (future) |
| Variable | ✗ (future) |
| Parameter | ✗ (future) |

---

## 5. Construction rules

Annotation construction follows the **same rules as struct construction**:

### Positional form `@Ann(args…)`

1. Arguments are matched to the annotation's member variables in declaration
   order (skipping synthetic fields like `__vptr__`).
2. The number of arguments must be compatible: either exactly matching the
   number of fields, or fewer if the remaining fields have default values.
3. Each argument must be type-compatible with the corresponding field.
4. **All expressions must be compile-time constants** — function calls,
   variable references, and other non-constant expressions are rejected.

```k
annotation Version {
    major : int;
    minor : int;
    patch : int = 0;
}

@Version(2, 1)      // major=2, minor=1, patch=0 (default)
class Api { /* ... */ }

@Version(3, 0, 1)   // major=3, minor=0, patch=1
class ApiNext { /* ... */ }
```

### Designated form `@Ann{.field = val, …}`

1. Each `.field` must name an existing, accessible member variable.
2. Unknown field names are an error.
3. Duplicate field names are an error.
4. Fields not mentioned are initialized with their declared default value,
   or zero-initialized if no default.
5. **All expressions must be compile-time constants.**
6. Both assignment form (`.field = expr`) and constructor form
   (`.field(args…)`) are supported, as in
   [designated struct initializers](../structs/designated-init.md).

```k
annotation Author {
    name : const char[];
    year : int = 2026;
}

@Author{.name = "Alice"}             // year=2026 (default)
class MyLib { /* ... */ }

@Author{.name = "Bob", .year = 2025} // both specified
class OtherLib { /* ... */ }
```

### Default form `@Ann`

Equivalent to `@Ann()` with zero arguments: all fields are initialized
with their default values or zero.

```k
annotation Config {
    level : int = 1;
    verbose : bool = false;
}

@Config                               // level=1, verbose=false
class DefaultService { /* ... */ }
```

### Recursive construction

Annotation fields that are themselves annotation types are initialized
recursively using the same construction forms:

```k
annotation Inner {
    x : int;
}

annotation Outer {
    inner : Inner;
    label : const char[];
}

@Outer{.inner = @Inner(42), .label = "hello"}
class Composed { /* ... */ }
```

Arrays of annotation views are initialized with brace-enclosed annotation
applications:

```k
annotation Tag {
    id : int;
}

annotation Tags {
    items : const Tag?[];
}

@Tags({@Tag(1), @Tag(2), @Tag(3)})
class MultiTagged { /* ... */ }
```

---

## 6. Constness and immutability

Annotation types are **implicitly `const`**:

- All member variables are immutable after construction.
- All non-static member methods are implicitly `const`.
- Annotation instances are materialized as **LLVM global constants** —
  they exist as read-only data in the compiled binary.
- Writing `const annotation` is accepted but redundant (a warning may
  be emitted).

This guarantee means:
- No field of an annotation can be modified at runtime.
- Annotation accessors (methods) are always `const`.
- Annotation instances are shared and thread-safe by construction.

---

## 7. Multiple annotations

Multiple annotations can be applied to the same declaration:

```k
@Deprecated @Version(2, 0)
class OldApi { /* ... */ }
```

At runtime, `getAnnotations()` returns an array containing one `k::Annotation`
instance for each applied annotation, in declaration order.

---

## 8. Annotation inheritance from `k::Annotation`

Every annotation type implicitly inherits from `k::Annotation` (defined in
`libk/libk/src/rtti.k`). This base class provides:

| Method | Return type | Description |
|--------|-------------|-------------|
| `getAnnotationType()` | `const k::AnnotationType&` | Returns the RTTI descriptor for the concrete annotation type. |

The `getAnnotationType()` method is declared `final` — it cannot be
overridden. It reads the annotation's vtable slot 0 (the RTTI pointer)
to identify the concrete annotation type at runtime.

---

## 9. Annotation type RTTI — `k::AnnotationType`

Each annotation type has a corresponding `k::AnnotationType` instance
generated by the compiler. `AnnotationType` implements `k::AggregateType`
and provides:

| Method | Return type | Description |
|--------|-------------|-------------|
| `getName()` | `const char[]?` | Short (unqualified) name of the annotation type. |
| `getFullName()` | `const char[]?` | Fully qualified name. |
| `getBases()` | `const TypeInfo?[]?` | Base type descriptors (if any). |
| `getNested()` | `const TypeInfo?[]?` | Nested type descriptors (if any). |
| `getEnclosing()` | `const TypeInfo?` | Enclosing type descriptor, or `null` if top-level. |
| `getVisibility()` | `Visibility` | Declared visibility. |
| `isStatic()` | `bool` | `true` if the annotation type is a static nested type. |

See [RTTI Types](../../stdlib/rtti.md) for the full stdlib RTTI type hierarchy.

---

## 10. Reading annotations at runtime

Annotations are accessed through the class or interface RTTI descriptor.
Both `k::Class` and `k::Interface` provide a `getAnnotations()` method:

```k
const getAnnotations() : const Annotation?[]?;
```

This returns `null` if the type has no annotations, or an array of
`k::Annotation?` pointers otherwise.

### Example — reading an annotation by type name

```k
import k;

annotation Marker {}

@Marker
class Target {
    public Target() {}
    public dummy() : int { return 0; }
}

test() : int {
    t : Target;
    anns : const k::Annotation?[]? = t.getClass().getAnnotations();
    if (anns == null) return 0;
    ann : const k::Annotation? = anns[0];
    if (ann == null) return 1;
    name : k::String(ann->getAnnotationType().getName());
    expected : k::String("Marker");
    if (name == expected) return 42;
    return 2;
}
```

---

## 11. Annotation visibility and scope

### Scope

Annotation types follow the same scoping rules as other type declarations:

- They can be declared at module (top-level) scope.
- They can be declared inside a namespace.
- They can be nested inside a class, struct, interface, or another annotation.

When nested, the annotation type is accessible via the enclosing type's
qualified name: `Outer::MyAnnotation`.

### Visibility

Annotation types support the same visibility specifiers as other aggregates:

| Specifier | Meaning |
|-----------|---------|
| `public` | Accessible from any scope. |
| `protected` | Accessible from the enclosing type and its subclasses. |
| `private` | Accessible only within the enclosing type. |

Top-level annotation types default to `public`.

Member variables default to `public` (like structs).

---

## 12. Allowed member types

### Summary table

| Type | As field | As array element | Notes |
|------|----------|------------------|-------|
| `int`, `bool`, `char`, `byte`, `short`, `long`, `float`, `double` | ✓ | ✓ | All primitive types. |
| `unsigned int`, `unsigned short`, `unsigned long`, `unsigned byte` | ✓ | ✓ | Unsigned variants. |
| `int[N]`, `char[N]`, etc. | ✓ | — | Sized arrays of primitives. |
| `const char[]` | ✓ | — | String-literal compatible. |
| Other `annotation` types | ✓ | — | Embedded by value. |
| `const AnnotationType?` | ✓ | ✓ | View to annotation — for polymorphic references. |
| `const AnnotationType?[]` | ✓ | — | Array of annotation views — for collections of annotations. |
| `class`, `struct` | ✗ | ✗ | Not allowed in annotations. |
| `T*`, `T!`, `T+`, `T&` | ✗ | ✗ | Indirection types (except `?` view to annotations) are not allowed. |

---

## 13. Inner enums

Annotation types can declare **inner enumeration types** in their body.
Inner enums provide a type-safe way to define a fixed set of named constants
for use as annotation field values.

### Declaring an inner enum

An inner enum is declared with the `enum` keyword inside the annotation body,
using the same syntax as top-level enums:

```k
annotation Severity {
    enum Level { LOW; MEDIUM; HIGH; };
    level : Level;
}
```

### Rules

| Rule | Description |
|------|-------------|
| **Syntax** | Standard enum syntax: `enum Name { ENTRY1; ENTRY2; ... };` |
| **Values** | Auto-incrementing from 0 unless explicitly assigned (`ENTRY = N;`). |
| **Scope** | The enum type is scoped to the annotation body. Fields within the same annotation can reference it by its short name (`Level`). |
| **Qualified access** | Enum entries are accessed with the qualified syntax: `Level::HIGH`. When applying the annotation, the enum name must be qualified: `@Severity(Level::HIGH)`. |
| **Multiple enums** | An annotation may declare more than one inner enum. |
| **As field type** | Inner enums can be used as member variable types, including array types (`ElementType[]`). |
| **Default values** | Fields with inner enum types can have defaults: `level : Level = Level::MEDIUM;`. |

### Examples

```k
annotation Config {
    enum Mode { FAST; SAFE; };
    enum Level { LOW; MEDIUM; HIGH; };
    mode : Mode;
    level : Level = Level::MEDIUM;
}

@Config(Mode::FAST, Level::HIGH)  // positional init
class HighPerf { /* ... */ }

@Config(Mode::SAFE)               // level defaults to MEDIUM
class SafeMode { /* ... */ }
```

Inner enums with array fields:

```k
annotation Target {
    enum ElementType { CLASS; INTERFACE; ANNOTATION; };
    value : ElementType[];
}

@Target({ElementType::CLASS, ElementType::INTERFACE})
annotation FlexMarker {}
```

---

## 14. Meta-annotations

**Meta-annotations** are annotations that can be applied to other annotation
type declarations to control their semantics. They configure how the compiler
processes, propagates, and emits annotation instances.

### Overview

Meta-annotations are themselves annotation types. They live in the K standard
library, in namespace `k::annotations` (module `k`, source file
`src/annotations.k`). Because every K module implicitly imports `k`, these
meta-annotations are always available.

The K standard library defines three meta-annotations:

| Meta-annotation | Purpose |
|-----------------|---------|
| [`@Retention`](#151-retentionpolicy) | Controls whether annotation instances are emitted into binary RTTI or kept only in the compiler model. |
| [`@Inherited`](#152-inherited) | Marks that an annotation propagates from base classes to derived classes. |
| [`@Target`](#153-targetelementtype) | Restricts which kinds of program elements an annotation can be applied to. |

### Self-referential and cross-referential annotations

Annotations can reference other annotations in the same module — including
themselves. The compiler creates all annotation type objects before any
resolver runs, which makes forward references and self-references possible:

```k
// Self-referential: @Retention applied to Retention itself
@Retention(Policy::RUNTIME)
@Target({ElementType::ANNOTATION})
annotation Retention {
    enum Policy { SOURCE; RUNTIME; };
    policy : Policy = Policy::RUNTIME;
}
```

This is exactly how the standard library's meta-annotations are defined.

---

## 15. Meta-annotation reference

### 15.1. `@Retention(Policy)`

Controls whether annotation instances are retained only in the compiler's
internal model or are also synthesized into the compiled binary as RTTI
metadata.

**Declared in:** `k::annotations::Retention`

```k
@Retention(Policy::RUNTIME)
@Target({ElementType::ANNOTATION})
annotation Retention {
    enum Policy { SOURCE; RUNTIME; };
    policy : Policy = Policy::RUNTIME;
}
```

#### `Retention.Policy`

| Entry | Value | Description |
|-------|-------|-------------|
| `SOURCE` | 0 | The annotation is kept in the compiler model only. It is **not** emitted into the generated binary — no RTTI constant is created. Useful for compile-time-only metadata (e.g. `@Target` itself). |
| `RUNTIME` | 1 | The annotation is retained in the binary as an RTTI constant global, accessible via reflection at runtime through `getAnnotations()`. This is the **default** when `@Retention` is absent. |

#### Rules

| Rule | Description |
|------|-------------|
| **Default** | If `@Retention` is not present on an annotation type, the policy is `RUNTIME`. |
| **Applicability** | `@Retention` can only be applied to annotation types (`@Target({ElementType::ANNOTATION})`). |
| **Model visibility** | Regardless of the retention policy, annotation instances are **always** present in the compiler model. `SOURCE` retention only suppresses binary emission. |
| **Effect on RTTI** | With `RUNTIME` retention, the compiler generates an LLVM global constant for each annotation instance and includes it in the `annotations` field of the `Class`, `Interface`, or `AnnotationType` RTTI descriptor. With `SOURCE` retention, no global is generated and `getAnnotations()` will not return the annotation. |

#### Examples

```k
// Annotation with default RUNTIME retention — visible at runtime
annotation Marker {}

@Marker
class Foo { /* ... */ }
// Foo's RTTI includes @Marker in getAnnotations()
```

```k
import k;

// Annotation with SOURCE retention — compile-time only
@k::annotations::Retention(Policy::SOURCE)
annotation CompileOnly {}

@CompileOnly
class Bar { /* ... */ }
// Bar's RTTI does NOT include @CompileOnly
```

---

### 15.2. `@Inherited`

Marks that an annotation type is automatically inherited by subclasses.

**Declared in:** `k::annotations::Inherited`

```k
@Retention(Policy::RUNTIME)
@Target({ElementType::ANNOTATION})
annotation Inherited {}
```

#### Rules

| Rule | Description |
|------|-------------|
| **Propagation** | When a class carries an `@Inherited` annotation, every **direct or indirect subclass** implicitly receives the same annotation instance — including its field values. |
| **Override** | If a derived class explicitly applies the same annotation type, the explicit instance **replaces** the inherited one. |
| **Interface exclusion** | `@Inherited` has **no effect** on interface implementation. Annotations on interfaces are never propagated to implementing classes. |
| **Annotation-only** | `@Inherited` can only be applied to annotation types (`@Target({ElementType::ANNOTATION})`). |
| **Marker** | `@Inherited` is a marker annotation — it has no fields. |
| **Retention interaction** | Inheritance propagation happens at the model level, before RTTI emission. A `@Retention(Policy::SOURCE)` annotation that is also `@Inherited` will propagate in the model but will not appear in the binary RTTI of either base or derived class. |

#### Examples

```k
import k;

@k::annotations::Inherited
annotation Version { value : int; }

@Version(3)
class Base { /* ... */ }

class Derived : Base { /* ... */ }
// Derived automatically has @Version(3)

@Version(5)
class Override : Base { /* ... */ }
// Override has @Version(5), not @Version(3)
```

Multi-level propagation:

```k
import k;

@k::annotations::Inherited
annotation Tag { value : int; }

@Tag(10)
class A { /* ... */ }
class B : A { /* ... */ }   // inherits @Tag(10)
class C : B { /* ... */ }   // inherits @Tag(10) from B

@Tag(99)
class D : B { /* ... */ }   // overrides with @Tag(99)
class E : D { /* ... */ }   // inherits @Tag(99) from D
```

---

### 15.3. `@Target({ElementType::…})`

Restricts the kinds of program elements to which an annotation type may be
applied. The compiler enforces this constraint at compile time.

**Declared in:** `k::annotations::Target`

```k
@Retention(Policy::RUNTIME)
@Target({ElementType::ANNOTATION})
annotation Target {
    enum ElementType { CLASS; INTERFACE; ANNOTATION; };
    value : ElementType[];
}
```

#### `Target.ElementType`

| Entry | Value | Description |
|-------|-------|-------------|
| `CLASS` | 0 | A `class` declaration. |
| `INTERFACE` | 1 | An `interface` declaration. |
| `ANNOTATION` | 2 | An `annotation` type declaration. |

#### Rules

| Rule | Description |
|------|-------------|
| **Absent `@Target`** | If `@Target` is not present on an annotation type, the annotation can be applied to **any** supported element type (class, interface, or annotation). |
| **Enforcement** | When `@Target` is present, applying the annotation to an element type not in the `value` array causes a compile-time error (`0x003C`). |
| **Multiple targets** | The `value` array can contain any combination of `ElementType` entries: `@Target({ElementType::CLASS, ElementType::INTERFACE})`. |
| **Self-applicable** | `@Target` is itself restricted to `ANNOTATION` targets (it can only be applied to annotation types). |

#### Examples

```k
import k;

// Only applicable to classes
@k::annotations::Target({ElementType::CLASS})
annotation ClassOnly {}

@ClassOnly
class Good { /* ... */ }       // OK

// @ClassOnly
// interface Bad { /* ... */ }  // ERROR 0x003C: @Target restricts to [CLASS]
```

```k
import k;

// Applicable to classes and interfaces, but not annotations
@k::annotations::Target({ElementType::CLASS, ElementType::INTERFACE})
annotation Documented {}

@Documented
class MyClass { /* ... */ }    // OK

@Documented
interface MyIface { /* ... */ } // OK

// @Documented
// annotation MyAnn {}          // ERROR 0x003C: @Target restricts to [CLASS, INTERFACE]
```

### Combining meta-annotations

Meta-annotations can be combined on a single annotation type to define
precise semantics:

```k
import k;

// Class-only, inherited, emitted at runtime
@k::annotations::Target({ElementType::CLASS})
@k::annotations::Inherited
@k::annotations::Retention(Policy::RUNTIME)
annotation Versioned { version : int; }

@Versioned(3)
class Base { /* ... */ }

class Derived : Base { /* ... */ }
// Derived inherits @Versioned(3) and it appears in runtime RTTI
```

---

## 16. Library export

Annotation types are **exported through `.kdi` files** like any other
aggregate type:

- The annotation type's name, fully qualified name, visibility, and member
  layout are included in the KDI descriptor.
- The KDI aggregate kind for annotations is `annotation_`.
- Annotation instances attached to exported classes and interfaces are
  serialized in the RTTI metadata.
- Consumer modules can apply and read annotations defined in imported
  libraries.

This means:

- A library can declare `annotation Deprecated {}` and export it.
- A consumer module can `import` the library and use `@Deprecated` on its
  own classes.
- A consumer can read annotations defined in the library from the RTTI of
  the library's exported classes.

---

## 17. Examples

### Simple marker annotation

```k
annotation Serializable {}

@Serializable
class Config {
    public Config() {}
    public load() : int { return 0; }
}
```

### Annotation with primitive fields

```k
annotation Version {
    major : int;
    minor : int;
    patch : int = 0;
}

@Version(2, 1)
class Api {
    public Api() {}
    public dummy() : int { return 0; }
}
// major=2, minor=1, patch=0 (default)
```

### Annotation with string field

```k
annotation Description {
    text : const char[];
}

@Description("Main entry point for the application")
class App {
    public App() {}
    public dummy() : int { return 0; }
}
```

### Annotation with computed accessor

```k
annotation Range {
    min : int;
    max : int;

    span() : int { return max - min; }
}

@Range{.min = 0, .max = 100}
class Score {
    public Score() {}
    public dummy() : int { return 0; }
}
```

### Nested annotation

```k
annotation Author {
    name : const char[];
}

annotation Metadata {
    author : Author;
    version : int;
}

@Metadata{.author = @Author("Alice"), .version = 3}
class Document {
    public Document() {}
    public dummy() : int { return 0; }
}
```

### Annotation array (polymorphic)

```k
annotation Constraint {
    id : int;
}

annotation Validators {
    rules : const Constraint?[];
}

@Validators({@Constraint(1), @Constraint(2), @Constraint(3)})
class Form {
    public Form() {}
    public dummy() : int { return 0; }
}
```

### Multiple annotations with different values

```k
annotation Priority {
    level : int;
}

annotation Label {
    text : const char[];
}

@Priority(5) @Label("critical")
class CriticalTask {
    public CriticalTask() {}
    public dummy() : int { return 0; }
}
```

### Default construction vs explicit

```k
annotation Config {
    level : int = 1;
    verbose : bool = false;
}

@Config
class DefaultCfg {
    public DefaultCfg() {}
    public dummy() : int { return 0; }
}
// level=1, verbose=false

@Config(5, true)
class CustomCfg {
    public CustomCfg() {}
    public dummy() : int { return 0; }
}
// level=5, verbose=true
```

### Same annotation type, different values on different classes

```k
annotation Version {
    major : int;
    minor : int;
}

@Version(1, 0)
class ApiV1 {
    public ApiV1() {}
    public dummy() : int { return 0; }
}

@Version(2, 3)
class ApiV2 {
    public ApiV2() {}
    public dummy() : int { return 0; }
}
// ApiV1 and ApiV2 share the Version annotation type but have distinct instances.
```

---

## 18. Grammar

### Annotation type declaration

```
AnnotationDecl:
    { AnnotationDef } { Specifier } 'annotation' Identifier [ ':' BaseClause ] '{' { Declaration } '}'
```

Annotation types are parsed as a special case of `AggregateDecl` where the
aggregate keyword is `annotation`. They share the same specifier, base
clause, and body grammar as structs and classes. The body may contain
variable declarations, method declarations, and enum declarations.

```
AnnotationBody:
    { EnumDecl | VariableDecl | FunctionDecl }
```

### Annotation application

```
AnnotationDef:
    '@' QualifiedIdentifier
    | '@' QualifiedIdentifier '(' [ ExpressionList ] ')'
    | '@' QualifiedIdentifier DesignatedBraceInitList
    | '@' QualifiedIdentifier BraceInitList

AnnotationDefList:
    { AnnotationDef }
```

Annotation applications appear before the aggregate declaration they
annotate:

```
AggregateDecl:
    { AnnotationDef } { Specifier } ( 'struct' | 'class' | 'interface' | 'annotation' )
    Identifier [ ':' BaseClause ] '{' { Declaration } '}'
```

---

## 19. Error reference

| Code | Phase | Condition |
|------|-------|-----------|
| `0x0024` | Model builder | Annotations applied to a struct (only classes, interfaces, and annotations are supported). |
| `0x003A` | Symbol resolver | Annotation type not found. |
| `0x003B` | Symbol resolver | Target is not an annotation type. |
| `0x003C` | Symbol resolver | `@Target` violation: annotation applied to an element type not in the allowed list. Error message includes the annotation name, the disallowed element kind, and the allowed set. |
| `0x0080` | Parser | Missing annotation type name after `@`. |
| `0x0081` | Parser | Missing `)` after annotation arguments. |

---

*See also:* [Classes and Virtuality](../structs/classes.md) · [Interfaces](../structs/interfaces.md) · [Designated Struct Initializers](../structs/designated-init.md) · [RTTI Types](../../stdlib/rtti.md) · [Object](../../stdlib/object.md)
