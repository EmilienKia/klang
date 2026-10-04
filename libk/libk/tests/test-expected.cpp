/*
 * K Language standard library — Expected<R,E> tests
 *
 * Copyright 2023-2026 Emilien Kia
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
 * Tests for ::k::Expected<R,E>.
 *
 * These tests exercise the static factory methods (expected(), unexpected(),
 * error()) and the copy constructor of Expected<R,E> by JIT-compiling small
 * K programs that use the stdlib type.
 *
 * The base standard library (module "k") is implicitly imported by the
 * compiler — no explicit "import k;" is needed in the K sources.
 */

#include <catch2/catch_all.hpp>
#include <cstdlib>
#include "helpers.hpp"

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined — check CMakeLists.txt"
#endif

namespace {

/// Shorthand: compile K source that uses the base stdlib and JIT it.
std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src, bool dump = false) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR, dump);
}

} // anonymous namespace

TEST_CASE("Expected: empty IoDone retains its tag through copy getters and owned completion",
          "[libk][expected][io-done-result][copy][regression]") {
    auto j = jit_k(R"SRC(
        module __expected_io_done_tag__;
        observed : int = -2;
        test() : int {
            empty : Expected<k::io::IoDone, k::io::IoError>;
            copiedEmpty : Expected<k::io::IoDone, k::io::IoError>(empty);
            if (empty.hasResult() || empty.hasError() ||
                copiedEmpty.hasResult() || copiedEmpty.hasError()) return -1;
            done : k::io::IoDone;
            empty.setResult(done);
            if (!empty.hasResult() || empty.hasError()) return -2;
            copy : Expected<k::io::IoDone, k::io::IoError>(empty);
            if (!copy.hasResult() || copy.hasError()) return -3;
            value : k::io::IoDone = copy.getResult();
            fromGetter : Expected<k::io::IoDone, k::io::IoError>;
            fromGetter.setResult(value);
            if (!fromGetter.hasResult() || fromGetter.hasError()) return -4;
            completion : k::io::IoCompletion<k::io::IoDone> =
                [](r: k::io::IoResult<k::io::IoDone>) {
                    observed = r.hasResult() ? 1 : (r.hasError() ? 0 : -1);
                    if (observed != 1) return;
                    copied : k::io::IoResult<k::io::IoDone>(r);
                    if (!copied.hasResult() || copied.hasError()) { observed = -3; return; }
                    value : k::io::IoDone = copied.getResult();
                    copied.setResult(value);
                    if (!copied.hasResult() || copied.hasError()) observed = -4;
                };
            completion(fromGetter);
            if (observed != 1 || !fromGetter.hasResult() || fromGetter.hasError()) return -5;
            error : k::io::IoError(k::io::IoErrorKind::Closed, 9, 3L);
            copy.setError(error);
            if (!copy.hasError() || copy.hasResult()) return -6;
            copiedError : Expected<k::io::IoDone, k::io::IoError>(copy);
            if (!copiedError.hasError() || copiedError.hasResult()) return -7;
            errorValue : k::io::IoError = copiedError.getError();
            if (errorValue.kind() != k::io::IoErrorKind::Closed ||
                errorValue.getErrno() != 9 || errorValue.bytesTransferred() != 3L) return -8;
            copiedError.setResult(value);
            if (!copiedError.hasResult() || copiedError.hasError()) return -9;
            copy.setResult(done);
            copy.setResult(done);
            if (!copy.hasResult() || copy.hasError()) return -10;
            bothEmpty : Expected<k::io::IoDone, k::io::IoDone>;
            bothEmpty.setError(done);
            if (!bothEmpty.hasError() || bothEmpty.hasResult()) return -11;
            bothEmpty.setResult(done);
            return bothEmpty.hasResult() && !bothEmpty.hasError() ? 1 : -12;
        }
        callbackState() : int { return observed; }
    )SRC", std::getenv("KLANG_F1_DUMP_IR") != nullptr);
    REQUIRE(j);
    auto test = j->lookup_symbol<int (*)()>("test");
    auto callbackState = j->lookup_symbol<int (*)()>("callbackState");
    REQUIRE(test);
    REQUIRE(callbackState);
    int result = 0;
    REQUIRE_NOTHROW(result = test());
    INFO("Expected IoDone failed boundary=" << result << " callback state=" << callbackState());
    CHECK(result == 1);
}

