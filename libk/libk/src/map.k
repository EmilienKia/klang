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
 * Entry<K,V> — abstract interface for a single read-only key/value association.
 *
 * Returned (as OptionalConstRef<Entry<K,V>>/via ConstIterator<Entry<K,V>>) by read-only
 * map traversal and lookup. Concrete map implementations back this interface with a
 * node that stores both the key and the value.
 */
template<typename K, typename V>
interface Entry {
  const key() : const K&;
  const value() : const V&;
}

/**
 * MutableEntry<K,V> — abstract interface for a key/value association whose value
 * can be updated in place.
 *
 * Only the value can be changed: the key is immutable once an entry has been
 * created, so structural invariants of ordered/hashed maps (which index by key)
 * are never at risk of being broken through this interface.
 */
template<typename K, typename V>
interface MutableEntry : public Entry<K,V> {
  value(val: const V&);
}

/**
 * Map<K,V> — abstract common read-only interface for all maps (dictionaries).
 *
 * A Map<K,V> associates at most one value with each key. It is not a Collection<T>:
 * unlike sets/lists, membership testing is split between containsKey (indexed,
 * typically fast) and containsValue (always a linear scan of the values).
 */
template<typename K, typename V>
interface Map : public Sequence<Entry<K,V>>, public Sized {
  /**
   * Return a read-only reference to the value associated with key, or empty if
   * key is not present.
   */
  const get(key: const K&) : OptionalConstRef<V>;

  /** Return true if some entry has a key equal to key. */
  const containsKey(key: const K&) : bool;

  /** Return true if some entry has a value equal to value. Always O(n): scans every entry. */
  const containsValue(value: const V&) : bool;

  // TODO Add:
  /*
  const keys() : SetView<K>;
  const values() : View<V>;
  const entries() : EntryView<K,V>;
  */
}

/** OrderedMap<K,V> — a Map<K,V> with a well-defined (but unspecified) iteration order. */
template<typename K, typename V>
interface OrderedMap : public Map<K,V> {
  /** Read-only reference to the first entry in iteration order, or empty if the map is empty. */
  const first() : OptionalConstRef<Entry<K,V>>;
  /** Read-only reference to the last entry in iteration order, or empty if the map is empty. */
  const last() : OptionalConstRef<Entry<K,V>>;
}

/** SortedMap<K,V> — an OrderedMap<K,V> whose order is a genuine sort order (by K's `<`). */
template<typename K, typename V>
interface SortedMap : public OrderedMap<K,V> {
  // TODO Add:
  /*
  const range(from: const K&, to: const K&) : EntryView<K,V>
  */
}


/**
 * MutableMap<K,V> — abstract interface for a mutable map.
 *
 * Adds insertion, removal, in-place value mutation (mutable get()/iterator()) and
 * the value-returning replace() on top of the read-only Map<K,V> contract.
 */
template<typename K, typename V>
interface MutableMap : public Map<K,V> {
  /** Mutable reference to the value associated with key, or empty if key is not present. */
  get(key: const K&) : OptionalRef<V>;

  /**
   * Associate value with key, overwriting any previous association. Returns nothing:
   * use replace() if the previous value is needed.
   */
  put(key: const K&, value: const V&);

  /**
   * Associate value with key only if key is not already present.
   * @return A reference to the existing value if key was already present (map
   * unchanged), or empty if key was absent (value was inserted).
   */
  putIfAbsent(key: const K&, value: const V&) : OptionalRef<V>;

  /**
   * Associate value with key and return the previous value, if any.
   * @param insertIfAbsent If true (default), key is inserted even if it was absent.
   * If false, the map is left unchanged when key is absent.
   * @return A copy of the previous value associated with key, or empty if key had
   * no previous value (whether or not it was just inserted).
   */
  replace(key: const K&, value: const V&, insertIfAbsent: bool = true) : Optional<V>;

  /** Remove the entry for key. @return true if key was found and removed. */
  remove(key: const K&) : bool;

  /** Remove every entry from the map. */
  clear();

  /** Mutable iterator over the entries: value() can be updated in place, key() cannot. */
  iterator() : Iterator<MutableEntry<K,V>>!;

  // TODO Add:
  /*
  mutableValues() : MutableView<V>;
  mutableEntries() : MutableEntryView<K,V>;
  */
}


/** MutableOrderedMap<K,V> — combines MutableMap<K,V> mutation with OrderedMap<K,V> ordering. */
template<typename K, typename V>
interface MutableOrderedMap : public OrderedMap<K,V>, public MutableMap<K,V>  {
  /** Mutable reference to the first entry in iteration order, or empty if the map is empty. */
  mutableFirst() : OptionalRef<MutableEntry<K,V>>;
  /** Mutable reference to the last entry in iteration order, or empty if the map is empty. */
  mutableLast() : OptionalRef<MutableEntry<K,V>>;
}


/** MutableSortedMap<K,V> — combines MutableMap<K,V> mutation with SortedMap<K,V> ordering. */
template<typename K, typename V>
interface MutableSortedMap : public SortedMap<K,V>, public MutableOrderedMap<K,V>  {
  // TODO Add:
  /*
  range(from: const K&, to: const K&) : MutableEntryView<K,V>
  */
}





