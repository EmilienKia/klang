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
    class ArrayType;
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
    class type_reference_resolver;
}


class context;

class aggregate;
class structure;

class null_type;
class reference_type;
class pointer_type;
class link_type;
class pinned_type;
class owner_type;
class const_type;
class sized_array_type;
class array_type;
class struct_type;
class function_reference_type;
class enum_type;
class enumeration;

/**
 * Base type class
 */
class type : public std::enable_shared_from_this<type>{
protected:
    std::weak_ptr<type> subtype;

    std::shared_ptr<reference_type> reference;
    std::shared_ptr<pointer_type> pointer;
    std::shared_ptr<link_type> link;
    std::shared_ptr<pinned_type> pinned;
    std::shared_ptr<owner_type> owner;
    std::shared_ptr<const_type> const_;
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
    /** True if the type is a primitive type (optionally stripping const). */
    inline static bool is_primitive(const std::shared_ptr<type>& type);
    /** True if the type is a primitive boolean type (optionally stripping const). */
    inline static bool is_prim_bool(const std::shared_ptr<type>& type);
    /** True if the type is a primitive integer type (optionally stripping const). */
    inline static bool is_prim_integer(const std::shared_ptr<type>& type);
    /** True if the type is a primitive integer or bool type (optionally stripping const). */
    inline static bool is_prim_integer_or_bool(const std::shared_ptr<type>& type);
    /** True if the type is a primitive float type (optionally stripping const). */
    inline static bool is_prim_float(const std::shared_ptr<type>& type);
    /** True if the type is a struct type (optionally stripping const). */
    inline static bool is_struct(const std::shared_ptr<type>& type);
    inline static bool is_reference(const std::shared_ptr<type>& type);
    inline static bool is_double_reference(const std::shared_ptr<type>& type);
    inline static bool is_pointer(const std::shared_ptr<type>& type);
    inline static bool is_link(const std::shared_ptr<type>& type);
    inline static bool is_pinned(const std::shared_ptr<type>& type);
    inline static bool is_owner(const std::shared_ptr<type>& type);
    /** True if the type is a const-qualified type. */
    inline static bool is_const(const std::shared_ptr<type>& type);
    /** Remove const qualifier if present, return the inner type. If not const, return as-is. */
    inline static std::shared_ptr<type> remove_const(const std::shared_ptr<type>& type);
    /** True for any of the four indirection kinds: reference, pointer, link, pinned. */
    inline static bool is_any_indirection(const std::shared_ptr<type>& type);
    /** True for indirections that are non-null (reference and link). */
    inline static bool is_strong_indirection(const std::shared_ptr<type>& type);
    /** True for indirections that are mutable (link and pointer). */
    inline static bool is_mutable_indirection(const std::shared_ptr<type>& type);
    /** True for indirections that may be null (pointer and pinned). */
    inline static bool is_nullable_indirection(const std::shared_ptr<type>& type);
    /** True for indirections that are immutable (reference and pinned). */
    inline static bool is_immutable_indirection(const std::shared_ptr<type>& type);
    inline static bool is_sized_array(const std::shared_ptr<type>& type);
    inline static bool is_array(const std::shared_ptr<type>& type);
    inline static bool is_function_reference(const std::shared_ptr<type>& type);
    /** True if the type is an enum type (optionally stripping const). */
    inline static bool is_enum(const std::shared_ptr<type>& type);
    /** True if the type is the null literal type. */
    inline static bool is_null(const std::shared_ptr<type>& type);

    inline static bool are_equal(const std::shared_ptr<type>& type1, const std::shared_ptr<type>& type2);

