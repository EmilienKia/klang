cmake_minimum_required(VERSION 3.17)
project(klang VERSION 0.0.1)

# Scale explicit CTest TIMEOUT values without changing the test-registration APIs.
set(KLANG_CTEST_TIMEOUT_MULTIPLIER "1.0" CACHE STRING
    "Positive decimal multiplier for explicit CTest TIMEOUT values")
if(NOT "${KLANG_CTEST_TIMEOUT_MULTIPLIER}" MATCHES "^[0-9]+(\\.[0-9]*)?$"
        AND NOT "${KLANG_CTEST_TIMEOUT_MULTIPLIER}" MATCHES "^\\.[0-9]+$")
    message(FATAL_ERROR
        "KLANG_CTEST_TIMEOUT_MULTIPLIER must be a positive decimal number; "
        "got '${KLANG_CTEST_TIMEOUT_MULTIPLIER}'")
endif()
if(NOT "${KLANG_CTEST_TIMEOUT_MULTIPLIER}" MATCHES "[1-9]")
    message(FATAL_ERROR
        "KLANG_CTEST_TIMEOUT_MULTIPLIER must be greater than zero; "
        "got '${KLANG_CTEST_TIMEOUT_MULTIPLIER}'")
endif()

function(klang_scale_ctest_timeout OUTPUT_VARIABLE BASE_TIMEOUT)
    if(NOT "${BASE_TIMEOUT}" MATCHES "^[0-9]+$")
        message(FATAL_ERROR "CTest timeout must be a whole number of seconds; got '${BASE_TIMEOUT}'")
    endif()

    set(_whole_part "${KLANG_CTEST_TIMEOUT_MULTIPLIER}")
    set(_fractional_part "")
    if("${KLANG_CTEST_TIMEOUT_MULTIPLIER}" MATCHES "^([0-9]*)\\.([0-9]*)$")
        set(_whole_part "${CMAKE_MATCH_1}")
        set(_fractional_part "${CMAKE_MATCH_2}")
    endif()
    if(_whole_part STREQUAL "")
        set(_whole_part "0")
    endif()
    string(REGEX REPLACE "^0+" "" _whole_part "${_whole_part}")
    if(_whole_part STREQUAL "")
        set(_whole_part "0")
    endif()
    string(REGEX REPLACE "0+$" "" _fractional_part "${_fractional_part}")
    string(LENGTH "${_fractional_part}" _fractional_digits)

    set(_multiplier_digits "${_whole_part}${_fractional_part}")
    string(REGEX REPLACE "^0+" "" _multiplier_digits "${_multiplier_digits}")

    string(LENGTH "${_multiplier_digits}" _multiplier_length)
    set(_product "")
    set(_carry 0)
    math(EXPR _index "${_multiplier_length} - 1")
    while(_index GREATER_EQUAL 0)
        string(SUBSTRING "${_multiplier_digits}" "${_index}" 1 _digit)
        math(EXPR _partial "${_digit} * ${BASE_TIMEOUT} + ${_carry}")
        math(EXPR _product_digit "${_partial} % 10")
        math(EXPR _carry "${_partial} / 10")
        set(_product "${_product_digit}${_product}")
        math(EXPR _index "${_index} - 1")
    endwhile()
    while(_carry GREATER 0)
        math(EXPR _product_digit "${_carry} % 10")
        math(EXPR _carry "${_carry} / 10")
        set(_product "${_product_digit}${_product}")
    endwhile()

    string(LENGTH "${_product}" _product_length)
    while(_product_length LESS_EQUAL _fractional_digits)
        set(_product "0${_product}")
        math(EXPR _product_length "${_product_length} + 1")
    endwhile()
    math(EXPR _whole_digits "${_product_length} - ${_fractional_digits}")
    string(SUBSTRING "${_product}" 0 "${_whole_digits}" _scaled_timeout)
    string(SUBSTRING "${_product}" "${_whole_digits}" "${_fractional_digits}" _fractional_product)
    string(REGEX REPLACE "^0+" "" _scaled_timeout "${_scaled_timeout}")
    if(_scaled_timeout STREQUAL "")
        set(_scaled_timeout "0")
    endif()

    if(_fractional_product MATCHES "[1-9]")
        string(LENGTH "${_scaled_timeout}" _scaled_length)
        math(EXPR _index "${_scaled_length} - 1")
        set(_carry 1)
        set(_rounded_timeout "")
        while(_index GREATER_EQUAL 0)
            string(SUBSTRING "${_scaled_timeout}" "${_index}" 1 _digit)
            if(_carry)
                math(EXPR _digit "${_digit} + 1")
                if(_digit GREATER 9)
                    set(_digit 0)
                else()
                    set(_carry 0)
                endif()
            endif()
            set(_rounded_timeout "${_digit}${_rounded_timeout}")
            math(EXPR _index "${_index} - 1")
        endwhile()
        if(_carry)
            set(_rounded_timeout "1${_rounded_timeout}")
        endif()
        set(_scaled_timeout "${_rounded_timeout}")
    endif()

    if(_scaled_timeout STREQUAL "0")
        set(_scaled_timeout "1")
    endif()
    set(${OUTPUT_VARIABLE} "${_scaled_timeout}" PARENT_SCOPE)
