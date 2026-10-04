#define _GNU_SOURCE
#include "executor_runtime.h"
#include "sync_primitives.h"
#include "park_lot.h"
#include "io_runtime.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>

#define K_EXECUTOR_CAPACITY 1024u

void __k_executor_report_failure(int origin, int code) {
    static const char* const origins[] = {
        "unknown", "task.run", "task.drop", "io.decode", "io.callback",
        "io.callback.drop", "io.promise.publish", "io.promise.drop", "worker.dispatch"
    };
    const char* name = origin > 0 && (unsigned)origin < sizeof(origins) / sizeof(origins[0])
                     ? origins[origin] : origins[0];
    int saved_errno = errno;
    fprintf(stderr, "K runtime: unhandled failure origin=%s code=%d\n", name, code);
    errno = saved_errno;
}

typedef struct KTask {
    void (*run)(void*);
    void (*drop)(void*);
    void* arg;
    int priority;
    int64_t deadline;
    uint64_t sequence;
    struct KTask* next;
} KTask;

typedef struct KEndpoint {
    KExecutorLease lease;
    KMutex* mutex;
    KCondition* changed;
    KTask* head;
    KTask* timers[K_EXECUTOR_CAPACITY];
    unsigned timer_count;
    unsigned count;
    uint64_t sequence;
    bool closed;
    bool running;
    bool stepping;
    uint64_t run_thread;
    pid_t creator_pid;
} KEndpoint;

static void dispose_task(KTask* task, bool run) {
    if (run && task->run) task->run(task->arg);
    if (task->drop) task->drop(task->arg);
    free(task);
}

static void endpoint_retain(KExecutorLease* lease) {
    atomic_fetch_add_explicit(&lease->refs, 1u, memory_order_relaxed);
}

static void endpoint_clear(KEndpoint* ep) {
    k_mutex_lock(ep->mutex);
    KTask* list = ep->head;
    ep->head = NULL;
    for (unsigned i = 0; i < ep->timer_count; ++i) {
        ep->timers[i]->next = list;
        list = ep->timers[i];
    }
    ep->timer_count = 0;
    ep->count = 0;
    k_mutex_unlock(ep->mutex);
    /* Cleanup is arbitrary K code: never run it under the queue lock. */
    while (list) {
        KTask* next = list->next;
        dispose_task(list, false);
        list = next;
    }
}

static void endpoint_release(KExecutorLease* lease) {
    if (((KEndpoint*)lease->ctx)->creator_pid != getpid()) return;
    if (atomic_fetch_sub_explicit(&lease->refs, 1u, memory_order_acq_rel) != 1u) return;
    KEndpoint* ep = lease->ctx;
    endpoint_clear(ep);
    k_condition_destroy(ep->changed);
    k_mutex_destroy(ep->mutex);
    free(ep);
}

static bool timer_less(KTask* a, KTask* b) {
    return a->deadline < b->deadline ||
           (a->deadline == b->deadline && a->sequence < b->sequence);
}

static void timer_push(KEndpoint* ep, KTask* task) {
    unsigned i = ep->timer_count++;
    while (i && timer_less(task, ep->timers[(i - 1) / 2])) {
        ep->timers[i] = ep->timers[(i - 1) / 2];
        i = (i - 1) / 2;
    }
    ep->timers[i] = task;
}

static KTask* timer_pop(KEndpoint* ep) {
    KTask* result = ep->timers[0];
    KTask* last = ep->timers[--ep->timer_count];
    unsigned i = 0;
    while (2 * i + 1 < ep->timer_count) {
        unsigned child = 2 * i + 1;
        if (child + 1 < ep->timer_count &&
            timer_less(ep->timers[child + 1], ep->timers[child])) ++child;
        if (!timer_less(ep->timers[child], last)) break;
        ep->timers[i] = ep->timers[child];
        i = child;
    }
    if (ep->timer_count) ep->timers[i] = last;
    return result;
}

static int enqueue(KEndpoint* ep, void (*run)(void*), void (*drop)(void*),
                   void* arg, int priority, int64_t delay) {
    if (ep && ep->creator_pid != getpid()) {
        if (drop) drop(arg);
        return -ENOTSUP;
    }
    KTask* task = calloc(1, sizeof(*task));
    if (!task || !ep) {
        free(task);
        if (drop) drop(arg);
        return -ENOMEM;
    }
    task->run = run;
    task->drop = drop;
    task->arg = arg;
    task->priority = priority;
    k_mutex_lock(ep->mutex);
    if (ep->closed || ep->count == K_EXECUTOR_CAPACITY) {
        int rc = ep->closed ? -ESHUTDOWN : -EBUSY;
        k_mutex_unlock(ep->mutex);
        dispose_task(task, false);
        return rc;
    }
    task->sequence = ++ep->sequence;
    if (delay > 0) {
        int64_t now = k_monotonic_nanos();
        task->deadline = delay > INT64_MAX - now ? INT64_MAX : now + delay;
        timer_push(ep, task);
    } else {
        KTask** cursor = &ep->head;
        while (*cursor && (*cursor)->priority >= priority) cursor = &(*cursor)->next;
        task->next = *cursor;
        *cursor = task;
    }
    ++ep->count;
    k_condition_signal_all(ep->changed);
    k_mutex_unlock(ep->mutex);
    return 0;
}

