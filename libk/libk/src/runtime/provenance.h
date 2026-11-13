/*
 * K Language runtime — scoped lifetime provenance ABI
 * Copyright 2026 Emilien Kia
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef KLANG_RUNTIME_PROVENANCE_H
#define KLANG_RUNTIME_PROVENANCE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KProvToken KProvToken;
typedef struct KProvPath KProvPath;
typedef struct KProvScope KProvScope;
typedef struct KProvDomain KProvDomain;

/*
 * Caller-owned, zero-initialized, address-stable stack storage. An active pin
 * is thread-confined, non-copyable and nonescaping; its fields are private.
 * Its storage must not overlap any path, slot, scope, or other pin.
 */
typedef struct KProvPathPin {
    struct KProvPathPin* _self;
    KProvPath* _path;
} KProvPathPin;

/* Embedded in stable union storage, zero-initialized once per lifetime. */
typedef struct KProvSlot {
    KProvToken* _token;
    KProvScope* _cleanup;
    uint64_t _pins;
    uint32_t _dead;
} KProvSlot;

/*
 * A parent is either a borrowed ordinary path, a borrowed active scope, or
 * neither. Supplying both is invalid. The compiler must supply the complete
 * actual ancestry; the runtime cannot infer containment from addresses. In
 * particular, passing an ordinary path instead of an active cleanup scope
 * must not be used to erase cleanup taint.
 */
typedef struct KProvParent {
    KProvPath* path;
    KProvScope* scope;
} KProvParent;

/*
 * Caller-owned, zero-initialized, address-stable stack storage. The slot holds
 * a borrowed pointer to an active cleanup scope only until that scope is left.
 * An active scope is thread-confined, non-copyable, and must be left exactly
 * once. Its storage must not overlap any slot or another scope. Parent scope
 * storage must outlive all children. Fields are runtime-private.
 */
struct KProvScope {
    KProvScope* _self;
    KProvScope* _parent_scope;
    KProvPath* _parent_path;
    KProvSlot* _slot;
    uint64_t _children;
    uint32_t _kind;
};

enum {
    K_PROVENANCE_OK = 0,
    K_PROVENANCE_STALE = 1,
    K_PROVENANCE_BUSY = 2,
    K_PROVENANCE_DEAD = 3,
    K_PROVENANCE_EXHAUSTED = 4,
    K_PROVENANCE_NO_MEMORY = 5,
    K_PROVENANCE_INVALID = 6,
    K_PROVENANCE_SCOPE_ESCAPE = 7
};

/*
 * Enter, leave, slot close and path release neither allocate nor call free.
 * A live scope pins its slot. A cleanup scope invalidates its slot's previous
 * payload paths and excludes reentry. Root ordinary-path ancestors are pinned
 * until the scope ends; nested scopes keep their parent alive. A parent with
 * children cannot be left. Failed entry leaves its zeroed output reusable and
 * rolls back acquired pins. A successful
 * cleanup entry invalidates old paths immediately, even if destruction throws.
 * Leave with storage_dead=1 permanently closes a cleanup slot; it is invalid
 * for a live scope. The slot itself must remain alive through leave, including
 * unwind. The compiler remains responsible for the union's constructed tag.
 * Slot close permanently closes a live slot without invoking cleanup; the
 * compiler must first guard any enclosing storage.
 */
int32_t __k_provenance_enter_live(KProvScope* out, KProvParent parent, KProvSlot* slot);
int32_t __k_provenance_enter_cleanup(KProvScope* out, KProvParent parent, KProvSlot* slot);
int32_t __k_provenance_scope_validate(const KProvScope* scope);
int32_t __k_provenance_leave(KProvScope* scope, int32_t storage_dead);
int32_t __k_provenance_slot_close(KProvSlot* slot);

/*
 * Capture creates an independently retained ordinary path for a live scope,
 * or retains an ordinary parent path. It rejects any cleanup-tainted scope
 * with SCOPE_ESCAPE and a NULL output. Allocation failure returns NO_MEMORY
 * with a NULL output; no untracked fallback is permitted. A NULL path means an
 * untracked ordinary observer and validates successfully; NULL is never a
 * successful capture result. The compiler must not substitute NULL for tracked
 * provenance, and must still perform the usual null-pointer and const checks.
 *
 * Paths contain retained tokens only: no slot or stack-scope pointers. A
 * caller using or retaining a path must already own a reference to it.
 */
int32_t __k_provenance_capture(KProvParent parent, KProvPath** out);
void __k_provenance_path_retain(KProvPath* path);
void __k_provenance_path_release(KProvPath* path);
int32_t __k_provenance_path_validate(const KProvPath* path);

