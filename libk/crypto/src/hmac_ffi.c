/*
 * K Language standard library -- private HMAC OpenSSL bridge
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */
#include <limits.h>
#include <stddef.h>
#include <pthread.h>
#include <string.h>

#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/evperr.h>
#include <openssl/params.h>
#include <openssl/proverr.h>

enum {
    HMAC_OK = 0, HMAC_UNAVAILABLE = 1, HMAC_INVALID = -1,
    HMAC_OPERATION = -3, HMAC_STATE = -4
};

typedef struct {
    unsigned char *bytes;
    unsigned int length;
} hmac_key;

typedef struct {
    EVP_MAC_CTX *ctx;
    unsigned int size;
} hmac_session;

static int provider_failure(int initializing) {
    unsigned long error;
    int unsupported = 0, invalid = 0, other = 0;
    while ((error = ERR_get_error()) != 0) {
        int reason = ERR_GET_REASON(error);
        if (!ERR_SYSTEM_ERROR(error) && !ERR_FATAL_ERROR(error) &&
            ERR_GET_LIB(error) == ERR_LIB_EVP &&
            (reason == ERR_R_UNSUPPORTED || reason == EVP_R_UNSUPPORTED_ALGORITHM ||
             (initializing && reason == EVP_R_INVALID_DIGEST)))
            unsupported = 1;
        else if (initializing && !ERR_SYSTEM_ERROR(error) && !ERR_FATAL_ERROR(error) &&
                 ERR_GET_LIB(error) == ERR_LIB_PROV &&
                 (reason == PROV_R_INVALID_DIGEST || reason == PROV_R_DIGEST_NOT_ALLOWED ||
                  reason == PROV_R_XOF_DIGESTS_NOT_ALLOWED || reason == PROV_R_NOT_SUPPORTED))
            unsupported = 1;
        else if (initializing && !ERR_SYSTEM_ERROR(error) && !ERR_FATAL_ERROR(error) &&
                 ((ERR_GET_LIB(error) == ERR_LIB_PROV &&
                   (reason == PROV_R_INVALID_KEY || reason == PROV_R_INVALID_KEY_LENGTH ||
                    reason == PROV_R_KEY_SIZE_TOO_SMALL || reason == PROV_R_UNSUPPORTED_KEY_SIZE)) ||
                  (ERR_GET_LIB(error) == ERR_LIB_EVP && reason == EVP_R_INVALID_KEY_LENGTH)))
            invalid = 1;
        else
            other = 1;
    }
    if (other || (unsupported && invalid)) return HMAC_OPERATION;
    if (unsupported) return HMAC_UNAVAILABLE;
    if (invalid) return HMAC_INVALID;
    return HMAC_OPERATION;
}

hmac_key *__k_hmac_key_new(const unsigned char *data, unsigned int length) {
    hmac_key *key = OPENSSL_zalloc(sizeof(*key));
    if (!key) return NULL;
    key->bytes = OPENSSL_malloc(length ? length : 1);
    if (!key->bytes) {
        OPENSSL_free(key);
        return NULL;
    }
    key->length = length;
    if (length) memcpy(key->bytes, data, length);
    else key->bytes[0] = 0;
    return key;
}

void __k_hmac_key_free(hmac_key *key) {
    if (!key) return;
    OPENSSL_clear_free(key->bytes, key->length ? key->length : 1);
    OPENSSL_clear_free(key, sizeof(*key));
}

const unsigned char *__k_hmac_key_borrow(const hmac_key *key) {
    return key ? key->bytes : NULL;
}

/* This helper runs only with an initially empty, isolated error queue. */
static hmac_session *hmac_new_isolated(const unsigned char *name, const hmac_key *key,
                                       int *status) {
    EVP_MD *digest = NULL;
    EVP_MAC *mac = NULL;
    hmac_session *session = NULL;
    size_t size;
    OSSL_PARAM params[2];

    *status = HMAC_OPERATION;
    if (!name || !key) return NULL;
    digest = EVP_MD_fetch(NULL, (const char *)name, NULL);
    if (!digest) {
        *status = provider_failure(0);
        return NULL;
    }
    ERR_clear_error();
    if ((EVP_MD_get_flags(digest) & EVP_MD_FLAG_XOF) != 0) {
        *status = HMAC_UNAVAILABLE;
        goto fail;
    }
    mac = EVP_MAC_fetch(NULL, "HMAC", NULL);
    if (!mac) {
        *status = provider_failure(0);
        goto fail;
    }
    ERR_clear_error();
    session = OPENSSL_zalloc(sizeof(*session));
    if (!session) goto fail;
    session->ctx = EVP_MAC_CTX_new(mac);
    if (!session->ctx) goto fail;
    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, (char *)name, 0);
    params[1] = OSSL_PARAM_construct_end();
    ERR_clear_error();
    if (EVP_MAC_init(session->ctx, key->bytes, key->length, params) != 1) {
        *status = provider_failure(1);
        goto fail;
    }
    size = EVP_MAC_CTX_get_mac_size(session->ctx);
    if (size == 0 || size > UINT_MAX) goto fail;
    session->size = (unsigned int)size;
    EVP_MD_free(digest);
    EVP_MAC_free(mac);
    ERR_clear_error();
    *status = HMAC_OK;
    return session;

