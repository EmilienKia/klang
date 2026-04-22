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

#ifndef KLANG_RESOLVERS_COMMON_HPP
#define KLANG_RESOLVERS_COMMON_HPP

#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "../model/model.hpp"
#include "../model/model_visitor.hpp"
#include "../model/statements.hpp"

#include "../model/context.hpp"

#include "../common/logger.hpp"
#include "../lex/lexer.hpp"

namespace k::model::gen {

/**
 * Helper: resolve the model element targeted by a using directive's target_name.
 * Returns the element (as ns or aggregate) or nullptr if not found.
 */
std::shared_ptr<const element>
resolve_using_target(const k::name& target_name, const unit& unit);


class resolution_error : public k::log::compiler_error {
public:
    explicit resolution_error(k::log::diagnostic diag)
        : k::log::compiler_error(std::move(diag)) {}
};

} // namespace k::model::gen

#endif //KLANG_RESOLVERS_COMMON_HPP

