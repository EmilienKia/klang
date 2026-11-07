/*
 * K Language standard library -- OpenSSL FFI wrappers
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/evperr.h>

EVP_MD* __k_EVP_MD_fetch(const unsigned char* name) {
    return EVP_MD_fetch(NULL, (const char*)name, NULL);
}

// Wrap of inlined EVP_MD_CTX_get_size(...).
int __k_EVP_MD_CTX_get_size(EVP_MD_CTX* ctx) {
    return EVP_MD_CTX_size(ctx);
}

typedef struct {
    char *name;
    unsigned int length;
    EVP_MD *md;
    int standard;
} cap_digest_entry;

typedef struct {
    cap_digest_entry *entries;
    size_t count;
    size_t capacity;
} cap_digest_snapshot;

typedef struct {
    char **names;
    size_t count;
    size_t capacity;
    int failed;
} cap_digest_candidates;

static const char *const cap_digest_standard_names[] = {
    "MD5", "SHA-1", "SHA-224", "SHA-256", "SHA-384", "SHA-512",
    "SHA-512/224", "SHA-512/256", "SHA3-224", "SHA3-256",
    "SHA3-384", "SHA3-512"
};

/*
 * Inspect all errors, not just the last one: fetch can append "unsupported"
 * after an earlier allocation failure. Discovery starts with an empty queue.
 */
static int cap_digest_errors(unsigned long previous, int failed) {
    unsigned long error;
    int unsupported = 0;
    int other = 0;

    if (previous != 0) {
        return failed || ERR_peek_last_error() != previous ? -1 : 0;
    }
    while ((error = ERR_get_error()) != 0) {
        if (ERR_GET_LIB(error) == ERR_LIB_EVP &&
            (ERR_GET_REASON(error) == ERR_R_UNSUPPORTED ||
             ERR_GET_REASON(error) == EVP_R_UNSUPPORTED_ALGORITHM)) {
            unsupported = 1;
        } else {
            other = 1;
        }
    }
    if (other) {
        return -1;
    }
    return unsupported ? 1 : 0;
}

static int cap_digest_probe_errors(unsigned long previous, int failed) {
    unsigned long error;
    int has_errors = 0;
    int internal = 0;

    if (previous != 0) {
        return -1;
    }
    while ((error = ERR_get_error()) != 0) {
        int reason = ERR_GET_REASON(error);
        has_errors = 1;
        if (ERR_SYSTEM_ERROR(error) || ERR_FATAL_ERROR(error) ||
            reason == ERR_R_FETCH_FAILED || reason == ERR_R_OPERATION_FAIL ||
            (ERR_GET_LIB(error) == ERR_LIB_EVP &&
             reason == EVP_R_INITIALIZATION_ERROR)) {
            internal = 1;
        }
    }
    if (internal || (!failed && has_errors)) {
        return -1;
    }
    return failed ? 0 : 1;
}

static void *cap_digest_grow(void *items, size_t *capacity, size_t count,
                             size_t item_size) {
    size_t next;
    void *grown;

    if (count < *capacity) {
        return items;
    }
    if (count >= UINT_MAX || count >= SIZE_MAX / item_size) {
        return NULL;
    }
    next = *capacity == 0 ? 8 : *capacity;
    if (next <= count) {
        if (next > UINT_MAX / 2 || next > SIZE_MAX / item_size / 2) {
            next = count + 1;
        } else {
            next *= 2;
        }
    }
    if (next > UINT_MAX || next > SIZE_MAX / item_size) {
        return NULL;
    }
    grown = realloc(items, next * item_size);
    if (grown == NULL) {
        return NULL;
    }
    *capacity = next;
    return grown;
}

static char *cap_digest_copy_name(const char *name, unsigned int *length) {
    size_t size;
    char *copy;

    if (name == NULL) {
        return NULL;
    }
    size = strlen(name);
    if (size == 0 || size >= UINT_MAX || size == SIZE_MAX) {
        return NULL;
    }
    copy = malloc(size + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, name, size + 1);
    if (length != NULL) {
        *length = (unsigned int)size;
    }
    return copy;
}

