# KDI CBOR Encoding Schema — v0.1

## Overview

A `.kdi` file is a **single CBOR item** at the top level: a definite or
indefinite-length CBOR map (major type 5).

Conventions used in this document:
* `map { key: type }` — a CBOR map with text-string keys.
* `uint`  — CBOR unsigned integer (major type 0).
* `int`   — CBOR integer, positive (major type 0) or negative (major type 1).
* `text`  — CBOR text string (major type 3).
* `bool`  — CBOR simple value `true` (0xf5) or `false` (0xf4).
* `array[T]` — CBOR array (major type 4) of items of type T.
* `?T`    — optional: the key is omitted from the map when the value is absent
            or is the zero/false default.
* `enum(N)` — encoded as a CBOR unsigned integer equal to the enum index.

---

## Top-level map

```
{
  "header"  : Header,
  "types"   : TypeTable,
  "unit"    : Unit
}
```

The keys `"schema_major"` and `"schema_minor"` are **not** at the top level;
they reside inside `Header` (first two pairs of the Header map).

---

## Header

```
Header = {
  "schema_major"  : uint,     -- MUST be 0
  "schema_minor"  : uint,     -- MUST be 1
  "module_name"   : text,
  "lib_base"      : text,
  "lib_path"      : text,
  "target_triple" : text,
  "compiler_ver"  : text,
  ?"dependencies" : array[text]  -- canonical module names of direct imports;
                                 -- omitted when the module imports nothing
}
```

---

## TypeTable

```
TypeTable = {
  "aggregates" : array[AggregateTypeEntry],
  ?"enums"     : array[EnumTypeEntry]
}

AggregateTypeEntry = {
  "fq_name"      : text,
  "mangled_name" : text
}

EnumTypeEntry = {
  "fq_name"      : text
}
```

---

## Unit

```
Unit = {
  "name"    : text,
  "root_ns" : Namespace
}
```

---

## Namespace

```
Namespace = {
  "name"       : text,
  "fq_name"    : text,
  ?"doc"       : DocBlock,
  "aggregates" : array[Aggregate],
  "enums"      : array[Enum],
  ?"aliases"   : array[Alias],
  "functions"  : array[Function],
  "variables"  : array[Variable],
  "namespaces" : array[Namespace],
  ?"template_defs" : array[TemplateDef]
}
```

---

## Alias

An exported `alias` (soft) or `typedef` (strong) declaration. Emitted in both
`Namespace` and `Aggregate` maps, and omitted when the scope declares none.

```
Alias = {
  "name"            : text,
  "fq_name"         : text,
  "visibility"      : Visibility,
  ?"is_strong"      : bool,    -- present and true only for 'typedef'
  ?"target_type"    : Type,    -- always present for a 'typedef'
  ?"target_fq_name" : text,    -- soft alias targeting a function or a variable
  ?"is_template"    : bool,    -- present and true for a parameterised alias
  ?"params"         : array[TemplateParam],  -- parameterised alias only
  ?"source"         : text,    -- raw K source, parameterised alias only
  ?"doc"            : DocBlock
}
```

Block-local and `private` aliases are never exported. A `typedef` never changes
the mangling of a symbol: mangled names always use the fully resolved
(alias-free) type.

A **parameterised** alias (`template<typename T> alias Vec : Array<T, 16>;`)
renames a family of types. Its renamed type contains template parameter
placeholders and therefore cannot be expressed as a resolved `Type`: it is
round-tripped as raw K source text in `source`, exactly like a `TemplateDef`,
and re-parsed by the importing compiler. `target_type` and `target_fq_name` are
then absent; `params` is informative only (documentation tooling), the compiler
rebuilds the parameters from `source`.

---

## Documentation payloads

```
DocBlock = {
  "brief"       : text,
  "description" : text
}

DocFunction = DocBlock + {
  ?"params"          : array[{ "name": text, "description": text }],
  ?"returns"         : text,
  ?"throws"          : array[{ "type_name": text, "description": text }],
  ?"template_params" : array[{ "name": text, "description": text }],
  ?"tags"            : array[{ "tag": text, "value": text }]
}
```

---

## Type encoding

Types are encoded as inline CBOR maps, distinguished by the `"kind"` key.

```
Type =
  { "kind": "void"  }
| { "kind": "bool"  }
| { "kind": "char"  }
| { "kind": "int",   "bits": uint, "signed": bool }
| { "kind": "float", "bits": uint }
| { "kind": "ref",    "inner": Type }
| { "kind": "ptr",    "inner": Type }
| { "kind": "link",   "inner": Type }
| { "kind": "view", "inner": Type }
| { "kind": "const",  "inner": Type }
| { "kind": "array",  "elem": Type }
| { "kind": "sized_array", "elem": Type, "size": uint }
| { "kind": "callable", "addresser": text, "ret": Type?, "params": array[Type],
|   "throws": array[Type]?, "member_of": text? }
| { "kind": "aggregate", "fq_name": text }
| { "kind": "enum", "fq_name": text }
| { "kind": "alias", "fq_name": text }
| { "kind": "template_param", "name": text }
| { "kind": "generic_ref", "name": text, "args": array[Type] }
```

