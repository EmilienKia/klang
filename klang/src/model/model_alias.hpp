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
#ifndef KLANG_MODEL_ALIAS_HPP
#define KLANG_MODEL_ALIAS_HPP
#include "model_element.hpp"
#include "template.hpp"
namespace k::model {

/**
 * An exported aliasing declaration: 'alias' (soft) or 'typedef' (strong).
 *
 *     alias   Name : aliased_symbol;
 *     typedef Name : aliased_type;
 *
 * Unlike a using directive, an alias declared at unit, namespace or aggregate
 * level is a first-class named element: it is exported through the KDI and is
 * therefore visible to importing modules.  Declared inside a statement block,
 * it is implicitly private and restricted to that block.
 *
 * An alias declares nothing new at code-generation time: it is always replaced
 * by the real underlying entity, recursively, until an entity that is not an
 * alias is reached.  No symbol is ever synthesised for an alias.
 *
 * Soft alias ('alias')
 *   A pure convenience renaming of any symbol — a type, a function, or a
 *   global/static variable.  It is fully transparent: the alias name and the
 *   aliased symbol are interchangeable in both directions.  Namespaces cannot
 *   be aliased; 'using N = namespace X::Y;' remains the only way to do that.
 *
 * Strong alias ('typedef')
 *   Introduces a nominally distinct type over an identical representation, so
 *   that a semantic layer can be added on top of the type system (for instance,
 *   two kinds of identifier that are both 'int' but denote different things).
 *   The nominal identity is carried by the alias_type built for it.
 */
class alias_definition : public element, public named_element {
public:
    /** Soft ('alias') versus strong ('typedef') aliasing. */
    enum class kind_t { SOFT, STRONG };

    /** What kind of entity the alias targets. Namespaces are excluded by design. */
    enum class target_kind_t { UNKNOWN, TYPE, FUNCTION, VARIABLE };

protected:
    friend class ns;
    friend class aggregate;
    friend class block;
    friend class unit;
    friend class model_builder;
    friend class kdi_importer;
    friend class gen::scope_lookup;
    friend class gen::symbol_resolver;
    friend class gen::aggregate_type_resolver;
    friend class gen::type_reference_resolver;

    kind_t _kind = kind_t::SOFT;
    visibility _visibility = PUBLIC;

    /** True when declared inside a statement block: implicitly private, never exported. */
    bool _block_local = false;

    /** True when imported from an external KDI: never re-exported. */
    bool _imported = false;

    /** The aliased entity name, exactly as written. */
    k::name _target_name;

    target_kind_t _target_kind = target_kind_t::UNKNOWN;

    /**
     * Aliased type. Always set for a strong alias; set for a soft alias whose
     * target turned out to be a type.
     */
    std::shared_ptr<type> _target_type;

    /** Aliased element, for a soft alias targeting a function or a variable. */
    std::weak_ptr<element> _target_element;

    /** Nominal type object, for a strong alias only. */
    std::shared_ptr<alias_type> _alias_type;

    /** True once the target has been resolved. */
    bool _resolved = false;

    /** True while the target is being resolved — guards against alias cycles. */
    bool _resolving = false;

    /**
     * Template parameters of a parameterised alias; null for a plain one.
     *
     * A parameterised alias renames a whole family of types:
     *
     *     template<typename T> alias   Vec : Array<T, 16>;
     *     template<typename T> typedef Id  : T;
     *
     * It is never instantiated into an entity of its own: a use such as
     * 'Vec<int>' substitutes the arguments into the target type and resolves
     * the result. Only the nominal identity of a strong parameterised alias is
     * materialised, one alias_type per distinct argument list.
     */
    std::unique_ptr<tpl_info> _tpl_info;

    /**
     * Nominal types of a strong parameterised alias, keyed by the textual form
     * of the substituted argument list (e.g. "int32"). Each distinct argument
     * list yields a distinct nominal type, exactly like a distinct typedef.
     */
    std::map<std::string, std::shared_ptr<alias_type>> _tpl_alias_types;

    /** Declaration lexeme, kept for diagnostics (alias_decl cannot expose its ast_node). */
    lex::opt_any_lexeme _decl_lexeme;

    alias_definition(std::shared_ptr<element> parent) : element(parent) {}