static void cap_digest_collect_name(const char *provider_name, void *arg) {
    cap_digest_candidates *candidates = arg;
    char *name;
    char **grown;

    if (candidates->failed) {
        return;
    }
    name = cap_digest_copy_name(provider_name, NULL);
    if (name == NULL) {
        candidates->failed = 1;
        return;
    }
    grown = cap_digest_grow(candidates->names, &candidates->capacity,
                            candidates->count, sizeof(*candidates->names));
    if (grown == NULL) {
        free(name);
        candidates->failed = 1;
        return;
    }
    candidates->names = grown;
    candidates->names[candidates->count++] = name;
}

static void cap_digest_collect(EVP_MD *md, void *arg) {
    cap_digest_candidates *candidates = arg;

    if (!candidates->failed &&
        EVP_MD_names_do_all(md, cap_digest_collect_name, candidates) != 1) {
        candidates->failed = 1;
    }
}

static int cap_digest_name_cmp(const void *a, const void *b) {
    const char *const *left = a;
    const char *const *right = b;
    return strcmp(*left, *right);
}

static int cap_digest_entry_cmp(const void *a, const void *b) {
    const cap_digest_entry *left = a;
    const cap_digest_entry *right = b;
    return strcmp(left->name, right->name);
}

static int cap_digest_equivalent(const EVP_MD *a, const EVP_MD *b) {
    const char *a_name = EVP_MD_get0_name(a);
    const char *b_name = EVP_MD_get0_name(b);
    return a == b ||
           (a_name != NULL && b_name != NULL &&
            EVP_MD_is_a(a, b_name) && EVP_MD_is_a(b, a_name));
}

static int cap_digest_same(const EVP_MD *a, const EVP_MD *b) {
    return EVP_MD_get0_provider(a) == EVP_MD_get0_provider(b) &&
           cap_digest_equivalent(a, b);
}

/* Return 1 for a fetch, 0 for an unavailable name, and -1 for a failure. */
static int cap_digest_fetch(const char *name, EVP_MD **result) {
    unsigned long previous = ERR_peek_last_error();
    int errors;

    *result = EVP_MD_fetch(NULL, name, NULL);
    errors = cap_digest_errors(previous, *result == NULL);
    if (*result == NULL) {
        return errors == 1 ? 0 : -1;
    }
    if (errors != 0) {
        EVP_MD_free(*result);
        *result = NULL;
        return -1;
    }
    return 1;
}

static int cap_digest_probe(EVP_MD *md, int update) {
    unsigned long previous;
    EVP_MD_CTX *ctx;
    unsigned char *output = NULL;
    const unsigned char byte = 0;
    unsigned int written = 0;
    int size;
    int ok = -1;

    previous = ERR_peek_last_error();
    ctx = EVP_MD_CTX_new();
    if (cap_digest_errors(previous, ctx == NULL) != 0 || ctx == NULL) {
        EVP_MD_CTX_free(ctx);
        return -1;
    }
    previous = ERR_peek_last_error();
    if (EVP_DigestInit_ex(ctx, md, NULL) != 1) {
        ok = cap_digest_probe_errors(previous, 1);
        goto done;
    }
    if (cap_digest_probe_errors(previous, 0) < 0) {
        goto done;
    }
    previous = ERR_peek_last_error();
    size = EVP_MD_CTX_size(ctx);
    if (cap_digest_errors(previous, 0) != 0) {
        goto done;
    }
    if (size <= 0) {
        ok = 0;
        goto done;
    }
    output = malloc((size_t)size);
    if (output == NULL) {
        goto done;
    }
    if (update) {
        previous = ERR_peek_last_error();
        if (EVP_DigestUpdate(ctx, &byte, 1) != 1) {
            ok = cap_digest_probe_errors(previous, 1);
            goto done;
        }
        if (cap_digest_probe_errors(previous, 0) < 0) {
            goto done;
        }
    }
    previous = ERR_peek_last_error();
    if (EVP_DigestFinal_ex(ctx, output, &written) != 1) {
        ok = cap_digest_probe_errors(previous, 1);
        goto done;
    }
    if (cap_digest_probe_errors(previous, 0) < 0) {
        goto done;
    }
    ok = written == (unsigned int)size ? 1 : 0;
done:
    free(output);
    EVP_MD_CTX_free(ctx);
    return ok;
}

/* Exercise both the empty-message path and the public nonempty update path. */
static int cap_digest_usable(EVP_MD *md) {
    int empty = cap_digest_probe(md, 0);
    return empty == 1 ? cap_digest_probe(md, 1) : empty;
}

