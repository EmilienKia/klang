# KDI — K Description Interface — Abstract Schema

Version: **0.1** (schema_major=0, schema_minor=1)

## Overview

A `.kdi` file describes the public/protected API of a compiled K library (`.so`
or `.a`).  It contains enough information for a consumer module to:

* Resolve types and call signatures of exported symbols.
* Inherit from exported aggregates (`struct` / `class` / `interface`), including
  reconstructing the exact LLVM struct layout needed to inject base sub-objects.
* Generate correct vtable layouts, vptr stores and this-adjustment thunks.
* Link against the exported binary symbols.

The canonical on-disk format is **CBOR** (RFC 8949), file extension `.kdi`.
A human-readable **JSON** equivalent can be produced with the `kdi` tool,
extension `.kdi.json`.

---

## Top-level structure

```
KdiFile {
  header   : KdiHeader
  types    : KdiTypeTable          -- all aggregate types referenced anywhere
  unit     : KdiUnit               -- the module being described
}
```

---

## KdiHeader

```
KdiHeader {
  schema_major : uint              -- must be 0
  schema_minor : uint              -- must be 1
  module_name  : string            -- e.g. "math::utils"
  lib_base     : string            -- e.g. "math.utils"  (:: → .)
  lib_path     : string            -- relative path to the .so/.a (informational)
  target_triple: string            -- e.g. "x86_64-pc-linux-gnu"
  compiler_ver : string            -- klangc version string
}
```

---

## KdiTypeTable

A flat list of all aggregate types referenced in this KDI.
Primitive types and indirection types are encoded inline where they appear
(see *Type encoding* below) and do not appear in this table.

```
KdiTypeTable {
  aggregates : [KdiAggregateType]
}

KdiAggregateType {
  fq_name      : string            -- fully-qualified K name, e.g. "math::Vec3"
  mangled_name : string            -- LLVM struct type name
}
```

---

## Type encoding (inline, not in type table)

Types are encoded as tagged objects wherever they appear (parameter, return
type, field type, …).

```
KdiType = one of:
  { kind: "void" }
  { kind: "bool" }
  { kind: "int",   bits: uint, signed: bool }
  { kind: "float", bits: uint }
  { kind: "char" }
  { kind: "ref",    inner: KdiType }
  { kind: "ptr",    inner: KdiType }
  { kind: "link",   inner: KdiType }     -- ~ operator
  { kind: "pinned", inner: KdiType }     -- ^ operator
  { kind: "const",  inner: KdiType }
  { kind: "array",  elem: KdiType }
  { kind: "sized_array", elem: KdiType, size: uint }
  { kind: "fn_ref",  ret: KdiType, params: [KdiType] }
  { kind: "aggregate", fq_name: string }  -- reference into KdiTypeTable
```

---

## KdiUnit

```
KdiUnit {
  name       : string              -- same as header.module_name
  root_ns    : KdiNamespace
}
```

---

## KdiNamespace

```
KdiNamespace {
  name       : string              -- short name ("" for root)
  fq_name    : string
  namespaces : [KdiNamespace]      -- nested namespaces
  aggregates : [KdiAggregate]      -- struct / class / interface
  functions  : [KdiFunction]       -- global and static functions (PUBLIC only)
  variables  : [KdiVariable]       -- global and static variables (PUBLIC only)
}
```

---

## KdiAggregate

Describes a `struct`, `class` or `interface`.

```
KdiAggregate {
  -- Identity
  kind         : "struct" | "class" | "interface"
  name         : string            -- short name
  fq_name      : string
  mangled_name : string

  -- Modifiers
  visibility   : "public" | "protected"
  is_abstract  : bool
  is_final     : bool
  is_const_struct : bool

  -- Inheritance
  bases        : [KdiBase]

  -- Physical layout (LLVM field order, ALL fields including synthetic)
  layout       : [KdiLayoutField]

  -- Public/protected API
  constructors : [KdiConstructor]
  destructor   : KdiDestructor?
  methods      : [KdiMethod]
  static_vars  : [KdiVariable]

  -- Vtable (class/interface only, null for struct)
  vtable       : KdiVtable?

  -- Nested aggregates
  nested       : [KdiAggregate]
}
```

---

## KdiBase

```
KdiBase {
  fq_name          : string        -- resolved base type
  visibility       : "public" | "protected"
  is_virtual       : bool          -- true for diamond virtual bases
  base_field_index : uint          -- index of __base_X__ in the LLVM struct
                                   -- (-1 for virtual bases: no embedded field)
  byte_offset      : uint          -- byte offset in the derived layout
}
```

---

## KdiLayoutField

The COMPLETE LLVM field list in declaration order.  Every field is present,
whether it is a named K member, a synthetic compiler field, or an opaque
private block.

