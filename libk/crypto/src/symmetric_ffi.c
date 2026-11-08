/*
 * K Language standard library -- private symmetric crypto bridge
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */
#define _FILE_OFFSET_BITS 64
#include "ccm_engine.h"

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/evperr.h>
#include <openssl/proverr.h>
#include <openssl/rand.h>

#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

enum { OK = 0, INVALID = -1, AUTH = -2, OPERATION = -3, STATE = -4 };
enum { AES = 1, ARIA, CAMELLIA, SM4, CHACHA20, ASCON_AEAD128 };
enum { GCM = 1, CCM, OCB, CBC, CTR, XTS, POLY1305, ASCON, CFB, OFB, CFB1, CFB8, STREAM };
enum { NO_PADDING = 0, PKCS7 = 1 };
enum { CHUNK = 65536, BLOCK = 16, MAX_KEY = 64 };
#define GCM_MAX_DATA ((UINT64_C(1) << 36) - 32)
#define GCM_MAX_AAD ((UINT64_C(1) << 61) - 1)
#define CHACHA_MAX_DATA ((UINT64_C(0xffffffff)) * 64)
#define XTS_MAX_DATA ((UINT64_C(1) << 20) * 16)

typedef struct {
    int algorithm, mode;
    const char *algorithm_name, *mode_name, *cipher_family;
    unsigned int bits_mask;
    unsigned int default_iv, default_tag;
    int default_padding;
} cipher_adapter;

#define B128 1u
#define B192 2u
#define B256 4u
#define FAMILY_MODES(id, family, mask, xts_mask) \
    {id,GCM,family,"GCM",family,mask,12,16,NO_PADDING}, \
    {id,CCM,family,"CCM",family,mask,12,16,NO_PADDING}, \
    {id,OCB,family,"OCB",family,mask,12,16,NO_PADDING}, \
    {id,CBC,family,"CBC",family,mask,16,0,PKCS7}, \
    {id,CTR,family,"CTR",family,mask,16,0,NO_PADDING}, \
    {id,XTS,family,"XTS",family,xts_mask,16,0,NO_PADDING}, \
    {id,CFB,family,"CFB",family,mask,16,0,NO_PADDING}, \
    {id,OFB,family,"OFB",family,mask,16,0,NO_PADDING}, \
    {id,CFB1,family,"CFB1",family,mask,16,0,NO_PADDING}, \
    {id,CFB8,family,"CFB8",family,mask,16,0,NO_PADDING}

static const cipher_adapter adapters[] = {
    FAMILY_MODES(AES, "AES", B128 | B192 | B256, B128 | B256),
    FAMILY_MODES(ARIA, "ARIA", B128 | B192 | B256, B128 | B192 | B256),
    FAMILY_MODES(CAMELLIA, "CAMELLIA", B128 | B192 | B256, B128 | B192 | B256),
    FAMILY_MODES(SM4, "SM4", B128, B128),
    {CHACHA20,POLY1305,"CHACHA20","POLY1305","ChaCha20-Poly1305",B256,12,16,NO_PADDING},
    {CHACHA20,STREAM,"CHACHA20","STREAM","ChaCha20",B256,16,0,NO_PADDING},
    {ASCON_AEAD128,ASCON,"ASCON-AEAD128","ASCON","ASCON-AEAD128",B128,16,16,NO_PADDING}
};
#define ADAPTER_COUNT (sizeof(adapters) / sizeof(adapters[0]))

typedef struct {
    EVP_CIPHER *cipher;
    EVP_CIPHER *block;
    const cipher_adapter *adapter;
    int algorithm, mode, padding, authenticated, xts_streaming;
    unsigned int bits, key_len, iv_len, tag_len;
    uint64_t max_data, max_aad;
    char name[64];
} symmetric_cipher;

typedef struct {
    int algorithm, xts;
    unsigned int bits, length;
    unsigned char bytes[MAX_KEY];
} symmetric_key;

typedef struct chunk {
    struct chunk *next;
    size_t length, read;
    unsigned char bytes[CHUNK];
} chunk;

typedef struct {
    int file_mode;
    FILE *file;
    uint64_t write_pos, read_pos;
    chunk *head, *tail;
} spool;

typedef struct {
    EVP_CIPHER_CTX *ctx;
    EVP_CIPHER *cipher, *block;
    k_ccm_ctx *ccm;
    atomic_int busy;
    int encrypt, state, mode, padding, authenticated, known_length, xts_streaming;
    unsigned int key_len, iv_len, tag_len;
    uint64_t max_data, max_aad, expected_length, data_length, aad_length;
    unsigned char key[MAX_KEY], *iv, tag[16];
    unsigned char tail[2 * BLOCK];
    unsigned int tail_len;
    unsigned char aad_tail[BLOCK];
    unsigned int aad_tail_len;
    spool aad, input, output;
} symmetric_session;

static int aead_mode(int mode) {
    return mode == GCM || mode == CCM || mode == OCB || mode == POLY1305 || mode == ASCON;
}

static unsigned int bits_mask(unsigned int bits) {
    return bits == 128 ? B128 : bits == 192 ? B192 : bits == 256 ? B256 : 0;
}

static const cipher_adapter *find_adapter(int algorithm, unsigned int bits, int mode) {
    for (size_t i = 0; i < ADAPTER_COUNT; ++i)
        if (adapters[i].algorithm == algorithm && adapters[i].mode == mode &&
            (adapters[i].bits_mask & bits_mask(bits))) return &adapters[i];
    return NULL;
}

static const cipher_adapter *find_adapter_names(const char *algorithm, unsigned int bits,
                                                 const char *mode) {
    if (!algorithm || !mode) return NULL;
    for (size_t i = 0; i < ADAPTER_COUNT; ++i)
        if (!strcmp(adapters[i].algorithm_name, algorithm) &&
            !strcmp(adapters[i].mode_name, mode) &&
            (adapters[i].bits_mask & bits_mask(bits))) return &adapters[i];
    return NULL;
}

static int cipher_parameters(symmetric_cipher *c, int algorithm, unsigned int bits,
                             int mode, unsigned int iv_len, unsigned int tag_len,
                             int padding) {
    const cipher_adapter *adapter = find_adapter(algorithm, bits, mode);
    if (!adapter) return 0;
    c->adapter = adapter;
    if (algorithm == CHACHA20 || algorithm == ASCON_AEAD128)
        snprintf(c->name, sizeof(c->name), "%s", adapter->cipher_family);
    else if (algorithm == SM4)
        snprintf(c->name, sizeof(c->name), "%s-%s", adapter->cipher_family, adapter->mode_name);
    else
        snprintf(c->name, sizeof(c->name), "%s-%u-%s", adapter->cipher_family, bits, adapter->mode_name);
    if (padding != NO_PADDING && padding != PKCS7) return 0;
    if (mode != CBC && padding != NO_PADDING) return 0;
    c->authenticated = aead_mode(mode);
    if (mode == GCM) {
        if (!iv_len || iv_len > INT_MAX ||
            !(tag_len == 4 || tag_len == 8 || (tag_len >= 12 && tag_len <= 16)))
            return 0;
        c->max_data = GCM_MAX_DATA;
        c->max_aad = GCM_MAX_AAD;
    } else if (mode == CCM) {
        if (iv_len < 7 || iv_len > 13 || tag_len < 4 || tag_len > 16 ||
            (tag_len & 1)) return 0;
        unsigned int l = 15 - iv_len;
        c->max_data = l == 8 ? UINT64_MAX : (UINT64_C(1) << (8 * l)) - 1;
        c->max_aad = UINT64_MAX;
    } else if (mode == OCB) {
        if (!iv_len || iv_len > 15 || !tag_len || tag_len > 16) return 0;
        c->max_data = UINT64_MAX;
        c->max_aad = UINT64_MAX;
    } else if (mode == POLY1305 || mode == ASCON) {
        if (iv_len != (mode == POLY1305 ? 12u : 16u) || tag_len != 16) return 0;
        c->max_data = mode == POLY1305 ? CHACHA_MAX_DATA : UINT64_MAX;
        c->max_aad = UINT64_MAX;
    } else {
        if (iv_len != BLOCK || tag_len != 0) return 0;
        c->max_data = mode == XTS ? XTS_MAX_DATA : UINT64_MAX;
        c->max_aad = 0;
    }
    c->algorithm = algorithm;
    c->bits = bits;
    c->mode = mode;
    c->padding = padding;
    c->key_len = bits / 8 * (mode == XTS ? 2 : 1);
    c->iv_len = iv_len;
    c->tag_len = tag_len;
    return 1;
}

