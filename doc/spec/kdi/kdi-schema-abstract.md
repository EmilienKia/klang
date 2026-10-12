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
  dependencies : [string]          -- canonical module names of direct imports
                                   -- (e.g. ["ival_lib", "aval_lib"]).  Used by
                                   -- consumers to load transitive KDIs.  Optional:
                                   -- omitted (empty) when the module has no imports.
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
  enums      : [KdiEnumType]        -- optional; omitted when empty
}

KdiAggregateType {
  fq_name      : string            -- fully-qualified K name, e.g. "math::Vec3"
  mangled_name : string            -- LLVM struct type name
}

KdiEnumType {
  fq_name      : string            -- fully-qualified K name, e.g. "math::Axis"
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
  { kind: "link",   inner: KdiType }     -- + operator
  { kind: "view",   inner: KdiType }     -- ? operator
  { kind: "owner",  inner: KdiType }     -- ! operator
  { kind: "const",  inner: KdiType }
  { kind: "array",  elem: KdiType }
  { kind: "sized_array", elem: KdiType, size: uint }
  { kind: "callable", addresser: string, ret: KdiType?, params: [KdiType],
    throws: [KdiType]?, member_of: string? }
    -- A K callable type. "addresser" is one of "none" (a bare prototype,
    -- which only ever appears as the target of an alias), "ptr" (*),
    -- "view" (?), "link" (+) or "ref" (&). "ret" is absent for a callable
    -- returning nothing. "throws" carries the declared checked-exception
    -- set (absent or empty means "throws nothing") and is part of the type
    -- identity. "member_of" is the fully-qualified name of the owner
    -- aggregate of an unbound member-function reference.
  { kind: "aggregate", fq_name: string }  -- reference into KdiTypeTable
  { kind: "enum", fq_name: string }       -- reference into KdiTypeTable.enums
  { kind: "alias", fq_name: string }      -- reference to an exported typedef
    -- Only a strong alias ('typedef') is referenced this way: it is nominally
    -- distinct from the type it renames, so the distinction has to survive the
    -- round trip. A soft alias ('alias') is fully transparent and is always
    -- exported as the type it renames.
  { kind: "template_param", name: string } -- template-signature placeholder
  { kind: "erased_param" }                 -- erased stand-in of a generic type
    -- parameter (opaque pointer, `byte*` at the LLVM level) in the exported
    -- single synthesis of a generic aggregate/function
  { kind: "generic_ref", name: string, args: [KdiType] }
    -- Reference to another (possibly still-uninstantiated) named type applied
    -- with template type arguments, as it appears inside an uninstantiated
    -- template's own declaration (e.g. member type "MultiSlot<T>" inside
    -- "template<typename T> class Vector"). Only type arguments are
    -- represented; a value argument in such a nested reference falls back to
    -- a single "void" placeholder entry in `args`.
```

---

## KdiUnit

```
KdiUnit {
  name        : string              -- same as header.module_name
  annotations : [KdiAnnotation]     -- optional; omitted when empty
  root_ns     : KdiNamespace
}
```

---

## KdiNamespace

```
KdiNamespace {
  name       : string              -- short name ("" for root)
  fq_name    : string
  doc        : KdiDocBlock?        -- optional brief/description
  annotations : [KdiAnnotation]   -- optional; omitted when empty
  namespaces : [KdiNamespace]      -- nested namespaces
  aggregates : [KdiAggregate]      -- struct / class / interface / annotation
  enums      : [KdiEnum]
  aliases    : [KdiAlias]          -- exported 'alias' / 'typedef' declarations
  functions  : [KdiFunction]       -- global and static functions (PUBLIC only)
  variables  : [KdiVariable]       -- global and static variables (PUBLIC only)
  template_defs : [KdiTemplateDef] -- optional; omitted when empty
}
```

```
KdiAnnotation {
  name   : string                  -- resolved annotation type name as written
  source : string                  -- canonical applied annotation syntax (`@Name(...)`)
}
```

KDI annotation fields are additive within schema version 0.1. Older readers
that omit them load empty annotation lists; absent arrays decode as empty.
Producers must preserve declaration and source order. Imported namespace
annotations describe the imported declaration and must not become part of the
consuming unit's locally owned RTTI inventory. The anonymous unit root
namespace has no declaration annotations; module annotations belong only to
`KdiUnit.annotations`.

Annotations on `KdiAggregate` describe the aggregate declaration and must be
preserved when the aggregate is imported or re-exported, including declaration-
only template aggregate signatures.

---

## KdiAlias

An exported aliasing declaration (`alias` or `typedef`). An alias declares
nothing new: it is always replaced by the entity it renames, recursively, until
a non-alias entity is reached — no symbol is ever synthesised for it. It is
exported as written so that an importing module can use the alias name and, for
a strong alias, keep its nominal identity.

```
KdiAlias {
  name           : string          -- short name
  fq_name        : string          -- fully-qualified K name
  visibility     : KdiVisibility
  is_strong      : bool            -- true for 'typedef', false for 'alias'
  target_type    : KdiType?        -- the renamed type; always set for a typedef
  target_fq_name : string?         -- renamed entity, as written; set for a soft
                                   --   alias targeting a function or a variable
  is_template    : bool            -- true for a parameterised alias
  params         : [KdiTemplateParam]  -- parameterised alias only (informative)
  source         : string          -- raw K source of a parameterised alias
  origin_module  : string?         -- original lexical namespace, parameterised alias only
  doc            : KdiDocBlock?
}
```

Notes:
* Block-local and `private` aliases are never exported.
* A namespace cannot be aliased, so `target_fq_name` never names a namespace.
* A **parameterised** alias (`template<typename T> alias Vec : Array<T, 16>;`)
  denotes no type by itself: it is resolved at each use site by substituting the
  arguments into the renamed type. Its target carries template parameter
  placeholders, so it is round-tripped as raw K source text in `source` — like a
  `KdiTemplateDef` — and re-parsed by the importing compiler. `target_type` and
  `target_fq_name` are then unset.
* Parameterised aliases must retain their declaration namespace across import
  and re-export. `origin_module`, when present, identifies that namespace without
  a leading `::`; otherwise it is the namespace part of `fq_name`.
  Explicit arguments resolve at the use site, but defaults and non-parameter
  names in the substituted target resolve in the declaration scope.
  Qualified and unqualified uses of one alias specialization must yield the
  same canonical type. Imported aliases must not be replaced by duplicated
  aggregate or callable definitions.
* A `typedef` never changes the mangling of a symbol: mangled names always use
  the fully resolved (alias-free) type.

---

## Documentation payload

```
KdiDocBlock {
  brief       : string
  description : string
}

