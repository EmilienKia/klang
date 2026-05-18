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
 * Shared<T> — reference-counted shared ownership smart pointer.
 *
 * Multiple Shared<T> instances can share ownership of a single
 * dynamically-allocated T object. The managed object is destroyed
 * when the last Shared<T> referencing it is destroyed or reset.
 *
 * Implementation uses an intrusive doubly-linked list of ChainNodes,
 * each embedded in a Shared<T>. The Block (holding the owned T! and
 * a reference count) is owned by exactly one ChainNode at a time.
 * When the block holder is released, it transfers Block ownership to
 * a neighbor in the chain. When the refcount reaches zero, the Block
 * (and thus T) is deleted.
 *
 * Usage:
 *   obj : MyClass! = new MyClass(42);
 *   s1 : Shared<MyClass>;
 *   s1.acquire(obj);            // s1 takes ownership, obj becomes null
 *
 *   s2 : Shared<MyClass>;
 *   s2.share(s1);               // s2 shares with s1, useCount = 2
 *
 *   ref : MyClass* = s1.get();  // access the managed object
 *   s1.reset();                 // s1 releases, s2 is now sole owner
 *
 * Thread safety: NONE. This implementation is not thread-safe.
 * A single Shared<T> must not be accessed from multiple threads
 * without external synchronisation.
 *
 * Note: This is a template struct — T must be a class type.
 * Each instantiation produces specialised code for the concrete T,
 * which allows calling T's destructor directly.
 */
