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
 * LinkedList<T> — a singly-linked list storing elements by value.
 *
 * Each element is embedded in a heap-allocated LinkedListNode.
 * Ownership of nodes is chained through owner pointers (similar to
 * std::unique_ptr in C++): the list owns the head node, each node
 * owns the next node. This guarantees automatic deallocation of all
 * nodes when the list is destroyed or cleared.
 *
 * _head is an owner (LinkedListNode!) — owns the first node.
 * _tail is a plain pointer (LinkedListNode*) — observes the last node.
 * Each node's _next is an owner (LinkedListNode!) — owns the successor.
 *
 * Usage:
 *   lst : LinkedList<int>;
 *   lst.append(42);
 *   lst.prepend(10);
 *   front : int& = lst.peekFront();   // 10
 *   lst.removeFront();
 *   lst.clear();
 */
template<typename T>
class LinkedList : public MutableIndexedCollection<T> {
    private:
    /**
     * LinkedListNode — internal node for the singly-linked list.
     *
     * Allocated dynamically via new. The contained value is stored by
     * value inside a UniSlot<T>, which provides explicit lifetime
     * management (construct / destruct) independently of the node's
     * own constructor/destructor.
     *
     * The _next field is an owner: each node owns its successor,
     * forming a chain of ownership rooted at LinkedList::_head.
     */
    static struct LinkedListNode {
        _slot : UniSlot<T>;
        _next : LinkedListNode! = null;

        LinkedListNode() {
            _slot.construct();
        }

        ~LinkedListNode() {
            _slot.destruct();
        }

