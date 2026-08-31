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

---

## 2. `Executor`

The base interface for task execution.

```k
public interface Executor {
    execute(task: !()) : void throws(RejectedExecutionException);
    default execute(task: !(), priority: int) : void throws(RejectedExecutionException) {
        execute(task);
    }
}
```

### Methods

| Method | Description |
|--------|-------------|
| `execute(task: !()) : void throws(RejectedExecutionException)` | Submits an owned callable task for execution with default priority (0). |
| `execute(task: !(), priority: int) : void throws(RejectedExecutionException)` | Submits an owned callable task with an explicit integer priority (higher value = higher priority). |

---

## 3. `DirectExecutor`

Executes each submitted task synchronously within the calling thread.

```k
public class DirectExecutor : public Executor {
public:
    DirectExecutor() -> default;
    override execute(task: !()) : void throws(RejectedExecutionException);
    override execute(task: !(), priority: int) : void throws(RejectedExecutionException);
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

    override execute(task: !()) : void throws(RejectedExecutionException);
    override execute(task: !(), priority: int) : void throws(RejectedExecutionException);

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

## See Also

- [Threading and Time](threading.md)
- [Synchronisation Primitives](synchronization.md)
- [Standard Library Index](index.md)
