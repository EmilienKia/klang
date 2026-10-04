# Executors and Thread Pools

> **Status:** Working Draft — 2026
> **Module:** `k` (base standard library, auto-imported)
> **Sources:** `libk/libk/src/executor.k`, `libk/libk/src/thread_exceptions.k`

This document describes the **Executor framework** in the K standard library.
Executors decouple task submission from the mechanics of how and when each task will be run,
including thread creation, scheduling, and lifecycle management.

All these types live directly at the root of the standard library namespace (`::k`),
so they are usable without qualification or `using` directives.

Related documents: [Threading and Time](threading.md), [Synchronisation Primitives](synchronization.md).

---

## 1. Overview

| Type | Kind | Description |
|------|------|-------------|
| [`Executor`](#2-executor) | `interface` | Common contract for submitting callable tasks. |
| [`DirectExecutor`](#3-directexecutor) | `class` | Executes submitted tasks synchronously in the caller's thread. |
| [`ThreadPoolExecutor`](#4-threadpoolexecutor) | `class` | Manages a fixed pool of worker threads executing priority-queued tasks. |
| [`SingleThreadExecutor`](#5-singlethreadexecutor) | `class` | Manages a single worker thread executing tasks sequentially in priority/FIFO order. |
| [`RejectedExecutionException`](#6-rejectedexecutionexception) | `class` | Thrown when a task cannot be accepted for execution. |
| [`ExecutorLease`](#8-executorlease) | `class` | Strong, shareable endpoint for task and I/O callback delivery. |

---

## 2. `Executor`

The base interface for task execution.

```k
public interface Executor {
    const execute(task: !()) : void throws(RejectedExecutionException);
    default const execute(task: !(), priority: int) : void throws(RejectedExecutionException) {
        execute(task);
    }
    acquireLease() : ExecutorLease! throws(RejectedExecutionException);
}
```

### Methods

| Method | Description |
|--------|-------------|
| `execute(task: !()) : void throws(RejectedExecutionException)` | Submits an owned callable task for execution with default priority (0). |
| `execute(task: !(), priority: int) : void throws(RejectedExecutionException)` | Submits an owned callable task with an explicit integer priority (higher value = higher priority). |
| `acquireLease() : ExecutorLease! throws(RejectedExecutionException)` | Retains an executor endpoint independently of the original K wrapper. A stopped endpoint rejects new work. |

The default `execute(command: Runnable+)` adapter contains exceptions from
`command.run()` and reports each failure once to standard error with
`origin=task.run` and the exception code. This applies when the adapter submits
to `DirectExecutor` too; the `Runnable` failure does not propagate to its caller.

---

## 3. `DirectExecutor`

Executes each submitted task synchronously within the calling thread.

Its raw callable overloads retain inline execution behavior. Execution through
an `ExecutorLease` contains task and cleanup failures at the native boundary
and reports them to standard error.

```k
public class DirectExecutor : public Executor {
public:
    DirectExecutor() -> default;
    override const execute(task: !()) : void throws(RejectedExecutionException);
    override const execute(task: !(), priority: int) : void throws(RejectedExecutionException);
    override acquireLease() : ExecutorLease! throws(RejectedExecutionException);
}
```

---

## 4. `ThreadPoolExecutor`

Manages a pool of $N$ worker threads that process tasks from an internal thread-safe priority queue.

- **Priority ordering:** Tasks with higher priority integers are dequeued before lower-priority tasks.
- **FIFO guarantee:** Tasks with identical priority are executed in the order they were submitted.
- **Fault isolation:** An uncaught exception within a task does not terminate the worker thread.

```k
public class ThreadPoolExecutor : public Executor {
public:
    ThreadPoolExecutor(poolSize: unsigned int);
    ~ThreadPoolExecutor();

    override const execute(task: !()) : void throws(RejectedExecutionException);
    override const execute(task: !(), priority: int) : void throws(RejectedExecutionException);
    override acquireLease() : ExecutorLease! throws(RejectedExecutionException);

    shutdown() : void;
    shutdownNow() : void;
    isShutdown() : bool;
    isTerminated() : bool;
    awaitTermination(timeout: Duration&) : bool throws(ThreadInterruptionException);

    getPoolSize() : unsigned int;
    getQueueSize() : unsigned int;
}
```

### Lifecycle Methods

| Method | Description |
|--------|-------------|
| `shutdown() : void` | Initiates an orderly shutdown: previously submitted tasks are executed, but new tasks are rejected. |
| `shutdownNow() : void` | Halts processing, clears waiting tasks, and interrupts active workers. |
| `isShutdown() : bool` | Returns `true` if the executor has been shut down. |
| `isTerminated() : bool` | Returns `true` if all tasks have completed following shutdown. |
| `awaitTermination(timeout: Duration&) : bool` | Blocks until all tasks complete or the timeout elapses. |

---

## 5. `SingleThreadExecutor`

A convenience subclass of `ThreadPoolExecutor` initialized with a pool size of 1.

```k
public class SingleThreadExecutor : public ThreadPoolExecutor {
public:
    SingleThreadExecutor() : ThreadPoolExecutor(1) {}
}
```

Guarantees sequential execution of tasks with at most one active task at any time.

---

## 6. `RejectedExecutionException`

Derived from `IllegalStateException` (checked exception with default error code 107).
Thrown by `execute()` when a task cannot be accepted (for example after `shutdown()`).

---

## 7. Example

```k
exec : SingleThreadExecutor;

exec.execute([]() {
    // Background work
}, 10); // Priority 10

exec.shutdown();
exec.awaitTermination(Duration::ofSeconds(5L));
```

---

## 8. `ExecutorLease`

An owning lease retains a native executor endpoint, not the K wrapper's address.
Destroying the originating wrapper must not leave a dangling callback target;
if destruction stops that executor, surviving leases reject new submissions.

| Method | Description |
|--------|-------------|
| `share() : ExecutorLease!` | Create another owning lease to the same endpoint. |
| `static system() : ExecutorLease!` | Acquire the process system callback executor; throws `RejectedExecutionException` if unavailable or permanently stopped. |
| `execute(task: !()) : void throws(RejectedExecutionException)` | Submit an owned task through this endpoint. |
| `execute(command: Runnable+) : void throws(RejectedExecutionException)` | Submit a borrowed Runnable adapter; the command must outlive execution. |

The default constructor creates an inactive lease; it must not be used as a
successful substitute when system acquisition fails. A lease consumes an owned
task on both acceptance and rejection. Its cleanup runs exactly once after
execution or task drop, including queue clearing; task/cleanup exceptions at
the native boundary are diagnosed rather than unwinding through C.

[Channel callbacks](io-async.md) accept `ExecutorLease&`. Explicit leases select
delivery, and omitted callback executors acquire `ExecutorLease::system()`.
There is no silent fallback to another executor. Later rejection is observable
through the `IoOperation` and still drops the completion capture once.
Future publication is independent of user callback dispatch.

A DirectExecutor lease runs inline on the thread dispatching that native task,
which need not be the original I/O caller. System leases use the callback pool;
EventLoop leases enqueue serial delivery. None performs the channel's transport
or DNS work. A callback must not block the native completion thread.

## See Also

- [Threading and Time](threading.md)
- [Synchronisation Primitives](synchronization.md)
- [Standard Library Index](index.md)
