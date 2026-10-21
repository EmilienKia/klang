/*
 * Borrowed synchronous argument bindings.
 * Copyright 2026 Emilien Kia
 * SPDX-License-Identifier: Apache-2.0
 */
module k;

enum ArgumentCategory {
    UNBOUND = 0;
    VALUE_LVALUE = 1;
    ADDRESSER_SLOT = 2;
    DRAIN = 3;
    REFERENCE = 4;
    VALUE_TEMPORARY = 5;
}

enum ArgumentErrorReason {
    UNBOUND = 1;
    TYPE_MISMATCH = 2;
    CONST_VIOLATION = 3;
    OWNER_TAKE_FORBIDDEN = 4;
    DRAIN_FORBIDDEN = 5;
    UNTRUSTED_TYPE = 6;
    NULL_NOT_ALLOWED = 7;
}

/** Unchecked, catchable failure of a checked Argument access. */
class ArgumentError : FatalError {
public:
    ArgumentError(reason : ArgumentErrorReason) : FatalError((int) reason) {}
    const getReason() : ArgumentErrorReason { return (ArgumentErrorReason) getCode(); }
}

/**
 * Immutable borrowed binding, not an owned or boxed value.
 *
 * SYNCHRONOUS USE ONLY. Do not retain or enqueue for asynchronous use, return
 * holders backed by expired locals, invalidate source slots, or relocate or
 * destroy containing storage while a holder is used. Copies do not extend any
 * lifetime. Keeping a pointee alive does not keep its original slot alive.
 *
 * Assignment replaces this holder's metadata; it never rebinds an external
 * slot. Copies, including array elements, observe the same current binding.
 */
struct Argument {
private:
    _slot : byte* = null;
    _logical : const rtti::Type? = null;
    _binding : const rtti::Type? = null;
    _referent : const rtti::Type? = null;
    _category : unsigned int = 0u;
    _capabilities : unsigned int = 0u;
    _indirect : bool = false;

    private static _unqualified(t : const rtti::Type?) : const rtti::Type? {
        if (t != null && t->getKind() == rtti::TypeKind::CONST)
            return _unqualified(t->getElement());
        return t;
    }

    private static _trusted(t : const rtti::Type?) : bool {
        if (t == null || !t->isResolved()) return false;
        kind : rtti::TypeKind = t->getKind();
        if (kind == rtti::TypeKind::PRIMITIVE) return true;
        if (kind == rtti::TypeKind::CONST
            || kind == rtti::TypeKind::REFERENCE || kind == rtti::TypeKind::VIEW
            || kind == rtti::TypeKind::LINK || kind == rtti::TypeKind::POINTER
            || kind == rtti::TypeKind::OWNER || kind == rtti::TypeKind::DRAIN
            || kind == rtti::TypeKind::ARRAY || kind == rtti::TypeKind::SIZED_ARRAY) {
            return _trusted(t->getElement());
        }
        id : const rtti::NominalIdentity? = t->getNominalIdentity();
        if (id == null || !id->hasResolvedProducer()) return false;
        if (kind == rtti::TypeKind::ALIAS) return _trusted(t->getElement());
        return true;
    }

    private static _sameType(a : const rtti::Type?, b : const rtti::Type?) : bool {
        if (a == b) return true;
        if (a == null || b == null || a->getKind() != b->getKind()) return false;
        kind : rtti::TypeKind = a->getKind();
        if (kind == rtti::TypeKind::PRIMITIVE) return a->getDetail() == b->getDetail();
        if (kind == rtti::TypeKind::CONST
            || kind == rtti::TypeKind::REFERENCE || kind == rtti::TypeKind::VIEW
            || kind == rtti::TypeKind::LINK || kind == rtti::TypeKind::POINTER
            || kind == rtti::TypeKind::OWNER || kind == rtti::TypeKind::DRAIN
            || kind == rtti::TypeKind::ARRAY || kind == rtti::TypeKind::SIZED_ARRAY) {
            return a->getExtent() == b->getExtent()
                && _sameType(a->getElement(), b->getElement());
        }
        // Nominal descriptors must be the canonical descriptor in this image.
        // Symbolic producer binding across images is deliberately not inferred.
        return false;
    }

    private const _checkBound() {
        if (!isBound()) throw ArgumentError(ArgumentErrorReason::UNBOUND);
    }

