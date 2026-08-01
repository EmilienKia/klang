**Portable Threading, Synchronization, and I/O Runtime**

Detailed Implementation Design Specification

| **Field** | **Value** |
|:---|:---|
| Language target | Native high-level compiled language with Java-like threading and I/O semantics |
| Primary Linux backend | io_uring-based asynchronous substrate |
| Primary Windows target | I/O Completion Ports with overlapped I/O |
| API style | Java-like Thread, InputStream, OutputStream, Socket, Channel, Future, Lock, Condition |
| Document status | Draft specification for implementation - expanded Layer 1 details |
| Version | 0.2 |
| Date | 2026-07-02 |

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th><p><strong>Normative intent</strong></p>
<p>This document defines the runtime design, API contracts, implementation layers, cancellation and interruption semantics, error model, and platform mapping required to implement a portable standard library. The wording is intentionally technical and direct. It is not a tutorial.</p></th>
</tr>
</thead>
<tbody>
</tbody>
</table>

# Table of Contents

- 1\. Scope and design drivers

- 2\. Terminology and normative rules

- 3\. Architecture overview

- 4\. Cross-platform interruption and cancellation model

- 5\. Layer 1: asynchronous substrate

- 6\. Layer 1A: Linux io_uring backend

- 7\. Layer 1B: Windows native backend target

- 8\. Layer 1C: runtime threading substrate

- 9\. Layer 1D: futures, promises, and completion model

- 10\. Layer 2: synchronous facade

- 11\. Layer 2A: synchronous threading and synchronization API

- 12\. Layer 2B: synchronous I/O API

- 13\. Layer 2C: exception and error model

- 14\. Layer 3: proactor and reactor layer

- 15\. Resource ownership and lifetime rules

- 16\. State machines and race handling

- 17\. Memory model and thread-safety rules

- 18\. Performance and scalability policy

- 19\. Testing and verification strategy

- 20\. Implementation roadmap

- Appendix A. API sketches

- Appendix B. Platform mapping

- Appendix C. References

# 1. Scope and Design Drivers

The standard library shall expose a portable, high-level threading and I/O API with Java-like behavior while remaining native, efficient, and explicit about cancellation. The API shall not expose POSIX or Win32 concepts directly to language users. The runtime shall own the mapping from language operations to platform-specific mechanisms.

- **Primary goal:** provide a coherent Thread.interrupt() semantics across blocking synchronization, socket I/O, file I/O, stream I/O, channel selection, sleep, and future waiting.

- **Primary Linux strategy:** build all interruptible I/O on an asynchronous io_uring substrate, then layer synchronous APIs over it.

- **Primary Windows strategy:** design the abstraction so a native Windows backend can use overlapped I/O, I/O Completion Ports, waitable objects, WaitOnAddress, and CancelIoEx without changing public APIs.

- **Design constraint:** never implement language-level interruption by killing an operating system thread asynchronously.

- **Design constraint:** never use file descriptor or handle close as the normal implementation of Thread.interrupt(). Resource close and thread interruption are separate concepts.

- **Design constraint:** all public blocking APIs in the standard library must pass through runtime-defined interruptible blocking points.

## 1.1 Non-goals

- It is not a goal to exactly emulate POSIX pthread cancellation.

- It is not a goal to make arbitrary foreign native code interruptible.

- It is not a goal to guarantee immediate cancellation of an I/O operation after it has already completed or crossed an uncancelable kernel or driver boundary.

- It is not a goal to expose io_uring or IOCP directly in the default language API.

# 2. Terminology and Normative Rules

| **Term** | **Definition** |
|:---|:---|
| Interruption | A cooperative, language-level request targeted at a RuntimeThread. It sets an interrupted flag, wakes interruptible waits, and causes selected public APIs to throw ThreadInterruptionException. |
| Cancellation | A request to stop a specific runtime operation, such as an io_uring SQE or Windows overlapped operation. Cancellation is operation-scoped and inherently race-prone. |
| Blocking point | A runtime-controlled region where the current thread may suspend until a condition, completion, timeout, or interruption occurs. |
| Async operation | An object representing one submitted I/O, timer, poll, cancellation, or control operation in Layer 1. |
| Completion | The final result of an async operation. It may represent success, failure, cancellation, timeout, or partial transfer. |
| Proactor | A model where the runtime submits operations and later receives completions. |
| Reactor | A model where the runtime observes readiness events and user code decides which operation to perform next. |

## 2.1 Normative keywords

The terms MUST, MUST NOT, SHOULD, SHOULD NOT, and MAY are normative. MUST means required for conformance. SHOULD means strongly recommended unless a documented platform limitation justifies deviation.

# 3. Architecture Overview

Public standard library API  
Thread, Runnable, Future, Promise, Lock, Condition, Semaphore  
InputStream, OutputStream, FileChannel, Socket, ServerSocket, Selector  
  
Layer 3: proactor and reactor layer  
EventLoop, CompletionPort, Selector, ChannelRegistration, Scheduler  
Converts completions into callbacks, tasks, futures, or readiness events  
  
Layer 2: synchronous facade  
Blocking Thread, Stream, Socket, Channel, Future.get, Condition.await  
Implemented by submitting Layer 1 operations and waiting interruptibly  
  
Layer 1: asynchronous substrate  
AsyncOperation, AsyncHandle, Completion, CancellationToken, Timeout  
Linux: io_uring  
Windows: IOCP plus overlapped I/O

<table>
<caption><p>Figure 1. Three-layer runtime architecture.</p></caption>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th><p><strong>Core architectural rule</strong></p>
<p>Synchronous APIs are not implemented with blocking system calls. They are implemented by submitting asynchronous operations to Layer 1 and waiting through a runtime blocking point that can observe interruption, timeout, completion, and resource close.</p></th>
</tr>
</thead>
<tbody>
</tbody>
</table>

## 3.1 Layer responsibility summary

| **Layer** | **Responsibility** | **Examples** |
|:---|:---|:---|
| Layer 1 | Portable asynchronous operation substrate and platform backend adapters. | AsyncRead, AsyncWrite, AsyncAccept, AsyncConnect, AsyncSleep, AsyncCancel, CompletionQueue. |
| Layer 2 | User-facing synchronous API that reuses Layer 1 for all blocking work. | InputStream.read(), Socket.write(), Thread.sleep(), Condition.await(), Future.get(). |
| Layer 3 | Higher-level proactor/reactor API, scheduler integration, and scalable event dispatch. | EventLoop.run(), Selector.select(), Channel.onReadable(), CompletionHandler. |

# 4. Cross-Platform Interruption and Cancellation Model

The runtime shall distinguish thread interruption from operation cancellation. A thread may be interrupted even when no I/O is active. An operation may be canceled even if its owning thread is not interrupted. Thread interruption often triggers operation cancellation, but the two concepts remain separate in the runtime model.

## 4.1 Thread interruption contract

- Thread.interrupt(target) MUST atomically set target.interrupted to true with release semantics.

- Thread.interrupt(target) MUST wake target if it is blocked in a runtime interruptible wait.

- Thread.interrupt(target) MUST request cancellation of the target thread current interruptible I/O operation when such an operation exists and when the backend supports targeted cancellation.

- Thread.interrupt(target) MUST NOT terminate the OS thread.

- Thread.interrupt(target) MUST NOT unlock mutexes owned by target.

- Thread.interrupt(target) MUST NOT close files, sockets, pipes, handles, or descriptors as a normal interruption mechanism.

- An interrupted thread MUST observe ThreadInterruptionException at the next interruptible blocking point, or when it explicitly calls Thread.checkInterrupted().

## 4.2 Operation cancellation contract

- Cancellation is best-effort in the platform backend. The operation may complete successfully before cancellation takes effect.

- Applications and runtime code MUST handle success, cancellation, timeout, and failure as valid outcomes after a cancellation request.

- Cancellation completion order MUST NOT be assumed. The cancellation request and the canceled operation may complete in either order.

- The runtime MUST keep operation metadata, buffers, user callbacks, and native descriptors alive until the backend has delivered a terminal completion for that operation.

## 4.3 Interruptible API behavior matrix

| **API** | **Before operation starts** | **Operation pending** | **After partial transfer** | **After completed transfer** |
|:---|:---|:---|:---|:---|
| Thread.sleep() | Throw ThreadInterruptionException. | Wake and throw ThreadInterruptionException. | Not applicable. | Return normally if timeout elapsed before interrupt was observed. |
| Condition.await() | Throw ThreadInterruptionException after reacquiring lock according to condition contract. | Wake, reacquire lock, remove waiter, then throw. | Not applicable. | Return normally if signaled before interrupt was observed. |
| InputStream.read() | Throw ThreadInterruptionException. | Cancel pending read if possible and throw after terminal completion. | Return bytes transferred or throw InterruptedIOException with count, depending on method contract. | Return bytes normally. |
| OutputStream.write() | Throw ThreadInterruptionException. | Cancel pending write if possible and throw after terminal completion. | Return count for low-level write or throw InterruptedIOException with count for writeFully. | Return normally. |
| Future.get() | Throw ThreadInterruptionException. | Wake waiter and throw unless the future completed first. | Not applicable. | Return result or throw future failure. |
| Selector.select() | Throw ThreadInterruptionException. | Wake selector and throw unless readiness/completion won the race. | Not applicable. | Return selected keys normally. |

