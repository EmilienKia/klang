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

#include "plugin_manager.hpp"
#include "structural/structural_plugin.hpp"
#include "../compiler.hpp"
#include "../model/model.hpp"
#include "../model/model_aggregate.hpp"
#include "../model/model_ns.hpp"

#include <dlfcn.h>
#include <iostream>

namespace k::plugin {

// ── Static registry implementation ──────────────────────────────────────────

static std::vector<plugin_factory_fn>& static_plugin_registry() {
    static std::vector<plugin_factory_fn> factories;
    return factories;
}

void register_static_plugin(plugin_factory_fn factory) {
    static_plugin_registry().push_back(std::move(factory));
}

const std::vector<plugin_factory_fn>& get_static_plugin_factories() {
    return static_plugin_registry();
}

// ── model_pass default recursive traversal ─────────────────────────────────

bool model_pass::run(compiler& comp, model::unit& unit, model::context& ctx) {
    _compiler = &comp;
    _unit = &unit;
    _context = &ctx;
    _modified = false;

    unit.accept(*this);

    return _modified;
}

void model_pass::visit_unit(model::unit& unit) {
    if (auto root = unit.get_root_namespace()) {
        root->accept(*this);
    }
}

void model_pass::visit_namespace(model::ns& ns) {
    // Copy children list to avoid iterator invalidation if the pass adds elements
    auto children = ns.get_children();
    for (auto& child : children) {
        if (child) child->accept(*this);
    }
}

void model_pass::visit_aggregate(model::aggregate& st) {
    // Copy children list to avoid iterator invalidation if the pass adds elements
    auto children = st.get_children();
    for (auto& child : children) {
        if (child) child->accept(*this);
    }
}

void model_pass::visit_structure(model::structure& st) {
    visit_aggregate(st);
}

void model_pass::visit_klass(model::klass& kl) {
    visit_aggregate(kl);
}

// ── plugin_manager implementation ──────────────────────────────────────────

plugin_manager::~plugin_manager() {
    for (auto& p : _plugins) {
        if (p) p->shutdown();
    }
    _model_passes.clear();
    _plugins.clear();

    for (void* handle : _dl_handles) {
        if (handle) dlclose(handle);
    }
    _dl_handles.clear();
}

void plugin_manager::register_model_pass(std::unique_ptr<model_pass> pass) {
    if (pass) {
        _model_passes.push_back(std::move(pass));
    }
}

void plugin_manager::init_static_plugins(compiler& comp) {
    if (_static_plugins_initialized) return;
    _static_plugins_initialized = true;

    // Ensure default built-in static plugins are registered
    structural::register_static_structural_plugin();

    for (const auto& factory : get_static_plugin_factories()) {
        if (!factory) continue;
        auto p = factory();
        if (p && is_plugin_enabled(*p)) {
            if (p->initialize(comp, *this)) {
                _plugins.push_back(std::move(p));
            }
        }
    }
}

bool plugin_manager::load_dynamic_plugin(const std::filesystem::path& path, compiler& comp) {
    // Clear any previous dlerror
    dlerror();

    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* err = dlerror();
        std::cerr << "Failed to load plugin '" << path.string() << "': "
                  << (err ? err : "unknown error") << std::endl;
        return false;
    }

    // Check optional API version
    using api_version_fn = uint32_t (*)();
    auto version_fn = reinterpret_cast<api_version_fn>(dlsym(handle, "klang_plugin_api_version"));
    if (version_fn) {
        uint32_t ver = version_fn();
        if (ver != KLANG_PLUGIN_API_VERSION) {
            std::cerr << "Plugin '" << path.string() << "' API version mismatch: expected "
                      << KLANG_PLUGIN_API_VERSION << ", got " << ver << std::endl;
            dlclose(handle);
            return false;
        }
    }

    // Check for factory entry point: klang_create_plugin()
    using create_plugin_fn = plugin* (*)();
    auto create_fn = reinterpret_cast<create_plugin_fn>(dlsym(handle, "klang_create_plugin"));
    if (create_fn) {
        plugin* raw_plugin = create_fn();
        if (!raw_plugin) {
            std::cerr << "klang_create_plugin() returned nullptr in '" << path.string() << "'" << std::endl;
            dlclose(handle);
            return false;
        }
        std::shared_ptr<plugin> p(raw_plugin);
        if (!p->initialize(comp, *this)) {
            std::cerr << "Plugin '" << path.string() << "' initialization failed" << std::endl;
            dlclose(handle);
            return false;
        }
        _plugins.push_back(std::move(p));
        _dl_handles.push_back(handle);
        return true;
    }

    // Check for direct init entry point: klang_plugin_init(compiler&, plugin_registry&)
    using init_plugin_fn = bool (*)(compiler&, plugin_registry&);
    auto init_fn = reinterpret_cast<init_plugin_fn>(dlsym(handle, "klang_plugin_init"));
    if (init_fn) {
        if (!init_fn(comp, *this)) {
            std::cerr << "klang_plugin_init() failed in '" << path.string() << "'" << std::endl;
            dlclose(handle);
            return false;
        }
        _dl_handles.push_back(handle);
        return true;
    }

    std::cerr << "Plugin '" << path.string()
              << "' does not export 'klang_create_plugin' or 'klang_plugin_init'" << std::endl;
    dlclose(handle);
    return false;
}

} // namespace k::plugin
