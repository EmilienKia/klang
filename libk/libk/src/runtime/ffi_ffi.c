#include "runtime_ffi.h"

#include <stddef.h>

void *__k_ffi_prepare(int32_t return_type, const int32_t *parameter_types,
                      uint32_t count, int32_t abi, int32_t *status) {
    return k_ffi_prepare(return_type, parameter_types, count, abi, status);
}

void __k_ffi_signature_release(void *signature) {
    k_ffi_signature_release(signature);
}

void *__k_ffi_frame_create(void *signature, int32_t *status) {
    return k_ffi_frame_create(signature, status);
}

void __k_ffi_frame_release(void *frame) {
    k_ffi_frame_release(frame);
}

int32_t __k_ffi_set_signed(void *frame, uint32_t index, int32_t type, int64_t value) {
    return k_ffi_frame_set_signed(frame, index, type, value);
}

int32_t __k_ffi_set_unsigned(void *frame, uint32_t index, int32_t type, uint64_t value) {
    return k_ffi_frame_set_unsigned(frame, index, type, value);
}

int32_t __k_ffi_set_float(void *frame, uint32_t index, int32_t type, double value) {
    return k_ffi_frame_set_float(frame, index, type, value);
}

int32_t __k_ffi_set_pointer(void *frame, uint32_t index, int32_t type, void *value) {
    return k_ffi_frame_set_pointer(frame, index, type, value);
}

int32_t __k_ffi_invoke(void *function, void *frame, uint32_t count,
                      int32_t capture_errno) {
    return k_ffi_frame_invoke(function, frame, count, capture_errno);
}

#define RESULT_BRIDGE(NAME, TYPE, INITIAL) \
    TYPE __k_ffi_result_##NAME(const void *frame, int32_t *status) { \
        TYPE value = INITIAL; \
        int32_t result = k_ffi_frame_result_##NAME(frame, &value); \
        if (status) { \
            *status = result; \
        } \
        return value; \
    }

RESULT_BRIDGE(type, int32_t, 0)
RESULT_BRIDGE(signed, int64_t, 0)
RESULT_BRIDGE(unsigned, uint64_t, 0)
RESULT_BRIDGE(float, double, 0.0)
RESULT_BRIDGE(pointer, void *, NULL)
RESULT_BRIDGE(errno, int32_t, 0)

#undef RESULT_BRIDGE

int32_t __k_ffi_type_canonical(int32_t type) {
    return k_ffi_type_canonical(type);
}

int32_t __k_ffi_type_size(int32_t type) {
    return k_ffi_type_size(type);
}

const char *__k_ffi_status_message(int32_t status) {
    return k_ffi_status_message(status);
}
