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

#include "naming_plugin.hpp"
#include "naming_pass.hpp"

namespace k::plugin::naming {

naming_plugin::naming_plugin() {
    _info.name = "naming_conventions";
    _info.version = "1.0.0";
    _info.description = "Enforce K language naming conventions by automatically renaming non-conforming symbols";
    _info.author = "K Language Team";
}

bool naming_plugin::initialize(compiler& /*comp*/, plugin_registry& registry) {
    registry.register_model_pass(std::make_unique<naming_pass>());
    return true;
}

} // namespace k::plugin::naming

// ── Exported C ABI symbols for dynamic plugin loading (.so) ──────────────────

extern "C" {

uint32_t klang_plugin_api_version() {
    return KLANG_PLUGIN_API_VERSION;
}

k::plugin::plugin* klang_create_plugin() {
    return new k::plugin::naming::naming_plugin();
}

}