TEST_CASE("Expected: class values construct before assignment and release only live alternatives",
          "[libk][expected][copy][lifecycle][regression]") {
    auto j = jit_k(R"SRC(
        module __expected_class_lifecycle__;
        copies : int = 0;
        assignments : int = 0;
        drops : int = 0;
        invalid : int = 0;
        failedStage : int = 0;
        failedMask : int = 0;
        failedResultRefs : int = 0;
        failedErrorRefs : int = 0;
        failedResultReleases : int = 0;
        failedErrorReleases : int = 0;
        failedCopies : int = 0;
        failedAssignments : int = 0;
        failedDrops : int = 0;
        failedInvalid : int = 0;
        failureStage() : int { return failedStage; }
        failureMask() : int { return failedMask; }
        resultRefsAtFailure() : int { return failedResultRefs; }
        errorRefsAtFailure() : int { return failedErrorRefs; }
        resultReleasesAtFailure() : int { return failedResultReleases; }
        errorReleasesAtFailure() : int { return failedErrorReleases; }
        copiesAtFailure() : int { return failedCopies; }
        assignmentsAtFailure() : int { return failedAssignments; }
        dropsAtFailure() : int { return failedDrops; }
        invalidAtFailure() : int { return failedInvalid; }
        struct State {
            refs : int = 0;
            releases : int = 0;
        }
        record(stage : int, valid : bool, result : State&, error : State&) {
            if (valid) return;
            failedMask |= 1 << (stage - 1);
            if (failedStage != 0) return;
            failedStage = stage;
            failedResultRefs = result.refs;
            failedErrorRefs = error.refs;
            failedResultReleases = result.releases;
            failedErrorReleases = error.releases;
            failedCopies = copies;
            failedAssignments = assignments;
            failedDrops = drops;
            failedInvalid = invalid;
        }
        class Resource {
            state : State* = null;
            magic : int = 73;
            Resource(value : State*) : state(value) { ++state->refs; }
            Resource(other : const Resource&) : state(other.state) {
                if (other.magic != 73) ++invalid;
                ++copies;
                ++state->refs;
            }
            operator=(other : const Resource&) : Resource& {
                if (magic != 73 || other.magic != 73) {
                    ++invalid;
                    return this;
                }
                ++assignments;
                next : State* = other.state;
                ++next->refs;
                --state->refs;
                ++state->releases;
                state = next;
                return this;
            }
            ~Resource() {
                if (magic != 73) { ++invalid; return; }
                --state->refs;
                ++state->releases;
                magic = 0;
                ++drops;
            }
            const valid() : bool { return magic == 73 && state->refs > 0; }
        }
        test() : int {
            copies = 0; assignments = 0; drops = 0; invalid = 0; failedStage = 0;
            failedMask = 0;
            resultState : State;
            errorState : State;
            valid : bool = true;
            {
                result : Resource(&resultState);
                error : Resource(&errorState);
                {
                    empty : Expected<Resource, Resource>;
                    copiedEmpty : Expected<Resource, Resource>(empty);
                    valid = valid && !empty.hasResult() && !empty.hasError();
                    valid = valid && !copiedEmpty.hasResult() && !copiedEmpty.hasError();
                    record(1, !empty.hasResult() && !empty.hasError() &&
                        !copiedEmpty.hasResult() && !copiedEmpty.hasError(), resultState, errorState);
                }
                valid = valid && drops == 0 && copies == 0 && assignments == 0;
                record(1, drops == 0 && copies == 0 && assignments == 0, resultState, errorState);
                {
                    expected : Expected<Resource, Resource>;
                    expected.setResult(result);
                    valid = valid && expected.hasResult() && !expected.hasError();
                    valid = valid && resultState.refs == 2 && copies == 1 && assignments == 0;
                    record(2, expected.hasResult() && !expected.hasError() &&
                        resultState.refs == 2 && copies == 1 && assignments == 0, resultState, errorState);
                    expected.setResult(error);
                    valid = valid && resultState.refs == 1 && errorState.refs == 2;
                    valid = valid && resultState.releases == 1 && assignments == 1 && drops == 0;
                    record(3, resultState.refs == 1 && errorState.refs == 2 &&
                        resultState.releases == 1 && assignments == 1 && drops == 0, resultState, errorState);
                    expected.setError(result);
                    valid = valid && expected.hasError() && !expected.hasResult();
                    valid = valid && resultState.refs == 2 && errorState.refs == 1;
                    valid = valid && errorState.releases == 1 && drops == 1 && copies == 2;
                    record(4, expected.hasError() && !expected.hasResult() &&
                        resultState.refs == 2 && errorState.refs == 1 &&
                        errorState.releases == 1 && drops == 1 && copies == 2, resultState, errorState);
                    expected.setError(error);
                    valid = valid && resultState.refs == 1 && errorState.refs == 2 && assignments == 2;
                    record(5, resultState.refs == 1 && errorState.refs == 2 && assignments == 2,
                        resultState, errorState);
                    expected.setResult(result);
                    valid = valid && resultState.refs == 2 && errorState.refs == 1;
                    valid = valid && errorState.releases == 2 && drops == 2 && copies == 3;
                    record(6, resultState.refs == 2 && errorState.refs == 1 &&
                        errorState.releases == 2 && drops == 2 && copies == 3, resultState, errorState);
                    {
                        value : Resource = expected.getResult();
                        valid = valid && value.valid() && resultState.refs == 3;
                        record(7, value.valid() && resultState.refs == 3, resultState, errorState);
                    }
                    valid = valid && resultState.refs == 2;
                    record(8, resultState.refs == 2, resultState, errorState);
                    {
                        copy : Expected<Resource, Resource>(expected);
                        valid = valid && copy.hasResult() && !copy.hasError() && resultState.refs == 3;
                        record(9, copy.hasResult() && !copy.hasError() && resultState.refs == 3,
                            resultState, errorState);
                        value : Resource = copy.getResult();
                        valid = valid && value.valid() && resultState.refs == 4;
                        record(10, value.valid() && resultState.refs == 4, resultState, errorState);
                    }
                    valid = valid && resultState.refs == 2 && errorState.refs == 1;
                    record(11, resultState.refs == 2 && errorState.refs == 1, resultState, errorState);
                }
                valid = valid && resultState.refs == 1 && errorState.refs == 1;
                record(12, resultState.refs == 1 && errorState.refs == 1, resultState, errorState);
            }
            record(13, resultState.refs == 0 && errorState.refs == 0 && invalid == 0,
                resultState, errorState);
            return valid && resultState.refs == 0 && errorState.refs == 0 && invalid == 0 ? 1 : 0;
        }
    )SRC", std::getenv("KLANG_F1_DUMP_IR") != nullptr);
    REQUIRE(j);
    auto fn = j->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    int result = 0;
    REQUIRE_NOTHROW(result = fn());
    auto diagnostic = [&](const char* name) {
        auto getter = j->lookup_symbol<int (*)()>(name);
        REQUIRE(getter);
        return getter();
    };
    INFO("Expected first failing stage=" << diagnostic("failureStage")
         << " failure mask=" << diagnostic("failureMask")
         << " refs(result/error)=" << diagnostic("resultRefsAtFailure") << "/"
         << diagnostic("errorRefsAtFailure")
         << " releases(result/error)=" << diagnostic("resultReleasesAtFailure") << "/"
         << diagnostic("errorReleasesAtFailure")
         << " copies=" << diagnostic("copiesAtFailure")
         << " assignments=" << diagnostic("assignmentsAtFailure")
         << " drops=" << diagnostic("dropsAtFailure")
         << " invalid=" << diagnostic("invalidAtFailure"));
    CHECK(result == 1);
}


