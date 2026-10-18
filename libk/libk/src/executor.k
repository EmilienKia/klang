/*
 * K Language standard library — Executor and Thread Pool Implementations
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
// TaskPromiseRunner — internal task runner bridging !():R to Promise<R>
// ═════════════════════════════════════════════════════════════════════════════

template<typename R>
struct TaskPromiseRunner {
    _task    : !():R;
    _promise : Promise<R>;

    TaskPromiseRunner(task: !():R) {
        _task = task;
    }
    ~TaskPromiseRunner() {
        _promise.tryCancel();
        _task = null;
    }

    future() : Future<R>! {
        return _promise.future();
    }

    run() {
        try {
            res : R = _task();
            _promise.trySuccess(res);
        } catch (e: Throwable&) {
            err : Throwable! = new Exception(e.getCode());
            _promise.tryFailure(err);
        }
    }

    static invokeAndDestroy(ptr: byte*) {
        runner : TaskPromiseRunner<R>! = (TaskPromiseRunner<R>!) ptr;
        runner.run();
        delete runner;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Native FFI declarations for ExecutorLease and Task Queue
// ═════════════════════════════════════════════════════════════════════════════

public struct NativeExecutorLease {
}

struct NativeTaskQueue {
}

@ffi::Extern("C") private __k_executor_lease_retain(lease: NativeExecutorLease*) : void;
@ffi::Extern("C") private __k_executor_lease_release(lease: NativeExecutorLease*) : void;
@ffi::Extern("C") private __k_executor_lease_create_direct() : NativeExecutorLease*;
@ffi::Extern("C") private __k_executor_lease_submit(lease: NativeExecutorLease*, run: *(byte*), drop: *(byte*), arg: byte*) : int;
@ffi::Extern("C") private __k_io_system_lease() : NativeExecutorLease*;
/**
 * Non-throwing native stderr diagnostic for failures contained at a C boundary.
 * Origins: 1 task run, 2 task drop, 3 I/O decode, 4 I/O callback,
 * 5 callback drop, 6 Future publication, 7 Future drop, 8 worker dispatch.
 */
@ffi::Extern("C") private __k_executor_report_failure(origin: int, code: int) : void;

@ffi::Extern("C") private __k_native_queue_create() : NativeTaskQueue*;
@ffi::Extern("C") private __k_native_queue_retain(q: NativeTaskQueue*) : void;
@ffi::Extern("C") private __k_native_queue_release(q: NativeTaskQueue*) : void;
@ffi::Extern("C") private __k_native_queue_enqueue(q: NativeTaskQueue*, run: *(byte*), drop: *(byte*), arg: byte*, priority: int) : int;
@ffi::Extern("C") private __k_native_queue_dequeue_and_run(q: NativeTaskQueue*) : int;
@ffi::Extern("C") private __k_native_queue_clear(q: NativeTaskQueue*) : void;
@ffi::Extern("C") private __k_native_queue_close(q: NativeTaskQueue*) : void;
@ffi::Extern("C") private __k_native_queue_size(q: NativeTaskQueue*) : int;
@ffi::Extern("C") private __k_native_queue_is_closed(q: NativeTaskQueue*) : int;
@ffi::Extern("C") private __k_native_queue_get_lease(q: NativeTaskQueue*) : NativeExecutorLease*;

struct KTaskCarrier {
    _task : !();

    KTaskCarrier(task: !()) {
        _task = task;
    }
    ~KTaskCarrier() { _task = null; }

    static runAndDestroy(ptr: byte*) {
        carrier : KTaskCarrier* = (KTaskCarrier*) ptr;
        try {
            if (carrier->_task != null) carrier->_task();
        } catch (e: Throwable&) {
            __k_executor_report_failure(1, e.getCode());
        }
    }

