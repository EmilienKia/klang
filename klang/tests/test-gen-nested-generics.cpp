/*
 * K Language compiler
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
 * Regression tests for compiler bugs found while implementing the Map<K,V>
 * stdlib collection (libk/libk/src/map.k). Klang's template machinery
 * historically assumed template arguments were always simple identifiers
 * (e.g. `Holder<int>`), which broke down for legitimately nested/compound
 * generic arguments such as `Holder<Pair<int>>` or a base-class list like
 * `Box<Pair<K>>` where the argument is itself a template instantiation.
 *
 * Each TEST_CASE below isolates one of the fixed code paths:
 *  - "owner-variable declaration with nested generic type" exercises
 *    `type_reference_resolver::try_instantiate_template_type`
 *    (klang/src/gen/resolvers_type_ref.cpp), which needed to recursively
 *    resolve compound nested-generic template arguments instead of only
 *    handling a single level of instantiation.
 *  - "template class with a nested-generic base class + upcast" exercises
 *    both `substitute_base_name` (base-class name substitution) and
 *    `resolve_base_type_name` (base-class type resolution) in
 *    klang/src/model/template_instantiator.cpp, which needed depth-aware
 *    comma splitting and recursion into compound base-class-name arguments.
 */

#include <catch2/catch_all.hpp>
#include "helpers.hpp"

// Regression test for `try_instantiate_template_type` not recursively
// resolving a nested/compound generic template argument. Declaring an owner
// variable of type `Holder<Pair<int>>!` (a template class instantiated with
// another template instantiation as its argument) used to fail to resolve
// the inner `Pair<int>` type while instantiating the outer `Holder<...>`.
TEST_CASE("Owner variable with nested generic type argument resolves and runs", "[gen][template][nested-generic][regression]") {
    auto jit = gen_jit(R"SRC(
module gen_nested_generics_01;
template<typename T>
struct Pair {
    a: T;
    b: T;
    Pair(x: T, y: T) : a(x), b(y) {}
}
template<typename T>
class Holder {
    v: T;
    Holder(x: T) : v(x) {}
    get() : T { return v; }
}
test() : int {
    p: Pair<int>(1, 2);
    h: Holder<Pair<int>>!(new Holder<Pair<int>>(p));
    return h.get().a + h.get().b;
}
)SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 3);
}

// Regression test for `substitute_base_name` / `resolve_base_type_name` not
// handling a nested/compound generic argument in a base-class list. A
// template class `Impl<K,V>` deriving from `Box<Pair<K>>` (the base's own
// template argument `Pair<K>` is itself a template instantiation depending on
// the derived class's own template parameter `K`) used to fail to resolve or
// mis-substitute the base type name, breaking the upcast from `Impl<K,V>` to
// `Box<Pair<K>>`.
TEST_CASE("Template class with a nested-generic base class upcasts and dispatches correctly", "[gen][template][nested-generic][regression]") {
    auto jit = gen_jit(R"SRC(
module gen_nested_generics_02;
template<typename T>
struct Pair {
    a: T;
    b: T;
    Pair(x: T, y: T) : a(x), b(y) {}
}
template<typename T>
interface Box {
    get() : T;
}
template<typename K, typename V>
class Impl : public Box<Pair<K>> {
    p: Pair<K>;
    Impl(x: K, y: K) : p(x, y) {}
    get() : Pair<K> { return p; }
}
use_box(b: Box<Pair<int>>&) : int {
    p: Pair<int> = b.get();
    return p.a + p.b;
}
test() : int {
    i: Impl<int,int>(3, 4);
    return use_box(i);
}
)SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 7);
}