# 5. Layer 1: Asynchronous Substrate

Layer 1 is the only layer allowed to talk directly to platform asynchronous I/O facilities. It provides a portable proactor-shaped API. It must be expressive enough to map to Linux io_uring and Windows IOCP without forcing either platform to behave like the other.

## 5.1 Core Layer 1 interfaces

| **Interface or class** | **Purpose** | **Mandatory properties** |
|:---|:---|:---|
| AsyncRuntime | Owns backend initialization, worker rings or ports, shared registries, and shutdown. | start(), stop(), submit(), cancel(), drain(), backendCapabilities(). |
| AsyncOperation | Represents a submitted or submit-ready operation. | operationId, ownerThread, kind, state, deadline, cancelPolicy, completionTarget. |
| AsyncHandle | Portable wrapper over a file, socket, pipe, device, or platform handle. | nativeHandle, capabilities, blockingModePolicy, closeState, reference count. |
| Completion | Terminal result delivered by backend. | operationId, resultKind, errorCode, bytesTransferred, flags, backendStatus. |
| CancellationToken | Operation-scoped cancellation handle. | requestCancel(), isCancellationRequested(), reason. |
| InterruptToken | Thread-scoped interruption token. | isInterrupted(), wake(), currentOperation. |
| CompletionQueue | Receives completions from backend and dispatches to waiters, futures, or event loops. | push(), poll(), wait(), postSynthetic(), close(). |
| BufferLease | Owns or borrows memory used by async operations. | address, length, ownership, pinState, releaseAfterCompletion. |

## 5.2 Required operation types

| **Category** | **Operations** |
|:---|:---|
| File I/O | open, close, read, write, readAt, writeAt, fsync, stat, truncate, rename, unlink. |
| Socket I/O | socket, bind, listen, accept, connect, recv, send, recvMsg, sendMsg, shutdown. |
| Pipe and stream I/O | read, write, splice or transfer when available, close. |
| Timer | sleep, delay, timeout, cancelTimeout. |
| Poll and readiness | pollAdd, pollRemove, readiness notification for reactor facade. |
| Cancellation | cancelByOperationId, cancelByHandle, cancelAllOwnedByThread, cancelAllInScope. |
| Runtime control | nop, wake, registerBuffer, unregisterBuffer, registerHandle, unregisterHandle. |
| Cross-loop messaging | postCompletion, wakeEventLoop, messageRing or equivalent when available. |

## 5.3 Operation lifecycle

NEW  
-\> PREPARED  
-\> SUBMITTED  
-\> IN_FLIGHT  
-\> CANCEL_REQUESTED optional, may race with completion  
-\> COMPLETED success, failure, canceled, timeout, closed  
-\> DELIVERED completion consumed by waiter, future, or event loop  
-\> RELEASED buffers and native structures may be reclaimed

- NEW operations have no native state and may be discarded freely.

- SUBMITTED operations own or reference native state and MUST be completed or explicitly abandoned during runtime shutdown.

- COMPLETED operations MUST contain a stable result independent of native CQE, OVERLAPPED, or backend-specific memory lifetime.

- RELEASED is the only state in which buffers marked releaseAfterCompletion may be freed.

## 5.4 Completion result model

| **Result kind** | **Meaning** | **Typical mapping** |
|:---|:---|:---|
| Success | Operation completed as requested. | res \>= 0 on Linux, TRUE plus byte count on Windows. |
| PartialSuccess | Some bytes transferred but operation-level semantic is incomplete. | short read, short write, partial datagram behavior if applicable. |
| WouldBlock | Nonterminal readiness condition for reactor or retry loops. | EAGAIN or EWOULDBLOCK where operation is configured as nonblocking. |
| Canceled | Operation was canceled before natural completion. | -ECANCELED, -EINTR, ERROR_OPERATION_ABORTED. |
| Timeout | Operation deadline expired. | IORING_OP_TIMEOUT completion, timer completion, WAIT_TIMEOUT. |
| Closed | Handle closed or peer closed. | EOF, ECONNRESET, ERROR_BROKEN_PIPE, graceful socket close. |
| Failure | Backend error not otherwise classified. | errno, GetLastError, WSA error code. |

## 5.5 Backend capability discovery

The runtime MUST query or configure backend capabilities at startup and expose them internally. Public behavior must remain stable across capability differences. Feature differences affect implementation strategy, not API contracts.

| **Capability** | **Examples** |
|:---|:---|
| supportsAsyncFileRead | io_uring read operations on regular files; Windows overlapped ReadFile. |
| supportsAsyncSocketRead | io_uring recv; Windows overlapped WSARecv. |
| supportsTargetedCancel | io_uring cancel by user_data; CancelIoEx by OVERLAPPED. |
| supportsTimeoutOperation | io_uring timeout operations; Windows waitable timers or IOCP timer integration. |
| supportsRegisteredBuffers | io_uring fixed buffers; Windows may use normal pinned buffers or registered I/O when supported. |
| supportsReadinessPolling | io_uring poll add; epoll fallback if enabled; Windows readiness facade over IOCP or Winsock events. |
| supportsCrossLoopMessage | io_uring msg_ring; IOCP PostQueuedCompletionStatus. |

# 6. Layer 1A: Linux io_uring Backend

The Linux backend shall use io_uring as the primary asynchronous I/O mechanism. The backend may use eventfd, futex, epoll, or worker threads for compatibility cases, but those mechanisms must not define public semantics.

## 6.1 Ring topology

| **Topology** | **Description** | **Recommended use** |
|:---|:---|:---|
| One ring per I/O worker | Each kernel worker thread or runtime carrier owns one ring and processes completions locally. | Default. Reduces contention and improves cache locality. |
| One global ring | All threads submit to one shared ring. | Simpler bootstrap or low-throughput applications. Requires stronger synchronization around SQEs. |
| One ring per scheduler shard | A scheduler shard owns a ring, a completion queue, and a runnable task queue. | Preferred for future virtual threads, fibers, or coroutine scheduling. |
| Dedicated file ring and network ring | Separate rings isolate slow file operations from latency-sensitive network operations. | Optional advanced policy for servers. |

## 6.2 Submission model

- Every public async operation maps to one or more SQEs.

- Each SQE MUST carry a unique operationId in user_data. The operationId indexes a runtime OperationRegistry entry.

- Multiple SQEs MAY represent one logical operation, for example read with linked timeout, connect with deadline, or cancel plus cleanup.

- Batching SHOULD be used when multiple operations are ready before a submit syscall is required.

- The backend MUST preserve sufficient operation metadata to interpret CQE.res without relying on the original public API call stack.

## 6.3 Completion model

- Every submitted SQE is expected to produce a CQE unless the ring is torn down under fatal runtime shutdown.

- cqe.res MUST be translated into the portable Completion model.

- cqe.user_data MUST be resolved through the OperationRegistry. Unknown user_data is a runtime integrity error unless it is known to represent a backend-generated sentinel.

- The completion dispatcher MUST be able to deliver a completion to exactly one of: blocking waiter, Future/Promise, EventLoop, Selector, or internal cleanup handler.

## 6.4 Cancellation with io_uring

Cancellation shall use io_uring asynchronous cancel operations wherever possible. The normal target is operationId in user_data. Cancellation by file descriptor may be used for close, shutdown, or scope cleanup.

interrupt(thread):  
thread.interrupted.store(true, release)  
wake_thread_blocking_point(thread)  
op = thread.currentInterruptibleOperation.load(acquire)  
if op != null:  
submit_async_cancel(op.operationId)

- The canceled operation completion and the cancellation request completion MUST both be consumed.

- If cancellation returns no matching request, the runtime MUST check whether the original operation already completed.

- If the original operation completes successfully before cancellation, the synchronous facade decides whether to return success or throw based on the race policy for that API.

- If the original operation completes with ECANCELED or EINTR and the owner thread interruption flag is set, the synchronous facade SHOULD throw ThreadInterruptionException or InterruptedIOException as appropriate.

## 6.5 Timeouts and deadlines

- Layer 1 MUST support absolute deadlines and relative timeouts at the operation model level.

- Linux SHOULD implement timeouts using io_uring timeout operations or linked timeouts when the timeout is semantically tied to a specific operation.

- A timeout completion MUST be distinguishable from thread interruption and explicit cancellation.

- When a timeout cancels another operation, both completion paths MUST be consumed and correlated.

## 6.6 Files

- Regular file reads and writes SHOULD use io_uring read and write operations rather than epoll readiness.

- File offsets MUST be explicit in FileChannel operations. Stream operations MAY maintain a runtime file position guarded by the stream object lock.

