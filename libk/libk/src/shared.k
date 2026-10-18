/*
 * K Language standard library — shared ownership
 *
 * Copyright 2023-2026 Emilien Kia
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
module k;

/**
 * Thrown when constructing a Shared from an empty or expired Weak.
 * Use Weak.lock() for a non-throwing promotion attempt.
 */
public final class BadWeakReferenceException : public IllegalStateException {
    public BadWeakReferenceException() : IllegalStateException() {}
}

// The common owner identity is independent of the typed pointer view held by
// each Shared/Weak handle. The erased base dispatches destruction back to the
// allocation's original typed block.
abstract class SharedControlBlock {
private:
    friend Shared;
    friend Weak;

    @ffi::Extern("C") private static __k_shared_count(count : const int*) : int;
    @ffi::Extern("C") private static __k_shared_retain(count : int*);
    @ffi::Extern("C") private static __k_shared_try_retain(count : int*) : bool;
    @ffi::Extern("C") private static __k_shared_release(count : int*) : bool;

    _strong : int;
    _weak : int;

    protected abstract destroyObject();

protected:
    SharedControlBlock() : _strong(1), _weak(1) {}

public:
    const useCount() : int {
        return __k_shared_count(&_strong);
    }

    retainStrong() {
        __k_shared_retain(&_strong);
    }

    tryRetainStrong() : bool {
        return __k_shared_try_retain(&_strong);
    }

    retainWeak() {
        __k_shared_retain(&_weak);
    }

    releaseStrong() {
        if (__k_shared_release(&_strong)) {
            // The implicit weak reference protects the block during T's
            // destructor, which may itself release explicit Weak handles.
            destroyObject();
            releaseWeak();
        }
    }

    releaseWeak() {
        if (__k_shared_release(&_weak)) {
            // Only the final releaser reconstructs the owner for deletion,
            // as in Future/Promise's reference-counted payload cleanup.
            owner : SharedControlBlock! = (SharedControlBlock!) &this;
            delete owner;
        }
    }
}

// This private helper supports imported Shared template bodies and is not part
// of the public ownership API.
template<typename T>
final class SharedBlock : public SharedControlBlock {
private:
    friend Shared<T>;
    friend Weak<T>;

    _ptr : T!;

    SharedBlock(ptr : T!) : SharedControlBlock(), _ptr(ptr) {}

protected:
    override destroyObject() {
        ptr : T! = _ptr;
        _ptr = null;
        delete ptr;
    }
}

/**
 * Shared<T> - shared ownership of a dynamically allocated class object.
 *
 * Copies retain the same object; drains transfer a handle and leave the source
 * empty. The last Shared destroys the object. Weak handles keep only the
 * control block alive and can attempt to acquire a new Shared with lock().
 *
 * Reference counting is atomic. Distinct handles may be used concurrently.
 * Modifying the same handle concurrently, or accessing T concurrently,
 * requires external synchronisation. T's destructor must not throw.
 *
 * ## Usage
 *
 * Copy a handle to retain the object independently of the original handle.
 * The declarations below belong at module scope; no explicit base-library
 * import is needed.
 *
 * ```k
 * class SharedExampleBox {
 *     public:
 *     value : int;
 *     SharedExampleBox(v : int) : value(v) {}
 * }
 *
 * sharedExample() : int {
 *     original : Shared<SharedExampleBox>(new SharedExampleBox(42));
 *     copy : Shared<SharedExampleBox>(original);
 *     moved : Shared<SharedExampleBox>(#original);
 *     moved.reset();
 *     return copy.get()->value; // 42; original is empty.
 * }
 * ```
 *
 * ## Ownership and lifetime
 *
 * - Construction from `T!`, acquire() and reset(owner) transfer the owner into
 *   the ownership group. They do not copy the managed object.
 * - get() borrows the typed view. Keep a strong handle alive for the entire
 *   use of that pointer; neither delete it nor adopt it into another owner.
 * - Weak back-references can break ownership cycles. A cycle made only of
 *   Shared handles keeps its objects alive even after outside handles go away.
 *
 * ## Pitfalls
 *
 * - A checked cross-type conversion can yield an empty destination even when
 *   the source is nonempty. Failed cross-type drains leave the source intact;
 *   drain assignment still replaces the destination with the empty result.
 * - useCount() is a snapshot, not a lock or a guarantee of exclusive access
 *   to the managed object.
 */