```
KdiLayoutField = one of:

  -- Named, accessible member (public or protected)
  { kind: "member",
    name: string,  fq_name: string,  visibility: "public"|"protected",
    llvm_field_index: uint,  type: KdiType,  is_const: bool,
    mangled_name: string }

  -- Primary vptr (first field in classes with vtable)
  { kind: "vptr",
    llvm_field_index: uint,
    vtable_symbol: string }         -- mangled name of the vtable global

  -- Secondary vptr for a non-primary base
  { kind: "vptr_secondary",
    llvm_field_index: uint,
    base_fq_name: string,
    vtable_symbol: string }

  -- Embedded base sub-object (non-virtual base)
  { kind: "base_subobject",
    llvm_field_index: uint,
    base_fq_name: string }

  -- Pointer-to-virtual-base slot
  { kind: "vbptr",
    llvm_field_index: uint,
    vbase_fq_name: string }

  -- Embedded virtual base sub-object (in the "collector" class)
  { kind: "vbase_subobject",
    llvm_field_index: uint,
    vbase_fq_name: string }

  -- Implicit parent reference (non-static inner aggregates)
  { kind: "parent_ref",
    llvm_field_index: uint,
    parent_fq_name: string }

  -- One or more consecutive private / hidden fields collapsed into a block
  { kind: "opaque_block",
    llvm_field_index: uint,    -- index of FIRST field in this block
    field_count: uint,         -- number of LLVM fields in the block
    size_bits: uint }          -- total bit-size of the block
```

---

## KdiVtable

```
KdiVtable {
  vtable_symbol  : string          -- mangled name of primary vtable global
  rtti_symbol    : string          -- mangled name of RTTI global
  slots          : [KdiVtableSlot]
  secondary      : [KdiSecondaryVtable]
}

KdiVtableSlot {
  slot_index       : uint
  introducing_func : string        -- fq_name of the introducing function
  override_symbol  : string        -- mangled name of the concrete implementation
                                   -- (empty string if abstract)
  is_abstract      : bool
}

KdiSecondaryVtable {
  base_fq_name   : string
  base_offset    : uint            -- byte offset of base in derived layout
  vtable_symbol  : string          -- mangled name of secondary vtable global
                                   -- (generated by derived-class compiler)
  thunks         : [KdiThunk]
}

KdiThunk {
  slot_index      : uint
  real_func_symbol: string         -- mangled name of concrete override
  this_adjustment : int            -- bytes to subtract from 'this'
  needs_thunk     : bool
}
```

---

## KdiFunction

```
KdiFunction {
  name         : string
  fq_name      : string
  visibility   : "public" | "protected"
  is_static    : bool
  return_type  : KdiType
  params       : [KdiParam]
  mangled_name : string
}

KdiParam {
  name : string
  type : KdiType
}
```

---

## KdiMethod  (member function)

```
KdiMethod {
  name           : string
  fq_name        : string
  visibility     : "public" | "protected"
  is_static      : bool
  is_const_member: bool
  is_virtual     : bool
  is_abstract    : bool
  is_final       : bool
  vtable_slot    : int             -- -1 if not virtual
  return_type    : KdiType
  params         : [KdiParam]     -- excluding 'this'
  mangled_name   : string
}
```

---

## KdiConstructor

```
KdiConstructor {
  visibility         : "public" | "protected"
  is_copy_constructor: bool
  is_defaulted       : bool
  is_deleted         : bool
  params             : [KdiParam]
  mangled_name       : string      -- C1 variant
  mangled_name_c2    : string      -- C2 (base-subobject) variant
}
```

---

## KdiDestructor

```
KdiDestructor {
  visibility     : "public" | "protected"
  is_virtual     : bool
  is_compiler_generated : bool
  mangled_name   : string          -- D1 variant
  mangled_name_d2: string          -- D2 (base-subobject) variant
}
```

---

## KdiVariable  (global, static, or member)

```
KdiVariable {
  name         : string
  fq_name      : string
  visibility   : "public" | "protected"
  type         : KdiType
  is_const     : bool
  mangled_name : string
}
```

---

## CBOR encoding rules

* The top-level item is a CBOR **map** (major type 5).
* All string keys are **text strings** (major type 3).
* Booleans use CBOR simple values `true` / `false`.
* Integers are unsigned (major type 0) unless the value may be negative
  (e.g. `this_adjustment`, `vtable_slot`), in which case signed integers
  (major type 1) are used.
* Optional fields that are absent are **omitted** from the map (not encoded
  as `null`).
* Arrays use CBOR **definite-length arrays** (major type 4).
* The schema version is encoded as the very first two key/value pairs in the
  top-level map: `"schema_major"` and `"schema_minor"`.