KdiDocFunction = KdiDocBlock + {
  params          : [ { name: string, description: string } ]
  returns         : string?
  throws          : [ { type_name: string, description: string } ]
  template_params : [ { name: string, description: string } ]
  tags            : [ { tag: string, value: string } ]
}
```

---

## KdiAggregate

Describes a `struct`, `class`, `interface` or annotation type.

```
KdiAggregate {
  kind         : "struct" | "class" | "interface" | "annotation"
  name         : string
  fq_name      : string
  visibility   : "public" | "protected"
  annotations  : [KdiAnnotation]?  -- annotations applied to this type
  ...                              -- aggregate layout, members and ABI declarations
}
```

When `KdiAggregate` is embedded inside a `KdiTemplateDef.aggregate_signature`, it
acts as a declaration-only signature carrier:

* `layout` contains only accessible named members needed for type-checking.
* `llvm_def`, `mangled_name`, and ABI-only fields may be empty.
* Method and constructor signatures may omit LLVM/mangling fields.

---

## KdiTemplateDef

```
KdiTemplateDef {
  name         : string
  fq_name      : string
  entity_kind  : "struct" | "class" | "interface" | "function"
  visibility   : "public" | "protected"
  is_generic   : bool?                 -- true for `generic<...>` declarations
  origin_module: string?               -- original lexical namespace, when different
  host_fq_name : string?               -- hosting aggregate for a member template
  params       : [KdiTemplateParam]
  source       : string                -- full source for classic templates and
                                       -- source-backed generic functions
  aggregate_signature : KdiAggregate?  -- declaration-only aggregate signature
  function_signature  : KdiFunction?   -- declaration-only function or method signature
}
```

Rules:

* Classic `template<...>` definitions export their full `source` (the
  authoritative form used by the compiler to re-parse/re-instantiate the
  template cross-module) **and** a declaration-only `aggregate_signature` or
  `function_signature` matching `entity_kind`. The structured signature lets
  documentation tooling document a template's fields/constructors/methods the
  same way as a regular (non-template) aggregate/function, with
  template-parameter-dependent types tagged as `template_param` /
  `generic_ref` instead of only a raw source dump.

* Re-parsed templates retain the original namespace identified by
  `origin_module` (or their declaring namespace when it is absent). Import
  lookup exposes them without flattening their declarations into the consumer
  root. Non-parameter names in signatures, callable components, and bodies
  resolve lexically in that namespace.
* `generic<...>` **aggregate** definitions export `is_generic = true`, MUST leave `source`
  empty and MUST provide `aggregate_signature`. The single erased synthesis of the aggregate
  is exported as a regular `KdiAggregate` (layout, mangled names, methods) whose types use
  `erased_param` for the erased type parameters; importers link against it instead of
  re-instantiating the definition.
* A public or protected `template<...>` **method** of a (non-template) aggregate is
  exported as a `KdiTemplateDef` with `entity_kind = "function"` and `host_fq_name`
  identifying its aggregate. It retains its full `source` and declaration visibility.
  Importers rebuild the method in that host, not as a namespace-level function,
  so derived classes can instantiate protected methods without exposing them publicly.
  Interface hosts retain interface declaration rules, including `default` methods.
  Concrete classic method instances are generated in the consumer with mergeable
  template linkage; the host's original imported methods remain external.
  Member templates of a source-backed template aggregate remain in that template's
  `source`; their concrete specializations MUST NOT be emitted again as hosted
  `KdiTemplateDef` entries. Signature-only generic aggregates remain represented by
  their `aggregate_signature`.
* A `generic<...>` **method** of a (non-template) aggregate is exported as a `KdiTemplateDef`
  with `entity_kind = "function"`, `host_fq_name` set to the fully-qualified name of the
  hosting aggregate, an empty `source` and a `function_signature`; its single erased
  synthesis is exported as a regular method of the host named `<name><generic>`.
* `generic<...>` **function** definitions export `is_generic = true` and keep their `source`
  (the importing module re-synthesises the single erased body); `function_signature` is only
  provided as a fallback when no source is available.
* A signature-only `generic<...>` definition MUST leave `source` empty and
  provide exactly one applicable signature field. Aggregate definitions use
  `aggregate_signature`; free functions and generic member methods use
  `function_signature`. A generic member method also sets `host_fq_name` and is
  imported as a method of that host.
* Same-named function template definitions must all be retained when their
  parameter lists differ. Deduplication must identify a definition, not only
  its name.
* Template `source` may include a private FFI declaration prelude in a
  compiler-generated namespace whose name begins with `__kdi_private_`.
  Such declarations are lexical dependencies of the originating template,
  not namespace-level exported API. Importers must preserve their native
  symbol names and private visibility, share identical declarations across
  templates from the same origin, and retain the definition scope when
  cloning template bodies. Consumer declarations must not replace these
  dependencies. This uses the existing `source` field; the schema remains `0.1`.
  Exporters must collect dependencies from actual symbol expressions using
  definition-site lexical lookup, including accessible enclosing namespaces
  and qualified declarations, not textual matches in comments or literals.
  References must identify their original target independently of consumer
  declarations and same-named native functions in other namespaces.
  Private native aliases, including producer declarations and dependency aliases
  in different definition scopes, may share one C symbol only
  with matching canonical parameter/return types, CString conversion flags and
  checked throws contracts. Code generation must reuse that symbol's compatible
  LLVM declaration, never silently rename an alias or accept an incompatible ABI.
  A consumer must not acquire definition privileges by naming, entering, or
  declaring a template in the generated dependency namespace.
  Dependency collection must retain the definition's original visibility rights;
  resolving a qualified name must not make an otherwise inaccessible private
  declaration available to that template.
  The prelude must use canonical K callable syntax (e.g. `*(byte*, byte*)`
  for a void-returning callback and `!(int):int` for an owned callback),
  preserving addressers, member owners, return types and checked `throws`
  contracts. FFI annotations must be fully qualified (`@k::ffi::Extern`,
  `@k::ffi::CString`); callback types must not be replaced with raw pointers.
  Root-qualified member-reference owners are valid K syntax (for example
  `::callbacks::Receiver::*(int):int`) and must retain their root prefix on
  re-parsing. Canonical resolved exception types may likewise be rooted, such
  as `throws(::k::Exception)`.
  Required private namespace-level aggregate definitions, including typed native
  handles used within nested callback/result types, are source dependencies too.
  Namespace declarations in those fragments must use legal K syntax (no leading
  `::`); import places the fragment at its fully qualified definition scope.
  Root-qualified expression references remain legal and must not be truncated.
  They retain declaration-scope type names and private visibility, are shared
  across templates from one origin, and do not become public aggregate DTOs.
  Private type preludes may use fully qualified namespace fragments to retain their
  original lexical ownership rather than relocating into the template namespace.
  Re-export must preserve the already-normalized template declaration and emit
  each dependency prelude once; consumer import exposure must not cause the
  declaration to be reconstructed with unrelated consumer `using` directives.
* Union templates are always classic (no signature payload exists for unions);
  they only ever export `source`.
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
  is_static_nested : bool           -- true if this is a static nested aggregate

  -- Nesting
  enclosing_fq_name : string        -- fq_name of the enclosing aggregate; empty
                                    -- if this aggregate is not nested

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
  aliases      : [KdiAlias]        -- optional; omitted when empty
  nested       : [KdiAggregate]

  -- LLVM IR struct type definition, e.g. "%struct.ns.Counter = type { i32*, i32 }"
  -- Used by importing compilers to reconstruct the exact LLVM StructType
  -- without re-deriving the layout from the KDI layout fields.
  llvm_def     : string
  doc          : KdiDocBlock?
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
  llvm_def       : string          -- LLVM IR declaration of the vtable global,
                                   -- e.g. "@_ZTV7Counter = constant [3 x i8*]
                                   --   [...]"
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

## KdiEnum

```
KdiEnum {
  name              : string
  fq_name           : string
  visibility        : "public" | "protected"
  underlying_type   : KdiType
  object_type       : KdiType?      -- aggregate ref for object-backed enums
  object_table_symbol : string?     -- required when object_type is set
  base_fq_name      : string?       -- enum derivation base
  entries           : [KdiEnumEntry]
  doc               : KdiDocBlock?
}

