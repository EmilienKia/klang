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

#ifndef KLANG_GEN_INTRINSICS_HPP
#define KLANG_GEN_INTRINSICS_HPP

#include "../model/model.hpp"
#include "../parse/ast.hpp"

#include <optional>
#include <string>

namespace k::model::gen {

/**
 * If the function/constructor carries @k::annotations::Intrinsic("..."),
 * return the intrinsic name string. Otherwise return std::nullopt.
 */
std::optional<std::string> get_intrinsic_name(const function& fn);

} // namespace k::model::gen

#endif // KLANG_GEN_INTRINSICS_HPP
