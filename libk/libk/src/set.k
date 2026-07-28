/*
 * K Language standard library — collections
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
 * Set<T> — abstract interface for a collection of unique elements.
 */
template<typename T>
interface Set : public Collection<T> {
    /**
     * Check if this set is a subset of another set.
     * Default implementation: every element of this set must be found in other.
     * @param other The other set to compare with.
     * @return True if this set is a subset of the other set, false otherwise.
     */
    default const isSubsetOf(other: const Set<T>&) : bool {
        it : ConstIterator<T>! = constIterator();
        cur : OptionalConstRef<T> = it.next();
        while (cur.hasValue()) {
            if (!other.contains(cur.get())) {
                return false;
            }
            cur = it.next();
        }
        return true;
    }

    /**
     * Check if this set is a superset of another set.
     * Default implementation: delegates to other.isSubsetOf(this).
     * @param other The other set to compare with.
     * @return True if this set is a superset of the other set, false otherwise.
     */
    default const isSupersetOf(other: const Set<T>&) : bool {
        return other.isSubsetOf(this);
    }
}

/**
 * MutableSet<T> — abstract interface for a mutable collection of unique elements.
 */
template<typename T>
interface MutableSet : public Set<T>, public MutableCollection<T> {
    /**
     * Add an element to the set.
     * @param element The element to add.
     * @return True if the element was added, false if it was already present.
     */
    add(element: const T&) : bool;

    /**
     * Remove an element from the set.
     * @param element The element to remove.
     * @return True if the element was removed, false if it was not present.
     */
    remove(element: const T&) : bool;
}

/**
 * OrderedSet<T> — abstract interface for a collection of unique elements that maintains order.
 */
template<typename T>
interface OrderedSet : public Set<T>, public OrderedCollection<T> {

}

/**
 * MutableOrderedSet<T> — abstract interface for a mutable collection of unique elements that maintains order.
 */
template<typename T>
interface MutableOrderedSet : public OrderedSet<T>, public MutableSet<T> {

}

/**
 * SortedSet<T> — abstract interface for a collection of unique elements that maintains sorted order.
 */
template<typename T>
interface SortedSet : public OrderedSet<T> {
}

/**
 * MutableSortedSet<T> — abstract interface for a mutable collection of unique elements that maintains sorted order.
 */
template<typename T>
interface MutableSortedSet : public MutableOrderedSet<T>, public MutableSet<T> {
}

/**
 * ListSet<T> — a concrete implementation of a mutable non-ordered set using a list.
 * Non-ordered collection of unique objects. The only guarantee is all the objects are different according to ==/!= operator.
 * Insertion and searching could be O(n).
 *
 * Internally backed by a DoubleLinkedList<T>: uniqueness is enforced by scanning the
 * backing list with the '==' operator before inserting. Iterators (forward, reverse,
 * const and mutable) are simply delegated to the backing list's own iterators.
 *
 * Usage:
 *   s : ListSet<int>;
 *   s.add(1);
 *   s.add(2);
 *   s.add(1);           // returns false, 1 is already present
 *   s.contains(2);       // true
 *   s.remove(1);
 */
template<typename T>
class ListSet : public MutableOrderedSet<T>, public MutableReversibleSequence<T> {
    private:
    _list : DoubleLinkedList<T>;

    public:
    /** Return true if the set contains no elements. */
    const isEmpty() : bool {
        return _list.isEmpty();
    }

    /** Return the number of elements in the set. */
    const size() : unsigned int {
        return _list.size();
    }

    /** Return true if the set contains an element equal to value. O(n). */
    const contains(value: const T&) : bool {
        it : ConstIterator<T>! = _list.constIterator();
        cur : OptionalConstRef<T> = it.next();
        while (cur.hasValue()) {
            if (cur.get() == value) {
                return true;
            }
            cur = it.next();
        }
        return false;
    }

    /**
     * Add a copy of element to the set if not already present. O(n).
     * @return true if the element was added, false if it was already present.
     */
    add(element: const T&) : bool {
        if (contains(element)) {
            return false;
        }
        _list.append(element);
        return true;
    }

    /**
     * Remove the element equal to value from the set. O(n).
     * @return true if the element was found and removed.
     */
    remove(value: const T&) : bool {
        i : unsigned int = 0;
        while (i < _list.size()) {
            if (_list.get(i) == value) {
                _list.removeAt(i);
                return true;
            }
            i = i + 1;
        }
        return false;
    }