    static dropAndDestroy(ptr: byte*) {
        carrier : KTaskCarrier! = (KTaskCarrier!) ptr;
        try {
            delete carrier;
        } catch (e: Throwable&) {
            __k_executor_report_failure(2, e.getCode());
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// ExecutorLease — refcounted lease to an executor
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Retained native execution endpoint, independent of its originating K wrapper.
 *
 * ### Endpoint lifetime is not executor availability
 *
 * A lease retains endpoint state, not the K Executor object or borrowed task
 * data. A pool or event loop can close while leases still exist; subsequent
 * submissions then throw RejectedExecutionException instead of accessing a
 * destroyed wrapper. Retaining a lease does not restart workers, keep an
 * executor accepting work, or reactivate a stopped process I/O runtime.
 *
 * Use share() for an independently owned lease; do not copy its native handle
 * as an ownership substitute. A default-constructed lease has no endpoint
 * and rejects submissions. system() selects the process-managed callback
 * executor and can itself reject when that endpoint is unavailable.
 *
 * ### Task and borrowed-command ownership
 *
 * execute(task) transfers the owned callable for execution and cleanup.
 * Rejection also disposes of it; do not retry by reusing the transferred owner.
 * execute(Runnable+) borrows its command, which must outlive queued execution.
 * A lease does not retain that Runnable.
 *
 * Task and cleanup failures cannot unwind through C: they are diagnosed on
 * native stderr with their boundary origin and Throwable code.
 *
 * ### Example: an inline endpoint outlives its DirectExecutor wrapper
 *
 * ```k
 * module executor_lease_example;
 *
 * main() : int throws(Throwable) {
 *     lease : ExecutorLease!;
 *     {
 *         executor : DirectExecutor;
 *         lease = executor.acquireLease();
 *     }
 *     lease->execute([]() { Thread::yield(); });
 *     return 0;
 * }
 * ```
 */
public class ExecutorLease {
private:
    _native : NativeExecutorLease*;

public:
    ExecutorLease() {
        _native = null;
    }

    ExecutorLease(native: NativeExecutorLease*) {
        _native = native;
    }

    ~ExecutorLease() {
        if (_native != null) {
            __k_executor_lease_release(_native);
            _native = null;
        }
    }

    const native() : NativeExecutorLease* {
        return _native;
    }

    share() : ExecutorLease! {
        if (_native != null) {
            __k_executor_lease_retain(_native);
        }
        return new ExecutorLease(_native);
    }

    static system() : ExecutorLease! {
        n : NativeExecutorLease* = __k_io_system_lease();
        if (n == null) throw RejectedExecutionException();
        return new ExecutorLease(n);
    }

    execute(command: Runnable+) : void throws(RejectedExecutionException) {
        cmd : Runnable* = &(*command);
        task : !() = [cmd]() {
            try {
                cmd->run();
            } catch (e: Throwable&) {
                __k_executor_report_failure(1, e.getCode());
            }
        };
        execute(task);
    }

    execute(task: !()) : void throws(RejectedExecutionException) {
        carrier : KTaskCarrier* = new KTaskCarrier(task);
        runFn : *(byte*) = KTaskCarrier::runAndDestroy;
        dropFn : *(byte*) = KTaskCarrier::dropAndDestroy;
        if (__k_executor_lease_submit(_native, runFn, dropFn, (byte*) carrier) != 0)
            throw RejectedExecutionException();
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Executor — common interface for task execution
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Execution policy for owned callable tasks and borrowed Runnable commands.
 *
 * Decouples task submission from the mechanics of how each task will be run,
 * including details of thread use and scheduling.
 *
 * ### Submission is not necessarily asynchronous
 *
 * DirectExecutor runs inline before execute() returns. Pool executors enqueue
 * work for their workers; a submitted task can start before submission
 * returns, and return from execute() does not imply completion.
 *
 * A successful submission transfers responsibility for the owned callable.
 * Any pointers or references captured by it still borrow their targets: keep
 * those targets alive until execution or task cleanup has ended. The
 * Runnable overload likewise borrows its command rather than taking ownership.
 *
 * ### Scheduling and rejection
 *
 * Priority is an implementation policy: the default overload ignores it.
 * ThreadPoolExecutor chooses higher priorities first among queued tasks and
 * FIFO among equal priorities; this does not preempt running tasks or order
 * completion across several workers. Closed or full native queues can reject
 * submissions. Code must handle RejectedExecutionException even when it
 * previously observed a live executor or acquired a lease.
 *
 * acquireLease() retains a native endpoint for deferred users, not this
 * wrapper. Its lifetime and shutdown contract is described by ExecutorLease.
 */
public interface Executor {
    /**
     * Executes the given task with default priority (0).
     *
     * @param task The owned task callable to execute.
     * @throws RejectedExecutionException if the executor cannot accept the task.
     */
    const execute(task: !()) : void throws(RejectedExecutionException);

    /**
     * Executes the given task with an explicit priority.
     * Higher integer values indicate higher priority.
     *
     * @param task The owned task callable to execute.
     * @param priority Task priority level.
     * @throws RejectedExecutionException if the executor cannot accept the task.
     */
    default const execute(task: !(), priority: int) : void throws(RejectedExecutionException) {
        execute(task);
    }

    /**
     * Executes the given Runnable command.
     *
     * @param command Borrowed command; keep it alive through task execution.
     * @throws RejectedExecutionException if the executor cannot accept the task.
     */
    default execute(command: Runnable+) : void throws(RejectedExecutionException) {
        cmd : Runnable* = &(*command);
        task : !() = [cmd]() {
            try {
                cmd->run();
            } catch (e: Throwable&) {
                __k_executor_report_failure(1, e.getCode());
            }
        };
        execute(task);
    }

    /**
     * Acquires a lease to this executor.
     *
     * @return An owned endpoint lease; later submissions can still be rejected.
     */
    acquireLease() : ExecutorLease! throws(RejectedExecutionException);
}

/**
 * Submits a value-returning task with an explicit priority and returns a Future
 * representing the pending result of the task.
 *
 * A task Throwable becomes an ExecutionException when the Future is read;
 * its stored cause is currently an Exception carrying the task's error code,
 * not the original throwable object or concrete type. Dropping a queued task
 * before it runs cancels its still-pending result.
 *
 * Cancelling the Future does not remove or interrupt this task: the task can
 * still run, with any later publication rejected by the terminal state.
 *
 * @param executor The executor to execute the task on.
 * @param task The owned value-returning task callable to execute.
 * @param priority Task priority level.
 * @return A Future observing the produced result or failure.
 * @throws RejectedExecutionException if the executor cannot accept the task.
 */
template<typename R>
execute(executor: const Executor&, task: !():R, priority: int) : Future<R>! throws(RejectedExecutionException) {
    runner : TaskPromiseRunner<R>! = new TaskPromiseRunner<R>(task);
    fut : Future<R>! = runner->future();
    taskWrapper : !() = [runner]() {
        runner->run();
    };

    executor.execute(taskWrapper, priority);
    return fut;
}

/**
 * Submits a value-returning task with default priority (0) and returns a Future
 * representing the pending result of the task.
 *
 * @param executor The executor to execute the task on.
 * @param task The owned value-returning task callable to execute.
 * @return A Future observing the produced result or failure.
 * @throws RejectedExecutionException if the executor cannot accept the task.
 */
template<typename R>
execute(executor: const Executor&, task: !():R) : Future<R>! throws(RejectedExecutionException) {
    return execute<R>(executor, task, 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// DirectExecutor — synchronous inline execution
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Runs each submitted task synchronously in the calling thread.
 *
 * No worker or queue is created; priority is ignored and a null callable is
 * a no-op. Direct execute(task) calls the callable normally, so its exceptions
 * propagate to the caller. By contrast, native lease execution contains and
 * diagnoses task failures at the native boundary. Value-returning execute<R>
 * captures task failure into its returned Future.
 *
 * Nested submission is ordinary nested execution, not a later queue turn.
 */
public class DirectExecutor : public Executor {
public:
    DirectExecutor() -> default;

    /**
     * Run the task immediately in the caller's thread.
     */
    override const execute(task: !()) : void throws(RejectedExecutionException) {
        if (task != null) {
            task();
        }
    }

    /**
     * Run the task immediately in the caller's thread (priority is ignored).
     */
    override const execute(task: !(), priority: int) : void throws(RejectedExecutionException) {
        execute(task);
    }

    override acquireLease() : ExecutorLease! throws(RejectedExecutionException) {
        return new ExecutorLease(__k_executor_lease_create_direct());
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Internal Priority Task Queue
// ═════════════════════════════════════════════════════════════════════════════

/**
 * TaskNode — internal singly-linked list node holding an owned priority task.
 */
struct TaskNode {
    _task     : !();
    _priority : int;
    _seq      : unsigned long;
    _next     : TaskNode! = null;

    TaskNode(task: !(), priority: int, seq: unsigned long) {
        _task = task;
        _priority = priority;
        _seq = seq;
        _next = null;
    }
}

/**
 * PriorityTaskQueue — thread-safe priority blocking queue for tasks.
 *
 * Tasks are ordered by descending priority (higher priority first).
 * Equal priority tasks are ordered FIFO based on submission sequence number.
 */
class PriorityTaskQueue {
private:
    _mutex    : Mutex;
    _notEmpty : Condition!;
    _head     : TaskNode! = null;
    _count    : unsigned int = 0;
    _seqCount : unsigned long = 0L;
    _closed   : bool = false;

public:
    PriorityTaskQueue() {
        _notEmpty = _mutex.newCondition();
    }

    ~PriorityTaskQueue() {
        clear();
        if (_notEmpty != null) {
            delete _notEmpty;
            _notEmpty = null;
        }
    }

    enqueue(task: !(), priority: int) : bool {
        if (task == null) {
            return false;
        }
        locker : MutexLocker(_mutex);
        if (_closed) {
            return false;
        }
        _seqCount = _seqCount + 1L;
        node : TaskNode! = new TaskNode(task, priority, _seqCount);

        if (_head == null || priority > _head->_priority) {
            node._next = _head;
            _head = node;
        } else {
            curr : TaskNode* = _head;
            while (curr->_next != null && curr->_next->_priority >= priority) {
                curr = curr->_next;
            }
            node._next = curr->_next;
            curr->_next = node;
        }
        _count = _count + 1;
        try {
            _notEmpty->signal();
        } catch (e: IllegalMonitorStateException&) {
        }
        return true;
    }

    dequeue() : !() {
        locker : MutexLocker(_mutex);
        while (_head == null && !_closed) {
            try {
                _notEmpty->await();
            } catch (e: ThreadInterruptionException&) {
                if (_closed) {
                    break;
                }
            } catch (e2: IllegalMonitorStateException&) {
                break;
            }
        }
        if (_head == null) {
            return null;
        }
        task : !() = _head->_task;
        next : TaskNode! = _head._next;
        _head = next;
        _count = _count - 1;
        return task;
    }

    close() : void {
        locker : MutexLocker(_mutex);
        _closed = true;
        try {
            _notEmpty->signalAll();
        } catch (e: IllegalMonitorStateException&) {
        }
    }

    clear() : void {
        locker : MutexLocker(_mutex);
        curr : TaskNode! = _head;
        _head = null;
        _count = 0;
        while (curr != null) {
            next : TaskNode! = curr._next;
            curr._next = null;
            delete curr;
            curr = next;
        }
    }

    size() : unsigned int {
        locker : MutexLocker(_mutex);
        return _count;
    }

    isEmpty() : bool {
        locker : MutexLocker(_mutex);
        return _count == 0;
    }

    isClosed() : bool {
        locker : MutexLocker(_mutex);
        return _closed;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// WorkerRunnable — thread loop for pool workers
// ═════════════════════════════════════════════════════════════════════════════

class WorkerRunnable : public Runnable {
private:
    _queue       : NativeTaskQueue*;
    _lock        : Mutex*;
    _termCond    : Condition*;
    _activeCount : unsigned int*;
    _running     : bool;

public:
    WorkerRunnable(queue: NativeTaskQueue*, lock: Mutex*, termCond: Condition*, activeCount: unsigned int*) {
        _queue = queue;
        _lock = lock;
        _termCond = termCond;
        _activeCount = activeCount;
        _running = true;
    }

    override run() : void throws(Throwable) {
        while (_running) {
            try {
                if (__k_native_queue_dequeue_and_run(_queue) == 0) {
                    break;
                }
            } catch (e: Throwable&) {
                __k_executor_report_failure(8, e.getCode());
            }
        }

        {
            locker : MutexLocker(*_lock);
            if (*_activeCount > 0) {
                *_activeCount = *_activeCount - 1;
            }
            if (*_activeCount == 0) {
                try {
                    _termCond->signalAll();
                } catch (e: IllegalMonitorStateException&) {
                }
            }
        }
    }

    stop() : void {
        _running = false;
    }
}

/**
 * WorkerNode — internal singly-linked list node holding a worker thread and runnable.
 */
struct WorkerNode {
    _worker : WorkerRunnable!;
    _thread : Thread!;
    _next   : WorkerNode! = null;

    WorkerNode(worker: WorkerRunnable!, thread: Thread!) {
        _worker = worker;
        _thread = thread;
        _next = null;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// ThreadPoolExecutor — pool of worker threads executing queued priority tasks
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Fixed pool of worker threads consuming an owned priority-task queue.
 *
 * Submitted tasks are held in an internal thread-safe priority queue.
 * Idle worker threads pull tasks from the queue as soon as they become available.
 * Higher priority is selected first among pending tasks; equal priorities are
 * FIFO. Several workers can run concurrently, so completion order can differ.
 * A requested pool size of zero is treated as one worker.
 *
 * ### Lifetime and failure boundaries
 *
 * Task callables are owned by the queue while pending and cleaned up after
 * execution or removal. Borrowed captures and Runnable commands are not kept
 * alive by the queue. Worker task failures are contained and diagnosed on
 * native stderr; use execute<R> and its Future to observe a value-task failure.
 *
 * ### Shutdown
 *
 * - shutdown() closes admission and allows already queued tasks to drain.
 * - shutdownNow() closes admission, drops pending tasks, and requests worker
 *   loops to stop. It does not interrupt or forcibly terminate a running
 *   callable; work already taken by a worker can still execute.
 * - awaitTermination() waits for worker exit; call shutdown first if orderly
 *   termination is intended. It does not initiate shutdown.
 * - Destruction requests orderly shutdown and joins workers before releasing
 *   their state. A task that never returns can therefore block destruction.
 *
 * Critical: do not destroy the pool from one of its own tasks, or block all
 * workers on results that only tasks queued to this same pool can produce.
 * Retained leases remain memory-valid after shutdown but cannot reopen the
 * closed queue. No lifecycle guarantee here extends borrowed application data.
 *
 * ### Example: consume a result, then stop accepting work
 *
 * ```k
 * module executor_example;
 *
 * main() : int throws(Throwable) {
 *     executor : SingleThreadExecutor;
 *     result : Future<int>! = executor.execute<int>(
 *         []() : int { return 42; });
 *     value : int = result->get();
 *     executor.shutdown();
 *     timeout : Duration = Duration::ofSeconds(2L);
 *     if (!executor.awaitTermination(timeout)) {
 *         return 1;
 *     }
 *     return value - 42;
 * }
 * ```
 */
public class ThreadPoolExecutor : public Executor {
protected:
    _queue         : NativeTaskQueue*;
    _workersHead   : WorkerNode! = null;
    _lock          : Mutex;
    _termCond      : Condition!;
    _activeThreads : unsigned int;
    _poolSize      : unsigned int;

public:
    /**
     * Create a thread pool with the specified number of worker threads.
     *
     * @param poolSize Number of worker threads to maintain in the pool.
     */
    ThreadPoolExecutor(poolSize: unsigned int) {
        if (poolSize == 0) {
            poolSize = 1;
        }
        _poolSize = poolSize;
        _activeThreads = poolSize;
        _termCond = _lock.newCondition();
        _queue = __k_native_queue_create();

        i : unsigned int = 0;
        while (i < poolSize) {
            worker : WorkerRunnable! = new WorkerRunnable(_queue, &_lock, _termCond, &_activeThreads);
            wPtr : WorkerRunnable* = worker;
            thread : Thread! = new Thread(wPtr);
            tPtr : Thread* = thread;
            tPtr->start();

            node : WorkerNode! = new WorkerNode(worker, thread);
            node._next = _workersHead;
            _workersHead = node;
            ++i;
        }
    }

    /**
     * Shut down and join all worker threads, releasing all resources.
     */
    ~ThreadPoolExecutor() {
        shutdown();
        curr : WorkerNode* = _workersHead;
        while (curr != null) {
            try {
                curr->_thread->join();
            } catch (e: ThreadInterruptionException&) {
            }
            curr = curr->_next;
        }
        _workersHead = null;
        if (_queue != null) {
            __k_native_queue_release(_queue);
            _queue = null;
        }
        if (_termCond != null) {
            delete _termCond;
            _termCond = null;
        }
    }

    /**
     * Execute a task with default priority (0).
     *
     * @throws RejectedExecutionException if the executor is shut down or the
     *         native queue cannot accept the task.
     */
    override const execute(task: !()) : void throws(RejectedExecutionException) {
        execute(task, 0);
    }

    /**
     * Execute a task with an explicit priority.
     * Higher integer values indicate higher priority.
     *
     * @throws RejectedExecutionException if the executor is shut down or the
     *         native queue cannot accept the task.
     */
    override const execute(task: !(), priority: int) : void throws(RejectedExecutionException) {
        if (task == null) {
            return;
        }
        if (isShutdown()) {
            throw RejectedExecutionException();
        }
        carrier : KTaskCarrier* = new KTaskCarrier(task);
        ptr : byte* = (byte*) carrier;
        runFn : *(byte*) = KTaskCarrier::runAndDestroy;
        dropFn : *(byte*) = KTaskCarrier::dropAndDestroy;
        if (__k_native_queue_enqueue(_queue, runFn, dropFn, ptr, priority) != 0) {
            throw RejectedExecutionException();
        }
    }

    /**
     * Acquires a lease to this executor.
     */
    override acquireLease() : ExecutorLease! throws(RejectedExecutionException) {
        if (isShutdown()) {
            throw RejectedExecutionException();
        }
        n : NativeExecutorLease* = __k_native_queue_get_lease(_queue);
        if (n == null) {
            throw RejectedExecutionException();
        }
        return new ExecutorLease(n);
    }

    /**
     * Initiate an orderly shutdown in which previously submitted tasks are
     * executed, but no new tasks will be accepted.
     */
    shutdown() : void {
        if (_queue != null) {
            __k_native_queue_close(_queue);
        }
    }

    /**
     * Close admission, discard pending tasks, and request worker loops to stop.
     *
     * Running tasks are neither interrupted nor forcibly terminated. A task
     * already dequeued can still run; this method does not wait for worker exit.
     */
    shutdownNow() : void {
        if (_queue != null) {
            __k_native_queue_close(_queue);
            __k_native_queue_clear(_queue);
        }
        curr : WorkerNode* = _workersHead;
        while (curr != null) {
            curr->_worker->stop();
            curr = curr->_next;
        }
    }

    /**
     * Return true if this executor has been shut down.
     */
    isShutdown() : bool {
        if (_queue == null) return true;
        return __k_native_queue_is_closed(_queue) != 0;
    }

    /**
     * Return true if all tasks have completed following shut down.
     */
    isTerminated() : bool {
        locker : MutexLocker(_lock);
        return isShutdown() && _activeThreads == 0;
    }

    /**
     * Wait for worker exit, timeout, or interruption of the calling thread.
     *
     * Does not initiate shutdown. The supplied duration is used for each
     * condition wait; repeated wakeups can extend the total elapsed wait.
     *
     * @param timeout Duration supplied to each condition wait.
     * @return true if this executor terminated, false if the timeout elapsed before termination.
     * @throws ThreadInterruptionException if interrupted while waiting.
     */
    awaitTermination(timeout: Duration&) : bool throws(ThreadInterruptionException) {
        locker : MutexLocker(_lock);
        while (_activeThreads > 0) {
            try {
                if (!_termCond->await(timeout)) {
                    return _activeThreads == 0;
                }
            } catch (e: IllegalMonitorStateException&) {
                return false;
            }
        }
        return true;
    }

    /**
     * Return the configured number of worker threads.
     */
    getPoolSize() : unsigned int {
        return _poolSize;
    }

    /**
     * Return the number of pending tasks currently in the priority queue.
     */
    getQueueSize() : unsigned int {
        if (_queue == null) return 0;
        return (unsigned int) __k_native_queue_size(_queue);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// SingleThreadExecutor — executor using a single worker thread
// ═════════════════════════════════════════════════════════════════════════════

/**
 * SingleThreadExecutor — an executor that uses a single worker thread operating
 * off an internal priority queue.
 *
 * Tasks are guaranteed to execute sequentially, and no more than one task
 * will be active at any given time.
 */
public class SingleThreadExecutor : public ThreadPoolExecutor {
public:
    /**
     * Create an executor that uses a single worker thread.
     */
    SingleThreadExecutor() : ThreadPoolExecutor(1) {}
}
