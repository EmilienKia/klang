/*
 * K Language standard library — Thread and Runnable
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
 * Opaque handle to the C-level KRuntimeThread structure.
 * Users never touch this struct; it is manipulated only through Thread methods.
 */
public struct NativeThread {
}

// ═════════════════════════════════════════════════════════════════════════════
// Runnable — interface for tasks that can be executed by a Thread.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Task body that a Thread invokes when started.
 *
 * Implement this interface and pass the object to Thread() to create a thread
 * that calls run() when started. Thread borrows the task: it neither deletes it
 * nor extends its lifetime. Keep the task and everything it borrows alive
 * until the thread has terminated. Other consumers, such as Executor, have
 * their own execution and lifetime contracts.
 */
public interface Runnable {
    /**
     * Execute the task body.
     * Called by the runtime when the owning Thread is started.
     *
     * Implementations may let any Throwable escape: the thread substrate
     * catches it and terminates only that thread.
     *
     * @throws Throwable any error raised by the task body.
     */
    run() : void throws(Throwable);
}

// ═════════════════════════════════════════════════════════════════════════════
// Thread — a language-level thread of execution.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Handle for a language-level thread backed by one OS thread.
 *
 * ### Start, join, and ownership
 *
 * Construction creates an unstarted handle, not a running task. Call start()
 * at most once, then join() to wait for termination. The lifecycle is NEW,
 * RUNNABLE, RUNNING, then TERMINATED; a terminated Thread is not restartable.
 * The one-start rule is a caller precondition, not a checked guard.
 *
 * The Thread and its borrowed Runnable must both remain alive while run() is
 * executing. A join observes termination, not a task result: an uncaught
 * Throwable is contained by the thread entry wrapper and is not rethrown by
 * join(). Use a Promise/Future when the caller needs a value or failure.
 *
 * An owning Thread destructor waits uninterruptibly for a started thread to
 * terminate before releasing its native state. It does not request
 * interruption or delete the Runnable, and it may block indefinitely.
 * Never destroy the owning Thread from its own task. Do not copy a Thread's
 * owning native handle; use references or pointers to the same live object.
 *
 * Thread::current() instead returns a newly allocated, non-owning wrapper
 * for the caller's native handle. Deleting that wrapper neither joins nor
 * destroys the current thread; it does not keep the native thread alive.
 *
 * ### Cooperative interruption
 *
 * interrupt() sets the target flag and wakes supported interruptible waits;
 * it does not forcibly stop run(), cancel its work, or release its locks.
 * Code doing computation must explicitly check for interruption.
 *
 * Flag consumption is method-specific. Thread sleep/join and Future get do
 * not clear it when reporting interruption; Thread::interrupted() reads and
 * clears it, and checkInterrupted() clears it before throwing. Mutex and
 * Condition interruptible methods also clear it when they throw.
 * Uninterruptible waits do not become interruptible merely because the flag
 * is set. This class provides no general synchronisation for task data.
 *
 * ### Example: keep the task alive until after the thread is joined
 *
 * Declaration order also ensures that unwinding destroys the Thread before
 * its borrowed task, even if join() is interrupted.
 *
 * ```k
 * module thread_example;
 *
 * class Work : public Runnable {
 * public:
 *     override run() : void {
 *         Thread::yield();
 *     }
 * }
 *
 * main() : int throws(ThreadInterruptionException) {
 *     task : Work! = new Work();
 *     thread : Thread! = new Thread(task);
 *     thread->start();
 *     thread->join();
 *     return 0;
 * }
 * ```
 */
public class Thread {
private:
    _native : NativeThread*;    // opaque KRuntimeThread*
    _owned  : bool;             // true = destructor must free native; false = borrowed
    _task   : Runnable*;        // borrowed task; null for a borrowed native handle

    /**
     * Thread body actually handed to the C substrate, as a callable bound to this
     * Thread (`this.runGuarded`). The C trampoline only knows `{ fn, ctx }`: the
     * virtual dispatch of Runnable::run() therefore always happens in K.
     *
     * Any Throwable escaping run() is swallowed here: unwinding out of this method
     * would propagate through the C trampoline frame, which is not unwind-aware,
     * and abort the whole process. Terminating just the offending thread matches
     * the interruption model, where a thread interrupted while blocked is expected
     * to unwind out of its run() body.
     */
    private runGuarded() : void {
        try {
            if (_task != null) {
                _task->run();
            }
        } catch (e: Throwable&) {
            // Uncaught in the thread body: terminate this thread only.
        }
    }


    // ── FFI declarations ─────────────────────────────────────────────────────────
    @ffi::Extern("C") private static __k_thread_create(run: *()) : NativeThread*;
    @ffi::Extern("C") private static __k_thread_start(native: NativeThread*) : int;
    @ffi::Extern("C") private static __k_thread_join(native: NativeThread*) : int;
    @ffi::Extern("C") private static __k_thread_join_timed(native: NativeThread*, timeout_nanos: long) : int;
    @ffi::Extern("C") private static __k_thread_interrupt(native: NativeThread*) : void;
    @ffi::Extern("C") private static __k_thread_is_interrupted(native: const NativeThread*) : bool;
    @ffi::Extern("C") private static __k_thread_yield() : void;
    @ffi::Extern("C") private static __k_thread_interrupted_and_clear() : bool;
    @ffi::Extern("C") private static __k_thread_sleep_nanos(nanos: long) : int;
    @ffi::Extern("C") private static __k_thread_current_native() : NativeThread*;

