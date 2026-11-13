#include "ccm_engine.h"

#include <openssl/crypto.h>
#include <string.h>

enum {
    CCM_BLOCK_SIZE = 16,
    CCM_AAD = 1,
    CCM_MESSAGE = 2,
    CCM_TERMINATED = 3
};

struct k_ccm_ctx {
    EVP_CIPHER_CTX *cipher;
    uint64_t aad_len;
    uint64_t aad_used;
    uint64_t message_len;
    uint64_t message_used;
    uint64_t next_counter;
    unsigned char mac[CCM_BLOCK_SIZE];
    unsigned char pending[CCM_BLOCK_SIZE];
    unsigned char counter[CCM_BLOCK_SIZE];
    unsigned char stream[CCM_BLOCK_SIZE];
    size_t pending_len;
    size_t stream_used;
    unsigned int length_bytes;
    unsigned int tag_len;
    int encrypt;
    int state;
};

static void ccm_terminate(k_ccm_ctx *ctx) {
    EVP_CIPHER_CTX_free(ctx->cipher);
    OPENSSL_cleanse(ctx, sizeof(*ctx));
    ctx->state = CCM_TERMINATED;
}

static int ccm_fail(k_ccm_ctx *ctx) {
    ccm_terminate(ctx);
    return K_CCM_OPERATION;
}

static void ccm_encode(unsigned char *dst, unsigned int len, uint64_t value) {
    while (len != 0) {
        dst[--len] = (unsigned char)value;
        value >>= 8;
    }
}

/* EVP may write up to one extra block to the output of an update. */
static int ccm_block(k_ccm_ctx *ctx, const unsigned char *in,
                     unsigned char *out) {
    unsigned char result[CCM_BLOCK_SIZE * 2];
    int written = 0;
    int ok = EVP_EncryptUpdate(ctx->cipher, result, &written, in,
                               CCM_BLOCK_SIZE) == 1
             && written == CCM_BLOCK_SIZE;
    if (ok) {
        memcpy(out, result, CCM_BLOCK_SIZE);
    }
    OPENSSL_cleanse(result, sizeof(result));
    return ok;
}

static int ccm_mac_block(k_ccm_ctx *ctx) {
    unsigned char block[CCM_BLOCK_SIZE];
    size_t i;
    int ok;

    for (i = 0; i < CCM_BLOCK_SIZE; ++i) {
        block[i] = ctx->mac[i] ^ ctx->pending[i];
    }
    ok = ccm_block(ctx, block, ctx->mac);
    OPENSSL_cleanse(block, sizeof(block));
    OPENSSL_cleanse(ctx->pending, sizeof(ctx->pending));
    ctx->pending_len = 0;
    return ok;
}

static int ccm_mac_update(k_ccm_ctx *ctx, const unsigned char *data,
                          size_t len) {
    while (len != 0) {
        size_t take = CCM_BLOCK_SIZE - ctx->pending_len;
        if (take > len) {
            take = len;
        }
        memcpy(ctx->pending + ctx->pending_len, data, take);
        ctx->pending_len += take;
        data += take;
        len -= take;
        if (ctx->pending_len == CCM_BLOCK_SIZE && !ccm_mac_block(ctx)) {
            return 0;
        }
    }
    return 1;
}

static int ccm_mac_pad(k_ccm_ctx *ctx) {
    if (ctx->pending_len == 0) {
        return 1;
    }
    memset(ctx->pending + ctx->pending_len, 0,
           CCM_BLOCK_SIZE - ctx->pending_len);
    return ccm_mac_block(ctx);
}

static int ccm_count_fits(size_t len, uint64_t remaining) {
#if SIZE_MAX > UINT64_MAX
    if (len > UINT64_MAX) {
        return 0;
    }
#endif
    return (uint64_t)len <= remaining;
}

static int ccm_overlaps(const unsigned char *input,
                        const unsigned char *output, size_t len) {
    uintptr_t a;
    uintptr_t b;

    if (len == 0) {
        return 0;
    }
#if SIZE_MAX > UINTPTR_MAX
    if (len > UINTPTR_MAX) {
        return 1;
    }
#endif
    a = (uintptr_t)input;
    b = (uintptr_t)output;
    if ((uintptr_t)(len - 1) > UINTPTR_MAX - a ||
        (uintptr_t)(len - 1) > UINTPTR_MAX - b) {
        return 1;
    }
    if (input == output) {
        return 0;
    }
    return a < b ? b - a < len : a - b < len;
}

