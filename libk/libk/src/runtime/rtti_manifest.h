/*
 * K Language RTTI image manifest record ABI, version 2.
 *
 * K compiler objects contribute immutable records of this shape to the ELF
 * section "k_rtti_units". Each record points to one compiler Unit descriptor.
 * The exported __k_rtti_manifest_v1 accessor retains its historical discovery
 * symbol name; consumers reject records using the incompatible version 1 layout.
 */
#ifndef KLANG_RTTI_MANIFEST_H
#define KLANG_RTTI_MANIFEST_H

#include <stdint.h>

#define K_RTTI_MANIFEST_ABI_VERSION 2u

typedef struct KRTTIManifestEntryV2 {
    uint32_t abi_version;
    uint32_t entry_size;
    const void* unit_descriptor;
} KRTTIManifestEntryV2;

typedef int (*KRTTIManifestAccessor)(
    const KRTTIManifestEntryV2** entries,
    uint32_t* entry_count);

#endif
