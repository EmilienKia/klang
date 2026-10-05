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
    REQUIRE(ensure_libk_loaded());
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

TEST_CASE("Shared converting views adjust secondary interfaces and preserve one owner",
          "[libk][shared][conversion][interface]") {
    auto j = jit_shared("converting_views", R"SRC(
        dualDestroyed : int = 0;
        otherDestroyed : int = 0;

        interface FirstFace {
            first() : int;
        }
        interface SecondFace {
            second() : int;
        }
        class DualFace : public FirstFace, public SecondFace {
            public DualFace() {}
            override first() : int { return 11; }
            override second() : int { return 22; }
            ~DualFace() { ++dualDestroyed; }
        }
        class OtherFace : public SecondFace {
            public OtherFace() {}
            override second() : int { return 99; }
            ~OtherFace() { ++otherDestroyed; }
        }
        callSecond(value : SecondFace&) : int { return value.second(); }

        test() : int {
            original : Shared<DualFace>(new DualFace());
            second : Shared<SecondFace>(original);
            if (original.useCount() != 2) return 1;
            if (callSecond(*second.get()) != 22) return 2;

            downcast : Shared<DualFace>(second);
            if (downcast.useCount() != 3) return 3;
            if (callSecond(*downcast.get()) != 22) return 4;
            downcast = second;
            if (!downcast) return 5;
            if (original.useCount() != 3) return 5;
            downcast.reset();
            if (original.useCount() != 2) return 6;

            mismatch : Shared<OtherFace>(new OtherFace());
            mismatch = second;
            if (mismatch) return 7;
            if (otherDestroyed != 1) return 8;
            if (original.useCount() != 2) return 9;

            failedDrainSource : Shared<SecondFace>(original);
            mismatch.acquire(new OtherFace());
            mismatch = #failedDrainSource;
            if (mismatch) return 10;
            if (failedDrainSource.useCount() != 3) return 11;
            if (otherDestroyed != 2) return 12;

            moved : Shared<DualFace>(#second);
            if (second) return 13;
            if (moved.useCount() != 3) return 14;
            if (callSecond(*moved.get()) != 22) return 15;
            moved.reset();
            failedDrainSource.reset();
            original.reset();
            if (dualDestroyed != 1) return 16;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto test = j->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Shared and Weak sibling-interface cross-casts retain the original owner",
          "[libk][shared][weak][conversion][interface]") {
    auto j = jit_shared("sibling_interface_crosscasts", R"SRC(
        dualDestroyed : int = 0;
        otherDestroyed : int = 0;
        interface FirstFacet {
            first() : int;
        }
        interface SecondFacet {
            second() : int;
        }
        class BothFacets : public FirstFacet, public SecondFacet {
            public BothFacets() {}
            override first() : int { return 11; }
            override second() : int { return 22; }
            ~BothFacets() { ++dualDestroyed; }
        }
        class FirstOnly : public FirstFacet {
            public FirstOnly() {}
            override first() : int { return 33; }
        }
        class OtherSecond : public SecondFacet {
            public OtherSecond() {}
            override second() : int { return 44; }
            ~OtherSecond() { ++otherDestroyed; }
        }
        getSecond(value : SecondFacet&) : int { return value.second(); }

        test() : int {
            root : Shared<BothFacets>(new BothFacets());
            first : Shared<FirstFacet>(root);
            second : Shared<SecondFacet>(first);
            secondAssigned : Shared<SecondFacet>;
            secondAssigned = first;
            if (!second || getSecond(*second.get()) != 22) return 1;
            if (!secondAssigned || getSecond(*secondAssigned.get()) != 22) return 23;
            if (root.useCount() != 4) return 2;

            weakFirst : Weak<FirstFacet>(first);
            weakSecond : Weak<SecondFacet>(weakFirst);
            if (root.useCount() != 4) return 3;
            locked : Shared<SecondFacet> = weakSecond.lock();
            if (!locked || getSecond(*locked.get()) != 22) return 4;
            if (root.useCount() != 5) return 5;
            locked.reset();

            drainSource : Shared<FirstFacet>(root);
            drainAssigned : Shared<SecondFacet>();
            drainAssigned = #drainSource;
            if (drainSource || getSecond(*drainAssigned.get()) != 22) return 14;
            drainAssigned.reset();

            weakAssignedSource : Weak<FirstFacet>(root);
            weakAssigned : Weak<SecondFacet>(weakSecond);
            weakAssigned = weakAssignedSource;
            weakAssignedLock : Shared<SecondFacet> = weakAssigned.lock();
            if (!weakAssignedLock || getSecond(*weakAssignedLock.get()) != 22) return 15;
            weakAssignedLock.reset();

            weakDrainSource : Weak<FirstFacet>(root);
            weakDrainAssigned : Weak<SecondFacet>();
            weakDrainAssigned = #weakDrainSource;
            if (weakDrainSource.useCount() != 0) return 16;
            weakDrainLock : Shared<SecondFacet> = weakDrainAssigned.lock();
            if (!weakDrainLock || getSecond(*weakDrainLock.get()) != 22) return 17;
            weakDrainLock.reset();
            weakDrainConstructSource : Weak<FirstFacet>(root);
            weakDrainConstruct : Weak<SecondFacet>(#weakDrainConstructSource);
            if (weakDrainConstructSource.useCount() != 0) return 20;
            weakDrainConstructLock : Shared<SecondFacet> = weakDrainConstruct.lock();
            if (!weakDrainConstructLock ||
                getSecond(*weakDrainConstructLock.get()) != 22) return 21;
            weakDrainConstructLock.reset();

            moved : Shared<SecondFacet>(#first);
            if (first || moved.useCount() != 4) return 6;
            second.reset();
            moved.reset();
            secondAssigned.reset();
            root.reset();
            if (dualDestroyed != 1) return 7;
            if (!weakSecond.expired()) return 8;

            only : Shared<FirstOnly>(new FirstOnly());
            onlyView : Shared<FirstFacet>(only);
            mismatch : Shared<SecondFacet>(onlyView);
            if (mismatch) return 9;
            failedDrainSource : Shared<FirstFacet>(only);
            rejectedDrain : Shared<SecondFacet>(#failedDrainSource);
            if (rejectedDrain || failedDrainSource.useCount() != 3) return 10;

            destination : Shared<SecondFacet>(new OtherSecond());
            destination = onlyView;
            if (destination || otherDestroyed != 1) return 11;
            failedDrainAssignSource : Shared<FirstFacet>(only);
            failedDrainAssigned : Shared<SecondFacet>(new OtherSecond());
            failedDrainAssigned = #failedDrainAssignSource;
            if (failedDrainAssigned || failedDrainAssignSource.useCount() != 4 ||
                otherDestroyed != 2) return 22;

            weakOnly : Weak<FirstFacet>(onlyView);
            weakMismatch : Weak<SecondFacet>(#weakOnly);
            if (weakMismatch.useCount() != 0 || weakOnly.useCount() != 4) return 12;
            weakDestination : Weak<SecondFacet>(weakSecond);
            weakDestination = weakOnly;
            if (weakDestination.useCount() != 0 || weakOnly.useCount() != 4) return 18;
            weakMismatchDrainSource : Weak<FirstFacet>(onlyView);
            weakMismatchDrain : Weak<SecondFacet>(weakSecond);
            weakMismatchDrain = #weakMismatchDrainSource;
            if (weakMismatchDrain.useCount() != 0 ||
                weakMismatchDrainSource.useCount() != 4) return 19;
            only.reset();
            onlyView.reset();
            failedDrainSource.reset();
            failedDrainAssignSource.reset();
            weakOnly.reset();
            weakFirst.reset();
            weakSecond.reset();
            if (dualDestroyed != 1 || otherDestroyed != 2) return 13;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto test = j->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Converting Shared drain construction transfers without retaining and preserves failed sources",
          "[libk][shared][conversion][drain]") {
    auto j = jit_shared("converting_drains", R"SRC(
        destroyedDual : int = 0;
        destroyedOther : int = 0;
        interface Face {
            value() : int;
        }
        class Dual : public Face {
            public Dual() {}
            override value() : int { return 42; }
            ~Dual() { ++destroyedDual; }
        }
        class Other : public Face {
            public Other() {}
            override value() : int { return -1; }
            ~Other() { ++destroyedOther; }
        }
        getValue(value : Face&) : int { return value.value(); }
        test() : int {
            strong : Shared<Dual>(new Dual());
            face : Shared<Face>(strong);
            before : int = strong.useCount();
            moved : Shared<Dual>(#face);
            if (face) return 1;
            if (moved.useCount() != before) return 2;
            if (getValue(*moved.get()) != 42) return 3;

            successSource : Shared<Face>(strong);
            successDestination : Shared<Dual>();
            successDestination = #successSource;
            if (successSource) return 16;
            if (successDestination.useCount() != before + 1) return 17;
            if (getValue(*successDestination.get()) != 42) return 18;
            successDestination.reset();

            failedSource : Shared<Face>(strong);
            rejected : Shared<Other>(#failedSource);
            if (rejected) return 4;
            if (failedSource.useCount() != before + 1) return 5;
            if (destroyedOther != 0) return 6;

            old : Shared<Other>(new Other());
            old = #failedSource;
            if (old) return 7;
            if (failedSource.useCount() != before + 1) return 8;
            if (destroyedOther != 1) return 9;

            strong.reset();
            moved.reset();
            failedSource.reset();
            if (destroyedDual != 1) return 10;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto test = j->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Weak converting views lock live objects, preserve static expired groups, and reject expired dynamic views",
          "[libk][weak][shared][conversion]") {
    auto j = jit_shared("weak_conversions", R"SRC(
        dualDestroyed : int = 0;
        interface BaseFace {
            value() : int;
        }
        class DerivedFace : public BaseFace {
            public DerivedFace() {}
            override value() : int { return 42; }
            ~DerivedFace() { ++dualDestroyed; }
        }
        class OtherFace : public BaseFace {
            public OtherFace() {}
            override value() : int { return -1; }
        }
        getValue(value : BaseFace&) : int { return value.value(); }

        test() : int {
            strong : Shared<DerivedFace>(new DerivedFace());
            baseWeak : Weak<BaseFace>(strong);
            derivedWeak : Weak<DerivedFace>(baseWeak);
            if (baseWeak.useCount() != 1) return 1;
            if (derivedWeak.useCount() != 1) return 2;
            locked : Shared<DerivedFace> = derivedWeak.lock();
            if (!locked || getValue(*locked.get()) != 42) return 3;
            locked.reset();
            try {
                promoted : Shared<DerivedFace>(baseWeak);
                if (!promoted || promoted.useCount() != 2 || getValue(*promoted.get()) != 42) return 21;
                promoted.reset();
            } catch (e : BadWeakReferenceException&) {
                return 21;
            }
            try {
                rejectedPromotion : Shared<OtherFace>(baseWeak);
                if (rejectedPromotion || strong.useCount() != 1) return 22;
            } catch (e : BadWeakReferenceException&) {
                return 22;
            }

            failedWeakSource : Weak<BaseFace>(strong);
            rejected : Weak<OtherFace>(#failedWeakSource);
            if (rejected.useCount() != 0) return 4;
            if (failedWeakSource.useCount() != 1) return 5;

            assignedSource : Weak<BaseFace>(strong);
            assigned : Weak<DerivedFace>();
            assigned = assignedSource;
            if (assigned.useCount() != 1 || assignedSource.useCount() != 1) return 16;
            assignedLock : Shared<DerivedFace> = assigned.lock();
            if (!assignedLock || getValue(*assignedLock.get()) != 42) return 17;
            assignedLock.reset();

            movedSource : Weak<BaseFace>(strong);
            moved : Weak<DerivedFace>(#movedSource);
            if (movedSource.useCount() != 0) return 6;
            if (moved.useCount() != 1) return 7;
            check : Shared<DerivedFace> = moved.lock();
            if (!check || getValue(*check.get()) != 42) return 8;
            check.reset();

            drainAssignSource : Weak<BaseFace>(strong);
            drainAssigned : Weak<DerivedFace>();
            drainAssigned = #drainAssignSource;
            if (drainAssignSource.useCount() != 0 || drainAssigned.useCount() != 1) return 18;

            mismatchAssignSource : Weak<BaseFace>(strong);
            mismatchAssigned : Weak<OtherFace>();
            mismatchAssigned = mismatchAssignSource;
            if (mismatchAssigned.useCount() != 0 || mismatchAssignSource.useCount() != 1) return 19;

            mismatchDrainSource : Weak<BaseFace>(strong);
            mismatchDrainAssigned : Weak<OtherFace>();
            mismatchDrainAssigned = #mismatchDrainSource;
            if (mismatchDrainAssigned.useCount() != 0 || mismatchDrainSource.useCount() != 1) return 20;

            strong.reset();
            locked.reset();
            if (!baseWeak.expired() || !derivedWeak.expired()) return 9;

            try {
                expiredPromotion : Shared<DerivedFace>(baseWeak);
                return 23;
            } catch (e : BadWeakReferenceException&) {}

            staticExpired : Weak<BaseFace>(derivedWeak);
            if (!staticExpired.expired() || staticExpired.useCount() != 0) return 10;
            dynamicExpired : Weak<DerivedFace>(baseWeak);
            if (dynamicExpired.useCount() != 0) return 11;

            failedExpiredDrain : Weak<BaseFace>(baseWeak);
            rejectedExpired : Weak<DerivedFace>(#failedExpiredDrain);
            if (rejectedExpired.useCount() != 0) return 12;
            if (!failedExpiredDrain.expired()) return 13;

            expiredStaticDrain : Weak<BaseFace>(#derivedWeak);
            if (derivedWeak.useCount() != 0 || !expiredStaticDrain.expired()) return 14;
            if (dualDestroyed != 1) return 15;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto test = j->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 0);
}

TEST_CASE("Same-group assignments replace adjusted Shared and Weak views",
          "[libk][shared][weak][conversion][assignment]") {
    auto j = jit_shared("same_group_view_assignment", R"SRC(
        dualDestroyed : int = 0;
        class BaseView {
            public BaseView() {}
        }
        class LeftBranch : public BaseView {}
        class RightBranch : public BaseView {}
        class Holder : public BaseView {
            public left : LeftBranch;
            public right : RightBranch;
            public Holder() {}
            ~Holder() { ++dualDestroyed; }
        }

        makeHolder() : Shared<Holder>! {
            return new Shared<Holder>(new Holder());
        }
        copyBase(owner : const Shared<Holder>&) : Shared<BaseView>! {
            return new Shared<BaseView>(owner);
        }
        holderView(owner : const Shared<Holder>&) : Holder* {
            return owner.get();
        }
        leftView(holder : Holder&) : BaseView* { return &holder.left; }
        rightView(holder : Holder&) : BaseView* { return &holder.right; }
        shareInto(destination : Shared<BaseView>*, source : const Shared<BaseView>&) {
            destination->share(source);
        }
        sharedView(handle : const Shared<BaseView>&) : BaseView* {
            return handle.get();
        }
        makeWeak(handle : const Shared<BaseView>&) : Weak<BaseView>! {
            return new Weak<BaseView>(handle);
        }
        assignWeak(destination : Weak<BaseView>*, source : const Weak<BaseView>&) {
            *destination = source;
        }
        lockWeak(handle : const Weak<BaseView>&) : Shared<BaseView>! {
            return new Shared<BaseView>(handle.lock());
        }
        dropShared(handle : Shared<BaseView>!) { delete handle; }
        dropWeak(handle : Weak<BaseView>!) { delete handle; }
        dropHolder(handle : Shared<Holder>!) { delete handle; }
        destroyedCount() : int { return dualDestroyed; }
    )SRC");
    REQUIRE(j);

    struct HandleLayout {
        void* block;
        void* view;
    };
    static_assert(sizeof(HandleLayout) == 2 * sizeof(void*));

    auto makeHolder = j->lookup_symbol<void*(*)()>("makeHolder");
    auto copyBase = j->lookup_symbol<void*(*)(void*)>("copyBase");
    auto holderView = j->lookup_symbol<void*(*)(void*)>("holderView");
    auto leftView = j->lookup_symbol<void*(*)(void*)>("leftView");
    auto rightView = j->lookup_symbol<void*(*)(void*)>("rightView");
    auto shareInto = j->lookup_symbol<void(*)(void*, void*)>("shareInto");
    auto sharedView = j->lookup_symbol<void*(*)(void*)>("sharedView");
    auto makeWeak = j->lookup_symbol<void*(*)(void*)>("makeWeak");
    auto assignWeak = j->lookup_symbol<void(*)(void*, void*)>("assignWeak");
    auto lockWeak = j->lookup_symbol<void*(*)(void*)>("lockWeak");
    auto dropShared = j->lookup_symbol<void(*)(void*)>("dropShared");
    auto dropWeak = j->lookup_symbol<void(*)(void*)>("dropWeak");
    auto dropHolder = j->lookup_symbol<void(*)(void*)>("dropHolder");
    auto destroyedCount = j->lookup_symbol<int(*)()>("destroyedCount");
    REQUIRE(makeHolder);
    REQUIRE(copyBase);
    REQUIRE(holderView);
    REQUIRE(leftView);
    REQUIRE(rightView);
    REQUIRE(shareInto);
    REQUIRE(sharedView);
    REQUIRE(makeWeak);
    REQUIRE(assignWeak);
    REQUIRE(lockWeak);
    REQUIRE(dropShared);
    REQUIRE(dropWeak);
    REQUIRE(dropHolder);
    REQUIRE(destroyedCount);

    auto* owner = static_cast<HandleLayout*>(makeHolder());
    REQUIRE(owner);
    auto* holder = holderView(owner);
    REQUIRE(holder);
    void* leftBase = leftView(holder);
    void* rightBase = rightView(holder);
    REQUIRE(leftBase);
    REQUIRE(rightBase);
    REQUIRE(leftBase != rightBase);

    auto* left = static_cast<HandleLayout*>(copyBase(owner));
    auto* right = static_cast<HandleLayout*>(copyBase(owner));
    REQUIRE(left);
    REQUIRE(right);
    CHECK(left->block == owner->block);
    CHECK(right->block == owner->block);

    // K coalesces repeated inheritance bases; use distinct branch-member
    // subobjects to exercise Shared's same-control-block view-copy fast path.
    left->view = leftBase;
    right->view = rightBase;
    shareInto(left, right);
    CHECK(sharedView(left) == rightBase);

    auto* leftWeak = static_cast<HandleLayout*>(makeWeak(left));
    auto* rightWeak = static_cast<HandleLayout*>(makeWeak(right));
    REQUIRE(leftWeak);
    REQUIRE(rightWeak);
    CHECK(leftWeak->block == rightWeak->block);
    leftWeak->view = leftBase;
    rightWeak->view = rightBase;
    assignWeak(leftWeak, rightWeak);
    auto* locked = static_cast<HandleLayout*>(lockWeak(leftWeak));
    REQUIRE(locked);
    CHECK(sharedView(locked) == rightBase);
    dropShared(locked);

    dropShared(left);
    dropShared(right);
    dropHolder(owner);
    CHECK(destroyedCount() == 1);

    assignWeak(leftWeak, rightWeak);
    CHECK(leftWeak->view == nullptr);
    dropWeak(leftWeak);
    dropWeak(rightWeak);
}

TEST_CASE("Shared converting views reject const removal",
          "[libk][shared][conversion][diagnostic]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __shared_const_drop__;
        class Base {
            public Base() {}
            public virtualMethod() : int { return 0; }
        }
        class Derived : public Base {
            public Derived() {}
            public value() : int { return 42; }
        }
        test() : int {
            source : Shared<const Derived>(new Derived());
            destination : Shared<Base>(source);
            return 0;
        }
    )SRC"));
}