- Blocking file methods in Layer 2 MUST submit Layer 1 async file operations and wait interruptibly.

- fsync and metadata operations MUST be modeled as async operations if the backend supports them.

## 6.7 Sockets

- Socket read and write SHOULD use io_uring recv, send, recvmsg, sendmsg, or read/write as appropriate for the descriptor type.

- Accept loops SHOULD use multishot accept where available only if the runtime can preserve per-accept cancellation and resource ownership semantics.

- Connection close MUST cancel pending operations associated with the socket before the descriptor is released.

- Socket.shutdown() is a resource operation and MUST NOT be used as the generic implementation of Thread.interrupt().

## 6.8 Buffer management

| **Buffer kind** | **Use** | **Lifetime rule** |
|:---|:---|:---|
| Owned heap buffer | Standard library allocated arrays and byte buffers. | Pinned or otherwise protected until operation completion. |
| Borrowed buffer | User-provided memory slice. | Caller-visible object must remain alive until completion. Synchronous facade guarantees this via stack scope. |
| Registered fixed buffer | High-throughput file or socket operations. | Managed by BufferPool and referenced by fixed index until unregistered. |
| Provided buffer pool | Recv operations where kernel selects a buffer. | Completion identifies selected buffer. Runtime must return it to pool after processing. |

## 6.9 Linux synchronization helpers

- eventfd MAY be used to wake a ring owner or integrate external wakeups into an epoll fallback path.

- futex SHOULD be used for low-level locks, parking, thread join, lightweight semaphores, and internal wait queues where appropriate.

- futex_waitv MAY be used when available for wait-on-many semantics, but the public API MUST not require it.

- pthread or clone3 MAY be used for OS thread creation. pthread_create is recommended for initial implementation unless the runtime needs custom TLS, stack, or clone flags.

# 7. Layer 1B: Windows Native Backend Target

The Windows backend shall implement the same Layer 1 contracts using Windows-native asynchronous I/O facilities. It should not emulate io_uring. It should map the portable operation and completion model to IOCP and overlapped I/O.

## 7.1 Windows mapping

| **Portable concept** | **Windows implementation target** |
|:---|:---|
| AsyncRuntime completion queue | I/O Completion Port. |
| AsyncOperation native state | OVERLAPPED plus runtime operation record. |
| Async file read/write | ReadFile and WriteFile on handles opened with FILE_FLAG_OVERLAPPED. |
| Async socket read/write | WSARecv and WSASend with OVERLAPPED. |
| Async accept/connect | AcceptEx and ConnectEx where available, or documented fallback. |
| Wake event loop | PostQueuedCompletionStatus with a runtime wake key. |
| Thread interrupt wake | Manual-reset event plus operation cancel when required. |
| Operation cancel | CancelIoEx(handle, overlapped). |
| Low-level wait address | WaitOnAddress and WakeByAddressSingle/All. |
| Thread sleep interruptible | WaitForSingleObject(interruptEvent, timeout). |

## 7.2 Windows operation lifetime rules

- An OVERLAPPED object MUST remain alive until the IOCP completion or GetOverlappedResult confirms terminal completion.

- CancelIoEx MUST be treated as a request, not as immediate completion.

- ERROR_OPERATION_ABORTED maps to Canceled unless another higher-level condition, such as resource close, is known to be the cause.

- If an operation completes successfully after Thread.interrupt() was requested, the synchronous facade applies the same race policy used on Linux.

# 8. Layer 1C: Runtime Threading Substrate

Runtime threads represent language-level threads and own interruption state, blocking state, scheduling metadata, and current operation state. A runtime thread may be backed by one OS thread in the initial implementation. The design must not prevent future virtual threads or fibers.

| **Class** | **Fields** | **Responsibilities** |
|:---|:---|:---|
| RuntimeThread | threadId, osThreadHandle, interrupted, interruptWake, currentOperation, state, schedulerRef. | Represents a language thread, performs interruption checks, owns blocking point metadata. |
| ThreadControlBlock | stack metadata, TLS pointer, safepoint state, native exception state. | Runtime-private execution state used by compiler and garbage collector. |
| InterruptToken | interrupted flag, wake primitive, epoch counter. | Thread-scoped interruption state and wake mechanism. |
| BlockingPoint | kind, waitSet, currentOperation, deadline, lockReacquirePolicy. | Common implementation for sleep, join, condition wait, future wait, and synchronous I/O wait. |
| ThreadRegistry | active threads, id map, shutdown list. | Finds target threads for interrupt, join, diagnostics, and shutdown. |

## 8.1 OS thread creation policy

- Linux initial implementation SHOULD use pthread_create unless the runtime requires clone3 features not available through pthreads.

- Windows implementation SHOULD use \_beginthreadex or CreateThread according to CRT integration requirements.

- The runtime MUST install ThreadControlBlock and language TLS before executing user code.

- User code MUST start through a runtime trampoline that records the RuntimeThread, installs exception boundaries, and performs final cleanup.

## 8.2 Blocking point algorithm

enterInterruptibleBlockingPoint(thread, kind, optionalOperation, deadline):  
if thread.interrupted.load(acquire):  
throw ThreadInterruptionException  
  
thread.currentOperation.store(optionalOperation, release)  
thread.state.store(kind, release)  
  
result = platform_wait_until_completion_timeout_or_interrupt(...)  
  
thread.state.store(RUNNING, release)  
thread.currentOperation.store(null, release)  
  
if result == INTERRUPTED:  
throw ThreadInterruptionException  
return result

# 9. Layer 1D: Futures, Promises, and Completion Model

Futures and promises are cross-layer primitives. Layer 1 operations may complete promises directly. Layer 2 blocking APIs may wait on futures. Layer 3 event loops may schedule continuations from futures.

| **Class or interface** | **Description** | **Required methods** |
|:---|:---|:---|
| Future\<T\> | Read-only handle to an eventual result. | isDone(), isCancelled(), get(), get(timeout), awaitAsync(), thenApply(), thenCompose(). |
| Promise\<T\> | Writable completion endpoint used by runtime or libraries. | trySuccess(value), tryFailure(error), tryCancel(reason), future(). |
| CompletionStage\<T\> | Composable continuation API. | thenApply, thenAccept, handle, exceptionally, whenComplete. |
| CancelableFuture\<T\> | Future linked to a CancellationToken or AsyncOperation. | cancel(), cancel(reason), cancellationToken(). |
| Task\<T\> | Scheduled computation that may produce a Future. | start(), await(), cancel(), resultFuture(). |

## 9.1 Future interruption semantics

- Future.get() is interruptible and MUST throw ThreadInterruptionException if the current thread is interrupted before the future completes.

- Interrupting a thread waiting in Future.get() MUST NOT automatically cancel the underlying future unless the method is explicitly getCancellable or the Future is scoped to the waiting thread.

- Future.cancel() requests operation cancellation if the future is backed by an AsyncOperation.

- A future completion is immutable. Once completed, it cannot change from success to canceled or from canceled to failure.

# 10. Layer 2: Synchronous Facade

Layer 2 exposes the language standard library APIs most users will consume. It must look synchronous where appropriate, but it must be implemented through Layer 1 operations and unified blocking points.

## 10.1 Synchronous implementation pattern

InputStream.read(buffer):  
check current thread interruption  
op = AsyncRead(handle, buffer)  
future = AsyncRuntime.submit(op)  
completion = currentThread.waitInterruptibly(future, op)  
return translateCompletionToReadResult(completion)

- The public synchronous method owns the buffer lifetime until the operation completes or is canceled and drained.

- The public synchronous method is responsible for translating Completion into the documented return value or exception.

- If the thread is interrupted while waiting, the method requests operation cancellation and waits until backend terminal completion before releasing native resources.

# 11. Layer 2A: Synchronous Threading and Synchronization API

## 11.1 Thread API

| **API** | **Semantics** |
|:---|:---|
| Thread.start() | Creates or schedules execution of a RuntimeThread. User code starts through runtime trampoline. |
| Thread.current() | Returns current language Thread object. |
| Thread.interrupt() | Sets interrupted flag on target and wakes interruptible waits. Does not kill OS thread. |
| Thread.isInterrupted() | Returns target interrupted flag without clearing it. |
| Thread.interrupted() | Returns current thread interrupted flag and clears it. |
| Thread.checkInterrupted() | Throws ThreadInterruptionException if current thread is interrupted. |
| Thread.join() | Waits interruptibly for target termination. |
| Thread.sleep(duration) | Waits interruptibly until duration elapses. |
| Thread.yield() | Scheduler hint. Not an interruption point unless specified separately. |

## 11.2 Mutex and Lock