    /** Remove all elements from the set. */
    clear() {
        _list.clear();
    }

    /** Return a read-only iterator over the elements of the set. */
    const constIterator() : ConstIterator<T>! {
        return _list.constIterator();
    }

    /** Return a mutable iterator over the elements of the set. */
    iterator() : Iterator<T>! {
        return _list.iterator();
    }

    /** Return a read-only reverse iterator over the elements of the set. */
    const constReverseIterator() : ConstIterator<T>! {
        return _list.constReverseIterator();
    }

    /** Return a mutable reverse iterator over the elements of the set. */
    reverseIterator() : Iterator<T>! {
        return _list.reverseIterator();
    }
}

/**
 * TreeSetNode<T> — internal node of a TreeSet<T> self-balancing binary search tree (AVL).
 *
 * Allocated dynamically via new. The contained value is stored by value inside a
 * UniSlot<T>. _left and _right are owners: each node owns its two children, forming
 * a tree of ownership rooted at TreeSet::_root. _height is the cached subtree height,
 * used to compute the AVL balance factor.
 *
 * Declared at module scope (rather than nested in TreeSet) so that the iterator
 * classes below can walk the tree structure directly with an explicit stack.
 * It is not part of the public Set<T> API surface.
 */
template<typename T>
struct TreeSetNode {
    _slot : UniSlot<T>;
    _left : TreeSetNode<T>! = null;
    _right : TreeSetNode<T>! = null;
    _height : int = 1;

    TreeSetNode() {
        _slot.construct();
    }

    ~TreeSetNode() {
        _slot.destruct();
        // _left/_right are intentionally NOT deleted here: comparing a self-referential
        // owner field to null (or deleting it) inside this struct's own method body
        // triggers a compiler limitation (internal error on the comparison's type
        // resolution). TreeSet<T>::deleteSubtree() performs the recursive free instead.
    }

    getValue() : T& {
        return _slot.get();
    }

    const getValue() : const T& {
        return _slot.get();
    }

    setValue(value : const T&) {
        _slot.get() = value;
    }
}

/**
 * TreeSet<T> — a concrete implementation of a mutable ordered set using a tree.
 * Ordered collection of objects. Guarantee objects are different according to == / != operator and are sorted according to < operator.
 * Insertion and searching could be O(log n).
 *
 * Implemented as an AVL self-balancing binary search tree, ordered by the '<' operator.
 * Every insertion/removal rebalances the tree so the height stays O(log n), which
 * bounds both search and mutation to O(log n) as documented above.
 *
 * Usage:
 *   s : TreeSet<int>;
 *   s.add(5);
 *   s.add(1);
 *   s.add(3);
 *   s.first();     // 1
 *   s.last();      // 5
 */
template<typename T>
class TreeSet : public MutableSortedSet<T>, public MutableReversibleSequence<T> {
    private:
    _root : TreeSetNode<T>! = null;
    _size : unsigned int = 0;

    public:
    /*
     * The following static helpers implement the AVL algorithm. They are internal
     * implementation details of TreeSet<T> — not part of the public Set<T> API —
     * and would normally be 'private'. They are declared 'public' to work around a
     * compiler limitation: a private static method cannot call another private
     * static method of the same class (including itself); only 'public' avoids it.
     */
    static height(node : TreeSetNode<T>*) : int {
        if (node == null) {
            return 0;
        }
        return node->_height;
    }

    static updateHeight(node : TreeSetNode<T>*) {
        lh : int = height(node->_left);
        rh : int = height(node->_right);
        if (lh > rh) {
            node->_height = lh + 1;
        } else {
            node->_height = rh + 1;
        }
    }

    static balanceFactor(node : TreeSetNode<T>*) : int {
        return height(node->_left) - height(node->_right);
    }

    /** Right rotation around y: promotes y's left child. Returns the new subtree root. */
    static rotateRight(y : TreeSetNode<T>!) : TreeSetNode<T>! {
        x : TreeSetNode<T>! = y._left;
        t2 : TreeSetNode<T>! = x._right;
        x._right = y;
        x._right._left = t2;
        updateHeight(x._right);
        updateHeight(x);
        return x;
    }

    /** Left rotation around x: promotes x's right child. Returns the new subtree root. */
    static rotateLeft(x : TreeSetNode<T>!) : TreeSetNode<T>! {
        y : TreeSetNode<T>! = x._right;
        t2 : TreeSetNode<T>! = y._left;
        y._left = x;
        y._left._right = t2;
        updateHeight(y._left);
        updateHeight(y);
        return y;
    }