    virtual std::shared_ptr<reference_type> get_reference();
    std::shared_ptr<pointer_type> get_pointer();
    std::shared_ptr<link_type> get_link();
    std::shared_ptr<pinned_type> get_pinned();
    std::shared_ptr<owner_type> get_owner();
    std::shared_ptr<const_type> get_const();
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
 * Null literal type.
 *
 * Sentinel type representing the `null` literal.  It is implicitly convertible
 * to any nullable indirection type (pointer, pinned, owner) and can appear on
 * either side of an address-equality comparison (==, !=) with any indirection.
 *
 * A single instance is held by `context` (singleton pattern).
 * The LLVM representation is an opaque pointer (ptr, address-space 0).
 */
class null_type : public type {
protected:
    friend class context;
    null_type() : type(nullptr) {}

public:
    bool is_resolved() const override { return true; }
    llvm::Type* get_llvm_type() const override;
    llvm::Constant* generate_default_value_initializer() const override;
    std::string to_string() const override { return "null"; }
};

inline bool type::is_null(const std::shared_ptr<type>& t) {
    return std::dynamic_pointer_cast<null_type>(t) != nullptr;
}

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
};

inline bool type::is_resolved(const std::shared_ptr<type>& type) {
    return type!=nullptr && type->is_resolved();
}

inline bool type::is_primitive(const std::shared_ptr<type>& t) {
    auto nc = remove_const(t);
    return std::dynamic_pointer_cast<primitive_type>(nc) != nullptr;
}

inline bool type::is_prim_integer(const std::shared_ptr<type>& t) {
    auto prim = std::dynamic_pointer_cast<primitive_type>(remove_const(t));
    return prim != nullptr && prim->is_integer();
}

inline bool type::is_prim_integer_or_bool(const std::shared_ptr<type>& t) {
    auto prim = std::dynamic_pointer_cast<primitive_type>(remove_const(t));
    return prim != nullptr && prim->is_integer_or_bool();
}

inline bool type::is_prim_bool(const std::shared_ptr<type>& t) {
    auto prim = std::dynamic_pointer_cast<primitive_type>(remove_const(t));
    return prim != nullptr && prim->is_boolean();
}

