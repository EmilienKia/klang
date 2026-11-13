/*
 * Scoped provenance substrate regressions.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
#include <catch2/catch_all.hpp>
#include "provenance.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <thread>

namespace {
std::atomic<int> allocations_before_failure{-1};
std::atomic<std::uint64_t> successful_allocations{0};
std::atomic<std::uint64_t> observed_frees{0};

bool allocation_fails() {
    int remaining = allocations_before_failure.load();
    if (remaining == 0) {
        allocations_before_failure = -1;
        return true;
    }
    if (remaining > 0) --allocations_before_failure;
    return false;
}

using Path = std::unique_ptr<KProvPath, decltype(&__k_provenance_path_release)>;

KProvParent path_parent(KProvPath* path) {
    return {path, nullptr};
}

KProvParent scope_parent(KProvScope& scope) {
    return {nullptr, &scope};
}

Path capture(KProvScope& scope) {
    KProvPath* result = nullptr;
    REQUIRE(__k_provenance_capture(scope_parent(scope), &result) == K_PROVENANCE_OK);
    REQUIRE(result != nullptr);
    return Path(result, __k_provenance_path_release);
}

Path capture_slot(KProvSlot& slot, KProvParent parent = {}) {
    KProvScope scope{};
    REQUIRE(__k_provenance_enter_live(&scope, parent, &slot) == K_PROVENANCE_OK);
    auto result = capture(scope);
    REQUIRE(__k_provenance_leave(&scope, 0) == K_PROVENANCE_OK);
    return result;
}
}

extern "C" {
void* __k_test_provenance_malloc(std::size_t size) {
    if (allocation_fails()) return nullptr;
    void* result = std::malloc(size);
    if (result) ++successful_allocations;
    return result;
}
void* __k_test_provenance_aligned_alloc(std::size_t alignment, std::size_t size) {
    if (allocation_fails()) return nullptr;
    void* result = std::aligned_alloc(alignment, size);
    if (result) ++successful_allocations;
    return result;
}
void __k_test_provenance_free(void* ptr) {
    if (ptr) ++observed_frees;
    std::free(ptr);
}
int32_t __k_test_provenance_enter_live(KProvScope*, KProvParent, KProvSlot*);
int32_t __k_test_provenance_enter_cleanup(KProvScope*, KProvParent, KProvSlot*);
int32_t __k_test_provenance_scope_validate(const KProvScope*);
int32_t __k_test_provenance_leave(KProvScope*, int32_t);
int32_t __k_test_provenance_slot_close(KProvSlot*);
int32_t __k_test_provenance_capture(KProvParent, KProvPath**);
void __k_test_provenance_path_retain(KProvPath*);
void __k_test_provenance_path_release(KProvPath*);
int32_t __k_test_provenance_path_validate(const KProvPath*);
int32_t __k_test_provenance_path_pin(KProvPathPin*, KProvPath*);
int32_t __k_test_provenance_path_unpin(KProvPathPin*);
void __k_test_provenance_set_token_pins(KProvSlot*, uint64_t);
KProvDomain* __k_test_provenance_domain();
uint64_t __k_test_provenance_collect(KProvDomain*, uint64_t);
}

namespace {
uint64_t test_collect(uint64_t limit) {
    return __k_test_provenance_collect(__k_test_provenance_domain(), limit);
}
}

TEST_CASE("Ordinary provenance survives slot death without reviving",
          "[libk][provenance][native]") {
    CHECK(__k_provenance_path_validate(nullptr) == K_PROVENANCE_OK);
    Path old(nullptr, __k_provenance_path_release);
    {
        KProvSlot slot{};
        old = capture_slot(slot);
        __k_provenance_path_retain(old.get());
        Path another(old.get(), __k_provenance_path_release);
        REQUIRE(__k_provenance_slot_close(&slot) == K_PROVENANCE_OK);
        CHECK(__k_provenance_path_validate(another.get()) == K_PROVENANCE_STALE);
        CHECK(__k_provenance_slot_close(&slot) == K_PROVENANCE_DEAD);
        KProvScope rejected{};
        CHECK(__k_provenance_enter_live(&rejected, {}, &slot) == K_PROVENANCE_DEAD);
    }
    KProvSlot replacement{};
    auto fresh = capture_slot(replacement);
    CHECK(__k_provenance_path_validate(old.get()) == K_PROVENANCE_STALE);
    CHECK(__k_provenance_path_validate(fresh.get()) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_slot_close(&replacement) == K_PROVENANCE_OK);
    CHECK(__k_provenance_path_validate(fresh.get()) == K_PROVENANCE_STALE);
}

TEST_CASE("Mixed cleanup and live ancestry pins every ancestor",
          "[libk][provenance][native]") {
    struct E { KProvSlot slot{}; };
    struct D { KProvSlot slot{}; E child; };
    struct C { KProvSlot slot{}; D child; };
    struct B { KProvSlot slot{}; C child; };
    struct A { KProvSlot slot{}; B child; };
    struct Middle { KProvSlot slot{}; A child; };
    struct Root { KProvSlot slot{}; Middle child; } object{};
    auto& root = object.slot;
    auto& middle = object.child.slot;
    auto& a = object.child.child.slot;
    auto& b = object.child.child.child.slot;
    auto& c = object.child.child.child.child.slot;
    auto& d = object.child.child.child.child.child.slot;
    auto& e = object.child.child.child.child.child.child.slot;
    auto root_path = capture_slot(root);
    auto middle_path = capture_slot(middle, path_parent(root_path.get()));
    auto a_path = capture_slot(a, path_parent(middle_path.get()));
    auto b_path = capture_slot(b, path_parent(a_path.get()));
    auto old = capture_slot(c, path_parent(b_path.get()));

    KProvScope outer{}, live{}, inner{}, deeper_live{}, deepest{};
    REQUIRE(__k_provenance_enter_cleanup(&outer, path_parent(middle_path.get()), &a)
            == K_PROVENANCE_OK);
    middle_path.reset();
    CHECK(__k_provenance_path_validate(old.get()) == K_PROVENANCE_STALE);
    REQUIRE(__k_provenance_enter_live(&live, scope_parent(outer), &b) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_enter_cleanup(&inner, scope_parent(live), &c) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_enter_live(&deeper_live, scope_parent(inner), &d)
            == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_enter_cleanup(&deepest, scope_parent(deeper_live), &e)
            == K_PROVENANCE_OK);
    CHECK(__k_provenance_scope_validate(&deepest) == K_PROVENANCE_OK);
    CHECK(__k_provenance_slot_close(&root) == K_PROVENANCE_BUSY);
    CHECK(__k_provenance_slot_close(&middle) == K_PROVENANCE_BUSY);
    CHECK(__k_provenance_slot_close(&a) == K_PROVENANCE_BUSY);
    CHECK(__k_provenance_slot_close(&b) == K_PROVENANCE_BUSY);
    CHECK(__k_provenance_slot_close(&c) == K_PROVENANCE_BUSY);
    CHECK(__k_provenance_slot_close(&d) == K_PROVENANCE_BUSY);
    CHECK(__k_provenance_slot_close(&e) == K_PROVENANCE_BUSY);
    KProvScope rejected{};
    CHECK(__k_provenance_enter_cleanup(&rejected, {}, &root) == K_PROVENANCE_BUSY);
    CHECK(__k_provenance_enter_cleanup(&rejected, {}, &middle) == K_PROVENANCE_BUSY);
    CHECK(__k_provenance_enter_cleanup(&rejected, scope_parent(deepest), &a)
          == K_PROVENANCE_BUSY);
    CHECK(__k_provenance_enter_cleanup(&rejected, scope_parent(deepest), &b)
          == K_PROVENANCE_BUSY);
    CHECK(__k_provenance_enter_cleanup(&rejected, scope_parent(deepest), &c)
          == K_PROVENANCE_BUSY);
    CHECK(__k_provenance_enter_live(&rejected, scope_parent(deepest), &e)
          == K_PROVENANCE_BUSY);
    CHECK(__k_provenance_leave(&outer, 0) == K_PROVENANCE_BUSY);
    CHECK(__k_provenance_leave(&live, 0) == K_PROVENANCE_BUSY);

    KProvPath* escaped = reinterpret_cast<KProvPath*>(1);
    CHECK(__k_provenance_capture(scope_parent(live), &escaped) == K_PROVENANCE_SCOPE_ESCAPE);
    CHECK(escaped == nullptr);
    CHECK(__k_provenance_capture(scope_parent(deepest), &escaped) == K_PROVENANCE_SCOPE_ESCAPE);
    CHECK(escaped == nullptr);
    KProvScope copy = deepest;
    CHECK(__k_provenance_scope_validate(&copy) == K_PROVENANCE_STALE);
    CHECK(__k_provenance_leave(&copy, 0) == K_PROVENANCE_INVALID);

    REQUIRE(__k_provenance_leave(&deepest, 1) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_leave(&deeper_live, 0) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_slot_close(&d) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_leave(&inner, 1) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_leave(&live, 0) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_slot_close(&b) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_leave(&outer, 0) == K_PROVENANCE_OK);
    CHECK(__k_provenance_scope_validate(&outer) == K_PROVENANCE_STALE);
    CHECK(__k_provenance_leave(&outer, 0) == K_PROVENANCE_INVALID);
    CHECK(__k_provenance_path_validate(old.get()) == K_PROVENANCE_STALE);
    REQUIRE(__k_provenance_slot_close(&a) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_slot_close(&middle) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_slot_close(&root) == K_PROVENANCE_OK);
}

TEST_CASE("Ordinary scope capture retains complete live ancestry",
          "[libk][provenance][native]") {
    struct Inner { KProvSlot slot{}; };
    struct Outer { KProvSlot slot{}; Inner child; } storage{};
    auto& a = storage.slot;
    auto& b = storage.child.slot;
    KProvScope outer{}, repeated{}, inner{};
    REQUIRE(__k_provenance_enter_live(&outer, {}, &a) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_enter_live(&repeated, scope_parent(outer), &a) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_enter_live(&inner, scope_parent(repeated), &b) == K_PROVENANCE_OK);
    auto path = capture(inner);
    KProvPath* retained = nullptr;
    REQUIRE(__k_provenance_capture(path_parent(path.get()), &retained) == K_PROVENANCE_OK);
    Path second(retained, __k_provenance_path_release);
    CHECK(__k_provenance_path_validate(path.get()) == K_PROVENANCE_OK);
    CHECK(__k_provenance_leave(&outer, 0) == K_PROVENANCE_BUSY);
    CHECK(__k_provenance_leave(&inner, 1) == K_PROVENANCE_INVALID);
    REQUIRE(__k_provenance_leave(&inner, 0) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_leave(&repeated, 0) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_leave(&outer, 0) == K_PROVENANCE_OK);
    KProvScope transition{};
    REQUIRE(__k_provenance_enter_cleanup(&transition, {}, &a) == K_PROVENANCE_OK);
    CHECK(__k_provenance_path_validate(path.get()) == K_PROVENANCE_STALE);
    CHECK(__k_provenance_path_validate(second.get()) == K_PROVENANCE_STALE);
    REQUIRE(__k_provenance_slot_close(&b) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_leave(&transition, 0) == K_PROVENANCE_OK);
    auto fresh = capture_slot(a);
    CHECK(__k_provenance_path_validate(fresh.get()) == K_PROVENANCE_OK);
    CHECK(__k_provenance_path_validate(path.get()) == K_PROVENANCE_STALE);
    REQUIRE(__k_provenance_slot_close(&a) == K_PROVENANCE_OK);
}

TEST_CASE("Failed nested acquisition rolls back ancestor pins",
          "[libk][provenance][native]") {
    KProvSlot a{}, b{}, target{};
    auto parent = capture_slot(a);
    auto child = capture_slot(b, path_parent(parent.get()));
    REQUIRE(__k_provenance_slot_close(&a) == K_PROVENANCE_OK);
    KProvScope rejected{};
    CHECK(__k_provenance_enter_cleanup(&rejected, path_parent(child.get()), &target)
          == K_PROVENANCE_STALE);
    REQUIRE(__k_provenance_slot_close(&b) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_enter_cleanup(&rejected, {}, &target) == K_PROVENANCE_OK);
    CHECK(__k_provenance_leave(&rejected, 2) == K_PROVENANCE_INVALID);
    REQUIRE(__k_provenance_leave(&rejected, 1) == K_PROVENANCE_OK);

    KProvSlot x{}, y{};
    auto x_path = capture_slot(x);
    KProvScope live{}, blocked{};
    REQUIRE(__k_provenance_enter_live(&live, {}, &y) == K_PROVENANCE_OK);
    CHECK(__k_provenance_enter_cleanup(&blocked, path_parent(x_path.get()), &y)
          == K_PROVENANCE_BUSY);
    REQUIRE(__k_provenance_slot_close(&x) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_leave(&live, 0) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_slot_close(&y) == K_PROVENANCE_OK);
}

TEST_CASE("A path pin protects all ancestors and retains the path",
          "[libk][provenance][native]") {
    struct Leaf { KProvSlot slot{}; };
    struct Middle { KProvSlot slot{}; Leaf leaf; };
    struct Root { KProvSlot slot{}; Middle middle; } storage{};
    auto root = capture_slot(storage.slot);
    auto middle = capture_slot(storage.middle.slot, path_parent(root.get()));
    auto leaf = capture_slot(storage.middle.leaf.slot, path_parent(middle.get()));

    KProvPathPin pin{}, second{};
    REQUIRE(__k_provenance_path_pin(&pin, leaf.get()) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_path_pin(&second, leaf.get()) == K_PROVENANCE_OK);
    KProvPathPin copy = pin;
    CHECK(__k_provenance_path_pin(&pin, nullptr) == K_PROVENANCE_INVALID);
    CHECK(__k_provenance_path_pin(&copy, nullptr) == K_PROVENANCE_INVALID);
    CHECK(__k_provenance_path_unpin(&copy) == K_PROVENANCE_INVALID);
    leaf.reset();
    middle.reset();
    root.reset();
    __k_provenance_collect(__k_provenance_domain(), UINT64_MAX);

    std::array<KProvSlot*, 3> slots{
        &storage.slot, &storage.middle.slot, &storage.middle.leaf.slot};
    for (auto* slot : slots) {
        KProvScope transition{};
        CHECK(__k_provenance_enter_cleanup(&transition, {}, slot) == K_PROVENANCE_BUSY);
        CHECK(__k_provenance_slot_close(slot) == K_PROVENANCE_BUSY);
    }

    REQUIRE(__k_provenance_path_unpin(&pin) == K_PROVENANCE_OK);
    CHECK(__k_provenance_path_unpin(&pin) == K_PROVENANCE_INVALID);
    CHECK(__k_provenance_path_unpin(&copy) == K_PROVENANCE_INVALID);
    for (auto* slot : slots) {
        KProvScope transition{};
        CHECK(__k_provenance_enter_cleanup(&transition, {}, slot) == K_PROVENANCE_BUSY);
        CHECK(__k_provenance_slot_close(slot) == K_PROVENANCE_BUSY);
    }
    REQUIRE(__k_provenance_path_unpin(&second) == K_PROVENANCE_OK);
    for (auto* slot : slots) {
        KProvScope transition{};
        REQUIRE(__k_provenance_enter_cleanup(&transition, {}, slot) == K_PROVENANCE_OK);
        REQUIRE(__k_provenance_leave(&transition, 0) == K_PROVENANCE_OK);
    }
    REQUIRE(__k_provenance_slot_close(&storage.middle.leaf.slot) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_slot_close(&storage.middle.slot) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_slot_close(&storage.slot) == K_PROVENANCE_OK);
}

TEST_CASE("Path pins distinguish stale and untracked paths",
          "[libk][provenance][native]") {
    KProvSlot root_slot{}, leaf_slot{};
    auto root = capture_slot(root_slot);
    auto leaf = capture_slot(leaf_slot, path_parent(root.get()));
    REQUIRE(__k_provenance_slot_close(&root_slot) == K_PROVENANCE_OK);

    KProvPathPin pin{};
    CHECK(__k_provenance_path_pin(&pin, leaf.get()) == K_PROVENANCE_STALE);
    REQUIRE(__k_provenance_slot_close(&leaf_slot) == K_PROVENANCE_OK);
    CHECK(__k_provenance_path_unpin(&pin) == K_PROVENANCE_INVALID);
    CHECK(__k_provenance_path_pin(nullptr, nullptr) == K_PROVENANCE_INVALID);
    CHECK(__k_provenance_path_unpin(nullptr) == K_PROVENANCE_INVALID);
    REQUIRE(__k_provenance_path_pin(&pin, nullptr) == K_PROVENANCE_OK);
    KProvPathPin copy = pin;
    CHECK(__k_provenance_path_unpin(&copy) == K_PROVENANCE_INVALID);
    KProvSlot untracked{};
    KProvScope transition{};
    REQUIRE(__k_provenance_enter_cleanup(&transition, {}, &untracked) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_leave(&transition, 1) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_path_unpin(&pin) == K_PROVENANCE_OK);
    CHECK(__k_provenance_path_unpin(&pin) == K_PROVENANCE_INVALID);
    REQUIRE(__k_provenance_path_pin(&pin, nullptr) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_path_unpin(&pin) == K_PROVENANCE_OK);
}

TEST_CASE("Path pin exhaustion rolls back already pinned descendants",
          "[libk][provenance][native]") {
    test_collect(UINT64_MAX);
    KProvSlot root{}, leaf{};
    KProvScope scope{};
    KProvPath* root_path = nullptr;
    KProvPath* leaf_path = nullptr;
    REQUIRE(__k_test_provenance_enter_live(&scope, {}, &root) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_capture(scope_parent(scope), &root_path) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&scope, 0) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_enter_live(&scope, path_parent(root_path), &leaf)
            == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_capture(scope_parent(scope), &leaf_path) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&scope, 0) == K_PROVENANCE_OK);

    auto alloc_start = successful_allocations.load();
    auto free_start = observed_frees.load();
    allocations_before_failure = 0;
    __k_test_provenance_set_token_pins(&root, UINT64_MAX);
    KProvPathPin pin{};
    CHECK(__k_test_provenance_path_pin(&pin, leaf_path) == K_PROVENANCE_EXHAUSTED);
    REQUIRE(__k_test_provenance_slot_close(&leaf) == K_PROVENANCE_OK);
    __k_test_provenance_set_token_pins(&root, 0);
    REQUIRE(__k_test_provenance_path_pin(&pin, root_path) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_path_unpin(&pin) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_slot_close(&root) == K_PROVENANCE_OK);
    __k_test_provenance_path_release(leaf_path);
    __k_test_provenance_path_release(root_path);
    CHECK(allocations_before_failure.load() == 0);
    CHECK(successful_allocations.load() == alloc_start);
    CHECK(observed_frees.load() == free_start);
    allocations_before_failure = -1;
    CHECK(test_collect(UINT64_MAX) == 4);
    CHECK(observed_frees.load() - free_start == 4);
}

TEST_CASE("Path pin unwind retires without allocation or free",
          "[libk][provenance][native]") {
    test_collect(UINT64_MAX);
    KProvSlot slot{};
    KProvScope scope{};
    KProvPath* path = nullptr;
    REQUIRE(__k_test_provenance_enter_live(&scope, {}, &slot) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_capture(scope_parent(scope), &path) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&scope, 0) == K_PROVENANCE_OK);
    auto alloc_start = successful_allocations.load();
    auto free_start = observed_frees.load();
    allocations_before_failure = 0;
    KProvPathPin pin{};
    REQUIRE(__k_test_provenance_path_pin(&pin, path) == K_PROVENANCE_OK);
    int32_t unwind_result = K_PROVENANCE_INVALID;
    struct UnpinOnExit {
        KProvPathPin* pin;
        int32_t* result;
        ~UnpinOnExit() { *result = __k_test_provenance_path_unpin(pin); }
    };
    try {
        UnpinOnExit guard{&pin, &unwind_result};
        __k_test_provenance_path_release(path);
        CHECK(__k_test_provenance_slot_close(&slot) == K_PROVENANCE_BUSY);
        throw 1;
    } catch (int) {
    }
    CHECK(unwind_result == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_slot_close(&slot) == K_PROVENANCE_OK);
    CHECK(allocations_before_failure.load() == 0);
    CHECK(successful_allocations.load() == alloc_start);
    CHECK(observed_frees.load() == free_start);
    allocations_before_failure = -1;
    CHECK(test_collect(UINT64_MAX) == 2);
    CHECK(observed_frees.load() - free_start == 2);
}

TEST_CASE("A live scope already present in an ordinary path remains capturable",
          "[libk][provenance][native]") {
    KProvSlot slot{};
    auto old = capture_slot(slot);
    KProvScope scope{}, rejected{};
    REQUIRE(__k_provenance_enter_live(&scope, path_parent(old.get()), &slot)
            == K_PROVENANCE_OK);
    auto duplicate = capture(scope);
    CHECK(__k_provenance_path_validate(duplicate.get()) == K_PROVENANCE_OK);
    CHECK(__k_provenance_enter_cleanup(&rejected, path_parent(old.get()), &slot)
          == K_PROVENANCE_BUSY);
    REQUIRE(__k_provenance_leave(&scope, 0) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_enter_cleanup(&rejected, {}, &slot) == K_PROVENANCE_OK);
    CHECK(__k_provenance_path_validate(duplicate.get()) == K_PROVENANCE_STALE);
    REQUIRE(__k_provenance_leave(&rejected, 1) == K_PROVENANCE_OK);
}

TEST_CASE("Path reference counts support concurrent ownership",
          "[libk][provenance][native]") {
    KProvSlot slot{};
    auto path = capture_slot(slot);
    std::atomic<int> failures{0};
    std::array<std::thread, 2> workers;
    for (auto& worker : workers) {
        __k_provenance_path_retain(path.get());
        worker = std::thread([raw = path.get(), &failures] {
            for (int i = 0; i < 1000; ++i) {
                __k_provenance_path_retain(raw);
                if (__k_provenance_path_validate(raw) != K_PROVENANCE_OK) ++failures;
                __k_provenance_path_release(raw);
            }
            __k_provenance_path_release(raw);
        });
    }
    for (auto& worker : workers) worker.join();
    CHECK(failures.load() == 0);
    REQUIRE(__k_provenance_slot_close(&slot) == K_PROVENANCE_OK);
}

TEST_CASE("Cleanup stays allocation-free at reusable generation exhaustion",
          "[libk][provenance][native]") {
    KProvSlot slot{}, nested{};
    KProvScope initial{};
    REQUIRE(__k_test_provenance_enter_live(&initial, {}, &slot) == K_PROVENANCE_OK);
    KProvPath* original = nullptr;
    REQUIRE(__k_test_provenance_capture(scope_parent(initial), &original) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&initial, 0) == K_PROVENANCE_OK);
    for (int i = 0; i < 3; ++i) {
        KProvScope cleanup{};
        REQUIRE(__k_test_provenance_enter_cleanup(&cleanup, {}, &slot) == K_PROVENANCE_OK);
        REQUIRE(__k_test_provenance_leave(&cleanup, 0) == K_PROVENANCE_OK);
    }
    KProvScope last_live{};
    REQUIRE(__k_test_provenance_enter_live(&last_live, {}, &slot) == K_PROVENANCE_OK);
    KProvPath* last = nullptr;
    REQUIRE(__k_test_provenance_capture(scope_parent(last_live), &last) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&last_live, 0) == K_PROVENANCE_OK);

    KProvSlot guarded_slot{};
    KProvScope guarded{};
    REQUIRE(__k_test_provenance_enter_cleanup(&guarded, path_parent(last), &guarded_slot)
            == K_PROVENANCE_OK);
    allocations_before_failure = 0;
    KProvScope blocked{};
    CHECK(__k_test_provenance_enter_cleanup(&blocked, {}, &slot) == K_PROVENANCE_BUSY);
    CHECK(__k_test_provenance_path_validate(last) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&guarded, 1) == K_PROVENANCE_OK);
    KProvScope cleanup{}, nested_live{}, nested_cleanup{};
    REQUIRE(__k_test_provenance_enter_cleanup(&cleanup, {}, &slot) == K_PROVENANCE_OK);
    CHECK(__k_test_provenance_path_validate(last) == K_PROVENANCE_STALE);
    REQUIRE(__k_test_provenance_enter_live(&nested_live, scope_parent(cleanup), &nested)
            == K_PROVENANCE_OK);
    KProvSlot leaf{};
    REQUIRE(__k_test_provenance_enter_cleanup(&nested_cleanup, scope_parent(nested_live), &leaf)
            == K_PROVENANCE_OK);
    KProvPath* escaped = reinterpret_cast<KProvPath*>(1);
    CHECK(__k_test_provenance_capture(scope_parent(nested_live), &escaped)
          == K_PROVENANCE_SCOPE_ESCAPE);
    CHECK(escaped == nullptr);
    REQUIRE(__k_test_provenance_leave(&nested_cleanup, 1) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&nested_live, 0) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&cleanup, 0) == K_PROVENANCE_OK);
    for (int i = 0; i < 4; ++i) {
        KProvScope again{};
        REQUIRE(__k_test_provenance_enter_cleanup(&again, {}, &slot) == K_PROVENANCE_OK);
        REQUIRE(__k_test_provenance_leave(&again, 0) == K_PROVENANCE_OK);
    }
    CHECK(allocations_before_failure.load() == 0);
    KProvScope next_live{};
    REQUIRE(__k_test_provenance_enter_live(&next_live, {}, &slot) == K_PROVENANCE_OK);
    KProvPath* fresh = reinterpret_cast<KProvPath*>(1);
    CHECK(__k_test_provenance_capture(scope_parent(next_live), &fresh) == K_PROVENANCE_NO_MEMORY);
    CHECK(fresh == nullptr);
    allocations_before_failure = 1;
    CHECK(__k_test_provenance_capture(scope_parent(next_live), &fresh) == K_PROVENANCE_NO_MEMORY);
    CHECK(fresh == nullptr);
    REQUIRE(__k_test_provenance_capture(scope_parent(next_live), &fresh) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&next_live, 0) == K_PROVENANCE_OK);
    CHECK(__k_test_provenance_path_validate(fresh) == K_PROVENANCE_OK);
    CHECK(__k_test_provenance_path_validate(original) == K_PROVENANCE_STALE);
    CHECK(__k_test_provenance_path_validate(last) == K_PROVENANCE_STALE);
    REQUIRE(__k_test_provenance_slot_close(&slot) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_slot_close(&nested) == K_PROVENANCE_OK);
    CHECK(__k_test_provenance_path_validate(fresh) == K_PROVENANCE_STALE);
    __k_test_provenance_path_release(fresh);
    __k_test_provenance_path_release(last);
    __k_test_provenance_path_release(original);
    allocations_before_failure = -1;
}

TEST_CASE("Exhausted cleanup can close storage without allocating",
          "[libk][provenance][native]") {
    KProvSlot slot{};
    KProvScope live{};
    REQUIRE(__k_test_provenance_enter_live(&live, {}, &slot) == K_PROVENANCE_OK);
    KProvPath* first = nullptr;
    REQUIRE(__k_test_provenance_capture(scope_parent(live), &first) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&live, 0) == K_PROVENANCE_OK);
    for (int i = 0; i < 3; ++i) {
        KProvScope cleanup{};
        REQUIRE(__k_test_provenance_enter_cleanup(&cleanup, {}, &slot) == K_PROVENANCE_OK);
        REQUIRE(__k_test_provenance_leave(&cleanup, 0) == K_PROVENANCE_OK);
    }
    KProvScope last_live{};
    REQUIRE(__k_test_provenance_enter_live(&last_live, {}, &slot) == K_PROVENANCE_OK);
    KProvPath* last = nullptr;
    REQUIRE(__k_test_provenance_capture(scope_parent(last_live), &last) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&last_live, 0) == K_PROVENANCE_OK);
    allocations_before_failure = 0;
    KProvScope final_cleanup{};
    REQUIRE(__k_test_provenance_enter_cleanup(&final_cleanup, {}, &slot) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&final_cleanup, 1) == K_PROVENANCE_OK);
    CHECK(allocations_before_failure.load() == 0);
    CHECK(__k_test_provenance_path_validate(last) == K_PROVENANCE_STALE);
    CHECK(__k_test_provenance_path_validate(first) == K_PROVENANCE_STALE);
    CHECK(__k_test_provenance_slot_close(&slot) == K_PROVENANCE_DEAD);
    allocations_before_failure = -1;
    __k_test_provenance_path_release(last);
    __k_test_provenance_path_release(first);
}

TEST_CASE("Capture allocation failure rolls back every unpublished token",
          "[libk][provenance][native]") {
    KProvSlot a{}, b{};
    KProvScope outer{}, inner{};
    REQUIRE(__k_test_provenance_enter_live(&outer, {}, &a) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_enter_live(&inner, scope_parent(outer), &b) == K_PROVENANCE_OK);
    for (int fail_after = 0; fail_after < 4; ++fail_after) {
        allocations_before_failure = fail_after;
        KProvPath* failed = reinterpret_cast<KProvPath*>(1);
        CHECK(__k_test_provenance_capture(scope_parent(inner), &failed) == K_PROVENANCE_NO_MEMORY);
        CHECK(failed == nullptr);
        CHECK(a._token == nullptr);
        CHECK(b._token == nullptr);
        CHECK(__k_test_provenance_scope_validate(&inner) == K_PROVENANCE_OK);
    }
    KProvPath* captured = nullptr;
    REQUIRE(__k_test_provenance_capture(scope_parent(inner), &captured) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&inner, 0) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&outer, 0) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_slot_close(&a) == K_PROVENANCE_OK);
    CHECK(__k_test_provenance_path_validate(captured) == K_PROVENANCE_STALE);
    REQUIRE(__k_test_provenance_slot_close(&b) == K_PROVENANCE_OK);
    __k_test_provenance_path_release(captured);
    allocations_before_failure = -1;
}

TEST_CASE("Retired paths cascade only at the explicit collection point",
          "[libk][provenance][native]") {
    test_collect(UINT64_MAX);
    auto alloc_start = successful_allocations.load();
    auto free_start = observed_frees.load();
    struct Inner { KProvSlot slot{}; };
    struct Outer { KProvSlot slot{}; Inner child; } storage{};
    KProvScope outer{}, inner{};
    KProvPath* parent = nullptr;
    KProvPath* child = nullptr;
    REQUIRE(__k_test_provenance_enter_live(&outer, {}, &storage.slot) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_capture(scope_parent(outer), &parent) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&outer, 0) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_enter_live(&inner, path_parent(parent), &storage.child.slot)
            == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_capture(scope_parent(inner), &child) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&inner, 0) == K_PROVENANCE_OK);
    REQUIRE(successful_allocations.load() - alloc_start == 4);
    REQUIRE(__k_test_provenance_slot_close(&storage.child.slot) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_slot_close(&storage.slot) == K_PROVENANCE_OK);
    CHECK(test_collect(UINT64_MAX) == 0);
    CHECK(__k_test_provenance_path_validate(child) == K_PROVENANCE_STALE);
    allocations_before_failure = 0;
    __k_test_provenance_path_release(parent);
    __k_test_provenance_path_release(child);
    CHECK(allocations_before_failure.load() == 0);
    CHECK(observed_frees.load() == free_start);
    CHECK(test_collect(0) == 0);
    CHECK(observed_frees.load() == free_start);
    allocations_before_failure = -1;
    CHECK(test_collect(1) == 1);
    CHECK(observed_frees.load() - free_start == 1);
    CHECK(test_collect(UINT64_MAX) == 3);
    CHECK(observed_frees.load() - free_start == 4);
    CHECK(test_collect(UINT64_MAX) == 0);
}

TEST_CASE("Unwind cleanup and failed capture only retire storage",
          "[libk][provenance][native]") {
    test_collect(UINT64_MAX);
    auto free_start = observed_frees.load();
    struct Inner { KProvSlot slot{}; };
    struct Outer { KProvSlot slot{}; Inner child; } storage{};
    KProvScope live{}, cleanup{};
    KProvPath* parent = nullptr;
    REQUIRE(__k_test_provenance_enter_live(&live, {}, &storage.slot) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_capture(scope_parent(live), &parent) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&live, 0) == K_PROVENANCE_OK);
    allocations_before_failure = 0;
    REQUIRE(__k_test_provenance_enter_cleanup(&cleanup, path_parent(parent), &storage.child.slot)
            == K_PROVENANCE_OK);
    __k_test_provenance_path_release(parent);
    REQUIRE(__k_test_provenance_leave(&cleanup, 1) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_slot_close(&storage.slot) == K_PROVENANCE_OK);
    CHECK(allocations_before_failure.load() == 0);
    CHECK(observed_frees.load() == free_start);
    allocations_before_failure = -1;
    CHECK(test_collect(UINT64_MAX) == 2);
    CHECK(observed_frees.load() - free_start == 2);

    KProvSlot failed_slot{};
    KProvScope failed_live{};
    REQUIRE(__k_test_provenance_enter_live(&failed_live, {}, &failed_slot) == K_PROVENANCE_OK);
    allocations_before_failure = 1;
    KProvPath* failed = reinterpret_cast<KProvPath*>(1);
    CHECK(__k_test_provenance_capture(scope_parent(failed_live), &failed) == K_PROVENANCE_NO_MEMORY);
    CHECK(failed == nullptr);
    CHECK(failed_slot._token == nullptr);
    CHECK(observed_frees.load() - free_start == 2);
    REQUIRE(__k_test_provenance_leave(&failed_live, 0) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_slot_close(&failed_slot) == K_PROVENANCE_OK);
    CHECK(test_collect(UINT64_MAX) == 1);
    CHECK(observed_frees.load() - free_start == 3);
    allocations_before_failure = -1;
}

TEST_CASE("Retiring the last exhausted token does not free during cleanup",
          "[libk][provenance][native]") {
    test_collect(UINT64_MAX);
    KProvSlot slot{};
    KProvScope live{};
    KProvPath* path = nullptr;
    REQUIRE(__k_test_provenance_enter_live(&live, {}, &slot) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_capture(scope_parent(live), &path) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&live, 0) == K_PROVENANCE_OK);
    __k_test_provenance_path_release(path);
    CHECK(test_collect(UINT64_MAX) == 1);
    for (int i = 0; i < 3; ++i) {
        KProvScope transition{};
        REQUIRE(__k_test_provenance_enter_cleanup(&transition, {}, &slot) == K_PROVENANCE_OK);
        REQUIRE(__k_test_provenance_leave(&transition, 0) == K_PROVENANCE_OK);
    }
    auto free_start = observed_frees.load();
    allocations_before_failure = 0;
    KProvScope exhausted{};
    REQUIRE(__k_test_provenance_enter_cleanup(&exhausted, {}, &slot) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&exhausted, 0) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_slot_close(&slot) == K_PROVENANCE_OK);
    CHECK(allocations_before_failure.load() == 0);
    CHECK(observed_frees.load() == free_start);
    allocations_before_failure = -1;
    CHECK(test_collect(UINT64_MAX) == 1);
    CHECK(observed_frees.load() - free_start == 1);
}

TEST_CASE("Retired objects return to their allocating runtime domain",
          "[libk][provenance][native]") {
    __k_provenance_collect(__k_provenance_domain(), UINT64_MAX);
    test_collect(UINT64_MAX);
    auto free_start = observed_frees.load();
    struct Inner { KProvSlot slot{}; };
    struct Outer { KProvSlot slot{}; Inner child; } storage{};
    auto parent = capture_slot(storage.slot);
    KProvScope child_scope{};
    KProvPath* child = nullptr;
    REQUIRE(__k_test_provenance_enter_live(&child_scope, path_parent(parent.get()),
                                          &storage.child.slot) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_capture(scope_parent(child_scope), &child) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&child_scope, 0) == K_PROVENANCE_OK);
    parent.reset();
    REQUIRE(__k_test_provenance_slot_close(&storage.child.slot) == K_PROVENANCE_OK);
    REQUIRE(__k_provenance_slot_close(&storage.slot) == K_PROVENANCE_OK);
    __k_provenance_path_release(child);
    CHECK(__k_provenance_collect(__k_provenance_domain(), UINT64_MAX) == 0);
    CHECK(observed_frees.load() == free_start);
    CHECK(__k_provenance_collect(__k_test_provenance_domain(), 1) == 1);
    CHECK(observed_frees.load() - free_start == 1);
    CHECK(__k_test_provenance_collect(__k_provenance_domain(), UINT64_MAX) == 2);
    CHECK(test_collect(UINT64_MAX) == 1);
    CHECK(observed_frees.load() - free_start == 2);
}

TEST_CASE("Concurrent retained paths remain alive while collection runs",
          "[libk][provenance][native]") {
    test_collect(UINT64_MAX);
    auto free_start = observed_frees.load();
    KProvSlot slot{};
    KProvScope scope{};
    KProvPath* path = nullptr;
    REQUIRE(__k_test_provenance_enter_live(&scope, {}, &slot) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_capture(scope_parent(scope), &path) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_leave(&scope, 0) == K_PROVENANCE_OK);
    REQUIRE(__k_test_provenance_slot_close(&slot) == K_PROVENANCE_OK);

    std::atomic<bool> start{false};
    std::atomic<int> failures{0};
    std::array<std::thread, 2> workers;
    for (auto& worker : workers) {
        __k_test_provenance_path_retain(path);
        worker = std::thread([path, &start, &failures] {
            while (!start.load()) std::this_thread::yield();
            for (int i = 0; i < 1000; ++i) {
                __k_test_provenance_path_retain(path);
                if (__k_test_provenance_path_validate(path) != K_PROVENANCE_STALE) ++failures;
                __k_test_provenance_path_release(path);
            }
            __k_test_provenance_path_release(path);
        });
    }
    std::array<std::thread, 2> collectors;
    for (auto& collector : collectors) {
        collector = std::thread([&start] {
            while (!start.load()) std::this_thread::yield();
            for (int i = 0; i < 1000; ++i) test_collect(2);
        });
    }
    __k_test_provenance_path_release(path);
    start = true;
    for (auto& worker : workers) worker.join();
    for (auto& collector : collectors) collector.join();
    test_collect(UINT64_MAX);
    CHECK(failures.load() == 0);
    CHECK(observed_frees.load() - free_start == 2);
}