static int init_evp(EVP_CIPHER_CTX *ctx, const symmetric_cipher *c,
                    const unsigned char *key, const unsigned char *iv, int encrypt) {
    if (EVP_CipherInit_ex2(ctx, c->cipher, NULL, NULL, encrypt, NULL) != 1)
        return 0;
    if (c->authenticated && c->mode != ASCON &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_IVLEN, (int)c->iv_len, NULL) != 1)
        return 0;
    if (c->mode == OCB &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, (int)c->tag_len, NULL) != 1)
        return 0;
    if (c->mode == CBC && EVP_CIPHER_CTX_set_padding(ctx, c->padding == PKCS7) != 1)
        return 0;
    return EVP_CipherInit_ex2(ctx, NULL, key, iv, encrypt, NULL) == 1;
}

static int cipher_matches(const symmetric_cipher *c) {
    unsigned long mode = EVP_CIPHER_get_mode(c->cipher), expected;
    switch (c->mode) {
        case GCM: expected = EVP_CIPH_GCM_MODE; break;
        case CCM: expected = EVP_CIPH_CCM_MODE; break;
        case OCB: expected = EVP_CIPH_OCB_MODE; break;
        case CBC: expected = EVP_CIPH_CBC_MODE; break;
        case CTR: expected = EVP_CIPH_CTR_MODE; break;
        case XTS: expected = EVP_CIPH_XTS_MODE; break;
        case CFB:
        case CFB1:
        case CFB8: expected = EVP_CIPH_CFB_MODE; break;
        case OFB: expected = EVP_CIPH_OFB_MODE; break;
        case STREAM:
        case POLY1305:
        case ASCON: expected = EVP_CIPH_STREAM_CIPHER; break;
        default: return 0;
    }
    if (mode != expected || EVP_CIPHER_get_key_length(c->cipher) != (int)c->key_len ||
        (!!(EVP_CIPHER_get_flags(c->cipher) & EVP_CIPH_FLAG_AEAD_CIPHER) != c->authenticated))
        return 0;
    return (c->authenticated && c->mode != ASCON) ||
           EVP_CIPHER_get_iv_length(c->cipher) == (int)c->iv_len;
}

static int expected_probe_error(unsigned long error, int authentication) {
    if (ERR_SYSTEM_ERROR(error) || ERR_FATAL_ERROR(error)) return 0;
    int lib = ERR_GET_LIB(error), reason = ERR_GET_REASON(error);
    if (authentication)
        return (lib == ERR_LIB_EVP && reason == EVP_R_BAD_DECRYPT) ||
               (lib == ERR_LIB_PROV &&
                (reason == PROV_R_BAD_DECRYPT || reason == PROV_R_INVALID_TAG));
    if (reason == ERR_R_UNSUPPORTED) return 1;
    if (lib == ERR_LIB_EVP &&
        (reason == EVP_R_COMMAND_NOT_SUPPORTED ||
         reason == EVP_R_CTRL_NOT_IMPLEMENTED ||
         reason == EVP_R_CTRL_OPERATION_NOT_IMPLEMENTED ||
         reason == EVP_R_INVALID_IV_LENGTH ||
         reason == EVP_R_INVALID_KEY_LENGTH ||
         reason == EVP_R_UNSUPPORTED_CIPHER ||
         reason == EVP_R_UNSUPPORTED_KEYLENGTH)) return 1;
    return lib == ERR_LIB_PROV &&
        (reason == PROV_R_NOT_SUPPORTED || reason == PROV_R_INVALID_IV_LENGTH ||
         reason == PROV_R_INVALID_KEY_LENGTH || reason == PROV_R_INVALID_TAG_LENGTH ||
         reason == PROV_R_INVALID_PADDING_MODE || reason == PROV_R_INVALID_MODE ||
         reason == PROV_R_UNSUPPORTED_KEY_SIZE);
}

static int discovery_probe_status(int failed, int authentication) {
    unsigned long error;
    int has_errors = 0, unexpected = 0;
    while ((error = ERR_get_error()) != 0) {
        has_errors = 1;
        if (!expected_probe_error(error, authentication)) unexpected = 1;
    }
    if (unexpected || (!failed && has_errors)) return OPERATION;
    return failed ? INVALID : OK;
}

static int cipher_probe(const symmetric_cipher *c, EVP_CIPHER_CTX *ctx,
                        const unsigned char *key, const unsigned char *iv,
                        int discovery, int *internal_error) {
    unsigned char plain[32], encrypted[80], recovered[80], tag[16];
    int a = 0, b = 0, f = 0, d = 0, e = 0, g = 0, ok = 0;
    int len = c->mode == CBC && c->padding == NO_PADDING ? 32 : 19;
    *internal_error = 0;
    for (int i = 0; i < len; ++i) plain[i] = (unsigned char)i;
    if (!init_evp(ctx, c, key, iv, 1) ||
        EVP_CipherUpdate(ctx, encrypted, &a, plain, 7) != 1 ||
        EVP_CipherUpdate(ctx, encrypted + a, &b, plain + 7, len - 7) != 1 ||
        EVP_CipherFinal_ex(ctx, encrypted + a + b, &f) != 1 ||
        (c->authenticated &&
         EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, (int)c->tag_len, tag) != 1) ||
        EVP_CIPHER_CTX_reset(ctx) != 1 || !init_evp(ctx, c, key, iv, 0) ||
        EVP_CipherUpdate(ctx, recovered, &d, encrypted, 7) != 1 ||
        EVP_CipherUpdate(ctx, recovered + d, &e, encrypted + 7, a + b + f - 7) != 1 ||
        (c->authenticated &&
         EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, (int)c->tag_len, tag) != 1) ||
        EVP_CipherFinal_ex(ctx, recovered + d + e, &g) != 1 ||
        d + e + g != len || CRYPTO_memcmp(plain, recovered, (size_t)len) != 0)
        goto done;
    if (discovery && discovery_probe_status(0, 0) != OK) {
        *internal_error = 1;
        goto done;
    }
    if (c->authenticated) {
        tag[0] ^= 1;
        if (EVP_CIPHER_CTX_reset(ctx) != 1 || !init_evp(ctx, c, key, iv, 0) ||
            EVP_CipherUpdate(ctx, recovered, &d, encrypted, a + b + f) != 1 ||
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, (int)c->tag_len, tag) != 1)
            goto done;
        if (EVP_CipherFinal_ex(ctx, recovered + d, &g) == 1) goto done;
        if (discovery && discovery_probe_status(1, 1) != INVALID) {
            *internal_error = 1;
            goto done;
        }
    }
    ok = 1;
done:
    OPENSSL_cleanse(plain, sizeof(plain));
    OPENSSL_cleanse(encrypted, sizeof(encrypted));
    OPENSSL_cleanse(recovered, sizeof(recovered));
    OPENSSL_cleanse(tag, sizeof(tag));
    return ok;
}

static int ascon_known_answer(const symmetric_cipher *c, int *internal_error) {
    static const unsigned char expected[2][16] = {
        {0x4f,0x9c,0x27,0x82,0x11,0xbe,0xc9,0x31,0x6b,0xf6,0x8f,0x46,0xee,0x8b,0x2e,0xc6},
        {0xcc,0xcb,0x67,0x4f,0xe1,0x8a,0x09,0xa2,0x85,0xd6,0xab,0x11,0xb3,0x56,0x75,0xc0}
    };
    static const unsigned char expected_message[17] = {
        0xe8,0xdd,0x57,0x6a,0xba,0x1c,0xd3,0xe6,0xfc,0x70,0x4d,0xe0,0x2a,0xed,0xb7,0x95,0x88
    };
    unsigned char key[16], iv[16], tag[16], out[32], aad = 0x30;
    unsigned char plaintext = 0x20;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    *internal_error = ctx == NULL;
    int written, final_written, ok = ctx != NULL;
    for (unsigned int i = 0; i < 16; ++i) {
        key[i] = (unsigned char)i;
        iv[i] = (unsigned char)(i + 16);
    }
    for (int n = 0; ok && n <= 1; ++n) {
        ok = EVP_CIPHER_CTX_reset(ctx) == 1 && init_evp(ctx, c, key, iv, 1) &&
             (!n || EVP_CipherUpdate(ctx, NULL, &written, &aad, 1) == 1) &&
             EVP_CipherFinal_ex(ctx, out, &written) == 1 && written == 0 &&
             EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, 16, tag) == 1 &&
             CRYPTO_memcmp(tag, expected[n], 16) == 0 &&
             EVP_CIPHER_CTX_reset(ctx) == 1 && init_evp(ctx, c, key, iv, 0) &&
             (!n || EVP_CipherUpdate(ctx, NULL, &written, &aad, 1) == 1) &&
             EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, 16, tag) == 1 &&
             EVP_CipherFinal_ex(ctx, out, &written) == 1 && written == 0;
    }
    if (ok) {
        ok = EVP_CIPHER_CTX_reset(ctx) == 1 && init_evp(ctx, c, key, iv, 1) &&
             EVP_CipherUpdate(ctx, out, &written, &plaintext, 1) == 1 &&
             written >= 0 && written <= 1 &&
             EVP_CipherFinal_ex(ctx, out + written, &final_written) == 1 &&
             final_written >= 0 && final_written == 1 - written &&
             CRYPTO_memcmp(out, expected_message, 1) == 0 &&
             EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_GET_TAG, 16, tag) == 1 &&
             CRYPTO_memcmp(tag, expected_message + 1, 16) == 0 &&
             EVP_CIPHER_CTX_reset(ctx) == 1 && init_evp(ctx, c, key, iv, 0) &&
             EVP_CipherUpdate(ctx, out, &written, expected_message, 1) == 1 &&
             written >= 0 && written <= 1 &&
             EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, 16, tag) == 1 &&
             EVP_CipherFinal_ex(ctx, out + written, &final_written) == 1 &&
             final_written >= 0 && final_written == 1 - written &&
             CRYPTO_memcmp(out, &plaintext, 1) == 0;
    }
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(key, sizeof(key));
    OPENSSL_cleanse(tag, sizeof(tag));
    OPENSSL_cleanse(out, sizeof(out));
    return ok;
}

