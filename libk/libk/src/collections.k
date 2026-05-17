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
 *   lst.pushBack(42);
 *   lst.pushFront(10);
 *   front : int& = lst.peekFront();   // 10
 *   lst.removeFront();
 *   lst.clear();
 */
template<typename T>
struct LinkedList {
    protected:
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

        setValue(value : T&) {
            _slot.get() = value;
        }
    }

    private:
    _head : LinkedListNode!;
    _tail : LinkedListNode*;
    _size : int;

    public:
    LinkedList() {
        _head = null;
        _tail = null;
        _size = 0;
    }

    ~LinkedList() {
        clear();
    }

    /** Return true if the list contains no elements. */
    const isEmpty() : bool {
        return _size == 0;
    }

    /** Return the number of elements in the list. */
    const getSize() : int {
        return _size;
    }

    /** Insert a copy of value at the front of the list. */
    pushFront(value : T&) {
        node : LinkedListNode! = new LinkedListNode();
        node.setValue(value);
        node._next = _head;
        if (_tail == null) {
            _tail = node;
        }
        _head = node;
        _size = _size + 1;
    }

    /** Insert a copy of value at the back of the list. */
    pushBack(value : T&) {
        node : LinkedListNode! = new LinkedListNode();
        node.setValue(value);
        if (_tail != null) {
            _tail->_next = node;
            _tail = _tail->_next;
        } else {
            _tail = node;
            _head = node;
        }
        _size = _size + 1;
    }

    /**
     * Insert a copy of value at the given index. O(n).
     * If index <= 0, inserts at the front.
     * If index >= size, inserts at the back.
     */
    insert(index : int, value : T&) {
        node : LinkedListNode! = new LinkedListNode();
        node.setValue(value);
        if (index <= 0) {
            node._next = _head;
            if (_tail == null) {
                _tail = node;
            }
            _head = node;
            _size = _size + 1;
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
            _size = _size + 1;
            return;
        }
        prev : LinkedListNode* = _head;
        i : int = 1;
        while (i < index) {
            prev = prev->_next;
            i = i + 1;
        }
        node._next = prev->_next;
        prev->_next = node;
        _size = _size + 1;
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
        _size = _size + 1;
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
        _size = _size + 1;
    }

    /**
     * Construct an element in-place at the given index. O(n).
     * If index <= 0, inserts at the front.
     * If index >= size, inserts at the back.
     */
    template<typename...Args>
    emplace(index : int, Args...args) {
        node : LinkedListNode! = new LinkedListNode();
        node.emplaceValue(args...);
        if (index <= 0) {
            node._next = _head;
            if (_tail == null) {
                _tail = node;
            }
            _head = node;
            _size = _size + 1;
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
            _size = _size + 1;
            return;
        }
        prev : LinkedListNode* = _head;
        i : int = 1;
        while (i < index) {
            prev = prev->_next;
            i = i + 1;
        }
        node._next = prev->_next;
        prev->_next = node;
        _size = _size + 1;
    }

    /** Return a mutable reference to the front element. Undefined if empty. */
    peekFront() : T& {
        return _head.getValue();
    }

    /** Return a const reference to the front element. Undefined if empty. */
    const peekFront() : const T& {
        return _head.getValue();
    }

    /** Return a mutable reference to the back element. Undefined if empty. */
    peekBack() : T& {
        return _tail->getValue();
    }

    /** Return a const reference to the back element. Undefined if empty. */
    const peekBack() : const T& {
        return _tail->getValue();
    }

    /** Return a mutable reference to the element at the given index. O(n). Undefined if out of bounds. */
    get(index : int) : T& {
        current : LinkedListNode* = _head;
        i : int = 0;
        while (i < index) {
            current = current->_next;
            i = i + 1;
        }
        return current->getValue();
    }

    /** Subscript operator — mutable access by index. O(n). Undefined if out of bounds. */
    operator [](index : int) : T& {
        current : LinkedListNode* = _head;
        i : int = 0;
        while (i < index) {
            current = current->_next;
            i = i + 1;
        }
        return current->getValue();
    }

    /** Subscript operator — const access by index. O(n). Undefined if out of bounds. */
    const operator [](index : int) : const T& {
        current : LinkedListNode* = _head;
        i : int = 0;
        while (i < index) {
            current = current->_next;
            i = i + 1;
        }
        return current->getValue();
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
        _size = _size - 1;
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
            _size = _size - 1;
            return true;
        }
        prev : LinkedListNode* = _head;
        while (prev->_next._next != null) {
            prev = prev->_next;
        }
        prev->_next = null;
        _tail = prev;
        _size = _size - 1;
        return true;
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
 *   lst.pushBack(42);
 *   lst.pushFront(10);
 *   front : int& = lst.peekFront();   // 10
 *   lst.removeFront();
 *   lst.removeBack();
 *   lst.clear();
 */
