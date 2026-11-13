#ifndef K_CCM_ENGINE_H
#define K_CCM_ENGINE_H

#include <stddef.h>
#include <stdint.h>
#include <openssl/evp.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct k_ccm_ctx k_ccm_ctx;

enum {
    K_CCM_OK = 0,
    K_CCM_INVALID = -1,
    K_CCM_AUTH = -2,
    K_CCM_OPERATION = -3,
    K_CCM_STATE = -4
};

/*
 * Private CCM engine. The caller supplies a fetched provider ECB cipher with
 * a 16-byte block and matching fixed key length, and must ensure nonce
 * uniqueness and the RFC 3610 per-key block-operation limit. The cipher and
 * input buffers need only remain valid for the duration of each call.
 *
 * AAD must be supplied before any message update, exactly as declared at
 * construction. The first message update (even with length zero) closes AAD;
 * final also closes it. Both declared counts must be met before final.
 * Zero-length updates may use null data pointers. Other input/output ranges
 * must be valid for their lengths; message input and output may be identical
 * or disjoint, but not partially overlapping.
 *
 * Encryption final writes tag_len bytes; decryption final reads tag_len bytes.
 * Decryption updates produce unauthenticated plaintext: the caller MUST keep
 * it private until final succeeds, and cleanse/discard it on any failure.
 * Output from a failing update is unspecified. Invalid arguments/counts
 * return INVALID; incorrect call order/incomplete counts return STATE; neither
 * changes the context. Operation failures and final (including AUTH) terminate
 * the context; subsequent operations return STATE. Free accepts null.
 *
 * This custom CCM composition is not automatically FIPS-validated, even
 * when its underlying provider is validated.
 */
int k_ccm_new(const EVP_CIPHER *block_cipher, const unsigned char *key,
              unsigned int key_len, const unsigned char *nonce,
              unsigned int nonce_len, unsigned int tag_len,
              uint64_t message_len, uint64_t aad_len, int encrypt,
              k_ccm_ctx **out);
int k_ccm_aad_update(k_ccm_ctx *ctx, const unsigned char *data, size_t len);
int k_ccm_update(k_ccm_ctx *ctx, const unsigned char *input,
                 unsigned char *output, size_t len);
int k_ccm_final(k_ccm_ctx *ctx, unsigned char *tag);
void k_ccm_free(k_ccm_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
