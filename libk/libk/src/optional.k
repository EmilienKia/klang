/*
 * K Language standard library — Optional<T>
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
 * Optional<T> - a type-safe container for an optional value.
 *
 * An Optional<T> either holds a value of type T or is empty (no value).
 * Storage is inline via UniSlot<T>; the wrapper does not allocate separate
 * storage for T. T's own construction and copying may allocate resources.
 *
 * Inspired by C++17's std::optional<T>.
 *
 * ## Usage
 *
 * Inside a function, using only the implicit base library:
 *
 * ```k
 * opt : Optional<int>;
 * opt.set(10);
 * if (opt.hasValue()) {
 *     value : int = opt.get(); // 10
 * }
 * opt.reset();
 * fallback : int = opt.getOr(0); // 0
 * filled : Optional<int>(42);
 * empty : Optional<int> = Optional<int>::empty();
 * ```
 *
 * ## Ownership and lifetime
 *
 * - The contained T is owned by the Optional. Copy construction copies its
 *   value using T's copy semantics, rather than borrowing the source's slot.
 * - reset(), replacement by set(), and destruction end the contained value's
 *   lifetime. References obtained from get() must not outlive that value.
 * - Use OptionalRef<T> or OptionalConstRef<T> to borrow an existing object
 *   instead of storing a value.
 *
 * ## Pitfalls
 *
 * > get() requires hasValue(). Use getOr() when a copied fallback is wanted.
 *
 * Unlike OptionalRef::getOr(), getOr() returns a value, not a reference to
 * either the slot or the fallback.
 */
template<typename T>
final struct Optional {
private:
    _slot : UniSlot<T>;
    _hasValue : bool;

public:
    /**
     * Default constructor. Creates an empty Optional (no value).
     */
    Optional() : _hasValue(false) {
    }

    /**
     * Value constructor. Creates an Optional holding a copy of value.
     */
    Optional(value : const T&) {
        _slot.construct(value);
        _hasValue = true;
    }

    /**
     * Copy constructor. Copies the state of other:
     *
     * - If other holds a value, this holds a copy of that value.
     * - If other is empty, this is also empty.
     */
    Optional(other : const Optional<T>&) {
        if (other.hasValue()) {
            _slot.construct(other.get());
            _hasValue = true;
        } else {
            _hasValue = false;
        }
    }

    /**
     * Destructor. If this Optional holds a value, destroys it.
     */
    ~Optional() {
        if (_hasValue) {
            _slot.destruct();
        }
    }

    /**
     * Return true if this Optional holds a value.
     */
    const hasValue() : bool {
        return _hasValue;
    }

    /**
     * Return a reference to the contained value.
     *
     * Precondition: hasValue() must be true.
     * Behavior is undefined if called on an empty Optional.
     */
    const get() : T& {
        return _slot.get();
    }

    /**
     * Return the contained value if present, or defaultValue otherwise.
     */
    const getOr(defaultValue : T&) : T {
        if (_hasValue) {
            return _slot.get();
        }
        return defaultValue;
    }

    /**
     * Set the Optional to hold a copy of value.
     * If a value was already present, it is destroyed first.
     */
    set(value : const T&) {
        if (_hasValue) {
            _slot.destruct();
        }
        _slot.construct(value);
        _hasValue = true;
    }

    /**
     * Clear the Optional. If a value is present, it is destroyed.
     * After reset(), hasValue() returns false.
     */
    reset() {
        if (_hasValue) {
            _slot.destruct();
            _hasValue = false;
        }
    }

    /**
     * Static factory: create an empty Optional<T>.
     * Equivalent to the default constructor; provided for expressiveness.
     */
    static empty() : Optional<T> {
        return Optional<T>();
    }
}

/**
 * OptionalRef<T> - a type-safe container for an optional mutable reference.
 *
 * Wraps a nullable mutable pointer to T. Does not own the referenced object.
 * Rebind with set(); read or write through get() / operator*().
 *
 * An OptionalRef<T> can be implicitly widened to an OptionalConstRef<T>.
 *
 * ## Usage
 *
 * Inside a function, using only the implicit base library:
 *
 * ```k
 * value : int = 3;
 * replacement : int = 9;
 * ref : OptionalRef<int>(value);
 * ref.get() = 7; // Changes value, not the binding.
 * ref.set(replacement);
 * ref.get() = 10; // Changes replacement; value is still 7.
 * ```
 *
 * ## Lifetime and pitfalls
 *
 * - The referent must remain alive while accessed. Copying or widening this
 *   wrapper copies only the pointer and never extends the referent's lifetime.
 * - hasValue() checks for a non-null pointer, not for a still-live referent.
 *   A reference invalidated by its owner does not become empty automatically.
 * - get() and operator*() require hasValue(). getOr() returns a borrowed
 *   reference to either the referent or the supplied fallback; that fallback
 *   must also remain alive for any later use of the returned reference.
 * - set() rebinds without changing or destroying the old referent.
 */
