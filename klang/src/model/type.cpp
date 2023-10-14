/*
 * K Language compiler
 *
 * Copyright 2023 Emilien Kia
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

#include "type.hpp"
#include "model.hpp"

namespace k::model {

//
// Base type
//

bool type::is_resolved() const
{
    return false;
}

bool type::is_primitive() const
{
    return false;
}

//
// Unresolved type
//

std::shared_ptr<type> unresolved_type::from_string(const std::string& type_name)
{
    auto prim = primitive_type::from_string(type_name);
    if(prim) {
        return prim;
    } else {
        return std::shared_ptr<type>{new unresolved_type(name(type_name))};
    }
}

std::shared_ptr<type> unresolved_type::from_identifier(const name& type_id)
{
    return std::shared_ptr<type>{new unresolved_type(type_id)};
}

std::shared_ptr<type> unresolved_type::from_type_specifier(const k::parse::ast::type_specifier& type_spec)
{
    if(auto ident = dynamic_cast<const k::parse::ast::identified_type_specifier*>(&type_spec)) {
        return std::shared_ptr<type>{new unresolved_type(ident->name.to_name())};
    } else if(auto kw = dynamic_cast<const k::parse::ast::keyword_type_specifier*>(&type_spec)) {
        return primitive_type::from_keyword(kw->keyword);
    } else {
        return {};
    }
}

//
// Resolved type
//

bool resolved_type::is_resolved() const
{
    return true;
}


//
// Primitive type
//

std::map<primitive_type::PRIMITIVE_TYPE, std::shared_ptr<primitive_type>> primitive_type::_predef_types{
        {primitive_type::BOOL, primitive_type::make_shared(primitive_type::BOOL, true, false, 1)},
        {primitive_type::BYTE, primitive_type::make_shared(primitive_type::BYTE, true, false, 1*8)},
        {primitive_type::CHAR, primitive_type::make_shared(primitive_type::CHAR, false, false, 1*8)},
        {primitive_type::SHORT, primitive_type::make_shared(primitive_type::SHORT, false, false, 2*8)},
        {primitive_type::UNSIGNED_SHORT, primitive_type::make_shared(primitive_type::UNSIGNED_SHORT, true, false, 2*8)},
        {primitive_type::INT, primitive_type::make_shared(primitive_type::INT, false, false, 4*8)},
        {primitive_type::UNSIGNED_INT, primitive_type::make_shared(primitive_type::UNSIGNED_INT, true, false, 4*8)},
        {primitive_type::LONG, primitive_type::make_shared(primitive_type::LONG, false, false, 8*8)},
        {primitive_type::UNSIGNED_LONG, primitive_type::make_shared(primitive_type::UNSIGNED_LONG, true, false, 8*8)},
        {primitive_type::FLOAT, primitive_type::make_shared(primitive_type::FLOAT, false, true, 4*8)},
        {primitive_type::DOUBLE, primitive_type::make_shared(primitive_type::DOUBLE, false, true, 8*8)},
};

std::shared_ptr<primitive_type> primitive_type::make_shared(primitive_type::PRIMITIVE_TYPE type, bool is_unsigned, bool is_float, size_t size) {
    return std::shared_ptr<primitive_type>(new primitive_type(type, is_unsigned, is_float, size));
}

bool primitive_type::is_primitive() const
{
    return true;
}

std::shared_ptr<primitive_type> primitive_type::from_type(PRIMITIVE_TYPE type){
    return _predef_types[type];
}

std::shared_ptr<type> primitive_type::from_string(const std::string& type_name) {
    static std::map<std::string, primitive_type::PRIMITIVE_TYPE> type_map {
            {"bool", BOOL},
            {"byte", BYTE},
            {"char", CHAR},
            {"unsigned char", UNSIGNED_CHAR},
            {"short", SHORT},
            {"unsigned short", UNSIGNED_SHORT},
            {"int", INT},
            {"unsigned int", UNSIGNED_INT},
            {"long", LONG},
            {"unsigned long", UNSIGNED_LONG},
            // TODO Add (unsigned) long long
            {"float", FLOAT},
            {"double", DOUBLE}
    };
    auto it = type_map.find(type_name);
    if(it!=type_map.end()) {
        return _predef_types[it->second];
    } else {
        // TODO throw exception
        // Error : predefined type is not recognized or supported.
        std::cerr << "Error: predefined type '" << type_name << "' is not recognized (or not supported yet)." << std::endl;
        return {};
    }
}

std::shared_ptr<type> primitive_type::from_keyword(const lex::keyword& kw, bool is_unsigned) {
    return from_string(is_unsigned ? ("unsigned " + kw.content) : kw.content);
}

const std::string& primitive_type::to_string()const {
    static std::map<primitive_type::PRIMITIVE_TYPE, std::string> type_names {
            {BOOL, "bool"},
            {BYTE, "byte"},
            {CHAR,"char"},
            {SHORT, "short"},
            {UNSIGNED_SHORT, "unsigned short"},
            {INT, "int"},
            {UNSIGNED_INT, "unsigned int"},
            {LONG, "long"},
            {UNSIGNED_LONG, "unsigned long"},
            // TODO Add (unsigned) long long
            {FLOAT, "float"},
            {DOUBLE, "double"}
    };
    return type_names[_type];
}


} // k::model
