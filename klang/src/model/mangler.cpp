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

#include "mangler.hpp"

#include "context.hpp"
#include "model.hpp"
#include "template.hpp"
#include "type.hpp"

#include "../common/operator_names.hpp"
#include "../errors.hpp"

#include <cstring>

#include <iosfwd>
#include <sstream>


#define K_LANG_SYMBOL_PREFIX "_K"

#define SYMBOL_TYPE_FUNCTION "F"
#define SYMBOL_TYPE_VARIABLE "V"

#define SYMBOL_QUALIFIED_PREFIX "N"
#define SYMBOL_QUALIFIED_SUFFIX "E"

#define SYMBOL_MEMBER            "M"

#define SYMBOL_MODIFIER_CONST      "K"
#define SYMBOL_MODIFIER_VOLATILE   "V"
#define SYMBOL_MODIFIER_RESTRICT   "r"
#define SYMBOL_MODIFIER_PTR        "P"
#define SYMBOL_MODIFIER_REF        "R"
#define SYMBOL_MODIFIER_REF_LVAL   SYMBOL_MODIFIER_REF
#define SYMBOL_MODIFIER_REF_RVAL   "O"
// 'L' for link (+) : mutable strong (non-null) indirection
#define SYMBOL_MODIFIER_LINK       "L"
// 'Q' for view (?) : immutable nullable indirection
// 'W' for owner (!) : owning pointer (unique ownership)
#define SYMBOL_MODIFIER_VIEW       "Q"
#define SYMBOL_MODIFIER_OWNER      "W"
// 'D' for drain (#) : drainable (immutable binding, non-null) indirection
#define SYMBOL_MODIFIER_DRAIN      "D"
// 'A' for array : { i32 size, [N x T] }. Followed by the element type, with the
// fixed size and a '_' separator inserted for sized arrays.
#define SYMBOL_MODIFIER_ARRAY      "A"

// Function reference type mangling:
// PF<params>E       : pointer (*) to free function
// QF<params>E       : view (?) to free function
// LF<params>E       : link (+) to free function
// PM<class>F<params>E  : pointer (*) to member function of <class>
// QM<class>F<params>E  : view (?)
// LM<class>F<params>E  : link (+)
#define SYMBOL_MODIFIER_FN_REF     "F"
#define SYMBOL_MODIFIER_MEM_FN     "M"

#define SYMBOL_STATIC_CONSTRUCTOR_NAME "C"
#define SYMBOL_STATIC_DESTRUCTOR_NAME  "D"

#define SYMBOL_CONSTRUCTOR_C1_NAME "C1"
#define SYMBOL_CONSTRUCTOR_C2_NAME "C2"
#define SYMBOL_DESTRUCTOR_D1_NAME  "D1"
#define SYMBOL_DESTRUCTOR_D2_NAME  "D2"

// Vtable global variable prefix: _KTV
#define SYMBOL_VTABLE_PREFIX "TV"

// Virtual dispatch thunk: 'v' after 'M' means "virtual dispatch"
#define SYMBOL_VIRTUAL_DISPATCH "Mv"

#define TYPE_VOID           "v"
#define TYPE_BOOL           "b"
#define TYPE_CHAR           "c"
#define TYPE_BYTE           "a"
#define TYPE_UCHAR          "h"
#define TYPE_UBYTE          "h"
#define TYPE_SHORT          "s"
#define TYPE_USHORT         "t"
#define TYPE_INT            "i"
#define TYPE_UINT           "j"
#define TYPE_LONG           "x"
#define TYPE_ULONG          "y"
#define TYPE_LONG_LONG      "n"
#define TYPE_ULONG_LONG     "o"
#define TYPE_FLOAT          "f"
#define TYPE_DOUBLE         "d"
#define TYPE_LONG_DOUBLE    "e"

/// Prefix introducing an enumeration type, followed by its qualified name.
#define TYPE_ENUM           "Te"

/// Placeholder for a not-yet-resolved type, followed by its length-prefixed type id.
#define TYPE_UNRESOLVED     "Xu"

/// The type of the `null` literal (only reachable as an argument type).
#define TYPE_NULL           "Xz"


