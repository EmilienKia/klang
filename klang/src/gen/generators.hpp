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

#ifndef KLANG_GENERATORS_HPP
#define KLANG_GENERATORS_HPP

#include <llvm/ADT/STLExtras.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>

#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/IRCompileLayer.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>

#include "../model/model.hpp"
#include "../model/model_visitor.hpp"

#include "../common/logger.hpp"
#include "../lex/lexer.hpp"

#include "../compiler.hpp"


namespace k {
class compiler;
}

namespace k::model::gen {

class jit;

/**
 * Returns true if a return type requires sret (structure-return) ABI:
 * i.e. non-primitive aggregate types (struct, class, interface, array).
 */
inline bool needs_sret_return(const std::shared_ptr<k::model::type>& ret_type) {
    if (!ret_type) return false;
    auto t = k::model::type::remove_const(ret_type);
    return k::model::type::is_struct(t) || k::model::type::is_array(t) || k::model::type::is_sized_array(t);
}

class generation_error : public k::log::compiler_error {
public:
    explicit generation_error(k::log::diagnostic diag)
        : k::log::compiler_error(std::move(diag)) {}
};


/**
 * First pass of IR generation.
 * Will generate global variables and function declarations.
 * Function implementation and global variable initialization will be done later with implementation_generator.
 * Used to ensure all declarations are done and accessible, useful for nesting and out-of-order declaration.
 */
class declaration_generator : public default_model_visitor, protected k::log::logger_relay {
protected:
    unit& _unit;

    std::shared_ptr<context> _context;

    std::unique_ptr<llvm::IRBuilder<>> _builder;

    llvm::Value* _value;

    std::stack<std::shared_ptr<aggregate>> _struct_stack;

    [[noreturn]] void throw_error(unsigned int code, const lex::opt_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        auto diag = k::log::diagnostic::make_error(code, message, args);
        if (lexeme) diag.at(*lexeme);
        logger_relay::report(diag);
        throw generation_error(std::move(diag));
    }


public:
    declaration_generator(k::log::logger& logger, std::shared_ptr<context> context, unit& unit);

    llvm::Module& get_module();

    void visit_unit(unit &) override;

    void visit_namespace(ns &) override;
    void visit_function(function &) override;
    void visit_aggregate(aggregate&) override;
    void visit_klass(klass&) override;
    void visit_interface(interface&) override;
    void visit_annotation_type(annotation_type&) override;
    void visit_enumeration(enumeration&) override;
    void visit_union(union_type_def&) override;
    void visit_member_variable_definition(member_variable_definition&) override;
    void visit_global_variable_definition(global_variable_definition &) override;

    /**
     * No-op: global tool functions (global ctor / dtor) are created directly
     * by the implementation_generator with InternalLinkage.  Declaring them
     * here would produce a bodyless declaration that conflicts with InternalLinkage.
     */
    void visit_global_tool_function(global_tool_function&) override;

    /**
     * Emit the @_KVT<mangled> vtable global stub and declare $impl / thunk
     * function variants for a polymorphic class aggregate.
     * Called from visit_klass for class aggregates only.
     */
    void emit_vtable_stub(klass& st);

    /**
     * Emit LLVM GlobalAlias entries for all redirected functions
     * in the given namespace and its children (aggregates, sub-namespaces).
     */
    void emit_redirect_aliases(ns& nspc);

    /**
     * Emit LLVM GlobalAlias entries for all redirected functions
     * in the given aggregate and its nested aggregates.
     */
    void emit_redirect_aliases_from_aggregate(aggregate& agg);

    /**
     * Emit a single LLVM GlobalAlias for a redirected function.
     * No-op if the function is not redirected.
     */
    void emit_redirect_alias(function& fn);

    void visit_block(block&) override;
    void visit_return_statement(return_statement&) override;
    void visit_break_statement(break_statement&) override;
    void visit_continue_statement(continue_statement&) override;
    void visit_if_else_statement(if_else_statement&) override;
    void visit_while_statement(while_statement&) override;
    void visit_for_statement(for_statement&) override;
    void visit_expression_statement(expression_statement&) override;
    void visit_variable_statement(variable_statement&) override;

