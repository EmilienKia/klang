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
    src/kdi_doc.hpp
    src/kdi_doc.cpp
    src/kdi_docgen.hpp
    src/kdi_docgen.cpp
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
    tests/test_doc.cpp
    tests/test_docgen.cpp
)

target_link_libraries(kdi-tests PRIVATE kdi Catch2::Catch2WithMain)

add_test(NAME kdi-tests COMMAND kdi-tests)
