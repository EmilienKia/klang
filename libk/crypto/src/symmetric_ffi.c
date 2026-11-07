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
#include <openssl/rand.h>

#include <fcntl.h>
#include <limits.h>
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
enum { GCM = 1, CCM, OCB, CBC, CTR, XTS, POLY1305, ASCON, CFB, OFB, CFB1, CFB8 };
enum { NO_PADDING = 0, PKCS7 = 1 };
enum { CHUNK = 65536, BLOCK = 16, MAX_KEY = 64 };
#define GCM_MAX_DATA ((UINT64_C(1) << 36) - 32)
#define GCM_MAX_AAD ((UINT64_C(1) << 61) - 1)
#define CHACHA_MAX_DATA ((UINT64_C(0xffffffff)) * 64)
#define XTS_MAX_DATA ((UINT64_C(1) << 20) * 16)

typedef struct {
    EVP_CIPHER *cipher;
    EVP_CIPHER *block;
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

static const char *mode_name(int mode) {
    switch (mode) {
        case GCM: return "GCM";
        case CCM: return "CCM";
        case OCB: return "OCB";
        case CBC: return "CBC";
        case CTR: return "CTR";
        case XTS: return "XTS";
        case CFB: return "CFB";
        case OFB: return "OFB";
        case CFB1: return "CFB1";
        case CFB8: return "CFB8";
        default: return NULL;
    }
}

static int cipher_parameters(symmetric_cipher *c, int algorithm, unsigned int bits,
                             int mode, unsigned int iv_len, unsigned int tag_len,
                             int padding) {
    const char *family = NULL, *m = mode_name(mode);
    if (algorithm == AES) family = "AES";
    else if (algorithm == ARIA) family = "ARIA";
    else if (algorithm == CAMELLIA) family = "CAMELLIA";
    else if (algorithm == SM4) family = "SM4";
    else if (algorithm != CHACHA20 && algorithm != ASCON_AEAD128) return 0;
    if (algorithm == CHACHA20) {
        if (bits != 256 || mode != POLY1305) return 0;
        strcpy(c->name, "ChaCha20-Poly1305");
    } else if (algorithm == ASCON_AEAD128) {
        if (bits != 128 || mode != ASCON) return 0;
        strcpy(c->name, "ASCON-AEAD128");
    } else {
        if (!m || (bits != 128 && bits != 192 && bits != 256) ||
            (algorithm == SM4 && bits != 128) ||
            (mode == XTS && algorithm == AES && bits == 192))
            return 0;
        if (algorithm == SM4) snprintf(c->name, sizeof(c->name), "SM4-%s", m);
        else snprintf(c->name, sizeof(c->name), "%s-%u-%s", family, bits, m);
    }
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

static int cipher_probe(const symmetric_cipher *c, EVP_CIPHER_CTX *ctx,
                        const unsigned char *key, const unsigned char *iv) {
    unsigned char plain[32], encrypted[80], recovered[80], tag[16];
    int a = 0, b = 0, f = 0, d = 0, e = 0, g = 0, ok = 0;
    int len = c->mode == CBC && c->padding == NO_PADDING ? 32 : 19;
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
    if (c->authenticated) {
        tag[0] ^= 1;
        if (EVP_CIPHER_CTX_reset(ctx) != 1 || !init_evp(ctx, c, key, iv, 0) ||
            EVP_CipherUpdate(ctx, recovered, &d, encrypted, a + b + f) != 1 ||
            EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_AEAD_SET_TAG, (int)c->tag_len, tag) != 1 ||
            EVP_CipherFinal_ex(ctx, recovered + d, &g) == 1) goto done;
    }
    ok = 1;
done:
    OPENSSL_cleanse(plain, sizeof(plain));
    OPENSSL_cleanse(encrypted, sizeof(encrypted));
    OPENSSL_cleanse(recovered, sizeof(recovered));
    OPENSSL_cleanse(tag, sizeof(tag));
    return ok;
}

static int ascon_known_answer(const symmetric_cipher *c) {
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
    key[c->key_len - 1] = 1;
    for (unsigned int i = 0; i < sizeof(plain); ++i) plain[i] = (unsigned char)i;
    if (!single || !split || !init_evp(single, c, key, iv, 1) ||
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
    return xts_stream_probe_length(c, 64) && xts_stream_probe_length(c, 79);
}

symmetric_cipher *__k_symmetric_cipher_new(int algorithm, unsigned int bits, int mode,
                                             unsigned int iv_len, unsigned int tag_len,
                                             int padding) {
    int marked = ERR_set_mark();
    symmetric_cipher *c = OPENSSL_zalloc(sizeof(*c));
    if (!c) {
        if (marked) ERR_pop_to_mark(); else ERR_clear_error();
        return NULL;
    }
    if (!cipher_parameters(c, algorithm, bits, mode, iv_len, tag_len, padding))
        goto fail;
    c->cipher = EVP_CIPHER_fetch(NULL, c->name, NULL);
    if (!c->cipher || !cipher_matches(c))
        goto fail;
    if (c->mode == CCM) {
        /* CCM is composed privately from the fetched block primitive. It is not
           a provider-validated or FIPS-validated CCM implementation. */
        char block_name[64];
        if (algorithm == SM4) snprintf(block_name, sizeof(block_name), "SM4-ECB");
        else snprintf(block_name, sizeof(block_name), "%s-%u-ECB",
                      algorithm == AES ? "AES" : algorithm == ARIA ? "ARIA" : "CAMELLIA",
                      bits);
        c->block = EVP_CIPHER_fetch(NULL, block_name, NULL);
        if (!c->block || EVP_CIPHER_get_block_size(c->block) != BLOCK ||
            EVP_CIPHER_get_key_length(c->block) != (int)c->key_len)
            goto fail;
        unsigned char key[MAX_KEY] = {0}, iv[13] = {0}, tag[16] = {0};
        k_ccm_ctx *probe = NULL;
        int good = k_ccm_new(c->block, key, c->key_len, iv, c->iv_len, c->tag_len,
                             0, 0, 1, &probe) == OK && k_ccm_final(probe, tag) == OK;
        k_ccm_free(probe);
        probe = NULL;
        good = good && k_ccm_new(c->block, key, c->key_len, iv, c->iv_len, c->tag_len,
                                 0, 0, 0, &probe) == OK && k_ccm_final(probe, tag) == OK;
        k_ccm_free(probe);
        OPENSSL_cleanse(key, sizeof(key));
        OPENSSL_cleanse(tag, sizeof(tag));
        if (!good) goto fail;
    } else {
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        unsigned char key[MAX_KEY] = {0};
        unsigned char *iv = OPENSSL_zalloc(c->iv_len);
        key[c->key_len - 1] = 1;
        int good = ctx && iv &&
                   (c->mode == XTS
                    ? (init_evp(ctx, c, key, iv, 1) && EVP_CIPHER_CTX_reset(ctx) == 1 &&
                       init_evp(ctx, c, key, iv, 0))
                    : cipher_probe(c, ctx, key, iv));
        EVP_CIPHER_CTX_free(ctx);
        OPENSSL_clear_free(iv, c->iv_len);
        OPENSSL_cleanse(key, sizeof(key));
        if (!good) goto fail;
        if (c->mode == XTS) c->xts_streaming = xts_stream_probe(c);
        if (c->mode == ASCON && !ascon_known_answer(c)) goto fail;
    }
    if (marked) ERR_pop_to_mark(); else ERR_clear_error();
    return c;
fail:
    EVP_CIPHER_free(c->cipher);
    EVP_CIPHER_free(c->block);
    OPENSSL_free(c);
    if (marked) ERR_pop_to_mark(); else ERR_clear_error();
    return NULL;
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

symmetric_cipher *__k_symmetric_cipher_from_name(const unsigned char *name) {
    if (!name) return NULL;
    for (int algorithm = AES; algorithm <= ASCON_AEAD128; ++algorithm) {
        for (unsigned int bits = 128; bits <= 256; bits += 64) {
            for (int mode = GCM; mode <= CFB8; ++mode) {
                symmetric_cipher candidate = {0};
                unsigned int iv = mode == CCM || mode == GCM || mode == OCB ||
                                  mode == POLY1305 ? 12 : 16;
                unsigned int tag = aead_mode(mode) ? 16 : 0;
                int padding = mode == CBC ? PKCS7 : NO_PADDING;
                if (cipher_parameters(&candidate, algorithm, bits, mode, iv, tag, padding) &&
                    (strcmp(candidate.name, (const char *)name) == 0 ||
                     (algorithm == CHACHA20 &&
                      strcmp((const char *)name, "CHACHA20-POLY1305") == 0)))
                    return __k_symmetric_cipher_new(algorithm, bits, mode, iv, tag, padding);
            }
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

symmetric_session *__k_symmetric_session_new(const symmetric_cipher *c,
        const symmetric_key *key, const unsigned char *iv, int encrypt, int file_staging,
        int known_length, uint64_t expected_length, int *status) {
    *status = INVALID;
    if (!c || !key || !iv || key->algorithm != c->algorithm || key->bits != c->bits ||
        key->xts != (c->mode == XTS) || key->length != c->key_len ||
        (encrypt != 0 && encrypt != 1) || (file_staging != 0 && file_staging != 1) ||
        (known_length != 0 && known_length != 1) ||
        (known_length && expected_length > c->max_data)) return NULL;
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
             (s->mode == CTR && !counter_fits(s->iv, s->data_length + len)))
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
