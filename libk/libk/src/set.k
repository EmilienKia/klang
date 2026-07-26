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
     * @param other The other set to compare with.
     * @return True if this set is a subset of the other set, false otherwise.
     */
    const isSubsetOf(other: const Set<T>&) : bool;

    /**
     * Check if this set is a superset of another set.
     * @param other The other set to compare with.
     * @return True if this set is a superset of the other set, false otherwise.
     */
    const isSupersetOf(other: const Set<T>&) : bool;
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
 * ListSet<T> — a concrete implementation of a mutable non-ordered set using a list.
 * Non-ordered collection of unique objects. The only guarantee is all the objects are different according to ==/!= operator.
 * Insertion and searching could be O(n).
 */
template<typename T>
class ListSet : public MutableSet<T>, public MutableReversibleSequence<T> {
    // TODO
}

/**
 * TreeSet<T> — a concrete implementation of a mutable ordered set using a tree.
 * Ordered collection of objects. Guarantee objects are different according to == / != operator and are sorted according to < operator.
 * Insertion and searching could be O(log n).
 */
template<typename T>
class TreeSet : public MutableOrderedSet<T>, public MutableReversibleSequence<T> {
    // TODO
}

/**
 * HashSet<T> — a concrete implementation of a mutable set using a hash table.
 * Non-ordered collection of unique objects. Objects are guarantee different according to their const T::hash(...) method result.
 */
template<typename T>
class HashSet : public MutableSet<T>, public MutableReversibleSequence<T> {
    // TODO
}