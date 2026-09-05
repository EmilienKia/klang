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
 * Optional<T> — a type-safe container for an optional value.
 *
 * An Optional<T> either holds a value of type T or is empty (no value).
 * Storage is stack-allocated via UniSlot<T> — no heap allocation occurs.
 *
 * Inspired by C++17's std::optional<T>.
 *
 * Usage:
 *   // Create an empty optional
 *   opt : Optional<int>;
 *   // opt.hasValue() == false
 *
 *   // Create an optional with a value
 *   opt2 : Optional<int>(42);
 *   // opt2.hasValue() == true, opt2.get() == 42
 *
 *   // Set / replace a value
 *   opt.set(10);
 *   // opt.hasValue() == true, opt.get() == 10
 *
 *   // Clear the value
 *   opt.reset();
 *   // opt.hasValue() == false
 *
 *   // Safe access with default
 *   val : int = opt.getOr(0);  // returns 0 since opt is empty
 *
 * Static factory:
 *   filled : Optional<int>(42);
 *   empty  : Optional<int> = Optional<int>::empty();
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
     *
     * Usage:
     *   opt : Optional<int> = Optional<int>::empty();
     */
    static empty() : Optional<T> {
        return Optional<T>();
    }
}

/**
 * OptionalRef<T> — a type-safe container for an optional mutable reference.
 *
 * Wraps a nullable mutable pointer to T. Does not own the referenced object.
 * Rebind with set(); read or write through get() / operator*().
 *
 * An OptionalRef<T> can be implicitly widened to an OptionalConstRef<T>.
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
 * OptionalConstRef<T> — a type-safe container for an optional read-only reference.
 *
 * Wraps a nullable pointer to const T, exposing only immutable (const) access.
 * Does not own the referenced object. Rebinding is not supported by design.
 *
 * Can be constructed from a const or mutable reference to T (mutable-to-const
 * widening), including from an OptionalRef<T>.
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
     * Direct pointer copy is possible because OptionalConstRef<T> is a friend of OptionalRef<T>.
     */
    OptionalConstRef(other : const OptionalRef<T>&) {
        _ptr = other._ptr;
    }

    /**
     * Return true if this OptionalConstRef holds a reference.
     */
    const hasValue() : bool {
        return _ptr != null;
    }

    /**
     * Return an immutable reference to the referenced object.
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