| **API** | **Interruptible** | **Behavior** |
|:---|:---|:---|
| Mutex.lock() | No | Blocks until acquired. Does not throw ThreadInterruptionException. |
| Mutex.lockInterruptibly() | Yes | Blocks until acquired or interrupted. Throws if interrupted before acquisition. |
| Mutex.tryLock() | No blocking | Returns immediately. |
| Mutex.tryLock(timeout) | Yes | Waits until acquired, timeout, or interruption. |
| Mutex.unlock() | No | Releases ownership. Throws IllegalMonitorStateException if not owner for owner-tracking mutexes. |
| ReadWriteLock.readLock() | Configurable | Supports interruptible and noninterruptible acquisition variants. |
| ReentrantLock | Configurable | Tracks owner and recursion count. Fairness is optional policy. |

## 11.3 Condition

| **API** | **Semantics** |
|:---|:---|
| Condition.await() | Atomically releases associated lock, waits interruptibly, then reacquires lock before returning or throwing. |
| Condition.await(timeout) | Same as await plus timeout result. |
| Condition.signal() | Wakes one waiter according to queue policy. |
| Condition.signalAll() | Wakes all waiters. |
| Condition.awaitUninterruptibly() | Optional. Waits even if interrupted and preserves interruption status for later observation. |

## 11.4 Semaphore, latch, and barrier

| **Class** | **Required API** | **Interruption behavior** |
|:---|:---|:---|
| Semaphore | acquire(), acquireInterruptibly(), tryAcquire(), release(). | Interruptible acquisition throws before permit ownership. Noninterruptible acquire preserves interrupted status if specified. |
| CountDownLatch | await(), await(timeout), countDown(). | await is interruptible. countDown is never blocking. |
| CyclicBarrier | await(), await(timeout), reset(). | Interrupting one waiter may break the barrier according to class contract. |
| Phaser | arrive(), awaitAdvance(), awaitAdvanceInterruptibly(). | Interruptible variants throw. Noninterruptible variants preserve status. |

# 12. Layer 2B: Synchronous I/O API

## 12.1 Stream hierarchy

interface Closeable {  
close(): void throws IOException  
}  
  
abstract class InputStream implements Closeable {  
read(buffer: ByteBuffer): int throws IOException, ThreadInterruptionException  
readFully(buffer: ByteBuffer): void throws IOException, InterruptedIOException  
skip(count: long): long throws IOException, ThreadInterruptionException  
}  
  
abstract class OutputStream implements Closeable {  
write(buffer: ByteBuffer): int throws IOException, ThreadInterruptionException  
writeFully(buffer: ByteBuffer): void throws IOException, InterruptedIOException  
flush(): void throws IOException, ThreadInterruptionException  
}

## 12.2 File I/O classes

| **Class** | **Purpose** | **Implementation** |
|:---|:---|:---|
| FileInputStream | Byte stream over a file. | Layer 2 wrapper over AsyncRead operations. Maintains stream position. |
| FileOutputStream | Byte stream for file output. | Layer 2 wrapper over AsyncWrite and flush/fsync policy. |
| FileChannel | Positioned, potentially concurrent file access. | Async readAt/writeAt operations with explicit offsets. |
| Path | Filesystem path object. | Pure value API. Platform normalization is separate from I/O execution. |
| FileSystemProvider | Provider abstraction for local and future virtual file systems. | May delegate to native backend or custom provider. |

## 12.3 Socket and network classes

| **Class** | **Purpose** | **Implementation** |
|:---|:---|:---|
| Socket | Connected stream socket. | Async recv/send internally. Synchronous read/write wait interruptibly. |
| ServerSocket | Listening socket. | Async accept internally. accept() waits interruptibly. |
| DatagramSocket | Datagram endpoint. | Async recvmsg/sendmsg where available. |
| SocketChannel | Selectable channel abstraction. | Can be used with Selector and async operations. |
| NetworkAddress | Address value object. | No blocking DNS in constructor. DNS lookups are explicit operations. |

## 12.4 Read and write partial transfer policy

- Low-level read(buffer) returns the number of bytes transferred when at least one byte was transferred before interruption or timeout is resolved.

- readFully(buffer) either fills the buffer or throws an exception that carries bytesTransferred.

- Low-level write(buffer) may return a partial byte count.

- writeFully(buffer) either writes all requested bytes or throws InterruptedIOException or IOException carrying bytesTransferred.

- EOF is not interruption. EOF maps to zero or negative one according to the language API convention selected by the standard library.

# 13. Layer 2C: Exception and Error Model

| **Exception** | **Meaning** | **Typical source** |
|:---|:---|:---|
| RuntimeException | Base unchecked runtime failure. | Programming errors, illegal state. |
| IOException | Base checked or declared I/O failure according to language design. | Native errno, Win32, WSA error. |
| ThreadInterruptionException | Current thread was interrupted at an interruptible blocking point before useful partial progress was returned. | Thread.interrupt(). |
| InterruptedIOException | I/O was interrupted after partial transfer or during a composite I/O operation. | readFully, writeFully, multi-step stream operation. |
| TimeoutException | Operation did not complete before deadline. | Future.get(timeout), socket timeout, selector timeout. |
| CancellationException | A future or async operation was canceled independently of thread interruption. | Future.cancel(), scoped cancellation. |
| ClosedChannelException | Operation attempted on closed channel. | close race or explicit close. |
| EndOfStreamException | End of stream when an exact amount was required. | readFully EOF before full buffer. |

## 13.1 Native error translation

- The runtime MUST preserve the native error code in IOException for diagnostics.

- The runtime MUST expose a stable language-level category independent of platform numeric codes.

- ECANCELED, EINTR, and ERROR_OPERATION_ABORTED are not automatically equivalent to thread interruption. They map to ThreadInterruptionException only when tied to a current thread interrupted state or explicit interrupt cause.

- Connection reset and broken pipe are I/O errors or peer close events, not interruption.

# 14. Layer 3: Proactor and Reactor Layer

Layer 3 exposes scalable asynchronous programming models above Layer 1. It contains a native proactor API and a reactor-compatible selector API. Both use the same backend completions.

## 14.1 Proactor API

interface EventLoop {  
run(): void  
stop(): void  
submit\<T\>(operation: AsyncOperation\<T\>): Future\<T\>  
schedule(task: Runnable): void  
scheduleAfter(delay: Duration, task: Runnable): ScheduledTask  
}  
  
interface CompletionHandler\<T\> {  
completed(result: T): void  
failed(error: Throwable): void  
cancelled(reason: CancellationReason): void  
}

- In the proactor model, the operation is submitted first and the completion is delivered later.

- EventLoop MUST own callback ordering and exception isolation.

- Callbacks MUST NOT run while backend internal locks are held.

- A completion handler executing on an event loop MUST be able to submit more operations without deadlocking.

## 14.2 Reactor API

interface Selector {  
register(channel: SelectableChannel, interestOps: InterestOps): SelectionKey  
select(): int throws ThreadInterruptionException, IOException  
select(timeout: Duration): int throws ThreadInterruptionException, IOException  
wakeup(): void  
selectedKeys(): Set\<SelectionKey\>  
}

- Reactor readiness is a facade. On Linux it may use io_uring poll operations or epoll fallback. On Windows it may be implemented over IOCP, Winsock events, or a dedicated readiness emulation layer.

- Selector.wakeup() is separate from Thread.interrupt(). wakeup causes select to return normally. interrupt causes ThreadInterruptionException.

- Readiness is advisory. A subsequent read or write may still complete with WouldBlock or no data.

## 14.3 Scheduler integration

| **Component** | **Responsibility** |
|:---|:---|
| Scheduler | Owns runnable queues, event loop shards, timers, and optional virtual thread parking. |
| CarrierThread | OS thread executing scheduled tasks and processing completions. |
| VirtualThread | Future extension. Language thread not permanently bound to OS thread. |
| ParkingLot | Synchronizes parked tasks, future waiters, condition waiters, and interruptible waits. |
| Continuation | Executable continuation scheduled after completion or unpark. |

# 15. Resource Ownership and Lifetime Rules

- AsyncHandle close is a state transition. It MUST prevent new operations, request cancellation of pending operations, and release the native handle only after all operations that reference it are terminal or safely detached.

- AsyncOperation owns a reference to each AsyncHandle it uses until terminal completion.

- Buffers passed to asynchronous operations MUST be pinned, copied, or otherwise guaranteed stable until terminal completion.

- Callbacks, promises, and futures MUST not be freed while they are reachable from an in-flight operation.

- Runtime shutdown MUST drain or cancel all rings, ports, operations, timers, and workers in a deterministic order.

## 15.1 Close semantics

close(handle):  
if handle.state transitions OPEN -\> CLOSING:  
reject new operations with ClosedChannelException  
request cancellation of pending operations on handle  
wait or arrange finalizer for all pending completions  
release native descriptor or HANDLE  
transition CLOSING -\> CLOSED

# 16. State Machines and Race Handling

## 16.1 Interruption versus completion race

