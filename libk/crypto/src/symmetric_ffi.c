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
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { OK = 0, INVALID = -1, AUTH = -2, OPERATION = -3, STATE = -4 };
enum { CHUNK = 65536, NONCE_SIZE = 12, TAG_SIZE = 16 };
#define MAX_DATA ((UINT64_C(1) << 36) - 32)
#define MAX_AAD ((UINT64_C(1) << 61) - 1)

typedef struct {
    unsigned int length;
    unsigned char bytes[32];
} symmetric_key;

typedef struct chunk {
    struct chunk *next;
    size_t length;
    size_t read;
    unsigned char bytes[CHUNK];
} chunk;

typedef struct {
    EVP_CIPHER_CTX *ctx;
    atomic_int busy;
    int encrypt;
    int state; /* 0: AAD, 1: data, 2: verified/final, 3: failed, 4: closed */
    int file_staging;
    uint64_t aad_length;
    uint64_t data_length;
    unsigned char nonce[NONCE_SIZE];
    FILE *file;
    chunk *head;
    chunk *tail;
} symmetric_session;

static int valid_key_length(unsigned int n) {
    return n == 16 || n == 24 || n == 32;
}

static const EVP_CIPHER *cipher_for(unsigned int n) {
    switch (n) {
        case 16: return EVP_aes_128_gcm();
        case 24: return EVP_aes_192_gcm();
        case 32: return EVP_aes_256_gcm();
        default: return NULL;
    }
}

int __k_symmetric_available(unsigned int size) {
    const char *name;
    switch (size) {
        case 16: name = "AES-128-GCM"; break;
        case 24: name = "AES-192-GCM"; break;
        case 32: name = "AES-256-GCM"; break;
        default: return 0;
    }

    EVP_CIPHER *cipher = EVP_CIPHER_fetch(NULL, name, NULL);
    if (!cipher) return 0;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    unsigned char key[32] = {0};
    unsigned char nonce[NONCE_SIZE] = {0};
    int available = ctx && EVP_CIPHER_get_mode(cipher) == EVP_CIPH_GCM_MODE &&
                    EVP_CIPHER_get_key_length(cipher) == (int)size;
    for (int encrypt = 0; available && encrypt <= 1; ++encrypt) {
        available = EVP_CIPHER_CTX_reset(ctx) == 1 &&
                    EVP_CipherInit_ex(ctx, cipher, NULL, NULL, NULL, encrypt) == 1 &&
                    EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, NONCE_SIZE, NULL) == 1 &&
                    EVP_CipherInit_ex(ctx, NULL, NULL, key, nonce, encrypt) == 1;
    }
    EVP_CIPHER_CTX_free(ctx);
    EVP_CIPHER_free(cipher);
    return available;
}

int __k_symmetric_random(unsigned char *out, unsigned int length) {
    if (!out || length > INT32_MAX) return INVALID;
    return RAND_bytes(out, (int)length) == 1 ? OK : OPERATION;
}

symmetric_key *__k_symmetric_key_new(unsigned int size, const unsigned char *data,
                                     int generate, int *status) {
    *status = INVALID;
    if (!valid_key_length(size) || (!generate && !data)) return NULL;
    symmetric_key *key = OPENSSL_zalloc(sizeof(*key));
    if (!key) {
        *status = OPERATION;
        return NULL;
    }
    key->length = size;
    if (generate) {
        if (RAND_priv_bytes(key->bytes, (int)size) != 1) {
            OPENSSL_clear_free(key, sizeof(*key));
            *status = OPERATION;
            return NULL;
        }
    } else {
        memcpy(key->bytes, data, size);
    }
    *status = OK;
    return key;
}

void __k_symmetric_key_free(symmetric_key *key) {
    if (key) OPENSSL_clear_free(key, sizeof(*key));
}