template<typename T>
public final struct Shared {
private:
    friend Shared;
    friend Weak;

    _block : SharedControlBlock*;
    _view : T*;

    doRelease() {
        block : SharedControlBlock* = _block;
        _block = null;
        _view = null;
        if (block != null) {
            block->releaseStrong();
        }
    }

public:
    /** Create an empty handle without allocating a control block. */
    Shared() : _block(null), _view(null) {}

    /** Acquire an owner. A null owner produces an empty handle. */
    Shared(ptr : T!) : _block(null), _view(null) {
        if (ptr != null) {
            view : T* = ptr;
            _block = new SharedBlock<T>(ptr);
            _view = view;
        }
    }

    /** Share ownership without copying T. The source may be const. */
    Shared(other : const Shared<T>&) : _block(other._block), _view(other._view) {
        if (_block != null) {
            _block->retainStrong();
        }
    }

    /** Transfer a handle without changing the reference count. */
    Shared(other : Shared<T>#) : _block(other._block), _view(other._view) {
        other._block = null;
        other._view = null;
    }

    /**
     * Share a related or polymorphic object through an adjusted typed view.
     * An unsuccessful checked cast creates an empty handle.
     */
    template<typename U>
    Shared(other : const ::k::Shared<U>&) : _block(null), _view(null) {
        source_block : SharedControlBlock* = other._block;
        if (source_block != null) {
            // Load the pointer slot before adapting it: only U qualifies
            // the pointee, not the const source handle.
            source_view : U* = other._view;
            view : T* = (T*) source_view;
            if (view != null) {
                _block = source_block;
                _view = view;
                _block->retainStrong();
            }
        }
    }

    /** Transfer a compatible handle; failed checked casts leave the source. */
    template<typename U>
    Shared(other : ::k::Shared<U>#) : _block(null), _view(null) {
        source_block : SharedControlBlock* = other._block;
        if (source_block != null) {
            view : T* = (T*) other._view;
            if (view != null) {
                _block = source_block;
                _view = view;
                other._block = null;
                other._view = null;
            }
        }
    }

    /** Promote a Weak, or throw if its object is no longer alive. */
    Shared(other : const Weak<T>&) : _block(null), _view(null)
        throws(BadWeakReferenceException) {
        block : SharedControlBlock* = other._block;
        if (block != null) {
            if (block->tryRetainStrong()) {
                _block = block;
                _view = other._view;
                return;
            }
        }
        throw BadWeakReferenceException();
    }

    /**
     * Promote a compatible weak view; mismatched live objects yield empty.
     *
     * Promotion happens before the checked cast. A live but incompatible
     * object does not throw BadWeakReferenceException.
     *
     * @throws BadWeakReferenceException If the source is empty or expired.
     */
    template<typename U>
    Shared(other : const ::k::Weak<U>&) : _block(null), _view(null)
        throws(BadWeakReferenceException) {
        promoted : ::k::Shared<U>(other);
        view : T* = (T*) promoted._view;
        if (view != null) {
            _block = promoted._block;
            _view = view;
            promoted._block = null;
            promoted._view = null;
        }
    }

    ~Shared() {
        doRelease();
    }

    /** Copy assignment. Self-assignment is safe. */
    operator =(other : const Shared<T>&) : Shared<T>& {
        share(other);
        return this;
    }

    /** Copy assignment adapts before releasing the previous ownership. */
    template<typename U>
    operator =(other : const ::k::Shared<U>&) : Shared<T>& {
        replacement : Shared<T>(other);
        block : SharedControlBlock* = replacement._block;
        view : T* = replacement._view;
        replacement._block = null;
        replacement._view = null;
        previous : SharedControlBlock* = _block;
        _block = block;
        _view = view;
        if (previous != null) {
            previous->releaseStrong();
        }
        return this;
    }

