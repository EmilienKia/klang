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

find_package(Catch2 REQUIRED)
find_package(nlohmann_json REQUIRED)

find_package(fmt REQUIRED)

find_package(Boost REQUIRED
    COMPONENTS program_options system filesystem
)
include_directories(${Boost_INCLUDE_DIRS})

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

# ── Sub-projects ──────────────────────────────────────────────────────────────
add_subdirectory(libkdi)
add_subdirectory(klang)
add_subdirectory(libk)