inline bool type::is_prim_float(const std::shared_ptr<type>& t){
    auto prim = std::dynamic_pointer_cast<primitive_type>(remove_const(t));
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
 * Link type (~) — mutable, non-null (strong) indirection.
 * Like a reference but rebindable (via assignment to the link itself).
 * Represented in LLVM as an opaque pointer, identical to pointer_type.
 * Mangling modifier: 'L'
 */
class link_type : public type {
protected:
    friend class type;

    link_type(const std::shared_ptr<type> &subtype);

public:
    bool is_resolved() const override;

    llvm::Type* get_llvm_type() const override;

    std::string to_string() const override;

    std::shared_ptr<type> get_linked_type() const {return get_subtype();}
};

inline bool type::is_link(const std::shared_ptr<type>& type) {
    return std::dynamic_pointer_cast<link_type>(type) != nullptr;
}


/**
 * Pinned type (^) — immutable (not rebindable after construction), nullable indirection.
 * Like a const pointer: can be null, but cannot be rebound after initialisation.
 * Represented in LLVM as an opaque pointer, identical to pointer_type.
 * Mangling modifier: 'Q'
 */
class pinned_type : public type {
protected:
    friend class type;

    pinned_type(const std::shared_ptr<type> &subtype);

public:
    bool is_resolved() const override;

    llvm::Type* get_llvm_type() const override;

    std::string to_string() const override;

    std::shared_ptr<type> get_pinned_type() const {return get_subtype();}
};

inline bool type::is_pinned(const std::shared_ptr<type>& type) {
    return std::dynamic_pointer_cast<pinned_type>(type) != nullptr;
}

/**
 * Owner type (!) — owning pointer (unique ownership), nullable, mutable.
 * Like std::unique_ptr: the single owner of a heap-allocated object.
 * When the owner goes out of scope or is assigned null, the object is destroyed (dtor + free).
 * Assignment transfers ownership (move semantics): source becomes null.
 * Represented in LLVM as an opaque pointer (same as pointer_type).
 * Mangling modifier: 'W' (owner)
 */
class owner_type : public type {
protected:
    friend class type;

    owner_type(const std::shared_ptr<type> &subtype);

public:
    bool is_resolved() const override;

    llvm::Type* get_llvm_type() const override;

    std::string to_string() const override;

    std::shared_ptr<type> get_owned_type() const { return get_subtype(); }
};

inline bool type::is_owner(const std::shared_ptr<type>& type) {
    return std::dynamic_pointer_cast<owner_type>(type) != nullptr;
}

/**
 * Const type — compile-time-only qualifier.
 * Wraps another type and marks it as immutable: no assignment or mutation is allowed
 * through a variable or indirection of this type.
 * const_type has NO impact on the generated LLVM IR; it is enforced at type-resolution time only.
 * Mangling modifier: 'K' (prefix before the inner type mangling).
 */
class const_type : public type {
protected:
    friend class type;

    const_type(const std::shared_ptr<type> &subtype);

public:
    bool is_resolved() const override;

    /** const_type delegates its LLVM type to the inner type (no IR impact). */
    llvm::Type* get_llvm_type() const override;

    std::string to_string() const override;

    std::shared_ptr<type> get_inner_type() const { return get_subtype(); }
};

inline bool type::is_const(const std::shared_ptr<type>& t) {
    return std::dynamic_pointer_cast<const_type>(t) != nullptr;
}

inline std::shared_ptr<type> type::remove_const(const std::shared_ptr<type>& t) {
    if (auto ct = std::dynamic_pointer_cast<const_type>(t)) {
        return ct->get_inner_type();
    }
    return t;
}

inline bool type::is_any_indirection(const std::shared_ptr<type>& t) {
    return is_reference(t) || is_pointer(t) || is_link(t) || is_pinned(t) || is_owner(t);
}

inline bool type::is_strong_indirection(const std::shared_ptr<type>& t) {
    return is_reference(t) || is_link(t);
}

inline bool type::is_mutable_indirection(const std::shared_ptr<type>& t) {
    return is_link(t) || is_pointer(t) || is_owner(t);
}

inline bool type::is_nullable_indirection(const std::shared_ptr<type>& t) {
    return is_pointer(t) || is_pinned(t) || is_owner(t);
}

inline bool type::is_immutable_indirection(const std::shared_ptr<type>& t) {
    return is_reference(t) || is_pinned(t);
}


/**
 * Array type, without size.
 * LLVM representation: { i32, [0 x T] } — a trailing-array struct.
 * Field 0 (i32): element count (set at runtime).
 * Field 1 ([0 x T]): variable-length element data (accessed via GEP).
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

    /** Index of the size/capacity field in the LLVM struct (always 0). */
    static constexpr unsigned FIELD_SIZE = 0;
    /** Index of the data array field in the LLVM struct (always 1). */
    static constexpr unsigned FIELD_DATA = 1;

    /**
     * Returns the LLVM struct type { i32, [0 x T] } for this unsized array.
     * Used for GEP-based element access when the size is unknown at compile time.
     */
    llvm::Type* get_llvm_type() const override;

    /** Returns the LLVM struct type cast, or nullptr. */
    llvm::StructType* get_llvm_struct_type() const;

    /** Returns the LLVM [0 x T] array type (field 1 of the struct). */
    llvm::ArrayType* get_llvm_data_array_type() const;

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

    /**
     * Returns the LLVM struct type { i32, [N x T] } for this sized array.
     * Field 0: i32 (element count / capacity)
     * Field 1: [N x T] (element data)
     */
    llvm::Type* get_llvm_type() const override;

    /** Index of the size/capacity field in the LLVM struct (always 0). */
    static constexpr unsigned FIELD_SIZE = 0;
    /** Index of the data array field in the LLVM struct (always 1). */
    static constexpr unsigned FIELD_DATA = 1;

    /** Returns the LLVM struct type cast, or nullptr. */
    llvm::StructType* get_llvm_struct_type() const;

    /** Returns the LLVM [N x T] array type (field 1 of the struct). */
    llvm::ArrayType* get_llvm_data_array_type() const;

    llvm::Constant* generate_default_value_initializer() const override;

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
    friend class k::model::aggregate;
    friend class k::model::structure;
    friend class k::model::gen::symbol_resolver;
    friend class k::model::gen::type_reference_resolver;
    friend class k::model::context;

    std::string _name;

    std::vector<field> _fields;

    std::weak_ptr<k::model::aggregate> _struct;

    llvm::Constant* _default_init_constant = nullptr;

    struct_type(const std::string& name, std::weak_ptr<k::model::aggregate> st, std::vector<field>&& fields, llvm::StructType* llvm_struct_type);

    void set_llvm_type(std::vector<field>&& fields, llvm::StructType* llvm_struct_type, llvm::Constant* default_init_constant);

public:
    struct_type(const std::string& name, std::weak_ptr<k::model::aggregate> st);

    std::string name() const {return _name;}

    bool is_resolved() const override;

    std::string to_string() const override;
    std::shared_ptr<aggregate> get_struct() const;

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
    std::weak_ptr<k::model::aggregate> _struct;
    std::vector<struct_type::field> _fields;

public:
    struct_type_builder(std::shared_ptr<context> context);

    void name(const std::string& name) {_name = name;}
    void structure(std::weak_ptr<k::model::aggregate> st) {_struct = st;}

    void append_field(const std::string& name, std::shared_ptr<type> type);

    std::shared_ptr<struct_type> build();
};



