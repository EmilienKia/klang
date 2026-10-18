/*
 * K Language standard library — serial executor and monotonic timers
 * Copyright 2026 Emilien Kia
 * Licensed under the Apache License, Version 2.0.
 */
module k;
namespace io {

struct NativeEventLoop {}
@ffi::Extern("C") private __k_event_loop_create() : NativeEventLoop*;
@ffi::Extern("C") private __k_event_loop_release(loop: NativeEventLoop*) : void;
@ffi::Extern("C") private __k_event_loop_lease(loop: NativeEventLoop*) : NativeExecutorLease*;
@ffi::Extern("C") private __k_event_loop_submit(loop: NativeEventLoop*, run: *(byte*),
    drop: *(byte*), arg: byte*, delay: long) : int;
@ffi::Extern("C") private __k_event_loop_run_once(loop: NativeEventLoop*, timeout: long) : int;
@ffi::Extern("C") private __k_event_loop_run(loop: NativeEventLoop*) : void;
@ffi::Extern("C") private __k_event_loop_stop(loop: NativeEventLoop*) : void;
@ffi::Extern("C") private __k_event_loop_is_running(loop: NativeEventLoop*) : int;
@ffi::Extern("C") private __k_event_loop_is_closed(loop: NativeEventLoop*) : int;

struct EventLoopCommand {
    _command : Runnable*;
    _cleanup : !();
    EventLoopCommand(command: Runnable*, cleanup: !()) {
        _command = command;
        _cleanup = cleanup;
    }
    ~EventLoopCommand() {
        if (_cleanup != null) {
            try {
                _cleanup();
            } catch (e: Throwable&) {
                __k_executor_report_failure(2, e.getCode());
            }
        }
    }
    static run(ptr: byte*) {
        carrier : EventLoopCommand* = (EventLoopCommand*) ptr;
        try {
            carrier->_command->run();
        } catch (e: Throwable&) {
            __k_executor_report_failure(1, e.getCode());
        }
    }
    static drop(ptr: byte*) {
        carrier : EventLoopCommand! = (EventLoopCommand!) ptr;
        try {
            delete carrier;
        } catch (e: Throwable&) {
            __k_executor_report_failure(2, e.getCode());
        }
    }
}

/** Strong native delivery endpoint, not a reference to an EventLoop wrapper.
 *
 * Keeping this lease alive does not keep borrowed K state alive or restart a
 * stopped loop. A surviving lease remains safe after wrapper destruction, but
 * submissions are rejected because the endpoint has been shut down.
 */
public class EventLoopLease : public ExecutorLease {
public:
    EventLoopLease(native: NativeExecutorLease*) : ExecutorLease(native) {}
}

/** Manually driven serial executor with FIFO immediate tasks and monotonic timers.
 *
 * ## Dispatch on the calling thread
 *
 * Construction does not start a worker. Submit tasks, then call runOnce or run
 * on the thread that should execute them. This is a callback executor, not a
 * readiness selector and not the engine performing channel I/O. Native I/O
 * can complete while the loop is idle, but callbacks selected onto its lease
 * need dispatch before application code observes them.
 *
 * runOnce executes at most one task and its cleanup, returning true when a
 * task ran. Untimed runOnce sleeps while idle; the Duration overload returns
 * false if no task runs before its timeout (negative durations become zero).
 * run dispatches until stop. Use only one dispatcher: recursive or concurrent
 * runOnce, or runOnce while run is active, raises IllegalStateException.
 * run does not start another dispatcher when already running or stepping.
 * isRunning describes run, not a runOnce call or the presence of queued work.
 *
 * Timers use a monotonic clock; a due timer becomes eligible for dispatch,
 * not a promise to run at an exact instant. A long task delays every other
 * task and timer. Do not block a loop task waiting for another task/callback
 * queued to this loop. I/O Future publication is independent of the loop,
 * but even such a Future join or Sync call blocks unrelated loop work.
 * Prefer submitting I/O and returning, then handling its callback.
 *
 * ## Ownership and terminal stop
 *
 * stop/shutdown are terminal: queued tasks are dropped, the idle waiter wakes,
 * and outstanding leases remain safe but reject further submissions.
 * The destructor shuts down the endpoint; an acquired lease retains native
 * endpoint storage, not this wrapper or the K objects a task may reference.
 * stop does not preempt or join a task already executing. If another thread
 * dispatches, arrange for that thread to finish before destroying borrowed
 * task state or the wrapper it calls through.
 *
 * Owning callable overloads take ownership of the callable, not arbitrary
 * objects reached through borrowed captures. Runnable arguments are borrowed
 * and must outlive execution or cleanup; submission does not delete them.
 * Optional cleanup runs after execution or when a queued/rejected task is
 * dropped, so it may run on a rejection/shutdown thread rather than the
 * dispatcher. Keep cleanup short and do not assume dispatcher affinity.
 * Run/cleanup failures are contained at the C boundary and diagnosed on native
 * stderr with their task origin and Throwable code.
 *
 * ## Example: one manual dispatch
 *
 * This complete module helper needs no worker thread or I/O runtime. The
 * Runnable owner survives runOnce and endpoint shutdown; submit borrows it.
 *
 * ```k
 * module event_loop_usage;
 *
 * class MarkDispatched : public Runnable {
 * public:
 *     done : bool = false;
 *     override run() : void { done = true; }
 * }
 *
 * dispatchOne() : bool throws(Throwable) {
 *     loop : k::io::EventLoop;
 *     task : MarkDispatched! = new MarkDispatched();
 *     loop.submit(task);
 *     loop.runOnce();
 *     completed : bool = task->done;
 *     loop.shutdown();
 *     delete task;
 *     return completed;
 * }
 * ```
 */
public class EventLoop : public Executor {
private:
    _native : NativeEventLoop*;

