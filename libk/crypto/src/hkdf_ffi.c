/*
 * K Language standard library -- private provider-backed HKDF bridge
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */
#include <limits.h>
#include <pthread.h>
#include <string.h>

#include <openssl/async.h>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/evperr.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/proverr.h>

enum { HKDF_OK = 0, HKDF_UNAVAILABLE = 1, HKDF_INVALID = -1, HKDF_OPERATION = -3 };

typedef struct {
    EVP_KDF *kdf;
    EVP_MD *digest;
    OSSL_LIB_CTX *context;
    char *name;
    unsigned int size;
} hkdf;

typedef struct {
    hkdf *algorithm;
    unsigned char *bytes;
    unsigned int length;
} hkdf_prk;

static void hkdf_free(hkdf *h) {
    if (!h) return;
    EVP_KDF_free(h->kdf);
    EVP_MD_free(h->digest);
    OPENSSL_free(h->name);
    OPENSSL_free(h);
}

void __k_hkdf_free(hkdf *h) { hkdf_free(h); }

void __k_hkdf_prk_free(hkdf_prk *p) {
    if (!p) return;
    OPENSSL_clear_free(p->bytes, p->length ? p->length : 1);
    hkdf_free(p->algorithm);
    OPENSSL_clear_free(p, sizeof(*p));
}

void __k_hkdf_clear(unsigned char *data, unsigned int length) {
    if (data && length) OPENSSL_cleanse(data, length);
}

unsigned int __k_hkdf_size(const hkdf *h) { return h->size; }
unsigned int __k_hkdf_prk_size(const hkdf_prk *p) { return p->length; }
unsigned int __k_hkdf_prk_hash_size(const hkdf_prk *p) { return p->algorithm->size; }

static int fetch_failure(void) {
    unsigned long e;
    int unsupported = 0, other = 0;
    while ((e = ERR_get_error()) != 0) {
        int lib = ERR_GET_LIB(e), reason = ERR_GET_REASON(e);
        if (!ERR_SYSTEM_ERROR(e) && reason == ERR_R_DISABLED)
            unsupported = 1;
        else if (!ERR_SYSTEM_ERROR(e) && !ERR_FATAL_ERROR(e) &&
            (reason == ERR_R_UNSUPPORTED ||
             (lib == ERR_LIB_EVP &&
              (reason == EVP_R_UNSUPPORTED_ALGORITHM ||
               reason == EVP_R_INVALID_DIGEST || reason == EVP_R_INVALID_KEY_LENGTH)) ||
             (lib == ERR_LIB_PROV &&
              (reason == PROV_R_INVALID_DIGEST || reason == PROV_R_INVALID_DIGEST_LENGTH ||
               reason == PROV_R_INVALID_DIGEST_SIZE || reason == PROV_R_DIGEST_NOT_ALLOWED ||
               reason == PROV_R_XOF_DIGESTS_NOT_ALLOWED || reason == PROV_R_NOT_SUPPORTED ||
               reason == PROV_R_INVALID_MODE || reason == PROV_R_INVALID_KEY ||
               reason == PROV_R_INVALID_KEY_LENGTH || reason == PROV_R_KEY_SIZE_TOO_SMALL ||
               reason == PROV_R_UNSUPPORTED_KEY_SIZE || reason == PROV_R_INVALID_SALT_LENGTH ||
               reason == PROV_R_INVALID_INPUT_LENGTH || reason == PROV_R_INVALID_OUTPUT_LENGTH))))
            unsupported = 1;
        else
            other = 1;
    }
    return unsupported && !other ? HKDF_UNAVAILABLE : HKDF_OPERATION;
}

/* All calls here use an initially empty error queue, isolated when needed. */
static hkdf *create_isolated(const char *name, int *status) {
    EVP_MD *md = NULL;
    EVP_KDF_CTX *ctx = NULL;
    hkdf *h = NULL;
    OSSL_PARAM params[2];
    int size;
    *status = HKDF_OPERATION;
    md = EVP_MD_fetch(NULL, name, NULL);
    if (!md) {
        *status = fetch_failure();
        goto done;
    }
    if (ERR_peek_error() != 0) goto done;
    if ((EVP_MD_get_flags(md) & EVP_MD_FLAG_XOF) != 0 ||
        (size = EVP_MD_get_size(md)) <= 0 || (unsigned int)size > UINT_MAX / 255u) {
        *status = HKDF_UNAVAILABLE;
        goto done;
    }
    h = OPENSSL_zalloc(sizeof(*h));
    if (!h) goto done;
    h->kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
    if (!h->kdf) {
        *status = fetch_failure();
        goto done;
    }
    if (ERR_peek_error() != 0) goto done;
    h->name = OPENSSL_strdup(name);
    if (!h->name) goto done;
    h->size = (unsigned int)size;
    h->context = OSSL_LIB_CTX_set0_default(NULL);
    if (!h->context) goto done;
    ctx = EVP_KDF_CTX_new(h->kdf);
    if (!ctx || ERR_peek_error() != 0) goto done;
    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, h->name, 0);
    params[1] = OSSL_PARAM_construct_end();
    if (EVP_KDF_CTX_set_params(ctx, params) != 1) {
        *status = fetch_failure();
        goto done;
    }
    if (ERR_peek_error() != 0) goto done;
    h->digest = md;
    md = NULL;
    *status = HKDF_OK;
