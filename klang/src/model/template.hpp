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

#ifndef KLANG_TEMPLATE_HPP
#define KLANG_TEMPLATE_HPP

#include "../common/common.hpp"

#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace k::parse::ast {
struct expression;
}

namespace k::model {

class type;
class aggregate;
class function;
class union_type_def;

/**
 * Describes the kind of a template parameter.
 *
 * - TYPENAME: accepts any type argument.
 * - STRUCT/CLASS/INTERFACE: constrained to the corresponding aggregate kind.
 * - VALUE: a non-type (value) parameter with an explicit type.
 */
enum class template_param_kind {
    TYPENAME,
    STRUCT,
    CLASS,
    INTERFACE,
    VALUE
};

/**
 * Describes a single parameter of a template definition.
 *
 * For type parameters: kind is TYPENAME/STRUCT/CLASS/INTERFACE,
 *   constraint_type is an optional base-type constraint,
 *   default_type is an optional default type.
 *
 * For value parameters: kind is VALUE,
 *   value_type is the explicit type (e.g. unsigned int),
 *   default_value is an optional compile-time constant default.
 */
struct template_param_descriptor {
    /** Parameter kind (type or value). */
    template_param_kind kind = template_param_kind::TYPENAME;

    /** Parameter name (e.g. "T", "N"). */
    std::string name;

    /** True if this is a parameter pack (e.g. typename... Ts). Only valid for type params. */
    bool is_pack = false;

    /**
     * For type parameters: optional constraint type (e.g. the "Base" in
     * "template<class T : Base>"). nullptr if unconstrained.
     */
    std::shared_ptr<type> constraint_type;

    /**
     * For type parameters: optional default type. nullptr if no default.
     */
    std::shared_ptr<type> default_type;

    /**
     * For value parameters: the explicit type of the value parameter
     * (e.g. "unsigned int" in "N : unsigned int"). nullptr for type parameters.
     */
    std::shared_ptr<type> value_type;

    /**
     * For value parameters: optional default value as a compile-time constant.
     * Holds the concrete primitive value (int, long, float, double, bool, char, …).
     * Only meaningful when kind == VALUE.
     */
    std::optional<k::value_type> default_value;

    /**
     * For value parameters: raw AST default expression.
     * Used when compile-time evaluation must be deferred until the value type
     * becomes fully resolvable (e.g. aggregate defaults captured before type passes).
     */
    std::shared_ptr<k::parse::ast::expression> default_value_expr;

    /** True if this is a type parameter. */
    bool is_type_param() const {
        return kind != template_param_kind::VALUE;
    }

    /** True if this is a value parameter. */
    bool is_value_param() const {
        return kind == template_param_kind::VALUE;
    }
};

/**
 * A concrete template argument provided at an instantiation site.
 *
 * Exactly one of type_arg / value_arg is set:
 * - type_arg for type arguments (e.g. "int" in Pair<int>)
 * - value_arg for value arguments (e.g. "10" in Array<int, 10>)
 */
struct template_argument {
    /** Type argument (nullptr if this is a value argument). */
    std::shared_ptr<type> type_arg;

    /** Value argument (nullopt if this is a type argument).
     *  Holds the concrete primitive value (int, long, float, double, bool, char, …). */
    std::optional<k::value_type> value_arg;

    /** For pack arguments: the list of type arguments in the pack. */
    std::vector<std::shared_ptr<type>> pack_types;

    /** Optional declared/deduced type of the value argument (e.g. enum_type). */
    std::shared_ptr<type> value_type;

    /** True if this is a type argument. */
    bool is_type() const { return type_arg != nullptr && pack_types.empty(); }

    /** True if this is a value argument. */
    bool is_value() const { return value_arg.has_value(); }

    /** True if this is a parameter pack argument (holds multiple types). */
    bool is_pack() const { return !pack_types.empty(); }

    /** Create a type argument. */
    static template_argument make_type(std::shared_ptr<type> t) {
        return {std::move(t), std::nullopt, {}, nullptr};
    }

    /** Create a value argument from any primitive value. */
    static template_argument make_value(k::value_type v, std::shared_ptr<type> vt = nullptr) {
        return {nullptr, std::move(v), {}, std::move(vt)};
    }

    /** Create a value argument from an integer (convenience overload, stores as int). */
    static template_argument make_value(int64_t v, std::shared_ptr<type> vt = nullptr) {
        return {nullptr, k::value_type{static_cast<int>(v)}, {}, std::move(vt)};
    }

    /** Create a pack argument from a list of types. */
    static template_argument make_pack(std::vector<std::shared_ptr<type>> types) {
        return {nullptr, std::nullopt, std::move(types), nullptr};
    }

    /**
     * Return the value argument as an int64_t (for instantiation key generation
     * and other contexts that need a single integer representation).
     * For integral types, returns the value cast to int64_t.
     * For floating types, returns the bit representation.
     * For other types, returns 0.
     */
    int64_t value_as_int64() const {
        if (!value_arg.has_value()) return 0;
        return std::visit([](auto&& v) -> int64_t {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_integral_v<T>) {
                return static_cast<int64_t>(v);
            } else if constexpr (std::is_floating_point_v<T>) {
                // Bit-cast for unique key generation
                if constexpr (std::is_same_v<T, float>) {
                    int32_t bits;
                    std::memcpy(&bits, &v, sizeof(bits));
                    return static_cast<int64_t>(bits);
                } else {
                    int64_t bits;
                    std::memcpy(&bits, &v, sizeof(bits));
                    return bits;
                }
            } else {
                return 0;
            }
        }, *value_arg);
    }
};

/**
 * Key for looking up an existing instantiation in the instantiation registry.
 * Encodes the list of concrete arguments as a string for map lookup.
 */
