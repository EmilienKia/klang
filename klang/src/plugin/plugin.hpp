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

#ifndef KLANG_PLUGIN_HPP
#define KLANG_PLUGIN_HPP

#include <string>
#include <string_view>
#include <memory>
#include <functional>
#include <vector>

#include "../model/model_visitor.hpp"

namespace k {
class compiler;

namespace model {
class unit;
class context;
class aggregate;
class ns;
}

namespace plugin {

/**
 * Metadata describing a compiler plugin.
 */
struct plugin_info {
    std::string name;
    std::string version;
    std::string description;
    std::string author;
};

class plugin_registry;

/**
 * Base class for a model visitor pass registered by a plugin.
 * Inherits from default_model_visitor to allow overriding only
 * the visitor callbacks of interest while providing default recursive
 * traversal across the model tree.
 */
class model_pass : public model::default_model_visitor {
protected:
    compiler* _compiler = nullptr;
    model::unit* _unit = nullptr;
    model::context* _context = nullptr;
    bool _modified = false;

public:
    virtual ~model_pass() = default;

    virtual std::string_view name() const = 0;

    compiler* get_compiler() const { return _compiler; }
    model::unit* get_unit() const { return _unit; }
    model::context* get_context() const { return _context; }

    void mark_modified() { _modified = true; }
    bool is_modified() const { return _modified; }

    /**
     * Entry point to execute this model pass on the unit.
     * Sets context pointers, runs unit.accept(*this), and returns whether
     * the model was modified.
     */
    virtual bool run(compiler& comp, model::unit& unit, model::context& ctx);

    // Recursive traversal defaults
    void visit_unit(model::unit& unit) override;
    void visit_namespace(model::ns& ns) override;
    void visit_aggregate(model::aggregate& st) override;
    void visit_structure(model::structure& st) override;
    void visit_klass(model::klass& kl) override;
};

/**
 * Interface for registering plugin components with the compiler.
 */
class plugin_registry {
public:
    virtual ~plugin_registry() = default;

    /**
     * Register a model visitor pass to be executed after the initial
     * resolution pass.
     */
    virtual void register_model_pass(std::unique_ptr<model_pass> pass) = 0;
};

/**
 * Base interface for all klang plugins.
 */
class plugin {
public:
    virtual ~plugin() = default;

    virtual const plugin_info& get_info() const = 0;

    /** Returns true if this plugin is enabled by default in the compiler. */
    virtual bool is_default() const { return false; }

    /**
     * Called during plugin initialization.
     * The plugin registers its passes via the provided registry.
     * @return true on success, false if initialization failed.
     */
    virtual bool initialize(compiler& comp, plugin_registry& registry) = 0;

    /**
     * Called when the compiler shuts down.
     */
    virtual void shutdown() {}
};

#define KLANG_PLUGIN_API_VERSION 1

using plugin_factory_fn = std::function<std::shared_ptr<plugin>()>;

/**
 * Register a static plugin factory.
 * Called automatically by KLANG_REGISTER_STATIC_PLUGIN.
 */
void register_static_plugin(plugin_factory_fn factory);

const std::vector<plugin_factory_fn>& get_static_plugin_factories();

} // namespace plugin
} // namespace k

/**
 * Macro to declare static registration of a plugin.
 */
#define KLANG_REGISTER_STATIC_PLUGIN(PluginClass) \
    namespace { \
    struct PluginClass##_StaticRegistration { \
        PluginClass##_StaticRegistration() { \
            ::k::plugin::register_static_plugin([]() { \
                return std::make_shared<PluginClass>(); \
            }); \
        } \
    } _static_reg_##PluginClass; \
    }

#endif // KLANG_PLUGIN_HPP