    @ffi::Extern("C") private static __k_thread_destroy(native: NativeThread*) : void;
    @ffi::Extern("C") private static __k_thread_register_main() : NativeThread*;

public:
    // ── Construction ─────────────────────────────────────────────────────────

    /**
     * Create a new Thread that will execute the given Runnable when started.
     *
     * The Thread is in the NEW state until start() is called.
     *
     * @param task Borrowed runnable; keep it alive until thread termination.
     */
    Thread(task: Runnable*) {
        _task   = task;
        _native = __k_thread_create(this.runGuarded);
        _owned  = true;
    }

    /**
     * Internal constructor used by Thread::current() to wrap an existing
     * native handle without taking ownership.
     * @param native  The KRuntimeThread* opaque handle.
     * @param owned   Pass false when borrowing (e.g. current thread handle).
     */
    Thread(native: NativeThread*, owned: bool) {
        _task   = null;
        _native = native;
        _owned  = owned;
    }

    /**
     * Release an owned native handle after waiting for termination.
     *
     * Waiting during destruction is uninterruptible and never interrupts the
     * task. A wrapper returned by current() does not own the native handle.
     */
    ~Thread() {
        // Only destroy the native RuntimeThread if this Thread owns it.
        // Threads returned by Thread::current() borrow the native handle
        // and must not free it.
        if (_owned && _native != null) {
            __k_thread_destroy(_native);
            _native = null;
        }
    }

    // ── Lifecycle ────────────────────────────────────────────────────────────

    /**
     * Start the thread.
     *
     * The Runnable.run() method will be called from a new OS thread.
     * Must be called at most once; start() exposes no native failure status.
     */
    start() : void {
        __k_thread_start(_native);
    }

    /**
     * Wait for this thread to terminate.
     *
     * Call only after start(). Waiting does not start a NEW thread and does
     * not propagate exceptions thrown by the task. Interruption concerns the
     * caller waiting here, not the target being joined.
     *
     * @throws ThreadInterruptionException if the calling thread is interrupted
     *         before or while waiting; its interrupted flag is not cleared.
     */
    join() : void throws(ThreadInterruptionException) {
        const result : int = __k_thread_join(_native);
        if (result == 1) {
            throw ThreadInterruptionException();
        }
    }

    /**
     * Wait for this thread to terminate, with a timeout.
     *
     * A timeout or interruption does not stop the target or release this
     * handle. As with join(), the target must already have been started.
     *
     * @param timeout Positive wait duration. A non-positive duration currently
     *                waits without a deadline; it is not a polling operation.
     * @throws ThreadInterruptionException if the calling thread is interrupted.
     * @throws TimeoutException if the timeout elapses before termination.
     */
    join(timeout: const Duration&) : void throws(ThreadInterruptionException, TimeoutException) {
        const result : int = __k_thread_join_timed(_native, timeout.toNanos());
        if (result == 1) {
            throw ThreadInterruptionException();
        }
        if (result == 2) {
            throw TimeoutException();
        }
    }

    // ── Interruption ─────────────────────────────────────────────────────────

    /**
     * Set the interrupted flag on this thread and wake any blocking point.
     * Does not terminate the OS thread.
     */
    interrupt() : void {
        __k_thread_interrupt(_native);
    }

    /**
     * Return true if this thread's interrupted flag is set.
     * Does NOT clear the flag.
     *
     * @return true if interrupted.
     */
    const isInterrupted() : bool {
        return __k_thread_is_interrupted(_native);
    }

    // ── Static methods ────────────────────────────────────────────────────────

    /**
     * Return the Thread object for the calling thread.
     * The returned Thread wraps the current thread's native handle.
     * It does NOT own the handle (destructor is a no-op for this wrapper).
     */
    static current() : Thread! {
        return new Thread(__k_thread_current_native(), false);
    }

    /**
     * Read and clear the interrupted flag of the calling thread.
     * Returns true if the flag was set, false otherwise.
     * After this call the flag is always false.
     *
     * @return true if this thread was interrupted.
     */
    static interrupted() : bool {
        return __k_thread_interrupted_and_clear();
    }

    /**
     * Throw ThreadInterruptionException if the calling thread is interrupted.
     * Clears the flag if it was set.
     *
     * @throws ThreadInterruptionException if interrupted.
     */
    static checkInterrupted() : void throws(ThreadInterruptionException) {
        if (__k_thread_interrupted_and_clear()) {
            throw ThreadInterruptionException();
        }
    }

    /**
     * Sleep the calling thread for the given duration.
     * The sleep may complete early if interrupted.
     *
     * @param duration Time to sleep.
     * @throws ThreadInterruptionException if the thread is interrupted.
     */
    static sleep(duration: const Duration&) : void throws(ThreadInterruptionException) {
        const result : int = __k_thread_sleep_nanos(duration.toNanos());
        if (result == 1) {
            throw ThreadInterruptionException();
        }
    }

    /**
     * Yield the CPU.
     * This is a scheduler hint; it does not constitute an interruption point.
     */
    static yield() : void {
        __k_thread_yield();
    }
}