template<typename T>
struct DoubleLinkedList {
    protected:
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

        setValue(value : T&) {
            _slot.get() = value;
        }
    }

    private:
    _head : DoubleLinkedListNode!;
    _tail : DoubleLinkedListNode*;
    _size : int;

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
    const getSize() : int {
        return _size;
    }

    /** Insert a copy of value at the front of the list. */
    pushFront(value : T&) {
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
        _size = _size + 1;
    }

    /** Insert a copy of value at the back of the list. */
    pushBack(value : T&) {
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
        _size = _size + 1;
    }

    /**
     * Insert a copy of value at the given index.
     * Searches from the nearest end for efficiency.
     * If index <= 0, inserts at the front.
     * If index >= size, inserts at the back.
     */
    insert(index : int, value : T&) {
        if (index <= 0) {
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
            _size = _size + 1;
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
            _size = _size + 1;
            return;
        }
        // Insert in the middle — search from nearest end
        if (index <= _size / 2) {
            // Search from front
            prev : DoubleLinkedListNode* = _head;
            i : int = 1;
            while (i < index) {
                prev = prev->_next;
                i = i + 1;
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
            i : int = _size - 1;
            while (i > index) {
                next = next->_prev;
                i = i - 1;
            }
            // next is at position 'index', we insert before it
            node : DoubleLinkedListNode! = new DoubleLinkedListNode();
            node.setValue(value);
            node._next = next->_prev->_next;
            node._prev = next->_prev;
            next->_prev = node;
            node._prev->_next = node;
        }
        _size = _size + 1;
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
        _size = _size + 1;
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
        _size = _size + 1;
    }

    /**
     * Construct an element in-place at the given index.
     * Searches from the nearest end for efficiency.
     * If index <= 0, inserts at the front.
     * If index >= size, inserts at the back.
     */
    template<typename...Args>
    emplace(index : int, Args...args) {
        if (index <= 0) {
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
            _size = _size + 1;
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
            _size = _size + 1;
            return;
        }
        if (index <= _size / 2) {
            prev : DoubleLinkedListNode* = _head;
            i : int = 1;
            while (i < index) {
                prev = prev->_next;
                i = i + 1;
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
            i : int = _size - 1;
            while (i > index) {
                next = next->_prev;
                i = i - 1;
            }
            node : DoubleLinkedListNode! = new DoubleLinkedListNode();
            node.emplaceValue(args...);
            node._next = next->_prev->_next;
            node._prev = next->_prev;
            next->_prev = node;
            node._prev->_next = node;
        }
        _size = _size + 1;
    }

    /** Return a mutable reference to the front element. Undefined if empty. */
    peekFront() : T& {
        return _head.getValue();
    }

    /** Return a const reference to the front element. Undefined if empty. */
    const peekFront() : const T& {
        return _head.getValue();
    }

    /** Return a mutable reference to the back element. Undefined if empty. */
    peekBack() : T& {
        return _tail->getValue();
    }

    /** Return a const reference to the back element. Undefined if empty. */
    const peekBack() : const T& {
        return _tail->getValue();
    }

    /**
     * Return a mutable reference to the element at the given index.
     * Searches from the nearest end. Undefined if out of bounds.
     */
    get(index : int) : T& {
        if (index <= _size / 2) {
            current : DoubleLinkedListNode* = _head;
            i : int = 0;
            while (i < index) {
                current = current->_next;
                i = i + 1;
            }
            return current->getValue();
        } else {
            current : DoubleLinkedListNode* = _tail;
            i : int = _size - 1;
            while (i > index) {
                current = current->_prev;
                i = i - 1;
            }
            return current->getValue();
        }
    }

    /**
     * Subscript operator — mutable access by index.
     * Searches from the nearest end. Undefined if out of bounds.
     */
    operator [](index : int) : T& {
        if (index <= _size / 2) {
            current : DoubleLinkedListNode* = _head;
            i : int = 0;
            while (i < index) {
                current = current->_next;
                i = i + 1;
            }
            return current->getValue();
        } else {
            current : DoubleLinkedListNode* = _tail;
            i : int = _size - 1;
            while (i > index) {
                current = current->_prev;
                i = i - 1;
            }
            return current->getValue();
        }
    }

    /**
     * Subscript operator — const access by index.
     * Searches from the nearest end. Undefined if out of bounds.
     */
    const operator [](index : int) : const T& {
        if (index <= _size / 2) {
            current : DoubleLinkedListNode* = _head;
            i : int = 0;
            while (i < index) {
                current = current->_next;
                i = i + 1;
            }
            return current->getValue();
        } else {
            current : DoubleLinkedListNode* = _tail;
            i : int = _size - 1;
            while (i > index) {
                current = current->_prev;
                i = i - 1;
            }
            return current->getValue();
        }
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
        _size = _size - 1;
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
            _size = _size - 1;
            return true;
        }
        prev : DoubleLinkedListNode* = _tail->_prev;
        prev->_next = null;
        _tail = prev;
        _size = _size - 1;
        return true;
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
}

/**
 * Vector<T> — a dynamic array storing elements contiguously in memory.
 *
 * Backed by a MultiSlot<T> which provides raw storage with explicit
 * lifetime management. The vector maintains a size (number of live
 * elements) and a capacity (allocated slots). When the size exceeds
 * the capacity, the buffer is reallocated with a growth factor of 2.
 *
 * Provides O(1) indexed access, O(1) amortized pushBack, O(n) insert
 * and remove operations.
 *
 * Usage:
 *   vec : Vector<int>;
 *   vec.pushBack(42);
 *   vec.pushBack(10);
 *   val : int& = vec[0];       // 42
 *   vec.removeAt(0);
 *   vec.clear();
 */
template<typename T>
struct Vector {
    private:
    _slot : MultiSlot<T>;
    _size : int;
    _capacity : int;

    /** Grow the internal buffer to at least minCapacity. */
    grow(minCapacity : int) {
        newCap : int = _capacity * 2;
        if (newCap < minCapacity) {
            newCap = minCapacity;
        }
        if (newCap < 4) {
            newCap = 4;
        }
        if (_capacity == 0) {
            _slot.allocate(newCap);
        } else {
            _slot.reallocate(newCap);
        }
        _capacity = newCap;
    }

    public:
    Vector() {
        _size = 0;
        _capacity = 0;
    }

    ~Vector() {
        clear();
        if (_capacity > 0) {
            _slot.deallocate();
        }
    }

    /** Return true if the vector contains no elements. */
    const isEmpty() : bool {
        return _size == 0;
    }

    /** Return the number of elements in the vector. */
    const getSize() : int {
        return _size;
    }

    /** Return the current capacity of the internal buffer. */
    const getCapacity() : int {
        return _capacity;
    }

    /** Insert a copy of value at the back of the vector. Amortized O(1). */
    pushBack(value : T&) {
        if (_size == _capacity) {
            grow(_size + 1);
        }
        idx : int = _size;
        _slot.construct(idx);
        _slot.get(idx) = value;
        _size = _size + 1;
    }

    /** Construct an element in-place at the back of the vector. */
    template<typename...Args>
    emplaceBack(Args...args) {
        if (_size == _capacity) {
            grow(_size + 1);
        }
        idx : int = _size;
        _slot.construct<Args...>(idx, args...);
        _size = _size + 1;
    }

    /**
     * Insert a copy of value at the given index. O(n).
     * Elements at index and beyond are shifted right.
     * If index <= 0, inserts at the front.
     * If index >= size, inserts at the back.
     */
    insert(index : int, value : T&) {
        if (index < 0) { index = 0; }
        if (index > _size) { index = _size; }
        if (_size == _capacity) {
            grow(_size + 1);
        }
        // Shift elements right from the end
        i : int = _size;
        while (i > index) {
            if (i == _size) {
                _slot.construct(i);
            }
            prev : int = i - 1;
            _slot.get(i) = _slot.get(prev);
            i = i - 1;
        }
        if (index == _size) {
            _slot.construct(index);
        }
        _slot.get(index) = value;
        _size = _size + 1;
    }

    /**
     * Construct an element in-place at the given index. O(n).
     * Elements at index and beyond are shifted right.
     * If index <= 0, inserts at the front.
     * If index >= size, inserts at the back.
     */
    template<typename...Args>
    emplace(index : int, Args...args) {
        if (index < 0) { index = 0; }
        if (index > _size) { index = _size; }
        if (_size == _capacity) {
            grow(_size + 1);
        }
        // Shift elements right from the end
        i : int = _size;
        while (i > index) {
            if (i == _size) {
                _slot.construct(i);
            }
            prev : int = i - 1;
            _slot.get(i) = _slot.get(prev);
            i = i - 1;
        }
        if (index < _size) {
            // Destruct existing element before re-constructing in place
            _slot.destruct(index);
        }
        _slot.construct<Args...>(index, args...);
        _size = _size + 1;
    }

    /** Return a mutable reference to the front element. Undefined if empty. */
    peekFront() : T& {
        return _slot.get(0);
    }

    /** Return a const reference to the front element. Undefined if empty. */
    const peekFront() : const T& {
        return _slot.get(0);
    }

    /** Return a mutable reference to the back element. Undefined if empty. */
    peekBack() : T& {
        idx : int = _size - 1;
        return _slot.get(idx);
    }

    /** Return a const reference to the back element. Undefined if empty. */
    const peekBack() : const T& {
        idx : int = _size - 1;
        return _slot.get(idx);
    }

    /** Return a mutable reference to the element at the given index. Undefined if out of bounds. */
    get(index : int) : T& {
        return _slot.get(index);
    }

    /** Return a const reference to the element at the given index. Undefined if out of bounds. */
    const get(index : int) : const T& {
        return _slot.get(index);
    }

    /** Subscript operator — mutable access by index. O(1). Undefined if out of bounds. */
    operator [](index : int) : T& {
        return _slot.get(index);
    }

    /** Subscript operator — const access by index. O(1). Undefined if out of bounds. */
    const operator [](index : int) : const T& {
        return _slot.get(index);
    }

    /** Remove the last element. Returns true if an element was removed. */
    removeBack() : bool {
        if (_size == 0) return false;
        _size = _size - 1;
        idx : int = _size;
        _slot.destruct(idx);
        return true;
    }

    /**
     * Remove the element at the given index. O(n).
     * Elements beyond the index are shifted left.
     * Returns true if an element was removed.
     */
    removeAt(index : int) : bool {
        if (index < 0) return false;
        if (index >= _size) return false;
        // Shift elements left
        i : int = index;
        while (i < _size - 1) {
            next : int = i + 1;
            _slot.get(i) = _slot.get(next);
            i = i + 1;
        }
        _size = _size - 1;
        idx : int = _size;
        _slot.destruct(idx);
        return true;
    }

    /**
     * Remove all elements and destruct them.
     * Does not release the underlying buffer (capacity unchanged).
     */
    clear() {
        i : int = 0;
        while (i < _size) {
            _slot.destruct(i);
            i = i + 1;
        }
        _size = 0;
    }

    /**
     * Reserve capacity for at least minCapacity elements.
     * If the current capacity is already sufficient, does nothing.
     */
    reserve(minCapacity : int) {
        if (minCapacity > _capacity) {
            grow(minCapacity);
        }
    }
}