/*
 * Pin an ordinary retained path across an operation such as RHS evaluation.
 * The caller must own path until pin returns. Success retains the complete
 * path and pins every token, blocking transitions and slot close for all
 * represented ancestors until unpin. NULL is an ordinary untracked path:
 * pin succeeds but protects no storage. A failed tracked pin never becomes an
 * untracked success. STALE or EXHAUSTED rolls back every acquired pin and
 * reference and leaves the zeroed output reusable.
 * The compiler must select the complete ancestry of the lvalue's storage:
 * attempting to transition any represented token, even as the final operation,
 * is BUSY while the pin is active.
 *
 * out must be zeroed and inactive. A successful pin, including a NULL pin,
 * must be unpinned exactly once on every normal and exceptional exit before
 * its stack storage dies. Pinning into an active handle or unpinning a
 * differently addressed copy or an already unpinned handle returns INVALID.
 * A cleared handle may be reused for a new pin. C cannot detect all bytewise
 * copying, including restoration at the original address.
 * The compiler must enforce the lifetime and non-copying constraints. Neither
 * operation allocates or calls free; final releases are deferred to collection.
 *
 * This API cannot represent cleanup-tainted scope access. Such accesses must
 * use their active scope ancestry; never manufacture an ordinary retained
 * path or NULL to erase cleanup authority or evade tracked provenance.
 */
int32_t __k_provenance_path_pin(KProvPathPin* out, KProvPath* path);
int32_t __k_provenance_path_unpin(KProvPathPin* pin);

/*
 * The domain accessor returns a borrowed pointer to this runtime instance's
 * domain. A host must register each allocating instance's actual domain while
 * that instance is resident, without symbol interposition redirecting the
 * accessor to another instance. Reclaim at most max_objects retired paths/tokens from
 * a non-NULL, registered, still-resident domain and return the number reclaimed.
 * Zero does no work. Collection may call free, cascade into releases, and
 * enqueue objects in other domains. It is only permitted at a verified safe point
 * outside unwinding/cleanup; it may run concurrently with other collectors
 * and provenance operations.
 *
 * A final release queues its object in its allocation domain, regardless of
 * which compatible runtime instance performs the release. A path owns its
 * parent and token until it is collected. All participating instances must
 * use this same ABI, including atomic representation and operations, and keep
 * their code and allocators resident while their objects or any calls to their
 * code exist. A zero collection count does not
 * prove there are no live objects, concurrent collectors, or pending releases.
 * This API provides no unload barrier or loader-handle retention; until a host
 * can prove global quiescence and complete reclamation, it must keep every
 * registered domain resident for the process lifetime.
 *
 * Compiler/host integration must schedule safe-point and shutdown collection.
 * There is no background collector or retention bound in this substrate.
 */
KProvDomain* __k_provenance_domain(void);
uint64_t __k_provenance_collect(KProvDomain* domain, uint64_t max_objects);

/*
 * The compiler must preserve and pass scoped ancestry through all K calls,
 * projections, casts, temporaries and exceptional exits. Scoped references
 * cannot be retained, returned, stored, or passed as ordinary paths; cleanup
 * privilege is limited to the payload being destroyed. Never retain a pointer
 * to a scope. C cannot check use of a scope after its stack lifetime ends.
 *
 * Every enclosing lifetime that can overwrite a nested slot must be guarded,
 * including containing aggregates. Every embedded slot must be closed before
 * its storage dies, including on exceptional exits. A live slot cannot be
 * copied, moved, or reinitialized; moving its containing value requires a
 * compiler-managed lifetime change, not a byte copy. A slot cannot be closed
 * while pinned or under cleanup. Raw FFI pointers are unsafe.
 * Per-object atomic-flag locks protect token/path state and reference counts
 * only: the caller must externally synchronize all access to union storage,
 * slots, and scopes across threads, including the interval between validation
 * and payload access. The
 * runtime holds no lock across user code or destructors.
 *
 * On generation exhaustion cleanup retires the old token without allocation;
 * the slot remains usable without a token until a later capture allocates one.
 * The caller must handle capture errors and must not manufacture an untracked
 * path; NO_MEMORY may be mapped to __k_fatal_memory_allocation outside the
 * allocation-free cleanup machinery. Pin/child count exhaustion reports
 * EXHAUSTED; explicit token and path reference-count overflow is fatal. C11
 * atomic_flag operations are lock-free, but the spinlocks can wait and are
 * neither async-signal-safe nor safe under asynchronous cancellation. The C
 * standard does not guarantee that an implementation's atomic operations
 * avoid internal allocation; that requires toolchain-specific verification.
 */

#ifdef __cplusplus
}
#endif
#endif
