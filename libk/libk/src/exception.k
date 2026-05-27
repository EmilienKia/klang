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
// Exception — base class for all throwable exception types.
//
// All exceptions in K should derive from this class. It provides a message
// code identifying the error.
//
// Usage:
//   throw Exception(1);
//
//   try {
//       riskyCall();
//   } catch (e: Exception&) {
//       // handle
//   }
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Exception — root base class for all K exceptions.
 *
 * Provides an integer error code identifying the error.
 * Subclasses should call the Exception constructor with an appropriate code.
 */
public class Exception {
    _code : int = 0;

public:
    /**
     * Construct an exception with code 0.
     */
    Exception() {
        _code = 0;
    }

    /**
     * Construct an exception with the given error code.
     */
    Exception(code: int) {
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
// RuntimeException — base class for exceptions that indicate programming
// errors or unexpected runtime conditions that may not be recoverable.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * RuntimeException — base class for runtime errors.
 *
 * Indicates conditions that a reasonable application should not try to catch,
 * such as null pointer dereferences or array index out of bounds.
 */
public class RuntimeException : public Exception {
public:
    RuntimeException() : Exception() {}

    RuntimeException(code: int) : Exception(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// MemoryException — thrown when a memory allocation or deallocation fails.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * MemoryException — signals a memory allocation failure.
 *
 * Thrown by the runtime when `new` or similar allocation operations cannot
 * satisfy the request (e.g. out-of-memory conditions).
 */
public class MemoryException : public RuntimeException {
public:
    MemoryException() : RuntimeException(1) {}

    MemoryException(code: int) : RuntimeException(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// NullPointerException — thrown on null dereference.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * NullPointerException — signals an attempt to dereference a null pointer.
 */
public class NullPointerException : public RuntimeException {
public:
    NullPointerException() : RuntimeException(2) {}

    NullPointerException(code: int) : RuntimeException(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// IndexOutOfBoundsException — thrown when an array/collection index is invalid.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * IndexOutOfBoundsException — signals an index outside the valid range.
 */
public class IndexOutOfBoundsException : public RuntimeException {
public:
    IndexOutOfBoundsException() : RuntimeException(3) {}

    IndexOutOfBoundsException(code: int) : RuntimeException(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// IllegalArgumentException — thrown when a function receives an invalid argument.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * IllegalArgumentException — signals that a method has been passed an
 * inappropriate or invalid argument.
 */
public class IllegalArgumentException : public RuntimeException {
public:
    IllegalArgumentException() : RuntimeException(4) {}

    IllegalArgumentException(code: int) : RuntimeException(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// IllegalStateException — thrown when an object is in an invalid state.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * IllegalStateException — signals that an object is not in an appropriate
 * state for the requested operation.
 */
public class IllegalStateException : public RuntimeException {
public:
    IllegalStateException() : RuntimeException(5) {}

    IllegalStateException(code: int) : RuntimeException(code) {}
}

