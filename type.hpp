//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#ifndef KLANG_TYPE_HPP
#define KLANG_TYPE_HPP

#include <memory>
#include "common.hpp"

namespace k {
namespace parse::ast {
class type_specifier;
}

namespace lex {
class keyword;
}
}


namespace k::unit {


/**
 * Base type class
 */
class type : public std::enable_shared_from_this<type>{
public:
    virtual ~type() = default;

    virtual bool is_resolved() const;
    virtual bool is_primitive() const;
};

/**
 * Unresolved type
 */
class unresolved_type : public type {
protected:
    name _type_id;

    unresolved_type(const name& type_id): _type_id(type_id) {}
    unresolved_type(name&& type_id): _type_id(type_id) {}

public:
    static std::shared_ptr<type> from_string(const std::string& type_name);
    static std::shared_ptr<type> from_identifier(const name& type_id);
    static std::shared_ptr<type> from_type_specifier(const k::parse::ast::type_specifier& type_spec);

    const name& type_id() const {return _type_id;}
};

/**
 * Base class for resolved types.
 */
class resolved_type : public type {
public:
    bool is_resolved() const override;
};

/**
 * Primitive type
 */
class primitive_type : public resolved_type {
public:
    enum PRIMITIVE_TYPE {
        BYTE,
        CHAR,
        SHORT,
        INT,
        LONG,
        FLOAT,
        DOUBLE
    }_type;

protected:
    primitive_type(PRIMITIVE_TYPE type):_type(type){}
    primitive_type(const primitive_type&) = default;
    primitive_type(primitive_type&&) = default;

public:
    bool is_primitive() const override;

    bool operator == (const primitive_type& other) const {
        return _type == other._type;
    }

    const std::string& to_string()const;

    static std::shared_ptr<primitive_type> from_type(PRIMITIVE_TYPE type);
    static std::shared_ptr<type> from_string(const std::string& type_name);
    static std::shared_ptr<type> from_keyword(const lex::keyword& kw);
};



} // namespace k::unit
#endif //KLANG_TYPE_HPP
