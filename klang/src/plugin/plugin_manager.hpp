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

#ifndef KLANG_PLUGIN_MANAGER_HPP
#define KLANG_PLUGIN_MANAGER_HPP

#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <set>

#include "plugin.hpp"

namespace k::plugin {

class plugin_manager : public plugin_registry {
protected:
    std::vector<std::shared_ptr<plugin>> _plugins;
    std::vector<std::unique_ptr<model_pass>> _model_passes;
    std::vector<void*> _dl_handles;

    bool _enable_default_plugins = true;
    std::set<std::string> _disabled_plugins;
    bool _static_plugins_initialized = false;

public:
    plugin_manager() = default;
    ~plugin_manager();

    // plugin_registry interface
    void register_model_pass(std::unique_ptr<model_pass> pass) override;

    void set_enable_default_plugins(bool enable) { _enable_default_plugins = enable; }
    bool are_default_plugins_enabled() const { return _enable_default_plugins; }

    void disable_plugin(const std::string& name) { _disabled_plugins.insert(name); }
    bool is_plugin_disabled(const std::string& name) const { return _disabled_plugins.contains(name); }

    bool is_plugin_enabled(const plugin& p) const {
        if (_disabled_plugins.contains(p.get_info().name)) return false;
        if (p.is_default() && !_enable_default_plugins) return false;
        return true;
    }

    /**
     * Initialize all plugins registered statically in the binary (if not already done).
     */
    void init_static_plugins(compiler& comp);

    /**
     * Load a shared library plugin (.so) dynamically.
     * @param path File system path to the .so library.
     * @param comp Compiler instance.
     * @return true on success, false on error.
     */
    bool load_dynamic_plugin(const std::filesystem::path& path, compiler& comp);

    /**
     * Returns true if any model passes are registered.
     */
    bool has_model_passes() const { return !_model_passes.empty(); }

    /**
     * Direct access to registered model passes.
     */
    const std::vector<std::unique_ptr<model_pass>>& get_model_passes() const {
        return _model_passes;
    }

    /**
     * Access to loaded plugins.
     */
    const std::vector<std::shared_ptr<plugin>>& get_plugins() const {
        return _plugins;
    }
};

} // namespace k::plugin

#endif // KLANG_PLUGIN_MANAGER_HPP
