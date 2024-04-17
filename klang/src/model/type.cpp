/*
 * K Language compiler
 *
 * Copyright 2023-2024 Emilien Kia
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
#include "../common/tools.hpp"

namespace k::model {

//
// Base type
//

type::type(std::shared_ptr<type> subtype):
        subtype(subtype)
{}

std::shared_ptr<type> type::get_subtype() const
{
    return subtype.lock();
}

bool type::is_primitive() const
{
    return subtype.use_count()!=0;
}

bool type::is_resolved() const
{
    return false;
}

std::shared_ptr<reference_type> type::get_reference()
{
    if(!reference) {
        reference = std::shared_ptr<reference_type>(new reference_type(shared_from_this()));
    }
    return reference;
}

std::shared_ptr<pointer_type> type::get_pointer()
{
    if(!pointer) {
        pointer = std::shared_ptr<pointer_type>(new pointer_type(shared_from_this()));
    }
    return pointer;
}

std::shared_ptr<array_type> type::get_array()
{
    if(!array) {
        array = std::shared_ptr<array_type>(new array_type(shared_from_this()));
    }
    return array;
}

std::shared_ptr<sized_array_type> type::get_array(unsigned long size)
{
    return get_array()->with_size(size);
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
    } else if(auto ptr = dynamic_cast<const k::parse::ast::pointer_type_specifier*>(&type_spec)) {
        auto subtype = unresolved_type::from_type_specifier(*ptr->subtype);
        if(ptr->pointer_type==lex::operator_::STAR) {
            return subtype->get_pointer();
        } else if(ptr->pointer_type==lex::operator_::AMPERSAND) {
            return subtype->get_reference();
        } else
            return {}; // Shall not happen
    } else if(auto arr = dynamic_cast<const k::parse::ast::array_type_specifier*>(&type_spec)) {
        auto subtype = unresolved_type::from_type_specifier(*arr->subtype);
        if(arr->lex_int) {
            return subtype->get_array(arr->lex_int->to_unsigned_int());
        } else {
            return subtype->get_array();
        }
    } else {
        return {};
    }
}

std::string unresolved_type::to_string() const {
    return "<<unresolved>>";
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

bool primitive_type::is_resolved() const
{
    return true;
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

std::string primitive_type::to_string()const {
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

//
// Pointer type
//
pointer_type::pointer_type(const std::shared_ptr<type> &subtype):
type(subtype)
{}

bool pointer_type::is_resolved() const
{
    return subtype.lock()->is_resolved();
}

std::string pointer_type::to_string() const {
    auto sub = subtype.lock();
    if(sub) {
        return sub->to_string() + "*";
    } else {
        return "<<nosub>>*";
    }
}


//
// Reference type
//
reference_type::reference_type(const std::shared_ptr<type> &subtype):
type(subtype)
{}

bool reference_type::is_resolved() const
{
    return subtype.lock()->is_resolved();
}

/*
std::shared_ptr<reference_type> reference_type::get_reference() {
    return std::dynamic_pointer_cast<reference_type>(shared_from_this());
}
*/

std::string reference_type::to_string() const {
    auto sub = subtype.lock();
    if(sub) {
        return sub->to_string() + "&";
    } else {
        return "<<nosub>>&";
    }
}

//
// Array type
//

array_type::array_type(std::shared_ptr<type> subtype) :
    type(subtype)
{}

bool array_type::is_resolved() const
{
    return subtype.lock()->is_resolved();
}

bool array_type::is_sized() const {
    return false;
}

std::shared_ptr<sized_array_type> array_type::with_size(unsigned long size) {
    return tools::compute_if_absent(_sized_types, size,
                    [&](unsigned long sz){return std::shared_ptr<sized_array_type>{new sized_array_type(std::weak_ptr<array_type>(std::dynamic_pointer_cast<array_type>(this->shared_from_this())), sz)};}
            )->second;
}

std::string array_type::to_string() const {
    auto sub = subtype.lock();
    if(sub) {
        return sub->to_string() + "[]";
    } else {
        return "<<nosub>>[]";
    }
}

//
// Sized array type
//

sized_array_type::sized_array_type(std::weak_ptr<array_type> unsized_array_type, unsigned long size) :
    array_type(unsized_array_type.lock()->get_subtype()),
    _unsized_array_type(unsized_array_type),
    size(size)
{}

unsigned long sized_array_type::get_size() const {
    return size;
}

bool sized_array_type::is_sized() const {
    return true;
}

std::shared_ptr<array_type> sized_array_type::get_unsized() const {
    return _unsized_array_type.lock();
}

std::shared_ptr<sized_array_type> sized_array_type::with_size(unsigned long size) {
    return _unsized_array_type.lock()->with_size(size);
}


std::string sized_array_type::to_string() const {
    auto sub = subtype.lock();
    std::ostringstream stm;
    if(sub) {
        stm << sub->to_string();
    } else {
        stm << "<<nosub>>";
    }
    stm << '[' << size << ']';
    return stm.str();
}



} // k::model