    /** Drain assignment. Self-draining leaves the handle unchanged. */
    operator =(other : Shared<T>#) : Shared<T>& {
        source : Shared<T>* = other;
        if (&this != source) {
            block : SharedControlBlock* = other._block;
            view : T* = other._view;
            other._block = null;
            other._view = null;
            previous : SharedControlBlock* = _block;
            _block = block;
            _view = view;
            if (previous != null) {
                previous->releaseStrong();
            }
        }
        return this;
    }

    /** Drain assignment leaves the source unchanged when checked adaptation fails. */
    template<typename U>
    operator =(other : ::k::Shared<U>#) : Shared<T>& {
        replacement : Shared<T>(other);
        block : SharedControlBlock* = replacement._block;
        view : T* = replacement._view;
        replacement._block = null;
        replacement._view = null;
        previous : SharedControlBlock* = _block;
        _block = block;
        _view = view;
        if (previous != null) {
            previous->releaseStrong();
        }
        return this;
    }

    /** Replace the current object by acquiring an owner, including null. */
    operator =(ptr : T!) : Shared<T>& {
        acquire(ptr);
        return this;
    }

    /** Replace the current object. Allocation failure leaves this unchanged. */
    acquire(ptr : T!) {
        replacement : Shared<T>(ptr);
        swap(replacement);
        replacement.reset();
    }

    /** Copy another handle's ownership; an empty source clears this handle. */
    share(other : const Shared<T>&) {
        block : SharedControlBlock* = other._block;
        if (_block == block) {
            _view = other._view;
            return;
        }
        if (block != null) {
            block->retainStrong();
        }
        previous : SharedControlBlock* = _block;
        _block = block;
        _view = other._view;
        if (previous != null) {
            previous->releaseStrong();
        }
    }

    /** Copy a related or polymorphic Shared view; mismatch clears this handle. */
    template<typename U>
    share(other : const ::k::Shared<U>&) {
        replacement : Shared<T>(other);
        block : SharedControlBlock* = replacement._block;
        view : T* = replacement._view;
        replacement._block = null;
        replacement._view = null;
        previous : SharedControlBlock* = _block;
        _block = block;
        _view = view;
        if (previous != null) {
            previous->releaseStrong();
        }
    }

    /**
     * Return a borrowed pointer, or null.
     *
     * Constness of the handle does not make the managed object const.
     * The pointer does not retain ownership; see the class lifetime rules.
     */
    const get() : T* {
        return _view;
    }

    /** Return true if no object is managed. */
    const isNull() : bool {
        return _block == null;
    }

    /** Return an atomic snapshot of the number of strong handles, or zero. */
    const useCount() : int {
        if (_block == null) {
            return 0;
        }
        return _block->useCount();
    }

    /** Boolean conversion: true if an object is managed. */
    const operator() : bool {
        return _block != null;
    }

    /** Logical negation: true if this handle is empty. */
    const operator !() : bool {
        return _block == null;
    }

    /** Compare the managed object addresses. */
    const operator ==(other : const Shared<T>&) : bool {
        return get() == other.get();
    }

    const operator !=(other : const Shared<T>&) : bool {
        return get() != other.get();
    }

    /** Release ownership and leave an empty handle. */
    reset() {
        doRelease();
    }

    /** Replace the current object by acquiring an owner. */
    reset(ptr : T!) {
        acquire(ptr);
    }

    /** Exchange handles without allocating or changing reference counts. */
    swap(other : Shared<T>&) {
        block : SharedControlBlock* = _block;
        _block = other._block;
        other._block = block;
        view : T* = _view;
        _view = other._view;
        other._view = view;
    }
}