    /** Recursive AVL insertion. Assumes value is not already present in the subtree. */
    static insertNode(node : TreeSetNode<T>!, value : const T&) : TreeSetNode<T>! {
        if (node == null) {
            created : TreeSetNode<T>! = new TreeSetNode<T>();
            created.setValue(value);
            return created;
        }
        if (value < node.getValue()) {
            node._left = insertNode(node._left, value);
        } else if (node.getValue() < value) {
            node._right = insertNode(node._right, value);
        } else {
            return node;
        }

        updateHeight(node);
        bf : int = balanceFactor(node);

        if (bf > 1 && value < node._left.getValue()) {
            return rotateRight(node);
        }
        if (bf < -1 && node._right.getValue() < value) {
            return rotateLeft(node);
        }
        if (bf > 1 && node._left.getValue() < value) {
            node._left = rotateLeft(node._left);
            return rotateRight(node);
        }
        if (bf < -1 && value < node._right.getValue()) {
            node._right = rotateRight(node._right);
            return rotateLeft(node);
        }
        return node;
    }

    static minNode(node : TreeSetNode<T>*) : TreeSetNode<T>* {
        cur : TreeSetNode<T>* = node;
        while (cur->_left != null) {
            cur = cur->_left;
        }
        return cur;
    }

    /** Recursive AVL deletion. Assumes value is present in the subtree. */
    static deleteNode(node : TreeSetNode<T>!, value : const T&) : TreeSetNode<T>! {
        if (node == null) {
            return node;
        }
        if (value < node.getValue()) {
            node._left = deleteNode(node._left, value);
        } else if (node.getValue() < value) {
            node._right = deleteNode(node._right, value);
        } else {
            if (node._left == null && node._right == null) {
                delete node;
                return null;
            } else if (node._left == null) {
                child : TreeSetNode<T>! = node._right;
                delete node;
                return child;
            } else if (node._right == null) {
                child : TreeSetNode<T>! = node._left;
                delete node;
                return child;
            } else {
                successor : TreeSetNode<T>* = minNode(node._right);
                succValue : T = successor->getValue();
                node.setValue(succValue);
                node._right = deleteNode(node._right, succValue);
            }
        }

        if (node == null) {
            return node;
        }

        updateHeight(node);
        bf : int = balanceFactor(node);

        if (bf > 1) {
            if (balanceFactor(node._left) >= 0) {
                return rotateRight(node);
            }
            node._left = rotateLeft(node._left);
            return rotateRight(node);
        }
        if (bf < -1) {
            if (balanceFactor(node._right) <= 0) {
                return rotateLeft(node);
            }
            node._right = rotateRight(node._right);
            return rotateLeft(node);
        }
        return node;
    }

    /**
     * Recursively free an entire subtree, one node at a time.
     * Substitutes for TreeSetNode<T>'s own destructor (see the note on
     * TreeSetNode::~TreeSetNode()): performing the deletion from this external,
     * non-self-referential context avoids the compiler limitation.
     */
    static deleteSubtree(node : TreeSetNode<T>!) {
        if (node == null) {
            return;
        }
        n : TreeSetNode<T>! = node;
        deleteSubtree(n._left);
        deleteSubtree(n._right);
        delete n;
    }

    ~TreeSet() {
        clear();
    }

    /** Return true if the set contains no elements. */
    const isEmpty() : bool {
        return _size == 0;
    }

    /** Return the number of elements in the set. */
    const size() : unsigned int {
        return _size;
    }

    /** Return true if the set contains an element equal to value. O(log n). */
    const contains(value : const T&) : bool {
        cur : TreeSetNode<T>* = _root;
        while (cur != null) {
            if (value < cur->getValue()) {
                cur = cur->_left;
            } else if (cur->getValue() < value) {
                cur = cur->_right;
            } else {
                return true;
            }
        }
        return false;
    }

    /**
     * Add a copy of element to the set if not already present. O(log n).
     * @return true if the element was added, false if it was already present.
     */
    add(element : const T&) : bool {
        if (contains(element)) {
            return false;
        }
        _root = insertNode(_root, element);
        _size = _size + 1;
        return true;
    }

    /**
     * Remove the element equal to value from the set. O(log n).
     * @return true if the element was found and removed.
     */
    remove(value : const T&) : bool {
        if (!contains(value)) {
            return false;
        }
        _root = deleteNode(_root, value);
        _size = _size - 1;
        return true;
    }