/*
 * Only publish a preferred name after checking that the default fetch policy
 * resolves it back to this implementation.
 */
static int cap_digest_preferred(EVP_MD *md, const char *candidate,
                                const char **name, int *standard) {
    size_t i;
    EVP_MD *check;
    int fetched;
    const char *primary;

    *name = candidate;
    *standard = 0;
    for (i = 0; i < sizeof(cap_digest_standard_names) /
                        sizeof(cap_digest_standard_names[0]); ++i) {
        if (!EVP_MD_is_a(md, cap_digest_standard_names[i])) {
            continue;
        }
        fetched = cap_digest_fetch(cap_digest_standard_names[i], &check);
        if (fetched < 0) {
            return 0;
        }
        if (fetched > 0) {
            int same = cap_digest_same(md, check);
            EVP_MD_free(check);
            if (same) {
                *name = cap_digest_standard_names[i];
                *standard = 1;
                return 1;
            }
        }
    }
    primary = EVP_MD_get0_name(md);
    if (primary == NULL) {
        return 0;
    }
    fetched = cap_digest_fetch(primary, &check);
    if (fetched < 0) {
        return 0;
    }
    if (fetched > 0) {
        int same = cap_digest_same(md, check);
        EVP_MD_free(check);
        if (same) {
            *name = primary;
        }
    }
    return 1;
}

void __k_cap_digest_free(void *handle) {
    cap_digest_snapshot *snapshot = handle;
    size_t i;

    if (snapshot == NULL) {
        return;
    }
    for (i = 0; i < snapshot->count; ++i) {
        free(snapshot->entries[i].name);
        EVP_MD_free(snapshot->entries[i].md);
    }
    free(snapshot->entries);
    free(snapshot);
}

static void *cap_digest_discover(int *status) {
    cap_digest_candidates candidates = {0};
    cap_digest_snapshot *snapshot;
    unsigned long previous;
    size_t i;
    int errors;

    if (status == NULL) {
        return NULL;
    }
    *status = -3;
    snapshot = calloc(1, sizeof(*snapshot));
    if (snapshot == NULL) {
        return NULL;
    }
    previous = ERR_peek_last_error();
    EVP_MD_do_all_provided(NULL, cap_digest_collect, &candidates);
    errors = cap_digest_errors(previous, 0);
    if (candidates.failed || errors != 0) {
        goto fail;
    }
    if (candidates.count > 1) {
        qsort(candidates.names, candidates.count, sizeof(*candidates.names),
              cap_digest_name_cmp);
    }
    for (i = 0; i < candidates.count; ++i) {
        EVP_MD *md = NULL;
        const char *name;
        char *copy;
        unsigned int length;
        size_t j;
        int standard;
        int fetched = cap_digest_fetch(candidates.names[i], &md);
        int usable;

        if (fetched < 0) {
            goto fail;
        }
        if (fetched == 0) {
            continue;
        }
        usable = cap_digest_usable(md);
        if (usable < 0) {
            EVP_MD_free(md);
            goto fail;
        }
        if (usable == 0) {
            EVP_MD_free(md);
            continue;
        }
        if (!cap_digest_preferred(md, candidates.names[i], &name, &standard)) {
            EVP_MD_free(md);
            goto fail;
        }
        for (j = 0; j < snapshot->count; ++j) {
            if (cap_digest_equivalent(snapshot->entries[j].md, md)) {
                break;
            }
        }
        if (j < snapshot->count &&
            (snapshot->entries[j].standard > standard ||
             (snapshot->entries[j].standard == standard &&
              strcmp(snapshot->entries[j].name, name) <= 0))) {
            EVP_MD_free(md);
            continue;
        }
        copy = cap_digest_copy_name(name, &length);
        if (copy == NULL) {
            EVP_MD_free(md);
            goto fail;
        }
        if (j < snapshot->count) {
            free(snapshot->entries[j].name);
            EVP_MD_free(snapshot->entries[j].md);
        } else {
            cap_digest_entry *grown =
                cap_digest_grow(snapshot->entries, &snapshot->capacity,
                                snapshot->count, sizeof(*snapshot->entries));
            if (grown == NULL) {
                free(copy);
                EVP_MD_free(md);
                goto fail;
            }
            snapshot->entries = grown;
            ++snapshot->count;
        }
        snapshot->entries[j].name = copy;
        snapshot->entries[j].length = length;
        snapshot->entries[j].md = md;
        snapshot->entries[j].standard = standard;
    }
    if (snapshot->count > 1) {
        qsort(snapshot->entries, snapshot->count, sizeof(*snapshot->entries),
              cap_digest_entry_cmp);
    }
    for (i = 0; i < snapshot->count; ++i) {
        EVP_MD_free(snapshot->entries[i].md);
        snapshot->entries[i].md = NULL;
    }
    for (i = 0; i < candidates.count; ++i) {
        free(candidates.names[i]);
    }
    free(candidates.names);
    *status = 0;
    return snapshot;
fail:
    for (i = 0; i < candidates.count; ++i) {
        free(candidates.names[i]);
    }
    free(candidates.names);
    __k_cap_digest_free(snapshot);
    return NULL;
}

