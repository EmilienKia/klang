cmake_minimum_required(VERSION 3.17)

set(CMAKE_CXX_STANDARD 20)

# ─────────────────────────────────────────────────────────────────────────────
# Dependencies
# ─────────────────────────────────────────────────────────────────────────────

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCBOR REQUIRED libcbor)

find_package(Catch2 REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED COMPONENTS program_options)

# ─────────────────────────────────────────────────────────────────────────────
# libkdi — KDI DTO + CBOR I/O + validation + dump
# ─────────────────────────────────────────────────────────────────────────────

add_library(kdi STATIC
    src/kdi_types.hpp
    src/kdi_aggregates.hpp
    src/kdi_file.hpp
    src/kdi_validate.hpp
    src/kdi_validate.cpp
    src/kdi_cbor.hpp
    src/kdi_cbor.cpp
    src/kdi_json.hpp
    src/kdi_json.cpp
    src/kdi_dump.hpp
    src/kdi_dump.cpp
    src/kdi_symbols.hpp
    src/kdi_symbols.cpp
)

target_include_directories(kdi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${LIBCBOR_INCLUDE_DIRS}
)

target_link_libraries(kdi PUBLIC ${LIBCBOR_LIBRARIES} nlohmann_json::nlohmann_json)

target_compile_options(kdi PRIVATE
    -Wno-unused-result
    ${LIBCBOR_CFLAGS_OTHER}
)

# ─────────────────────────────────────────────────────────────────────────────
# kdi-tool — command-line dump / validate utility
# ─────────────────────────────────────────────────────────────────────────────

add_executable(kdi-tool
    src/main.cpp
)

# Output binary name is "kditool"
set_target_properties(kdi-tool PROPERTIES OUTPUT_NAME kditool)

target_link_libraries(kdi-tool PRIVATE kdi Boost::program_options)

# ─────────────────────────────────────────────────────────────────────────────
# Tests
# ─────────────────────────────────────────────────────────────────────────────

add_executable(kdi-tests
    tests/test_main.cpp
    tests/test_dto.cpp
    tests/test_cbor.cpp
    tests/test_validate.cpp
    tests/test_json.cpp
    tests/test_symbols.cpp
)

target_link_libraries(kdi-tests PRIVATE kdi Catch2::Catch2WithMain)

enable_testing()
add_test(NAME kdi-tests COMMAND kdi-tests)
