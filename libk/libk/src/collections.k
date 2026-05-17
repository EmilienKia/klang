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

        getValue() : T& {
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
    isEmpty() : bool {
        return _size == 0;
    }

    /** Return the number of elements in the list. */
    getSize() : int {
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

    /** Return a reference to the front element. Undefined if empty. */
    peekFront() : T& {
        return _head.getValue();
    }

    /** Return a reference to the back element. Undefined if empty. */
    peekBack() : T& {
        return _tail->getValue();
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