using template_instantiation_key = std::string;

/**
 * Describes a template definition attached to an aggregate or function.
 *
 * Stores the template parameter descriptors.  The model members of the
 * template entity (variables, functions, body, etc.) are built with
 * unresolved_type placeholders for template parameter references.
 * Instantiation clones and type-substitutes at the model level.
 *
 * The original AST nodes are retained optionally for diagnostic purposes
 * (source location in error/warning messages) but are NOT required for
 * instantiation.
 *
 * The instantiations map caches already-instantiated concrete entities
 * so that the same <Args> combination is not instantiated twice.
 */
struct tpl_info {
    /** Template parameter descriptors (in declaration order). */
    std::vector<template_param_descriptor> params;

    /**
    /**
     * For templates imported from another module's KDI: the originating module's
     * normalised fully-qualified namespace, WITHOUT a leading root prefix
     * (e.g. "k" for ::k::Optional, "a::b" for ::a::b::Vec).
     *
     * Empty for templates declared in the current compilation unit.
     *
     * Imported templates are re-homed under the consumer module's namespace, which
     * loses their true origin. This tag preserves it so the template-instantiation
     * registry key (unit::make_instantiation_registry_key) can be qualified by the
     * origin namespace — preventing two same-named templates imported from different
     * namespaces from colliding to a single struct_type in the registry.
     */
    std::string origin_module_ns_fq;

    /** True if this template was declared with the 'generic' keyword.
     *
     * When true:
     *  - All parameters are type parameters (no value parameters).
     *  - Type params may only be used via addressers in member types and bodies.
     *  - Owner ('!') of a generic type param requires a 'class' or 'interface' constraint.
     *  - Code synthesis is performed once (uniform materialization), with all generic
     *    type params mapped to opaque pointers in LLVM IR.
     *  - The same synthesised aggregate/function is reused for all instantiations.
     */
    bool is_generic = false;

    /** True when this template definition comes from KDI signature metadata only. */
    bool is_imported_signature_only = false;

    /**
     * Raw K source text of the complete template declaration + entity body.
     * Captured during parsing for KDI export, so that importing compilers can
     * re-parse and re-instantiate the template locally with new arguments.
     * Empty if not captured (e.g. for imported or synthetic templates).
     * Always empty for generic declarations (synthesised in declaration module,
     * KDI exports the signature only).
     */
    std::string source_text;

    /**
     * Usage-site concrete type bindings for generic declarations.
     *
     * Keyed by build_instantiation_key(args), this stores the concrete type
     * selected for each type parameter name at use sites.
     *
     * This metadata is used for Phase 8 type tracking and does not trigger
     * additional code synthesis.
     */
    struct generic_usage_descriptor {
        std::unordered_map<std::string, std::shared_ptr<type>> type_bindings;
    };
    std::unordered_map<template_instantiation_key, generic_usage_descriptor> generic_usages;

    /**
     * Cache of concrete instantiations keyed by a canonical string
     * encoding of the template arguments.
     *
     * For template aggregates: values are shared_ptr<aggregate>.
     * For template functions: values are shared_ptr<function>.
     * (We use a variant so both can share the same tpl_info type.)
     */
    using instantiation_entry = std::variant<
        std::shared_ptr<aggregate>,
        std::shared_ptr<function>,
        std::shared_ptr<union_type_def>
    >;
    std::unordered_map<template_instantiation_key, instantiation_entry> instantiations;

    /** True if this tpl_info has been populated with parameters. */
    bool is_valid() const { return !params.empty(); }

    /** Number of template parameters. */
    size_t param_count() const { return params.size(); }
};

/**
 * Human-readable string for a template_param_kind.
 */
inline const char* to_string(template_param_kind kind) {
    switch (kind) {
        case template_param_kind::TYPENAME:  return "typename";
        case template_param_kind::STRUCT:    return "struct";
        case template_param_kind::CLASS:     return "class";
        case template_param_kind::INTERFACE: return "interface";
        case template_param_kind::VALUE:     return "value";
    }
    return "unknown";
}

/**
 * Validate that a vector of concrete template arguments satisfies the
 * constraints declared on the template parameters (kind filter and
 * base-type constraint).
 *
 * @param params  Template parameter descriptors (from tpl_info).
 * @param args    Concrete template arguments to validate.
 * @param[out] error_index  Set to the 0-based index of the first argument
 *                          that violates a constraint, or (size_t)-1 if
 *                          all arguments pass.
 * @param[out] error_kind   A human-readable reason for the failure:
 *                          "kind" for kind-filter mismatch,
 *                          "constraint" for base-type constraint violation,
 *                          "not_aggregate" if a kind-filtered param receives
 *                          a non-aggregate type.
 * @return true if all arguments pass, false otherwise.
 */
bool validate_template_arg_constraints(
    const std::vector<template_param_descriptor>& params,
    const std::vector<template_argument>& args,
    size_t& error_index,
    std::string& error_kind);

/**
 * Build a pair { diagnostic_code, formatted_message } for a template
 * constraint violation identified by validate_template_arg_constraints.
 *
 * @param template_name  The short name of the template being instantiated.
 * @param params         Template parameter descriptors.
 * @param args           Concrete template arguments.
 * @param error_index    The 0-based index of the violating argument.
 * @param error_kind     The kind of violation ("kind", "not_aggregate", "constraint").
 * @return { error_code (from template_diag), formatted message string }
 */
std::pair<unsigned int, std::string> format_constraint_error(
    const std::string& template_name,
    const std::vector<template_param_descriptor>& params,
    const std::vector<template_argument>& args,
    size_t error_index,
    const std::string& error_kind);

} // namespace k::model
#endif // KLANG_TEMPLATE_HPP
