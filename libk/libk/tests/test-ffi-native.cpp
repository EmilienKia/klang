#include <catch2/catch_all.hpp>
#include "runtime_ffi.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <limits>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

extern "C" {
#define IDENTITY(NAME, TYPE) TYPE ffi_fixture_##NAME(TYPE value) { return value; }
IDENTITY(sint8, int8_t)
IDENTITY(uint8, uint8_t)
IDENTITY(sint16, int16_t)
IDENTITY(uint16, uint16_t)
IDENTITY(sint32, int32_t)
IDENTITY(uint32, uint32_t)
IDENTITY(sint64, int64_t)
IDENTITY(uint64, uint64_t)
IDENTITY(c_char, char)
IDENTITY(c_schar, signed char)
IDENTITY(c_uchar, unsigned char)
IDENTITY(c_short, short)
IDENTITY(c_ushort, unsigned short)
IDENTITY(c_int, int)
IDENTITY(c_uint, unsigned int)
IDENTITY(c_long, long)
IDENTITY(c_ulong, unsigned long)
IDENTITY(c_llong, long long)
IDENTITY(c_ullong, unsigned long long)
IDENTITY(c_size, size_t)
IDENTITY(c_ptrdiff, ptrdiff_t)
IDENTITY(c_bool, bool)
IDENTITY(floating, float)
IDENTITY(double_floating, double)
IDENTITY(pointer, void *)
#undef IDENTITY

int64_t ffi_fixture_zero() {
    return INT64_MIN;
}

void ffi_fixture_write(int32_t *output, uint8_t value) {
    *output = value;
}

int64_t ffi_fixture_errno(int64_t value, int32_t error) {
    errno = error;
    return value;
}

int32_t ffi_fixture_observe_errno() {
    int value = errno;
    errno = ERANGE;
    return value;
}

double ffi_fixture_mixed(int64_t a1, double b1, int64_t a2, double b2,
                         int64_t a3, double b3, int64_t a4, double b4,
                         int64_t a5, double b5, int64_t a6, double b6,
                         int64_t a7, double b7, int64_t a8, double b8,
                         int64_t a9, double b9, int64_t a10, double b10,
                         uint64_t *output, float final_value) {
    const int64_t integers[] = {a1, a2, a3, a4, a5, a6, a7, a8, a9, a10};
    const double floating[] = {b1, b2, b3, b4, b5, b6, b7, b8, b9, b10};
    double result = final_value;
    *output = 0;
    for (unsigned i = 0; i < 10; ++i) {
        *output += static_cast<uint64_t>(integers[i]) * (i + 1);
        result += integers[i] * (i + 1) + floating[i] * (i + 11);
    }
    return result;
}
}