    /** Remove all elements from the set. */
    clear() {
        deleteSubtree(_root);
        _root = null;
        _size = 0;
    }

    /** Return a read-only reference to the smallest element, or empty if the set is empty. */
    first() : OptionalConstRef<T> {
        result : OptionalConstRef<T>;
        if (_root == null) {
            return result;
        }
        node : TreeSetNode<T>* = minNode(_root);
        result.set(node->getValue());
        return result;
    }

    /** Return a read-only reference to the largest element, or empty if the set is empty. */
    last() : OptionalConstRef<T> {
        result : OptionalConstRef<T>;
        if (_root == null) {
            return result;
        }
        cur : TreeSetNode<T>* = _root;
        while (cur->_right != null) {
            cur = cur->_right;
        }
        result.set(cur->getValue());
        return result;
    }

    /** Return a read-only ascending iterator over the elements of the set. */
    const constIterator() : ConstIterator<T>! {
        return new TreeSetConstIterator<T>(_root);
    }

    /** Return a mutable ascending iterator over the elements of the set. */
    iterator() : Iterator<T>! {
        return new TreeSetIterator<T>(_root);
    }

    /** Return a read-only descending iterator over the elements of the set. */
    const constReverseIterator() : ConstIterator<T>! {
        return new TreeSetReverseConstIterator<T>(_root);
    }

    /** Return a mutable descending iterator over the elements of the set. */
    reverseIterator() : Iterator<T>! {
        return new TreeSetReverseIterator<T>(_root);
    }
}

/**
 * TreeSetConstIterator<T> — a read-only ascending (in-order) iterator over a TreeSet<T>.
 *
 * Implements ConstIterator<T>: next() returns OptionalConstRef<T>.
 * Uses an explicit stack of the left spine to perform an iterative in-order
 * traversal in O(1) amortized time per step and O(log n) auxiliary space.
 */
template<typename T>
class TreeSetConstIterator : public ConstIterator<T> {
private:
    _stack : Vector<TreeSetNode<T>*>;

    pushLeftSpine(node : TreeSetNode<T>*) {
        cur : TreeSetNode<T>* = node;
        while (cur != null) {
            _stack.append(cur);
            cur = cur->_left;
        }
    }

public:
    TreeSetConstIterator(root : TreeSetNode<T>*) {
        pushLeftSpine(root);
    }

    next() : OptionalConstRef<T> {
        result : OptionalConstRef<T>;
        if (_stack.isEmpty()) {
            return result;
        }
        node : TreeSetNode<T>* = _stack.last();
        _stack.removeLast();
        result.set(node->getValue());
        pushLeftSpine(node->_right);
        return result;
    }
}

/**
 * TreeSetIterator<T> — a mutable ascending (in-order) iterator over a TreeSet<T>.
 *
 * Implements Iterator<T> (extends ConstIterator<T>): next() returns OptionalRef<T>.
 *
 * Caution: mutating the returned value in place can break the binary search tree
 * ordering invariant. Prefer remove()/add() to change an element's value.
 */
template<typename T>
class TreeSetIterator : public Iterator<T> {
private:
    _stack : Vector<TreeSetNode<T>*>;

    pushLeftSpine(node : TreeSetNode<T>*) {
        cur : TreeSetNode<T>* = node;
        while (cur != null) {
            _stack.append(cur);
            cur = cur->_left;
        }
    }

public:
    TreeSetIterator(root : TreeSetNode<T>*) {
        pushLeftSpine(root);
    }

    next() : OptionalRef<T> {
        result : OptionalRef<T>;
        if (_stack.isEmpty()) {
            return result;
        }
        node : TreeSetNode<T>* = _stack.last();
        _stack.removeLast();
        result.set(node->getValue());
        pushLeftSpine(node->_right);
        return result;
    }
}

/**
 * TreeSetReverseConstIterator<T> — a read-only descending iterator over a TreeSet<T>.
 *
 * Implements ConstIterator<T>: next() returns OptionalConstRef<T>.
 * Mirrors TreeSetConstIterator, walking the right spine instead of the left one.
 */
template<typename T>
class TreeSetReverseConstIterator : public ConstIterator<T> {
private:
    _stack : Vector<TreeSetNode<T>*>;

