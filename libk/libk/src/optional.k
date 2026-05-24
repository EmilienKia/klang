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
 * Factory functions:
 *   filled : Optional<int> = makeOptional<int>(42);
 *   empty  : Optional<int> = makeEmpty<int>();
 */
template<typename T>
struct Optional {
    private:
    _slot : UniSlot<T>;
    _hasValue : bool;

    public:
    /**
     * Default constructor. Creates an empty Optional (no value).
     */
    Optional() {
        _hasValue = false;
    }

    /**
     * Value constructor. Creates an Optional holding a copy of value.
     */
    Optional(value : T&) {
        _slot.construct(value);
        _hasValue = true;
    }

    /**
     * Copy constructor. Copies the state of other:
     * - If other holds a value, this holds a copy of that value.
     * - If other is empty, this is also empty.
     */
    Optional(other : Optional<T>&) {
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
    set(value : T&) {
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
}

/**
 * Create an Optional<T> holding a copy of value.
 */
template<typename T>
makeOptional(value : T&) : Optional<T> {
    o : Optional<T>(value);
    return o;
}

/**
 * Create an empty Optional<T>.
 */
template<typename T>
makeEmpty() : Optional<T> {
    o : Optional<T>;
    return o;
}