template<typename T>
final struct OptionalRef {
private:
    _ptr : T*;

    friend OptionalConstRef<T>;

public:
    /**
     * Default constructor. Creates an empty OptionalRef (no reference).
     */
    OptionalRef() : _ptr(null) {
    }

    /**
     * Constructor from a mutable reference. Stores a pointer to value.
     */
    OptionalRef(value : T&) {
        _ptr = &value;
    }

    /**
     * Copy constructor. Both instances refer to the same object.
     */
    OptionalRef(other : OptionalRef<T>&) {
        _ptr = other._ptr;
    }

    /**
     * Copy constructor (const source). Both instances refer to the same object.
     */
    OptionalRef(other : const OptionalRef<T>&) {
        _ptr = other._ptr;
    }

    /**
     * Return true if this OptionalRef holds a reference.
     */
    const hasValue() : bool {
        return _ptr != null;
    }

    /**
     * Return a mutable reference to the referenced object.
     *
     * Precondition: hasValue() must be true.
     */
    const get() : T& {
        return *_ptr;
    }

    /**
     * Dereference operator. Equivalent to get().
     */
    const operator*() : T& {
        return *_ptr;
    }

    /**
     * Return the referenced value if present, or defaultValue otherwise.
     */
    const getOr(defaultValue : T&) : T& {
        if (_ptr != null) {
            return *_ptr;
        }
        return defaultValue;
    }

    /**
     * Rebind this OptionalRef to a different object.
     */
    set(value : T&) {
        _ptr = &value;
    }
}

/**
 * OptionalConstRef<T> - a type-safe container for an optional read-only reference.
 *
 * Wraps a nullable pointer to const T, exposing only immutable (const) access.
 * Does not own the referenced object. set() rebinds the wrapper without
 * modifying the referent.
 *
 * Can be constructed from a const or mutable reference to T (mutable-to-const
 * widening), including from an OptionalRef<T>.
 *
 * ## Lifetime and pitfalls
 *
 * The same borrowed-lifetime rules as OptionalRef<T> apply: neither copying
 * nor widening keeps the referent alive, and hasValue() cannot detect a
 * dangling pointer. get() and operator*() require a nonempty, live reference;
 * getOr() borrows its fallback when empty.
 *
 * Read-only access prevents mutation through this wrapper, but another
 * mutable reference can still change the underlying object.
 */
template<typename T>
final struct OptionalConstRef {
protected:
    _ptr : const T*;

public:
    /**
     * Default constructor. Creates an empty OptionalConstRef (no reference).
     */
    OptionalConstRef() : _ptr(null) {
    }

    /**
     * Constructor from a reference. Stores a pointer to value.
     * Accepts a const reference so both const and mutable sources can be wrapped.
     */
    OptionalConstRef(value : const T&) {
        _ptr = &value;
    }

    /**
     * Copy constructor.
     */
    OptionalConstRef(other : const OptionalConstRef<T>&) {
        _ptr = other._ptr;
    }

    /**
     * Construct from an OptionalRef — mutable reference widens to read-only view.
     * An empty source remains empty.
     */
    OptionalConstRef(other : const OptionalRef<T>&) {
        _ptr = other.hasValue() ? &other.get() : null;
    }

    /**
     * Return true if this OptionalConstRef holds a reference.
     */
    const hasValue() : bool {
        return _ptr != null;
    }

    /**
     * Return an immutable reference to the referenced object.
     *
     * Precondition: hasValue() must be true.
     */
    const get() : const T& {
        return *_ptr;
    }

    /**
     * Dereference operator. Equivalent to get().
     */
    const operator*() : const T& {
        return *_ptr;
    }

    /**
     * Rebind this OptionalConstRef to point at value.
     * Accepts a const reference; the referenced pointer is stored as
     * `const T*`, and immutability is enforced through the get/operator*
     * accessors (which always return `const T&`).
     */
    set(value : const T&) {
        _ptr = &value;
    }

    /**
     * Return the referenced value if present, or defaultValue otherwise.
     */
    const getOr(defaultValue : const T&) : const T& {
        if (_ptr != null) {
            return *_ptr;
        }
        return defaultValue;
    }
}