static int xts_stream_probe_length(const symmetric_cipher *c, unsigned int length) {
    EVP_CIPHER_CTX *single = EVP_CIPHER_CTX_new(), *split = EVP_CIPHER_CTX_new();
    unsigned char key[MAX_KEY] = {0}, iv[BLOCK] = {0};
    unsigned char plain[79], encrypted[96], divided[96], recovered[96];
    int a, b, d, e, f, ok = 0;
    if (!single || !split) {
        EVP_CIPHER_CTX_free(single);
        EVP_CIPHER_CTX_free(split);
        return -1;
    }
    key[c->key_len - 1] = 1;
    for (unsigned int i = 0; i < sizeof(plain); ++i) plain[i] = (unsigned char)i;
    if (!init_evp(single, c, key, iv, 1) ||
        !init_evp(split, c, key, iv, 1) ||
        EVP_CipherUpdate(single, encrypted, &a, plain, (int)length) != 1 ||
        EVP_CipherFinal_ex(single, encrypted + a, &f) != 1 || a + f != (int)length ||
        EVP_CipherUpdate(split, divided, &b, plain, 16) != 1 || b != 16 ||
        EVP_CipherUpdate(split, divided + b, &d, plain + 16, 16) != 1 || d != 16 ||
        EVP_CipherUpdate(split, divided + b + d, &e, plain + 32, (int)length - 32) != 1 ||
        EVP_CipherFinal_ex(split, divided + b + d + e, &f) != 1 ||
        b + d + e + f != (int)length ||
        CRYPTO_memcmp(encrypted, divided, length) != 0 ||
        EVP_CIPHER_CTX_reset(single) != 1 || EVP_CIPHER_CTX_reset(split) != 1 ||
        !init_evp(single, c, key, iv, 0) || !init_evp(split, c, key, iv, 0) ||
        EVP_CipherUpdate(single, recovered, &a, encrypted, (int)length) != 1 ||
        EVP_CipherFinal_ex(single, recovered + a, &f) != 1 || a + f != (int)length ||
        CRYPTO_memcmp(recovered, plain, length) != 0 ||
        EVP_CipherUpdate(split, divided, &b, encrypted, 16) != 1 || b != 16 ||
        EVP_CipherUpdate(split, divided + b, &d, encrypted + 16, 16) != 1 || d != 16 ||
        EVP_CipherUpdate(split, divided + b + d, &e, encrypted + 32, (int)length - 32) != 1 ||
        EVP_CipherFinal_ex(split, divided + b + d + e, &f) != 1 ||
        b + d + e + f != (int)length ||
        CRYPTO_memcmp(divided, plain, length) != 0) goto done;
    ok = 1;
done:
    EVP_CIPHER_CTX_free(single);
    EVP_CIPHER_CTX_free(split);
    OPENSSL_cleanse(key, sizeof(key));
    OPENSSL_cleanse(plain, sizeof(plain));
    OPENSSL_cleanse(encrypted, sizeof(encrypted));
    OPENSSL_cleanse(divided, sizeof(divided));
    OPENSSL_cleanse(recovered, sizeof(recovered));
    return ok;
}

static int xts_stream_probe(const symmetric_cipher *c) {
    int result = xts_stream_probe_length(c, 64);
    if (result <= 0) return result;
    return xts_stream_probe_length(c, 79);
}

static int chacha_stream_probe(const symmetric_cipher *c, int *internal_error) {
    static const unsigned char expected[16] = {
        0x76,0xb8,0xe0,0xad,0xa0,0xf1,0x3d,0x90,
        0x40,0x5d,0x6a,0xe5,0x53,0x86,0xbd,0x28
    };
    unsigned char key[32] = {0}, iv[16] = {0}, input[128] = {0};
    unsigned char out[128], second[64];
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    *internal_error = ctx == NULL;
    int n = 0, m = 0, ok = 0;
    if (!ctx || !init_evp(ctx, c, key, iv, 1) ||
        EVP_CipherUpdate(ctx, out, &n, input, 64) != 1 || n != 64 ||
        CRYPTO_memcmp(out, expected, sizeof(expected)) != 0) goto done;
    memset(iv, 0xff, 4);
    if (EVP_CIPHER_CTX_reset(ctx) != 1 || !init_evp(ctx, c, key, iv, 1) ||
        EVP_CipherUpdate(ctx, out, &n, input, 128) != 1 || n != 128) goto done;
    memset(iv, 0, 4);
    iv[4] = 1;
    if (EVP_CIPHER_CTX_reset(ctx) != 1 || !init_evp(ctx, c, key, iv, 1) ||
        EVP_CipherUpdate(ctx, second, &m, input, 64) != 1 || m != 64 ||
        CRYPTO_memcmp(out + 64, second, 64) != 0) goto done;
    ok = 1;
done:
    EVP_CIPHER_CTX_free(ctx);
    OPENSSL_cleanse(out, sizeof(out));
    OPENSSL_cleanse(second, sizeof(second));
    return ok;
}

static int begin_error_scope(unsigned long *sentinel) {
    int marked = ERR_set_mark();
    ERR_raise(ERR_LIB_USER, 0x4b1);
    *sentinel = ERR_peek_last_error();
    return marked;
}

static void end_error_scope(int marked) {
    if (marked) ERR_pop_to_mark(); else ERR_clear_error();
}

static int probe_failure_status(unsigned long previous_error, int discovery) {
    if (discovery) return discovery_probe_status(1, 0);
    unsigned long error = ERR_peek_last_error();
    if (!error || error == previous_error) return INVALID;
    return expected_probe_error(error, 0) ? INVALID : OPERATION;
}

static EVP_CIPHER *fetch_cipher(const char *name, int discovery,
                                unsigned long previous_error, int *status) {
    /* Discovery runs on an isolated worker, so its entire error queue can be inspected. */
    if (discovery && ERR_peek_error() != 0) return NULL;
    EVP_CIPHER *cipher = EVP_CIPHER_fetch(NULL, name, NULL);
    if (!discovery) {
        if (!cipher) {
            unsigned long error = ERR_peek_last_error();
            if (error != previous_error && ERR_GET_LIB(error) == ERR_LIB_EVP &&
                ERR_GET_REASON(error) == ERR_R_UNSUPPORTED)
                *status = INVALID;
        }
        return cipher;
    }

    int unsupported = 0, other = 0;
    unsigned long error;
    while ((error = ERR_get_error()) != 0) {
        int reason = ERR_GET_REASON(error);
        if (ERR_GET_LIB(error) == ERR_LIB_EVP &&
            (reason == ERR_R_UNSUPPORTED || reason == EVP_R_UNSUPPORTED_ALGORITHM))
            unsupported = 1;
        else
            other = 1;
    }
    if (cipher && !unsupported && !other) return cipher;
    if (cipher) {
        EVP_CIPHER_free(cipher);
        *status = OPERATION;
    } else {
        *status = unsupported && !other ? INVALID : OPERATION;
    }
    return NULL;
}

