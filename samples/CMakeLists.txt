# Each executable is one K compilation unit, optionally with shared sources.
function(klang_add_sample target source)
    set(sources)
    foreach(input IN ITEMS "${source}" ${ARGN})
        get_filename_component(absolute_source "${input}" ABSOLUTE
            BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND sources "${absolute_source}")
    endforeach()
    get_filename_component(output_name "${source}" NAME_WE)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/${output_name}")
    add_custom_command(
        OUTPUT "${output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}"
        COMMAND $<TARGET_FILE:klangc> ${sources}
            -I "${KLANG_STDLIB_KDI_DIR}"
            -L "${KLANG_STDLIB_LIB_DIR}"
            -o "${output}"
        DEPENDS klangc libk_stdlib ${sources}
            "${KLANG_STDLIB_KDI_DIR}/libk.kdi"
            "${KLANG_STDLIB_LIB_DIR}/libk.so"
        VERBATIM
    )
    add_custom_target(${target} DEPENDS "${output}")
    add_dependencies(klang-samples ${target})
endfunction()

add_custom_target(klang-samples ALL)
klang_add_sample(sample-fibo fibo.k)
klang_add_sample(sample-file-channel-fully file_channel_fully.k)
klang_add_sample(sample-stream-channel-loopback stream_channel_loopback.k)
klang_add_sample(sample-datagram-loopback datagram_loopback.k)

if(KLANG_BUILD_IO_SAMPLES)
    add_subdirectory(io)
endif()