        template<typename...Args>
        emplaceValue(Args...args) {
            _slot.destruct();
            _slot.construct(args...);
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

    private:
    _head : LinkedListNode!;
    _tail : LinkedListNode*;
    _size : unsigned int;

    public:
    LinkedList() {
        _head = null;
        _tail = null;
        _size = 0;
    }

    ~LinkedList() {
        clear();
    }

    /**
     * Tell whether the list is empty.
     *
     * @return `true` when no element is stored.
     */
    const isEmpty() : bool {
        return _size == 0;
    }

    /**
     * Return the number of elements in the list.
     *
     * @return Element count.
     */
    const size() : unsigned int {
        return _size;
    }

    /**
     * Insert a value at the back of the list.
     *
     * @param value Value to insert.
     */
    append(value : const T&) {
       node : LinkedListNode! = new LinkedListNode();
       node.setValue(value);
       if (_tail != null) {
           _tail->_next = node;
           _tail = _tail->_next;
       } else {
           _tail = node;
           _head = node;
       }
       ++_size;
    }

    /**
     * Insert a value at the front of the list.
     *
     * @param value Value to insert.
     */
    prepend(value : const T&) {
       node : LinkedListNode! = new LinkedListNode();
       node.setValue(value);
       node._next = _head;
       if (_tail == null) {
           _tail = node;
       }
       _head = node;
       ++_size;
    }

    prependAll(coll : Sequence<T>&) {
       // TODO: generic Sequence<T> traversal in stdlib currently lacks a stable dispatch path.
       // Keep API surface and provide concrete overloads below.
    }

    prependAll(coll : const T[]&) {
       i : unsigned int = coll.size;
       while (i > 0) {
           --i;
           prepend(coll[i]);
       }
    }

    appendAll(coll : Sequence<T>&) {
       it : ConstIterator<T>! = coll.constIterator();
       cur : OptionalConstRef<T> = it.next();
       while (cur.hasValue()) {
           append(cur.get());
           cur = it.next();
       }
    }

    appendAll(coll : const T[]&) {
       i : unsigned int = 0;
       while (i < coll.size) {
           append(coll[i]);
           ++i;
       }
    }

    /**
     * Insert a value at the given index.
     *
     * Searches linearly; O(n). If index = 0, inserts at the front.
     * If index >= size, inserts at the back.
     *
     * @param index Target insertion index.
     * @param value Value to insert.
     */
    insert(index : unsigned int, value : const T&) {
        node : LinkedListNode! = new LinkedListNode();
        node.setValue(value);
        if (index == 0) {
            node._next = _head;
            if (_tail == null) {
                _tail = node;
            }
            _head = node;
            ++_size;
            return;
        }
        if (index >= _size) {
            if (_tail != null) {
                _tail->_next = node;
                _tail = _tail->_next;
            } else {
                _tail = node;
                _head = node;
            }
            ++_size;
            return;
        }
        prev : LinkedListNode* = _head;
        i : unsigned int = 1;
        while (i < index) {
            prev = prev->_next;
            ++i;
        }
        node._next = prev->_next;
        prev->_next = node;
        ++_size;
    }

    /** Construct an element in-place at the front of the list. */
    template<typename...Args>
    emplaceFront(Args...args) {
        node : LinkedListNode! = new LinkedListNode();
        node.emplaceValue(args...);
        node._next = _head;
        if (_tail == null) {
            _tail = node;
        }
        _head = node;
        ++_size;
    }

    /** Construct an element in-place at the back of the list. */
    template<typename...Args>
    emplaceBack(Args...args) {
        node : LinkedListNode! = new LinkedListNode();
        node.emplaceValue(args...);
        if (_tail != null) {
            _tail->_next = node;
            _tail = _tail->_next;
        } else {
            _tail = node;
            _head = node;
        }
        ++_size;
    }

    /**
     * Construct an element in-place at the given index. O(n).
     * If index = 0, inserts at the front.
     * If index >= size, inserts at the back.
     */
    template<typename...Args>
    emplace(index : unsigned int, Args...args) {
        node : LinkedListNode! = new LinkedListNode();
        node.emplaceValue(args...);
        if (index == 0) {
            node._next = _head;
            if (_tail == null) {
                _tail = node;
            }
            _head = node;
            ++_size;
            return;
        }
        if (index >= _size) {
            if (_tail != null) {
                _tail->_next = node;
                _tail = _tail->_next;
            } else {
                _tail = node;
                _head = node;
            }
            ++_size;
            return;
        }
        prev : LinkedListNode* = _head;
        i : unsigned int = 1;
        while (i < index) {
            prev = prev->_next;
            ++i;
        }
        node._next = prev->_next;
        prev->_next = node;
        ++_size;
    }

    /** Return a mutable reference to the front element. Undefined if empty. */
    first() : T& {
        return _head.getValue();
    }

    /** Return a const reference to the front element. Undefined if empty. */
    const first() : const T& {
        return _head.getValue();
    }

    /** Return a mutable reference to the back element. Undefined if empty. */
    last() : T& {
        return _tail->getValue();
    }

    /** Return a const reference to the back element. Undefined if empty. */
    const last() : const T& {
        return _tail->getValue();
    }

    /** Return a mutable reference to the element at the given index. O(n). Undefined if out of bounds. */
    get(index : unsigned int) : T& {
        current : LinkedListNode* = _head;
        i : unsigned int = 0;
        while (i < index) {
            current = current->_next;
            ++i;
        }
        return current->getValue();
    }

    /** Return a const reference to the element at the given index. O(n). Undefined if out of bounds. */
    const get(index : unsigned int) : const T& {
        current : LinkedListNode* = _head;
        i : unsigned int = 0;
        while (i < index) {
            current = current->_next;
            ++i;
        }
        return current->getValue();
    }

    /** Subscript operator — mutable access by index. O(n). Undefined if out of bounds. */
    operator [](index : unsigned int) : T& {
        current : LinkedListNode* = _head;
        i : unsigned int = 0;
        while (i < index) {
            current = current->_next;
            ++i;
        }
        return current->getValue();
    }

    /** Subscript operator — const access by index. O(n). Undefined if out of bounds. */
    const operator [](index : unsigned int) : const T& {
        current : LinkedListNode* = _head;
        i : unsigned int = 0;
        while (i < index) {
            current = current->_next;
            ++i;
        }
        return current->getValue();
    }

    set(index : unsigned int, value : const T&) {
        get(index) = value;
    }

    /**
     * Remove the front element. Returns true if an element was removed.
     * The old head node is destroyed via owner reassignment: its _next
     * is moved out first to avoid recursive chain destruction.
     */
    removeFront() : bool {
        if (_head == null) return false;
        next : LinkedListNode! = _head._next;
        _head = next;
        if (_head == null) {
            _tail = null;
        }
        --_size;
        return true;
    }

    /**
     * Remove the back element. Returns true if an element was removed.
     * O(n) traversal since this is singly-linked.
     */
    removeBack() : bool {
        if (_head == null) return false;
        if (_head._next == null) {
            _head = null;
            _tail = null;
            --_size;
            return true;
        }
        prev : LinkedListNode* = _head;
        while (prev->_next._next != null) {
            prev = prev->_next;
        }
        prev->_next = null;
        _tail = prev;
        --_size;
        return true;
    }

    /**
     * Remove the element at the given index. Returns true if an element was removed.
     * O(n) traversal to find the node before the target index.
     * If index is out of bounds, returns false.
     */
    removeAt(index : unsigned int) : bool {
        if (index >= _size) return false;
        if (index == 0) {
            return removeFront();
        }
        prev : LinkedListNode* = _head;
        i : unsigned int = 1;
        while (i < index) {
            prev = prev->_next;
            ++i;
        }
        // prev is the node before the one to remove
        toRemove : LinkedListNode! = prev->_next;
        prev->_next = toRemove._next;
        if (prev->_next == null) {
            _tail = prev;
        }
        --_size;
        return true;
    }

    remove(value : const T&) : bool {
        // TODO: generic value equality for arbitrary T is not yet available in the language.
        // Keep API contract while avoiding invalid eager instantiation for non-primitive T.
        return false;
    }

    /**
     * Remove all elements and deallocate all nodes.
     * Iteratively breaks ownership links to avoid recursive destruction.
     */
    clear() {
        while (_head != null) {
            next : LinkedListNode! = _head._next;
            _head = next;
        }
        _tail = null;
        _size = 0;
    }

    /**
     * Return a read-only iterator over the list from head to tail.
     */
    const constIterator() : ConstIterator<T>! {
        return new LinkedListConstIterator<T>(this);
    }

    /**
     * Return a mutable iterator over the list from head to tail.
     */
    iterator() : Iterator<T>! {
        return new LinkedListIterator<T>(this);
    }

    /**
     * Consume a const reference of each element in sequence order.
     * @param consumer A callable consuming each element.
     */
    const forEach(consumer : functional::Consumer<const T&>) {
        current : LinkedListNode* = _head;
        while (current != null) {
            consumer(current->getValue());
            current = current->_next;
        }
    }

    /**
     * Consume a mutable reference of each element in sequence order.
     * @param consumer A callable consuming each element.
     */
    forEach(consumer : functional::Consumer<T&>) {
        current : LinkedListNode* = _head;
        while (current != null) {
            consumer(current->getValue());
            current = current->_next;
        }
    }

    /**
     * Return true if the list contains an element equal to value.
     * O(n) traversal to check each element.
     */
    const contains(value: const T&) : bool {
        // TODO: generic value equality for arbitrary T is not yet available in the language.
        return false;
    }
}

/**
 * LinkedListConstIterator<T> — a read-only iterator for LinkedList<T>.
 *
 * Implements ConstIterator<T>: next() returns OptionalConstRef<T>.
 * Iterates from the head to the tail of the list.
 */
template<typename T>
class LinkedListConstIterator : public ConstIterator<T> {
private:
    _list : const LinkedList<T>*;
    _index : unsigned int;

public:
    LinkedListConstIterator(list : const LinkedList<T>&) {
        _list = &list;
        _index = 0;
    }

