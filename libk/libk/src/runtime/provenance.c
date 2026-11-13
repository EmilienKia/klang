/*
 * K Language runtime — scoped lifetime provenance substrate
 * Copyright 2026 Emilien Kia
 * Licensed under the Apache License, Version 2.0.
 */
#include "provenance.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdlib.h>

enum { TOKEN_LIVE = 0, TOKEN_TRANSITION = 1, TOKEN_DEAD = 2 };
enum { SCOPE_LIVE = 1, SCOPE_CLEANUP = 2 };
#define TOKEN_STATE_MASK UINT64_C(3)
#ifndef K_PROVENANCE_GENERATION_MAX
#define K_PROVENANCE_GENERATION_MAX (UINT64_MAX >> 2)
#endif
_Static_assert(K_PROVENANCE_GENERATION_MAX <= (UINT64_MAX >> 2),
               "provenance generation does not fit");

typedef struct KProvRetired {
    struct KProvRetired* next;
    KProvDomain* domain;
    uint32_t kind;
} KProvRetired;

struct KProvDomain {
    atomic_flag lock;
    KProvRetired* head;
    KProvRetired* tail;
    void (*deallocate)(void*);
};

enum { RETIRED_TOKEN = 1, RETIRED_PATH = 2 };
static void domain_free(void* object) {
    free(object);
}
static KProvDomain domain = { ATOMIC_FLAG_INIT, NULL, NULL, domain_free };

struct KProvToken {
    KProvRetired retired;
    atomic_flag lock;
    uint64_t refs;
    uint64_t state;
    uint64_t pins;
};

struct KProvPath {
    KProvRetired retired;
    atomic_flag lock;
    uint64_t refs;
    KProvPath* parent;
    KProvToken* token;
    uint64_t generation;
};

static void flag_lock(atomic_flag* lock) {
    while (atomic_flag_test_and_set_explicit(lock, memory_order_acquire)) {}
}

/* Dynamically allocated flags begin indeterminate; clear before publication. */
static void flag_init(atomic_flag* lock) {
    atomic_flag_clear_explicit(lock, memory_order_relaxed);
}

static void flag_unlock(atomic_flag* lock) {
    atomic_flag_clear_explicit(lock, memory_order_release);
}

static int try_retain_count(atomic_flag* lock, uint64_t* refs) {
    flag_lock(lock);
    uint64_t count = *refs;
    if (count && count != UINT64_MAX) ++*refs;
    flag_unlock(lock);
    if (!count) abort();
    return count != UINT64_MAX;
}

static void retain_count(atomic_flag* lock, uint64_t* refs) {
    if (!try_retain_count(lock, refs)) abort();
}

static int release_count(atomic_flag* lock, uint64_t* refs) {
    flag_lock(lock);
    uint64_t count = *refs;
    if (count) --*refs;
    flag_unlock(lock);
    if (!count) abort();
    return count == 1;
}

static void retire(KProvRetired* object) {
    KProvDomain* owner = object->domain;
    flag_lock(&owner->lock);
    object->next = NULL;
    if (owner->tail) owner->tail->next = object;
    else owner->head = object;
    owner->tail = object;
    flag_unlock(&owner->lock);
}

static void* allocate_object(size_t size, size_t alignment) {
    if (alignment <= _Alignof(max_align_t)) return malloc(size);
    return aligned_alloc(alignment, size);
}

static void token_lock(KProvToken* token) {
    flag_lock(&token->lock);
}

static void token_unlock(KProvToken* token) {
    flag_unlock(&token->lock);
}

static KProvToken* token_new(void) {
    KProvToken* token = allocate_object(sizeof(*token), _Alignof(KProvToken));
    if (!token) return NULL;
    flag_init(&token->lock);
    token->retired = (KProvRetired){NULL, &domain, RETIRED_TOKEN};
    token->refs = 1;
    token->state = TOKEN_LIVE;
    token->pins = 0;
    return token;
}

static void token_retain(KProvToken* token) {
    retain_count(&token->lock, &token->refs);
}

static void token_release(KProvToken* token) {
    if (token && release_count(&token->lock, &token->refs)) retire(&token->retired);
}

void __k_provenance_path_retain(KProvPath* path) {
    if (path) retain_count(&path->lock, &path->refs);
}

void __k_provenance_path_release(KProvPath* path) {
    if (path && release_count(&path->lock, &path->refs)) retire(&path->retired);
}

KProvDomain* __k_provenance_domain(void) {
    return &domain;
}

