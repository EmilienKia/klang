//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#include "type.hpp"
#include "unit.hpp"

namespace k::unit {

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
        {primitive_type::BYTE, primitive_type::make_shared(primitive_type::BYTE, false, false, 1)},
        {primitive_type::CHAR, primitive_type::make_shared(primitive_type::CHAR, false, false, 1)},
        {primitive_type::SHORT, primitive_type::make_shared(primitive_type::SHORT, false, false, 2)},
        {primitive_type::UNSIGNED_SHORT, primitive_type::make_shared(primitive_type::UNSIGNED_SHORT, true, false, 2)},
        {primitive_type::INT, primitive_type::make_shared(primitive_type::INT, false, false, 4)},
        {primitive_type::UNSIGNED_INT, primitive_type::make_shared(primitive_type::UNSIGNED_INT, true, false, 4)},
        {primitive_type::LONG, primitive_type::make_shared(primitive_type::LONG, false, false, 8)},
        {primitive_type::UNSIGNED_LONG, primitive_type::make_shared(primitive_type::UNSIGNED_LONG, true, false, 8)},
        {primitive_type::FLOAT, primitive_type::make_shared(primitive_type::FLOAT, false, true, 4)},
        {primitive_type::DOUBLE, primitive_type::make_shared(primitive_type::DOUBLE, false, true, 8)},
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
            {"byte", BYTE},
            {"char", CHAR},
            {"short", SHORT},
            // TODO Add unsigned short
            {"int", INT},
            // TODO Add unsigned int
            {"long", LONG},
            // TODO Add unsigned long
            // TODO Add (unsigned) long long
            {"float", FLOAT},
            {"double", DOUBLE}
    };
    auto it = type_map.find(type_name);
    if(it!=type_map.end()) {
        return _predef_types[it->second];
    }
    return {};
}

std::shared_ptr<type> primitive_type::from_keyword(const lex::keyword& kw) {
    return from_string(kw.content);
}

const std::string& primitive_type::to_string()const {
    static std::map<primitive_type::PRIMITIVE_TYPE, std::string> type_names {
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


} // k::unit
