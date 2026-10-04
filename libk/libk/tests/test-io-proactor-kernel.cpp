#include <catch2/catch_all.hpp>
#include <dlfcn.h>
#include <string>
#include <unistd.h>
#include <cerrno>

namespace {
template<class T> T symbol(void* lib, const char* name) {
    auto ptr = dlsym(lib, name);
    REQUIRE(ptr);
    return reinterpret_cast<T>(ptr);
}
}
TEST_CASE("Proactor kernel: real pipe transfer and linked timeout",
          "[libk][io][proactor][kernel]") {
    void* lib = dlopen((std::string(LIBK_LIB_DIR) + "/libk.so").c_str(), RTLD_NOW | RTLD_GLOBAL);
    REQUIRE(lib);
    auto init = symbol<int (*)()>(lib, "__k_io_runtime_init");
    int rc = init();
    if (rc == -ENOTSUP || rc == -ENOSYS || rc == -EPERM || rc == -EACCES) {
        SKIP("io_uring is unavailable, prohibited, or lacks required data-plane opcodes");
    }
    REQUIRE(rc == 0); // resource failures are failures, not kernel skips
    auto create = symbol<void* (*)(int, int)>(lib, "__k_io_handle_create");
    auto release_handle = symbol<void (*)(void*)>(lib, "__k_io_handle_release");
    auto submit = symbol<void* (*)(void*, void*, int, long long, long long,
                                  void*, void*, void*, void*)>(lib, "__k_io_submit_read");
    auto wait = symbol<int (*)(void*, long long)>(lib, "__k_io_op_wait_quiescent");
    auto bytes = symbol<long long (*)(const void*)>(lib, "__k_io_op_bytes_transferred");
    auto error = symbol<int (*)(const void*)>(lib, "__k_io_op_error_kind");
    auto release_op = symbol<void (*)(void*)>(lib, "__k_io_op_release");
    int pipefd[2];
    REQUIRE(pipe(pipefd) == 0);
    void* h = create(pipefd[0], 0);
    REQUIRE(h);
    char buffer[8] = {};
    REQUIRE(write(pipefd[1], "proactor", 8) == 8);
    void* op = submit(h, buffer, 8, -1, -1, nullptr, nullptr, nullptr, nullptr);
    REQUIRE(op);
    REQUIRE(wait(op, 5000000000LL) == 0);
    REQUIRE(bytes(op) == 8);
    REQUIRE(std::string(buffer, 8) == "proactor");
    release_op(op);
    op = submit(h, buffer, 8, -1, 1000000LL, nullptr, nullptr, nullptr, nullptr);
    REQUIRE(op);
    REQUIRE(wait(op, 5000000000LL) == 0);
    REQUIRE(error(op) == 3); // Timeout, never nondeterministically Cancelled
    release_op(op);
    release_handle(h);
    close(pipefd[1]);
}
