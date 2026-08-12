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
#ifndef KLANG_MODEL_NS_HPP
#define KLANG_MODEL_NS_HPP
#include "model_function.hpp"
namespace k::model {

class global_variable_definition : public element, public variable_definition {
protected:

    friend class ns;
    friend class aggregate;
    friend class block;
    friend class gen::implementation_generator;

    /** Declared visibility of this global/static variable. PUBLIC by default. */
    visibility _visibility = PUBLIC;

    global_variable_definition(std::shared_ptr<variable_holder> parent);

    static std::shared_ptr<global_variable_definition> make_shared(std::shared_ptr<variable_holder> parent, const std::string& name);

    void update_mangled_name() override;

public:
    void accept(model_visitor& visitor) override;

    visibility get_visibility() const { return _visibility; }
    void set_visibility(visibility v) { _visibility = v; }
};


class ns : public element, public named_element, public variable_holder, public function_holder, public aggregate_holder, public enum_holder, public union_holder, public using_holder, public alias_holder {
protected:

    friend class unit;

    /** Collection of all children of this namespace. */
    std::vector<std::shared_ptr</*ns_element*/element>> _children;

    /** Map of direct child namespaces. */
    std::map<std::string, std::shared_ptr<ns>> _ns;

    ns(std::shared_ptr<element> parent):
        element(parent) {}

    static std::shared_ptr<ns> make_shared(std::shared_ptr<element> parent, const std::string& name);

    std::shared_ptr<variable_definition> do_create_variable(const std::string &name, bool is_static) override;
    void on_variable_defined(std::shared_ptr<variable_definition>) override;

    std::shared_ptr<function> do_create_function(const std::string &name, bool is_static) override;
    void on_function_defined(std::shared_ptr<function> func) override;
    void on_function_removed(const std::shared_ptr<function>& func) override;

    std::shared_ptr<structure> do_create_structure(const std::string &name) override;
    std::shared_ptr<klass> do_create_class(const std::string &name) override;
    std::shared_ptr<interface> do_create_interface(const std::string &name) override;
    std::shared_ptr<annotation_type> do_create_annotation(const std::string &name) override;
    void on_aggregate_defined(std::shared_ptr<aggregate>) override;

    std::shared_ptr<enumeration> do_create_enum(const std::string &name) override;
    void on_enum_defined(std::shared_ptr<enumeration>) override;

    std::shared_ptr<union_type_def> do_create_union(const std::string &name) override;
    void on_union_defined(std::shared_ptr<union_type_def>) override;

    void update_mangled_name() override;
public:

    void accept(model_visitor& visitor) override;

    //
    // This namespace manipulations
    //

    /**
     * Test if this namespace is the root namespace.
     * @return True if root namespace, false otherwise.
     */
    bool is_root() const { return !!parent<unit>(); }

    //
    // Children namespace manipulations
    //

    /**
     * Retrieve the direct child namespace of given name, creating it if not found.
     * @param child_name Child namespace name to look for.
     * @return The child namespace.
     */
    std::shared_ptr<ns> get_child_namespace(const std::string& child_name);

    /**
     * Retrieve the direct child namespace of given name.
     * @param child_name Child namespace name to look for.
     * @return The child namespace, null if not found.
     */
    std::shared_ptr<const ns> get_child_namespace(const std::string& child_name)const;

    //
    // Children functions
    //

    const std::vector<std::shared_ptr</*ns_element*/element>>& get_children() const {
        return _children;
    }
};



class unit : public element {
protected:
    friend class element;
    /** Analysis context */
    std::shared_ptr<context> _context;

    /** Unit name */
    name _unit_name;

    /** Root namespace.*/
    std::shared_ptr<ns> _root_ns;

    std::shared_ptr<global_constructor_function> _global_constructor_func;
    std::shared_ptr<global_destructor_function> _global_destructor_func;

    /** Declared imports (populated by model_builder, resolved by kdi_importer). */
    std::vector<imported_module> _imported_modules;

    /**
     * KDI files loaded as transitive dependencies (not direct imports of this unit).
     * Populated by kdi_importer::import_all() to allow find_imported_type() to
     * resolve types from indirectly-imported modules (e.g. base classes).
     */
    std::vector<std::shared_ptr<kdi::kdi_file>> _transitive_kdis;

