/*
 * Prepared stream socket ABI. Copyright 2026 Emilien Kia.
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef KLANG_RUNTIME_IO_STREAM_FFI_H
#define KLANG_RUNTIME_IO_STREAM_FFI_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int __k_io_op_set_stream_endpoint(void* operation, const uint32_t* endpoint,
                                int port, int unix_socket);
void* __k_io_stream_handle_prepare(int fd);
void* __k_io_stream_handle_create(int fd);
int __k_io_stream_bind(const uint32_t* endpoint, int port, int unix_socket, int backlog);
int __k_io_stream_local_port(void* handle);
int __k_io_stream_configure(int fd);
/* Capacity counts UTF-32 elements including the terminator; returns copied
 * codepoints excluding it, or negative errno without writing. NULL address is empty. */
int __k_io_address_copy_host(const void* address, uint32_t* destination, int capacity);
#ifdef __cplusplus
}
#endif
#endif