fail:
    if (session) {
        EVP_MAC_CTX_free(session->ctx);
        OPENSSL_free(session);
    }
    EVP_MD_free(digest);
    EVP_MAC_free(mac);
    ERR_clear_error();
    return NULL;
}

void __k_hmac_free(hmac_session *session) {
    if (!session) return;
    EVP_MAC_CTX_free(session->ctx);
    OPENSSL_free(session);
}

typedef struct {
    OSSL_LIB_CTX *context;
    const unsigned char *name;
    const hmac_key *key;
    hmac_session *result;
    int status, done;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
} hmac_work;

static void *hmac_worker(void *arg) {
    hmac_work *work = arg;
    OSSL_LIB_CTX *previous = OSSL_LIB_CTX_set0_default(work->context);
    if (previous && OSSL_LIB_CTX_set0_default(NULL) == work->context &&
        ERR_peek_error() == 0) {
        work->result = hmac_new_isolated(work->name, work->key, &work->status);
    }
    if (previous && OSSL_LIB_CTX_set0_default(previous) != work->context) {
        __k_hmac_free(work->result);
        work->result = NULL;
        work->status = HMAC_OPERATION;
    }
    OPENSSL_thread_stop_ex(work->context);
    OPENSSL_thread_stop();
    pthread_mutex_lock(&work->mutex);
    work->done = 1;
    pthread_cond_signal(&work->condition);
    pthread_mutex_unlock(&work->mutex);
    return NULL;
}

hmac_session *__k_hmac_new(const unsigned char *name, const hmac_key *key, int *status) {
    pthread_t thread;
    hmac_work work;
    OSSL_LIB_CTX *context;

    if (!status) return NULL;
    *status = HMAC_OPERATION;
    if (!name || !key) return NULL;
    if (ERR_peek_error() == 0) return hmac_new_isolated(name, key, status);

    /* OpenSSL exposes no way to inspect new errors without consuming old marks. */
    context = OSSL_LIB_CTX_set0_default(NULL);
    if (!context) return NULL;
    memset(&work, 0, sizeof(work));
    work.context = context;
    work.name = name;
    work.key = key;
    work.status = HMAC_OPERATION;
    if (pthread_mutex_init(&work.mutex, NULL) != 0) return NULL;
    if (pthread_cond_init(&work.condition, NULL) != 0) {
        pthread_mutex_destroy(&work.mutex);
        return NULL;
    }
    if (pthread_create(&thread, NULL, hmac_worker, &work) != 0) {
        pthread_cond_destroy(&work.condition);
        pthread_mutex_destroy(&work.mutex);
        return NULL;
    }
    if (pthread_join(thread, NULL) != 0) {
        pthread_mutex_lock(&work.mutex);
        while (!work.done) pthread_cond_wait(&work.condition, &work.mutex);
        pthread_mutex_unlock(&work.mutex);
        pthread_detach(thread);
        __k_hmac_free(work.result);
        work.result = NULL;
        work.status = HMAC_OPERATION;
    }
    pthread_cond_destroy(&work.condition);
    pthread_mutex_destroy(&work.mutex);
    *status = work.status;
    return work.result;
}

unsigned int __k_hmac_size(const hmac_session *session) {
    return session->size;
}

int __k_hmac_update(hmac_session *session, const unsigned char *data, unsigned int length) {
    if (!session || !session->ctx) return HMAC_STATE;
    if (length && EVP_MAC_update(session->ctx, data, length) != 1) {
        EVP_MAC_CTX_free(session->ctx);
        session->ctx = NULL;
        return HMAC_OPERATION;
    }
    return HMAC_OK;
}

int __k_hmac_final(hmac_session *session, unsigned char *out) {
    size_t actual = 0;
    int ok;
    if (!session || !session->ctx) return HMAC_STATE;
    ok = EVP_MAC_final(session->ctx, out, &actual, session->size);
    EVP_MAC_CTX_free(session->ctx);
    session->ctx = NULL;
    return ok == 1 && actual == session->size ? HMAC_OK : HMAC_OPERATION;
}

int __k_hmac_equal(const unsigned char *a, const unsigned char *b, unsigned int length) {
    return length == 0 || CRYPTO_memcmp(a, b, length) == 0;
}