`alias` references an exported strong alias (`typedef`) by its fully-qualified
K name. Only a strong alias is referenced this way: it is nominally distinct
from the type it renames. A soft alias (`alias`) is fully transparent and is
always encoded as the type it renames.

`template_param` references a template parameter by name (e.g. `T`) as it
appears inside an uninstantiated template's own declaration.

`generic_ref` references another named type applied with template type
arguments, as it appears inside an uninstantiated template's own declaration
(e.g. the member type `MultiSlot<T>` inside `template<typename T> class
Vector`). Unlike `aggregate`, the referenced name may not resolve to a
concrete aggregate outside of an instantiation context — it can name another
template (own or imported) that has not been (and may never be)
instantiated with concrete arguments. Each entry in `args` is itself a full
`Type`, so nested template-parameter references (e.g. `Node<T>` inside
`List<T>`) are preserved structurally. Only type arguments are represented;
a non-type (value) argument in such a nested reference falls back to a
`void` placeholder entry.

---

## Visibility (enum)

```
0 = public
1 = protected
```

Encoded as a CBOR uint.

---

## Param

```
Param = {
  "name" : text,
  "type" : Type
}
```

---

## Variable

```
Variable = {
  "name"         : text,
  "fq_name"      : text,
  "visibility"   : Visibility,
  "type"         : Type,
  ?"is_const"    : bool,       -- omitted when false
  "mangled_name" : text,
  ?"doc"         : DocBlock
}
```

---

## Function

```
Function = {
  "name"         : text,
  "fq_name"      : text,
  "visibility"   : Visibility,
  ?"is_static"   : bool,
  "return_type"  : Type,
  "params"       : array[Param],
  "mangled_name" : text,
  "llvm_def"     : text,          -- mandatory LLVM IR prototype, e.g.
                                  -- "declare i32 @_ZN3foo3barEi(i32)"
  ?"doc"         : DocFunction
}
```

---

## Method

```
Method = {
  "name"             : text,
  "fq_name"          : text,
  "visibility"       : Visibility,
  ?"is_static"       : bool,
  ?"is_const_member" : bool,
  ?"is_virtual"      : bool,
  ?"is_abstract"     : bool,
  ?"is_final"        : bool,
  ?"vtable_slot"     : uint,   -- omitted when -1
  "return_type"      : Type,
  "params"           : array[Param],
  "mangled_name"     : text,
  "llvm_def"         : text,   -- mandatory LLVM IR prototype (with implicit 'this')
  ?"doc"             : DocFunction
}
```

---

## Constructor

```
Constructor = {
  "visibility"           : Visibility,
  ?"is_copy_constructor" : bool,
  ?"is_defaulted"        : bool,
  ?"is_deleted"          : bool,
  "params"               : array[Param],
  "mangled_name"         : text,     -- C1 variant
  "mangled_name_c2"      : text,     -- C2 variant
  "llvm_def"             : text,     -- mandatory LLVM IR prototype of C1 variant
  ?"doc"                 : DocFunction
}
```

---

## Destructor

```
Destructor = {
  "visibility"              : Visibility,
  ?"is_virtual"             : bool,
  ?"is_compiler_generated"  : bool,
  "mangled_name"            : text,  -- D1 variant
  "mangled_name_d2"         : text,  -- D2 variant
  "llvm_def"                : text,  -- mandatory LLVM IR prototype of D1 variant
  ?"doc"                    : DocFunction
}
```

---

## VtableSlot

```
VtableSlot = {
  "slot_index"       : uint,
  "introducing_func" : text,    -- fq_name
  "override_symbol"  : text,    -- mangled name; empty string if abstract
  ?"is_abstract"     : bool
}
```

---

## Thunk

```
Thunk = {
  "slot_index"       : uint,
  "real_func_symbol" : text,
  "this_adjustment"  : int,
  ?"needs_thunk"     : bool
}
```

---

## SecondaryVtable

```
SecondaryVtable = {
  "base_fq_name"  : text,
  "base_offset"   : uint,
  "vtable_symbol" : text,
  "thunks"        : array[Thunk]
}
```

---

## Vtable

```
Vtable = {
  "vtable_symbol" : text,
  "rtti_symbol"   : text,
  "llvm_def"      : text,           -- mandatory LLVM IR declaration of vtable global
  "slots"         : array[VtableSlot],
  "secondary"     : array[SecondaryVtable]
}
```

---

## Base

```
Base = {
  "fq_name"          : text,
  "visibility"       : Visibility,
  ?"is_virtual"      : bool,
  "base_field_index" : int,    -- -1 for virtual bases
  "byte_offset"      : uint
}
```

---

## LayoutField