/**
 * MapEntry<K,V> — concrete key/value pair storage implementing MutableEntry<K,V>.
 *
 * Shared by every concrete Map<K,V> implementation (ListMap, TreeMap, HashMap) as the
 * embedded per-node entry: the key and the value are each stored by value in their own
 * UniSlot. Only key()/value()/value(val) (declared by Entry<K,V>/MutableEntry<K,V>) are
 * part of the public map API surface; getValue()/setKey() are internal helpers used by
 * the concrete map implementations (which hold a typed MapEntry<K,V>, not just an
 * Entry<K,V>) to build and relocate entries.
 */
template<typename K, typename V>
class MapEntry : public MutableEntry<K,V> {
    private:
    _key : UniSlot<K>;
    _value : UniSlot<V>;

    public:
    MapEntry() {
        _key.construct();
        _value.construct();
    }

    MapEntry(other : const MapEntry<K,V>&) {
        _key.construct(other.key());
        _value.construct(other.value());
    }

    ~MapEntry() {
        _key.destruct();
        _value.destruct();
    }

    /** Read-only access to the key. */
    const key() : const K& {
        return _key.get();
    }

    /** Read-only access to the value. */
    const value() : const V& {
        return _value.get();
    }

    /** Replace the value in place. */
    value(val : const V&) {
        _value.get() = val;
    }

    /** Mutable access to the value. Internal helper, not part of the Entry<K,V> API. */
    getValue() : V& {
        return _value.get();
    }

    /** Set the key. Internal helper, used only while constructing/relocating an entry. */
    setKey(k : const K&) {
        _key.get() = k;
    }
}


/**
 * ListMap<K,V> — a concrete implementation of a mutable, insertion-ordered map using a list.
 *
 * Internally backed by a DoubleLinkedList<MapEntry<K,V>>: keys are looked up with a
 * linear scan using the '==' operator, so lookup, insertion and removal are all O(n).
 * Re-inserting an already-present key updates its value in place without moving the
 * entry in iteration order: first() always returns the oldest surviving key, last()
 * the most recently inserted one.
 *
 * Usage:
 *   m : ListMap<int, int>;
 *   m.put(1, 10);
 *   m.put(2, 20);
 *   m.get(1);            // OptionalConstRef holding 10
 *   m.containsKey(2);     // true
 *   m.remove(1);
 */
template<typename K, typename V>
class ListMap : public MutableOrderedMap<K, V> {
    private:
    _list : DoubleLinkedList<MapEntry<K,V>>;

    /** Return the index of the entry whose key equals key, or -1 if not found. O(n). */
    const _indexOfKey(key : const K&) : int {
        it : ConstIterator<MapEntry<K,V>>! = _list.constIterator();
        idx : int = 0;
        cur : OptionalConstRef<MapEntry<K,V>> = it.next();
        while (cur.hasValue()) {
            if (cur.get().key() == key) {
                return idx;
            }
            idx = idx + 1;
            cur = it.next();
        }
        return -1;
    }

    public:
    ~ListMap() {
        clear();
    }

    /** Return true if the map contains no entries. */
    const isEmpty() : bool {
        return _list.isEmpty();
    }

    /** Return the number of entries in the map. */
    const size() : unsigned int {
        return _list.size();
    }

    /** Return true if some entry has a key equal to key. O(n). */
    const containsKey(key : const K&) : bool {
        return _indexOfKey(key) >= 0;
    }

    /** Return true if some entry has a value equal to value. O(n). */
    const containsValue(value : const V&) : bool {
        it : ConstIterator<MapEntry<K,V>>! = _list.constIterator();
        cur : OptionalConstRef<MapEntry<K,V>> = it.next();
        while (cur.hasValue()) {
            if (cur.get().value() == value) {
                return true;
            }
            cur = it.next();
        }
        return false;
    }

    /** Read-only reference to the value associated with key, or empty. O(n). */
    const get(key : const K&) : OptionalConstRef<V> {
        result : OptionalConstRef<V>;
        idx : int = _indexOfKey(key);
        if (idx >= 0) {
            result.set(_list.get((unsigned int) idx).value());
        }
        return result;
    }

    /** Mutable reference to the value associated with key, or empty. O(n). */
    get(key : const K&) : OptionalRef<V> {
        result : OptionalRef<V>;
        idx : int = _indexOfKey(key);
        if (idx >= 0) {
            result.set(_list.get((unsigned int) idx).getValue());
        }
        return result;
    }

    /** Associate value with key, updating it in place if key is already present. O(n). */
    put(key : const K&, value : const V&) {
        idx : int = _indexOfKey(key);
        if (idx >= 0) {
            _list.get((unsigned int) idx).getValue() = value;
            return;
        }
        entry : MapEntry<K,V>;
        entry.setKey(key);
        entry.getValue() = value;
        _list.append(entry);
    }

    /** Associate value with key only if key is absent. O(n). */
    putIfAbsent(key : const K&, value : const V&) : OptionalRef<V> {
        result : OptionalRef<V>;
        idx : int = _indexOfKey(key);
        if (idx >= 0) {
            result.set(_list.get((unsigned int) idx).getValue());
            return result;
        }
        entry : MapEntry<K,V>;
        entry.setKey(key);
        entry.getValue() = value;
        _list.append(entry);
        return result;
    }