int k_ccm_new(const EVP_CIPHER *block_cipher, const unsigned char *key,
              unsigned int key_len, const unsigned char *nonce,
              unsigned int nonce_len, unsigned int tag_len,
              uint64_t message_len, uint64_t aad_len, int encrypt,
              k_ccm_ctx **out) {
    k_ccm_ctx *ctx;
    unsigned char b0[CCM_BLOCK_SIZE] = {0};
    unsigned int length_bytes;
    int cipher_key_len;
    int ok;

    if (out == NULL) {
        return K_CCM_INVALID;
    }
    *out = NULL;
    if (block_cipher == NULL || key == NULL || nonce == NULL ||
        nonce_len < 7 || nonce_len > 13 ||
        tag_len < 4 || tag_len > 16 || (tag_len & 1) != 0 ||
        (encrypt != 0 && encrypt != 1)) {
        return K_CCM_INVALID;
    }
    length_bytes = 15 - nonce_len;
    if (length_bytes < 8 && message_len >= (UINT64_C(1) << (8 * length_bytes))) {
        return K_CCM_INVALID;
    }
    cipher_key_len = EVP_CIPHER_get_key_length(block_cipher);
    if (EVP_CIPHER_get0_provider(block_cipher) == NULL ||
        EVP_CIPHER_get_mode(block_cipher) != EVP_CIPH_ECB_MODE ||
        EVP_CIPHER_get_block_size(block_cipher) != CCM_BLOCK_SIZE ||
        cipher_key_len <= 0 || (unsigned int)cipher_key_len != key_len) {
        return K_CCM_INVALID;
    }

    ctx = OPENSSL_zalloc(sizeof(*ctx));
    if (ctx == NULL) {
        return K_CCM_OPERATION;
    }
    ctx->cipher = EVP_CIPHER_CTX_new();
    if (ctx->cipher == NULL ||
        EVP_EncryptInit_ex(ctx->cipher, block_cipher, NULL, key, NULL) != 1 ||
        EVP_CIPHER_CTX_set_padding(ctx->cipher, 0) != 1 ||
        EVP_CIPHER_CTX_get_block_size(ctx->cipher) != CCM_BLOCK_SIZE ||
        EVP_CIPHER_CTX_get_key_length(ctx->cipher) != cipher_key_len) {
        k_ccm_free(ctx);
        return K_CCM_OPERATION;
    }

    ctx->aad_len = aad_len;
    ctx->message_len = message_len;
    ctx->length_bytes = length_bytes;
    ctx->tag_len = tag_len;
    ctx->encrypt = encrypt;
    ctx->state = CCM_AAD;
    ctx->next_counter = 1;
    ctx->stream_used = CCM_BLOCK_SIZE;

    b0[0] = (unsigned char)((aad_len != 0 ? 0x40 : 0) |
                            (((tag_len - 2) / 2) << 3) | (length_bytes - 1));
    memcpy(b0 + 1, nonce, nonce_len);
    ccm_encode(b0 + CCM_BLOCK_SIZE - length_bytes, length_bytes, message_len);
    ctx->counter[0] = (unsigned char)(length_bytes - 1);
    memcpy(ctx->counter + 1, nonce, nonce_len);
    ok = ccm_block(ctx, b0, ctx->mac);
    OPENSSL_cleanse(b0, sizeof(b0));
    if (!ok) {
        k_ccm_free(ctx);
        return K_CCM_OPERATION;
    }

    if (aad_len != 0) {
        if (aad_len < UINT64_C(0xff00)) {
            ctx->pending_len = 2;
            ccm_encode(ctx->pending, 2, aad_len);
        } else if (aad_len < UINT64_C(0x100000000)) {
            ctx->pending[0] = 0xff;
            ctx->pending[1] = 0xfe;
            ctx->pending_len = 6;
            ccm_encode(ctx->pending + 2, 4, aad_len);
        } else {
            ctx->pending[0] = 0xff;
            ctx->pending[1] = 0xff;
            ctx->pending_len = 10;
            ccm_encode(ctx->pending + 2, 8, aad_len);
        }
    }
    *out = ctx;
    return K_CCM_OK;
}

int k_ccm_aad_update(k_ccm_ctx *ctx, const unsigned char *data, size_t len) {
    if (ctx == NULL) {
        return K_CCM_INVALID;
    }
    if (ctx->state != CCM_AAD) {
        return K_CCM_STATE;
    }
    if ((len != 0 && data == NULL) ||
        !ccm_count_fits(len, ctx->aad_len - ctx->aad_used)) {
        return K_CCM_INVALID;
    }
    if (!ccm_mac_update(ctx, data, len)) {
        return ccm_fail(ctx);
    }
    ctx->aad_used += (uint64_t)len;
    return K_CCM_OK;
}