namespace k::model {

std::string mangler::mangle_short_name(const std::string& short_name) {
    // Operator names (__operator_XX_) are mangled as raw 2-letter Itanium codes
    if (k::op::is_operator_name(short_name)) {
        auto code = k::op::get_operator_mangling_code(short_name);
        if (!code.empty()) {
            if (k::op::is_cast_operator(short_name)) {
                // Cast operator: "__operator_cv_<encoded_type>" → "cv" + mangled type suffix
                // The encoded type suffix is everything after "__operator_cv_"
                constexpr size_t prefix_len = 14; // "__operator_cv_"
                std::string type_suffix = short_name.substr(prefix_len);
                return "cv" + std::to_string(type_suffix.size()) + type_suffix;
            }
            return code;
        }
    }
    //TODO Ensure name is a valid short name (e.g. no special chars, begin with letter or _ , etc)
    return std::to_string(short_name.size()) + short_name;
}

std::string mangler::mangle_fq_name(const name& name, bool with_k_prefix) {
    std::ostringstream mangled;
    if (with_k_prefix) {
        mangled << K_LANG_SYMBOL_PREFIX;
    }
    mangled << SYMBOL_QUALIFIED_PREFIX;
    for (const auto& part : name.parts()) {
        mangled << mangle_short_name(part);
    }
    mangled << SYMBOL_QUALIFIED_SUFFIX;
    return mangled.str();
}

std::string mangler::mangle_fq_name_with_raw_last_part(const name& name, const std::string& last_part, bool with_k_prefix) {
    std::ostringstream mangled;
    if (with_k_prefix) {
        mangled << K_LANG_SYMBOL_PREFIX;
    }
    mangled << SYMBOL_QUALIFIED_PREFIX;
    for (const auto& part : name.parts()) {
        mangled << mangle_short_name(part);
    }
    mangled << last_part;
    mangled << SYMBOL_QUALIFIED_SUFFIX;
    return mangled.str();
}

std::string mangler::mangle_template_args(const std::vector<template_argument>& args) const {
    std::ostringstream s;
    s << "I";
    for (const auto& arg : args) {
        if (arg.is_type() && arg.type_arg) {
            s << mangle_type(*arg.type_arg);
        } else if (arg.is_value() && arg.value_arg.has_value()) {
            // L<type><value>E encoding
            std::visit([&s](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, bool>) {
                    s << "Lb" << (v ? "1" : "0") << "E";
                } else if constexpr (std::is_same_v<T, char>) {
                    s << "Lc" << static_cast<int>(v) << "E";
                } else if constexpr (std::is_same_v<T, unsigned char>) {
                    s << "Lh" << static_cast<unsigned>(v) << "E";
                } else if constexpr (std::is_same_v<T, short>) {
                    s << "Ls";
                    if (v < 0) s << "n" << (-v); else s << v;
                    s << "E";
                } else if constexpr (std::is_same_v<T, unsigned short>) {
                    s << "Lt" << v << "E";
                } else if constexpr (std::is_same_v<T, int>) {
                    s << "Li";
                    if (v < 0) s << "n" << (-v); else s << v;
                    s << "E";
                } else if constexpr (std::is_same_v<T, unsigned int>) {
                    s << "Lj" << v << "E";
                } else if constexpr (std::is_same_v<T, long>) {
                    s << "Ll";
                    if (v < 0) s << "n" << (-v); else s << v;
                    s << "E";
                } else if constexpr (std::is_same_v<T, unsigned long>) {
                    s << "Lm" << v << "E";
                } else if constexpr (std::is_same_v<T, long long>) {
                    s << "Lx";
                    if (v < 0) s << "n" << (-v); else s << v;
                    s << "E";
                } else if constexpr (std::is_same_v<T, unsigned long long>) {
                    s << "Ly" << v << "E";
                } else if constexpr (std::is_same_v<T, float>) {
                    // Mangle float as hex bit pattern
                    uint32_t bits;
                    std::memcpy(&bits, &v, sizeof(bits));
                    s << "Lf" << std::hex << bits << std::dec << "E";
                } else if constexpr (std::is_same_v<T, double>) {
                    // Mangle double as hex bit pattern
                    uint64_t bits;
                    std::memcpy(&bits, &v, sizeof(bits));
                    s << "Ld" << std::hex << bits << std::dec << "E";
                } else {
                    // Fallback: treat as int 0
                    s << "Li0E";
                }
            }, *arg.value_arg);
        }
    }
    s << "E";
    return s.str();
}

std::string mangler::mangle_template_short_name(const std::string& base_name, const std::vector<template_argument>& args) const {
    return mangle_short_name(base_name) + mangle_template_args(args);
}

std::string mangler::mangle_fq_name_templated(
    const name& n,
    const std::string& tpl_inst_name,
    const std::string& tpl_base_name,
    const std::vector<template_argument>& tpl_args,
    bool with_k_prefix) const
{
    std::ostringstream mangled;
    if (with_k_prefix) {
        mangled << K_LANG_SYMBOL_PREFIX;
    }
    mangled << SYMBOL_QUALIFIED_PREFIX;
    for (const auto& part : n.parts()) {
        if (part == tpl_inst_name) {
            mangled << mangle_template_short_name(tpl_base_name, tpl_args);
        } else {
            mangled << mangle_short_name(part);
        }
    }
    mangled << SYMBOL_QUALIFIED_SUFFIX;
    return mangled.str();
}

