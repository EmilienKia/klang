/*
 * Prepared file request ABI. Copyright 2026 Emilien Kia.
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef KLANG_RUNTIME_IO_FILE_FFI_H
#define KLANG_RUNTIME_IO_FILE_FFI_H

#include "callable_abi.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void* __k_io_prepare_request(int opcode, void* handle, void* buffer, int length,
    long long offset, int flags, void* context, KCallable invoke,
    KCallable drop, void* lease);
int __k_io_submit_prepared(void* operation, long long timeout);
int __k_io_op_set_path(void* operation, const uint32_t* path);
int __k_io_op_set_request_error(void* operation, int error);
int __k_io_op_set_file_options(void* operation, int options);
int __k_io_op_set_implicit_position(void* operation);
int __k_io_op_set_fully(void* operation);
void* __k_io_file_handle_prepare(int fd);
void __k_io_file_handle_claim(void* handle, void* result);
long long __k_io_handle_position(const void* handle);
int __k_io_handle_set_position(void* handle, long long position);
long long __k_io_statx_size(long long result);

#ifdef __cplusplus
}
#endif
#endif
