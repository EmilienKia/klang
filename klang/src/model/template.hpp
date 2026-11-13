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
#include <functional>
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
class generic_source_type;
class aggregate;
class function;
class union_type_def;
class ns;

// Identity is local to this model, not a serialized declaration origin.
// Different specializations of one source declaration require separate binding
// environments even though their source binder identities are the same.
struct template_binder_scope final {};

struct template_binder_id {
    std::shared_ptr<const template_binder_scope> scope;
    size_t ordinal = 0;

    bool operator==(const template_binder_id&) const = default;
};

struct template_binder_id_hash {
    size_t operator()(const template_binder_id& id) const {
        return std::hash<const template_binder_scope*>{}(id.scope.get())
            ^ (std::hash<size_t>{}(id.ordinal) << 1);
    }
};

inline std::string template_private_scope_name(const std::string& origin) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result = "__kdi_private_";
    for (unsigned char ch : origin) {
        result += hex[ch >> 4];
        result += hex[ch & 15];
    }
    return result;
}

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
     * Compilation-local source identity of the declared constraint, captured
     * before enclosing substitutions. A null value with a non-null constraint
     * means provenance is unavailable, not that the constraint is satisfied.
     * This metadata is neither a resolved constraint nor proof.
     */
    std::shared_ptr<generic_source_type> constraint_source_type;

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
    /** Shared by clones of this source declaration; never a KDI origin key. */
    std::shared_ptr<const template_binder_scope> binder_scope =
        std::make_shared<const template_binder_scope>();

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

    /** Private declarations carried by imported template source, outside consumer lookup. */
    std::shared_ptr<ns> private_definition_scope;

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

enum class constraint_analysis_state {
    verified,
    deferred,
    invalid
};

enum class constraint_analysis_reason {
    none,
    missing_argument,
    extra_argument,
    argument_shape,
    parameter_pack,
    value_parameter,
    declaration_provenance,
    argument_source_unknown,
    symbolic_argument,
    erased_argument,
    argument_aggregate_missing,
    unsupported_argument,
    unresolved_bound,
    erased_bound,
    bound_aggregate_missing,
    bound_source_unknown,
    symbolic_bound,
    unsupported_bound,
    incomplete_hierarchy,
    generic_owner_arg,
    generic_drain_arg,
    not_aggregate,
    kind,
    constraint,
    value_type_mismatch
};

/**
 * Tri-state result of the declaration-level constraint evaluation (kind filter,
 * generic owner/drain rules and base-type bounds). `deferred` means that no
 * violation was proven but at least one obligation could not be decided from
 * the model alone; deferred bound reasons (unresolved_bound, unsupported_bound,
 * bound_aggregate_missing) must never be treated as satisfied by a caller that
 * is performing the final check.
 */
struct template_constraint_verdict {
    constraint_analysis_state state = constraint_analysis_state::verified;
    constraint_analysis_reason reason = constraint_analysis_reason::none;
    size_t error_index = static_cast<size_t>(-1);
    std::string error_kind; // legacy kind, set only when state is invalid

    bool is_invalid() const { return state == constraint_analysis_state::invalid; }
    bool is_unproven_bound() const;
};

struct template_constraint_check {
    constraint_analysis_state state = constraint_analysis_state::deferred;
    constraint_analysis_reason reason = constraint_analysis_reason::none;
    std::optional<size_t> parameter_index;
    std::optional<size_t> argument_index;
};

/**
 * A compilation-local snapshot for later constraint-obligation propagation.
 * The type anchors retain wrapper subtypes; aggregate anchors retain the model
 * identities referenced by otherwise weak struct_type links. Model identities
 * remain mutable, so analysis must be repeated after relevant model changes.
 */
struct template_constraint_analysis {
    constraint_analysis_state state = constraint_analysis_state::deferred;
    std::shared_ptr<const template_binder_scope> declaration_scope;
    std::vector<template_param_descriptor> params;
    std::vector<template_argument> args;
    std::vector<std::shared_ptr<generic_source_type>> argument_sources;
    std::vector<std::shared_ptr<generic_source_type>> argument_type_anchors;
    std::vector<std::shared_ptr<generic_source_type>> bound_type_anchors;
    std::vector<std::shared_ptr<aggregate>> aggregate_anchors;
    std::vector<template_constraint_check> checks;
};

/**
 * Analyze constraint evidence without resolving names or changing acceptance.
 * The caller must supply complete declaration-derived descriptors. Source
 * arguments are indexed like args and must be captured before erasure or
 * other loss of source identity; missing provenance and unsupported forms
 * defer. Verified is a model-level proof only, not a proof of unresolved
 * declarations. Pack and value-parameter checking is deliberately deferred.
 */
template_constraint_analysis analyze_template_arg_constraints(
    const tpl_info& ti,
    const std::vector<template_argument>& args,
    const std::vector<std::shared_ptr<generic_source_type>>& argument_sources = {});

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
 *
 * Legacy boolean API, fail closed: a violation or a bound that is unresolved,
 * not an aggregate, or without an aggregate model is rejected (error_kind
 * "unresolved_bound" for the latter). Other deferred obligations (erased
 * generic arguments and bounds) are not rejected. Callers that must tell a
 * proven violation from an unproven bound use evaluate_template_arg_constraints.
 */
bool validate_template_arg_constraints(
    const std::vector<template_param_descriptor>& params,
    const std::vector<template_argument>& args,
    size_t& error_index,
    std::string& error_kind);

/**
 * Tri-state evaluation shared by every call site that validates concrete
 * template arguments. invalid carries the legacy error kind and the
 * offending argument index; deferred carries the first undecided obligation.
 * Hierarchy evidence is the aggregate model's: it treats an imported base
 * list as complete until KDI records completeness.
 */
template_constraint_verdict evaluate_template_arg_constraints(
    const std::vector<template_param_descriptor>& params,
    const std::vector<template_argument>& args);

/**
 * Copy of ti.params where a bound naming an earlier parameter of the same list
 * (`template<class B, class T : B>`) is replaced by that parameter's concrete
 * aggregate argument, so the obligation can be decided when the arguments are known.
 * Other bounds are copied unchanged.
 */
std::vector<template_param_descriptor> bind_same_list_dependent_bounds(
    const tpl_info& ti, const std::vector<template_argument>& args);

/** Same as above, with the restrictions of generic (type-erased) declarations. */
template_constraint_verdict evaluate_template_arg_constraints(
    const tpl_info& ti,
    const std::vector<template_argument>& args);

/**
 * True when the bound of params[index] is a still-unresolved plain written name
 * (no template arguments, binder or earlier-parameter dependency), i.e. a name
 * that lexical lookup is expected to find. Such a bound that cannot be found
 * is an error rather than an obligation to replay. The name is returned in
 * @p written_name.
 */
bool is_independent_written_bound(
    const std::vector<template_param_descriptor>& params, size_t index,
    k::name* written_name = nullptr);

/**
 * Same as above, with the additional restrictions of generic (type-erased)
 * declarations: when @p ti describes a generic, owner ('!') and drain ('#')
 * types are rejected as type arguments (error kinds "generic_owner_arg" and
 * "generic_drain_arg").
 */
bool validate_template_arg_constraints(
    const tpl_info& ti,
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
 * @param error_kind     The kind of violation ("kind", "not_aggregate", "constraint",
 *                       "generic_owner_arg", "generic_drain_arg").
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