/**
 * Weak<T> - non-owning observation of a Shared ownership group.
 *
 * A Weak never extends T's lifetime. lock() atomically acquires a Shared if
 * the object is still alive, or returns an empty Shared otherwise. An expired
 * group cannot be resurrected.
 *
 * Distinct handles may be used concurrently. Concurrent modification of the
 * same handle requires external synchronisation.
 *
 * ## Usage
 *
 * Keep the result of lock() alive while accessing the object. These are
 * module-scope declarations using only the implicit base library.
 *
 * ```k
 * class WeakExampleBox {
 *     public:
 *     value : int;
 *     WeakExampleBox(v : int) : value(v) {}
 * }
 *
 * weakExample() : bool {
 *     owner : Shared<WeakExampleBox>(new WeakExampleBox(7));
 *     observer : Weak<WeakExampleBox>(owner);
 *     held : Shared<WeakExampleBox> = observer.lock();
 *     owner.reset();
 *     if (!held || held.get()->value != 7) return false;
 *     held.reset();
 *     missing : Shared<WeakExampleBox> = observer.lock();
 *     return !missing && observer.expired();
 * }
 * ```
 *
 * ## Lifetime
 *
 * A Weak retains the control block, not the object. Replacing a Shared with
 * another allocation does not redirect existing Weak observers to the new
 * object. Shared construction from an empty or expired Weak throws
 * BadWeakReferenceException; lock() instead returns an empty Shared.
 *
 * ## Pitfalls
 *
 * > An expired() check cannot reserve the object's lifetime. Use lock() and
 * > test the returned Shared, rather than checking expired() before access.
 *
 * Static upcasts can preserve an expired ownership group. Dynamic conversions
 * require a live object for the checked cast and otherwise produce an empty
 * observation; a failed dynamic drain leaves its source unchanged.
 */
template<typename T>
public final struct Weak {
private:
    friend Shared;
    friend Weak;

    _block : SharedControlBlock*;
    _view : T*;

    doRelease() {
        block : SharedControlBlock* = _block;
        _block = null;
        _view = null;
        if (block != null) {
            block->releaseWeak();
        }
    }

public:
    /** Create an empty weak handle. */
    Weak() : _block(null), _view(null) {}

    /** Observe an ownership group without retaining its object. */
    Weak(other : const Shared<T>&) : _block(other._block), _view(other._view) {
        if (_block != null) {
            _block->retainWeak();
        }
    }

    /** Observe a compatible or polymorphic Shared view. */
    template<typename U>
    Weak(other : const ::k::Shared<U>&) : _block(null), _view(null) {
        source_block : SharedControlBlock* = other._block;
        if (source_block != null) {
            source_view : U* = other._view;
            view : T* = (T*) source_view;
            if (view != null) {
                _block = source_block;
                _view = view;
                _block->retainWeak();
            }
        }
    }

    /** Copy a weak handle, including an expired one. */
    Weak(other : const Weak<T>&) : _block(other._block), _view(null) {
        if (_block != null) {
            _block->retainWeak();
            if (_block->useCount() != 0) {
                _view = other._view;
            }
        }
    }

    /** Static upcasts preserve expired ownership identity without stale views. */
    template<typename U : T>
    Weak(other : const ::k::Weak<U>&) : _block(other._block), _view(null) {
        if (_block != null) {
            _block->retainWeak();
            if (_block->tryRetainStrong()) {
                source_view : U* = other._view;
                _view = (T*) source_view;
                _block->releaseStrong();
            }
        }
    }

    /** Dynamic conversion uses a temporary strong reference before RTTI access. */
    template<typename U>
    Weak(other : const ::k::Weak<U>&) : _block(null), _view(null) {
        source_block : SharedControlBlock* = other._block;
        if (source_block != null) {
            promoted : ::k::Shared<U> = other.lock();
            if (promoted) {
                view : T* = (T*) promoted._view;
                if (view != null) {
                    _block = source_block;
                    _view = view;
                    _block->retainWeak();
                }
            }
        }
    }

    /** Transfer a weak handle and leave the source empty. */
    Weak(other : Weak<T>#) : _block(other._block), _view(other._view) {
        other._block = null;
        other._view = null;
    }

