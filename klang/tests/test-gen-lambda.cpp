/*
 * K Language compiler
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
 * Lambda lowering smoke tests.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

TEST_CASE("Lambda: capture-free lambda binds to a callable", "[gen][lambda]") {
    SKIP("Lambda return-type inference still needs destination-context binding for capture-free lambdas");
}