Layout fields are discriminated by the `"kind"` key.

```
LayoutField =
  { "kind": "member",
    "llvm_field_index" : uint,
    "name"             : text,
    "fq_name"          : text,
    "visibility"       : Visibility,
    "type"             : Type,
    ?"is_const"        : bool,
    "mangled_name"     : text }

| { "kind": "vptr",
    "llvm_field_index" : uint,
    "vtable_symbol"    : text }

| { "kind": "vptr_secondary",
    "llvm_field_index" : uint,
    "base_fq_name"     : text,
    "vtable_symbol"    : text }

| { "kind": "base_subobject",
    "llvm_field_index" : uint,
    "base_fq_name"     : text }

| { "kind": "vbptr",
    "llvm_field_index" : uint,
    "vbase_fq_name"    : text }

| { "kind": "vbase_subobject",
    "llvm_field_index" : uint,
    "vbase_fq_name"    : text }

| { "kind": "parent_ref",
    "llvm_field_index" : uint,
    "parent_fq_name"   : text }

| { "kind": "opaque_block",
    "llvm_field_index" : uint,
    "field_count"      : uint,
    "size_bits"        : uint }
```

---

## Aggregate

```
Aggregate = {
  "kind"           : "struct" | "class" | "interface",
  "name"           : text,
  "fq_name"        : text,
  "mangled_name"   : text,
  "visibility"     : Visibility,
  ?"is_abstract"   : bool,
  ?"is_final"      : bool,
  ?"is_const_struct": bool,
  ?"is_static_nested": bool,
  ?"enclosing_fq_name": text,        -- fq_name of enclosing aggregate; omitted
                                     -- when not nested
  "bases"          : array[Base],
  "layout"         : array[LayoutField],
  "constructors"   : array[Constructor],
  ?"destructor"    : Destructor,
  "methods"        : array[Method],
  "static_vars"    : array[Variable],
  ?"vtable"        : Vtable,
  ?"aliases"       : array[Alias],
  "nested"         : array[Aggregate],
  "llvm_def"       : text,          -- mandatory LLVM IR struct type definition,
                                    -- e.g. "%struct.ns.Counter = type { i32*, i32 }"
  ?"doc"           : DocBlock
}
```

When embedded under a generic `TemplateDef.aggregate_signature`, this payload is
declaration-only: ABI fields such as `mangled_name` and `llvm_def` may be empty,
and `layout` only needs the accessible named members required for type-checking.

---

## TemplateDef

```
TemplateDef = {
  "name"        : text,
  "fq_name"     : text,
  "entity_kind" : text,
  "visibility"  : text,
  ?"is_generic" : bool,
  "params"      : array[TemplateParam],
  "source"      : text,
  ?"aggregate_signature" : Aggregate,
  ?"function_signature"  : Function
}
```

Rules:

* Classic (non-generic) templates serialize their full `source` (the
  authoritative form used by the compiler to re-parse/re-instantiate the
  template cross-module) **and** a declaration-only `aggregate_signature` or
  `function_signature` matching `entity_kind`. The structured signature lets
  documentation tooling (e.g. `kditool docgen`) render a template's
  fields/constructors/methods the same way as a regular (non-template)
  aggregate/function — with template-parameter-dependent types tagged as
  `template_param` / `generic_ref` — instead of only a raw source dump.
* Generic templates set `is_generic = true`, serialize an empty `source`, and
  provide exactly one declaration signature matching `entity_kind`.
* Union templates are always classic (no `union_signature` payload exists);
  they only ever serialize `source`.

---

## Enum

```
Enum = {
  "name"                : text,
  "fq_name"             : text,
  "visibility"          : Visibility,
  "underlying_type"     : Type,
  ?"object_type"         : Type,     -- aggregate ref for object-backed enums
  ?"object_table_symbol" : text,
  ?"base_fq_name"        : text,
  "entries"             : array[EnumEntry],
  ?"doc"                : DocBlock
}

EnumEntry = {
  "name"                : text,
  "value"               : int,
  ?"is_default"          : bool,
  ?"object_init_members" : array[ObjectInitMember]
}

ObjectInitMember = {
  "name"                : text,
  "value"               : int
}
```

Compatibility:

- Integer-backed enums omit typed metadata fields.
- Older payloads (without typed-enum fields) remain valid and must be accepted.

---

## Union

```
Union = {
  "name"                  : text,
  "fq_name"               : text,
  "mangled_name"          : text,
  "visibility"            : Visibility,
  ?"base_union"           : text,      -- base union for union inheritance
  ?"polymorphic_base"     : text,      -- base class or interface for polymorphic unions
  ?"llvm_def"             : text,
  "alternatives"          : array[UnionAlternative],
  ?"template_origin"      : TemplateOrigin,
  ?"doc"                  : DocBlock
}

UnionAlternative = {
  "name"      : text,
  "type"      : Type,
  ?"is_const" : bool
}
```

