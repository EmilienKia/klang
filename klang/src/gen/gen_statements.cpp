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
#include "resolvers.hpp"
#include "generators.hpp"
#include "gen_helpers.hpp"

#include "../model/imported.hpp"
#include "../errors.hpp"

namespace k::model::gen {

using namespace k::model;

//
// Expression temporaries cleanup
//

void implementation_generator::emit_expression_temporaries_cleanup() {
    if (_expression_temporaries.empty()) return;
    // Destroy in reverse creation order
    for (auto it = _expression_temporaries.rbegin(); it != _expression_temporaries.rend(); ++it) {
        _builder->CreateCall(it->second, {it->first});
    }
    _expression_temporaries.clear();
}


//
// Block
//

void symbol_resolver::visit_block(block& block)
{
    // Look at static/global var definitions
    for (auto var_entry : block.variables()) {
        if (auto global_var = std::dynamic_pointer_cast<global_variable_definition>(var_entry.second)) {
            global_var->accept(*this);
        }
    }
    // Statements
    for(auto& stmt : block.get_statements()) {
        stmt->accept(*this);
    }
}

void type_reference_resolver::visit_block(block& block)
{
    // Look at static/global var definitions
    for (auto var_entry : block.variables()) {
        if (auto global_var = std::dynamic_pointer_cast<global_variable_definition>(var_entry.second)) {
            global_var->accept(*this);
        }
    }
    // Statements
    for(auto& stmt : block.get_statements()) {
        stmt->accept(*this);
    }
}

void declaration_generator::visit_block(block& block) {
    // Look at static/global var definitions
    for (auto var_entry : block.variables()) {
        if (auto global_var = std::dynamic_pointer_cast<global_variable_definition>(var_entry.second)) {
            global_var->accept(*this);
        }
    }
    // Statements
    for(auto stmt : block.get_statements()) {
        stmt->accept(*this);
    }
}

/**
 * Generate LLVM IR for a block: set up cleanup tracking and visit all statements.
 *
 * Steps:
 *   1. Push new cleanup block and variable tracking stacks.
 *   2. Visit each statement in the block.
 *   3. On block exit: emit destructor calls for all block-scoped struct/owner variables.
 *   4. Pop cleanup stacks.
 */
void implementation_generator::visit_block(block& blk) {
    // Look at static/global var definitions
    for (auto var_entry : blk.variables()) {
        if (auto global_var = std::dynamic_pointer_cast<global_variable_definition>(var_entry.second)) {
            global_var->accept(*this);
        }
    }

    // Collect local variable_statements whose type is a struct with a destructor,
    // or an owner type (owner variables must be freed at scope exit), in declaration order.
    std::vector<std::shared_ptr<variable_statement>> dtor_vars;
    for (auto& stmt : blk.get_statements()) {
        if (auto var_stmt = std::dynamic_pointer_cast<variable_statement>(stmt)) {
            auto vt = var_stmt->get_type();
            // Struct with destructor
            if (auto st_type = std::dynamic_pointer_cast<struct_type>(vt)) {
                if (st_type->get_struct() && st_type->get_struct()->get_destructor()) {
                    dtor_vars.push_back(var_stmt);
                }
            }
            // Owner type — always needs cleanup (destroy + free on scope exit)
            if (type::is_owner(vt)) {
                dtor_vars.push_back(var_stmt);
            }
        }
    }

    const bool needs_cleanup = !dtor_vars.empty();

    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* cleanup_block = nullptr;
    llvm::BasicBlock* continue_block = nullptr;

    // Step 1: Push new cleanup block and variable tracking stacks
    if (needs_cleanup) {
        cleanup_block  = llvm::BasicBlock::Create(**_context, "block-cleanup");
        continue_block = llvm::BasicBlock::Create(**_context, "block-continue");
        // Push cleanup block and variable list so visit_return_statement can use them
        _cleanup_blocks.push(cleanup_block);
        _cleanup_vars_stack.push(dtor_vars);
    }

    // Generate statements normally
    for (auto& stmt : blk.get_statements()) {
        stmt->accept(*this);
    }

    if (needs_cleanup) {
        // On the normal exit path, branch to cleanup
        _builder->CreateBr(cleanup_block);

        // Step 2: Visit each statement in the block
        // Emit cleanup block: call destructors in REVERSE declaration order
        func->insert(func->end(), cleanup_block);
        _builder->SetInsertPoint(cleanup_block);

        for (auto it = dtor_vars.rbegin(); it != dtor_vars.rend(); ++it) {
            auto& var_stmt = *it;

            // NRVO: do NOT destroy the NRVO candidate — it lives in caller's storage
            if (_nrvo_candidate && var_stmt == _nrvo_candidate) continue;

            auto vt = var_stmt->get_type();

            auto var_it = _context->_variables.find(var_stmt);
            if (var_it == _context->_variables.end()) continue;
            llvm::AllocaInst* alloca = var_it->second;

            // Step 3: On block exit: emit destructor calls for all block-scoped struct/owner variables
            if (auto st_type = std::dynamic_pointer_cast<struct_type>(vt)) {
                // Struct destructor
                auto dtor = st_type->get_struct()->get_destructor();
                if (!dtor) continue;
                auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
                if (dtor_it == _context->_functions.end()) continue;
                _builder->CreateCall(dtor_it->second, {alloca});
            } else if (auto own_type = std::dynamic_pointer_cast<owner_type>(vt)) {
                // Owner: emit conditional destroy+free (only if non-null)
                emit_owner_cleanup_if_nonnull(_builder.get(), get_module(), _context->_functions,
                    alloca, own_type->get_owned_type(), "owner_cleanup");
            }
        }

        // On normal path, branch to continue
        _builder->CreateBr(continue_block);

        // Step 4: Pop cleanup stacks
        // Pop cleanup entries
        _cleanup_blocks.pop();
        _cleanup_vars_stack.pop();

        // Emit continue block and set insert point there
        func->insert(func->end(), continue_block);
        _builder->SetInsertPoint(continue_block);
    }
}

//
// Return
//

void symbol_resolver::visit_return_statement(return_statement& stmt)
{
    if(auto expr = stmt.get_expression()) {
        expr->accept(*this);
    }
}

/**
 * Resolve a return statement: validate return expression type matches function return type.
 *
 * Steps:
 *   1. Resolve the return expression (if any).
 *   2. Adapt the expression type to match the enclosing function's return type.
 */
void type_reference_resolver::visit_return_statement(return_statement& stmt)
{
    auto func = stmt.get_block()->get_function();
    auto ret_type = func->get_return_type();
    // TODO check if return type is void to prevent to return sometinhg

    // Step 1: Resolve the return expression (if any)
    if(auto expr = stmt.get_expression()) {
        // Warn if function uses named return variable and return has an expression
        if (func->has_named_return_var()) {
            if (stmt.get_ast_return_statement()) {
                warn(static_cast<unsigned int>(k::diag::statement_diag::WARN_UNREACHABLE_AFTER_RETURN), lex::any_lexeme{stmt.get_ast_return_statement()->ret},
                    "Function with named return variable '{}' uses 'return expr;'; "
                    "the expression will be assigned to '{}' before returning",
                    {func->get_named_return_var()->get_short_name(),
                     func->get_named_return_var()->get_short_name()});
            }
        }

        // Step 2: Adapt the expression type to match the enclosing function's return type
        _replacement_expr = nullptr;
        expr->accept(*this);
        if (_replacement_expr) {
            stmt.set_expression(_replacement_expr);
            expr = _replacement_expr;
            _replacement_expr = nullptr;
        }
        auto cast = adapt_type(expr, ret_type);
        if(!cast) {
            throw_error(static_cast<unsigned int>(k::diag::statement_diag::ERR_RETURN_TYPE_MISMATCH), stmt.get_ast_return_statement()->ret, "Return expression type must be compatible to the expected function return type");
        } else if(cast != expr ) {
            // Casted, assign casted expression as return expr.
            stmt.set_expression(cast);
        } else {
            // Compatible type, no need to cast.
        }
    }
}

void declaration_generator::visit_return_statement(return_statement& stmt) {
    // Nothing to do here (nothing in expressions)
}

/**
 * Generate LLVM IR for a return statement.
 *
 * Steps:
 *   1. Evaluate the return expression (if any).
 *   2. Emit cleanup for all block-scoped variables (reverse order).
 *   3. For sret functions: store result into sret pointer.
 *   4. For NRVO candidates: no copy needed (already in sret destination).
 *   5. Emit ret instruction.
 */
void implementation_generator::visit_return_statement(return_statement& stmt) {

    auto func = stmt.get_block()->get_function();
    auto named_ret_var = func ? func->get_named_return_var() : nullptr;

    // Evaluate the return expression first (before any destructor calls)
    llvm::Value* ret_value = nullptr;
    if (auto expr = stmt.get_expression()) {
        _value = nullptr;

        // Named return variable: return expr is treated as assignment to the named var + return.
        // (Warning is emitted during validation, not here.)
        if (named_ret_var) {
            // Evaluate the expression
            expr->accept(*this);
            ret_value = _value;
            _value = nullptr;

            // Store into named return variable
            if (ret_value) {
                auto var_it = _context->_variables.find(named_ret_var);
                if (var_it != _context->_variables.end()) {
                    if (_sret_ptr) {
                        // sret: store into sret_ptr (which is what the named var alloca will be RAUW'd to)
                        if (ret_value != _sret_ptr) {
                            if (llvm::isa<llvm::AllocaInst>(ret_value) || llvm::isa<llvm::GetElementPtrInst>(ret_value)) {
                                auto ret_type = func->get_return_type();
                                llvm::Type* llvm_ret_type = _context->get_llvm_type(ret_type);
                                llvm::Value* loaded = _builder->CreateLoad(llvm_ret_type, ret_value, "nrv_sret_load");
                                _builder->CreateStore(loaded, var_it->second);
                            } else {
                                _builder->CreateStore(ret_value, var_it->second);
                            }
                        }
                    } else {
                        // non-sret: store into the variable's alloca
                        _builder->CreateStore(ret_value, var_it->second);
                    }
                }
            }

            // Destroy any struct temporaries created during the expression evaluation
            emit_expression_temporaries_cleanup();

            // Fall through to scope cleanup and return
            ret_value = nullptr; // don't use ret_value for the ret instruction

        } else {
        // For sret functions: if the return expression is the NRVO candidate,
        // the data is already in _sret_ptr (the variable's alloca IS _sret_ptr),
        // so we skip evaluation and store entirely.
        bool is_nrvo_return = false;
        if (_sret_ptr && _nrvo_candidate) {
            auto sym = std::dynamic_pointer_cast<symbol_expression>(expr);
            if (!sym) {
                if (auto lv = std::dynamic_pointer_cast<load_value_expression>(expr))
                    sym = std::dynamic_pointer_cast<symbol_expression>(lv->sub_expr());
            }
            if (sym && sym->is_variable_def()) {
                auto var_def = sym->get_variable_def();
                auto var_stmt = std::dynamic_pointer_cast<variable_statement>(var_def);
                if (var_stmt == _nrvo_candidate) {
                    is_nrvo_return = true;
                }
            }
        }

        if (!is_nrvo_return) {
            // For sret functions: set _sret_destination so that if the return expression
            // is a function call returning sret, it writes directly into our _sret_ptr.
            if (_sret_ptr) {
                _sret_destination = _sret_ptr;
            }

            expr->accept(*this);
            ret_value = _value;
            _value = nullptr;

            // Reset destination (may have been consumed by a function call)
            _sret_destination = nullptr;

            if (_sret_ptr && ret_value) {
                // For sret: store the result into the sret pointer
                // (unless already written there by a nested sret call)
                if (ret_value != _sret_ptr) {
                    // If ret_value is a pointer (alloca from materialized struct or local var),
                    // load the aggregate and store into sret
                    if (llvm::isa<llvm::AllocaInst>(ret_value) || llvm::isa<llvm::GetElementPtrInst>(ret_value)) {
                        auto func_model = stmt.get_block()->get_function();
                        auto ret_type = func_model->get_return_type();
                        llvm::Type* llvm_ret_type = _context->get_llvm_type(ret_type);
                        llvm::Value* loaded = _builder->CreateLoad(llvm_ret_type, ret_value, "sret_load");
                        _builder->CreateStore(loaded, _sret_ptr);
                    } else {
                        // Raw aggregate value
                        _builder->CreateStore(ret_value, _sret_ptr);
                    }
                }
            } else if (ret_value && _retval_alloca) {
                // Non-sret: store the return value so we can load it after destructor calls
                _builder->CreateStore(ret_value, _retval_alloca);
            }
        } else {
            // Full NRVO return: after the RAUW pass in visit_function, the NRVO
            // candidate's alloca will have been replaced by _sret_ptr, so the
            // constructor already wrote directly into the caller's destination.
            // Nothing to copy here.
        }

        // Step 1: Evaluate the return expression (if any)
        // Destroy any struct temporaries created during the return expression evaluation
        emit_expression_temporaries_cleanup();
        } // end else (non-named-return expression handling)
    }

    // Step 2: Emit cleanup for all block-scoped variables (reverse order)
    // Emit destructor calls for all active scopes, from innermost to outermost.
    // We use a copy of the cleanup vars stack to iterate without modifying the live stack.
    if (!_cleanup_vars_stack.empty() || !_owner_params_stack.empty() || !_struct_params_stack.empty()) {
        // Collect all scope variable lists from innermost to outermost
        std::vector<std::vector<std::shared_ptr<variable_statement>>> all_scopes;
        std::stack<std::vector<std::shared_ptr<variable_statement>>> tmp = _cleanup_vars_stack;
        while (!tmp.empty()) {
            all_scopes.push_back(tmp.top());
            tmp.pop();
        }
        // Each scope: emit destructor calls in reverse declaration order
        for (auto& scope_vars : all_scopes) {
            for (auto it = scope_vars.rbegin(); it != scope_vars.rend(); ++it) {
                auto& var_stmt = *it;

                // NRVO / named return: do NOT destroy the candidate — it lives in caller's storage
                if (_nrvo_candidate && var_stmt == _nrvo_candidate) continue;
                if (named_ret_var && var_stmt == named_ret_var) continue;

                auto vt = var_stmt->get_type();

                auto var_it = _context->_variables.find(var_stmt);
                if (var_it == _context->_variables.end()) continue;
                llvm::AllocaInst* alloca = var_it->second;

                // Step 3: For sret functions: store result into sret pointer
                if (auto st_type = std::dynamic_pointer_cast<struct_type>(vt)) {
                    auto dtor = st_type->get_struct()->get_destructor();
                    if (!dtor) continue;
                    auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
                    if (dtor_it == _context->_functions.end()) continue;
                    _builder->CreateCall(dtor_it->second, {alloca});
                } else if (auto own_type = std::dynamic_pointer_cast<owner_type>(vt)) {
                    emit_owner_cleanup_if_nonnull(_builder.get(), get_module(), _context->_functions,
                        alloca, own_type->get_owned_type(), "ret_owner");
                } else if (auto arr_type = std::dynamic_pointer_cast<sized_array_type>(vt)) {
                    // Sized array of owners or structs-with-dtors: cleanup each element
                    emit_sized_array_elements_cleanup(_builder.get(), get_module(),
                        _context->_functions, alloca, arr_type);
                }
            }
        }
        // Also clean up owner-typed parameters (outermost scope, reverse order)
        if (!_owner_params_stack.empty()) {
            auto params_copy = _owner_params_stack.top(); // only outermost (function body) scope
            for (auto it = params_copy.rbegin(); it != params_copy.rend(); ++it) {
                auto& param = *it;
                auto own_type = std::dynamic_pointer_cast<owner_type>(param->get_type());
                if (!own_type) continue;
                auto param_it = _context->_parameter_variables.find(param);
                if (param_it == _context->_parameter_variables.end()) continue;
                llvm::AllocaInst* alloca = param_it->second;
                emit_owner_cleanup_if_nonnull(_builder.get(), get_module(), _context->_functions,
                    alloca, own_type->get_owned_type(), "ret_param");
            }
        }
        // Also clean up struct-typed by-value parameters (reverse order)
        if (!_struct_params_stack.empty()) {
            auto params_copy = _struct_params_stack.top();
            for (auto it = params_copy.rbegin(); it != params_copy.rend(); ++it) {
                auto& param = *it;
                auto st_type = std::dynamic_pointer_cast<struct_type>(param->get_type());
                if (!st_type || !st_type->get_struct() || !st_type->get_struct()->get_destructor()) continue;
                auto dtor = st_type->get_struct()->get_destructor();
                auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
                if (dtor_it == _context->_functions.end()) continue;
                auto param_it = _context->_parameter_variables.find(param);
                if (param_it == _context->_parameter_variables.end()) continue;
                _builder->CreateCall(dtor_it->second, {param_it->second});
            }
        }
    }

    // Step 4: For NRVO candidates: no copy needed (already in sret destination)
    // Step 5: Emit ret instruction
    // Emit the actual ret instruction
    if (_sret_ptr) {
        // sret functions always return void
        _builder->CreateRetVoid();
    } else if (named_ret_var) {
        // Named return variable (non-sret): load and return
        auto var_it = _context->_variables.find(named_ret_var);
        if (var_it != _context->_variables.end()) {
            llvm::Type* ret_type = _context->get_llvm_type(func->get_return_type());
            llvm::Value* loaded = _builder->CreateLoad(ret_type, var_it->second, "named_ret_load");
            _builder->CreateRet(loaded);
        } else {
            _builder->CreateRetVoid();
        }
    } else if (stmt.get_expression()) {
        if (_retval_alloca && ret_value) {
            // Load the stored return value (after destructors)
            llvm::Value* loaded = _builder->CreateLoad(
                _retval_alloca->getAllocatedType(), _retval_alloca, "ret_loaded");
            _builder->CreateRet(loaded);
        } else if (ret_value) {
            // No cleanup was needed, we have the value directly
            _builder->CreateRet(ret_value);
        } else {
            _builder->CreateRetVoid();
        }
    } else {
        _builder->CreateRetVoid();
    }
}

//
// Break
//

void symbol_resolver::visit_break_statement(break_statement& stmt)
{
    // Nothing to resolve
}

void type_reference_resolver::visit_break_statement(break_statement& stmt)
{
    // Nothing to resolve
}

void declaration_generator::visit_break_statement(break_statement& stmt) {
    // Nothing to do here
}

/**
 * Generate LLVM IR for a break statement.
 *
 * Steps:
 *   1. Emit cleanup for all block-scoped variables between the break and the loop boundary.
 *   2. Branch to the loop exit block.
 *   3. Create a new unreachable basic block for any code following the break.
 */
void implementation_generator::visit_break_statement(break_statement& stmt) {
    // Emit cleanup for all scopes between the break and the loop boundary.
    // _loop_cleanup_depth.top() tells us how many cleanup scopes existed when
    // the loop was entered; anything above that is inside the loop.
    size_t loop_depth = _loop_cleanup_depth.top();
    size_t current_depth = _cleanup_vars_stack.size();

    if (current_depth > loop_depth) {
        // Collect scope variable lists from innermost to the loop boundary
        std::stack<std::vector<std::shared_ptr<variable_statement>>> tmp = _cleanup_vars_stack;
        std::vector<std::vector<std::shared_ptr<variable_statement>>> loop_scopes;
        size_t count = current_depth - loop_depth;
        for (size_t i = 0; i < count && !tmp.empty(); ++i) {
            loop_scopes.push_back(tmp.top());
            tmp.pop();
        }
        // Emit destructor calls in reverse declaration order for each scope
        for (auto& scope_vars : loop_scopes) {
            for (auto it = scope_vars.rbegin(); it != scope_vars.rend(); ++it) {
                auto& var_stmt = *it;

                // NRVO: do NOT destroy the NRVO candidate
                if (_nrvo_candidate && var_stmt == _nrvo_candidate) continue;

                auto vt = var_stmt->get_type();
                auto var_it = _context->_variables.find(var_stmt);
                if (var_it == _context->_variables.end()) continue;
                llvm::AllocaInst* alloca = var_it->second;

                if (auto st_type = std::dynamic_pointer_cast<struct_type>(vt)) {
                    auto dtor = st_type->get_struct()->get_destructor();
                    if (!dtor) continue;
                    auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
                    if (dtor_it == _context->_functions.end()) continue;
                    _builder->CreateCall(dtor_it->second, {alloca});
                } else if (auto own_type = std::dynamic_pointer_cast<owner_type>(vt)) {
                    emit_owner_cleanup_if_nonnull(_builder.get(), get_module(), _context->_functions,
                        alloca, own_type->get_owned_type(), "break_owner");
                }
            }
        }
    }

    // Branch to loop exit block
    _builder->CreateBr(_loop_exit_blocks.top());

    // Create unreachable basic block for any code following the break
    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* after_break = llvm::BasicBlock::Create(**_context, "after-break");
    func->insert(func->end(), after_break);
    _builder->SetInsertPoint(after_break);
}

//
// Continue
//

void symbol_resolver::visit_continue_statement(continue_statement& stmt)
{
    // Nothing to resolve
}

void type_reference_resolver::visit_continue_statement(continue_statement& stmt)
{
    // Nothing to resolve
}

void declaration_generator::visit_continue_statement(continue_statement& stmt) {
    // Nothing to do here
}

/**
 * Generate LLVM IR for a continue statement.
 *
 * Steps:
 *   1. Emit cleanup for all block-scoped variables between the continue and the loop boundary.
 *   2. Branch to the loop continue block (condition for while, step for for).
 *   3. Create a new unreachable basic block for any code following the continue.
 */
void implementation_generator::visit_continue_statement(continue_statement& stmt) {
    // Emit cleanup for all scopes between the continue and the loop boundary.
    size_t loop_depth = _loop_cleanup_depth.top();
    size_t current_depth = _cleanup_vars_stack.size();

    if (current_depth > loop_depth) {
        // Collect scope variable lists from innermost to the loop boundary
        std::stack<std::vector<std::shared_ptr<variable_statement>>> tmp = _cleanup_vars_stack;
        std::vector<std::vector<std::shared_ptr<variable_statement>>> loop_scopes;
        size_t count = current_depth - loop_depth;
        for (size_t i = 0; i < count && !tmp.empty(); ++i) {
            loop_scopes.push_back(tmp.top());
            tmp.pop();
        }
        // Emit destructor calls in reverse declaration order for each scope
        for (auto& scope_vars : loop_scopes) {
            for (auto it = scope_vars.rbegin(); it != scope_vars.rend(); ++it) {
                auto& var_stmt = *it;

                // NRVO: do NOT destroy the NRVO candidate
                if (_nrvo_candidate && var_stmt == _nrvo_candidate) continue;

                auto vt = var_stmt->get_type();
                auto var_it = _context->_variables.find(var_stmt);
                if (var_it == _context->_variables.end()) continue;
                llvm::AllocaInst* alloca = var_it->second;

                if (auto st_type = std::dynamic_pointer_cast<struct_type>(vt)) {
                    auto dtor = st_type->get_struct()->get_destructor();
                    if (!dtor) continue;
                    auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
                    if (dtor_it == _context->_functions.end()) continue;
                    _builder->CreateCall(dtor_it->second, {alloca});
                } else if (auto own_type = std::dynamic_pointer_cast<owner_type>(vt)) {
                    emit_owner_cleanup_if_nonnull(_builder.get(), get_module(), _context->_functions,
                        alloca, own_type->get_owned_type(), "continue_owner");
                }
            }
        }
    }

    // Branch to loop continue block
    _builder->CreateBr(_loop_continue_blocks.top());

    // Create unreachable basic block for any code following the continue
    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* after_continue = llvm::BasicBlock::Create(**_context, "after-continue");
    func->insert(func->end(), after_continue);
    _builder->SetInsertPoint(after_continue);
}

//
// If-then-else
//

void symbol_resolver::visit_if_else_statement(if_else_statement& stmt)
{
    if(stmt.has_cond_var()) {
        stmt.get_cond_var()->accept(*this);
    }
    if(stmt.has_cond_var_with_test() || !stmt.has_cond_var()) {
        stmt.get_test_expr()->accept(*this);
    }

    stmt.get_then_stmt()->accept(*this);

    // Resolve else statement
    if(auto expr = stmt.get_else_stmt()) {
        expr->accept(*this);
    }
}

void type_reference_resolver::visit_if_else_statement(if_else_statement& stmt)
{
    if(stmt.has_cond_var()) {
        // Resolve the condition variable (type + init)
        stmt.get_cond_var()->accept(*this);

        if(stmt.has_cond_var_with_test()) {
            // if(var; test) form: resolve and cast the separate test expression to bool.
            // No bool-castability check on the variable itself — the test expression determines branching.
            auto expr = stmt.get_test_expr();
            expr->accept(*this);
            auto cast = adapt_type(expr, _context->from_type(primitive_type::BOOL));
            if(!cast) {
                throw_error(static_cast<unsigned int>(k::diag::statement_diag::ERR_IF_COND_NOT_BOOL), stmt.get_ast_if_else_stmt()->if_kw, "If test expression type must be convertible to bool");
            } else if(cast != expr) {
                stmt.set_test_expr(cast);
            }
        } else {
            // Classic if-let: the boolean test is derived from the variable's type at codegen time.
            // For non-nullable addressors (ref &, link +), the soft-fail mechanism
            // handles branching — no explicit bool cast needed.
            // For all other types, adapt_type to bool is done at codegen.
            // We just verify the type is convertible to bool here.
            auto var_type = stmt.get_cond_var()->get_type();
            if(var_type) {
                bool is_ref_or_link = std::dynamic_pointer_cast<reference_type>(var_type) != nullptr
                                   || std::dynamic_pointer_cast<link_type>(var_type) != nullptr;
                if(!is_ref_or_link) {
                    // Check convertibility to bool: primitives, pointers, owners, views,
                    // and aggregates with bool cast operator are ok.
                    bool can_cast = false;
                    if(std::dynamic_pointer_cast<primitive_type>(var_type)) can_cast = true;
                    else if(std::dynamic_pointer_cast<pointer_type>(var_type)) can_cast = true;
                    else if(std::dynamic_pointer_cast<owner_type>(var_type)) can_cast = true;
                    else if(std::dynamic_pointer_cast<view_type>(var_type)) can_cast = true;
                    else if(auto st_type = std::dynamic_pointer_cast<struct_type>(var_type)) {
                        // Check for bool cast operator via resolver
                        auto bool_type = _context->from_type(primitive_type::BOOL);
                        auto cast_fn = resolve_cast_operator_overload(st_type->get_struct(), bool_type);
                        if(cast_fn) can_cast = true;
                    }
                    if(!can_cast) {
                        throw_error(static_cast<unsigned int>(k::diag::statement_diag::ERR_IF_COND_NOT_BOOL), stmt.get_ast_if_else_stmt()->if_kw, "If condition variable type must be convertible to bool");
                    }
                }
            }
        }
    } else {
        // Resolve and cast test
        {
            auto expr = stmt.get_test_expr();
            expr->accept(*this);
            auto cast = adapt_type(expr, _context->from_type(primitive_type::BOOL));
            if(!cast) {
                throw_error(static_cast<unsigned int>(k::diag::statement_diag::ERR_IF_COND_NOT_BOOL), stmt.get_ast_if_else_stmt()->if_kw, "If test expression type must be convertible to bool");
            } else if(cast != expr ) {
                // Casted, assign casted expression as return expr.
                stmt.set_test_expr(cast);
            } else {
                // Compatible type, no need to cast.
            }
        }
    }

    // Resolve then statement
    stmt.get_then_stmt()->accept(*this);

    // Resolve else statement
    if(auto expr = stmt.get_else_stmt()) {
        expr->accept(*this);
    }
}

void declaration_generator::visit_if_else_statement(if_else_statement& stmt) {
    if(stmt.has_cond_var()) {
        stmt.get_cond_var()->accept(*this);
    }
    if(stmt.has_cond_var_with_test() || !stmt.has_cond_var()) {
        // Visit test_expr for declarations (e.g. lambdas or nested constructs)
    }
    stmt.get_then_stmt()->accept(*this);
    if(stmt.get_else_stmt()) {
        stmt.get_else_stmt()->accept(*this);
    }
}

/**
 * Emit cleanup (destructor / owner free) for a single variable.
 * Helper for if-let condition variable cleanup.
 */
void implementation_generator::emit_cond_var_cleanup(
    const std::shared_ptr<variable_statement>& var_stmt)
{
    auto vt = var_stmt->get_type();
    auto var_it = _context->_variables.find(var_stmt);
    if (var_it == _context->_variables.end()) return;
    llvm::AllocaInst* alloca = var_it->second;

    if (auto st_type = std::dynamic_pointer_cast<struct_type>(vt)) {
        auto dtor = st_type->get_struct()->get_destructor();
        if (!dtor) return;
        auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
        if (dtor_it == _context->_functions.end()) return;
        _builder->CreateCall(dtor_it->second, {alloca});
    } else if (auto own_type = std::dynamic_pointer_cast<owner_type>(vt)) {
        emit_owner_cleanup_if_nonnull(_builder.get(), get_module(), _context->_functions,
            alloca, own_type->get_owned_type(), "if_cond_var_cleanup");
    }
}

/**
 * Generate LLVM IR for an if-else statement.
 *
 * Steps:
 *   1. Create then/else/merge basic blocks (before condition evaluation).
 *   2. Set up _null_failure_bb so that link null-checks in the condition
 *      soft-fail to else (or continue) instead of trapping.
 *   3. Evaluate the condition expression.
 *   4. Restore _null_failure_bb (supports nesting).
 *   5. Emit conditional branch.
 *   6. Visit then-block, emit cleanup for cond var, emit branch to merge.
 *   7. Visit else-block (if present), emit cleanup for cond var, emit branch to merge.
 *   8. Set insertion point to merge block.
 */
void implementation_generator::visit_if_else_statement(if_else_statement& stmt) {

    bool has_else = (bool)stmt.get_else_stmt();
    bool has_cond_var = stmt.has_cond_var();
    bool has_cond_var_with_test = stmt.has_cond_var_with_test();

    // Step 1: Create then/else/merge basic blocks BEFORE evaluating the condition,
    // so that _null_failure_bb can reference them during condition codegen.
    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* then_block = llvm::BasicBlock::Create(**_context, "if-then", func);
    llvm::BasicBlock* else_block = has_else ? llvm::BasicBlock::Create(**_context, "if-else") : nullptr;
    llvm::BasicBlock* cont_block = llvm::BasicBlock::Create(**_context, "if-continue");

    // Step 2: Set up soft-fail destination for link null-checks in the condition.
    // For if(var; test) form, do NOT set up soft-fail — null assignments are fatal errors.
    auto* saved_null_failure_bb = _null_failure_bb;
    if(!has_cond_var_with_test) {
        _null_failure_bb = has_else ? else_block : cont_block;
    }

    if(has_cond_var) {
        // Emit the condition variable declaration (alloca + init)
        stmt.get_cond_var()->accept(*this);

        if(has_cond_var_with_test) {
            // if(var; test) form: variable declared, now evaluate the separate test expression.
            // No soft-fail, no bool cast of the variable — only the test expression matters.
            _null_failure_bb = saved_null_failure_bb;

            _value = nullptr;
            stmt.get_test_expr()->accept(*this);
            auto test_value = _value;
            _value = nullptr;

            emit_expression_temporaries_cleanup();

            // Emit conditional branch based on the test expression
            if(has_else) {
                _builder->CreateCondBr(test_value, then_block, else_block);
            } else {
                _builder->CreateCondBr(test_value, then_block, cont_block);
            }
        } else {
            // Classic if-let form: determine branch from the variable's type.
            auto var_type = stmt.get_cond_var()->get_type();
            bool is_ref_or_link = std::dynamic_pointer_cast<reference_type>(var_type) != nullptr
                               || std::dynamic_pointer_cast<link_type>(var_type) != nullptr;

            if(is_ref_or_link) {
                // For ref/link: the soft-fail mechanism handles null → else branching.
                // If we reach here, the ref/link was successfully bound → go to then.
                _null_failure_bb = saved_null_failure_bb;
                _builder->CreateBr(then_block);
            } else {
                _null_failure_bb = saved_null_failure_bb;

                // Generate bool cast from the variable value
                auto var_it = _context->_variables.find(stmt.get_cond_var());
                llvm::AllocaInst* alloca = var_it->second;

                llvm::Value* test_value = nullptr;
                if(auto prim_type = std::dynamic_pointer_cast<primitive_type>(var_type)) {
                    // Numeric primitive: != 0
                    llvm::Type* llvm_t = _context->get_llvm_type(var_type);
                    llvm::Value* loaded = _builder->CreateLoad(llvm_t, alloca, "if_cond_load");
                    if(prim_type->is_float()) {
                        test_value = _builder->CreateFCmpONE(loaded,
                            llvm::ConstantFP::get(llvm_t, 0.0), "if_cond_ne_zero");
                    } else {
                        test_value = _builder->CreateICmpNE(loaded,
                            llvm::ConstantInt::get(llvm_t, 0), "if_cond_ne_zero");
                    }
                } else if(std::dynamic_pointer_cast<pointer_type>(var_type)
                       || std::dynamic_pointer_cast<owner_type>(var_type)
                       || std::dynamic_pointer_cast<view_type>(var_type)) {
                    // Pointer/owner/view: != null
                    auto& llvm_ctx = _builder->getContext();
                    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
                    llvm::Value* loaded = _builder->CreateLoad(ptr_ty, alloca, "if_cond_ptr_load");
                    test_value = _builder->CreateICmpNE(loaded,
                        llvm::ConstantPointerNull::get(ptr_ty), "if_cond_ne_null");
                } else if(auto st_type = std::dynamic_pointer_cast<struct_type>(var_type)) {
                    // Aggregate: call bool cast operator (__operator_cv_bool)
                    auto agg = st_type->get_struct();
                    auto funcs = agg->get_functions("__operator_cv_bool");
                    if(!funcs.empty()) {
                        auto cast_fn = funcs[0];
                        auto fn_it = _context->_functions.find(cast_fn);
                        if(fn_it != _context->_functions.end()) {
                            test_value = _builder->CreateCall(fn_it->second, {alloca}, "if_cond_bool_cast");
                        }
                    }
                }

                if(!test_value) {
                    // Fallback: should not happen if type_reference_resolver validated
                    test_value = _builder->getTrue();
                }

                emit_expression_temporaries_cleanup();

                // Emit conditional branch
                if(has_else) {
                    _builder->CreateCondBr(test_value, then_block, else_block);
                } else {
                    _builder->CreateCondBr(test_value, then_block, cont_block);
                }
            }
        }
    } else {
        // Classic form: evaluate the condition expression
        _value = nullptr;
        stmt.get_test_expr()->accept(*this);
        auto test_value = _value;
        _value = nullptr;

        // Step 4: Restore previous _null_failure_bb (supports nested if-statements)
        _null_failure_bb = saved_null_failure_bb;

        // Destroy any struct temporaries created during condition evaluation.
        emit_expression_temporaries_cleanup();

        // Step 5: Emit conditional branch
        if(has_else) {
            _builder->CreateCondBr(test_value, then_block, else_block);
        } else {
            _builder->CreateCondBr(test_value, then_block, cont_block);
        }
    }

    // Step 6: Visit then-block, emit cleanup for cond var, emit branch to merge
    _builder->SetInsertPoint(then_block);
    stmt.get_then_stmt()->accept(*this);
    if(has_cond_var) {
        emit_cond_var_cleanup(stmt.get_cond_var());
    }
    _builder->CreateBr(cont_block);

    // Step 7: Visit else-block (if present), emit cleanup for cond var, emit branch to merge
    if(has_else) {
        func->insert(func->end(), else_block);
        _builder->SetInsertPoint(else_block);
        stmt.get_else_stmt()->accept(*this);
        // For ref/link soft-fail (classic if-let): the variable doesn't exist on the else path,
        // so skip cleanup. For if(var; test) form, cleanup always happens.
        // For all other types, clean up.
        if(has_cond_var) {
            if(has_cond_var_with_test) {
                // if(var; test) form: variable always exists, always cleanup
                emit_cond_var_cleanup(stmt.get_cond_var());
            } else {
                auto var_type = stmt.get_cond_var()->get_type();
                bool is_ref_or_link = std::dynamic_pointer_cast<reference_type>(var_type) != nullptr
                                   || std::dynamic_pointer_cast<link_type>(var_type) != nullptr;
                if(!is_ref_or_link) {
                    emit_cond_var_cleanup(stmt.get_cond_var());
                }
            }
        }
        _builder->CreateBr(cont_block);
    }

    // Step 8: Set insertion point to merge block
    func->insert(func->end(), cont_block);
    _builder->SetInsertPoint(cont_block);
}

//
// While
//

void symbol_resolver::visit_while_statement(while_statement& stmt)
{
    stmt.get_test_expr()->accept(*this);
    stmt.get_nested_stmt()->accept(*this);
}

void type_reference_resolver::visit_while_statement(while_statement& stmt)
{
    // Resolve and cast test
    {
        auto expr = stmt.get_test_expr();
        expr->accept(*this);
        auto cast = adapt_type(expr, _context->from_type(primitive_type::BOOL));
        if(!cast) {
            throw_error(static_cast<unsigned int>(k::diag::statement_diag::ERR_WHILE_COND_NOT_BOOL), stmt.get_ast_while_stmt()->while_kw, "While test expression type must be convertible to bool");
        } else if(cast != expr ) {
            // Casted, assign casted expression as return expr.
            stmt.set_test_expr(cast);
        } else {
            // Compatible type, no need to cast.
        }
    }

    // Resolve nested statement
    stmt.get_nested_stmt()->accept(*this);
}

void declaration_generator::visit_while_statement(while_statement& stmt) {
    stmt.get_nested_stmt()->accept(*this);
}

/**
 * Generate LLVM IR for a while loop.
 *
 * Steps:
 *   1. Create cond/body/after basic blocks.
 *   2. Evaluate condition, branch to body or after.
 *   3. Visit body block, branch back to cond.
 *   4. Set insertion point to after block.
 */
void implementation_generator::visit_while_statement(while_statement& stmt) {

    // Step 1: Create cond/body/after basic blocks
    // Retrieve current block and create nested and continue blocks
    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* while_block = llvm::BasicBlock::Create(**_context, "while-condition");
    llvm::BasicBlock* nested_block = llvm::BasicBlock::Create(**_context, "while-nested");
    llvm::BasicBlock* cont_block = llvm::BasicBlock::Create(**_context, "while-continue");

    // While test block
    _builder->CreateBr(while_block);
    func->insert(func->end(), while_block);
    _builder->SetInsertPoint(while_block);

    // Condition expression
    _value = nullptr;
    stmt.get_test_expr()->accept(*this);
    auto test_value = _value;
    _value = nullptr;

    // Step 2: Evaluate condition, branch to body or after
    // Destroy any struct temporaries created during condition evaluation
    emit_expression_temporaries_cleanup();

    // Step 3: Visit body block, branch back to cond
    // Do branching
    _builder->CreateCondBr(test_value, nested_block, cont_block);

    // Step 4: Set insertion point to after block
    // Nest block
    func->insert(func->end(), nested_block);
    _builder->SetInsertPoint(nested_block);

    // Push loop exit context for break statements
    _loop_exit_blocks.push(cont_block);
    _loop_continue_blocks.push(while_block);
    _loop_cleanup_depth.push(_cleanup_vars_stack.size());

    stmt.get_nested_stmt()->accept(*this);

    // Pop loop exit context
    _loop_cleanup_depth.pop();
    _loop_continue_blocks.pop();
    _loop_exit_blocks.pop();

    // Go back to test
    _builder->CreateBr(while_block);

    // Generate "continuation" block
    func->insert(func->end(), cont_block);
    _builder->SetInsertPoint(cont_block);
}

//
// For
//

void symbol_resolver::visit_for_statement(for_statement& stmt)
{
    // Resolve variable decl, if any
    if(auto decl = stmt.get_decl_stmt()) {
        decl->accept(*this);
    }

    // Resolve and cast test
    if(auto expr = stmt.get_test_expr()) {
        expr->accept(*this);
    }

    // Resolve step
    if(auto step = stmt.get_step_expr()) {
        step->accept(*this);
    }

    // Resolve nested statement
    stmt.get_nested_stmt()->accept(*this);
}

void type_reference_resolver::visit_for_statement(for_statement& stmt)
{
    // Resolve variable decl, if any
    if(auto decl = stmt.get_decl_stmt()) {
        decl->accept(*this);
    }

    // Resolve and cast test
    if(auto expr = stmt.get_test_expr()) {
        expr->accept(*this);
        auto cast = adapt_type(expr, _context->from_type(primitive_type::BOOL));
        if(!cast) {
            throw_error(static_cast<unsigned int>(k::diag::statement_diag::ERR_FOR_COND_NOT_BOOL), stmt.get_ast_for_stmt()->for_kw, "For test expression type must be convertible to bool");
        } else if(cast != expr ) {
            // Casted, assign casted expression as return expr.
            stmt.set_test_expr(cast);
        } else {
            // Compatible type, no need to cast.
        }
    }

    // Resolve step
    if(auto step = stmt.get_step_expr()) {
        step->accept(*this);
    }

    // Resolve nested statement
    stmt.get_nested_stmt()->accept(*this);
}

void declaration_generator::visit_for_statement(for_statement& stmt) {
    stmt.get_nested_stmt()->accept(*this);
}

/**
 * Generate LLVM IR for a for loop.
 *
 * Steps:
 *   1. Visit init statement/expression.
 *   2. Create cond/body/step/after basic blocks.
 *   3. Evaluate condition, branch to body or after.
 *   4. Visit body block, branch to step.
 *   5. Visit step expression, branch to cond.
 *   6. Set insertion point to after block.
 */
void implementation_generator::visit_for_statement(for_statement& stmt) {

    // Step 1: Visit init statement/expression
    // Retrieve current block and create nested and continue blocks
    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* for_block = llvm::BasicBlock::Create(**_context, "for-condition");
    llvm::BasicBlock* nested_block = llvm::BasicBlock::Create(**_context, "for-nested");
    llvm::BasicBlock* step_block = llvm::BasicBlock::Create(**_context, "for-step");
    llvm::BasicBlock* cont_block = llvm::BasicBlock::Create(**_context, "for-continue");

    // Generate variable decl, if any
    if(auto decl = stmt.get_decl_stmt()) {
        decl->accept(*this);
    }

    // If test block
    _builder->CreateBr(for_block);
    func->insert(func->end(), for_block);
    _builder->SetInsertPoint(for_block);

    // Condition expression
    if(auto test_expr = stmt.get_test_expr()) {
        _value = nullptr;
        stmt.get_test_expr()->accept(*this);
        auto test_value = _value;
        _value = nullptr;

        // Destroy any struct temporaries created during condition evaluation
        emit_expression_temporaries_cleanup();

        // Do branching
        _builder->CreateCondBr(test_value, nested_block, cont_block);
    } else {
        _builder->CreateBr(nested_block);
    }


    // Nest block
    func->insert(func->end(), nested_block);
    _builder->SetInsertPoint(nested_block);

    // Push loop context for break/continue statements
    // continue jumps to step_block, break jumps to cont_block
    _loop_exit_blocks.push(cont_block);
    _loop_continue_blocks.push(step_block);
    _loop_cleanup_depth.push(_cleanup_vars_stack.size());

    stmt.get_nested_stmt()->accept(*this);

    // Pop loop context
    _loop_cleanup_depth.pop();
    _loop_continue_blocks.pop();
    _loop_exit_blocks.pop();

    // Fall through to step block
    _builder->CreateBr(step_block);

    // Step block
    func->insert(func->end(), step_block);
    _builder->SetInsertPoint(step_block);

    // Step, if any
    if(auto step = stmt.get_step_expr()) {
        _value = nullptr;
        step->accept(*this);
        auto step_value = _value;
        _value = nullptr;

        // Destroy any struct temporaries created during step evaluation
        emit_expression_temporaries_cleanup();
    }

    // Go back to test
    _builder->CreateBr(for_block);

    // Generate "continuation" block
    func->insert(func->end(), cont_block);
    _builder->SetInsertPoint(cont_block);
}

//
// Expression statement
//

void symbol_resolver::visit_expression_statement(expression_statement& stmt)
{
    if(auto expr = stmt.get_expression()) {
        expr->accept(*this);
    }
}

void type_reference_resolver::visit_expression_statement(expression_statement& stmt)
{
    if(auto expr = stmt.get_expression()) {
        _replacement_expr = nullptr;
        expr->accept(*this);
        if (_replacement_expr) {
            stmt.set_expression(_replacement_expr);
            expr = _replacement_expr;
            _replacement_expr = nullptr;
        }

        // Warning 0x5010: if the expression produces an owner type and its result
        // is not assigned (bare expression statement), the allocated object will be
        // immediately discarded.  This covers both 'new Foo();' and a function call
        // returning T! used as a statement.
        if (type::is_owner(expr->get_type())) {
            warn(static_cast<unsigned int>(k::diag::statement_diag::WARN_UNUSED_EXPR_RESULT), expr->first_lexeme(),
                "Result of expression producing owner type '{}' is immediately discarded — "
                "the allocated object will be deleted right after construction",
                {expr->get_type()->to_string()});
        }
    }
}

void declaration_generator::visit_expression_statement(expression_statement& stmt) {
    // Nothing to do here (nothing in expressions)
}

void implementation_generator::visit_expression_statement(expression_statement& stmt) {
    if(auto expr = stmt.get_expression()) {
        expr->accept(*this);

        // If the expression produces an owner type (e.g. bare 'new Foo();' or a function
        // call returning T!), the result is unassigned.  Immediately destroy and free
        // the allocated object so it does not leak.
        if (_value && type::is_owner(expr->get_type())) {
            auto own_type = std::dynamic_pointer_cast<owner_type>(expr->get_type());
            if (own_type) {
                auto alloc_type = own_type->get_owned_type();
                emit_owner_object_destroy(_builder.get(), get_module(),
                    _context->_functions, _value, alloc_type);
                _value = nullptr;
            }
        }

        // Destroy any struct temporaries created during expression evaluation
        emit_expression_temporaries_cleanup();
    }
}

//
// Variable statement
//

void symbol_resolver::visit_variable_statement(variable_statement& var)
{
    visit_named_element(var);

    if (auto expr = var.get_init_expr()) {
        expr->accept(*this);
    }
}

/**
 * Resolve a variable statement (local variable declaration with optional init).
 *
 * Steps:
 *   1. Delegate type and init expression resolution to visit_variable_definition.
 *   2. Track struct-typed and owner-typed variables for block-scoped cleanup.
 */
void type_reference_resolver::visit_variable_statement(variable_statement& var)
{
    // Step 1: Delegate type and init expression resolution to visit_variable_definition
    // For local variables, first try to resolve qualified types using the statement's
    // element context (which allows walking up the scope chain).
    if (!type::is_resolved(var.get_type())) {
        if (auto ufrt = std::dynamic_pointer_cast<unresolved_function_ref_type>(var.get_type())) {
            // Function reference type (*(int), ^(int), ~()): resolve using the variable's
            // element context for scope-based type lookup.
            auto resolved = resolve_function_ref_type(ufrt, static_cast<const element&>(var));
            if (resolved && type::is_resolved(resolved)) {
                var.set_type(resolved);
            }
        } else {
        auto unres_type = std::dynamic_pointer_cast<unresolved_type>(var.get_type());
        if (unres_type) {
            std::shared_ptr<type> resolved;
            if (unres_type->type_id().has_root_prefix()) {
                resolved = resolve_type_from_root(unres_type->type_id().without_root_prefix());
            } else {
                // var is a statement (element), so walk up from it
                resolved = resolve_type_by_name(unres_type->type_id(), static_cast<const element&>(var));
            }
            // Fallback: try imported aggregates
            if (!resolved || !type::is_resolved(resolved)) {
                if (auto imported_agg = _unit.get_or_create_imported_aggregate(
                        unres_type->type_id(), _context)) {
                    resolved = imported_agg->get_struct_type();
                }
            }
            // Fallback: try imported enums
            if (!resolved || !type::is_resolved(resolved)) {
                if (auto imported_en = _unit.get_or_create_imported_enum(
                        unres_type->type_id(), _context)) {
                    resolved = imported_en->get_enum_type();
                }
            }
            if (resolved && type::is_resolved(resolved)) {
                var.set_type(resolved);
            }
        }
        } // end else (not unresolved_function_ref_type)
    }
    visit_variable_definition(var);
}

void declaration_generator::visit_variable_statement(variable_statement& stmt) {
    // Nothing to do here (nothing in expressions)
}

/**
 * Generate LLVM IR for a local variable declaration.
 *
 * Steps:
 *   1. Allocate stack space (alloca) for the variable.
 *   2. For NRVO candidates: alias alloca to sret pointer.
 *   3. Evaluate init expression (if any).
 *   4. For struct types with constructor: call constructor.
 *   5. For primitives: store init value.
 *   6. For owner/pointer/link/view: store init value.
 *   7. Emit expression temporaries cleanup.
 *   8. Track variable for block-scoped cleanup.
 */
void implementation_generator::visit_variable_statement(variable_statement& var) {
    // Create the alloca at beginning of the function ...
    auto var_func = var.get_function();
    auto func = _context->_functions[var_func];
    llvm::IRBuilder<> build(&func->getEntryBlock(),func->getEntryBlock().begin());

    std::shared_ptr<k::model::type> var_type = var.get_type();
    llvm::Type *  type = _context->get_llvm_type(var_type);

    // Step 1: Allocate stack space (alloca) for the variable
    // NRVO: if this variable is the NRVO candidate, we still create a normal alloca here.
    // After the function body is fully generated, visit_function will RAUW (Replace All
    // Uses With) this alloca with _sret_ptr and erase it — achieving zero-copy NRVO
    // while keeping AllocaInst* in the _variables map during code generation.
    llvm::AllocaInst* alloca;
    bool is_nrvo_var = (_nrvo_candidate && var.shared_as<variable_statement>() == _nrvo_candidate && _sret_ptr);

    // Step 2: For NRVO candidates: alias alloca to sret pointer
    alloca = build.CreateAlloca(type, nullptr, var.get_short_name());
    _context->_variables.insert({var.shared_as<variable_statement>(), alloca});

    // But initialize at the decl place
    auto init = var.get_init_expr();
    if (k::model::type::is_owner(var_type)
        || k::model::type::is_pointer(var_type)
        || k::model::type::is_link(var_type)
        || k::model::type::is_view(var_type)) {
        // ...existing owner/pointer/link/pin init code...
        auto& llvm_ctx = _builder->getContext();
        auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
        _builder->CreateStore(llvm::ConstantPointerNull::get(ptr_ty), alloca);
        if (init != nullptr) {
            _value = nullptr;
            init->accept(*this);
            if (_value) {
                auto init_type = init->get_type();
                llvm::Value* addr_val = _value;
                if (k::model::type::is_reference(init_type)) {
                    auto sub = std::dynamic_pointer_cast<k::model::reference_type>(init_type);
                    auto inner = sub->get_subtype();
                    if (k::model::type::is_any_indirection(inner)) {
                        llvm::Type* inner_llvm = _context->get_llvm_type(inner);
                        addr_val = _builder->CreateLoad(inner_llvm, _value, "indir_init_load");
                    }
                }
                // For link/reference variables initialized from a nullable source,
                // emit a null-check with soft-fail when _null_failure_bb is set.
                if ((k::model::type::is_link(var_type) || k::model::type::is_reference(var_type))
                    && _null_failure_bb) {
                    auto* cur_fn = _builder->GetInsertBlock()->getParent();
                    auto* ok_bb = llvm::BasicBlock::Create(_builder->getContext(), "link_init_ok", cur_fn);
                    auto* is_null = _builder->CreateICmpEQ(
                        addr_val,
                        llvm::ConstantPointerNull::get(ptr_ty),
                        "link_init_null_check");
                    _builder->CreateCondBr(is_null, _null_failure_bb, ok_bb);
                    _builder->SetInsertPoint(ok_bb);
                }
                _builder->CreateStore(addr_val, alloca);
            }
        }
    } else if (init != nullptr) {
        // Set _sret_destination for sret-returning function calls so they write
        // directly into this variable's alloca (no intermediate copy)
        bool set_sret_dest = !is_nrvo_var && needs_sret_return(var_type);
        if (set_sret_dest) {
            _sret_destination = alloca;
        }

        _value = nullptr;
        init->accept(*this);

        if (set_sret_dest) {
            _sret_destination = nullptr; // ensure cleanup even if init didn't consume it
        }

        // constructor_invocation_expression handles the store for all types.
    } else if (k::model::type::is_sized_array(var_type)) {
        auto sized_arr = std::dynamic_pointer_cast<k::model::sized_array_type>(var_type);
        auto* struct_llvm = sized_arr->get_llvm_struct_type();
        _builder->CreateStore(llvm::ConstantAggregateZero::get(struct_llvm), alloca);
        llvm::Value* size_ptr = _builder->CreateStructGEP(struct_llvm, alloca,
            k::model::sized_array_type::FIELD_SIZE, "arr_size");
        _builder->CreateStore(
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(_builder->getContext()),
                sized_arr->get_size(), false),
            size_ptr);
    } else {
        lex::opt_any_lexeme var_lexeme;
        if (auto ast_decl = var.get_ast_variable_decl()) {
            var_lexeme = lex::any_lexeme{ast_decl->name};
        }
        throw_error(static_cast<unsigned int>(k::diag::statement_diag::ERR_LOCAL_VAR_TYPE_UNRESOLVED), var_lexeme,
            "Variable '{}' has no initialisation expression; "
            "all variable declarations must have an initialiser (uninitialized variables are not yet supported)",
            {var.get_fq_name()});
    }

    // Step 3: Evaluate init expression (if any)
    // Destroy any struct temporaries created during the init expression evaluation
    // Step 7: Emit expression temporaries cleanup
    emit_expression_temporaries_cleanup();

}


} // namespace k::model::gen