    next() : OptionalConstRef<T> {
        result : OptionalConstRef<T>;
        if (_index >= _list->size()) {
            return result;
        }
        current : unsigned int = _index;
        ++_index;
        value : const T& = _list->get(current);
        result.set(value);
        return result;
    }
}

/**
 * LinkedListIterator<T> — a mutable iterator for LinkedList<T>.
 *
 * Implements Iterator<T> (extends ConstIterator<T>): next() returns OptionalRef<T>.
 * Iterates from the head to the tail of the list.
 */
template<typename T>
class LinkedListIterator : public Iterator<T> {
private:
    _list : LinkedList<T>*;
    _index : unsigned int;

public:
    LinkedListIterator(list : LinkedList<T>&) {
        _list = &list;
        _index = 0;
    }

    next() : OptionalRef<T> {
        result : OptionalRef<T>;
        if (_index >= _list->size()) {
            return result;
        }
        current : unsigned int = _index;
        ++_index;
        value : T& = _list->get(current);
        result.set(value);
        return result;
    }
}

/**
 * DoubleLinkedList<T> — a doubly-linked list storing elements by value.
 *
 * Each element is embedded in a heap-allocated DoubleLinkedListNode.
 * Ownership of nodes is chained forward through owner pointers:
 * _head owns the first node, each node owns its _next.
 * Back-pointers (_prev) are plain pointers (non-owning observers).
 *
 * Compared to LinkedList<T>:
 *   - removeBack() is O(1) instead of O(n)
 *   - Indexed access searches from the nearest end (front or back)
 *   - Slightly more memory per node (extra pointer for _prev)
 *
 * Usage:
 *   lst : DoubleLinkedList<int>;
 *   lst.append(42);
 *   lst.prepend(10);
 *   front : int& = lst.peekFront();   // 10
 *   lst.removeFront();
 *   lst.removeBack();
 *   lst.clear();
 */
template<typename T>
class DoubleLinkedList : public MutableIndexedCollection<T>, public MutableReversibleSequence<T> {
    private:
    /**
     * DoubleLinkedListNode — internal node for the doubly-linked list.
     *
     * _next is an owner: each node owns its successor.
     * _prev is a plain pointer: observes the predecessor (non-owning).
     */
    static struct DoubleLinkedListNode {
        _slot : UniSlot<T>;
        _next : DoubleLinkedListNode! = null;
        _prev : DoubleLinkedListNode* = null;

