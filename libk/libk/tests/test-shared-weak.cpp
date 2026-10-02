/*
 * K Language standard library — Shared/Weak lifecycle and concurrency tests
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

#include <catch2/catch_all.hpp>
#include "helpers.hpp"

#include <atomic>
#include <cstdint>
#include <dlfcn.h>
#include <memory>
#include <string>
#include <thread>

namespace {

std::unique_ptr<k::model::gen::jit> jit_shared(
        std::string_view name, std::string_view body) {
    std::string source = "module __shared_weak_" + std::string(name) + "__;\n";
    source += R"SRC(
        destroyed : int = 0;
        class Box {
            public value : int;
            Box(v : int) : value(v) {}
            ~Box() { ++destroyed; }
        }
    )SRC";
    source += body;
    return gen_jit_with_stdlib(source, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

template<typename T>
T shared_symbol(void* library, const char* name) {
    dlerror();
    void* symbol = dlsym(library, name);
    const char* error = dlerror();
    INFO(name);
    INFO((error ? error : ""));
    REQUIRE(symbol);
    return reinterpret_cast<T>(symbol);
}

} // namespace

TEST_CASE("Atomic shared counters balance concurrent copies and never resurrect",
          "[libk][shared][weak][native][thread]") {
    const std::string path = std::string(LIBK_LIB_DIR) + "/libk.so";
    std::unique_ptr<void, decltype(&dlclose)> library(
        dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL), &dlclose);
    REQUIRE(library);
    auto count = shared_symbol<int32_t(*)(const int32_t*)>(
        library.get(), "__k_shared_count");
    auto retain = shared_symbol<void(*)(int32_t*)>(
        library.get(), "__k_shared_retain");
    auto tryRetain = shared_symbol<bool(*)(int32_t*)>(
        library.get(), "__k_shared_try_retain");
    auto release = shared_symbol<bool(*)(int32_t*)>(
        library.get(), "__k_shared_release");

    SECTION("Concurrent copies preserve the original reference") {
        int32_t strong = 1;
        std::atomic<int> finalReleases{0};
        auto worker = [&] {
            for (int i = 0; i < 2000; ++i) {
                retain(&strong);
                if (release(&strong)) {
                    finalReleases.fetch_add(1);
                }
            }
        };
        std::thread first(worker);
        std::thread second(worker);
        first.join();
        second.join();
        CHECK(finalReleases.load() == 0);
        CHECK(count(&strong) == 1);
        CHECK(release(&strong));
        CHECK(count(&strong) == 0);
        CHECK_FALSE(tryRetain(&strong));
    }

    SECTION("Promotion and last release have exactly one final releaser") {
        for (int round = 0; round < 20; ++round) {
            int32_t strong = 1;
            std::atomic<int> ready{0};
            std::atomic<bool> start{false};
            std::atomic<int> finalReleases{0};
            auto worker = [&] {
                ready.fetch_add(1);
                while (!start.load()) {
                    std::this_thread::yield();
                }
                for (int i = 0; i < 2000; ++i) {
                    if (tryRetain(&strong) && release(&strong)) {
                        finalReleases.fetch_add(1);
                    }
                }
            };
            std::thread first(worker);
            std::thread second(worker);
            while (ready.load() != 2) {
                std::this_thread::yield();
            }
            start.store(true);
            if (release(&strong)) {
                finalReleases.fetch_add(1);
            }
            first.join();
            second.join();
            CHECK(finalReleases.load() == 1);
            CHECK(count(&strong) == 0);
            CHECK_FALSE(tryRetain(&strong));
        }
    }
}

TEST_CASE("Shared ownership metadata cannot be constructed by callers",
          "[libk][shared][visibility]") {
    auto j = jit_shared("private_metadata", R"SRC(
        test() : int {
            block : SharedBlock<Box>(new Box(42));
            return 0;
        }
    )SRC");
    CHECK_FALSE(j);
}

TEST_CASE("Shared copies preserve ownership through values and collections",
          "[libk][shared][copy]") {
    auto j = jit_shared("copies", R"SRC(
        echo(value : Shared<Box>) : Shared<Box> {
            return value;
        }
        test() : int {
            original : Shared<Box>(new Box(42));
            source : const Shared<Box>& = original;
            {
                copy : Shared<Box>(source);
                if (original.useCount() != 2) return 1;
                if (copy.get() != original.get()) return 2;
                assigned : Shared<Box>;
                assigned = source;
                if (source.useCount() != 3) return 3;
                returned : Shared<Box> = echo(copy);
                if (returned.get()->value != 42) return 4;
                if (original.useCount() != 4) return 5;
                values : Vector<Shared<Box>>;
                values.append(original);
                values.append(copy);
                if (original.useCount() != 6) return 6;
                values.clear();
                if (original.useCount() != 4) return 7;
                assigned = assigned;
                copy.share(copy);
                if (original.useCount() != 4) return 8;
            }
            if (original.useCount() != 1) return 9;
            if (destroyed != 0) return 10;
            original.reset();
            if (destroyed != 1) return 11;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto test = j->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Shared drains transfer handles including same-group assignment",
          "[libk][shared][drain]") {
    auto j = jit_shared("drains", R"SRC(
        test() : int {
            original : Shared<Box>(new Box(42));
            sibling : Shared<Box>(original);
            moved : Shared<Box>(#original);
            if (!original.isNull()) return 1;
            if (moved.useCount() != 2) return 2;
            moved = #moved;
            if (moved.useCount() != 2) return 3;
            moved = #sibling;
            if (!sibling.isNull()) return 4;
            if (moved.useCount() != 1) return 5;
            destination : Shared<Box>(new Box(17));
            destination = #moved;
            if (destroyed != 1) return 6;
            if (!moved.isNull()) return 7;
            if (destination.get()->value != 42) return 8;
            empty : Shared<Box>;
            moved = #empty;
            if (!moved.isNull()) return 9;
            destination.reset();
            if (destroyed != 2) return 10;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto test = j->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Shared null replacement and swaps preserve exact destruction",
          "[libk][shared][null][swap]") {
    auto j = jit_shared("null_swap", R"SRC(
        test() : int {
            nullOwner : Box! = null;
            empty : Shared<Box>(nullOwner);
            if (empty.useCount() != 0) return 1;
            emptyCopy : Shared<Box>(empty);
            if (!emptyCopy.isNull()) return 13;
            first : Shared<Box>(new Box(1));
            second : Shared<Box>(new Box(2));
            first.swap(second);
            if (first.get()->value != 2) return 2;
            if (second.get()->value != 1) return 3;
            first.swap(first);
            if (first.useCount() != 1) return 4;
            first.swap(empty);
            if (!first.isNull()) return 5;
            if (empty.get()->value != 2) return 6;
            first.acquire(new Box(3));
            first.share(Shared<Box>());
            if (!first.isNull()) return 7;
            if (destroyed != 1) return 8;
            second = null;
            if (!second.isNull()) return 9;
            empty.reset(null);
            if (empty.useCount() != 0) return 10;
            if (destroyed != 3) return 11;
            first = new Box(4);
            first = first;
            first = emptyCopy;
            if (destroyed != 4) return 12;
            if (first.useCount() != 0) return 14;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto test = j->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Shared const boolean conversion retains builtin short circuit",
          "[libk][shared][bool]") {
    auto j = jit_shared("boolean", R"SRC(
        touched : int = 0;
        touch() : bool { ++touched; return true; }
        test() : int {
            owner : Shared<Box>(new Box(42));
            value : const Shared<Box>& = owner;
            empty : const Shared<Box>;
            if (!value) return 1;
            if (empty) return 2;
            converted : bool = value;
            if (!converted) return 3;
            if (empty && touch()) return 4;
            if (touched != 0) return 5;
            if (value || touch()) {
                if (touched != 0) return 6;
            } else { return 7; }
            if (value && touch()) {
                if (touched != 1) return 8;
            } else { return 9; }
            if (empty || touch()) {
                if (touched != 2) return 10;
            } else { return 11; }
            copy : Shared<Box>(value);
            if (copy != value) return 12;
            if (owner == empty) return 13;
            if (owner.useCount() != 2) return 14;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto test = j->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Weak promotion counts only strong handles and cannot resurrect",
          "[libk][weak][shared]") {
    auto j = jit_shared("promotion", R"SRC(
        test() : int {
            empty : Weak<Box>;
            if (!empty.expired()) return 1;
            if (empty.useCount() != 0) return 2;
            missing : Shared<Box> = empty.lock();
            if (missing) return 3;
            strong : Shared<Box>(new Box(42));
            weak : Weak<Box>(strong);
            view : const Weak<Box>& = weak;
            copy : Weak<Box>(view);
            if (strong.useCount() != 1) return 4;
            {
                locked : Shared<Box> = view.lock();
                if (!locked) return 5;
                if (locked.get() != strong.get()) return 6;
                if (weak.useCount() != 2) return 7;
                strong.reset();
                if (weak.expired()) return 8;
                if (locked.get()->value != 42) return 9;
            }
            if (destroyed != 1) return 10;
            if (!copy.expired()) return 11;
            expiredCopy : Weak<Box>(copy);
            expiredAssigned : Weak<Box>;
            expiredAssigned = expiredCopy;
            expiredMoved : Weak<Box>(#expiredAssigned);
            if (!expiredAssigned.expired()) return 15;
            if (!expiredMoved.expired()) return 16;
            strong.acquire(new Box(17));
            stillMissing : Shared<Box> = expiredCopy.lock();
            if (stillMissing) return 12;
            if (expiredCopy.useCount() != 0) return 13;
            weak.reset();
            copy.reset();
            expiredCopy.reset();
            expiredMoved.reset();
            strong.reset();
            if (destroyed != 2) return 14;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto test = j->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Weak copy drain assignment and swaps preserve observations",
          "[libk][weak][copy][drain][swap]") {
    auto j = jit_shared("weak_lifecycle", R"SRC(
        test() : int {
            first : Shared<Box>(new Box(1));
            second : Shared<Box>(new Box(2));
            original : Weak<Box>(first);
            copied : Weak<Box>;
            copied = original;
            copied = copied;
            moved : Weak<Box>(#copied);
            if (!copied.expired()) return 1;
            moved = #moved;
            copied = #moved;
            if (!moved.expired()) return 2;
            destination : Weak<Box>(second);
            destination = #copied;
            if (!copied.expired()) return 3;
            locked : Shared<Box> = destination.lock();
            if (locked.get()->value != 1) return 4;
            locked.reset();
            copied = second;
            destination.swap(copied);
            destination.swap(destination);
            locked = destination.lock();
            if (locked.get()->value != 2) return 5;
            locked.reset();
            original = #copied;
            if (!copied.expired()) return 11;
            if (original.useCount() != 1) return 12;
            empty : Weak<Box>;
            copied = empty;
            if (!copied.expired()) return 6;
            destination = Shared<Box>();
            if (!destination.expired()) return 7;
            if (first.useCount() != 1) return 8;
            if (second.useCount() != 1) return 9;
            first.reset();
            second.reset();
            original.reset();
            if (destroyed != 2) return 10;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto test = j->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Shared checked weak promotion throws only for missing ownership",
          "[libk][weak][shared][exceptions]") {
    auto j = jit_shared("checked_promotion", R"SRC(
        test() : int {
            source : Shared<Box>(new Box(42));
            weak : Weak<Box>(source);
            try {
                promoted : Shared<Box>(weak);
                if (promoted.get()->value != 42) return 1;
                if (source.useCount() != 2) return 2;
            } catch (e : BadWeakReferenceException&) {
                return 3;
            }
            source.reset();
            try {
                expired : Shared<Box>(weak);
                return 4;
            } catch (e : BadWeakReferenceException&) {
                if (e.getCode() != 5) return 5;
            }
            empty : Weak<Box>;
            try {
                absent : Shared<Box>(empty);
                return 6;
            } catch (e : BadWeakReferenceException&) {}
            if (destroyed != 1) return 7;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto test = j->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Weak self-links may release the last explicit weak during destruction",
          "[libk][weak][shared][destructor]") {
    auto j = jit_shared("reentrant", R"SRC(
        class Node {
            public back : Weak<Node>;
            ~Node() {
                if (!back.expired()) {
                    destroyed = 100;
                }
                locked : Shared<Node> = back.lock();
                if (locked) {
                    destroyed = 200;
                }
                back.reset();
                ++destroyed;
            }
        }
        test() : int {
            node : Shared<Node>(new Node());
            node.get()->back = node;
            node.reset();
            return destroyed;
        }
    )SRC");
    REQUIRE(j);
    auto test = j->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 1);
}

TEST_CASE("Shared replacement is installed before releasing reentrant destructors",
          "[libk][shared][destructor][copy][drain]") {
    auto j = jit_shared("reentrant_assignment", R"SRC(
        failures : int = 0;
        class Resetting {
            destination : Shared<Resetting>*;
            id : int;
            Resetting(d : Shared<Resetting>*, n : int)
                : destination(d), id(n) {}
            ~Resetting() {
                if (id == 1) {
                    if (destination->isNull()) {
                        ++failures;
                    }
                    destination->reset();
                }
                ++destroyed;
            }
        }
        test() : int {
            destination : Shared<Resetting>;
            destination.acquire(new Resetting(&destination, 1));
            replacement : Shared<Resetting>(new Resetting(&destination, 2));
            destination = replacement;
            if (destination) return 1;
            if (replacement.useCount() != 1) return 2;
            if (destroyed != 1) return 3;
            replacement.reset();
            if (destroyed != 2) return 4;
            destination.acquire(new Resetting(&destination, 1));
            replacement.acquire(new Resetting(&destination, 2));
            destination = #replacement;
            if (destination) return 5;
            if (replacement) return 6;
            if (destroyed != 4) return 7;
            return failures;
        }
    )SRC");
    REQUIRE(j);
    auto test = j->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Shared copies on distinct threads balance atomic ownership",
          "[libk][shared][thread]") {
    auto j = jit_shared("concurrent_copies", R"SRC(
        root : Shared<Box>;
        setup() { root.acquire(new Box(42)); }
        exercise() : int {
            i : int = 0;
            while (i < 2000) {
                first : Shared<Box>(root);
                second : Shared<Box>(first);
                moved : Shared<Box>(#second);
                first = moved;
                if (moved.get()->value != 42) return 1;
                first.reset();
                ++i;
            }
            return 0;
        }
        finish() : int {
            if (root.useCount() != 1) return -1;
            root.reset();
            return destroyed;
        }
    )SRC");
    REQUIRE(j);
    auto setup = j->lookup_symbol<void(*)()>("setup");
    auto exercise = j->lookup_symbol<int(*)()>("exercise");
    auto finish = j->lookup_symbol<int(*)()>("finish");
    REQUIRE(setup);
    REQUIRE(exercise);
    REQUIRE(finish);
    setup();
    std::atomic<int> errors{0};
    std::thread first([&] { errors.fetch_add(exercise()); });
    std::thread second([&] { errors.fetch_add(exercise()); });
    first.join();
    second.join();
    CHECK(errors.load() == 0);
    CHECK(finish() == 1);
}

TEST_CASE("Weak lock races safely with the last strong release",
          "[libk][weak][shared][thread]") {
    auto j = jit_shared("concurrent_lock", R"SRC(
        root : Shared<Box>;
        observer : Weak<Box>;
        setup() {
            root.acquire(new Box(42));
            observer = root;
        }
        promote() : Shared<Box>! {
            return new Shared<Box>(observer.lock());
        }
        drop(handle : Shared<Box>!) {
            delete handle;
        }
        strongCount() : int {
            return observer.useCount();
        }
        exercise() : int {
            local : Weak<Box>(observer);
            i : int = 0;
            while (i < 2000) {
                copy : Weak<Box>(local);
                acquired : Shared<Box> = copy.lock();
                if (acquired) {
                    if (acquired.get()->value != 42) return 1;
                }
                ++i;
            }
            return 0;
        }
        release() { root.reset(); }
        finish() : int {
            if (!observer.expired()) return -1;
            acquired : Shared<Box> = observer.lock();
            if (acquired) return -2;
            observer.reset();
            return destroyed;
        }
    )SRC");
    REQUIRE(j);
    auto setup = j->lookup_symbol<void(*)()>("setup");
    auto promote = j->lookup_symbol<void*(*)()>("promote");
    auto drop = j->lookup_symbol<void(*)(void*)>("drop");
    auto strongCount = j->lookup_symbol<int(*)()>("strongCount");
    auto exercise = j->lookup_symbol<int(*)()>("exercise");
    auto release = j->lookup_symbol<void(*)()>("release");
    auto finish = j->lookup_symbol<int(*)()>("finish");
    REQUIRE(setup);
    REQUIRE(promote);
    REQUIRE(drop);
    REQUIRE(strongCount);
    REQUIRE(exercise);
    REQUIRE(release);
    REQUIRE(finish);
    for (int round = 0; round < 20; ++round) {
        setup();
        std::atomic<int> ready{0};
        std::atomic<bool> start{false};
        std::atomic<int> errors{0};
        auto worker = [&] {
            void* handle = promote();
            ready.fetch_add(1);
            while (!start.load()) {
                std::this_thread::yield();
            }
            drop(handle);
            errors.fetch_add(exercise());
        };
        std::thread first(worker);
        std::thread second(worker);
        while (ready.load() != 2) {
            std::this_thread::yield();
        }
        CHECK(strongCount() == 3);
        release();
        CHECK(strongCount() == 2);
        start.store(true);
        first.join();
        second.join();
        CHECK(errors.load() == 0);
        CHECK(finish() == round + 1);
    }
}
