/*
 * K Language compiler
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

#ifndef KLANG_KDI_IMPORTER_HPP
#define KLANG_KDI_IMPORTER_HPP

#include "../model.hpp"
#include "../import.hpp"
#include "../../common/file_resolver.hpp"
#include "../../common/logger.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

// Forward declarations (avoid pulling all of libkdi in model headers)
namespace kdi {
    struct kdi_file;
    struct kdi_namespace;
    struct kdi_aggregate;
    struct kdi_function;
    struct kdi_variable;
    struct kdi_template_def;
    struct kdi_union;
    struct kdi_alias;
}

namespace k::model {

class context; // forward (defined in context.hpp)

/**
 * Resolves, loads and materialises the .kdi files for all imports declared
 * in a compilation unit.
 *
 * ## Responsibilities
 *
 * ### Phase A — Loading (`import_all()`)
 *   - Walk unit._imported_modules and call the file_resolver for each.
 *   - Deserialise each .kdi with kdi_read_cbor_file().
 *   - Validate namespace-root collisions between different imported modules.
 *   - Fill in imported_module::resolved_kdi_path and imported_module::kdi.
 *
 * ### Phase B — Materialisation (`materialise_all()`)
 *   - For every aggregate / function / variable in every loaded KDI, eagerly
 *     create the corresponding imported_* model node (via the unit's
 *     get_or_create_*() methods) and register LLVM struct types in context.
 *   - After this phase, all resolver passes can find every imported symbol
 *     without any further on-demand creation.
 *
 * These two phases are separated so that, in the future, `materialise_all()`
 * can be replaced by on-demand calls to the same unit helper methods without
 * changing any other code.
 *
 * ### Phase C — Unused-import check (`check_unused_imports()`)
 *   - Emit a warning for every declared import with used == false.
 *   - Call once, after symbol and type resolution is complete.
 */
class kdi_importer {
public:
    /**
     * @param unit       The compilation unit whose imports are to be loaded.
     * @param resolver   File resolver used to locate .kdi files on disk.
     * @param logger     Diagnostic logger (errors / warnings).
     * @param enforce_ns_collision
     *          When true, the namespace root of the unit being compiled must
     *          also not collide with any imported module's root.
     *          When false (default), only collisions between two different
     *          imports are rejected; the current unit always prevails.
     */
    kdi_importer(unit& unit,
                 const k::file_resolver& resolver,
                 k::log::logger& logger,
                 bool enforce_ns_collision = false);

    // ── Phase A ──────────────────────────────────────────────────────────────

    /**
     * Load all imports declared in the unit.
     * Throws k::log::compiler_error on:
     *   - .kdi file not found (fatal)
     *   - Circular dependency
     *   - Namespace-root collision between two distinct imports
     *   - (optionally) namespace-root collision with the current unit
     *
     * After this call, every imported_module entry has its kdi field set.
     * No model nodes are created yet — call materialise_all() next.
     */
    void import_all();

    // ── Phase B ──────────────────────────────────────────────────────────────

    /**
     * Eagerly materialise every aggregate, function and variable from every
     * loaded KDI file into the unit's imported_* model caches.
     *
     * After this call every imported symbol has a corresponding model node
     * (imported_aggregate / imported_function / imported_variable) and its
     * LLVM StructType is registered in @p ctx.  Resolver passes can then
     * find imported types by name without any further on-demand creation.
     *
     * Design note: this method intentionally delegates the actual node
     * creation to unit::get_or_create_imported_*().  The materialise_*
     * helpers below are thin wrappers that walk the KDI tree and call those
     * unit methods.  Transitioning to lazy loading in the future only
     * requires not calling materialise_all() — all other code stays the same.
     *
     * @param ctx  The compilation context (needed to intern LLVM struct types).
     */
    void materialise_all(std::shared_ptr<context> ctx);

    // ── Phase C ──────────────────────────────────────────────────────────────

    /**
     * Emit a warning for every imported_module with used == false.
     * Call once, after symbol and type resolution is complete.
     */
    void check_unused_imports() const;

private:
    unit&                       _unit;
    const k::file_resolver&     _resolver;
    k::log::logger&             _logger;
    bool                        _enforce_ns_collision;

