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

/**
 * @file imported.cpp
 *
 * Implementations for all imported_* model nodes and the unit methods that
 * create / search them:
 *   - imported_function, imported_constructor, imported_destructor,
 *     imported_method, imported_aggregate (+ imported_structure/klass/interface),
 *     imported_variable
 *   - unit::find_imported_function / find_imported_functions / find_imported_variable / find_imported_type
 *   - unit::get_or_create_imported_function / get_or_create_imported_variable
 *     / get_or_create_imported_aggregate
 *   - Internal helpers: search_in_kdi, navigate_ns, fq_to_abs_kname,
 *     attach_params
 */

#include "imported.hpp"
#include "model.hpp"
#include "context.hpp"
#include "model_visitor.hpp"
#include "template.hpp"
#include "tools/kdi_type_converter.hpp"

#include <kdi.hpp>

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <unordered_set>

namespace k::model {

// ─────────────────────────────────────────────────────────────────────────────
// Constructors / update_mangled_name / make_shared
// ─────────────────────────────────────────────────────────────────────────────

// imported_function
imported_function::imported_function(std::shared_ptr<element> parent,
                                     const kdi::kdi_function* kdi_fn)
    : function(std::move(parent), /*is_static=*/true)
    , _kdi_function(kdi_fn)
{
    if (kdi_fn) {
        _mangled_name = kdi_fn->mangled_name;
        _is_operator  = kdi_fn->is_operator;
    }
}
void imported_function::update_mangled_name() {
    if (_kdi_function) _mangled_name = _kdi_function->mangled_name;
}
std::shared_ptr<imported_function>
imported_function::make_shared(std::shared_ptr<element> parent, const kdi::kdi_function* kdi_fn) {
    return std::shared_ptr<imported_function>(new imported_function(std::move(parent), kdi_fn));
}

// imported_constructor
imported_constructor::imported_constructor(std::shared_ptr<aggregate> parent,
                                           const kdi::kdi_constructor* kdi_ctor)
    : constructor(std::move(parent))
    , _kdi_ctor(kdi_ctor)
{
    if (kdi_ctor) {
        _mangled_name = kdi_ctor->mangled_name;
        if (kdi_ctor->is_copy_constructor) _is_copy_constructor = true;
        if (kdi_ctor->is_defaulted)        _aliasing = function_aliasing::DEFAULT;
        if (kdi_ctor->is_deleted)          _aliasing = function_aliasing::DELETE;
    }
}
void imported_constructor::update_mangled_name() {
    if (_kdi_ctor) _mangled_name = _kdi_ctor->mangled_name;
}
std::shared_ptr<imported_constructor>
imported_constructor::make_shared(std::shared_ptr<aggregate> parent, const kdi::kdi_constructor* kdi_ctor) {
    return std::shared_ptr<imported_constructor>(new imported_constructor(std::move(parent), kdi_ctor));
}
const std::string& imported_constructor::get_c2_mangled_name() const {
    static const std::string empty;
    return _kdi_ctor ? _kdi_ctor->mangled_name_c2 : empty;
}

// imported_destructor
imported_destructor::imported_destructor(std::shared_ptr<aggregate> parent,
                                         const kdi::kdi_destructor* kdi_dtor)
    : destructor(std::move(parent))
    , _kdi_dtor(kdi_dtor)
{
    if (kdi_dtor) {
        _mangled_name = kdi_dtor->mangled_name;
        if (kdi_dtor->is_virtual) _is_virtual = true;
        _vtable_slot = kdi_dtor->vtable_slot;
    }
}
void imported_destructor::update_mangled_name() {
    if (_kdi_dtor) _mangled_name = _kdi_dtor->mangled_name;
}
std::shared_ptr<imported_destructor>
imported_destructor::make_shared(std::shared_ptr<aggregate> parent, const kdi::kdi_destructor* kdi_dtor) {
    return std::shared_ptr<imported_destructor>(new imported_destructor(std::move(parent), kdi_dtor));
}
const std::string& imported_destructor::get_d2_mangled_name() const {
    static const std::string empty;
    return _kdi_dtor ? _kdi_dtor->mangled_name_d2 : empty;
}

// imported_method
imported_method::imported_method(std::shared_ptr<element> parent,
                                 const kdi::kdi_method* kdi_m)
    : function(std::move(parent), kdi_m ? kdi_m->is_static : false)
    , _kdi_method(kdi_m)
{
    if (kdi_m) {
        _mangled_name     = kdi_m->mangled_name;
        _is_virtual       = kdi_m->is_virtual;
        _is_const_member  = kdi_m->is_const_member;
        _is_abstract_func = kdi_m->is_abstract;
        _is_final_func    = kdi_m->is_final;
        _is_operator      = kdi_m->is_operator;
        _vtable_slot      = kdi_m->vtable_slot;
    }
}
void imported_method::update_mangled_name() {
    if (_kdi_method) _mangled_name = _kdi_method->mangled_name;
}
std::shared_ptr<imported_method>
imported_method::make_shared(std::shared_ptr<element> parent, const kdi::kdi_method* kdi_m) {
    return std::shared_ptr<imported_method>(new imported_method(std::move(parent), kdi_m));
}

// imported_aggregate
imported_aggregate::imported_aggregate(std::shared_ptr<element> parent,
                                       const kdi::kdi_aggregate* kdi_agg)
    : aggregate(std::move(parent))
    , _kdi_aggregate(kdi_agg)
{
    if (kdi_agg) _mangled_name = kdi_agg->mangled_name;
}
void imported_aggregate::update_mangled_name() {
    if (_kdi_aggregate) _mangled_name = _kdi_aggregate->mangled_name;
}

// imported_structure
imported_structure::imported_structure(std::shared_ptr<element> parent,
                                       const kdi::kdi_aggregate* kdi_agg)
    : imported_aggregate(std::move(parent), kdi_agg) {}
std::shared_ptr<imported_structure>
imported_structure::make_shared(std::shared_ptr<element> parent, const kdi::kdi_aggregate* kdi_agg) {
    return std::shared_ptr<imported_structure>(new imported_structure(std::move(parent), kdi_agg));
}

// imported_klass
imported_klass::imported_klass(std::shared_ptr<element> parent,
                               const kdi::kdi_aggregate* kdi_agg)
    : imported_aggregate(std::move(parent), kdi_agg)
{
    if (kdi_agg) {
        _has_vtable  = kdi_agg->vtable.has_value();
        _is_abstract = kdi_agg->is_abstract;
        _is_final    = kdi_agg->is_final;
    }
}
std::shared_ptr<imported_klass>
imported_klass::make_shared(std::shared_ptr<element> parent, const kdi::kdi_aggregate* kdi_agg) {
    return std::shared_ptr<imported_klass>(new imported_klass(std::move(parent), kdi_agg));
}

// imported_interface
imported_interface::imported_interface(std::shared_ptr<element> parent,
                                       const kdi::kdi_aggregate* kdi_agg)
    : imported_aggregate(std::move(parent), kdi_agg)
{
    _is_abstract = true;
    if (kdi_agg) {
        _is_final = kdi_agg->is_final;
        // Interfaces always have a vtable (they are purely virtual) — no _has_vtable field here,
        // has_vtable() is overridden to always return true in imported_interface.
    }
}
std::shared_ptr<imported_interface>
imported_interface::make_shared(std::shared_ptr<element> parent, const kdi::kdi_aggregate* kdi_agg) {
    return std::shared_ptr<imported_interface>(new imported_interface(std::move(parent), kdi_agg));
}

// imported_annotation_type
imported_annotation_type::imported_annotation_type(std::shared_ptr<element> parent,
                                                    const kdi::kdi_aggregate* kdi_agg)
    : imported_klass(std::move(parent), kdi_agg)
{}
std::shared_ptr<imported_annotation_type>
imported_annotation_type::make_shared(std::shared_ptr<element> parent, const kdi::kdi_aggregate* kdi_agg) {
    return std::shared_ptr<imported_annotation_type>(new imported_annotation_type(std::move(parent), kdi_agg));
}

// imported_variable
imported_variable::imported_variable(std::shared_ptr<variable_holder> parent,
                                     const kdi::kdi_variable* kdi_var)
    : global_variable_definition(std::move(parent))
    , _kdi_variable(kdi_var)
{
    if (kdi_var) _mangled_name = kdi_var->mangled_name;
}
void imported_variable::update_mangled_name() {
    if (_kdi_variable) _mangled_name = _kdi_variable->mangled_name;
}
std::shared_ptr<imported_variable>
imported_variable::make_shared(std::shared_ptr<variable_holder> parent, const kdi::kdi_variable* kdi_var) {
    return std::shared_ptr<imported_variable>(new imported_variable(std::move(parent), kdi_var));
}

// ─────────────────────────────────────────────────────────────────────────────
// accept() implementations for imported entities
// ─────────────────────────────────────────────────────────────────────────────

void imported_function::accept(model_visitor& v)    { v.visit_function(*this); }
void imported_constructor::accept(model_visitor& v) { v.visit_constructor(*this); }
void imported_destructor::accept(model_visitor& v)  { v.visit_destructor(*this); }
void imported_method::accept(model_visitor& v)      { v.visit_function(*this); }
void imported_variable::accept(model_visitor& v)    { v.visit_global_variable_definition(*this); }
// imported_aggregate variants: use visit_aggregate (they don't inherit from structure/klass/interface)
void imported_aggregate::accept(model_visitor& v)   { v.visit_aggregate(*this); }
void imported_structure::accept(model_visitor& v)   { v.visit_aggregate(*this); }
void imported_klass::accept(model_visitor& v)       { v.visit_aggregate(*this); }
void imported_interface::accept(model_visitor& v)   { v.visit_aggregate(*this); }
void imported_annotation_type::accept(model_visitor& v) { v.visit_aggregate(*this); }

// ─────────────────────────────────────────────────────────────────────────────
// Cross-module symbol lookup helpers (anonymous namespace)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/**
 * Navigate a kdi_namespace hierarchy following the components of @p parts
 * starting at index @p depth.
 *
 * The kdi_file root_ns has name="" and its direct children are the first-level
 * namespace components of the module (e.g. for module "math::vec" the root_ns
 * contains a child ns named "math" which itself contains "vec").
 *
 * Returns the kdi_namespace* at the end of the path, or nullptr if any
 * intermediate component is not found.
 */
const kdi::kdi_namespace*
navigate_ns(const kdi::kdi_namespace& current,
            const std::vector<std::string>& parts,
            std::size_t depth)
{
    if (depth >= parts.size()) return &current;
    const std::string& next = parts[depth];
    for (const auto& child : current.namespaces) {
        if (child.name == next) {
            return navigate_ns(child, parts, depth + 1);
        }
    }
    return nullptr;
}

/**
 * Search @p name inside @p kdi_file.
 *
 * Strategy:
 *  - Walk the component list. At each level, either enter a sub-namespace or
 *    look for a terminal symbol (function / variable / aggregate) in the last
 *    level's namespace.
 *  - The kdi root_ns name is ""; its children represent the first-level K
 *    namespaces. So for name {"math", "vec", "dot"} we descend into root_ns →
 *    "math" → "vec" and look for function "dot".
 *  - If the first component matches the module_name's first part, we try to
 *    skip it (some callers may pass the full qualified name including the
 *    module root).
 */
struct kdi_search_result {
    const kdi::kdi_function*  func  = nullptr;
    const kdi::kdi_variable*  var   = nullptr;
    const kdi::kdi_aggregate* agg   = nullptr;
    const kdi::kdi_enum*      en    = nullptr;
};

kdi_search_result
search_in_kdi(const kdi::kdi_file& kdi, const k::name& name)
{
    if (name.empty()) return {};
    const auto& parts = name.parts(); // e.g. {"math", "utils", "square"}

    // The kdi exporter sets root_ns.name to the *last* component of the module name.
    // e.g. module "math::utils" → root_ns.name == "utils"
    //      module "mylib"       → root_ns.name == "mylib"
    //
    // The root_ns directly contains the functions/variables/aggregates and
    // sub-namespaces of that last component.
    //
    // Search strategy:
    //
    //  1. Strip a module-name prefix from the caller's name, then look in root_ns.
    //     e.g. name {"math","utils","square"}, module "math::utils"
    //          → strip {"math","utils"} → look for {"square"} in root_ns
    //
    //  2. If name starts with just the last module component, strip one part.
    //     e.g. name {"utils","square"}, module "math::utils"
    //          → strip {"utils"} → look for {"square"} in root_ns
    //
    //  3. Plain lookup (name has no module prefix at all).
    //     e.g. name {"square"} → look for {"square"} in root_ns

    // Split module name into components
    std::vector<std::string> mod_parts;
    {
        const std::string& mod = kdi.header.module_name;
        std::size_t start = 0;
        while (true) {
            auto pos = mod.find("::", start);
            if (pos == std::string::npos) {
                mod_parts.push_back(mod.substr(start));
                break;
            }
            mod_parts.push_back(mod.substr(start, pos - start));
            start = pos + 2;
        }
    }

    // Helper: try to find symbol in root_ns using parts[skip..end-1] as
    // intermediate namespaces and parts.back() as symbol name.
    auto try_find_from_root = [&](std::size_t skip) -> kdi_search_result {
        if (skip >= parts.size()) return {};

        const kdi::kdi_namespace* ns_ptr = &kdi.unit.root_ns;

        // Navigate intermediate namespaces (parts[skip] .. parts[end-2])
        for (std::size_t i = skip; i + 1 < parts.size(); ++i) {
            const kdi::kdi_namespace* child = nullptr;
            for (const auto& c : ns_ptr->namespaces) {
                if (c.name == parts[i]) { child = &c; break; }
            }
            if (!child) return {};
            ns_ptr = child;
        }

        const std::string& sym = parts.back();
        for (const auto& f : ns_ptr->functions)
            if (f.name == sym) return { &f, nullptr, nullptr, nullptr };
        for (const auto& v : ns_ptr->variables)
            if (v.name == sym) return { nullptr, &v, nullptr, nullptr };
        for (const auto& a : ns_ptr->aggregates)
            if (a.name == sym) return { nullptr, nullptr, &a, nullptr };
        for (const auto& e : ns_ptr->enums)
            if (e.name == sym) return { nullptr, nullptr, nullptr, &e };
        return {};
    };

    // Strategy 1: strip full module prefix if parts starts with it
    if (parts.size() > mod_parts.size()) {
        bool prefix_match = true;
        for (std::size_t i = 0; i < mod_parts.size(); ++i) {
            if (parts[i] != mod_parts[i]) { prefix_match = false; break; }
        }
        if (prefix_match) {
            // After stripping the module prefix, the remaining parts[mod_parts.size()..]
            // are relative to root_ns. But root_ns.name is already mod_parts.back(),
            // so we skip that and look from root_ns directly.
            auto res = try_find_from_root(mod_parts.size());
            if (res.func || res.var || res.agg || res.en) return res;
        }
    }

    // Strategy 2: strip just the last module component if it matches parts[0]
    if (!mod_parts.empty() && !parts.empty() && parts[0] == mod_parts.back()) {
        auto res = try_find_from_root(1);
        if (res.func || res.var || res.agg || res.en) return res;
    }

    // Strategy 3: plain lookup (no module prefix)
    return try_find_from_root(0);
}

std::vector<const kdi::kdi_function*>
search_functions_in_kdi(const kdi::kdi_file& kdi, const k::name& name)
{
    std::vector<const kdi::kdi_function*> matches;
    if (name.empty()) return matches;

    const auto& parts = name.parts();
    std::vector<std::string> mod_parts;
    {
        const std::string& mod = kdi.header.module_name;
        std::size_t start = 0;
        while (true) {
            auto pos = mod.find("::", start);
            if (pos == std::string::npos) {
                mod_parts.push_back(mod.substr(start));
                break;
            }
            mod_parts.push_back(mod.substr(start, pos - start));
            start = pos + 2;
        }
    }

    std::unordered_set<std::string> seen_mangled;
    auto append_unique = [&](const kdi::kdi_function* fn) {
        if (!fn) return;
        if (seen_mangled.insert(fn->mangled_name).second) {
            matches.push_back(fn);
        }
    };

    auto try_collect_from_root = [&](std::size_t skip) {
        if (skip >= parts.size()) return;

        const kdi::kdi_namespace* ns_ptr = &kdi.unit.root_ns;
        for (std::size_t i = skip; i + 1 < parts.size(); ++i) {
            const kdi::kdi_namespace* child = nullptr;
            for (const auto& c : ns_ptr->namespaces) {
                if (c.name == parts[i]) { child = &c; break; }
            }
            if (!child) return;
            ns_ptr = child;
        }

        const std::string& sym = parts.back();
        for (const auto& f : ns_ptr->functions) {
            if (f.name == sym) append_unique(&f);
        }
    };

    if (parts.size() > mod_parts.size()) {
        bool prefix_match = true;
        for (std::size_t i = 0; i < mod_parts.size(); ++i) {
            if (parts[i] != mod_parts[i]) { prefix_match = false; break; }
        }
        if (prefix_match) {
            try_collect_from_root(mod_parts.size());
        }
    }

    if (!mod_parts.empty() && !parts.empty() && parts[0] == mod_parts.back()) {
        try_collect_from_root(1);
    }

    try_collect_from_root(0);
    return matches;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// unit::find_imported_* — Public API
// ─────────────────────────────────────────────────────────────────────────────

const kdi::kdi_function* unit::find_imported_function(const k::name& name) {
    for (auto& imp : _imported_modules) {
        if (!imp.kdi) continue;
        auto res = search_in_kdi(*imp.kdi, name);
        if (res.func) {
            imp.used = true;
            return res.func;
        }
    }
    // Also search transitive dependencies
    for (const auto& tdep : _transitive_kdis) {
        if (!tdep) continue;
        auto res = search_in_kdi(*tdep, name);
        if (res.func) return res.func;
    }
    return nullptr;
}

std::vector<const kdi::kdi_function*>
unit::find_imported_functions(const k::name& name) {
    std::vector<const kdi::kdi_function*> result;
    std::unordered_set<std::string> seen_mangled;

    auto append_unique = [&](const kdi::kdi_function* fn) {
        if (!fn) return;
        if (seen_mangled.insert(fn->mangled_name).second) {
            result.push_back(fn);
        }
    };

    for (auto& imp : _imported_modules) {
        if (!imp.kdi) continue;
        auto matches = search_functions_in_kdi(*imp.kdi, name);
        if (!matches.empty()) imp.used = true;
        for (const auto* fn : matches) append_unique(fn);
    }

    for (const auto& tdep : _transitive_kdis) {
        if (!tdep) continue;
        auto matches = search_functions_in_kdi(*tdep, name);
        for (const auto* fn : matches) append_unique(fn);
    }

    return result;
}

const kdi::kdi_variable* unit::find_imported_variable(const k::name& name) {
    for (auto& imp : _imported_modules) {
        if (!imp.kdi) continue;
        auto res = search_in_kdi(*imp.kdi, name);
        if (res.var) {
            imp.used = true;
            return res.var;
        }
    }
    // Also search transitive dependencies
    for (const auto& tdep : _transitive_kdis) {
        if (!tdep) continue;
        auto res = search_in_kdi(*tdep, name);
        if (res.var) return res.var;
    }
    return nullptr;
}

const kdi::kdi_aggregate* unit::find_imported_type(const k::name& name) {
    for (auto& imp : _imported_modules) {
        if (!imp.kdi) continue;
        auto res = search_in_kdi(*imp.kdi, name);
        if (res.agg) {
            imp.used = true;
            return res.agg;
        }
    }
    // Also search transitive dependencies
    for (const auto& tdep : _transitive_kdis) {
        if (!tdep) continue;
        auto res = search_in_kdi(*tdep, name);
        if (res.agg) return res.agg;
    }
    return nullptr;
}

const kdi::kdi_enum* unit::find_imported_enum(const k::name& name) {
    for (auto& imp : _imported_modules) {
        if (!imp.kdi) continue;
        auto res = search_in_kdi(*imp.kdi, name);
        if (res.en) {
            imp.used = true;
            return res.en;
        }
    }
    for (const auto& tdep : _transitive_kdis) {
        if (!tdep) continue;
        auto res = search_in_kdi(*tdep, name);
        if (res.en) return res.en;
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers for building imported model nodes
// ─────────────────────────────────────────────────────────────────────────────

/// Parse a "::" separated fq_name string into a k::name with root prefix.
/// This preserves the mangled name when assign_name() is called, because
/// named_element::update_names() only calls update_mangled_name() when
/// the name has a root prefix.
///
/// Handles both "ns::Type" and "::ns::Type" forms — the leading "::" is
/// stripped from the latter before splitting (the root-prefix flag covers it).
static k::name fq_to_abs_kname(const std::string& fq) {
    // Strip leading "::" if present
    const std::string& normalized = (fq.size() >= 2 && fq[0] == ':' && fq[1] == ':')
                                    ? fq.substr(2) : fq;
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        auto pos = normalized.find("::", start);
        if (pos == std::string::npos) {
            parts.push_back(normalized.substr(start));
            break;
        }
        parts.push_back(normalized.substr(start, pos - start));
        start = pos + 2;
    }
    return k::name(true, std::move(parts));  // true = has root prefix → mangled name preserved
}

static void attach_params(function& fn,
                          const std::vector<kdi::kdi_param>& params,
                          unit& owner,
                          std::shared_ptr<context> ctx)
{
    for (const auto& kp : params) {
        auto model_type = kdi_type_to_model_type(kp.type, owner, ctx);
        // Use an anonymous name if the KDI entry has no name
        const std::string& pname = kp.name.empty() ? "__p" : kp.name;
        auto p = fn.append_parameter(pname, model_type);
        if (p) p->set_varargs(kp.is_varargs);
    }
}

/**
 * Parse a kdi_template_arg value string back to a k::value_type, guided by
 * the optional kdi_type discriminator.
 *
 * The exporter stores:
 *   - bools as "true"/"false"
 *   - strings as "\"...\""
 *   - integers/floats via std::to_string()
 */
static k::value_type parse_value_arg_string(const std::string& val_str,
                                             const std::optional<kdi::kdi_type>& vtype)
{
    // Check for bool
    if (val_str == "true")  return k::value_type{true};
    if (val_str == "false") return k::value_type{false};

    // Check for quoted string
    if (val_str.size() >= 2 && val_str.front() == '"' && val_str.back() == '"')
        return k::value_type{val_str.substr(1, val_str.size() - 2)};

    // Use the value_type kdi_type hint if available
    if (vtype) {
        if (auto* ft = std::get_if<kdi::kdi_float_type>(&vtype->value)) {
            try {
                if (ft->bits <= 32) return k::value_type{std::stof(val_str)};
                return k::value_type{std::stod(val_str)};
            } catch (...) {}
        }
        if (auto* bt = std::get_if<kdi::kdi_bool_type>(&vtype->value)) {
            return k::value_type{val_str == "1" || val_str == "true"};
        }
        if (auto* ct = std::get_if<kdi::kdi_char_type>(&vtype->value)) {
            if (!val_str.empty()) return k::value_type{static_cast<char>(std::stoi(val_str))};
        }
        if (auto* it = std::get_if<kdi::kdi_int_type>(&vtype->value)) {
            try {
                long long v = std::stoll(val_str);
                if (it->bits <= 32 && it->is_signed)   return k::value_type{static_cast<int>(v)};
                if (it->bits <= 32 && !it->is_signed)  return k::value_type{static_cast<unsigned int>(v)};
                if (it->bits <= 64 && it->is_signed)   return k::value_type{static_cast<long>(v)};
                if (it->bits <= 64 && !it->is_signed)  return k::value_type{static_cast<unsigned long>(static_cast<unsigned long long>(v))};
                if (it->is_signed)                      return k::value_type{static_cast<long long>(v)};
                return k::value_type{static_cast<unsigned long long>(static_cast<unsigned long long>(v))};
            } catch (...) {}
        }
    }

    // Fallback: try integer, then float
    try { return k::value_type{static_cast<int>(std::stoll(val_str))}; } catch (...) {}
    try { return k::value_type{std::stod(val_str)}; } catch (...) {}
    return k::value_type{0};
}

/**
 * Convert a kdi_template_origin's argument list into model template_argument vector.
 * Used by the importer to populate set_tpl_instantiation_info() on imported entities.
 */
static std::vector<template_argument>
convert_template_origin_args(const kdi::kdi_template_origin& origin,
                             unit& owner,
                             std::shared_ptr<context> ctx)
{
    std::vector<template_argument> model_args;
    model_args.reserve(origin.args.size());
    for (const auto& karg : origin.args) {
        if (karg.type_arg) {
            auto model_type = kdi_type_to_model_type(*karg.type_arg, owner, ctx);
            model_args.push_back(template_argument::make_type(model_type));
        } else if (karg.value_arg) {
            auto val = parse_value_arg_string(*karg.value_arg, karg.value_type);
            model_args.push_back(template_argument::make_value(std::move(val)));
        }
    }
    return model_args;
}

static std::shared_ptr<k::parse::ast::brace_init_list>
rebuild_object_init_brace(const kdi::kdi_enum_entry& entry)
{
    if (entry.object_init_members.empty()) return nullptr;

    std::vector<std::shared_ptr<k::parse::ast::expression>> elements;
    elements.reserve(entry.object_init_members.size());
    for (const auto& [member_name, member_value] : entry.object_init_members) {
        std::string lit_txt = std::to_string(member_value);
        auto lit = std::make_shared<k::parse::ast::literal_expr>(
            lex::any_literal{lex::integer{
                lit_txt,
                /*num_prefix_size=*/0,
                /*num_content_size=*/lit_txt.size(),
                lex::DECIMAL,
                /*unsigned_num=*/false,
                lex::LONGLONG}});
        auto designated = std::make_shared<k::parse::ast::designated_init_element>(
            lex::operator_(".", lex::operator_::DOT),
            lex::identifier(member_name),
            std::vector<lex::identifier>{},
            std::static_pointer_cast<k::parse::ast::expression>(lit));
        elements.push_back(std::static_pointer_cast<k::parse::ast::expression>(designated));
    }

    return std::make_shared<k::parse::ast::brace_init_list>(
        lex::punctuator("{", lex::punctuator::BRACE_OPEN),
        lex::punctuator("}", lex::punctuator::BRACE_CLOSE),
        elements,
        /*is_designated=*/true);
}

static std::shared_ptr<doc::doc_entity>
make_doc_entity(const std::optional<kdi::kdi_doc_block>& kdoc)
{
    if (!kdoc) return nullptr;
    auto out = std::make_shared<doc::doc_entity>();
    out->brief = kdoc->brief;
    out->description = kdoc->description;
    return out;
}

static std::shared_ptr<doc::function_doc>
make_function_doc(const std::optional<kdi::kdi_doc_function>& kdoc)
{
    if (!kdoc) return nullptr;
    auto out = std::make_shared<doc::function_doc>();
    out->brief = kdoc->brief;
    out->description = kdoc->description;
    for (const auto& p : kdoc->params) {
        out->params.push_back(doc::param_doc{
            .name = p.name,
            .description = p.description,
        });
    }
    if (kdoc->returns.has_value()) {
        out->returns = doc::return_doc{ .description = *kdoc->returns };
    }
    for (const auto& t : kdoc->throws) {
        out->throws.push_back(doc::throws_doc{
            .type_name = t.type_name,
            .description = t.description,
        });
    }
    for (const auto& tp : kdoc->template_params) {
        out->template_params.push_back(doc::template_param_doc{
            .name = tp.name,
            .description = tp.description,
        });
    }
    for (const auto& tag : kdoc->tags) {
        out->tags.push_back(doc::tagged_doc{
            .tag = tag.tag,
            .value = tag.value,
        });
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// unit::get_or_create_imported_function
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<imported_function>
unit::get_or_create_imported_function(const kdi::kdi_function* kdi_fn,
                                      std::shared_ptr<context> ctx)
{
    if (!kdi_fn) return nullptr;

    // Check cache
    auto it = _imported_functions.find(kdi_fn->mangled_name);
    if (it != _imported_functions.end()) return it->second;

    // Create node — parent is the root_ns (logical ownership)
    auto root = get_root_namespace();
    auto fn = imported_function::make_shared(root, kdi_fn);
    // Use the absolute FQ name (root-prefix) so that update_names() calls
    // update_mangled_name(), which preserves the KDI mangled name in _mangled_name.
    // Without the root prefix, update_names() clears _mangled_name to "".
    if (!kdi_fn->fq_name.empty()) {
        fn->assign_name(fq_to_abs_kname(kdi_fn->fq_name));
    } else {
        fn->assign_name(k::name(false, {kdi_fn->name}));
    }

    // Return type (nullptr = void)
    auto ret = kdi_type_to_model_type(kdi_fn->return_type, *this, ctx);
    if (ret) fn->set_return_type(ret);

    // Parameters
    attach_params(*fn, kdi_fn->params, *this, ctx);

    // Visibility
    fn->set_visibility(kdi_fn->visibility == kdi::kdi_visibility::public_ ? PUBLIC : PROTECTED);

    // Template origin — mark as a concrete template instantiation if applicable
    if (kdi_fn->template_origin) {
        auto model_args = convert_template_origin_args(*kdi_fn->template_origin, *this, ctx);
        fn->set_tpl_instantiation_info(kdi_fn->template_origin->base_name, std::move(model_args));
    }

    // Import throws spec
    for (const auto& ts : kdi_fn->throws_spec) {
        auto exc_type = kdi_type_to_model_type(ts, *this, ctx);
        if (exc_type) fn->add_throws_type(exc_type);
    }
    if (auto doc = make_function_doc(kdi_fn->doc)) {
        fn->set_documentation(std::move(doc));
    }

    _imported_functions[kdi_fn->mangled_name] = fn;
    return fn;
}

// ─────────────────────────────────────────────────────────────────────────────
// unit::get_or_create_imported_variable
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<imported_variable>
unit::get_or_create_imported_variable(const kdi::kdi_variable* kdi_var,
                                      std::shared_ptr<context> ctx)
{
    if (!kdi_var) return nullptr;

    auto it = _imported_variables.find(kdi_var->mangled_name);
    if (it != _imported_variables.end()) return it->second;

    auto root = get_root_namespace();
    auto var = imported_variable::make_shared(root, kdi_var);
    var->assign_name(fq_to_abs_kname(kdi_var->fq_name.empty() ? kdi_var->name : kdi_var->fq_name));

    auto vtype = kdi_type_to_model_type(kdi_var->type, *this, ctx);
    if (vtype) var->set_type(vtype);

    var->set_visibility(kdi_var->visibility == kdi::kdi_visibility::public_ ? PUBLIC : PROTECTED);
    if (auto doc = make_doc_entity(kdi_var->doc)) {
        var->set_documentation(std::move(doc));
    }

    _imported_variables[kdi_var->mangled_name] = var;
    return var;
}

// ─────────────────────────────────────────────────────────────────────────────
// unit::get_or_create_imported_aggregate
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<imported_aggregate>
unit::get_or_create_imported_aggregate(const k::name& fq_name,
                                       std::shared_ptr<context> ctx)
{
    // Normalise the cache key: always use the version WITHOUT root prefix ("a::b", not "::a::b")
    // so that resolve_type_from_root and resolve_type_by_name hit the same entry regardless of
    // whether they pass a root-prefixed name or not.
    const k::name normalised_name = fq_name.has_root_prefix()
                                    ? fq_name.without_root_prefix() : fq_name;
    const std::string fq_str = normalised_name.to_string();

    // Check cache
    auto it = _imported_aggregates.find(fq_str);
    if (it != _imported_aggregates.end()) return it->second;

    // Search in KDI files
    const kdi::kdi_aggregate* kdi_agg = find_imported_type(fq_name);
    if (!kdi_agg) return nullptr;

    // Compute canonical key from the KDI aggregate's authoritative FQ name.
    // This prevents duplicate imported aggregates when the same type is requested
    // via different name forms (e.g. "Annotation" vs "k::Annotation").
    const std::string canonical_key = [&]() -> std::string {
        const auto& f = kdi_agg->fq_name;
        if (f.size() >= 2 && f[0] == ':' && f[1] == ':') return f.substr(2);
        return f.empty() ? kdi_agg->name : f;
    }();
    if (canonical_key != fq_str) {
        auto it2 = _imported_aggregates.find(canonical_key);
        if (it2 != _imported_aggregates.end()) {
            // Re-cache under the caller's key for future lookups
            _imported_aggregates[fq_str] = it2->second;
            return it2->second;
        }
    }

    // Create the right concrete class based on kind
    auto root = get_root_namespace();
    std::shared_ptr<imported_aggregate> agg;
    switch (kdi_agg->kind) {
        case kdi::kdi_aggregate_kind::struct_:
            agg = imported_structure::make_shared(root, kdi_agg);
            break;
        case kdi::kdi_aggregate_kind::class_:
            agg = imported_klass::make_shared(root, kdi_agg);
            break;
        case kdi::kdi_aggregate_kind::interface_:
            agg = imported_interface::make_shared(root, kdi_agg);
            break;
        case kdi::kdi_aggregate_kind::annotation_:
            // Annotation types are imported with is_annotation() = true
            agg = imported_annotation_type::make_shared(root, kdi_agg);
            break;
    }
    if (!agg) return nullptr;

    // Assign the absolute FQ name
    if (fq_name.has_root_prefix()) {
        agg->assign_name(fq_name);
    } else {
        agg->assign_name(fq_to_abs_kname(kdi_agg->fq_name.empty() ? kdi_agg->name : kdi_agg->fq_name));
    }

    // ── Set nesting modifiers from KDI ──────────────────────────────────────
    agg->set_static_nested(kdi_agg->is_static_nested);
    agg->set_const_struct(kdi_agg->is_const_struct);
    if (auto doc = make_doc_entity(kdi_agg->doc)) {
        agg->set_documentation(std::move(doc));
    }

    // ── Register struct_type in context (before recursing to break cycles) ─
    const std::string struct_key = [&]() -> std::string {
        const auto& f = kdi_agg->fq_name;
        if (f.size() >= 2 && f[0] == ':' && f[1] == ':') return f.substr(2);
        return f.empty() ? kdi_agg->name : f;
    }();

    // The LLVM type name used in llvm_def is the mangled_name of the aggregate.
    const std::string& llvm_type_name = kdi_agg->mangled_name.empty()
                                        ? struct_key : kdi_agg->mangled_name;

    // For template instantiations, consult the unification registry (keyed by the
    // mangled short instantiation name) so the imported and any locally-synthesised
    // instantiation of the same template share a single struct_type
    // (see unit::_instantiation_struct_types). A non-instantiation aggregate is never
    // duplicated (the _imported_aggregates cache guards that), so only instantiations
    // need this.
    const bool is_instantiation = kdi_agg->template_origin.has_value();
    // Origin-qualified registry key: qualify the short instantiation name with the
    // originating template's namespace (derived from template_origin.base_fq_name,
    // e.g. "k::Optional" -> origin "k"). Built via the same helper the local
    // instantiator uses, so both map the same instantiation to one struct_type while
    // same-named templates from different namespaces stay distinct.
    std::string inst_key;
    if (is_instantiation) {
        const std::string& base_fq = kdi_agg->template_origin->base_fq_name;
        std::string origin_ns;
        if (auto pos = base_fq.rfind("::"); pos != std::string::npos) {
            origin_ns = base_fq.substr(0, pos);
        }
        inst_key = make_instantiation_registry_key(origin_ns, kdi_agg->name);
    }
    std::shared_ptr<struct_type> st;
    if (is_instantiation) {
        st = find_instantiation_struct_type(inst_key);
    }
    const bool reused_st = static_cast<bool>(st);
    if (!st) {
        st = std::make_shared<struct_type>(struct_key, std::weak_ptr<aggregate>(agg));
        if (is_instantiation) {
            register_instantiation_struct_type(inst_key, st);
        }
    }
    agg->set_struct_type(st);
    ctx->add_struct(st);

    // Put in cache BEFORE recursing on bases (prevents infinite loops on cyclic types)
    _imported_aggregates[fq_str] = agg;
    if (canonical_key != fq_str) {
        _imported_aggregates[canonical_key] = agg;
    }

    // ── Resolve base classes FIRST ────────────────────────────────────────
    // We MUST intern the LLVM types of base classes before interning the
    // derived type, because the derived type's llvm_def references the base
    // types by name (e.g. '%Counter = type { ptr, %ICounter, i32 }').
    // If we try to intern Counter before ICounter is known, LLVM creates an
    // opaque forward-reference for ICounter, leaving Counter unsized.
    for (const auto& kbase : kdi_agg->bases) {
        std::string base_fq = kbase.fq_name;
        if (base_fq.size() >= 2 && base_fq[0] == ':' && base_fq[1] == ':')
            base_fq = base_fq.substr(2);

        model::visibility base_vis = (kbase.visibility == kdi::kdi_visibility::protected_)
                                     ? model::PROTECTED : model::PUBLIC;

        // base_spec::raw_name must produce (via sanitised_name(), i.e. "::" -> "_")
        // the EXACT literal used for the struct's "__base_X__" field name, because
        // that is what client-side codegen (e.g. gen_expr_cast.cpp's static-upcast
        // GEP walk, template_instantiator.cpp's base-subobject injection, ...) looks
        // up by name via bs.sanitised_name() — it never re-derives a fully-qualified
        // name. kdi_exporter.cpp exports that literal AS-USED-LOCALLY (not FQ) in the
        // matching kdi_layout_base_subobject entry, so for non-virtual bases we can
        // recover it precisely via kdi_base::base_field_index, which records the LLVM
        // field index of the "__base_X__" subobject. Falling back to the (sanitised)
        // fully-qualified name preserves prior behaviour when no match is found
        // (e.g. virtual bases, where no such per-class field index is recorded).
        std::string raw_name_for_base_spec = base_fq;
        if (!kbase.is_virtual && kbase.base_field_index >= 0) {
            for (const auto& lf : kdi_agg->layout) {
                if (auto* bso = std::get_if<kdi::kdi_layout_base_subobject>(&lf)) {
                    if (bso->llvm_field_index == kbase.base_field_index) {
                        raw_name_for_base_spec = bso->base_fq_name;
                        break;
                    }
                }
            }
        }
        agg->add_base(raw_name_for_base_spec, base_vis);

        // Recursively materialise the base aggregate (and its LLVM type)
        std::vector<std::string> bparts;
        std::size_t bpos = 0;
        while (true) {
            auto sep = base_fq.find("::", bpos);
            if (sep == std::string::npos) { bparts.push_back(base_fq.substr(bpos)); break; }
            bparts.push_back(base_fq.substr(bpos, sep - bpos));
            bpos = sep + 2;
        }
        auto base_imp = get_or_create_imported_aggregate(k::name{false, std::move(bparts)}, ctx);
        if (base_imp) {
            auto& bases_mut = agg->get_bases_mutable();
            if (!bases_mut.empty()) {
                bases_mut.back().base      = std::dynamic_pointer_cast<aggregate>(base_imp);
                bases_mut.back().is_virtual = kbase.is_virtual;
            }
        }
    }

    // ── Intern LLVM StructType (after bases, so their LLVM types are known) ─
    llvm::StructType* llvm_st = nullptr;
    if (!kdi_agg->llvm_def.empty()) {
        llvm_st = ctx->intern_llvm_struct_from_def(kdi_agg->llvm_def, llvm_type_name);
    }

    // ── Build K model member variables from KDI layout ────────────────────
    // This is needed so that scope_lookup can find named members by name
    // (e.g. for 'obj.field' access).
    // We also build the struct_type::field list for get_member() lookups.
    std::vector<struct_type::field> named_fields;
    for (const auto& layout_field : kdi_agg->layout) {
        std::visit([&](const auto& lf) {
            using T = std::decay_t<decltype(lf)>;
            if constexpr (std::is_same_v<T, kdi::kdi_layout_member>) {
                auto var_def = agg->append_variable(lf.name);
                if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(var_def)) {
                    auto mtype = kdi_type_to_model_type(lf.type, *this, ctx);
                    if (mtype) mv->set_type(mtype);
                    mv->set_visibility(lf.visibility == kdi::kdi_visibility::public_
                                       ? PUBLIC : PROTECTED);
                    // Register the named field so struct_type::get_member() can find it
                    named_fields.push_back(struct_type::field{
                        .index      = static_cast<std::size_t>(lf.llvm_field_index),
                        .name       = lf.name,
                        .field_type = mtype
                    });
                }
            } else if constexpr (std::is_same_v<T, kdi::kdi_layout_base_subobject>) {
                // Register as __base_<sanitised_fq_name>__ so that visit_cast_expression
                // can GEP into the right sub-object for static upcast (Derived → Base).
                // The field name uses the sanitised form ("::" → "_") to match the
                // naming convention used by gen_struct when building the LLVM struct.
                const std::string& raw = lf.base_fq_name;
                std::string norm = (raw.size() >= 2 && raw[0] == ':' && raw[1] == ':')
                                   ? raw.substr(2) : raw;
                // Sanitise: replace "::" with "_"
                std::string sanitised;
                sanitised.reserve(norm.size());
                std::size_t sp = 0;
                while (sp < norm.size()) {
                    if (sp + 1 < norm.size() && norm[sp] == ':' && norm[sp+1] == ':') {
                        sanitised += '_'; sp += 2;
                    } else {
                        sanitised += norm[sp++];
                    }
                }
                std::string field_name = "__base_" + sanitised + "__";
                named_fields.push_back(struct_type::field{
                    .index      = static_cast<std::size_t>(lf.llvm_field_index),
                    .name       = field_name,
                    .field_type = {}  // type only needed for member variable access
                });
            } else if constexpr (std::is_same_v<T, kdi::kdi_layout_vbase_subobject>) {
                // Register as __vbase_<sanitised_fq_name>__ for virtual base sub-object GEPs.
                const std::string& raw = lf.vbase_fq_name;
                std::string norm = (raw.size() >= 2 && raw[0] == ':' && raw[1] == ':')
                                   ? raw.substr(2) : raw;
                std::string sanitised;
                sanitised.reserve(norm.size());
                std::size_t sp = 0;
                while (sp < norm.size()) {
                    if (sp + 1 < norm.size() && norm[sp] == ':' && norm[sp+1] == ':') {
                        sanitised += '_'; sp += 2;
                    } else {
                        sanitised += norm[sp++];
                    }
                }
                std::string field_name = "__vbase_" + sanitised + "__";
                named_fields.push_back(struct_type::field{
                    .index      = static_cast<std::size_t>(lf.llvm_field_index),
                    .name       = field_name,
                    .field_type = {}
                });
            } else if constexpr (std::is_same_v<T, kdi::kdi_layout_vbptr>) {
                // Register as __vbptr_<sanitised_fq_name>__ for virtual base pointer slots.
                const std::string& raw = lf.vbase_fq_name;
                std::string norm = (raw.size() >= 2 && raw[0] == ':' && raw[1] == ':')
                                   ? raw.substr(2) : raw;
                std::string sanitised;
                sanitised.reserve(norm.size());
                std::size_t sp = 0;
                while (sp < norm.size()) {
                    if (sp + 1 < norm.size() && norm[sp] == ':' && norm[sp+1] == ':') {
                        sanitised += '_'; sp += 2;
                    } else {
                        sanitised += norm[sp++];
                    }
                }
                std::string field_name = "__vbptr_" + sanitised + "__";
                named_fields.push_back(struct_type::field{
                    .index      = static_cast<std::size_t>(lf.llvm_field_index),
                    .name       = field_name,
                    .field_type = {}
                });
            }
            // kdi_layout_vptr and kdi_layout_vptr_secondary are purely codegen-level
            // constructs; they are accessed via vptr_field_index in the vtable dispatch
            // code, not by name lookup.
        }, layout_field);
    }

    // Wire the struct_type with the LLVM type and named fields.
    // When the struct_type was reused from the unification registry and already
    // carries an LLVM type (a locally-synthesised instantiation resolved it
    // first), keep that one — re-attaching would needlessly replace an
    // equivalent type. Otherwise attach the KDI-derived layout.
    if (reused_st && st->is_resolved()) {
        // Already resolved by the locally-synthesised instantiation; leave as-is.
    } else if (llvm_st) {
        ctx->attach_llvm_struct_type(st, llvm_st, std::move(named_fields));
    } else {
        // Fallback: opaque placeholder (no named fields — codegen will fail on unsized)
        ctx->materialise_opaque_struct_type(st);
    }

    // ── Materialise K-model members ───────────────────────────────────────

    // Global/static variables
    for (const auto& kv : kdi_agg->static_vars) {
        get_or_create_imported_variable(&kv, ctx);
    }

    // Constructors
    for (const auto& kc : kdi_agg->constructors) {
        auto ic = imported_constructor::make_shared(agg, &kc);
        ic->assign_name(fq_to_abs_kname(kdi_agg->fq_name.empty() ? kdi_agg->name : kdi_agg->fq_name));
        attach_params(*ic, kc.params, *this, ctx);
        ic->set_visibility(kc.visibility == kdi::kdi_visibility::public_ ? PUBLIC : PROTECTED);
        if (auto doc = make_function_doc(kc.doc)) {
            ic->set_documentation(std::move(doc));
        }
        ic->create_this_parameter();
        agg->_constructors.push_back(ic);
    }

    // Destructor
    if (kdi_agg->destructor.has_value()) {
        const auto& kd = kdi_agg->destructor.value();
        auto id = imported_destructor::make_shared(agg, &kd);
        id->assign_name(fq_to_abs_kname(
            (kdi_agg->fq_name.empty() ? kdi_agg->name : kdi_agg->fq_name)
            + "::~" + kdi_agg->name));
        id->set_visibility(kd.visibility == kdi::kdi_visibility::public_ ? PUBLIC : PROTECTED);
        if (auto doc = make_function_doc(kd.doc)) {
            id->set_documentation(std::move(doc));
        }
        id->create_this_parameter();
        agg->_destructor = id;
    }

    // Methods — set virtual/abstract/slot flags from KDI so that
    // annotate_dispatch_info and overload resolution work correctly.
    for (const auto& km : kdi_agg->methods) {
        auto im = imported_method::make_shared(agg, &km);
        im->assign_name(fq_to_abs_kname(km.fq_name.empty() ? km.name : km.fq_name));
        auto ret = kdi_type_to_model_type(km.return_type, *this, ctx);
        if (ret) im->set_return_type(ret);
        attach_params(*im, km.params, *this, ctx);
        im->set_visibility(km.visibility == kdi::kdi_visibility::public_ ? PUBLIC : PROTECTED);
        if (km.is_virtual)     { im->set_virtual(true); }
        if (km.vtable_slot >= 0) { im->set_vtable_slot(km.vtable_slot); }
        if (km.is_abstract)    { im->set_abstract_func(true); }
        if (km.is_final)       { im->set_final_func(true); }
        if (km.is_const_member){ im->set_const_member(true); }
        // Template origin for method instantiations
        if (km.template_origin) {
            auto method_args = convert_template_origin_args(*km.template_origin, *this, ctx);
            im->set_tpl_instantiation_info(km.template_origin->base_name, std::move(method_args));
        }
        if (auto doc = make_function_doc(km.doc)) {
            im->set_documentation(std::move(doc));
        }
        im->create_this_parameter();
        agg->_children.push_back(im);
        agg->_functions.push_back(im);
    }

    // ── Set up vtable_layout for imported types with vtables ────────────────
    // Store vtable/RTTI symbol names and a FULLY populated entry list (both
    // `introducing_func` and `func` for every slot), not just slot counts.
    // This mirrors gen_class.cpp's build_vtable_layout() inherit-then-override
    // algorithm, applied here to imported aggregates so that a LOCAL class
    // deriving from an imported base can correctly inherit ALL of that base's
    // vtable entries — including slots introduced by a further-removed
    // grandparent that this immediate imported base never itself redeclares
    // (e.g. ::k::Exception redeclares no methods of its own: all of its vtable
    // slots — the destructor, hash(), getCode(), getCause(), hasCause() — are
    // inherited from ::k::Object/::k::Throwable). Without this, a local class
    // like `MyErr : public Exception` that doesn't override anything would
    // still need its OWN vtable once its own (compiler-generated) destructor
    // overrides the inherited destructor slot, and that new vtable would be
    // missing every slot Exception itself never redeclared — corrupting
    // virtual dispatch for those methods. Bases are always materialised
    // (recursively, bottom-up) before this point, so the primary base's own
    // vtable_layout (if any) is already complete when we get here.
    if (kdi_agg->vtable.has_value()) {
        const auto& kdi_vt = kdi_agg->vtable.value();

        auto vt_layout = std::make_shared<vtable_layout>();
        vt_layout->vtable_symbol = kdi_vt.vtable_symbol;
        vt_layout->rtti_symbol = kdi_vt.rtti_symbol;

        // Find the primary base (first declared base with a vtable, local or
        // imported), matching build_vtable_layout()'s selection rule.
        std::shared_ptr<vtable_layout> primary_base_vt;
        for (auto& bs : agg->get_bases()) {
            if (auto base_kl = std::dynamic_pointer_cast<klass>(bs.base)) {
                if (base_kl->has_vtable()) { primary_base_vt = base_kl->get_vtable(); break; }
            } else if (auto base_imp = std::dynamic_pointer_cast<imported_aggregate>(bs.base)) {
                if (base_imp->has_vtable()) { primary_base_vt = base_imp->get_vtable(); break; }
            }
        }

        // 1. Inherit the primary base's fully-resolved entries verbatim.
        if (primary_base_vt) {
            for (auto& entry : primary_base_vt->entries) {
                vtable_entry inherited;
                inherited.slot_index = entry.slot_index;
                inherited.introducing_func = entry.introducing_func;
                inherited.func = entry.func;
                vt_layout->entries.push_back(inherited);
            }
        }

        // 2. Overlay this aggregate's own slots (destructor + methods), either
        //    overriding an inherited slot at the same index or introducing a
        //    brand-new one (e.g. this is the very first class in the chain to
        //    declare a vtable at all).
        auto find_or_append_slot = [&](std::uint32_t slot_index) -> vtable_entry& {
            for (auto& entry : vt_layout->entries) {
                if (entry.slot_index == slot_index) return entry;
            }
            vtable_entry new_entry;
            new_entry.slot_index = slot_index;
            vt_layout->entries.push_back(new_entry);
            return vt_layout->entries.back();
        };

        for (const auto& slot : kdi_vt.slots) {
            auto& entry = find_or_append_slot(slot.slot_index);
            if (slot.introducing_func.find("::~") != std::string::npos) {
                if (auto id = agg->get_destructor()) {
                    if (!entry.introducing_func) entry.introducing_func = id;
                    entry.func = id;
                    id->set_vtable_slot((int)slot.slot_index);
                }
            } else {
                for (auto& child : agg->get_children()) {
                    auto im = std::dynamic_pointer_cast<imported_method>(child);
                    if (im && im->get_vtable_slot() == (int)slot.slot_index) {
                        if (!entry.introducing_func) entry.introducing_func = im;
                        entry.func = im;
                        break;
                    }
                }
            }
        }

        // Wire the vtable layout to the imported aggregate
        if (auto ik = std::dynamic_pointer_cast<imported_klass>(agg)) {
            ik->_vtable = vt_layout;
        } else if (auto ii = std::dynamic_pointer_cast<imported_interface>(agg)) {
            ii->_vtable = vt_layout;
        }
    }

    // ── Template origin metadata ────────────────────────────────────────────
    // If this aggregate is a concrete template instantiation, populate the
    // model-level template metadata so that has_tpl_args() returns true and
    // the base name / arguments are available for introspection.
    if (kdi_agg->template_origin) {
        auto model_args = convert_template_origin_args(*kdi_agg->template_origin, *this, ctx);
        agg->set_tpl_instantiation_info(kdi_agg->template_origin->base_name, std::move(model_args));
    }

    return agg;
}

// ─────────────────────────────────────────────────────────────────────────────
// unit::get_or_create_imported_enum
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<enumeration>
unit::get_or_create_imported_enum(const k::name& fq_name,
                                  std::shared_ptr<context> ctx)
{
    const k::name normalised_name = fq_name.has_root_prefix()
                                    ? fq_name.without_root_prefix() : fq_name;
    const std::string key = normalised_name.to_string();
    auto it = _imported_enums.find(key);
    if (it != _imported_enums.end()) {
        return it->second;
    }

    // Find the enum definition in loaded KDIs
    const kdi::kdi_enum* kdi_en = find_imported_enum(fq_name);
    if (!kdi_en) {
        return nullptr;
    }

    // Create the enumeration model node
    auto root = get_root_namespace();
    auto en = enumeration::make_shared(root, kdi_en->name);
    if (!kdi_en->fq_name.empty()) {
        en->assign_name(fq_to_abs_kname(kdi_en->fq_name));
    }

    // Cache before recursive base resolution to break cycles.
    _imported_enums[key] = en;

    // Resolve underlying type from KDI
    auto underlying_model_type = kdi_type_to_model_type(kdi_en->underlying_type, *this, ctx);
    auto underlying = std::dynamic_pointer_cast<primitive_type>(underlying_model_type);
    if (underlying) {
        en->set_underlying_type(underlying);
    }

    // Typed object-backed metadata (optional, for backward compatibility with old KDI files).
    if (kdi_en->object_type.has_value()) {
        auto object_model_type = kdi_type_to_model_type(*kdi_en->object_type, *this, ctx);
        if (auto object_st = std::dynamic_pointer_cast<struct_type>(object_model_type)) {
            en->set_object_type(object_st);
            if (kdi_en->object_table_symbol.has_value() && !kdi_en->object_table_symbol->empty()) {
                en->set_table_symbol(*kdi_en->object_table_symbol);
            } else {
                en->set_table_symbol("__klang_enum_table_" + en->get_mangled_name() + "__");
            }
        }
    }

    // Add all entries
    for (auto& kdi_entry : kdi_en->entries) {
        en->add_entry(
            kdi_entry.name,
            kdi_entry.value,
            kdi_entry.is_default,
            rebuild_object_init_brace(kdi_entry));
    }

    // Resolve base enum (derivation)
    if (kdi_en->base_fq_name.has_value()) {
        en->set_base_name(*kdi_en->base_fq_name);
        // Parse base fq_name into k::name
        std::vector<std::string> base_parts;
        const std::string& bfq = *kdi_en->base_fq_name;
        std::size_t start = 0;
        while (true) {
            auto pos = bfq.find("::", start);
            if (pos == std::string::npos) { base_parts.push_back(bfq.substr(start)); break; }
            base_parts.push_back(bfq.substr(start, pos - start));
            start = pos + 2;
        }
        auto base_en = get_or_create_imported_enum(k::name{false, std::move(base_parts)}, ctx);
        if (base_en) en->set_base(base_en);
    }

    en->set_resolved(true);

    // Create the enum_type and register it in the context
    if (underlying) {
        auto et = std::shared_ptr<enum_type>(new enum_type(en, underlying));
        en->set_enum_type(et);
        ctx->add_enum(key, et);
    }

    // Set visibility
    en->set_visibility(kdi_en->visibility == kdi::kdi_visibility::protected_ ? PROTECTED : PUBLIC);
    if (auto doc = make_doc_entity(kdi_en->doc)) {
        en->set_documentation(std::move(doc));
    }

    return en;
}

} // namespace k::model