        DoubleLinkedListNode() {
            _slot.construct();
        }

        ~DoubleLinkedListNode() {
            _slot.destruct();
        }

        template<typename...Args>
        emplaceValue(Args...args) {
            _slot.destruct();
            _slot.construct(args...);
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

    private:
    _head : DoubleLinkedListNode!;
    _tail : DoubleLinkedListNode*;
    _size : unsigned int;

    public:
    DoubleLinkedList() {
        _head = null;
        _tail = null;
        _size = 0;
    }

    ~DoubleLinkedList() {
        clear();
    }

    /** Return true if the list contains no elements. */
    const isEmpty() : bool {
        return _size == 0;
    }

    /** Return the number of elements in the list. */
    const size() : unsigned int {
        return _size;
    }

    /** Insert a copy of value at the back of the list. */
    append(value : const T&) {
        node : DoubleLinkedListNode! = new DoubleLinkedListNode();
        node.setValue(value);
        node._prev = _tail;
        if (_tail != null) {
            _tail->_next = node;
            _tail = _tail->_next;
        } else {
            _tail = node;
            _head = node;
        }
        ++_size;
    }

    /** Insert a copy of value at the front of the list. */
    prepend(value : const T&) {
        node : DoubleLinkedListNode! = new DoubleLinkedListNode();
        node.setValue(value);
        node._prev = null;
        node._next = _head;
        if (node._next != null) {
            node._next._prev = node;
        } else {
            _tail = node;
        }
        _head = node;
        ++_size;
    }

    prependAll(coll : Sequence<T>&) {
        // TODO: generic Sequence<T> traversal in stdlib currently lacks a stable dispatch path.
        // Keep API surface and provide concrete overloads below.
    }

    prependAll(coll : const T[]&) {
        i : unsigned int = coll.size;
        while (i > 0) {
            --i;
            prepend(coll[i]);
        }
    }

    appendAll(coll : Sequence<T>&) {
        it : ConstIterator<T>! = coll.constIterator();
        cur : OptionalConstRef<T> = it.next();
        while (cur.hasValue()) {
            append(cur.get());
            cur = it.next();
        }
    }

    appendAll(coll : const T[]&) {
        i : unsigned int = 0;
        while (i < coll.size) {
            append(coll[i]);
            ++i;
        }
    }

    /**
     * Insert a copy of value at the given index.
     * Searches from the nearest end for efficiency.
     * If index = 0, inserts at the front.
     * If index >= size, inserts at the back.
     */
    insert(index : unsigned int, value : const T&) {
        if (index == 0) {
            // Insert at front
            node : DoubleLinkedListNode! = new DoubleLinkedListNode();
            node.setValue(value);
            node._prev = null;
            node._next = _head;
            if (node._next != null) {
                node._next._prev = node;
            } else {
                _tail = node;
            }
            _head = node;
            ++_size;
            return;
        }
        if (index >= _size) {
            // Insert at back
            node : DoubleLinkedListNode! = new DoubleLinkedListNode();
            node.setValue(value);
            node._prev = _tail;
            if (_tail != null) {
                _tail->_next = node;
                _tail = _tail->_next;
            } else {
                _tail = node;
                _head = node;
            }
            ++_size;
            return;
        }
        // Insert in the middle — search from nearest end
        if (index <= _size / 2) {
            // Search from front
            prev : DoubleLinkedListNode* = _head;
            i : unsigned int = 1;
            while (i < index) {
                prev = prev->_next;
                ++i;
            }
            node : DoubleLinkedListNode! = new DoubleLinkedListNode();
            node.setValue(value);
            node._prev = prev;
            node._next = prev->_next;
            if (prev->_next != null) {
                prev->_next._prev = node;
            }
            prev->_next = node;
        } else {
            // Search from back
            next : DoubleLinkedListNode* = _tail;
            i : unsigned int = _size - 1;
            while (i > index) {
                next = next->_prev;
                --i;
            }
            // next is at position 'index', we insert before it
            node : DoubleLinkedListNode! = new DoubleLinkedListNode();
            node.setValue(value);
            node._next = next->_prev->_next;
            node._prev = next->_prev;
            next->_prev = node;
            node._prev->_next = node;
        }
        ++_size;
    }

    /** Construct an element in-place at the front of the list. */
    template<typename...Args>
    emplaceFront(Args...args) {
        node : DoubleLinkedListNode! = new DoubleLinkedListNode();
        node.emplaceValue(args...);
        node._prev = null;
        node._next = _head;
        if (node._next != null) {
            node._next._prev = node;
        } else {
            _tail = node;
        }
        _head = node;
        ++_size;
    }

    /** Construct an element in-place at the back of the list. */
    template<typename...Args>
    emplaceBack(Args...args) {
        node : DoubleLinkedListNode! = new DoubleLinkedListNode();
        node.emplaceValue(args...);
        node._prev = _tail;
        if (_tail != null) {
            _tail->_next = node;
            _tail = _tail->_next;
        } else {
            _tail = node;
            _head = node;
        }
        ++_size;
    }

    /**
     * Construct an element in-place at the given index.
     * Searches from the nearest end for efficiency.
     * If index = 0, inserts at the front.
     * If index >= size, inserts at the back.
     */
    template<typename...Args>
    emplace(index : unsigned int, Args...args) {
        if (index == 0) {
            node : DoubleLinkedListNode! = new DoubleLinkedListNode();
            node.emplaceValue(args...);
            node._prev = null;
            node._next = _head;
            if (node._next != null) {
                node._next._prev = node;
            } else {
                _tail = node;
            }
            _head = node;
            ++_size;
            return;
        }
        if (index >= _size) {
            node : DoubleLinkedListNode! = new DoubleLinkedListNode();
            node.emplaceValue(args...);
            node._prev = _tail;
            if (_tail != null) {
                _tail->_next = node;
                _tail = _tail->_next;
            } else {
                _tail = node;
                _head = node;
            }
            ++_size;
            return;
        }
        if (index <= _size / 2) {
            prev : DoubleLinkedListNode* = _head;
            i : unsigned int = 1;
            while (i < index) {
                prev = prev->_next;
                ++i;
            }
            node : DoubleLinkedListNode! = new DoubleLinkedListNode();
            node.emplaceValue(args...);
            node._prev = prev;
            node._next = prev->_next;
            if (prev->_next != null) {
                prev->_next._prev = node;
            }
            prev->_next = node;
        } else {
            next : DoubleLinkedListNode* = _tail;
            i : unsigned int = _size - 1;
            while (i > index) {
                next = next->_prev;
                --i;
            }
            // next is at position 'index', we insert before it
            node : DoubleLinkedListNode! = new DoubleLinkedListNode();
            node.emplaceValue(args...);
            node._next = next->_prev->_next;
            node._prev = next->_prev;
            next->_prev = node;
            node._prev->_next = node;
        }
        ++_size;
    }

    /** Return a mutable reference to the front element. Undefined if empty. */
    first() : T& {
        return _head.getValue();
    }

    /** Return a const reference to the front element. Undefined if empty. */
    const first() : const T& {
        return _head.getValue();
    }

    /** Return a mutable reference to the back element. Undefined if empty. */
    last() : T& {
        return _tail->getValue();
    }

    /** Return a const reference to the back element. Undefined if empty. */
    const last() : const T& {
        return _tail->getValue();
    }

    /**
     * Return a mutable reference to the element at the given index.
     * Searches from the nearest end. Undefined if out of bounds.
     */
    get(index : unsigned int) : T& {
        if (index <= _size / 2) {
            current : DoubleLinkedListNode* = _head;
            i : unsigned int = 0;
            while (i < index) {
                current = current->_next;
                ++i;
            }
            return current->getValue();
        } else {
            current : DoubleLinkedListNode* = _tail;
            i : unsigned int = _size - 1;
            while (i > index) {
                current = current->_prev;
                --i;
            }
            return current->getValue();
        }
    }

    /**
     * Return a const reference to the element at the given index.
     * Searches from the nearest end. Undefined if out of bounds.
     */
    const get(index : unsigned int) : const T& {
        if (index <= _size / 2) {
            current : DoubleLinkedListNode* = _head;
            i : unsigned int = 0;
            while (i < index) {
                current = current->_next;
                ++i;
            }
            return current->getValue();
        } else {
            current : DoubleLinkedListNode* = _tail;
            i : unsigned int = _size - 1;
            while (i > index) {
                current = current->_prev;
                --i;
            }
            return current->getValue();
        }
    }

    /**
     * Subscript operator — mutable access by index.
     * Searches from the nearest end. Undefined if out of bounds.
     */
    operator [](index : unsigned int) : T& {
        if (index <= _size / 2) {
            current : DoubleLinkedListNode* = _head;
            i : unsigned int = 0;
            while (i < index) {
                current = current->_next;
                ++i;
            }
            return current->getValue();
        } else {
            current : DoubleLinkedListNode* = _tail;
            i : unsigned int = _size - 1;
            while (i > index) {
                current = current->_prev;
                --i;
            }
            return current->getValue();
        }
    }

    /**
     * Subscript operator — const access by index.
     * Searches from the nearest end. Undefined if out of bounds.
     */
    const operator [](index : unsigned int) : const T& {
        if (index <= _size / 2) {
            current : DoubleLinkedListNode* = _head;
            i : unsigned int = 0;
            while (i < index) {
                current = current->_next;
                ++i;
            }
            return current->getValue();
        } else {
            current : DoubleLinkedListNode* = _tail;
            i : unsigned int = _size - 1;
            while (i > index) {
                current = current->_prev;
                --i;
            }
            return current->getValue();
        }
    }

    set(index : unsigned int, value : const T&) {
        get(index) = value;
    }

    /**
     * Remove the front element. Returns true if an element was removed.
     */
    removeFront() : bool {
        if (_head == null) return false;
        next : DoubleLinkedListNode! = _head._next;
        _head = next;
        if (_head != null) {
            _head._prev = null;
        } else {
            _tail = null;
        }
        --_size;
        return true;
    }

    /**
     * Remove the back element. Returns true if an element was removed. O(1).
     */
    removeBack() : bool {
        if (_tail == null) return false;
        if (_tail->_prev == null) {
            // Single element
            _head = null;
            _tail = null;
            --_size;
            return true;
        }
        prev : DoubleLinkedListNode* = _tail->_prev;
        prev->_next = null;
        _tail = prev;
        --_size;
        return true;
    }

    removeAt(index : unsigned int) : bool {
        if (index >= _size) {
            return false;
        }
        if (index == 0) {
            return removeFront();
        }
        if (index == _size - 1) {
            return removeBack();
        }

        current : DoubleLinkedListNode*;
        if (index <= _size / 2) {
            current = _head;
            i : unsigned int = 0;
            while (i < index) {
                current = current->_next;
                ++i;
            }
        } else {
            current = _tail;
            i : unsigned int = _size - 1;
            while (i > index) {
                current = current->_prev;
                --i;
            }
        }

        prev : DoubleLinkedListNode* = current->_prev;
        next : DoubleLinkedListNode! = current->_next;
        prev->_next = next;
        if (prev->_next != null) {
            prev->_next._prev = prev;
        }
        --_size;
        return true;
    }

    remove(value : const T&) : bool {
        // TODO: generic value equality for arbitrary T is not yet available in the language.
        // Keep API contract while avoiding invalid eager instantiation for non-primitive T.
        return false;
    }

    /**
     * Remove all elements and deallocate all nodes.
     * Iteratively breaks ownership links to avoid recursive destruction.
     */
    clear() {
        while (_head != null) {
            next : DoubleLinkedListNode! = _head._next;
            _head = next;
        }
        _tail = null;
        _size = 0;
    }

    /**
     * Return a read-only iterator over the list from head to tail.
     */
    const constIterator() : ConstIterator<T>! {
        return new DoubleLinkedListConstIterator<T>(this);
    }

    /**
     * Return a mutable iterator over the list from head to tail.
     */
    iterator() : Iterator<T>! {
        return new DoubleLinkedListIterator<T>(this);
    }

    /**
     * Consume a const reference of each element in sequence order.
     * @param consumer A callable consuming each element.
     */
    const forEach(consumer : functional::Consumer<const T&>) {
        current : DoubleLinkedListNode* = _head;
        while (current != null) {
            consumer(current->getValue());
            current = current->_next;
        }
    }

    /**
     * Consume a mutable reference of each element in sequence order.
     * @param consumer A callable consuming each element.
     */
    forEach(consumer : functional::Consumer<T&>) {
        current : DoubleLinkedListNode* = _head;
        while (current != null) {
            consumer(current->getValue());
            current = current->_next;
        }
    }

    /**
     * Return true if the list contains an element equal to value.
     * O(n) traversal to check each element.
     */
    const contains(value: const T&) : bool {
        // TODO: generic value equality for arbitrary T is not yet available in the language.
        return false;
    }

    /**
     * Return a read-only reverse iterator over the list from tail to head.
     */
    const constReverseIterator() : ConstIterator<T>! {
        return new ReverseDoubleLinkedListConstIterator<T>(this);
    }

    /**
     * Return a mutable reverse iterator over the list from tail to head.
     */
    reverseIterator() : Iterator<T>! {
        return new ReverseDoubleLinkedListIterator<T>(this);
    }
}

/**
 * DoubleLinkedListConstIterator<T> — a read-only iterator for DoubleLinkedList<T>.
 *
 * Implements ConstIterator<T>: next() returns OptionalConstRef<T>.
 * Iterates from the head to the tail of the list.
 */
template<typename T>
class DoubleLinkedListConstIterator : public ConstIterator<T> {
private:
    _list : const DoubleLinkedList<T>*;
    _index : unsigned int;
public:
    DoubleLinkedListConstIterator(list : const DoubleLinkedList<T>&) {
        _list = &list;
        _index = 0;
    }

