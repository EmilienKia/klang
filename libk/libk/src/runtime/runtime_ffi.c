#include "runtime_ffi.h"

#include <errno.h>
#include <ffi.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(sizeof(int32_t) * CHAR_BIT == 32, "The K bridge requires int32_t");
_Static_assert(sizeof(int64_t) * CHAR_BIT == 64, "The K bridge requires int64_t");
_Static_assert(sizeof(ffi_arg) >= sizeof(void *), "libffi return storage must hold pointers");
_Static_assert(sizeof(ffi_arg) == sizeof(ffi_sarg), "libffi argument word sizes must match");
_Static_assert(FFI_DEFAULT_ABI > FFI_FIRST_ABI && FFI_DEFAULT_ABI < FFI_LAST_ABI,
               "libffi must provide a default native ABI");

typedef union KFFIStorage {
    max_align_t alignment;
    ffi_arg word;
    ffi_sarg signed_word;
    int8_t sint8;
    uint8_t uint8;
    int16_t sint16;
    uint16_t uint16;
    int32_t sint32;
    uint32_t uint32;
    int64_t sint64;
    uint64_t uint64;
    char c_char;
    signed char c_schar;
    unsigned char c_uchar;
    short c_short;
    unsigned short c_ushort;
    int c_int;
    unsigned int c_uint;
    long c_long;
    unsigned long c_ulong;
    long long c_llong;
    unsigned long long c_ullong;
    size_t c_size;
    ptrdiff_t c_ptrdiff;
    bool c_bool;
    float floating;
    double double_floating;
    void *pointer;
} KFFIStorage;

struct KFFISignature {
    ffi_cif cif;
    ffi_type **parameters;
    int32_t *types;
    int32_t *canonical;
    int32_t return_type;
    int32_t return_canonical;
    uint32_t count;
    atomic_size_t references;
};

struct KFFIFrame {
    KFFISignature *signature;
    KFFIStorage *arguments;
    void **argv;
    unsigned char *initialized;
    KFFIStorage result;
    int32_t captured_errno;
    bool invoked;
};

static void set_status(int32_t *status, int32_t value) {
    if (status) {
        *status = value;
    }
}

static bool supported_environment(void) {
#if defined(__linux__)
    return CHAR_BIT == 8 && CHAR_MIN < 0 && sizeof(short) == 2 && sizeof(int) == 4
        && sizeof(long) == 8 && sizeof(long long) == 8 && sizeof(void *) == 8
        && sizeof(size_t) == 8 && sizeof(ptrdiff_t) == 8 && sizeof(bool) == 1
        && sizeof(float) == 4 && sizeof(double) == 8 && sizeof(ffi_arg) == 8
        && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128
        && FLT_MIN_EXP == -125 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024
        && DBL_MIN_EXP == -1021;
#else
    return false;
#endif
}

static int32_t integer_type(size_t size, bool is_signed) {
    switch (size * CHAR_BIT) {
        case 8: return is_signed ? K_FFI_SINT8 : K_FFI_UINT8;
        case 16: return is_signed ? K_FFI_SINT16 : K_FFI_UINT16;
        case 32: return is_signed ? K_FFI_SINT32 : K_FFI_UINT32;
        case 64: return is_signed ? K_FFI_SINT64 : K_FFI_UINT64;
        default: return -1;
    }
}

int32_t k_ffi_type_canonical(int32_t type) {
    switch (type) {
        case K_FFI_VOID:
        case K_FFI_SINT8:
        case K_FFI_UINT8:
        case K_FFI_SINT16:
        case K_FFI_UINT16:
        case K_FFI_SINT32:
        case K_FFI_UINT32:
        case K_FFI_SINT64:
        case K_FFI_UINT64:
        case K_FFI_FLOAT:
        case K_FFI_DOUBLE:
        case K_FFI_POINTER:
        case K_FFI_BOOL:
            return type;
        case K_FFI_C_CHAR: return integer_type(sizeof(char), CHAR_MIN < 0);
        case K_FFI_C_SCHAR: return integer_type(sizeof(signed char), true);
        case K_FFI_C_UCHAR: return integer_type(sizeof(unsigned char), false);
        case K_FFI_C_SHORT: return integer_type(sizeof(short), true);
        case K_FFI_C_USHORT: return integer_type(sizeof(unsigned short), false);
        case K_FFI_C_INT: return integer_type(sizeof(int), true);
        case K_FFI_C_UINT: return integer_type(sizeof(unsigned int), false);
        case K_FFI_C_LONG: return integer_type(sizeof(long), true);
        case K_FFI_C_ULONG: return integer_type(sizeof(unsigned long), false);
        case K_FFI_C_LLONG: return integer_type(sizeof(long long), true);
        case K_FFI_C_ULLONG: return integer_type(sizeof(unsigned long long), false);
        case K_FFI_SIZE_T: return integer_type(sizeof(size_t), false);
        case K_FFI_PTRDIFF_T: return integer_type(sizeof(ptrdiff_t), true);
        default: return -1;
    }
}