static int endpoint_submit(KExecutorLease* lease, void (*run)(void*),
                           void (*drop)(void*), void* arg) {
    return enqueue(lease->ctx, run, drop, arg, 0, 0);
}

static const KExecutorLeaseOps endpoint_ops = {
    endpoint_retain, endpoint_release, endpoint_submit
};

void* __k_native_queue_create(void) {
    KEndpoint* ep = calloc(1, sizeof(*ep));
    if (!ep) return NULL;
    ep->mutex = k_mutex_create(false);
    if (ep->mutex) ep->changed = k_condition_create(ep->mutex);
    if (!ep->changed) {
        k_mutex_destroy(ep->mutex);
        free(ep);
        return NULL;
    }
    ep->lease.ops = &endpoint_ops;
    ep->lease.ctx = ep;
    ep->creator_pid = getpid();
    atomic_init(&ep->lease.refs, 1u);
    return ep;
}

void __k_native_queue_retain(void* queue) {
    if (queue) endpoint_retain(&((KEndpoint*)queue)->lease);
}
void __k_native_queue_release(void* queue) {
    if (queue) endpoint_release(&((KEndpoint*)queue)->lease);
}
void __k_native_queue_clear(void* queue) {
    if (queue && ((KEndpoint*)queue)->creator_pid == getpid()) endpoint_clear(queue);
}
void __k_native_queue_close(void* queue) {
    if (!queue) return;
    KEndpoint* ep = queue;
    if (ep->creator_pid != getpid()) return;
    k_mutex_lock(ep->mutex);
    ep->closed = true;
    k_condition_signal_all(ep->changed);
    k_mutex_unlock(ep->mutex);
}
int __k_native_queue_enqueue(void* queue, KCallable run, KCallable drop,
                             void* arg, int priority) {
    if (run.ctx || drop.ctx) {
        k_callable_drop(drop, arg);
        return -EINVAL;
    }
    return enqueue(queue, (void (*)(void*))run.fn, (void (*)(void*))drop.fn,
                   arg, priority, 0);
}

static int take_and_run(KEndpoint* ep, int64_t timeout, bool serial) {
    if (ep->creator_pid != getpid()) return -ENOTSUP;
    int64_t now = k_monotonic_nanos();
    int64_t deadline = timeout < 0 || timeout > INT64_MAX - now
                     ? INT64_MAX : now + timeout;
    KTask* task = NULL;
    k_mutex_lock(ep->mutex);
    if (serial && (ep->stepping ||
        (ep->running && ep->run_thread != k_sync_current_thread_id()))) {
        k_mutex_unlock(ep->mutex);
        return -EBUSY;
    }
    if (serial) ep->stepping = true;
    for (;;) {
        now = k_monotonic_nanos();
        if (serial && ep->closed) break;
        if (ep->timer_count && ep->timers[0]->deadline <= now) {
            task = timer_pop(ep);
            break;
        }
        if (ep->head) {
            task = ep->head;
            ep->head = task->next;
            break;
        }
        if (ep->closed || now >= deadline) break;
        int64_t wait = deadline == INT64_MAX ? -1 : deadline - now;
        if (ep->timer_count) {
            int64_t until_timer = ep->timers[0]->deadline - now;
            if (wait < 0 || until_timer < wait) wait = until_timer;
        }
        int rc = k_condition_await(ep->changed, wait, false);
        if (rc == K_SYNC_ILLEGAL) break;
    }
    if (task) --ep->count;
    k_mutex_unlock(ep->mutex);
    if (task) dispose_task(task, true);
    if (serial) {
        k_mutex_lock(ep->mutex);
        ep->stepping = false;
        k_condition_signal_all(ep->changed);
        k_mutex_unlock(ep->mutex);
    }
    return task ? 1 : 0;
}

