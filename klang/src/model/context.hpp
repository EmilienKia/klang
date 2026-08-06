/*
 * K Language compiler
 *
 * Copyright 2024 Emilien Kia
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

#ifndef KLANG_CONTEXT_HPP
#define KLANG_CONTEXT_HPP

#include <map>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>

#include "../lex/lexer.hpp"
#include "../parse/ast.hpp"
#include "../parse/parser.hpp"
#include "../common/common.hpp"
#include "../common/logger.hpp"
#include "type.hpp"


#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>


namespace k {
class compiler;
}

namespace k::model {

class context_resolution_error : public k::log::compiler_error {
public:
    explicit context_resolution_error(k::log::diagnostic diag)
        : k::log::compiler_error(std::move(diag)) {}
};

class value_expression;
class variable_statement;
class parameter;
class function;
class global_variable_definition;

namespace gen {
    class declaration_generator;
    class implementation_generator;
    class type_reference_resolver;
    class aggregate_type_resolver;
}


class context {
protected:
    friend class k::model::gen::declaration_generator;
    friend class k::model::gen::implementation_generator;

    std::unique_ptr<llvm::LLVMContext> _context;

    // Types:
    std::map<primitive_type::PRIMITIVE_TYPE, std::shared_ptr<primitive_type>> _primitive_types;
    std::shared_ptr<null_type> _null_type;
    std::map<std::string, std::shared_ptr<struct_type>> _struct_types;
    std::map<std::string, std::shared_ptr<enum_type>> _enum_types;
    /** Nominal types of strong aliases, interned per alias_definition. */
    std::map<const alias_definition*, std::shared_ptr<alias_type>> _alias_types;
    std::vector<std::shared_ptr<unresolved_type>> _unresolved;
    /** The unique `%__k.callable = type { ptr, ptr }` named struct of this context. */
    llvm::StructType* _callable_llvm_type = nullptr;
    /**
     * Interning pool of fat callable types, keyed on the *nominal* identity of the
     * component types (shared_ptr identity — canonical() is deliberately NOT applied,
     * so a typedef stays a distinct component) plus the addresser.
     */
    std::map<std::string, std::shared_ptr<callable_type>> _callable_types;

    // Entities:
    std::map<std::shared_ptr<global_variable_definition>, llvm::GlobalVariable*> _global_vars;
    std::map<std::shared_ptr<function>, llvm::Function*> _functions;
    std::map<std::shared_ptr<parameter>, llvm::AllocaInst*> _parameter_variables;
    std::map<std::shared_ptr<function>, llvm::AllocaInst*> _function_this_variables;
    std::map<std::shared_ptr<variable_statement>, llvm::AllocaInst*> _variables;

    // Virtual base standalone allocas: for each aggregate that has a direct virtual base,
    // maps vbase short name → the stack alloca for the standalone virtual base sub-object.
    // Set during constructor IR generation; used by constructor_invocation_expression to
    // find where to place the virtual base sub-object.
    std::map<std::shared_ptr<aggregate>, std::map<std::string, llvm::AllocaInst*>> _vbase_standalone_allocas;

    // LLVM module
    std::unique_ptr<llvm::Module> _module;

    // String literal deduplication pool: maps string content → LLVM global constant
    std::map<std::string, llvm::GlobalVariable*> _string_pool;

    // Encoded (prefixed) string literal pool: maps an encoding-tagged key → LLVM global.
    std::map<std::string, llvm::GlobalVariable*> _encoded_string_pool;

    /**
     * Stack of template parameter name sets.
     * Pushed by model_builder when entering a template declaration,
     * popped when leaving. Used by create_unresolved() to automatically
     * mark unresolved types that are template parameter placeholders.
     */
    std::vector<std::unordered_set<std::string>> _template_param_scopes;

    context();

