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
 * - `result` (index 0): holds the success value of type R
 * - `error`  (index 1): holds the error value of type E
 *
 * A boolean flag `_hasResult` tracks which alternative is active.
 *
 * Usage:
 *   // Create a successful Expected
 *   e : Expected<int, int>;
 *   e.setResult(42);
 *
 *   // Create an error Expected
 *   e2 : Expected<int, int>;
 *   e2.setError(-1);
 *
 *   // Query and access
 *   if (e.hasResult()) {
 *       val : int = e.getResult();
 *   }
 *   if (e2.hasError()) {
 *       err : int = e2.getError();
 *   }
 *
 * Factory functions:
 *   ok : Expected<int, int> = makeExpected<int, int>(42);
 *   err : Expected<int, int> = makeUnexpected<int, int>(-1);
 */
template<typename R, typename E>
struct Expected {
    private:
    /**
     * Storage — internal union backing Expected<R, E>.
     *
     * This union holds either a result value (alternative index 0) or an error
     * value (alternative index 1). It is not intended for direct use.
     */
    union Storage {
        result: R;
        error: E;
    }

    _storage : Storage;
    _hasResult : bool;

    public:
    /**
     * Default constructor. Creates an Expected in an undefined state.
     * Caller must immediately call setResult() or setError().
     */
    Expected() {
        _hasResult = false;
    }

    /**
     * Return true if this Expected holds a successful result.
     */
    const hasResult() : bool {
        return _hasResult;
    }

    /**
     * Return true if this Expected holds an error.
     */
    const hasError() : bool {
        return !_hasResult;
    }

    /**
     * Set this Expected to hold a successful result value.
     * Any previously stored value is overwritten.
     */
    setResult(value : R&) {
        _storage.result = value;
        _hasResult = true;
    }

    /**
     * Set this Expected to hold an error value.
     * Any previously stored value is overwritten.
     */
    setError(value : E&) {
        _storage.error = value;
        _hasResult = false;
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
        if (_hasResult) {
            return _storage.result;
        }
        return defaultValue;
    }

    /**
     * Return the error value, or the provided default if this holds a result.
     */
    const getErrorOr(defaultValue : E&) : E {
        if (!_hasResult) {
            return _storage.error;
        }
        return defaultValue;
    }
}

/**
 * Create an Expected<R, E> holding a successful result.
 */
template<typename R, typename E>
makeExpected(value : R&) : Expected<R, E> {
    e : Expected<R, E>;
    e.setResult(value);
    return e;
}

/**
 * Create an Expected<R, E> holding an error.
 */
template<typename R, typename E>
makeUnexpected(value : E&) : Expected<R, E> {
    e : Expected<R, E>;
    e.setError(value);
    return e;
}