static int clear_staging(symmetric_session *s) {
    int rc = OK;
    chunk *p = s->head;
    while (p) {
        chunk *next = p->next;
        OPENSSL_clear_free(p, sizeof(*p));
        p = next;
    }
    s->head = s->tail = NULL;
    if (s->file) {
        if (fclose(s->file) != 0) rc = OPERATION;
        s->file = NULL;
    }
    return rc;
}

void __k_symmetric_session_free(symmetric_session *s) {
    if (!s) return;
    clear_staging(s);
    EVP_CIPHER_CTX_free(s->ctx);
    OPENSSL_clear_free(s, sizeof(*s));
}

symmetric_session *__k_symmetric_session_new(unsigned int size, const symmetric_key *key,
                                             const unsigned char *nonce, int encrypt,
                                             int file_staging, int *status) {
    *status = INVALID;
    if (!key || key->length != size || !cipher_for(size) || !nonce ||
        (encrypt != 0 && encrypt != 1) || (file_staging != 0 && file_staging != 1))
        return NULL;
    *status = OPERATION;
    symmetric_session *s = OPENSSL_zalloc(sizeof(*s));
    if (!s) return NULL;
    atomic_init(&s->busy, 0);
    s->encrypt = encrypt;
    s->file_staging = file_staging;
    memcpy(s->nonce, nonce, NONCE_SIZE);
    s->ctx = EVP_CIPHER_CTX_new();
    if (!s->ctx ||
        EVP_CipherInit_ex(s->ctx, cipher_for(size), NULL, NULL, NULL, encrypt) != 1 ||
        EVP_CIPHER_CTX_ctrl(s->ctx, EVP_CTRL_GCM_SET_IVLEN, NONCE_SIZE, NULL) != 1 ||
        EVP_CipherInit_ex(s->ctx, NULL, NULL, key->bytes, nonce, encrypt) != 1)
        goto fail;
    if (!encrypt && file_staging) {
        /* POSIX tmpfile creates an unlinked file; do not silently fall back to memory. */
        s->file = tmpfile();
        if (!s->file) goto fail;
        int fd = fileno(s->file);
        int flags = fcntl(fd, F_GETFD);
        if (fd < 0 || flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0 ||
            fchmod(fd, S_IRUSR | S_IWUSR) != 0)
            goto fail;
    }
    *status = OK;
    return s;
fail:
    __k_symmetric_session_free(s);
    return NULL;
}

static int lock_session(symmetric_session *s) {
    if (!s || atomic_exchange_explicit(&s->busy, 1, memory_order_acquire))
        return STATE;
    return OK;
}

static void unlock_session(symmetric_session *s) {
    atomic_store_explicit(&s->busy, 0, memory_order_release);
}

int __k_symmetric_close(symmetric_session *s) {
    int rc = lock_session(s);
    if (rc != OK) return rc;
    if (s->state != 4) {
        rc = clear_staging(s);
        EVP_CIPHER_CTX_free(s->ctx);
        s->ctx = NULL;
        s->state = 4;
    }
    unlock_session(s);
    return rc;
}

int __k_symmetric_nonce(symmetric_session *s, unsigned char *out) {
    int rc = lock_session(s);
    if (rc != OK) return rc;
    memcpy(out, s->nonce, NONCE_SIZE);
    unlock_session(s);
    return OK;
}

int __k_symmetric_aad(symmetric_session *s, const unsigned char *data, unsigned int len) {
    int rc = lock_session(s);
    if (rc != OK) return rc;
    if (s->state != 0) rc = STATE;
    else if ((uint64_t)len > MAX_AAD - s->aad_length || (len && !data)) rc = INVALID;
    else {
        unsigned int offset = 0;
        while (offset < len) {
            unsigned int n = len - offset;
            if (n > CHUNK) n = CHUNK;
            int written = 0;
            if (EVP_CipherUpdate(s->ctx, NULL, &written, data + offset, (int)n) != 1) {
                s->state = 3;
                rc = OPERATION;
                break;
            }
            offset += n;
        }
        if (rc == OK) s->aad_length += len;
    }
    unlock_session(s);
    return rc;
}