static symmetric_cipher *cipher_new(int algorithm, unsigned int bits, int mode,
                                    unsigned int iv_len, unsigned int tag_len,
                                    int padding, int *status, int discovery) {
    unsigned long previous_error = ERR_peek_last_error();
    *status = OPERATION;
    if (discovery && ERR_peek_error() != 0) return NULL;
    int marked = ERR_set_mark();
    symmetric_cipher *c = OPENSSL_zalloc(sizeof(*c));
    if (!c) {
        end_error_scope(marked);
        return NULL;
    }
    if (!cipher_parameters(c, algorithm, bits, mode, iv_len, tag_len, padding)) {
        *status = INVALID;
        goto fail;
    }
    c->cipher = fetch_cipher(c->name, discovery, previous_error, status);
    if (!c->cipher) goto fail;
    if (!cipher_matches(c)) {
        *status = discovery ? probe_failure_status(previous_error, 1) : INVALID;
        goto fail;
    }
    if (discovery && discovery_probe_status(0, 0) != OK) goto fail;
    if (c->mode == CCM) {
        /* CCM is composed privately from the fetched block primitive. It is not
           a provider-validated or FIPS-validated CCM implementation. */
        char block_name[64];
        if (algorithm == SM4) snprintf(block_name, sizeof(block_name), "SM4-ECB");
        else snprintf(block_name, sizeof(block_name), "%s-%u-ECB",
                      algorithm == AES ? "AES" : algorithm == ARIA ? "ARIA" : "CAMELLIA",
                      bits);
        c->block = fetch_cipher(block_name, discovery, previous_error, status);
        if (!c->block) goto fail;
        if (EVP_CIPHER_get_block_size(c->block) != BLOCK ||
            EVP_CIPHER_get_key_length(c->block) != (int)c->key_len) {
            *status = discovery ? probe_failure_status(previous_error, 1) : INVALID;
            goto fail;
        }
        if (discovery && discovery_probe_status(0, 0) != OK) goto fail;
        unsigned char key[MAX_KEY] = {0}, iv[13] = {0}, tag[16] = {0};
        k_ccm_ctx *probe = NULL;
        int probe_status = k_ccm_new(c->block, key, c->key_len, iv, c->iv_len, c->tag_len,
                                     0, 0, 1, &probe);
        if (probe_status == OK) probe_status = k_ccm_final(probe, tag);
        k_ccm_free(probe);
        probe = NULL;
        if (probe_status == OK)
            probe_status = k_ccm_new(c->block, key, c->key_len, iv, c->iv_len, c->tag_len,
                                     0, 0, 0, &probe);
        if (probe_status == OK) probe_status = k_ccm_final(probe, tag);
        k_ccm_free(probe);
        OPENSSL_cleanse(key, sizeof(key));
        OPENSSL_cleanse(tag, sizeof(tag));
        if (probe_status != OK) {
            if (probe_status != OPERATION)
                *status = discovery ? probe_failure_status(previous_error, 1) : INVALID;
            goto fail;
        }
        if (discovery && discovery_probe_status(0, 0) != OK) goto fail;
    } else {
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        unsigned char key[MAX_KEY] = {0};
        unsigned char *iv = OPENSSL_zalloc(c->iv_len);
        int probe_internal_error = 0;
        key[c->key_len - 1] = 1;
        int good = ctx && iv &&
                   (c->mode == XTS
                    ? (init_evp(ctx, c, key, iv, 1) && EVP_CIPHER_CTX_reset(ctx) == 1 &&
                       init_evp(ctx, c, key, iv, 0))
                    : cipher_probe(c, ctx, key, iv, discovery, &probe_internal_error));
        int allocated = ctx != NULL && iv != NULL;
        EVP_CIPHER_CTX_free(ctx);
        OPENSSL_clear_free(iv, c->iv_len);
        OPENSSL_cleanse(key, sizeof(key));
        if (!good) {
            if (allocated && !probe_internal_error)
                *status = probe_failure_status(previous_error, discovery);
            goto fail;
        }
        if (discovery && discovery_probe_status(0, 0) != OK) goto fail;
        if (c->mode == XTS) {
            c->xts_streaming = xts_stream_probe(c);
            if (c->xts_streaming < 0) goto fail;
            if (discovery &&
                discovery_probe_status(!c->xts_streaming, 0) !=
                    (c->xts_streaming ? OK : INVALID))
                goto fail;
        }
        previous_error = ERR_peek_last_error();
        int internal_error = 0;
        if (c->mode == ASCON && !ascon_known_answer(c, &internal_error)) {
            if (!internal_error) *status = probe_failure_status(previous_error, discovery);
            goto fail;
        }
        if (discovery && discovery_probe_status(0, 0) != OK) goto fail;
        if (c->mode == STREAM && !chacha_stream_probe(c, &internal_error)) {
            if (!internal_error) *status = probe_failure_status(previous_error, discovery);
            goto fail;
        }
        if (discovery && discovery_probe_status(0, 0) != OK) goto fail;
    }
    *status = OK;
    end_error_scope(marked);
    return c;
fail:
    EVP_CIPHER_free(c->cipher);
    EVP_CIPHER_free(c->block);
    OPENSSL_free(c);
    end_error_scope(marked);
    return NULL;
}

symmetric_cipher *__k_symmetric_cipher_new(int algorithm, unsigned int bits, int mode,
                                             unsigned int iv_len, unsigned int tag_len,
                                             int padding) {
    int status;
    return cipher_new(algorithm, bits, mode, iv_len, tag_len, padding, &status, 0);
}

symmetric_cipher *__k_symmetric_cipher_create(const unsigned char *algorithm,
        unsigned int bits, const unsigned char *mode, unsigned int iv_len,
        unsigned int tag_len, const unsigned char *padding, int defaults) {
    const cipher_adapter *a = find_adapter_names((const char *)algorithm, bits, (const char *)mode);
    if (!a) return NULL;
    int pad;
    if (defaults) {
        iv_len = a->default_iv;
        tag_len = a->default_tag;
        pad = a->default_padding;
    } else {
        if (!padding) return NULL;
        if (!strcmp((const char *)padding, "NONE")) pad = NO_PADDING;
        else if (!strcmp((const char *)padding, "PKCS7")) pad = PKCS7;
        else return NULL;
    }
    return __k_symmetric_cipher_new(a->algorithm, bits, a->mode, iv_len, tag_len, pad);
}

void __k_symmetric_cipher_free(symmetric_cipher *c) {
    if (!c) return;
    EVP_CIPHER_free(c->cipher);
    EVP_CIPHER_free(c->block);
    OPENSSL_free(c);
}

uint64_t __k_symmetric_cipher_info(const symmetric_cipher *c, int field) {
    if (!c) return 0;
    switch (field) {
        case 0: return c->key_len;
        case 1: return c->iv_len;
        case 2: return c->tag_len;
        case 3: return c->authenticated;
        case 4: return c->max_data;
        case 5: return c->max_aad;
        case 6: return (uint64_t)c->algorithm;
        case 7: return (uint64_t)c->mode;
        case 8: return c->bits;
        case 9: return (uint64_t)c->padding;
        case 10: return (uint64_t)c->xts_streaming;
        case 11: return strlen(c->name);
        default: return 0;
    }
}

void __k_symmetric_cipher_name(const symmetric_cipher *c, unsigned char *out) {
    memcpy(out, c->name, strlen(c->name) + 1);
}

static const char *cipher_field(const symmetric_cipher *c, int field) {
    if (!c || !c->adapter) return NULL;
    if (field == 0) return c->adapter->algorithm_name;
    if (field == 1) return c->adapter->mode_name;
    if (field == 2) return c->padding == PKCS7 ? "PKCS7" : "NONE";
    return NULL;
}

unsigned int __k_symmetric_cipher_field_size(const symmetric_cipher *c, int field) {
    const char *value = cipher_field(c, field);
    return value ? (unsigned int)strlen(value) : 0;
}

void __k_symmetric_cipher_field_copy(const symmetric_cipher *c, int field, unsigned char *out) {
    const char *value = cipher_field(c, field);
    if (value && out) memcpy(out, value, strlen(value) + 1);
}

symmetric_cipher *__k_symmetric_cipher_from_name(const unsigned char *name) {
    if (!name) return NULL;
    for (size_t i = 0; i < ADAPTER_COUNT; ++i) {
        const cipher_adapter *a = &adapters[i];
        for (unsigned int bits = 128; bits <= 256; bits += 64) {
            symmetric_cipher candidate = {0};
            if (cipher_parameters(&candidate, a->algorithm, bits, a->mode,
                                  a->default_iv, a->default_tag, a->default_padding) &&
                (!strcmp(candidate.name, (const char *)name) ||
                 (a->algorithm == CHACHA20 && a->mode == POLY1305 &&
                  !strcmp((const char *)name, "CHACHA20-POLY1305"))))
                return __k_symmetric_cipher_new(a->algorithm, bits, a->mode,
                                                a->default_iv, a->default_tag, a->default_padding);
        }
    }
    return NULL;
}

int __k_symmetric_random(unsigned char *out, unsigned int length) {
    if ((length && !out) || length > INT_MAX) return INVALID;
    return !length || RAND_bytes(out, (int)length) == 1 ? OK : OPERATION;
}

symmetric_key *__k_symmetric_key_new(const symmetric_cipher *c, const unsigned char *data,
                                     int generate, int *status) {
    *status = INVALID;
    if (!c || (generate != 0 && generate != 1) || (!generate && !data)) return NULL;
    symmetric_key *key = OPENSSL_zalloc(sizeof(*key));
    if (!key) { *status = OPERATION; return NULL; }
    key->algorithm = c->algorithm;
    key->bits = c->bits;
    key->xts = c->mode == XTS;
    key->length = c->key_len;
    if (generate) {
        do {
            if (RAND_priv_bytes(key->bytes, (int)key->length) != 1) {
                *status = OPERATION;
                goto fail;
            }
        } while (key->xts && CRYPTO_memcmp(key->bytes, key->bytes + key->length / 2,
                                           key->length / 2) == 0);
    } else {
        memcpy(key->bytes, data, key->length);
        if (key->xts && CRYPTO_memcmp(key->bytes, key->bytes + key->length / 2,
                                      key->length / 2) == 0)
            goto fail;
    }
    *status = OK;
    return key;
fail:
    OPENSSL_clear_free(key, sizeof(*key));
    return NULL;
}