namespace {
using Signature = std::unique_ptr<KFFISignature, decltype(&k_ffi_signature_release)>;
using Frame = std::unique_ptr<KFFIFrame, decltype(&k_ffi_frame_release)>;

Signature prepare(int32_t result, const std::vector<int32_t>& parameters) {
    int32_t status = -1;
    Signature signature(k_ffi_prepare(result, parameters.data(),
                                       static_cast<uint32_t>(parameters.size()),
                                       K_FFI_ABI_DEFAULT, &status),
                         k_ffi_signature_release);
    REQUIRE(status == K_FFI_OK);
    REQUIRE(signature);
    return signature;
}

Frame create_frame(KFFISignature *signature) {
    int32_t status = -1;
    Frame frame(k_ffi_frame_create(signature, &status), k_ffi_frame_release);
    REQUIRE(status == K_FFI_OK);
    REQUIRE(frame);
    return frame;
}

template<class Function>
void *function_address(Function function) {
    return reinterpret_cast<void *>(function);
}

template<class T>
void check_integer(int32_t type, T (*function)(T)) {
    CAPTURE(type);
    auto signature = prepare(type, {type});
    auto frame = create_frame(signature.get());
    REQUIRE(k_ffi_type_size(type) == sizeof(T));
    if constexpr (std::is_signed_v<T>) {
        const std::array<T, 4> values = {
            std::numeric_limits<T>::min(), T(-1), T(0), std::numeric_limits<T>::max()
        };
        for (T value : values) {
            CAPTURE(static_cast<int64_t>(value));
            REQUIRE(k_ffi_frame_set_signed(frame.get(), 0, type, value) == K_FFI_OK);
            REQUIRE(k_ffi_frame_invoke(function_address(function), frame.get(), 1, false) == K_FFI_OK);
            int64_t result = 0;
            REQUIRE(k_ffi_frame_result_signed(frame.get(), &result) == K_FFI_OK);
            REQUIRE(result == static_cast<int64_t>(value));
            int32_t return_type = -1;
            REQUIRE(k_ffi_frame_result_type(frame.get(), &return_type) == K_FFI_OK);
            REQUIRE(return_type == type);
        }
        if constexpr (sizeof(T) < sizeof(int64_t)) {
            REQUIRE(k_ffi_frame_set_signed(frame.get(), 0, type,
                static_cast<int64_t>(std::numeric_limits<T>::max()) + 1) == K_FFI_OUT_OF_RANGE);
            REQUIRE(k_ffi_frame_set_signed(frame.get(), 0, type,
                static_cast<int64_t>(std::numeric_limits<T>::min()) - 1) == K_FFI_OUT_OF_RANGE);
        }
        REQUIRE(k_ffi_frame_set_unsigned(frame.get(), 0, type, 0) == K_FFI_ARGUMENT_TYPE);
    } else {
        const std::array<T, 3> values = {T(0), T(1), std::numeric_limits<T>::max()};
        for (T value : values) {
            CAPTURE(static_cast<uint64_t>(value));
            REQUIRE(k_ffi_frame_set_unsigned(frame.get(), 0, type, value) == K_FFI_OK);
            REQUIRE(k_ffi_frame_invoke(function_address(function), frame.get(), 1, false) == K_FFI_OK);
            uint64_t result = 0;
            REQUIRE(k_ffi_frame_result_unsigned(frame.get(), &result) == K_FFI_OK);
            REQUIRE(result == static_cast<uint64_t>(value));
        }
        if constexpr (sizeof(T) < sizeof(uint64_t)) {
            REQUIRE(k_ffi_frame_set_unsigned(frame.get(), 0, type,
                static_cast<uint64_t>(std::numeric_limits<T>::max()) + 1) == K_FFI_OUT_OF_RANGE);
        }
        REQUIRE(k_ffi_frame_set_signed(frame.get(), 0, type, 0) == K_FFI_ARGUMENT_TYPE);
    }
}

std::atomic<unsigned> forbidden_calls{0};
extern "C" int8_t ffi_fixture_forbidden(int8_t value) {
    forbidden_calls.fetch_add(1, std::memory_order_relaxed);
    return value;
}
}

TEST_CASE("Native FFI: exact integer widths and narrow return normalization",
          "[libk][ffi][native]") {
    check_integer(K_FFI_SINT8, ffi_fixture_sint8);
    check_integer(K_FFI_UINT8, ffi_fixture_uint8);
    check_integer(K_FFI_SINT16, ffi_fixture_sint16);
    check_integer(K_FFI_UINT16, ffi_fixture_uint16);
    check_integer(K_FFI_SINT32, ffi_fixture_sint32);
    check_integer(K_FFI_UINT32, ffi_fixture_uint32);
    check_integer(K_FFI_SINT64, ffi_fixture_sint64);
    check_integer(K_FFI_UINT64, ffi_fixture_uint64);
}

TEST_CASE("Native FFI: C aliases follow native compiler widths and signedness",
          "[libk][ffi][native]") {
    check_integer(K_FFI_C_CHAR, ffi_fixture_c_char);
    check_integer(K_FFI_C_SCHAR, ffi_fixture_c_schar);
    check_integer(K_FFI_C_UCHAR, ffi_fixture_c_uchar);
    check_integer(K_FFI_C_SHORT, ffi_fixture_c_short);
    check_integer(K_FFI_C_USHORT, ffi_fixture_c_ushort);
    check_integer(K_FFI_C_INT, ffi_fixture_c_int);
    check_integer(K_FFI_C_UINT, ffi_fixture_c_uint);
    check_integer(K_FFI_C_LONG, ffi_fixture_c_long);
    check_integer(K_FFI_C_ULONG, ffi_fixture_c_ulong);
    check_integer(K_FFI_C_LLONG, ffi_fixture_c_llong);
    check_integer(K_FFI_C_ULLONG, ffi_fixture_c_ullong);
    check_integer(K_FFI_SIZE_T, ffi_fixture_c_size);
    check_integer(K_FFI_PTRDIFF_T, ffi_fixture_c_ptrdiff);
    REQUIRE(k_ffi_type_canonical(K_FFI_C_CHAR)
            == (std::is_signed_v<char> ? K_FFI_SINT8 : K_FFI_UINT8));
    REQUIRE(k_ffi_type_canonical(K_FFI_C_SHORT) == K_FFI_SINT16);
    REQUIRE(k_ffi_type_canonical(K_FFI_C_INT) == K_FFI_SINT32);
    REQUIRE(k_ffi_type_canonical(K_FFI_C_LONG) == K_FFI_SINT64);
    REQUIRE(k_ffi_type_canonical(K_FFI_SIZE_T) == K_FFI_UINT64);
    REQUIRE(k_ffi_type_canonical(K_FFI_PTRDIFF_T) == K_FFI_SINT64);
}