    /**
     * Cache of imported_function model nodes keyed by mangled name (C1 for ctors).
     * Created lazily by get_or_create_imported_function().
     */
    std::unordered_map<std::string, std::shared_ptr<imported_function>>  _imported_functions;

    /**
     * Cache of imported_aggregate model nodes keyed by fully-qualified K name.
     * Created lazily by get_or_create_imported_aggregate().
     */
    std::unordered_map<std::string, std::shared_ptr<imported_aggregate>> _imported_aggregates;

    /**
     * Cache of imported_variable model nodes keyed by mangled name.
     * Created lazily by get_or_create_imported_variable().
     */
    std::unordered_map<std::string, std::shared_ptr<imported_variable>>  _imported_variables;

    /**
     * Cache of imported enumeration model nodes keyed by fully-qualified K name.
     * Created lazily by get_or_create_imported_enum().
     */
    std::unordered_map<std::string, std::shared_ptr<enumeration>>        _imported_enums;

    /**
     * Registry of struct_types for template instantiations, keyed by an
     * origin-namespace–qualified instantiation key (e.g. "k::Optional__byte"),
     * built by make_instantiation_registry_key().
     *
     * Both the KDI importer (get_or_create_imported_aggregate) and the local
     * template instantiator (try_instantiate_template_type) consult this registry so
     * that the same instantiation always maps to a single struct_type object.
     * Pointer-based struct-identity checks in overload/cast resolution then treat
     * imported and locally-synthesised instantiations as the same type.
     *
     * The key is qualified by the originating module's namespace so that two
     * same-named templates imported from different namespaces (e.g. a::Optional and
     * b::Optional) never collide to a single struct_type. The importer derives the
     * namespace from the instantiation's fully-qualified KDI name; the local
     * instantiator derives it from the template's origin tag (tpl_info::
     * origin_module_ns_fq) or, for locally-declared templates, its enclosing
     * namespace.
     */
    std::unordered_map<std::string, std::weak_ptr<struct_type>> _instantiation_struct_types;

    std::shared_ptr<global_main_function> _global_main_func;

    /**
     * The Application class used as entry point for this unit.
     * - nullptr  → no main function / library module
     * - non-null → either the synthesized Application class (Phase 2b) or the
     *              user-defined one (Phase 3).
     */
    std::shared_ptr<klass> _application_class;

    /**
     * True when _application_class was synthesised by the compiler (Phase 2b)
     * rather than written by the user (Phase 3+).
     */
    bool _application_class_synthesized = false;

    /**
     * Global variable holding the single Application instance.
     * Declared as private in the unit's root namespace.
     */
    std::shared_ptr<global_variable_definition> _app_instance_var;

    /**
     * Phase 4: the topmost declared `main` overload in the `::k::Application`
     * abstract-class chain that decides the application's entry-point
     * signature (see gen_unit.cpp Pre-pass 1b).
     * - nullptr                      → no chain / not applicable (Phase 2-3
     *                                  behaviour: `_application_class`'s own
     *                                  single usable `main` is used directly).
     * - non-null, _application_entry_main_is_virtual == false
     *                                → same as Phase 2-3 (single-level case).
     * - non-null, _application_entry_main_is_virtual == true
     *                                → the C-ABI main proxy must call this
     *                                  function through virtual dispatch, so
     *                                  the call cascades through however many
     *                                  delegating overrides exist down to the
     *                                  final concrete Application's override.
     */
    std::shared_ptr<function> _application_entry_main;

    /** See _application_entry_main. */
    bool _application_entry_main_is_virtual = false;


    /**
     * Source objects for re-parsed template definitions imported from KDI.
     * Lexer tokens in template model elements hold string_views into these
     * buffers, so they must stay alive for the unit's lifetime.
     */
    std::vector<std::unique_ptr<k::source>> _imported_template_sources;

    friend class k::model::gen::symbol_resolver;
    friend class k::model::gen::aggregate_type_resolver;
    friend class k::model::gen::model_materializer;
    friend class k::model::gen::type_reference_resolver;
    friend class k::model::gen::declaration_generator;
    friend class k::model::gen::implementation_generator;
    friend class k::model::gen::init_order_resolver;
    friend class kdi_importer;

    global_constructor_function& get_global_constructor_function() {return *_global_constructor_func;}
    global_destructor_function& get_global_destructor_function() {return *_global_destructor_func;}

    std::shared_ptr<global_main_function> generate_main_function(std::shared_ptr<function> func);