    pushRightSpine(node : TreeSetNode<T>*) {
        cur : TreeSetNode<T>* = node;
        while (cur != null) {
            _stack.append(cur);
            cur = cur->_right;
        }
    }

public:
    TreeSetReverseConstIterator(root : TreeSetNode<T>*) {
        pushRightSpine(root);
    }

    next() : OptionalConstRef<T> {
        result : OptionalConstRef<T>;
        if (_stack.isEmpty()) {
            return result;
        }
        node : TreeSetNode<T>* = _stack.last();
        _stack.removeLast();
        result.set(node->getValue());
        pushRightSpine(node->_left);
        return result;
    }
}

/**
 * TreeSetReverseIterator<T> — a mutable descending iterator over a TreeSet<T>.
 *
 * Implements Iterator<T> (extends ConstIterator<T>): next() returns OptionalRef<T>.
 *
 * Caution: mutating the returned value in place can break the binary search tree
 * ordering invariant. Prefer remove()/add() to change an element's value.
 */
template<typename T>
class TreeSetReverseIterator : public Iterator<T> {
private:
    _stack : Vector<TreeSetNode<T>*>;

    pushRightSpine(node : TreeSetNode<T>*) {
        cur : TreeSetNode<T>* = node;
        while (cur != null) {
            _stack.append(cur);
            cur = cur->_right;
        }
    }

public:
    TreeSetReverseIterator(root : TreeSetNode<T>*) {
        pushRightSpine(root);
    }

    next() : OptionalRef<T> {
        result : OptionalRef<T>;
        if (_stack.isEmpty()) {
            return result;
        }
        node : TreeSetNode<T>* = _stack.last();
        _stack.removeLast();
        result.set(node->getValue());
        pushRightSpine(node->_left);
        return result;
    }
}

/**
 * HashSetNode<T> — internal singly-linked chain node for HashSet<T>.
 *
 * Allocated dynamically via new. The contained value is stored by value inside a
 * UniSlot<T>. _next is an owner of the next node in the same bucket's collision chain.
 *
 * Declared at module scope (rather than nested in HashSet) so that HashSetChain<T>
 * and the iterator classes below can walk the chain structure directly.
 * It is not part of the public Set<T> API surface.
 */
template<typename T>
struct HashSetNode {
    _slot : UniSlot<T>;
    _next : HashSetNode<T>! = null;

    HashSetNode() {
        _slot.construct();
    }

    ~HashSetNode() {
        _slot.destruct();
        // _next is intentionally NOT deleted here: HashSetChain<T> frees the whole
        // chain iteratively (see clearChain()) to avoid deep recursion on long chains.
    }

    getValue() : T& {
        return _slot.get();
    }

    const getValue() : const T& {
        return _slot.get();
    }

    setValue(value : const T&) {
        _slot.get() = value;
    }
}

/**
 * HashSetChain<T> — head of a singly-linked collision chain, one per hash bucket.
 *
 * Allocated dynamically via new (one instance per bucket). HashSet<T> keeps every
 * live HashSetChain<T> reachable from two independent structures:
 *   - _nextChain links every chain into a single intrusive owned list rooted at
 *     HashSet::_chainsHead, used solely to guarantee every chain is freed exactly
 *     once (see HashSet::freeChainList()). Comparing/deleting this self-referential
 *     field from within HashSetChain's own destructor hits the same compiler
 *     limitation documented on TreeSetNode::~TreeSetNode(), so it is NOT touched here.
 *   - HashSet::_buckets holds a non-owning raw pointer to each chain, indexed by
 *     bucket number, for O(1) lookup. (Vector<HashSetChain<T>> storing the chains
 *     BY VALUE is not usable here: instantiating Vector<T> with a self-referential
 *     template struct element type triggers an unrelated compiler limitation deep
 *     inside Vector<T>'s own generated methods, so buckets must be raw pointers to
 *     individually heap-allocated chains instead.)
 */
template<typename T>
struct HashSetChain {
    _head : HashSetNode<T>! = null;
    _nextChain : HashSetChain<T>! = null;

    ~HashSetChain() {
        clearChain();
        // _nextChain is intentionally NOT deleted here (see the class comment above).
        // HashSet<T>::freeChainList() frees the intrusive chain-of-chains list instead.
    }

    /** Remove and free every node of this chain. */
    clearChain() {
        while (_head != null) {
            next : HashSetNode<T>! = _head._next;
            _head = next;
        }
    }
}

