# ─────────────────────────────────────────────────────────────────────────────
# libk/crypto — Crypto standard library for the K language (module "k::crypto")
#
# This library is compiled from K source using the klangc compiler built
# by the klang/ sub-project.  It produces:
#   - libk.crypto.so   (shared library)
#   - libk.crypto.a    (static library)
#   - libk.crypto.kdi  (K descriptor interface for import resolution)
# ─────────────────────────────────────────────────────────────────────────────
set(LIBK_CRYPTO_DIGEST_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/src/digest.k)
set(LIBK_CRYPTO_GENERATED_SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated-sources)
set(LIBK_CRYPTO_DIGEST_CORE_SOURCE ${LIBK_CRYPTO_GENERATED_SOURCE_DIR}/digest.k)
set(LIBK_CRYPTO_IO_DIGEST_SOURCE ${LIBK_CRYPTO_GENERATED_SOURCE_DIR}/io_crypto_digest.k)

# digest.k contains both modules' implementations. K source files belong to a
# single module, so generate the two module-specific compiler inputs from the
# shared source at configure time.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${LIBK_CRYPTO_DIGEST_SOURCE}
)
file(READ ${LIBK_CRYPTO_DIGEST_SOURCE} _libk_crypto_digest_source)
set(_libk_crypto_io_marker "// KLANG_CRYPTO_IO_DIGEST_SOURCE")
string(FIND "${_libk_crypto_digest_source}" "${_libk_crypto_io_marker}"
       _libk_crypto_io_marker_offset)
if(_libk_crypto_io_marker_offset EQUAL -1)
    message(FATAL_ERROR "Missing crypto I/O digest source marker in digest.k")
endif()
string(LENGTH "${_libk_crypto_io_marker}" _libk_crypto_io_marker_length)
string(SUBSTRING "${_libk_crypto_digest_source}" 0
       ${_libk_crypto_io_marker_offset} _libk_crypto_digest_core)
math(EXPR _libk_crypto_io_source_offset
     "${_libk_crypto_io_marker_offset} + ${_libk_crypto_io_marker_length}")
string(SUBSTRING "${_libk_crypto_digest_source}" ${_libk_crypto_io_source_offset}
       -1 _libk_crypto_io_digest)
file(MAKE_DIRECTORY ${LIBK_CRYPTO_GENERATED_SOURCE_DIR})
file(WRITE ${LIBK_CRYPTO_DIGEST_CORE_SOURCE} "${_libk_crypto_digest_core}")
file(WRITE ${LIBK_CRYPTO_IO_DIGEST_SOURCE}
    "module k::io::crypto;\nimport k::crypto;\n${_libk_crypto_io_digest}")

set(LIBK_CRYPTO_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/crypto.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/capabilities.k
    ${LIBK_CRYPTO_DIGEST_CORE_SOURCE}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hmac.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hkdf.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/keys.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/symmetric.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/libcrypto_ffi.k
)
set(LIBK_CRYPTO_IO_SOURCES
    ${LIBK_CRYPTO_IO_DIGEST_SOURCE}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/hmac_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cipher_stream.k
)

set(LIBK_CRYPTO_OPENSSL_FFI_C ${CMAKE_CURRENT_SOURCE_DIR}/src/openssl_ffi.c)
set(LIBK_CRYPTO_OPENSSL_FFI_OBJ ${CMAKE_CURRENT_BINARY_DIR}/openssl_ffi.o)
set(LIBK_CRYPTO_SYMMETRIC_FFI_C ${CMAKE_CURRENT_SOURCE_DIR}/src/symmetric_ffi.c)
set(LIBK_CRYPTO_SYMMETRIC_FFI_OBJ ${CMAKE_CURRENT_BINARY_DIR}/symmetric_ffi.o)
set(LIBK_CRYPTO_HMAC_FFI_C ${CMAKE_CURRENT_SOURCE_DIR}/src/hmac_ffi.c)
set(LIBK_CRYPTO_HMAC_FFI_OBJ ${CMAKE_CURRENT_BINARY_DIR}/hmac_ffi.o)
set(LIBK_CRYPTO_HKDF_FFI_C ${CMAKE_CURRENT_SOURCE_DIR}/src/hkdf_ffi.c)
set(LIBK_CRYPTO_HKDF_FFI_OBJ ${CMAKE_CURRENT_BINARY_DIR}/hkdf_ffi.o)
set(LIBK_CRYPTO_CCM_C ${CMAKE_CURRENT_SOURCE_DIR}/src/ccm_engine.c)
set(LIBK_CRYPTO_CCM_H ${CMAKE_CURRENT_SOURCE_DIR}/src/ccm_engine.h)
set(LIBK_CRYPTO_CCM_OBJ ${CMAKE_CURRENT_BINARY_DIR}/ccm_engine.o)
set(LIBK_CRYPTO_C_FLAGS -c -fPIC -fexceptions -fasynchronous-unwind-tables -pthread
    -I${OPENSSL_INCLUDE_DIR})
