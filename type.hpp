//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#ifndef KLANG_TYPE_HPP
#define KLANG_TYPE_HPP

#include <memory>
#include <map>

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

    inline static bool is_resolved(const std::shared_ptr<type>& type);
    inline static bool is_primitive(const std::shared_ptr<type>& type);
    inline static bool is_prim_integer(const std::shared_ptr<type>& type);
    inline static bool is_prim_integer_or_bool(const std::shared_ptr<type>& type);
    inline static bool is_prim_bool(const std::shared_ptr<type>& type);
    inline static bool is_prim_float(const std::shared_ptr<type>& type);
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


inline bool type::is_resolved(const std::shared_ptr<type>& type) {
    return std::dynamic_pointer_cast<resolved_type>(type) != nullptr;
}


/**
 * Primitive type
 */
class primitive_type : public resolved_type {
public:
    enum PRIMITIVE_TYPE {
        BOOL,
        CHAR,
        BYTE,
        UNSIGNED_CHAR = BYTE,
        SHORT,
        UNSIGNED_SHORT,
        INT,
        UNSIGNED_INT,
        LONG,
        UNSIGNED_LONG,
        // TODO add (unsigned) long long
        FLOAT,
        DOUBLE
    };

protected:
    PRIMITIVE_TYPE _type;
    bool _is_unsigned;
    bool _is_float;
    size_t _size; // Size in bits, boolean is 1 (unsigned)

    primitive_type(PRIMITIVE_TYPE type, bool is_unsigned, bool is_float, size_t size):
        _type(type), _is_unsigned(is_unsigned),_is_float(is_float), _size(size){}
    primitive_type(const primitive_type&) = default;
    primitive_type(primitive_type&&) = default;

    static std::map<PRIMITIVE_TYPE, std::shared_ptr<primitive_type>> _predef_types;
    static std::shared_ptr<primitive_type> make_shared(PRIMITIVE_TYPE type, bool is_unsigned, bool is_float, size_t size);

public:
    bool is_primitive() const override;

    bool is_boolean() const {return _type == BOOL;}

    bool is_unsigned() const {return _is_unsigned;}
    bool is_signed() const {return !_is_unsigned;}
    bool is_float() const {return _is_float;}
    bool is_integer()const {return !_is_float && _type!=BOOL;}
    bool is_integer_or_bool()const {return !_is_float;}

    size_t type_size()const {return _size;}

    bool operator == (const primitive_type& other) const {
        return _type == other._type;
    }

    bool operator == (PRIMITIVE_TYPE t) const {
        return _type == t;
    }


    const std::string& to_string()const;

    static std::shared_ptr<primitive_type> from_type(PRIMITIVE_TYPE type);
    static std::shared_ptr<type> from_string(const std::string& type_name);
    static std::shared_ptr<type> from_keyword(const lex::keyword& kw, bool is_unsigned = false);
};

inline bool type::is_primitive(const std::shared_ptr<type>& type) {
    return std::dynamic_pointer_cast<primitive_type>(type) != nullptr;
}

inline bool type::is_prim_integer(const std::shared_ptr<type>& type) {
    auto prim = std::dynamic_pointer_cast<primitive_type>(type);
    return prim != nullptr && prim->is_integer();
}

inline bool type::is_prim_integer_or_bool(const std::shared_ptr<type>& type) {
    auto prim = std::dynamic_pointer_cast<primitive_type>(type);
    return prim != nullptr && prim->is_integer_or_bool();
}

inline bool type::is_prim_bool(const std::shared_ptr<type>& type) {
    auto prim = std::dynamic_pointer_cast<primitive_type>(type);
    return prim != nullptr && prim->is_boolean();
}

inline bool type::is_prim_float(const std::shared_ptr<type>& type){
    auto prim = std::dynamic_pointer_cast<primitive_type>(type);
    return prim != nullptr && prim->is_float();
}

} // namespace k::unit
#endif //KLANG_TYPE_HPP