| **Race outcome** | **Required behavior** |
|:---|:---|
| Interrupt observed before operation submit | Do not submit operation. Throw ThreadInterruptionException. |
| Interrupt after submit and before completion | Request cancellation and wait for terminal completion. Throw interruption exception unless operation completed with useful partial result according to API policy. |
| Operation completion before interrupt wake is processed | Return operation result if completion was already committed to the waiting thread. Preserve interrupted status if interrupt flag remains set. |
| Cancel request succeeds before operation completes | Consume both cancel completion and operation completion. Throw interruption or cancellation exception according to cause. |
| Cancel request reports not found | Check operation registry. If operation already completed, use completed result. Otherwise treat as backend integrity error. |

## 16.2 Condition.await race rules

- Condition.await MUST enqueue the waiter while holding the associated lock.

- It MUST release the lock atomically with respect to becoming visible as a waiter.

- On wake, timeout, cancellation, or interruption, it MUST reacquire the lock before returning or throwing.

- If signal and interruption race, the implementation MUST have a deterministic policy. Recommended policy: if signal wins and the waiter is removed from the condition queue before interrupt is observed, return normally and preserve interrupted status; otherwise throw ThreadInterruptionException.

# 17. Memory Model and Thread-Safety Rules

- The interrupted flag MUST be atomic.

- Thread.interrupt uses release semantics when setting the flag. Blocking points use acquire semantics when checking it.

- Completion publication MUST establish a happens-before relationship from backend completion processing to future waiters and callback execution.

- Lock unlock MUST release and lock acquisition MUST acquire.

- Condition signal MUST publish state changes made before signal to the awakened waiter after the waiter reacquires the lock.

- Async operation state transitions MUST be linearizable with respect to cancellation and completion delivery.

# 18. Performance and Scalability Policy

- The default Linux backend SHOULD avoid one epoll instance or one OS thread per blocking user operation.

- The default Windows backend SHOULD use IOCP for high-concurrency I/O rather than WaitForMultipleObjects for many handles.

- Layer 2 synchronous APIs MAY block an OS thread in the initial implementation, but the design must not prevent later parking of virtual threads.

- Batch submission and completion draining SHOULD be used on hot paths.

- Registered buffers and fixed file descriptors SHOULD be optional optimizations, not semantic requirements.

- Fairness policies for locks, semaphores, and schedulers MUST be documented because they affect starvation and latency.

# 19. Testing and Verification Strategy

| **Test category** | **Required cases** |
|:---|:---|
| Interruption | Interrupt before wait, during wait, after completion race, repeated interrupt, interrupt status clearing. |
| Cancellation | Cancel active read, cancel already completed operation, cancel missing operation, cancel all operations on handle. |
| Partial I/O | Read partial then interrupt, write partial then interrupt, readFully EOF, writeFully drain. |
| Close races | Close while read pending, close while write pending, close while selector waiting, double close. |
| Synchronization | Mutex contention, interruptible lock acquisition, condition signal vs interrupt race, semaphore fairness. |
| Timeouts | Timeout before completion, completion before timeout, timeout cancel race, nested deadlines. |
| Cross-platform equivalence | Same public API behavior on Linux and Windows for all documented race outcomes. |
| Stress | Millions of operations, thousands of sockets, cancellation storm, shutdown under load. |

# 20. Implementation Roadmap

1.  Implement RuntimeThread, ThreadRegistry, interruption flag, interruptible sleep, join, and future wait.

2.  Implement Layer 1 operation registry, completion model, futures, promises, and portable error categories.

3.  Implement a minimal Linux io_uring backend for read, write, timeout, cancel, and wake operations.

4.  Implement FileInputStream, FileOutputStream, FileChannel through Layer 1.

5.  Implement Socket, ServerSocket, and SocketChannel through Layer 1.

6.  Implement Mutex, ReentrantLock, Semaphore, Condition, CountDownLatch, and interruptible variants.

7.  Implement EventLoop and proactor callbacks over completion dispatch.

8.  Implement Selector facade over io_uring poll or backend readiness adapter.

9.  Implement Windows backend with IOCP, OVERLAPPED operations, CancelIoEx, waitable events, and WaitOnAddress primitives.

10. Add virtual thread support only after the OS-thread implementation has a stable interruption, cancellation, and resource lifetime model.

# Appendix A. API Sketches

## A.1 Thread and interruption

class Thread {  
static Thread current();  
static bool interrupted(); // current thread, reads and clears  
  
void start();  
void join() throws ThreadInterruptionException;  
void join(Duration timeout) throws ThreadInterruptionException, TimeoutException;  
void interrupt();  
bool isInterrupted() const;  
  
static void sleep(Duration duration) throws ThreadInterruptionException;  
static void checkInterrupted() throws ThreadInterruptionException;  
}

## A.2 Synchronization

interface Lock {  
void lock();  
void lockInterruptibly() throws ThreadInterruptionException;  
bool tryLock();  
bool tryLock(Duration timeout) throws ThreadInterruptionException;  
void unlock();  
Condition newCondition();  
}  
  
interface Condition {  
void await() throws ThreadInterruptionException;  
bool await(Duration timeout) throws ThreadInterruptionException;  
void awaitUninterruptibly();  
void signal();  
void signalAll();  
}  
  
class Semaphore {  
void acquire() throws ThreadInterruptionException;  
bool tryAcquire();  
bool tryAcquire(Duration timeout) throws ThreadInterruptionException;  
void release();  
}

## A.3 Futures

interface Future\<T\> {  
bool isDone();  
bool isCancelled();  
T get() throws ThreadInterruptionException, ExecutionException;  
T get(Duration timeout) throws ThreadInterruptionException, ExecutionException, TimeoutException;  
bool cancel();  
Future\<U\> thenApply\<U\>(Function\<T, U\> fn);  
}  
  
interface Promise\<T\> {  
bool trySuccess(T value);  
bool tryFailure(Throwable error);  
bool tryCancel(CancellationReason reason);  
Future\<T\> future();  
}

## A.4 I/O

interface Channel extends Closeable {  
bool isOpen();  
}  
  
interface ReadableChannel extends Channel {  
int read(ByteBuffer dst) throws IOException, ThreadInterruptionException;  
Future\<int\> readAsync(ByteBuffer dst);  
}  
  
interface WritableChannel extends Channel {  
int write(ByteBuffer src) throws IOException, ThreadInterruptionException;  
Future\<int\> writeAsync(ByteBuffer src);  
}  
  
class Selector implements Closeable {  
SelectionKey register(SelectableChannel channel, InterestOps ops);  
int select() throws IOException, ThreadInterruptionException;  
int select(Duration timeout) throws IOException, ThreadInterruptionException;  
void wakeup();  
Set\<SelectionKey\> selectedKeys();  
}

# Appendix B. Platform Mapping

| **Area** | **Linux backend** | **Windows backend** |
|:---|:---|:---|
| Thread creation | pthread_create initially; clone3 only if required. | \_beginthreadex or CreateThread according to CRT policy. |
| Operation submission | io_uring SQE submission. | Overlapped I/O submission. |
| Completion retrieval | io_uring CQE drain. | GetQueuedCompletionStatus or GetQueuedCompletionStatusEx. |
| Wake event loop | Ring wake operation, eventfd, or msg_ring if supported. | PostQueuedCompletionStatus. |
| Operation cancellation | IORING_OP_ASYNC_CANCEL by user_data or fd. | CancelIoEx by HANDLE and OVERLAPPED. |
| Lightweight wait | futex or futex_waitv when available. | WaitOnAddress. |
| Wait on many | io_uring completions, epoll fallback, futex_waitv optional. | IOCP or WaitForMultipleObjects for small fixed sets. |
| File I/O | io_uring read/write/open/stat/fsync. | ReadFile/WriteFile with FILE_FLAG_OVERLAPPED. |
| Socket I/O | io_uring recv/send/accept/connect. | WSARecv/WSASend/AcceptEx/ConnectEx with IOCP. |
| Timer | io_uring timeout or timerfd fallback. | Waitable timer or runtime timer queue posting IOCP completions. |

# Appendix C. References

- Linux man-pages: io_uring(7), asynchronous I/O facility based on shared submission and completion ring buffers.

- Linux man-pages: io_uring_cancelation(7), cancellation overview for in-flight io_uring requests.

- Linux man-pages: futex(7), fast user-space locking primitive.

- Microsoft Learn: I/O Completion Ports, efficient completion queue model for asynchronous I/O.

- Microsoft Learn: CancelIoEx, cancellation request API for pending I/O on a Windows handle.

- Microsoft Learn: Wait Functions, Windows waitable object model.

- Microsoft Learn: WaitOnAddress, address-based wait and wake synchronization primitive.

- Microsoft Learn: Alertable I/O, APC-based asynchronous I/O model and limitations.

# 21. Layer 1 Detailed Implementation Specification

This section refines Layer 1 into implementation-level subsystems, method contracts, state transitions, concurrency rules, and platform adapter responsibilities. It is normative for the runtime implementation. Layer 2 and Layer 3 must not bypass these contracts.

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th><p><strong>Layer 1 design invariant</strong></p>
<p>Every interruptible operation exposed by the standard library must be represented as an AsyncOperation or as a BlockingPoint backed by an AsyncOperation-compatible completion path. This invariant is required to keep Thread.interrupt(), timeout, cancellation, close, and shutdown behavior uniform across Linux and Windows.</p></th>
</tr>
</thead>
<tbody>
</tbody>
</table>

