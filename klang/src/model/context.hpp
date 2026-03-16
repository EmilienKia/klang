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
#include "type.hpp"


#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>


namespace k {
class compiler;
}

namespace k::model {
class value_expression;
class variable_statement;
class parameter;
class function;
class global_variable_definition;

namespace gen {
    class declaration_generator;
    class implementation_generator;
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
    std::vector<std::shared_ptr<unresolved_type>> _unresolved;

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

    void resolve_types();

    std::shared_ptr<type> resolve_type(const std::shared_ptr<type>& type);


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
    std::unique_ptr<llvm::LLVMContext> move_llvm_context();

private:
    void reset();
    void init_primitive_types();
    void resolve_struct_type(std::shared_ptr<struct_type> st_type,
                             std::unordered_set<struct_type*>& in_progress);
};




} // namespace k::model
#endif //KLANG_CONTEXT_HPP
