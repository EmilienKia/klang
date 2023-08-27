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

bool primitive_type::is_primitive() const
{
    return true;
}

std::shared_ptr<primitive_type> primitive_type::from_type(PRIMITIVE_TYPE type){
    return std::shared_ptr<primitive_type>{new primitive_type(type)};
}

std::shared_ptr<type> primitive_type::from_string(const std::string& type_name) {
    static std::map<std::string, primitive_type::PRIMITIVE_TYPE> type_map {
            {"byte", BYTE},
            {"char", CHAR},
            {"short", SHORT},
            {"int", INT},
            {"long", LONG},
            {"float", FLOAT},
            {"double", DOUBLE}
    };
    static std::map<primitive_type::PRIMITIVE_TYPE, std::shared_ptr<primitive_type> > predef_types {
            {BYTE, from_type(BYTE)},
            {CHAR,   from_type(CHAR)},
            {SHORT,  from_type(SHORT)},
            {INT,    from_type(INT)},
            {LONG,   from_type(LONG)},
            {FLOAT,  from_type(FLOAT)},
            {DOUBLE, from_type(DOUBLE)}
    };
    auto it = type_map.find(type_name);
    if(it!=type_map.end()) {
        return predef_types[it->second];
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
            {INT, "int"},
            {LONG, "long"},
            {FLOAT, "float"},
            {DOUBLE, "double"}
    };
    return type_names[_type];
}


} // k::unit
