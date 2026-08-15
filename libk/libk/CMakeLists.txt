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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/exception.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/memory.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shared.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/expected.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/optional.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/collections.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/list.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vector.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/set.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/map.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/string.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/array_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/filter_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/transform_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/buffered_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/data_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/print_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/file.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/stdio.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/math/math.k
    # ── Phase 1: time, threading ──────────────────────────────────────────
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/thread_exceptions.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/thread.k
    # ── Phase 2: futures ──────────────────────────────────────────────────
    ${CMAKE_CURRENT_SOURCE_DIR}/src/future.k
    # ── Phase 3: synchronisation ──────────────────────────────────────────
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sync/mutex.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sync/semaphore.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sync/latch.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sync/rwlock.k
    # ── Phase 4: asynchronous I/O ─────────────────────────────────────────
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/io_exceptions.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/byte_buffer.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/channel.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/path.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/file_channel.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/file_stream.k
    # ── Phase 5: asynchronous network I/O ─────────────────────────────────
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/network_address.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/socket.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/selector.k
    # ── Phase 6: event loop / reactor foundation ──────────────────────────
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/event_loop.k
    # ── Phase 7: functional programming ───────────────────────────────────
    ${CMAKE_CURRENT_SOURCE_DIR}/src/functional.k
    # ── Application entry-point infrastructure ────────────────────────────
    ${CMAKE_CURRENT_SOURCE_DIR}/src/application.k
)
set(LIBK_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(LIBK_SO  ${LIBK_OUTPUT_DIR}/libk.so)
set(LIBK_A   ${LIBK_OUTPUT_DIR}/libk.a)
set(LIBK_KDI ${LIBK_OUTPUT_DIR}/libk.kdi)
set(LIBK_KDI_LINK ${LIBK_OUTPUT_DIR}/k.kdi)
set(LIBK_MD_DOC_ROOT ${LIBK_OUTPUT_DIR}/doc/md)
set(LIBK_DOC_STAMP ${LIBK_MD_DOC_ROOT}/.docgen.stamp)
set(LIBK_HTML_DOC_ROOT ${LIBK_OUTPUT_DIR}/doc/html)
set(LIBK_HTML_DOC_STAMP ${LIBK_HTML_DOC_ROOT}/.docgen-html.stamp)

# ── Compile the C runtime helpers (fatal error functions) ────────────────
# These are compiled to a .o and passed to klangc so they end up in both
# libk.so and libk.a.  Every K module resolves the fatal symbols from libk
# instead of duplicating them.
set(FATAL_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/fatal.c)
set(FATAL_OBJ ${LIBK_OUTPUT_DIR}/fatal.o)

# -fexceptions: K exceptions may unwind through these C frames (in particular
# the thread trampoline, which calls back into K to run a Runnable). Without
# unwind information the unwinder cannot find handlers located below them.
set(FATAL_C_FLAGS -c -fPIC -fexceptions -fasynchronous-unwind-tables)
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

# ── Compile the exception chaining C++ runtime helpers ────────────────────
set(EXC_CHAIN_CPP ${CMAKE_CURRENT_SOURCE_DIR}/src/exception_chaining.cpp)
set(EXC_CHAIN_OBJ ${LIBK_OUTPUT_DIR}/exception_chaining.o)

add_custom_command(
    OUTPUT  ${EXC_CHAIN_OBJ}
    COMMAND ${CMAKE_CXX_COMPILER} -std=c++20 -c -fPIC
            -o ${EXC_CHAIN_OBJ} ${EXC_CHAIN_CPP}
    DEPENDS ${EXC_CHAIN_CPP}
    COMMENT "Compiling K runtime exception chaining helpers (exception_chaining.cpp)"
    VERBATIM
)

# ── Compile time helpers (Phase 1) ───────────────────────────────────────
set(TIME_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/time.c)
set(TIME_OBJ ${LIBK_OUTPUT_DIR}/time.o)

add_custom_command(
    OUTPUT  ${TIME_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -o ${TIME_OBJ} ${TIME_C}
    DEPENDS ${TIME_C}
    COMMENT "Compiling K runtime time helpers (time.c)"
    VERBATIM
)

# ── Compile thread runtime (Phase 1) ─────────────────────────────────────
set(RUNTIME_THREAD_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/runtime_thread.c)
set(RUNTIME_THREAD_OBJ ${LIBK_OUTPUT_DIR}/runtime_thread.o)

add_custom_command(
    OUTPUT  ${RUNTIME_THREAD_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${RUNTIME_THREAD_OBJ} ${RUNTIME_THREAD_C}
    DEPENDS ${RUNTIME_THREAD_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/runtime_thread.h
    COMMENT "Compiling K runtime thread substrate (runtime_thread.c)"
    VERBATIM
)

set(THREAD_FFI_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/thread_ffi.c)
set(THREAD_FFI_OBJ ${LIBK_OUTPUT_DIR}/thread_ffi.o)

add_custom_command(
    OUTPUT  ${THREAD_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${THREAD_FFI_OBJ} ${THREAD_FFI_C}
    DEPENDS ${THREAD_FFI_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/runtime_thread.h
    COMMENT "Compiling K runtime thread FFI wrappers (thread_ffi.c)"
    VERBATIM
)

# ── Compile future runtime (Phase 2) ─────────────────────────────────────
set(FUTURE_STATE_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/future_state.c)
set(FUTURE_STATE_OBJ ${LIBK_OUTPUT_DIR}/future_state.o)

add_custom_command(
    OUTPUT  ${FUTURE_STATE_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${FUTURE_STATE_OBJ} ${FUTURE_STATE_C}
    DEPENDS ${FUTURE_STATE_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/future_state.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/runtime_thread.h
    COMMENT "Compiling K runtime future substrate (future_state.c)"
    VERBATIM
)

set(FUTURE_FFI_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/future_ffi.c)
set(FUTURE_FFI_OBJ ${LIBK_OUTPUT_DIR}/future_ffi.o)

add_custom_command(
    OUTPUT  ${FUTURE_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${FUTURE_FFI_OBJ} ${FUTURE_FFI_C}
    DEPENDS ${FUTURE_FFI_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/future_state.h
    COMMENT "Compiling K runtime future FFI wrappers (future_ffi.c)"
    VERBATIM
)

# ── Compile synchronisation runtime (Phase 3) ────────────────────────────
set(SYNC_PRIMITIVES_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/sync_primitives.c)
set(SYNC_PRIMITIVES_OBJ ${LIBK_OUTPUT_DIR}/sync_primitives.o)

add_custom_command(
    OUTPUT  ${SYNC_PRIMITIVES_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${SYNC_PRIMITIVES_OBJ} ${SYNC_PRIMITIVES_C}
    DEPENDS ${SYNC_PRIMITIVES_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/sync_primitives.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/runtime_thread.h
    COMMENT "Compiling K runtime synchronisation substrate (sync_primitives.c)"
    VERBATIM
)

set(SYNC_FFI_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/sync_ffi.c)
set(SYNC_FFI_OBJ ${LIBK_OUTPUT_DIR}/sync_ffi.o)

add_custom_command(
    OUTPUT  ${SYNC_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${SYNC_FFI_OBJ} ${SYNC_FFI_C}
    DEPENDS ${SYNC_FFI_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/sync_primitives.h
    COMMENT "Compiling K runtime synchronisation FFI wrappers (sync_ffi.c)"
    VERBATIM
)

# ── Compile asynchronous I/O runtime (Phase 4) ───────────────────────────
if(KLANG_HAVE_LIBURING)
    set(ASYNC_C_FLAGS -DK_ASYNC_HAVE_URING=1)
    foreach(_dir ${LIBURING_INCLUDE_DIRS})
        list(APPEND ASYNC_C_FLAGS -I${_dir})
    endforeach()
    # Forwarded to klangc so that libk.so records a DT_NEEDED on liburing.
    set(LIBK_EXTRA_LINK_ARGS)
    foreach(_dir ${LIBURING_LIBRARY_DIRS})
        list(APPEND LIBK_EXTRA_LINK_ARGS -L ${_dir})
    endforeach()
    list(APPEND LIBK_EXTRA_LINK_ARGS -l uring)
else()
    set(ASYNC_C_FLAGS -DK_ASYNC_HAVE_URING=0)
    set(LIBK_EXTRA_LINK_ARGS)
endif()

set(PARK_LOT_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/park_lot.c)
set(PARK_LOT_OBJ ${LIBK_OUTPUT_DIR}/park_lot.o)

add_custom_command(
    OUTPUT  ${PARK_LOT_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${PARK_LOT_OBJ} ${PARK_LOT_C}
    DEPENDS ${PARK_LOT_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/park_lot.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/runtime_thread.h
    COMMENT "Compiling K runtime park lot (park_lot.c)"
    VERBATIM
)

set(ASYNC_IO_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/async_io.c)
set(ASYNC_IO_OBJ ${LIBK_OUTPUT_DIR}/async_io.o)

add_custom_command(
    OUTPUT  ${ASYNC_IO_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS} ${ASYNC_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${ASYNC_IO_OBJ} ${ASYNC_IO_C}
    DEPENDS ${ASYNC_IO_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/async_io.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/park_lot.h
    COMMENT "Compiling K runtime asynchronous I/O substrate (async_io.c)"
    VERBATIM
)

set(ASYNC_FFI_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/async_ffi.c)
set(ASYNC_FFI_OBJ ${LIBK_OUTPUT_DIR}/async_ffi.o)

add_custom_command(
    OUTPUT  ${ASYNC_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS} ${ASYNC_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${ASYNC_FFI_OBJ} ${ASYNC_FFI_C}
    DEPENDS ${ASYNC_FFI_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/async_io.h
    COMMENT "Compiling K runtime asynchronous I/O FFI wrappers (async_ffi.c)"
    VERBATIM
)

set(NETWORK_FFI_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/network_ffi.c)
set(NETWORK_FFI_OBJ ${LIBK_OUTPUT_DIR}/network_ffi.o)

add_custom_command(
    OUTPUT  ${NETWORK_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${NETWORK_FFI_OBJ} ${NETWORK_FFI_C}
    DEPENDS ${NETWORK_FFI_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/runtime_thread.h
    COMMENT "Compiling K runtime network I/O FFI wrappers (network_ffi.c)"
    VERBATIM
)

# ── Compile Application entry-point FFI helpers ───────────────────────────
set(APP_FFI_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/application_ffi.c)
set(APP_FFI_OBJ ${LIBK_OUTPUT_DIR}/application_ffi.o)

add_custom_command(
    OUTPUT  ${APP_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -o ${APP_FFI_OBJ} ${APP_FFI_C}
    DEPENDS ${APP_FFI_C}
    COMMENT "Compiling K Application entry-point FFI helpers (application_ffi.c)"
    VERBATIM
)

set(C_RUNTIME_OBJS ${FATAL_OBJ} ${RTTI_OBJ} ${IO_HELPERS_OBJ} ${FILE_IO_OBJ} ${EXC_CHAIN_OBJ}
                   ${TIME_OBJ} ${RUNTIME_THREAD_OBJ} ${THREAD_FFI_OBJ}
                   ${FUTURE_STATE_OBJ} ${FUTURE_FFI_OBJ}
                   ${SYNC_PRIMITIVES_OBJ} ${SYNC_FFI_OBJ}
                   ${PARK_LOT_OBJ} ${ASYNC_IO_OBJ} ${ASYNC_FFI_OBJ}
                   ${NETWORK_FFI_OBJ}
                   ${APP_FFI_OBJ})

# ── Compile the K standard library using klangc ──────────────────────────────
add_custom_command(
    OUTPUT  ${LIBK_SO} ${LIBK_A} ${LIBK_KDI}
    COMMAND $<TARGET_FILE:klangc>
            --dyn-lib --static-lib
            --module-name k
            ${LIBK_EXTRA_LINK_ARGS}
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

# ── Generate Markdown documentation for libk from the produced KDI ───────────
# Output tree root: ${LIBK_MD_DOC_ROOT}/k/
add_custom_command(
    OUTPUT  ${LIBK_DOC_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBK_MD_DOC_ROOT}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${LIBK_MD_DOC_ROOT}/k
    COMMAND $<TARGET_FILE:kdi-tool> docgen --md ${LIBK_KDI} ${LIBK_MD_DOC_ROOT}
    COMMAND ${CMAKE_COMMAND} -E touch ${LIBK_DOC_STAMP}
    DEPENDS kdi-tool ${LIBK_KDI}
    WORKING_DIRECTORY ${LIBK_OUTPUT_DIR}
    COMMENT "Generating libk Markdown documentation from libk.kdi"
    VERBATIM
)

# ── Generate static HTML documentation for libk from the produced KDI ────────
# Output tree root: ${LIBK_HTML_DOC_ROOT}/k/
add_custom_command(
    OUTPUT  ${LIBK_HTML_DOC_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBK_HTML_DOC_ROOT}
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${LIBK_HTML_DOC_ROOT}/k
    COMMAND $<TARGET_FILE:kdi-tool> docgen --html ${LIBK_KDI} ${LIBK_HTML_DOC_ROOT}
    COMMAND ${CMAKE_COMMAND} -E touch ${LIBK_HTML_DOC_STAMP}
    DEPENDS kdi-tool ${LIBK_KDI}
    WORKING_DIRECTORY ${LIBK_OUTPUT_DIR}
    COMMENT "Generating libk static HTML documentation from libk.kdi"
    VERBATIM
)

add_custom_target(libk_stdlib_doc ALL
    DEPENDS ${LIBK_DOC_STAMP} ${LIBK_HTML_DOC_STAMP}
)
add_dependencies(libk_stdlib_doc libk_stdlib)

# ── Export paths so other targets (compiler, tests) can find the stdlib ───────
set(KLANG_STDLIB_KDI_DIR "${LIBK_OUTPUT_DIR}" CACHE INTERNAL
    "Directory containing k.kdi (K base stdlib descriptor)")
set(KLANG_STDLIB_LIB_DIR "${LIBK_OUTPUT_DIR}" CACHE INTERNAL
    "Directory containing libk.so / libk.a (K base stdlib binaries)")

# ── libk functional tests (core) ─────────────────────────────────────────────
# Core stdlib semantics (exceptions, RTTI, strings, value types, math, shared).
# Pure single-threaded tests: no spawned threads, no network, no async I/O.
# ─────────────────────────────────────────────────────────────────────────────
add_executable(libk-tests-core
    tests/test-exceptions.cpp
    tests/test-rtti.cpp
    tests/test-rtti-constructors.cpp
    tests/test-rtti-parameters.cpp
    tests/test-expected.cpp
    tests/test-string.cpp
    tests/test-string-literal.cpp
    tests/test-string-search.cpp
    tests/test-string-builder-mutation.cpp
    tests/test-math.cpp
    tests/test-optional.cpp
    tests/test-shared.cpp
    tests/test-application.cpp
)
target_link_libraries(libk-tests-core klang-test-helpers Catch2::Catch2WithMain dl)
add_dependencies(libk-tests-core libk_stdlib)
target_compile_definitions(libk-tests-core PRIVATE
    LIBK_KDI_DIR="${LIBK_OUTPUT_DIR}"
    LIBK_LIB_DIR="${LIBK_OUTPUT_DIR}"
)
add_test(NAME libk-tests-core COMMAND libk-tests-core)
set_tests_properties(libk-tests-core PROPERTIES TIMEOUT 180)

# ── libk functional tests (collections + stream I/O) ─────────────────────────
# Collection containers and synchronous stream/file/path I/O APIs.
# Split from core to keep each CTest target below timeout budget.
# ─────────────────────────────────────────────────────────────────────────────
add_executable(libk-tests-collections-io
    tests/test-io-byte-array-streams.cpp
    tests/test-io-filter-streams.cpp
    tests/test-io-transform-streams.cpp
    tests/test-io-buffered-streams.cpp
    tests/test-io-data-streams.cpp
    tests/test-io-print-stream.cpp
    tests/test-io-stdio.cpp
    tests/test-list.cpp
    tests/test-vector.cpp
    tests/test-set.cpp
    tests/test-map.cpp
    tests/test-io-byte-buffer.cpp
    tests/test-io-path.cpp
)
target_link_libraries(libk-tests-collections-io klang-test-helpers Catch2::Catch2WithMain dl)
add_dependencies(libk-tests-collections-io libk_stdlib)
target_compile_definitions(libk-tests-collections-io PRIVATE
    LIBK_KDI_DIR="${LIBK_OUTPUT_DIR}"
    LIBK_LIB_DIR="${LIBK_OUTPUT_DIR}"
)
add_test(NAME libk-tests-collections-io COMMAND libk-tests-collections-io)
set_tests_properties(libk-tests-collections-io PROPERTIES TIMEOUT 180)

# ── libk threading and I/O tests ──────────────────────────────────────────────
# Functional correctness tests that involve real threads, futures, sync
# primitives, file I/O, and network sockets.  Heavier than libk-tests but
# still required for full functional coverage; registered unconditionally.
# ─────────────────────────────────────────────────────────────────────────────
add_executable(libk-thread-io-tests
    # ── Time, threads, futures ────────────────────────────────────────────
    tests/test-thread-basic.cpp
    tests/test-future.cpp
    # ── Synchronisation ───────────────────────────────────────────────────
    tests/test-sync-mutex.cpp
    tests/test-sync-condition.cpp
    tests/test-sync-semaphore.cpp
    tests/test-sync-latch.cpp
    tests/test-sync-rwlock.cpp
    # ── Async file I/O ────────────────────────────────────────────────────
    tests/test-io-file.cpp
    tests/test-io-file-channel.cpp
    tests/test-io-file-stream.cpp
    # ── Network and socket I/O ────────────────────────────────────────────
    tests/test-io-socket.cpp
    tests/test-io-server-socket.cpp
    tests/test-io-socket-interrupt.cpp
    tests/test-io-datagram.cpp
    tests/test-io-unix-socket-native.cpp
    tests/test-io-event-loop.cpp
    tests/test-io-selector.cpp
)
target_link_libraries(libk-thread-io-tests klang-test-helpers Catch2::Catch2WithMain dl)
add_dependencies(libk-thread-io-tests libk_stdlib)
target_compile_definitions(libk-thread-io-tests PRIVATE
    LIBK_KDI_DIR="${LIBK_OUTPUT_DIR}"
    LIBK_LIB_DIR="${LIBK_OUTPUT_DIR}"
)
add_test(NAME libk-thread-io-tests COMMAND libk-thread-io-tests)
set_tests_properties(libk-thread-io-tests PROPERTIES TIMEOUT 120)

# ── libk performance / stress tests ──────────────────────────────────────────
# Extremely resource-intensive (thread storms, I/O saturation, cancellation
# floods).  Excluded from the default CTest run to avoid overwhelming low-spec
# hosts.  Enable with -DLIBK_PERF_TESTS=ON, then run with:
#   ctest -R libk-perf-tests
# ─────────────────────────────────────────────────────────────────────────────
option(LIBK_PERF_TESTS "Build and register the libk performance/stress tests" OFF)

add_executable(libk-perf-tests
    tests/test-thread-stress.cpp
    tests/test-io-stress.cpp
    tests/test-cancellation-storm.cpp
    tests/test-runtime-shutdown.cpp
)
target_link_libraries(libk-perf-tests klang-test-helpers Catch2::Catch2WithMain dl)
add_dependencies(libk-perf-tests libk_stdlib)
target_compile_definitions(libk-perf-tests PRIVATE
    LIBK_KDI_DIR="${LIBK_OUTPUT_DIR}"
    LIBK_LIB_DIR="${LIBK_OUTPUT_DIR}"
)

if(LIBK_PERF_TESTS)
    add_test(NAME libk-perf-tests COMMAND libk-perf-tests)
endif()
