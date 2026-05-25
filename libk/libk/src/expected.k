/*
 * K Language standard library — Expected<R,E>
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
 * Expected<R, E> — a discriminated union wrapper representing either
 * a successful result of type R or an error of type E.
 *
 * Inspired by C++23's std::expected and Rust's Result<T, E>.
 *
 * The underlying storage is a nested template union with two alternatives:
 * - `result` (Kind::result = 0): holds the success value of type R
 * - `error`  (Kind::error  = 1): holds the error value of type E
 *
 * The active alternative is tracked by the union's built-in discriminant,
 * queryable via _storage.index().
 *
 * Usage:
 *   // Create a successful Expected via static factory
 *   ok : Expected<int, int> = Expected<int, int>::expected(42);
 *
 *   // Create an error Expected via static factory
 *   err : Expected<int, int> = Expected<int, int>::unexpected(-1);
 *   // or, using the alias:
 *   err2 : Expected<int, int> = Expected<int, int>::error(-1);
 *
 *   // Manual construction
 *   e : Expected<int, int>;
 *   e.setResult(42);
 *
 *   // Query and access
 *   if (ok.hasResult()) {
 *       val : int = ok.getResult();
 *   }
 *   if (err.hasError()) {
 *       e : int = err.getError();
 *   }
 *
 * Static factory methods (preferred over direct construction):
 *   Expected<R,E>::expected(value)    — wraps a successful result
 *   Expected<R,E>::unexpected(value)  — wraps an error
 *   Expected<R,E>::error(value)       — alias for unexpected()
 */
template<typename R, typename E>
struct Expected {
    private:
    /**
     * Storage — internal union backing Expected<R, E>.
     *
     * This union holds either a result value (alternative index 0) or an error
     * value (alternative index 1). It is not intended for direct use.
     * The Kind enum is automatically generated:
     *   Storage::Kind::result = 0
     *   Storage::Kind::error  = 1
     */
    union Storage {
        result: R;
        error: E;
    }

    _storage : Storage;

    public:
    /**
     * Default constructor. Creates an Expected in an undefined state.
     * Caller must immediately call setResult() or setError().
     */
    Expected() {
    }

    /**
     * Copy constructor. Creates an Expected with the same state as other.
     * - If other holds a result, this holds the same result value.
     * - If other holds an error, this holds the same error value.
     * - If other is in an undefined state, this is also left undefined.
     */
    Expected(other : Expected<R, E>&) {
        if (other.hasResult()) {
            r : R = other.getResult();
            setResult(r);
        } else {
            if (other.hasError()) {
                e : E = other.getError();
                setError(e);
            }
        }
    }

    /**
     * Return true if this Expected holds a successful result.
     */
    const hasResult() : bool {
        return _storage.index() == Storage::Kind::result;
    }

    /**
     * Return true if this Expected holds an error.
     */
    const hasError() : bool {
        return _storage.index() == Storage::Kind::error;
    }

    /**
     * Set this Expected to hold a successful result value.
     * Any previously stored value is overwritten.
     */
    setResult(value : R&) {
        _storage.result = value;
    }

    /**
     * Set this Expected to hold an error value.
     * Any previously stored value is overwritten.
     */
    setError(value : E&) {
        _storage.error = value;
    }

    /**
     * Return the result value.
     * Precondition: hasResult() must be true.
     * Behavior is undefined if called when hasError() is true.
     */
    const getResult() : R {
        return _storage.result;
    }

    /**
     * Return the error value.
     * Precondition: hasError() must be true.
     * Behavior is undefined if called when hasResult() is true.
     */
    const getError() : E {
        return _storage.error;
    }

    /**
     * Return the result value, or the provided default if this holds an error.
     */
    const getResultOr(defaultValue : R&) : R {
        if (_storage.index() == Storage::Kind::result) {
            return _storage.result;
        }
        return defaultValue;
    }

    /**
     * Return the error value, or the provided default if this holds a result.
     */
    const getErrorOr(defaultValue : E&) : E {
        if (_storage.index() == Storage::Kind::error) {
            return _storage.error;
        }
        return defaultValue;
    }

    /**
     * Static factory — create an Expected<R, E> holding a successful result.
     */
    public static expected(value : R&) : Expected<R, E> {
        e : Expected<R, E>;
        e.setResult(value);
        return e;
    }

    /**
     * Static factory — create an Expected<R, E> holding an error.
     */
    public static unexpected(value : E&) : Expected<R, E> {
        e : Expected<R, E>;
        e.setError(value);
        return e;
    }

    /**
     * Static factory — alias for unexpected().
     * Provided for readability when the error type is an error code or exception.
     */
    public static error(value : E&) : Expected<R, E> {
        return unexpected(value);
    }
}

/**
 * Create an Expected<R, E> holding a successful result.
 *
 * Note: this free function is retained as a parser workaround.  The preferred
 * API is Expected<R,E>::expected() but the K parser does not yet support
 * Type<A,B>::method() call syntax in expression context.
 */
template<typename R, typename E>
makeExpected(value : R&) : Expected<R, E> {
    return Expected<R, E>::expected(value);
}

/**
 * Create an Expected<R, E> holding an error.
 *
 * Note: this free function is retained as a parser workaround.  The preferred
 * API is Expected<R,E>::unexpected() but the K parser does not yet support
 * Type<A,B>::method() call syntax in expression context.
 */
template<typename R, typename E>
makeUnexpected(value : E&) : Expected<R, E> {
    return Expected<R, E>::unexpected(value);
}