    private const _check(expected : const rtti::Type?, access : unsigned int) {
        _checkBound();
        if (!_trusted(_referent) || !_trusted(expected)) {
            throw ArgumentError(ArgumentErrorReason::UNTRUSTED_TYPE);
        }
        if (!_sameType(_unqualified(_referent), _unqualified(expected))) {
            throw ArgumentError(ArgumentErrorReason::TYPE_MISMATCH);
        }
        if (access != 0u && expected->getKind() != rtti::TypeKind::CONST
            && _referent->getKind() == rtti::TypeKind::CONST) {
            throw ArgumentError(ArgumentErrorReason::CONST_VIOLATION);
        }
        if (access == 1u && !canMutate()) {
            throw ArgumentError(ArgumentErrorReason::CONST_VIOLATION);
        }
        if (access == 2u && !canTakeOwner()) {
            throw ArgumentError(ArgumentErrorReason::OWNER_TAKE_FORBIDDEN);
        }
        if (access == 2u && _unqualified(_logical)->getKind() != rtti::TypeKind::OWNER) {
            throw ArgumentError(ArgumentErrorReason::TYPE_MISMATCH);
        }
        if (access == 3u && !canDrain()) {
            throw ArgumentError(ArgumentErrorReason::DRAIN_FORBIDDEN);
        }
    }

    private const _checkTake(expected : const rtti::Type?) {
        _checkBound();
        if (!canTakeOwner()) throw ArgumentError(ArgumentErrorReason::OWNER_TAKE_FORBIDDEN);
        if (!_trusted(_logical) || !_trusted(expected)) {
            throw ArgumentError(ArgumentErrorReason::UNTRUSTED_TYPE);
        }
        if (!_sameType(_unqualified(_logical), _unqualified(expected))) {
            throw ArgumentError(ArgumentErrorReason::TYPE_MISMATCH);
        }
    }

    private static _checkAddress(address : byte*) : byte* {
        if (address == null) throw ArgumentError(ArgumentErrorReason::NULL_NOT_ALLOWED);
        return address;
    }

    @annotations::Intrinsic("argument.is_null.v1")
    private const _isNull() : bool;

public:
    /** Capture an addressable caller binding without copying or moving its value.
     * Borrowed synchronous use only; the original slot must remain alive and stable.
     */
    @annotations::Intrinsic("argument.capture.v1")
    template<typename T>
    static capture(source : T&) : Argument;

    /** Readonly, non-consuming capture. Copies cannot recover removed capabilities.
     * Borrowed synchronous use only; neither source nor referent is retained.
     */
    @annotations::Intrinsic("argument.borrow.v1")
    template<typename T>
    static borrow(source : T&) : Argument;

    const isBound() : bool { return _slot != null; }
    const getLogicalType() : const rtti::Type? { return _logical; }
    const getBindingType() : const rtti::Type? { return _binding; }
    const getReferentType() : const rtti::Type? { return _referent; }
    const getCategory() : ArgumentCategory { return (ArgumentCategory) _category; }
    const canRead() : bool { return (_capabilities & 1u) != 0u; }
    const canMutate() : bool { return (_capabilities & 2u) != 0u; }
    const canReadSlot() : bool { return (_capabilities & 4u) != 0u; }
    const canTakeOwner() : bool { return (_capabilities & 8u) != 0u; }
    const canDrain() : bool { return (_capabilities & 16u) != 0u; }
    const isSlotWritable() : bool { return (_capabilities & 32u) != 0u; }

    /** Inspect the current binding. Unbound is an error, not bound-null.
     * Borrowed synchronous use only; this operation does not validate lifetime.
     */
    const isNull() : bool { return _isNull(); }

    /** Checked readonly referent access; T is an exact value type, not an addresser.
     * Borrowed synchronous use only. Null, wrong type and unbound accesses throw.
     */
    @annotations::Intrinsic("argument.read.v1")
    template<typename T>
    const read() : const T&;

    /** Checked non-owning, nullable referent access. Use const T to narrow access.
     * Borrowed synchronous use only; the returned pointer retains nothing.
     */
    @annotations::Intrinsic("argument.get.v1")
    template<typename T>
    const get() : T*;

    /** Load and clear the ORIGINAL writable owner slot. Null is a valid result.
     * All holder copies observe the clear and subsequent source reassignment.
     * T must match the referent; ordinary typed owner upcasts may follow the take.
     * Strong aliases of owner bindings use take<Alias>() instead.
     * Borrowed synchronous use only; capturing alone never transfers ownership.
     */
    @annotations::Intrinsic("argument.take_owner.v1")
    template<typename T>
    const takeOwner() : T!;

    /** Exact logical owner take, including strong aliases of owner types.
     * T itself must be an owner type; this never fabricates ownership.
     * Borrowed synchronous use only; the original slot is cleared.
     */
    @annotations::Intrinsic("argument.take.v1")
    template<typename T>
    const take() : T;

    /** Recover an explicitly granted drain, never ownership of its source.
     * Borrowed synchronous use only; ordinary drain construction rules still apply.
     */
    @annotations::Intrinsic("argument.drain.v1")
    template<typename T>
    const drain() : T#;
}

alias ArgumentPack : Argument[];
