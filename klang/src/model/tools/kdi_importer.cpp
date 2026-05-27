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
#include "../model_builder.hpp"
#include "kdi_type_converter.hpp"
#include "../../parse/parser.hpp"

#include <kdi.hpp>  // kdi_read_cbor_file, kdi_file, kdi_namespace, kdi_aggregate, …

#include <sstream>
#include <stdexcept>
#include "../../errors.hpp"

namespace k::model {

namespace {

template_param_kind template_param_kind_from_kdi(const std::string& kind) {
    if (kind == "struct") return template_param_kind::STRUCT;
    if (kind == "class") return template_param_kind::CLASS;
    if (kind == "interface") return template_param_kind::INTERFACE;
    if (kind == "value") return template_param_kind::VALUE;
    return template_param_kind::TYPENAME;
}

std::unique_ptr<tpl_info> build_tpl_info_from_kdi(const kdi::kdi_template_def& tdef,
                                                  unit& owner,
                                                  const std::shared_ptr<context>& ctx) {
    auto ti = std::make_unique<tpl_info>();
    ti->is_generic = tdef.is_generic;
    ti->is_imported_signature_only = tdef.is_generic;
    for (const auto& param : tdef.params) {
        template_param_descriptor desc;
        desc.kind = template_param_kind_from_kdi(param.kind);
        desc.name = param.name;
        if (param.constraint_type) {
            desc.constraint_type = kdi_type_to_model_type(*param.constraint_type, owner, ctx);
        }
        if (param.default_type) {
            desc.default_type = kdi_type_to_model_type(*param.default_type, owner, ctx);
        }
        if (param.value_type) {
            desc.value_type = kdi_type_to_model_type(*param.value_type, owner, ctx);
        }
        ti->params.push_back(std::move(desc));
    }
    return ti;
}

void populate_template_signature_aggregate(aggregate& agg,
                                           const kdi::kdi_aggregate& sig,
                                           unit& owner,
                                           const std::shared_ptr<context>& ctx) {
    // Restore base class declarations (raw_name only — resolved during instantiation)
    for (const auto& kb : sig.bases) {
        visibility vis = (kb.visibility == kdi::kdi_visibility::protected_) ? PROTECTED : PUBLIC;
        agg.add_base(kb.fq_name, vis);
    }

    for (const auto& field : sig.layout) {
        auto* member = std::get_if<kdi::kdi_layout_member>(&field);
        if (!member) continue;
        auto var = agg.append_variable(member->name, false);
        auto mv = std::dynamic_pointer_cast<member_variable_definition>(var);
        if (!mv) continue;
        mv->set_visibility(member->visibility == kdi::kdi_visibility::protected_ ? PROTECTED : PUBLIC);
        mv->set_const(member->is_const);
        mv->set_type(kdi_type_to_model_type(member->type, owner, ctx));
    }

    for (const auto& ctor_sig : sig.constructors) {
        auto fn = agg.define_function(agg.get_short_name(), false);
        auto ctor = std::dynamic_pointer_cast<constructor>(fn);
        if (!ctor) continue;
        ctor->set_visibility(ctor_sig.visibility == kdi::kdi_visibility::protected_ ? PROTECTED : PUBLIC);
        ctor->set_copy_constructor(ctor_sig.is_copy_constructor);
        ctor->set_aliasing(ctor_sig.is_deleted ? function::function_aliasing::DELETE
                                              : (ctor_sig.is_defaulted ? function::function_aliasing::DEFAULT
                                                                       : function::function_aliasing::NONE));
        for (const auto& param : ctor_sig.params) {
            auto p = ctor->append_parameter(param.name, kdi_type_to_model_type(param.type, owner, ctx));
            if (p) p->set_varargs(param.is_varargs);
        }
    }

    for (const auto& method_sig : sig.methods) {
        auto fn = agg.define_function(method_sig.name, method_sig.is_static);
        if (!fn) continue;
        fn->set_visibility(method_sig.visibility == kdi::kdi_visibility::protected_ ? PROTECTED : PUBLIC);
        fn->set_const_member(method_sig.is_const_member);
        fn->set_virtual(method_sig.is_virtual);
        fn->set_abstract_func(method_sig.is_abstract);
        fn->set_final_func(method_sig.is_final);
        fn->set_operator(method_sig.is_operator);
        auto ret_type = kdi_type_to_model_type(method_sig.return_type, owner, ctx);
        if (ret_type) fn->set_return_type(ret_type);
        for (const auto& param : method_sig.params) {
            auto p = fn->append_parameter(param.name, kdi_type_to_model_type(param.type, owner, ctx));
            if (p) p->set_varargs(param.is_varargs);
        }
        // Import throws spec
        for (const auto& ts : method_sig.throws_spec) {
            auto exc_type = kdi_type_to_model_type(ts, owner, ctx);
            if (exc_type) fn->add_throws_type(exc_type);
        }
    }
}

void populate_template_signature_function(function& fn,
                                          const kdi::kdi_function& sig,
                                          unit& owner,
                                          const std::shared_ptr<context>& ctx) {
    fn.set_visibility(sig.visibility == kdi::kdi_visibility::protected_ ? PROTECTED : PUBLIC);
    fn.set_operator(sig.is_operator);
    auto ret_type = kdi_type_to_model_type(sig.return_type, owner, ctx);
    if (ret_type) fn.set_return_type(ret_type);
    for (const auto& param : sig.params) {
        auto p = fn.append_parameter(param.name, kdi_type_to_model_type(param.type, owner, ctx));
        if (p) p->set_varargs(param.is_varargs);
    }
    // Import throws spec
    for (const auto& ts : sig.throws_spec) {
        auto exc_type = kdi_type_to_model_type(ts, owner, ctx);
        if (exc_type) fn.add_throws_type(exc_type);
    }
}

} // namespace

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
                static_cast<unsigned int>(k::diag::compiler_diag::ERR_NS_ROOT_COLLISION),
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
                    static_cast<unsigned int>(k::diag::compiler_diag::ERR_NS_COLLISION_ENFORCED),
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
            static_cast<unsigned int>(k::diag::compiler_diag::ERR_CIRCULAR_IMPORT),
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
            static_cast<unsigned int>(k::diag::compiler_diag::ERR_KDI_NOT_FOUND),
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
            static_cast<unsigned int>(k::diag::compiler_diag::ERR_KDI_PARSE_FAILED),
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
                static_cast<unsigned int>(k::diag::compiler_diag::ERR_CIRCULAR_IMPORT),
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
        // Nested unions (public/protected inner unions)
        for (const auto& nested_un : agg.nested_unions) {
            if (!nested_un.llvm_def.empty()) {
                out += nested_un.llvm_def;
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

    // Pass 2b — unions in this namespace
    for (const auto& un : ns.unions) {
        materialise_union(un, ctx);
    }

    // Pass 3 — free functions in this namespace
    for (const auto& fn : ns.functions) {
        materialise_function(fn, ctx);
    }

    // Pass 4 — global / static variables in this namespace
    for (const auto& var : ns.variables) {
        materialise_variable(var, ctx);
    }

    // Pass 5 — template definitions (for cross-module re-instantiation)
    for (const auto& tdef : ns.template_defs) {
        materialise_template_def(tdef, ns, ctx);
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

    // Materialise nested unions (public/protected nested unions).
    for (const auto& nested_un : agg.nested_unions) {
        materialise_union(nested_un, ctx);
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

void kdi_importer::materialise_union(const kdi::kdi_union& un,
                                      std::shared_ptr<context> ctx)
{
    const std::string& fq = un.fq_name.empty() ? un.name : un.fq_name;
    const std::string normalised = (fq.size() >= 2 && fq[0] == ':' && fq[1] == ':')
                                   ? fq.substr(2) : fq;

    // Parse fq_name into name parts to locate / create the target namespace
    std::vector<std::string> parts;
    std::size_t pos = 0;
    while (true) {
        auto sep = normalised.find("::", pos);
        if (sep == std::string::npos) { parts.push_back(normalised.substr(pos)); break; }
        parts.push_back(normalised.substr(pos, sep - pos));
        pos = sep + 2;
    }

    // Navigate to parent namespace (all parts except the last)
    auto target_ns = _unit.get_root_namespace();
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        target_ns = target_ns->get_child_namespace(parts[i]);
    }

    // Check if union already exists
    const std::string& union_name = parts.back();
    for (auto& [uname, udef] : target_ns->unions()) {
        if (uname == union_name) return; // already materialised
    }

    // Create the union in the target namespace
    auto udef = target_ns->define_union(union_name);
    if (!udef) return;

    udef->set_visibility(un.visibility == kdi::kdi_visibility::public_ ? PUBLIC :
                         un.visibility == kdi::kdi_visibility::protected_ ? PROTECTED : PRIVATE);

    // Create an opaque LLVM struct type immediately so that imported function
    // parameters referencing this union resolve to the same struct_type instance.
    {
        auto& llvm_ctx = ctx->llvm_context();
        auto* union_llvm_type = llvm::StructType::create(llvm_ctx, un.mangled_name + "_union");
        auto st_type = std::make_shared<struct_type>(normalised, std::weak_ptr<aggregate>{});
        ctx->attach_llvm_struct_type(st_type, union_llvm_type);
        udef->set_struct_type(st_type);
        ctx->add_struct(st_type);
    }

    // Add alternatives and resolve their types immediately using kdi_type_to_model_type.
    // This ensures that declaration_generator::visit_union can compute the correct
    // union body layout (max alternative size) for imported unions.
    for (auto& alt : un.alternatives) {
        // Convert kdi_type to a raw type name (for display/debug)
        std::string raw_type_name;
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, kdi::kdi_int_type>) {
                if (arg.is_signed) {
                    switch (arg.bits) {
                        case 8:  raw_type_name = "byte"; break;
                        case 16: raw_type_name = "short"; break;
                        case 32: raw_type_name = "int"; break;
                        case 64: raw_type_name = "long"; break;
                        default: raw_type_name = "int"; break;
                    }
                } else {
                    switch (arg.bits) {
                        case 8:  raw_type_name = "ubyte"; break;
                        case 16: raw_type_name = "ushort"; break;
                        case 32: raw_type_name = "uint"; break;
                        case 64: raw_type_name = "ulong"; break;
                        default: raw_type_name = "uint"; break;
                    }
                }
            } else if constexpr (std::is_same_v<T, kdi::kdi_float_type>) {
                raw_type_name = (arg.bits == 64) ? "double" : "float";
            } else if constexpr (std::is_same_v<T, kdi::kdi_bool_type>) {
                raw_type_name = "bool";
            } else if constexpr (std::is_same_v<T, kdi::kdi_char_type>) {
                raw_type_name = "char";
            } else if constexpr (std::is_same_v<T, kdi::kdi_aggregate_ref>) {
                raw_type_name = arg.fq_name;
            } else if constexpr (std::is_same_v<T, kdi::kdi_enum_ref>) {
                raw_type_name = arg.fq_name;
            } else {
                raw_type_name = "?"; // placeholder for complex types
            }
        }, alt.type.value);
        udef->add_alternative(alt.name, raw_type_name, alt.is_const);

        // Resolve the type immediately so declaration_generator can compute layout
        auto resolved = kdi_type_to_model_type(alt.type, _unit, ctx);
        if (resolved) {
            udef->alternatives_mutable().back().resolved_type = resolved;
        }
    }

    // Synthesize the Kind enum for the imported union so that
    // UnionName::Kind::entry syntax works across modules.
    udef->synthesize_kind_enum();
    if (auto kind_enum = udef->get_kind_enum()) {
        if (!kind_enum->get_enum_type()) {
            auto uint_type = ctx->from_type(primitive_type::UNSIGNED_INT);
            kind_enum->set_underlying_type(uint_type);
            auto et = std::shared_ptr<enum_type>(new enum_type(kind_enum, uint_type));
            kind_enum->set_enum_type(et);
            std::string efq = kind_enum->get_fq_name();
            if (efq.empty()) efq = kind_enum->get_short_name();
            ctx->add_enum(efq, et);
        }
    }

    // ── Link the base union (if declared) ────────────────────────────────────
    // The base union must already be materialised (transitive dependency loading
    // guarantees this). If it can be found, set up the inheritance link and
    // reindex own alternatives so global discriminant values are correct.
    if (!un.base_union_fq_name.empty()) {
        // Look up the base union in the root namespace by FQ name
        auto root_ns = _unit.get_root_namespace();
        if (root_ns) {
            // Navigate the FQ name to find the base union_type_def
            std::vector<std::string> base_parts;
            std::size_t pos = 0;
            while (true) {
                auto s = un.base_union_fq_name.find("::", pos);
                if (s == std::string::npos) { base_parts.push_back(un.base_union_fq_name.substr(pos)); break; }
                base_parts.push_back(un.base_union_fq_name.substr(pos, s - pos));
                pos = s + 2;
            }
            std::shared_ptr<ns> cur_ns = root_ns;
            for (size_t i = 0; i + 1 < base_parts.size(); ++i) {
                if (cur_ns) cur_ns = cur_ns->get_child_namespace(base_parts[i]);
            }
            if (cur_ns && !base_parts.empty()) {
                auto base_udef = cur_ns->get_union(base_parts.back());
                if (base_udef) {
                    udef->set_base_union_raw_name(un.base_union_fq_name);
                    udef->set_base_union(base_udef);
                    udef->reindex_own_alternatives();
                    // Re-synthesize Kind enum now that the full chain is known
                    udef->resynthesise_kind_enum();
                    if (auto kind_enum2 = udef->get_kind_enum()) {
                        if (!kind_enum2->get_enum_type()) {
                            auto uint_type = ctx->from_type(primitive_type::UNSIGNED_INT);
                            kind_enum2->set_underlying_type(uint_type);
                            auto et2 = std::shared_ptr<enum_type>(new enum_type(kind_enum2, uint_type));
                            kind_enum2->set_enum_type(et2);
                            std::string efq2 = kind_enum2->get_fq_name();
                            if (efq2.empty()) efq2 = kind_enum2->get_short_name();
                            ctx->add_enum(efq2, et2);
                        }
                    }
                }
            }
        }
    }
}

void kdi_importer::materialise_variable(const kdi::kdi_variable& var,
                                         std::shared_ptr<context> ctx)
{
    _unit.get_or_create_imported_variable(&var, ctx);
}

void kdi_importer::materialise_template_def(const kdi::kdi_template_def& tdef,
                                             const kdi::kdi_namespace& parent_kdi_ns,
                                             std::shared_ptr<context> ctx)
{
    const bool generic_signature_only =
        tdef.is_generic &&
        (tdef.aggregate_signature != nullptr ||
         (tdef.function_signature != nullptr && tdef.source.empty()));

    if (generic_signature_only) {
        std::shared_ptr<ns> target_ns = _unit.get_root_namespace();
        const std::string& ns_fq = parent_kdi_ns.fq_name;
        if (!ns_fq.empty()) {
            std::size_t pos = 0;
            while (true) {
                auto sep = ns_fq.find("::", pos);
                std::string part = (sep == std::string::npos) ? ns_fq.substr(pos) : ns_fq.substr(pos, sep - pos);
                if (!part.empty()) {
                    target_ns = target_ns->get_child_namespace(part);
                }
                if (sep == std::string::npos) break;
                pos = sep + 2;
            }
        }

        std::unordered_set<std::string> param_names;
        for (const auto& param : tdef.params) {
            if (!param.name.empty()) param_names.insert(param.name);
        }
        ctx->push_template_param_scope(param_names);

        auto ti = build_tpl_info_from_kdi(tdef, _unit, ctx);

        if (tdef.aggregate_signature) {
            if (auto existing = target_ns->get_aggregate(tdef.name)) {
                if (existing->is_template()) {
                    ctx->pop_template_param_scope();
                    return;
                }
            }

            std::shared_ptr<aggregate> tpl_agg;
            switch (tdef.aggregate_signature->kind) {
                case kdi::kdi_aggregate_kind::class_:
                    tpl_agg = target_ns->define_class(tdef.name);
                    break;
                case kdi::kdi_aggregate_kind::interface_:
                    tpl_agg = target_ns->define_interface(tdef.name);
                    break;
                case kdi::kdi_aggregate_kind::annotation_:
                    tpl_agg = target_ns->define_annotation(tdef.name);
                    break;
                case kdi::kdi_aggregate_kind::struct_:
                default:
                    tpl_agg = target_ns->define_structure(tdef.name);
                    break;
            }

            if (tpl_agg) {
                tpl_agg->set_visibility(tdef.visibility == "protected" ? PROTECTED : PUBLIC);
                tpl_agg->set_tpl_info(std::move(ti));
                populate_template_signature_aggregate(*tpl_agg, *tdef.aggregate_signature, _unit, ctx);
            }
        } else if (tdef.function_signature) {
            if (auto existing = target_ns->get_function(tdef.name)) {
                if (existing->is_template()) {
                    ctx->pop_template_param_scope();
                    return;
                }
            }

            auto tpl_fn = target_ns->define_function(tdef.name, tdef.function_signature->is_static);
            if (tpl_fn) {
                tpl_fn->set_tpl_info(std::move(ti));
                populate_template_signature_function(*tpl_fn, *tdef.function_signature, _unit, ctx);
            }
        }

        ctx->pop_template_param_scope();
        return;
    }

    if (tdef.source.empty()) return;

    // ── 1. Navigate to (or create) the target namespace in the model ────────
    //    Use the parent KDI namespace's fq_name to place the template.
    std::shared_ptr<ns> target_ns = _unit.get_root_namespace();
    {
        const std::string& ns_fq = parent_kdi_ns.fq_name;
        if (!ns_fq.empty()) {
            std::size_t pos = 0;
            while (true) {
                auto sep = ns_fq.find("::", pos);
                std::string part;
                if (sep == std::string::npos) {
                    part = ns_fq.substr(pos);
                } else {
                    part = ns_fq.substr(pos, sep - pos);
                }
                if (!part.empty()) {
                    target_ns = target_ns->get_child_namespace(part);
                }
                if (sep == std::string::npos) break;
                pos = sep + 2;
            }
        }
    }

    // ── 2. Check if a template with this name already exists ────────────────
    if (auto existing = target_ns->get_aggregate(tdef.name)) {
        if (existing->is_template()) return;
    }
    if (auto existing_fn = target_ns->get_function(tdef.name)) {
        if (existing_fn->is_template()) return;
    }
    if (auto existing_un = target_ns->get_union(tdef.name)) {
        if (existing_un->is_template()) return;
    }

    // ── 3. Parse the template source text ───────────────────────────────────
    try {
        // The source is a complete template declaration.  Wrap in a module
        // declaration matching the parent namespace so model_builder places
        // the template in the correct namespace.
        std::string wrapped_src;
        const std::string& ns_fq = parent_kdi_ns.fq_name;
        if (!ns_fq.empty()) {
            wrapped_src = "module " + ns_fq + ";\n" + tdef.source;
        } else {
            wrapped_src = tdef.source;
        }

        // Store the source in the unit so lexer tokens (string_views into the
        // source buffer) remain valid throughout compilation.
        auto ksrc_ptr = std::make_unique<k::source>(std::string_view("lib.k"), std::string_view(wrapped_src));
        auto& ksrc = *ksrc_ptr;
        _unit._imported_template_sources.push_back(std::move(ksrc_ptr));

        k::parse::parser parser(_logger, ksrc);
        auto ast_unit = parser.parse_unit();

        if (!ast_unit || ast_unit->declarations.empty()) return;

        // ── 4. Build the parsed declaration into the model ──────────────────
        //    Save and restore the unit name because model_builder::visit will
        //    set it from the module declaration in the wrapped source.
        auto saved_name = _unit.get_unit_name();
        k::model::model_builder::visit(_logger, ctx, *ast_unit, _unit);
        _unit.set_unit_name(saved_name);

    } catch (const std::exception&) {
        // If parsing/building fails, silently skip this template definition.
        // The template simply won't be available for cross-module instantiation.
    }
}

void kdi_importer::check_unused_imports() const {
    for (const auto& imp : _unit.get_imports()) {
        if (!imp.used && !imp.implicit) {
            auto diag = k::log::diagnostic::make_warning(
                static_cast<unsigned int>(k::diag::compiler_diag::WARN_UNUSED_IMPORT),
                "Imported module '{}' is declared but none of its symbols "
                "are used in this compilation unit",
                {canonical(imp.module_name)});
            _logger.report(diag);
        }
    }
}

} // namespace k::model