## 21.1 Layer 1 subsystem decomposition

| **Subsystem** | **Primary classes** | **Responsibility** |
|:---|:---|:---|
| Runtime core | AsyncRuntime, BackendDriver, BackendCapabilities | Owns lifecycle, backend selection, global configuration, submission policy, and shutdown. |
| Operation model | AsyncOperation, OperationBuilder, OperationRegistry, OperationId | Represents operation identity, state, ownership, native metadata, cancellation policy, deadlines, and completion routing. |
| Handle model | AsyncHandle, HandleRegistry, NativeHandleRef, CloseToken | Tracks resource ownership, pending operations, close state, descriptor or HANDLE lifetime, and backend-specific registration. |
| Completion model | Completion, CompletionQueue, CompletionDispatcher, CompletionSink | Normalizes platform completions and delivers them to blocking waiters, promises, event loops, or internal cleanup paths. |
| Cancellation model | CancellationToken, CancellationSource, CancellationScope, CancelRequest | Provides operation-scoped and scope-scoped cancellation independent of thread interruption. |
| Interruption bridge | InterruptToken, BlockingPoint, ThreadWaiter | Connects RuntimeThread interruption to operation cancellation and wait wakeup. |
| Buffer model | BufferLease, BufferPool, RegisteredBufferSet, ProvidedBufferPool | Guarantees stable memory for in-flight native I/O and exposes optional fast paths. |
| Timer model | Deadline, TimerOperation, TimeoutLink | Provides operation deadlines, sleeps, linked timeouts, scheduled tasks, and timeout cancellation. |
| Diagnostics | RuntimeMetrics, OperationTrace, BackendProbe | Exposes counters, latency histograms, ring depth, cancellation races, and resource-leak detection. |

## 21.2 AsyncRuntime implementation contract

AsyncRuntime is the process-level owner of Layer 1. A program may have one default runtime and may optionally create isolated runtime instances for tests, sandboxes, or specialized schedulers. Public standard library APIs should use the current runtime selected by the execution context.

| **Method** | **Signature sketch** | **Contract** |
|:---|:---|:---|
| create | static AsyncRuntime create(RuntimeConfig config) | Allocates a runtime instance, validates backend requirements, but does not start worker threads or rings. |
| start | void start() | Initializes BackendDriver, creates completion queues, starts workers, and transitions runtime to RUNNING. |
| shutdown | void shutdown(ShutdownMode mode) | Stops accepting new operations, cancels or drains pending operations according to mode, joins workers, and releases backend resources. |
| submit | Future\<Completion\> submit(AsyncOperation op) | Validates handle state and buffer lifetime, registers op, forwards it to backend, and returns a future backed by the operation completion. |
| submitBatch | int submitBatch(List\<AsyncOperation\> ops) | Submits multiple operations atomically from the runtime perspective. Individual operations may fail validation independently. |
| cancel | CancelResult cancel(OperationId id, CancellationReason reason) | Requests cancellation of a specific operation. It is valid for the operation to complete before cancellation takes effect. |
| cancelHandle | int cancelHandle(AsyncHandle handle, CancellationReason reason) | Requests cancellation of all pending operations associated with a handle. |
| cancelScope | int cancelScope(CancellationScope scope) | Requests cancellation of all operations registered in a cancellation scope. |
| registerHandle | AsyncHandle registerHandle(NativeHandle native, HandleCapabilities caps) | Creates an AsyncHandle and performs optional native registration, such as fixed file registration on Linux or IOCP association on Windows. |
| unregisterHandle | void unregisterHandle(AsyncHandle handle) | Transitions the handle out of the registry after close and after all pending operations are terminal. |
| registerBuffer | BufferLease registerBuffer(ByteBuffer buffer, BufferRegistrationPolicy policy) | Pins or registers memory according to backend capabilities. |
| newCompletionQueue | CompletionQueue newCompletionQueue(QueuePolicy policy) | Creates a completion queue for a scheduler shard, event loop, or test harness. |
| backendCapabilities | BackendCapabilities backendCapabilities() | Returns immutable runtime capabilities and limits discovered at startup. |
| metrics | RuntimeMetrics metrics() | Returns runtime counters and snapshots for diagnostics and testing. |

## 21.3 BackendDriver interface

BackendDriver is the platform adapter boundary. The Linux implementation maps it to io_uring. The Windows implementation maps it to I/O Completion Ports and overlapped I/O. BackendDriver must not expose platform-native completion objects to Layer 2 or Layer 3.

interface BackendDriver {  
BackendCapabilities probe();  
void initialize(RuntimeConfig config, CompletionQueue systemQueue);  
void start();  
void stop(ShutdownMode mode);  
  
SubmitResult submit(PreparedOperation op);  
int submitBatch(ReadOnlySpan\<PreparedOperation\> ops);  
  
CancelSubmitResult requestCancel(OperationId id, CancellationReason reason);  
CancelSubmitResult requestCancelByHandle(AsyncHandle handle, CancellationReason reason);  
  
Completion pollCompletion();  
int drainCompletions(CompletionQueue target, int maxItems);  
Completion waitCompletion(Deadline deadline, InterruptToken interruptToken);  
  
NativeHandleRegistration registerHandle(AsyncHandle handle);  
void unregisterHandle(AsyncHandle handle);  
BufferRegistration registerBuffer(BufferLease lease);  
void unregisterBuffer(BufferRegistration registration);  
  
void wake();  
}

- **submit() requirement:** The method must either publish the operation to the backend or return a non-submitted error. It must not leave an operation in an ambiguous state.

- **pollCompletion() requirement:** The method must return only normalized Completion values. Native CQE, OVERLAPPED, or platform status blocks remain backend-private.

- **requestCancel() requirement:** The method submits a cancellation request. It does not guarantee that the target operation will complete as canceled.

- **wake() requirement:** The method wakes a backend loop blocked in waitCompletion or equivalent wait. It is used for shutdown, event-loop wakeup, and cross-thread notification.

## 21.4 Operation identity, registry, and generation counters

- OperationId MUST be globally unique within an AsyncRuntime instance for the lifetime of any in-flight native request.

- OperationId SHOULD contain an index and a generation counter to prevent stale completion or stale cancellation from resolving a newly allocated operation slot.

- OperationRegistry MUST be the only component that resolves OperationId to AsyncOperation.

- When an operation reaches RELEASED, its registry slot MAY be reused only after its generation counter is incremented.

- Unknown completion identifiers are fatal integrity errors unless they represent explicitly documented backend sentinels such as wake completions.

struct OperationId {  
uint32 index;  
uint32 generation;  
}  
  
registry.resolve(id):  
slot = slots\[id.index\]  
if slot.generation != id.generation:  
return StaleOperation  
return slot.operation

## 21.5 AsyncOperation class method list

| **Method** | **Contract** |
|:---|:---|
| kind() | Returns operation kind: READ, WRITE, ACCEPT, CONNECT, TIMEOUT, CANCEL, POLL, CLOSE, OPEN, FSYNC, WAKE, or INTERNAL. |
| id() | Returns stable OperationId after preparation. Before preparation, returns null or invalid. |
| ownerThread() | Returns the RuntimeThread that submitted or synchronously waits for the operation, if any. |
| handle() | Returns primary AsyncHandle. Multi-handle operations expose additional handles through handles(). |
| handles() | Returns all referenced handles. The runtime increments references before submission. |
| bufferLeases() | Returns all BufferLease objects that must remain alive until terminal completion. |
| deadline() | Returns optional absolute deadline. |
| timeoutPolicy() | Returns NONE, LINKED_TIMEOUT, INDEPENDENT_TIMEOUT, or CALLER_MANAGED. |
| cancelPolicy() | Returns whether explicit cancel, interrupt-triggered cancel, close-triggered cancel, or timeout-triggered cancel is enabled. |
| state() | Returns current operation state. State transitions are atomic and linearizable. |
| tryTransition(expected,next) | Performs atomic state transition. Used by cancellation and completion race handlers. |
| markSubmitted(nativeToken) | Records native token, CQE user_data, OVERLAPPED pointer, or backend-private request handle. |
| requestCancel(reason) | Marks cancellation requested and delegates to AsyncRuntime.cancel unless already terminal. |
| complete(completion) | Publishes final Completion. Exactly one terminal completion may win. |
| completionFuture() | Returns Future\<Completion\> associated with the operation. |
| completionSink() | Returns destination: future, blocking waiter, event loop, selector, or internal sink. |
| release() | Releases handle references, buffer leases, registry slot, and backend-specific metadata after delivery. |

## 21.6 OperationBuilder factories