done:
    EVP_KDF_CTX_free(ctx);
    EVP_MD_free(md);
    ERR_clear_error();
    if (*status != HKDF_OK) {
        hkdf_free(h);
        h = NULL;
    }
    return h;
}

static hkdf *copy_algorithm(const hkdf *source) {
    hkdf *copy = OPENSSL_zalloc(sizeof(*copy));
    if (!copy) return NULL;
    copy->name = OPENSSL_strdup(source->name);
    if (!copy->name || EVP_KDF_up_ref(source->kdf) != 1) {
        hkdf_free(copy);
        return NULL;
    }
    copy->kdf = source->kdf;
    if (EVP_MD_up_ref(source->digest) != 1) {
        hkdf_free(copy);
        return NULL;
    }
    copy->digest = source->digest;
    copy->context = source->context;
    copy->size = source->size;
    return copy;
}

static hkdf_prk *prk_new(const hkdf *algorithm, const unsigned char *bytes,
                         unsigned int length) {
    hkdf_prk *p = OPENSSL_zalloc(sizeof(*p));
    if (!p) return NULL;
    p->algorithm = copy_algorithm(algorithm);
    p->length = length;
    p->bytes = OPENSSL_malloc(length ? length : 1);
    if (!p->algorithm || !p->bytes) {
        __k_hkdf_prk_free(p);
        return NULL;
    }
    if (bytes && length) memcpy(p->bytes, bytes, length);
    return p;
}

void __k_hkdf_prk_export(const hkdf_prk *p, unsigned char *out) {
    if (p && p->length) memcpy(out, p->bytes, p->length);
}

typedef struct {
    OSSL_LIB_CTX *context;
    int kind, status, done;
    const char *name;
    const hkdf *algorithm;
    const unsigned char *key, *salt, *info;
    unsigned int key_length, salt_length, info_length, output_length;
    int absent_salt;
    unsigned char *output;
    hkdf *created;
    hkdf_prk *created_prk;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
} hkdf_work;

