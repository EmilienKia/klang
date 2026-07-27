/*
 * K Language compiler
 *
 * Copyright 2023-2026 Emilien Kia
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
#ifndef KLANG_MODEL_ENUM_HPP
#define KLANG_MODEL_ENUM_HPP
#include "model_element.hpp"
namespace k::model {
struct enum_entry_def {
    std::string name;
    int64_t value = 0;
    bool is_default = false;
    /** For object-backed enums: the brace-initializer AST node for this entry. */
    std::shared_ptr<k::parse::ast::brace_init_list> brace_init;
    /** For object-backed enums: constructor-style args for `ENTRY(...)` initializers. */
    std::vector<std::shared_ptr<expression>> ctor_args;
    /** For object-backed enums: true if this entry is implicitly incremented from the previous one. */
    bool is_implicit_increment = false;
    /** True when this entry aliases another entry (shares its backing-table slot). */
    bool is_alias = false;
};

/**
 * A raw (unresolved) entry in an enumeration definition.
 * Stored during model building; resolved during the symbol resolution phase.
 */
struct enum_raw_entry_def {
    std::string name;
    std::optional<int64_t> explicit_value;  ///< Set if entry has an integer literal value.
    std::string ref_name;                    ///< Set if entry references another entry by name.
    bool is_default = false;
    /** Brace-initializer for object-backed enum entries (e.g. `ENTRY{.x=1, .y=2}`). */
    std::shared_ptr<k::parse::ast::brace_init_list> brace_init;
    /** Constructor-style args for object-backed enum entries (e.g. `ENTRY(1, 2)`). */
    std::vector<std::shared_ptr<expression>> ctor_args;
};

/**
 * Enumeration: a named set of integer-valued constants.
 *
 * An enumeration is a nominal type backed by the smallest primitive integer
 * type that can hold all declared values. Each entry maps a name to a
 * compile-time constant integer value.
 *
 * Enumerations support single inheritance: a derived enum inherits all entries
 * from its base and may add new ones. Multi-level inheritance (A : B : C) is
 * supported. Cycles are detected and rejected.
 */
class enumeration : public element, public named_element {
protected:
    friend class ns;
    friend class aggregate;
    friend class union_type_def;
    friend class unit;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;

    std::vector<enum_entry_def> _entries;
    std::shared_ptr<enum_type> _type;
    std::shared_ptr<primitive_type> _underlying_type;
    /**
     * Explicit underlying primitive type requested via 'enum X : <primitive>'
     * (e.g. 'enum X : long'). Set by model_builder when the ':' type spec is a
     * primitive keyword rather than a base enum or object type name. When set,
     * symbol_resolver uses it verbatim as _underlying_type instead of computing
     * the smallest type that fits the declared entry values.
     */
    std::shared_ptr<primitive_type> _explicit_underlying_type;
    /** Non-null for object-backed typed enums: the struct_type of the backing object. */
    std::shared_ptr<struct_type> _object_type;
    /** LLVM global constant array for object-backed typed enums: `[N x StructType]`. */
    llvm::GlobalVariable* _table_global = nullptr;
    /** Canonical LLVM symbol name for the object-backed enum table. */
    std::string _table_symbol;
    visibility _visibility = PUBLIC;

    /** Optional base enum name (unresolved, from AST). */
    std::optional<std::string> _base_name;
    /** Resolved base enumeration (set during symbol resolution). */
    std::shared_ptr<enumeration> _base;
    /** Raw (unresolved) entries from AST — used for deferred resolution of derived enums. */
    std::vector<enum_raw_entry_def> _raw_entries;
    /** True when entry values, underlying type and enum_type have been fully resolved. */
    bool _resolved = false;
    /** True while this enum is being resolved (for cycle detection). */
    bool _resolving = false;

    enumeration(std::shared_ptr<element> parent)
        : element(parent) {}

    static std::shared_ptr<enumeration> make_shared(std::shared_ptr<element> parent, const std::string& name);

