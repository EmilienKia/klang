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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffi_symbol.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffi_runtime.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/annotations.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/object.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/rtti.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/exception.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/big_integer.k
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/text_exceptions.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/text_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/encoded_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/codecs.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/codec_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/base_n.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/base64.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/hex.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/base2.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/base32.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/percent_codec.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/uri_exceptions.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/query_params.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/uri.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/buffered_reader.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/text_scanner.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/array_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/filter_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/transform_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/buffered_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/data_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/text_printer.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/print_stream.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/stdio.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/math/math.k
    # ── Phase 1: time, threading ──────────────────────────────────────────
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/errors.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/arithmetic.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/gregorian.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/duration.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/instant.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/period.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/local_time.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/local_date.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/local_date_time.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/zone_offset.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/iso_format.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/iso_parse.k
    # ── Phase 2: clock domains & native clock adapters ────────────────────
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/posix_timestamp.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/clock.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/monotonic.k
    # ── Phase 3: zone data core ───────────────────────────────────────────
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/zone_id.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/leap_seconds.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/tzdb.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/local_resolution.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/zone_rules.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/time_zone.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/zoned_date_time.k
    # ── Phase 4: chronologies, locale & formatting ────────────────────────
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/chronology.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/calendar_date.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/locale.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/temporal_formatter.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/temporal_parser.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/time/time_scale.k
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
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/io_result.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/execution_context.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/io_operation.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/byte_buffer.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/mapped_byte_buffer.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/channel.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/file/entries.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/file/file_system.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/file/path.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/file/file.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/file/file_channel.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/file/local_fs.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/file/directory_fs.k
    # ── Phase 5: asynchronous network I/O ─────────────────────────────────
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/network_address.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/socket_channel.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/server_socket.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/datagram_socket.k
    # ── Phase 6: serial executor / monotonic timers ───────────────────────
    ${CMAKE_CURRENT_SOURCE_DIR}/src/io/event_loop.k
    # ── Phase 7: functional programming ───────────────────────────────────
    ${CMAKE_CURRENT_SOURCE_DIR}/src/functional.k
    # ── Phase 8: executors and thread pools ───────────────────────────────
    ${CMAKE_CURRENT_SOURCE_DIR}/src/executor.k
    # ── Application entry-point infrastructure ────────────────────────────
    ${CMAKE_CURRENT_SOURCE_DIR}/src/module.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/application.k
    # ── Cryptography and identifiers ──────────────────────────────────────
    ${CMAKE_CURRENT_SOURCE_DIR}/src/crypto/md5.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/crypto/sha1.k
    ${CMAKE_CURRENT_SOURCE_DIR}/src/uuid.k
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

# Colocated metadata remains valid when the runtime artifacts are moved together.
set(LIBK_PC_ICU_REQUIRES "")
if(KLANG_HAVE_ICU)
    set(LIBK_PC_ICU_REQUIRES "icu-uc icu-i18n")
endif()
set(LIBK_PC_LIBS_PRIVATE "-pthread")
foreach(_lib IN LISTS CMAKE_DL_LIBS)
    string(APPEND LIBK_PC_LIBS_PRIVATE " -l${_lib}")
endforeach()
string(APPEND LIBK_PC_LIBS_PRIVATE " -lm")
configure_file(libk.pc.in "${LIBK_OUTPUT_DIR}/libk.pc" @ONLY)

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
    DEPENDS ${FATAL_C} ${CMAKE_CURRENT_SOURCE_DIR}/src/fatal_abi.h
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

set(SYMBOL_MANGLING_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/symbol_mangling.c)
set(SYMBOL_MANGLING_OBJ ${LIBK_OUTPUT_DIR}/symbol_mangling.o)