    /** Associate value with key, returning the previous value if any. O(n). */
    replace(key : const K&, value : const V&, insertIfAbsent : bool = true) : Optional<V> {
        result : Optional<V>;
        idx : int = _indexOfKey(key);
        if (idx >= 0) {
            slot : V& = _list.get((unsigned int) idx).getValue();
            result.set(slot);
            slot = value;
            return result;
        }
        if (insertIfAbsent) {
            entry : MapEntry<K,V>;
            entry.setKey(key);
            entry.getValue() = value;
            _list.append(entry);
        }
        return result;
    }

    /** Remove the entry for key. Returns true if key was found and removed. O(n). */
    remove(key : const K&) : bool {
        idx : int = _indexOfKey(key);
        if (idx < 0) {
            return false;
        }
        return _list.removeAt((unsigned int) idx);
    }

    /** Remove every entry from the map. */
    clear() {
        _list.clear();
    }

    /** Read-only reference to the oldest surviving entry, or empty. O(1). */
    const first() : OptionalConstRef<Entry<K,V>> {
        result : OptionalConstRef<Entry<K,V>>;
        if (!_list.isEmpty()) {
            result.set(_list.get(0));
        }
        return result;
    }

    /** Read-only reference to the most recently inserted entry, or empty. O(1). */
    const last() : OptionalConstRef<Entry<K,V>> {
        result : OptionalConstRef<Entry<K,V>>;
        if (!_list.isEmpty()) {
            result.set(_list.get(_list.size() - 1));
        }
        return result;
    }

    /** Mutable reference to the oldest surviving entry, or empty. O(1). */
    mutableFirst() : OptionalRef<MutableEntry<K,V>> {
        result : OptionalRef<MutableEntry<K,V>>;
        if (!_list.isEmpty()) {
            result.set(_list.get(0));
        }
        return result;
    }

    /** Mutable reference to the most recently inserted entry, or empty. O(1). */
    mutableLast() : OptionalRef<MutableEntry<K,V>> {
        result : OptionalRef<MutableEntry<K,V>>;
        if (!_list.isEmpty()) {
            result.set(_list.get(_list.size() - 1));
        }
        return result;
    }

    /** Read-only iterator over the entries, in insertion order. */
    const constIterator() : ConstIterator<Entry<K,V>>! {
        return new ListMapConstIterator<K,V>(_list.constIterator());
    }

    /** Mutable iterator over the entries, in insertion order: value() can be updated. */
    iterator() : Iterator<MutableEntry<K,V>>! {
        return new ListMapIterator<K,V>(_list.iterator());
    }
}

/**
 * ListMapConstIterator<K,V> — a read-only iterator over a ListMap<K,V>.
 *
 * Adapts the backing DoubleLinkedList<MapEntry<K,V>>'s own ConstIterator<MapEntry<K,V>>,
 * upcasting each entry to Entry<K,V> as required by Map<K,V>::constIterator().
 */
template<typename K, typename V>
class ListMapConstIterator : public ConstIterator<Entry<K,V>> {
    private:
    _inner : ConstIterator<MapEntry<K,V>>!;

    public:
    ListMapConstIterator(inner : ConstIterator<MapEntry<K,V>>!) {
        _inner = inner;
    }

    next() : OptionalConstRef<Entry<K,V>> {
        result : OptionalConstRef<Entry<K,V>>;
        cur : OptionalConstRef<MapEntry<K,V>> = _inner.next();
        if (cur.hasValue()) {
            result.set(cur.get());
        }
        return result;
    }
}

/**
 * ListMapIterator<K,V> — a mutable iterator over a ListMap<K,V>.
 *
 * Adapts the backing DoubleLinkedList<MapEntry<K,V>>'s own Iterator<MapEntry<K,V>>,
 * upcasting each entry to MutableEntry<K,V> as required by MutableMap<K,V>::iterator().
 * The key cannot be changed through this iterator, only the value.
 */
template<typename K, typename V>
class ListMapIterator : public Iterator<MutableEntry<K,V>> {
    private:
    _inner : Iterator<MapEntry<K,V>>!;

    public:
    ListMapIterator(inner : Iterator<MapEntry<K,V>>!) {
        _inner = inner;
    }

    next() : OptionalRef<MutableEntry<K,V>> {
        result : OptionalRef<MutableEntry<K,V>>;
        cur : OptionalRef<MapEntry<K,V>> = _inner.next();
        if (cur.hasValue()) {
            result.set(cur.get());
        }
        return result;
    }
}


/**
 * TreeMapNode<K,V> — internal node of a TreeMap<K,V> self-balancing binary search tree (AVL).
 *
 * Allocated dynamically via new. Mirrors TreeSetNode<T> (see set.k): _left and _right are
 * owners forming a tree of ownership rooted at TreeMap::_root. _height is the cached
 * subtree height. Not part of the public Map<K,V> API surface.
 */
template<typename K, typename V>
struct TreeMapNode {
    _entry : MapEntry<K,V>;
    _left : TreeMapNode<K,V>! = null;
    _right : TreeMapNode<K,V>! = null;
    _height : int = 1;

    ~TreeMapNode() {
        // _entry (class-typed field) is destructed automatically.
        // _left/_right are intentionally NOT deleted here: see TreeSetNode's identical
        // note in set.k — freeing a self-referential owner field from within this
        // struct's own method body triggers a compiler limitation. TreeMap<K,V>'s
        // deleteSubtree() performs the recursive free instead.
    }

    const getKey() : const K& {
        return _entry.key();
    }