int32_t k_ffi_type_size(int32_t type) {
    switch (type) {
        case K_FFI_VOID: return 0;
        case K_FFI_SINT8: return sizeof(int8_t);
        case K_FFI_UINT8: return sizeof(uint8_t);
        case K_FFI_SINT16: return sizeof(int16_t);
        case K_FFI_UINT16: return sizeof(uint16_t);
        case K_FFI_SINT32: return sizeof(int32_t);
        case K_FFI_UINT32: return sizeof(uint32_t);
        case K_FFI_SINT64: return sizeof(int64_t);
        case K_FFI_UINT64: return sizeof(uint64_t);
        case K_FFI_FLOAT: return sizeof(float);
        case K_FFI_DOUBLE: return sizeof(double);
        case K_FFI_POINTER: return sizeof(void *);
        case K_FFI_C_CHAR: return sizeof(char);
        case K_FFI_C_SCHAR: return sizeof(signed char);
        case K_FFI_C_UCHAR: return sizeof(unsigned char);
        case K_FFI_C_SHORT: return sizeof(short);
        case K_FFI_C_USHORT: return sizeof(unsigned short);
        case K_FFI_C_INT: return sizeof(int);
        case K_FFI_C_UINT: return sizeof(unsigned int);
        case K_FFI_C_LONG: return sizeof(long);
        case K_FFI_C_ULONG: return sizeof(unsigned long);
        case K_FFI_C_LLONG: return sizeof(long long);
        case K_FFI_C_ULLONG: return sizeof(unsigned long long);
        case K_FFI_SIZE_T: return sizeof(size_t);
        case K_FFI_PTRDIFF_T: return sizeof(ptrdiff_t);
        case K_FFI_BOOL: return sizeof(bool);
        default: return -1;
    }
}

static ffi_type *native_type(int32_t canonical) {
    switch (canonical) {
        case K_FFI_VOID: return &ffi_type_void;
        case K_FFI_SINT8: return &ffi_type_sint8;
        case K_FFI_UINT8:
        case K_FFI_BOOL: return &ffi_type_uint8;
        case K_FFI_SINT16: return &ffi_type_sint16;
        case K_FFI_UINT16: return &ffi_type_uint16;
        case K_FFI_SINT32: return &ffi_type_sint32;
        case K_FFI_UINT32: return &ffi_type_uint32;
        case K_FFI_SINT64: return &ffi_type_sint64;
        case K_FFI_UINT64: return &ffi_type_uint64;
        case K_FFI_FLOAT: return &ffi_type_float;
        case K_FFI_DOUBLE: return &ffi_type_double;
        case K_FFI_POINTER: return &ffi_type_pointer;
        default: return NULL;
    }
}

static bool allocation_fits(uint32_t count, size_t element_size) {
    return (size_t)count <= SIZE_MAX / element_size;
}

