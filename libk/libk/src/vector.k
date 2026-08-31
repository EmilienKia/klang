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
 * Vector<T> — a dynamic array storing elements contiguously in memory.
 *
 * Backed by a MultiSlot<T> which provides raw storage with explicit
 * lifetime management. The vector maintains a size (number of live
 * elements) and a capacity (allocated slots). When the size exceeds
 * the capacity, the buffer is reallocated with a growth factor of 2.
 *
 * Provides O(1) indexed access, O(1) amortized append, O(n) insert
 * and remove operations.
 *
 * Usage:
 *   vec : Vector<int>;
 *   vec.append(42);
 *   vec.prepend(10);
 *   val : int& = vec[0];       // 42
 *   vec.removeAt(0);
 *   vec.clear();
 */
template<typename T>
class Vector : public MutableIndexedCollection<T>, public MutableReversibleSequence<T> {
protected:
    _slot : MultiSlot<T>;
    _size : unsigned int = 0;
    _capacity : unsigned int = 0;

    /** Grow the internal buffer to at least minCapacity. */
    grow(minCapacity : unsigned int) {
        newCap : unsigned int = _capacity * 2;
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
    /**
     * Default constructor — creates an empty vector with zero capacity.
     */
    Vector() -> default;

    /**
     * Copy constructor — deep-copies the buffer so the two vectors own
     * independent storage. Without this explicit constructor, the compiler
     * would either refuse to copy the vector (no owner-typed field is
     * directly visible) or, worse, synthesize an unsafe bytewise copy that
     * aliases the heap buffer between the original and the copy, causing a
     * double free when both are destroyed.
     */
    Vector(other : const Vector<T>&) {
        _size = 0;
        _capacity = 0;
        n : unsigned int = other.size();
        if (n > 0) {
            grow(n);
            i : unsigned int = 0;
            while (i < n) {
                _slot.construct(i);
                _slot.get(i) = other.get(i);
                ++i;
            }
            _size = n;
        }
    }

    /**
     * Drain constructor - moves the buffer from the other vector to this one, leaving the other vector empty.
     */
    Vector(other : Vector<T>#) {
        _size = other._size;
        _capacity = other._capacity;
        _slot = # other._slot;
        other._size = 0;
        other._capacity = 0;
    }

    /**
     * Copy constructor from a raw array — deep-copies the elements into the vector.
     *
     * Copy each element from the given array into the vector,
     * constructing new elements in the vector's buffer with their own copy constructor.
     */
    Vector(other : const T[]&) {
        _size = 0;
        _capacity = 0;
        n : unsigned int = other.size;
        if (n > 0) {
            grow(n);
            i : unsigned int = 0;
            while (i < n) {
                _slot.construct(i);
                _slot.get(i) = other[i];
                ++i;
            }
            _size = n;
        }
    }

    /**
     * Copy constructor from a Sequence<T> — deep-copies the elements into the vector.
     */
    Vector(coll : Sequence<T>&) {
        appendAll(coll);
    }

    ~Vector() {
        clear();
        if (_capacity > 0) {
            _slot.deallocate();
        }
    }

    /**
     * Copy assignment operator — deep-copies the buffer so the two vectors own independent storage.
     * If the vector already has allocated storage, it is cleared and reused if possible.
     * If the other vector has more elements than the current capacity, the buffer is reallocated.
     */
    operator = (other : const Vector<T>&) : Vector<T>& {
// TODO : add check for self-assignment to avoid unnecessary work and potential issues with aliasing.
//        if (this != other) {
            clear();
            n : unsigned int = other.size();
            if (n > 0) {
                grow(n);
                i : unsigned int = 0;
                while (i < n) {
                    _slot.construct(i);
                    _slot.get(i) = other.get(i);
                    ++i;
                }
                _size = n;
            }
//        }
        return this;
    }

    /**
     * Drain assignment operator — moves the buffer from the other vector to this one, leaving the other vector empty.
     * If the vector already has allocated storage, it is cleared and reused if possible.
     * If the other vector has more elements than the current capacity, the buffer is reallocated.
     */
    operator = (other : Vector<T>#) : Vector<T>& {
// TODO : add check for self-assignment to avoid unnecessary work and potential issues with aliasing.
//        if (this != other) {
            clear();
            _size = other._size;
            _capacity = other._capacity;
            _slot = # other._slot;
            other._size = 0;
            other._capacity = 0;
//        }
        return this;
    }

    /**
     * Copy assignment operator from a raw array — deep-copies the elements into the vector.
     */
    operator = (other : const T[]&) : Vector<T>& {
        clear();
        n : unsigned int = other.size;
        if (n > 0) {
            grow(n);
            i : unsigned int = 0;
            while (i < n) {
                _slot.construct(i);
                _slot.get(i) = other[i];
                ++i;
            }
            _size = n;
        }
        return this;
    }

    /**
     * Copy assignment operator from a Sequence<T> — deep-copies the elements into the vector.
     */
    operator = (coll : Sequence<T>&) : Vector<T>& {
        clear();
        appendAll(coll);
        return this;
    }

    /** Return true if the vector contains no elements. */
    const isEmpty() : bool {
        return _size == 0;
    }

    /** Return the number of elements in the vector. */
    const size() : unsigned int {
        return _size;
    }

    /** Return the current capacity of the internal buffer. */
    const getCapacity() : unsigned int {
        return _capacity;
    }

    /** Insert a copy of value at the back of the vector. Amortized O(1). */
    append(value : const T&) {
        if (_size == _capacity) {
            grow(_size + 1);
        }
        idx : unsigned int = _size;
        _slot.construct(idx);
        _slot.get(idx) = value;
        ++_size;
    }

    /** Insert a copy of value at the front of the vector. O(n). */
    prepend(value : const T&) {
        insert(0, value);
    }

    prependAll(coll : Sequence<T>&) {
        // TODO: generic Sequence<T> traversal in stdlib currently lacks a stable dispatch path.
        // Keep API surface and provide concrete overloads below.
    }

    prependAll(coll : const T[]&) {
        // TODO: check if the sequence is Sized. If so, reserve the size in advance to avoid multiple reallocations.
        i : unsigned int = coll.size;
        while (i > 0) {
            --i;
            prepend(coll[i]);
        }
    }

    appendAll(coll : Sequence<T>&) {
        // TODO: check if the sequence is Sized. If so, reserve the size in advance to avoid multiple reallocations.
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

    /** Construct an element in-place at the back of the vector. */
    template<typename...Args>
    emplaceBack(Args...args) {
        if (_size == _capacity) {
            grow(_size + 1);
        }
        _slot.construct(_size, args...);
        ++_size;
    }

    /**
     * Insert a copy of value at the given index. O(n).
     * Elements at index and beyond are shifted right.
     * If index = 0, inserts at the front.
     * If index >= size, inserts at the back.
     */
    insert(index : unsigned int, value : const T&) {
        if (index > _size) { index = _size; }
        if (_size == _capacity) {
            grow(_size + 1);
        }
        // Shift elements right from the end
        i : unsigned int = _size;
        while (i > index) {
            if (i == _size) {
                _slot.construct(i);
            }
            prev : unsigned int = i - 1;
            _slot.get(i) = _slot.get(prev);
            --i;
        }
        if (index == _size) {
            _slot.construct(index);
        }
        _slot.get(index) = value;
        ++_size;
    }

    // TODO add insertAll(...)

    /**
     * Construct an element in-place at the given index. O(n).
     * Elements at index and beyond are shifted right.
     * If index = 0, inserts at the front.
     * If index >= size, inserts at the back.
     */
    template<typename...Args>
    emplace(index : unsigned int, Args...args) {
        if (index > _size) { index = _size; }
        if (_size == _capacity) {
            grow(_size + 1);
        }
        // Shift elements right from the end
        i : unsigned int = _size;
        while (i > index) {
            if (i == _size) {
                _slot.construct(i);
            }
            prev : unsigned int = i - 1;
            _slot.get(i) = _slot.get(prev);
            --i;
        }
        if (index < _size) {
            // Destruct existing element before re-constructing in place
            _slot.destruct(index);
        }
        _slot.construct(index, args...);
        ++_size;
    }

    /** Return a mutable reference to the front element. Undefined if empty. */
    first() : T& {
        return _slot.get(0);
    }

    /** Return a const reference to the front element. Undefined if empty. */
    const first() : const T& {
        return _slot.get(0);
    }

    /** Return a mutable reference to the back element. Undefined if empty. */
    last() : T& {
        return _slot.get(_size - 1);
    }

    /** Return a const reference to the back element. Undefined if empty. */
    const last() : const T& {
        return _slot.get(_size - 1);
    }

    /** Return a mutable reference to the element at the given index. Undefined if out of bounds. */
    get(index : unsigned int) : T& {
        return _slot.get(index);
    }

    /** Return a const reference to the element at the given index. Undefined if out of bounds. */
    const get(index : unsigned int) : const T& {
        return _slot.get(index);
    }

    /** Subscript operator — mutable access by index. O(1). Undefined if out of bounds. */
    operator [](index : unsigned int) : T& {
        return _slot.get(index);
    }

    /** Subscript operator — const access by index. O(1). Undefined if out of bounds. */
    const operator [](index : unsigned int) : const T& {
        return _slot.get(index);
    }

    set(index : unsigned int, value : const T&) {
        _slot.get(index) = value;
    }

    /** Remove the front element. O(n). Returns true if an element was removed. */
    removeFirst() : bool {
        return removeAt(0);
    }

    /** Remove the last element. Returns true if an element was removed. */
    removeLast() : bool {
        if (_size == 0) return false;
        --_size;
        _slot.destruct(_size);
        return true;
    }

    /**
     * Remove the element at the given index. O(n).
     * Elements beyond the index are shifted left.
     * Returns true if an element was removed.
     */
    removeAt(index : unsigned int) : bool {
        if (index >= _size) return false;
        // Shift elements left
        i : unsigned int = index;
        while (i < _size - 1) {
            next : unsigned int = i + 1;
            _slot.get(i) = _slot.get(next);
            ++i;
        }
        --_size;
        _slot.destruct(_size);
        return true;
    }

    remove(value : const T&) : bool {
        // TODO: generic value equality for arbitrary T is not yet available in the language.
        // Keep API contract while avoiding invalid eager instantiation for non-primitive T.
        return false;
    }

    /**
     * Remove all elements and destruct them.
     * Does not release the underlying buffer (capacity unchanged).
     */
    clear() {
        i : unsigned int = 0;
        while (i < _size) {
            _slot.destruct(i);
            ++i;
        }
        _size = 0;
    }

    /**
     * Reserve capacity for at least minCapacity elements.
     * If the current capacity is already sufficient, does nothing.
     */
    reserve(minCapacity : unsigned int) {
        if (minCapacity > _capacity) {
            grow(minCapacity);
        }
    }

    /**
     * Shrink the capacity to fit the current size.
     * If the size is zero, deallocates the buffer.
     * If the size is less than the capacity, reallocates the buffer to match the size.
     */
    shrinkToFit() {
        if (_size < _capacity) {
            if (_size == 0) {
                _slot.deallocate();
                _capacity = 0;
            } else {
                _slot.reallocate(_size);
                _capacity = _size;
            }
        }
    }

    /**
     * Return true if the vector contains the given value. O(n).
     * TODO: generic value equality for arbitrary T is not yet available in the language.
     */
    const contains(value: const T&) : bool {
        return false;
    }

    /**
     *  Return a read-only iterator over the elements of the vector.
     */
    const constIterator() : ConstIterator<T>! {
        return new VectorConstIterator<T>(this, 0);
    }

    /**
     *  Return a mutable iterator over the elements of the vector.
     */
    iterator() : Iterator<T>! {
        return new VectorIterator<T>(this, 0);
    }

    /**
     * Consume a const reference of each element in sequence order.
     * @param consumer A callable consuming each element.
     */
    const forEach(consumer : functional::Consumer<const T&>) {
        i : unsigned int = 0;
        while (i < _size) {
            consumer(_slot.get(i));
            ++i;
        }
    }

    /**
     * Consume a mutable reference of each element in sequence order.
     * @param consumer A callable consuming each element.
     */
    forEach(consumer : functional::Consumer<T&>) {
        i : unsigned int = 0;
        while (i < _size) {
            consumer(_slot.get(i));
            ++i;
        }
    }

    /**
     * Return a read-only reverse iterator over the elements of the vector.
     */
    const constReverseIterator() : ConstIterator<T>! {
        return new VectorReverseConstIterator<T>(this);
    }

    /**
     * Return a mutable reverse iterator over the elements of the vector.
     */
    reverseIterator() : Iterator<T>! {
        return new VectorReverseIterator<T>(this);
    }
}

/**
 * VectorConstIterator<T> — a read-only forward iterator over a Vector<T>.
 *
 * Implements ConstIterator<T>: next() returns OptionalConstRef<T>.
 * Maintains a reference to the vector and an index into it.
 */
template<typename T>
class VectorConstIterator : public ConstIterator<T> {
private:
    _vec : const Vector<T>*;
    _index : unsigned int;

public:
    VectorConstIterator(vec : const Vector<T>&, index : unsigned int) {
        _vec = &vec;
        _index = index;
    }

    next() : OptionalConstRef<T> {
        result : OptionalConstRef<T>;
        if (_index >= _vec->size()) {
            return result;
        }
        current : unsigned int = _index;
        ++_index;
        value : const T& = _vec->get(current);
        result.set(value);
        return result;
    }
}

/**
 * VectorIterator<T> — a mutable forward iterator over a Vector<T>.
 *
 * Implements Iterator<T> (which extends ConstIterator<T>): next() returns OptionalRef<T>.
 * Maintains a reference to the vector and an index into it.
 * Returns elements in order from front to back.
 */
template<typename T>
class VectorIterator : public Iterator<T> {
private:
    _vec : Vector<T>*;
    _index : unsigned int;

public:
    VectorIterator(vec : Vector<T>&, index : unsigned int) {
        _vec = &vec;
        _index = index;
    }

    next() : OptionalRef<T> {
        result : OptionalRef<T>;
        if (_index >= _vec->size()) {
            return result;
        }
        current : unsigned int = _index;
        ++_index;
        value : T& = _vec->get(current);
        result.set(value);
        return result;
    }
}

/**
 * VectorReverseConstIterator<T> — a read-only reverse iterator over a Vector<T>.
 *
 * Implements ConstIterator<T>: next() returns OptionalConstRef<T>.
 * Returns elements in order from back to front.
 */
template<typename T>
class VectorReverseConstIterator : public ConstIterator<T> {
private:
    _vec : const Vector<T>*;
    _index : unsigned int;

public:
    VectorReverseConstIterator(vec : const Vector<T>&) {
        _vec = &vec;
        _index = vec.size();
    }

    next() : OptionalConstRef<T> {
        result : OptionalConstRef<T>;
        if (_index > _vec->size() || _index == 0) {
            return result;
        }
        --_index;
        current : unsigned int = _index;
        value : const T& = _vec->get(current);
        result.set(value);
        return result;
    }
}

/**
 * VectorReverseIterator<T> — a mutable reverse iterator over a Vector<T>.
 *
 * Implements Iterator<T> (which extends ConstIterator<T>): next() returns OptionalRef<T>.
 * Returns elements in order from back to front.
 */
template<typename T>
class VectorReverseIterator : public Iterator<T> {
private:
    _vec : Vector<T>*;
    _index : unsigned int;

public:
    VectorReverseIterator(vec : Vector<T>&) {
        _vec = &vec;
        _index = vec.size();
    }

    next() : OptionalRef<T> {
        result : OptionalRef<T>;
        if (_index > _vec->size() || _index == 0) {
            return result;
        }
        --_index;
        current : unsigned int = _index;
        value : T& = _vec->get(current);
        result.set(value);
        return result;
    }
}
