#ifndef KLANG_EXECUTOR_RUNTIME_H
#define KLANG_EXECUTOR_RUNTIME_H

#include "io_operation.h"
#include <stdint.h>

/* Submission consumes the explicit carrier on both acceptance and rejection.
 * An accepted task always receives one drop, after run or without run.
 * Neither run nor drop may unwind through the C endpoint. */
/* Non-throwing stderr diagnostic. Numeric origins mirror executor.k. */
void __k_executor_report_failure(int origin, int code);
void* __k_native_queue_create(void);
void __k_native_queue_retain(void* queue);
void __k_native_queue_release(void* queue);
int __k_native_queue_enqueue(void* queue, KCallable run,
                            KCallable drop, void* arg, int priority);
int __k_native_queue_dequeue_and_run(void* queue);
void __k_native_queue_clear(void* queue);
void __k_native_queue_close(void* queue);
int __k_native_queue_size(void* queue);
int __k_native_queue_is_closed(void* queue);
void* __k_native_queue_get_lease(void* queue);
void* __k_executor_lease_create_direct(void);
int __k_executor_lease_submit(void* lease, KCallable run,
                             KCallable drop, void* arg);
void __k_executor_lease_retain(void* lease);
void __k_executor_lease_release(void* lease);

void* __k_event_loop_create(void);
void __k_event_loop_release(void* loop);
void* __k_event_loop_lease(void* loop);
int __k_event_loop_submit(void* loop, KCallable run,
                          KCallable drop, void* arg, int64_t delay);
int __k_event_loop_run_once(void* loop, int64_t timeout);
void __k_event_loop_run(void* loop);
void __k_event_loop_stop(void* loop);
int __k_event_loop_is_running(void* loop);
int __k_event_loop_is_closed(void* loop);

#endif