KFFISignature *k_ffi_prepare(int32_t return_type, const int32_t *parameter_types,
                            uint32_t count, int32_t abi, int32_t *status) {
    set_status(status, K_FFI_OK);
    if (abi != K_FFI_ABI_DEFAULT || !supported_environment()) {
        set_status(status, K_FFI_UNSUPPORTED_ABI);
        return NULL;
    }
    int32_t canonical = k_ffi_type_canonical(return_type);
    if (canonical < 0) {
        set_status(status, K_FFI_INVALID_TYPE);
        return NULL;
    }
    /*
     * libffi stores the native stack byte count in an unsigned int. Bound the
     * worst-case scalar slot total as well as every allocation multiplication.
     */
    if ((count && !parameter_types) || count > UINT_MAX / sizeof(KFFIStorage)
        || !allocation_fits(count, sizeof(ffi_type *))
        || !allocation_fits(count, sizeof(int32_t))) {
        set_status(status, K_FFI_INVALID_SIGNATURE);
        return NULL;
    }
    KFFISignature *signature = calloc(1, sizeof(*signature));
    if (!signature) {
        set_status(status, K_FFI_OUT_OF_MEMORY);
        return NULL;
    }
    atomic_init(&signature->references, 1);
    signature->return_type = return_type;
    signature->return_canonical = canonical;
    signature->count = count;
    if (count) {
        signature->parameters = malloc((size_t)count * sizeof(*signature->parameters));
        signature->types = malloc((size_t)count * sizeof(*signature->types));
        signature->canonical = malloc((size_t)count * sizeof(*signature->canonical));
        if (!signature->parameters || !signature->types || !signature->canonical) {
            set_status(status, K_FFI_OUT_OF_MEMORY);
            k_ffi_signature_release(signature);
            return NULL;
        }
        for (uint32_t i = 0; i < count; ++i) {
            int32_t parameter = k_ffi_type_canonical(parameter_types[i]);
            if (parameter < 0 || parameter == K_FFI_VOID) {
                set_status(status, parameter < 0 ? K_FFI_INVALID_TYPE : K_FFI_INVALID_SIGNATURE);
                k_ffi_signature_release(signature);
                return NULL;
            }
            signature->types[i] = parameter_types[i];
            signature->canonical[i] = parameter;
            signature->parameters[i] = native_type(parameter);
        }
    }
    ffi_status prepared = ffi_prep_cif(&signature->cif, FFI_DEFAULT_ABI, count,
                                       native_type(canonical), signature->parameters);
    if (prepared != FFI_OK) {
        set_status(status, prepared == FFI_BAD_ABI
                   ? K_FFI_UNSUPPORTED_ABI : K_FFI_INVALID_SIGNATURE);
        k_ffi_signature_release(signature);
        return NULL;
    }
    return signature;
}

void k_ffi_signature_release(KFFISignature *signature) {
    if (signature && atomic_fetch_sub_explicit(&signature->references, 1,
                                               memory_order_acq_rel) == 1) {
        free(signature->parameters);
        free(signature->types);
        free(signature->canonical);
        free(signature);
    }
}

static bool retain_signature(KFFISignature *signature) {
    size_t previous = atomic_load_explicit(&signature->references, memory_order_relaxed);
    do {
        if (previous == SIZE_MAX) {
            return false;
        }
    } while (!atomic_compare_exchange_weak_explicit(&signature->references, &previous,
                                                     previous + 1, memory_order_relaxed,
                                                     memory_order_relaxed));
    return true;
}

KFFIFrame *k_ffi_frame_create(KFFISignature *signature, int32_t *status) {
    set_status(status, K_FFI_OK);
    if (!signature) {
        set_status(status, K_FFI_INVALID_SIGNATURE);
        return NULL;
    }
    uint32_t count = signature->count;
    if (!allocation_fits(count, sizeof(KFFIStorage))
        || !allocation_fits(count, sizeof(void *))) {
        set_status(status, K_FFI_OUT_OF_MEMORY);
        return NULL;
    }
    KFFIFrame *frame = calloc(1, sizeof(*frame));
    if (!frame) {
        set_status(status, K_FFI_OUT_OF_MEMORY);
        return NULL;
    }
    if (!retain_signature(signature)) {
        free(frame);
        set_status(status, K_FFI_OUT_OF_MEMORY);
        return NULL;
    }
    frame->signature = signature;
    if (count) {
        frame->arguments = calloc(count, sizeof(*frame->arguments));
        frame->argv = malloc((size_t)count * sizeof(*frame->argv));
        frame->initialized = calloc(count, sizeof(*frame->initialized));
        if (!frame->arguments || !frame->argv || !frame->initialized) {
            set_status(status, K_FFI_OUT_OF_MEMORY);
            k_ffi_frame_release(frame);
            return NULL;
        }
        for (uint32_t i = 0; i < count; ++i) {
            frame->argv[i] = &frame->arguments[i];
        }
    }
    return frame;
}

