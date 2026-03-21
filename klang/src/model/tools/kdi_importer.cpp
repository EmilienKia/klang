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

#include "../tools/kdi_importer.hpp"

#include "../../common/logger.hpp"
#include "../context.hpp"
#include "../imported.hpp"

#include <kdi.hpp>  // kdi_read_cbor_file, kdi_file, kdi_namespace, kdi_aggregate, …

#include <sstream>
#include <stdexcept>

namespace k::model {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

kdi_importer::kdi_importer(unit& unit,
                           const k::file_resolver& resolver,
                           k::log::logger& logger,
                           bool enforce_ns_collision)
    : _unit(unit)
    , _resolver(resolver)
    , _logger(logger)
    , _enforce_ns_collision(enforce_ns_collision)
{}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

std::string kdi_importer::canonical(const k::name& n) {
    return n.to_string();   // e.g. "math::vec"
}

void kdi_importer::register_root_ns(const std::string& module_name) {
    // Root component = first "::" separated part
    const std::string root = [&]() -> std::string {
        auto pos = module_name.find("::");
        return (pos == std::string::npos) ? module_name : module_name.substr(0, pos);
    }();

    auto it = _root_ns_owners.find(root);
    if (it != _root_ns_owners.end() && it->second != module_name) {
        // Two different modules share the same root component.
        // Allow it if one module is a sub-module of the other (same hierarchy):
        //   e.g. "k" and "k::math" → both in the "k" hierarchy → OK
        const auto& existing = it->second;
        bool same_hierarchy =
            (module_name.starts_with(existing + "::")) ||
            (existing.starts_with(module_name + "::"));
        if (!same_hierarchy) {
            auto diag = k::log::diagnostic::make_error(
                0x80001,
                "Namespace root collision: module '{}' and module '{}' share the "
                "same root namespace component '{}'",
                {module_name, existing, root});
            _logger.report(diag);
            throw k::log::compiler_error(std::move(diag));
        }
    }
    _root_ns_owners[root] = module_name;

    // When enforcement is active, the current unit's own root must not collide
    if (_enforce_ns_collision) {
        const auto& unit_name = _unit.get_unit_name();
        if (!unit_name.empty() && !unit_name.parts().empty()) {
            const std::string unit_root = unit_name.parts().front();
            if (!unit_root.empty() && unit_root == root) {
                auto diag = k::log::diagnostic::make_error(
                    0x80002,
                    "--enforce-ns-collision: imported module '{}' root '{}' "
                    "collides with the root namespace of the unit being compiled",
                    {module_name, root});
                _logger.report(diag);
                throw k::log::compiler_error(std::move(diag));
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Core: load one module (with cycle detection and caching)
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<kdi::kdi_file>
kdi_importer::load_module(const std::string& canon) {

    // ── Cycle detection (must be BEFORE cache lookup) ─────────────────────
    // A module that is already on the current load stack → circular dependency.
    // Check this *before* the cache so that a module that started loading but
    // was optimistically cached below (to break indirect cycles) does not
    // silently mask a real cycle originating from the same traversal path.
    if (_loading_set.count(canon)) {
        // Build human-readable cycle path: "A → B → C → A"
        std::string cycle_path;
        bool in_cycle = false;
        for (const auto& step : _loading_stack) {
            if (!in_cycle && step == canon) in_cycle = true;
            if (in_cycle) { cycle_path += step; cycle_path += " → "; }
        }
        cycle_path += canon;   // closing node of the cycle
        auto diag = k::log::diagnostic::make_error(
            0x80003,
            "Circular import dependency detected: {}",
            {cycle_path});
        _logger.report(diag);
        throw k::log::compiler_error(std::move(diag));
    }

    // Already in cache? (only valid after the loading-set check above)
    auto cached = _cache.find(canon);
    if (cached != _cache.end()) return cached->second;
    _loading_stack.push_back(canon);
    _loading_set.insert(canon);

    // Resolve path
    auto path_opt = _resolver.resolve(canon, ".kdi");
    if (!path_opt) {
        auto diag = k::log::diagnostic::make_error(
            0x80004,
            "Cannot find KDI description file for imported module '{}': "
            "no .kdi file found on any search path",
            {canon});
        _logger.report(diag);
        throw k::log::compiler_error(std::move(diag));
    }

    // Deserialise
    std::shared_ptr<kdi::kdi_file> kdi_ptr;
    try {
        kdi_ptr = std::make_shared<kdi::kdi_file>(kdi::kdi_read_cbor_file(path_opt->string()));
    } catch (const std::exception& ex) {
        auto diag = k::log::diagnostic::make_error(
            0x80005,
            "Failed to read or parse KDI file '{}' for module '{}': {}",
            {path_opt->string(), canon, ex.what()});
        _logger.report(diag);
        throw k::log::compiler_error(std::move(diag));
    }

    // Cache before recursing (handles indirect cycles via dependency list)
    _cache[canon] = kdi_ptr;

    // Recursively load transitive dependencies listed in the header.
    // These are not added to the unit's direct imports but are stored in
    // _transitive_kdis so that materialise_all() can intern their LLVM struct
    // types and make their symbols available for base-class resolution.
    // A missing transitive dependency is a fatal error: without it, base types
    // and vtable layouts cannot be fully reconstructed, leading to undefined
    // behaviour.  The caller (direct or indirect importer) must ensure all
    // transitive KDIs are reachable via the configured file resolver.
    for (const auto& dep_name : kdi_ptr->header.dependencies) {
        // Check for cycle FIRST (even if the module is already in cache,
        // if it is still on the loading stack that is a cycle).
        if (_loading_set.count(dep_name)) {
            // Build cycle path and report error
            std::string cycle_path;
            bool in_cycle = false;
            for (const auto& step : _loading_stack) {
                if (!in_cycle && step == dep_name) in_cycle = true;
                if (in_cycle) { cycle_path += step; cycle_path += " \u2192 "; }
            }
            cycle_path += dep_name;
            auto diag = k::log::diagnostic::make_error(
                0x80003,
                "Circular import dependency detected: {}",
                {cycle_path});
            _logger.report(diag);
            throw k::log::compiler_error(std::move(diag));
        }

        if (_cache.find(dep_name) == _cache.end()) {
            auto dep_kdi = load_module(dep_name);   // throws on failure
            if (dep_kdi) {
                bool is_direct = false;
                for (const auto& imp : _unit.get_imports()) {
                    if (canonical(imp.module_name) == dep_name) {
                        is_direct = true; break;
                    }
                }
                if (!is_direct) {
                    _transitive_kdis.push_back(dep_kdi);
                }
            }
        }
    }

    _loading_stack.pop_back();
    _loading_set.erase(canon);
    return kdi_ptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void kdi_importer::import_all() {
    // First pass: collect indices of imports that must be removed (implicit
    // imports whose KDI is not available — e.g. the base stdlib during bootstrap).
    std::vector<std::size_t> to_remove;

    for (std::size_t i = 0; i < _unit.get_imports().size(); ++i) {
        auto& imp = _unit.get_imports()[i];
        const std::string canon = canonical(imp.module_name);

        if (imp.implicit) {
            // Implicit import: try to resolve but don't fail if missing
            auto path_opt = _resolver.resolve(canon, ".kdi");
            if (!path_opt) {
                to_remove.push_back(i);
                continue;
            }
        }

        auto kdi_ptr = load_module(canon);

        // Validate namespace-root collision
        register_root_ns(canon);

        // Fill in the imported_module fields
        imp.kdi = kdi_ptr;
        auto path_opt = _resolver.resolve(canon, ".kdi");
        if (path_opt) {
            imp.resolved_kdi_path = path_opt->string();
        }
    }

    // Remove unresolved implicit imports (reverse order to keep indices valid)
    if (!to_remove.empty()) {
        auto& imports = _unit.get_imports();
        for (auto it = to_remove.rbegin(); it != to_remove.rend(); ++it) {
            imports.erase(imports.begin() + static_cast<std::ptrdiff_t>(*it));
        }
    }

    // Register transitive KDIs in the unit so that find_imported_type() and
    // friends can resolve symbols from indirectly-imported modules.
    for (const auto& tdep : _transitive_kdis) {
        _unit.add_transitive_kdi(tdep);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase B — Materialisation
// ─────────────────────────────────────────────────────────────────────────────

// ── helpers ──────────────────────────────────────────────────────────────────

/// Recursively collect all struct type definition strings from every aggregate
/// in a KDI namespace tree, appending them into @p out.  Called before any
/// model node is created so that every type definition is present in the
/// combined IR blob that will be parsed in a single LLVM module (avoiding
/// opaque forward-refs).
///
/// IMPORTANT: only aggregate struct type definitions ('%Name = type { ... }')
/// are collected here.  Vtable global variable definitions
/// ('@sym = constant %VtType { ... }') are intentionally excluded because they
/// reference undefined functions/globals, which would cause LLVM's IR parser to
/// abort before reaching subsequent struct type definitions in the combined
/// blob.  Vtable struct types are not needed during import anyway — imported
/// virtual dispatch uses byte-offset GEP, not struct GEP on the vtable type.
static void collect_llvm_defs_from_namespace(const kdi::kdi_namespace& ns,
                                              std::string& out)
{
    for (const auto& agg : ns.aggregates) {
        // Aggregate's own struct type def (e.g. '%Base = type { ptr, i32 }')
        if (!agg.llvm_def.empty()) {
            out += agg.llvm_def;
            out += '\n';
        }
        // Nested aggregates (public/protected inner types)
        for (const auto& nested : agg.nested) {
            if (!nested.llvm_def.empty()) {
                out += nested.llvm_def;
                out += '\n';
            }
        }
    }
    for (const auto& child_ns : ns.namespaces) {
        collect_llvm_defs_from_namespace(child_ns, out);
    }
}

// ─────────────────────────────────────────────────────────────────────────────

void kdi_importer::materialise_all(std::shared_ptr<context> ctx) {
    // ── Step B1: collect all llvm_def strings from every loaded KDI ─────────
    // Parse them together in one LLVM IR module so that forward-references
    // between types (e.g. %Derived referencing %Base) are resolved immediately,
    // without creating opaque placeholders that would remain unsized later.
    //
    // Include both direct imports AND transitive deps so that base-class types
    // from indirect imports are available during resolution.
    std::string combined_ir;
    combined_ir.reserve(4096);
    // Transitive deps first (bases before derived)
    for (const auto& tdep : _transitive_kdis) {
        if (!tdep) continue;
        collect_llvm_defs_from_namespace(tdep->unit.root_ns, combined_ir);
    }
    for (const auto& imp : _unit.get_imports()) {
        if (!imp.kdi) continue;
        collect_llvm_defs_from_namespace(imp.kdi->unit.root_ns, combined_ir);
    }
    if (!combined_ir.empty()) {
        ctx->intern_all_llvm_struct_defs(combined_ir);
    }

    // ── Step B2: walk every loaded KDI and materialise model nodes ──────────
    // At this point all aggregate LLVM struct types are already interned in the
    // context, so get_or_create_imported_aggregate() will find them immediately
    // via llvm::StructType::getTypeByName() instead of parsing new snippets.
    //
    // Materialise transitive deps first so that base-class model nodes are in
    // the unit's import cache before derived types (from direct imports)
    // reference them.
    for (const auto& tdep : _transitive_kdis) {
        if (!tdep) continue;
        materialise_namespace(tdep->unit.root_ns, ctx);
    }
    for (const auto& imp : _unit.get_imports()) {
        if (!imp.kdi) continue;
        materialise_namespace(imp.kdi->unit.root_ns, ctx);
    }
}

void kdi_importer::materialise_namespace(const kdi::kdi_namespace& ns,
                                          std::shared_ptr<context> ctx)
{
    // Pass 1 — aggregates (depth-first so base classes are materialised before
    // derived classes that reference them in their llvm_def).
    for (const auto& agg : ns.aggregates) {
        materialise_aggregate(agg, ctx);
    }

    // Pass 2 — enumerations in this namespace
    for (const auto& en : ns.enums) {
        materialise_enum(en, ctx);
    }

    // Pass 3 — free functions in this namespace
    for (const auto& fn : ns.functions) {
        materialise_function(fn, ctx);
    }

    // Pass 4 — global / static variables in this namespace
    for (const auto& var : ns.variables) {
        materialise_variable(var, ctx);
    }

    // Recurse into nested namespaces (same two-pass order)
    for (const auto& child_ns : ns.namespaces) {
        materialise_namespace(child_ns, ctx);
    }
}

void kdi_importer::materialise_aggregate(const kdi::kdi_aggregate& agg,
                                          std::shared_ptr<context> ctx)
{
    // Derive the k::name from fq_name.  The cache key inside
    // get_or_create_imported_aggregate() is always the non-root-prefixed form.
    const std::string& fq = agg.fq_name.empty() ? agg.name : agg.fq_name;

    // Strip leading "::" if present (normalise to "a::b::C" form).
    const std::string normalised = (fq.size() >= 2 && fq[0] == ':' && fq[1] == ':')
                                   ? fq.substr(2) : fq;

    // Build k::name parts
    std::vector<std::string> parts;
    std::size_t pos = 0;
    while (true) {
        auto sep = normalised.find("::", pos);
        if (sep == std::string::npos) { parts.push_back(normalised.substr(pos)); break; }
        parts.push_back(normalised.substr(pos, sep - pos));
        pos = sep + 2;
    }

    // Eagerly materialise — the unit method handles caching and LLVM interning.
    _unit.get_or_create_imported_aggregate(k::name{false, std::move(parts)}, ctx);

    // Materialise nested aggregates (public/protected nested types).
    for (const auto& nested : agg.nested) {
        materialise_aggregate(nested, ctx);
    }
}

void kdi_importer::materialise_function(const kdi::kdi_function& fn,
                                         std::shared_ptr<context> ctx)
{
    _unit.get_or_create_imported_function(&fn, ctx);
}

void kdi_importer::materialise_enum(const kdi::kdi_enum& en,
                                     std::shared_ptr<context> ctx)
{
    const std::string& fq = en.fq_name.empty() ? en.name : en.fq_name;
    const std::string normalised = (fq.size() >= 2 && fq[0] == ':' && fq[1] == ':')
                                   ? fq.substr(2) : fq;

    std::vector<std::string> parts;
    std::size_t pos = 0;
    while (true) {
        auto sep = normalised.find("::", pos);
        if (sep == std::string::npos) { parts.push_back(normalised.substr(pos)); break; }
        parts.push_back(normalised.substr(pos, sep - pos));
        pos = sep + 2;
    }

    _unit.get_or_create_imported_enum(k::name{false, std::move(parts)}, ctx);
}

void kdi_importer::materialise_variable(const kdi::kdi_variable& var,
                                         std::shared_ptr<context> ctx)
{
    _unit.get_or_create_imported_variable(&var, ctx);
}

void kdi_importer::check_unused_imports() const {
    for (const auto& imp : _unit.get_imports()) {
        if (!imp.used && !imp.implicit) {
            auto diag = k::log::diagnostic::make_warning(
                0x80010,
                "Imported module '{}' is declared but none of its symbols "
                "are used in this compilation unit",
                {canonical(imp.module_name)});
            _logger.report(diag);
        }
    }
}

} // namespace k::model




