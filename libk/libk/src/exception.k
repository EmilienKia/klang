/*
 * K Language standard library — exception types
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

// ═════════════════════════════════════════════════════════════════════════════
// Throwable — root base class for all types that can be thrown.
//
// Every type passed to `throw` must derive from Throwable. It provides an
// integer error code identifying the error category.
//
// Two direct subclasses define the checked/unchecked contract:
//   - Exception  (checked: must be declared in `throws` clause)
//   - FatalError (unchecked: propagates freely without declaration)
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Throwable — root base class for all throwable types in K.
 *
 * Provides an integer error code identifying the error.
 * Subclasses should call the Throwable constructor with an appropriate code.
 */
public class Throwable {
    _code : int = 0;

public:
    /**
     * Construct a throwable with code 0.
     */
    Throwable() {
        _code = 0;
    }

    /**
     * Construct a throwable with the given error code.
     */
    Throwable(code: int) {
        _code = code;
    }

    /**
     * Return the error code.
     */
    const getCode() : int {
        return _code;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Exception — base class for checked exceptions.
//
// Exceptions derived from this class are "checked": they must be declared in
// the `throws` clause of functions that throw them (or caught locally).
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Exception — base class for checked exceptions.
 *
 * Functions that throw Exception-derived types must declare them with `throws`.
 * Callers must either catch them or propagate them via their own `throws` clause.
 */
public class Exception : public Throwable {
public:
    Exception() : Throwable() {}

    Exception(code: int) : Throwable(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// FatalError — base class for unchecked fatal errors.
//
// Fatal errors are conditions that functions are not required to declare.
// They propagate freely through the call stack without `throws` declarations.
// Typically thrown by the runtime for unrecoverable conditions (OOM, etc.).
// ═════════════════════════════════════════════════════════════════════════════

/**
 * FatalError — base class for unchecked fatal errors.
 *
 * Functions that throw FatalError-derived types do NOT need to declare them
 * in their `throws` clause. These represent conditions that any code may
 * encounter at any time (e.g. out of memory).
 */
public class FatalError : public Throwable {
public:
    FatalError() : Throwable() {}

    FatalError(code: int) : Throwable(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// OutOfMemory — thrown when a memory allocation fails.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * OutOfMemory — signals a memory allocation failure.
 *
 * Thrown by the runtime when `new` or similar allocation operations cannot
 * satisfy the request (e.g. out-of-memory conditions).
 * As a FatalError subclass, it does not require a `throws` declaration.
 */
public class OutOfMemory : public FatalError {
public:
    OutOfMemory() : FatalError(1) {}

    OutOfMemory(code: int) : FatalError(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// NullPointerException — thrown on null dereference.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * NullPointerException — signals an attempt to dereference a null pointer.
 */
public class NullPointerException : public Exception {
public:
    NullPointerException() : Exception(2) {}

    NullPointerException(code: int) : Exception(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// IndexOutOfBoundsException — thrown when an array/collection index is invalid.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * IndexOutOfBoundsException — signals an index outside the valid range.
 */
public class IndexOutOfBoundsException : public Exception {
public:
    IndexOutOfBoundsException() : Exception(3) {}

    IndexOutOfBoundsException(code: int) : Exception(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// IllegalArgumentException — thrown when a function receives an invalid argument.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * IllegalArgumentException — signals that a method has been passed an
 * inappropriate or invalid argument.
 */
public class IllegalArgumentException : public Exception {
public:
    IllegalArgumentException() : Exception(4) {}

    IllegalArgumentException(code: int) : Exception(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// IllegalStateException — thrown when an object is in an invalid state.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * IllegalStateException — signals that an object is not in an appropriate
 * state for the requested operation.
 */
public class IllegalStateException : public Exception {
public:
    IllegalStateException() : Exception(5) {}

    IllegalStateException(code: int) : Exception(code) {}
}
