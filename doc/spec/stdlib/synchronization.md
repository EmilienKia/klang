# Synchronisation Primitives

> **Module:** `k` — automatically imported into every K program.
> **Sources:** `libk/libk/src/sync/mutex.k`, `semaphore.k`, `latch.k`, `rwlock.k`
> **Status:** Working Draft — 2026

This document describes the synchronisation layer of the K standard library:
mutual exclusion (`Mutex`, `ReentrantLock`), condition variables (`Condition`),
counting permits (`Semaphore`), and the coordination primitives
`CountDownLatch`, `CyclicBarrier` and `ReadWriteLock`.

They complement the [threading layer](threading.md) — which provides `Thread`,
`Runnable`, `Duration` and interruption — and the
[futures layer](futures.md), which provides one-shot asynchronous results.

---

## 1. Overview

| Type | Purpose |
|------|---------|
| [`Lock`](#3-lock) | Interface implemented by every mutual-exclusion primitive. |
| [`Mutex`](#4-mutex) | Plain, non-reentrant mutual exclusion. |
| [`ReentrantLock`](#5-reentrantlock) | A `Mutex` its owner may re-acquire. |
| [`Condition`](#6-condition) | Wait queue attached to a `Mutex`. |
| [`Semaphore`](#7-semaphore) | Counting permit dispenser. |
| [`CountDownLatch`](#8-countdownlatch) | One-shot gate opening when a counter reaches zero. |
| [`CyclicBarrier`](#9-cyclicbarrier) | Reusable N-party rendezvous. |
| [`ReadWriteLock`](#10-readwritelock) | Shared read mode / exclusive write mode. |

All of them own native state, are **not copyable**, and must be shared by
pointer (`*`), link (`+`) or reference (`&`) — never by value.

---

## 2. Common conventions

### 2.1 Blocking, interruption and timeouts

Every blocking operation comes in three flavours:

| Flavour | Shape | Behaviour |
|---------|-------|-----------|
| Uninterruptible | `lock()`, `readLock()`, `awaitUninterruptibly()` | Blocks until it succeeds. An interruption request issued meanwhile stays **pending** and can be observed later through `Thread::interrupted()`. |
| Interruptible | `lockInterruptibly()`, `acquire()`, `await()` | Throws `ThreadInterruptionException` if the calling thread is interrupted, **clearing** the interrupted flag. The operation has no effect: no permit is consumed, no lock is acquired. |
| Timed | `tryLock(Duration&)`, `tryAcquire(Duration&)`, `await(Duration&)` | Also interruptible. Returns `false` (or `-1` for `CyclicBarrier.await`) when the deadline expires. A non-positive `Duration` degrades to the non-blocking `try…` form. |

Only threads created through `k::Thread` are interruptible. Code running on the
process main thread (or on a thread not created by `k::Thread`) blocks
correctly but cannot be woken by `interrupt()`.

### 2.2 Wait outcome constants

The K wrappers translate the native outcome codes into return values and
exceptions, but the raw codes are also exported for advanced use:

| Constant | Value | Meaning |
|----------|-------|---------|
| `SYNC_OK` | 0 | The wait completed. |
| `SYNC_INTERRUPTED` | 1 | The calling thread was interrupted. |
| `SYNC_TIMEOUT` | 2 | The deadline expired. |
| `SYNC_ILLEGAL` | 3 | Invalid in the current state (e.g. unlocking a free lock). |
| `SYNC_BROKEN` | 4 | The barrier generation was broken. |

### 2.3 Exceptions

| Exception | Code | Raised when |
|-----------|------|-------------|
| `ThreadInterruptionException` | 101 | An interruptible wait was interrupted. |
| `IllegalMonitorStateException` | 105 | A monitor operation is attempted without holding the required lock. Derives from `IllegalStateException`. |
| `BrokenBarrierException` | 106 | A `CyclicBarrier` generation was broken by another party. |

### 2.4 The lock / try / finally idiom

Acquisitions must always be released on every exit path:

```k
m.lock();
try {
    // critical section
} finally {
    m.unlock();
}
```

---

## 3. `Lock`

```k
public interface Lock {
    lock() : void;
    lockInterruptibly() : void throws ThreadInterruptionException;
    tryLock() : bool;
    unlock() : void throws IllegalMonitorStateException;
}
```

The minimal mutual-exclusion contract. `Mutex` and `ReentrantLock` implement it,
so algorithms can be written against `Lock+` and work with either.

---

## 4. `Mutex`

A plain, non-reentrant lock. Re-locking it from the owning thread **deadlocks**;
use [`ReentrantLock`](#5-reentrantlock) when reentrancy is needed.

### 4.1 Construction

| Signature | Description |
|-----------|-------------|
| `Mutex()` | Creates a free, non-reentrant mutex. |
| `Mutex(reentrant: bool)` | *Protected.* Used by `ReentrantLock`. |
| `~Mutex()` | Releases the native state. The mutex must not be in use. |

### 4.2 Methods

| Signature | Description |
|-----------|-------------|
| `lock() : void` | Acquires the lock, blocking uninterruptibly. |
| `lockInterruptibly() : void` | Acquires the lock, throwing `ThreadInterruptionException` on interruption. |
| `tryLock() : bool` | Non-blocking acquisition. |
| `tryLock(timeout: Duration&) : bool` | Timed acquisition; `false` on timeout. |
| `unlock() : void` | Releases the lock; throws `IllegalMonitorStateException` when not held by the caller. |
| `isHeldByCurrentThread() : bool` | Ownership test. |
| `holdCount() : int` | Number of acquisitions held by the calling thread; `0` when it does not own the lock. |
| `newCondition() : Condition!` | Creates a [`Condition`](#6-condition) bound to this lock. The condition must not outlive the lock. |

### 4.3 Example

```k
public struct Counter {
    lock  : Mutex;
    value : long;
}

increment(c: Counter+) : void {
    c->lock.lock();
    try {
        c->value = c->value + 1L;
    } finally {
        c->lock.unlock();
    }
}
```

---

## 5. `ReentrantLock`

```k
public class ReentrantLock : public Mutex
```

Identical to `Mutex` except that the owning thread may acquire it several times.
`holdCount()` reports the nesting depth; the lock becomes free only when the
count drops back to zero. Every `lock()` / `tryLock()` must be matched by exactly
one `unlock()`.

```k
l : ReentrantLock;
l.lock();
l.lock();               // holdCount() == 2, no deadlock
l.unlock();
l.unlock();             // now free
```

---

## 6. `Condition`

A wait queue attached to a `Mutex`, obtained through `Mutex.newCondition()`.
The lock **must be held** when calling any method; otherwise
`IllegalMonitorStateException` is thrown.

`await()` atomically releases the lock and blocks. The lock is **always**
re-acquired before the call returns — including when it returns by throwing.

| Signature | Description |
|-----------|-------------|
| `Condition(mutex: NativeMutex*)` | Low-level constructor; prefer `Mutex.newCondition()`. |
| `await() : void` | Releases the lock and waits for a signal. |
| `await(timeout: Duration&) : bool` | Same, giving up after `timeout`; `false` on timeout. |
| `awaitUninterruptibly() : void` | Same as `await()` but ignores interruption (which stays pending). |
| `signal() : void` | Wakes one waiter. |
| `signalAll() : void` | Wakes every waiter. |

Wake-ups may be spurious, so **always wait inside a loop** re-testing the
predicate:

```k
m.lock();
try {
    while (!ready) {
        cond->await();
    }
    // `ready` is true and the lock is held here
} finally {
    m.unlock();
}
```

---

## 7. `Semaphore`

A counting permit dispenser. Permits are **not** tied to the thread that took
them: one thread may acquire and another release, which makes `Semaphore`
suitable both for bounding concurrency and for producer/consumer signalling.

| Signature | Description |
|-----------|-------------|
| `Semaphore()` | Creates a semaphore with no permit. |
| `Semaphore(permits: int)` | Creates a semaphore with `permits` permits (clamped to 0). |
| `acquire() : void` | Takes one permit, blocking; interruptible. |
| `acquire(permits: int) : void` | Takes `permits` permits **atomically**. |
| `tryAcquire() : bool` | Non-blocking single-permit acquisition. |
| `tryAcquire(permits: int) : bool` | Non-blocking, all-or-nothing. |
| `tryAcquire(timeout: Duration&) : bool` | Timed single-permit acquisition. |
| `tryAcquire(permits: int, timeout: Duration&) : bool` | Timed, all-or-nothing. |
| `release() : void` | Gives one permit back. |
| `release(permits: int) : void` | Gives several permits back at once. |
| `availablePermits() : int` | Snapshot of the free permits — diagnostics only. |
| `drainPermits() : int` | Atomically takes every available permit and returns how many. |

Multi-permit acquisition is all-or-nothing: a thread asking for 2 permits from a
semaphore holding 1 blocks (or fails) rather than taking the single permit.

```k
sem : Semaphore(2);          // at most two threads in the section
sem.acquire();
try {
    // ...
} finally {
    sem.release();
}
```

---

## 8. `CountDownLatch`

A **one-shot** gate. Threads calling `await()` block until the count reaches
zero; once open, the latch stays open forever and every later `await()` returns
immediately. It cannot be reset — use [`CyclicBarrier`](#9-cyclicbarrier) for a
reusable rendezvous.

| Signature | Description |
|-----------|-------------|
| `CountDownLatch(count: long)` | Creates a latch needing `count` count-downs. `0` creates an already-open latch. |
| `await() : void` | Blocks until the latch opens; interruptible. |
| `await(timeout: Duration&) : bool` | Same with a deadline; `false` on timeout. |
| `countDown() : void` | Decrements the count, waking every waiter when it reaches zero. A no-op on an open latch — the count never goes negative. |
| `count() : long` | Remaining count. |

```k
done : CountDownLatch(3L);
// each of three workers ends with done.countDown();
done.await();               // returns when all three finished
```

---

## 9. `CyclicBarrier`

A **reusable** rendezvous for a fixed number of threads. Each `await()` blocks
until `parties()` threads have arrived; they are then released together and the
barrier resets for the next generation.

| Signature | Description |
|-----------|-------------|
| `CyclicBarrier(parties: int)` | Creates a barrier for `parties` threads (clamped to at least 1). |
| `await() : int` | Waits for every party. Returns the arrival index: `parties() - 1` for the first arrival down to `0` for the last one, which trips the barrier. |
| `await(timeout: Duration&) : int` | Same with a deadline; returns `-1` on timeout. |
| `reset() : void` | Breaks the current generation and starts a fresh one. |
| `parties() : int` | Number of parties required. |
| `numberWaiting() : int` | How many parties are currently waiting. |
| `isBroken() : bool` | Whether the current generation is broken. |

### 9.1 Breakage

A barrier is an all-or-nothing rendezvous: if any waiter leaves abnormally the
others can never be released, so the generation is marked **broken**. This
happens on interruption, on timeout, and on `reset()`.

- The party that left abnormally sees `ThreadInterruptionException` (interrupt)
  or `-1` (timeout).
- Every other waiter, and every arrival until the barrier is reset, gets
  `BrokenBarrierException`.
- `reset()` clears the broken state and starts a fresh generation.

```k
barrier : CyclicBarrier(3);
// in each of three threads:
index : int = barrier.await();     // 0 for the last arrival
```

---

## 10. `ReadWriteLock`

A lock with a **shared read mode** and an **exclusive write mode**. Any number of
threads may hold the read lock while no thread holds the write lock; the write
lock excludes both readers and other writers.

The implementation is **writer-preferring**: a reader arriving while a writer is
queued waits behind it, so a steady stream of readers cannot starve a writer.

The lock is **not reentrant** and does **not** support upgrading a read hold into
a write hold — attempting either deadlocks.

| Signature | Description |
|-----------|-------------|
| `ReadWriteLock()` | Creates a free lock. |
| `readLock() : void` | Acquires a shared hold, blocking uninterruptibly. |
| `readLockInterruptibly() : void` | Same, interruptible. |
| `tryReadLock() : bool` | Non-blocking shared acquisition. |
| `tryReadLock(timeout: Duration&) : bool` | Timed shared acquisition. |
| `readUnlock() : void` | Releases one shared hold; throws `IllegalMonitorStateException` when none is held. |
| `writeLock() : void` | Acquires the exclusive hold, blocking uninterruptibly. |
| `writeLockInterruptibly() : void` | Same, interruptible. |
| `tryWriteLock() : bool` | Non-blocking exclusive acquisition. |
| `tryWriteLock(timeout: Duration&) : bool` | Timed exclusive acquisition. |
| `writeUnlock() : void` | Releases the exclusive hold; throws `IllegalMonitorStateException` when not held. |
| `readCount() : int` | Snapshot of the active shared holds. |
| `isWriteLocked() : bool` | Whether some thread holds the write lock. |

```k
lock.readLock();
try {
    // shared access — several readers may be here at once
} finally {
    lock.readUnlock();
}
```

---

## 11. Choosing a primitive

| Need | Primitive |
|------|-----------|
| Protect a small critical section | `Mutex` |
| Protect a section entered recursively | `ReentrantLock` |
| Wait for a state change under a lock | `Mutex` + `Condition` |
| Bound how many threads enter a section | `Semaphore` |
| Hand a signal from one thread to another | `Semaphore` |
| Wait for N one-off events | `CountDownLatch` |
| Repeatedly rendezvous the same N threads | `CyclicBarrier` |
| Read-mostly shared data | `ReadWriteLock` |
| Deliver a single asynchronous value | [`Future<T>` / `Promise<T>`](futures.md) |

---

## 12. Implementation notes

The K API is a thin façade over a C substrate
(`libk/libk/src/runtime/sync_primitives.c`, exposed through
`runtime/sync_ffi.c`). Every primitive is built on the same *park lot*: a
`pthread_mutex_t` guarding both the primitive state and the list of blocked
runtime threads. A blocked thread parks on the futex word of its own interrupt
token — the very word `Thread.interrupt()` bumps — so interruptibility and
timeouts come for free and uniformly.

Because that token belongs to `k::Thread`, a thread not created by `k::Thread`
(such as the process main thread) has none; it falls back to a short polling
loop, which blocks correctly but cannot be interrupted.

Only the K API described here is public; the C entry points and the
`Native*` opaque structs are internal to `libk` and are not part of the stable
interface.

---

## See Also

- [Threading and Time](threading.md)
- [Futures and Promises](futures.md)
- [Exception Types](exceptions.md)
- [Standard Library Index](index.md)