typedef struct {
    OSSL_LIB_CTX *libctx;
    cap_digest_snapshot *snapshot;
    int status;
    atomic_uint references;
    atomic_int done;
} cap_digest_job;

static void cap_digest_job_release(cap_digest_job *job) {
    if (atomic_fetch_sub_explicit(&job->references, 1, memory_order_acq_rel) == 1) {
        __k_cap_digest_free(job->snapshot);
        free(job);
    }
}

static void *cap_digest_worker(void *arg) {
    cap_digest_job *job = arg;
    OSSL_LIB_CTX *previous = OSSL_LIB_CTX_set0_default(job->libctx);

    if (previous != NULL) {
        int selected = OSSL_LIB_CTX_set0_default(NULL) == job->libctx;
        if (cap_digest_errors(0, 0) == 0 && selected) {
            job->snapshot = cap_digest_discover(&job->status);
        }
        if (OSSL_LIB_CTX_set0_default(previous) != job->libctx) {
            __k_cap_digest_free(job->snapshot);
            job->snapshot = NULL;
            job->status = -3;
        }
    }
    OPENSSL_thread_stop_ex(job->libctx);
    OPENSSL_thread_stop();
    atomic_store_explicit(&job->done, 1, memory_order_release);
    cap_digest_job_release(job);
    return NULL;
}

void *__k_cap_digest_new(int *status) {
    cap_digest_job *job;
    cap_digest_snapshot *snapshot;
    pthread_t thread;

    if (status == NULL) {
        return NULL;
    }
    *status = -3;
    if (ERR_peek_last_error() == 0) {
        return cap_digest_discover(status);
    }

    /*
     * OpenSSL does not expose a way to inspect all newly pushed errors without
     * removing older caller-owned errors. Probe on an isolated error queue,
     * retaining the calling thread's actual default context and properties.
     */
    job = calloc(1, sizeof(*job));
    if (job == NULL) {
        return NULL;
    }
    job->libctx = OSSL_LIB_CTX_set0_default(NULL);
    if (job->libctx == NULL) {
        free(job);
        return NULL;
    }
    job->status = -3;
    atomic_init(&job->references, 2);
    atomic_init(&job->done, 0);
    if (pthread_create(&thread, NULL, cap_digest_worker, job) != 0) {
        free(job);
        return NULL;
    }
    if (pthread_join(thread, NULL) != 0) {
        while (!atomic_load_explicit(&job->done, memory_order_acquire)) {
            sched_yield();
        }
        pthread_detach(thread);
        cap_digest_job_release(job);
        return NULL;
    }
    snapshot = job->snapshot;
    job->snapshot = NULL;
    *status = job->status;
    cap_digest_job_release(job);
    return snapshot;
}

unsigned int __k_cap_digest_count(const void *handle) {
    const cap_digest_snapshot *snapshot = handle;
    return (unsigned int)snapshot->count;
}

unsigned int __k_cap_digest_size(const void *handle, unsigned int index) {
    const cap_digest_snapshot *snapshot = handle;
    return snapshot->entries[index].length;
}

void __k_cap_digest_copy(const void *handle, unsigned int index, unsigned char *out) {
    const cap_digest_snapshot *snapshot = handle;
    const cap_digest_entry *entry = &snapshot->entries[index];
    memcpy(out, entry->name, (size_t)entry->length + 1);
}