| **Factory** | **Required parameters** | **Notes** |
|:---|:---|:---|
| read | AsyncHandle handle, BufferLease dst, long offset, ReadOptions options | offset may be STREAM_POSITION for stream semantics. Positioned file channels must pass explicit offsets. |
| write | AsyncHandle handle, BufferLease src, long offset, WriteOptions options | May complete partially. writeFully is Layer 2 logic built on repeated write operations. |
| accept | AsyncHandle serverSocket, AcceptOptions options | May support multishot internally only when per-accepted-socket ownership is preserved. |
| connect | AsyncHandle socket, NetworkAddress remote, ConnectOptions options | Deadline support must cancel the pending connect operation when possible. |
| recv | AsyncHandle socket, BufferLease dst, RecvOptions options | For stream sockets, zero bytes means peer orderly shutdown when applicable. |
| send | AsyncHandle socket, BufferLease src, SendOptions options | Partial sends are valid outcomes. |
| poll | AsyncHandle handle, InterestOps interests, PollOptions options | Used by reactor facade and readiness-based compatibility paths. |
| timeout | Deadline deadline, TimeoutOptions options | Completes with Timeout unless canceled first. |
| cancel | OperationId target, CancellationReason reason | Represents a backend cancellation request as a first-class operation when the backend emits a completion for cancellation itself. |
| open | Path path, OpenOptions options | May be synchronous during bootstrap if backend cannot open asynchronously. Public semantics remain asynchronous. |
| close | AsyncHandle handle, CloseOptions options | Prevents new operations, cancels pending operations, drains completions, releases native resource. |
| fsync | AsyncHandle file, FsyncOptions options | Used by OutputStream.flush policies and FileChannel.force. |
| wake | WakeTarget target | Posts a synthetic completion or backend wake event. |

## 21.7 CompletionQueue and CompletionDispatcher methods

| **Class** | **Method** | **Contract** |
|:---|:---|:---|
| CompletionQueue | offer(Completion c) | Publishes a completion to this queue with release semantics. |
| CompletionQueue | poll() | Returns one completion or null without blocking. |
| CompletionQueue | pollBatch(List\<Completion\> out, int max) | Transfers up to max completions without blocking. |
| CompletionQueue | wait(Deadline deadline, InterruptToken interrupt) | Blocks until completion, timeout, shutdown, or interruption. |
| CompletionQueue | wake(WakeReason reason) | Unblocks waiters without requiring an I/O completion. |
| CompletionQueue | close() | Rejects new completions except internal shutdown sentinels and wakes all waiters. |
| CompletionDispatcher | dispatch(Completion c) | Resolves operation and forwards completion to the configured CompletionSink. |
| CompletionDispatcher | dispatchBatch(List\<Completion\> completions) | Processes a batch, preserving per-operation correctness but not global completion order unless the backend guarantees it. |
| CompletionDispatcher | installSink(OperationId id, CompletionSink sink) | Associates an operation with a future, blocking point, event loop, selector, or internal handler. |
| CompletionDispatcher | removeSink(OperationId id) | Removes sink after delivery or operation release. |

## 21.8 AsyncHandle methods and close state machine

| **Method** | **Contract** |
|:---|:---|
| nativeHandle() | Returns backend-private native descriptor or handle only to Layer 1 backend code. |
| capabilities() | Returns read, write, seek, socket, datagram, pollable, overlapped, registered, and close capabilities. |
| state() | Returns OPEN, CLOSING, CLOSED, FAILED. |
| retain() / release() | Reference counting used by operations to keep native resources alive. |
| registerOperation(OperationId id) | Adds operation to pending set. Fails if handle is not OPEN. |
| unregisterOperation(OperationId id) | Removes operation from pending set after terminal completion. |
| pendingOperations() | Returns diagnostic snapshot of pending operations. |
| closeAsync() | Starts close sequence and returns Future\<Void\>. |
| markCloseRequested() | Transitions OPEN to CLOSING. Rejects new operations. |
| nativeRegistration() | Returns backend-specific registration, such as fixed file slot or IOCP association key. |
| onBackendClosed() | Final transition to CLOSED after pending operations are complete or safely detached. |

OPEN  
-\> CLOSING_REQUESTED  
-\> CANCEL_PENDING_OPERATIONS  
-\> DRAIN_PENDING_COMPLETIONS  
-\> RELEASE_NATIVE_HANDLE  
-\> CLOSED

## 21.9 BufferLease and buffer registration methods

| **Method** | **Contract** |
|:---|:---|
| address() | Returns stable native address for the duration of the lease. |
| length() | Returns byte length visible to the native operation. |
| slice(offset,length) | Creates a child lease sharing the same underlying ownership. |
| retain() / release() | Keeps buffer alive across nested operations and completion delivery. |
| pin() / unpin() | Prevents relocation or releases pin. No-op for non-moving heaps or native memory. |
| register(Runtime) | Requests backend registration if supported and policy allows it. |
| registrationId() | Returns fixed buffer index or backend registration token if registered. |
| ownership() | Returns OWNED, BORROWED, PINNED, REGISTERED, PROVIDED_BY_BACKEND. |
| isWritable() | Used to validate read destination buffers. |
| isReadable() | Used to validate write source buffers. |

- Layer 1 MUST treat user-provided buffers as borrowed memory whose lifetime is extended by synchronous stack ownership, future ownership, or explicit BufferLease ownership.

- If the language has a moving garbage collector, BufferLease.pin() or an equivalent object relocation barrier is mandatory before native submission.

- Registered buffer fast paths must be optional. Failure to register a buffer must not change public semantics.

## 21.10 Linux io_uring preparation rules

- The backend must write operationId into SQE user_data for every request that can produce a CQE visible to the runtime.

- If a logical operation uses multiple SQEs, each SQE must either have its own internal OperationId or share a parent operation with explicit sub-operation metadata.

- Linked operations may be used for operation plus timeout, but the runtime must still consume completion events for all linked SQEs.

- For cancellation, the preferred target is user_data operationId. Cancellation by file descriptor is reserved for close, scope shutdown, or resource-wide abort.

- SQE allocation failure due to a full submission queue must either apply backpressure, flush submissions, or fail before operation publication. It must not expose a partially submitted operation.

prepareRead(op):  
sqe = ring.get_sqe_or_flush()  
sqe.opcode = READ_OR_RECV  
sqe.fd = op.handle.nativeFd  
sqe.addr = op.buffer.address  
sqe.len = op.buffer.length  
sqe.off = op.offset  
sqe.user_data = op.id.raw  
op.markSubmitted(nativeToken = sqe.user_data)

## 21.11 Completion processing algorithm

completionLoop(ring):  
while runtime.running:  
cqes = backend.drainCompletions(maxBatch)  
for cqe in cqes:  
completion = translate(cqe)  
op = registry.resolve(completion.operationId)  
if op is stale:  
reportFatalIntegrityError(completion)  
continue  
if op.tryComplete(completion):  
dispatcher.dispatch(completion)  
else:  
recordLateCompletion(completion)  
scheduler.runReadyTasks()

- Completion translation must happen before user callbacks execute.

- The dispatcher must not hold the OperationRegistry lock while invoking user callbacks.

- Completion delivery must publish operation result before waking blocking waiters or completing futures.

- Late completions are permitted only for explicitly detached cleanup operations. Otherwise they indicate a lifecycle bug.

## 21.12 Backpressure and queue-full policy

| **Condition** | **Required policy** |
|:---|:---|
| Submission queue full | Flush pending SQEs, retry once, then either block an internal worker or fail with RejectedExecutionException if runtime is shutting down. |
| Completion queue overflow risk | Drain completions before submitting more work. Metrics must record high-water marks. |
| Operation registry exhausted | Apply backpressure before native submission. Never submit without a registry slot. |
| Buffer pool exhausted | Use heap fallback if policy allows. Otherwise fail before submission. |
| Shutdown in progress | Reject new public operations with RuntimeShuttingDownException or ClosedChannelException depending on context. |

## 21.13 Cancellation, interruption, timeout, and close precedence

| **Cause combination** | **Resolution rule** |
|:---|:---|
| Interruption only | Throw ThreadInterruptionException at synchronous blocking point after safe operation drain. |
| Timeout only | Throw TimeoutException or return timeout status according to API. |
| Explicit operation cancel only | Complete future with CancellationException or return canceled Completion to Layer 3. |
| Close only | Complete pending operations with ClosedChannelException or EOF/peer-close semantics where appropriate. |
| Interruption and successful completion race | If completion was committed before interruption handling, return completion result and preserve interrupted flag. Otherwise throw interruption exception according to API policy. |
| Timeout and successful completion race | If completion wins, return success. If timeout wins and cancels operation, throw TimeoutException after terminal drain. |
| Close and interruption race | Close owns resource state. The waiting interrupted thread may still receive ThreadInterruptionException, but the handle state becomes closed independently. |
| Explicit cancel and interruption race | If interruption is thread-scoped and operation cancel is explicit, the exposed exception depends on the waiting API: Future.get may throw ThreadInterruptionException while the future completes as canceled. |

