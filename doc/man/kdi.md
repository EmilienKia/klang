% KDITOOL(1) klang 0.0.1
% Emilien Kia
% March 2026

# NAME

kditool — K Description Interface utility

# SYNOPSIS

**kditool** *COMMAND* [*FILE*]

# DESCRIPTION

**kditool** is the command-line tool for inspecting and validating `.kdi` files.
A `.kdi` file is a binary CBOR document (schema v0.1) produced by **klangc**(1)
whenever it compiles a K source file into a shared library (`.so`) or static
archive (`.a`).  The `.kdi` file describes the public and protected API of the
library: types, functions, aggregates and their LLVM layout — enough for another
K module to call or inherit from the exported symbols.

# COMMANDS

**dump** *file.kdi*
:   Parse *file.kdi* and print its content in a structured, human-readable form
    similar to a K header file.  Exits with code **0** on success, **2** on I/O
    or parse error.

**validate** *file.kdi*
:   Parse *file.kdi* and validate it against KDI schema v0.1.  All validation
    errors are printed to *stderr* in the form:

        INVALID: N error(s) in file.kdi
          [path.to.field] description

    Exits with code **0** if valid, **1** if invalid, **2** on I/O or parse error.

**json-dump** *file.kdi*
:   Parse *file.kdi* (CBOR) and print its content as pretty-printed JSON to
    *stdout*.  Exits with code **0** on success, **2** on error.

**doc** [**--json**] [**--list-children**] *file.kdi* *symbol*
:   Resolve *symbol* in *file.kdi* and print the in-code documentation attached
    to that element.  If *symbol* is already mangled, it is matched directly;
    otherwise the lookup also tries the symbol name with the module root prefix
    stripped.  On ambiguity, all candidate symbols are listed on *stderr*.
    Exits with code **0** on success, **1** if the symbol is ambiguous or not
    found, **2** on I/O or parse error.  With **--json**, the output is emitted
    as JSON.  With **--list-children**, direct child symbols are listed too.

**to-json** *file.kdi*
:   Convert a `.kdi` CBOR file to a human-readable `.kdi.json` file in the same
    directory.  The output file name is *file.kdi.json*.
    Exits with code **0** on success, **2** on error.

**to-cbor** *file.kdi.json*
:   Convert a `.kdi.json` JSON file back to CBOR format.  The output file name
    is derived by stripping the trailing `.json` suffix (i.e. *file.kdi*).
    Exits with code **0** on success, **2** on error.

**check-symbols** *file.kdi* *file.so*
:   Load *file.kdi* and cross-check that every mangled symbol declared in it
    (functions, methods, constructors, destructors, static variables, vtables)
    is actually present in *file.so* using **nm(1)**.  
    Prints one line per missing symbol to *stderr* in the form:

        MISSING SYMBOL: <mangled_name>

    Exits with code **0** if all symbols are found, **1** if any are missing,
    **2** on I/O / parse error, **3** if **nm** is not found in `PATH`.

**help**
:   Display a short usage summary.

# EXIT STATUS

| Code | Meaning |
|------|---------|
| 0    | Success / file is valid |
| 1    | Semantic or validation error(s) found |
| 2    | I/O error or malformed CBOR |
| 3    | Usage error (unknown command, missing argument) |

# FILE FORMAT

`.kdi` files are binary CBOR (RFC 8949) documents.  The top-level item is a
CBOR map with the following required keys (always the first two):

```
schema_major  uint   -- must be 0
schema_minor  uint   -- must be 1
```

See **doc/spec/kdi/kdi-schema-abstract.md** for the full abstract schema and
**doc/spec/kdi/kdi-cbor-schema.md** for the complete CBOR encoding rules.

A human-readable JSON equivalent can be produced with `kditool json-dump`.

# EXAMPLES

Dump the content of a library description file:

    kditool dump libmath.utils.kdi

Validate a KDI file and fail the build if invalid:

    kditool validate libmath.utils.kdi || exit 1

Dump a KDI file as JSON to stdout:

    kditool json-dump libmath.utils.kdi

Show the documentation of one symbol:

    kditool doc libmath.utils.kdi math::utils::StringBuilder
    kditool doc --json libmath.utils.kdi _KFN...
    kditool doc --list-children libmath.utils.kdi math::utils::StringBuilder

Convert a CBOR KDI to its JSON equivalent:

    kditool to-json libmath.utils.kdi
    # produces: libmath.utils.kdi.json

Convert a JSON KDI back to CBOR:

    kditool to-cbor libmath.utils.kdi.json
    # produces: libmath.utils.kdi

Cross-check that all declared symbols are present in the binary:

    kditool check-symbols libmath.utils.kdi libmath.utils.so

# SEE ALSO

**klangc**(1)

# NOTES

* KDI files are generated automatically by **klangc** whenever `--dyn-lib` or
  `--static-lib` is used.  The output name is derived from the module name
  (e.g. module `math::utils` → `libmath.utils.kdi`).

* The `validate` command performs schema-level checks only.  It does not verify
  that the described symbols actually exist in the associated binary.  Use
  `check-symbols` for that cross-check.

* The `header.dependencies` field lists the direct imports of the compiled
  module (canonical module names, e.g. `["ival_lib", "aval_lib"]`).
  A consumer compiler (**klangc**) reads this list and loads those KDIs
  recursively as *transitive dependencies*.  A missing transitive KDI is a
  fatal compilation error.