    void update_mangled_name() override;

public:
    void accept(model_visitor& visitor) override;

    const std::vector<enum_entry_def>& entries() const { return _entries; }
    void add_entry(const std::string& name, int64_t value, bool is_default,
                   std::shared_ptr<k::parse::ast::brace_init_list> brace_init = nullptr,
                   std::vector<std::shared_ptr<expression>> ctor_args = {},
                   bool is_implicit_increment = false,
                   bool is_alias = false) {
        _entries.push_back({name, value, is_default, std::move(brace_init), std::move(ctor_args), is_implicit_increment, is_alias});
    }

    std::optional<enum_entry_def> get_entry_by_name(const std::string& name) const {
        for (auto& e : _entries) {
            if (e.name == name) return e;
        }
        return std::nullopt;
    }

    enum_entry_def get_default_entry() const {
        for (auto& e : _entries) {
            if (e.is_default) return e;
        }
        // Fallback: first entry (should always exist)
        return _entries.front();
    }

    std::shared_ptr<enum_type> get_enum_type() const { return _type; }
    void set_enum_type(std::shared_ptr<enum_type> t) { _type = t; }

    std::shared_ptr<primitive_type> get_underlying_type() const { return _underlying_type; }
    void set_underlying_type(std::shared_ptr<primitive_type> t) { _underlying_type = t; }

    /** Explicit underlying type from 'enum X : <primitive>', if any (see field doc above). */
    std::shared_ptr<primitive_type> get_explicit_underlying_type() const { return _explicit_underlying_type; }
    void set_explicit_underlying_type(std::shared_ptr<primitive_type> t) { _explicit_underlying_type = std::move(t); }

    /** True when enum entries are represented as indices into a static backing table. */
    bool is_object_backed() const { return _object_type != nullptr; }
    std::shared_ptr<struct_type> get_object_type() const { return _object_type; }
    void set_object_type(std::shared_ptr<struct_type> st) { _object_type = std::move(st); }

    /** LLVM global constant array used at runtime for object-backed enums. */
    llvm::GlobalVariable* get_table_global() const { return _table_global; }
    void set_table_global(llvm::GlobalVariable* gv) {
        _table_global = gv;
        if (gv) _table_symbol = gv->getName().str();
    }
    const std::string& get_table_symbol() const { return _table_symbol; }
    void set_table_symbol(std::string symbol) { _table_symbol = std::move(symbol); }

    visibility get_visibility() const { return _visibility; }
    void set_visibility(visibility v) { _visibility = v; }

    // ── Derivation support ──

    void set_base_name(const std::string& name) { _base_name = name; }
    const std::optional<std::string>& get_base_name() const { return _base_name; }

    void set_base(std::shared_ptr<enumeration> base) { _base = base; }
    std::shared_ptr<enumeration> get_base() const { return _base; }
    bool has_base() const { return _base != nullptr; }

    /** Check whether this enum is derived (directly or transitively) from `other`. */
    bool is_derived_from(const std::shared_ptr<enumeration>& other) const {
        for (auto b = _base; b; b = b->_base) {
            if (b == other) return true;
        }
        return false;
    }

    const std::vector<enum_raw_entry_def>& raw_entries() const { return _raw_entries; }
    void add_raw_entry(const enum_raw_entry_def& e) { _raw_entries.push_back(e); }

    bool is_resolved() const { return _resolved; }
    void set_resolved(bool v) { _resolved = v; }

    // ── AST node accessors ──
    void set_ast_enum_decl(std::shared_ptr<k::parse::ast::enum_decl> ast) {
        _ast_node = std::static_pointer_cast<k::parse::ast::ast_node>(std::move(ast));
    }
    std::shared_ptr<k::parse::ast::enum_decl> get_ast_enum_decl() const {
        return get_ast_node_as<k::parse::ast::enum_decl>();
    }
};


} // namespace k::model

#endif //KLANG_MODEL_ENUM_HPP