std::string mangler::mangle_fq_name_with_raw_last_part_templated(
    const name& name, const std::string& last_part,
    const std::string& tpl_inst_name,
    const std::string& tpl_base_name,
    const std::vector<template_argument>& tpl_args,
    bool with_k_prefix) const
{
    std::ostringstream mangled;
    if (with_k_prefix) {
        mangled << K_LANG_SYMBOL_PREFIX;
    }
    mangled << SYMBOL_QUALIFIED_PREFIX;
    for (const auto& part : name.parts()) {
        if (part == tpl_inst_name) {
            mangled << mangle_template_short_name(tpl_base_name, tpl_args);
        } else {
            mangled << mangle_short_name(part);
        }
    }
    mangled << last_part;
    mangled << SYMBOL_QUALIFIED_SUFFIX;
    return mangled.str();
}

std::string mangler::mangle_fq_name_template_base_only(
    const name& n,
    const std::string& tpl_inst_name,
    const std::string& tpl_base_name,
    bool with_k_prefix) const
{
    std::ostringstream mangled;
    if (with_k_prefix) {
        mangled << K_LANG_SYMBOL_PREFIX;
    }
    mangled << SYMBOL_QUALIFIED_PREFIX;
    for (const auto& part : n.parts()) {
        if (part == tpl_inst_name) {
            mangled << mangle_short_name(tpl_base_name);
        } else {
            mangled << mangle_short_name(part);
        }
    }
    mangled << SYMBOL_QUALIFIED_SUFFIX;
    return mangled.str();
}

std::string mangler::mangle_fq_name_with_raw_last_part_template_base_only(
    const name& n,
    const std::string& last_part,
    const std::string& tpl_inst_name,
    const std::string& tpl_base_name,
    bool with_k_prefix) const
{
    std::ostringstream mangled;
    if (with_k_prefix) {
        mangled << K_LANG_SYMBOL_PREFIX;
    }
    mangled << SYMBOL_QUALIFIED_PREFIX;
    for (const auto& part : n.parts()) {
        if (part == tpl_inst_name) {
            mangled << mangle_short_name(tpl_base_name);
        } else {
            mangled << mangle_short_name(part);
        }
    }
    mangled << last_part;
    mangled << SYMBOL_QUALIFIED_SUFFIX;
    return mangled.str();
}

bool mangler::is_generic_template_instantiation(const aggregate& agg) const {
    if (!agg.has_tpl_args() || agg.get_tpl_base_name().empty()) {
        return false;
    }

    if (auto ast_decl = agg.get_ast_aggregate_decl()) {
        if (ast_decl->is_generic) {
            return true;
        }
    }

    for (auto current = agg.parent<element>(); current; current = current->parent<element>()) {
        auto* holder = dynamic_cast<const aggregate_holder*>(current.get());
        if (!holder) continue;

        auto origin = holder->get_aggregate(agg.get_tpl_base_name());
        if (!origin || !origin->is_template()) continue;
        return origin->is_generic();
    }

    return false;
}

bool mangler::is_generic_template_instantiation(const function& func) const {
    if (!func.has_tpl_args() || func.get_tpl_base_name().empty()) {
        return false;
    }

    if (auto ast_decl = func.get_ast_function_decl()) {
        if (ast_decl->is_generic) {
            return true;
        }
    }

    for (auto current = func.parent<element>(); current; current = current->parent<element>()) {
        auto* holder = dynamic_cast<const function_holder*>(current.get());
        if (!holder) continue;

        auto origin = holder->get_function(func.get_tpl_base_name());
        if (!origin || !origin->is_template()) continue;
        return origin->is_generic();
    }

    return false;
}


std::string mangler::mangle_namespace(const name& ns_name) {
    return mangle_fq_name(ns_name, true);
}

std::string mangler::mangle_global_variable(const name& ns_name) {
    return mangle_fq_name(ns_name, true);
}

std::string mangler::mangle_structure(const name& ns_name) {
    return mangle_fq_name(ns_name, true);
}

std::string mangler::mangle_structure(const aggregate& agg) const {
    if (agg.has_tpl_args()) {
        if (is_generic_template_instantiation(agg)) {
            return mangle_fq_name_template_base_only(
                agg.get_name(), agg.get_short_name(), agg.get_tpl_base_name(), true);
        }
        return mangle_fq_name_templated(
            agg.get_name(), agg.get_short_name(),
            agg.get_tpl_base_name(), agg.get_tpl_args(), true);
    }
    return mangle_structure(agg.get_name());
}

std::string mangler::mangle_union(const union_type_def& un) const {
    if (un.has_tpl_args()) {
        return mangle_fq_name_templated(
            un.get_name(), un.get_short_name(),
            un.get_tpl_base_name(), un.get_tpl_args(), true);
    }
    return mangle_structure(un.get_name());
}

