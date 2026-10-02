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

// Imported template bodies need the helper's definition in KDI. Only its type
// is exported; its entire API is private to Shared and Weak.
template<class T>
public final struct SharedBlock {
private:
    friend Shared<T>;
    friend Weak<T>;

    @ffi::Extern("C") private static __k_shared_count(count : const int*) : int;
    @ffi::Extern("C") private static __k_shared_retain(count : int*);
    @ffi::Extern("C") private static __k_shared_try_retain(count : int*) : bool;
    @ffi::Extern("C") private static __k_shared_release(count : int*) : bool;

    _ptr : T!;
    _strong : int;
    _weak : int;

    SharedBlock(ptr : T!) : _ptr(ptr), _strong(1), _weak(1) {}

    ~SharedBlock() {
        delete _ptr;
    }

    const getPtr() : T* {
        return _ptr;
    }

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
            delete _ptr;
            releaseWeak();
        }
    }

    releaseWeak() {
        if (__k_shared_release(&_weak)) {
            // Only the final releaser reconstructs the owner for deletion,
            // as in Future/Promise's reference-counted payload cleanup.
            owner : SharedBlock<T>! = (SharedBlock<T>!) &this;
            delete owner;
        }
    }
}

/**
 * Shared<T> — shared ownership of a dynamically allocated class object.
 *
 * Copies retain the same object; drains transfer a handle and leave the source
 * empty. The last Shared destroys the object. Weak handles keep only the
 * control block alive and can attempt to acquire a new Shared with lock().
 *
 * Reference counting is atomic. Distinct handles may be used concurrently.
 * Modifying the same handle concurrently, or accessing T concurrently,
 * requires external synchronisation. T's destructor must not throw.
 */
template<class T>
public final struct Shared {
private:
    friend Weak<T>;

    _block : SharedBlock<T>*;

    doRelease() {
        block : SharedBlock<T>* = _block;
        _block = null;
        if (block != null) {
            block->releaseStrong();
        }
    }

public:
    /** Create an empty handle without allocating a control block. */
    Shared() : _block(null) {}

    /** Acquire an owner. A null owner produces an empty handle. */
    Shared(ptr : T!) : _block(null) {
        if (ptr != null) {
            _block = new SharedBlock<T>(ptr);
        }
    }

    /** Share ownership without copying T. The source may be const. */
    Shared(other : const Shared<T>&) : _block(other._block) {
        if (_block != null) {
            _block->retainStrong();
        }
    }

    /** Transfer a handle without changing the reference count. */
    Shared(other : Shared<T>#) : _block(other._block) {
        other._block = null;
    }

    /** Promote a Weak, or throw if its object is no longer alive. */
    Shared(other : const Weak<T>&) : _block(null)
        throws(BadWeakReferenceException) {
        block : SharedBlock<T>* = other._block;
        if (block != null) {
            if (block->tryRetainStrong()) {
                _block = block;
                return;
            }
        }
        throw BadWeakReferenceException();
    }

    ~Shared() {
        doRelease();
    }

    /** Copy assignment. Self-assignment is safe. */
    operator =(other : const Shared<T>&) : Shared<T>& {
        share(other);
        return this;
    }

    /** Drain assignment. Self-draining leaves the handle unchanged. */
    operator =(other : Shared<T>#) : Shared<T>& {
        source : Shared<T>* = other;
        if (&this != source) {
            block : SharedBlock<T>* = other._block;
            other._block = null;
            previous : SharedBlock<T>* = _block;
            _block = block;
            if (previous != null) {
                previous->releaseStrong();
            }
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
    }

    /** Copy another handle's ownership; an empty source clears this handle. */
    share(other : const Shared<T>&) {
        block : SharedBlock<T>* = other._block;
        if (_block == block) {
            return;
        }
        if (block != null) {
            block->retainStrong();
        }
        previous : SharedBlock<T>* = _block;
        _block = block;
        if (previous != null) {
            previous->releaseStrong();
        }
    }

    /** Return a borrowed pointer, or null. Constness of the handle does not
     * make the managed object const. */
    const get() : T* {
        if (_block == null) {
            return null;
        }
        return _block->getPtr();
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
        block : SharedBlock<T>* = _block;
        _block = other._block;
        other._block = block;
    }
}

/**
 * Weak<T> — non-owning observation of a Shared ownership group.
 *
 * A Weak never extends T's lifetime. lock() atomically acquires a Shared if
 * the object is still alive, or returns an empty Shared otherwise. An expired
 * group cannot be resurrected.
 *
 * Distinct handles may be used concurrently. Concurrent modification of the
 * same handle requires external synchronisation.
 */
template<class T>
public final struct Weak {
private:
    friend Shared<T>;

    _block : SharedBlock<T>*;

    doRelease() {
        block : SharedBlock<T>* = _block;
        _block = null;
        if (block != null) {
            block->releaseWeak();
        }
    }

public:
    /** Create an empty weak handle. */
    Weak() : _block(null) {}

    /** Observe an ownership group without retaining its object. */
    Weak(other : const Shared<T>&) : _block(other._block) {
        if (_block != null) {
            _block->retainWeak();
        }
    }

    /** Copy a weak handle, including an expired one. */
    Weak(other : const Weak<T>&) : _block(other._block) {
        if (_block != null) {
            _block->retainWeak();
        }
    }

    /** Transfer a weak handle and leave the source empty. */
    Weak(other : Weak<T>#) : _block(other._block) {
        other._block = null;
    }

    ~Weak() {
        doRelease();
    }

    /** Copy assignment. Self-assignment is safe. */
    operator =(other : const Weak<T>&) : Weak<T>& {
        block : SharedBlock<T>* = other._block;
        if (_block == block) {
            return this;
        }
        if (block != null) {
            block->retainWeak();
        }
        doRelease();
        _block = block;
        return this;
    }

    /** Observe a Shared, replacing any previous observation. */
    operator =(other : const Shared<T>&) : Weak<T>& {
        block : SharedBlock<T>* = other._block;
        if (_block == block) {
            return this;
        }
        if (block != null) {
            block->retainWeak();
        }
        doRelease();
        _block = block;
        return this;
    }

    /** Drain assignment. Self-draining leaves the handle unchanged. */
    operator =(other : Weak<T>#) : Weak<T>& {
        source : Weak<T>* = other;
        if (&this != source) {
            block : SharedBlock<T>* = other._block;
            other._block = null;
            doRelease();
            _block = block;
        }
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

    /** Atomically acquire ownership, or return an empty Shared. */
    const lock() result : Shared<T> {
        block : SharedBlock<T>* = _block;
        if (block != null) {
            if (block->tryRetainStrong()) {
                result._block = block;
            }
        }
    }

    /** Release the observation and leave an empty weak handle. */
    reset() {
        doRelease();
    }

    /** Exchange weak handles without changing reference counts. */
    swap(other : Weak<T>&) {
        block : SharedBlock<T>* = _block;
        _block = other._block;
        other._block = block;
    }
}
