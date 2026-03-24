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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/object.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/string.k
)
set(LIBK_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(LIBK_SO  ${LIBK_OUTPUT_DIR}/libk.so)
set(LIBK_A   ${LIBK_OUTPUT_DIR}/libk.a)
set(LIBK_KDI ${LIBK_OUTPUT_DIR}/libk.kdi)
set(LIBK_KDI_LINK ${LIBK_OUTPUT_DIR}/k.kdi)
# ── Compile the K standard library using klangc ──────────────────────────────
add_custom_command(
    OUTPUT  ${LIBK_SO} ${LIBK_A} ${LIBK_KDI}
    COMMAND $<TARGET_FILE:klangc>
            --dyn-lib --static-lib
            --module-name k
            ${LIBK_SOURCES}
    DEPENDS klangc ${LIBK_SOURCES}
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
    tests/test-string.cpp
    tests/test-string-literal.cpp
    tests/test-string-search.cpp
    tests/test-string-builder-mutation.cpp
)
target_link_libraries(libk-tests klang-test-helpers Catch2::Catch2WithMain dl)
add_dependencies(libk-tests libk_stdlib)
target_compile_definitions(libk-tests PRIVATE
    LIBK_KDI_DIR="${LIBK_OUTPUT_DIR}"
    LIBK_LIB_DIR="${LIBK_OUTPUT_DIR}"
)

add_test(NAME libk-tests COMMAND libk-tests)