void k_ffi_frame_release(KFFIFrame *frame) {
    if (frame) {
        k_ffi_signature_release(frame->signature);
        free(frame->arguments);
        free(frame->argv);
        free(frame->initialized);
        free(frame);
    }
}

static int32_t check_slot(KFFIFrame *frame, uint32_t index, int32_t provided_type) {
    if (!frame) {
        return K_FFI_INVALID_FRAME;
    }
    frame->invoked = false;
    if (index >= frame->signature->count) {
        return K_FFI_ARGUMENT_COUNT;
    }
    frame->initialized[index] = 0;
    int32_t canonical = k_ffi_type_canonical(provided_type);
    if (canonical < 0) {
        return K_FFI_INVALID_TYPE;
    }
    if (canonical != frame->signature->canonical[index]) {
        return K_FFI_ARGUMENT_TYPE;
    }
    return K_FFI_OK;
}

static unsigned integer_bits(int32_t canonical) {
    switch (canonical) {
        case K_FFI_SINT8:
        case K_FFI_UINT8: return 8;
        case K_FFI_SINT16:
        case K_FFI_UINT16: return 16;
        case K_FFI_SINT32:
        case K_FFI_UINT32: return 32;
        case K_FFI_SINT64:
        case K_FFI_UINT64: return 64;
        default: return 0;
    }
}

static bool signed_type(int32_t canonical) {
    return canonical == K_FFI_SINT8 || canonical == K_FFI_SINT16
        || canonical == K_FFI_SINT32 || canonical == K_FFI_SINT64;
}

static bool unsigned_type(int32_t canonical) {
    return canonical == K_FFI_UINT8 || canonical == K_FFI_UINT16
        || canonical == K_FFI_UINT32 || canonical == K_FFI_UINT64
        || canonical == K_FFI_BOOL;
}

int32_t k_ffi_frame_set_signed(KFFIFrame *frame, uint32_t index,
                               int32_t provided_type, int64_t value) {
    int32_t status = check_slot(frame, index, provided_type);
    if (status != K_FFI_OK) {
        return status;
    }
    int32_t canonical = frame->signature->canonical[index];
    if (!signed_type(canonical)) {
        return K_FFI_ARGUMENT_TYPE;
    }
    unsigned bits = integer_bits(canonical);
    if (bits < 64) {
        int64_t limit = INT64_C(1) << (bits - 1);
        if (value < -limit || value >= limit) {
            return K_FFI_OUT_OF_RANGE;
        }
    }
    KFFIStorage *storage = &frame->arguments[index];
#define STORE_SIGNED(TYPE, FIELD, NATIVE) case TYPE: storage->FIELD = (NATIVE)value; break
    switch (frame->signature->types[index]) {
        STORE_SIGNED(K_FFI_SINT8, sint8, int8_t);
        STORE_SIGNED(K_FFI_SINT16, sint16, int16_t);
        STORE_SIGNED(K_FFI_SINT32, sint32, int32_t);
        STORE_SIGNED(K_FFI_SINT64, sint64, int64_t);
        STORE_SIGNED(K_FFI_C_CHAR, c_char, char);
        STORE_SIGNED(K_FFI_C_SCHAR, c_schar, signed char);
        STORE_SIGNED(K_FFI_C_SHORT, c_short, short);
        STORE_SIGNED(K_FFI_C_INT, c_int, int);
        STORE_SIGNED(K_FFI_C_LONG, c_long, long);
        STORE_SIGNED(K_FFI_C_LLONG, c_llong, long long);
        STORE_SIGNED(K_FFI_PTRDIFF_T, c_ptrdiff, ptrdiff_t);
        default: return K_FFI_ARGUMENT_TYPE;
    }
#undef STORE_SIGNED
    frame->initialized[index] = 1;
    return K_FFI_OK;
}