    getValue() : V& {
        return _entry.getValue();
    }

    const getValue() : const V& {
        return _entry.value();
    }

    setKey(k : const K&) {
        _entry.setKey(k);
    }

    setValue(v : const V&) {
        _entry.getValue() = v;
    }
}

/**
 * TreeMap<K,V> — a concrete implementation of a mutable map sorted by key.
 * Guarantees entries are visited in ascending key order according to K's '<' operator.
 * Lookup, insertion and removal are O(log n).
 *
 * Implemented as an AVL self-balancing binary search tree, ordered by key. Mirrors
 * TreeSet<T>'s algorithm (see set.k): every insertion/removal rebalances the tree so
 * the height stays O(log n).
 *
 * Usage:
 *   m : TreeMap<int, int>;
 *   m.put(5, 50);
 *   m.put(1, 10);
 *   m.put(3, 30);
 *   m.first();     // Entry (1, 10)
 *   m.last();       // Entry (5, 50)
 */
template<typename K, typename V>
class TreeMap : public MutableSortedMap<K, V> {
    private:
    _root : TreeMapNode<K,V>! = null;
    _size : unsigned int = 0;

    public:
    /*
     * The following static helpers implement the AVL algorithm. They mirror
     * TreeSet<T>'s and are declared 'public' for the same reason (see set.k): a
     * private static method cannot call another private static method of the same
     * class; only 'public' avoids the compiler limitation.
     */
    static height(node : TreeMapNode<K,V>*) : int {
        if (node == null) {
            return 0;
        }
        return node->_height;
    }

    static updateHeight(node : TreeMapNode<K,V>*) {
        lh : int = height(node->_left);
        rh : int = height(node->_right);
        if (lh > rh) {
            node->_height = lh + 1;
        } else {
            node->_height = rh + 1;
        }
    }

    static balanceFactor(node : TreeMapNode<K,V>*) : int {
        return height(node->_left) - height(node->_right);
    }

    /** Right rotation around y: promotes y's left child. Returns the new subtree root. */
    static rotateRight(y : TreeMapNode<K,V>!) : TreeMapNode<K,V>! {
        x : TreeMapNode<K,V>! = y._left;
        t2 : TreeMapNode<K,V>! = x._right;
        x._right = y;
        x._right._left = t2;
        updateHeight(x._right);
        updateHeight(x);
        return x;
    }

    /** Left rotation around x: promotes x's right child. Returns the new subtree root. */
    static rotateLeft(x : TreeMapNode<K,V>!) : TreeMapNode<K,V>! {
        y : TreeMapNode<K,V>! = x._right;
        t2 : TreeMapNode<K,V>! = y._left;
        y._left = x;
        y._left._right = t2;
        updateHeight(y._left);
        updateHeight(y);
        return y;
    }

    /** Recursive AVL insertion. Assumes key is not already present in the subtree. */
    static insertNode(node : TreeMapNode<K,V>!, key : const K&, value : const V&) : TreeMapNode<K,V>! {
        if (node == null) {
            created : TreeMapNode<K,V>! = new TreeMapNode<K,V>();
            created.setKey(key);
            created.setValue(value);
            return created;
        }
        if (key < node.getKey()) {
            node._left = insertNode(node._left, key, value);
        } else if (node.getKey() < key) {
            node._right = insertNode(node._right, key, value);
        } else {
            return node;
        }

        updateHeight(node);
        bf : int = balanceFactor(node);

        if (bf > 1 && key < node._left.getKey()) {
            return rotateRight(node);
        }
        if (bf < -1 && node._right.getKey() < key) {
            return rotateLeft(node);
        }
        if (bf > 1 && node._left.getKey() < key) {
            node._left = rotateLeft(node._left);
            return rotateRight(node);
        }
        if (bf < -1 && key < node._right.getKey()) {
            node._right = rotateRight(node._right);
            return rotateLeft(node);
        }
        return node;
    }

    static minNode(node : TreeMapNode<K,V>*) : TreeMapNode<K,V>* {
        cur : TreeMapNode<K,V>* = node;
        while (cur->_left != null) {
            cur = cur->_left;
        }
        return cur;
    }