uint64_t __k_provenance_collect(KProvDomain* owner, uint64_t max_objects) {
    uint64_t collected = 0;
    while (collected < max_objects) {
        flag_lock(&owner->lock);
        KProvRetired* object = owner->head;
        if (object) {
            owner->head = object->next;
            if (!owner->head) owner->tail = NULL;
        }
        flag_unlock(&owner->lock);
        if (!object) break;
        if (object->kind == RETIRED_PATH) {
            KProvPath* path = (KProvPath*)object;
            __k_provenance_path_release(path->parent);
            token_release(path->token);
        }
        owner->deallocate(object);
        ++collected;
    }
    return collected;
}

int32_t __k_provenance_path_validate(const KProvPath* path) {
    for (; path; path = path->parent) {
        token_lock(path->token);
        uint64_t state = path->token->state;
        token_unlock(path->token);
        if (state != (path->generation << 2)) return K_PROVENANCE_STALE;
    }
    return K_PROVENANCE_OK;
}

static int path_contains(const KProvPath* path, const KProvToken* token) {
    for (; path; path = path->parent) {
        if (path->token == token) return 1;
    }
    return 0;
}

/* The path remains retained for the whole interval in which it is pinned. */
static void unpin_path_until(KProvPath* path, const KProvPath* stop) {
    for (; path != stop; path = path->parent) {
        token_lock(path->token);
        --path->token->pins;
        token_unlock(path->token);
    }
}

static int32_t pin_path(KProvPath* path) {
    for (KProvPath* p = path; p; p = p->parent) {
        token_lock(p->token);
        uint64_t state = p->token->state;
        int32_t result = K_PROVENANCE_OK;
        if (state != (p->generation << 2)) result = K_PROVENANCE_STALE;
        else if (p->token->pins == UINT64_MAX) result = K_PROVENANCE_EXHAUSTED;
        else ++p->token->pins;
        token_unlock(p->token);
        if (result != K_PROVENANCE_OK) {
            unpin_path_until(path, p);
            return result;
        }
    }
    return K_PROVENANCE_OK;
}

int32_t __k_provenance_path_pin(KProvPathPin* out, KProvPath* path) {
    if (!out || out->_self || out->_path) return K_PROVENANCE_INVALID;
    if (path) {
        if (!try_retain_count(&path->lock, &path->refs))
            return K_PROVENANCE_EXHAUSTED;
        int32_t result = pin_path(path);
        if (result != K_PROVENANCE_OK) {
            __k_provenance_path_release(path);
            return result;
        }
    }
    out->_self = out;
    out->_path = path;
    return K_PROVENANCE_OK;
}

int32_t __k_provenance_path_unpin(KProvPathPin* pin) {
    if (!pin || pin->_self != pin) return K_PROVENANCE_INVALID;
    KProvPath* path = pin->_path;
    *pin = (KProvPathPin){0};
    unpin_path_until(path, NULL);
    __k_provenance_path_release(path);
    return K_PROVENANCE_OK;
}

int32_t __k_provenance_scope_validate(const KProvScope* scope) {
    if (!scope) return K_PROVENANCE_INVALID;
    const KProvScope* root = scope;
    while (root->_parent_scope) {
        if (root->_self != root) return K_PROVENANCE_STALE;
        root = root->_parent_scope;
    }
    if (root->_self != root ||
        __k_provenance_path_validate(root->_parent_path) != K_PROVENANCE_OK)
        return K_PROVENANCE_STALE;

    /* Check outer storage before dereferencing any slot nested inside it. */
    const KProvScope* previous = NULL;
    for (;;) {
        const KProvScope* current = scope;
        while (current->_parent_scope != previous) current = current->_parent_scope;
        if (current->_self != current || !current->_slot || current->_slot->_dead)
            return K_PROVENANCE_STALE;
        if (current->_kind == SCOPE_LIVE) {
            if (!current->_slot->_pins || current->_slot->_cleanup)
                return K_PROVENANCE_STALE;
        } else if (current->_kind == SCOPE_CLEANUP) {
            if (current->_slot->_cleanup != current) return K_PROVENANCE_STALE;
        } else {
            return K_PROVENANCE_STALE;
        }
        if (current == scope) return K_PROVENANCE_OK;
        previous = current;
    }
}

