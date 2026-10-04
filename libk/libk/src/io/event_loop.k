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

/** A strong native endpoint, not a reference to the EventLoop wrapper. */
public class EventLoopLease : public ExecutorLease {
public:
    EventLoopLease(native: NativeExecutorLease*) : ExecutorLease(native) {}
}

/**
 * FIFO serial executor. Timers use CLOCK_MONOTONIC and a native min-heap.
 * stop/shutdown are terminal: queued tasks are dropped, the idle waiter wakes,
 * and outstanding leases remain safe but reject further submissions.
 * Borrowed Runnable arguments must outlive execution or cleanup.
 * Run/cleanup failures are contained at the C boundary and diagnosed on native
 * stderr with their task origin and Throwable code.
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