    /// Cache: canonical module name → loaded kdi_file (avoids double loading)
    std::unordered_map<std::string, std::shared_ptr<kdi::kdi_file>> _cache;

    /// Stack of module names currently on the load path (order preserved for
    /// cycle-path reporting).  Paired with _loading_set for O(1) membership test.
    std::vector<std::string> _loading_stack;
    std::unordered_set<std::string> _loading_set;

    /// Set of root-namespace components already claimed by a loaded import.
    std::unordered_map<std::string, std::string> _root_ns_owners;

    /// KDI files loaded as transitive dependencies (not direct imports of the
    /// current unit).  These are materialised alongside direct imports so that
    /// base-class types from indirect deps are available during resolution.
    std::vector<std::shared_ptr<kdi::kdi_file>> _transitive_kdis;

    // ── Phase A helpers ──────────────────────────────────────────────────────

    /// Load one module by name (may be called recursively for dependencies).
    /// @param lexeme  Best-effort source location of the 'import' declaration
    ///                that (directly or transitively) triggered this load.
    std::shared_ptr<kdi::kdi_file>
    load_module(const std::string& canonical_name, const lex::opt_any_lexeme& lexeme = std::nullopt);

    /// Register the root namespace component of an import; throw on collision.
    void register_root_ns(const std::string& module_name, const lex::opt_any_lexeme& lexeme = std::nullopt);

    // ── Phase B helpers ──────────────────────────────────────────────────────

    /**
     * Materialise all symbols inside a kdi_namespace (recursively descends
     * into nested namespaces).
     *
     * Aggregates are materialised before functions and variables so that type
     * references inside function signatures resolve correctly.
     */
    void materialise_namespace(const kdi::kdi_namespace& ns,
                               std::shared_ptr<context> ctx);

    /**
     * Materialise a single aggregate (struct / class / interface) and all its
     * nested aggregates.  Delegates to unit::get_or_create_imported_aggregate().
     * The fq_name of @p agg is used as the lookup key.
     */
    void materialise_aggregate(const kdi::kdi_aggregate& agg,
                               std::shared_ptr<context> ctx);

    /**
     * Materialise a single enumeration.
     * Delegates to unit::get_or_create_imported_enum().
     */
    void materialise_enum(const kdi::kdi_enum& en,
                          std::shared_ptr<context> ctx);

    /**
     * Materialise a single global / namespace-level function.
     * Delegates to unit::get_or_create_imported_function().
     */
    void materialise_function(const kdi::kdi_function& fn,
                              std::shared_ptr<context> ctx);

    /**
     * Materialise a single global / static variable.
     * Delegates to unit::get_or_create_imported_variable().
     */
    void materialise_variable(const kdi::kdi_variable& var,
                              std::shared_ptr<context> ctx);

    /**
     * Materialise a single exported alias / typedef declaration into the
     * matching namespace (or aggregate) of the model.
     *
     * The alias keeps its nominal identity: a strong alias (typedef) gets its
     * own alias_type, a soft alias resolves directly to the aliased type.
     */
    void materialise_alias(const kdi::kdi_alias& al,
                           std::shared_ptr<context> ctx);

    /**
     * Materialise a single discriminated union.
     * Creates a union_type_def in the target namespace with resolved alternatives.
     */
    void materialise_union(const kdi::kdi_union& un,
                           std::shared_ptr<context> ctx);

    /**
     * Materialise a template definition by re-parsing its source text and
     * building it into the unit's model as a template aggregate or function.
     * This makes the template available for re-instantiation by the consumer.
     */
    void materialise_template_def(const kdi::kdi_template_def& tdef,
                                  const kdi::kdi_namespace& parent_kdi_ns,
                                  std::shared_ptr<context> ctx);

    // ── Shared utilities ─────────────────────────────────────────────────────

    /// Returns the canonical (to_string()) form of a k::name.
    static std::string canonical(const k::name& n);
};

} // namespace k::model

#endif // KLANG_KDI_IMPORTER_HPP