    enqueueCommand(command: Runnable*, cleanup: !(), delay: long) {
        if (command == null) {
            if (cleanup != null) cleanup();
            throw RejectedExecutionException();
        }
        carrier : EventLoopCommand* = new EventLoopCommand(command, cleanup);
        runFn : *(byte*) = EventLoopCommand::run;
        dropFn : *(byte*) = EventLoopCommand::drop;
        if (__k_event_loop_submit(_native, runFn, dropFn, (byte*) carrier, delay) != 0) {
            throw RejectedExecutionException();
        }
    }

public:
    EventLoop() {
        _native = __k_event_loop_create();
        if (_native == null) throw OutOfMemory();
    }
    ~EventLoop() {
        shutdown();
        __k_event_loop_release(_native);
        _native = null;
    }
    const isRunning() : bool { return __k_event_loop_is_running(_native) != 0; }
    const isShutdown() : bool { return __k_event_loop_is_closed(_native) != 0; }
    override acquireLease() : ExecutorLease! throws(RejectedExecutionException) {
        if (isShutdown()) throw RejectedExecutionException();
        return new EventLoopLease(__k_event_loop_lease(_native));
    }
    override const execute(task: !()) : void throws(RejectedExecutionException) {
        carrier : KTaskCarrier* = new KTaskCarrier(task);
        runFn : *(byte*) = KTaskCarrier::runAndDestroy;
        dropFn : *(byte*) = KTaskCarrier::dropAndDestroy;
        if (__k_event_loop_submit(_native, runFn, dropFn, (byte*) carrier, 0L) != 0) {
            throw RejectedExecutionException();
        }
    }
    override const execute(task: !(), priority: int) : void throws(RejectedExecutionException) {
        execute(task);
    }
    override execute(command: Runnable+) : void throws(RejectedExecutionException) {
        enqueueCommand(&(*command), null, 0L);
    }
    execute(command: Runnable*) : void throws(RejectedExecutionException) {
        enqueueCommand(command, null, 0L);
    }
    execute(command: Runnable*, cleanup: !()) : void throws(RejectedExecutionException) {
        enqueueCommand(command, cleanup, 0L);
    }
    submit(command: Runnable*) : void throws(RejectedExecutionException) {
        enqueueCommand(command, null, 0L);
    }
    scheduleAfter(delay: const Duration&, command: Runnable+) {
        enqueueCommand(&(*command), null, delay.toNanos());
    }
    scheduleAfter(delay: const Duration&, command: Runnable*) {
        enqueueCommand(command, null, delay.toNanos());
    }
    scheduleAfter(delay: const Duration&, command: Runnable*, cleanup: !()) {
        enqueueCommand(command, cleanup, delay.toNanos());
    }
    scheduleAfter(delay: const Duration&, task: !()) {
        carrier : KTaskCarrier* = new KTaskCarrier(task);
        runFn : *(byte*) = KTaskCarrier::runAndDestroy;
        dropFn : *(byte*) = KTaskCarrier::dropAndDestroy;
        if (__k_event_loop_submit(_native, runFn, dropFn, (byte*) carrier, delay.toNanos()) != 0) {
            throw RejectedExecutionException();
        }
    }
    runOnce() : bool {
        rc : int = __k_event_loop_run_once(_native, -1L);
        if (rc < 0) throw IllegalStateException();
        return rc != 0;
    }
    runOnce(timeout: const Duration&) : bool {
        nanos : long = timeout.toNanos();
        if (nanos < 0L) nanos = 0L;
        rc : int = __k_event_loop_run_once(_native, nanos);
        if (rc < 0) throw IllegalStateException();
        return rc != 0;
    }
    run() { __k_event_loop_run(_native); }
    stop() { __k_event_loop_stop(_native); }
    shutdown() { stop(); }
}
}
public alias EventLoop : io::EventLoop;
public alias EventLoopLease : io::EventLoopLease;