int k_ccm_update(k_ccm_ctx *ctx, const unsigned char *input,
                 unsigned char *output, size_t len) {
    size_t remaining = len;

    if (ctx == NULL) {
        return K_CCM_INVALID;
    }
    if (ctx->state != CCM_AAD && ctx->state != CCM_MESSAGE) {
        return K_CCM_STATE;
    }
    if (ctx->aad_used != ctx->aad_len) {
        return K_CCM_STATE;
    }
    if ((len != 0 && (input == NULL || output == NULL)) ||
        !ccm_count_fits(len, ctx->message_len - ctx->message_used) ||
        ccm_overlaps(input, output, len)) {
        return K_CCM_INVALID;
    }
    if (ctx->state == CCM_AAD) {
        if (!ccm_mac_pad(ctx)) {
            return ccm_fail(ctx);
        }
        ctx->state = CCM_MESSAGE;
    }
    while (remaining != 0) {
        size_t take;
        size_t i;
        if (ctx->stream_used == CCM_BLOCK_SIZE) {
            ccm_encode(ctx->counter + CCM_BLOCK_SIZE - ctx->length_bytes,
                       ctx->length_bytes, ctx->next_counter++);
            if (!ccm_block(ctx, ctx->counter, ctx->stream)) {
                return ccm_fail(ctx);
            }
            ctx->stream_used = 0;
        }
        take = CCM_BLOCK_SIZE - ctx->stream_used;
        if (take > remaining) {
            take = remaining;
        }
        if (ctx->encrypt && !ccm_mac_update(ctx, input, take)) {
            return ccm_fail(ctx);
        }
        for (i = 0; i < take; ++i) {
            output[i] = input[i] ^ ctx->stream[ctx->stream_used + i];
        }
        if (!ctx->encrypt && !ccm_mac_update(ctx, output, take)) {
            return ccm_fail(ctx);
        }
        ctx->stream_used += take;
        input += take;
        output += take;
        remaining -= take;
    }
    ctx->message_used += (uint64_t)len;
    return K_CCM_OK;
}

int k_ccm_final(k_ccm_ctx *ctx, unsigned char *tag) {
    unsigned char mask[CCM_BLOCK_SIZE] = {0};
    unsigned char calculated[CCM_BLOCK_SIZE] = {0};
    unsigned char final_block[CCM_BLOCK_SIZE * 2] = {0};
    int final_len = 0;
    int result = K_CCM_OPERATION;
    size_t i;

    if (ctx == NULL) {
        return K_CCM_INVALID;
    }
    if (ctx->state != CCM_AAD && ctx->state != CCM_MESSAGE) {
        return K_CCM_STATE;
    }
    if (tag == NULL) {
        return K_CCM_INVALID;
    }
    if (ctx->aad_used != ctx->aad_len || ctx->message_used != ctx->message_len) {
        return K_CCM_STATE;
    }
    if (!ccm_mac_pad(ctx)) {
        goto done;
    }
    memset(ctx->counter + CCM_BLOCK_SIZE - ctx->length_bytes, 0,
           ctx->length_bytes);
    if (!ccm_block(ctx, ctx->counter, mask) ||
        EVP_EncryptFinal_ex(ctx->cipher, final_block, &final_len) != 1 ||
        final_len != 0) {
        goto done;
    }
    for (i = 0; i < ctx->tag_len; ++i) {
        calculated[i] = ctx->mac[i] ^ mask[i];
    }
    if (ctx->encrypt) {
        memcpy(tag, calculated, ctx->tag_len);
        result = K_CCM_OK;
    } else {
        result = CRYPTO_memcmp(tag, calculated, ctx->tag_len) == 0
                     ? K_CCM_OK : K_CCM_AUTH;
    }

done:
    OPENSSL_cleanse(mask, sizeof(mask));
    OPENSSL_cleanse(calculated, sizeof(calculated));
    OPENSSL_cleanse(final_block, sizeof(final_block));
    ccm_terminate(ctx);
    return result;
}

void k_ccm_free(k_ccm_ctx *ctx) {
    if (ctx == NULL) {
        return;
    }
    EVP_CIPHER_CTX_free(ctx->cipher);
    OPENSSL_clear_free(ctx, sizeof(*ctx));
}
