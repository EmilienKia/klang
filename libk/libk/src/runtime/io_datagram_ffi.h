/*
 * Prepared datagram ABI. Copyright 2026 Emilien Kia.
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef KLANG_RUNTIME_IO_DATAGRAM_FFI_H
#define KLANG_RUNTIME_IO_DATAGRAM_FFI_H
#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/uio.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct GNativeAddress GNativeAddress;
typedef struct KDatagramMessage {
    struct msghdr message;
    struct iovec vector;
    struct sockaddr_storage source_storage;
    GNativeAddress* source;
    int64_t datagram_length;
    uint32_t copied;
    bool truncated;
} KDatagramMessage;
void* __k_io_address_create(const uint32_t* endpoint, int port, int unix_socket);
void __k_io_address_retain(void* address);
void __k_io_address_release(void* address);
const uint32_t* __k_io_address_host(const void* address);
int __k_io_address_host_length(const void* address);
int __k_io_address_port(const void* address);
int __k_io_address_family(const void* address);
int __k_io_address_resolved(const void* address);
int __k_io_setup_errno(void);
void* __k_io_datagram_bind(const uint32_t* endpoint, int port, int unix_socket);
void* __k_io_datagram_handle_create(int fd);
void* __k_io_datagram_bound_address(void* handle);
int __k_io_op_set_datagram_endpoint(void* operation, const void* address);
int __k_io_op_set_datagram_message(void* operation);
int __k_io_datagram_finish(void* operation, int result);
void __k_io_datagram_message_release(void* message);
void* __k_io_datagram_source(int64_t value);
int __k_io_datagram_copied(int64_t value);
int __k_io_datagram_truncated(int64_t value);
int64_t __k_io_datagram_length(int64_t value);
#ifdef __cplusplus
}
#endif
#endif
