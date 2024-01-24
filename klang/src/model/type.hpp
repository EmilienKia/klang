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

#ifndef KLANG_TYPE_HPP
#define KLANG_TYPE_HPP

#include <memory>
#include <map>

#include "../common/common.hpp"

namespace k {
namespace parse::ast {
class type_specifier;
}

namespace lex {
class keyword;
}
}


namespace k::model {

class reference_type;
class pointer_type;
class sized_array_type;
class array_type;


/**
 * Base type class
 */
class type : public std::enable_shared_from_this<type>{
protected:
    std::weak_ptr<type> subtype;

    std::shared_ptr<reference_type> reference;
    std::shared_ptr<pointer_type> pointer;
    std::shared_ptr<array_type> array;

    type() = default;
    type(std::shared_ptr<type> subtype);

public:
    virtual ~type() = default;

    std::shared_ptr<type> get_subtype() const;

    virtual bool is_primitive() const;

    virtual bool is_resolved() const;

    inline static bool is_resolved(const std::shared_ptr<type>& type);
    inline static bool is_primitive(const std::shared_ptr<type>& type);
    inline static bool is_prim_integer(const std::shared_ptr<type>& type);
    inline static bool is_prim_integer_or_bool(const std::shared_ptr<type>& type);
    inline static bool is_prim_bool(const std::shared_ptr<type>& type);
    inline static bool is_prim_float(const std::shared_ptr<type>& type);
    inline static bool is_reference(const std::shared_ptr<type>& type);
    inline static bool is_pointer(const std::shared_ptr<type>& type);
    inline static bool is_sized_array(const std::shared_ptr<type>& type);
    inline static bool is_array(const std::shared_ptr<type>& type);


    std::shared_ptr<reference_type> get_reference();
    std::shared_ptr<pointer_type> get_pointer();
    std::shared_ptr<array_type> get_array();
    std::shared_ptr<sized_array_type> get_array(unsigned long size);
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
 * Primitive type
 */
class primitive_type : public type {
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

    bool is_resolved() const override;
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


inline bool type::is_resolved(const std::shared_ptr<type>& type) {
    return type!=nullptr && type->is_resolved();
}

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

/**
 * Reference type
 */
class reference_type : public type {
protected:
    friend class type;

    reference_type(const std::shared_ptr<type> &subtype);

public:
    bool is_resolved() const override;
};

inline bool type::is_reference(const std::shared_ptr<type>& type) {
    return std::dynamic_pointer_cast<reference_type>(type) != nullptr;
}


/**
 * Pointer type
 */
class pointer_type : public type {
protected:
    friend class type;

    pointer_type(const std::shared_ptr<type> &subtype);

public:
    bool is_resolved() const override;
};

inline bool type::is_pointer(const std::shared_ptr<type>& type) {
    return std::dynamic_pointer_cast<pointer_type>(type) != nullptr;
}


/**
 * Array type, without size
 */
class array_type : public type {
protected:
    friend class type;

    array_type(std::shared_ptr<type> subtype);

    std::map<unsigned long, std::shared_ptr<sized_array_type>> _sized_types;

public:
    bool is_resolved() const override;

    virtual bool is_sized() const;

    virtual std::shared_ptr<sized_array_type> with_size(unsigned long size);
};


class sized_array_type : public array_type {
protected:
    unsigned long size;

    std::weak_ptr<array_type> _unsized_array_type;

    friend class array_type;
    sized_array_type(std::weak_ptr<array_type> unsized_array_type, unsigned long size);

public:
    unsigned long get_size() const;

    bool is_sized() const override;

    std::shared_ptr<array_type> get_unsized() const;

    std::shared_ptr<sized_array_type> with_size(unsigned long size) override;
};


inline bool type::is_array(const std::shared_ptr<type>& type) {
    return std::dynamic_pointer_cast<array_type>(type) != nullptr;
}

inline bool type::is_sized_array(const std::shared_ptr<type>& type) {
    return std::dynamic_pointer_cast<sized_array_type>(type) != nullptr;
}

} // namespace k::model
#endif //KLANG_TYPE_HPP
