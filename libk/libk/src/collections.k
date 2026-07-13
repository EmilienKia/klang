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
 * ConstIterator<T> — abstract interface for read-only, consumable, forward-only traversal.
 *
 * Defines the read-only iteration contract: a single `next()` method that returns
 * an `OptionalConstRef<T>` — a read-only view of the next element, or empty when
 * the iteration is finished.
 *
 * A `ConstIterator<T>`:
 * - is consumable (single-pass);
 * - does not allow modification of the iterated elements;
 * - does not guarantee backward navigation;
 * - does not guarantee restartability.
 *
 * @see Iterator<T> for the mutable counterpart.
 */
template<typename T>
interface ConstIterator {
    /**
     * Return a read-only reference to the next element, or empty if the iteration is finished.
     * @return An OptionalConstRef<T> holding the next element, or empty.
     */
    next() : OptionalConstRef<T>;
}

/**
 * Iterator<T> — abstract interface for mutable, consumable, forward-only traversal.
 *
 * Extends ConstIterator<T>: every mutable iterator IS-A const iterator and can be
 * used wherever a ConstIterator<T> is expected. The overridden `next()` returns an
 * `OptionalRef<T>` — a mutable reference to the next element.
 *
 * An `Iterator<T>`:
 * - is consumable (single-pass);
 * - allows in-place modification of the iterated elements;
 * - does not guarantee backward navigation.
 *
 * A non-const iterator can be passed to code that accepts a ConstIterator<T>
 * (upcast via owner pointer), but not the reverse.
 */
template<typename T>
interface Iterator : public ConstIterator<T> {
    /**
     * Return a mutable reference to the next element, or empty if the iteration is finished.
     * @return An OptionalRef<T> holding the next element, or empty.
     */
    next() : OptionalRef<T>;
}

/**
 * Sequence<T> — abstract common interface for all iterable types.
 *
 * Defines the common read-only iteration contract: a single method to retrieve a
 * `ConstIterator<T>` over the elements. A `Sequence<T>` is not necessarily a
 * collection — it may be finite or infinite, lazy or eager, re-iterable or single-use.
 *
 * @see MutableSequence<T> for the mutable counterpart.
 */
template<typename T>
interface Sequence {
    /**
     * Return a read-only iterator over the elements.
     * @return A ConstIterator<T> for read-only traversal.
     */
    constIterator() : ConstIterator<T>!;

/*
    // TODO implement following manipulators:
    map<U>(f: Function<T, U>) -> Sequence<U>;
    filter(predicate: Predicate<T>) -> Sequence<T>;
    // ...
 */
}

/**
 * MutableSequence<T> — abstract interface for sequences that also support mutable iteration.
 *
 * Extends Sequence<T>: every mutable sequence IS-A sequence and provides `constIterator()`
 * as well as the mutable `iterator()`.
 */
template<typename T>
interface MutableSequence : public Sequence<T> {
    /**
     * Return a mutable iterator over the elements.
     * @return An Iterator<T> allowing in-place modification during traversal.
     */
    iterator() : Iterator<T>!;
}

template<typename T>
interface ReversibleSequence : public Sequence<T> {
    /**
     * Return a read-only reverse iterator over the elements.
     * @return A ConstIterator<T> traversing elements back to front.
     */
    constReverseIterator() : ConstIterator<T>!;
}

/**
 * MutableReversibleSequence<T> — mutable sequence that also supports reverse iteration.
 */
template<typename T>
interface MutableReversibleSequence : public MutableSequence<T>, public ReversibleSequence<T> {
    /**
     * Return a mutable reverse iterator over the elements.
     * @return An Iterator<T> traversing elements back to front.
     */
    reverseIterator() : Iterator<T>!;
}


// TODO Add View<T> and FiniteView<Y>

/**
 * Sized — abstract interface for all sized containers.
 */
interface Sized {
    /**
     * Return the number of elements in the container.
     * @return Element count.
     */
    const size() : unsigned int;
    /**
     * Tell whether the container is empty.
     */
    default const isEmpty() : bool {
        return size() == 0;
    }
}


/**
 * Collection<T> — abstract interface for all collections.
 *
 * Defines the common contract shared by all ordered, iterable
 * collection types: size queries, back-insertion, peek, removal,
 * and clearing.
 *
 * Implementing classes: LinkedList<T>, DoubleLinkedList<T>, Vector<T>.
 */
