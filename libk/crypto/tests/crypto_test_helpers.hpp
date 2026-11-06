/*
 * K Language crypto standard library test helpers
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

#ifndef LIBK_CRYPTO_TEST_HELPERS_HPP
#define LIBK_CRYPTO_TEST_HELPERS_HPP

#include <memory>
#include <string_view>

#include "../../../klang/tests/helpers.hpp"

std::unique_ptr<k::model::gen::jit> gen_jit_with_crypto(std::string_view source);

#endif
