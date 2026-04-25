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
  "aggregates" : array[Aggregate],
  "enums"      : array[Enum],
  "functions"  : array[Function],
  "variables"  : array[Variable],
  "namespaces" : array[Namespace],
  ?"template_defs" : array[TemplateDef]
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
| { "kind": "fn_ref", "ret": Type, "params": array[Type] }
| { "kind": "aggregate", "fq_name": text }
| { "kind": "enum", "fq_name": text }
| { "kind": "template_param", "name": text }
```

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
  "mangled_name" : text
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
  "llvm_def"     : text           -- mandatory LLVM IR prototype, e.g.
                                  -- "declare i32 @_ZN3foo3barEi(i32)"
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
  "llvm_def"         : text    -- mandatory LLVM IR prototype (with implicit 'this')
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
  "llvm_def"             : text      -- mandatory LLVM IR prototype of C1 variant
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
  "llvm_def"                : text   -- mandatory LLVM IR prototype of D1 variant
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
  "nested"         : array[Aggregate],
  "llvm_def"       : text           -- mandatory LLVM IR struct type definition,
                                    -- e.g. "%struct.ns.Counter = type { i32*, i32 }"
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

* Classic templates serialize their full `source` and may omit signature fields.
* Generic templates set `is_generic = true`, serialize an empty `source`, and
  provide exactly one declaration signature matching `entity_kind`.

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
  "entries"             : array[EnumEntry]
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