void __k_symmetric_key_free(symmetric_key *key) {
    if (key) OPENSSL_clear_free(key, sizeof(*key));
}

const unsigned char *__k_symmetric_key_borrow(const symmetric_key *key) {
    return key ? key->bytes : NULL;
}

static int spool_init(spool *s, int file_mode) {
    s->file_mode = file_mode;
    if (!file_mode) return OK;
    s->file = tmpfile();
    if (!s->file) return OPERATION;
    int fd = fileno(s->file), flags = fcntl(fd, F_GETFD);
    if (fd < 0 || flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0 ||
        fchmod(fd, S_IRUSR | S_IWUSR) != 0) return OPERATION;
    return OK;
}

static int spool_clear(spool *s) {
    int rc = OK;
    chunk *p = s->head;
    while (p) {
        chunk *next = p->next;
        OPENSSL_clear_free(p, sizeof(*p));
        p = next;
    }
    s->head = s->tail = NULL;
    if (s->file && fclose(s->file) != 0) rc = OPERATION;
    s->file = NULL;
    s->read_pos = s->write_pos = 0;
    return rc;
}

static int spool_write(spool *s, const unsigned char *data, size_t len) {
    if ((uint64_t)len > UINT64_MAX - s->write_pos) return INVALID;
    if (!len) return OK;
    if (s->file_mode) {
        if (s->write_pos > INT64_MAX || len > INT64_MAX - s->write_pos ||
            fseeko(s->file, (off_t)s->write_pos, SEEK_SET) != 0 ||
            fwrite(data, 1, len, s->file) != len) return OPERATION;
        s->write_pos += len;
        return OK;
    }
    size_t original = len;
    while (len) {
        if (!s->tail || s->tail->length == CHUNK) {
            chunk *p = OPENSSL_zalloc(sizeof(*p));
            if (!p) return OPERATION;
            if (s->tail) s->tail->next = p;
            else s->head = p;
            s->tail = p;
        }
        size_t n = CHUNK - s->tail->length;
        if (n > len) n = len;
        memcpy(s->tail->bytes + s->tail->length, data, n);
        s->tail->length += n;
        data += n;
        len -= n;
    }
    s->write_pos += original;
    return OK;
}

static int spool_read(spool *s, unsigned char *out, size_t len, size_t *count) {
    *count = 0;
    uint64_t remaining = s->write_pos - s->read_pos;
    if ((uint64_t)len > remaining) len = (size_t)remaining;
    if (!len) return OK;
    if (s->file_mode) {
        if (fflush(s->file) != 0 || s->read_pos > INT64_MAX ||
            fseeko(s->file, (off_t)s->read_pos, SEEK_SET) != 0 ||
            fread(out, 1, len, s->file) != len) return OPERATION;
        s->read_pos += len;
        if (s->read_pos == s->write_pos) {
            int fd = fileno(s->file);
            if (fd < 0 || fseeko(s->file, 0, SEEK_SET) != 0 ||
                ftruncate(fd, 0) != 0) return OPERATION;
            s->read_pos = s->write_pos = 0;
        }
        *count = len;
        return OK;
    }
    while (*count < len) {
        chunk *p = s->head;
        if (!p) return OPERATION;
        size_t n = p->length - p->read;
        if (n > len - *count) n = len - *count;
        memcpy(out + *count, p->bytes + p->read, n);
        OPENSSL_cleanse(p->bytes + p->read, n);
        p->read += n;
        *count += n;
        if (p->read == p->length) {
            s->head = p->next;
            if (!s->head) s->tail = NULL;
            OPENSSL_clear_free(p, sizeof(*p));
        }
    }
    s->read_pos += *count;
    return OK;
}

static int lock_session(symmetric_session *s) {
    return !s || atomic_exchange_explicit(&s->busy, 1, memory_order_acquire) ? STATE : OK;
}
static void unlock_session(symmetric_session *s) {
    atomic_store_explicit(&s->busy, 0, memory_order_release);
}

static int clear_session(symmetric_session *s) {
    int rc = spool_clear(&s->aad);
    if (spool_clear(&s->input) != OK) rc = OPERATION;
    if (spool_clear(&s->output) != OK) rc = OPERATION;
    EVP_CIPHER_CTX_free(s->ctx);
    s->ctx = NULL;
    k_ccm_free(s->ccm);
    s->ccm = NULL;
    EVP_CIPHER_free(s->cipher);
    s->cipher = NULL;
    EVP_CIPHER_free(s->block);
    s->block = NULL;
    OPENSSL_cleanse(s->key, sizeof(s->key));
    OPENSSL_cleanse(s->tag, sizeof(s->tag));
    OPENSSL_cleanse(s->tail, sizeof(s->tail));
    OPENSSL_cleanse(s->aad_tail, sizeof(s->aad_tail));
    s->aad_tail_len = 0;
    return rc;
}

void __k_symmetric_session_free(symmetric_session *s) {
    if (!s) return;
    clear_session(s);
    OPENSSL_clear_free(s->iv, s->iv_len);
    OPENSSL_clear_free(s, sizeof(*s));
}

static uint64_t chacha_stream_limit(const unsigned char *iv) {
    uint64_t counter = 0;
    for (unsigned int i = 0; i < 8; ++i)
        counter |= (uint64_t)iv[i] << (8 * i);
    uint64_t remaining_minus_one = UINT64_MAX - counter;
    if (remaining_minus_one >= UINT64_MAX / 64) return UINT64_MAX;
    return (remaining_minus_one + 1) * 64;
}

symmetric_session *__k_symmetric_session_new(const symmetric_cipher *c,
        const symmetric_key *key, const unsigned char *iv, int encrypt, int file_staging,
        int known_length, uint64_t expected_length, int *status) {
    *status = INVALID;
    if (!c || !key || !iv || key->algorithm != c->algorithm || key->bits != c->bits ||
        key->xts != (c->mode == XTS) || key->length != c->key_len ||
        (encrypt != 0 && encrypt != 1) || (file_staging != 0 && file_staging != 1) ||
        (known_length != 0 && known_length != 1) ||
        (known_length && (expected_length > c->max_data ||
         (c->mode == STREAM && expected_length > chacha_stream_limit(iv))))) return NULL;
    *status = OPERATION;
    symmetric_session *s = OPENSSL_zalloc(sizeof(*s));
    if (!s) return NULL;
    atomic_init(&s->busy, 0);
    s->encrypt = encrypt;
    s->mode = c->mode;
    s->padding = c->padding;
    s->authenticated = c->authenticated;
    s->xts_streaming = c->xts_streaming;
    s->key_len = c->key_len;
    s->iv_len = c->iv_len;
    s->tag_len = c->tag_len;
    s->max_data = c->max_data;
    s->max_aad = c->max_aad;
    s->known_length = known_length;
    s->expected_length = expected_length;
    memcpy(s->key, key->bytes, c->key_len);
    s->iv = OPENSSL_malloc(c->iv_len);
    if (!s->iv) goto fail;
    memcpy(s->iv, iv, c->iv_len);
    if (EVP_CIPHER_up_ref(c->cipher) != 1) goto fail;
    s->cipher = c->cipher;
    if (c->block) {
        if (EVP_CIPHER_up_ref(c->block) != 1) goto fail;
        s->block = c->block;
    }
    if (spool_init(&s->output, file_staging) != OK ||
        (s->mode == CCM && spool_init(&s->aad, file_staging) != OK) ||
        (((s->mode == CCM && !known_length) || (s->mode == XTS && !s->xts_streaming)) &&
         spool_init(&s->input, file_staging) != OK)) goto fail;
    if (s->mode != CCM) {
        s->ctx = EVP_CIPHER_CTX_new();
        if (!s->ctx || !init_evp(s->ctx, c, key->bytes, iv, encrypt)) goto fail;
    }
    *status = OK;
    return s;
fail:
    __k_symmetric_session_free(s);
    return NULL;
}

int __k_symmetric_close(symmetric_session *s) {
    int rc = lock_session(s);
    if (rc != OK) return rc;
    if (s->state != 4) {
        rc = clear_session(s);
        s->state = 4;
    }
    unlock_session(s);
    return rc;
}

int __k_symmetric_nonce(symmetric_session *s, unsigned char *out) {
    int rc = lock_session(s);
    if (rc != OK) return rc;
    if (!out) rc = INVALID;
    else memcpy(out, s->iv, s->iv_len);
    unlock_session(s);
    return rc;
}