    unit() = delete;
    unit(std::shared_ptr<context> context);
public:

    static std::shared_ptr<unit> create(std::shared_ptr<context> context);

    void accept(model_visitor& visitor) override;

    /**
     * Get the model name.
     * @return Unit name identifier
     */
    name get_unit_name() const {
        return _unit_name;
    }

    /**
     * Set the model name
     * @param unit_name New model name
     */
    void set_unit_name(const name& unit_name);

    //
    // Imports
    //

    /**
     * Register an import declaration (called by model_builder).
     * @param module_name  Qualified name of the module to import.
     * @param lexeme       Best-effort source location of the import declaration
     *                     (used to report precise diagnostics, e.g. circular
     *                     imports or missing KDI files).
     */
    void add_import(const k::name& module_name, const lex::opt_any_lexeme& lexeme = std::nullopt);

    /**
     * Read-only access to all declared imports.
     */
    const std::vector<imported_module>& get_imports() const {
        return _imported_modules;
    }

    /**
     * Mutable access to all declared imports (used by kdi_importer to fill
     * in resolved_kdi_path, kdi and used fields).
     */
    std::vector<imported_module>& get_imports() {
        return _imported_modules;
    }

    /**
     * Find an import by module name.
     * @return Pointer to the matching entry, or nullptr if not found.
     */
    imported_module* find_import(const k::name& module_name);
    const imported_module* find_import(const k::name& module_name) const;

    /**
     * Register a KDI file loaded as a transitive dependency (not a direct
     * import of this unit).  Called by kdi_importer so that find_imported_type()
     * and friends can resolve symbols from indirectly-imported modules.
     */
    void add_transitive_kdi(std::shared_ptr<kdi::kdi_file> kdi_ptr) {
        if (kdi_ptr) _transitive_kdis.push_back(std::move(kdi_ptr));
    }

    /** Read-only access to the list of transitive KDIs. */
    const std::vector<std::shared_ptr<kdi::kdi_file>>& get_transitive_kdis() const {
        return _transitive_kdis;
    }

    // ── Cross-module symbol lookup ──────────────────────────────────────────
    //
    // These methods search ALL loaded imports for a symbol identified by its
    // qualified K name (e.g. name{"math", "vec", "dot"}).
    //
    // When a match is found, the owning imported_module is marked used=true.
    // The current unit's own namespace is NOT searched here — callers are
    // expected to try local resolution first.
    //
    // Return nullptr when no import contains the requested symbol.

    /**
     * Find a global/namespace-level function in any loaded import.
     * @param name  Qualified name of the function (without root prefix).
     * @return Pointer into the kdi_file's kdi_function entry, or nullptr.
     */
    const kdi::kdi_function*
    find_imported_function(const k::name& name);

    /**
     * Find all global/namespace-level overloads matching @p name in loaded imports.
     * @param name  Qualified name of the function (without root prefix).
     * @return All matching KDI function entries, de-duplicated by mangled name.
     */
    std::vector<const kdi::kdi_function*>
    find_imported_functions(const k::name& name);

    /**
     * Find a global/static variable in any loaded import.
     * @param name  Qualified name of the variable (without root prefix).
     * @return Pointer into the kdi_file's kdi_variable entry, or nullptr.
     */
    const kdi::kdi_variable*
    find_imported_variable(const k::name& name);

    /**
     * Find an aggregate type (struct/class/interface) in any loaded import.
     * @param name  Qualified name of the aggregate (without root prefix).
     * @return Pointer into the kdi_file's kdi_aggregate entry, or nullptr.
     */
    const kdi::kdi_aggregate*
    find_imported_type(const k::name& name);

    /**
     * Find an enum type in any loaded import.
     * @param name  Qualified name of the enum (without root prefix).
     * @return Pointer into the kdi_file's kdi_enum entry, or nullptr.
     */
    const kdi::kdi_enum*
    find_imported_enum(const k::name& name);

    // ── Imported model-node factory methods ─────────────────────────────────
    //
    // Each method returns (or retrieves from cache) a fully-built model node
    // for the corresponding KDI descriptor.  Signatures and types are resolved
    // using kdi_type_to_model_type(); the nodes have no body / initialiser.
    // All side-effects (struct_type registration in context, marking
    // imported_module::used) happen here.