KdiEnumEntry {
  name              : string
  value             : int
  is_default        : bool
  object_init_members : [KdiObjectInitMember]  -- only for object-backed enums
}

KdiObjectInitMember {
  name  : string
  value : int
}
```

Compatibility rules:

* Integer-backed enums omit `object_type` and `object_table_symbol`.
* Legacy payloads without typed-enum fields remain valid.
* If `object_type` is present, importers must treat the enum as object-backed.

---

## KdiUnion

```
KdiUnion {
  name                     : string
  fq_name                  : string
  mangled_name             : string
  visibility               : "public" | "protected"
  alternatives             : [KdiUnionAlternative]
  base_union_fq_name       : string?   -- parent union for union inheritance
  polymorphic_base_fq_name : string?   -- base class or interface for polymorphic unions
  llvm_def                 : string    -- LLVM IR struct definition, e.g. "{ i32, [8 x i8] }"
  template_origin          : KdiTemplateOrigin?
  doc                      : KdiDocBlock?
}

KdiUnionAlternative {
  name     : string
  type     : KdiType
  is_const : bool
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
  llvm_def     : string            -- LLVM IR prototype, e.g.
                                   -- "declare i32 @_ZN3foo3barEi(i32)"
                                   -- Used by importing compilers to reconstruct
                                   -- the exact LLVM Function declaration.
  doc          : KdiDocFunction?
}

