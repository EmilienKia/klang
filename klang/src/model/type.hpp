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
#include <unordered_map>

#include "../common/common.hpp"
#include "../lex/lexemes.hpp"

namespace llvm {
    class ArrayType;
    class Constant;
    class ConstantStruct;
    class GlobalVariable;
    class Type;
    class StructType;
    class Value;
}

namespace k {
namespace parse::ast {
class type_specifier;
struct template_arg;
}

namespace lex {
class keyword;
}
}

namespace k::model {

namespace gen {
    class symbol_resolver;
    class aggregate_type_resolver;
    class type_reference_resolver;
}

class kdi_importer;

class context;

class aggregate;
class structure;

class null_type;
class reference_type;
class pointer_type;
class link_type;
class view_type;
class owner_type;
class drain_type;
class const_type;
class sized_array_type;
class array_type;
class struct_type;
class callable_type;
class enum_type;
class enumeration;
class alias_type;
class alias_definition;

/**
 * Base type class
 */
class type : public std::enable_shared_from_this<type>{
protected:
    std::weak_ptr<type> subtype;

    std::shared_ptr<reference_type> reference;
    std::shared_ptr<pointer_type> pointer;
    std::shared_ptr<link_type> link;
    std::shared_ptr<view_type> view;
    std::shared_ptr<owner_type> owner;
    std::shared_ptr<drain_type> drain;
    std::shared_ptr<const_type> const_;
    std::shared_ptr<array_type> array;

    /**
     * Strong owning reference to the wrapped subtype, used only for wrappers
     * built by make_pinned_wrapper() during template-argument substitution.
     * Normal wrappers leave this empty and rely on the subtype keeping the
     * wrapper alive through the cache members above. A pinned wrapper instead
     * keeps its (freshly cloned, otherwise unowned) subtype alive while
     * deliberately NOT being cached on that subtype, so no reference cycle is
     * created. See substitute_type() in type.cpp.
     */
    std::shared_ptr<type> _pinned_subtype;

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
    inline static bool is_view(const std::shared_ptr<type>& type);
    inline static bool is_owner(const std::shared_ptr<type>& type);
    inline static bool is_drain(const std::shared_ptr<type>& type);
    /** True if the type is a const-qualified type. */
    inline static bool is_const(const std::shared_ptr<type>& type);
    /** Remove const qualifier if present, return the inner type. If not const, return as-is. */
    inline static std::shared_ptr<type> remove_const(const std::shared_ptr<type>& type);
    /** True for any of the four indirection kinds: reference, pointer, link, view. */
    inline static bool is_any_indirection(const std::shared_ptr<type>& type);
    /** True for indirections that are non-null (reference and link). */
    inline static bool is_strong_indirection(const std::shared_ptr<type>& type);
    /** True for indirections that are mutable (link and pointer). */
    inline static bool is_mutable_indirection(const std::shared_ptr<type>& type);
    /** True for indirections that may be null (pointer and view). */
    inline static bool is_nullable_indirection(const std::shared_ptr<type>& type);
    /** True for indirections that are immutable (reference and view). */
    inline static bool is_immutable_indirection(const std::shared_ptr<type>& type);
    inline static bool is_sized_array(const std::shared_ptr<type>& type);
    inline static bool is_array(const std::shared_ptr<type>& type);
    inline static bool is_callable(const std::shared_ptr<type>& type);
    inline static bool is_fat_callable(const std::shared_ptr<type>& type);
    /** True if the type is an enum type (optionally stripping const). */
    inline static bool is_enum(const std::shared_ptr<type>& type);
    /** True if the type is a strong alias (typedef) type (optionally stripping const). */
    inline static bool is_alias(const std::shared_ptr<type>& type);
    /**
     * Strip every alias_type layer, recursively and through indirection
     * wrappers, yielding the real underlying type an alias stands for.
     * Returns the type itself when it contains no alias.
     *
     * This is the single primitive that keeps a strong alias from influencing
     * any layout, ABI or code-generation decision: an alias_type is nominal at
     * the K level only, and every consumer that reasons about representation
     * must canonicalise first.
     */
    static std::shared_ptr<type> canonical(const std::shared_ptr<type>& type);
    /**
     * True when both types have the same representation, i.e. are equal once
     * every alias layer has been stripped. Use this instead of are_equal()
     * wherever the question is about layout rather than about nominal identity.
     */
    inline static bool are_layout_equal(const std::shared_ptr<type>& type1, const std::shared_ptr<type>& type2);
    /** True if the type is the null literal type. */
    inline static bool is_null(const std::shared_ptr<type>& type);
    /** True if the type chain contains any unresolved_type nodes. */
    static bool contains_unresolved(const std::shared_ptr<type>& type);

