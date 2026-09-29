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

#ifndef KLANG_PLUGIN_STRUCTURAL_PASS_HPP
#define KLANG_PLUGIN_STRUCTURAL_PASS_HPP

#include "../plugin.hpp"

namespace k::plugin::structural {

/**
 * Model visitor pass that performs structural code generation on aggregates.
 * Currently synthesizes accessor methods (getters and setters, inspired by Lombok)
 * for member and static variables annotated with @::k::lang::Getter or @::k::lang::Setter.
 * Can be extended with further structural generation patterns.
 */
class structural_pass : public model_pass {
public:
    std::string_view name() const override {
        return "structural";
    }

    void visit_aggregate(model::aggregate& agg) override;
};

} // namespace k::plugin::structural

#endif // KLANG_PLUGIN_STRUCTURAL_PASS_HPP