endfunction()

# ── C++ standard ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Testing ───────────────────────────────────────────────────────────────────
enable_testing()

# ── Common dependencies ───────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCBOR REQUIRED libcbor)
pkg_check_modules(LIBFFI REQUIRED IMPORTED_TARGET libffi)
pkg_check_modules(GMP REQUIRED IMPORTED_TARGET gmp)
pkg_check_modules(CMARK_GFM REQUIRED libcmark-gfm)
# Only the core CommonMark parser is needed. Some pkg-config files also list
# the optional extensions library, whose development files may not be installed.
find_library(CMARK_GFM_CORE_LIBRARY NAMES cmark-gfm HINTS ${CMARK_GFM_LIBRARY_DIRS})
if(NOT CMARK_GFM_CORE_LIBRARY)
    message(FATAL_ERROR "The cmark-gfm core library is required for KDI documentation")
endif()
add_library(CmarkGFM::Core UNKNOWN IMPORTED)
set_target_properties(CmarkGFM::Core PROPERTIES
    IMPORTED_LOCATION "${CMARK_GFM_CORE_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${CMARK_GFM_INCLUDE_DIRS}"
    INTERFACE_COMPILE_OPTIONS "${CMARK_GFM_CFLAGS_OTHER}"
)

find_package(Catch2 REQUIRED)
find_package(nlohmann_json REQUIRED)

find_package(fmt REQUIRED)

find_package(Boost REQUIRED
    COMPONENTS program_options system filesystem
)
include_directories(${Boost_INCLUDE_DIRS})

# ── liburing (Linux asynchronous I/O backend for libk) ────────────────────────
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Klang's I/O proactor currently requires Linux and liburing")
endif()
pkg_check_modules(LIBURING REQUIRED liburing>=2.5)
set(KLANG_HAVE_LIBURING ON)
message(STATUS "Found liburing ${LIBURING_VERSION} — libk async I/O uses io_uring")

# ── ICU (Unicode & internationalisation backend for libk time/locale) ────────
pkg_check_modules(ICU icu-uc icu-i18n)
if(ICU_FOUND)
    set(KLANG_HAVE_ICU ON)
    message(STATUS "Found ICU ${ICU_VERSION} — libk time enabled with ICU bridge")
else()
    set(KLANG_HAVE_ICU OFF)
    message(STATUS "ICU not found — libk time ICU bridge disabled")
endif()

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
if(LLVM_PACKAGE_VERSION VERSION_LESS "18.0.0")
    message(FATAL_ERROR "LLVM version 18.0.0 or higher is required")
endif()
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})
llvm_map_components_to_libnames(LLVM_LIBRARIES
    core irreader native orcjit support
    AArch64 ARM X86
)
message(STATUS "LLVM libs (by cmake): ${LLVM_LIBRARIES}")

# ── Path resolver configuration ───────────────────────────────────────────────
# Name of the environment variable used to pass extra KDI/lib search dirs.
if(NOT DEFINED KLANG_LIB_PATH_ENV_VAR)
    set(KLANG_LIB_PATH_ENV_VAR "KLANG_LIB_PATH")
endif()

# Build-time path to the K base standard library KDI and binary directories
# (used by tests and the compiler to locate libk.kdi and libk.so/.a)
set(KLANG_STDLIB_KDI_DIR "${CMAKE_BINARY_DIR}/libk/libk")
set(KLANG_STDLIB_LIB_DIR "${CMAKE_BINARY_DIR}/libk/libk")

# Multi-arch tuple (e.g. "x86_64-linux-gnu").
if(CMAKE_LIBRARY_ARCHITECTURE)
    set(KLANG_LIBRARY_ARCHITECTURE "${CMAKE_LIBRARY_ARCHITECTURE}")
else()
    execute_process(
        COMMAND gcc -dumpmachine
        OUTPUT_VARIABLE _gcc_machine
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(_gcc_machine)
        set(KLANG_LIBRARY_ARCHITECTURE "${_gcc_machine}")
    else()
        set(KLANG_LIBRARY_ARCHITECTURE "")
    endif()
endif()
message(STATUS "KLANG_LIBRARY_ARCHITECTURE: ${KLANG_LIBRARY_ARCHITECTURE}")
message(STATUS "KLANG_LIB_PATH_ENV_VAR: ${KLANG_LIB_PATH_ENV_VAR}")
message(STATUS "KLANG_STDLIB_KDI_DIR: ${KLANG_STDLIB_KDI_DIR}")
message(STATUS "KLANG_STDLIB_LIB_DIR: ${KLANG_STDLIB_LIB_DIR}")

# ── Sub-projects ──────────────────────────────────────────────────────────────
option(KLANG_BUILD_SAMPLES "Compile the example K programs" OFF)
option(KLANG_BUILD_IO_SAMPLES "Include file and TCP I/O examples when samples are enabled" ON)

add_subdirectory(libkdi)
add_subdirectory(klang)
add_subdirectory(libk)
if(KLANG_BUILD_SAMPLES)
    add_subdirectory(samples)
endif()
