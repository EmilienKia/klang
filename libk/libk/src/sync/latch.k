/*
 * K Language standard library — CountDownLatch and CyclicBarrier
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
 * Opaque handle to the C-level KLatch structure.
 */
public struct NativeLatch {
}

/**
 * Opaque handle to the C-level KBarrier structure.
 */
public struct NativeBarrier {
}

// ═════════════════════════════════════════════════════════════════════════════
// CountDownLatch — one-shot countdown gate.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * CountDownLatch — a one-shot gate that opens when a counter reaches zero.
 *
 * Threads calling `await()` block until the count drops to zero; once open the
 * latch stays open and every subsequent `await()` returns immediately. A latch
 * cannot be reset — use CyclicBarrier for a reusable rendezvous.
 *
 * ```k
 * latch : CountDownLatch = CountDownLatch(3L);
 * // three workers eventually call latch.countDown();
 * latch.await();     // returns once all three are done
 * ```
 *
 * A CountDownLatch owns its native state and is **not** copyable.
 */
public class CountDownLatch {
    private:
    _native : NativeLatch*;
    @ffi::Extern("C") private static __k_latch_create(count: long) : NativeLatch*;
    @ffi::Extern("C") private static __k_latch_destroy(l: NativeLatch*) : void;
    @ffi::Extern("C") private static __k_latch_await(l: NativeLatch*, timeout_nanos: long) : int;
    @ffi::Extern("C") private static __k_latch_count_down(l: NativeLatch*) : void;
    @ffi::Extern("C") private static __k_latch_count(l: const NativeLatch*) : long;

    public:
    /**
     * Construct a latch.
     * @param count Number of `countDown()` calls needed to open the latch.
     *              A count of 0 creates an already-open latch.
     */
    CountDownLatch(count: long) {
        _native = __k_latch_create(count);
    }

    /** Release the native state. The latch must not be in use. */
    ~CountDownLatch() {
        if (_native != null) {
            __k_latch_destroy(_native);
            _native = null;
        }
    }

    /**
     * Block until the count reaches zero.
     *
     * @throws ThreadInterruptionException the calling thread was interrupted;
     *         the interrupted flag is cleared.
     */
    await() : void throws(ThreadInterruptionException) {
        if (__k_latch_await(_native, -1L) == SYNC_INTERRUPTED) {
            Thread::interrupted();
            throw ThreadInterruptionException();
        }
    }

    /**
     * Block until the count reaches zero or `timeout` elapses.
     *
     * @param timeout Maximum time to wait.
     * @return true if the latch is open, false on timeout.
     * @throws ThreadInterruptionException the calling thread was interrupted;
     *         the interrupted flag is cleared.
     */
    await(timeout: Duration&) : bool throws(ThreadInterruptionException) {
        rc : int = __k_latch_await(_native, timeout.toNanos());
        if (rc == SYNC_INTERRUPTED) {
            Thread::interrupted();
            throw ThreadInterruptionException();
        }
        return rc == SYNC_OK;
    }

    /**
     * Decrement the count, waking every waiter when it reaches zero.
     * Counting down an already-open latch has no effect.
     */
    countDown() : void {
        __k_latch_count_down(_native);
    }

    /** @return the remaining count, 0 once the latch is open. */
    count() : long {
        return __k_latch_count(_native);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// CyclicBarrier — reusable N-party rendezvous.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * CyclicBarrier — a reusable rendezvous point for a fixed number of threads.
 *
 * Every thread calling `await()` blocks until `parties()` threads have arrived;
 * they are then all released together and the barrier resets itself for the
 * next round.
 *
 * If a waiting thread leaves the barrier abnormally — interrupted, timed out,
 * or through an explicit `reset()` — the current generation is *broken*: every
 * other waiter fails with BrokenBarrierException, and so does every arrival
 * until the barrier is reset. This all-or-nothing rule keeps the rendezvous
 * from silently deadlocking a subset of the participants.
 *
 * ```k
 * barrier : CyclicBarrier = CyclicBarrier(3);
 * // in each of three threads:
 * index : int = barrier.await();   // 0 for the last arrival
 * ```
 *
 * A CyclicBarrier owns its native state and is **not** copyable.
 */
public class CyclicBarrier {
    private:
    _native : NativeBarrier*;
    @ffi::Extern("C") private static __k_barrier_create(parties: int) : NativeBarrier*;
    @ffi::Extern("C") private static __k_barrier_destroy(b: NativeBarrier*) : void;
    @ffi::Extern("C") private static __k_barrier_await(b: NativeBarrier*, timeout_nanos: long) : int;
    @ffi::Extern("C") private static __k_barrier_reset(b: NativeBarrier*) : void;
    @ffi::Extern("C") private static __k_barrier_parties(b: const NativeBarrier*) : int;
    @ffi::Extern("C") private static __k_barrier_waiting(b: const NativeBarrier*) : int;
    @ffi::Extern("C") private static __k_barrier_is_broken(b: const NativeBarrier*) : int;

    public:
    /**
     * Construct a barrier.
     * @param parties Number of threads that must call `await()` before any of
     *                them is released. Clamped to at least 1.
     */
    CyclicBarrier(parties: int) {
        _native = __k_barrier_create(parties);
    }

    /** Release the native state. The barrier must not be in use. */
    ~CyclicBarrier() {
        if (_native != null) {
            __k_barrier_destroy(_native);
            _native = null;
        }
    }

    /**
     * Wait until every party has arrived.
     *
     * @return the arrival index of this thread: `parties() - 1` for the first
     *         arrival down to 0 for the last one, which is the thread that
     *         trips the barrier.
     * @throws ThreadInterruptionException the calling thread was interrupted;
     *         the interrupted flag is cleared and the barrier is broken.
     * @throws BrokenBarrierException another party left the barrier abnormally
     *         or the barrier was reset while waiting.
     */
    await() : int throws(ThreadInterruptionException, BrokenBarrierException) {
        rc : int = __k_barrier_await(_native, -1L);
        if (rc == -1) {
            Thread::interrupted();
            throw ThreadInterruptionException();
        }
        if (rc < 0) {
            throw BrokenBarrierException();
        }
        return rc;
    }

    /**
     * Wait until every party has arrived or `timeout` elapses.
     * A timeout breaks the barrier for every other participant.
     *
     * @param timeout Maximum time to wait.
     * @return the arrival index of this thread, or -1 on timeout.
     * @throws ThreadInterruptionException the calling thread was interrupted;
     *         the interrupted flag is cleared and the barrier is broken.
     * @throws BrokenBarrierException another party left the barrier abnormally
     *         or the barrier was reset while waiting.
     */
    await(timeout: Duration&) : int throws(ThreadInterruptionException, BrokenBarrierException) {
        rc : int = __k_barrier_await(_native, timeout.toNanos());
        if (rc == -1) {
            Thread::interrupted();
            throw ThreadInterruptionException();
        }
        if (rc == -2) {
            return -1;
        }
        if (rc < 0) {
            throw BrokenBarrierException();
        }
        return rc;
    }

    /**
     * Break the current generation and start a fresh one.
     * Threads currently waiting fail with BrokenBarrierException.
     */
    reset() : void {
        __k_barrier_reset(_native);
    }

    /** @return the number of parties required to trip this barrier. */
    parties() : int {
        return __k_barrier_parties(_native);
    }

    /** @return how many parties are currently waiting at the barrier. */
    numberWaiting() : int {
        return __k_barrier_waiting(_native);
    }

    /** @return true if the current generation is broken. */
    isBroken() : bool {
        return __k_barrier_is_broken(_native) != 0;
    }
}
