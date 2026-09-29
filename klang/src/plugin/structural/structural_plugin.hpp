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

#ifndef KLANG_PLUGIN_STRUCTURAL_PLUGIN_HPP
#define KLANG_PLUGIN_STRUCTURAL_PLUGIN_HPP

#include "../plugin.hpp"

namespace k::plugin::structural {

void register_static_structural_plugin();

class structural_plugin : public plugin {
private:
    plugin_info _info;

public:
    structural_plugin();
    const plugin_info& get_info() const override { return _info; }
    bool is_default() const override { return true; }
    bool initialize(compiler& comp, plugin_registry& registry) override;
};

} // namespace k::plugin::structural

#endif // KLANG_PLUGIN_STRUCTURAL_PLUGIN_HPP