static int32_t prepare_parent(KProvParent parent) {
    if (parent.path && parent.scope) return K_PROVENANCE_INVALID;
    if (parent.scope) {
        if (__k_provenance_scope_validate(parent.scope) != K_PROVENANCE_OK)
            return K_PROVENANCE_STALE;
        if (parent.scope->_children == UINT64_MAX) return K_PROVENANCE_EXHAUSTED;
    } else if (parent.path) {
        if (!try_retain_count(&parent.path->lock, &parent.path->refs))
            return K_PROVENANCE_EXHAUSTED;
        int32_t result = pin_path(parent.path);
        if (result != K_PROVENANCE_OK) {
            __k_provenance_path_release(parent.path);
            return result;
        }
    }
    return K_PROVENANCE_OK;
}

static void rollback_parent(KProvParent parent) {
    if (parent.path) {
        unpin_path_until(parent.path, NULL);
        __k_provenance_path_release(parent.path);
    }
}

static void install_scope(KProvScope* out, KProvParent parent, KProvSlot* slot,
                          uint32_t kind) {
    out->_self = out;
    out->_parent_scope = parent.scope;
    out->_parent_path = parent.path;
    out->_slot = slot;
    out->_children = 0;
    out->_kind = kind;
    if (parent.scope) ++parent.scope->_children;
}

static int32_t enter(KProvScope* out, KProvParent parent, KProvSlot* slot,
                     uint32_t kind) {
    if (!out || !slot || out->_self || out->_parent_scope || out->_parent_path ||
        out->_slot || out->_children || out->_kind)
        return K_PROVENANCE_INVALID;
    int32_t result = prepare_parent(parent);
    if (result != K_PROVENANCE_OK) return result;

    KProvToken* retired = NULL;
    if (slot->_dead) result = K_PROVENANCE_DEAD;
    else if (slot->_cleanup) result = K_PROVENANCE_BUSY;
    else if (kind == SCOPE_LIVE) {
        if (slot->_pins == UINT64_MAX) result = K_PROVENANCE_EXHAUSTED;
        else {
            ++slot->_pins;
            install_scope(out, parent, slot, kind);
        }
    } else if (slot->_pins) {
        result = K_PROVENANCE_BUSY;
    } else {
        KProvToken* token = slot->_token;
        if (token) {
            token_lock(token);
            uint64_t state = token->state;
            if ((state & TOKEN_STATE_MASK) != TOKEN_LIVE)
                result = K_PROVENANCE_STALE;
            else if (token->pins)
                result = K_PROVENANCE_BUSY;
            else if ((state >> 2) == K_PROVENANCE_GENERATION_MAX) {
                token->state = TOKEN_DEAD;
                slot->_token = NULL;
                retired = token;
            } else {
                token->state = (((state >> 2) + 1) << 2) | TOKEN_TRANSITION;
            }
            token_unlock(token);
        }
        if (result == K_PROVENANCE_OK) {
            install_scope(out, parent, slot, kind);
            slot->_cleanup = out;
        }
    }
    if (result != K_PROVENANCE_OK) rollback_parent(parent);
    token_release(retired);
    return result;
}

int32_t __k_provenance_enter_live(KProvScope* out, KProvParent parent, KProvSlot* slot) {
    return enter(out, parent, slot, SCOPE_LIVE);
}

int32_t __k_provenance_enter_cleanup(KProvScope* out, KProvParent parent, KProvSlot* slot) {
    return enter(out, parent, slot, SCOPE_CLEANUP);
}

int32_t __k_provenance_leave(KProvScope* scope, int32_t storage_dead) {
    if (!scope || scope->_self != scope || (storage_dead != 0 && storage_dead != 1))
        return K_PROVENANCE_INVALID;
    if (scope->_children) return K_PROVENANCE_BUSY;
    if (storage_dead && scope->_kind != SCOPE_CLEANUP) return K_PROVENANCE_INVALID;
    if (__k_provenance_scope_validate(scope) != K_PROVENANCE_OK)
        return K_PROVENANCE_STALE;

    KProvScope* parent_scope = scope->_parent_scope;
    KProvPath* parent_path = scope->_parent_path;
    KProvSlot* slot = scope->_slot;
    KProvToken* retired = NULL;
    if (scope->_kind == SCOPE_LIVE) {
        --slot->_pins;
    } else {
        KProvToken* token = slot->_token;
        if (token) {
            token_lock(token);
            uint64_t state = token->state;
            if ((state & TOKEN_STATE_MASK) != TOKEN_TRANSITION || token->pins) {
                token_unlock(token);
                return K_PROVENANCE_STALE;
            }
            token->state = (state & ~TOKEN_STATE_MASK) |
                           (storage_dead ? TOKEN_DEAD : TOKEN_LIVE);
            token_unlock(token);
            if (storage_dead) {
                slot->_token = NULL;
                retired = token;
            }
        }
        if (storage_dead) slot->_dead = 1;
        slot->_cleanup = NULL;
    }
    *scope = (KProvScope){0};
    if (parent_scope) --parent_scope->_children;
    if (parent_path) {
        unpin_path_until(parent_path, NULL);
        __k_provenance_path_release(parent_path);
    }
    token_release(retired);
    return K_PROVENANCE_OK;
}

