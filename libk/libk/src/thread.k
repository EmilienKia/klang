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

// ── FFI declarations ─────────────────────────────────────────────────────────
@ffi::Extern("C") __k_thread_create(runnable: Runnable*, invoke: *(Runnable*)) : NativeThread*;
@ffi::Extern("C") __k_thread_start(native: NativeThread*) : int;
@ffi::Extern("C") __k_thread_join(native: NativeThread*) : int;
@ffi::Extern("C") __k_thread_join_timed(native: NativeThread*, timeout_nanos: long) : int;
@ffi::Extern("C") __k_thread_interrupt(native: NativeThread*) : void;
@ffi::Extern("C") __k_thread_is_interrupted(native: const NativeThread*) : bool;
@ffi::Extern("C") __k_thread_interrupted_and_clear() : bool;
@ffi::Extern("C") __k_thread_sleep_nanos(nanos: long) : int;
@ffi::Extern("C") __k_thread_yield() : void;
@ffi::Extern("C") __k_thread_current_native() : NativeThread*;
@ffi::Extern("C") __k_thread_destroy(native: NativeThread*) : void;
@ffi::Extern("C") __k_thread_register_main() : NativeThread*;

// ═════════════════════════════════════════════════════════════════════════════
// Runnable — interface for tasks that can be executed by a Thread.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Runnable — a task that can be submitted for execution in a Thread.
 *
 * Implement this interface and pass the object to Thread() to create a thread
 * that calls run() when started.
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
    run() : void throws Throwable;
}

/**
 * Trampoline used by the C thread substrate to invoke a Runnable.
 *
 * The OS-level thread created in C only knows raw pointers; it cannot perform
 * a K virtual dispatch on its own. Rather than hard-coding the vtable slot of
 * `run()` in C (which would silently break whenever ::k::Object or Runnable
 * gain a virtual method), Thread passes the address of this function to
 * `__k_thread_create()`, and the C trampoline calls it back with the Runnable
 * pointer it was given. The virtual dispatch therefore always happens in K.
 *
 * Any Throwable escaping run() is swallowed here: unwinding out of this
 * function would propagate through the C trampoline frame, which is not
 * unwind-aware, and abort the whole process. Terminating just the offending
 * thread matches the interruption model, where a thread interrupted while
 * blocked is expected to unwind out of its run() body.
 *
 * @param r The runnable whose run() must be invoked.
 */
private __k_invoke_runnable(r: Runnable*) : void {
    try {
        r->run();
    } catch (e: Throwable&) {
        // Uncaught in the thread body: terminate this thread only.
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Thread — a language-level thread of execution.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Thread — represents a language-level thread backed by one OS thread.
 *
 * Usage:
 *   t : Thread!(myRunnable);
 *   t.start();
 *   // ... do other work ...
 *   t.join();
 *
 * Interruption model:
 *   - Thread.interrupt() sets the interrupted flag and wakes any blocking
 *     point (sleep, join, future wait, condition wait, lock acquisition).
 *   - Interruptible blocking methods throw ThreadInterruptionException if
 *     the flag is set when they are called or while they are waiting.
 *   - The flag persists until explicitly cleared by Thread.interrupted().
 *
 * Lifecycle:
 *   NEW → RUNNABLE (after start()) → RUNNING → TERMINATED
 *
 * This class is not thread-safe with respect to start(): start() must be
 * called at most once per Thread instance.
 */
public class Thread {
private:
    _native : NativeThread*;    // opaque KRuntimeThread*
    _owned  : bool;             // true = destructor must free native; false = borrowed

public:
    // ── Construction ─────────────────────────────────────────────────────────

    /**
     * Create a new Thread that will execute the given Runnable when started.
     * The Thread is in the NEW state until start() is called.
     *
     * @param task The runnable task to execute.
     */
    Thread(task: Runnable*) {
        _native = __k_thread_create(task, __k_invoke_runnable);
        _owned  = true;
    }

    /**
     * Internal constructor used by Thread::current() to wrap an existing
     * native handle without taking ownership.
     * @param native  The KRuntimeThread* opaque handle.
     * @param owned   Pass false when borrowing (e.g. current thread handle).
     */
    Thread(native: NativeThread*, owned: bool) {
        _native = native;
        _owned  = owned;
    }

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
     * The Runnable.run() method will be called from a new OS thread.
     * Must be called at most once.
     */
    start() : void {
        __k_thread_start(_native);
    }

    /**
     * Wait for this thread to terminate.
     *
     * @throws ThreadInterruptionException if the calling thread is interrupted
     *         while waiting.
     */
    join() : void throws ThreadInterruptionException {
        const result : int = __k_thread_join(_native);
        if (result == 1) {
            throw ThreadInterruptionException();
        }
    }

    /**
     * Wait for this thread to terminate, with a timeout.
     *
     * @param timeout Maximum time to wait.
     * @throws ThreadInterruptionException if the calling thread is interrupted.
     * @throws TimeoutException if the timeout elapses before termination.
     */
    join(timeout: const Duration&) : void throws ThreadInterruptionException, TimeoutException {
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
     * Does NOT clear the flag.
     *
     * @throws ThreadInterruptionException if interrupted.
     */
    static checkInterrupted() : void throws ThreadInterruptionException {
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
    static sleep(duration: const Duration&) : void throws ThreadInterruptionException {
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
