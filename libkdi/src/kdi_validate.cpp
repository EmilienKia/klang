/*
 * K Language compiler — libkdi
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

#include "kdi_validate.hpp"

#include <string_view>
#include <type_traits>
#include <unordered_set>

namespace kdi {

namespace {

std::string_view unrooted(std::string_view name) {
    return name.starts_with("::") ? name.substr(2) : name;
}

void validate_aggregate(const kdi_aggregate& agg,
                        const std::string& path,
                        const kdi_type_table& types,
                        kdi_validation_result& result);

void validate_params(const std::vector<kdi_param>& params,
                     const std::string& path,
                     const kdi_type_table& types,
                     kdi_validation_result& result,
                     const std::unordered_set<std::string>* template_params = nullptr,
                     bool check_refs = true);

void validate_template_def(const kdi_template_def& def,
                           const std::string& path,
                           const kdi_type_table& types,
                           kdi_validation_result& result);

void validate_type_tree(const kdi_type& t, const std::string& path,
                        const kdi_type_table& types, kdi_validation_result& result,
                        const std::unordered_set<std::string>* template_params,
                        bool nested_refs, bool check_ref,
                        std::unordered_set<const kdi_type*>& active) {
    if (!active.insert(&t).second) return;
    if (const auto* param = std::get_if<kdi_template_param_ref>(&t.value);
        param && (param->name.empty() || !template_params || !template_params->count(param->name))) {
        result.add(path, "template parameter '" + param->name + "' is not declared in this scope");
    }
    if (auto* aref = check_ref ? std::get_if<kdi_aggregate_ref>(&t.value) : nullptr) {
        bool found = false;
        for (auto& e : types.aggregates) {
            if (unrooted(e.fq_name) == unrooted(aref->fq_name)) { found = true; break; }
        }
        if (!found) {
            result.add(path, "aggregate reference '" + aref->fq_name
                              + "' not found in type table");
        }
    }
    if (auto* eref = check_ref ? std::get_if<kdi_enum_ref>(&t.value) : nullptr) {
        bool found = false;
        for (auto& e : types.enums) {
            if (unrooted(e.fq_name) == unrooted(eref->fq_name)) { found = true; break; }
        }
        if (!found) {
            result.add(path, "enum reference '" + eref->fq_name
                              + "' not found in type table");
        }
    }
    auto descend = [&](const std::shared_ptr<kdi_type>& child, const std::string& suffix) {
        if (child) validate_type_tree(*child, path + suffix, types, result,
                                     template_params, nested_refs, nested_refs, active);
    };
    std::visit([&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (requires { value.inner; }) {
            descend(value.inner, ".inner");
        } else if constexpr (requires { value.elem; }) {
            descend(value.elem, ".elem");
        } else if constexpr (std::is_same_v<T, kdi_callable_type>) {
            descend(value.ret, ".ret");
            for (size_t i = 0; i < value.params.size(); ++i)
                descend(value.params[i], ".params[" + std::to_string(i) + "]");
            for (size_t i = 0; i < value.throws.size(); ++i)
                descend(value.throws[i], ".throws[" + std::to_string(i) + "]");
        } else if constexpr (std::is_same_v<T, kdi_generic_ref_type>) {
            for (size_t i = 0; i < value.args.size(); ++i)
                descend(value.args[i], ".args[" + std::to_string(i) + "]");
        }
    }, t.value);
    active.erase(&t);
}

void validate_type(const kdi_type& t, const std::string& path,
                   const kdi_type_table& types, kdi_validation_result& result,
                   const std::unordered_set<std::string>* template_params = nullptr,
                   bool nested_refs = false, bool check_ref = true) {
    std::unordered_set<const kdi_type*> active;
    validate_type_tree(t, path, types, result, template_params, nested_refs, check_ref, active);
}

void validate_throws_placeholders(const std::vector<kdi_type>& spec,
                                  const std::string& path, const kdi_type_table& types,
                                  kdi_validation_result& result,
                                  const std::unordered_set<std::string>* template_params = nullptr) {
    for (size_t i = 0; i < spec.size(); ++i)
        validate_type(spec[i], path + ".throws_spec[" + std::to_string(i) + "]",
                      types, result, template_params, false, false);
}

void validate_template_signature_aggregate(const kdi_aggregate& agg,
                                           const std::string& path,
                                           const kdi_type_table& types,
                                           kdi_validation_result& result,
                                           const std::unordered_set<std::string>& template_params,
                                           bool check_refs = true) {
    if (check_refs && agg.name.empty()) {
        result.add(path + ".name", "must not be empty");
    }
    for (size_t i = 0; i < agg.layout.size(); ++i) {
        if (auto* member = std::get_if<kdi_layout_member>(&agg.layout[i])) {
            validate_type(member->type, path + ".layout[" + std::to_string(i) + "].type",
                          types, result, &template_params, false, check_refs);
        }
    }
    for (size_t i = 0; i < agg.methods.size(); ++i) {
        auto mp = path + ".methods[" + std::to_string(i) + "]";
        validate_type(agg.methods[i].return_type, mp + ".return_type",
                      types, result, &template_params, false, check_refs);
        validate_params(agg.methods[i].params, mp, types, result, &template_params, check_refs);
        validate_throws_placeholders(agg.methods[i].throws_spec, mp, types, result, &template_params);
    }
    for (size_t i = 0; i < agg.constructors.size(); ++i) {
        auto cp = path + ".constructors[" + std::to_string(i) + "]";
        validate_params(agg.constructors[i].params, cp, types, result, &template_params, check_refs);
    }
    for (size_t i = 0; i < agg.static_vars.size(); ++i) {
        validate_type(agg.static_vars[i].type,
                      path + ".static_vars[" + std::to_string(i) + "].type",
                      types, result, &template_params, false, check_refs);
    }
    for (size_t i = 0; i < agg.nested.size(); ++i)
        validate_template_signature_aggregate(agg.nested[i],
            path + ".nested[" + std::to_string(i) + "]", types, result, template_params, check_refs);
}

void validate_template_signature_function(const kdi_function& fn,
                                          const std::string& path,
                                          const kdi_type_table& types,
                                          kdi_validation_result& result,
                                          const std::unordered_set<std::string>& template_params,
                                          bool check_refs = true) {
    if (check_refs && fn.name.empty()) {
        result.add(path + ".name", "must not be empty");
    }
    validate_type(fn.return_type, path + ".return_type", types, result, &template_params, false, check_refs);
    validate_params(fn.params, path, types, result, &template_params, check_refs);
    validate_throws_placeholders(fn.throws_spec, path, types, result, &template_params);
}

void validate_template_def(const kdi_template_def& def,
                           const std::string& path,
                           const kdi_type_table& types,
                           kdi_validation_result& result) {
    if (def.name.empty()) {
        result.add(path + ".name", "must not be empty");
    }
    if (def.fq_name.empty()) {
        result.add(path + ".fq_name", "must not be empty");
    }
    if (def.entity_kind.empty()) {
        result.add(path + ".entity_kind", "must not be empty");
    }
    if (def.params.empty()) {
        result.add(path + ".params", "must not be empty");
    }
    std::unordered_set<std::string> template_params;
    for (const auto& param : def.params) {
        if (!param.name.empty() && (param.kind == "typename" || param.kind == "struct" ||
            param.kind == "class" || param.kind == "interface"))
            template_params.insert(param.name);
    }
    if (def.is_generic) {
        if (!def.source.empty()) {
            result.add(path + ".source", "must be empty for generic template definitions");
        }
        for (size_t i = 0; i < def.params.size(); ++i) {
            if (def.params[i].kind == "value") {
                result.add(path + ".params[" + std::to_string(i) + "]",
                           "generic template definitions may not declare value parameters");
            }
        }
        if (def.entity_kind == "function") {
            if (!def.function_signature) {
                result.add(path + ".function_signature", "is required for generic function definitions");
            } else {
                validate_template_signature_function(*def.function_signature,
                                                    path + ".function_signature",
                                                    types,
                                                    result, template_params);
            }
        } else {
            if (!def.aggregate_signature) {
                result.add(path + ".aggregate_signature", "is required for generic aggregate definitions");
            } else {
                validate_template_signature_aggregate(*def.aggregate_signature,
                                                     path + ".aggregate_signature",
                                                     types,
                                                     result, template_params);
            }
        }
    } else {
        if (def.function_signature)
            validate_template_signature_function(*def.function_signature, path + ".function_signature",
                                                 types, result, template_params, false);
        if (def.aggregate_signature)
            validate_template_signature_aggregate(*def.aggregate_signature, path + ".aggregate_signature",
                                                  types, result, template_params, false);
    }
}

void validate_enum(const kdi_enum& en,
                   const std::string& path,
                   const kdi_type_table& types,
                   kdi_validation_result& result) {
    if (en.fq_name.empty()) {
        result.add(path, "fq_name must not be empty");
    }
    validate_type(en.underlying_type, path + ".underlying_type", types, result);
    if (en.object_table_kind && *en.object_table_kind != "runtime")
        result.add(path + ".object_table_kind", "must be 'runtime' when present");
    if (en.object_table_kind && !en.object_type)
        result.add(path + ".object_table_kind", "must not be set when object_type is absent");
    if (en.object_type.has_value()) {
        validate_type(*en.object_type, path + ".object_type", types, result);
        if (!std::holds_alternative<kdi_aggregate_ref>(en.object_type->value)) {
            result.add(path + ".object_type", "must be an aggregate type reference");
        }
        if (!en.object_table_symbol.has_value() || en.object_table_symbol->empty()) {
            result.add(path + ".object_table_symbol", "must be set for object-backed enums");
        }
    } else {
        if (en.object_table_symbol.has_value() && !en.object_table_symbol->empty()) {
            result.add(path + ".object_table_symbol", "must not be set when object_type is absent");
        }
        for (size_t i = 0; i < en.entries.size(); ++i) {
            if (!en.entries[i].object_init_members.empty()) {
                result.add(path + ".entries[" + std::to_string(i) + "].object_init_members",
                           "must be empty for integer-backed enums");
            }
        }
    }
}

void validate_params(const std::vector<kdi_param>& params,
                     const std::string& path,
                     const kdi_type_table& types,
                     kdi_validation_result& result,
                     const std::unordered_set<std::string>* template_params,
                     bool check_refs) {
    for (size_t i = 0; i < params.size(); ++i) {
        validate_type(params[i].type, path + ".params[" + std::to_string(i) + "]",
                      types, result, template_params, false, check_refs);
    }
}

void validate_layout(const std::vector<kdi_layout_field>& layout,
                     const std::string& path,
                     kdi_validation_result& result) {
    // Field indices must be strictly increasing
    int32_t prev_index = -1;
    for (size_t i = 0; i < layout.size(); ++i) {
        uint32_t idx = std::visit([](const auto& f) -> uint32_t {
            return f.llvm_field_index;
        }, layout[i]);
        if (static_cast<int32_t>(idx) <= prev_index) {
            result.add(path + ".layout[" + std::to_string(i) + "]",
                       "llvm_field_index " + std::to_string(idx)
                       + " is not strictly greater than previous "
                       + std::to_string(prev_index));
        }
        prev_index = static_cast<int32_t>(idx);
    }
}

void validate_vtable(const kdi_vtable& vt, const std::string& path,
                     kdi_validation_result& result) {
    if (vt.vtable_symbol.empty()) {
        result.add(path, "vtable_symbol must not be empty");
    }
    // Slot indices must be contiguous starting at 0
    for (size_t i = 0; i < vt.slots.size(); ++i) {
        if (vt.slots[i].slot_index != static_cast<uint32_t>(i)) {
            result.add(path + ".slots[" + std::to_string(i) + "]",
                       "slot_index " + std::to_string(vt.slots[i].slot_index)
                       + " expected " + std::to_string(i));
        }
    }
}

void validate_aggregate(const kdi_aggregate& agg,
                        const std::string& path,
                        const kdi_type_table& types,
                        kdi_validation_result& result) {
    if (agg.fq_name.empty()) {
        result.add(path, "fq_name must not be empty");
    }
    if (agg.llvm_def.empty()) {
        result.add(path, "llvm_def must not be empty");
    }
    validate_layout(agg.layout, path, result);

    for (size_t i = 0; i < agg.methods.size(); ++i) {
        auto mp = path + ".methods[" + std::to_string(i) + "]";
        validate_type(agg.methods[i].return_type, mp + ".return_type", types, result);
        validate_params(agg.methods[i].params, mp, types, result);
        validate_throws_placeholders(agg.methods[i].throws_spec, mp, types, result);
        if (!agg.methods[i].is_deleted && agg.methods[i].mangled_name.empty()) {
            result.add(mp, "mangled_name must not be empty");
        }
        if (!agg.methods[i].is_deleted && agg.methods[i].llvm_def.empty()) {
            result.add(mp, "llvm_def must not be empty");
        }
    }
    for (size_t i = 0; i < agg.constructors.size(); ++i) {
        auto cp = path + ".constructors[" + std::to_string(i) + "]";
        validate_params(agg.constructors[i].params, cp, types, result);
        if (!agg.constructors[i].is_deleted && agg.constructors[i].mangled_name.empty()) {
            result.add(cp, "mangled_name must not be empty");
        }
        if (!agg.constructors[i].is_deleted && agg.constructors[i].llvm_def.empty()) {
            result.add(cp, "llvm_def must not be empty");
        }
    }
    if (agg.destructor && agg.destructor->llvm_def.empty()) {
        result.add(path + ".destructor", "llvm_def must not be empty");
    }
    if (agg.vtable) {
        validate_vtable(*agg.vtable, path + ".vtable", result);
        if (agg.vtable->llvm_def.empty()) {
            result.add(path + ".vtable", "llvm_def must not be empty");
        }
    }
    for (size_t i = 0; i < agg.nested.size(); ++i) {
        validate_aggregate(agg.nested[i],
                           path + ".nested[" + std::to_string(i) + "]",
                           types, result);
    }
    for (size_t i = 0; i < agg.nested_unions.size(); ++i) {
        auto& u = agg.nested_unions[i];
        std::string upath = path + ".nested_unions[" + std::to_string(i) + "]";
        if (u.name.empty()) result.add(upath, "union name must not be empty");
    }
}

void validate_namespace(const kdi_namespace& ns,
                        const std::string& path,
                        const kdi_type_table& types,
                        kdi_validation_result& result) {
    // Check for duplicate fq_names among aggregates
    std::unordered_set<std::string> seen;
    for (size_t i = 0; i < ns.aggregates.size(); ++i) {
        auto ap = path + ".aggregates[" + std::to_string(i) + "]";
        if (!seen.insert(ns.aggregates[i].fq_name).second) {
            result.add(ap, "duplicate fq_name '" + ns.aggregates[i].fq_name + "'");
        }
        validate_aggregate(ns.aggregates[i], ap, types, result);
    }
    for (size_t i = 0; i < ns.functions.size(); ++i) {
        auto fp = path + ".functions[" + std::to_string(i) + "]";
        validate_type(ns.functions[i].return_type, fp + ".return_type", types, result);
        validate_params(ns.functions[i].params, fp, types, result);
        validate_throws_placeholders(ns.functions[i].throws_spec, fp, types, result);
        if (ns.functions[i].mangled_name.empty()) {
            result.add(fp, "mangled_name must not be empty");
        }
        if (ns.functions[i].llvm_def.empty()) {
            result.add(fp, "llvm_def must not be empty");
        }
    }
    for (size_t i = 0; i < ns.variables.size(); ++i) {
        auto vp = path + ".variables[" + std::to_string(i) + "]";
        validate_type(ns.variables[i].type, vp + ".type", types, result);
        if (ns.variables[i].mangled_name.empty()) {
            result.add(vp, "mangled_name must not be empty");
        }
    }
    for (size_t i = 0; i < ns.enums.size(); ++i) {
        auto ep = path + ".enums[" + std::to_string(i) + "]";
        validate_enum(ns.enums[i], ep, types, result);
    }
    for (size_t i = 0; i < ns.namespaces.size(); ++i) {
        validate_namespace(ns.namespaces[i],
                           path + ".namespaces[" + std::to_string(i) + "]",
                           types, result);
    }
    for (size_t i = 0; i < ns.template_defs.size(); ++i) {
        validate_template_def(ns.template_defs[i],
                              path + ".template_defs[" + std::to_string(i) + "]",
                              types,
                              result);
    }
}

} // anonymous namespace

kdi_validation_result kdi_validate(const kdi_file& file) {
    kdi_validation_result result;

    // Schema version
    if (file.header.schema_major != KDI_SCHEMA_MAJOR) {
        result.add("header.schema_major",
                   "expected " + std::to_string(KDI_SCHEMA_MAJOR)
                   + " got " + std::to_string(file.header.schema_major));
    }
    if (file.header.schema_minor != KDI_SCHEMA_MINOR) {
        result.add("header.schema_minor",
                   "expected " + std::to_string(KDI_SCHEMA_MINOR)
                   + " got " + std::to_string(file.header.schema_minor));
    }
    if (file.header.module_name.empty()) {
        result.add("header.module_name", "must not be empty");
    }

    // Namespace tree
    validate_namespace(file.unit.root_ns, "unit.root_ns", file.types, result);

    return result;
}

} // namespace kdi

