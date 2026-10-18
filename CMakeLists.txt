cmake_minimum_required(VERSION 3.17)
project(klang VERSION 0.0.1)

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
