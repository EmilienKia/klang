# ─────────────────────────────────────────────────────────────────────────────
# libk/libk — Base standard library for the K language (module "k")
#
# This library is compiled from K source using the klangc compiler built
# by the klang/ sub-project.  It produces:
#   - libk.so   (shared library)
#   - libk.a    (static library)
#   - libk.kdi  (K descriptor interface for import resolution)
#   - k.kdi     (symlink, used by the directory-based file resolver)
#
# Every K program and library is automatically linked against libk.
# ─────────────────────────────────────────────────────────────────────────────
set(LIBK_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffi.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/annotations.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/object.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rtti.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/memory.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shared.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/expected.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/collections.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/string.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/filter_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/buffered_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/data_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/print_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/file.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/stdio.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/math/math.k
)
set(LIBK_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(LIBK_SO  ${LIBK_OUTPUT_DIR}/libk.so)
set(LIBK_A   ${LIBK_OUTPUT_DIR}/libk.a)
set(LIBK_KDI ${LIBK_OUTPUT_DIR}/libk.kdi)
set(LIBK_KDI_LINK ${LIBK_OUTPUT_DIR}/k.kdi)

# ── Compile the C runtime helpers (fatal error functions) ────────────────
# These are compiled to a .o and passed to klangc so they end up in both
# libk.so and libk.a.  Every K module resolves the fatal symbols from libk
# instead of duplicating them.
set(FATAL_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/fatal.c)
set(FATAL_OBJ ${LIBK_OUTPUT_DIR}/fatal.o)

set(FATAL_C_FLAGS -c -fPIC)
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND FATAL_C_FLAGS -DNDEBUG)
endif()

add_custom_command(
    OUTPUT  ${FATAL_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -o ${FATAL_OBJ} ${FATAL_C}
    DEPENDS ${FATAL_C}
    COMMENT "Compiling K runtime fatal helpers (fatal.c)"
    VERBATIM
)

# ── Compile the RTTI C runtime helpers ───────────────────────────────────
set(RTTI_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/rtti.c)
set(RTTI_OBJ ${LIBK_OUTPUT_DIR}/rtti.o)

add_custom_command(
    OUTPUT  ${RTTI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -o ${RTTI_OBJ} ${RTTI_C}
    DEPENDS ${RTTI_C}
    COMMENT "Compiling K runtime RTTI helpers (rtti.c)"
    VERBATIM
)

# ── Compile the I/O C runtime helpers ─────────────────────────────────────
set(IO_HELPERS_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/io/io_helpers.c)
set(IO_HELPERS_OBJ ${LIBK_OUTPUT_DIR}/io_helpers.o)

add_custom_command(
    OUTPUT  ${IO_HELPERS_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -o ${IO_HELPERS_OBJ} ${IO_HELPERS_C}
    DEPENDS ${IO_HELPERS_C}
    COMMENT "Compiling K runtime I/O helpers (io_helpers.c)"
    VERBATIM
)

# ── Compile the file I/O C runtime helpers ────────────────────────────────
set(FILE_IO_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/io/file.c)
set(FILE_IO_OBJ ${LIBK_OUTPUT_DIR}/file_io.o)

add_custom_command(
    OUTPUT  ${FILE_IO_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -o ${FILE_IO_OBJ} ${FILE_IO_C}
    DEPENDS ${FILE_IO_C}
    COMMENT "Compiling K runtime file I/O helpers (file.c)"
    VERBATIM
)

set(C_RUNTIME_OBJS ${FATAL_OBJ} ${RTTI_OBJ} ${IO_HELPERS_OBJ} ${FILE_IO_OBJ})

# ── Compile the K standard library using klangc ──────────────────────────────
add_custom_command(
    OUTPUT  ${LIBK_SO} ${LIBK_A} ${LIBK_KDI}
    COMMAND $<TARGET_FILE:klangc>
            --dyn-lib --static-lib
            --module-name k
            ${LIBK_SOURCES}
            ${C_RUNTIME_OBJS}
    DEPENDS klangc ${LIBK_SOURCES} ${C_RUNTIME_OBJS}
    WORKING_DIRECTORY ${LIBK_OUTPUT_DIR}
    COMMENT "Compiling K base standard library (module k)"
    VERBATIM
)
# The directory-based file resolver looks for "k.kdi" (module-name based),
# but gen_kdi produces "libk.kdi" (library-name based).  Create a symlink
# so both names work.
add_custom_command(
    OUTPUT  ${LIBK_KDI_LINK}
    COMMAND ${CMAKE_COMMAND} -E create_symlink libk.kdi k.kdi
    DEPENDS ${LIBK_KDI}
    WORKING_DIRECTORY ${LIBK_OUTPUT_DIR}
    COMMENT "Creating k.kdi symlink for directory-based resolver"
    VERBATIM
)
add_custom_target(libk_stdlib ALL
    DEPENDS ${LIBK_SO} ${LIBK_A} ${LIBK_KDI} ${LIBK_KDI_LINK}
)
# ── Export paths so other targets (compiler, tests) can find the stdlib ───────
set(KLANG_STDLIB_KDI_DIR "${LIBK_OUTPUT_DIR}" CACHE INTERNAL
    "Directory containing k.kdi (K base stdlib descriptor)")
set(KLANG_STDLIB_LIB_DIR "${LIBK_OUTPUT_DIR}" CACHE INTERNAL
    "Directory containing libk.so / libk.a (K base stdlib binaries)")

# ── libk tests ───────────────────────────────────────────────────────────────
# Tests that exercise the K standard library features (String, StringBuilder,
# etc.) via JIT compilation.  The test K source uses `import k;` to reference
# the real stdlib types — no class definitions are duplicated.
# ─────────────────────────────────────────────────────────────────────────────
add_executable(libk-tests
    tests/test-rtti.cpp
    tests/test-rtti-constructors.cpp
    tests/test-rtti-parameters.cpp
    tests/test-expected.cpp
    tests/test-string.cpp
    tests/test-string-literal.cpp
    tests/test-string-search.cpp
    tests/test-string-builder-mutation.cpp
    tests/test-io-byte-array-streams.cpp
    tests/test-io-filter-streams.cpp
    tests/test-io-buffered-streams.cpp
    tests/test-io-data-streams.cpp
    tests/test-io-file.cpp
    tests/test-io-print-stream.cpp
    tests/test-io-stdio.cpp
    tests/test-math.cpp
    tests/test-list.cpp
    tests/test-vector.cpp
)
target_link_libraries(libk-tests klang-test-helpers Catch2::Catch2WithMain dl)
add_dependencies(libk-tests libk_stdlib)
target_compile_definitions(libk-tests PRIVATE
    LIBK_KDI_DIR="${LIBK_OUTPUT_DIR}"
    LIBK_LIB_DIR="${LIBK_OUTPUT_DIR}"
)

add_test(NAME libk-tests COMMAND libk-tests)
