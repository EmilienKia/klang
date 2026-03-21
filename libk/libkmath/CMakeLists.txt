# ─────────────────────────────────────────────────────────────────────────────
# libk/libkmath — Optional math library for the K language (module "k::math")
#
# This library is compiled from K source using the klangc compiler.
# It produces:
#   - libk.math.so   (shared library)
#   - libk.math.a    (static library)
#   - libk.math.kdi  (K descriptor interface for import resolution)
#   - k.math.kdi     (symlink for directory-based file resolver)
#
# This is an OPTIONAL library — it is NOT auto-linked.
# Users must write: import k::math;
# ─────────────────────────────────────────────────────────────────────────────
set(LIBKMATH_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/math.k
)
set(LIBKMATH_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(LIBKMATH_SO  ${LIBKMATH_OUTPUT_DIR}/libk.math.so)
set(LIBKMATH_A   ${LIBKMATH_OUTPUT_DIR}/libk.math.a)
set(LIBKMATH_KDI ${LIBKMATH_OUTPUT_DIR}/libk.math.kdi)
set(LIBKMATH_KDI_LINK ${LIBKMATH_OUTPUT_DIR}/k.math.kdi)
# ── Compile the K math library using klangc ──────────────────────────────────
add_custom_command(
    OUTPUT  ${LIBKMATH_SO} ${LIBKMATH_A} ${LIBKMATH_KDI}
    COMMAND $<TARGET_FILE:klangc>
            --dyn-lib --static-lib
            --module-name k::math
            -I ${KLANG_STDLIB_KDI_DIR}
            -L ${KLANG_STDLIB_LIB_DIR}
            ${LIBKMATH_SOURCES}
    DEPENDS klangc libk_stdlib ${LIBKMATH_SOURCES}
    WORKING_DIRECTORY ${LIBKMATH_OUTPUT_DIR}
    COMMENT "Compiling K math library (module k::math)"
    VERBATIM
)
# The directory-based file resolver looks for "k.math.kdi" (module-name based),
# but gen_kdi produces "libk.math.kdi" (library-name based).  Create a symlink
# so both names work.
add_custom_command(
    OUTPUT  ${LIBKMATH_KDI_LINK}
    COMMAND ${CMAKE_COMMAND} -E create_symlink libk.math.kdi k.math.kdi
    DEPENDS ${LIBKMATH_KDI}
    WORKING_DIRECTORY ${LIBKMATH_OUTPUT_DIR}
    COMMENT "Creating k.math.kdi symlink for directory-based resolver"
    VERBATIM
)
add_custom_target(libkmath ALL
    DEPENDS ${LIBKMATH_SO} ${LIBKMATH_A} ${LIBKMATH_KDI} ${LIBKMATH_KDI_LINK}
)
# ── Export paths so tests can find the library ────────────────────────────────
set(KLANG_MATH_KDI_DIR "${LIBKMATH_OUTPUT_DIR}" CACHE INTERNAL
    "Directory containing k.math.kdi descriptor")
set(KLANG_MATH_LIB_DIR "${LIBKMATH_OUTPUT_DIR}" CACHE INTERNAL
    "Directory containing libk.math.so / libk.math.a")
# ── libkmath tests ───────────────────────────────────────────────────────────
add_executable(libkmath-tests
    tests/test-math.cpp
)
target_link_libraries(libkmath-tests klang-test-helpers Catch2::Catch2WithMain dl)
add_dependencies(libkmath-tests libkmath libk_stdlib)
target_compile_definitions(libkmath-tests PRIVATE
    LIBK_KDI_DIR="${KLANG_STDLIB_KDI_DIR}"
    LIBK_LIB_DIR="${KLANG_STDLIB_LIB_DIR}"
    LIBKMATH_KDI_DIR="${LIBKMATH_OUTPUT_DIR}"
    LIBKMATH_LIB_DIR="${LIBKMATH_OUTPUT_DIR}"
)
add_test(NAME libkmath-tests COMMAND libkmath-tests)
# libk.math.so has a DT_NEEDED on libk.so — the dynamic linker must find it.
set_tests_properties(libkmath-tests PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=${KLANG_STDLIB_LIB_DIR}:${LIBKMATH_OUTPUT_DIR}"
)