template<typename T>
interface Collection : public Sequence<T>, public Sized {
    /**
     * Return the number of elements in the collection.
     *
     * @return Element count.
     */
    const size() : unsigned int;

    /**
     * Tell whether the collection is empty.
     *
     * @return `true` when no element is stored.
     */
    const isEmpty() : bool;

    /**
     * Return a read-only iterator over the elements in the collection.
     * @return A ConstIterator<T> for the collection.
     */
    constIterator() : ConstIterator<T>!;

    /**
     * Tell whether the collection contains a given value.
     * @param value Value to search for.
     * @return `true` if the value is found in the collection.
     */
// TODO Add a default implementation based on the iterator
    const contains(value: const T&) : bool;
}

/**
 * IndexedCollection<T> — abstract interface for all indexed collections.
 * An indexed collection is a collection that allows access to its elements by index.
 */
template<typename T>
interface IndexedCollection : public Collection<T> {
    /**
     * Return a mutable reference to an element at a given index.
     *
     * @param index Zero-based element index.
     * @return Element reference at `index`.
     */
    get(index : unsigned int) : T&;

    /**
     * Return a const reference to an element at a given index.
     *
     * @param index Zero-based element index.
     * @return Const element reference at `index`.
     */
    const get(index : unsigned int) : const T&;

    /**
     * Subscript operator — mutable access by index.
     *
     * @param index Zero-based element index.
     * @return Element reference at `index`.
     */
    operator [](index : unsigned int) : T&;

    /**
     * Subscript operator — const access by index.
     *
     * @param index Zero-based element index.
     * @return Const element reference at `index`.
     */
    const operator [](index : unsigned int) : const T&;

    // TODO add indexOf(value)->Optional<index> / find
}

/**
 * MutableCollection<T> — abstract interface for all mutable collections.
 * A mutable collection is a collection that allows modification of its elements, including insertion, removal, and updating of values.
 * It also provides a mutable iterator (via MutableSequence<T>) in addition to the read-only constIterator().
 */
template<typename T>
interface MutableCollection : public Collection<T>, public MutableSequence<T>, public Appendable<T>, public Prependable<T> {
    /**
     * Remove the first occurrence of a value from the collection.
     * @param value Value to remove.
     * @return `true` if the value was found and removed.
     */
    remove(value : const T&) : bool;

    /**
     * Remove all the elements of the collection.
     */
    clear();

    // TODO add mutableItems() -> MutableView<T>;
}

/**
 * MutableIndexedCollection<T> — abstract interface for all mutable indexed collections.
 * A mutable indexed collection is a collection that allows modification of its elements by index, including insertion, removal, and updating of values.
 */
template<typename T>
interface MutableIndexedCollection : public IndexedCollection<T>, public MutableCollection<T> {

    /**
     * Set the value of an element at a given index.
     * @param index Zero-based element index.
     * @param value Value to set.
     */
    set(index : unsigned int, value : const T&);

    /**
     * Insert a copy of value at the given index.
     * If index = 0, inserts at the front.
     * If index >= size, inserts at the back.
     *
     * @param index Target insertion index.
     * @param value Value to insert.
     */
    insert(index : unsigned int, value : const T&);

    /**
     * Remove the element at the given index.
     * @param index Zero-based element index.
     * @return `true` if the element was found and removed.
     */
    removeAt(index : unsigned int) : bool;
}

// TODO Add RandomAccessCollection<T> interface for collections that support random access by index, with O(1) time complexity for get and set operations.

/**
 * Appendable<T> — abstract interface for all appendable collections.
 * An appendable collection is a collection that allows adding elements to the end of the collection.
 */
template<typename T>
interface Appendable {
    /**
     * Append a value to the end of the collection.
     * @param value Value to append.
     */
    append(value : const T&);

    /**
     * Append all elements from another collection to the end of this collection.
     * @param coll The collection to append.
     */
    appendAll(coll : Sequence<T>&);
}

template<typename T>
interface Prependable {
    /**
     * Prepend a value to the front of the collection.
     * @param value Value to prepend.
     */
    prepend(value : const T&);

    /**
     * Prepend all elements from another collection to the front of this collection.
     * @param coll The collection to prepend.
     */
    prependAll(coll : Sequence<T>&);
}




// TODO FiniteView<T> ?
// TODO ReversibleSequence<T> ?