int __k_symmetric_capacity(symmetric_session *s, unsigned int len, unsigned int *capacity) {
    int rc = lock_session(s);
    if (rc != OK) return rc;
    if (!s->encrypt || s->state > 1) rc = STATE;
    else if (!len || (s->mode == CCM && !s->known_length) ||
             (s->mode == XTS && !s->xts_streaming)) *capacity = 0;
    else if (s->mode == CBC || s->mode == OCB || s->mode == XTS || s->mode == ASCON) {
        if (len > UINT_MAX - BLOCK) rc = INVALID;
        else *capacity = len + BLOCK;
    } else *capacity = len;
    unlock_session(s);
    return rc;
}

int __k_symmetric_incremental_output(const symmetric_session *s) {
    if (!s || (!s->encrypt && s->authenticated)) return 0;
    if (s->mode == CCM && !s->known_length) return 0;
    if (s->mode == XTS && !s->xts_streaming) return 0;
    return 1;
}

uint64_t __k_symmetric_session_max_data(const symmetric_session *s) {
    if (!s) return 0;
    if (s->mode == STREAM) return s->iv ? chacha_stream_limit(s->iv) : 0;
    if (s->mode != CTR) return s->max_data;
    for (unsigned int i = 0; i < 8; ++i)
        if (s->iv[i] != 0xff) return UINT64_MAX;
    uint64_t low = 0;
    for (unsigned int i = 8; i < 16; ++i) low = (low << 8) | s->iv[i];
    uint64_t remaining_minus_one = ~low;
    if (remaining_minus_one >= UINT64_MAX / BLOCK) return UINT64_MAX;
    return (remaining_minus_one + 1) * BLOCK;
}

static int fail_session(symmetric_session *s, int rc) {
    s->state = 3;
    clear_session(s);
    return rc;
}

static int ocb_aad_update(symmetric_session *s, const unsigned char *data,
                          unsigned int len) {
    unsigned int off = 0;
    int written = 0;
    if (s->aad_tail_len) {
        unsigned int n = BLOCK - s->aad_tail_len;
        if (n > len) n = len;
        if (n) memcpy(s->aad_tail + s->aad_tail_len, data, n);
        s->aad_tail_len += n;
        off += n;
        if (s->aad_tail_len == BLOCK) {
            if (EVP_CipherUpdate(s->ctx, NULL, &written, s->aad_tail, BLOCK) != 1)
                return OPERATION;
            OPENSSL_cleanse(s->aad_tail, sizeof(s->aad_tail));
            s->aad_tail_len = 0;
        }
    }
    while (len - off >= BLOCK) {
        unsigned int n = len - off;
        if (n > CHUNK) n = CHUNK;
        n -= n % BLOCK;
        if (EVP_CipherUpdate(s->ctx, NULL, &written, data + off, (int)n) != 1)
            return OPERATION;
        off += n;
    }
    if (off < len) {
        s->aad_tail_len = len - off;
        memcpy(s->aad_tail, data + off, s->aad_tail_len);
    }
    return OK;
}

int __k_symmetric_aad(symmetric_session *s, const unsigned char *data, unsigned int len) {
    int rc = lock_session(s);
    if (rc != OK) return rc;
    if (s->state != 0) rc = STATE;
    else if (!s->authenticated || (len && !data) ||
             (uint64_t)len > s->max_aad - s->aad_length) rc = INVALID;
    else if (s->mode == CCM) {
        rc = spool_write(&s->aad, data, len);
    } else if (s->mode == OCB) {
        rc = ocb_aad_update(s, data, len);
    } else {
        unsigned int off = 0;
        while (off < len) {
            unsigned int n = len - off > CHUNK ? CHUNK : len - off;
            int written = 0;
            if (EVP_CipherUpdate(s->ctx, NULL, &written, data + off, (int)n) != 1) {
                rc = OPERATION;
                break;
            }
            off += n;
        }
    }
    if (rc == OK) s->aad_length += len;
    else if (rc == OPERATION) fail_session(s, rc);
    unlock_session(s);
    return rc;
}

static int start_ccm(symmetric_session *s, uint64_t length) {
    int rc = k_ccm_new(s->block, s->key, s->key_len, s->iv, s->iv_len, s->tag_len,
                       length, s->aad_length, s->encrypt, &s->ccm);
    if (rc != OK) return rc;
    unsigned char buffer[CHUNK];
    size_t n;
    while (s->aad.read_pos < s->aad.write_pos) {
        if ((rc = spool_read(&s->aad, buffer, sizeof(buffer), &n)) != OK ||
            (rc = k_ccm_aad_update(s->ccm, buffer, n)) != OK) break;
    }
    OPENSSL_cleanse(buffer, sizeof(buffer));
    if (spool_clear(&s->aad) != OK && rc == OK) rc = OPERATION;
    return rc;
}

static int start_ocb_data(symmetric_session *s) {
    if (!s->aad_tail_len) return OK;
    int written = 0;
    if (EVP_CipherUpdate(s->ctx, NULL, &written, s->aad_tail,
                         (int)s->aad_tail_len) != 1) return OPERATION;
    OPENSSL_cleanse(s->aad_tail, sizeof(s->aad_tail));
    s->aad_tail_len = 0;
    return OK;
}

static int counter_fits(const unsigned char *iv, uint64_t bytes) {
    if (!bytes) return 1;
    uint64_t blocks = (bytes - 1) / BLOCK;
    unsigned int carry = 0;
    for (int i = 15; i >= 0; --i) {
        unsigned int add = (unsigned int)(blocks & 0xff);
        unsigned int sum = iv[i] + add + carry;
        carry = sum >> 8;
        blocks >>= 8;
    }
    return blocks == 0 && carry == 0;
}

static int xts_update(symmetric_session *s, const unsigned char *data, unsigned int len,
                      unsigned char *out, unsigned int capacity, unsigned int *count) {
    unsigned char input[CHUNK + 2 * BLOCK], output[CHUNK + 2 * BLOCK];
    unsigned int off = 0;
    int rc = OK;
    while (off < len) {
        unsigned int n = len - off;
        if (n > CHUNK) n = CHUNK;
        memcpy(input, s->tail, s->tail_len);
        memcpy(input + s->tail_len, data + off, n);
        unsigned int total = s->tail_len + n;
        unsigned int processed = total >= 2 * BLOCK ? ((total - BLOCK) / BLOCK) * BLOCK : 0;
        if (processed) {
            int written = 0;
            if (EVP_CipherUpdate(s->ctx, output, &written, input, (int)processed) != 1 ||
                written != (int)processed) {
                rc = OPERATION;
                break;
            }
            if (s->encrypt) {
                if ((unsigned int)written > capacity - *count) { rc = INVALID; break; }
                memcpy(out + *count, output, (size_t)written);
                *count += (unsigned int)written;
            } else if ((rc = spool_write(&s->output, output, (size_t)written)) != OK) break;
        }
        s->tail_len = total - processed;
        memcpy(s->tail, input + processed, s->tail_len);
        off += n;
    }
    OPENSSL_cleanse(input, sizeof(input));
    OPENSSL_cleanse(output, sizeof(output));
    return rc;
}

int __k_symmetric_update(symmetric_session *s, const unsigned char *data,
                         unsigned int len, unsigned char *out, unsigned int capacity,
                         unsigned int *count) {
    int rc = lock_session(s);
    if (rc != OK) return rc;
    *count = 0;
    if (s->state > 1) rc = STATE;
    else if ((len && !data) || (s->encrypt && capacity && !out) ||
             (uint64_t)len > s->max_data - s->data_length ||
             (s->known_length && (uint64_t)len > s->expected_length - s->data_length) ||
             (s->mode == CTR && !counter_fits(s->iv, s->data_length + len)) ||
             (s->mode == STREAM &&
              s->data_length + len > chacha_stream_limit(s->iv)))
        rc = INVALID;
    else {
        if (s->state == 0 && s->mode == OCB) rc = start_ocb_data(s);
        if (s->state == 0 && s->mode == CCM && s->known_length)
            rc = start_ccm(s, s->expected_length);
        if (rc == OK) s->state = 1;
        unsigned int off = 0;
        unsigned char buffer[CHUNK + BLOCK];
        if (rc == OK && s->mode == XTS && s->xts_streaming)
            rc = xts_update(s, data, len, out, capacity, count);
        while (rc == OK && off < len && !(s->mode == XTS && s->xts_streaming)) {
            unsigned int n = len - off > CHUNK ? CHUNK : len - off;
            int written = 0;
            if ((s->mode == CCM && !s->known_length) ||
                (s->mode == XTS && !s->xts_streaming)) {
                rc = spool_write(&s->input, data + off, n);
            } else if (s->mode == CCM) {
                rc = k_ccm_update(s->ccm, data + off, buffer, n);
                written = (int)n;
            } else if (EVP_CipherUpdate(s->ctx, buffer, &written, data + off, (int)n) != 1) {
                rc = OPERATION;
            }
            if (rc == OK && (written < 0 || written > (int)n + BLOCK)) rc = OPERATION;
            if (rc == OK && written) {
                if (s->encrypt) {
                    if ((unsigned int)written > capacity - *count) rc = INVALID;
                    else {
                        memcpy(out + *count, buffer, (size_t)written);
                        *count += (unsigned int)written;
                    }
                } else rc = spool_write(&s->output, buffer, (size_t)written);
            }
            off += n;
        }
        OPENSSL_cleanse(buffer, sizeof(buffer));
        if (rc == OK) s->data_length += len;
        else fail_session(s, rc);
    }
    unlock_session(s);
    return rc;
}