std::string mangler::mangle_function(const function& func) const {
    auto name = func.get_name();
    if (!name.has_root_prefix()) {
        // Must be fully qualified name
        return "";
    }

    std::ostringstream mangled;
    mangled << K_LANG_SYMBOL_PREFIX SYMBOL_TYPE_FUNCTION;

    if (func.is_member() && ! func.is_static()) {
        mangled << SYMBOL_MEMBER;
        if (func.is_const_member()) {
            mangled << SYMBOL_MODIFIER_CONST; // MK = const member function
        }
    }

    // Check if owner aggregate is a template instantiation
    auto owner = func.get_owner();
    if (owner && owner->has_tpl_args()) {
        if (is_generic_template_instantiation(*owner)) {
            mangled << mangle_fq_name_template_base_only(
                name, owner->get_short_name(), owner->get_tpl_base_name(), false);
        } else {
            mangled << mangle_fq_name_templated(name, owner->get_short_name(),
                owner->get_tpl_base_name(), owner->get_tpl_args(), false);
        }
    } else if (func.has_tpl_args()) {
        // Free template function instantiation — encode the function name itself with I…E
        if (is_generic_template_instantiation(func)) {
            mangled << mangle_fq_name_template_base_only(
                name, func.get_short_name(), func.get_tpl_base_name(), false);
        } else {
            mangled << mangle_fq_name_templated(name, func.get_short_name(),
                func.get_tpl_base_name(), func.get_tpl_args(), false);
        }
    } else {
        mangled << mangle_fq_name(name, false);
    }

    if (func.get_parameter_size() == 0) {
        mangled << TYPE_VOID; // void parameter list
    } else {
        for(size_t i = 0; i < func.get_parameter_size(); ++i) {
            auto param = func.get_parameter(i);
            if (param->is_const()) {
                mangled << SYMBOL_MODIFIER_CONST;
            }
            mangled << mangle_type(*param->get_type());
        }
    }

    return mangled.str();
}

std::string mangler::mangle_constructor(const constructor& ctor) const {
    auto name = ctor.get_name();
    if (!name.has_root_prefix()) {
        // Must be fully qualified name
        return "";
    }
    if (name.size()<2 && name.parts().back() != *(name.parts().end()-2)) {
        // Constructor name must be at least 2 parts : qualified struct name + constructor name
        // And the constructor name must be the same as the struct name
        // TODO throw an exception : invalid constructor name
        return "";
    }

    std::ostringstream mangled;
    mangled << K_LANG_SYMBOL_PREFIX SYMBOL_TYPE_FUNCTION SYMBOL_MEMBER;

    auto owner = ctor.get_owner();
    if (owner && owner->has_tpl_args()) {
        if (is_generic_template_instantiation(*owner)) {
            mangled << mangle_fq_name_with_raw_last_part_template_base_only(
                name.without_back(), SYMBOL_CONSTRUCTOR_C1_NAME,
                owner->get_short_name(), owner->get_tpl_base_name(), false);
        } else {
            mangled << mangle_fq_name_with_raw_last_part_templated(
                name.without_back(), SYMBOL_CONSTRUCTOR_C1_NAME,
                owner->get_short_name(), owner->get_tpl_base_name(), owner->get_tpl_args(), false);
        }
    } else {
        mangled << mangle_fq_name_with_raw_last_part(name.without_back(), SYMBOL_CONSTRUCTOR_C1_NAME, false);
    }

    if (ctor.get_parameter_size() == 0) {
        mangled << TYPE_VOID; // void parameter list
    } else {
        for(size_t i = 0; i < ctor.get_parameter_size(); ++i) {
            auto param = ctor.get_parameter(i);
            if (param->is_const()) {
                mangled << SYMBOL_MODIFIER_CONST;
            }
            mangled << mangle_type(*param->get_type());
        }
    }

    return mangled.str();
}

std::string mangler::mangle_destructor(const destructor& dtor) const {
    auto name = dtor.get_name();
    if (!name.has_root_prefix()) {
        // Must be fully qualified name
        return "";
    }
    if (name.size() < 2) {
        // Destructor name must be at least 2 parts : qualified struct name + destructor name
        // TODO throw an exception : invalid destructor name
        return "";
    }

    std::ostringstream mangled;
    mangled << K_LANG_SYMBOL_PREFIX SYMBOL_TYPE_FUNCTION SYMBOL_MEMBER;

    // Use the parent structure name (without the last "~name" part), with D1 suffix
    auto owner = dtor.get_owner();
    if (owner && owner->has_tpl_args()) {
        if (is_generic_template_instantiation(*owner)) {
            mangled << mangle_fq_name_with_raw_last_part_template_base_only(
                name.without_back(), SYMBOL_DESTRUCTOR_D1_NAME,
                owner->get_short_name(), owner->get_tpl_base_name(), false);
        } else {
            mangled << mangle_fq_name_with_raw_last_part_templated(
                name.without_back(), SYMBOL_DESTRUCTOR_D1_NAME,
                owner->get_short_name(), owner->get_tpl_base_name(), owner->get_tpl_args(), false);
        }
    } else {
        mangled << mangle_fq_name_with_raw_last_part(name.without_back(), SYMBOL_DESTRUCTOR_D1_NAME, false);
    }

    // Destructor has no parameters, encode as void
    mangled << TYPE_VOID;

    return mangled.str();
}