TEST_CASE("Native FFI: bool uses the C bool ABI and remains distinct from bytes",
          "[libk][ffi][native]") {
    auto signature = prepare(K_FFI_BOOL, {K_FFI_BOOL});
    auto frame = create_frame(signature.get());
    REQUIRE(k_ffi_type_size(K_FFI_BOOL) == sizeof(bool));
    REQUIRE(k_ffi_type_canonical(K_FFI_BOOL) == K_FFI_BOOL);
    for (uint64_t value : {UINT64_C(0), UINT64_C(1)}) {
        REQUIRE(k_ffi_frame_set_unsigned(frame.get(), 0, K_FFI_BOOL, value) == K_FFI_OK);
        REQUIRE(k_ffi_frame_invoke(function_address(ffi_fixture_c_bool), frame.get(), 1, false) == K_FFI_OK);
        uint64_t result = 42;
        REQUIRE(k_ffi_frame_result_unsigned(frame.get(), &result) == K_FFI_OK);
        REQUIRE(result == value);
        int64_t invalid = 0;
        REQUIRE(k_ffi_frame_result_signed(frame.get(), &invalid) == K_FFI_ARGUMENT_TYPE);
    }
    REQUIRE(k_ffi_frame_set_unsigned(frame.get(), 0, K_FFI_BOOL, 2) == K_FFI_OUT_OF_RANGE);
    REQUIRE(k_ffi_frame_set_unsigned(frame.get(), 0, K_FFI_UINT8, 1) == K_FFI_ARGUMENT_TYPE);
    REQUIRE(k_ffi_frame_set_signed(frame.get(), 0, K_FFI_BOOL, 1) == K_FFI_ARGUMENT_TYPE);
}

TEST_CASE("Native FFI: floating point ranges, special values and return storage",
          "[libk][ffi][native]") {
    auto float_signature = prepare(K_FFI_FLOAT, {K_FFI_FLOAT});
    auto float_frame = create_frame(float_signature.get());
    const std::array<float, 7> floats = {
        0.0f, -0.0f, 1.25f, -2.5f, std::numeric_limits<float>::max(),
        std::numeric_limits<float>::lowest(), std::numeric_limits<float>::denorm_min()
    };
    for (float value : floats) {
        REQUIRE(k_ffi_frame_set_float(float_frame.get(), 0, K_FFI_FLOAT, value) == K_FFI_OK);
        REQUIRE(k_ffi_frame_invoke(function_address(ffi_fixture_floating),
                                  float_frame.get(), 1, false) == K_FFI_OK);
        double result = 0;
        REQUIRE(k_ffi_frame_result_float(float_frame.get(), &result) == K_FFI_OK);
        REQUIRE(result == static_cast<double>(value));
        REQUIRE(std::signbit(result) == std::signbit(value));
    }
    REQUIRE(k_ffi_frame_set_float(float_frame.get(), 0, K_FFI_FLOAT,
                                  std::numeric_limits<double>::max()) == K_FFI_OUT_OF_RANGE);
    REQUIRE(k_ffi_frame_set_float(float_frame.get(), 0, K_FFI_FLOAT,
                                  -std::numeric_limits<double>::max()) == K_FFI_OUT_OF_RANGE);
    REQUIRE(k_ffi_frame_set_float(float_frame.get(), 0, K_FFI_FLOAT,
                                  std::numeric_limits<double>::denorm_min()) == K_FFI_OUT_OF_RANGE);
    REQUIRE(k_ffi_frame_set_float(float_frame.get(), 0, K_FFI_DOUBLE, 0) == K_FFI_ARGUMENT_TYPE);
    for (double value : {std::numeric_limits<double>::infinity(),
                         -std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::quiet_NaN()}) {
        REQUIRE(k_ffi_frame_set_float(float_frame.get(), 0, K_FFI_FLOAT, value) == K_FFI_OK);
        REQUIRE(k_ffi_frame_invoke(function_address(ffi_fixture_floating),
                                  float_frame.get(), 1, false) == K_FFI_OK);
        double result = 0;
        REQUIRE(k_ffi_frame_result_float(float_frame.get(), &result) == K_FFI_OK);
        if (std::isnan(value)) {
            REQUIRE(std::isnan(result));
        } else {
            REQUIRE(result == value);
        }
    }
    auto double_signature = prepare(K_FFI_DOUBLE, {K_FFI_DOUBLE});
    auto double_frame = create_frame(double_signature.get());
    for (double value : {0.0, -0.0, 1.125, -8.5, std::numeric_limits<double>::max(),
                         std::numeric_limits<double>::lowest(),
                         std::numeric_limits<double>::denorm_min(),
                         std::numeric_limits<double>::infinity()}) {
        REQUIRE(k_ffi_frame_set_float(double_frame.get(), 0, K_FFI_DOUBLE, value) == K_FFI_OK);
        REQUIRE(k_ffi_frame_invoke(function_address(ffi_fixture_double_floating),
                                  double_frame.get(), 1, false) == K_FFI_OK);
        double result = 0;
        REQUIRE(k_ffi_frame_result_float(double_frame.get(), &result) == K_FFI_OK);
        REQUIRE(result == value);
        REQUIRE(std::signbit(result) == std::signbit(value));
    }
}

