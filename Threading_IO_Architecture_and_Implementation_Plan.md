# Architecture et Plan d'Implémentation — Threading, Synchronisation et I/O

**Basé sur :** Portable_Runtime_Threading_IO_Design_Spec_v0.2.md  
**Date :** 2026-07-31  
**Statut :** Document de travail

---

## Table des matières

1. [Analyse de la spécification](#1-analyse-de-la-spécification)
2. [Architecture générale](#2-architecture-générale)
3. [Couche 1 — Substrate asynchrone (C)](#3-couche-1--substrate-asynchrone-c)
4. [Couche 2 — Façade synchrone (K + C)](#4-couche-2--façade-synchrone-k--c)
5. [Couche 3 — Proactor/Reactor (K + C)](#5-couche-3--proactorreactor-k--c)
6. [Plan d'implémentation par phases](#6-plan-dimplémentation-par-phases)
7. [Stratégie de tests](#7-stratégie-de-tests)
8. [Contraintes et décisions d'architecture](#8-contraintes-et-décisions-darchitecture)

---

## 1. Analyse de la spécification

### 1.1 Résumé des fonctionnalités demandées

La spec définit un runtime portable en trois couches :

| Couche | Rôle | Technologie |
|--------|------|-------------|
| Layer 1 | Substrate asynchrone portable | Linux: io_uring; Windows: IOCP |
| Layer 2 | Façade synchrone utilisateur | Threading, Sync, I/O avec sémantique bloquante |
| Layer 3 | Proactor/Reactor haute performance | EventLoop, Selector, Scheduler |

### 1.2 Points clés de conception

**Modèle d'interruption vs annulation :**
- `Thread.interrupt()` est un signal coopératif au niveau langage, jamais un kill POSIX
- L'annulation d'opération est orthogonale à l'interruption de thread
- Chaque point de blocage (`BlockingPoint`) doit surveiller les deux

**Invariant central :**
> Toute API bloquante publique passe par un `AsyncOperation` Layer 1. Il n'existe pas d'appel système bloquant direct dans Layer 2 ou Layer 3.

**Gestion des races critiques :**
- Interruption vs complétion d'opération : policy documentée par API
- Annulation vs complétion : les deux CQE doivent être consommées
- Close vs opération en cours : le handle gère son propre état CLOSING

### 1.3 Adéquation avec l'existant

La libk actuelle a :
- Une hiérarchie d'exceptions (`Throwable → Exception → FatalError`)
- Des streams I/O (`InputStream<T>`, `OutputStream<T>`) sans threading
- Des fichiers via FFI C (`file.c`)
- Pas de notion de thread, mutex, futur, ni d'I/O asynchrone

Les nouvelles fonctionnalités s'ajoutent **sans casser l'API existante**. Les streams actuels restent inchangés ; les nouvelles classes les complètent.

---

## 2. Architecture générale

### 2.1 Structure des namespaces

```
::k                         (libk — module k)
├── Thread                  Classe thread public
├── Runnable                Interface tâche
├── ThreadInterruptionException
├── Duration                Valeur de durée
├── Instant                 Point dans le temps
│
├── Lock                    Interface verrou
├── Mutex                   Verrou non-réentrant
├── ReentrantLock           Verrou réentrant
├── ReadWriteLock           Verrou lecture/écriture
├── Condition               Variable de condition
├── Semaphore               Sémaphore de comptage
├── CountDownLatch          Latch à décompte
├── CyclicBarrier           Barrière cyclique
│
├── Future<T>               Résultat futur en lecture seule
├── Promise<T>              Complétion d'un futur
├── CancelableFuture<T>     Futur annulable
│
└── io                      (namespace existant étendu)
    ├── IOException          Nouvelle exception I/O
    ├── InterruptedIOException
    ├── ClosedChannelException
    ├── TimeoutException     (aussi dans ::k)
    │
    ├── ByteBuffer           Buffer de bytes managé
    ├── Channel              Interface canal
    ├── ReadableChannel
    ├── WritableChannel
    ├── SelectableChannel
    │
    ├── FileInputStream      (remplace/étend file.k)
    ├── FileOutputStream
    ├── FileChannel
    ├── Path
    │
    ├── Socket
    ├── ServerSocket
    ├── SocketChannel
    ├── DatagramSocket
    ├── NetworkAddress
    │
    ├── Selector
    ├── SelectionKey
    ├── InterestOps
    │
    └── EventLoop            (Layer 3)
        CompletionHandler<T>
        ScheduledTask
```

### 2.2 Structure des fichiers source

```
libk/libk/src/
├── (existants, inchangés)
│   ├── object.k, exception.k, string.k, ...
│   └── io/stream.k, file.k, ...
│
├── thread.k                Thread, Runnable, ThreadState
├── thread_exceptions.k     ThreadInterruptionException, TimeoutException
├── time.k                  Duration, Instant, Clock
│
├── sync/
│   ├── lock.k              Lock interface
│   ├── mutex.k             Mutex, ReentrantLock
│   ├── rwlock.k            ReadWriteLock
│   ├── condition.k         Condition
│   ├── semaphore.k         Semaphore
│   └── latch.k             CountDownLatch, CyclicBarrier
│
├── future/
│   ├── future.k            Future<T>, Promise<T>
│   ├── cancelable_future.k CancelableFuture<T>
│   └── completion_stage.k  CompletionStage<T>
│
└── io/  (extension du namespace existant)
    ├── (existants inchangés)
    ├── io_exceptions.k     IOException, InterruptedIOException, etc.
    ├── byte_buffer.k       ByteBuffer
    ├── channel.k           Channel, ReadableChannel, WritableChannel
    ├── file_channel.k      FileInputStream, FileOutputStream, FileChannel, Path
    ├── socket.k            Socket, ServerSocket, SocketChannel, DatagramSocket
    ├── network_address.k   NetworkAddress
    └── selector.k          Selector, SelectionKey (Layer 3)

libk/libk/src/runtime/   (C — Layer 1, jamais exposé en K directement)
├── async_runtime.h/.c      AsyncRuntime, BackendDriver, init/shutdown
├── async_operation.h/.c    AsyncOperation, OperationRegistry, OperationId
├── async_handle.h/.c       AsyncHandle, HandleRegistry
├── completion.h/.c         Completion, CompletionQueue, CompletionDispatcher
├── buffer_lease.h/.c       BufferLease, BufferPool
├── cancellation.h/.c       CancellationToken, CancellationScope
├── interrupt_token.h/.c    InterruptToken, BlockingPoint
├── timer.h/.c              Deadline, TimerService
├── runtime_thread.h/.c     RuntimeThread, ThreadRegistry, ThreadControlBlock
│
├── linux/                  Backend Linux (io_uring)
│   ├── uring_backend.h/.c  BackendDriver impl pour io_uring
│   ├── uring_ops.h/.c      Préparation des SQE par type d'opération
│   └── uring_cancel.h/.c   Annulation et drain
│
└── win32/                  Backend Windows (IOCP) — Phase ultérieure
    ├── iocp_backend.h/.c
    └── iocp_ops.h/.c
```

### 2.3 Dépendances de build

```
Layer 1 (C runtime)
    liburing (Linux) / Win32 SDK (Windows)
    → compile en libk_runtime.a (statique, interne)

Layer 2 (K + C FFI)
    ← libk_runtime.a
    ← libk.so (exception, string, collections)
    → libk.so / libk.a (enrichi)

Layer 3 (K + C FFI)
    ← Layer 2
    → inclus dans libk.so
```

---

## 3. Couche 1 — Substrate asynchrone (C)

### 3.1 Sous-systèmes et classes principales

#### 3.1.1 AsyncRuntime

Point d'entrée singleton du runtime asynchrone. Une seule instance par processus (default runtime), créée au démarrage du premier Thread.

```c
// runtime/async_runtime.h
typedef struct AsyncRuntime AsyncRuntime;

AsyncRuntime* k_runtime_create(const RuntimeConfig* config);
void          k_runtime_start(AsyncRuntime* rt);
void          k_runtime_shutdown(AsyncRuntime* rt, ShutdownMode mode);
RuntimeState  k_runtime_state(AsyncRuntime* rt);

// Submission
AsyncOperation* k_runtime_submit(AsyncRuntime* rt, AsyncOperation* op);
int             k_runtime_submit_batch(AsyncRuntime* rt, AsyncOperation** ops, int n);

// Cancellation
CancelResult k_runtime_cancel(AsyncRuntime* rt, OperationId id, CancellationReason reason);
int          k_runtime_cancel_handle(AsyncRuntime* rt, AsyncHandle* h, CancellationReason reason);

// Handle and buffer management
AsyncHandle*      k_runtime_register_handle(AsyncRuntime* rt, NativeHandle native, HandleCapabilities caps);
void              k_runtime_unregister_handle(AsyncRuntime* rt, AsyncHandle* h);
BufferLease*      k_runtime_lease_buffer(AsyncRuntime* rt, void* buf, size_t len, BufferAccess access);
BufferLease*      k_runtime_alloc_buffer(AsyncRuntime* rt, size_t len, BufferAccess access);

BackendCapabilities k_runtime_capabilities(AsyncRuntime* rt);
RuntimeMetrics      k_runtime_metrics(AsyncRuntime* rt);
AsyncRuntime*       k_runtime_default(void);  // lazy singleton
```

#### 3.1.2 OperationId et OperationRegistry

```c
typedef struct { uint32_t index; uint32_t generation; } OperationId;

// Registry slot : index + génération (ABA-safe) + pointeur opération
typedef struct OperationRegistry OperationRegistry;

OperationRegistry*  k_opregistry_create(uint32_t capacity);
OperationId         k_opregistry_allocate(OperationRegistry* reg, AsyncOperation* op);
AsyncOperation*     k_opregistry_resolve(OperationRegistry* reg, OperationId id);
void                k_opregistry_release(OperationRegistry* reg, OperationId id);
```

#### 3.1.3 AsyncOperation

```c
typedef enum {
    OP_KIND_READ, OP_KIND_WRITE, OP_KIND_ACCEPT, OP_KIND_CONNECT,
    OP_KIND_RECV, OP_KIND_SEND, OP_KIND_POLL, OP_KIND_TIMEOUT,
    OP_KIND_CANCEL, OP_KIND_OPEN, OP_KIND_CLOSE, OP_KIND_FSYNC,
    OP_KIND_WAKE, OP_KIND_INTERNAL
} OperationKind;

typedef enum {
    OP_STATE_NEW, OP_STATE_PREPARED, OP_STATE_SUBMITTED,
    OP_STATE_IN_FLIGHT, OP_STATE_CANCEL_REQUESTED,
    OP_STATE_COMPLETED, OP_STATE_DELIVERED, OP_STATE_RELEASED
} OperationState;

typedef struct AsyncOperation AsyncOperation;

// Factory — OperationBuilder
AsyncOperation* k_op_read    (AsyncHandle* h, BufferLease* dst, int64_t offset, ReadOptions opts);
AsyncOperation* k_op_write   (AsyncHandle* h, BufferLease* src, int64_t offset, WriteOptions opts);
AsyncOperation* k_op_accept  (AsyncHandle* server, AcceptOptions opts);
AsyncOperation* k_op_connect (AsyncHandle* sock, const NetworkAddress* addr, ConnectOptions opts);
AsyncOperation* k_op_recv    (AsyncHandle* sock, BufferLease* dst, RecvOptions opts);
AsyncOperation* k_op_send    (AsyncHandle* sock, BufferLease* src, SendOptions opts);
AsyncOperation* k_op_poll    (AsyncHandle* h, InterestOps interests, PollOptions opts);
AsyncOperation* k_op_timeout (Deadline deadline, TimeoutOptions opts);
AsyncOperation* k_op_cancel  (OperationId target, CancellationReason reason);
AsyncOperation* k_op_open    (const char* path, OpenOptions opts);
AsyncOperation* k_op_close   (AsyncHandle* h, CloseOptions opts);
AsyncOperation* k_op_fsync   (AsyncHandle* h, FsyncOptions opts);
AsyncOperation* k_op_wake    (WakeTarget target);

// Méthodes
OperationId     k_op_id(const AsyncOperation* op);
OperationKind   k_op_kind(const AsyncOperation* op);
OperationState  k_op_state(const AsyncOperation* op);
AsyncHandle*    k_op_handle(const AsyncOperation* op);
Deadline        k_op_deadline(const AsyncOperation* op);
bool            k_op_try_transition(AsyncOperation* op, OperationState expected, OperationState next);
void            k_op_mark_submitted(AsyncOperation* op, NativeOperationToken token);
CancelResult    k_op_request_cancel(AsyncOperation* op, CancellationReason reason);
bool            k_op_complete(AsyncOperation* op, Completion completion);
void            k_op_release(AsyncOperation* op);
Future*         k_op_completion_future(AsyncOperation* op);
```

#### 3.1.4 AsyncHandle

```c
typedef enum {
    HANDLE_STATE_OPEN, HANDLE_STATE_CLOSING_REQUESTED,
    HANDLE_STATE_CANCEL_PENDING, HANDLE_STATE_DRAIN_PENDING,
    HANDLE_STATE_RELEASE_NATIVE, HANDLE_STATE_CLOSED
} HandleState;

typedef struct AsyncHandle AsyncHandle;

HandleState  k_handle_state(const AsyncHandle* h);
void         k_handle_retain(AsyncHandle* h);
void         k_handle_release(AsyncHandle* h);
bool         k_handle_register_op(AsyncHandle* h, OperationId id);
void         k_handle_unregister_op(AsyncHandle* h, OperationId id);
bool         k_handle_mark_close_requested(AsyncHandle* h);  // OPEN → CLOSING_REQUESTED
void         k_handle_on_backend_closed(AsyncHandle* h);     // transition finale → CLOSED
Future*      k_handle_close_async(AsyncHandle* h, CloseOptions opts);
int          k_handle_pending_count(const AsyncHandle* h);
NativeHandle k_handle_native(const AsyncHandle* h);          // Layer 1 uniquement
```

#### 3.1.5 Completion et CompletionQueue

```c
typedef enum {
    COMPLETION_SUCCESS, COMPLETION_PARTIAL_SUCCESS,
    COMPLETION_WOULD_BLOCK, COMPLETION_CANCELED,
    COMPLETION_TIMEOUT, COMPLETION_CLOSED, COMPLETION_FAILURE
} CompletionKind;

typedef struct {
    OperationId  operationId;
    OperationKind operationKind;
    CompletionKind kind;
    int          nativeErrorCode;
    int64_t      bytesTransferred;
    uint32_t     flags;
    CancellationReason cancellationReason;
} Completion;

typedef struct CompletionQueue CompletionQueue;

CompletionQueue* k_cq_create(QueuePolicy policy);
void             k_cq_offer(CompletionQueue* q, Completion c);
bool             k_cq_poll(CompletionQueue* q, Completion* out);
int              k_cq_poll_batch(CompletionQueue* q, Completion* out, int max);
Completion       k_cq_wait(CompletionQueue* q, Deadline deadline, InterruptToken* token);
void             k_cq_wake(CompletionQueue* q, WakeReason reason);
void             k_cq_close(CompletionQueue* q);

// Dispatcher
typedef struct CompletionDispatcher CompletionDispatcher;

void k_dispatcher_install_sink(CompletionDispatcher* d, OperationId id, CompletionSink sink);
void k_dispatcher_remove_sink(CompletionDispatcher* d, OperationId id);
void k_dispatcher_dispatch(CompletionDispatcher* d, Completion c);
void k_dispatcher_dispatch_batch(CompletionDispatcher* d, Completion* batch, int n);
```

#### 3.1.6 InterruptToken et BlockingPoint

```c
typedef struct {
    _Atomic uint32_t  interrupted;     // flag atomique
    _Atomic uint64_t  epoch;           // protection ABA
    _Atomic AsyncOperation* currentOp; // opération en cours ou NULL
    void*             wakeHandle;      // futex addr ou event handle
} InterruptToken;

void     k_itoken_interrupt(InterruptToken* t);
bool     k_itoken_is_interrupted(const InterruptToken* t);
bool     k_itoken_consume_interrupted(InterruptToken* t);  // lit et efface
void     k_itoken_wake(InterruptToken* t);
uint64_t k_itoken_epoch(const InterruptToken* t);

typedef enum {
    BP_KIND_SLEEP, BP_KIND_JOIN, BP_KIND_CONDITION,
    BP_KIND_FUTURE, BP_KIND_IO, BP_KIND_LOCK
} BlockingKind;

typedef enum {
    WAIT_RESULT_COMPLETED, WAIT_RESULT_INTERRUPTED,
    WAIT_RESULT_TIMEOUT, WAIT_RESULT_CANCELED
} WaitResult;

typedef struct BlockingPoint BlockingPoint;

BlockingPoint* k_bp_enter(RuntimeThread* thread, BlockingKind kind);
void           k_bp_attach_operation(BlockingPoint* bp, AsyncOperation* op);
void           k_bp_set_deadline(BlockingPoint* bp, Deadline dl);
WaitResult     k_bp_wait_completion(BlockingPoint* bp, CompletionQueue* q);
WaitResult     k_bp_wait_future(BlockingPoint* bp, Future* f);
void           k_bp_wake(BlockingPoint* bp, WakeReason reason);
void           k_bp_leave(BlockingPoint* bp);
```

#### 3.1.7 RuntimeThread et ThreadRegistry

```c
typedef enum {
    THREAD_STATE_NEW, THREAD_STATE_RUNNABLE, THREAD_STATE_RUNNING,
    THREAD_STATE_BLOCKED_SLEEP, THREAD_STATE_BLOCKED_IO,
    THREAD_STATE_BLOCKED_LOCK, THREAD_STATE_BLOCKED_FUTURE,
    THREAD_STATE_BLOCKED_JOIN, THREAD_STATE_TERMINATED
} ThreadState;

typedef struct RuntimeThread {
    uint64_t          threadId;
    pthread_t         osThread;           // Linux
    InterruptToken    interruptToken;
    _Atomic ThreadState state;
    BlockingPoint*    currentBlockingPoint;
    AsyncRuntime*     runtime;
    void*             tlsBlock;
} RuntimeThread;

RuntimeThread* k_thread_create(Runnable fn, void* arg, ThreadConfig* cfg);
void           k_thread_start(RuntimeThread* t);
RuntimeThread* k_thread_current(void);        // TLS
void           k_thread_interrupt(RuntimeThread* t);
bool           k_thread_is_interrupted(const RuntimeThread* t);
bool           k_thread_interrupted(void);    // courant, efface
void           k_thread_check_interrupted(void);  // throw si interrompu
void           k_thread_join(RuntimeThread* t, Deadline dl);
void           k_thread_sleep(Duration duration);
void           k_thread_yield(void);

typedef struct ThreadRegistry ThreadRegistry;

ThreadRegistry* k_tregistry_global(void);
void            k_tregistry_register(ThreadRegistry* r, RuntimeThread* t);
void            k_tregistry_unregister(ThreadRegistry* r, RuntimeThread* t);
RuntimeThread*  k_tregistry_find(ThreadRegistry* r, uint64_t threadId);
```

#### 3.1.8 Futures et Promises

```c
typedef struct Future Future;
typedef struct Promise Promise;

// Promise
Promise* k_promise_create(void);
bool     k_promise_try_success(Promise* p, void* value, size_t size);
bool     k_promise_try_failure(Promise* p, Throwable* error);
bool     k_promise_try_cancel(Promise* p, CancellationReason reason);
Future*  k_promise_future(Promise* p);

// Future
bool          k_future_is_done(const Future* f);
bool          k_future_is_cancelled(const Future* f);
void*         k_future_get(Future* f, InterruptToken* token);         // bloquant interruptible
void*         k_future_get_timed(Future* f, Deadline dl, InterruptToken* token);
bool          k_future_cancel(Future* f, CancellationReason reason);
void          k_future_add_callback(Future* f, void (*cb)(Completion, void*), void* ctx);
```

#### 3.1.9 Backend Linux io_uring

```c
// runtime/linux/uring_backend.h
typedef struct UringBackend UringBackend;

// Implémente BackendDriver via table de fonctions
extern const BackendDriverVTable k_uring_driver_vtable;

UringBackend* k_uring_create(const RuntimeConfig* cfg, CompletionQueue* sysQueue);

// Boucle de complétion (interne, tourne dans worker thread)
void k_uring_completion_loop(UringBackend* b);

// Préparation des SQE (uring_ops.c)
void k_uring_prepare_read   (struct io_uring_sqe* sqe, AsyncOperation* op);
void k_uring_prepare_write  (struct io_uring_sqe* sqe, AsyncOperation* op);
void k_uring_prepare_accept (struct io_uring_sqe* sqe, AsyncOperation* op);
void k_uring_prepare_connect(struct io_uring_sqe* sqe, AsyncOperation* op);
void k_uring_prepare_recv   (struct io_uring_sqe* sqe, AsyncOperation* op);
void k_uring_prepare_send   (struct io_uring_sqe* sqe, AsyncOperation* op);
void k_uring_prepare_poll   (struct io_uring_sqe* sqe, AsyncOperation* op);
void k_uring_prepare_timeout(struct io_uring_sqe* sqe, AsyncOperation* op);
void k_uring_prepare_cancel (struct io_uring_sqe* sqe, AsyncOperation* op);
void k_uring_prepare_openat (struct io_uring_sqe* sqe, AsyncOperation* op);
void k_uring_prepare_close  (struct io_uring_sqe* sqe, AsyncOperation* op);
void k_uring_prepare_fsync  (struct io_uring_sqe* sqe, AsyncOperation* op);

// Translation CQE → Completion portable (uring_cancel.c)
Completion k_uring_translate_cqe(const struct io_uring_cqe* cqe, OperationRegistry* reg);
```

**Topologie ring :**
- Une ring io_uring par worker thread (SQPOLL optionnel)
- Worker thread dédié à la boucle de complétion par ring
- `msg_ring` pour wakeup cross-thread (io_uring 5.18+, sinon eventfd)

**Algorithme de soumission :**
```
submit(op):
  1. Allouer slot dans OperationRegistry → obtenir OperationId
  2. Acquérir SQE (flush si ring pleine)
  3. Préparer SQE avec op.id.raw dans user_data
  4. op.markSubmitted(sqe.user_data)
  5. k_handle_register_op(op.handle, op.id)
  6. io_uring_submit()
```

**Algorithme de complétion :**
```
completion_loop(ring):
  while runtime.running:
    n = io_uring_peek_batch_cqe(ring, cqes, BATCH_SIZE)
    if n == 0:
      io_uring_wait_cqe_timeout(ring, deadline)
      continue
    for cqe in cqes[0..n]:
      comp = k_uring_translate_cqe(cqe, registry)
      op = k_opregistry_resolve(registry, comp.operationId)
      if op == STALE: log_fatal_integrity_error(); continue
      if k_op_complete(op, comp):
        k_dispatcher_dispatch(dispatcher, comp)
        k_handle_unregister_op(op.handle, op.id)
      else:
        record_late_completion(comp)
    io_uring_cq_advance(ring, n)
    run_ready_tasks(scheduler)
```

### 3.2 BufferLease et gestion mémoire

```c
typedef enum {
    BUF_OWN_OWNED,     // runtime a alloué
    BUF_OWN_BORROWED,  // caller fournit, runtime étend la durée de vie
    BUF_OWN_REGISTERED // fixed buffer io_uring
} BufferOwnership;

typedef struct BufferLease {
    void*           address;
    size_t          length;
    _Atomic int32_t refcount;
    BufferOwnership ownership;
    int             registrationId;  // -1 si non enregistré
    bool            pinned;
} BufferLease;
```

**Règles de durée de vie :**
- Un `AsyncOperation` retient (`retain`) tous ses `BufferLease` à la soumission
- `k_op_release()` appelle `release()` sur chaque lease
- Les buffers empruntés (stack) sont garantis stables par la façade synchrone (appel bloquant = durée de vie stack)

### 3.3 Timers et deadlines

```c
typedef struct {
    int64_t nanoseconds_since_epoch;  // CLOCK_MONOTONIC
    bool    is_none;
} Deadline;

Deadline k_deadline_none(void);
Deadline k_deadline_after(int64_t nanos);
Deadline k_deadline_at(int64_t epoch_nanos);
bool     k_deadline_is_expired(Deadline d);
int64_t  k_deadline_remaining_nanos(Deadline d);

// Sur Linux : io_uring_prep_timeout + linked_timeout pour lier à une opération
// Sur Linux : timerfd comme fallback si io_uring < 5.4
AsyncOperation* k_timer_sleep(Duration duration);
AsyncOperation* k_timer_linked_timeout(AsyncOperation* target, Deadline deadline);
```

---

## 4. Couche 2 — Façade synchrone (K + C)

### 4.1 Nouvelles exceptions (K)

**`thread_exceptions.k`** — namespace `::k`

```k
public class TimeoutException : public Exception {
    TimeoutException() : Exception(100) {}
    TimeoutException(code: int) : Exception(code) {}
}

public class ThreadInterruptionException : public Exception {
    ThreadInterruptionException() : Exception(101) {}
}

public class CancellationException : public Exception {
    CancellationException() : Exception(102) {}
}
```

**`io/io_exceptions.k`** — namespace `::k::io`

```k
public class IOException : public ::k::Exception { ... }
public class InterruptedIOException : public IOException {
    _bytesTransferred: long;
    bytesTransferred() const : long { return _bytesTransferred; }
    ...
}
public class ClosedChannelException : public IOException { ... }
public class EndOfStreamException : public IOException { ... }
```

### 4.2 Duration et Instant (K)

**`time.k`** — namespace `::k`

```k
public struct Duration {
    _nanos: long;

    public:
    static zero() : Duration { ... }
    static ofNanos(n: long) : Duration { ... }
    static ofMillis(ms: long) : Duration { ... }
    static ofSeconds(s: long) : Duration { ... }
    toNanos() const : long { return _nanos; }
    toMillis() const : long { return _nanos / 1_000_000; }
    operator+(other: Duration const&) : Duration { ... }
    operator<(other: Duration const&) : bool { ... }
    // ...
}

public struct Instant {
    _epochNanos: long;
    public:
    static now() : Instant;  // FFI vers clock_gettime
    plus(d: Duration) : Instant { ... }
    minus(other: Instant) : Duration { ... }
    isBefore(other: Instant const&) : bool { ... }
}
```

FFI C minimal pour `Instant::now()` dans `time.c`.

### 4.3 Thread (K + C FFI)

**`thread.k`** — namespace `::k`

```k
public interface Runnable {
    run() : void;
}

public class Thread {
    _native: void*;  // RuntimeThread* opaque

    public:
    // Constructeurs
    Thread(task: Runnable!);
    Thread(task: Runnable!, name: String const&);

    // Cycle de vie
    start() : void;
    join() : void throws ThreadInterruptionException;
    join(timeout: Duration) : void throws ThreadInterruptionException, TimeoutException;

    // Interruption
    interrupt() : void;
    isInterrupted() const : bool;

    // Statiques
    static current() : Thread&;
    static interrupted() : bool;               // lit et efface
    static checkInterrupted() : void throws ThreadInterruptionException;
    static sleep(duration: Duration) : void throws ThreadInterruptionException;
    static yield() : void;
}
```

**FFI C (thread_ffi.c) :**
```c
void   k_Thread_start(void* nativeThread);
void   k_Thread_join(void* nativeThread, int64_t timeoutNanos);
void   k_Thread_interrupt(void* nativeThread);
bool   k_Thread_isInterrupted(const void* nativeThread);
bool   k_Thread_interrupted_static(void);
void   k_Thread_check_interrupted(void);
void   k_Thread_sleep(int64_t nanos);
void   k_Thread_yield(void);
void*  k_Thread_current(void);
void*  k_Thread_create(void* runnableObj, const char* name);
```

### 4.4 Synchronisation (K + C FFI)

**`sync/mutex.k`** — namespace `::k`

```k
public interface Lock {
    lock() : void;
    lockInterruptibly() : void throws ThreadInterruptionException;
    tryLock() : bool;
    tryLock(timeout: Duration) : bool throws ThreadInterruptionException;
    unlock() : void;
    newCondition() : Condition!;
}

public class Mutex : public Lock {
    _native: void*;  // opaque futex/pthread_mutex
    // ...
}

public class ReentrantLock : public Lock {
    _native: void*;
    _owner: void*;     // RuntimeThread* ou null
    _count: int;
    // ...
}
```

**`sync/condition.k`** — namespace `::k`

```k
public class Condition {
    _native: void*;   // k_Condition C struct

    await() : void throws ThreadInterruptionException;
    await(timeout: Duration) : bool throws ThreadInterruptionException;
    awaitUninterruptibly() : void;
    signal() : void;
    signalAll() : void;
}
```

**`sync/semaphore.k`** — namespace `::k`

```k
public class Semaphore {
    _native: void*;

    acquire() : void throws ThreadInterruptionException;
    tryAcquire() : bool;
    tryAcquire(timeout: Duration) : bool throws ThreadInterruptionException;
    release() : void;
    availablePermits() const : int;
}
```

**`sync/latch.k`** — namespace `::k`

```k
public class CountDownLatch {
    _native: void*;

    await() : void throws ThreadInterruptionException;
    await(timeout: Duration) : bool throws ThreadInterruptionException;
    countDown() : void;
    count() const : long;
}

public class CyclicBarrier {
    _native: void*;

    await() : int throws ThreadInterruptionException;
    await(timeout: Duration) : int throws ThreadInterruptionException;
    reset() : void;
    parties() const : int;
    numberWaiting() const : int;
}
```

**Implémentation C des primitives de synchronisation (`sync_primitives.c`) :**

```c
// Mutex — basé sur futex Linux (FUTEX_WAIT / FUTEX_WAKE)
// Implémentation similaire à un mutex non-récursif userspace
typedef struct {
    _Atomic uint32_t state;  // 0=unlocked, 1=locked, 2=locked+waiters
    InterruptToken*  ownerToken;
} k_Mutex;

// Condition — utilise futex + liste de waiters
typedef struct {
    _Atomic uint32_t seq;       // séquence pour détecter les signaux
    k_Mutex*         assocMutex;
} k_Condition;

// Semaphore — compteur atomique + futex park/unpark
typedef struct {
    _Atomic int32_t count;
    _Atomic uint32_t waiters;
} k_Semaphore;
```

**Algorithme `Condition.await` (gestion des races) :**
```
await(condition, mutex):
  1. if thread.interrupted → throw ThreadInterruptionException
  2. Ajouter thread dans waiter list (sous verrou mutex)
  3. Relâcher mutex atomiquement + marquer BlockingPoint
  4. futex_wait(condition.seq, current_seq)
  5. Réacquérir mutex
  6. Retirer thread de waiter list
  7. if woke_by_signal:
       if thread.interrupted: préserver flag, retourner normalement
       else: retourner normalement
  8. if woke_by_interrupt: throw ThreadInterruptionException
```

### 4.5 Futures et Promises (K + C FFI)

**`future/future.k`** — namespace `::k`

```k
template<T>
public class Future {
    _native: void*;   // k_Future C struct (type-erased)

    isDone() const : bool;
    isCancelled() const : bool;
    get() : T throws ThreadInterruptionException, ExecutionException;
    get(timeout: Duration) : T throws ThreadInterruptionException, ExecutionException, TimeoutException;
    cancel() : bool;
    cancel(reason: int) : bool;
}

template<T>
public class Promise {
    _native: void*;

    trySuccess(value: T) : bool;
    tryFailure(error: Throwable!) : bool;
    tryCancel(reason: int) : bool;
    future() : Future<T>!;
}
```

**`future/cancelable_future.k`** — namespace `::k`

```k
template<T>
public class CancelableFuture : public Future<T> {
    _cancelToken: void*;

    cancelToken() : void*;  // opaque pour l'instant
    cancelWithToken(reason: int) : bool;
}
```

### 4.6 I/O synchrone (K + C FFI)

#### 4.6.1 ByteBuffer

**`io/byte_buffer.k`** — namespace `::k::io`

```k
public class ByteBuffer {
    _data: byte[];
    _position: unsigned int;
    _limit: unsigned int;
    _capacity: unsigned int;

    public:
    static allocate(capacity: unsigned int) : ByteBuffer!;
    static wrap(array: byte[]) : ByteBuffer!;

    position() const : unsigned int;
    limit() const : unsigned int;
    capacity() const : unsigned int;
    remaining() const : unsigned int;
    hasRemaining() const : bool;

    get() : byte;
    get(index: unsigned int) : byte;
    put(b: byte) : ByteBuffer&;
    put(index: unsigned int, b: byte) : ByteBuffer&;
    put(src: const byte[]) : ByteBuffer&;

    flip() : ByteBuffer&;
    clear() : ByteBuffer&;
    compact() : ByteBuffer&;
    rewind() : ByteBuffer&;
}
```

#### 4.6.2 Canal et hiérarchie

**`io/channel.k`** — namespace `::k::io`

```k
public interface Channel {
    isOpen() const : bool;
    close() : void throws IOException;
}

public interface ReadableChannel : public Channel {
    read(dst: ByteBuffer&) : int throws IOException, ThreadInterruptionException;
    readAsync(dst: ByteBuffer&) : ::k::Future<int>!;
}

public interface WritableChannel : public Channel {
    write(src: ByteBuffer const&) : int throws IOException, ThreadInterruptionException;
    writeAsync(src: ByteBuffer const&) : ::k::Future<int>!;
}

public interface SelectableChannel : public Channel {
    // Configuré pour être utilisé avec Selector (Layer 3)
    configureBlocking(blocking: bool) : SelectableChannel&;
    isBlocking() const : bool;
}
```

#### 4.6.3 Fichiers

**`io/file_channel.k`** — namespace `::k::io`

```k
public class Path {
    _native: void*;  // string path interne

    public:
    static of(s: String const&) : Path!;
    toString() const : String!;
    resolve(other: Path const&) : Path!;
    parent() const : ::k::Optional<Path>;
    fileName() const : String!;
    exists() const : bool;
    isDirectory() const : bool;
}

public class FileInputStream : public ::k::io::InputStream<byte>, public ReadableChannel {
    _native: void*;  // AsyncHandle*

    public:
    FileInputStream(path: Path const&) throws IOException;

    read(dst: ByteBuffer&) : int throws IOException, ThreadInterruptionException;
    readFully(dst: ByteBuffer&) : void throws IOException, ::k::InterruptedIOException;
    skip(count: long) : long throws IOException, ThreadInterruptionException;

    // implémentation de InputStream<byte>
    read() : ::k::Optional<byte>;
    read(buf: byte[], off: unsigned int, len: unsigned int) : Expected<unsigned int, ::k::io::StreamOutOfData>;
    available() : Expected<unsigned int, ::k::io::StreamOutOfData>;
    close() : void;
}

public class FileOutputStream : public ::k::io::OutputStream<byte>, public WritableChannel {
    _native: void*;

    public:
    FileOutputStream(path: Path const&) throws IOException;
    FileOutputStream(path: Path const&, append: bool) throws IOException;

    write(src: ByteBuffer const&) : int throws IOException, ThreadInterruptionException;
    writeFully(src: ByteBuffer const&) : void throws IOException, ::k::InterruptedIOException;
    flush() : void throws IOException, ThreadInterruptionException;
    close() : void;
}

public class FileChannel : public ReadableChannel, public WritableChannel {
    _native: void*;

    public:
    static open(path: Path const&, opts: int) : FileChannel! throws IOException;

    read(dst: ByteBuffer&, position: long) : int throws IOException, ThreadInterruptionException;
    write(src: ByteBuffer const&, position: long) : int throws IOException, ThreadInterruptionException;
    readFully(dst: ByteBuffer&, position: long) : void throws IOException, ::k::InterruptedIOException;
    writeFully(src: ByteBuffer const&, position: long) : void throws IOException, ::k::InterruptedIOException;

    size() const : long throws IOException;
    truncate(size: long) : FileChannel& throws IOException;
    force(metaData: bool) : void throws IOException, ThreadInterruptionException;  // fsync
    transferTo(position: long, count: long, target: WritableChannel&) : long throws IOException;
    close() : void throws IOException;
}
```

#### 4.6.4 Sockets

**`io/socket.k`** — namespace `::k::io`

```k
public class NetworkAddress {
    _native: void*;

    public:
    static ofIPv4(ip: String const&, port: unsigned short) : NetworkAddress! throws IOException;
    static ofIPv6(ip: String const&, port: unsigned short) : NetworkAddress! throws IOException;
    host() const : String!;
    port() const : unsigned short;
    toString() const : String!;
}

public class Socket : public ReadableChannel, public WritableChannel {
    _native: void*;

    public:
    Socket();  // crée un socket TCP non connecté
    Socket(addr: NetworkAddress const&) throws IOException, ThreadInterruptionException;

    connect(addr: NetworkAddress const&) : void throws IOException, ThreadInterruptionException;
    connect(addr: NetworkAddress const&, timeout: ::k::Duration) : void throws IOException, ThreadInterruptionException, ::k::TimeoutException;

    read(dst: ByteBuffer&) : int throws IOException, ThreadInterruptionException;
    readFully(dst: ByteBuffer&) : void throws IOException, ::k::InterruptedIOException;
    write(src: ByteBuffer const&) : int throws IOException, ThreadInterruptionException;
    writeFully(src: ByteBuffer const&) : void throws IOException, ::k::InterruptedIOException;
    flush() : void throws IOException;

    setSoTimeout(timeout: ::k::Duration) : void;
    getSoTimeout() const : ::k::Duration;
    isConnected() const : bool;
    close() : void throws IOException;
}

public class ServerSocket {
    _native: void*;

    public:
    ServerSocket(port: unsigned short) throws IOException;
    ServerSocket(addr: NetworkAddress const&) throws IOException;

    accept() : Socket! throws IOException, ThreadInterruptionException;
    accept(timeout: ::k::Duration) : Socket! throws IOException, ThreadInterruptionException, ::k::TimeoutException;

    bind(addr: NetworkAddress const&) : void throws IOException;
    close() : void throws IOException;
    isBound() const : bool;
    isClosed() const : bool;
}

public class SocketChannel : public SelectableChannel, public ReadableChannel, public WritableChannel {
    _native: void*;

    public:
    static open() : SocketChannel! throws IOException;

    connect(addr: NetworkAddress const&) : bool throws IOException;
    finishConnect() : bool throws IOException;
    isConnected() const : bool;
    isConnectionPending() const : bool;

    read(dst: ByteBuffer&) : int throws IOException;
    write(src: ByteBuffer const&) : int throws IOException;
    close() : void throws IOException;
}
```

**Implémentation Layer 2 (patron général) :**
```
FileInputStream.read(dst: ByteBuffer):
  1. k_thread_check_interrupted()
  2. op = k_op_read(handle, lease_of(dst), STREAM_POSITION, opts)
  3. future = k_runtime_submit(runtime, op)
  4. bp = k_bp_enter(current_thread, BP_KIND_IO)
  5. k_bp_attach_operation(bp, op)
  6. result = k_bp_wait_future(bp, future)
  7. k_bp_leave(bp)
  8. if result == WAIT_RESULT_INTERRUPTED:
       k_runtime_cancel(runtime, op.id, CANCEL_THREAD_INTERRUPT)
       drain_until_terminal(future)
       throw ThreadInterruptionException
  9. comp = k_future_get_nowait(future)
  10. return translate_completion_to_read_result(comp)
```

---

## 5. Couche 3 — Proactor/Reactor (K + C)

### 5.1 EventLoop (proactor)

**`io/event_loop.k`** — namespace `::k::io`

```k
public interface CompletionHandler<T> {
    completed(result: T) : void;
    failed(error: ::k::Exception!) : void;
    cancelled(reason: int) : void;
}

public class EventLoop {
    _native: void*;

    public:
    static create() : EventLoop!;

    run() : void;
    runOnce() : void;
    stop() : void;
    isRunning() const : bool;

    submit<T>(op: void*) : ::k::Future<T>!;
    schedule(task: ::k::Runnable!) : void;
    scheduleAfter(delay: ::k::Duration, task: ::k::Runnable!) : ScheduledTask!;
}

public class ScheduledTask {
    cancel() : bool;
    isDone() const : bool;
    isCancelled() const : bool;
}
```

### 5.2 Selector (reactor)

**`io/selector.k`** — namespace `::k::io`

```k
public enum InterestOps {
    READ   = 1;
    WRITE  = 4;
    ACCEPT = 16;
    CONNECT = 8;
}

public class SelectionKey {
    _channel: SelectableChannel*;
    _interestOps: int;
    _readyOps: int;
    _attachment: void*;

    public:
    channel() const : SelectableChannel&;
    interestOps() const : int;
    readyOps() const : int;
    isReadable() const : bool;
    isWritable() const : bool;
    isAcceptable() const : bool;
    isConnectable() const : bool;
    cancel() : void;
    isValid() const : bool;
}

public class Selector {
    _native: void*;

    public:
    static open() : Selector! throws IOException;

    register(channel: SelectableChannel&, ops: int) : SelectionKey! throws IOException;
    select() : int throws IOException, ::k::ThreadInterruptionException;
    select(timeout: ::k::Duration) : int throws IOException, ::k::ThreadInterruptionException;
    selectNow() : int throws IOException;
    wakeup() : Selector&;
    selectedKeys() : ::k::Set<SelectionKey&>!;
    keys() : ::k::Set<SelectionKey&>!;
    close() : void throws IOException;
}
```

**Implémentation Linux :** `io_uring poll add` par canal enregistré. Fallback epoll si io_uring < 5.7.  
`wakeup()` envoie un wake SQE ou écrit dans un eventfd dédié.

### 5.3 Scheduler (interne, futur)

Composant interne non exposé publiquement dans cette version. Gère :
- La pool de carrier threads
- Les files runnable par shard
- Le parking de threads bloqués (pour futurs virtual threads)

---

## 6. Plan d'implémentation par phases

### Phase 1 — Runtime de base et threading (Layer 1 + Layer 2 thread)

**Objectif :** `Thread.sleep()`, `Thread.interrupt()`, `Thread.join()` fonctionnels.

**Livrables :**

| # | Composant | Fichiers |
|---|-----------|---------|
| 1.1 | `Duration`, `Instant`, `Clock` | `time.k`, `time.c` |
| 1.2 | Exceptions de thread | `thread_exceptions.k` |
| 1.3 | `RuntimeThread`, `InterruptToken`, `ThreadRegistry` | `runtime/runtime_thread.h/.c` |
| 1.4 | `BlockingPoint` (sleep seulement) | `runtime/blocking_point.h/.c` |
| 1.5 | `AsyncRuntime` init/shutdown minimale | `runtime/async_runtime.h/.c` |
| 1.6 | Backend io_uring minimal (timeout, wake) | `runtime/linux/uring_backend.h/.c` |
| 1.7 | `Thread` K class (start/join/sleep/interrupt) | `thread.k`, `thread_ffi.c` |
| 1.8 | Tests | `libk/tests/test-thread-basic.cpp` |

**Tests Phase 1 :**
- Thread démarre et exécute un Runnable
- Thread.sleep() attend la durée
- Thread.interrupt() sur sleep → `ThreadInterruptionException`
- Thread.join() attend la terminaison
- Thread.join() interruptible
- interrupted() efface le flag
- checkInterrupted() throw si interrompu

**Dépendances système :** `liburing` (>= 2.1), `pthread`, `futex`

---

### Phase 2 — Futures et Promises (Layer 1D + Layer 2)

**Objectif :** `Promise<T>`, `Future<T>`, `Future.get()` interruptible.

**Livrables :**

| # | Composant | Fichiers |
|---|-----------|---------|
| 2.1 | `OperationId`, `OperationRegistry` | `runtime/async_operation.h/.c` |
| 2.2 | `Completion`, `CompletionQueue` | `runtime/completion.h/.c` |
| 2.3 | `Promise<T>`, `Future<T>` (C runtime) | `runtime/future.h/.c` |
| 2.4 | `BlockingPoint.waitFuture()` | `runtime/blocking_point.c` (extension) |
| 2.5 | `Future<T>`, `Promise<T>` K wrappers | `future/future.k`, `future/promise.k` |
| 2.6 | `CancelableFuture<T>` | `future/cancelable_future.k` |
| 2.7 | Tests | `libk/tests/test-future.cpp` |

**Tests Phase 2 :**
- `Promise.trySuccess()` → `Future.get()` retourne la valeur
- `Promise.tryFailure()` → `Future.get()` throw ExecutionException
- `Future.get()` bloquant interruptible
- `Future.cancel()` → `isCancelled()`
- `Future.get(timeout)` → TimeoutException
- Race : complétion avant interruption → succès + flag préservé
- Race : interruption avant complétion → throw ThreadInterruptionException

---

### Phase 3 — Primitives de synchronisation (Layer 2A sync) — ✅ TERMINÉE

**Objectif :** `Mutex`, `ReentrantLock`, `Condition`, `Semaphore`, `CountDownLatch`.

**Livrables :**

| # | Composant | Fichiers |
|---|-----------|---------|
| 3.1 | `k_Mutex` futex-based (C) | `runtime/sync_primitives.h/.c` |
| 3.2 | `k_Condition` (C) | `runtime/sync_primitives.c` |
| 3.3 | `k_Semaphore` (C) | `runtime/sync_primitives.c` |
| 3.4 | `Mutex`, `ReentrantLock`, `Lock` interface | `sync/mutex.k`, `sync/lock.k` |
| 3.5 | `Condition` | `sync/condition.k` |
| 3.6 | `Semaphore` | `sync/semaphore.k` |
| 3.7 | `CountDownLatch`, `CyclicBarrier` | `sync/latch.k` |
| 3.8 | `ReadWriteLock` | `sync/rwlock.k` |
| 3.9 | Tests | `libk/tests/test-sync-mutex.cpp`, `test-sync-condition.cpp`, `test-sync-semaphore.cpp`, `test-sync-latch.cpp` |

**Tests Phase 3 :**
- Mutex contention entre threads
- `lockInterruptibly()` throw si interrompu avant et pendant l'attente
- `tryLock(timeout)` : timeout, succès, interruption
- `Condition.await()` : signal, signalAll, timeout, interruption
- Race signal vs interrupt sur `Condition.await()` : policy documentée
- `Semaphore.acquire()` et `release()` depuis plusieurs threads
- `CountDownLatch.await()` interruptible, countDown depuis N threads
- `CyclicBarrier.await()` → tous les threads franchissent ensemble
- `ReentrantLock` : réentrance, IllegalMonitorStateException sur unlock sans lock

---

### Phase 4 — I/O fichier asynchrone (Layer 1A + Layer 2B fichiers) — ✅ TERMINÉE

**Objectif :** `FileInputStream`, `FileOutputStream`, `FileChannel` via io_uring.

**Livrables :**

| # | Composant | Fichiers |
|---|-----------|---------|
| 4.1 | Backend io_uring complet (read/write/open/close/fsync) | `runtime/linux/uring_ops.c` |
| 4.2 | `AsyncHandle` complet + `HandleRegistry` | `runtime/async_handle.h/.c` |
| 4.3 | `BufferLease`, `BufferPool` | `runtime/buffer_lease.h/.c` |
| 4.4 | `CancellationToken`, `CancellationScope` | `runtime/cancellation.h/.c` |
| 4.5 | Exceptions I/O | `io/io_exceptions.k` |
| 4.6 | `ByteBuffer` | `io/byte_buffer.k` |
| 4.7 | `Channel` interfaces | `io/channel.k` |
| 4.8 | `Path` | `io/file_channel.k` (partie Path) |
| 4.9 | `FileInputStream`, `FileOutputStream` | `io/file_channel.k` |
| 4.10 | `FileChannel` | `io/file_channel.k` |
| 4.11 | `file_io_async.c` helpers FFI | `io/file_io_async.c` |
| 4.12 | Tests | `libk/tests/test-io-file-async.cpp`, `test-io-file-channel.cpp`, `test-io-file-interrupt.cpp` |

**Réalisation effective :**

| # | Composant livré | Fichiers |
|---|-----------------|---------|
| 4.1 | Backend io_uring (open/read/write/fsync/close/truncate) + repli POSIX synchrone | `runtime/async_io.h` / `.c` |
| 4.2 | Registre d'opérations sans ABA, machine à états de handle (OPEN→CLOSING→CLOSED) avec cancel-all + drain | `runtime/async_io.c` |
| 4.3 | Park lot interruptible extrait de `sync_primitives.c` et partagé | `runtime/park_lot.h` / `.c` |
| 4.4 | Annulation sur interruption / deadline / close, avec reap non interruptible | `runtime/async_io.c` |
| 4.5 | Exceptions I/O (200–204) | `io/io_exceptions.k` |
| 4.6 | `ByteBuffer` complet | `io/byte_buffer.k` |
| 4.7 | `Channel`, `ReadableChannel`, `WritableChannel` | `io/channel.k` |
| 4.8 | `Path` + FFI d'inspection | `io/path.k` |
| 4.9 | `AsyncFileInputStream`, `AsyncFileOutputStream` | `io/file_stream.k` |
| 4.10 | `FileChannel` (positionnel + séquentiel, `readFully`/`writeFully`, `size`, `truncate`, `force`) | `io/file_channel.k` |
| 4.11 | Pont FFI `__k_async_*` (encodage de résultat, drapeaux d'ouverture portables, transcodage UTF-32→UTF-8) | `runtime/async_ffi.c` |
| 4.12 | Tests (29 cas) | `libk/tests/test-io-byte-buffer.cpp`, `test-io-path.cpp`, `test-io-file-channel.cpp`, `test-io-file-stream.cpp` |

*Écarts par rapport au plan initial :* `BufferLease`/`BufferPool` et
`CancellationToken`/`CancellationScope` n'ont pas été nécessaires — l'annulation est
portée par la machine à états du handle et par le park lot par opération. Les flux
asynchrones sont nommés `AsyncFile*Stream` afin de coexister avec les
`File*Stream` synchrones déjà présents dans `io/file.k`.

*Documentation :* `doc/spec/stdlib/io-async.md`.

**Tests Phase 4 :**
- `FileInputStream.read()` lit correctement un fichier existant
- `FileInputStream.readFully()` : succès, EOF prématuré → `EndOfStreamException`
- `FileOutputStream.write()` et `writeFully()`
- `FileChannel.read/write` avec offsets explicites
- `FileChannel.force()` (fsync)
- Interruption pendant lecture → `ThreadInterruptionException`
- Interruption pendant `readFully` partielle → `InterruptedIOException` avec `bytesTransferred`
- Close pendant lecture en cours → `ClosedChannelException`
- Timeout de lecture (via opération avec deadline)
- Annulation d'opération déjà complétée → pas d'erreur

---

### Phase 5 — I/O réseau asynchrone (Layer 1A + Layer 2B sockets)

**Objectif :** `Socket`, `ServerSocket`, `SocketChannel`, `NetworkAddress`.

**Livrables :**

| # | Composant | Fichiers |
|---|-----------|---------|
| 5.1 | Backend io_uring sockets (recv/send/accept/connect) | `runtime/linux/uring_ops.c` (extension) |
| 5.2 | `NetworkAddress` + résolution | `io/network_address.k`, `network_ffi.c` |
| 5.3 | `Socket` | `io/socket.k` |
| 5.4 | `ServerSocket` | `io/socket.k` |
| 5.5 | `SocketChannel` | `io/socket.k` |
| 5.6 | Tests | `libk/tests/test-io-socket.cpp`, `test-io-server-socket.cpp`, `test-io-socket-interrupt.cpp` |

**Tests Phase 5 :**
- TCP client/server loopback : connect, send, recv, close
- `ServerSocket.accept()` interruptible
- `Socket.read()` interruptible (lecture bloquante interrompue)
- `Socket.connect(timeout)` → `TimeoutException` si pas de serveur
- Close pendant accept ou recv → `ClosedChannelException`
- Fermeture propre du pair (EOF) → retour 0 ou -1 selon API
- Envoi sur socket fermée → `IOException`
- `DatagramSocket` envoi/réception UDP

---

### Phase 6 — Proactor/Reactor (Layer 3)

**Objectif :** `EventLoop`, `Selector`, `ScheduledTask`.

**Livrables :**

| # | Composant | Fichiers |
|---|-----------|---------|
| 6.1 | `CompletionDispatcher` complet avec callbacks | `runtime/completion.c` (extension) |
| 6.2 | `EventLoop` C runtime | `runtime/event_loop.h/.c` |
| 6.3 | `EventLoop` K wrapper + `ScheduledTask` | `io/event_loop.k` |
| 6.4 | Backend io_uring poll operations | `runtime/linux/uring_ops.c` (extension) |
| 6.5 | `Selector`, `SelectionKey`, `InterestOps` | `io/selector.k`, `selector_ffi.c` |
| 6.6 | `SocketChannel` intégration Selector | extension de `io/socket.k` |
| 6.7 | Tests | `libk/tests/test-io-event-loop.cpp`, `test-io-selector.cpp` |

**Tests Phase 6 :**
- `EventLoop.run()` exécute des tâches soumises
- `EventLoop.scheduleAfter(delay)` déclenche après le délai
- `EventLoop.stop()` termine proprement
- `Selector.select()` détecte readability sur socket
- `Selector.select(timeout)` timeout si rien
- `Selector.wakeup()` ≠ Thread.interrupt()
- `Selector.select()` interruptible (ThreadInterruptionException)
- Multi-channel select sur plusieurs sockets actifs
- Enregistrement/désenregistrement de canal dynamique

---

### Phase 7 — Intégration et robustesse

**Objectif :** Tests de stress, observabilité, documentation.

**Livrables :**

| # | Composant | Fichiers |
|---|-----------|---------|
| 7.1 | `RuntimeMetrics` exposé | `runtime/async_runtime.h` (extension) |
| 7.2 | Tests de stress threading | `libk/tests/test-thread-stress.cpp` |
| 7.3 | Tests de stress I/O | `libk/tests/test-io-stress.cpp` |
| 7.4 | Tests de cancellation storm | `libk/tests/test-cancellation-storm.cpp` |
| 7.5 | Tests shutdown sous charge | `libk/tests/test-runtime-shutdown.cpp` |
| 7.6 | Documentation spec stdlib | `doc/spec/stdlib/threading.md`, `doc/spec/stdlib/io-async.md` |
| 7.7 | Mise à jour `doc/spec/stdlib/io.md` | Extension API |

---

## 7. Stratégie de tests

### 7.1 Organisation des fichiers de test

Tous les tests s'ajoutent à `libk/libk/tests/` et s'enregistrent dans `libk/libk/CMakeLists.txt`.

```
libk/libk/tests/
├── (existants, inchangés)
├── test-thread-basic.cpp          Phase 1
├── test-future.cpp                Phase 2
├── test-sync-mutex.cpp            Phase 3
├── test-sync-condition.cpp        Phase 3
├── test-sync-semaphore.cpp        Phase 3
├── test-sync-latch.cpp            Phase 3
├── test-io-file-async.cpp         Phase 4
├── test-io-file-channel.cpp       Phase 4
├── test-io-file-interrupt.cpp     Phase 4
├── test-io-socket.cpp             Phase 5
├── test-io-server-socket.cpp      Phase 5
├── test-io-socket-interrupt.cpp   Phase 5
├── test-io-event-loop.cpp         Phase 6
├── test-io-selector.cpp           Phase 6
├── test-thread-stress.cpp         Phase 7
├── test-io-stress.cpp             Phase 7
├── test-cancellation-storm.cpp    Phase 7
└── test-runtime-shutdown.cpp      Phase 7
```

### 7.2 Pattern de test standard

Les tests utilisent les mêmes helpers que les tests klang existants, via Catch2 :

```cpp
// test-thread-basic.cpp — exemple de pattern
#include "helpers.hpp"

TEST_CASE("Thread sleep and interrupt", "[thread][sync]") {
    klang_test_context ctx;
    ctx.compile_with_libk(R"(
        module test;

        interruptedSleep() : bool {
            try {
                ::k::Thread::sleep(::k::Duration::ofSeconds(60));
                return false;
            } catch (ex: ::k::ThreadInterruptionException) {
                return true;
            }
        }
    )");

    // Lancer le thread via JIT, l'interrompre depuis C++, vérifier résultat
    auto fn = ctx.get_function<bool()>("::interruptedSleep");
    // ... (wrapper thread + interrupt depuis test)
}
```

Pour les tests qui nécessitent plusieurs threads concurrents, les tests C++ créent directement des `std::thread` et appellent des fonctions JIT compilées ou des fonctions C du runtime.

### 7.3 Matrice de couverture des cas spécifiés (§19 de la spec)

| Catégorie spec | Cas | Phase |
|----------------|-----|-------|
| Interruption | Avant wait | 1, 2, 3 |
| Interruption | Pendant wait | 1, 2, 3 |
| Interruption | Race après complétion | 2, 4, 5 |
| Interruption | Répétée | 1 |
| Interruption | Effacement du flag | 1 |
| Annulation | Lecture active | 4, 5 |
| Annulation | Op déjà complétée | 4, 5 |
| Annulation | Op introuvable | 4 |
| Annulation | Toutes ops d'un handle | 4, 5 |
| I/O partielle | Read partiel + interrupt | 4 |
| I/O partielle | Write partiel + interrupt | 4 |
| I/O partielle | readFully EOF | 4 |
| Races close | Close pendant read | 4, 5 |
| Races close | Close pendant write | 4, 5 |
| Races close | Double close | 4, 5 |
| Synchronisation | Contention mutex | 3 |
| Synchronisation | Lock interruptible | 3 |
| Synchronisation | Race condition signal vs interrupt | 3 |
| Synchronisation | Équité sémaphore | 3 |
| Timeouts | Avant complétion | 2, 4, 5 |
| Timeouts | Après complétion | 2, 4, 5 |
| Timeouts | Race timeout/cancel | 2 |
| Stress | Millions d'opérations | 7 |
| Stress | Cancellation storm | 7 |
| Stress | Shutdown sous charge | 7 |

---

## 8. Contraintes et décisions d'architecture

### 8.1 Décisions prises

| Décision | Justification |
|----------|---------------|
| Layer 1 entièrement en C | FFI K→C existant, contrôle total de l'ABI, pas de C++ exceptions dans le runtime |
| Une ring io_uring par worker thread | Réduction de contention, meilleure localité cache |
| `msg_ring` pour wakeup cross-thread si disponible | Évite un eventfd par thread |
| Fallback timerfd si io_uring < 5.4 | Compatibilité noyau |
| Futures type-erased en C (void*) | K ne supporte pas encore les génériques au niveau ABI runtime |
| Mutex basé sur futex (pas pthread_mutex) | Permet l'interruptibilité fine et l'intégration avec BlockingPoint |
| Windows backend en Phase ultérieure | Hors scope immédiat, mais toute l'architecture est conçue pour l'accueillir |

### 8.2 Contraintes K language actuelles

- **Pas d'exceptions génériques au niveau ABI :** les `Future<T>` et `Promise<T>` sont wrappées en K avec des types concrets (type erasure C). Les types `T` doivent être copiables dans des buffers de taille fixe ou alloués dynamiquement.
- **FFI obligatoire pour l'atomique :** K n'expose pas d'opérations atomiques. Le runtime C gère tous les accès atomiques.
- **Pas de threads virtuels pour l'instant :** l'architecture est conçue pour les accueillir (BlockingPoint, Scheduler) mais la Phase 1 utilise 1 thread K = 1 OS thread.
- **Lambdas/closures :** si K ne supporte pas les lambdas, les `Runnable` passées à `Thread()` sont des classes anonymes ou des classes nommées implémentant `Runnable`. Vérifier le support existant.

### 8.3 Dépendances système requises

| Dépendance | Version minimale | Usage |
|------------|-----------------|-------|
| `liburing` | 2.1 (noyau 5.10+) | Backend io_uring |
| noyau Linux | 5.10 LTS minimum, 5.15+ recommandé | Timeouts liés, multishot accept |
| `pthread` | POSIX | Création de threads OS |
| `liburing` 2.3+ / noyau 5.18+ | optionnel | `msg_ring` (wakeup cross-thread sans eventfd) |

Ajouter dans `CMakeLists.txt` (racine) :
```cmake
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBURING REQUIRED liburing>=2.1)
```

### 8.4 Intégration CMake

La couche 1 (C runtime) sera compilée comme bibliothèque statique interne `libk_runtime` puis linkée dans `libk.so`. Pas de `find_package` dans les `CMakeLists.txt` des sous-projets.

```cmake
# Dans libk/CMakeLists.txt (racine libk, non sous-projet)
# La détection de liburing est dans le CMakeLists.txt racine du workspace
add_library(libk_runtime STATIC
    libk/src/runtime/async_runtime.c
    libk/src/runtime/async_operation.c
    ...
    libk/src/runtime/linux/uring_backend.c
    libk/src/runtime/linux/uring_ops.c
)
target_link_libraries(libk_runtime ${LIBURING_LIBRARIES})
```

### 8.5 Invariants à préserver à chaque phase

1. **Les API K existantes ne changent pas.** Les streams actuels (`InputStream<T>`, `OutputStream<T>`, `FileInputStream` simple) restent fonctionnels. Les nouvelles classes ajoutent des fonctionnalités.
2. **Tout futur bloquant passe par un BlockingPoint.** Aucun appel système bloquant direct dans Layer 2 ou Layer 3.
3. **L'interrupted flag est atomique avec release/acquire.**
4. **Un AsyncOperation consomme exactement un slot OperationRegistry** jusqu'à RELEASED.
5. **k_op_release() est appelé exactement une fois**, après la livraison de la complétion.
6. **Les deux CQE** (annulation + opération originale) **sont toujours consommées.**

---

*Ce document doit être mis à jour à chaque début de phase pour refléter les décisions prises en cours d'implémentation. Il sera supprimé quand toutes les phases seront terminées et les specs `doc/spec/stdlib/` à jour.*
