---
description: Apply KDI changes safely across DTO, codecs, interop, tests, and specs.
---

# KDI Change Impact Checklist

## Goal
Apply KDI-related changes safely across compiler interop, codecs, tooling, tests, and specs.

## Checklist
1. DTO updated (`libkdi/src/kdi_types.hpp`)?
2. JSON codec updated (`libkdi/src/kdi_json.cpp`)?
3. CBOR codec updated (`libkdi/src/kdi_cbor.cpp`)?
4. Compiler converter alignment checked (`klang/src/model/tools/kdi_type_converter.cpp`)?
5. Tooling output path validated (`libkdi/src/kditool*`)?
6. Tests updated (`libkdi/tests/*.cpp`, plus import-related compiler tests if needed)?
7. Spec updated (`doc/spec/kdi/`) with version policy preserved (`0.1`)?

## Deliverable
- Explicit yes/no status for each checklist item in final change note.