TEST_CASE("Native FFI: zero parameters, void returns, null and writable pointers",
          "[libk][ffi][native]") {
    auto zero_signature = prepare(K_FFI_SINT64, {});
    auto zero_frame = create_frame(zero_signature.get());
    REQUIRE(k_ffi_frame_invoke(function_address(ffi_fixture_zero),
                              zero_frame.get(), 0, false) == K_FFI_OK);
    int64_t zero_result = 0;
    REQUIRE(k_ffi_frame_result_signed(zero_frame.get(), &zero_result) == K_FFI_OK);
    REQUIRE(zero_result == INT64_MIN);

    auto void_signature = prepare(K_FFI_VOID, {K_FFI_POINTER, K_FFI_UINT8});
    auto void_frame = create_frame(void_signature.get());
    int32_t output = -1;
    REQUIRE(k_ffi_frame_set_pointer(void_frame.get(), 0, K_FFI_POINTER, &output) == K_FFI_OK);
    REQUIRE(k_ffi_frame_set_unsigned(void_frame.get(), 1, K_FFI_UINT8, 255) == K_FFI_OK);
    REQUIRE(k_ffi_frame_invoke(function_address(ffi_fixture_write),
                              void_frame.get(), 2, false) == K_FFI_OK);
    REQUIRE(output == 255);
    int32_t result_type = -1;
    REQUIRE(k_ffi_frame_result_type(void_frame.get(), &result_type) == K_FFI_OK);
    REQUIRE(result_type == K_FFI_VOID);
    REQUIRE(k_ffi_frame_result_signed(void_frame.get(), &zero_result) == K_FFI_ARGUMENT_TYPE);

    auto pointer_signature = prepare(K_FFI_POINTER, {K_FFI_POINTER});
    auto pointer_frame = create_frame(pointer_signature.get());
    for (void *value : std::array<void *, 2>{nullptr, &output}) {
        REQUIRE(k_ffi_frame_set_pointer(pointer_frame.get(), 0, K_FFI_POINTER, value) == K_FFI_OK);
        REQUIRE(k_ffi_frame_invoke(function_address(ffi_fixture_pointer),
                                  pointer_frame.get(), 1, false) == K_FFI_OK);
        void *result = reinterpret_cast<void *>(1);
        REQUIRE(k_ffi_frame_result_pointer(pointer_frame.get(), &result) == K_FFI_OK);
        REQUIRE(result == value);
    }
}

TEST_CASE("Native FFI: mixed register classes and argument spills preserve order",
          "[libk][ffi][native]") {
    std::vector<int32_t> parameters;
    for (unsigned i = 0; i < 10; ++i) {
        parameters.push_back(K_FFI_SINT64);
        parameters.push_back(K_FFI_DOUBLE);
    }
    parameters.push_back(K_FFI_POINTER);
    parameters.push_back(K_FFI_FLOAT);
    auto signature = prepare(K_FFI_DOUBLE, parameters);
    auto frame = create_frame(signature.get());
    double expected = 1.5;
    uint64_t expected_integer = 0;
    for (uint32_t i = 0; i < 10; ++i) {
        int64_t integer = i + 3;
        double floating = i + 0.25;
        REQUIRE(k_ffi_frame_set_signed(frame.get(), i * 2, K_FFI_SINT64, integer) == K_FFI_OK);
        REQUIRE(k_ffi_frame_set_float(frame.get(), i * 2 + 1, K_FFI_DOUBLE, floating) == K_FFI_OK);
        expected_integer += static_cast<uint64_t>(integer) * (i + 1);
        expected += integer * (i + 1) + floating * (i + 11);
    }
    uint64_t output = 0;
    REQUIRE(k_ffi_frame_set_pointer(frame.get(), 20, K_FFI_POINTER, &output) == K_FFI_OK);
    REQUIRE(k_ffi_frame_set_float(frame.get(), 21, K_FFI_FLOAT, 1.5) == K_FFI_OK);
    REQUIRE(k_ffi_frame_invoke(function_address(ffi_fixture_mixed),
                              frame.get(), 22, false) == K_FFI_OK);
    double result = 0;
    REQUIRE(k_ffi_frame_result_float(frame.get(), &result) == K_FFI_OK);
    REQUIRE(result == expected);
    REQUIRE(output == expected_integer);
}