/**
 * HashSet<T> — a concrete implementation of a mutable set using a hash table.
 * Non-ordered collection of unique objects. Objects are guarantee different according to their const T::hash(...) method result.
 *
 * Implemented with separate chaining: an array of buckets (Vector<HashSetChain<T>*>,
 * one raw pointer per bucket to an individually heap-allocated HashSetChain<T>), each
 * holding a singly-linked chain of colliding elements. The bucket array grows
 * (doubling) and every element is rehashed whenever the load factor would exceed 0.75.
 *
 * T must provide a `const hash() : int` method (inherited from Object, or overridden)
 * and support the '==' operator, used to resolve collisions within a bucket.
 *
 * Usage:
 *   s : HashSet<int>;
 *   s.add(1);
 *   s.add(2);
 *   s.contains(1);   // true
 *   s.remove(2);
 */
template<typename T>
class HashSet : public MutableSet<T>, public MutableReversibleSequence<T> {
    private:
    static DEFAULT_CAPACITY : unsigned int = 16;

    _buckets : Vector<HashSetChain<T>*>;
    _chainsHead : HashSetChain<T>! = null;
    _size : unsigned int = 0;
    _capacity : unsigned int = 0;

    public:
    /*
     * computeIndex() and freeChainList() are internal implementation details of
     * HashSet<T> — not part of the public Set<T> API — and would normally be
     * 'private'. They are declared 'public' to work around a compiler limitation:
     * a private static method cannot call another private static method of the
     * same class; only 'public' avoids it.
     */
    static computeIndex(value : const T&, capacity : unsigned int) : unsigned int {
        h : int = value.hash();
        u : unsigned int = (unsigned int) h;
        return u % capacity;
    }

    /** Free every HashSetChain<T> reachable from the intrusive chains list. */
    static freeChainList(head : HashSetChain<T>!) {
        cur : HashSetChain<T>! = head;
        while (cur != null) {
            next : HashSetChain<T>! = cur._nextChain;
            cur._nextChain = null;
            delete cur;
            cur = next;
        }
    }

    private:
    /** Allocate 'capacity' empty chains, linking them into _chainsHead and _buckets. */
    buildBuckets(capacity : unsigned int) {
        _buckets.reserve(capacity);
        i : unsigned int = 0;
        while (i < capacity) {
            created : HashSetChain<T>! = new HashSetChain<T>();
            raw : HashSetChain<T>* = created;
            created._nextChain = _chainsHead;
            _chainsHead = created;
            _buckets.append(raw);
            i = i + 1;
        }
    }

    /** Grow to newCapacity buckets and redistribute every existing element. */
    rehash(newCapacity : unsigned int) {
        newBuckets : Vector<HashSetChain<T>*>;
        newChainsHead : HashSetChain<T>! = null;
        newBuckets.reserve(newCapacity);
        i : unsigned int = 0;
        while (i < newCapacity) {
            created : HashSetChain<T>! = new HashSetChain<T>();
            raw : HashSetChain<T>* = created;
            created._nextChain = newChainsHead;
            newChainsHead = created;
            newBuckets.append(raw);
            i = i + 1;
        }

        bi : unsigned int = 0;
        while (bi < _buckets.size()) {
            oldChain : HashSetChain<T>* = _buckets.get(bi);
            node : HashSetNode<T>! = oldChain->_head;
            oldChain->_head = null;
            while (node != null) {
                next : HashSetNode<T>! = node._next;
                node._next = null;
                newIdx : unsigned int = computeIndex(node.getValue(), newCapacity);
                newChain : HashSetChain<T>* = newBuckets.get(newIdx);
                node._next = newChain->_head;
                newChain->_head = node;
                node = next;
            }
            bi = bi + 1;
        }

        // Old chains are now all empty (nodes moved out above): free the wrappers.
        freeChainList(_chainsHead);
        _buckets.clear();
        k : unsigned int = 0;
        while (k < newBuckets.size()) {
            _buckets.append(newBuckets.get(k));
            k = k + 1;
        }
        _chainsHead = newChainsHead;
        _capacity = newCapacity;
    }

    public:
    HashSet() {
        cap : unsigned int = DEFAULT_CAPACITY;
        _capacity = cap;
        buildBuckets(cap);
    }

    ~HashSet() {
        freeChainList(_chainsHead);
        _chainsHead = null;
    }

    /** Return true if the set contains no elements. */
    const isEmpty() : bool {
        return _size == 0;
    }

    /** Return the number of elements in the set. */
    const size() : unsigned int {
        return _size;
    }

