#include <stdint.h>

typedef struct {
    uint32_t abi_version;
    uint32_t entry_size;
    const void* unit_descriptor;
} ManifestEntry;

static const ManifestEntry entry = {
    .abi_version = 1,
    .entry_size = sizeof(ManifestEntry),
    .unit_descriptor = &entry
};

__attribute__((visibility("protected")))
int __k_rtti_manifest_v1(const ManifestEntry** entries, uint32_t* count) {
    *entries = &entry;
    *count = 1;
    return 0;
}