static void execute(hkdf_work *w) {
    EVP_KDF_CTX *ctx = NULL;
    EVP_KDF *kdf = NULL;
    EVP_MD *digest = NULL;
    unsigned char *default_salt = NULL;
    unsigned char empty = 0;
    OSSL_PARAM params[6], *p = params;
    int mode;
    w->status = HKDF_OPERATION;
    if (w->kind == -1) {
        w->created = create_isolated(w->name, &w->status);
        return;
    }
    if (w->kind == -2) {
        if (!w->algorithm ||
            (w->key_length && !w->key) || w->key_length < w->algorithm->size) {
            w->status = HKDF_INVALID;
            return;
        }
        w->created_prk = prk_new(w->algorithm, w->key, w->key_length);
        w->status = w->created_prk ? HKDF_OK : HKDF_OPERATION;
        ERR_clear_error();
        return;
    }
    if (w->kind == -3) {
        hkdf_work extraction;
        if (!w->algorithm) {
            w->status = HKDF_INVALID;
            return;
        }
        w->created_prk = prk_new(w->algorithm, NULL, w->algorithm->size);
        if (!w->created_prk) {
            ERR_clear_error();
            return;
        }
        memset(&extraction, 0, sizeof(extraction));
        extraction.kind = EVP_KDF_HKDF_MODE_EXTRACT_ONLY;
        extraction.algorithm = w->algorithm;
        extraction.key = w->key;
        extraction.key_length = w->key_length;
        extraction.salt = w->salt;
        extraction.salt_length = w->salt_length;
        extraction.absent_salt = w->absent_salt;
        extraction.output = w->created_prk->bytes;
        extraction.output_length = w->created_prk->length;
        execute(&extraction);
        w->status = extraction.status;
        if (w->status != HKDF_OK) {
            __k_hkdf_prk_free(w->created_prk);
            w->created_prk = NULL;
        }
        return;
    }
    if (!w->algorithm || (w->key_length && !w->key) ||
        (w->salt_length && !w->salt) || (w->info_length && !w->info) ||
        (w->output_length && !w->output)) {
        w->status = HKDF_INVALID;
        return;
    }
    if (w->kind != EVP_KDF_HKDF_MODE_EXTRACT_ONLY &&
        w->output_length > w->algorithm->size * 255u) {
        w->status = HKDF_INVALID;
        return;
    }
    /* Refetch under current properties; method object addresses need not be stable. */
    digest = EVP_MD_fetch(w->algorithm->context, w->algorithm->name, NULL);
    if (!digest || !EVP_MD_get0_name(digest) ||
        !EVP_MD_get0_name(w->algorithm->digest) ||
        EVP_MD_get0_provider(digest) != EVP_MD_get0_provider(w->algorithm->digest) ||
        !EVP_MD_is_a(digest, EVP_MD_get0_name(w->algorithm->digest)) ||
        !EVP_MD_is_a(w->algorithm->digest, EVP_MD_get0_name(digest)) ||
        EVP_MD_get_size(digest) != (int)w->algorithm->size ||
        (EVP_MD_get_flags(digest) & EVP_MD_FLAG_XOF) != 0 || ERR_peek_error() != 0)
        goto done;
    kdf = EVP_KDF_fetch(w->algorithm->context, "HKDF", NULL);
    if (!kdf || !EVP_KDF_get0_name(kdf) ||
        !EVP_KDF_get0_name(w->algorithm->kdf) ||
        EVP_KDF_get0_provider(kdf) != EVP_KDF_get0_provider(w->algorithm->kdf) ||
        !EVP_KDF_is_a(kdf, EVP_KDF_get0_name(w->algorithm->kdf)) ||
        !EVP_KDF_is_a(w->algorithm->kdf, EVP_KDF_get0_name(kdf)) ||
        ERR_peek_error() != 0)
        goto done;
    ctx = EVP_KDF_CTX_new(kdf);
    if (!ctx || ERR_peek_error() != 0) goto done;
    mode = w->kind;
    *p++ = OSSL_PARAM_construct_int(OSSL_KDF_PARAM_MODE, &mode);
    *p++ = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, w->algorithm->name, 0);
    *p++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
                                             (void *)(w->key ? w->key : &empty), w->key_length);
    if (mode != EVP_KDF_HKDF_MODE_EXPAND_ONLY) {
        if (w->absent_salt) {
            default_salt = OPENSSL_zalloc(w->algorithm->size);
            if (!default_salt) goto done;
            *p++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, default_salt,
                                                     w->algorithm->size);
        } else {
            *p++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
                (void *)(w->salt ? w->salt : &empty), w->salt_length);
        }
    }
    if (mode != EVP_KDF_HKDF_MODE_EXTRACT_ONLY)
        *p++ = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO,
            (void *)(w->info ? w->info : &empty), w->info_length);
    *p = OSSL_PARAM_construct_end();
    if (EVP_KDF_derive(ctx, w->output ? w->output : &empty, w->output_length, params) == 1 &&
        ERR_peek_error() == 0)
        w->status = HKDF_OK;
done:
    if (w->status != HKDF_OK && w->output && w->output_length)
        OPENSSL_cleanse(w->output, w->output_length);
    OPENSSL_clear_free(default_salt, w->algorithm->size);
    EVP_KDF_CTX_free(ctx);
    EVP_KDF_free(kdf);
    EVP_MD_free(digest);
    ERR_clear_error();
}

static void discard_work(void *arg) {
    hkdf_work *w = arg;
    hkdf_free(w->created);
    w->created = NULL;
    __k_hkdf_prk_free(w->created_prk);
    w->created_prk = NULL;
    if (w->output && w->output_length) OPENSSL_cleanse(w->output, w->output_length);
    w->status = HKDF_OPERATION;
}

static void execute_in_context(hkdf_work *w) {
    OSSL_LIB_CTX *previous = OSSL_LIB_CTX_set0_default(w->context);
    if (previous && OSSL_LIB_CTX_set0_default(NULL) == w->context &&
        ERR_peek_error() == 0)
        execute(w);
    if (previous && OSSL_LIB_CTX_set0_default(previous) != w->context) {
        discard_work(w);
    }
    ERR_clear_error();
}