int32_t __k_provenance_slot_close(KProvSlot* slot) {
    if (!slot) return K_PROVENANCE_INVALID;
    if (slot->_dead) return K_PROVENANCE_DEAD;
    if (slot->_cleanup || slot->_pins) return K_PROVENANCE_BUSY;
    KProvToken* token = slot->_token;
    if (token) {
        token_lock(token);
        uint64_t state = token->state;
        if ((state & TOKEN_STATE_MASK) != TOKEN_LIVE) {
            token_unlock(token);
            return K_PROVENANCE_STALE;
        }
        if (token->pins) {
            token_unlock(token);
            return K_PROVENANCE_BUSY;
        }
        token->state = TOKEN_DEAD;
        token_unlock(token);
        slot->_token = NULL;
    }
    slot->_dead = 1;
    token_release(token);
    return K_PROVENANCE_OK;
}

/* A repeated live frame or an already represented root token needs no node. */
static int needs_node(const KProvScope* leaf, const KProvScope* current,
                      const KProvPath* base) {
    for (const KProvScope* p = leaf; p != current; p = p->_parent_scope) {
        if (p->_slot == current->_slot) return 0;
    }
    return !current->_slot->_token || !path_contains(base, current->_slot->_token);
}

int32_t __k_provenance_capture(KProvParent parent, KProvPath** out) {
    if (!out) return K_PROVENANCE_INVALID;
    *out = NULL;
    if (parent.path && parent.scope) return K_PROVENANCE_INVALID;
    if (!parent.scope) {
        if (!parent.path) return K_PROVENANCE_INVALID;
        if (__k_provenance_path_validate(parent.path) != K_PROVENANCE_OK)
            return K_PROVENANCE_STALE;
        __k_provenance_path_retain(parent.path);
        *out = parent.path;
        return K_PROVENANCE_OK;
    }
    if (__k_provenance_scope_validate(parent.scope) != K_PROVENANCE_OK)
        return K_PROVENANCE_STALE;
    const KProvScope* root = parent.scope;
    for (const KProvScope* p = parent.scope; p; p = p->_parent_scope) {
        if (p->_kind == SCOPE_CLEANUP) return K_PROVENANCE_SCOPE_ESCAPE;
        root = p;
    }
    KProvPath* base = root->_parent_path;
    KProvPath* first = NULL;
    KProvPath** tail = &first;
    for (const KProvScope* p = parent.scope; p; p = p->_parent_scope) {
        if (!needs_node(parent.scope, p, base)) continue;
        KProvPath* node = allocate_object(sizeof(*node), _Alignof(KProvPath));
        if (!node) {
            __k_provenance_path_release(first);
            return K_PROVENANCE_NO_MEMORY;
        }
        flag_init(&node->lock);
        node->retired = (KProvRetired){NULL, &domain, RETIRED_PATH};
        node->refs = 1;
        node->parent = NULL;
        node->token = NULL;
        node->generation = 0;
        KProvToken* token = p->_slot->_token;
        if (token) token_retain(token);
        else token = token_new();
        if (!token) {
            __k_provenance_path_release(node);
            __k_provenance_path_release(first);
            return K_PROVENANCE_NO_MEMORY;
        }
        token_lock(token);
        uint64_t state = token->state;
        token_unlock(token);
        if ((state & TOKEN_STATE_MASK) != TOKEN_LIVE) {
            token_release(token);
            __k_provenance_path_release(node);
            __k_provenance_path_release(first);
            return K_PROVENANCE_STALE;
        }
        node->token = token;
        node->generation = state >> 2;
        *tail = node;
        tail = &node->parent;
    }
    *tail = base;
    __k_provenance_path_retain(base);
    if (__k_provenance_scope_validate(parent.scope) != K_PROVENANCE_OK) {
        __k_provenance_path_release(first);
        return K_PROVENANCE_STALE;
    }
    KProvPath* node = first;
    for (const KProvScope* p = parent.scope; p; p = p->_parent_scope) {
        if (!needs_node(parent.scope, p, base)) continue;
        if (!p->_slot->_token) {
            token_retain(node->token);
            p->_slot->_token = node->token;
        }
        node = node->parent;
    }
    *out = first;
    return K_PROVENANCE_OK;
}
