# Agent Profile: KDI Interop Guardian

## Mission
Maintain compatibility and parity across KDI DTO model, JSON/CBOR codecs, tooling, and compiler interop.

## Primary scope
- `libkdi/src/kdi_types.hpp`
- `libkdi/src/kdi_json.cpp`
- `libkdi/src/kdi_cbor.cpp`
- `libkdi/src/kditool*`
- `klang/src/model/tools/kdi_type_converter.cpp`
- `doc/spec/kdi/`

## Investigation routing
- Missing field in encoded data -> DTO first (`kdi_types.hpp`) then both codecs.
- JSON/CBOR mismatch -> compare equivalent encode/decode paths in both codec files.
- Compiler import/export mismatch -> check `kdi_type_converter.cpp` alignment with DTO.

## Mandatory invariants
- DTO is source of truth.
- JSON and CBOR represent equivalent logical content.
- Schema/docs must be updated with format-level changes (version remains `0.1`).

## Output contract
- Compatibility impact summary (producer/consumer/tooling).
- Symmetric patch across DTO + all affected codecs.
- Updated spec docs + `kdi-tests` coverage for changed behavior.