std::string mangler::mangle_static_constructor(const static_constructor& sctor) const {
    auto name = sctor.get_name();
    if (!name.has_root_prefix()) {
        return "";
    }
    if (name.size() < 2) {
        return "";
    }

    std::ostringstream mangled;
    // Static constructor: 'F' (no SYMBOL_MEMBER since it is static), C suffix
    mangled << K_LANG_SYMBOL_PREFIX SYMBOL_TYPE_FUNCTION;
    auto sctor_owner = sctor.get_owner();
    if (sctor_owner && sctor_owner->has_tpl_args()) {
        if (is_generic_template_instantiation(*sctor_owner)) {
            mangled << mangle_fq_name_with_raw_last_part_template_base_only(
                name.without_back(), SYMBOL_STATIC_CONSTRUCTOR_NAME,
                sctor_owner->get_short_name(), sctor_owner->get_tpl_base_name(), false);
        } else {
            mangled << mangle_fq_name_with_raw_last_part_templated(
                name.without_back(), SYMBOL_STATIC_CONSTRUCTOR_NAME,
                sctor_owner->get_short_name(), sctor_owner->get_tpl_base_name(), sctor_owner->get_tpl_args(), false);
        }
    } else {
        mangled << mangle_fq_name_with_raw_last_part(name.without_back(), SYMBOL_STATIC_CONSTRUCTOR_NAME, false);
    }
    // No parameters: encode as void
    mangled << TYPE_VOID;

    return mangled.str();
}

std::string mangler::mangle_static_destructor(const static_destructor& sdtor) const {
    auto name = sdtor.get_name();
    if (!name.has_root_prefix()) {
        return "";
    }
    if (name.size() < 2) {
        return "";
    }

    std::ostringstream mangled;
    // Static destructor: 'F' (no SYMBOL_MEMBER since it is static), D suffix
    mangled << K_LANG_SYMBOL_PREFIX SYMBOL_TYPE_FUNCTION;
    auto sdtor_owner = sdtor.get_owner();
    if (sdtor_owner && sdtor_owner->has_tpl_args()) {
        if (is_generic_template_instantiation(*sdtor_owner)) {
            mangled << mangle_fq_name_with_raw_last_part_template_base_only(
                name.without_back(), SYMBOL_STATIC_DESTRUCTOR_NAME,
                sdtor_owner->get_short_name(), sdtor_owner->get_tpl_base_name(), false);
        } else {
            mangled << mangle_fq_name_with_raw_last_part_templated(
                name.without_back(), SYMBOL_STATIC_DESTRUCTOR_NAME,
                sdtor_owner->get_short_name(), sdtor_owner->get_tpl_base_name(), sdtor_owner->get_tpl_args(), false);
        }
    } else {
        mangled << mangle_fq_name_with_raw_last_part(name.without_back(), SYMBOL_STATIC_DESTRUCTOR_NAME, false);
    }
    // No parameters: encode as void
    mangled << TYPE_VOID;

    return mangled.str();
}



