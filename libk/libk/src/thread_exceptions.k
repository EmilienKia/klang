/*
 * K Language standard library — thread and concurrency exceptions
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
// ThreadInterruptionException
// ═════════════════════════════════════════════════════════════════════════════

/**
 * ThreadInterruptionException — thrown when a thread is interrupted at an
 * interruptible blocking point.
 *
 * Thrown by Thread.sleep(), Thread.join(), Future.get(), Condition.await(),
 * Mutex.lockInterruptibly(), and all other interruptible blocking APIs when
 * the calling thread's interrupted flag is set before or during the wait.
 *
 * Callers must either catch this exception or declare it in their `throws`
 * clause.  Catching it does not automatically clear the interrupted flag;
 * call Thread.interrupted() if re-entry into an interruptible blocking point
 * must not throw again.
 */
public class ThreadInterruptionException : public Exception {
public:
    /** Construct with default error code 101. */
    ThreadInterruptionException() : Exception(101) {}

    /**
     * Construct with a custom error code.
     * @param code Application-defined subcode.
     */
    ThreadInterruptionException(code: int) : Exception(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// TimeoutException
// ═════════════════════════════════════════════════════════════════════════════

/**
 * TimeoutException — thrown when an operation does not complete within its
 * deadline.
 *
 * Thrown by Thread.join(Duration), Future.get(Duration),
 * Condition.await(Duration), Mutex.tryLock(Duration), and other timed
 * blocking APIs.
 */
public class TimeoutException : public Exception {
public:
    /** Construct with default error code 102. */
    TimeoutException() : Exception(102) {}

    /**
     * Construct with a custom error code.
     * @param code Application-defined subcode.
     */
    TimeoutException(code: int) : Exception(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// CancellationException
// ═════════════════════════════════════════════════════════════════════════════

/**
 * CancellationException — thrown when a Future or async operation is
 * canceled independently of thread interruption.
 *
 * Thrown by Future.get() when the future was explicitly canceled via
 * Future.cancel(), or by a scoped cancellation policy, before the future
 * completed with a value.
 */
public class CancellationException : public Exception {
public:
    /** Construct with default error code 103. */
    CancellationException() : Exception(103) {}

    /**
     * Construct with a custom error code.
     * @param code Application-defined subcode.
     */
    CancellationException(code: int) : Exception(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// ExecutionException
// ═════════════════════════════════════════════════════════════════════════════

/**
 * ExecutionException — wraps an exception thrown by a task running in a
 * separate thread or asynchronous operation.
 *
 * Thrown by Future.get() when the computation represented by the Future
 * terminated with an exception.  The original exception is the cause of this
 * ExecutionException and can be retrieved via getCause().
 */
public class ExecutionException : public Exception {
public:
    /** Construct with default error code 104. */
    ExecutionException() : Exception(104) {}

    /**
     * Construct with a custom error code.
     * @param code Application-defined subcode.
     */
    ExecutionException(code: int) : Exception(code) {}

    /**
     * Construct wrapping an existing throwable cause.
     * @param cause The throwable thrown by the underlying computation.
     */
    ExecutionException(cause: Throwable?) : Exception(104, cause) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// IllegalMonitorStateException
// ═════════════════════════════════════════════════════════════════════════════

/**
 * IllegalMonitorStateException — a monitor operation was attempted by a thread
 * that does not hold the monitor.
 *
 * Thrown by Mutex.unlock(), ReadWriteLock.*Unlock() and by every Condition
 * method when the calling thread does not own the associated lock.
 */
public class IllegalMonitorStateException : public IllegalStateException {
public:
    /** Construct with default error code 105. */
    IllegalMonitorStateException() : IllegalStateException(105) {}

    /**
     * Construct with a custom error code.
     * @param code Application-defined subcode.
     */
    IllegalMonitorStateException(code: int) : IllegalStateException(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// BrokenBarrierException
// ═════════════════════════════════════════════════════════════════════════════

/**
 * BrokenBarrierException — a CyclicBarrier generation was broken while this
 * thread was waiting on it.
 *
 * A generation breaks when any waiting party leaves through interruption or
 * timeout, or when reset() is called while parties are waiting.
 */
public class BrokenBarrierException : public Exception {
public:
    /** Construct with default error code 106. */
    BrokenBarrierException() : Exception(106) {}

    /**
     * Construct with a custom error code.
     * @param code Application-defined subcode.
     */
    BrokenBarrierException(code: int) : Exception(code) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// RejectedExecutionException
// ═════════════════════════════════════════════════════════════════════════════

/**
 * RejectedExecutionException — thrown by an Executor when a task cannot be
 * accepted for execution (e.g. after the executor has been shut down).
 */
public class RejectedExecutionException : public IllegalStateException {
public:
    /** Construct with default error code 107. */
    RejectedExecutionException() : IllegalStateException(107) {}

    /**
     * Construct with a custom error code.
     * @param code Application-defined subcode.
     */
    RejectedExecutionException(code: int) : IllegalStateException(code) {}
}