    void generate();
};

/**
 * Really achieve function implementations and global variable initialization.
 * Must be run after declaration_generator.
 */
class implementation_generator : public default_model_visitor, protected k::log::logger_relay {
protected:
    unit& _unit;

    std::shared_ptr<context> _context;

    std::unique_ptr<llvm::IRBuilder<>> _builder;

    llvm::Value* _value;

    std::stack<std::shared_ptr<aggregate>> _struct_stack;

    /** Stack of active cleanup BasicBlocks, one per block with destructible local variables. */
    std::stack<llvm::BasicBlock*> _cleanup_blocks;

    /** Parallel stack: for each cleanup block, the list of variable_statements to destroy (declaration order). */
    std::stack<std::vector<std::shared_ptr<variable_statement>>> _cleanup_vars_stack;

    /** Stack of loop exit blocks for 'break' statements. */
    std::stack<llvm::BasicBlock*> _loop_exit_blocks;

    /** Stack of loop continue blocks for 'continue' statements. */
    std::stack<llvm::BasicBlock*> _loop_continue_blocks;

    /** Stack: cleanup stack depth at each loop entry (for scoped cleanup on break/continue). */
    std::stack<size_t> _loop_cleanup_depth;

    /** Parallel stack: for each cleanup block, the list of owner-typed PARAMETERS to destroy (function body only). */
    std::stack<std::vector<std::shared_ptr<parameter>>> _owner_params_stack;

    /** Stack of struct-typed by-value parameters that need destructor calls at function exit. */
    std::stack<std::vector<std::shared_ptr<parameter>>> _struct_params_stack;

    /**
     * Temporary struct objects created during expression evaluation.
     * Each entry: { alloca pointer, destructor LLVM function }.
     * Destroyed in reverse creation order at end of full-expression (statement boundary).
     */
    std::vector<std::pair<llvm::AllocaInst*, llvm::Function*>> _expression_temporaries;

    /** Per-function alloca for return value (used when destructions must happen before a return). */
    llvm::AllocaInst* _retval_alloca = nullptr;

    /** sret pointer argument for functions returning non-primitive types.
     *  When non-null, the callee writes the return value into this pointer instead of
     *  returning an LLVM aggregate.  Set during visit_function prologue. */
    llvm::Value* _sret_ptr = nullptr;

    /** NRVO candidate variable — when non-null, this variable's alloca is aliased to _sret_ptr
     *  so it is constructed directly into the caller's destination. */
    std::shared_ptr<variable_statement> _nrvo_candidate;

    /** Destination pointer for sret calls from variable declarations or return statements.
     *  When non-null, visit_function_invocation_expression uses this pointer as the sret
     *  destination instead of creating a temporary. Set by visit_variable_statement or
     *  visit_return_statement. */
    llvm::Value* _sret_destination = nullptr;

    /** When non-null, link-related null-checks (link rebind from nullable source, dynamic cast
     *  to link with null_is_fatal) branch to this block on null instead of calling a fatal trap.
     *  Set by visit_if_else_statement to the else-block (or continue-block when there is no else)
     *  so that a failing link assignment in an if-condition acts as a false value.
     *  Saved/restored for nested if-statements. */
    llvm::BasicBlock* _null_failure_bb = nullptr;

    [[noreturn]] void throw_error(unsigned int code, const lex::opt_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        auto diag = k::log::diagnostic::make_error(code, message, args);
        if (lexeme) diag.at(*lexeme);
        logger_relay::report(diag);
        throw generation_error(std::move(diag));
    }


public:
    implementation_generator(k::log::logger& logger, std::shared_ptr<context> context, unit& unit);

    llvm::Module& get_module();

    void visit_unit(unit &) override;

    void visit_namespace(ns &) override;
    void visit_function(function &) override;
    void visit_global_constructor_function(global_constructor_function&) override;
    void visit_global_destructor_function(global_destructor_function&) override;
    void visit_aggregate(aggregate&) override;
    void visit_klass(klass&) override;
    void visit_interface(interface&) override;
    void visit_annotation_type(annotation_type&) override;
    void visit_member_variable_definition(member_variable_definition&) override;