    /** Recursive AVL deletion by key. Assumes key is present in the subtree. */
    static deleteNode(node : TreeMapNode<K,V>!, key : const K&) : TreeMapNode<K,V>! {
        if (node == null) {
            return node;
        }
        if (key < node.getKey()) {
            node._left = deleteNode(node._left, key);
        } else if (node.getKey() < key) {
            node._right = deleteNode(node._right, key);
        } else {
            if (node._left == null && node._right == null) {
                delete node;
                return null;
            } else if (node._left == null) {
                child : TreeMapNode<K,V>! = node._right;
                delete node;
                return child;
            } else if (node._right == null) {
                child : TreeMapNode<K,V>! = node._left;
                delete node;
                return child;
            } else {
                successor : TreeMapNode<K,V>* = minNode(node._right);
                succKey : K = successor->getKey();
                succValue : V = successor->getValue();
                node.setKey(succKey);
                node.setValue(succValue);
                node._right = deleteNode(node._right, succKey);
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

    /** Recursively free an entire subtree, one node at a time. See TreeSet<T>::deleteSubtree(). */
    static deleteSubtree(node : TreeMapNode<K,V>!) {
        if (node == null) {
            return;
        }
        n : TreeMapNode<K,V>! = node;
        deleteSubtree(n._left);
        deleteSubtree(n._right);
        delete n;
    }

    private:
    /** Find the node whose key equals key, or null. O(log n). */
    const _find(key : const K&) : TreeMapNode<K,V>* {
        cur : TreeMapNode<K,V>* = _root;
        while (cur != null) {
            if (key < cur->getKey()) {
                cur = cur->_left;
            } else if (cur->getKey() < key) {
                cur = cur->_right;
            } else {
                return cur;
            }
        }
        return null;
    }

    public:
    ~TreeMap() {
        clear();
    }

    /** Return true if the map contains no entries. */
    const isEmpty() : bool {
        return _size == 0;
    }

    /** Return the number of entries in the map. */
    const size() : unsigned int {
        return _size;
    }

    /** Return true if some entry has a key equal to key. O(log n). */
    const containsKey(key : const K&) : bool {
        return _find(key) != null;
    }

    /** Return true if some entry has a value equal to value. Always O(n). */
    const containsValue(value : const V&) : bool {
        it : ConstIterator<Entry<K,V>>! = constIterator();
        cur : OptionalConstRef<Entry<K,V>> = it.next();
        while (cur.hasValue()) {
            if (cur.get().value() == value) {
                return true;
            }
            cur = it.next();
        }
        return false;
    }

    /** Read-only reference to the value associated with key, or empty. O(log n). */
    const get(key : const K&) : OptionalConstRef<V> {
        result : OptionalConstRef<V>;
        node : TreeMapNode<K,V>* = _find(key);
        if (node != null) {
            result.set(node->getValue());
        }
        return result;
    }

    /** Mutable reference to the value associated with key, or empty. O(log n). */
    get(key : const K&) : OptionalRef<V> {
        result : OptionalRef<V>;
        node : TreeMapNode<K,V>* = _find(key);
        if (node != null) {
            result.set(node->getValue());
        }
        return result;
    }

    /** Associate value with key, updating it in place if key is already present. O(log n). */
    put(key : const K&, value : const V&) {
        node : TreeMapNode<K,V>* = _find(key);
        if (node != null) {
            node->setValue(value);
            return;
        }
        _root = insertNode(_root, key, value);
        _size = _size + 1;
    }

    /** Associate value with key only if key is absent. O(log n). */
    putIfAbsent(key : const K&, value : const V&) : OptionalRef<V> {
        result : OptionalRef<V>;
        node : TreeMapNode<K,V>* = _find(key);
        if (node != null) {
            result.set(node->getValue());
            return result;
        }
        _root = insertNode(_root, key, value);
        _size = _size + 1;
        return result;
    }

    /** Associate value with key, returning the previous value if any. O(log n). */
    replace(key : const K&, value : const V&, insertIfAbsent : bool = true) : Optional<V> {
        result : Optional<V>;
        node : TreeMapNode<K,V>* = _find(key);
        if (node != null) {
            result.set(node->getValue());
            node->setValue(value);
            return result;
        }
        if (insertIfAbsent) {
            _root = insertNode(_root, key, value);
            _size = _size + 1;
        }
        return result;
    }

    /** Remove the entry for key. Returns true if key was found and removed. O(log n). */
    remove(key : const K&) : bool {
        if (_find(key) == null) {
            return false;
        }
        _root = deleteNode(_root, key);
        _size = _size - 1;
        return true;
    }

    /** Remove every entry from the map. */
    clear() {
        deleteSubtree(_root);
        _root = null;
        _size = 0;
    }

    /** Read-only reference to the entry with the smallest key, or empty. O(log n). */
    const first() : OptionalConstRef<Entry<K,V>> {
        result : OptionalConstRef<Entry<K,V>>;
        if (_root == null) {
            return result;
        }
        node : TreeMapNode<K,V>* = minNode(_root);
        result.set(node->_entry);
        return result;
    }

    /** Read-only reference to the entry with the largest key, or empty. O(log n). */
    const last() : OptionalConstRef<Entry<K,V>> {
        result : OptionalConstRef<Entry<K,V>>;
        if (_root == null) {
            return result;
        }
        cur : TreeMapNode<K,V>* = _root;
        while (cur->_right != null) {
            cur = cur->_right;
        }
        result.set(cur->_entry);
        return result;
    }

    /** Mutable reference to the entry with the smallest key, or empty. O(log n). */
    mutableFirst() : OptionalRef<MutableEntry<K,V>> {
        result : OptionalRef<MutableEntry<K,V>>;
        if (_root == null) {
            return result;
        }
        node : TreeMapNode<K,V>* = minNode(_root);
        result.set(node->_entry);
        return result;
    }

    /** Mutable reference to the entry with the largest key, or empty. O(log n). */
    mutableLast() : OptionalRef<MutableEntry<K,V>> {
        result : OptionalRef<MutableEntry<K,V>>;
        if (_root == null) {
            return result;
        }
        cur : TreeMapNode<K,V>* = _root;
        while (cur->_right != null) {
            cur = cur->_right;
        }
        result.set(cur->_entry);
        return result;
    }

    /** Read-only ascending (in-order, by key) iterator over the entries. */
    const constIterator() : ConstIterator<Entry<K,V>>! {
        return new TreeMapConstIterator<K,V>(_root);
    }

    /** Mutable ascending (in-order, by key) iterator over the entries. */
    iterator() : Iterator<MutableEntry<K,V>>! {
        return new TreeMapIterator<K,V>(_root);
    }
}

/**
 * TreeMapConstIterator<K,V> — a read-only ascending (in-order) iterator over a TreeMap<K,V>.
 * See TreeSetConstIterator<T> (set.k) for the traversal technique.
 */
template<typename K, typename V>
class TreeMapConstIterator : public ConstIterator<Entry<K,V>> {
    private:
    _stack : Vector<TreeMapNode<K,V>*>;

    pushLeftSpine(node : TreeMapNode<K,V>*) {
        cur : TreeMapNode<K,V>* = node;
        while (cur != null) {
            _stack.append(cur);
            cur = cur->_left;
        }
    }

    public:
    TreeMapConstIterator(root : TreeMapNode<K,V>*) {
        pushLeftSpine(root);
    }

    next() : OptionalConstRef<Entry<K,V>> {
        result : OptionalConstRef<Entry<K,V>>;
        if (_stack.isEmpty()) {
            return result;
        }
        node : TreeMapNode<K,V>* = _stack.last();
        _stack.removeLast();
        result.set(node->_entry);
        pushLeftSpine(node->_right);
        return result;
    }
}

/**
 * TreeMapIterator<K,V> — a mutable ascending (in-order) iterator over a TreeMap<K,V>.
 * The key cannot be changed through this iterator, only the value (see MutableEntry<K,V>).
 */
template<typename K, typename V>
class TreeMapIterator : public Iterator<MutableEntry<K,V>> {
    private:
    _stack : Vector<TreeMapNode<K,V>*>;

    pushLeftSpine(node : TreeMapNode<K,V>*) {
        cur : TreeMapNode<K,V>* = node;
        while (cur != null) {
            _stack.append(cur);
            cur = cur->_left;
        }
    }

    public:
    TreeMapIterator(root : TreeMapNode<K,V>*) {
        pushLeftSpine(root);
    }

    next() : OptionalRef<MutableEntry<K,V>> {
        result : OptionalRef<MutableEntry<K,V>>;
        if (_stack.isEmpty()) {
            return result;
        }
        node : TreeMapNode<K,V>* = _stack.last();
        _stack.removeLast();
        result.set(node->_entry);
        pushLeftSpine(node->_right);
        return result;
    }
}


/**
 * HashMapNode<K,V> — internal node of a HashMap<K,V> collision chain.
 * Mirrors HashSetNode<T> (see set.k). Not part of the public Map<K,V> API surface.
 */
template<typename K, typename V>
struct HashMapNode {
    _entry : MapEntry<K,V>;
    _next : HashMapNode<K,V>! = null;

    ~HashMapNode() {
        // _entry (class-typed field) is destructed automatically.
        // _next is intentionally NOT deleted here (see HashSetNode's identical note in
        // set.k): HashMapChain<K,V> frees the whole chain iteratively (see clearChain()).
    }

    const getKey() : const K& {
        return _entry.key();
    }

    getValue() : V& {
        return _entry.getValue();
    }

    const getValue() : const V& {
        return _entry.value();
    }

    setKey(k : const K&) {
        _entry.setKey(k);
    }

    setValue(v : const V&) {
        _entry.getValue() = v;
    }
}

/**
 * HashMapChain<K,V> — head of a singly-linked collision chain, one per hash bucket.
 * Mirrors HashSetChain<T> (see set.k).
 */
template<typename K, typename V>
struct HashMapChain {
    _head : HashMapNode<K,V>! = null;
    _nextChain : HashMapChain<K,V>! = null;

    ~HashMapChain() {
        clearChain();
        // _nextChain is intentionally NOT deleted here (see HashSetChain's identical
        // note in set.k). HashMap<K,V>::freeChainList() frees the intrusive
        // chain-of-chains list instead.
    }

    /** Remove and free every node of this chain. */
    clearChain() {
        while (_head != null) {
            next : HashMapNode<K,V>! = _head._next;
            _head = next;
        }
    }
}

/**
 * HashMap<K,V> — a concrete implementation of a mutable map using a hash table.
 * Unordered: keys are indexed by their `const hash() : int` method result. Average
 * O(1) lookup, insertion and removal.
 *
 * Implemented with separate chaining, mirroring HashSet<T> (see set.k): an array of
 * buckets (one raw pointer per bucket to an individually heap-allocated
 * HashMapChain<K,V>), each holding a singly-linked chain of colliding entries. The
 * bucket array grows (doubling) and every entry is rehashed whenever the load factor
 * would exceed 0.75.
 *
 * K must provide a `const hash() : int` method (inherited from Object, or overridden)
 * and support the '==' operator, used to resolve collisions within a bucket.
 *
 * Usage:
 *   m : HashMap<int, int>;
 *   m.put(1, 10);
 *   m.put(2, 20);
 *   m.containsKey(1);   // true
 *   m.remove(2);
 */
template<typename K, typename V>
class HashMap : public MutableMap<K, V> {
    private:
    static DEFAULT_CAPACITY : unsigned int = 16;

    _buckets : Vector<HashMapChain<K,V>*>;
    _chainsHead : HashMapChain<K,V>! = null;
    _size : unsigned int = 0;
    _capacity : unsigned int = 0;

    public:
    /*
     * computeIndex() and freeChainList() mirror HashSet<T>'s and are declared 'public'
     * for the same reason (see set.k): a private static method cannot call another
     * private static method of the same class; only 'public' avoids it.
     */
    static computeIndex(key : const K&, capacity : unsigned int) : unsigned int {
        h : int = key.hash();
        u : unsigned int = (unsigned int) h;
        return u % capacity;
    }

    /** Free every HashMapChain<K,V> reachable from the intrusive chains list. */
    static freeChainList(head : HashMapChain<K,V>!) {
        cur : HashMapChain<K,V>! = head;
        while (cur != null) {
            next : HashMapChain<K,V>! = cur._nextChain;
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
            created : HashMapChain<K,V>! = new HashMapChain<K,V>();
            raw : HashMapChain<K,V>* = created;
            created._nextChain = _chainsHead;
            _chainsHead = created;
            _buckets.append(raw);
            i = i + 1;
        }
    }

    /** Grow to newCapacity buckets and redistribute every existing entry. */
    rehash(newCapacity : unsigned int) {
        newBuckets : Vector<HashMapChain<K,V>*>;
        newChainsHead : HashMapChain<K,V>! = null;
        newBuckets.reserve(newCapacity);
        i : unsigned int = 0;
        while (i < newCapacity) {
            created : HashMapChain<K,V>! = new HashMapChain<K,V>();
            raw : HashMapChain<K,V>* = created;
            created._nextChain = newChainsHead;
            newChainsHead = created;
            newBuckets.append(raw);
            i = i + 1;
        }

        bi : unsigned int = 0;
        while (bi < _buckets.size()) {
            oldChain : HashMapChain<K,V>* = _buckets.get(bi);
            node : HashMapNode<K,V>! = oldChain->_head;
            oldChain->_head = null;
            while (node != null) {
                next : HashMapNode<K,V>! = node._next;
                node._next = null;
                newIdx : unsigned int = computeIndex(node.getKey(), newCapacity);
                newChain : HashMapChain<K,V>* = newBuckets.get(newIdx);
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

    /** Find the node whose key equals key, or null. Average O(1). */
    const _find(key : const K&) : HashMapNode<K,V>* {
        cap : unsigned int = _capacity;
        idx : unsigned int = computeIndex(key, cap);
        chain : HashMapChain<K,V>* = _buckets.get(idx);
        node : HashMapNode<K,V>* = chain->_head;
        while (node != null) {
            if (node->getKey() == key) {
                return node;
            }
            node = node->_next;
        }
        return null;
    }

    public:
    HashMap() {
        cap : unsigned int = DEFAULT_CAPACITY;
        _capacity = cap;
        buildBuckets(cap);
    }

    ~HashMap() {
        freeChainList(_chainsHead);
        _chainsHead = null;
    }

    /** Return true if the map contains no entries. */
    const isEmpty() : bool {
        return _size == 0;
    }

    /** Return the number of entries in the map. */
    const size() : unsigned int {
        return _size;
    }

    /** Return true if some entry has a key equal to key. Average O(1). */
    const containsKey(key : const K&) : bool {
        return _find(key) != null;
    }

    /** Return true if some entry has a value equal to value. Always O(n). */
    const containsValue(value : const V&) : bool {
        i : unsigned int = 0;
        while (i < _buckets.size()) {
            chain : HashMapChain<K,V>* = _buckets.get(i);
            node : HashMapNode<K,V>* = chain->_head;
            while (node != null) {
                if (node->getValue() == value) {
                    return true;
                }
                node = node->_next;
            }
            i = i + 1;
        }
        return false;
    }

    /** Read-only reference to the value associated with key, or empty. Average O(1). */
    const get(key : const K&) : OptionalConstRef<V> {
        result : OptionalConstRef<V>;
        node : HashMapNode<K,V>* = _find(key);
        if (node != null) {
            result.set(node->getValue());
        }
        return result;
    }

    /** Mutable reference to the value associated with key, or empty. Average O(1). */
    get(key : const K&) : OptionalRef<V> {
        result : OptionalRef<V>;
        node : HashMapNode<K,V>* = _find(key);
        if (node != null) {
            result.set(node->getValue());
        }
        return result;
    }

    /**
     * Associate value with key, updating it in place if key is already present.
     * Average O(1). May trigger a rehash if the load factor would exceed 0.75.
     */
    put(key : const K&, value : const V&) {
        existing : HashMapNode<K,V>* = _find(key);
        if (existing != null) {
            existing->setValue(value);
            return;
        }
        if ((_size + 1) * 4 > _capacity * 3) {
            rehash(_capacity * 2);
        }
        cap : unsigned int = _capacity;
        idx : unsigned int = computeIndex(key, cap);
        chain : HashMapChain<K,V>* = _buckets.get(idx);
        created : HashMapNode<K,V>! = new HashMapNode<K,V>();
        created.setKey(key);
        created.setValue(value);
        created._next = chain->_head;
        chain->_head = created;
        _size = _size + 1;
    }

    /** Associate value with key only if key is absent. Average O(1). */
    putIfAbsent(key : const K&, value : const V&) : OptionalRef<V> {
        result : OptionalRef<V>;
        existing : HashMapNode<K,V>* = _find(key);
        if (existing != null) {
            result.set(existing->getValue());
            return result;
        }
        if ((_size + 1) * 4 > _capacity * 3) {
            rehash(_capacity * 2);
        }
        cap : unsigned int = _capacity;
        idx : unsigned int = computeIndex(key, cap);
        chain : HashMapChain<K,V>* = _buckets.get(idx);
        created : HashMapNode<K,V>! = new HashMapNode<K,V>();
        created.setKey(key);
        created.setValue(value);
        created._next = chain->_head;
        chain->_head = created;
        _size = _size + 1;
        return result;
    }

    /** Associate value with key, returning the previous value if any. Average O(1). */
    replace(key : const K&, value : const V&, insertIfAbsent : bool = true) : Optional<V> {
        result : Optional<V>;
        existing : HashMapNode<K,V>* = _find(key);
        if (existing != null) {
            result.set(existing->getValue());
            existing->setValue(value);
            return result;
        }
        if (insertIfAbsent) {
            if ((_size + 1) * 4 > _capacity * 3) {
                rehash(_capacity * 2);
            }
            cap : unsigned int = _capacity;
            idx : unsigned int = computeIndex(key, cap);
            chain : HashMapChain<K,V>* = _buckets.get(idx);
            created : HashMapNode<K,V>! = new HashMapNode<K,V>();
            created.setKey(key);
            created.setValue(value);
            created._next = chain->_head;
            chain->_head = created;
            _size = _size + 1;
        }
        return result;
    }

    /** Remove the entry for key. Returns true if key was found and removed. Average O(1). */
    remove(key : const K&) : bool {
        cap : unsigned int = _capacity;
        idx : unsigned int = computeIndex(key, cap);
        chain : HashMapChain<K,V>* = _buckets.get(idx);
        if (chain->_head == null) {
            return false;
        }
        if (chain->_head.getKey() == key) {
            next : HashMapNode<K,V>! = chain->_head._next;
            chain->_head = next;
            _size = _size - 1;
            return true;
        }
        prev : HashMapNode<K,V>* = chain->_head;
        while (prev->_next != null) {
            if (prev->_next.getKey() == key) {
                matched : HashMapNode<K,V>! = prev->_next;
                afterMatched : HashMapNode<K,V>! = matched._next;
                prev->_next = afterMatched;
                _size = _size - 1;
                return true;
            }
            prev = prev->_next;
        }
        return false;
    }

    /** Remove every entry from the map. Bucket capacity is left unchanged. */
    clear() {
        i : unsigned int = 0;
        while (i < _buckets.size()) {
            b : HashMapChain<K,V>* = _buckets.get(i);
            b->clearChain();
            i = i + 1;
        }
        _size = 0;
    }

    /** Read-only iterator over the entries. Order is unspecified. */
    const constIterator() : ConstIterator<Entry<K,V>>! {
        return new HashMapConstIterator<K,V>(_buckets);
    }

    /** Mutable iterator over the entries. Order is unspecified. */
    iterator() : Iterator<MutableEntry<K,V>>! {
        return new HashMapIterator<K,V>(_buckets);
    }
}

/**
 * HashMapConstIterator<K,V> — a read-only iterator over a HashMap<K,V>.
 * See HashSetConstIterator<T> (set.k) for the bucket-walking technique.
 */
template<typename K, typename V>
class HashMapConstIterator : public ConstIterator<Entry<K,V>> {
    private:
    _buckets : const Vector<HashMapChain<K,V>*>*;
    _bucketIndex : unsigned int;
    _current : const HashMapNode<K,V>*;

    advance() {
        while (_current == null && _bucketIndex < _buckets->size()) {
            idx : unsigned int = _bucketIndex;
            chain : HashMapChain<K,V>* = _buckets->get(idx);
            _current = chain->_head;
            _bucketIndex = _bucketIndex + 1;
        }
    }

    public:
    HashMapConstIterator(buckets : const Vector<HashMapChain<K,V>*>&) {
        _buckets = &buckets;
        _bucketIndex = 0;
        _current = null;
        advance();
    }

    next() : OptionalConstRef<Entry<K,V>> {
        result : OptionalConstRef<Entry<K,V>>;
        if (_current == null) {
            return result;
        }
        result.set(_current->_entry);
        _current = _current->_next;
        advance();
        return result;
    }
}

/**
 * HashMapIterator<K,V> — a mutable iterator over a HashMap<K,V>.
 * The key cannot be changed through this iterator, only the value (see MutableEntry<K,V>).
 */
template<typename K, typename V>
class HashMapIterator : public Iterator<MutableEntry<K,V>> {
    private:
    _buckets : Vector<HashMapChain<K,V>*>*;
    _bucketIndex : unsigned int;
    _current : HashMapNode<K,V>*;

    advance() {
        while (_current == null && _bucketIndex < _buckets->size()) {
            idx : unsigned int = _bucketIndex;
            chain : HashMapChain<K,V>* = _buckets->get(idx);
            _current = chain->_head;
            _bucketIndex = _bucketIndex + 1;
        }
    }

    public:
    HashMapIterator(buckets : Vector<HashMapChain<K,V>*>&) {
        _buckets = &buckets;
        _bucketIndex = 0;
        _current = null;
        advance();
    }

    next() : OptionalRef<MutableEntry<K,V>> {
        result : OptionalRef<MutableEntry<K,V>>;
        if (_current == null) {
            return result;
        }
        result.set(_current->_entry);
        _current = _current->_next;
        advance();
        return result;
    }
}