    next() : OptionalConstRef<T> {
        result : OptionalConstRef<T>;
        if (_index >= _list->size()) {
            return result;
        }
        current : unsigned int = _index;
        ++_index;
        value : const T& = _list->get(current);
        result.set(value);
        return result;
    }
}

/**
 * DoubleLinkedListIterator<T> — a mutable iterator for DoubleLinkedList<T>.
 *
 * Implements Iterator<T> (extends ConstIterator<T>): next() returns OptionalRef<T>.
 * Iterates from the head to the tail of the list.
 */
template<typename T>
class DoubleLinkedListIterator : public Iterator<T> {
private:
    _list : DoubleLinkedList<T>*;
    _index : unsigned int;
public:
    DoubleLinkedListIterator(list : DoubleLinkedList<T>&) {
        _list = &list;
        _index = 0;
    }

    next() : OptionalRef<T> {
        result : OptionalRef<T>;
        if (_index >= _list->size()) {
            return result;
        }
        current : unsigned int = _index;
        ++_index;
        value : T& = _list->get(current);
        result.set(value);
        return result;
    }
}

/**
 * ReverseDoubleLinkedListConstIterator<T> — a read-only reverse iterator for DoubleLinkedList<T>.
 *
 * Implements ConstIterator<T>: next() returns OptionalConstRef<T>.
 * Iterates from the tail to the head of the list.
 */
template<typename T>
class ReverseDoubleLinkedListConstIterator : public ConstIterator<T> {
private:
    _list : const DoubleLinkedList<T>*;
    _nextIndex : unsigned int;
    _hasNext : bool;
public:
    ReverseDoubleLinkedListConstIterator(list : const DoubleLinkedList<T>&) {
        _list = &list;
        if (list.size() == 0) {
            _nextIndex = 0;
            _hasNext = false;
        } else {
            _nextIndex = list.size() - 1;
            _hasNext = true;
        }
    }