TEST_CASE("Native FFI: copied signatures, canonical matching and frame retention",
          "[libk][ffi][native]") {
    std::vector<int32_t> parameters{K_FFI_C_INT};
    auto signature = prepare(K_FFI_C_INT, parameters);
    parameters[0] = K_FFI_DOUBLE;
    parameters.clear();
    parameters.shrink_to_fit();
    auto first = create_frame(signature.get());
    auto second = create_frame(signature.get());
    signature.reset();
    REQUIRE(k_ffi_frame_set_signed(first.get(), 0, K_FFI_SINT32, -42) == K_FFI_OK);
    REQUIRE(k_ffi_frame_set_signed(second.get(), 0, K_FFI_C_INT, 77) == K_FFI_OK);
    REQUIRE(k_ffi_frame_set_signed(first.get(), 0, K_FFI_SINT64, -42) == K_FFI_ARGUMENT_TYPE);
    REQUIRE(k_ffi_frame_set_signed(first.get(), 0, K_FFI_SINT32, -42) == K_FFI_OK);
    REQUIRE(k_ffi_frame_invoke(function_address(ffi_fixture_c_int), first.get(), 1, false) == K_FFI_OK);
    first.reset();
    REQUIRE(k_ffi_frame_invoke(function_address(ffi_fixture_c_int), second.get(), 1, false) == K_FFI_OK);
    int64_t value = 0;
    REQUIRE(k_ffi_frame_result_signed(second.get(), &value) == K_FFI_OK);
    REQUIRE(value == 77);
    REQUIRE(k_ffi_frame_set_signed(second.get(), 0, K_FFI_SINT32, -9) == K_FFI_OK);
    REQUIRE(k_ffi_frame_result_signed(second.get(), &value) == K_FFI_INVALID_FRAME);
    REQUIRE(k_ffi_frame_invoke(function_address(ffi_fixture_c_int), second.get(), 1, false) == K_FFI_OK);
    REQUIRE(k_ffi_frame_result_signed(second.get(), &value) == K_FFI_OK);
    REQUIRE(value == -9);
}