    inline static bool are_equal(const std::shared_ptr<type>& type1, const std::shared_ptr<type>& type2);

    virtual std::shared_ptr<reference_type> get_reference();
    std::shared_ptr<pointer_type> get_pointer();
    std::shared_ptr<link_type> get_link();
    std::shared_ptr<view_type> get_view();
    std::shared_ptr<owner_type> get_owner();
    std::shared_ptr<drain_type> get_drain();
    std::shared_ptr<const_type> get_const();
    std::shared_ptr<array_type> get_array();
    std::shared_ptr<sized_array_type> get_array(unsigned long size);

    /** True if this wrapper strongly pins its subtype (built by make_pinned_wrapper). */
    bool is_pinned() const { return (bool)_pinned_subtype; }

    /**
     * Build a fresh indirection wrapper of the same kind as @p kind_of around
     * @p inner, strongly pinning @p inner so it stays alive even when it has no
     * other owner (e.g. a freshly cloned unresolved_type produced during
     * template-argument substitution). Unlike get_reference()/get_pointer()/…,
     * the resulting wrapper is NOT cached on @p inner, so no reference cycle is
     * formed. Returns nullptr if @p kind_of is not a supported wrapper kind.
     */
    static std::shared_ptr<type> make_pinned_wrapper(
        const std::shared_ptr<type>& kind_of,
        const std::shared_ptr<type>& inner);

    static std::shared_ptr<const_type> make_pinned_const(const std::shared_ptr<type>& inner);
    static std::shared_ptr<reference_type> make_pinned_reference(const std::shared_ptr<type>& inner);
    static std::shared_ptr<pointer_type> make_pinned_pointer(const std::shared_ptr<type>& inner);
    static std::shared_ptr<link_type> make_pinned_link(const std::shared_ptr<type>& inner);
    static std::shared_ptr<view_type> make_pinned_view(const std::shared_ptr<type>& inner);
    static std::shared_ptr<owner_type> make_pinned_owner(const std::shared_ptr<type>& inner);
    static std::shared_ptr<drain_type> make_pinned_drain(const std::shared_ptr<type>& inner);
    static std::shared_ptr<array_type> make_pinned_array(const std::shared_ptr<type>& inner);

    virtual llvm::Type* get_llvm_type() const;

    virtual llvm::Constant* generate_default_value_initializer() const;

    virtual std::string to_string() const =0;

    /** Returns the first lexeme associated with this type, if any. */
    virtual lex::opt_any_lexeme get_first_lexeme() const;

    /** Returns the last lexeme associated with this type, if any. */
    virtual lex::opt_any_lexeme get_last_lexeme() const;

    /** Returns the lexeme that is considered the "interest point" of this type (e.g. name of struct/enum/alias), if any. */
    virtual lex::opt_any_lexeme get_interest_lexeme() const;
};

/**
 * Unresolved type
 */
class unresolved_type : public type {
protected:
    name _type_id;

    std::shared_ptr<type> _resolved;

    /**
     * Optional AST-level template arguments carried from identified_type_specifier.
     * Empty if the type is not a template instantiation (e.g. plain "Foo").
     * Populated when the parser sees "Foo<int, float>" — the resolvers later
     * convert these into model-level template_argument values and trigger instantiation.
     */
    std::vector<std::shared_ptr<k::parse::ast::template_arg>> _ast_template_args;

    /**
     * True when '<>' or '<args>' was explicitly written in source, even if the
     * arg list is empty (all defaults). Used to distinguish "Box" (not a
     * template instantiation) from "Box<>" (instantiation with all defaults).
     */
    bool _has_explicit_template_args = false;

    /**
     * True when this unresolved type represents a template parameter placeholder
     * (e.g. "T" inside a template definition).  Such types are expected to remain
     * unresolved until instantiation substitutes them with concrete types.
     * Used to suppress cosmetic "cannot resolve type" diagnostics.
     */
    bool _is_template_param_placeholder = false;

