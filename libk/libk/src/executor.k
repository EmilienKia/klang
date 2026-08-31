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
// Executor — common interface for task execution
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Executor — an object that executes submitted callable tasks.
 *
 * Decouples task submission from the mechanics of how each task will be run,
 * including details of thread use, scheduling, etc.
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
}

/**
 * Submits a value-returning task with an explicit priority and returns a Future
 * representing the pending result of the task.
 *
 * @param executor The executor to execute the task on.
 * @param task The owned value-returning task callable to execute.
 * @param priority Task priority level.
 * @return A Future observing the produced result or failure.
 * @throws RejectedExecutionException if the executor cannot accept the task.
 */
template<typename R>
execute(executor: const Executor&, task: !():R, priority: int) : Future<R>! throws(RejectedExecutionException) {
    runnerPtr : TaskPromiseRunner<R>* = new TaskPromiseRunner<R>(task);
    fut : Future<R>! = runnerPtr->future();
    ptr : byte* = (byte*) runnerPtr;
    invoker : *(byte*) = TaskPromiseRunner<R>::invokeAndDestroy;

    taskWrapper : !() = [invoker, ptr]() {
        invoker(ptr);
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
 * DirectExecutor — executes each submitted task synchronously in the calling thread.
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
        _head = null;
        _count = 0;
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
    _queue       : PriorityTaskQueue*;
    _lock        : Mutex*;
    _termCond    : Condition*;
    _activeCount : unsigned int*;
    _running     : bool;

public:
    WorkerRunnable(queue: PriorityTaskQueue*, lock: Mutex*, termCond: Condition*, activeCount: unsigned int*) {
        _queue = queue;
        _lock = lock;
        _termCond = termCond;
        _activeCount = activeCount;
        _running = true;
    }

    override run() : void throws(Throwable) {
        while (_running) {
            task : !() = _queue->dequeue();
            if (task == null) {
                break;
            }
            try {
                task();
            } catch (e: Throwable&) {
                // Task exceptions are isolated to keep the worker thread alive
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
 * ThreadPoolExecutor — manages a pool of worker threads to execute tasks.
 *
 * Submitted tasks are held in an internal thread-safe priority queue.
 * Idle worker threads pull tasks from the queue as soon as they become available.
 */
public class ThreadPoolExecutor : public Executor {
protected:
    _queue         : PriorityTaskQueue;
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

        i : unsigned int = 0;
        while (i < poolSize) {
            worker : WorkerRunnable! = new WorkerRunnable(&_queue, &_lock, _termCond, &_activeThreads);
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
        if (_termCond != null) {
            delete _termCond;
            _termCond = null;
        }
    }

    /**
     * Execute a task with default priority (0).
     *
     * @throws RejectedExecutionException if the executor has been shut down.
     */
    override const execute(task: !()) : void throws(RejectedExecutionException) {
        execute(task, 0);
    }

    /**
     * Execute a task with an explicit priority.
     * Higher integer values indicate higher priority.
     *
     * @throws RejectedExecutionException if the executor has been shut down.
     */
    override const execute(task: !(), priority: int) : void throws(RejectedExecutionException) {
        if (task == null) {
            return;
        }
        if (isShutdown()) {
            throw RejectedExecutionException();
        }
        if (!_queue.enqueue(task, priority)) {
            throw RejectedExecutionException();
        }
    }

    /**
     * Initiate an orderly shutdown in which previously submitted tasks are
     * executed, but no new tasks will be accepted.
     */
    shutdown() : void {
        _queue.close();
    }

    /**
     * Attempt to stop all actively executing tasks and halt the processing
     * of waiting tasks.
     */
    shutdownNow() : void {
        _queue.close();
        _queue.clear();
        curr : WorkerNode* = _workersHead;
        while (curr != null) {
            curr->_worker->stop();
            curr->_thread->interrupt();
            curr = curr->_next;
        }
    }

    /**
     * Return true if this executor has been shut down.
     */
    isShutdown() : bool {
        return _queue.isClosed();
    }

    /**
     * Return true if all tasks have completed following shut down.
     */
    isTerminated() : bool {
        locker : MutexLocker(_lock);
        return _queue.isClosed() && _activeThreads == 0;
    }

    /**
     * Block until all tasks have completed execution after a shutdown request,
     * or the timeout occurs, or the current thread is interrupted, whichever happens first.
     *
     * @param timeout Maximum duration to wait.
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
        return _queue.size();
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