TEST_CASE("Native FFI: invalid signatures and arguments never execute a target",
          "[libk][ffi][native]") {
    int32_t status = -1;
    REQUIRE(k_ffi_prepare(-1, nullptr, 0, K_FFI_ABI_DEFAULT, &status) == nullptr);
    REQUIRE(status == K_FFI_INVALID_TYPE);
    REQUIRE(k_ffi_prepare(K_FFI_SINT8, nullptr, 0, 100, &status) == nullptr);
    REQUIRE(status == K_FFI_UNSUPPORTED_ABI);
    REQUIRE(k_ffi_prepare(K_FFI_SINT8, nullptr, 1, K_FFI_ABI_DEFAULT, &status) == nullptr);
    REQUIRE(status == K_FFI_INVALID_SIGNATURE);
    int32_t invalid_type = 26;
    REQUIRE(k_ffi_prepare(K_FFI_SINT8, &invalid_type, 1, K_FFI_ABI_DEFAULT, &status) == nullptr);
    REQUIRE(status == K_FFI_INVALID_TYPE);
    int32_t void_type = K_FFI_VOID;
    REQUIRE(k_ffi_prepare(K_FFI_VOID, &void_type, 1, K_FFI_ABI_DEFAULT, &status) == nullptr);
    REQUIRE(status == K_FFI_INVALID_SIGNATURE);
    int32_t valid_type = K_FFI_SINT8;
    REQUIRE(k_ffi_prepare(K_FFI_SINT8, &valid_type, UINT32_MAX,
                          K_FFI_ABI_DEFAULT, &status) == nullptr);
    REQUIRE(status == K_FFI_INVALID_SIGNATURE);
    REQUIRE(k_ffi_frame_create(nullptr, &status) == nullptr);
    REQUIRE(status == K_FFI_INVALID_SIGNATURE);
    REQUIRE(k_ffi_type_canonical(-1) == -1);
    REQUIRE(k_ffi_type_size(26) == -1);
    REQUIRE(k_ffi_type_size(K_FFI_VOID) == 0);
    k_ffi_signature_release(nullptr);
    k_ffi_frame_release(nullptr);

    forbidden_calls.store(0, std::memory_order_relaxed);
    auto signature = prepare(K_FFI_SINT8, {K_FFI_SINT8});
    auto frame = create_frame(signature.get());
    void *function = function_address(ffi_fixture_forbidden);
    REQUIRE(k_ffi_frame_set_signed(nullptr, 0, K_FFI_SINT8, 1) == K_FFI_INVALID_FRAME);
    REQUIRE(k_ffi_frame_set_unsigned(nullptr, 0, K_FFI_UINT8, 1) == K_FFI_INVALID_FRAME);
    REQUIRE(k_ffi_frame_set_float(nullptr, 0, K_FFI_FLOAT, 1) == K_FFI_INVALID_FRAME);
    REQUIRE(k_ffi_frame_set_pointer(nullptr, 0, K_FFI_POINTER, nullptr) == K_FFI_INVALID_FRAME);
    REQUIRE(k_ffi_frame_invoke(function, nullptr, 1, false) == K_FFI_INVALID_FRAME);
    REQUIRE(k_ffi_frame_set_signed(frame.get(), 1, K_FFI_SINT8, 1) == K_FFI_ARGUMENT_COUNT);
    REQUIRE(k_ffi_frame_set_signed(frame.get(), 0, -1, 1) == K_FFI_INVALID_TYPE);
    REQUIRE(k_ffi_frame_set_signed(frame.get(), 0, K_FFI_UINT8, 1) == K_FFI_ARGUMENT_TYPE);
    REQUIRE(k_ffi_frame_set_signed(frame.get(), 0, K_FFI_SINT8, 128) == K_FFI_OUT_OF_RANGE);
    REQUIRE(k_ffi_frame_set_signed(frame.get(), 0, K_FFI_SINT8, -129) == K_FFI_OUT_OF_RANGE);
    REQUIRE(k_ffi_frame_set_float(frame.get(), 0, K_FFI_SINT8, 1) == K_FFI_ARGUMENT_TYPE);
    REQUIRE(k_ffi_frame_set_pointer(frame.get(), 0, K_FFI_SINT8, nullptr) == K_FFI_ARGUMENT_TYPE);
    REQUIRE(k_ffi_frame_invoke(function, frame.get(), 1, false) == K_FFI_ARGUMENT_COUNT);
    REQUIRE(k_ffi_frame_invoke(function, frame.get(), 0, false) == K_FFI_ARGUMENT_COUNT);
    REQUIRE(k_ffi_frame_set_signed(frame.get(), 0, K_FFI_SINT8, 1) == K_FFI_OK);
    REQUIRE(k_ffi_frame_set_signed(frame.get(), 0, K_FFI_SINT8, 128) == K_FFI_OUT_OF_RANGE);
    REQUIRE(k_ffi_frame_invoke(function, frame.get(), 1, false) == K_FFI_ARGUMENT_COUNT);
    REQUIRE(k_ffi_frame_set_signed(frame.get(), 0, K_FFI_SINT8, 1) == K_FFI_OK);
    REQUIRE(k_ffi_frame_invoke(nullptr, frame.get(), 1, false) == K_FFI_NULL_FUNCTION);
    REQUIRE(k_ffi_frame_invoke(function, frame.get(), 2, false) == K_FFI_ARGUMENT_COUNT);
    REQUIRE(__k_ffi_invoke(function, frame.get(), 1, 2) == K_FFI_OUT_OF_RANGE);
    REQUIRE(forbidden_calls.load(std::memory_order_relaxed) == 0);
    int64_t result = 0;
    REQUIRE(k_ffi_frame_result_signed(frame.get(), &result) == K_FFI_INVALID_FRAME);
    REQUIRE(k_ffi_frame_result_signed(nullptr, &result) == K_FFI_INVALID_FRAME);
    REQUIRE(k_ffi_frame_result_errno(frame.get(), &status) == K_FFI_INVALID_FRAME);
}

