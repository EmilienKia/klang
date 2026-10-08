/*
 * K Language standard library — ::k::Application / main(args) tests
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * Tests for the `main(args : const String[])` entry-point form and the
 * compiler-synthesised ::k::Application class (env() accessor).
 *
 * These tests JIT-compile small K programs that `import k;` and define a
 * top-level `main` function. The compiler synthesises a private `Application`
 * class (extending ::k::Application) in the module, moves `main` into it, and
 * generates a C-ABI `main(int argc, char** argv)` proxy that constructs the
 * Application instance, optionally builds the `args` array from argv, invokes
 * `main`, and returns the appropriate exit code.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"
#include <filesystem>
#include <unistd.h>



#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined — check CMakeLists.txt"
#endif

namespace {

std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

} // anonymous namespace


// ═════════════════════════════════════════════════════════════════════════════
// 1. main() with no parameters, no return type
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application — main() with no args, no return", "[libk][application]") {
    auto jit = jit_k(R"SRC(
        module __app_main_void__;
        import k;

        main() {
        }
        )SRC");
    REQUIRE(jit);

    static const int argc = 1;
    static const char* argv[] = {"prog", nullptr};

    auto main = jit->lookup_main_entry_symbol< int(*)(int, char**) >();
    auto res = main(argc, (char**)argv);
    REQUIRE(res == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. main() : int with no parameters
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application — main() : int with no args", "[libk][application]") {
    auto jit = jit_k(R"SRC(
        module __app_main_int__;
        import k;

        main() : int {
            return 42;
        }
        )SRC");
    REQUIRE(jit);

    static const int argc = 1;
    static const char* argv[] = {"prog", nullptr};

    auto main = jit->lookup_main_entry_symbol< int(*)(int, char**) >();
    auto res = main(argc, (char**)argv);
    REQUIRE(res == 42);
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. main(args : const String[]) — argc/argv propagation, no return type
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application — main(args) receives argv as String[]", "[libk][application]") {
    auto jit = jit_k(R"SRC(
        module __app_main_args__;
        import k;

        argCount : int = -1;
        firstArg : String;

        main(args : const String[]) {
            argCount = args.size;
            if (args.size > 0u) {
                firstArg = args[0];
            }
        }
        )SRC");
    REQUIRE(jit);

    static const int argc = 3;
    static const char* argv[] = {"myprog", "hello", "world", nullptr};

    auto main = jit->lookup_main_entry_symbol< int(*)(int, char**) >();
    auto res = main(argc, (char**)argv);
    REQUIRE(res == 0);

    auto argCount = jit->lookup_symbol< int* >("argCount");
    REQUIRE(*argCount == 3);
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. main(args : const String[]) : int — argc/argv propagation with return value
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application — main(args) : int returns computed exit code", "[libk][application]") {
    auto jit = jit_k(R"SRC(
        module __app_main_args_int__;
        import k;

        main(args : const String[]) : int {
            return args.size;
        }
        )SRC");
    REQUIRE(jit);

    static const int argc = 4;
    static const char* argv[] = {"myprog", "a", "b", "c", nullptr};

    auto main = jit->lookup_main_entry_symbol< int(*)(int, char**) >();
    auto res = main(argc, (char**)argv);
    REQUIRE(res == 4);
}

TEST_CASE("Application singleton — activation, inheritance casts, duplicate rejection and teardown",
          "[libk][application][singleton]") {
    auto jit = jit_k(R"SRC(
        module __app_singleton_lifecycle__;
        import k;

        @ffi::Extern("C") private __k_application_activate(application: k::Application*): int;

        seen_empty_in_constructor : bool = false;
        seen_active_in_main : bool = false;
        seen_empty_in_global_constructor : bool = false;
        seen_empty_in_global_destructor : bool = false;
        same_concrete_object : bool = false;
        same_intermediate_object : bool = false;
        interface_cast_succeeded : bool = false;
        same_interface_object : bool = false;
        seen_active_in_worker : bool = false;
        same_worker_object : bool = false;
        worker_join_interrupted : bool = false;
        duplicate_rejected : bool = false;
        duplicate_kept_original : bool = false;
        seen_empty_in_destructor : bool = false;

        struct ApplicationGlobalObserver {
            static ApplicationGlobalObserver() {
                current : OptionalRef<k::Application> = k::Application::getApplication();
                seen_empty_in_global_constructor = !current.hasValue();
            }

            static ~ApplicationGlobalObserver() {
                current : OptionalRef<k::Application> = k::Application::getApplication();
                seen_empty_in_global_destructor = !current.hasValue();
            }
        }

        interface ApplicationMarker {
            marker() : int;
        }

        abstract class ApplicationLayer : public ApplicationMarker, public k::Application {
            public main() : int -> delete;
            public main(args : const String[]) -> delete;
            public main(args : const String[]) : int -> delete;
            public abstract main() : int;

            public layerMarker() : int {
                return 915;
            }
        }

        class Application : public ApplicationLayer {
            private _marker : int = 914;

            public Application() {
                current : OptionalRef<k::Application> = k::Application::getApplication();
                seen_empty_in_constructor = !current.hasValue();
            }

            public override marker() : int {
                return _marker;
            }

            public override main() : int {
                current : OptionalRef<k::Application> = k::Application::getApplication();
                seen_active_in_main = current.hasValue();
                if (current.hasValue()) {
                    concrete : Application? = &current.get();
                    if (concrete != null) {
                        same_concrete_object = concrete->marker() == _marker;
                    }
                    intermediate : ApplicationLayer? = &current.get();
                    if (intermediate != null) {
                        same_intermediate_object = intermediate->layerMarker() == 915;
                    }
                    if (concrete != null) {
                        marker_view : ApplicationMarker? = &*concrete;
                        interface_cast_succeeded = marker_view != null;
                        if (marker_view != null) {
                            marker_ref : ApplicationMarker& = *marker_view;
                            same_interface_object = marker_ref.marker() == _marker;
                        }
                    }
                }

                duplicate_status : int = __k_application_activate(&this);
                duplicate_rejected = duplicate_status == 4;
                duplicate_current : OptionalRef<k::Application> =
                    k::Application::getApplication();
                if (duplicate_current.hasValue()) {
                    original_base : k::Application* = &current.get();
                    after_duplicate_base : k::Application* = &duplicate_current.get();
                    duplicate_kept_original = original_base == after_duplicate_base;
                }

                worker : ApplicationWorker! = new ApplicationWorker();
                thread : Thread! = new Thread(worker);
                thread.start();
                try {
                    thread.join();
                } catch (e : ThreadInterruptionException&) {
                    worker_join_interrupted = true;
                }
                return 0;
            }

            ~Application() {
                current : OptionalRef<k::Application> = k::Application::getApplication();
                seen_empty_in_destructor = !current.hasValue();
            }
        }

        class ApplicationWorker : public Runnable {
            public override run() : void {
                current : OptionalRef<k::Application> = k::Application::getApplication();
                if (current.hasValue()) {
                    concrete : Application? = &current.get();
                    if (concrete != null) {
                        seen_active_in_worker = true;
                        same_worker_object = concrete->marker() == 914;
                    }
                }
            }
        }
    )SRC");
    REQUIRE(jit);

    auto run = jit->run_main();
    REQUIRE(run.has_value());
    REQUIRE_FALSE(run->uncaught_exception);
    CHECK(run->status == 0);

    CHECK(*jit->lookup_symbol<bool*>("seen_empty_in_constructor"));
    CHECK(*jit->lookup_symbol<bool*>("seen_empty_in_global_constructor"));
    CHECK(*jit->lookup_symbol<bool*>("seen_active_in_main"));
    CHECK(*jit->lookup_symbol<bool*>("same_concrete_object"));
    CHECK(*jit->lookup_symbol<bool*>("same_intermediate_object"));
    CHECK(*jit->lookup_symbol<bool*>("interface_cast_succeeded"));
    CHECK(*jit->lookup_symbol<bool*>("same_interface_object"));
    CHECK(*jit->lookup_symbol<bool*>("seen_active_in_worker"));
    CHECK(*jit->lookup_symbol<bool*>("same_worker_object"));
    CHECK_FALSE(*jit->lookup_symbol<bool*>("worker_join_interrupted"));
    CHECK(*jit->lookup_symbol<bool*>("duplicate_rejected"));
    CHECK(*jit->lookup_symbol<bool*>("duplicate_kept_original"));
    CHECK(*jit->lookup_symbol<bool*>("seen_empty_in_destructor"));

    jit->finalize_runtime();
    CHECK(*jit->lookup_symbol<bool*>("seen_empty_in_global_destructor"));
}

TEST_CASE("Application singleton — withdrawal and destruction on exceptional main exit",
          "[libk][application][singleton][exceptions]") {
    auto jit = jit_k(R"SRC(
        module __app_singleton_exception__;
        import k;

        seen_active_in_main : bool = false;
        seen_empty_in_destructor : bool = false;

        class Application : public k::Application {
            public main() : int {
                current : OptionalRef<k::Application> = k::Application::getApplication();
                seen_active_in_main = current.hasValue();
                throw FatalError(37);
            }

            ~Application() {
                current : OptionalRef<k::Application> = k::Application::getApplication();
                seen_empty_in_destructor = !current.hasValue();
            }
        }
    )SRC");
    REQUIRE(jit);

    auto run = jit->run_main();
    REQUIRE(run.has_value());
    CHECK(run->uncaught_exception);
    CHECK(*jit->lookup_symbol<bool*>("seen_active_in_main"));
    CHECK(*jit->lookup_symbol<bool*>("seen_empty_in_destructor"));

    auto next_jit = jit_k(R"SRC(
        module __app_singleton_next_run__;
        import k;

        main() : int {
            current : OptionalRef<k::Application> = k::Application::getApplication();
            if (current.hasValue()) {
                return 23;
            }
            return 1;
        }
    )SRC");
    REQUIRE(next_jit);
    auto next_run = next_jit->run_main();
    REQUIRE(next_run.has_value());
    CHECK_FALSE(next_run->uncaught_exception);
    CHECK(next_run->status == 23);
}

TEST_CASE("Application standard streams — custom roots, text adapters, and frozen configuration",
          "[libk][application][stdio]") {
    auto jit = jit_k(R"SRC(
        module __app_stdio_custom__;
        import k;

        frozenRejected : bool = false;
        nullInputRejected : bool = false;
        constructorAccessorsAvailable : bool = false;

        outsideMain() : bool {
            try {
                printer : k::io::TextPrinter& = k::io::stdout();
            } catch (e : k::io::NoActiveApplicationError&) {
                return true;
            }
            return false;
        }

        class FixtureInput : public k::io::InputStream<byte> {
            private _pos : int;

            public FixtureInput() : _pos(0) {}

            public override read() : Optional<byte> {
                if (_pos >= 4) return Optional<byte>();
                value : byte = (byte) 0u;
                if (_pos == 0) value = (byte) 0x68u;
                else if (_pos == 1) value = (byte) 0xC3u;
                else if (_pos == 2) value = (byte) 0xA9u;
                else value = (byte) 0x0Au;
                ++_pos;
                return Optional<byte>(value);
            }

            public override read(buf: byte[], off: unsigned int, len: unsigned int)
                    : Expected<unsigned int, k::io::StreamOutOfData> {
                count : unsigned int = 0u;
                while (count < len && _pos < 4) {
                    buf[off + count] = read().get();
                    ++count;
                }
                return Expected<unsigned int, k::io::StreamOutOfData>::expected(count);
            }

            public override read(buf: byte[])
                    : Expected<unsigned int, k::io::StreamOutOfData> {
                return read(buf, 0u, (unsigned int) buf.size);
            }

            public override skip(n: unsigned long) : unsigned long {
                skipped : unsigned long = 0uL;
                while (skipped < n && _pos < 4) {
                    read();
                    ++skipped;
                }
                return skipped;
            }

            public override available() : Expected<unsigned int, k::io::StreamOutOfData> {
                remaining : unsigned int = (unsigned int) (4 - _pos);
                return Expected<unsigned int, k::io::StreamOutOfData>::expected(remaining);
            }

            public override close() {}
        }

        class TrackedRoot : public k::io::ArrayOutputStream<byte> {
            public TrackedRoot() {}
        }

        class Application : public k::Application {
            private _stdoutCapture : k::io::ArrayOutputStream<byte>*;
            private _stderrCapture : k::io::ArrayOutputStream<byte>*;

            public Application() {
                setStandardInput(new FixtureInput());
                nullInput : k::io::InputStream<byte>! = null;
                try {
                    setStandardInput(nullInput);
                } catch (e : k::io::StandardStreamConfigurationError&) {
                    nullInputRejected = true;
                }
                stdoutRoot : TrackedRoot! = new TrackedRoot();
                _stdoutCapture = &*stdoutRoot;
                setStandardOutput(stdoutRoot);
                stderrRoot : k::io::ArrayOutputStream<byte>! =
                    new k::io::ArrayOutputStream<byte>();
                _stderrCapture = &*stderrRoot;
                setStandardError(stderrRoot);
                this.stdin();
                this.stdinBytes();
                this.stdinReader();
                this.stdoutBytes();
                this.stderrBytes();
                this.stdout();
                this.stderr();
                this.stdoutPrint();
                this.stderrPrint();
                constructorAccessorsAvailable = true;
            }

            public main() : int {
                firstByte : Optional<byte> = k::io::stdinBytes().read();
                if (!firstByte.hasValue() || firstByte.get() != (byte) 'h') {
                    return 4;
                }
                line : Optional<String> = k::io::stdinReader().readLine();
                if (!line.hasValue() || line.get() != String("é")) {
                    return 1;
                }

                k::io::stdout().print("A");
                k::io::stdoutBytes().write((byte) 'B');
                k::io::stdoutPrint().print("C");
                k::io::stderr().print("D");
                k::io::stderrBytes().write((byte) 'E');
                k::io::stderrPrint().print("F");

                try {
                    setStandardOutput(new k::io::ArrayOutputStream<byte>());
                } catch (e : k::io::StandardStreamConfigurationError&) {
                    frozenRejected = true;
                }

                if (_stdoutCapture == null || _stderrCapture == null) return 3;
                stdoutData : byte[]! = _stdoutCapture->toArray();
                stderrData : byte[]! = _stderrCapture->toArray();
                valid : bool = stdoutData.size == 3u && stderrData.size == 3u;
                if (valid) {
                    valid = stdoutData[0] == (byte) 'A' &&
                        stdoutData[1] == (byte) 'B' &&
                        stdoutData[2] == (byte) 'C' &&
                        stderrData[0] == (byte) 'D' &&
                        stderrData[1] == (byte) 'E' &&
                        stderrData[2] == (byte) 'F';
                }
                delete stdoutData;
                delete stderrData;
                if (!valid || !frozenRejected || !nullInputRejected ||
                        !constructorAccessorsAvailable) {
                    return 2;
                }

                return 0;
            }
        }
    )SRC");
    REQUIRE(jit);

    auto outside_main = jit->lookup_symbol<bool(*)()>("outsideMain");
    REQUIRE(outside_main());

    static const int argc = 1;
    static const char* argv[] = {"prog", nullptr};
    auto main = jit->lookup_main_entry_symbol<int(*)(int, char**)>();
    CHECK(main(argc, (char**)argv) == 0);
    CHECK(outside_main());
}

TEST_CASE("Application standard streams — decorators intercept operations and own their roots",
          "[libk][application][stdio][decoration][lifecycle]") {
    auto jit = jit_k(R"SRC(
        module __app_stdio_decorators__;
        import k;

        inputIntercepts : int = 0;
        outputIntercepts : int = 0;
        outputCloses : int = 0;
        outputDestructions : int = 0;

        class TrackedRoot : public k::io::ArrayOutputStream<byte> {
            ~TrackedRoot() {
                ++outputDestructions;
            }
        }

        class CountingOutput : public k::io::FilterOutputStream<byte> {
            public CountingOutput(output: k::io::OutputStream<byte>*)
                    : FilterOutputStream(output) {}

            public override write(value: byte) : k::io::OutputStream<byte>& {
                ++outputIntercepts;
                return _out->write(value);
            }

            public override write(values: const byte[], off: unsigned int, len: unsigned int)
                    : k::io::OutputStream<byte>& {
                ++outputIntercepts;
                return _out->write(values, off, len);
            }

            public override write(values: const byte[]) : k::io::OutputStream<byte>& {
                ++outputIntercepts;
                return _out->write(values);
            }

            public override flush() : k::io::OutputStream<byte>& {
                return _out->flush();
            }

            public override close() : k::io::OutputStream<byte>& {
                ++outputCloses;
                flush();
                return _out->close();
            }

            ~CountingOutput() {
                ++outputDestructions;
            }
        }

        class Application : public k::Application {
            private _stdoutCapture : k::io::ArrayOutputStream<byte>*;
            private _stderrCapture : k::io::ArrayOutputStream<byte>*;

            public Application() {
                setStandardOutput(new TrackedRoot());
                _stdoutCapture = (k::io::ArrayOutputStream<byte>*)&*_stdout;
                outputDecorator : CountingOutput! =
                    new CountingOutput((k::io::OutputStream<byte>*)&*_stdout);
                decorateStandardOutput(
                    (k::io::FilterOutputStream<byte>!) outputDecorator);
            }

            public main() : int {
                _stdout->write((byte)65);
                return 0;
            }
        }
    )SRC");
    REQUIRE(jit);

    auto run = jit->run_main();
    REQUIRE(run.has_value());
    CHECK_FALSE(run->uncaught_exception);
    CHECK(run->status == 0);
    CHECK(*jit->lookup_symbol<int*>("outputIntercepts") == 1);
    CHECK(*jit->lookup_symbol<int*>("outputCloses") == 1);
    CHECK(*jit->lookup_symbol<int*>("outputDestructions") == 2);
}

TEST_CASE("Application standard streams — input and error decorators intercept and own their roots",
          "[libk][application][stdio][decoration][lifecycle]") {
    auto jit = jit_k(R"SRC(
        module __app_stdio_input_error_decorators__;
        import k;

        inputIntercepts : int = 0;
        inputRootDestructions : int = 0;
        inputDecoratorDestructions : int = 0;
        errorIntercepts : int = 0;
        errorRootDestructions : int = 0;
        errorDecoratorDestructions : int = 0;

        class TrackedInput : public k::io::InputStream<byte> {
            private _position : int;

            public TrackedInput() : _position(0) {}

            public override read() : Optional<byte> {
                if (_position >= 3) return Optional<byte>();
                value : byte = (byte) 'I';
                if (_position == 1) value = (byte) 'O';
                else if (_position == 2) value = (byte) '\n';
                ++_position;
                return Optional<byte>(value);
            }

            public override read(buf: byte[], off: unsigned int, len: unsigned int)
                    : Expected<unsigned int, k::io::StreamOutOfData> {
                count : unsigned int = 0u;
                while (count < len && _position < 3) {
                    buf[off + count] = read().get();
                    ++count;
                }
                return Expected<unsigned int, k::io::StreamOutOfData>::expected(count);
            }

            public override read(buf: byte[])
                    : Expected<unsigned int, k::io::StreamOutOfData> {
                return read(buf, 0u, (unsigned int) buf.size);
            }

            public override skip(n: unsigned long) : unsigned long {
                skipped : unsigned long = 0uL;
                while (skipped < n && _position < 3) {
                    read();
                    ++skipped;
                }
                return skipped;
            }

            public override available() : Expected<unsigned int, k::io::StreamOutOfData> {
                return Expected<unsigned int, k::io::StreamOutOfData>::expected(
                    (unsigned int) (3 - _position));
            }

            public override close() {}

            ~TrackedInput() {
                ++inputRootDestructions;
            }
        }

        class CountingInput : public k::io::FilterInputStream<byte> {
            public CountingInput(input: TrackedInput*)
                    : FilterInputStream(input) {}

            public override read() : Optional<byte> {
                ++inputIntercepts;
                return _in->read();
            }

            public override read(buf: byte[], off: unsigned int, len: unsigned int)
                    : Expected<unsigned int, k::io::StreamOutOfData> {
                ++inputIntercepts;
                return _in->read(buf, off, len);
            }

            public override read(buf: byte[])
                    : Expected<unsigned int, k::io::StreamOutOfData> {
                ++inputIntercepts;
                return _in->read(buf);
            }

            ~CountingInput() {
                ++inputDecoratorDestructions;
            }
        }

        class TrackedErrorRoot : public k::io::ArrayOutputStream<byte> {
            ~TrackedErrorRoot() {
                ++errorRootDestructions;
            }
        }

        class CountingErrorOutput : public k::io::FilterOutputStream<byte> {
            public CountingErrorOutput(output: TrackedErrorRoot*)
                    : FilterOutputStream(output) {}

            public override write(value: byte) : k::io::OutputStream<byte>& {
                ++errorIntercepts;
                return _out->write(value);
            }

            public override write(values: const byte[], off: unsigned int, len: unsigned int)
                    : k::io::OutputStream<byte>& {
                ++errorIntercepts;
                return _out->write(values, off, len);
            }

            public override write(values: const byte[]) : k::io::OutputStream<byte>& {
                ++errorIntercepts;
                return _out->write(values);
            }

            public override flush() : k::io::OutputStream<byte>& {
                return _out->flush();
            }

            ~CountingErrorOutput() {
                ++errorDecoratorDestructions;
            }
        }

        class Application : public k::Application {
            private _errorCapture : k::io::ArrayOutputStream<byte>*;

            public Application() {
                inputRoot : TrackedInput! = new TrackedInput();
                inputRootRaw : TrackedInput* = &*inputRoot;
                setStandardInput(inputRoot);
                inputDecorator : CountingInput! =
                    new CountingInput(inputRootRaw);
                decorateStandardInput(
                    (k::io::FilterInputStream<byte>!) inputDecorator);

                errorRoot : TrackedErrorRoot! = new TrackedErrorRoot();
                errorRootRaw : TrackedErrorRoot* = &*errorRoot;
                setStandardError(errorRoot);
                _errorCapture = errorRootRaw;
                errorDecorator : CountingErrorOutput! =
                    new CountingErrorOutput(errorRootRaw);
                decorateStandardError(
                    (k::io::FilterOutputStream<byte>!) errorDecorator);
            }

            public main() : int {
                first : Optional<byte> = k::io::stdinBytes().read();
                if (!first.hasValue() || first.get() != (byte) 'I') return 1;
                line : Optional<String> = k::io::stdinReader().readLine();
                if (!line.hasValue() || line.get() != String("O")) return 2;

                k::io::stderr().print("e");
                k::io::stderrBytes().write((byte) 'r');
                k::io::stderrPrint().print("l");

                captured : byte[]! = _errorCapture->toArray();
                valid : bool = captured.size == 3u &&
                    captured[0] == (byte) 'e' &&
                    captured[1] == (byte) 'r' &&
                    captured[2] == (byte) 'l';
                delete captured;
                if (!valid || inputIntercepts < 2 || errorIntercepts < 3) return 3;
                return 0;
            }
        }
    )SRC");
    REQUIRE(jit);

    auto run = jit->run_main();
    REQUIRE(run.has_value());
    CHECK_FALSE(run->uncaught_exception);
    CHECK(run->status == 0);
    CHECK(*jit->lookup_symbol<int*>("inputIntercepts") >= 2);
    CHECK(*jit->lookup_symbol<int*>("errorIntercepts") >= 3);
    CHECK(*jit->lookup_symbol<int*>("inputRootDestructions") == 1);
    CHECK(*jit->lookup_symbol<int*>("inputDecoratorDestructions") == 1);
    CHECK(*jit->lookup_symbol<int*>("errorRootDestructions") == 1);
    CHECK(*jit->lookup_symbol<int*>("errorDecoratorDestructions") == 1);
}

TEST_CASE("Application standard streams — failed decoration leaves the old root and adapters installed",
          "[libk][application][stdio][decoration][exceptions]") {
    auto jit = jit_k(R"SRC(
        module __app_stdio_decoration_failure__;
        import k;

        configurationRejected : bool = false;
        flushRejected : bool = false;
        rootPreserved : bool = false;
        flushAttempts : int = 0;

        class FailingOutput : public k::io::ArrayOutputStream<byte> {
            public override flush() : k::io::OutputStream<byte>& {
                ++flushAttempts;
                throw k::io::IOException(5);
            }
        }

        class CountingOutput : public k::io::FilterOutputStream<byte> {
            public CountingOutput(output: k::io::OutputStream<byte>*)
                    : FilterOutputStream(output) {}
        }

        class Application : public k::Application {
            public Application() {
                setStandardOutput(new FailingOutput());
                oldRoot : k::io::OutputStream<byte>* = &*_stdout;
                _stdout->write((byte) 'X');

                invalidDecorator : CountingOutput! = new CountingOutput(null);
                try {
                    decorateStandardOutput(
                        (k::io::FilterOutputStream<byte>!) invalidDecorator);
                } catch (error : io::StandardStreamConfigurationError&) {
                    configurationRejected = true;
                }

                validDecorator : CountingOutput! =
                    new CountingOutput((k::io::OutputStream<byte>*)&*_stdout);
                try {
                    decorateStandardOutput(
                        (k::io::FilterOutputStream<byte>!) validDecorator);
                } catch (error : io::IOException&) {
                    flushRejected = true;
                }

                rootPreserved = &*_stdout == oldRoot;
            }

            public main() : int {
                if (!configurationRejected || !flushRejected || !rootPreserved) {
                    return 1;
                }
                return 0;
            }
        }
    )SRC");
    REQUIRE(jit);

    auto run = jit->run_main();
    REQUIRE(run.has_value());
    CHECK_FALSE(run->uncaught_exception);
    CHECK(run->status == 1);
    CHECK(*jit->lookup_symbol<bool*>("configurationRejected"));
    CHECK(*jit->lookup_symbol<bool*>("flushRejected"));
    CHECK(*jit->lookup_symbol<bool*>("rootPreserved"));
    CHECK(*jit->lookup_symbol<int*>("flushAttempts") == 3);
}

TEST_CASE("Application standard streams — flush failures report without replacing main exceptions",
          "[libk][application][stdio][exceptions]") {
    auto normal_jit = jit_k(R"SRC(
        module __app_stdio_flush_failure__;
        import k;

        class Application : public k::Application {
            public Application() {
                setStandardOutput(new k::io::file::FileOutputStream("/dev/full"));
            }

            public main() : int {
                k::io::stdout().print("normal flush failure");
                return 0;
            }
        }
    )SRC");
    REQUIRE(normal_jit);
    auto normal_run = normal_jit->run_main();
    REQUIRE(normal_run.has_value());
    CHECK_FALSE(normal_run->uncaught_exception);
    CHECK(normal_run->status == 1);

    auto exception_jit = jit_k(R"SRC(
        module __app_stdio_flush_failure_exception__;
        import k;

        class Application : public k::Application {
            public Application() {
                setStandardOutput(new k::io::file::FileOutputStream("/dev/full"));
            }

            public main() : int {
                k::io::stdout().print("exceptional flush failure");
                throw FatalError(37);
            }
        }
    )SRC");
    REQUIRE(exception_jit);
    auto exception_run = exception_jit->run_main();
    REQUIRE(exception_run.has_value());
    CHECK(exception_run->uncaught_exception);
}

TEST_CASE("Application standard streams — borrowed native handles survive close and repeated runs",
          "[libk][application][stdio][lifecycle]") {
    auto jit = jit_k(R"SRC(
        module __app_stdio_native_lifetime__;
        import k;

        @ffi::Extern("C") private __k_io_file_get_stdin() : io::file::CFile*;
        @ffi::Extern("C") private __k_io_file_get_stdout() : io::file::CFile*;
        @ffi::Extern("C") private __k_io_file_get_stderr() : io::file::CFile*;
        @ffi::Extern("C") private fileno(stream: io::file::CFile*) : int;
        @ffi::Extern("C") private fcntl(fd: int, command: int) : int;

        main() : int {
            io::stdinBytes().close();
            io::stdoutBytes().close();
            io::stderrBytes().close();
            if (fcntl(fileno(__k_io_file_get_stdin()), 1) < 0) return 1;
            if (fcntl(fileno(__k_io_file_get_stdout()), 1) < 0) return 2;
            if (fcntl(fileno(__k_io_file_get_stderr()), 1) < 0) return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    static const int argc = 1;
    static const char* argv[] = {"prog", nullptr};
    auto main = jit->lookup_main_entry_symbol<int(*)(int, char**)>();
    CHECK(main(argc, (char**)argv) == 0);
    CHECK(main(argc, (char**)argv) == 0);
}

TEST_CASE("Application standard streams — owned file roots close after adapter teardown",
          "[libk][application][stdio][lifecycle]") {
    const std::string path = "application-owned-output-test.bin";
    std::filesystem::remove(path);
    auto jit = jit_k(R"SRC(
        module __app_stdio_owned_file__;
        import k;

        capturedFd : int = -1;
        @ffi::Extern("C") private close(fd: int) : int;

        class TrackedOutput : public k::io::file::FileOutputStream {
            public TrackedOutput(path: const char[])
                : FileOutputStream(path) {
                capturedFd = getFD().getFd();
            }
        }

        class Application : public k::Application {
            public Application() {
                setStandardOutput(new TrackedOutput("application-owned-output-test.bin"));
            }

            public main() : int {
                k::io::stdout().print("owned");
                return 0;
            }
        }
    )SRC");
    REQUIRE(jit);

    auto run = jit->run_main();
    REQUIRE(run.has_value());
    CHECK_FALSE(run->uncaught_exception);
    CHECK(run->status == 0);
    auto fd = jit->lookup_symbol<int*>("capturedFd");
    REQUIRE(fd);
    REQUIRE(*fd >= 0);
    CHECK(::close(*fd) == -1);
    std::filesystem::remove(path);
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. ::k::Application::env() — process environment variable access
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application — env() exposes process environment variables", "[libk][application]") {
    setenv("KLANG_TEST_APP_ENV_VAR", "hello_from_env", 1);

    auto jit = jit_k(R"SRC(
        module __app_env__;
        import k;

        found : bool = false;

        main() {
            e : const EnvironmentMap& = env();
            v : OptionalConstRef<String> = e.get(String("KLANG_TEST_APP_ENV_VAR"));
            if (v.hasValue()) {
                if (v.get() == String("hello_from_env")) {
                    found = true;
                }
            }
        }
        )SRC");
    REQUIRE(jit);

    static const int argc = 1;
    static const char* argv[] = {"prog", nullptr};

    auto main = jit->lookup_main_entry_symbol< int(*)(int, char**) >();
    auto res = main(argc, (char**)argv);
    REQUIRE(res == 0);

    auto found = jit->lookup_symbol< bool* >("found");
    REQUIRE(*found == true);

    unsetenv("KLANG_TEST_APP_ENV_VAR");
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. Phase 4 — abstract ::k::Application chain: single abstract standard main
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application chain — single abstract standard main is implemented by the final class",
          "[libk][application][application-chain]") {
    auto jit = jit_k(R"SRC(
        module __app_chain_single__;
        import k;

        abstract class Layer1 : public k::Application {
            public main() : int -> delete;
            public main(args : const String[]) -> delete;
            public main(args : const String[]) : int -> delete;
            public abstract main() : int;
        }

        class Application : public Layer1 {
            public main() : int {
                return 42;
            }
        }
        )SRC");
    REQUIRE(jit);

    static const int argc = 1;
    static const char* argv[] = {"prog", nullptr};
    auto main = jit->lookup_main_entry_symbol< int(*)(int, char**) >();
    REQUIRE(main(argc, (char**)argv) == 42);
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. Phase 4 — delegating standard main + custom abstract main (single level)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application chain — delegating implementation invokes a custom abstract main",
          "[libk][application][application-chain]") {
    auto jit = jit_k(R"SRC(
        module __app_chain_delegate__;
        import k;

        abstract class Layer1 : public k::Application {
            public main() -> delete;
            public main() : int -> delete;
            public main(args : const String[]) : int -> delete;
            public main(args : const String[]) : int {
                return main(args.size);
            }
            protected abstract main(n : int) : int;
        }

        class Application : public Layer1 {
            protected main(n : int) : int {
                return 100 + n;
            }
        }
        )SRC");
    REQUIRE(jit);

    static const int argc = 3;
    static const char* argv[] = {"prog", "a", "b", nullptr};
    auto main = jit->lookup_main_entry_symbol< int(*)(int, char**) >();
    REQUIRE(main(argc, (char**)argv) == 103);
}

// ═════════════════════════════════════════════════════════════════════════════
// 8. Phase 4 — multi-level abstract chain (2 abstract classes)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application chain — two-level abstract chain dispatches virtually end to end",
          "[libk][application][application-chain]") {
    auto jit = jit_k(R"SRC(
        module __app_chain_multilevel__;
        import k;

        abstract class Layer1 : public k::Application {
            public main() : int -> delete;
            public main(args : const String[]) -> delete;
            public main(args : const String[]) : int -> delete;
            public abstract main() : int;
        }

        abstract class Layer2 : public Layer1 {
            public main() : int {
                return main(1) + 1;
            }
            protected abstract main(code : int) : int;
        }

        class Application : public Layer2 {
            protected main(code : int) : int {
                return 97 + code;
            }
        }
        )SRC");
    REQUIRE(jit);

    static const int argc = 1;
    static const char* argv[] = {"prog", nullptr};
    auto main = jit->lookup_main_entry_symbol< int(*)(int, char**) >();
    REQUIRE(main(argc, (char**)argv) == 99);
}

// ═════════════════════════════════════════════════════════════════════════════
// 9. Phase 4 — validation errors
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application chain — all four standard mains deleted without a custom delegate is an error",
          "[libk][application][application-chain]") {
    auto jit = jit_k(R"SRC(
        module __app_chain_err_all_deleted__;
        import k;

        abstract class Layer1 : public k::Application {
            public main() -> delete;
            public main() : int -> delete;
            public main(args : const String[]) -> delete;
            public main(args : const String[]) : int -> delete;
        }

        class Application : public Layer1 {
            public main() {
            }
        }
        )SRC");
    REQUIRE_FALSE(jit);
}

TEST_CASE("Application chain — delegating main without a paired custom abstract main is an error",
          "[libk][application][application-chain]") {
    auto jit = jit_k(R"SRC(
        module __app_chain_err_no_delegate__;
        import k;

        abstract class Layer1 : public k::Application {
            public main() -> delete;
            public main() : int -> delete;
            public main(args : const String[]) : int -> delete;
            public main(args : const String[]) {
            }
        }

        class Application : public Layer1 {
        }
        )SRC");
    REQUIRE_FALSE(jit);
}

TEST_CASE("Application chain — final concrete class not implementing the required main is an error",
          "[libk][application][application-chain]") {
    auto jit = jit_k(R"SRC(
        module __app_chain_err_final_missing__;
        import k;

        abstract class Layer1 : public k::Application {
            public main() : int -> delete;
            public main(args : const String[]) -> delete;
            public main(args : const String[]) : int -> delete;
            public abstract main() : int;
        }

        class Application : public Layer1 {
            public main() : int -> delete;
        }
        )SRC");
    REQUIRE_FALSE(jit);
}