if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    list(APPEND LIBK_CRYPTO_C_FLAGS -DNDEBUG)
endif()

add_custom_command(
    OUTPUT ${LIBK_CRYPTO_OPENSSL_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${LIBK_CRYPTO_C_FLAGS}
            -o ${LIBK_CRYPTO_OPENSSL_FFI_OBJ} ${LIBK_CRYPTO_OPENSSL_FFI_C}
    DEPENDS ${LIBK_CRYPTO_OPENSSL_FFI_C}
    COMMENT "Compiling libk-crypto OpenSSL FFI wrappers (openssl_ffi.c)"
    VERBATIM
)

add_custom_command(
    OUTPUT ${LIBK_CRYPTO_SYMMETRIC_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${LIBK_CRYPTO_C_FLAGS}
            -o ${LIBK_CRYPTO_SYMMETRIC_FFI_OBJ} ${LIBK_CRYPTO_SYMMETRIC_FFI_C}
    DEPENDS ${LIBK_CRYPTO_SYMMETRIC_FFI_C} ${LIBK_CRYPTO_CCM_H}
    COMMENT "Compiling libk-crypto symmetric OpenSSL bridge"
    VERBATIM
)

add_custom_command(
    OUTPUT ${LIBK_CRYPTO_HMAC_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${LIBK_CRYPTO_C_FLAGS}
            -o ${LIBK_CRYPTO_HMAC_FFI_OBJ} ${LIBK_CRYPTO_HMAC_FFI_C}
    DEPENDS ${LIBK_CRYPTO_HMAC_FFI_C}
    COMMENT "Compiling libk-crypto HMAC OpenSSL bridge"
    VERBATIM
)

add_custom_command(
    OUTPUT ${LIBK_CRYPTO_HKDF_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${LIBK_CRYPTO_C_FLAGS}
            -o ${LIBK_CRYPTO_HKDF_FFI_OBJ} ${LIBK_CRYPTO_HKDF_FFI_C}
    DEPENDS ${LIBK_CRYPTO_HKDF_FFI_C}
    COMMENT "Compiling libk-crypto HKDF OpenSSL bridge"
    VERBATIM
)

add_custom_command(
    OUTPUT ${LIBK_CRYPTO_CCM_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${LIBK_CRYPTO_C_FLAGS}
            -o ${LIBK_CRYPTO_CCM_OBJ} ${LIBK_CRYPTO_CCM_C}
    DEPENDS ${LIBK_CRYPTO_CCM_C} ${LIBK_CRYPTO_CCM_H}
    COMMENT "Compiling libk-crypto streaming CCM engine"
    VERBATIM
)

set(LIBK_CRYPTO_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(LIBK_CRYPTO_SO  ${LIBK_CRYPTO_OUTPUT_DIR}/libk.crypto.so)
set(LIBK_CRYPTO_A   ${LIBK_CRYPTO_OUTPUT_DIR}/libk.crypto.a)
set(LIBK_CRYPTO_KDI ${LIBK_CRYPTO_OUTPUT_DIR}/libk.crypto.kdi)
set(LIBK_CRYPTO_KDI_LINK ${LIBK_CRYPTO_OUTPUT_DIR}/k.crypto.kdi)
set(LIBK_CRYPTO_IO_SO ${LIBK_CRYPTO_OUTPUT_DIR}/libk.io.crypto.so)
set(LIBK_CRYPTO_IO_A ${LIBK_CRYPTO_OUTPUT_DIR}/libk.io.crypto.a)
set(LIBK_CRYPTO_IO_KDI ${LIBK_CRYPTO_OUTPUT_DIR}/libk.io.crypto.kdi)
set(LIBK_CRYPTO_IO_KDI_LINK ${LIBK_CRYPTO_OUTPUT_DIR}/k.io.crypto.kdi)
set(LIBK_CRYPTO_MD_DOC_ROOT ${LIBK_CRYPTO_OUTPUT_DIR}/doc/md)
set(LIBK_CRYPTO_DOC_STAMP ${LIBK_CRYPTO_MD_DOC_ROOT}/.docgen.stamp)
set(LIBK_CRYPTO_IO_MD_DOC_STAMP ${LIBK_CRYPTO_MD_DOC_ROOT}/k::io::crypto/.docgen.stamp)
set(LIBK_CRYPTO_HTML_DOC_ROOT ${LIBK_CRYPTO_OUTPUT_DIR}/doc/html)
set(LIBK_CRYPTO_HTML_DOC_STAMP ${LIBK_CRYPTO_HTML_DOC_ROOT}/.docgen-html.stamp)
set(LIBK_CRYPTO_IO_HTML_DOC_STAMP ${LIBK_CRYPTO_HTML_DOC_ROOT}/k::io::crypto/.docgen-html.stamp)

configure_file(libk.crypto.pc.in "${LIBK_CRYPTO_OUTPUT_DIR}/libk.crypto.pc" @ONLY)

# ── Compile the K crypto library using klangc ────────────────────────────────
# klangc places --lib dependencies after the native objects.
add_custom_command(
    OUTPUT ${LIBK_CRYPTO_SO} ${LIBK_CRYPTO_A} ${LIBK_CRYPTO_KDI}
    COMMAND $<TARGET_FILE:klangc>
        --dyn-lib --static-lib
        --module-name k::crypto
        --lib ${OPENSSL_CRYPTO_LIBRARY}
        --lib pthread
        ${LIBK_CRYPTO_SOURCES}
        ${LIBK_CRYPTO_OPENSSL_FFI_OBJ}
        ${LIBK_CRYPTO_SYMMETRIC_FFI_OBJ}
        ${LIBK_CRYPTO_HMAC_FFI_OBJ}
        ${LIBK_CRYPTO_HKDF_FFI_OBJ}
        ${LIBK_CRYPTO_CCM_OBJ}
    DEPENDS klangc libk_stdlib ${LIBK_CRYPTO_SOURCES}
            ${LIBK_CRYPTO_OPENSSL_FFI_OBJ} ${LIBK_CRYPTO_SYMMETRIC_FFI_OBJ}
            ${LIBK_CRYPTO_HMAC_FFI_OBJ} ${LIBK_CRYPTO_HKDF_FFI_OBJ}
            ${LIBK_CRYPTO_CCM_OBJ}
            ${OPENSSL_CRYPTO_LIBRARY}
    WORKING_DIRECTORY ${LIBK_CRYPTO_OUTPUT_DIR}
    COMMENT "Compiling K crypto standard library (module k::crypto)"
    VERBATIM
)
# The resolver uses the module-name-based filename k.crypto.kdi, while the
# compiler emits libk.crypto.kdi next to the library.
add_custom_command(
    OUTPUT ${LIBK_CRYPTO_KDI_LINK}
    COMMAND ${CMAKE_COMMAND} -E create_symlink libk.crypto.kdi k.crypto.kdi
    DEPENDS ${LIBK_CRYPTO_KDI}
    WORKING_DIRECTORY ${LIBK_CRYPTO_OUTPUT_DIR}
    COMMENT "Creating k.crypto.kdi symlink for the directory-based resolver"
    VERBATIM
)
add_custom_command(
    OUTPUT ${LIBK_CRYPTO_IO_SO} ${LIBK_CRYPTO_IO_A} ${LIBK_CRYPTO_IO_KDI}
    COMMAND $<TARGET_FILE:klangc>
        --dyn-lib --static-lib
        --module-name k::io::crypto
        --lib ${LIBK_CRYPTO_SO}
        ${LIBK_CRYPTO_IO_SOURCES}
    DEPENDS klangc libk_stdlib ${LIBK_CRYPTO_SO} ${LIBK_CRYPTO_KDI_LINK}
            ${LIBK_CRYPTO_IO_SOURCES}
    WORKING_DIRECTORY ${LIBK_CRYPTO_OUTPUT_DIR}
    COMMENT "Compiling K crypto I/O module (module k::io::crypto)"
    VERBATIM
)
add_custom_command(
    OUTPUT ${LIBK_CRYPTO_IO_KDI_LINK}
    COMMAND ${CMAKE_COMMAND} -E create_symlink libk.io.crypto.kdi k.io.crypto.kdi
    DEPENDS ${LIBK_CRYPTO_IO_KDI}
    WORKING_DIRECTORY ${LIBK_CRYPTO_OUTPUT_DIR}
    COMMENT "Creating k.io.crypto.kdi symlink for the directory-based resolver"
    VERBATIM
)
add_custom_target(libk_crypto_stdlib ALL
    DEPENDS ${LIBK_CRYPTO_SO} ${LIBK_CRYPTO_A} ${LIBK_CRYPTO_KDI} ${LIBK_CRYPTO_KDI_LINK}
            ${LIBK_CRYPTO_IO_SO} ${LIBK_CRYPTO_IO_A} ${LIBK_CRYPTO_IO_KDI}
            ${LIBK_CRYPTO_IO_KDI_LINK}
)
add_dependencies(libk_crypto_stdlib libk_stdlib)

# ── Generate Markdown documentation from the produced KDI ────────────────────
# Output tree root: ${LIBK_CRYPTO_MD_DOC_ROOT}/k::crypto/
add_custom_command(
    OUTPUT ${LIBK_CRYPTO_DOC_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBK_CRYPTO_MD_DOC_ROOT}
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${LIBK_CRYPTO_MD_DOC_ROOT}/k::crypto"
    COMMAND $<TARGET_FILE:kdi-tool> docgen --md ${LIBK_CRYPTO_KDI} ${LIBK_CRYPTO_MD_DOC_ROOT}
    COMMAND ${CMAKE_COMMAND} -E touch ${LIBK_CRYPTO_DOC_STAMP}
    DEPENDS kdi-tool ${LIBK_CRYPTO_KDI}
    WORKING_DIRECTORY ${LIBK_CRYPTO_OUTPUT_DIR}
    COMMENT "Generating libk.crypto Markdown documentation"
    VERBATIM
)
add_custom_command(
    OUTPUT ${LIBK_CRYPTO_IO_MD_DOC_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBK_CRYPTO_MD_DOC_ROOT}
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${LIBK_CRYPTO_MD_DOC_ROOT}/k::io::crypto"
    COMMAND $<TARGET_FILE:kdi-tool> docgen --md ${LIBK_CRYPTO_IO_KDI} ${LIBK_CRYPTO_MD_DOC_ROOT}
    COMMAND ${CMAKE_COMMAND} -E touch ${LIBK_CRYPTO_IO_MD_DOC_STAMP}
    DEPENDS kdi-tool ${LIBK_CRYPTO_IO_KDI}
    WORKING_DIRECTORY ${LIBK_CRYPTO_OUTPUT_DIR}
    COMMENT "Generating libk.crypto I/O Markdown documentation"
    VERBATIM
)

# ── Generate static HTML documentation from the produced KDI ─────────────────
# Output tree root: ${LIBK_CRYPTO_HTML_DOC_ROOT}/k::crypto/
add_custom_command(
    OUTPUT ${LIBK_CRYPTO_HTML_DOC_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBK_CRYPTO_HTML_DOC_ROOT}
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${LIBK_CRYPTO_HTML_DOC_ROOT}/k::crypto"
    COMMAND $<TARGET_FILE:kdi-tool> docgen --html ${LIBK_CRYPTO_KDI} ${LIBK_CRYPTO_HTML_DOC_ROOT}
    COMMAND ${CMAKE_COMMAND} -E touch ${LIBK_CRYPTO_HTML_DOC_STAMP}
    DEPENDS kdi-tool ${LIBK_CRYPTO_KDI}
    WORKING_DIRECTORY ${LIBK_CRYPTO_OUTPUT_DIR}
    COMMENT "Generating libk.crypto HTML documentation"
    VERBATIM
)
add_custom_command(
    OUTPUT ${LIBK_CRYPTO_IO_HTML_DOC_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBK_CRYPTO_HTML_DOC_ROOT}
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${LIBK_CRYPTO_HTML_DOC_ROOT}/k::io::crypto"
    COMMAND $<TARGET_FILE:kdi-tool> docgen --html ${LIBK_CRYPTO_IO_KDI} ${LIBK_CRYPTO_HTML_DOC_ROOT}
    COMMAND ${CMAKE_COMMAND} -E touch ${LIBK_CRYPTO_IO_HTML_DOC_STAMP}
    DEPENDS kdi-tool ${LIBK_CRYPTO_IO_KDI}
    WORKING_DIRECTORY ${LIBK_CRYPTO_OUTPUT_DIR}
    COMMENT "Generating libk.crypto I/O HTML documentation"
    VERBATIM
)

add_custom_target(libk_crypto_stdlib_doc ALL
    DEPENDS ${LIBK_CRYPTO_DOC_STAMP} ${LIBK_CRYPTO_HTML_DOC_STAMP}
            ${LIBK_CRYPTO_IO_MD_DOC_STAMP} ${LIBK_CRYPTO_IO_HTML_DOC_STAMP}
)
add_dependencies(libk_crypto_stdlib_doc libk_crypto_stdlib)

# ── Export paths so compiler and tests can find the crypto stdlib ─────────────
set(KLANG_LIBK_CRYPTO_KDI_DIR "${LIBK_CRYPTO_OUTPUT_DIR}" CACHE INTERNAL
    "Directory containing k.crypto.kdi (K crypto descriptor)" FORCE)
set(KLANG_LIBK_CRYPTO_LIB_DIR "${LIBK_CRYPTO_OUTPUT_DIR}" CACHE INTERNAL
    "Directory containing libk.crypto and libk.io.crypto binaries" FORCE)

# ── Crypto test helper and registration macro ─────────────────────────────────
add_library(libk-crypto-test-helpers STATIC tests/crypto_test_helpers.cpp)
target_link_libraries(libk-crypto-test-helpers PUBLIC klang-test-helpers ${CMAKE_DL_LIBS})
target_include_directories(libk-crypto-test-helpers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_compile_definitions(libk-crypto-test-helpers PUBLIC
    LIBK_KDI_DIR="${KLANG_STDLIB_KDI_DIR}"
    LIBK_LIB_DIR="${KLANG_STDLIB_LIB_DIR}"
    LIBK_CRYPTO_KDI_DIR="${LIBK_CRYPTO_OUTPUT_DIR}"
    LIBK_CRYPTO_LIB_DIR="${LIBK_CRYPTO_OUTPUT_DIR}"
)
add_dependencies(libk-crypto-test-helpers libk_stdlib libk_crypto_stdlib)

macro(libk_crypto_add_test TARGET TIMEOUT)
    add_executable(${TARGET} ${ARGN})
    target_link_libraries(${TARGET} PRIVATE libk-crypto-test-helpers Catch2::Catch2WithMain)
    add_dependencies(${TARGET} libk_stdlib libk_crypto_stdlib)
    add_test(NAME ${TARGET} COMMAND ${TARGET})
    klang_scale_ctest_timeout(_libk_crypto_test_timeout "${TIMEOUT}")
    set_tests_properties(${TARGET} PROPERTIES TIMEOUT "${_libk_crypto_test_timeout}")
endmacro()

libk_crypto_add_test(libk-crypto-tests 120 tests/test-crypto.cpp)
libk_crypto_add_test(libk-crypto-hmac-tests 120
    tests/test-hmac.cpp tests/test-hmac-stream.cpp tests/test-hmac-capabilities.cpp)
target_include_directories(libk-crypto-hmac-tests PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(libk-crypto-hmac-tests PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
libk_crypto_add_test(libk-crypto-hkdf-tests 120
    tests/test-hkdf.cpp tests/test-hkdf-capabilities.cpp)
target_include_directories(libk-crypto-hkdf-tests PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(libk-crypto-hkdf-tests PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
libk_crypto_add_test(libk-crypto-symmetric-tests 120
    tests/test-symmetric-descriptors.cpp tests/test-symmetric-cipher-streams.cpp)
target_sources(libk-crypto-symmetric-tests PRIVATE tests/test-crypto-capabilities.cpp)
target_include_directories(libk-crypto-symmetric-tests PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(libk-crypto-symmetric-tests PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
add_executable(libk-crypto-ccm-engine-tests tests/test-ccm-engine.cpp src/ccm_engine.c)
target_include_directories(libk-crypto-ccm-engine-tests PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(libk-crypto-ccm-engine-tests PRIVATE Catch2::Catch2WithMain ${OPENSSL_CRYPTO_LIBRARY})
add_test(NAME libk-crypto-ccm-engine-tests COMMAND libk-crypto-ccm-engine-tests)
klang_scale_ctest_timeout(_ccm_test_timeout 120)
set_tests_properties(libk-crypto-ccm-engine-tests PROPERTIES TIMEOUT "${_ccm_test_timeout}")