add_custom_command(
    OUTPUT  ${SYMBOL_MANGLING_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -o ${SYMBOL_MANGLING_OBJ} ${SYMBOL_MANGLING_C}
    DEPENDS ${SYMBOL_MANGLING_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/symbol_mangling.h
    COMMENT "Compiling K runtime symbol mangling helpers (symbol_mangling.c)"
    VERBATIM
)

set(SHARED_FFI_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/shared_ffi.c)
set(SHARED_FFI_OBJ ${LIBK_OUTPUT_DIR}/shared_ffi.o)

add_custom_command(
    OUTPUT  ${SHARED_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -o ${SHARED_FFI_OBJ} ${SHARED_FFI_C}
    DEPENDS ${SHARED_FFI_C}
    COMMENT "Compiling K atomic shared ownership counters (shared_ffi.c)"
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
set(FILE_IO_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/io/file/file.c)
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
set(RUNTIME_TLS_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/runtime_tls.c)
set(RUNTIME_TLS_OBJ ${LIBK_OUTPUT_DIR}/runtime_tls.o)

add_custom_command(
    OUTPUT  ${RUNTIME_TLS_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${RUNTIME_TLS_OBJ} ${RUNTIME_TLS_C}
    DEPENDS ${RUNTIME_TLS_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/runtime_tls.h
    COMMENT "Compiling K runtime TLS substrate (runtime_tls.c)"
    VERBATIM
)

set(RUNTIME_THREAD_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/runtime_thread.c)
set(RUNTIME_THREAD_OBJ ${LIBK_OUTPUT_DIR}/runtime_thread.o)

add_custom_command(
    OUTPUT  ${RUNTIME_THREAD_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${RUNTIME_THREAD_OBJ} ${RUNTIME_THREAD_C}
    DEPENDS ${RUNTIME_THREAD_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/runtime_thread.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/runtime_tls.h
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
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/park_lot.h
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
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/park_lot.h
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
    set(ASYNC_C_FLAGS)
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
    message(FATAL_ERROR "The proactor runtime requires Linux and liburing >= 2.5")
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

# ── Compile proactor io_uring substrate (Lot C & Lot E) ──────────────────
set(IO_URING_BACKEND_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_uring_backend.c)
set(IO_URING_BACKEND_OBJ ${LIBK_OUTPUT_DIR}/io_uring_backend.o)

add_custom_command(
    OUTPUT  ${IO_URING_BACKEND_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS} ${ASYNC_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${IO_URING_BACKEND_OBJ} ${IO_URING_BACKEND_C}
    DEPENDS ${IO_URING_BACKEND_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_backend.h
    COMMENT "Compiling K runtime io_uring backend (io_uring_backend.c)"
    VERBATIM
)

set(IO_OPERATION_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_operation.c)
set(IO_OPERATION_OBJ ${LIBK_OUTPUT_DIR}/io_operation.o)

add_custom_command(
    OUTPUT  ${IO_OPERATION_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS} ${ASYNC_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${IO_OPERATION_OBJ} ${IO_OPERATION_C}
    DEPENDS ${IO_OPERATION_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_operation.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_datagram_ffi.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/callable_abi.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_backend.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_runtime.h
    COMMENT "Compiling K runtime io_operation (io_operation.c)"
    VERBATIM
)

set(IO_RESOLVER_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_resolver.c)
set(IO_RESOLVER_OBJ ${LIBK_OUTPUT_DIR}/io_resolver.o)

add_custom_command(
    OUTPUT  ${IO_RESOLVER_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${IO_RESOLVER_OBJ} ${IO_RESOLVER_C}
    DEPENDS ${IO_RESOLVER_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_resolver.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_operation.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_runtime.h
    COMMENT "Compiling K runtime io_resolver (io_resolver.c)"
    VERBATIM
)

set(IO_RUNTIME_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_runtime.c)
set(IO_RUNTIME_OBJ ${LIBK_OUTPUT_DIR}/io_runtime.o)

add_custom_command(
    OUTPUT  ${IO_RUNTIME_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS} ${ASYNC_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${IO_RUNTIME_OBJ} ${IO_RUNTIME_C}
    DEPENDS ${IO_RUNTIME_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_runtime.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_datagram_ffi.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_backend.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_operation.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_resolver.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/runtime_thread.h
    COMMENT "Compiling K runtime io_runtime (io_runtime.c)"
    VERBATIM
)

set(IO_FFI_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_ffi.c)
set(IO_FFI_OBJ ${LIBK_OUTPUT_DIR}/io_ffi.o)

add_custom_command(
    OUTPUT  ${IO_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS} ${ASYNC_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${IO_FFI_OBJ} ${IO_FFI_C}
    DEPENDS ${IO_FFI_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_file_ffi.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_stream_ffi.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_datagram_ffi.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_runtime.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_operation.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_resolver.h
    COMMENT "Compiling K runtime io_ffi wrappers (io_ffi.c)"
    VERBATIM
)

set(IO_BUFFER_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_buffer.c)
set(IO_BUFFER_OBJ ${LIBK_OUTPUT_DIR}/io_buffer.o)

add_custom_command(
    OUTPUT  ${IO_BUFFER_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS} ${ASYNC_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${IO_BUFFER_OBJ} ${IO_BUFFER_C}
    DEPENDS ${IO_BUFFER_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_buffer.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_runtime.h
    COMMENT "Compiling K runtime native I/O buffer (io_buffer.c)"
    VERBATIM
)

set(EXECUTOR_RUNTIME_C ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/executor_runtime.c)
set(EXECUTOR_RUNTIME_OBJ ${LIBK_OUTPUT_DIR}/executor_runtime.o)
file(GLOB LIBK_PROACTOR_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/executor_runtime.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/callable_abi.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/future_state.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/park_lot.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/sync_primitives.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/runtime_thread.h
)
add_custom_command(
    OUTPUT ${EXECUTOR_RUNTIME_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime
            -o ${EXECUTOR_RUNTIME_OBJ} ${EXECUTOR_RUNTIME_C}
    DEPENDS ${EXECUTOR_RUNTIME_C} ${LIBK_PROACTOR_HEADERS}
    COMMENT "Compiling strong executor endpoints and EventLoop timers"
    VERBATIM
)
# Header changes must rebuild every C object that shares these state layouts.
foreach(_obj ${IO_URING_BACKEND_OBJ} ${IO_OPERATION_OBJ} ${IO_RESOLVER_OBJ}
             ${IO_RUNTIME_OBJ} ${IO_FFI_OBJ} ${IO_BUFFER_OBJ})
    add_custom_command(OUTPUT ${_obj} APPEND DEPENDS ${LIBK_PROACTOR_HEADERS})
endforeach()

# ── Compile Application entry-point FFI helpers ───────────────────────────
set(APP_FFI_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/application_ffi.c)
set(APP_FFI_OBJ ${LIBK_OUTPUT_DIR}/application_ffi.o)

add_custom_command(
    OUTPUT  ${APP_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -o ${APP_FFI_OBJ} ${APP_FFI_C}
    DEPENDS ${APP_FFI_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_runtime.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/io_operation.h
    COMMENT "Compiling K Application entry-point FFI helpers (application_ffi.c)"
    VERBATIM
)

# ── Compile dynamic module loading FFI helpers ────────────────────────────
set(MODULE_FFI_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/module_ffi.c)
set(MODULE_FFI_OBJ ${LIBK_OUTPUT_DIR}/module_ffi.o)

add_custom_command(
    OUTPUT  ${MODULE_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -o ${MODULE_FFI_OBJ} ${MODULE_FFI_C}
    DEPENDS ${MODULE_FFI_C}
    COMMENT "Compiling K dynamic module loading FFI helpers (module_ffi.c)"
    VERBATIM
)

# ── Compile fixed-signature native C calls ─────────────────────────────────
set(RUNTIME_FFI_C ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/runtime_ffi.c)
set(RUNTIME_FFI_OBJ ${LIBK_OUTPUT_DIR}/runtime_ffi.o)
set(FFI_FFI_C ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/ffi_ffi.c)
set(FFI_FFI_OBJ ${LIBK_OUTPUT_DIR}/ffi_ffi.o)
set(RUNTIME_FFI_H ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/runtime_ffi.h)

add_custom_command(
    OUTPUT ${RUNTIME_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS} -std=c11 ${LIBFFI_CFLAGS}
            -o ${RUNTIME_FFI_OBJ} ${RUNTIME_FFI_C}
    DEPENDS ${RUNTIME_FFI_C} ${RUNTIME_FFI_H}
    COMMENT "Compiling K fixed-signature native call engine (runtime_ffi.c)"
    VERBATIM
)
add_custom_command(
    OUTPUT ${FFI_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS} -std=c11 ${LIBFFI_CFLAGS}
            -o ${FFI_FFI_OBJ} ${FFI_FFI_C}
    DEPENDS ${FFI_FFI_C} ${RUNTIME_FFI_H}
    COMMENT "Compiling K primitive native call bridges (ffi_ffi.c)"
    VERBATIM
)
foreach(_dir ${LIBFFI_LIBRARY_DIRS})
    list(APPEND LIBK_EXTRA_LINK_ARGS -L ${_dir})
endforeach()
foreach(_lib ${LIBFFI_LIBRARIES})
    list(APPEND LIBK_EXTRA_LINK_ARGS -l ${_lib})
endforeach()
# The archive includes these runtime objects, not libffi itself. Static
# consumers must also link the libffi dependencies returned by pkg-config.

# ── Compile UUID FFI helpers ──────────────────────────────────────────────
set(UUID_FFI_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/uuid_ffi.c)
set(UUID_FFI_OBJ ${LIBK_OUTPUT_DIR}/uuid_ffi.o)

add_custom_command(
    OUTPUT  ${UUID_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -o ${UUID_FFI_OBJ} ${UUID_FFI_C}
    DEPENDS ${UUID_FFI_C}
    COMMENT "Compiling K UUID FFI helpers (uuid_ffi.c)"
    VERBATIM
)

# ── Compile platform clock runtime (Phase 2) ──────────────────────────────
set(PLATFORM_CLOCK_POSIX_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time/platform_clock_posix.c)
set(PLATFORM_CLOCK_POSIX_OBJ ${LIBK_OUTPUT_DIR}/platform_clock_posix.o)

add_custom_command(
    OUTPUT  ${PLATFORM_CLOCK_POSIX_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time
            -o ${PLATFORM_CLOCK_POSIX_OBJ} ${PLATFORM_CLOCK_POSIX_C}
    DEPENDS ${PLATFORM_CLOCK_POSIX_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time/platform_clock.h
    COMMENT "Compiling K runtime platform clock (platform_clock_posix.c)"
    VERBATIM
)

# ── Compile leap second runtime (Phase 3) ──────────────────────────────
set(LEAP_SECOND_DATA_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time/leap_second_data.c)
set(LEAP_SECOND_DATA_OBJ ${LIBK_OUTPUT_DIR}/leap_second_data.o)

add_custom_command(
    OUTPUT  ${LEAP_SECOND_DATA_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time
            -o ${LEAP_SECOND_DATA_OBJ} ${LEAP_SECOND_DATA_C}
    DEPENDS ${LEAP_SECOND_DATA_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time/leap_second_data.h
    COMMENT "Compiling K runtime leap second data (leap_second_data.c)"
    VERBATIM
)

# ── Compile TZif reader runtime (Phase 3) ────────────────────────────────
set(TZIF_READER_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time/tzif_reader.c)
set(TZIF_READER_OBJ ${LIBK_OUTPUT_DIR}/tzif_reader.o)

add_custom_command(
    OUTPUT  ${TZIF_READER_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time
            -o ${TZIF_READER_OBJ} ${TZIF_READER_C}
    DEPENDS ${TZIF_READER_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time/tzif_reader.h
    COMMENT "Compiling K runtime TZif reader (tzif_reader.c)"
    VERBATIM
)

# ── Compile system TZDB provider runtime (Phase 3) ───────────────────────
set(TZDB_SYSTEM_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time/tzdb_system.c)
set(TZDB_SYSTEM_OBJ ${LIBK_OUTPUT_DIR}/tzdb_system.o)

add_custom_command(
    OUTPUT  ${TZDB_SYSTEM_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time
            -o ${TZDB_SYSTEM_OBJ} ${TZDB_SYSTEM_C}
    DEPENDS ${TZDB_SYSTEM_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time/tzdb_system.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time/tzif_reader.h
    COMMENT "Compiling K runtime system TZDB provider (tzdb_system.c)"
    VERBATIM
)

set(TIME_FFI_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time/time_ffi.c)
set(TIME_FFI_OBJ ${LIBK_OUTPUT_DIR}/time_ffi.o)

add_custom_command(
    OUTPUT  ${TIME_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time
            -o ${TIME_FFI_OBJ} ${TIME_FFI_C}
    DEPENDS ${TIME_FFI_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time/platform_clock.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time/leap_second_data.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time/tzif_reader.h
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time/tzdb_system.h
    COMMENT "Compiling K runtime time FFI wrappers (time_ffi.c)"
    VERBATIM
)

# ── Compile ICU bridge runtime (Phase 4) ──────────────────────────────────
if(KLANG_HAVE_ICU)
    set(ICU_C_FLAGS -DK_TIME_HAVE_ICU=1)
    foreach(_dir ${ICU_INCLUDE_DIRS})
        list(APPEND ICU_C_FLAGS -I${_dir})
    endforeach()
    foreach(_dir ${ICU_LIBRARY_DIRS})
        list(APPEND LIBK_EXTRA_LINK_ARGS -L ${_dir})
    endforeach()
    foreach(_lib ${ICU_LIBRARIES})
        list(APPEND LIBK_EXTRA_LINK_ARGS -l ${_lib})
    endforeach()
else()
    set(ICU_C_FLAGS -DK_TIME_HAVE_ICU=0)
endif()

set(ICU_BRIDGE_C   ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time/icu_bridge.c)
set(ICU_BRIDGE_OBJ ${LIBK_OUTPUT_DIR}/icu_bridge.o)

add_custom_command(
    OUTPUT  ${ICU_BRIDGE_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS} ${ICU_C_FLAGS}
            -I${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time
            -o ${ICU_BRIDGE_OBJ} ${ICU_BRIDGE_C}
    DEPENDS ${ICU_BRIDGE_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/time/icu_bridge.h
    COMMENT "Compiling K runtime ICU bridge (icu_bridge.c)"
    VERBATIM
)

set(BIG_INTEGER_FFI_C ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/big_integer_ffi.c)
set(BIG_INTEGER_FFI_OBJ ${LIBK_OUTPUT_DIR}/big_integer_ffi.o)
set(BIG_INTEGER_C_FLAGS ${GMP_CFLAGS_OTHER})
foreach(_dir IN LISTS GMP_INCLUDE_DIRS)
    list(APPEND BIG_INTEGER_C_FLAGS -I${_dir})
endforeach()
foreach(_dir IN LISTS GMP_LIBRARY_DIRS)
    list(APPEND LIBK_EXTRA_LINK_ARGS -L ${_dir})
endforeach()
foreach(_lib IN LISTS GMP_LIBRARIES)
    list(APPEND LIBK_EXTRA_LINK_ARGS -l ${_lib})
endforeach()
add_custom_command(
    OUTPUT ${BIG_INTEGER_FFI_OBJ}
    COMMAND ${CMAKE_C_COMPILER} ${FATAL_C_FLAGS} ${BIG_INTEGER_C_FLAGS}
            -o ${BIG_INTEGER_FFI_OBJ} ${BIG_INTEGER_FFI_C}
    DEPENDS ${BIG_INTEGER_FFI_C}
            ${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/big_integer_ffi.h
    COMMENT "Compiling K GMP BigInteger bridge (big_integer_ffi.c)"
    VERBATIM
)

set(C_RUNTIME_OBJS ${FATAL_OBJ} ${RTTI_OBJ} ${SYMBOL_MANGLING_OBJ} ${IO_HELPERS_OBJ} ${FILE_IO_OBJ} ${EXC_CHAIN_OBJ}
                   ${SHARED_FFI_OBJ}
                   ${BIG_INTEGER_FFI_OBJ}
                   ${TIME_OBJ} ${RUNTIME_TLS_OBJ} ${RUNTIME_THREAD_OBJ} ${THREAD_FFI_OBJ}
                   ${FUTURE_STATE_OBJ} ${FUTURE_FFI_OBJ}
                   ${SYNC_PRIMITIVES_OBJ} ${SYNC_FFI_OBJ}
                   ${PARK_LOT_OBJ}
                   ${IO_URING_BACKEND_OBJ} ${IO_OPERATION_OBJ} ${IO_RUNTIME_OBJ}
                   ${IO_RESOLVER_OBJ} ${IO_FFI_OBJ}
                   ${IO_BUFFER_OBJ}
                   ${EXECUTOR_RUNTIME_OBJ}
                   ${APP_FFI_OBJ}
                   ${MODULE_FFI_OBJ}
                   ${RUNTIME_FFI_OBJ} ${FFI_FFI_OBJ}
                   ${UUID_FFI_OBJ}
                   ${PLATFORM_CLOCK_POSIX_OBJ}
                   ${LEAP_SECOND_DATA_OBJ}
                   ${TZIF_READER_OBJ}
                   ${TZDB_SYSTEM_OBJ}
                   ${TIME_FFI_OBJ}
                   ${ICU_BRIDGE_OBJ})

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

# ── libk test helper macro ───────────────────────────────────────────────────
# Registers a libk test executable with standard link libraries, dependencies,
# compile definitions (KDI/lib output paths), and CTest properties.
# Usage: libk_add_test(<target> <timeout_s> <source1> [<source2> ...])
# ─────────────────────────────────────────────────────────────────────────────
macro(libk_add_test TARGET TIMEOUT)
    add_executable(${TARGET} ${ARGN})
    target_link_libraries(${TARGET} klang-test-helpers Catch2::Catch2WithMain dl)
    add_dependencies(${TARGET} libk_stdlib)
    target_compile_definitions(${TARGET} PRIVATE
        LIBK_KDI_DIR="${LIBK_OUTPUT_DIR}"
        LIBK_LIB_DIR="${LIBK_OUTPUT_DIR}"
        LIBK_TIME_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/time/fixtures"
    )
    add_test(NAME ${TARGET} COMMAND ${TARGET})
    klang_scale_ctest_timeout(_libk_test_timeout "${TIMEOUT}")
    set_tests_properties(${TARGET} PROPERTIES TIMEOUT "${_libk_test_timeout}")
endmacro()

# ── Native-only FFI ABI tests ──────────────────────────────────────────────
# Exercise the exact runtime objects without JIT or compiled K fixtures.
set_source_files_properties(${RUNTIME_FFI_OBJ} ${FFI_FFI_OBJ}
    PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
add_executable(libk-tests-ffi-native
    tests/test-ffi-native.cpp ${RUNTIME_FFI_OBJ} ${FFI_FFI_OBJ})
target_include_directories(libk-tests-ffi-native PRIVATE src/runtime)
target_link_libraries(libk-tests-ffi-native PRIVATE
    Catch2::Catch2WithMain PkgConfig::LIBFFI pthread dl)
add_test(NAME libk-tests-ffi-native COMMAND libk-tests-ffi-native)
klang_scale_ctest_timeout(_libk_test_timeout 120)
set_tests_properties(libk-tests-ffi-native PROPERTIES TIMEOUT "${_libk_test_timeout}")

set_source_files_properties(${BIG_INTEGER_FFI_OBJ}
    PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
add_executable(libk-tests-big-integer-native
    tests/test-big-integer-native.cpp ${BIG_INTEGER_FFI_OBJ})
target_include_directories(libk-tests-big-integer-native PRIVATE src/runtime)
target_link_libraries(libk-tests-big-integer-native PRIVATE
    Catch2::Catch2WithMain PkgConfig::GMP)
add_test(NAME libk-tests-big-integer-native COMMAND libk-tests-big-integer-native)
klang_scale_ctest_timeout(_libk_test_timeout 120)
set_tests_properties(libk-tests-big-integer-native PROPERTIES TIMEOUT "${_libk_test_timeout}")

# ── Public K runtime C FFI tests ───────────────────────────────────────────
add_library(klang-ffi-native-fixture SHARED tests/ffi/native_fixture.c)
set_target_properties(klang-ffi-native-fixture PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${LIBK_OUTPUT_DIR}/ffi-fixtures")
libk_add_test(libk-tests-ffi 120
    tests/test-ffi.cpp
)
target_link_options(libk-tests-ffi PRIVATE -Wl,--export-dynamic)
target_compile_definitions(libk-tests-ffi PRIVATE
    LIBK_FFI_FIXTURE_PATH="$<TARGET_FILE:klang-ffi-native-fixture>")
add_dependencies(libk-tests-ffi klang-ffi-native-fixture)

libk_add_test(libk-tests-ffi-distribution 120 tests/test-ffi-distribution.cpp)
target_compile_definitions(libk-tests-ffi-distribution PRIVATE
    KLANGC_PATH="$<TARGET_FILE:klangc>"
    LIBK_FFI_RUNTIME_INCLUDE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/src/runtime")

# ── libk sharded test helper macro ───────────────────────────────────────────
# Like libk_add_test, but registers <SHARDS> CTest entries running disjoint Catch2
# shards of the same executable, each with its own timeout (keeps heavy suites
# under the per-test budget on constrained machines).
# Usage: libk_add_test_sharded(<target> <timeout_s> <shards> <source1> [<source2> ...])
# ─────────────────────────────────────────────────────────────────────────────
macro(libk_add_test_sharded TARGET TIMEOUT SHARDS)
    add_executable(${TARGET} ${ARGN})
    target_link_libraries(${TARGET} klang-test-helpers Catch2::Catch2WithMain dl)
    add_dependencies(${TARGET} libk_stdlib)
    target_compile_definitions(${TARGET} PRIVATE
        LIBK_KDI_DIR="${LIBK_OUTPUT_DIR}"
        LIBK_LIB_DIR="${LIBK_OUTPUT_DIR}"
        LIBK_TIME_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/time/fixtures"
    )
    math(EXPR _libk_last_shard "${SHARDS} - 1")
    foreach(_libk_shard RANGE ${_libk_last_shard})
        klang_scale_ctest_timeout(_libk_test_timeout "${TIMEOUT}")
        add_test(NAME ${TARGET}-shard-${_libk_shard}
                 COMMAND ${TARGET} --shard-count ${SHARDS} --shard-index ${_libk_shard})
        set_tests_properties(${TARGET}-shard-${_libk_shard}
                             PROPERTIES TIMEOUT "${_libk_test_timeout}" RUN_SERIAL TRUE)
    endforeach()
endmacro()

# ── libk RTTI & Exception tests ──────────────────────────────────────────────
libk_add_test_sharded(libk-tests-rtti-exceptions 120 2
    tests/test-exceptions.cpp
    tests/test-rtti.cpp
    tests/test-rtti-constructors.cpp
    tests/test-rtti-parameters.cpp
)

# ── libk String & StringBuilder tests ────────────────────────────────────────
libk_add_test_sharded(libk-tests-strings 120 2
    tests/test-string.cpp
    tests/test-string-literal.cpp
    tests/test-string-search.cpp
    tests/test-string-builder-mutation.cpp
)

# ── libk proactor io_uring tests (Lot C & Lot E) ─────────────────────────────
libk_add_test(libk-tests-io-proactor 120
    tests/test-io-operation-backend.cpp
)
libk_add_test(libk-tests-io-proactor-kernel 120
    tests/test-io-proactor-kernel.cpp
)
libk_add_test(libk-tests-io-foundations-api 120
    tests/test-io-foundations-api.cpp
)
libk_add_test(libk-tests-file-channel-fully 120
    tests/test-io-file-channel-fully.cpp
)
libk_add_test(libk-tests-stream-channels 120
    tests/test-io-stream-channels.cpp
)
libk_add_test(libk-tests-datagram-channels 120
    tests/test-io-datagram-channels.cpp
)
libk_add_test(libk-tests-application-lifecycle 120
    tests/test-application-lifecycle.cpp
)
# The JIT resolves the fixture's C callbacks through the process symbol table.
target_link_options(libk-tests-application-lifecycle PRIVATE -Wl,--export-dynamic)

# ── libk foundational types & memory tests ───────────────────────────────────
libk_add_test_sharded(libk-tests-types 120 3
    tests/test-expected.cpp
    tests/test-optional.cpp
    tests/test-shared.cpp
    tests/test-shared-weak.cpp
    tests/test-memory.cpp
    tests/test-math.cpp
    tests/test-big-integer.cpp
    tests/test-application.cpp
    tests/test-symbol-mangling-native.cpp
)

# ── libk dynamic module loading tests (Application::loadModule) ──────────────
# Fixture libraries: K libraries (a registry and two plugins whose global objects
# register themselves in the registry) plus C libraries producing load errors.
set(MODFX_DIR ${LIBK_OUTPUT_DIR}/module-fixtures)
set(MODFX_SRC ${CMAKE_CURRENT_SOURCE_DIR}/tests/modules)
set(MODFX_KLANGC $<TARGET_FILE:klangc>
    -I ${LIBK_OUTPUT_DIR} -L ${LIBK_OUTPUT_DIR} -I ${MODFX_DIR} -L ${MODFX_DIR})
set(MODFX_C_FLAGS -shared -fPIC)
add_custom_command(
    OUTPUT  ${MODFX_DIR}/.fixtures.stamp
    COMMAND ${CMAKE_COMMAND} -E make_directory ${MODFX_DIR}
    COMMAND ${MODFX_KLANGC} --dyn-lib ${MODFX_SRC}/modreg.k
    COMMAND ${CMAKE_COMMAND} -E create_symlink libmodreg.kdi modreg.kdi
    COMMAND ${MODFX_KLANGC} --dyn-lib ${MODFX_SRC}/visitor_fixture.k
    COMMAND ${CMAKE_COMMAND} -E create_symlink libvisitor_fixture.kdi visitor_fixture.kdi
    COMMAND ${MODFX_KLANGC} --dyn-lib ${MODFX_SRC}/plugin_a.k
    COMMAND ${MODFX_KLANGC} --dyn-lib ${MODFX_SRC}/plugin_b.k
    COMMAND ${MODFX_KLANGC} --dyn-lib ${MODFX_SRC}/application_probe.k
    COMMAND ${MODFX_KLANGC} -o application_anchor_app ${MODFX_SRC}/application_anchor_app.k
    COMMAND ${CMAKE_C_COMPILER} ${MODFX_C_FLAGS} -Wl,-Bsymbolic
            -o libapplication_static_copy_probe.so ${APP_FFI_OBJ}
            ${MODFX_SRC}/application_static_copy_probe.c
    COMMAND ${CMAKE_C_COMPILER} ${MODFX_C_FLAGS} -o libmodmissing_target.so ${MODFX_SRC}/missing_target.c
    COMMAND ${CMAKE_C_COMPILER} ${MODFX_C_FLAGS} -o libmodmissing_user.so ${MODFX_SRC}/missing_user.c
            -L. -lmodmissing_target -Wl,-rpath,${MODFX_DIR}
    COMMAND ${CMAKE_COMMAND} -E remove libmodmissing_target.so
    COMMAND ${CMAKE_C_COMPILER} ${MODFX_C_FLAGS} -o libmodundef.so ${MODFX_SRC}/undefined_symbol.c
    COMMAND ${CMAKE_C_COMPILER} ${MODFX_C_FLAGS} -o libmodnative_dep.so ${MODFX_SRC}/native_dependency.c
            -L${MODFX_DIR} -Wl,--no-as-needed -lmodreg -Wl,--as-needed -Wl,-rpath,${MODFX_DIR}
    COMMAND ${CMAKE_C_COMPILER} ${MODFX_C_FLAGS} -o libmodincompatible_manifest.so
            ${MODFX_SRC}/incompatible_manifest.c
    COMMAND ${CMAKE_C_COMPILER} ${MODFX_C_FLAGS} -o libmodmalformed_manifest.so
            ${MODFX_SRC}/malformed_manifest.c
    COMMAND ${CMAKE_C_COMPILER} ${MODFX_C_FLAGS} -o libmodmalformed_dependency.so
            ${MODFX_SRC}/malformed_dependency.c -L${MODFX_DIR} -lmodmalformed_manifest
            -Wl,-rpath,${MODFX_DIR}
    COMMAND ${MODFX_KLANGC} --static-lib -o libstaticused.a ${MODFX_SRC}/static_used.k
    COMMAND ${CMAKE_COMMAND} -E create_symlink libstaticused.kdi staticused.kdi
    COMMAND ${MODFX_KLANGC} --static-lib -o libstaticunused.a ${MODFX_SRC}/static_unused.k
    COMMAND ${MODFX_KLANGC} --lib ${MODFX_DIR}/libstaticunused.a
            -o static_manifest_app ${MODFX_SRC}/static_manifest_app.k
    COMMAND ${CMAKE_COMMAND} -E copy ${MODFX_SRC}/not_a_library.txt ${MODFX_DIR}/not_a_library.txt
    COMMAND ${CMAKE_COMMAND} -E touch ${MODFX_DIR}/.fixtures.stamp
    DEPENDS klangc libk_stdlib
            ${MODFX_SRC}/modreg.k ${MODFX_SRC}/visitor_fixture.k
            ${MODFX_SRC}/plugin_a.k ${MODFX_SRC}/plugin_b.k
            ${MODFX_SRC}/application_probe.k
            ${MODFX_SRC}/application_anchor_app.k
            ${MODFX_SRC}/application_static_copy_probe.c
            ${APP_FFI_OBJ}
            ${MODFX_SRC}/missing_target.c ${MODFX_SRC}/missing_user.c
            ${MODFX_SRC}/undefined_symbol.c ${MODFX_SRC}/native_dependency.c
            ${MODFX_SRC}/incompatible_manifest.c ${MODFX_SRC}/malformed_manifest.c
            ${MODFX_SRC}/malformed_dependency.c
            ${MODFX_SRC}/static_used.k ${MODFX_SRC}/static_unused.k
            ${MODFX_SRC}/static_manifest_app.k
            ${MODFX_SRC}/not_a_library.txt
    WORKING_DIRECTORY ${MODFX_DIR}
    COMMENT "Building dynamic module loading test fixtures"
    VERBATIM
)
add_custom_target(libk_module_fixtures DEPENDS ${MODFX_DIR}/.fixtures.stamp)

libk_add_test(libk-tests-module 120
    tests/test-module.cpp
)
target_link_options(libk-tests-module PRIVATE
    -Wl,--export-dynamic-symbol=klang_module_symbol_application_fixture
    -Wl,--export-dynamic-symbol=klang_module_symbol_zero_fixture)
add_dependencies(libk-tests-module libk_module_fixtures)
target_compile_definitions(libk-tests-module PRIVATE LIBK_MODULE_FIXTURES_DIR="${MODFX_DIR}")
add_test(NAME libk-rtti-static-link-fixture
         COMMAND ${MODFX_DIR}/static_manifest_app)
klang_scale_ctest_timeout(_libk_test_timeout 120)
set_tests_properties(libk-rtti-static-link-fixture PROPERTIES TIMEOUT "${_libk_test_timeout}")
add_test(NAME libk-native-application-anchor
         COMMAND ${MODFX_DIR}/application_anchor_app)
klang_scale_ctest_timeout(_libk_test_timeout 120)
set_tests_properties(libk-native-application-anchor PROPERTIES
    TIMEOUT "${_libk_test_timeout}"
    WORKING_DIRECTORY ${MODFX_DIR}
    ENVIRONMENT "TMPDIR=${LIBK_OUTPUT_DIR}")

# ── libk basic stream I/O tests ──────────────────────────────────────────────
libk_add_test_sharded(libk-tests-io 120 2
    tests/test-io-byte-array-streams.cpp
    tests/test-io-filter-streams.cpp
    tests/test-io-transform-streams.cpp
    tests/test-io-buffered-streams.cpp
    tests/test-io-data-streams.cpp
    tests/test-io-stdio.cpp
    tests/test-io-byte-buffer.cpp
    tests/test-io-path.cpp
    tests/test-io-uri.cpp
)

# ── libk text stream I/O tests ───────────────────────────────────────────────
libk_add_test(libk-tests-io-text 120
    tests/test-io-print-stream.cpp
    tests/test-io-text-codecs.cpp
    tests/test-io-codecs.cpp
    tests/test-io-buffered-reader.cpp
    tests/test-io-text-scanner.cpp
    tests/test-io-text-printer.cpp
    tests/test-io-utf8-pipelines.cpp
)

# ── libk sequential collection tests (List, Vector, Sequences) ───────────────
libk_add_test_sharded(libk-tests-collections-sequential 120 3
    tests/test-list.cpp
    tests/test-vector.cpp
    tests/test-sequence-helpers.cpp
)

# ── libk associative collection tests (Set, Map) ─────────────────────────────
libk_add_test(libk-tests-collections-associative 120
    tests/test-set.cpp
    tests/test-map.cpp
)

# ── libk threading and I/O tests ─────────────────────────────────────────────
libk_add_test_sharded(libk-thread-io-tests 120 2
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
    tests/test-io-file-stream.cpp # retained channel interruption/close regressions
    # ── Network and socket I/O ────────────────────────────────────────────
    tests/test-io-socket.cpp
    tests/test-io-server-socket.cpp
    tests/test-io-socket-interrupt.cpp
    tests/test-io-datagram.cpp
    tests/test-io-unix-socket-native.cpp
    tests/test-io-event-loop.cpp
)

# ── libk executor and thread pool tests ─────────────────────────────────────
libk_add_test(libk-tests-executor 120
    tests/test-executor.cpp
)

# ── libk UUID & Cryptographic hash tests ─────────────────────────────────────
libk_add_test(libk-tests-uuid 120
    tests/test-uuid.cpp
)

# ── libk time API tests (Phase 1, Phase 2, Phase 3 & Phase 4) ─────────────────
libk_add_test_sharded(libk-tests-time 120 2
    tests/time/test-time-arithmetic.cpp
    tests/time/test-time-iso.cpp
    tests/time/test-time-clock.cpp
    tests/time/test-time-tzdb.cpp
    tests/time/test-time-zones.cpp
    tests/time/test-time-leap-seconds.cpp
    tests/time/test-time-chronology.cpp
    tests/time/test-time-locale.cpp
    tests/time/test-time-scales.cpp
)

# ── libk performance / stress tests ──────────────────────────────────────────
# Extremely resource-intensive (thread storms, I/O saturation, cancellation
# floods).  Excluded from the default CTest run to avoid overwhelming low-spec
# hosts.  Enable with -DLIBK_PERF_TESTS=ON, then run with:
#   ctest -R libk-perf-tests
# ─────────────────────────────────────────────────────────────────────────────
option(LIBK_PERF_TESTS "Build and register the libk performance/stress tests" OFF)
if(LIBK_PERF_TESTS)
    libk_add_test(libk-perf-tests 180
        tests/test-thread-stress.cpp
        tests/test-io-stress.cpp
        tests/test-cancellation-storm.cpp
        tests/test-runtime-shutdown.cpp
    )
endif()