    /**
     * Pre-resolved model-level template arguments, set by substitute_type() when
     * a template parameter name (e.g. "R") inside the AST arg list of an
     * unresolved_type (e.g. Expected<R,E>) is substituted with a concrete type.
     * When non-empty, try_instantiate_template_type() uses these directly instead
     * of re-resolving the AST arg names — which would fail in a concrete function
     * body where the original template params are no longer in scope.
     */
    std::vector<std::shared_ptr<type>> _model_template_args;

    friend class context;

    unresolved_type(const name& type_id): _type_id(type_id) {}
    unresolved_type(name&& type_id): _type_id(type_id) {}

    void resolve(std::shared_ptr<type> res_type) {_resolved = res_type;}

public:
    const name& type_id() const {return _type_id;}

    std::string to_string() const override;

    bool is_resolved()const {return !!_resolved;}
    std::shared_ptr<type> get_resolved()const {return _resolved;}

    /** True if this unresolved type is a template parameter placeholder (e.g. "T"). */
    bool is_template_param_placeholder() const { return _is_template_param_placeholder; }
    void set_template_param_placeholder(bool v = true) { _is_template_param_placeholder = v; }

    /** True if '<>' or '<args>' was explicitly written (even if args is empty). */
    bool has_explicit_template_args() const { return _has_explicit_template_args; }
    void set_has_explicit_template_args(bool v = true) { _has_explicit_template_args = v; }

    /** True if this unresolved type carries template arguments (e.g. Box<int>).
     *  Also true for Box<> (explicit empty arg list). */
    bool has_template_args() const { return !_ast_template_args.empty() || _has_explicit_template_args; }

    /** Return the AST-level template arguments (empty if none). */
    const std::vector<std::shared_ptr<k::parse::ast::template_arg>>& get_ast_template_args() const {
        return _ast_template_args;
    }

    /** Set the AST-level template arguments (e.g. when synthesising an unresolved
     *  template type reference from a call-site like Optional<byte>(args)). */
    void set_ast_template_args(std::vector<std::shared_ptr<k::parse::ast::template_arg>> args) {
        _ast_template_args = std::move(args);
        _has_explicit_template_args = true;
    }

    /** True if model-level template argument overrides are present (set by substitute_type). */
    bool has_model_template_args() const { return !_model_template_args.empty(); }

    /** Model-level template arguments, indexed parallel to get_ast_template_args().
     *  A null entry means the corresponding AST arg was not substituted. */
    const std::vector<std::shared_ptr<type>>& get_model_template_args() const {
        return _model_template_args;
    }

    static std::shared_ptr<type> substitute_ast_type_spec(
        const k::parse::ast::type_specifier* spec,
        const std::unordered_map<std::string, std::shared_ptr<type>>& subst);

    /**
     * Create a clone of this unresolved_type with model-level template arg overrides
     * derived from @p subst.  For each AST arg that is a simple identifier name found
     * in the substitution map, the corresponding slot in _model_template_args is set
     * to the substituted concrete type.  Returns nullptr if no arg was substituted.
     */
    std::shared_ptr<unresolved_type> clone_with_substituted_model_args(
        const std::unordered_map<std::string, std::shared_ptr<type>>& subst) const;

    llvm::Type* get_llvm_type() const override {
        return _resolved ? _resolved->get_llvm_type() : nullptr;
    }

    lex::opt_any_lexeme get_first_lexeme() const override;
    lex::opt_any_lexeme get_last_lexeme() const override;
    lex::opt_any_lexeme get_interest_lexeme() const override;

};

/**
 * Null literal type.
 *
 * Sentinel type representing the `null` literal.  It is implicitly convertible
 * to any nullable indirection type (pointer, view, owner) and can appear on
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
        UNSIGNED_BYTE,
        UNSIGNED_CHAR = UNSIGNED_BYTE,
        SHORT,
        UNSIGNED_SHORT,
        INT,
        UNSIGNED_INT,
        LONG,
        UNSIGNED_LONG,
        LONG_LONG,
        UNSIGNED_LONG_LONG,
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
 * Link type (+) — mutable, non-null (strong) indirection.
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
 * View type (?) — immutable (not rebindable after construction), nullable indirection.
 * Like a const pointer: can be null, but cannot be rebound after initialisation.
 * Represented in LLVM as an opaque pointer, identical to pointer_type.
 * Mangling modifier: 'Q'
 */
class view_type : public type {
protected:
    friend class type;