public:

    static std::shared_ptr<context> create();

    inline llvm::LLVMContext& llvm_context() {return *_context.get();}
    inline llvm::LLVMContext& operator *() {return *_context.get();}

    std::shared_ptr<primitive_type> from_type(primitive_type::PRIMITIVE_TYPE type);
    /** Return the singleton null literal type. */
    std::shared_ptr<k::model::null_type> get_null_type() const { return _null_type; }

    std::shared_ptr<type> from_string(const std::string& type_name);
    std::shared_ptr<type> from_keyword(const lex::keyword& kw, bool is_unsigned = false);
    std::shared_ptr<type> from_type_specifier(const k::parse::ast::type_specifier& type_spec);
    std::shared_ptr<type> from_literal(const k::lex::any_literal &literal);

    void add_struct(std::shared_ptr<struct_type> st_type);
    void add_enum(const std::string& name, std::shared_ptr<enum_type> et);

    /**
     * Create an opaque (body-less) LLVM StructType and assign it to @p st_type.
     * Used for imported aggregates whose layout is already known from the KDI but
     * whose LLVM body does not need to be resolved by context::resolve_types().
     * After this call st_type->is_resolved() returns true.
     */
    void materialise_opaque_struct_type(std::shared_ptr<struct_type> st_type);

    /**
     * Build the LLVM struct body for an imported aggregate from its member
     * variable children.  Unlike resolve_struct_type(), this method does NOT
     * check is_resolved() first: it always (re)builds the body.
     *
     * Call this after all member_variable_definition children have been added
     * to the aggregate so that get_member() queries can find them.
     *
     * @param st_type  The struct_type to fill (must already be in the context).
     */
    void build_imported_struct_body(std::shared_ptr<struct_type> st_type);

    llvm::Type* get_llvm_type(const std::shared_ptr<type>& type);

    llvm::Constant* get_llvm_constant_from_literal(const k::lex::any_literal &literal);
    llvm::Constant* get_llvm_constant_from_value(const k::value_type &value);
    llvm::Constant* get_llvm_constant_from_value_expression(const value_expression& value);

    /**
     * Return (or create) a global constant for a string literal.
     * The global has type { i32, [N x i8] } where N = content.size()
     * (content must already include the null terminator).
     * Deduplicates identical strings via _string_pool.
     */
    llvm::Constant* get_or_create_string_literal(const std::string& content);

    /**
     * Return (or create) a global constant for an encoding-prefixed string
     * literal. The code points are re-encoded into the encoding's code units
     * (UTF-8 → unsigned byte, UTF-16 → unsigned short, UTF-32 → char), a zero
     * terminator is appended, and a { i32 size, [N x elem] } global is built.
     * Deduplicates identical literals via _encoded_string_pool.
     */
    llvm::Constant* get_or_create_encoded_string_literal(
        const std::vector<char32_t>& code_points, k::lex::literal_encoding enc);

    void resolve_types();

    /**
     * Rebuild LLVM struct bodies for template instantiations that gained
     * virtual-base subobject fields (__vbptr_X__/__vbase_X__) after their body
     * was first materialised. This happens when an intermediate template
     * instantiation is laid out before the derived diamond that makes one of its
     * bases virtual has been instantiated. Because LLVM struct bodies cannot be
     * re-set in place, each instantiation struct type is given a fresh opaque
     * LLVM type and re-resolved together so cross-references stay coherent.
     * Must run after all instantiation + base-subobject injection (i.e. after the
     * type_reference_resolver pass) and before code generation.
     */
    void rebuild_instantiation_layouts();

    std::shared_ptr<type> resolve_type(const std::shared_ptr<type>& type);

    /**
     * Return (creating it on first call) the unique `%__k.callable = type { ptr, ptr }`
     * named LLVM struct of this context. Never build a callable LLVM type inline: a
     * second anonymous/auto-uniquified struct would break cross-module type identity.
     */
    llvm::StructType* get_or_create_callable_llvm_type();

    /**
     * Return (creating and interning on first request) the fat callable type with the
     * given prototype and addresser.
     *
     * The interning key uses the *nominal* identity of @p ret, @p params and @p throws
     * (raw pointer identity, no canonical() stripping) so that two callables differing
     * only by a typedef component stay distinct types.
     */
    std::shared_ptr<callable_type> get_callable_type(
        const std::shared_ptr<type>& ret,
        const std::vector<std::shared_ptr<type>>& params,
        callable_type::addresser addr,
        const std::vector<std::shared_ptr<type>>& throws = {});

    /** Return the same callable prototype carrying a different addresser. */
    std::shared_ptr<callable_type> readdress(
        const std::shared_ptr<callable_type>& callable,
        callable_type::addresser addr);

    /**
     * Apply a `* ? + &` type suffix to a callable type by re-addressing it in place
     * (a callable is already an indirection, so it is never wrapped).
     * @return nullptr when @p subtype is not a callable.
     */
    std::shared_ptr<type> readdress_callable_suffix(const std::shared_ptr<type>& subtype,
                                                    const lex::operator_& op);

    /** Reject `!`, `#` and `[]` applied to a callable type. */
    static void reject_callable_addresser(const std::shared_ptr<type>& subtype, const char* suffix);

    /**
     * Build (once) the nominal alias_type of a strong alias (typedef).
     *
     * The returned type is representation-identical to @p underlying — it
     * forwards get_llvm_type() to it — but is a distinct type at the K level.
     * Aliases are interned per declaration so that nominal identity can be
     * tested by pointer or by alias_definition identity.
     */
    std::shared_ptr<alias_type> create_alias_type(const std::shared_ptr<alias_definition>& alias,
                                                  const std::shared_ptr<type>& underlying);

    /**
     * Build (or reuse) the nominal type of one instantiation of a parameterised
     * strong alias.
     *
     * Unlike create_alias_type(), which interns a single type per declaration,
     * a parameterised alias yields one distinct nominal type per argument list:
     * 'Id<int>' and 'Id<long>' are as unrelated as two separate typedefs. The
     * types are interned on the declaration itself, keyed by @p args_key.
     */
    std::shared_ptr<alias_type> create_template_alias_type(const std::shared_ptr<alias_definition>& alias,
                                                           const std::shared_ptr<type>& underlying,
                                                           const std::string& args_key,
                                                           const std::string& display_name);

    /**
     * Push a set of template parameter names onto the scope stack.
     * While a scope is active, create_unresolved() will mark matching
     * unresolved types as template parameter placeholders.
     */
    void push_template_param_scope(const std::unordered_set<std::string>& param_names);

    /** Pop the most recent template parameter scope. */
    void pop_template_param_scope();


    void init_module(const std::string& module_name);
    llvm::Module& module() {return *_module;}

    /**
     * Parse a '%Name = type { ... }' LLVM IR snippet and intern the named
     * StructType into the current LLVMContext.  Returns the StructType* or
     * nullptr on failure.  If the type is already interned it is returned as-is.
     *
     * NOTE: do not use this to intern types that reference other types defined
     * in separate snippets — LLVM forward-references created in one temporary
     * module are not resolved by later calls.  Use intern_all_llvm_struct_defs()
     * instead when multiple inter-dependent definitions must be interned together.
     */
    llvm::StructType* intern_llvm_struct_from_def(const std::string& llvm_def,
                                                  const std::string& type_name);

    /**
     * Parse a combined IR block containing multiple '%Name = type { ... }'
     * definitions and intern all named StructTypes into the current LLVMContext.
     *
     * All definitions are parsed in a single LLVM IR module so that forward
     * references between types (e.g. '%Derived = type { %Base, i32 }') are
     * resolved correctly.  This must be called once, before any call to
     * intern_llvm_struct_from_def() for the same types, to avoid LLVM creating
     * separate opaque forward-references for them.
     *
     * @param combined_ir  Concatenated LLVM IR type definitions (one per line),
     *                     e.g. "%A = type { ptr }\n%B = type { %A, i32 }\n".
     */
    void intern_all_llvm_struct_defs(const std::string& combined_ir);

    /**
     * Attach a pre-built llvm::StructType to an already-registered struct_type.
     * Used by imported.cpp to wire the LLVM type obtained from llvm_def.
     * No fields are registered in the model (GEP uses llvm_field_index directly).
     */
    void attach_llvm_struct_type(std::shared_ptr<struct_type> st_type,
                                 llvm::StructType* llvm_st);

    /**
     * Attach a pre-built llvm::StructType to an already-registered struct_type,
     * and also register named fields for get_member() lookups.
     * Used by imported.cpp to wire the LLVM type and expose named members.
     */
    void attach_llvm_struct_type(std::shared_ptr<struct_type> st_type,
                                 llvm::StructType* llvm_st,
                                 std::vector<struct_type::field> named_fields);

    /**
     * Parse a 'declare ... @mangled(...)' LLVM IR snippet and create/return
     * the corresponding llvm::Function* (ExternalLinkage) in _module.
     * Returns nullptr if _module is not yet initialised or parsing fails.
     */
    llvm::Function* declare_llvm_function_from_def(const std::string& llvm_def,
                                                   const std::string& mangled_name);

    /**
     * Look up the LLVM Function* for a model function.
     * Returns nullptr if not yet declared.
     */
    llvm::Function* lookup_llvm_function(const std::shared_ptr<function>& func) const {
        auto it = _functions.find(func);
        return (it != _functions.end()) ? it->second : nullptr;
    }

protected:

    std::shared_ptr<unresolved_type> create_unresolved(const name& type_id);
    std::shared_ptr<unresolved_type> create_unresolved(name&& type_id);


    friend class k::compiler;
    friend class gen::type_reference_resolver;
    friend class gen::aggregate_type_resolver;
    std::unique_ptr<llvm::LLVMContext> move_llvm_context();

private:
    void reset();
    void init_primitive_types();
    void resolve_struct_type(std::shared_ptr<struct_type> st_type,
                             std::unordered_set<struct_type*>& in_progress);
};




} // namespace k::model
#endif //KLANG_CONTEXT_HPP