    // ── visit_klass extracted helpers (defined in gen_class.cpp) ─────────────

    /** Check if this module imports libk (directly or transitively). */
    bool has_libk_import() const;

    /** Patch the RTTI global with real vtable pointers, base/nested/enclosing lists,
     *  flags, annotations, and function/constructor descriptors. */
    void patch_rtti_global(klass& klass);

    /** Fill the primary vtable with resolved function pointers. */
    void fill_primary_vtable(klass& klass);

    /** Build secondary vtables from model_materializer pre-computed specs. */
    void fill_secondary_vtables(klass& klass);

    /** Build secondary vtables for imported (external) base classes. */
    void fill_imported_base_vtables(klass& klass);
    void visit_global_variable_definition(global_variable_definition &) override;

    /**
     * Fill the @_KVT<mangled> vtable global constant with the actual function pointers
     * and offset-to-top values for a polymorphic class.
     * Called from visit_klass for class aggregates.
     */
    void fill_vtable(klass& st);

    /**
     * Get or create an adjustment thunk for `func` when dispatched from the secondary
     * base sub-object at `sub_object_offset` bytes.
     * Returns the LLVM function for the thunk, creating it if necessary.
     */
    llvm::Function* get_or_create_thunk(structure& st, function& func,
                                        ptrdiff_t sub_object_offset,
                                        size_t section_index);

    /**
     * Emit vptr store instructions at the start of a constructor body.
     * @param b            The IRBuilder positioned at the entry of the constructor.
     * @param st           The class owning the constructor.
     * @param this_val     The 'this' pointer LLVM value.
     * @param is_complete  True for C1 (complete-object ctor), false for C2 (base-object ctor).
     */
    void emit_vptr_stores(llvm::IRBuilder<>& b, structure& st,
                          llvm::Value* this_val, bool is_complete);

    // ── visit_function extracted helpers (defined in gen_function.cpp) ────────

    /**
     * Emit constructor pre-block IR: zero-init, parent pointer store, generated copy
     * constructor memberwise copy, standalone virtual base initialization.
     * @return true if the function was fully handled (e.g. generated copy ctor) and
     *         visit_function should return immediately.
     */
    bool emit_constructor_pre_block(function& function, llvm::Function* func);

    /**
     * Emit compiler-generated copy assignment operator (operator=) memberwise copy.
     * @return true if the function was fully handled and visit_function should return.
     */
    bool emit_copy_assignment_operator(function& function, llvm::Function* func);

    /**
     * Emit post-block constructor IR: vptr stores (after base ctors so most-derived wins)
     * and virtual base pointer initialization across sub-objects.
     */
    void emit_constructor_post_block(function& function);

    /**
     * Emit destructor IR: member struct destructor calls (reverse declaration order),
     * base destructors (reverse base-declaration order), and virtual base sub-object
     * destructor calls (most-derived class only).
     */
    void emit_destructor_cleanup(function& function);

    /**
     * Emit intrinsic body for UniSlot::construct — placement constructor call on _slot.
     */
    void emit_intrinsic_unislot_construct(function& function, llvm::Function* func);

    /**
     * Emit intrinsic body for UniSlot::destruct — destructor call on _slot without free.
     */
    void emit_intrinsic_unislot_destruct(function& function, llvm::Function* func);

    // ── MultiSlot<T> intrinsic emitters ──────────────────────────────────────
    void emit_intrinsic_multislot_constructor(function& function, llvm::Function* func);
    void emit_intrinsic_multislot_destructor(function& function, llvm::Function* func);
    void emit_intrinsic_multislot_allocate(function& function, llvm::Function* func);
    void emit_intrinsic_multislot_reallocate(function& function, llvm::Function* func);
    void emit_intrinsic_multislot_deallocate(function& function, llvm::Function* func);
    void emit_intrinsic_multislot_construct(function& function, llvm::Function* func);
    void emit_intrinsic_multislot_destruct(function& function, llvm::Function* func);
    void emit_intrinsic_multislot_get(function& function, llvm::Function* func);

