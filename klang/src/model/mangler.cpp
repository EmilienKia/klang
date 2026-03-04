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
#include "type.hpp"

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
// 'L' for link (~) : mutable strong (non-null) indirection
#define SYMBOL_MODIFIER_LINK       "L"
// 'Q' for pinned (^) : immutable nullable indirection
#define SYMBOL_MODIFIER_PINNED     "Q"

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
#define TYPE_UCHAR          "h"
#define TYPE_SHORT          "s"
#define TYPE_USHORT         "t"
#define TYPE_INT            "i"
#define TYPE_UINT           "j"
#define TYPE_LONG           "x"
#define TYPE_ULONG          "y"
#define TYPE_FLOAT          "f"
#define TYPE_DOUBLE         "d"
#define TYPE_LONG_DOUBLE    "e"


namespace k::model {

std::string mangler::mangle_short_name(const std::string& short_name) {
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


std::string mangler::mangle_namespace(const name& ns_name) {
    return mangle_fq_name(ns_name, true);
}

std::string mangler::mangle_global_variable(const name& ns_name) {
    return mangle_fq_name(ns_name, true);
}

std::string mangler::mangle_structure(const name& ns_name) {
    return mangle_fq_name(ns_name, true);
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
    mangled << mangle_fq_name(name, false);

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

    mangled << mangle_fq_name_with_raw_last_part(name.without_back(), SYMBOL_CONSTRUCTOR_C1_NAME, false);

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
    mangled << mangle_fq_name_with_raw_last_part(name.without_back(), SYMBOL_DESTRUCTOR_D1_NAME, false);

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
    mangled << mangle_fq_name_with_raw_last_part(name.without_back(), SYMBOL_STATIC_CONSTRUCTOR_NAME, false);
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
    mangled << mangle_fq_name_with_raw_last_part(name.without_back(), SYMBOL_STATIC_DESTRUCTOR_NAME, false);
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
            case primitive_type::UNSIGNED_CHAR: return TYPE_UCHAR;
            case primitive_type::SHORT: return TYPE_SHORT;
            case primitive_type::UNSIGNED_SHORT: return TYPE_USHORT;
            case primitive_type::INT: return TYPE_INT;
            case primitive_type::UNSIGNED_INT: return TYPE_UINT;
            case primitive_type::LONG: return TYPE_LONG;
            case primitive_type::UNSIGNED_LONG: return TYPE_ULONG;
            case primitive_type::FLOAT: return TYPE_FLOAT;
            case primitive_type::DOUBLE: return TYPE_DOUBLE;
            default:
                // TODO throw an exception : unsupported primitive ty
                return "";
        }
    } else if (auto ref_ty = dynamic_cast<const reference_type*>(&ty)) {
        return SYMBOL_MODIFIER_REF + mangle_type(*ref_ty->get_referenced_type());
    } else if (auto ptr_ty = dynamic_cast<const pointer_type*>(&ty)) {
        return SYMBOL_MODIFIER_PTR + mangle_type(*ptr_ty->get_pointed_type());
    } else if (auto link_ty = dynamic_cast<const link_type*>(&ty)) {
        return SYMBOL_MODIFIER_LINK + mangle_type(*link_ty->get_linked_type());
    } else if (auto pin_ty = dynamic_cast<const pinned_type*>(&ty)) {
        return SYMBOL_MODIFIER_PINNED + mangle_type(*pin_ty->get_pinned_type());
    } else if (auto struct_ty = dynamic_cast<const struct_type*>(&ty)) {
        auto st = struct_ty->get_struct();
        if (!st) {
            // TODO throw exception : struct type not resolved
            return "";
        }
        return mangle_structure(st->get_name());
    } else {
        // TODO throw exception : unsupported type
        return "";
    }

}

std::string mangler::mangle_constructor_c2(const constructor& ctor) const {
    auto name = ctor.get_name();
    if (!name.has_root_prefix()) return "";
    if (name.size() < 2) return "";

    std::ostringstream mangled;
    mangled << K_LANG_SYMBOL_PREFIX SYMBOL_TYPE_FUNCTION SYMBOL_MEMBER;
    mangled << mangle_fq_name_with_raw_last_part(name.without_back(), SYMBOL_CONSTRUCTOR_C2_NAME, false);

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
    mangled << mangle_fq_name_with_raw_last_part(name.without_back(), SYMBOL_DESTRUCTOR_D2_NAME, false);
    mangled << TYPE_VOID;
    return mangled.str();
}

std::string mangler::mangle_vtable(const name& class_name) {
    std::ostringstream mangled;
    mangled << K_LANG_SYMBOL_PREFIX << SYMBOL_VTABLE_PREFIX;
    mangled << mangle_fq_name(class_name, false);
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
    mangled << mangle_fq_name(name, false);

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