    /** Return true if the set contains an element equal to value. Average O(1). */
    const contains(value : const T&) : bool {
        cap : unsigned int = _capacity;
        idx : unsigned int = computeIndex(value, cap);
        chain : HashSetChain<T>* = _buckets.get(idx);
        node : const HashSetNode<T>* = chain->_head;
        while (node != null) {
            if (node->getValue() == value) {
                return true;
            }
            node = node->_next;
        }
        return false;
    }

    /**
     * Add a copy of element to the set if not already present. Average O(1).
     * May trigger a rehash (grow and redistribute all elements) if the load
     * factor would exceed 0.75.
     * @return true if the element was added, false if it was already present.
     */
    add(element : const T&) : bool {
        if (contains(element)) {
            return false;
        }
        if ((_size + 1) * 4 > _capacity * 3) {
            rehash(_capacity * 2);
        }
        cap : unsigned int = _capacity;
        idx : unsigned int = computeIndex(element, cap);
        chain : HashSetChain<T>* = _buckets.get(idx);
        node : HashSetNode<T>! = new HashSetNode<T>();
        node.setValue(element);
        node._next = chain->_head;
        chain->_head = node;
        _size = _size + 1;
        return true;
    }

    /**
     * Remove the element equal to value from the set. Average O(1).
     * @return true if the element was found and removed.
     */
    remove(value : const T&) : bool {
        cap : unsigned int = _capacity;
        idx : unsigned int = computeIndex(value, cap);
        chain : HashSetChain<T>* = _buckets.get(idx);
        if (chain->_head == null) {
            return false;
        }
        if (chain->_head.getValue() == value) {
            next : HashSetNode<T>! = chain->_head._next;
            chain->_head = next;
            _size = _size - 1;
            return true;
        }
        prev : HashSetNode<T>* = chain->_head;
        while (prev->_next != null) {
            if (prev->_next.getValue() == value) {
                matched : HashSetNode<T>! = prev->_next;
                afterMatched : HashSetNode<T>! = matched._next;
                prev->_next = afterMatched;
                _size = _size - 1;
                return true;
            }
            prev = prev->_next;
        }
        return false;
    }

    /** Remove all elements from the set. Bucket capacity is left unchanged. */
    clear() {
        i : unsigned int = 0;
        while (i < _buckets.size()) {
            b : HashSetChain<T>* = _buckets.get(i);
            b->clearChain();
            i = i + 1;
        }
        _size = 0;
    }

    /** Return a read-only iterator over the elements of the set. Order is unspecified. */
    const constIterator() : ConstIterator<T>! {
        return new HashSetConstIterator<T>(_buckets);
    }

    /** Return a mutable iterator over the elements of the set. Order is unspecified. */
    iterator() : Iterator<T>! {
        return new HashSetIterator<T>(_buckets);
    }

    /** Return a read-only reverse iterator over the elements of the set. Order is unspecified. */
    const constReverseIterator() : ConstIterator<T>! {
        return new HashSetReverseConstIterator<T>(_buckets);
    }

    /** Return a mutable reverse iterator over the elements of the set. Order is unspecified. */
    reverseIterator() : Iterator<T>! {
        return new HashSetReverseIterator<T>(_buckets);
    }
}

/**
 * HashSetConstIterator<T> — a read-only iterator over a HashSet<T>.
 *
 * Implements ConstIterator<T>: next() returns OptionalConstRef<T>.
 * Walks the bucket array from first to last; within each bucket, walks the
 * collision chain from head to tail. Iteration order is unspecified and
 * unrelated to insertion order.
 */
template<typename T>
class HashSetConstIterator : public ConstIterator<T> {
private:
    _buckets : const Vector<HashSetChain<T>*>*;
    _bucketIndex : unsigned int;
    _current : const HashSetNode<T>*;

    advance() {
        while (_current == null && _bucketIndex < _buckets->size()) {
            idx : unsigned int = _bucketIndex;
            chain : HashSetChain<T>* = _buckets->get(idx);
            _current = chain->_head;
            _bucketIndex = _bucketIndex + 1;
        }
    }

public:
    HashSetConstIterator(buckets : const Vector<HashSetChain<T>*>&) {
        _buckets = &buckets;
        _bucketIndex = 0;
        _current = null;
        advance();
    }

    next() : OptionalConstRef<T> {
        result : OptionalConstRef<T>;
        if (_current == null) {
            return result;
        }
        result.set(_current->getValue());
        _current = _current->_next;
        advance();
        return result;
    }
}