inline bool type::is_struct(const std::shared_ptr<type>& t) {
    return std::dynamic_pointer_cast<struct_type>(remove_const(t)) != nullptr;
}


/**
 * Function reference type.
 *
 * Represents the type of a reference (pointer *, pin ^, or link ~) to a free or
 * static function.  The "ref_kind" field records which of the three reference
 * flavours was declared; however all three share the same LLVM representation
 * (an opaque function pointer).
 *
 * A symbol that resolves to a function (without the call parentheses) has a type
 * that is always a reference_type wrapping a function_reference_type — because a
 * function address is non-null and immutable, which matches the semantics of a
 * reference (& in K).  It can be assigned at construction to a pin (^) or link (~)
 * variable, and at construction and re-assignment to a pointer (*) variable.
 */
class function_reference_type : public type {
public:
    /** The reference qualifier declared in source (*, ^, ~) — informational only for LLVM. */
    enum class ref_kind { pointer, pin, link };

protected:
    friend class type;
    friend class function_reference_type_builder;
    friend class context;

    std::shared_ptr<type> _return_type;
    std::vector<std::shared_ptr<type>> _parameter_types;
    ref_kind _ref_kind = ref_kind::pointer;

    function_reference_type() : type(nullptr) {}
    function_reference_type(const std::shared_ptr<type>& return_type,
                            const std::vector<std::shared_ptr<type>>& parameter_types,
                            ref_kind rk,
                            llvm::Type* llvm_type):
        type(llvm_type), _return_type(return_type), _parameter_types(parameter_types), _ref_kind(rk) {}

public:
    bool is_resolved() const override;
    llvm::Type* get_llvm_type() const override;
    llvm::Constant* generate_default_value_initializer() const override;
    std::string to_string() const override;

    const std::shared_ptr<type>& get_return_type() const { return _return_type; }
    void set_return_type(const std::shared_ptr<type>& rt) { _return_type = rt; }
    const std::vector<std::shared_ptr<type>>& get_parameter_types() const { return _parameter_types; }
    ref_kind get_ref_kind() const { return _ref_kind; }

    /** Structural equality: same return type, same parameter types, same ref_kind. */
    bool structurally_equal(const function_reference_type& other) const;
};

/**
 * Member function reference type (unbound method pointer).
 *
 * Represents the type of a reference to a non-static member function.
 * The binding (which object to call it on) is done at the call site, following
 * the unified calling convention of K (implicit or explicit this).
 * LLVM representation: same as function_reference_type but with an implicit
 * first parameter of type "reference to owner struct".
 */
class member_function_reference_type : public function_reference_type {
protected:
    friend class function_reference_type_builder;
    friend class context;

    std::shared_ptr<aggregate> _member_of;

