/* Exercise generation exhaustion and allocation failures in the same substrate. */
#include <stddef.h>
#include <stdlib.h>

void* __k_test_provenance_malloc(size_t size);
void* __k_test_provenance_aligned_alloc(size_t alignment, size_t size);
void __k_test_provenance_free(void* object);

#define K_PROVENANCE_GENERATION_MAX 3
#define __k_provenance_enter_live __k_test_provenance_enter_live
#define __k_provenance_enter_cleanup __k_test_provenance_enter_cleanup
#define __k_provenance_scope_validate __k_test_provenance_scope_validate
#define __k_provenance_leave __k_test_provenance_leave
#define __k_provenance_slot_close __k_test_provenance_slot_close
#define __k_provenance_capture __k_test_provenance_capture
#define __k_provenance_path_retain __k_test_provenance_path_retain
#define __k_provenance_path_release __k_test_provenance_path_release
#define __k_provenance_path_validate __k_test_provenance_path_validate
#define __k_provenance_path_pin __k_test_provenance_path_pin
#define __k_provenance_path_unpin __k_test_provenance_path_unpin
#define __k_provenance_domain __k_test_provenance_domain
#define __k_provenance_collect __k_test_provenance_collect
#define malloc __k_test_provenance_malloc
#define aligned_alloc __k_test_provenance_aligned_alloc
#define free __k_test_provenance_free
#include "../src/runtime/provenance.c"

/* Simulate an otherwise impractical pin-count exhaustion. */
void __k_test_provenance_set_token_pins(KProvSlot* slot, uint64_t pins) {
    if (!slot || !slot->_token) abort();
    token_lock(slot->_token);
    slot->_token->pins = pins;
    token_unlock(slot->_token);
}