    /**
     * Emit function return epilogue: owner/struct parameter cleanup, return instruction,
     * NRVO alloca→sret replacement, dead instruction elimination, and verification.
     */
    void emit_function_return_epilogue(function& function, llvm::Function* func, bool use_sret);

    void visit_block(block&) override;
    void visit_return_statement(return_statement&) override;
    void visit_break_statement(break_statement&) override;
    void visit_continue_statement(continue_statement&) override;
    void visit_if_else_statement(if_else_statement&) override;
    void visit_while_statement(while_statement&) override;
    void visit_for_statement(for_statement&) override;
    void visit_expression_statement(expression_statement&) override;
    void visit_variable_statement(variable_statement&) override;

    llvm::Constant* get_llvm_constant_from_value_expr(const value_expression&) const;
    void visit_value_expression(value_expression&) override;
    void visit_symbol_expression(symbol_expression&) override;

    llvm::Value* process_unary_expression(unary_expression&);
    std::pair<llvm::Value*,llvm::Value*> process_binary_expression(binary_expression&);

    /**
     * Generate a function call for a binary operator overload.
     * Returns true if the overload was handled (and _value is set), false if not an overload.
     */
    bool generate_binary_operator_overload(binary_expression& expr);

    /**
     * Generate a function call for a unary operator overload.
     * Returns true if the overload was handled (and _value is set), false if not an overload.
     */
    bool generate_unary_operator_overload(unary_expression& expr);

    /**
     * Generate a function call for a casting operator overload.
     * Returns true if the overload was handled (and _value is set), false if not an overload.
     */
    bool generate_cast_operator_overload(cast_expression& expr);

    void visit_addition_expression(addition_expression&) override;
    void visit_substraction_expression(substraction_expression&) override;
    void visit_multiplication_expression(multiplication_expression&) override;
    void visit_division_expression(division_expression&) override;
    void visit_modulo_expression(modulo_expression&) override;
    void visit_bitwise_and_expression(bitwise_and_expression&) override;
    void visit_bitwise_or_expression(bitwise_or_expression&) override;
    void visit_bitwise_xor_expression(bitwise_xor_expression&) override;
    void visit_left_shift_expression(left_shift_expression&) override;
    void visit_right_shift_expression(right_shift_expression&) override;

    void visit_simple_assignation_expression(simple_assignation_expression&) override;
    void visit_addition_assignation_expression(additition_assignation_expression&) override;
    void visit_substraction_assignation_expression(substraction_assignation_expression&) override;
    void visit_multiplication_assignation_expression(multiplication_assignation_expression&) override;
    void visit_division_assignation_expression(division_assignation_expression&) override;
    void visit_modulo_assignation_expression(modulo_assignation_expression&) override;
    void visit_bitwise_and_assignation_expression(bitwise_and_assignation_expression&) override;
    void visit_bitwise_or_assignation_expression(bitwise_or_assignation_expression&) override;
    void visit_bitwise_xor_assignation_expression(bitwise_xor_assignation_expression&) override;
    void visit_left_shift_assignation_expression(left_shift_assignation_expression&) override;
    void visit_right_shift_assignation_expression(right_shift_assignation_expression&) override;

    void visit_unary_plus_expression(unary_plus_expression&) override;
    void visit_unary_minus_expression(unary_minus_expression&) override;
    void visit_bitwise_not_expression(bitwise_not_expression&) override;

    void visit_prefix_increment_expression(prefix_increment_expression&) override;
    void visit_prefix_decrement_expression(prefix_decrement_expression&) override;
    void visit_postfix_increment_expression(postfix_increment_expression&) override;
    void visit_postfix_decrement_expression(postfix_decrement_expression&) override;

    void visit_logical_and_expression(logical_and_expression&) override;
    void visit_logical_or_expression(logical_or_expression&) override;
    void visit_logical_not_expression(logical_not_expression&) override;

    void visit_load_value_expression(load_value_expression&) override;
    void visit_address_of_expression(address_of_expression&) override;
    void visit_drain_expression(drain_expression&) override;
    void visit_dereference_expression(dereference_expression&) override;
    void visit_member_of_object_expression(member_of_object_expression&) override;
    void visit_member_of_pointer_expression(member_of_pointer_expression&) override;
    void visit_pm_expression(pm_expression&) override;

