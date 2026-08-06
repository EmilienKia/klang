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

    /** Declaration lexeme, kept for diagnostics (alias_decl cannot expose its ast_node). */
    lex::opt_any_lexeme _decl_lexeme;

    alias_definition(std::shared_ptr<element> parent) : element(parent) {}

    void update_mangled_name() override;

public:
    static std::shared_ptr<alias_definition> make_shared(std::shared_ptr<element> parent,
                                                         const std::string& name,
                                                         kind_t kind);

    void accept(model_visitor& visitor) override;

    kind_t get_kind() const { return _kind; }
    bool is_soft() const { return _kind == kind_t::SOFT; }
    bool is_strong() const { return _kind == kind_t::STRONG; }

    visibility get_visibility() const { return _visibility; }
    void set_visibility(visibility vis) { _visibility = vis; }

    bool is_block_local() const { return _block_local; }
    void set_block_local(bool v = true) { _block_local = v; }

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
