#include <stdint.h>

typedef struct {
    uint32_t abi_version;
    uint32_t entry_size;
    const void* unit_descriptor;
} ManifestEntry;

int klang_malformed_manifest_fixture(void) {
    return 1;
}

__attribute__((visibility("protected")))
int __k_rtti_manifest_v1(const ManifestEntry** entries, uint32_t* count) {
    *entries = 0;
    *count = 1;
    return 0;
}