    view_type(const std::shared_ptr<type> &subtype);

public:
    bool is_resolved() const override;

    llvm::Type* get_llvm_type() const override;

    std::string to_string() const override;

    std::shared_ptr<type> get_viewed_type() const {return get_subtype();}
};

inline bool type::is_view(const std::shared_ptr<type>& type) {
    return std::dynamic_pointer_cast<view_type>(type) != nullptr;
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
 * Drain type (#) — immutable binding, non-null, drainable indirection.
 * Similar to a reference (&) but grants the consumer the permission to drain
 * (steal the internal resources of) the referenced object.  The consumer may
 * choose to drain or simply copy; if draining occurs the source object must be
 * left in a valid, reusable state (typically equivalent to default construction).
 * Represented in LLVM as an opaque pointer, identical to reference_type.
 * Mangling modifier: 'D'
 */
class drain_type : public type {
protected:
    friend class type;

    drain_type(const std::shared_ptr<type> &subtype);

public:
    bool is_resolved() const override;

    llvm::Type* get_llvm_type() const override;

    std::string to_string() const override;

    std::shared_ptr<type> get_drained_type() const { return get_subtype(); }
};

inline bool type::is_drain(const std::shared_ptr<type>& type) {
    return std::dynamic_pointer_cast<drain_type>(type) != nullptr;
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
    return is_reference(t) || is_pointer(t) || is_link(t) || is_view(t) || is_owner(t) || is_drain(t);
}

inline bool type::is_strong_indirection(const std::shared_ptr<type>& t) {
    return is_reference(t) || is_link(t) || is_drain(t);
}

inline bool type::is_mutable_indirection(const std::shared_ptr<type>& t) {
    return is_link(t) || is_pointer(t) || is_owner(t);
}

inline bool type::is_nullable_indirection(const std::shared_ptr<type>& t) {
    return is_pointer(t) || is_view(t) || is_owner(t);
}

inline bool type::is_immutable_indirection(const std::shared_ptr<type>& t) {
    return is_reference(t) || is_view(t) || is_drain(t);
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

    /**
     * Rebind the owning aggregate of this struct_type.
     *
     * Used when unifying the KDI-imported and locally-synthesised instantiations
     * of the same generic template into a single struct_type: the locally
     * synthesised concrete aggregate carries real method/constructor bodies, so
     * the shared struct_type must point to it (rather than the signature-only
     * imported aggregate) for code generation to emit those bodies.
     */
    void reassign_aggregate(std::weak_ptr<k::model::aggregate> st) { _struct = std::move(st); }

    inline fields_t::size_type fields_size()const {return _fields.size();}
    inline fields_t::const_iterator fields_begin()const {return _fields.begin();}
    inline fields_t::const_iterator fields_end()const {return _fields.end();}

    bool has_member(const std::string& name) const;
    std::optional<field> get_member(const std::string& name) const;

    llvm::Constant* generate_default_value_initializer() const override;

    lex::opt_any_lexeme get_first_lexeme() const override;
    lex::opt_any_lexeme get_last_lexeme() const override;
    lex::opt_any_lexeme get_interest_lexeme() const override;
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
 * Callable type — a first-class "something invocable with a fixed prototype".
 *
 * Runtime representation is a fat reference `%__k.callable = type { ptr, ptr }`:
 *   * field 0 `fn`  — address of the function to invoke;
 *   * field 1 `ctx` — the invocation context (`this`), or null for a free/static target.
 *
 * The addresser is a compile-time-only property (nullability / rebindability),
 * exactly like `T&` vs `T*`:
 *   * `none`      — bare prototype `(int):long`; not instantiable as a value type;
 *   * `pointer`   — `*(int):long`, nullable and rebindable;
 *   * `view`      — `?(int):long`, nullable, not rebindable;
 *   * `link`      — `+(int):long`, non-null, rebindable;
 *   * `reference` — `&(int):long`, non-null, not rebindable;
 *   * `owner`     — `!(int):long`, nullable, rebindable (owning closure).
 */
class callable_type : public type {
public:
    /** Addresser applied to the prototype. `none` = bare prototype (not instantiable). */
    enum class addresser { none, pointer, view, link, reference, owner };

protected:
    friend class type;
    friend class callable_type_builder;
    friend class context;

    /** Declared return type; nullptr means void. */
    std::shared_ptr<type> _return_type;
    std::vector<std::shared_ptr<type>> _parameter_types;
    /** Declared checked-exception set. Empty == throws nothing. */
    std::vector<std::shared_ptr<type>> _throws;
    addresser _addresser = addresser::pointer;

    callable_type() : type(nullptr) {}
    callable_type(const std::shared_ptr<type>& return_type,
                  const std::vector<std::shared_ptr<type>>& parameter_types,
                  addresser rk,
                  llvm::Type* llvm_type):
        type(llvm_type), _return_type(return_type), _parameter_types(parameter_types), _addresser(rk) {}

public:
    bool is_resolved() const override;
    llvm::Type* get_llvm_type() const override;
    llvm::Constant* generate_default_value_initializer() const override;
    std::string to_string() const override;

    const std::shared_ptr<type>& get_return_type() const { return _return_type; }
    void set_return_type(const std::shared_ptr<type>& rt) { _return_type = rt; }
    const std::vector<std::shared_ptr<type>>& get_parameter_types() const { return _parameter_types; }
    const std::vector<std::shared_ptr<type>>& get_throws() const { return _throws; }
    void set_throws(const std::vector<std::shared_ptr<type>>& t) { _throws = t; }
    addresser get_addresser() const { return _addresser; }

    /** A bare prototype has no addresser: it denotes a signature, not a value type. */
    bool is_prototype() const { return _addresser == addresser::none; }
    /** `*`, `?` and `!` callables may hold a null target. */
    bool is_nullable() const { return _addresser == addresser::pointer
                                   || _addresser == addresser::view
                                   || _addresser == addresser::owner; }
    /** `*`, `+` and `!` callables may be re-assigned after initialisation. */
    bool is_rebindable() const { return _addresser == addresser::pointer
                                     || _addresser == addresser::link
                                     || _addresser == addresser::owner; }
    /** An owned callable (`!`) owns its closure environment. */
    bool is_owner() const { return _addresser == addresser::owner; }
    /** An unbound member function reference (`T::*(int)`) is not a fat callable. */
    virtual bool is_unbound_member() const { return false; }

    /**
     * Build a callable that shares @p proto's addresser and LLVM representation but
     * carries the given prototype components. Used by template type substitution,
     * which has no `context` at hand; the resulting type is *not* interned.
     */
    static std::shared_ptr<callable_type> make_like(
        const callable_type& proto,
        const std::shared_ptr<type>& ret,
        const std::vector<std::shared_ptr<type>>& params,
        const std::vector<std::shared_ptr<type>>& throws);

    /** Nominal: same return type, parameter types and throws set (addresser ignored). */
    bool signature_equal(const callable_type& other) const;
    /** signature_equal() plus the same addresser. */
    bool structurally_equal(const callable_type& other) const;
};

/**
 * Member function reference type (unbound method pointer).
 *
 * Represents the type of a reference to a non-static member function.
 * The binding (which object to call it on) is done at the call site, following
 * the unified calling convention of K (implicit or explicit this).
 * Unlike a plain callable, this is NOT a fat reference: its LLVM representation is a
 * bare function pointer whose first parameter is a reference to the owner aggregate.
 */
class member_function_reference_type : public callable_type {
protected:
    friend class callable_type_builder;
    friend class context;
    friend class callable_type;

    std::shared_ptr<aggregate> _member_of;

    member_function_reference_type(const std::shared_ptr<aggregate>& member_of,
                                   const std::shared_ptr<type>& return_type,
                                   const std::vector<std::shared_ptr<type>>& parameter_types,
                                   addresser rk,
                                   llvm::Type* llvm_type):
        callable_type(return_type, parameter_types, rk, llvm_type),
        _member_of(member_of) {}

public:
    llvm::Type* get_llvm_type() const override;
    llvm::Constant* generate_default_value_initializer() const override;
    std::string to_string() const override;
    bool is_unbound_member() const override { return true; }

    const std::shared_ptr<aggregate>& get_member_of() const { return _member_of; }

    /** Structural equality (includes owner struct). */
    bool structurally_equal(const member_function_reference_type& other) const;
};


class callable_type_builder {
protected:
    std::shared_ptr<context> _context;
    std::shared_ptr<aggregate> _member_of;
    std::shared_ptr<type> _return_type;
    std::vector<std::shared_ptr<type>> _parameter_types;
    std::vector<std::shared_ptr<type>> _throws;
    callable_type::addresser _addresser = callable_type::addresser::pointer;
public:
    explicit callable_type_builder(const std::shared_ptr<context>& context);
    void member_of(const std::shared_ptr<structure>& st) { _member_of = std::static_pointer_cast<aggregate>(st); }
    /** Accept any aggregate (structure or klass) as member owner. */
    void member_of(const std::shared_ptr<aggregate>& agg) { _member_of = agg; }
    void return_type(const std::shared_ptr<type>& return_type) { _return_type = return_type; }
    void append_parameter_type(const std::shared_ptr<type>& param_type) { _parameter_types.push_back(param_type); }
    void addresser(callable_type::addresser rk) { _addresser = rk; }
    void throws(const std::vector<std::shared_ptr<type>>& t) { _throws = t; }
    std::shared_ptr<callable_type> build() const;
};


inline bool type::is_callable(const std::shared_ptr<type>& type) {
    return std::dynamic_pointer_cast<callable_type>(type) != nullptr;
}

/** True when @p t is a fat callable (excludes unbound member function references). */
inline bool type::is_fat_callable(const std::shared_ptr<type>& t) {
    auto c = std::dynamic_pointer_cast<callable_type>(remove_const(t));
    return c && !c->is_unbound_member();
}

/**
 * Unresolved function reference type.
 *
 * Placeholder created by context::from_type_specifier() when parsing a
 * callable_type_specifier.  Carries the raw parameter types (which may
 * themselves contain unresolved_type entries) and the optional owner name
 * (for member function pointers).  Resolved to callable_type or
 * member_function_reference_type by type_reference_resolver.
 */
class unresolved_callable_type : public type {
protected:
    friend class context;
    friend class gen::type_reference_resolver;

    k::name _owner_name;        ///< Empty for free functions.
    callable_type::addresser _addresser;
    std::vector<std::shared_ptr<type>> _parameter_types;
    /** Declared return type (may itself be unresolved); nullptr means void. */
    std::shared_ptr<type> _return_type;
    /** Declared checked-exception set (may contain unresolved entries). */
    std::vector<std::shared_ptr<type>> _throws;
    std::shared_ptr<type> _resolved;

    unresolved_callable_type(
        const k::name& owner_name,
        callable_type::addresser rk,
        const std::vector<std::shared_ptr<type>>& param_types,
        const std::shared_ptr<type>& return_type = nullptr,
        const std::vector<std::shared_ptr<type>>& throws = {})
        : _owner_name(owner_name), _addresser(rk), _parameter_types(param_types),
          _return_type(return_type), _throws(throws) {}

    void resolve(std::shared_ptr<type> res_type) { _resolved = res_type; }

public:
    /** Build a fresh unresolved callable placeholder (used by template substitution). */
    static std::shared_ptr<unresolved_callable_type> make_shared(
        const k::name& owner_name,
        callable_type::addresser addr,
        const std::vector<std::shared_ptr<type>>& param_types,
        const std::shared_ptr<type>& return_type,
        const std::vector<std::shared_ptr<type>>& throws);

    const k::name& owner_name() const { return _owner_name; }
    callable_type::addresser get_addresser() const { return _addresser; }
    const std::vector<std::shared_ptr<type>>& parameter_types() const { return _parameter_types; }
    const std::shared_ptr<type>& get_return_type() const { return _return_type; }
    const std::vector<std::shared_ptr<type>>& get_throws() const { return _throws; }

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
    friend class gen::aggregate_type_resolver;
    friend class gen::type_reference_resolver;
    friend class kdi_importer;

    std::weak_ptr<enumeration> _enumeration;
    std::shared_ptr<primitive_type> _underlying_type;

    enum_type(std::weak_ptr<enumeration> e, std::shared_ptr<primitive_type> underlying)
        : _enumeration(std::move(e)), _underlying_type(std::move(underlying)) {}

public:
    bool is_resolved() const override { return _underlying_type != nullptr; }

    std::shared_ptr<primitive_type> get_underlying_type() const { return _underlying_type; }
    std::shared_ptr<enumeration> get_enumeration() const { return _enumeration.lock(); }

    /** True when enum entries are represented as indices into a static backing table. */
    bool is_object_backed() const;
    /** Non-null for object-backed enums: the struct_type of the backing object. */
    std::shared_ptr<struct_type> get_object_type() const;

    llvm::Type* get_llvm_type() const override;
    llvm::Constant* generate_default_value_initializer() const override;
    std::string to_string() const override;

    lex::opt_any_lexeme get_first_lexeme() const override;
    lex::opt_any_lexeme get_last_lexeme() const override;
    lex::opt_any_lexeme get_interest_lexeme() const override;
};

inline bool type::is_enum(const std::shared_ptr<type>& t) {
    auto nc = remove_const(t);
    return std::dynamic_pointer_cast<enum_type>(nc) != nullptr;
}

/**
 * Strong alias (typedef) type.
 *
 * A typedef introduces a type that is nominally distinct from the type it
 * renames, while sharing exactly the same representation: in LLVM IR an
 * alias_type is indistinguishable from its underlying type, and it contributes
 * nothing to name mangling.
 *
 * Its whole purpose is to add a semantic layer on top of the type system —
 * for instance, expressing that two kinds of identifier are both 'int' but
 * denote different things.
 *
 * Conversions:
 *   - alias → underlying: implicit
 *   - underlying → alias: explicit cast required, except in a variable
 *     definition, for a compile-time constant, and within an expression that
 *     already involves the alias (see the tainting rules in the resolver).
 *
 * Nothing below the resolution layer may observe an alias_type: every consumer
 * reasoning about representation must go through type::canonical().
 */
class alias_type : public type {
protected:
    friend class context;
    friend class gen::symbol_resolver;
    friend class gen::aggregate_type_resolver;
    friend class gen::type_reference_resolver;
    friend class kdi_importer;

    std::weak_ptr<alias_definition> _alias;

    /** The renamed type. May itself be an alias_type (alias chain). */
    std::shared_ptr<type> _underlying;

    /** Fully-qualified K name of the alias, kept for diagnostics and KDI export. */
    std::string _fq_name;

    alias_type(std::weak_ptr<alias_definition> alias, std::shared_ptr<type> underlying,
               std::string fq_name)
        : _alias(std::move(alias)), _underlying(std::move(underlying)),
          _fq_name(std::move(fq_name)) {}

    void set_underlying(std::shared_ptr<type> underlying) { _underlying = std::move(underlying); }

public:
    bool is_resolved() const override { return _underlying && _underlying->is_resolved(); }

    bool is_primitive() const override { return _underlying && _underlying->is_primitive(); }

    std::shared_ptr<type> get_underlying() const { return _underlying; }
    std::shared_ptr<alias_definition> get_alias() const { return _alias.lock(); }
    const std::string& get_fq_name() const { return _fq_name; }

    llvm::Type* get_llvm_type() const override {
        return _underlying ? _underlying->get_llvm_type() : nullptr;
    }

    llvm::Constant* generate_default_value_initializer() const override {
        return _underlying ? _underlying->generate_default_value_initializer() : nullptr;
    }

    std::string to_string() const override;

    lex::opt_any_lexeme get_first_lexeme() const override;
    lex::opt_any_lexeme get_last_lexeme() const override;
    lex::opt_any_lexeme get_interest_lexeme() const override;
};

inline bool type::is_alias(const std::shared_ptr<type>& t) {
    auto nc = remove_const(t);
    return std::dynamic_pointer_cast<alias_type>(nc) != nullptr;
}


inline bool type::are_equal(const std::shared_ptr<type>& type1, const std::shared_ptr<type>& type2) {
    if (type1 == type2) return true;
    if (!type1 || !type2) return false;

    // Nominal equality for strong alias (typedef) types: two aliases are equal
    // only when they denote the same declaration, and an alias is never equal
    // to the type it renames. This is precisely what makes a typedef a
    // distinct type at the K level, while remaining representation-identical.
    {
        auto al1 = std::dynamic_pointer_cast<alias_type>(type1);
        auto al2 = std::dynamic_pointer_cast<alias_type>(type2);
        if (al1 || al2) {
            if (!al1 || !al2) return false;
            auto d1 = al1->get_alias();
            auto d2 = al2->get_alias();
            if (d1 && d2) return d1 == d2;
            return al1->get_fq_name() == al2->get_fq_name();
        }
    }

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

    // Structural equality for view types (?): compare viewed types recursively
    if (auto v1 = std::dynamic_pointer_cast<view_type>(type1)) {
        auto v2 = std::dynamic_pointer_cast<view_type>(type2);
        return v2 && are_equal(v1->get_subtype(), v2->get_subtype());
    }

    // Structural equality for pointer types (*): compare pointed types recursively
    if (auto p1 = std::dynamic_pointer_cast<pointer_type>(type1)) {
        auto p2 = std::dynamic_pointer_cast<pointer_type>(type2);
        return p2 && are_equal(p1->get_subtype(), p2->get_subtype());
    }

    // Structural equality for link types (+): compare linked types recursively
    if (auto l1 = std::dynamic_pointer_cast<link_type>(type1)) {
        auto l2 = std::dynamic_pointer_cast<link_type>(type2);
        return l2 && are_equal(l1->get_subtype(), l2->get_subtype());
    }

    // Structural equality for drain types: compare drained types recursively
    if (auto d1 = std::dynamic_pointer_cast<drain_type>(type1)) {
        auto d2 = std::dynamic_pointer_cast<drain_type>(type2);
        return d2 && are_equal(d1->get_subtype(), d2->get_subtype());
    }

    // Structural equality for const types: compare inner types recursively
    if (auto c1 = std::dynamic_pointer_cast<const_type>(type1)) {
        auto c2 = std::dynamic_pointer_cast<const_type>(type2);
        return c2 && are_equal(c1->get_inner_type(), c2->get_inner_type());
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

    // Nominal equality for struct/aggregate types: same underlying aggregate
    if (auto s1 = std::dynamic_pointer_cast<struct_type>(type1)) {
        auto s2 = std::dynamic_pointer_cast<struct_type>(type2);
        return s2 && s1->get_struct() == s2->get_struct();
    }

    // Structural equality for function reference types
    if (auto f1 = std::dynamic_pointer_cast<member_function_reference_type>(type1)) {
        if (auto f2 = std::dynamic_pointer_cast<member_function_reference_type>(type2)) {
            return f1->structurally_equal(*f2);
        }
        return false;
    }
    if (auto f1 = std::dynamic_pointer_cast<callable_type>(type1)) {
        if (auto f2 = std::dynamic_pointer_cast<callable_type>(type2)) {
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

inline bool type::are_layout_equal(const std::shared_ptr<type>& type1, const std::shared_ptr<type>& type2) {
    return are_equal(canonical(type1), canonical(type2));
}

// ═══════════════════════════════════════════════════════════════════════════
// Type substitution support (for template instantiation at model level)
// ═══════════════════════════════════════════════════════════════════════════

/** Map from template parameter name to concrete type. */
using type_substitution_map = std::unordered_map<std::string, std::shared_ptr<type>>;

/**
 * A bound template value argument: the concrete underlying primitive value,
 * plus the value parameter's originally declared type (used to preserve
 * strong typing — e.g. an enum type — on the `value_expression` created
 * when substituting the value parameter into the template body).
 * `declared_type` may be null when the declared type is not available/needed
 * (in which case the substituted expression keeps the raw primitive type of
 * the underlying value).
 */
struct value_param_binding {
    k::value_type value;
    std::shared_ptr<type> declared_type;
};

/** Map from template value parameter name to its bound concrete value. */
using value_substitution_map = std::unordered_map<std::string, value_param_binding>;

/** Map from template pack parameter name to a list of concrete types. */
using pack_substitution_map = std::unordered_map<std::string, std::vector<std::shared_ptr<type>>>;

/**
 * Recursively substitute types through wrapper chains.
 *
 * If \p t is an unresolved_type whose name matches a key in \p subst,
 * the concrete type from the map is returned.  For wrapper types
 * (pointer, reference, const, owner, view, link, drain, array) the
 * inner type is substituted and the wrapper is rebuilt on the new inner
 * type.  All other types are returned unchanged.
 */
std::shared_ptr<type> substitute_type(
    const std::shared_ptr<type>& t,
    const type_substitution_map& subst);

} // namespace k::model
#endif //KLANG_TYPE_HPP