    next() : OptionalConstRef<T> {
        result : OptionalConstRef<T>;
        if (!_hasNext) {
            return result;
        }

        index : unsigned int = _nextIndex;
        if (index == 0) {
            _hasNext = false;
        } else {
            _nextIndex = index - 1;
        }

        value : const T& = _list->get(index);
        result.set(value);
        return result;
    }
}

/**
 * ReverseDoubleLinkedListIterator<T> — a mutable reverse iterator for DoubleLinkedList<T>.
 *
 * Implements Iterator<T> (extends ConstIterator<T>): next() returns OptionalRef<T>.
 * Iterates from the tail to the head of the list.
 */
template<typename T>
class ReverseDoubleLinkedListIterator : public Iterator<T> {
private:
    _list : DoubleLinkedList<T>*;
    _nextIndex : unsigned int;
    _hasNext : bool;
public:
    ReverseDoubleLinkedListIterator(list : DoubleLinkedList<T>&) {
        _list = &list;
        if (list.size() == 0) {
            _nextIndex = 0;
            _hasNext = false;
        } else {
            _nextIndex = list.size() - 1;
            _hasNext = true;
        }
    }

    next() : OptionalRef<T> {
        result : OptionalRef<T>;
        if (!_hasNext) {
            return result;
        }

        index : unsigned int = _nextIndex;
        if (index == 0) {
            _hasNext = false;
        } else {
            _nextIndex = index - 1;
        }

        value : T& = _list->get(index);
        result.set(value);
        return result;
    }
}
