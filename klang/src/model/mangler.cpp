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

    if (func.is_member()) {
        // TODO test if static methof
        mangled << SYMBOL_MEMBER;
    }
    mangled << mangle_fq_name(name, false);

    if (func.get_parameter_size() == 0) {
        mangled << TYPE_VOID; // void parameter list
    } else {
        for(size_t i = 0; i < func.get_parameter_size(); ++i) {
            auto param = func.get_parameter(i);
            mangled << mangle_type(*param->get_type());
        }
    }

    return mangled.str();
}

std::string mangler::mangle_type(const type& ty) const {
    if (auto prim = dynamic_cast<const primitive_type*>(&ty)) {
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



} // k::model