KdiParam {
  name           : string
  type           : KdiType
  is_varargs     : bool?           -- defaults to false
  default_source : string?         -- K expression, absent for required parameters
}
```

`default_source` preserves the default argument of functions, methods, and
constructors. Consumers must parse and type-check it in the declaring callable's
lexical scope, not the caller's scope, and evaluate it only when the argument is
omitted. Producers must preserve symbol bindings (including qualified enum
entries), using rooted names where needed to prevent consumer shadowing. This
optional field does not change the LLVM prototype or mangled symbol. Descriptors
without the field retain required-parameter behavior. JSON and CBOR carry the
same expression text; the schema version remains `0.1`.

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
  llvm_def       : string          -- LLVM IR prototype (with implicit 'this'
                                   -- as first arg), e.g.
                                   -- "declare i32 @_ZN2ns5Adder3addEi
                                   --   (%struct.ns.Adder* %this, i32)"
  doc            : KdiDocFunction?
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
  llvm_def           : string      -- LLVM IR prototype of the C1 variant, e.g.
                                   -- "declare void @_ZN7CounterC1Ev
                                   --   (%struct.Counter* %this)"
  doc                : KdiDocFunction?
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
  llvm_def       : string          -- LLVM IR prototype of the D1 variant, e.g.
                                   -- "declare void @_ZN7CounterD1Ev
                                   --   (%struct.Counter* %this)"
  doc            : KdiDocFunction?
}
```

---

## KdiVariable  (global, static, or member)

```
KdiVariable {
  name            : string
  fq_name         : string
  visibility      : "public" | "protected"
  type            : KdiType
  is_const        : bool
  is_thread_local : bool?        -- optional, true if thread-local storage (omitted when false)
  mangled_name    : string
  doc             : KdiDocBlock?
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
* The `llvm_def` field is **mandatory** on `KdiFunction`, `KdiMethod`,
  `KdiConstructor`, `KdiDestructor`, `KdiAggregate`, and `KdiVtable`.
  It must not be empty.  This field carries the exact LLVM IR text emitted
  by the exporting compiler and is authoritative for ABI-faithful import.
* The `dependencies` field in `KdiHeader` is **optional** (omitted when the
  module has no imports).  When present, it lists the canonical module names
  of all direct imports of the compiled module in declaration order.  A
  consumer **must** treat a missing transitive dependency as a fatal error:
  without all transitive KDIs the aggregate layouts and vtable slots cannot
  be fully reconstructed.
* For enums, `object_type`, `object_table_symbol`, and `object_init_members`
  are optional typed-enum extensions; consumers must remain backward-compatible
  with payloads that only contain integer-enum fields.