static int finish_ccm(symmetric_session *s, unsigned char *tag) {
    int rc = OK;
    unsigned char input[CHUNK], output[CHUNK];
    size_t n;
    if (!s->ccm && (rc = start_ccm(s, s->data_length)) != OK) return rc;
    while (s->input.read_pos < s->input.write_pos) {
        if ((rc = spool_read(&s->input, input, sizeof(input), &n)) != OK ||
            (rc = k_ccm_update(s->ccm, input, output, n)) != OK ||
            (rc = spool_write(&s->output, output, n)) != OK) break;
    }
    if (rc == OK) rc = k_ccm_final(s->ccm, tag);
    OPENSSL_cleanse(input, sizeof(input));
    OPENSSL_cleanse(output, sizeof(output));
    return rc;
}

static int finish_xts(symmetric_session *s) {
    if (s->data_length < BLOCK || s->data_length > XTS_MAX_DATA) return INVALID;
    if (!s->xts_streaming) {
        size_t len = (size_t)s->data_length, n = 0;
        unsigned char *input = OPENSSL_malloc(len), *output = OPENSSL_malloc(len + BLOCK);
        int written = 0, final = 0, rc = OPERATION;
        if (input && output && spool_read(&s->input, input, len, &n) == OK && n == len &&
            EVP_CipherUpdate(s->ctx, output, &written, input, (int)len) == 1 &&
            written >= 0 && (size_t)written <= len &&
            EVP_CipherFinal_ex(s->ctx, output + written, &final) == 1 &&
            final >= 0 && (size_t)(written + final) == len &&
            spool_write(&s->output, output, len) == OK) rc = OK;
        if (input) OPENSSL_clear_free(input, len);
        if (output) OPENSSL_clear_free(output, len + BLOCK);
        return rc;
    }
    unsigned char output[3 * BLOCK];
    int written = 0, final = 0, rc = OPERATION;
    if (EVP_CipherUpdate(s->ctx, output, &written, s->tail, (int)s->tail_len) == 1 &&
        written >= 0 && written <= (int)sizeof(output) - BLOCK &&
        EVP_CipherFinal_ex(s->ctx, output + written, &final) == 1 &&
        final >= 0 && (unsigned int)(written + final) == s->tail_len &&
        spool_write(&s->output, output, (size_t)(written + final)) == OK)
        rc = OK;
    OPENSSL_cleanse(output, sizeof(output));
    return rc;
}

int __k_symmetric_finish(symmetric_session *s, const unsigned char *tag,
                         unsigned int tag_len) {
    int rc = lock_session(s);
    if (rc != OK) return rc;
    if (s->state > 1) rc = STATE;
    else if ((!s->encrypt && (tag_len != s->tag_len || (tag_len && !tag))) ||
             (s->encrypt && tag_len != 0)) rc = INVALID;
    else if (s->known_length && s->data_length != s->expected_length) rc = INVALID;
    else {
        unsigned char final[BLOCK + BLOCK];
        int written = 0;
        if (!s->encrypt && s->tag_len) memcpy(s->tag, tag, s->tag_len);
        if (s->mode == CCM) rc = finish_ccm(s, s->tag);
        else if (s->mode == XTS) rc = finish_xts(s);
        else {
            if (s->state == 0 && s->mode == OCB) rc = start_ocb_data(s);
            if (rc == OK && !s->encrypt && s->authenticated &&
                EVP_CIPHER_CTX_ctrl(s->ctx, EVP_CTRL_AEAD_SET_TAG,
                                    (int)s->tag_len, s->tag) != 1) rc = OPERATION;
            if (rc == OK && EVP_CipherFinal_ex(s->ctx, final, &written) != 1)
                rc = !s->encrypt && s->authenticated ? AUTH : OPERATION;
            if (rc == OK && (written < 0 || written > (int)sizeof(final))) rc = OPERATION;
            if (rc == OK && written && spool_write(&s->output, final, (size_t)written) != OK)
                rc = OPERATION;
            if (rc == OK && s->encrypt && s->authenticated &&
                EVP_CIPHER_CTX_ctrl(s->ctx, EVP_CTRL_AEAD_GET_TAG,
                                    (int)s->tag_len, s->tag) != 1) rc = OPERATION;
        }
        OPENSSL_cleanse(final, sizeof(final));
        if (rc == OK) s->state = 2;
        else fail_session(s, rc);
    }
    unlock_session(s);
    return rc;
}

int __k_symmetric_tag(symmetric_session *s, unsigned char *out) {
    int rc = lock_session(s);
    if (rc != OK) return rc;
    if (!s->encrypt || s->state != 2) rc = STATE;
    else if (s->tag_len && !out) rc = INVALID;
    else if (s->tag_len) memcpy(out, s->tag, s->tag_len);
    unlock_session(s);
    return rc;
}

int __k_symmetric_read(symmetric_session *s, unsigned char *out, unsigned int len,
                        unsigned int *read_count) {
    int rc = lock_session(s);
    if (rc != OK) return rc;
    *read_count = 0;
    if (s->state != 2 &&
        (s->encrypt || s->authenticated || s->state > 1)) rc = STATE;
    else if (len && !out) rc = INVALID;
    else {
        size_t n = 0;
        rc = spool_read(&s->output, out, len, &n);
        if (rc == OK) *read_count = (unsigned int)n;
        else fail_session(s, rc);
    }
    unlock_session(s);
    return rc;
}

typedef struct {
    char *algorithm, *mode, *padding;
    unsigned int bits, iv_len, tag_len;
} capability_row;

typedef struct {
    capability_row *rows;
    unsigned int count, capacity;
} symmetric_capabilities;

typedef struct {
    unsigned char present[ADAPTER_COUNT][3];
} candidate_set;

static void collect_cipher_candidate(EVP_CIPHER *cipher, void *argument) {
    candidate_set *set = argument;
    for (size_t i = 0; i < ADAPTER_COUNT; ++i) {
        const cipher_adapter *a = &adapters[i];
        for (unsigned int j = 0; j < 3; ++j) {
            unsigned int bits = 128 + j * 64;
            symmetric_cipher candidate = {0};
            if (cipher_parameters(&candidate, a->algorithm, bits, a->mode,
                                  a->default_iv, a->default_tag, a->default_padding) &&
                EVP_CIPHER_is_a(cipher, candidate.name))
                set->present[i][j] = 1;
        }
    }
}

static int capability_witness(const symmetric_cipher *c) {
    unsigned char raw[MAX_KEY] = {0}, iv[16] = {0}, plain[32], encrypted[128], recovered[128];
    unsigned char tag[16] = {0}, aad[7] = {1,2,3,4,5,6,7};
    unsigned int len = c->mode == CBC && c->padding == NO_PADDING ? 32 :
                       c->mode == XTS ? 31 : 19;
    unsigned int n = 0, total = 0, read = 0, recovered_len = 0;
    int status = OPERATION, rc = OPERATION;
    symmetric_key *key = NULL;
    symmetric_session *enc = NULL, *dec = NULL;
    int marked = ERR_set_mark();
    for (unsigned int i = 0; i < len; ++i) plain[i] = (unsigned char)i;
    raw[c->key_len - 1] = 1;
    key = __k_symmetric_key_new(c, raw, 0, &status);
    if (!key || status != OK) goto done;
    enc = __k_symmetric_session_new(c, key, iv, 1, 0, 1, len, &status);
    if (!enc || status != OK) goto done;
    if (c->authenticated && __k_symmetric_aad(enc, aad, sizeof(aad)) != OK) goto done;
    if (__k_symmetric_update(enc, plain, len, encrypted, sizeof(encrypted), &n) != OK)
        goto done;
    total = n;
    if (__k_symmetric_finish(enc, NULL, 0) != OK ||
        (c->authenticated && __k_symmetric_tag(enc, tag) != OK)) goto done;
    if (__k_symmetric_read(enc, encrypted + total, sizeof(encrypted) - total, &read) != OK)
        goto done;
    total += read;
    dec = __k_symmetric_session_new(c, key, iv, 0, 0, 1, total, &status);
    if (!dec || status != OK) goto done;
    if (c->authenticated && __k_symmetric_aad(dec, aad, sizeof(aad)) != OK) goto done;
    if (__k_symmetric_update(dec, encrypted, total, NULL, 0, &n) != OK ||
        __k_symmetric_finish(dec, c->authenticated ? tag : NULL, c->tag_len) != OK)
        goto done;
    if (__k_symmetric_read(dec, recovered, sizeof(recovered), &recovered_len) != OK ||
        recovered_len != len || CRYPTO_memcmp(plain, recovered, len) != 0) goto done;
    rc = OK;
done:
    __k_symmetric_session_free(enc);
    __k_symmetric_session_free(dec);
    __k_symmetric_key_free(key);
    OPENSSL_cleanse(raw, sizeof(raw));
    OPENSSL_cleanse(plain, sizeof(plain));
    OPENSSL_cleanse(encrypted, sizeof(encrypted));
    OPENSSL_cleanse(recovered, sizeof(recovered));
    OPENSSL_cleanse(tag, sizeof(tag));
    if (rc == OK && discovery_probe_status(0, 0) != OK) rc = OPERATION;
    if (marked) ERR_pop_to_mark(); else ERR_clear_error();
    return rc;
}

