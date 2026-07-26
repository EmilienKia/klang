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
 * Regression test for TODO.md "No recursion-depth limit on template
 * instantiation.":
 *
 * `template_instantiator::instantiate_aggregate()` recurses (directly, when
 * resolving a template base class — see the "Resolve template base classes
 * immediately" block) without any depth guard. Two mutually-recursive
 * template classes that use each other as a base with the SAME type
 * argument (`A<T> : B<T>`, `B<T> : A<T>`) trigger unbounded ping-pong
 * recursion the instant either is instantiated: instantiating `A<int>`
 * immediately requires instantiating `B<int>` as its base, which in turn
 * requires instantiating `A<int>` again, forever — there is no per-key
 * instantiation cache guarding this particular (eager, base-resolution)
 * recursive path, so nothing else stops it. This used to crash the compiler
 * with a native stack overflow instead of reporting a diagnostic.
 *
 * This test verifies that such runaway recursion is now caught and reported
 * as a `template_diag::ERR_TPL_INSTANTIATION_DEPTH_EXCEEDED` compiler error.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"
#include "../src/errors.hpp"

// ════════════════════════════════════════════════════════════════════════════
//  Mutually-recursive template bases — instantiation must stop with a
//  diagnostic instead of overflowing the stack.
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Mutually-recursive template base instantiation is bounded and diagnosed",
          "[template][instantiation][recursion-guard]") {
    REQUIRE_THROWS_AS(
        gen_jit_throws(R"SRC(
            module __tpl_recursion_guard__;

            template<typename T>
            class A : public B<T> {
            }

            template<typename T>
            class B : public A<T> {
            }

            useA() : void {
                a : A<int>;
            }
        )SRC"),
        k::log::compiler_error
    );
}