template<class T>
final struct Shared {
    private:
    /**
     * Block — internal control block for Shared<T>.
     *
     * Holds the owned pointer to the managed object and a reference
     * count. Allocated once per sharing group via `new Block()`.
     * The Block is owned by exactly one ChainNode; ownership is
     * transferred between nodes as Shared instances are created and
     * destroyed.
     */
    final static struct Block {
        _ptr : T!;
        _refCount : int;
        Block() {
            _ptr = null;
            _refCount = 0;
        }
        /**
         * Explicit destructor to ensure the owned T is properly destroyed.
         * Required because the compiler does not yet auto-delete owner fields
         * in nested template struct destructors.
         */
        ~Block() {
            delete _ptr;
        }
        /** Move the given owner into this block. */
        setPtr(ptr : T!) {
            _ptr = ptr;
        }
        /** Return an observer pointer to the managed object. */
        const getPtr() : T* {
            return _ptr;
        }
        /** Increment the reference count. */
        retain() {
            _refCount = _refCount + 1;
        }
        /** Decrement the reference count. Returns the new count. */
        release() : int {
            _refCount = _refCount - 1;
            return _refCount;
        }
    }
    /**
     * ChainNode — intrusive doubly-linked list node.
     *
     * Each Shared<T> embeds one ChainNode. The chain links all
     * Shared<T> instances sharing the same managed object. Exactly
     * one node in the chain holds the Block! owner.
     */
    final static struct ChainNode {
        _block : Block!;
        _prev  : ChainNode*;
        _next  : ChainNode*;
        ChainNode() {
            _block = null;
            _prev = null;
            _next = null;
        }
    }
    _node : ChainNode;
    _obs  : T*;
    public:
    /**
     * Create an empty (null) shared pointer.
     * No object is managed; get() returns null.
     */
    Shared() {
        _obs = null;
    }
    /**
     * Create a shared pointer by acquiring ownership of a
     * dynamically-allocated object. The owner is drained
     * (ptr becomes null after this call).
     */
    Shared(ptr : T!) {
        _obs = ptr;
        _node._block = new Block();
        _node._block.setPtr(ptr);
        _node._block.retain();
    }
    /**
     * Destructor. Removes this node from the sharing chain.
     * If this was the last reference, destroys the managed object.
     */
    ~Shared() {
        doRelease();
    }
    /**
     * Acquire ownership of a dynamically-allocated object.
     * The owner is drained (ptr becomes null after this call).
     * If this Shared already manages an object, it is released first.
     */
    acquire(ptr : T!) {
        doRelease();
        _obs = ptr;
        _node._block = new Block();
        _node._block.setPtr(ptr);
        _node._block.retain();
    }
    /**
     * Share ownership with another Shared instance.
     * This Shared joins the sharing group of 'other'.
     * If this Shared already manages an object, it is released first.
     * If 'other' is empty, this Shared remains empty.
     */
    share(other : Shared<T>&) {
        if (other._obs == null) { return; }
        doRelease();
        _obs = other._obs;
        _node._block = null;
        // Insert self after other's node in the chain
        _node._prev = &other._node;
        _node._next = other._node._next;
        if (_node._next != null) {
            _node._next->_prev = &_node;
        }
        other._node._next = &_node;
        // Retain: find the block in the chain and increment refcount
        p : ChainNode* = _node._prev;
        while (p != null) {
            if (p->_block != null) { p->_block.retain(); break; }
            p = p->_prev;
        }
        if (p == null) {
            n : ChainNode* = _node._next;
            while (n != null) {
                if (n->_block != null) { n->_block.retain(); break; }
                n = n->_next;
            }
        }
    }
    /**
     * Return an observer pointer to the managed object.
     * Returns null if this Shared is empty.
     */
    get() : T* {
        return _obs;
    }
    /**
     * Return true if this Shared is empty (manages no object).
     */
    isNull() : bool {
        return _obs == null;
    }
    /**
     * Return the number of Shared instances sharing this object.
     * Returns 0 if empty.
     * Complexity: O(n) for chain traversal to find the block.
     */
    useCount() : int {
        if (_obs == null) { return 0; }
        if (_node._block != null) { return _node._block._refCount; }
        p : ChainNode* = _node._prev;
        while (p != null) {
            if (p->_block != null) { return p->_block._refCount; }
            p = p->_prev;
        }
        n : ChainNode* = _node._next;
        while (n != null) {
            if (n->_block != null) { return n->_block._refCount; }
            n = n->_next;
        }
        return 0;
    }
    /**
     * Release this Shared's participation in the sharing group.
     * After this call, this Shared is empty (get() returns null).
     * If this was the last Shared in the group, the managed object
     * is destroyed.
     */
    reset() {
        doRelease();
    }
    /**
     * Release current managed object (if any) and acquire a new one.
     */
    reset(ptr : T!) {
        doRelease();
        _obs = ptr;
        _node._block = new Block();
        _node._block.setPtr(ptr);
        _node._block.retain();
    }
    private:
    /**
     * Internal: unlink self from the chain, decrement refcount,
     * and handle block ownership transfer or deletion.
     */
    doRelease() {
        if (_obs == null) { return; }
        // Unlink self from the doubly-linked chain
        if (_node._prev != null) {
            _node._prev->_next = _node._next;
        }
        if (_node._next != null) {
            _node._next->_prev = _node._prev;
        }
        // Handle block ownership and refcount
        if (_node._block != null) {
            // We hold the block — decrement and decide
            rc : int = _node._block.release();
            if (rc == 0) {
                // Last reference — delete block (auto-deletes T)
                delete _node._block;
            } else {
                // Transfer block ownership to a neighbor
                if (_node._next != null) {
                    _node._next->_block = _node._block;
                } else if (_node._prev != null) {
                    _node._prev->_block = _node._block;
                }
            }
        } else {
            // We don't hold the block — find it in the chain and decrement
            p : ChainNode* = _node._prev;
            while (p != null) {
                if (p->_block != null) {
                    rc : int = p->_block.release();
                    if (rc == 0) { delete p->_block; }
                    break;
                }
                p = p->_prev;
            }
            if (p == null) {
                n : ChainNode* = _node._next;
                while (n != null) {
                    if (n->_block != null) {
                        rc : int = n->_block.release();
                        if (rc == 0) { delete n->_block; }
                        break;
                    }
                    n = n->_next;
                }
            }
        }
        // Clear to empty state
        _obs = null;
        _node._prev = null;
        _node._next = null;
    }
}