TEST_CASE("Expected<Shared<T>> — body-local specialization releases its active result",
          "[libk][expected][shared][late-instantiation][regression]") {
    auto j = jit_k(R"SRC(
        module __expected_shared_body_local__;
        class Payload {}
        test() : int {
            owner : Shared<Payload>(new Payload());
            if (owner.useCount() != 1) return 1;
            {
                result : Expected<Shared<Payload>, int>;
                result.setResult(owner);
                if (owner.useCount() != 2) return 10 + owner.useCount();
                {
                    copy : Shared<Payload> = result.getResult();
                    if (owner.useCount() != 3) return 20 + owner.useCount();
                }
                if (owner.useCount() != 2) return 30 + owner.useCount();
            }
            if (owner.useCount() != 1) return 40 + owner.useCount();
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto test = j->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Expected<Shared<T>> — copies retain and union members release ownership",
          "[libk][expected][shared][regression]") {
    auto j = jit_k(R"SRC(
        module __expected_shared_lifetime__;
        destroyed : int = 0;
        class Payload {
            ~Payload() { ++destroyed; }
        }
        wrap(owner : Shared<Payload>&, success : bool) : Expected<Shared<Payload>, int> {
            if (!success) {
                failure : Expected<Shared<Payload>, int>;
                code : int = 7;
                failure.setError(code);
                return failure;
            }
            result : Expected<Shared<Payload>, int>;
            result.setResult(owner);
            return result;
        }
        test() : int {
            destroyed = 0;
            weak : Weak<Payload>;
            {
                owner : Shared<Payload>(new Payload());
                weak = Weak<Payload>(owner);
                if (owner.useCount() != 1) return 1;
                {
                    result : Expected<Shared<Payload>, int>;
                    result.setResult(owner);
                    if (owner.useCount() != 2) return 2;
                    {
                        copy : Shared<Payload> = result.getResult();
                        if (owner.useCount() != 3) return 3;
                    }
                    if (owner.useCount() != 2) return 4;
                    {
                        copy : Expected<Shared<Payload>, int>(result);
                        if (owner.useCount() != 3) return 5;
                    }
                    if (owner.useCount() != 2) return 6;
                }
                if (owner.useCount() != 1) return 7;
                {
                    result : Expected<Shared<Payload>, int> = wrap(owner, true);
                    if (!result.hasResult()) return 8;
                    if (owner.useCount() != 2) return 9;
                }
                if (owner.useCount() != 1) return 10;
                {
                    result : Expected<Shared<Payload>, int> = wrap(owner, false);
                    if (!result.hasError() || result.getError() != 7) return 11;
                }
                if (owner.useCount() != 1 || destroyed != 0) return 12;
            }
            if (destroyed != 1 || !weak.expired()) return 13;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Expected<Shared<T>> — returned results and comparison temporaries have distinct lifetimes",
          "[libk][expected][shared][return-count][regression]") {
    auto j = jit_k(R"SRC(
        module __expected_shared_return_count__;
        class Payload {}
        wrap(owner : Shared<Payload>&) : Expected<Shared<Payload>, int> {
            result : Expected<Shared<Payload>, int>;
            result.setResult(owner);
            return result;
        }
        test() : int {
            owner : Shared<Payload>(new Payload());
            before : int = owner.useCount();
            {
                result : Expected<Shared<Payload>, int> = wrap(owner);
                if (owner.useCount() != before + 1) return 100 + owner.useCount();
                if (result.getResult() != owner) return 2;
                if (owner.useCount() != before + 1) return 200 + owner.useCount();
            }
            if (owner.useCount() != before) return 40 + owner.useCount();
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto test = j->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Static factory methods — direct call syntax
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Expected::expected() — static factory for result", "[libk][expected][factory]") {
    auto j = jit_k(R"SRC(
        module __expected_factory_result__;
        test() : int {
            e : Expected<int, int> = Expected<int, int>::expected(42);
            if (!e.hasResult()) return 0;
            if (e.getResult() != 42) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("Expected::unexpected() — static factory for error", "[libk][expected][factory]") {
    auto j = jit_k(R"SRC(
        module __expected_factory_unexpected__;
        test() : int {
            e : Expected<int, int> = Expected<int, int>::unexpected(-7);
            if (!e.hasError()) return 0;
            if (e.getError() != -7) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("Expected::error() — alias for unexpected()", "[libk][expected][factory]") {
    auto j = jit_k(R"SRC(
        module __expected_factory_error__;
        test() : int {
            e : Expected<int, int> = Expected<int, int>::error(-99);
            if (!e.hasError()) return 0;
            if (e.getError() != -99) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("k::Expected::expected() — namespace-qualified static factory", "[libk][expected][factory]") {
    auto j = jit_k(R"SRC(
        module __expected_factory_ns__;
        test() : int {
            e : k::Expected<int, int> = k::Expected<int, int>::expected(42);
            if (!e.hasResult()) return 0;
            if (e.getResult() != 42) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("::k::Expected::expected() — absolute-prefix static factory", "[libk][expected][factory]") {
    auto j = jit_k(R"SRC(
        module __expected_factory_abs__;
        test() : int {
            e : ::k::Expected<int, int> = ::k::Expected<int, int>::expected(42);
            if (!e.hasResult()) return 0;
            if (e.getResult() != 42) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("::k::Expected::unexpected() — absolute-prefix static factory", "[libk][expected][factory]") {
    auto j = jit_k(R"SRC(
        module __expected_factory_abs_unexpected__;
        test() : int {
            e : ::k::Expected<int, int> = ::k::Expected<int, int>::unexpected(-7);
            if (!e.hasError()) return 0;
            if (e.getError() != -7) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("::k::Expected::error() — absolute-prefix alias", "[libk][expected][factory]") {
    auto j = jit_k(R"SRC(
        module __expected_factory_abs_error__;
        test() : int {
            e : ::k::Expected<int, int> = ::k::Expected<int, int>::error(-99);
            if (!e.hasError()) return 0;
            if (e.getError() != -99) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Copy constructor — copy of a result-holding Expected
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Expected copy constructor — copy result", "[libk][expected][copy]") {
    auto j = jit_k(R"SRC(
        module __expected_copy_result__;
        test() : int {
            src : Expected<int, int> = Expected<int, int>::expected(42);
            dst : Expected<int, int>(src);
            if (!dst.hasResult()) return 0;
            if (dst.getResult() != 42) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Copy constructor — copy of an error-holding Expected
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Expected copy constructor — copy error", "[libk][expected][copy]") {
    auto j = jit_k(R"SRC(
        module __expected_copy_error__;
        test() : int {
            src : Expected<int, int> = Expected<int, int>::unexpected(-1);
            dst : Expected<int, int>(src);
            if (!dst.hasError()) return 0;
            if (dst.getError() != -1) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Copy constructor — copy does not share state with original (result)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Expected copy constructor — modifying copy does not affect original (result)", "[libk][expected][copy]") {
    auto j = jit_k(R"SRC(
        module __expected_copy_independence_result__;
        test() : int {
            src : Expected<int, int> = Expected<int, int>::expected(10);
            dst : Expected<int, int>(src);
            // Overwrite dst with an error; src must keep its result
            dst.setError(99);
            result : int = 0;
            if (src.hasResult()) { ++result; }
            if (src.getResult() == 10) { result += 10; }
            if (dst.hasError()) { result += 100; }
            if (dst.getError() == 99) { result += 1000; }
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Copy constructor — copy does not share state with original (error)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Expected copy constructor — modifying copy does not affect original (error)", "[libk][expected][copy]") {
    auto j = jit_k(R"SRC(
        module __expected_copy_independence_error__;
        test() : int {
            src : Expected<int, int> = Expected<int, int>::error(-5);
            dst : Expected<int, int>(src);
            // Overwrite dst with a result; src must keep its error
            dst.setResult(77);
            result : int = 0;
            if (src.hasError()) { ++result; }
            if (src.getError() == -5) { result += 10; }
            if (dst.hasResult()) { result += 100; }
            if (dst.getResult() == 77) { result += 1000; }
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Copy constructor — chained copies preserve state
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Expected copy constructor — chained copies preserve result", "[libk][expected][copy]") {
    auto j = jit_k(R"SRC(
        module __expected_copy_chain__;
        test() : int {
            a : Expected<int, int> = ::k::Expected<int, int>::expected(7);
            b : Expected<int, int>(a);
            c : Expected<int, int>(b);
            result : int = 0;
            if (a.hasResult() && a.getResult() == 7) { ++result; }
            if (b.hasResult() && b.getResult() == 7) { result += 10; }
            if (c.hasResult() && c.getResult() == 7) { result += 100; }
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Regression: static factory call with a root-prefixed IMPORTED enum arg
// ═══════════════════════════════════════════════════════════════════════════════
//
// A qualified static factory call on a template instantiation whose template
// argument is a ROOT-PREFIXED imported enum — e.g.
//     Expected<unsigned int, ::k::io::StreamOutOfData>::expected(x)
// — used to fail with diagnostic 000E5 ("No viable overload found for
// 'expected'"). Root cause: aggregate_type_resolver::resolve_type_from_root
// lacked the imported-enum fallback that type_reference_resolver already had, so
// try_instantiate_template_type() returned {} for the qualifier type. The
// qualified static-call candidate collection then fell back to the
// un-instantiated template method whose R& parameter could not match the
// argument. This blocked the whole Expected-based k::io stream API (array_stream.k
// etc. use `Expected<unsigned int, ::k::io::StreamOutOfData>::expected(...)`
// unqualified via the implicit `import k`). Fixed by adding the
// get_or_create_imported_enum fallback to
// aggregate_type_resolver::resolve_type_from_root.
TEST_CASE("Expected static factory with root-prefixed imported enum arg",
          "[libk][expected][factory][import-enum]") {
    auto j = jit_k(R"SRC(
        module __expected_rootprefixed_enum__;
        test() : int {
            n : unsigned int = 5;
            // Unqualified type name (reached via implicit import k) + root-prefixed
            // imported enum template argument.
            e : Expected<unsigned int, ::k::io::StreamOutOfData> =
                Expected<unsigned int, ::k::io::StreamOutOfData>::expected(n);
            if (!e.hasResult()) return 0;
            return (int) e.getResultOr((unsigned int) 0);
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 5);
}
