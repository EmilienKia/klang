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
    _cause : Throwable* = null;
    _cause_handle : Throwable* = null;

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
     * Construct a throwable with the given error code and a cause.
     * The cause is the exception that triggered this one (chaining).
     */
    Throwable(code: int, cause: Throwable?) {
        _code = code;
        _cause = cause;
    }

    /**
     * Return the error code.
     */
    const getCode() : int {
        return _code;
    }

    /**
     * Return the cause of this throwable, or null if there is no cause.
     */
    const getCause() : Throwable* {
        return _cause;
    }

    /**
     * Return true if this throwable has a cause.
     */
    const hasCause() : bool {
        return _cause != null;
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

    Exception(code: int, cause: Throwable?) : Throwable(code, cause) {}
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

    FatalError(code: int, cause: Throwable?) : Throwable(code, cause) {}
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

// ═════════════════════════════════════════════════════════════════════════════
// ConstructionException — fatal error when object construction fails.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * ConstructionException — signals that an object constructor threw an exception.
 *
 * Thrown by UniSlot<T>::construct() and MultiSlot<T>::construct() when the
 * underlying T constructor raises a checked exception. The original exception
 * is intercepted and replaced by this ConstructionException.
 *
 * As a FatalError, it does not need to be declared in `throws` clauses.
 */
public class ConstructionException : public FatalError {
public:
    ConstructionException() : FatalError(6) {}

    ConstructionException(code: int) : FatalError(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// NullPointerError — base class for null pointer fatal errors.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * NullPointerError — base class for all null-pointer related fatal errors.
 *
 * Thrown by the runtime when a null pointer is used in an invalid context.
 * As a FatalError subclass, it does not require a `throws` declaration.
 */
public class NullPointerError : public FatalError {
public:
    NullPointerError() : FatalError(7) {}

    NullPointerError(code: int) : FatalError(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// NullDereferenceError — thrown on null pointer dereference.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * NullDereferenceError — signals a dereference of a null pointer.
 *
 * Thrown by the runtime when code attempts to dereference a null owner,
 * pointer, or link.
 */
public class NullDereferenceError : public NullPointerError {
public:
    NullDereferenceError() : NullPointerError(8) {}

    NullDereferenceError(code: int) : NullPointerError(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// NullAssignationError — thrown on assignment to a null pointer.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * NullAssignationError — signals an assignment through a null pointer.
 *
 * Thrown by the runtime when code attempts to assign a value through a null
 * pointer or owner.
 */
public class NullAssignationError : public NullPointerError {
public:
    NullAssignationError() : NullPointerError(9) {}

    NullAssignationError(code: int) : NullPointerError(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// NullCastError — thrown when a dynamic cast yields null on a non-nullable target.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * NullCastError — signals that a dynamic cast produced null for a non-nullable
 * target type (reference or link).
 *
 * Thrown by the runtime when a dynamic cast to a non-nullable addressor fails.
 */
public class NullCastError : public NullPointerError {
public:
    NullCastError() : NullPointerError(10) {}

    NullCastError(code: int) : NullPointerError(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// IndexOutOfBoundsError — thrown when an array index is out of bounds.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * IndexOutOfBoundsError — signals an array index outside the valid range.
 *
 * Thrown by the runtime when an array subscript operation exceeds the array
 * bounds. As a FatalError subclass, it does not require a `throws` declaration.
 */
public class IndexOutOfBoundsError : public FatalError {
public:
    IndexOutOfBoundsError() : FatalError(11) {}

    IndexOutOfBoundsError(code: int) : FatalError(code) {}
}