static void *worker(void *arg) {
    hkdf_work *w = arg;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    execute_in_context(w);
    OPENSSL_thread_stop_ex(w->context);
    OPENSSL_thread_stop();
    pthread_mutex_lock(&w->mutex);
    w->done = 1;
    pthread_cond_signal(&w->condition);
    pthread_mutex_unlock(&w->mutex);
    return NULL;
}

static void run(hkdf_work *w) {
    pthread_t thread;
    int old_cancel_state;
    w->status = HKDF_OPERATION;
    if (ASYNC_get_current_job() != NULL) return;
    if (pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_cancel_state) != 0) return;
    pthread_cleanup_push(discard_work, w);
    w->context = w->algorithm ? w->algorithm->context : OSSL_LIB_CTX_set0_default(NULL);
    if (!w->context) goto done;
    if (!ERR_peek_error()) {
        execute_in_context(w);
        goto done;
    }
    if (pthread_mutex_init(&w->mutex, NULL) != 0) goto done;
    if (pthread_cond_init(&w->condition, NULL) != 0) {
        pthread_mutex_destroy(&w->mutex);
        goto done;
    }
    if (pthread_create(&thread, NULL, worker, w) != 0) {
        pthread_cond_destroy(&w->condition);
        pthread_mutex_destroy(&w->mutex);
        goto done;
    }
    if (pthread_join(thread, NULL) != 0) {
        pthread_mutex_lock(&w->mutex);
        while (!w->done) pthread_cond_wait(&w->condition, &w->mutex);
        pthread_mutex_unlock(&w->mutex);
        pthread_detach(thread);
        discard_work(w);
    }
    pthread_cond_destroy(&w->condition);
    pthread_mutex_destroy(&w->mutex);
done:
    if (w->status != HKDF_OK) {
        int status = w->status;
        discard_work(w);
        w->status = status;
    }
    pthread_setcancelstate(old_cancel_state, NULL);
    if (old_cancel_state == PTHREAD_CANCEL_ENABLE) pthread_testcancel();
    pthread_cleanup_pop(0);
}

hkdf *__k_hkdf_new(const unsigned char *name, int *status) {
    hkdf_work w = {0};
    if (!name || !status) return NULL;
    w.kind = -1;
    w.name = (const char *)name;
    run(&w);
    *status = w.status;
    return w.created;
}

hkdf_prk *__k_hkdf_import(const hkdf *h, const unsigned char *bytes,
                          unsigned int length, int *status) {
    hkdf_work w = {0};
    if (!status) return NULL;
    w.kind = -2;
    w.algorithm = h;
    w.key = bytes;
    w.key_length = length;
    run(&w);
    *status = w.status;
    return w.created_prk;
}

int __k_hkdf_derive(const hkdf *h, int mode, const unsigned char *key,
                    unsigned int key_length, const unsigned char *salt,
                    unsigned int salt_length, int absent_salt, const unsigned char *info,
                    unsigned int info_length, unsigned char *out, unsigned int length) {
    hkdf_work w = {0};
    w.kind = mode;
    w.algorithm = h;
    w.key = key;
    w.key_length = key_length;
    w.salt = salt;
    w.salt_length = salt_length;
    w.absent_salt = absent_salt;
    w.info = info;
    w.info_length = info_length;
    w.output = out;
    w.output_length = length;
    run(&w);
    if (w.status != HKDF_OK && out && length) OPENSSL_cleanse(out, length);
    return w.status;
}

hkdf_prk *__k_hkdf_extract(const hkdf *h, const unsigned char *key, unsigned int key_length,
                           const unsigned char *salt, unsigned int salt_length,
                           int absent_salt, int *status) {
    hkdf_work w = {0};
    hkdf_prk *p;
    if (!h || !status) return NULL;
    w.kind = -3;
    w.algorithm = h;
    w.key = key;
    w.key_length = key_length;
    w.salt = salt;
    w.salt_length = salt_length;
    w.absent_salt = absent_salt;
    run(&w);
    *status = w.status;
    p = w.created_prk;
    return p;
}

int __k_hkdf_expand(const hkdf_prk *p, const unsigned char *info, unsigned int info_length,
                    unsigned char *out, unsigned int length) {
    if (!p) return HKDF_INVALID;
    return __k_hkdf_derive(p->algorithm, EVP_KDF_HKDF_MODE_EXPAND_ONLY,
                           p->bytes, p->length, NULL, 0, 0, info, info_length, out, length);
}
