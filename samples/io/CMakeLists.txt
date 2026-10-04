add_custom_target(klang-io-samples)
foreach(mode IN ITEMS proactor future sync)
    klang_add_sample(sample-io-file-read-${mode} file_read_${mode}.k common.k)
    klang_add_sample(sample-io-tcp-client-${mode} tcp_client_${mode}.k common.k)
    klang_add_sample(sample-io-tcp-server-${mode} tcp_server_${mode}.k common.k)
    add_dependencies(klang-io-samples
        sample-io-file-read-${mode}
        sample-io-tcp-client-${mode}
        sample-io-tcp-server-${mode})
endforeach()
add_dependencies(klang-samples klang-io-samples)
