#include <catch2/catch_all.hpp>
#include "../../klang/tests/helpers.hpp"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <future>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

TEST_CASE("I/O foundations: execution contexts support const overrides and scoped destruction",
          "[libk][io][proactor][api][execution-context]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __io_foundation_execution_context__;
        destroyed : int = 0;
        class CancelledContext : public k::io::ExecutionContext {
        public:
            CancelledContext() : ExecutionContext() {}
            ~CancelledContext() { ++destroyed; }
            override const isCancellationRequested() : bool { return true; }
        }
        test() : int {
            destroyed = 0;
            {
                base : k::io::ExecutionContext;
                cancelled : CancelledContext;
                view : const k::io::ExecutionContext& = cancelled;
                operation : k::io::IoOperation;
                operationView : const k::io::ExecutionContext& = operation;
                if (base.isCancellationRequested() ||
                    !view.isCancellationRequested() ||
                    operationView.isCancellationRequested()) return -1;
                current : k::io::ExecutionContext! = k::io::ExecutionContext::current();
                currentOperation : k::io::IoOperation! = k::io::IoOperation::current();
                if (current != null || currentOperation != null) return -2;
            }
            return destroyed;
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(test);
    REQUIRE(test() == 1);
}

TEST_CASE("I/O foundations: result aliases and explicit callable carriers import through KDI",
          "[libk][io][proactor][api]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __io_foundation_aliases__;
        observed : int = 0;
        decode(value: long) : int { return (int) value; }
        test() : int {
            result : k::io::IoResult<int>;
            value : int = 42;
            result.setResult(value);
            done : k::io::IoResult<k::io::IoDone>;
            sentinel : k::io::IoDone;
            done.setResult(sentinel);
            callback : k::io::IoCompletion<int> = [](r: k::io::IoResult<int>) {
                if (r.hasResult()) observed = r.getResult();
                else observed = -r.getError().getErrno();
            };
            callback(result);
            if (observed != 42 || !done.hasResult()) return -1;
            error : k::io::IoError = k::io::IoError(k::io::IoErrorKind::System, 5);
            result.setError(error);
            callback(result);
            if (observed != -5) return -2;
            result.setResult(value);
            carrier : k::io::IoCallbackCarrier<int>(callback, decode);
            return result.getResult();
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(test);
    REQUIRE(test() == 42);
}