## 21.14 Layer 1 diagnostics and observability

| **Metric or trace** | **Purpose** |
|:---|:---|
| submittedOperations | Total operations submitted by kind. |
| completedOperations | Total completions by result kind. |
| cancellationRequests | Number of explicit, interrupt-triggered, timeout-triggered, and close-triggered cancellations. |
| cancellationRaces | Number of cancels where original operation already completed or was not found. |
| ringSubmitBatchSize | Batch size distribution for Linux io_uring submission. |
| completionBatchSize | Completion drain batch size distribution. |
| operationLatency | End-to-end latency from publication to terminal completion. |
| queueDepth | Submission queue, completion queue, registry, scheduler, and event-loop depth. |
| bufferPins | Current and high-water pinned buffer count. |
| handleLeakCandidates | Handles in CLOSING with pending operations exceeding threshold. |
| lateCompletions | Completions arriving after operation release attempt or stale generation mismatch. |

# Appendix D. Layer 1 API Reference

This appendix lists the Layer 1 interfaces and classes with implementation-oriented method sets. It is intentionally more detailed than the public standard library API. Names are provisional and may be adapted to the language naming conventions.

## D.1 AsyncRuntime

class AsyncRuntime {  
static AsyncRuntime create(RuntimeConfig config);  
void start();  
void shutdown(ShutdownMode mode);  
RuntimeState state();  
Future\<Completion\> submit(AsyncOperation operation);  
int submitBatch(List\<AsyncOperation\> operations);  
CancelResult cancel(OperationId id, CancellationReason reason);  
int cancelHandle(AsyncHandle handle, CancellationReason reason);  
int cancelScope(CancellationScope scope, CancellationReason reason);  
AsyncHandle registerHandle(NativeHandle native, HandleCapabilities capabilities);  
void unregisterHandle(AsyncHandle handle);  
BufferLease leaseBuffer(ByteBuffer buffer, BufferAccess access);  
BufferRegistration registerBuffer(BufferLease lease, BufferRegistrationPolicy policy);  
CompletionQueue newCompletionQueue(QueuePolicy policy);  
BackendCapabilities backendCapabilities();  
RuntimeMetrics metrics();  
}

## D.2 BackendDriver

interface BackendDriver {  
BackendCapabilities probe();  
void initialize(RuntimeConfig config, CompletionQueue systemQueue);  
void start();  
void stop(ShutdownMode mode);  
SubmitResult submit(PreparedOperation operation);  
int submitBatch(ReadOnlySpan\<PreparedOperation\> operations);  
CancelSubmitResult requestCancel(OperationId id, CancellationReason reason);  
CancelSubmitResult requestCancelByHandle(AsyncHandle handle, CancellationReason reason);  
Completion pollCompletion();  
int drainCompletions(CompletionQueue target, int maxItems);  
Completion waitCompletion(Deadline deadline, InterruptToken interruptToken);  
NativeHandleRegistration registerHandle(AsyncHandle handle);  
void unregisterHandle(AsyncHandle handle);  
BufferRegistration registerBuffer(BufferLease lease);  
void unregisterBuffer(BufferRegistration registration);  
void wake();  
}

## D.3 AsyncOperation and OperationBuilder

class AsyncOperation {  
OperationId id();  
OperationKind kind();  
OperationState state();  
RuntimeThread ownerThread();  
AsyncHandle handle();  
List\<AsyncHandle\> handles();  
List\<BufferLease\> bufferLeases();  
Deadline deadline();  
TimeoutPolicy timeoutPolicy();  
CancelPolicy cancelPolicy();  
CompletionSink completionSink();  
Future\<Completion\> completionFuture();  
bool tryTransition(OperationState expected, OperationState next);  
void markSubmitted(NativeOperationToken token);  
CancelResult requestCancel(CancellationReason reason);  
bool complete(Completion completion);  
void release();  
}  
  
class OperationBuilder {  
static AsyncOperation read(AsyncHandle handle, BufferLease dst, long offset, ReadOptions options);  
static AsyncOperation write(AsyncHandle handle, BufferLease src, long offset, WriteOptions options);  
static AsyncOperation accept(AsyncHandle server, AcceptOptions options);  
static AsyncOperation connect(AsyncHandle socket, NetworkAddress remote, ConnectOptions options);  
static AsyncOperation recv(AsyncHandle socket, BufferLease dst, RecvOptions options);  
static AsyncOperation send(AsyncHandle socket, BufferLease src, SendOptions options);  
static AsyncOperation poll(AsyncHandle handle, InterestOps interests, PollOptions options);  
static AsyncOperation timeout(Deadline deadline, TimeoutOptions options);  
static AsyncOperation cancel(OperationId target, CancellationReason reason);  
static AsyncOperation open(Path path, OpenOptions options);  
static AsyncOperation close(AsyncHandle handle, CloseOptions options);  
static AsyncOperation fsync(AsyncHandle file, FsyncOptions options);  
static AsyncOperation wake(WakeTarget target);  
}

## D.4 AsyncHandle and registries

class AsyncHandle {  
NativeHandle nativeHandle();  
HandleCapabilities capabilities();  
HandleState state();  
NativeHandleRegistration nativeRegistration();  
void retain();  
void release();  
bool registerOperation(OperationId id);  
void unregisterOperation(OperationId id);  
List\<OperationId\> pendingOperations();  
Future\<Void\> closeAsync(CloseOptions options);  
bool markCloseRequested();  
void onBackendClosed();  
}  
  
class HandleRegistry {  
AsyncHandle create(NativeHandle native, HandleCapabilities capabilities);  
AsyncHandle resolve(HandleId id);  
void remove(HandleId id);  
List\<AsyncHandle\> openHandlesSnapshot();  
}

## D.5 Completion and dispatch

class Completion {  
OperationId operationId();  
OperationKind operationKind();  
CompletionKind kind();  
int nativeErrorCode();  
ErrorCategory errorCategory();  
long bytesTransferred();  
CompletionFlags flags();  
CancellationReason cancellationReason();  
BackendStatus backendStatus();  
}  
  
interface CompletionSink {  
void complete(Completion completion);  
}  
  
class CompletionQueue {  
void offer(Completion completion);  
Completion poll();  
int pollBatch(List\<Completion\> out, int maxItems);  
Completion wait(Deadline deadline, InterruptToken interruptToken);  
void wake(WakeReason reason);  
void close();  
}  
  
class CompletionDispatcher {  
void installSink(OperationId id, CompletionSink sink);  
void removeSink(OperationId id);  
void dispatch(Completion completion);  
void dispatchBatch(List\<Completion\> completions);  
}

## D.6 Cancellation and scopes

class CancellationSource {  
CancellationToken token();  
bool cancel(CancellationReason reason);  
bool isCancellationRequested();  
}  
  
class CancellationToken {  
bool isCancellationRequested();  
CancellationReason reason();  
Registration onCancel(Runnable callback);  
void throwIfCancellationRequested();  
}  
  
class CancellationScope implements AutoCloseable {  
CancellationToken token();  
void attach(OperationId id);  
void detach(OperationId id);  
int cancelAll(CancellationReason reason);  
void close();  
}

## D.7 BlockingPoint and interrupt bridge

class BlockingPoint {  
static BlockingPoint enter(RuntimeThread thread, BlockingKind kind);  
void attachOperation(AsyncOperation operation);  
void setDeadline(Deadline deadline);  
WaitResult waitFor(CompletionQueue queue) throws ThreadInterruptionException;  
WaitResult waitFor(Future\<?\> future) throws ThreadInterruptionException;  
void wake(WakeReason reason);  
void leave();  
}  
  
class InterruptToken {  
bool isInterrupted();  
bool consumeInterrupted();  
void interrupt();  
void wake();  
uint64 epoch();  
AsyncOperation currentOperation();  
}

## D.8 BufferLease and pools

class BufferLease {  
void\* address();  
long length();  
BufferLease slice(long offset, long length);  
void retain();  
void release();  
void pin();  
void unpin();  
BufferRegistration register(AsyncRuntime runtime, BufferRegistrationPolicy policy);  
Optional\<int\> registrationId();  
BufferOwnership ownership();  
bool isReadable();  
bool isWritable();  
}  
  
class BufferPool {  
BufferLease acquire(long minSize, BufferAccess access);  
void release(BufferLease lease);  
BufferPoolStats stats();  
}

## D.9 Timers and deadlines

class Deadline {  
static Deadline none();  
static Deadline after(Duration duration);  
static Deadline at(Instant instant);  
bool isExpired();  
Duration remaining();  
}  
  
class TimerService {  
Future\<Void\> sleep(Duration duration);  
AsyncOperation timeout(Deadline deadline);  
bool cancelTimeout(OperationId timeoutOperationId);  
ScheduledTask scheduleAt(Instant instant, Runnable task);  
ScheduledTask scheduleAfter(Duration delay, Runnable task);  
}
