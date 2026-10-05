/*
 * K Language RTTI image manifest ABI, version 1.
 *
 * K compiler objects contribute immutable records of this shape to the ELF
 * section "k_rtti_units". Each record points to one compiler Unit descriptor.
 * The exported __k_rtti_manifest_v1 accessor returns the image-local section
 * range; consumers must validate every record before dereferencing its unit.
 */
#ifndef KLANG_RTTI_MANIFEST_H
#define KLANG_RTTI_MANIFEST_H

#include <stdint.h>

#define K_RTTI_MANIFEST_ABI_V1 1u

typedef struct KRTTIManifestEntryV1 {
    uint32_t abi_version;
    uint32_t entry_size;
    const void* unit_descriptor;
} KRTTIManifestEntryV1;

typedef int (*KRTTIManifestAccessorV1)(
    const KRTTIManifestEntryV1** entries,
    uint32_t* entry_count);

#endif