int __k_native_queue_dequeue_and_run(void* queue) {
    return queue ? take_and_run(queue, -1, false) : 0;
}
int __k_native_queue_size(void* queue) {
    if (!queue) return 0;
    KEndpoint* ep = queue;
    if (ep->creator_pid != getpid()) return 0;
    k_mutex_lock(ep->mutex);
    int count = (int)ep->count;
    k_mutex_unlock(ep->mutex);
    return count;
}
int __k_native_queue_is_closed(void* queue) {
    if (!queue) return 1;
    KEndpoint* ep = queue;
    if (ep->creator_pid != getpid()) return 1;
    k_mutex_lock(ep->mutex);
    bool closed = ep->closed;
    k_mutex_unlock(ep->mutex);
    return closed;
}
void* __k_native_queue_get_lease(void* queue) {
    if (!queue) return NULL;
    __k_native_queue_retain(queue);
    return &((KEndpoint*)queue)->lease;
}
void __k_executor_lease_retain(void* lease) { k_executor_lease_retain(lease); }
void __k_executor_lease_release(void* lease) { k_executor_lease_release(lease); }
int __k_executor_lease_submit(void* lease, KCallable run,
                             KCallable drop, void* arg) {
    if (run.ctx || drop.ctx) {
        k_callable_drop(drop, arg);
        return -EINVAL;
    }
    return k_executor_lease_submit(lease, (void (*)(void*))run.fn,
                                   (void (*)(void*))drop.fn, arg);
}

static void direct_release(KExecutorLease* lease) {
    if (atomic_fetch_sub_explicit(&lease->refs, 1u, memory_order_acq_rel) == 1u)
        free(lease);
}
static int direct_submit(KExecutorLease* lease, void (*run)(void*),
                         void (*drop)(void*), void* arg) {
    (void)lease;
    /* I/O dispatch must hop off the reaper before selecting a direct lease. */
    if (k_io_runtime_is_reaper()) {
        if (drop) drop(arg);
        return -EDEADLK;
    }
    if (run) run(arg);
    if (drop) drop(arg);
    return 0;
}
static const KExecutorLeaseOps direct_ops = {
    endpoint_retain, direct_release, direct_submit
};
void* __k_executor_lease_create_direct(void) {
    KExecutorLease* lease = calloc(1, sizeof(*lease));
    if (lease) {
        lease->ops = &direct_ops;
        atomic_init(&lease->refs, 1u);
    }
    return lease;
}

void* __k_event_loop_create(void) { return __k_native_queue_create(); }
void __k_event_loop_release(void* loop) { __k_native_queue_release(loop); }
void* __k_event_loop_lease(void* loop) { return __k_native_queue_get_lease(loop); }
int __k_event_loop_submit(void* loop, KCallable run,
                          KCallable drop, void* arg, int64_t delay) {
    if (run.ctx || drop.ctx) {
        k_callable_drop(drop, arg);
        return -EINVAL;
    }
    return enqueue(loop, (void (*)(void*))run.fn, (void (*)(void*))drop.fn,
                   arg, 0, delay);
}
int __k_event_loop_run_once(void* loop, int64_t timeout) {
    if (!loop) return 0;
    KEndpoint* ep = loop;
    if (ep->creator_pid != getpid()) return -ENOTSUP;
    __k_native_queue_retain(ep);
    k_mutex_lock(ep->mutex);
    bool running = ep->running;
    k_mutex_unlock(ep->mutex);
    int rc = running ? -EBUSY : take_and_run(ep, timeout, true);
    __k_native_queue_release(ep);
    return rc;
}
void __k_event_loop_run(void* loop) {
    if (!loop) return;
    KEndpoint* ep = loop;
    if (ep->creator_pid != getpid()) return;
    __k_native_queue_retain(ep);
    k_mutex_lock(ep->mutex);
    if (ep->running || ep->stepping || ep->closed) {
        k_mutex_unlock(ep->mutex);
        __k_native_queue_release(ep);
        return;
    }
    ep->running = true;
    ep->run_thread = k_sync_current_thread_id();
    k_mutex_unlock(ep->mutex);
    while (take_and_run(ep, -1, true) > 0) {}
    k_mutex_lock(ep->mutex);
    ep->running = false;
    k_condition_signal_all(ep->changed);
    k_mutex_unlock(ep->mutex);
    __k_native_queue_release(ep);
}
void __k_event_loop_stop(void* loop) {
    __k_native_queue_close(loop);
    __k_native_queue_clear(loop);
}
int __k_event_loop_is_running(void* loop) {
    if (!loop) return 0;
    KEndpoint* ep = loop;
    if (ep->creator_pid != getpid()) return 0;
    k_mutex_lock(ep->mutex);
    bool running = ep->running && !ep->closed;
    k_mutex_unlock(ep->mutex);
    return running;
}
int __k_event_loop_is_closed(void* loop) { return __k_native_queue_is_closed(loop); }