    void visit_equal_expression(equal_expression&) override;
    void visit_different_expression(different_expression&) override;
    void visit_lesser_expression(lesser_expression&) override;
    void visit_greater_expression(greater_expression&) override;
    void visit_lesser_equal_expression(lesser_equal_expression&) override;
    void visit_greater_equal_expression(greater_equal_expression&) override;

    void visit_subscript_expression(subscript_expression&) override;
    void visit_function_invocation_expression(function_invocation_expression&) override;
    void visit_constructor_invocation_expression(constructor_invocation_expression&) override;
    void visit_temporary_construction_expression(temporary_construction_expression&) override;
    void visit_new_expression(new_expression&) override;
    void visit_delete_expression(delete_expression&) override;
    void visit_owner_move_expression(owner_move_expression&) override;
    void visit_array_init_expression(array_init_expression&) override;
    void visit_designated_struct_init_expression(designated_struct_init_expression&) override;

    void visit_cast_expression(cast_expression&) override;

    /**
     * Emit destructor calls for all tracked expression temporaries, in reverse
     * creation order, then clear the list.  Must be called at full-expression
     * boundaries (end of expression_statement, variable_statement init, etc.).
     */
    void emit_expression_temporaries_cleanup();

    /**
     * Emit cleanup (destructor / owner free) for a single condition variable.
     * Used for if-let condition variable cleanup at end of then/else blocks.
     */
    void emit_cond_var_cleanup(const std::shared_ptr<variable_statement>& var_stmt);

    void generate();

protected:
    void optimize_function_dead_inst_elimination(llvm::Function& func);

    /**
     * Emit or retrieve the declaration of @__k_fatal_null_assignation or @__k_fatal_null_dereference.
     * The function is declared as: void() noreturn nounwind cold, calling llvm.trap or llvm.debugtrap.
     */
    llvm::Function* get_or_declare_fatal_null_function(const std::string& name);

    /** Emit RTTI-based dynamic cast IR for a cast_expression whose types require it. */
    void emit_dynamic_cast(cast_expression& expr,
                           std::shared_ptr<struct_type> src_st_type,
                           std::shared_ptr<struct_type> tgt_st_type);

    /**
     * Emit a runtime null-check for `ptr_value`. If null, calls fatal_fn and then unreachable.
     * `ok_bb` is the block to continue in if not null. The function creates the check+branch.
     * If `soft_fail_bb` is non-null, branch there on null instead of calling the fatal function.
     */
    void emit_null_check(llvm::Value* ptr_value, llvm::Function* fatal_fn, const std::string& label = "",
                         llvm::BasicBlock* soft_fail_bb = nullptr);
};



class jit {
protected:
    std::shared_ptr<compiler> _compiler;
    std::unique_ptr<llvm::orc::LLJIT> _lljit;
    llvm::orc::JITDylib &_main_dynlib;

    enum {
        DEFAULT,
        INITIALIZED,
        FINALIZED
    } _state = DEFAULT;

    jit(std::shared_ptr<compiler> compiler);

    friend class k::compiler;
    static std::unique_ptr<jit> create(std::shared_ptr<compiler> compiler);

    llvm::Expected<llvm::orc::ExecutorAddr> lookup_symbol_address(const std::string& name);
    llvm::Expected<llvm::orc::ExecutorAddr> lookup_main_entry_symbol_address();

    void add_module(llvm::orc::ThreadSafeModule module);

public:
    ~jit();

    void initialize_runtime();
    void finalize_runtime();

    template<typename T>
    T lookup_symbol(const std::string& name) {
        auto symb = lookup_symbol_address(name);
        if (symb) {
            return lookup_symbol_address(name)->toPtr<T>();
        } else {
            return nullptr;
        }
    }

    template<typename T>
    T lookup_main_entry_symbol() {
        return lookup_main_entry_symbol_address()->toPtr<T>();
    }

};

} // k::model::gen

#endif //KLANG_GENERATORS_HPP
