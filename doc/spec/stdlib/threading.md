# Threading and Time

> **Status:** Working Draft — 2026
> **Module:** `k` (base standard library, auto-imported)
> **Sources:** `libk/libk/src/time.k`, `libk/libk/src/thread.k`,
> `libk/libk/src/thread_exceptions.k`

This document describes the *portable runtime* threading layer of the K standard
library: the time value types (`Duration`, `Instant`), the thread abstraction
(`Runnable`, `Thread`) and the exception types used by every blocking operation.

All these types live directly at the root of the standard library namespace
(`::k`), so they are usable without any qualification or `using` directive.

Related documents: [Futures and Promises](futures.md),
[Synchronisation Primitives](synchronization.md) and
[Asynchronous I/O](io-async.md), [Asynchronous Network I/O](io-network.md),
whose blocking operations all honour the same
interruption and timeout contract described here.

---

## 1. Overview

| Type | Kind | Description |
|------|------|-------------|
| [`Duration`](#2-duration) | `struct` | Immutable, signed, nanosecond-resolution time span. |
| [`Instant`](#3-instant) | `struct` | Immutable point on the wall-clock timeline, in nanoseconds since the Unix epoch. |
| [`Runnable`](#4-runnable) | `interface` | Unit of work that a thread executes. |
| [`Thread`](#5-thread) | `class` | Handle on a platform thread. |
| [`ThreadInterruptionException`](#6-exceptions) | `class` | A blocking operation was interrupted. |
| [`TimeoutException`](#6-exceptions) | `class` | A bounded blocking operation expired. |
| [`CancellationException`](#6-exceptions) | `class` | An operation was cancelled before completion. |
| [`ExecutionException`](#6-exceptions) | `class` | A task failed; wraps the original cause. |

### Design principles

- **Value semantics for time.** `Duration` and `Instant` are plain structs
  holding a single `long`; they are copied freely and never allocate.
- **Monotonic waiting.** Every timed wait is expressed as a `Duration` and is
  measured against a monotonic clock, so it is immune to wall-clock changes.
  `Instant`, in contrast, reads the real-time clock and is only meant for
  timestamps.
- **Interruption is cooperative.** Interrupting a thread sets a flag and wakes
  it up from any blocking point of the runtime. It never kills a thread.
- **Blocking operations are checked.** Every method that can block declares
  `throws ThreadInterruptionException` (plus `TimeoutException` when bounded),
  so callers must acknowledge these outcomes.

---

## 2. `Duration`

A signed span of time with nanosecond resolution, stored as a single `long`.
The representable range is roughly ±292 years.

### Construction

| Signature | Description |
|-----------|-------------|
| `Duration()` | Zero-length duration. |
| `Duration(nanos: long)` | Duration of `nanos` nanoseconds. |
| `static zero() : Duration` | Zero-length duration. |
| `static ofNanos(n: long) : Duration` | `n` nanoseconds. |
| `static ofMillis(ms: long) : Duration` | `ms` milliseconds. |
| `static ofSeconds(s: long) : Duration` | `s` seconds. |
| `static ofMinutes(m: long) : Duration` | `m` minutes. |

### Accessors

| Signature | Description |
|-----------|-------------|
| `const toNanos() : long` | Total length in nanoseconds. |
| `const toMillis() : long` | Total length truncated to whole milliseconds. |
| `const toSeconds() : long` | Total length truncated to whole seconds. |
| `const isPositive() : bool` | `true` when the duration is strictly positive. |
| `const isNegativeOrZero() : bool` | `true` when the duration is zero or negative. |

### Operators

`Duration` supports `+`, `-`, and the full set of comparisons
(`==`, `!=`, `<`, `>`, `<=`, `>=`).

```k
d : Duration = Duration::ofMillis(250L) + Duration::ofSeconds(1L);
if (d > Duration::ofSeconds(1L)) {
    // 1.25 s
}
```

> **Caveat.** Integer literals in K do not accept underscore separators; write
> `1000000L`, never `1_000_000L`.

---

## 3. `Instant`

A point on the wall-clock timeline, stored as a `long` count of nanoseconds
since the Unix epoch (1970-01-01T00:00:00Z).

| Signature | Description |
|-----------|-------------|
| `Instant()` | The epoch itself. |
| `Instant(epochNanos: long)` | Explicit epoch offset. |
| `static now() : Instant` | Current real-time clock reading. |
| `const plus(d: const Duration&) : Instant` | This instant shifted forward by `d`. |
| `const minus(other: const Instant&) : Duration` | Span from `other` to this instant. |
| `const isBefore(other: const Instant&) : bool` | Chronological comparison. |
| `const isAfter(other: const Instant&) : bool` | Chronological comparison. |
| `const toEpochNanos() : long` | Raw epoch offset. |

`Instant` also supports `==`, `!=`, `<`, `>`, `<=` and `>=`.

> `Instant` reads the **real-time** clock and can therefore jump backwards when
> the system clock is adjusted. Never use it to implement a timeout — use a
> `Duration` and the timed variants of the blocking methods instead.

---

## 4. `Runnable`

```k
public interface Runnable {
    run() : void throws Throwable;
}
```

A `Runnable` is the unit of work handed to a `Thread`. Implementations override
`run()`; the declaration allows any `Throwable`, so an implementation is free to
declare a narrower `throws` clause or none at all.

An exception that escapes `run()` **terminates that thread only**; it never
propagates into the thread that started it and never reaches the runtime's C
frames. Phase 2 (`Future`/`Promise`) introduces the machinery to observe such a
failure from another thread through `ExecutionException`.

The `Runnable` instance must stay alive for the whole execution of the thread.
The idiomatic form is an owner local that outlives the `join()`:

```k
class Job : public Runnable {
public:
    Job() {}
    override run() : void {
        Thread::sleep(Duration::ofMillis(50L));
    }
}

j : Job! = new Job();
t : Thread! = new Thread(j);
t->start();
t->join();
```

---

## 5. `Thread`

A handle on a platform thread.

### Lifecycle

| Signature | Description |
|-----------|-------------|
| `Thread(task: Runnable*)` | Create a thread bound to `task`. The thread is **not** started. |
| `~Thread()` | Wait for the thread to terminate, reap it, then release the handle. |
| `start() : void` | Start the OS thread. Calling `start()` twice is an error. |

Because the destructor waits for termination, a `Thread` owner going out of
scope while its task is still running blocks until that task returns. Interrupt
and join explicitly when you need a bounded shutdown.

### Joining

| Signature | Description |
|-----------|-------------|
| `join() : void throws ThreadInterruptionException` | Block until the thread terminates. |
| `join(timeout: const Duration&) : void throws ThreadInterruptionException, TimeoutException` | Block until the thread terminates or `timeout` elapses, in which case `TimeoutException` is thrown and the thread keeps running. |

### Interruption

| Signature | Description |
|-----------|-------------|
| `interrupt() : void` | Set this thread's interrupted flag and wake it from any blocking point. Does not terminate the thread. |
| `const isInterrupted() : bool` | Read this thread's flag without clearing it. |
| `static interrupted() : bool` | Read **and clear** the calling thread's flag. |
| `static checkInterrupted() : void throws ThreadInterruptionException` | Throw (and clear the flag) if the calling thread has been interrupted. |

A thread blocked in `Thread::sleep()` or `join()` when it is interrupted wakes
up immediately, clears its flag, and throws `ThreadInterruptionException`.

### Static utilities

| Signature | Description |
|-----------|-------------|
| `static current() : Thread!` | A non-owning `Thread` wrapper on the calling thread. Its destructor does not join anything. |
| `static sleep(duration: const Duration&) : void throws ThreadInterruptionException` | Suspend the calling thread for at least `duration`. A non-positive duration returns immediately. |
| `static yield() : void` | Hint to the scheduler that the calling thread is willing to give up the CPU. |

### Example — interrupting a sleeper

```k
class Sleeper : public Runnable {
public:
    Sleeper() {}
    override run() : void {
        try {
            Thread::sleep(Duration::ofSeconds(60L));
        } catch (e : ThreadInterruptionException&) {
            // Cooperative shutdown.
        }
    }
}

s : Sleeper! = new Sleeper();
t : Thread! = new Thread(s);
t->start();
Thread::sleep(Duration::ofMillis(20L));
t->interrupt();
t->join();
```

---

## 6. Exceptions

All four types derive from [`Exception`](exceptions.md#exception) and are
therefore **checked**: they must appear in the `throws` clause of any function
that lets them propagate.

| Type | Default code | Thrown when |
|------|--------------|-------------|
| `ThreadInterruptionException` | 101 | A blocking operation was interrupted. The interrupted flag is cleared as the exception is thrown. |
| `TimeoutException` | 102 | A bounded blocking operation expired before completing. |
| `CancellationException` | 103 | An operation was cancelled before it could complete. |
| `ExecutionException` | 104 | A task failed; the original failure is available as the exception's cause. |

Each type offers a default constructor using the code above and a
`(code: int)` constructor for callers that need a more specific code.
`ExecutionException` additionally offers `ExecutionException(cause: Throwable?)`,
which records the underlying failure.

---

## 7. Implementation notes

The K-level API is a thin façade over a small C substrate
(`libk/libk/src/runtime/`), which provides thread creation and joining, a
monotonic sleep, and a futex-based park/unpark primitive used to implement
interruptible waits. Only the K API described here is public; the C entry points
are internal to `libk` and are not part of the stable interface.

---

## See Also

- [Synchronisation Primitives](synchronization.md)
- [Futures and Promises](futures.md)
- [Exception Types](exceptions.md)
- [Object](object.md)
- [Standard Library Index](index.md)