TEST_CASE("Native FFI: optional errno capture has an explicit restoration policy",
          "[libk][ffi][native]") {
    auto signature = prepare(K_FFI_SINT32, {});
    auto frame = create_frame(signature.get());
    errno = EDOM;
    int32_t invocation_status = k_ffi_frame_invoke(function_address(ffi_fixture_observe_errno),
                                                  frame.get(), 0, true);
    const int captured_errno = errno;
    REQUIRE(invocation_status == K_FFI_OK);
    REQUIRE(captured_errno == ERANGE);
    int64_t observed = -1;
    REQUIRE(k_ffi_frame_result_signed(frame.get(), &observed) == K_FFI_OK);
    REQUIRE(observed == 0);
    int32_t error = 0;
    REQUIRE(k_ffi_frame_result_errno(frame.get(), &error) == K_FFI_OK);
    REQUIRE(error == ERANGE);

    errno = EDOM;
    invocation_status = k_ffi_frame_invoke(function_address(ffi_fixture_observe_errno),
                                          frame.get(), 0, false);
    const int restored_errno = errno;
    REQUIRE(invocation_status == K_FFI_OK);
    REQUIRE(restored_errno == EDOM);
    REQUIRE(k_ffi_frame_result_signed(frame.get(), &observed) == K_FFI_OK);
    REQUIRE(observed == EDOM);
    REQUIRE(k_ffi_frame_result_errno(frame.get(), &error) == K_FFI_OK);
    REQUIRE(error == 0);
    REQUIRE(k_ffi_frame_invoke(nullptr, frame.get(), 0, true) == K_FFI_NULL_FUNCTION);
    REQUIRE(k_ffi_frame_result_errno(frame.get(), &error) == K_FFI_INVALID_FRAME);

    auto clean_signature = prepare(K_FFI_SINT64, {});
    auto clean_frame = create_frame(clean_signature.get());
    errno = EDOM;
    invocation_status = k_ffi_frame_invoke(function_address(ffi_fixture_zero),
                                          clean_frame.get(), 0, true);
    const int cleared_errno = errno;
    REQUIRE(invocation_status == K_FFI_OK);
    REQUIRE(cleared_errno == 0);
    REQUIRE(k_ffi_frame_result_errno(clean_frame.get(), &error) == K_FFI_OK);
    REQUIRE(error == 0);
}