std::string mangler::mangle_type(const type& ty) const {
    if (auto const_ty = dynamic_cast<const const_type*>(&ty)) {
        return SYMBOL_MODIFIER_CONST + mangle_type(*const_ty->get_inner_type());
    } else if (auto prim = dynamic_cast<const primitive_type*>(&ty)) {
        switch(prim->get_type()) {
            case primitive_type::BOOL: return TYPE_BOOL;
            case primitive_type::CHAR: return TYPE_CHAR;
            case primitive_type::BYTE: return TYPE_BYTE;
            case primitive_type::UNSIGNED_BYTE: return TYPE_UBYTE;
            case primitive_type::SHORT: return TYPE_SHORT;
            case primitive_type::UNSIGNED_SHORT: return TYPE_USHORT;
            case primitive_type::INT: return TYPE_INT;
            case primitive_type::UNSIGNED_INT: return TYPE_UINT;
            case primitive_type::LONG: return TYPE_LONG;
            case primitive_type::UNSIGNED_LONG: return TYPE_ULONG;
            case primitive_type::LONG_LONG: return TYPE_LONG_LONG;
            case primitive_type::UNSIGNED_LONG_LONG: return TYPE_ULONG_LONG;
            case primitive_type::FLOAT: return TYPE_FLOAT;
            case primitive_type::DOUBLE: return TYPE_DOUBLE;
        }
        throw_unmanglable_type(ty, "unknown primitive type kind");
    } else if (auto enum_ty = dynamic_cast<const enum_type*>(&ty)) {
        return TYPE_ENUM + mangle_enum(*enum_ty);
    } else if (dynamic_cast<const null_type*>(&ty)) {
        return TYPE_NULL;
    } else if (auto unres = dynamic_cast<const unresolved_type*>(&ty)) {
        // Mangled names are computed once from the raw declaration and recomputed by
        // signature_resolver once the signature is fully resolved. Until then a type may
        // still be unresolved: emit a deterministic, non-empty placeholder rather than an
        // empty string, which would make distinct overloads collide. A placeholder that
        // survives to code generation is a compiler bug and is rejected by
        // compiler::verify_mangled_names().
        const std::string id = unres->type_id();
        return TYPE_UNRESOLVED + std::to_string(id.size()) + id;
    } else if (auto unres_fn = dynamic_cast<const unresolved_function_ref_type*>(&ty)) {
        // Function-reference types (`*(int)`, `Counter::*(int)`) are deliberately left
        // unresolved by signature_resolver and only resolved by type_reference_resolver.
        // Emit a provisional but structurally distinct placeholder in the meantime.
        std::ostringstream s;
        s << TYPE_UNRESOLVED << static_cast<int>(unres_fn->get_ref_kind())
          << SYMBOL_MODIFIER_FN_REF;
        if (!unres_fn->owner_name().parts().empty()) {
            s << SYMBOL_MODIFIER_MEM_FN << mangle_fq_name(unres_fn->owner_name(), false);
        }
        for (const auto& p : unres_fn->parameter_types()) {
            s << (p ? mangle_type(*p) : std::string{TYPE_VOID});
        }
        s << SYMBOL_QUALIFIED_SUFFIX;
        return s.str();
    } else if (auto ref_ty = dynamic_cast<const reference_type*>(&ty)) {
        return SYMBOL_MODIFIER_REF + mangle_type(*ref_ty->get_referenced_type());
    } else if (auto ptr_ty = dynamic_cast<const pointer_type*>(&ty)) {
        return SYMBOL_MODIFIER_PTR + mangle_type(*ptr_ty->get_pointed_type());
    } else if (auto link_ty = dynamic_cast<const link_type*>(&ty)) {
        return SYMBOL_MODIFIER_LINK + mangle_type(*link_ty->get_linked_type());
    } else if (auto view_ty = dynamic_cast<const view_type*>(&ty)) {
        return SYMBOL_MODIFIER_VIEW + mangle_type(*view_ty->get_viewed_type());
    } else if (auto own_ty = dynamic_cast<const owner_type*>(&ty)) {
        return SYMBOL_MODIFIER_OWNER + mangle_type(*own_ty->get_owned_type());
    } else if (auto drain_ty = dynamic_cast<const drain_type*>(&ty)) {
        return SYMBOL_MODIFIER_DRAIN + mangle_type(*drain_ty->get_drained_type());
    } else if (auto arr_ty = dynamic_cast<const array_type*>(&ty)) {
        std::ostringstream s;
        s << SYMBOL_MODIFIER_ARRAY;
        if (auto sized = dynamic_cast<const sized_array_type*>(&ty)) {
            s << sized->get_size() << "_";
        }
        s << mangle_type(*arr_ty->get_subtype());
        return s.str();
    } else if (auto mem_fn_ty = dynamic_cast<const member_function_reference_type*>(&ty)) {
        std::ostringstream s;
        // ref_kind modifier
        switch (mem_fn_ty->get_ref_kind()) {
            case function_reference_type::ref_kind::pointer: s << SYMBOL_MODIFIER_PTR;    break;
            case function_reference_type::ref_kind::view:    s << SYMBOL_MODIFIER_VIEW;   break;
            case function_reference_type::ref_kind::link:    s << SYMBOL_MODIFIER_LINK;   break;
        }
        s << SYMBOL_MODIFIER_FN_REF SYMBOL_MODIFIER_MEM_FN;
        // owner struct
        if (mem_fn_ty->get_member_of()) {
            auto mem_owner = mem_fn_ty->get_member_of();
            if (mem_owner->has_tpl_args()) {
                s << mangle_structure(*mem_owner);
            } else {
                s << mangle_structure(mem_owner->get_name());
            }
        }
        // parameters
        if (mem_fn_ty->get_parameter_types().empty()) {
            s << TYPE_VOID;
        } else {
            for (const auto& p : mem_fn_ty->get_parameter_types()) {
                s << mangle_type(*p);
            }
        }
        s << SYMBOL_QUALIFIED_SUFFIX;
        return s.str();
    } else if (auto fn_ty = dynamic_cast<const function_reference_type*>(&ty)) {
        std::ostringstream s;
        switch (fn_ty->get_ref_kind()) {
            case function_reference_type::ref_kind::pointer: s << SYMBOL_MODIFIER_PTR;    break;
            case function_reference_type::ref_kind::view:    s << SYMBOL_MODIFIER_VIEW;   break;
            case function_reference_type::ref_kind::link:    s << SYMBOL_MODIFIER_LINK;   break;
        }
        s << SYMBOL_MODIFIER_FN_REF;
        if (fn_ty->get_parameter_types().empty()) {
            s << TYPE_VOID;
        } else {
            for (const auto& p : fn_ty->get_parameter_types()) {
                s << mangle_type(*p);
            }
        }
        s << SYMBOL_QUALIFIED_SUFFIX;
        return s.str();
    } else if (auto struct_ty = dynamic_cast<const struct_type*>(&ty)) {
        auto st = struct_ty->get_struct();
        if (!st) {
            // Union type (struct_type with no owning aggregate): mangle by name.
            // struct_type::name() holds the fully-qualified name (without the leading
            // "::") for every named union, so split it back into components and emit the
            // regular 'N<len><part>...E' qualified form. Emitting the raw short name here
            // would let two same-named unions from different namespaces collide.
            return mangle_qualified_type_name(struct_ty->name());
        }
        if (st->has_tpl_args()) {
            return mangle_structure(*st);
        }
        return mangle_structure(st->get_name());
    }
    throw_unmanglable_type(ty, "unsupported type kind");
}