int32_t k_ffi_frame_set_unsigned(KFFIFrame *frame, uint32_t index,
                                 int32_t provided_type, uint64_t value) {
    int32_t status = check_slot(frame, index, provided_type);
    if (status != K_FFI_OK) {
        return status;
    }
    int32_t canonical = frame->signature->canonical[index];
    if (!unsigned_type(canonical)) {
        return K_FFI_ARGUMENT_TYPE;
    }
    unsigned bits = integer_bits(canonical);
    if ((canonical == K_FFI_BOOL && value > 1)
        || (bits && bits < 64 && value >= (UINT64_C(1) << bits))) {
        return K_FFI_OUT_OF_RANGE;
    }
    KFFIStorage *storage = &frame->arguments[index];
#define STORE_UNSIGNED(TYPE, FIELD, NATIVE) case TYPE: storage->FIELD = (NATIVE)value; break
    switch (frame->signature->types[index]) {
        STORE_UNSIGNED(K_FFI_UINT8, uint8, uint8_t);
        STORE_UNSIGNED(K_FFI_UINT16, uint16, uint16_t);
        STORE_UNSIGNED(K_FFI_UINT32, uint32, uint32_t);
        STORE_UNSIGNED(K_FFI_UINT64, uint64, uint64_t);
        STORE_UNSIGNED(K_FFI_C_CHAR, c_char, char);
        STORE_UNSIGNED(K_FFI_C_UCHAR, c_uchar, unsigned char);
        STORE_UNSIGNED(K_FFI_C_USHORT, c_ushort, unsigned short);
        STORE_UNSIGNED(K_FFI_C_UINT, c_uint, unsigned int);
        STORE_UNSIGNED(K_FFI_C_ULONG, c_ulong, unsigned long);
        STORE_UNSIGNED(K_FFI_C_ULLONG, c_ullong, unsigned long long);
        STORE_UNSIGNED(K_FFI_SIZE_T, c_size, size_t);
        STORE_UNSIGNED(K_FFI_BOOL, c_bool, bool);
        default: return K_FFI_ARGUMENT_TYPE;
    }
#undef STORE_UNSIGNED
    frame->initialized[index] = 1;
    return K_FFI_OK;
}

int32_t k_ffi_frame_set_float(KFFIFrame *frame, uint32_t index,
                              int32_t provided_type, double value) {
    int32_t status = check_slot(frame, index, provided_type);
    if (status != K_FFI_OK) {
        return status;
    }
    int32_t canonical = frame->signature->canonical[index];
    if (canonical == K_FFI_FLOAT) {
        if (isfinite(value) && (value > FLT_MAX || value < -FLT_MAX)) {
            return K_FFI_OUT_OF_RANGE;
        }
        float converted = (float)value;
        if (isfinite(value) && value != 0.0 && converted == 0.0f) {
            return K_FFI_OUT_OF_RANGE;
        }
        frame->arguments[index].floating = converted;
    } else if (canonical == K_FFI_DOUBLE) {
        frame->arguments[index].double_floating = value;
    } else {
        return K_FFI_ARGUMENT_TYPE;
    }
    frame->initialized[index] = 1;
    return K_FFI_OK;
}

int32_t k_ffi_frame_set_pointer(KFFIFrame *frame, uint32_t index,
                                int32_t provided_type, void *value) {
    int32_t status = check_slot(frame, index, provided_type);
    if (status != K_FFI_OK) {
        return status;
    }
    if (frame->signature->canonical[index] != K_FFI_POINTER) {
        return K_FFI_ARGUMENT_TYPE;
    }
    frame->arguments[index].pointer = value;
    frame->initialized[index] = 1;
    return K_FFI_OK;
}

int32_t k_ffi_frame_invoke(void *function, KFFIFrame *frame, uint32_t count,
                          int32_t capture_errno) {
    if (!frame) {
        return K_FFI_INVALID_FRAME;
    }
    frame->invoked = false;
    frame->captured_errno = 0;
    if (capture_errno != 0 && capture_errno != 1) {
        return K_FFI_OUT_OF_RANGE;
    }
    if (!function) {
        return K_FFI_NULL_FUNCTION;
    }
    KFFISignature *signature = frame->signature;
    if (count != signature->count) {
        return K_FFI_ARGUMENT_COUNT;
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (!frame->initialized[i]) {
            return K_FFI_ARGUMENT_COUNT;
        }
    }
    memset(&frame->result, 0, sizeof(frame->result));
    int previous_errno = errno;
    if (capture_errno) {
        errno = 0;
    }
    /* Unix dlsym symbols are converted using libffi's function-pointer macro. */
    ffi_call(&signature->cif, FFI_FN(function),
             signature->return_canonical == K_FFI_VOID ? NULL : &frame->result,
             frame->argv);
    int foreign_errno = errno;
    if (capture_errno) {
        frame->captured_errno = foreign_errno;
    } else {
        errno = previous_errno;
    }
    frame->invoked = true;
    return K_FFI_OK;
}