    member_function_reference_type(const std::shared_ptr<aggregate>& member_of,
                                   const std::shared_ptr<type>& return_type,
                                   const std::vector<std::shared_ptr<type>>& parameter_types,
                                   ref_kind rk,
                                   llvm::Type* llvm_type):
        function_reference_type(return_type, parameter_types, rk, llvm_type),
        _member_of(member_of) {}

public:
    llvm::Type* get_llvm_type() const override;
    std::string to_string() const override;

    const std::shared_ptr<aggregate>& get_member_of() const { return _member_of; }

    /** Structural equality (includes owner struct). */
    bool structurally_equal(const member_function_reference_type& other) const;
};


class function_reference_type_builder {
protected:
    std::shared_ptr<context> _context;
    std::shared_ptr<aggregate> _member_of;
    std::shared_ptr<type> _return_type;
    std::vector<std::shared_ptr<type>> _parameter_types;
    function_reference_type::ref_kind _ref_kind = function_reference_type::ref_kind::pointer;
public:
    explicit function_reference_type_builder(const std::shared_ptr<context>& context);
    void member_of(const std::shared_ptr<structure>& st) { _member_of = std::static_pointer_cast<aggregate>(st); }
    /** Accept any aggregate (structure or klass) as member owner. */
    void member_of(const std::shared_ptr<aggregate>& agg) { _member_of = agg; }
    void return_type(const std::shared_ptr<type>& return_type) { _return_type = return_type; }
    void append_parameter_type(const std::shared_ptr<type>& param_type) { _parameter_types.push_back(param_type); }
    void ref_kind(function_reference_type::ref_kind rk) { _ref_kind = rk; }
    std::shared_ptr<function_reference_type> build() const;
};


inline bool type::is_function_reference(const std::shared_ptr<type>& type) {
    return std::dynamic_pointer_cast<function_reference_type>(type) != nullptr;
}

/**
 * Unresolved function reference type.
 *
 * Placeholder created by context::from_type_specifier() when parsing a
 * function_ref_type_specifier.  Carries the raw parameter types (which may
 * themselves contain unresolved_type entries) and the optional owner name
 * (for member function pointers).  Resolved to function_reference_type or
 * member_function_reference_type by type_reference_resolver.
 */
class unresolved_function_ref_type : public type {
protected:
    friend class context;
    friend class gen::type_reference_resolver;

    k::name _owner_name;        ///< Empty for free functions.
    function_reference_type::ref_kind _ref_kind;
    std::vector<std::shared_ptr<type>> _parameter_types;
    std::shared_ptr<type> _resolved;

    unresolved_function_ref_type(
        const k::name& owner_name,
        function_reference_type::ref_kind rk,
        const std::vector<std::shared_ptr<type>>& param_types)
        : _owner_name(owner_name), _ref_kind(rk), _parameter_types(param_types) {}

    void resolve(std::shared_ptr<type> res_type) { _resolved = res_type; }

public:
    const k::name& owner_name() const { return _owner_name; }
    function_reference_type::ref_kind get_ref_kind() const { return _ref_kind; }
    const std::vector<std::shared_ptr<type>>& parameter_types() const { return _parameter_types; }

    bool is_resolved() const override { return !!_resolved; }
    std::shared_ptr<type> get_resolved() const { return _resolved; }

    std::string to_string() const override;
};


/**
 * Enum type — a nominal type backed by a primitive integer type.
 *
 * An enum_type wraps a primitive_type and is associated with an enumeration
 * model object. In LLVM IR, the enum is represented identically to its
 * underlying primitive integer type.
 *
 * Conversions:
 *   - enum ↔ underlying int: implicit
 *   - enum ↔ different enum: allowed with a warning (both must be primitive-backed)
 */
class enum_type : public type {
protected:
    friend class context;
    friend class model_builder;
    friend class unit;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;

    std::weak_ptr<enumeration> _enumeration;
    std::shared_ptr<primitive_type> _underlying_type;

    enum_type(std::weak_ptr<enumeration> e, std::shared_ptr<primitive_type> underlying)
        : _enumeration(std::move(e)), _underlying_type(std::move(underlying)) {}

public:
    bool is_resolved() const override { return _underlying_type != nullptr; }