std::string mangler::mangle_enum(const enum_type& ty) const {
    if (auto en = ty.get_enumeration()) {
        const auto& n = en->get_name();
        if (!n.parts().empty()) {
            return mangle_fq_name(n, true);
        }
        return mangle_qualified_type_name(en->get_fq_name());
    }
    throw_unmanglable_type(ty, "enumeration type with no backing enumeration");
}

std::string mangler::mangle_qualified_type_name(const std::string& fq_name) {
    // Accept both "::a::b::C" and "a::b::C"; produce "_KN1a1b1CE".
    std::string_view sv{fq_name};
    while (sv.size() >= 2 && sv[0] == ':' && sv[1] == ':') {
        sv.remove_prefix(2);
    }
    if (sv.empty()) {
        throw_unmanglable_type_name(fq_name);
    }
    std::ostringstream mangled;
    mangled << K_LANG_SYMBOL_PREFIX SYMBOL_QUALIFIED_PREFIX;
    size_t pos = 0;
    while (pos <= sv.size()) {
        const size_t next = sv.find("::", pos);
        const std::string_view part = (next == std::string_view::npos)
            ? sv.substr(pos)
            : sv.substr(pos, next - pos);
        if (!part.empty()) {
            mangled << part.size() << part;
        }
        if (next == std::string_view::npos) break;
        pos = next + 2;
    }
    mangled << SYMBOL_QUALIFIED_SUFFIX;
    return mangled.str();
}

[[noreturn]] void mangler::throw_unmanglable_type(const type& ty, const std::string& reason) {
    std::string desc;
    try {
        desc = ty.to_string();
    } catch (...) {
        desc = "<undisplayable>";
    }
    throw log::compiler_error(log::diagnostic{
        static_cast<unsigned int>(diag::codegen_diag::INTERNAL_ERR_MANGLE_TYPE),
        log::diagnostic::severity::fatal,
        "Internal error: cannot mangle type '{}' ({}); every resolved type must have a "
        "unique non-empty mangled encoding",
        {desc, reason}
    });
}

[[noreturn]] void mangler::throw_unmanglable_type_name(const std::string& fq_name) {
    throw log::compiler_error(log::diagnostic{
        static_cast<unsigned int>(diag::codegen_diag::INTERNAL_ERR_MANGLE_TYPE),
        log::diagnostic::severity::fatal,
        "Internal error: cannot mangle an unnamed type (raw name '{}'); every type "
        "reaching the mangler must have a qualified name",
        {fq_name}
    });
}

std::string mangler::mangle_constructor_c2(const constructor& ctor) const {
    auto name = ctor.get_name();
    if (!name.has_root_prefix()) return "";
    if (name.size() < 2) return "";

    std::ostringstream mangled;
    mangled << K_LANG_SYMBOL_PREFIX SYMBOL_TYPE_FUNCTION SYMBOL_MEMBER;
    auto owner_c2 = ctor.get_owner();
    if (owner_c2 && owner_c2->has_tpl_args()) {
        if (is_generic_template_instantiation(*owner_c2)) {
            mangled << mangle_fq_name_with_raw_last_part_template_base_only(
                name.without_back(), SYMBOL_CONSTRUCTOR_C2_NAME,
                owner_c2->get_short_name(), owner_c2->get_tpl_base_name(), false);
        } else {
            mangled << mangle_fq_name_with_raw_last_part_templated(
                name.without_back(), SYMBOL_CONSTRUCTOR_C2_NAME,
                owner_c2->get_short_name(), owner_c2->get_tpl_base_name(), owner_c2->get_tpl_args(), false);
        }
    } else {
        mangled << mangle_fq_name_with_raw_last_part(name.without_back(), SYMBOL_CONSTRUCTOR_C2_NAME, false);
    }

    if (ctor.get_parameter_size() == 0) {
        mangled << TYPE_VOID;
    } else {
        for (size_t i = 0; i < ctor.get_parameter_size(); ++i) {
            auto param = ctor.get_parameter(i);
            if (param->is_const()) mangled << SYMBOL_MODIFIER_CONST;
            mangled << mangle_type(*param->get_type());
        }
    }
    return mangled.str();
}