static int32_t check_result(const KFFIFrame *frame, const void *value) {
    return frame && frame->invoked && value ? K_FFI_OK : K_FFI_INVALID_FRAME;
}

int32_t k_ffi_frame_result_type(const KFFIFrame *frame, int32_t *value) {
    int32_t status = check_result(frame, value);
    if (status == K_FFI_OK) {
        *value = frame->signature->return_type;
    }
    return status;
}

int32_t k_ffi_frame_result_signed(const KFFIFrame *frame, int64_t *value) {
    int32_t status = check_result(frame, value);
    if (status != K_FFI_OK) {
        return status;
    }
    int32_t canonical = frame->signature->return_canonical;
    if (!signed_type(canonical)) {
        return K_FFI_ARGUMENT_TYPE;
    }
    /*
     * libffi widens integral returns to ffi_arg, including on big-endian
     * targets. Decode the numeric word, not the first byte of return storage.
     */
    uint64_t bits = (uint64_t)frame->result.word;
    unsigned width = integer_bits(canonical);
    if (width == 64) {
        memcpy(value, &bits, sizeof(*value));
    } else {
        uint64_t mask = (UINT64_C(1) << width) - 1;
        bits &= mask;
        *value = bits & (UINT64_C(1) << (width - 1))
            ? -(int64_t)((~bits & mask) + 1) : (int64_t)bits;
    }
    return K_FFI_OK;
}

int32_t k_ffi_frame_result_unsigned(const KFFIFrame *frame, uint64_t *value) {
    int32_t status = check_result(frame, value);
    if (status != K_FFI_OK) {
        return status;
    }
    int32_t canonical = frame->signature->return_canonical;
    if (!unsigned_type(canonical)) {
        return K_FFI_ARGUMENT_TYPE;
    }
    uint64_t bits = (uint64_t)frame->result.word;
    if (canonical == K_FFI_BOOL) {
        *value = (bits & UINT8_MAX) != 0;
    } else {
        unsigned width = integer_bits(canonical);
        *value = width == 64 ? bits : bits & ((UINT64_C(1) << width) - 1);
    }
    return K_FFI_OK;
}

int32_t k_ffi_frame_result_float(const KFFIFrame *frame, double *value) {
    int32_t status = check_result(frame, value);
    if (status != K_FFI_OK) {
        return status;
    }
    switch (frame->signature->return_canonical) {
        case K_FFI_FLOAT: *value = frame->result.floating; return K_FFI_OK;
        case K_FFI_DOUBLE: *value = frame->result.double_floating; return K_FFI_OK;
        default: return K_FFI_ARGUMENT_TYPE;
    }
}

int32_t k_ffi_frame_result_pointer(const KFFIFrame *frame, void **value) {
    int32_t status = check_result(frame, value);
    if (status != K_FFI_OK) {
        return status;
    }
    if (frame->signature->return_canonical != K_FFI_POINTER) {
        return K_FFI_ARGUMENT_TYPE;
    }
    *value = frame->result.pointer;
    return K_FFI_OK;
}

int32_t k_ffi_frame_result_errno(const KFFIFrame *frame, int32_t *value) {
    int32_t status = check_result(frame, value);
    if (status == K_FFI_OK) {
        *value = frame->captured_errno;
    }
    return status;
}

const char *k_ffi_status_message(int32_t status) {
    switch (status) {
        case K_FFI_OK: return "success";
        case K_FFI_INVALID_TYPE: return "invalid native type";
        case K_FFI_UNSUPPORTED_ABI:
            return "unsupported native ABI (requires default Linux LP64 C ABI with binary32/binary64)";
        case K_FFI_INVALID_SIGNATURE: return "invalid native signature";
        case K_FFI_ARGUMENT_COUNT: return "native argument count mismatch or uninitialized argument";
        case K_FFI_ARGUMENT_TYPE: return "native argument or result type mismatch";
        case K_FFI_OUT_OF_RANGE: return "native argument out of range";
        case K_FFI_NULL_FUNCTION: return "null native function";
        case K_FFI_OUT_OF_MEMORY: return "native call allocation failed";
        case K_FFI_INVALID_FRAME: return "invalid native call frame or unavailable result";
        default: return "unknown native call status";
    }
}