    /** Transfer even an expired statically-upcast observation safely. */
    template<typename U : T>
    Weak(other : ::k::Weak<U>#) : _block(other._block), _view(null) {
        if (_block != null && _block->tryRetainStrong()) {
            source_view : U* = other._view;
            _view = (T*) source_view;
            _block->releaseStrong();
        }
        other._block = null;
        other._view = null;
    }

    /** A failed dynamic weak drain leaves its source unchanged. */
    template<typename U>
    Weak(other : ::k::Weak<U>#) : _block(null), _view(null) {
        source_block : SharedControlBlock* = other._block;
        if (source_block != null) {
            promoted : ::k::Shared<U> = other.lock();
            if (promoted) {
                view : T* = (T*) promoted._view;
                if (view != null) {
                    _block = source_block;
                    _view = view;
                    _block->retainWeak();
                    other.doRelease();
                }
            }
        }
    }

    ~Weak() {
        doRelease();
    }

    /** Copy assignment. Self-assignment is safe. */
    operator =(other : const Weak<T>&) : Weak<T>& {
        block : SharedControlBlock* = other._block;
        view : T* = null;
        if (block != null && block->tryRetainStrong()) {
            view = other._view;
            block->releaseStrong();
        }
        if (_block == block) {
            _view = view;
            return this;
        }
        if (block != null) {
            block->retainWeak();
        }
        doRelease();
        _block = block;
        _view = view;
        return this;
    }

    /** Copy-convert a weak view; expired dynamic sources produce empty. */
    template<typename U>
    operator =(other : const ::k::Weak<U>&) : Weak<T>& {
        replacement : Weak<T>(other);
        swap(replacement);
        replacement.reset();
        return this;
    }

    /** Observe a Shared, replacing any previous observation. */
    operator =(other : const Shared<T>&) : Weak<T>& {
        block : SharedControlBlock* = other._block;
        if (_block == block) {
            _view = other._view;
            return this;
        }
        if (block != null) {
            block->retainWeak();
        }
        doRelease();
        _block = block;
        _view = other._view;
        return this;
    }

    /** Observe a compatible or polymorphic Shared view. */
    template<typename U>
    operator =(other : const ::k::Shared<U>&) : Weak<T>& {
        replacement : Weak<T>(other);
        swap(replacement);
        replacement.reset();
        return this;
    }

    /** Drain assignment. Self-draining leaves the handle unchanged. */
    operator =(other : Weak<T>#) : Weak<T>& {
        source : Weak<T>* = other;
        if (&this != source) {
            replacement : Weak<T>(other);
            source_block : SharedControlBlock* = other._block;
            if (source_block == null || replacement._block != null) {
                other.doRelease();
            }
            swap(replacement);
            replacement.reset();
        }
        return this;
    }

    /** A failed dynamic weak drain leaves its source unchanged. */
    template<typename U>
    operator =(other : ::k::Weak<U>#) : Weak<T>& {
        replacement : Weak<T>(other);
        source_block : SharedControlBlock* = other._block;
        if (source_block == null || replacement._block != null) {
            other.doRelease();
        }
        swap(replacement);
        replacement.reset();
        return this;
    }

    /** Return an atomic snapshot of the number of strong handles, or zero. */
    const useCount() : int {
        if (_block == null) {
            return 0;
        }
        return _block->useCount();
    }

    /** Return true if empty or if the last strong handle has been released. */
    const expired() : bool {
        return useCount() == 0;
    }

    /**
     * Atomically acquire ownership, or return an empty Shared.
     *
     * @return A strong handle that keeps the object alive until released, or
     * an empty handle if this observation is empty or expired.
     */
    const lock() result : Shared<T> {
        block : SharedControlBlock* = _block;
        if (block != null) {
            if (block->tryRetainStrong()) {
                result._block = block;
                result._view = _view;
            }
        }
    }

    /** Release the observation and leave an empty weak handle. */
    reset() {
        doRelease();
    }

    /** Exchange weak handles without changing reference counts. */
    swap(other : Weak<T>&) {
        block : SharedControlBlock* = _block;
        _block = other._block;
        other._block = block;
        view : T* = _view;
        _view = other._view;
        other._view = view;
    }
}