    std::shared_ptr<primitive_type> get_underlying_type() const { return _underlying_type; }
    std::shared_ptr<enumeration> get_enumeration() const { return _enumeration.lock(); }

    llvm::Type* get_llvm_type() const override;
    llvm::Constant* generate_default_value_initializer() const override;
    std::string to_string() const override;
};

inline bool type::is_enum(const std::shared_ptr<type>& t) {
    auto nc = remove_const(t);
    return std::dynamic_pointer_cast<enum_type>(nc) != nullptr;
}


inline bool type::are_equal(const std::shared_ptr<type>& type1, const std::shared_ptr<type>& type2) {
    if (type1 == type2) return true;
    if (!type1 || !type2) return false;

    // Structural equality for array types: compare element types recursively
    if (auto a1 = std::dynamic_pointer_cast<sized_array_type>(type1)) {
        auto a2 = std::dynamic_pointer_cast<sized_array_type>(type2);
        return a2 && a1->get_size() == a2->get_size()
            && are_equal(a1->get_subtype(), a2->get_subtype());
    }
    if (auto a1 = std::dynamic_pointer_cast<array_type>(type1)) {
        auto a2 = std::dynamic_pointer_cast<array_type>(type2);
        if (!a2) return false;
        // Both must be unsized (sized_array_type was checked above)
        if (a1->is_sized() || a2->is_sized()) return false;
        return are_equal(a1->get_subtype(), a2->get_subtype());
    }

    // Structural equality for owner types: compare owned types recursively
    if (auto o1 = std::dynamic_pointer_cast<owner_type>(type1)) {
        auto o2 = std::dynamic_pointer_cast<owner_type>(type2);
        return o2 && are_equal(o1->get_owned_type(), o2->get_owned_type());
    }

    // Structural equality for reference types
    if (auto r1 = std::dynamic_pointer_cast<reference_type>(type1)) {
        auto r2 = std::dynamic_pointer_cast<reference_type>(type2);
        return r2 && are_equal(r1->get_subtype(), r2->get_subtype());
    }

    // Structural equality for function reference types
    if (auto f1 = std::dynamic_pointer_cast<member_function_reference_type>(type1)) {
        if (auto f2 = std::dynamic_pointer_cast<member_function_reference_type>(type2)) {
            return f1->structurally_equal(*f2);
        }
        return false;
    }
    if (auto f1 = std::dynamic_pointer_cast<function_reference_type>(type1)) {
        if (auto f2 = std::dynamic_pointer_cast<function_reference_type>(type2)) {
            if (std::dynamic_pointer_cast<member_function_reference_type>(type2)) return false;
            return f1->structurally_equal(*f2);
        }
        return false;
    }
    // Enum type nominal equality: same enumeration object
    if (auto e1 = std::dynamic_pointer_cast<enum_type>(type1)) {
        auto e2 = std::dynamic_pointer_cast<enum_type>(type2);
        return e2 && e1->get_enumeration() == e2->get_enumeration();
    }

    // Unresolved type: compare by name if both unresolved, or compare resolved forms
    if (auto u1 = std::dynamic_pointer_cast<unresolved_type>(type1)) {
        if (auto u2 = std::dynamic_pointer_cast<unresolved_type>(type2)) {
            // Both unresolved: if both resolved, compare resolved types
            if (u1->is_resolved() && u2->is_resolved()) {
                return are_equal(u1->get_resolved(), u2->get_resolved());
            }
            // Otherwise compare by type name
            return u1->type_id() == u2->type_id();
        }
        // One unresolved, one not: compare resolved form if available
        if (u1->is_resolved()) {
            return are_equal(u1->get_resolved(), type2);
        }
        return false;
    }
    if (auto u2 = std::dynamic_pointer_cast<unresolved_type>(type2)) {
        if (u2->is_resolved()) {
            return are_equal(type1, u2->get_resolved());
        }
        return false;
    }

    return false;
}


} // namespace k::model
#endif //KLANG_TYPE_HPP
