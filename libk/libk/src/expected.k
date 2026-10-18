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
 * Expected<R, E> - a discriminated union wrapper representing either
 * a successful result of type R or an error of type E.
 *
 * Inspired by C++23's std::expected and Rust's Result<T, E>.
 *
 * ## States
 *
 * The underlying storage is a nested union with an empty state and two values:
 *
 * - `undefined`: no result or error has been constructed
 * - `result`: holds the success value of type R
 * - `error`: holds the error value of type E
 *
 * The active alternative is tracked by the union's built-in discriminant.
 * Default construction leaves both hasResult() and hasError() false. An
 * undefined Expected is not an error result.
 *
 * ## Usage
 *
 * Static factories choose the state even when R and E are the same type.
 * Inside a function, using only the implicit base library:
 *
 * ```k
 * ok : Expected<int, int> = Expected<int, int>::expected(42);
 * err : Expected<int, int> = Expected<int, int>::unexpected(-1);
 * if (ok.hasResult()) {
 *     value : int = ok.getResult();
 * }
 * if (err.hasError()) {
 *     code : int = err.getError();
 * }
 * pending : Expected<int, int>;
 * fallback : int = pending.getResultOr(0); // 0, also for undefined.
 * pending.setResult(7);
 * pending.setError(-2); // Replaces the result with an error.
 * ```
 *
 * expected(value) wraps success, unexpected(value) wraps an error, and
 * error(value) is an alias for unexpected().
 *
 * ## Ownership and lifetime
 *
 * Only the active result or error is live in the inline union storage. Copy
 * construction copies the active value using its type's copy semantics and
 * preserves an undefined source as undefined. Switching alternatives destroys
 * the old value; assigning the already-active alternative assigns its value.
 * Getters return values rather than borrowed references to the union.
 *
 * ## Pitfalls
 *
 * > Test the matching state before using getResult() or getError().
 *
 * Inactive-alternative reads follow the union's fatal-check behavior, not an
 * error return. `!hasError()` does not imply hasResult(), because the object
 * may be undefined. The getResultOr()/getErrorOr() fallbacks cover both the
 * opposite alternative and the undefined state.
 */
template<typename R, typename E>
struct Expected {
    private:
    /**
     * Storage — internal union backing Expected<R, E>.
     *
     * The trivial first alternative represents the empty state, so default
     * construction never pretends that an R or E object already exists.
     * This storage is not intended for direct use.
     * The Kind enum is automatically generated:
     *   Storage::Kind::undefined = 0
     *   Storage::Kind::result = 1
     *   Storage::Kind::error = 2
     */
    union Storage {
        undefined: byte;
        result: R;
        error: E;
    }

    _storage : Storage;

    public:
    /**
     * Default constructor. Creates an Expected in an undefined state.
     * No result or error is live until setResult() or setError() succeeds.
     */
    Expected() {
    }

    /**
     * Copy constructor. Creates an Expected with the same state as other.
     *
     * - If other holds a result, this holds the same result value.
     * - If other holds an error, this holds the same error value.
     * - If other is in an undefined state, this is also left undefined.
     *
     * @param other Source expected object to copy.
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
     *
     * @return `true` when the active alternative is `result`.
     */
    const hasResult() : bool {
        return _storage.index() == Storage::Kind::result;
    }

    /**
     * Return true if this Expected holds an error.
     *
     * @return `true` when the active alternative is `error`.
     */
    const hasError() : bool {
        return _storage.index() == Storage::Kind::error;
    }

    /**
     * Set this Expected to hold a successful result value.
     * Any previously stored value is overwritten.
     *
     * @param value Result value to store.
     */
    setResult(value : R&) {
        _storage.result = value;
    }

    /**
     * Set this Expected to hold an error value.
     * Any previously stored value is overwritten.
     *
     * @param value Error value to store.
     */
    setError(value : E&) {
        _storage.error = value;
    }

    /**
     * Return the result value.
     *
     * Precondition: hasResult() must be true.
     * An error or undefined state fails the union's runtime discriminant check.
     *
     * @return The stored result value.
     */
    const getResult() : R {
        return _storage.result;
    }

    /**
     * Return the error value.
     *
     * Precondition: hasError() must be true.
     * A result or undefined state fails the union's runtime discriminant check.
     *
     * @return The stored error value.
     */
    const getError() : E {
        return _storage.error;
    }

    /**
     * Return the result value, or the provided default if no result is active.
     *
     * @param defaultValue Value returned when this object does not hold a result.
     * @return The stored result value, or `defaultValue`.
     */
    const getResultOr(defaultValue : R&) : R {
        if (_storage.index() == Storage::Kind::result) {
            return _storage.result;
        }
        return defaultValue;
    }

    /**
     * Return the error value, or the provided default if no error is active.
     *
     * @param defaultValue Value returned when this object does not hold an error.
     * @return The stored error value, or `defaultValue`.
     */
    const getErrorOr(defaultValue : E&) : E {
        if (_storage.index() == Storage::Kind::error) {
            return _storage.error;
        }
        return defaultValue;
    }

    /**
     * Static factory — create an Expected<R, E> holding a successful result.
     *
     * @param value Result value to wrap.
     * @return A new expected object holding `value` as a result.
     */
    public static expected(value : R&) : Expected<R, E> {
        e : Expected<R, E>;
        e.setResult(value);
        return e;
    }

    /**
     * Static factory — create an Expected<R, E> holding an error.
     *
     * @param value Error value to wrap.
     * @return A new expected object holding `value` as an error.
     */
    public static unexpected(value : E&) : Expected<R, E> {
        e : Expected<R, E>;
        e.setError(value);
        return e;
    }

    /**
     * Static factory — alias for unexpected().
     * Provided for readability when the error type is an error code or exception.
     *
     * @param value Error value to wrap.
     * @return A new expected object holding `value` as an error.
     */
    public static error(value : E&) : Expected<R, E> {
        return unexpected(value);
    }
}