static int stage(symmetric_session *s, const unsigned char *data, size_t len) {
    if (s->file_staging)
        return fwrite(data, 1, len, s->file) == len ? OK : OPERATION;
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
    return OK;
}

int __k_symmetric_update(symmetric_session *s, const unsigned char *data,
                         unsigned int len, unsigned char *out) {
    int rc = lock_session(s);
    if (rc != OK) return rc;
    if (s->state > 1) rc = STATE;
    else if ((uint64_t)len > MAX_DATA - s->data_length ||
             (len && (!data || (s->encrypt && !out)))) rc = INVALID;
    else {
        s->state = 1;
        unsigned int offset = 0;
        unsigned char buffer[CHUNK + 16];
        while (offset < len) {
            unsigned int n = len - offset;
            if (n > CHUNK) n = CHUNK;
            int written = 0;
            if (EVP_CipherUpdate(s->ctx, buffer, &written, data + offset, (int)n) != 1 ||
                written != (int)n) {
                rc = OPERATION;
                break;
            }
            if (s->encrypt) memcpy(out + offset, buffer, n);
            else if (stage(s, buffer, n) != OK) {
                rc = OPERATION;
                break;
            }
            offset += n;
        }
        OPENSSL_cleanse(buffer, sizeof(buffer));
        if (rc == OK) s->data_length += len;
        else {
            s->state = 3;
            clear_staging(s);
        }
    }
    unlock_session(s);
    return rc;
}

int __k_symmetric_finish(symmetric_session *s, unsigned char *tag) {
    int rc = lock_session(s);
    if (rc != OK) return rc;
    if (s->state > 1) rc = STATE;
    else if (!tag) rc = INVALID;
    else {
        unsigned char last[16];
        int written = 0;
        if (!s->encrypt &&
            EVP_CIPHER_CTX_ctrl(s->ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE, tag) != 1)
            rc = OPERATION;
        else if (EVP_CipherFinal_ex(s->ctx, last, &written) != 1)
            rc = s->encrypt ? OPERATION : AUTH;
        else if (written != 0 ||
                 (s->encrypt && EVP_CIPHER_CTX_ctrl(s->ctx, EVP_CTRL_GCM_GET_TAG,
                                                    TAG_SIZE, tag) != 1))
            rc = OPERATION;
        else if (!s->encrypt && s->file &&
                 (fflush(s->file) != 0 || fseek(s->file, 0, SEEK_SET) != 0))
            rc = OPERATION;
        OPENSSL_cleanse(last, sizeof(last));
        s->state = rc == OK ? 2 : 3;
        if (rc != OK) clear_staging(s);
    }
    unlock_session(s);
    return rc;
}

int __k_symmetric_read(symmetric_session *s, unsigned char *out, unsigned int len,
                        unsigned int *read_count) {
    int rc = lock_session(s);
    if (rc != OK) return rc;
    *read_count = 0;
    if (s->encrypt || s->state != 2) rc = STATE;
    else if (len && !out) rc = INVALID;
    else if (len && s->file) {
        size_t n = fread(out, 1, len, s->file);
        if (ferror(s->file)) {
            s->state = 3;
            clear_staging(s);
            rc = OPERATION;
        } else {
            *read_count = (unsigned int)n;
            if (n == 0 && feof(s->file) && clear_staging(s) != OK) {
                s->state = 3;
                rc = OPERATION;
            }
        }
    } else {
        while (*read_count < len && s->head) {
            chunk *p = s->head;
            size_t n = p->length - p->read;
            if (n > len - *read_count) n = len - *read_count;
            memcpy(out + *read_count, p->bytes + p->read, n);
            OPENSSL_cleanse(p->bytes + p->read, n);
            p->read += n;
            *read_count += (unsigned int)n;
            if (p->read == p->length) {
                s->head = p->next;
                if (!s->head) s->tail = NULL;
                OPENSSL_clear_free(p, sizeof(*p));
            }
        }
    }
    unlock_session(s);
    return rc;
}