    void update_mangled_name() override;

public:
    static std::shared_ptr<alias_definition> make_shared(std::shared_ptr<element> parent,
                                                         const std::string& name,
                                                         kind_t kind);

    void accept(model_visitor& visitor) override;

    /** True when this alias is parameterised by a 'template<...>' clause. */
    bool is_template() const { return _tpl_info != nullptr; }

    tpl_info* get_tpl_info() const { return _tpl_info.get(); }
    void set_tpl_info(std::unique_ptr<tpl_info> info) { _tpl_info = std::move(info); }

    /** Look up the nominal type already built for an argument list, if any. */
    std::shared_ptr<alias_type> get_tpl_alias_type(const std::string& args_key) const {
        auto it = _tpl_alias_types.find(args_key);
        return it != _tpl_alias_types.end() ? it->second : nullptr;
    }

    /** Register the nominal type built for an argument list. */
    void set_tpl_alias_type(const std::string& args_key, std::shared_ptr<alias_type> t) {
        _tpl_alias_types[args_key] = std::move(t);
    }

    kind_t get_kind() const { return _kind; }
    bool is_soft() const { return _kind == kind_t::SOFT; }
    bool is_strong() const { return _kind == kind_t::STRONG; }

    visibility get_visibility() const { return _visibility; }
    void set_visibility(visibility vis) { _visibility = vis; }

    bool is_block_local() const { return _block_local; }
    void set_block_local(bool v = true) { _block_local = v; }

    bool is_imported() const { return _imported; }
    void set_imported(bool v = true) { _imported = v; }

    /** True when this alias is part of the module's exported interface. */
    bool is_exported() const {
        return !_block_local && _visibility != PRIVATE;
    }

    const k::name& get_target_name() const { return _target_name; }
    void set_target_name(k::name target_name) { _target_name = std::move(target_name); }

    target_kind_t get_target_kind() const { return _target_kind; }

    std::shared_ptr<type> get_target_type() const { return _target_type; }
    void set_target_type(std::shared_ptr<type> target_type) { _target_type = std::move(target_type); }
    std::shared_ptr<element> get_target_element() const { return _target_element.lock(); }

    /** The nominal type of a strong alias; null for a soft alias. */
    std::shared_ptr<alias_type> get_alias_type() const { return _alias_type; }

    /**
     * The type this alias denotes: the nominal alias_type for a strong alias,
     * the aliased type itself for a soft alias. Null when the alias does not
     * target a type (or is not resolved yet).
     */
    std::shared_ptr<type> get_declared_type() const;

    bool is_resolved() const { return _resolved; }

    const lex::opt_any_lexeme& get_decl_lexeme() const { return _decl_lexeme; }
    void set_decl_lexeme(lex::opt_any_lexeme lexeme) { _decl_lexeme = std::move(lexeme); }

    void set_ast_alias_decl(std::shared_ptr<k::parse::ast::alias_decl> ast);
    std::shared_ptr<k::parse::ast::alias_decl> get_ast_alias_decl() const;

    lex::opt_any_lexeme get_first_lexeme() const override {
        if (auto lex = element::get_first_lexeme()) return lex;
        return _decl_lexeme;
    }
    lex::opt_any_lexeme get_interest_lexeme() const override {
        if (auto lex = element::get_interest_lexeme()) return lex;
        return _decl_lexeme;
    }
};


/**
 * Interface for scopes that can hold alias/typedef declarations.
 *
 * Mixed into ns, aggregate, block and for_statement — every scope where an
 * alias declaration may appear.
 */
class alias_holder
{
public:
    void add_alias(std::shared_ptr<alias_definition> alias) {
        if (!alias) return;
        _alias_index[alias->get_short_name()] = alias;
        _aliases.push_back(std::move(alias));
    }

    std::shared_ptr<alias_definition> get_alias(const std::string& name) const {
        auto it = _alias_index.find(name);
        return it != _alias_index.end() ? it->second : nullptr;
    }

    const std::vector<std::shared_ptr<alias_definition>>& get_aliases() const {
        return _aliases;
    }

protected:
    std::vector<std::shared_ptr<alias_definition>> _aliases;
    std::map<std::string, std::shared_ptr<alias_definition>> _alias_index;
};

} // namespace k::model
#endif //KLANG_MODEL_ALIAS_HPP