static int add_capability(symmetric_capabilities *snapshot, const symmetric_cipher *c) {
    if (snapshot->count == snapshot->capacity) {
        unsigned int next = snapshot->capacity ? snapshot->capacity * 2 : 32;
        if (next < snapshot->capacity || (size_t)next > SIZE_MAX / sizeof(*snapshot->rows))
            return OPERATION;
        capability_row *rows = OPENSSL_realloc(snapshot->rows, (size_t)next * sizeof(*rows));
        if (!rows) return OPERATION;
        snapshot->rows = rows;
        snapshot->capacity = next;
    }
    capability_row row = {0};
    row.algorithm = OPENSSL_strdup(cipher_field(c, 0));
    row.mode = OPENSSL_strdup(cipher_field(c, 1));
    row.padding = OPENSSL_strdup(cipher_field(c, 2));
    if (!row.algorithm || !row.mode || !row.padding) {
        OPENSSL_free(row.algorithm);
        OPENSSL_free(row.mode);
        OPENSSL_free(row.padding);
        return OPERATION;
    }
    row.bits = c->bits;
    row.iv_len = c->iv_len;
    row.tag_len = c->tag_len;
    snapshot->rows[snapshot->count++] = row;
    return OK;
}

static int compare_capability_rows(const void *left, const void *right) {
    const capability_row *a = left, *b = right;
    int n = strcmp(a->algorithm, b->algorithm);
    if (n) return n;
    n = strcmp(a->mode, b->mode);
    if (n) return n;
    if (a->bits != b->bits) return a->bits < b->bits ? -1 : 1;
    n = strcmp(a->padding, b->padding);
    if (n) return n;
    if (a->iv_len != b->iv_len) return a->iv_len < b->iv_len ? -1 : 1;
    if (a->tag_len != b->tag_len) return a->tag_len < b->tag_len ? -1 : 1;
    return 0;
}

void __k_cap_symmetric_free(void *handle) {
    symmetric_capabilities *snapshot = handle;
    if (!snapshot) return;
    for (unsigned int i = 0; i < snapshot->count; ++i) {
        OPENSSL_free(snapshot->rows[i].algorithm);
        OPENSSL_free(snapshot->rows[i].mode);
        OPENSSL_free(snapshot->rows[i].padding);
    }
    OPENSSL_free(snapshot->rows);
    OPENSSL_free(snapshot);
}

static void *build_symmetric_capabilities(int *status) {
    if (!status) return NULL;
    *status = OPERATION;
    symmetric_capabilities *snapshot = OPENSSL_zalloc(sizeof(*snapshot));
    if (!snapshot) return NULL;
    candidate_set set = {0};
    unsigned long before;
    int marked = begin_error_scope(&before);
    EVP_CIPHER_do_all_provided(NULL, collect_cipher_candidate, &set);
    unsigned long after = ERR_peek_last_error();
    end_error_scope(marked);
    if (!before || after != before) goto fail;
    for (size_t i = 0; i < ADAPTER_COUNT; ++i) {
        const cipher_adapter *a = &adapters[i];
        for (unsigned int j = 0; j < 3; ++j) {
            if (!set.present[i][j]) continue;
            unsigned int bits = 128 + j * 64;
            for (int p = 0; p < (a->mode == CBC ? 2 : 1); ++p) {
                int pad = p == 0 ? a->default_padding : NO_PADDING;
                int result;
                symmetric_cipher *c = cipher_new(a->algorithm, bits, a->mode,
                                                  a->default_iv, a->default_tag, pad, &result, 1);
                if (!c) {
                    if (result == INVALID) continue;
                    goto fail;
                }
                result = capability_witness(c);
                if (result == OK) result = add_capability(snapshot, c);
                __k_symmetric_cipher_free(c);
                if (result != OK) goto fail;
            }
        }
    }
    if (snapshot->count > 1)
        qsort(snapshot->rows, snapshot->count, sizeof(*snapshot->rows), compare_capability_rows);
    *status = OK;
    return snapshot;
fail:
    __k_cap_symmetric_free(snapshot);
    return NULL;
}

typedef struct {
    OSSL_LIB_CTX *context;
    void *result;
    int status, done;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
} capability_work;

static void *capability_worker(void *arg) {
    capability_work *work = arg;
    OSSL_LIB_CTX *previous = OSSL_LIB_CTX_set0_default(work->context);
    if (previous && OSSL_LIB_CTX_set0_default(NULL) == work->context) {
        work->result = build_symmetric_capabilities(&work->status);
        if (!OSSL_LIB_CTX_set0_default(previous) ||
            OSSL_LIB_CTX_set0_default(NULL) != previous) {
            __k_cap_symmetric_free(work->result);
            work->result = NULL;
            work->status = OPERATION;
        }
        OPENSSL_thread_stop_ex(work->context);
    } else if (previous) {
        OSSL_LIB_CTX_set0_default(previous);
        OPENSSL_thread_stop_ex(work->context);
    }
    pthread_mutex_lock(&work->mutex);
    work->done = 1;
    pthread_cond_signal(&work->condition);
    pthread_mutex_unlock(&work->mutex);
    return NULL;
}

void *__k_cap_symmetric_new(int *status) {
    if (!status) return NULL;
    *status = OPERATION;
    OSSL_LIB_CTX *context = OSSL_LIB_CTX_set0_default(NULL);
    if (!context) return NULL;
    capability_work work = {
        .context = context, .result = NULL, .status = OPERATION, .done = 0,
        .mutex = PTHREAD_MUTEX_INITIALIZER, .condition = PTHREAD_COND_INITIALIZER
    };
    pthread_t thread;
    if (pthread_create(&thread, NULL, capability_worker, &work) != 0) {
        pthread_cond_destroy(&work.condition);
        pthread_mutex_destroy(&work.mutex);
        return NULL;
    }
    if (pthread_join(thread, NULL) != 0) {
        pthread_mutex_lock(&work.mutex);
        while (!work.done) pthread_cond_wait(&work.condition, &work.mutex);
        pthread_mutex_unlock(&work.mutex);
        pthread_detach(thread);
        __k_cap_symmetric_free(work.result);
        pthread_cond_destroy(&work.condition);
        pthread_mutex_destroy(&work.mutex);
        return NULL;
    }
    pthread_cond_destroy(&work.condition);
    pthread_mutex_destroy(&work.mutex);
    *status = work.status;
    return work.result;
}

unsigned int __k_cap_symmetric_count(const void *handle) {
    const symmetric_capabilities *snapshot = handle;
    return snapshot ? snapshot->count : 0;
}

static const char *capability_field(const void *handle, unsigned int index, int field) {
    const symmetric_capabilities *snapshot = handle;
    if (!snapshot || index >= snapshot->count) return NULL;
    const capability_row *row = &snapshot->rows[index];
    if (field == 0) return row->algorithm;
    if (field == 1) return row->mode;
    if (field == 2) return row->padding;
    return NULL;
}

unsigned int __k_cap_symmetric_size(const void *handle, unsigned int index, int field) {
    const char *value = capability_field(handle, index, field);
    return value ? (unsigned int)strlen(value) : 0;
}

void __k_cap_symmetric_copy(const void *handle, unsigned int index, int field,
                            unsigned char *out) {
    const char *value = capability_field(handle, index, field);
    if (value && out) memcpy(out, value, strlen(value) + 1);
}

unsigned int __k_cap_symmetric_value(const void *handle, unsigned int index, int field) {
    const symmetric_capabilities *snapshot = handle;
    if (!snapshot || index >= snapshot->count) return 0;
    const capability_row *row = &snapshot->rows[index];
    if (field == 0) return row->bits;
    if (field == 1) return row->iv_len;
    if (field == 2) return row->tag_len;
    return 0;
}
