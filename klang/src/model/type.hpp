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
#include <optional>

#include "../common/common.hpp"

namespace llvm {
    class Constant;
    class ConstantStruct;
    class Type;
    class StructType;
    class Value;
}

namespace k {
namespace parse::ast {
class type_specifier;
}

namespace lex {
class keyword;
}
}


namespace k::model {

namespace gen {
    class symbol_resolver;
}


class context;

class structure;

class reference_type;
class pointer_type;
class sized_array_type;
class array_type;
class struct_type;
class function_reference_type;

/**
 * Base type class
 */
class type : public std::enable_shared_from_this<type>{
protected:
    std::weak_ptr<type> subtype;

    std::shared_ptr<reference_type> reference;
    std::shared_ptr<pointer_type> pointer;
    std::shared_ptr<array_type> array;

    mutable llvm::Type* _llvm_type;

    type(llvm::Type* llvm_type = nullptr) : _llvm_type(llvm_type) {}
    type(std::shared_ptr<type> subtype, llvm::Type* llvm_type = nullptr);

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
    inline static bool is_double_reference(const std::shared_ptr<type>& type);
    inline static bool is_pointer(const std::shared_ptr<type>& type);
    inline static bool is_sized_array(const std::shared_ptr<type>& type);
    inline static bool is_array(const std::shared_ptr<type>& type);
    inline static bool is_struct(const std::shared_ptr<type>& type);
    inline static bool is_function_reference(const std::shared_ptr<type>& type);

    virtual std::shared_ptr<reference_type> get_reference();
    std::shared_ptr<pointer_type> get_pointer();
    std::shared_ptr<array_type> get_array();
    std::shared_ptr<sized_array_type> get_array(unsigned long size);

    virtual llvm::Type* get_llvm_type() const;

    virtual llvm::Constant* generate_default_value_initializer() const;

    virtual std::string to_string() const =0;
};

/**
 * Unresolved type
 */
class unresolved_type : public type {
protected:
    name _type_id;

    std::shared_ptr<type> _resolved;

    friend class context;

    unresolved_type(const name& type_id): _type_id(type_id) {}
    unresolved_type(name&& type_id): _type_id(type_id) {}

    void resolve(std::shared_ptr<type> res_type) {_resolved = res_type;}

public:
    const name& type_id() const {return _type_id;}

    std::string to_string() const override;

    bool is_resolved()const {return !!_resolved;}
    std::shared_ptr<type> get_resolved()const {return _resolved;}

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

    primitive_type(PRIMITIVE_TYPE prim_type, bool is_unsigned, bool is_float, size_t size, llvm::Type* llvm_type):
        type(llvm_type), _type(prim_type), _is_unsigned(is_unsigned),_is_float(is_float), _size(size){}
    primitive_type(const primitive_type&) = default;
    primitive_type(primitive_type&&) = default;

    friend class context;
    static std::shared_ptr<primitive_type> make_shared(PRIMITIVE_TYPE type, bool is_unsigned, bool is_float, size_t size, llvm::Type* llvm_type);

public:

    PRIMITIVE_TYPE get_type() const {return _type;}

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

    llvm::Constant* generate_default_value_initializer() const override;

    std::string to_string()const override;

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

    llvm::Type* get_llvm_type() const override;

    std::string to_string() const override;

    std::shared_ptr<type> get_referenced_type() const {return get_subtype();}

};

inline bool type::is_reference(const std::shared_ptr<type>& type) {
    return std::dynamic_pointer_cast<reference_type>(type) != nullptr;
}

inline bool type::is_double_reference(const std::shared_ptr<type>& type) {
    std::shared_ptr<reference_type> ref_type = std::dynamic_pointer_cast<reference_type>(type);
    if(!ref_type) {
        return false;
    }
    return is_reference(ref_type->get_subtype());
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

    llvm::Type* get_llvm_type() const override;

    std::string to_string() const override;

    std::shared_ptr<type> get_pointed_type() const {return get_subtype();}

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

    llvm::Type* get_llvm_type() const override;

    std::string to_string() const override;
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

    llvm::Type* get_llvm_type() const override;

    std::string to_string() const override;

};


inline bool type::is_array(const std::shared_ptr<type>& type) {
    return std::dynamic_pointer_cast<array_type>(type) != nullptr;
}

inline bool type::is_sized_array(const std::shared_ptr<type>& type) {
    return std::dynamic_pointer_cast<sized_array_type>(type) != nullptr;
}




/**
 * Struct type
 */
class struct_type : public type {
public:
    struct field {
        size_t index;
        std::string name;
        std::weak_ptr<type> field_type;
    };
    typedef std::vector<field> fields_t;

protected:
    friend class type;
    friend class struct_type_builder;
    friend class k::model::structure;
    friend class k::model::gen::symbol_resolver;
    friend class k::model::context;

