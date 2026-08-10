# KDI Library (`libkdi/`) — AI Agent Guide

Scope: KDI data model, serialization/deserialization, tooling, and KDI tests.

---

## 1. What lives here

- `src/kdi_types.hpp`: DTO/type model for KDI.
- `src/kdi_json.cpp`: JSON serialization/deserialization.
- `src/kdi_cbor.cpp`: CBOR serialization/deserialization.
- `src/kditool*`: KDI inspection tooling.
- `tests/*.cpp`: DTO/JSON/CBOR/symbol/validation tests.

## 1.1 Investigation map

| Symptom | Start with | Then check |
|---------|------------|------------|
| Type field missing in serialized output | `src/kdi_types.hpp` | `src/kdi_json.cpp`, `src/kdi_cbor.cpp` |
| JSON and CBOR divergence | compare `src/kdi_json.cpp` vs `src/kdi_cbor.cpp` for same DTO path | matching tests in `tests/test_json.cpp`, `tests/test_cbor.cpp` |
| Tool dump mismatch | `src/kditool*` | DTO conversion path in serializer files |
| Import/export compatibility break | compiler converter in `klang/src/model/tools/kdi_type_converter.cpp` | KDI schema docs in `doc/spec/kdi/` |

---

## 2. libkdi architecture (DTO -> codecs -> tools)

### Layering

| Layer | Responsibility | Main files |
|------|----------------|------------|
| DTO/type model | canonical in-memory representation of KDI entities | `src/kdi_types.hpp` |
| JSON codec | textual import/export of DTO | `src/kdi_json.cpp` |
| CBOR codec | binary import/export of DTO | `src/kdi_cbor.cpp` |
| Tooling | dump/introspection helpers for produced artifacts | `src/kditool*` |
| Tests | cross-codec parity and invariants | `tests/*.cpp` |

### End-to-end data flow

1. Producer (compiler side) emits KDI-compatible model data.
2. DTO is serialized to JSON or CBOR.
3. Consumer deserializes back to DTO.
4. Tooling and tests validate parity and schema invariants.

### Architectural boundaries

- DTO layer is the source of truth; codecs must not invent semantics.
- JSON and CBOR are equivalent transport encodings of the same logical content.
- Tooling should consume existing conversion paths, not duplicate schema logic.
- Compiler interoperability path (`klang/src/model/tools/kdi_type_converter.cpp`) must stay aligned with DTO definitions.

### Change-impact map

| Change type | Must update |
|------------|-------------|
| Add/remove DTO field | `kdi_types.hpp` + both codecs + tests + KDI spec docs |
| Encoding rule update | impacted codec(s) + parity tests |
| Interop shape change | compiler converter + libkdi tests + docs |

---

## 3. Invariants

- Keep semantic parity across DTO, JSON, and CBOR representations.
- Preserve import/export compatibility with compiler-side KDI converter.
- Any format/schema change must be reflected in documentation under `doc/spec/kdi/`.
- Schema version remains `0.1` until formal stabilization policy changes.

---

## 4. Practical workflow

```bash
# Build kdi targets
cd cmake-build-debug && ninja -j3 kdi-tests

# Run kdi tests
cd cmake-build-debug && ctest -R kdi-tests --output-on-failure

# Inspect a generated KDI
./cmake-build-debug/libkdi/kditool dump path/to/lib.kdi
```

Suggested minimal test runs:
```bash
cd cmake-build-debug && ./libkdi/kdi-tests "[json]"
cd cmake-build-debug && ./libkdi/kdi-tests "[cbor]"
```