    /**
     * Return (or create) the imported_function model node for @p kdi_fn.
     * Keyed by mangled_name.  Populates return type and parameter list.
     */
    std::shared_ptr<imported_function>
    get_or_create_imported_function(const kdi::kdi_function* kdi_fn,
                                    std::shared_ptr<context> ctx);

    /**
     * Return (or create) the imported_aggregate model node for the aggregate
     * identified by its fully-qualified K name @p fq_name.
     *
     * Searches all loaded imports; builds the LLVM StructType from the KDI
     * layout; materialises all public/protected members, methods (as
     * imported_method), constructors (as imported_constructor) and destructor
     * (as imported_destructor).
     */
    std::shared_ptr<imported_aggregate>
    get_or_create_imported_aggregate(const k::name& fq_name,
                                     std::shared_ptr<context> ctx);

    /**
     * Look up the unified struct_type for a template instantiation by its
     * origin-qualified registry key (see make_instantiation_registry_key).
     * @return the shared struct_type, or nullptr if none registered yet.
     */
    std::shared_ptr<struct_type>
    find_instantiation_struct_type(const std::string& key) const {
        auto it = _instantiation_struct_types.find(key);
        if (it == _instantiation_struct_types.end()) return nullptr;
        return it->second.lock();
    }

    /**
     * Register the unified struct_type for a template instantiation under its
     * origin-qualified registry key (see make_instantiation_registry_key).
     */
    void register_instantiation_struct_type(const std::string& key,
                                            const std::shared_ptr<struct_type>& st) {
        if (st) _instantiation_struct_types[key] = st;
    }

    /**
     * Compose the canonical key for the template-instantiation struct_type registry.
     *
     * Qualifies the mangled short instantiation name (e.g. "Optional__byte") with the
     * originating module's namespace (e.g. "k") so that same-named templates from
     * different namespaces map to distinct struct_types. Any leading root prefix
     * ("::") on @p origin_ns_fq is stripped. Returns "<origin>::<short>", or just
     * "<short>" when @p origin_ns_fq is empty (template declared in this unit with no
     * enclosing namespace).
     */
    static std::string make_instantiation_registry_key(std::string origin_ns_fq,
                                                        const std::string& short_inst_name);

    /**
     * Build the ORIGIN-ABSOLUTE name of a template instantiation:
     * "::<origin_ns_fq>::<short_name>" (root-prefixed), e.g. origin "k" + "Optional"
     * → ::k::Optional. Any leading root prefix ("::") on @p origin_ns_fq is stripped
     * first; multi-level origins ("a::b") become nested name parts. Used so a
     * consumer-synthesised instantiation of an imported template gets the same
     * mangled symbol as in its origin module, enabling linkonce_odr/COMDAT dedup.
     */
    static k::name make_origin_absolute_name(const std::string& origin_ns_fq,
                                             const std::string& short_name);

    /**
     * Return (or create) the imported_variable model node for @p kdi_var.
     * Keyed by mangled_name.  Resolves the variable type.
     */
    std::shared_ptr<imported_variable>
    get_or_create_imported_variable(const kdi::kdi_variable* kdi_var,
                                    std::shared_ptr<context> ctx);

    /**
     * Return (or create) an imported enumeration model node for the enum
     * identified by its fully-qualified K name @p fq_name.
     *
     * Searches all loaded imports; builds the underlying type, entries,
     * and registers the enum_type in context::add_enum().
     */
    std::shared_ptr<enumeration>
    get_or_create_imported_enum(const k::name& fq_name,
                                std::shared_ptr<context> ctx);

    // ── Accessors ────────────────────────────────────────────────────────────

    const std::unordered_map<std::string, std::shared_ptr<imported_function>>&
    get_imported_functions() const { return _imported_functions; }

    const std::unordered_map<std::string, std::shared_ptr<imported_aggregate>>&
    get_imported_aggregates() const { return _imported_aggregates; }

    const std::unordered_map<std::string, std::shared_ptr<imported_variable>>&
    get_imported_variables() const { return _imported_variables; }


    //
    // Namespaces
    //

    /**
     * Retrieve the root namespace of this model.
     * @return The root namespace.
     */
    std::shared_ptr<ns> get_root_namespace();
    std::shared_ptr<const ns> get_root_namespace() const {
        return _root_ns;
    }

    bool has_main_method() const {
        return _global_main_func !=  nullptr;
    }
};



} // namespace k::model

#endif //KLANG_MODEL_NS_HPP