std::string mangler::mangle_destructor_d2(const destructor& dtor) const {
    auto name = dtor.get_name();
    if (!name.has_root_prefix()) return "";
    if (name.size() < 2) return "";

    std::ostringstream mangled;
    mangled << K_LANG_SYMBOL_PREFIX SYMBOL_TYPE_FUNCTION SYMBOL_MEMBER;
    auto owner_d2 = dtor.get_owner();
    if (owner_d2 && owner_d2->has_tpl_args()) {
        if (is_generic_template_instantiation(*owner_d2)) {
            mangled << mangle_fq_name_with_raw_last_part_template_base_only(
                name.without_back(), SYMBOL_DESTRUCTOR_D2_NAME,
                owner_d2->get_short_name(), owner_d2->get_tpl_base_name(), false);
        } else {
            mangled << mangle_fq_name_with_raw_last_part_templated(
                name.without_back(), SYMBOL_DESTRUCTOR_D2_NAME,
                owner_d2->get_short_name(), owner_d2->get_tpl_base_name(), owner_d2->get_tpl_args(), false);
        }
    } else {
        mangled << mangle_fq_name_with_raw_last_part(name.without_back(), SYMBOL_DESTRUCTOR_D2_NAME, false);
    }
    mangled << TYPE_VOID;
    return mangled.str();
}

std::string mangler::mangle_vtable(const name& class_name) {
    std::ostringstream mangled;
    mangled << K_LANG_SYMBOL_PREFIX << SYMBOL_VTABLE_PREFIX;
    mangled << mangle_fq_name(class_name, false);
    return mangled.str();
}

// RTTI global variable prefix: _KTRI
#define SYMBOL_RTTI_PREFIX "TRI"

// RTTI function descriptor prefix: _KTRF
#define SYMBOL_RTTI_FUNCTION_PREFIX "TRF"

// RTTI constructor descriptor prefix: _KTRC
#define SYMBOL_RTTI_CONSTRUCTOR_PREFIX "TRC"

// RTTI unit descriptor prefix: _KTRU
#define SYMBOL_RTTI_UNIT_PREFIX "TRU"

std::string mangler::mangle_rtti(const name& class_name) {
    std::ostringstream mangled;
    mangled << K_LANG_SYMBOL_PREFIX << SYMBOL_RTTI_PREFIX;
    mangled << mangle_fq_name(class_name, false);
    return mangled.str();
}

std::string mangler::mangle_rtti_function(const name& func_name) {
    std::ostringstream mangled;
    mangled << K_LANG_SYMBOL_PREFIX << SYMBOL_RTTI_FUNCTION_PREFIX;
    mangled << mangle_fq_name(func_name, false);
    return mangled.str();
}

std::string mangler::mangle_rtti_constructor(const name& ctor_name) {
    std::ostringstream mangled;
    mangled << K_LANG_SYMBOL_PREFIX << SYMBOL_RTTI_CONSTRUCTOR_PREFIX;
    mangled << mangle_fq_name(ctor_name, false);
    return mangled.str();
}

std::string mangler::mangle_rtti_unit(const name& unit_name) {
    std::ostringstream mangled;
    mangled << K_LANG_SYMBOL_PREFIX << SYMBOL_RTTI_UNIT_PREFIX;
    mangled << mangle_fq_name(unit_name, false);
    return mangled.str();
}

std::string mangler::mangle_virtual_dispatch(const function& func) const {
    auto name = func.get_name();
    if (!name.has_root_prefix()) return "";

    std::ostringstream mangled;
    // Virtual dispatch thunk: _KFMvN<name>E<params>
    mangled << K_LANG_SYMBOL_PREFIX SYMBOL_TYPE_FUNCTION SYMBOL_VIRTUAL_DISPATCH;
    if (func.is_const_member()) {
        mangled << SYMBOL_MODIFIER_CONST;
    }

    auto owner = func.get_owner();
    if (owner && owner->has_tpl_args()) {
        if (is_generic_template_instantiation(*owner)) {
            mangled << mangle_fq_name_template_base_only(
                name, owner->get_short_name(), owner->get_tpl_base_name(), false);
        } else {
            mangled << mangle_fq_name_templated(name, owner->get_short_name(),
                owner->get_tpl_base_name(), owner->get_tpl_args(), false);
        }
    } else {
        mangled << mangle_fq_name(name, false);
    }

    if (func.get_parameter_size() == 0) {
        mangled << TYPE_VOID;
    } else {
        for (size_t i = 0; i < func.get_parameter_size(); ++i) {
            auto param = func.get_parameter(i);
            if (param->is_const()) mangled << SYMBOL_MODIFIER_CONST;
            mangled << mangle_type(*param->get_type());
        }
    }
    return mangled.str();
}


} // k::model