    std::string _name;

    std::vector<field> _fields;

    std::weak_ptr<k::model::structure> _struct;

    struct_type(const std::string& name, std::weak_ptr<k::model::structure> st, std::vector<field>&& fields, llvm::StructType* llvm_struct_type);

    void set_llvm_type(std::vector<field>&& fields, llvm::StructType* llvm_struct_type);

public:
    struct_type(const std::string& name, std::weak_ptr<k::model::structure> st);

    std::string name() const {return _name;}

    bool is_resolved() const override;

    std::string to_string() const override;
    std::shared_ptr<structure> get_struct() const;

    inline fields_t::size_type fields_size()const {return _fields.size();}
    inline fields_t::const_iterator fields_begin()const {return _fields.begin();}
    inline fields_t::const_iterator fields_end()const {return _fields.end();}

    bool has_member(const std::string& name) const;
    std::optional<field> get_member(const std::string& name) const;

    llvm::Constant* generate_default_value_initializer() const override;
};

class struct_type_builder {
protected:
    std::shared_ptr<context> _context;
    std::string _name;
    std::weak_ptr<k::model::structure> _struct;
    std::vector<struct_type::field> _fields;

public:
    struct_type_builder(std::shared_ptr<context> context);

    void name(const std::string& name) {_name = name;}
    void structure(std::weak_ptr<k::model::structure> st) {_struct = st;}

    void append_field(const std::string& name, std::shared_ptr<type> type);

    std::shared_ptr<struct_type> build();
};



inline bool type::is_struct(const std::shared_ptr<type>& type) {
    return std::dynamic_pointer_cast<struct_type>(type) != nullptr;
}


/**
 * Function reference type
 */
class function_reference_type : public type {
protected:
    friend class type;
    friend class function_reference_type_builder;

    std::shared_ptr<type> _return_type;
    std::vector<std::shared_ptr<type>> _parameter_types;

    function_reference_type() : type(nullptr) {}
    function_reference_type(const std::shared_ptr<type>& return_type, const std::vector<std::shared_ptr<type>>& parameter_types, llvm::Type* llvm_type):
        type(llvm_type), _return_type(return_type), _parameter_types(parameter_types) {}

public:
    bool is_resolved() const override;
    std::string to_string() const override;
};

class member_function_reference_type : public function_reference_type {
protected:
    std::shared_ptr<structure> _member_of;

    member_function_reference_type(const std::shared_ptr<structure>& member_of, const std::shared_ptr<type>& return_type, const std::vector<std::shared_ptr<type>>& parameter_types, llvm::Type* llvm_type):
        function_reference_type(return_type, parameter_types, llvm_type), _member_of(member_of) {}

    std::string to_string() const override;
};


class function_reference_type_builder {
protected:
    std::shared_ptr<context> _context;
    std::shared_ptr<structure> _member_of;
    std::shared_ptr<type> _return_type;
    std::vector<std::shared_ptr<type>> _parameter_types;
public:
    function_reference_type_builder(const std::shared_ptr<context>& context);
    void member_of(const std::shared_ptr<structure>& st) {_member_of = st;}
    void return_type(const std::shared_ptr<type>& return_type) {_return_type = return_type;}
    void append_parameter_type(const std::shared_ptr<type>& param_type) {_parameter_types.push_back(param_type);}
    std::shared_ptr<function_reference_type> build() const;
};


inline bool type::is_function_reference(const std::shared_ptr<type>& type) {
    return std::dynamic_pointer_cast<function_reference_type>(type) != nullptr;
}


} // namespace k::model
#endif //KLANG_TYPE_HPP