/**
 * HashSetIterator<T> — a mutable iterator over a HashSet<T>.
 *
 * Implements Iterator<T> (extends ConstIterator<T>): next() returns OptionalRef<T>.
 *
 * Caution: mutating the returned value in place changes its hash bucket only if
 * the set is later rehashed; contains()/remove() may fail to find a mutated
 * element until the set is rebuilt. Prefer remove()/add() to change a value.
 */
template<typename T>
class HashSetIterator : public Iterator<T> {
private:
    _buckets : Vector<HashSetChain<T>*>*;
    _bucketIndex : unsigned int;
    _current : HashSetNode<T>*;

    advance() {
        while (_current == null && _bucketIndex < _buckets->size()) {
            idx : unsigned int = _bucketIndex;
            chain : HashSetChain<T>* = _buckets->get(idx);
            _current = chain->_head;
            _bucketIndex = _bucketIndex + 1;
        }
    }

public:
    HashSetIterator(buckets : Vector<HashSetChain<T>*>&) {
        _buckets = &buckets;
        _bucketIndex = 0;
        _current = null;
        advance();
    }

    next() : OptionalRef<T> {
        result : OptionalRef<T>;
        if (_current == null) {
            return result;
        }
        result.set(_current->getValue());
        _current = _current->_next;
        advance();
        return result;
    }
}

/**
 * HashSetReverseConstIterator<T> — a read-only reverse iterator over a HashSet<T>.
 *
 * Implements ConstIterator<T>: next() returns OptionalConstRef<T>.
 * Walks the bucket array from last to first; within each bucket, walks the
 * collision chain from head to tail. Iteration order is unspecified.
 */
template<typename T>
class HashSetReverseConstIterator : public ConstIterator<T> {
private:
    _buckets : const Vector<HashSetChain<T>*>*;
    _hasBucket : bool;
    _bucketIndex : unsigned int;
    _current : const HashSetNode<T>*;

    advance() {
        while (_current == null && _hasBucket) {
            idx : unsigned int = _bucketIndex;
            chain : HashSetChain<T>* = _buckets->get(idx);
            _current = chain->_head;
            if (_bucketIndex == 0) {
                _hasBucket = false;
            } else {
                _bucketIndex = _bucketIndex - 1;
            }
        }
    }

public:
    HashSetReverseConstIterator(buckets : const Vector<HashSetChain<T>*>&) {
        _buckets = &buckets;
        _current = null;
        if (buckets.size() == 0) {
            _hasBucket = false;
            _bucketIndex = 0;
        } else {
            _hasBucket = true;
            _bucketIndex = buckets.size() - 1;
        }
        advance();
    }

    next() : OptionalConstRef<T> {
        result : OptionalConstRef<T>;
        if (_current == null) {
            return result;
        }
        result.set(_current->getValue());
        _current = _current->_next;
        advance();
        return result;
    }
}

/**
 * HashSetReverseIterator<T> — a mutable reverse iterator over a HashSet<T>.
 *
 * Implements Iterator<T> (extends ConstIterator<T>): next() returns OptionalRef<T>.
 * See HashSetIterator for the caveat about mutating values in place.
 */
template<typename T>
class HashSetReverseIterator : public Iterator<T> {
private:
    _buckets : Vector<HashSetChain<T>*>*;
    _hasBucket : bool;
    _bucketIndex : unsigned int;
    _current : HashSetNode<T>*;

    advance() {
        while (_current == null && _hasBucket) {
            idx : unsigned int = _bucketIndex;
            chain : HashSetChain<T>* = _buckets->get(idx);
            _current = chain->_head;
            if (_bucketIndex == 0) {
                _hasBucket = false;
            } else {
                _bucketIndex = _bucketIndex - 1;
            }
        }
    }

public:
    HashSetReverseIterator(buckets : Vector<HashSetChain<T>*>&) {
        _buckets = &buckets;
        _current = null;
        if (buckets.size() == 0) {
            _hasBucket = false;
            _bucketIndex = 0;
        } else {
            _hasBucket = true;
            _bucketIndex = buckets.size() - 1;
        }
        advance();
    }

    next() : OptionalRef<T> {
        result : OptionalRef<T>;
        if (_current == null) {
            return result;
        }
        result.set(_current->getValue());
        _current = _current->_next;
        advance();
        return result;
    }
}
}