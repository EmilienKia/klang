#ifndef K_RUNTIME_FFI_H
#define K_RUNTIME_FFI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum KFFIType {
    K_FFI_VOID = 0,
    K_FFI_SINT8 = 1,
    K_FFI_UINT8 = 2,
    K_FFI_SINT16 = 3,
    K_FFI_UINT16 = 4,
    K_FFI_SINT32 = 5,
    K_FFI_UINT32 = 6,
    K_FFI_SINT64 = 7,
    K_FFI_UINT64 = 8,
    K_FFI_FLOAT = 9,
    K_FFI_DOUBLE = 10,
    K_FFI_POINTER = 11,
    K_FFI_C_CHAR = 12,
    K_FFI_C_SCHAR = 13,
    K_FFI_C_UCHAR = 14,
    K_FFI_C_SHORT = 15,
    K_FFI_C_USHORT = 16,
    K_FFI_C_INT = 17,
    K_FFI_C_UINT = 18,
    K_FFI_C_LONG = 19,
    K_FFI_C_ULONG = 20,
    K_FFI_C_LLONG = 21,
    K_FFI_C_ULLONG = 22,
    K_FFI_SIZE_T = 23,
    K_FFI_PTRDIFF_T = 24,
    K_FFI_BOOL = 25
} KFFIType;

typedef enum KFFIStatus {
    K_FFI_OK = 0,
    K_FFI_INVALID_TYPE = 1,
    K_FFI_UNSUPPORTED_ABI = 2,
    K_FFI_INVALID_SIGNATURE = 3,
    K_FFI_ARGUMENT_COUNT = 4,
    K_FFI_ARGUMENT_TYPE = 5,
    K_FFI_OUT_OF_RANGE = 6,
    K_FFI_NULL_FUNCTION = 7,
    K_FFI_OUT_OF_MEMORY = 8,
    K_FFI_INVALID_FRAME = 9
} KFFIStatus;

enum { K_FFI_ABI_DEFAULT = 0 };

typedef struct KFFISignature KFFISignature;
typedef struct KFFIFrame KFFIFrame;

/*
 * Fixed signatures, default C ABI, Linux LP64 with signed 8-bit char and IEEE
 * binary32/binary64 floating point only. No aggregates, variadic arguments or
 * callbacks. Type aliases use this C compiler's widths and
 * signedness. BOOL is distinct from UINT8 and requires an unsigned 0 or 1.
 * Unsupported environments fail preparation with UNSUPPORTED_ABI.
 */
int32_t k_ffi_type_canonical(int32_t type);
int32_t k_ffi_type_size(int32_t type);
const char *k_ffi_status_message(int32_t status);

/*
 * Preparation copies contiguous int32_t parameter elements, not a K array
 * header or K struct. The returned handle owns one reference.
 */
KFFISignature *k_ffi_prepare(int32_t return_type, const int32_t *parameter_types,
                            uint32_t count, int32_t abi, int32_t *status);
void k_ffi_signature_release(KFFISignature *signature);

/*
 * Frames retain their immutable signatures. Different frames may use one
 * signature concurrently; a single frame must not be accessed concurrently.
 * Release accepts NULL. Non-NULL handles must be live handles from this API.
 */
KFFIFrame *k_ffi_frame_create(KFFISignature *signature, int32_t *status);
void k_ffi_frame_release(KFFIFrame *frame);
int32_t k_ffi_frame_set_signed(KFFIFrame *frame, uint32_t index,
                               int32_t provided_type, int64_t value);
int32_t k_ffi_frame_set_unsigned(KFFIFrame *frame, uint32_t index,
                                 int32_t provided_type, uint64_t value);
/*
 * FLOAT values are rounded to native float. Finite overflow and rounding a
 * nonzero finite value to zero are rejected; infinities and NaNs are accepted.
 */
int32_t k_ffi_frame_set_float(KFFIFrame *frame, uint32_t index,
                              int32_t provided_type, double value);
int32_t k_ffi_frame_set_pointer(KFFIFrame *frame, uint32_t index,
                                int32_t provided_type, void *value);

/*
 * provided_type must canonically match the prepared parameter type. Setter
 * attempts invalidate previous results; a rejected write to a valid slot leaves
 * that slot uninitialized. Every slot must be set before invocation.
 * Pointer lifetimes and function/signature compatibility belong to the caller.
 * count must equal the prepared arity. No runtime lock is held across ffi_call.
 * capture_errno must be 0 or 1. With capture_errno, errno is cleared immediately
 * before the call and captured immediately afterwards (and left at that value).
 * Otherwise the pre-call errno is restored and the result's errno is zero.
 * A failed invocation invalidates
 * the previous result. A frame can be invoked again with its current arguments.
 */
int32_t k_ffi_frame_invoke(void *function, KFFIFrame *frame, uint32_t count,
                          int32_t capture_errno);
int32_t k_ffi_frame_result_type(const KFFIFrame *frame, int32_t *value);
int32_t k_ffi_frame_result_signed(const KFFIFrame *frame, int64_t *value);
int32_t k_ffi_frame_result_unsigned(const KFFIFrame *frame, uint64_t *value);
int32_t k_ffi_frame_result_float(const KFFIFrame *frame, double *value);
int32_t k_ffi_frame_result_pointer(const KFFIFrame *frame, void **value);
int32_t k_ffi_frame_result_errno(const KFFIFrame *frame, int32_t *value);

/*
 * Private K bridge: only primitive arguments and opaque byte* handles cross
 * the boundary. status pointers are optional; failed getters return zero/NULL.
 * K int -> int32_t, unsigned int -> uint32_t, long -> int64_t,
 * unsigned long -> uint64_t, double -> double, byte* -> void*.
 */
void *__k_ffi_prepare(int32_t return_type, const int32_t *parameter_types,
                      uint32_t count, int32_t abi, int32_t *status);
void __k_ffi_signature_release(void *signature);
void *__k_ffi_frame_create(void *signature, int32_t *status);
void __k_ffi_frame_release(void *frame);
int32_t __k_ffi_set_signed(void *frame, uint32_t index, int32_t type, int64_t value);
int32_t __k_ffi_set_unsigned(void *frame, uint32_t index, int32_t type, uint64_t value);
int32_t __k_ffi_set_float(void *frame, uint32_t index, int32_t type, double value);
int32_t __k_ffi_set_pointer(void *frame, uint32_t index, int32_t type, void *value);
int32_t __k_ffi_invoke(void *function, void *frame, uint32_t count,
                      int32_t capture_errno);
int32_t __k_ffi_result_type(const void *frame, int32_t *status);
int64_t __k_ffi_result_signed(const void *frame, int32_t *status);
uint64_t __k_ffi_result_unsigned(const void *frame, int32_t *status);
double __k_ffi_result_float(const void *frame, int32_t *status);
void *__k_ffi_result_pointer(const void *frame, int32_t *status);
int32_t __k_ffi_result_errno(const void *frame, int32_t *status);
int32_t __k_ffi_type_canonical(int32_t type);
int32_t __k_ffi_type_size(int32_t type);
const char *__k_ffi_status_message(int32_t status);

#ifdef __cplusplus
}
#endif

#endif
