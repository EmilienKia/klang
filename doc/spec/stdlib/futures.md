# Futures and Promises

> **Status:** Working Draft — 2026
> **Module:** `k` (base standard library, auto-imported)
> **Sources:** `libk/libk/src/future.k`, `libk/libk/src/runtime/future_state.c`

This document describes the asynchronous result types of the K standard library:
`Future<T>`, the read side of a one-shot asynchronous computation, and
`Promise<T>`, its write side.

Both types live directly at the root of the standard library namespace (`::k`),
so they are usable without any qualification or `using` directive.

---

## 1. Overview

| Type | Kind | Description |
|------|------|-------------|
| [`Future<T>`](#3-futuret) | `struct` (template) | Read-only handle on a value that will be produced later. |
| [`Promise<T>`](#4-promiset) | `struct` (template) | Write-once producer side that completes a `Future<T>`. |

### Design principles

- **One-shot.** A future is completed exactly once, with a value, with a
  failure, or by cancellation. All later completion attempts are no-ops that
  return `false`.
- **Explicit sharing.** `Future<T>` is a handle, not a value. Copying the struct
  does *not* duplicate the shared state; call `share()` to obtain an additional
  independent handle, and destroy every handle you obtain.
- **Interruptible waiting.** Every blocking `get()` participates in the
  cooperative interruption protocol of [`Thread`](threading.md): interrupting a
  waiting thread makes `get()` throw `ThreadInterruptionException`.
- **Completion wins.** If a future completes at the same instant as the waiting
  thread is interrupted, completion is reported; the interrupted flag stays set
  so the caller can observe it later.
- **No implicit thread pool.** `Future<T>` says nothing about *where* the work
  runs. Producing a value is the caller's responsibility (typically from a
  [`Thread`](threading.md) running a `Runnable`).

---

## 2. Completion model

A future is always in exactly one of four states:

| State | `isDone()` | `isSuccess()` | `isFailed()` | `isCancelled()` |
|-------|-----------|---------------|--------------|-----------------|
| pending | `false` | `false` | `false` | `false` |
| succeeded | `true` | `true` | `false` | `false` |
| failed | `true` | `false` | `true` | `false` |
| cancelled | `true` | `false` | `false` | `true` |

The transition from *pending* to any terminal state happens at most once and is
atomic with respect to every other thread.

The corresponding integer codes are exposed as module-level constants for
diagnostics and for the C substrate: `FUTURE_PENDING`, `FUTURE_SUCCESS`,
`FUTURE_FAILED`, `FUTURE_CANCELLED`.

---

## 3. `Future<T>`

```k
template<typename T>
public final struct Future { … }
```

### Lifecycle

| Member | Description |
|--------|-------------|
| `Future()` | Creates a detached future that is permanently pending. Useful as a placeholder. |
| `~Future()` | Releases this handle's share of the state. The last handle destroys the shared payload. |
| `share() : Future<T>` | Returns an additional, independent handle on the same shared state. |

`Future<T>` is *not* implicitly copyable in a meaningful way: always obtain
additional handles through `share()`, and destroy each of them exactly once.

### Inspection

| Member | Description |
|--------|-------------|
| `isDone() : bool` | `true` once the future reached any terminal state. Never blocks. |
| `isSuccess() : bool` | `true` if the future completed with a value. |
| `isFailed() : bool` | `true` if the future completed with a throwable. |
| `isCancelled() : bool` | `true` if the future was cancelled. |

### Waiting

| Member | Description |
|--------|-------------|
| `get() : T` | Blocks until the future completes, then returns the value. |
| `get(timeout: Duration&) : T` | Same, but gives up after `timeout` elapses. |

`get()` throws:

| Exception | Condition |
|-----------|-----------|
| `ThreadInterruptionException` | The calling thread was interrupted while waiting. The interrupted flag is cleared. |
| `TimeoutException` | *(timed overload only)* `timeout` expired before completion. |
| `CancellationException` | The future was cancelled. |
| `ExecutionException` | The future failed. The original throwable is the exception's cause. |

A non-positive `timeout` polls: it either returns immediately or throws
`TimeoutException`.

### Cancellation

| Member | Description |
|--------|-------------|
| `cancel() : bool` | Attempts to move a pending future to the cancelled state. Returns `false` if it was already done. |

Cancellation is a *completion*, not an abort: it wakes every waiter and makes
them throw `CancellationException`, but it does not interrupt the producer.
Producers that want to react to cancellation should poll `isCancelled()` on
their `Promise<T>` through `isDone()`.

---

## 4. `Promise<T>`

```k
template<typename T>
public final struct Promise { … }
```

| Member | Description |
|--------|-------------|
| `Promise()` | Creates a fresh pending shared state. |
| `~Promise()` | Releases the producer's share of the state. |
| `future() : Future<T>!` | Returns a new consumer handle on the same state. May be called repeatedly. |
| `isDone() : bool` | `true` once the associated future reached a terminal state. |
| `trySuccess(value: T) : bool` | Completes with a value. Returns `false` if already done. |
| `tryFailure(error: Throwable!) : bool` | Completes with a failure. Returns `false` if already done. |
| `tryCancel() : bool` | Completes as cancelled. Returns `false` if already done. |

All three `try*` methods are atomic: exactly one of them succeeds for a given
promise, and the payload write is visible to every consumer that observes the
terminal state.

> **Ownership note.** `tryFailure()` takes ownership of the throwable. The
> `ExecutionException` raised by `get()` exposes it through `getCause()`, but
> the cause is a non-owning reference: it stays valid only while at least one
> `Future<T>` or `Promise<T>` handle on that state is alive.

---

## 5. Example

```k
class Producer : public Runnable {
    private:
    _p : Promise<int>*;
    public:
    Producer(p: Promise<int>*) { _p = p; }
    override run() : void throws Throwable {
        Thread::sleep(Duration::ofMillis(50L));
        _p->trySuccess(42);
    }
}

compute() : int throws Throwable {
    p : Promise<int>;
    f : Future<int>! = p.future();

    task : Producer! = new Producer(&p);
    runner : Runnable* = task;
    t : Thread! = new Thread(runner);
    t->start();

    v : int = f->get();      // blocks until the producer completes the promise
    t->join();
    return v;                // 42
}
```

---

## 6. Implementation notes

The shared state lives in a small C substrate
(`libk/libk/src/runtime/future_state.c`) that owns:

- an **atomic completion word** carrying the state code, written with release
  semantics and read with acquire semantics, so the K payload written before
  completion is visible to every consumer that observes the terminal state;
- an **atomic reference count** shared by every `Future<T>` / `Promise<T>`
  handle; the handle that drops it to zero destroys the K payload and the state;
- a **chain mutex** serialising producers so that the payload write and the
  completion transition are atomic with respect to one another;
- a **waiter registry** parking each waiting thread on its own futex word — the
  same word used by `Thread::interrupt()`, which is what makes `get()`
  interruptible. Threads created outside the K runtime (for example the process
  main thread) have no futex token and fall back to a short polling loop.

Only the K API described here is public; the `__k_future_*` entry points are
internal to `libk` and are not part of the stable interface.

---

## See Also

- [Threading and Time](threading.md)
- [Exception Types](exceptions.md)
- [Standard Library Index](index.md)