TEST_CASE("Native FFI: independent concurrent frames share a prepared signature",
          "[libk][ffi][native]") {
    auto signature = prepare(K_FFI_SINT64, {K_FFI_SINT64, K_FFI_SINT32});
    std::atomic<bool> success{true};
    std::array<std::thread, 4> workers;
    for (unsigned worker = 0; worker < workers.size(); ++worker) {
        workers[worker] = std::thread([&, worker] {
            int32_t status = -1;
            Frame frame(k_ffi_frame_create(signature.get(), &status), k_ffi_frame_release);
            if (!frame || status != K_FFI_OK) {
                success.store(false, std::memory_order_relaxed);
                return;
            }
            const int32_t expected_errno = worker % 2 ? EDOM : ERANGE;
            for (int64_t i = 0; i < 128; ++i) {
                int64_t expected = -(static_cast<int64_t>(worker) * 1000 + i + 1);
                int64_t value = 0;
                int32_t error = 0;
                if (k_ffi_frame_set_signed(frame.get(), 0, K_FFI_SINT64, expected) != K_FFI_OK
                    || k_ffi_frame_set_signed(frame.get(), 1, K_FFI_SINT32, expected_errno) != K_FFI_OK
                    || k_ffi_frame_invoke(function_address(ffi_fixture_errno),
                                          frame.get(), 2, true) != K_FFI_OK
                    || k_ffi_frame_result_signed(frame.get(), &value) != K_FFI_OK
                    || k_ffi_frame_result_errno(frame.get(), &error) != K_FFI_OK
                    || value != expected || error != expected_errno || errno != expected_errno) {
                    success.store(false, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    REQUIRE(success.load(std::memory_order_relaxed));
}

TEST_CASE("Native FFI: real Unix symbols accept byte buffers and native size_t",
          "[libk][ffi][native]") {
    void *strlen_symbol = dlsym(RTLD_DEFAULT, "strlen");
    REQUIRE(strlen_symbol);
    auto length_signature = prepare(K_FFI_SIZE_T, {K_FFI_POINTER});
    auto length_frame = create_frame(length_signature.get());
    char text[] = "native buffer";
    REQUIRE(k_ffi_frame_set_pointer(length_frame.get(), 0, K_FFI_POINTER, text) == K_FFI_OK);
    REQUIRE(k_ffi_frame_invoke(strlen_symbol, length_frame.get(), 1, false) == K_FFI_OK);
    uint64_t length = 0;
    REQUIRE(k_ffi_frame_result_unsigned(length_frame.get(), &length) == K_FFI_OK);
    REQUIRE(length == std::strlen(text));

    void *memcpy_symbol = dlsym(RTLD_DEFAULT, "memcpy");
    REQUIRE(memcpy_symbol);
    auto copy_signature = prepare(K_FFI_POINTER, {K_FFI_POINTER, K_FFI_POINTER, K_FFI_SIZE_T});
    auto copy_frame = create_frame(copy_signature.get());
    std::array<unsigned char, 5> input{0, 128, 255, 7, 0};
    std::array<unsigned char, 5> output{};
    REQUIRE(k_ffi_frame_set_pointer(copy_frame.get(), 0, K_FFI_POINTER, output.data()) == K_FFI_OK);
    REQUIRE(k_ffi_frame_set_pointer(copy_frame.get(), 1, K_FFI_POINTER, input.data()) == K_FFI_OK);
    REQUIRE(k_ffi_frame_set_unsigned(copy_frame.get(), 2, K_FFI_SIZE_T, input.size()) == K_FFI_OK);
    REQUIRE(k_ffi_frame_invoke(memcpy_symbol, copy_frame.get(), 3, false) == K_FFI_OK);
    void *result = nullptr;
    REQUIRE(k_ffi_frame_result_pointer(copy_frame.get(), &result) == K_FFI_OK);
    REQUIRE(result == output.data());
    REQUIRE(output == input);
}

TEST_CASE("Native FFI: primitive K bridge reports statuses without native layouts",
          "[libk][ffi][native]") {
    int32_t status = -1;
    int32_t parameter = K_FFI_C_LONG;
    void *signature = __k_ffi_prepare(K_FFI_C_LONG, &parameter, 1, K_FFI_ABI_DEFAULT, &status);
    REQUIRE(signature);
    REQUIRE(status == K_FFI_OK);
    void *frame = __k_ffi_frame_create(signature, &status);
    REQUIRE(frame);
    REQUIRE(status == K_FFI_OK);
    __k_ffi_signature_release(signature);
    REQUIRE(__k_ffi_result_signed(frame, &status) == 0);
    REQUIRE(status == K_FFI_INVALID_FRAME);
    REQUIRE(__k_ffi_set_signed(frame, 0, K_FFI_SINT64, INT64_MIN) == K_FFI_OK);
    REQUIRE(__k_ffi_invoke(function_address(ffi_fixture_c_long), frame, 1, 0) == K_FFI_OK);
    REQUIRE(__k_ffi_result_signed(frame, &status) == INT64_MIN);
    REQUIRE(status == K_FFI_OK);
    REQUIRE(__k_ffi_result_type(frame, &status) == K_FFI_C_LONG);
    REQUIRE(status == K_FFI_OK);
    REQUIRE(__k_ffi_result_unsigned(frame, &status) == 0);
    REQUIRE(status == K_FFI_ARGUMENT_TYPE);
    REQUIRE(__k_ffi_result_float(frame, &status) == 0.0);
    REQUIRE(status == K_FFI_ARGUMENT_TYPE);
    REQUIRE(__k_ffi_result_pointer(frame, &status) == nullptr);
    REQUIRE(status == K_FFI_ARGUMENT_TYPE);
    REQUIRE(__k_ffi_result_errno(frame, &status) == 0);
    REQUIRE(status == K_FFI_OK);
    REQUIRE(__k_ffi_type_size(K_FFI_C_LONG) == sizeof(long));
    REQUIRE(__k_ffi_type_canonical(K_FFI_C_LONG) == K_FFI_SINT64);
    REQUIRE(std::strcmp(__k_ffi_status_message(K_FFI_OUT_OF_RANGE),
                        k_ffi_status_message(K_FFI_OUT_OF_RANGE)) == 0);
    for (int32_t code = K_FFI_OK; code <= K_FFI_INVALID_FRAME; ++code) {
        REQUIRE(k_ffi_status_message(code));
        REQUIRE(std::strlen(k_ffi_status_message(code)) > 0);
        REQUIRE(k_ffi_status_message(code) == k_ffi_status_message(code));
    }
    REQUIRE(std::strcmp(k_ffi_status_message(-1), "unknown native call status") == 0);
    __k_ffi_frame_release(frame);
    __k_ffi_frame_release(nullptr);
    __k_ffi_signature_release(nullptr);
    REQUIRE(__k_ffi_prepare(-1, nullptr, 0, K_FFI_ABI_DEFAULT, nullptr) == nullptr);
}
