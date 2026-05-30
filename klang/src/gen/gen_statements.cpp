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
#include "../model/mangler.hpp"
#include "../errors.hpp"

namespace k::model::gen {

using namespace k::model;

// ═══════════════════════════════════════════════════════════════════════════════
// Exception-aware call/invoke helper
// ═══════════════════════════════════════════════════════════════════════════════

llvm::Value* implementation_generator::create_call_or_invoke(
    llvm::FunctionType* fn_type, llvm::Value* callee,
    llvm::ArrayRef<llvm::Value*> args, const llvm::Twine& name)
{
    if (!_landing_pad_stack.empty()) {
        // Inside a try-catch: use invoke so exceptions unwind to the landing pad
        auto& llvm_ctx = _context->llvm_context();
        auto* current_func = _builder->GetInsertBlock()->getParent();
        auto* normal_bb = llvm::BasicBlock::Create(llvm_ctx, "invoke_cont", current_func);
        auto* invoke_inst = _builder->CreateInvoke(
            fn_type, callee, normal_bb, _landing_pad_stack.top().lpad_bb, args);
        if (!fn_type->getReturnType()->isVoidTy() && !name.isTriviallyEmpty()) {
            invoke_inst->setName(name);
        }
        _builder->SetInsertPoint(normal_bb);
        return invoke_inst;
    }
    // Not inside try-catch: plain call
    return _builder->CreateCall(fn_type, callee, args, name);
}

llvm::Value* implementation_generator::create_call_or_invoke(
    llvm::FunctionCallee callee, llvm::ArrayRef<llvm::Value*> args,
    const llvm::Twine& name)
{
    return create_call_or_invoke(callee.getFunctionType(), callee.getCallee(), args, name);
}

/**
 * Find the union_type_def whose struct_type matches the given type.
 * Searches all namespaces in the unit (currently only root namespace).
 * Returns nullptr if not found or if the struct_type belongs to a regular aggregate.
 */
static std::shared_ptr<union_type_def> find_union_for_struct_type(
    unit& u, const std::shared_ptr<struct_type>& st)
{
    if (!st || st->get_struct()) return nullptr; // has owning aggregate → not a union
    auto root_ns = u.get_root_namespace();
    if (!root_ns) return nullptr;
    return find_union_by_struct_type(root_ns, st);
}

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
    // an owner type (owner variables must be freed at scope exit), or a union with
    // non-trivially-destructible alternatives, in declaration order.
    std::vector<std::shared_ptr<variable_statement>> dtor_vars;
    for (auto& stmt : blk.get_statements()) {
        if (auto var_stmt = std::dynamic_pointer_cast<variable_statement>(stmt)) {
            auto vt = var_stmt->get_type();
            // Struct with destructor
            if (auto st_type = std::dynamic_pointer_cast<struct_type>(vt)) {
                if (st_type->get_struct() && st_type->get_struct()->get_destructor()) {
                    dtor_vars.push_back(var_stmt);
                }
                // Union with non-trivial alternative(s)
                else if (!st_type->get_struct()) {
                    if (auto udef = find_union_for_struct_type(_unit, st_type)) {
                        if (udef->has_nontrivial_destructor_alternative()) {
                            dtor_vars.push_back(var_stmt);
                        }
                    }
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

            // Step 3: On block exit: emit destructor calls for all block-scoped struct/owner/union variables
            if (auto st_type = std::dynamic_pointer_cast<struct_type>(vt)) {
                if (st_type->get_struct()) {
                    // Struct destructor
                    auto dtor = st_type->get_struct()->get_destructor();
                    if (!dtor) continue;
                    auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
                    if (dtor_it == _context->_functions.end()) continue;
                    _builder->CreateCall(dtor_it->second, {alloca});
                } else {
                    // Union with non-trivial alternatives: switch on discriminant
                    auto udef = find_union_for_struct_type(_unit, st_type);
                    if (!udef) continue;
                    emit_union_cleanup(alloca, *udef);
                }
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

    // Emit finally blocks for enclosing try-catch-finally scopes (innermost to outermost).
    // If we are inside a catch body, also emit __cxa_end_catch() before the finally.
    if (!_finally_stack.empty()) {
        auto& mod = _context->module();
        auto* ptr_ty = llvm::PointerType::get(_context->llvm_context(), 0);
        auto cxa_end_fn = mod.getOrInsertFunction("__cxa_end_catch",
            llvm::FunctionType::get(llvm::Type::getVoidTy(_context->llvm_context()), {}, false));

        // Copy the stack to iterate from top (innermost) to bottom (outermost)
        std::stack<finally_context> tmp = _finally_stack;
        std::vector<finally_context> finally_contexts;
        while (!tmp.empty()) {
            finally_contexts.push_back(tmp.top());
            tmp.pop();
        }
        for (auto& ctx : finally_contexts) {
            if (ctx.in_catch) {
                _builder->CreateCall(cxa_end_fn);
            }
            if (ctx.finally_body) {
                ctx.finally_body->accept(*this);
            }
        }
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
                    if (st_type->get_struct()) {
                        auto dtor = st_type->get_struct()->get_destructor();
                        if (!dtor) continue;
                        auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
                        if (dtor_it == _context->_functions.end()) continue;
                        _builder->CreateCall(dtor_it->second, {alloca});
                    } else {
                        // Union with non-trivial alternatives
                        auto udef = find_union_for_struct_type(_unit, st_type);
                        if (udef) emit_union_cleanup(alloca, *udef);
                    }
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

    // Emit finally blocks between the break and the loop boundary
    size_t loop_finally_depth = _loop_finally_depth.top();
    size_t current_finally_depth = _finally_stack.size();
    if (current_finally_depth > loop_finally_depth) {
        auto& mod = _context->module();
        auto cxa_end_fn = mod.getOrInsertFunction("__cxa_end_catch",
            llvm::FunctionType::get(llvm::Type::getVoidTy(_context->llvm_context()), {}, false));

        std::stack<finally_context> tmp_finally = _finally_stack;
        size_t count = current_finally_depth - loop_finally_depth;
        for (size_t i = 0; i < count && !tmp_finally.empty(); ++i) {
            auto ctx = tmp_finally.top();
            tmp_finally.pop();
            if (ctx.in_catch) {
                _builder->CreateCall(cxa_end_fn);
            }
            if (ctx.finally_body) {
                ctx.finally_body->accept(*this);
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

    // Emit finally blocks between the continue and the loop boundary
    size_t loop_finally_depth_cont = _loop_finally_depth.top();
    size_t current_finally_depth_cont = _finally_stack.size();
    if (current_finally_depth_cont > loop_finally_depth_cont) {
        auto& mod = _context->module();
        auto cxa_end_fn = mod.getOrInsertFunction("__cxa_end_catch",
            llvm::FunctionType::get(llvm::Type::getVoidTy(_context->llvm_context()), {}, false));

        std::stack<finally_context> tmp_finally = _finally_stack;
        size_t count = current_finally_depth_cont - loop_finally_depth_cont;
        for (size_t i = 0; i < count && !tmp_finally.empty(); ++i) {
            auto ctx = tmp_finally.top();
            tmp_finally.pop();
            if (ctx.in_catch) {
                _builder->CreateCall(cxa_end_fn);
            }
            if (ctx.finally_body) {
                ctx.finally_body->accept(*this);
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
// Throw statement
//

void symbol_resolver::visit_throw_statement(throw_statement& stmt)
{
    if(auto expr = stmt.get_expression()) {
        expr->accept(*this);
    }
}

void type_reference_resolver::visit_throw_statement(throw_statement& stmt)
{
    if(auto expr = stmt.get_expression()) {
        _replacement_expr = nullptr;
        expr->accept(*this);
        if (_replacement_expr) {
            stmt.set_expression(_replacement_expr);
            expr = _replacement_expr;
            _replacement_expr = nullptr;
        }
    }

    // Bare 'throw;' — rethrow: validate that we are inside a catch block
    if (!stmt.get_expression()) {
        lex::opt_any_lexeme throw_lexeme;
        if (auto ast_node = stmt.get_ast_node_as<k::parse::ast::throw_statement>()) {
            throw_lexeme = lex::any_lexeme{ast_node->throw_kw};
        }

        if (_catch_clause_stack.empty()) {
            throw_error(static_cast<unsigned int>(k::diag::exception_diag::ERR_RETHROW_OUTSIDE_CATCH),
                        throw_lexeme,
                        "Bare 'throw;' (rethrow) is only valid inside a catch block");
        } else {
            // Contract check: the rethrown exception type must be declared or caught
            auto& cs = _catch_clause_stack.back();
            if (cs.caught_type) {
                check_throw_contract(cs.caught_type, throw_lexeme);
            }
        }
        return;
    }

    // Validate that the thrown type derives from ::k::Throwable
    if (auto expr = stmt.get_expression()) {
        if (auto expr_type = expr->get_type()) {
            auto thrown_agg = get_exception_aggregate(expr_type);
            if (thrown_agg) {
                // Look up ::k::Throwable in the model (local or imported)
                std::shared_ptr<aggregate> throwable_class;
                // First try local namespaces
                auto root_ns = _unit.get_root_namespace();
                if (root_ns) {
                    auto k_ns = root_ns->get_child_namespace("k");
                    if (k_ns) {
                        throwable_class = k_ns->get_aggregate("Throwable");
                    }
                }
                // If not found locally, try imported aggregates (from KDI)
                if (!throwable_class) {
                    k::name thr_name(false, {"k", "Throwable"});
                    throwable_class = _unit.get_or_create_imported_aggregate(thr_name, _context);
                }
                if (throwable_class) {
                    // Thrown type must be Throwable itself or derive from it
                    if (thrown_agg != throwable_class && !thrown_agg->is_derived_from(throwable_class)) {
                        throw_error(static_cast<unsigned int>(k::diag::exception_diag::ERR_THROW_NOT_EXCEPTION_TYPE),
                                    expr->first_lexeme(),
                                    "Cannot throw type '{}': only types derived from ::k::Throwable can be thrown",
                                    {thrown_agg->get_short_name()});
                    }
                }
                // If ::k::Throwable is not found (e.g. module 'k' itself), skip the check
            } else {
                // Non-struct type (primitive, etc.) — cannot be thrown
                throw_error(static_cast<unsigned int>(k::diag::exception_diag::ERR_THROW_NOT_EXCEPTION_TYPE),
                            expr->first_lexeme(),
                            "Cannot throw a non-class type: only types derived from ::k::Throwable can be thrown",
                            {});
            }

            // Exception contract check: verify thrown type is declared in the function's throws clause
            // or caught by an enclosing try-catch (only for checked Exception-derived types)
            check_throw_contract(expr_type, expr->first_lexeme());
        }
    }
}

void declaration_generator::visit_throw_statement(throw_statement& stmt) {
    // Nothing to do here
}

void implementation_generator::visit_throw_statement(throw_statement& stmt) {
    auto expr = stmt.get_expression();
    if (!expr) {
        // Bare "throw;" — rethrow the current exception via __cxa_rethrow.
        // This re-throws the exception that was started with __cxa_begin_catch.
        auto& llvm_ctx = _context->llvm_context();
        auto& mod = _context->module();
        auto* void_ty = llvm::Type::getVoidTy(llvm_ctx);
        auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
        auto* current_func = _builder->GetInsertBlock()->getParent();

        auto cxa_rethrow = mod.getOrInsertFunction("__cxa_rethrow",
            llvm::FunctionType::get(void_ty, {}, false));

        if (!_landing_pad_stack.empty()) {
            // Inside a try-catch: use invoke so the rethrown exception unwinds to the landing pad
            auto* after_rethrow = llvm::BasicBlock::Create(llvm_ctx, "after_rethrow", current_func);
            auto* invoke_inst = _builder->CreateInvoke(
                cxa_rethrow, after_rethrow, _landing_pad_stack.top().lpad_bb, {});
            invoke_inst->setDoesNotReturn();
            _builder->SetInsertPoint(after_rethrow);
            _builder->CreateUnreachable();
            // Create continuation block for any code after throw (unreachable in practice)
            auto* cont_bb = llvm::BasicBlock::Create(llvm_ctx, "post_rethrow", current_func);
            _builder->SetInsertPoint(cont_bb);
        } else {
            // Not inside a try-catch: plain call (unwinds past this frame)
            auto* call = _builder->CreateCall(cxa_rethrow, {});
            call->setDoesNotReturn();
            _builder->CreateUnreachable();
            auto* after_rethrow = llvm::BasicBlock::Create(llvm_ctx, "after_rethrow", current_func);
            _builder->SetInsertPoint(after_rethrow);
        }
        return;
    }

    // Evaluate the throw expression
    _value = nullptr;
    expr->accept(*this);
    llvm::Value* throw_val = _value;
    _value = nullptr;

    if (!throw_val) {
        // Fallback: trap if expression evaluation failed
        auto* trap_fn = llvm::Intrinsic::getDeclaration(&_context->module(), llvm::Intrinsic::trap);
        _builder->CreateCall(trap_fn);
        _builder->CreateUnreachable();
        auto* func = _builder->GetInsertBlock()->getParent();
        auto* after_throw = llvm::BasicBlock::Create(_context->llvm_context(), "after_throw", func);
        _builder->SetInsertPoint(after_throw);
        return;
    }

    auto& llvm_ctx = _context->llvm_context();
    auto& mod = _context->module();
    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
    auto* i64_ty = llvm::Type::getInt64Ty(llvm_ctx);
    auto* void_ty = llvm::Type::getVoidTy(llvm_ctx);

    // Determine the exception object's type and size
    // Peel reference/const wrappers: temporary_construction_expression sets the
    // expression type as reference_type<struct_type>, but the actual object is
    // the struct itself. We need the struct size, not the pointer size.
    auto expr_type = expr->get_type();
    auto obj_type = expr_type;
    while (obj_type && obj_type->get_subtype() &&
           (type::is_reference(obj_type) || type::is_const(obj_type))) {
        obj_type = obj_type->get_subtype();
    }
    llvm::Type* llvm_exc_type = _context->get_llvm_type(obj_type);
    uint64_t exc_size = mod.getDataLayout().getTypeAllocSize(llvm_exc_type);

    // 1. __cxa_allocate_exception(size_t) -> void*
    auto cxa_alloc = mod.getOrInsertFunction("__cxa_allocate_exception",
        llvm::FunctionType::get(ptr_ty, {i64_ty}, false));
    llvm::Value* exc_mem = _builder->CreateCall(cxa_alloc,
        {llvm::ConstantInt::get(i64_ty, exc_size)}, "exc_mem");

    // 2. Copy the exception value into the allocated memory
    if (llvm_exc_type->isAggregateType()) {
        // For struct types, use memcpy
        // If throw_val is a pointer/alloca, load from it; if it's a value, store then copy
        if (throw_val->getType()->isPointerTy()) {
            // Source is a pointer — memcpy from it
            _builder->CreateMemCpy(exc_mem, llvm::MaybeAlign(1),
                                   throw_val, llvm::MaybeAlign(1), exc_size);
        } else {
            // Have an aggregate value — store to temporary, then memcpy
            auto* tmp = _builder->CreateAlloca(llvm_exc_type, nullptr, "exc_tmp");
            _builder->CreateStore(throw_val, tmp);
            _builder->CreateMemCpy(exc_mem, llvm::MaybeAlign(1),
                                   tmp, llvm::MaybeAlign(1), exc_size);
        }
    } else {
        // For primitive types, store directly
        _builder->CreateStore(throw_val, exc_mem);
    }

    // 3. Build or retrieve the typeinfo global for this exception type.
    //    We use a simple Itanium ABI compatible typeinfo: an external global
    //    named _KTI<mangled_type> that serves as the type identifier.
    //    For matching in the catch clause, both throw and catch must use the
    //    same global — pointer equality is all that matters.
    //    Peel addresser wrappers (reference, pointer, const) to get the inner type.
    auto inner_type = obj_type;
    while (inner_type && inner_type->get_subtype() &&
           (type::is_pointer(inner_type) || type::is_reference(inner_type) ||
            type::is_const(inner_type))) {
        inner_type = inner_type->get_subtype();
    }
    std::string typeinfo_name;
    std::shared_ptr<aggregate> thrown_agg;
    if (auto st = std::dynamic_pointer_cast<struct_type>(inner_type)) {
        if (auto agg = st->get_struct()) {
            typeinfo_name = mangler::mangle_rtti(agg->get_name());
            thrown_agg = agg;
        } else {
            typeinfo_name = "_KTI_" + st->name();
        }
    } else {
        typeinfo_name = "_KTI_" + (inner_type ? inner_type->to_string() : expr_type->to_string());
    }
    auto* typeinfo_gv = mod.getNamedGlobal(typeinfo_name);
    if (!typeinfo_gv) {
        typeinfo_gv = new llvm::GlobalVariable(
            mod, ptr_ty, /*isConstant=*/true,
            llvm::GlobalValue::LinkOnceODRLinkage,
            llvm::ConstantPointerNull::get(ptr_ty),
            typeinfo_name);
    }

    // 4. Build the typeinfo chain: [{ti, offset}, {ti, offset}, ..., {null, 0}]
    //    This supports polymorphic catch (catching by base class).
    //    Each entry contains the typeinfo pointer and the byte offset of the corresponding
    //    base sub-object within the thrown object's memory layout.
    //    Store the chain in a module-level constant array, then store its pointer
    //    in _k_thrown_typeinfo_chain global.
    auto* i32_type = llvm::Type::getInt32Ty(llvm_ctx);
    auto* chain_entry_ty = llvm::StructType::get(llvm_ctx, {ptr_ty, i32_type});

    struct chain_entry_data {
        llvm::Constant* ti;
        uint32_t offset;
    };
    std::vector<chain_entry_data> chain_data;
    chain_data.push_back({typeinfo_gv, 0}); // Self — offset 0

    if (thrown_agg) {
        // Walk the base class hierarchy and compute byte offsets for each base sub-object.
        // K's class layout: each class struct has { vptr, base_subobject, ...fields }.
        // The base_subobject is typically at field index 1 in the LLVM struct.
        const auto& data_layout = mod.getDataLayout();
        std::function<void(const std::shared_ptr<aggregate>&, uint32_t)> add_bases;
        add_bases = [&](const std::shared_ptr<aggregate>& agg, uint32_t cumulative_offset) {
            for (auto& bs : agg->get_bases()) {
                std::shared_ptr<aggregate> base_agg = bs.base;
                // For imported aggregates, base_spec.base may still be null
                if (!base_agg && !bs.raw_name.empty()) {
                    std::vector<std::string> parts;
                    std::size_t pos = 0;
                    std::string raw = bs.raw_name;
                    if (raw.size() >= 2 && raw[0] == ':' && raw[1] == ':') {
                        raw = raw.substr(2);
                    }
                    while (true) {
                        auto sep = raw.find("::", pos);
                        if (sep == std::string::npos) { parts.push_back(raw.substr(pos)); break; }
                        parts.push_back(raw.substr(pos, sep - pos));
                        pos = sep + 2;
                    }
                    k::name lookup_name{false, std::move(parts)};
                    base_agg = _unit.get_or_create_imported_aggregate(lookup_name, _context);
                }
                if (!base_agg) continue;

                // Compute the byte offset of this base sub-object within the parent.
                // In K's layout, the base sub-object is the field named "__base_<Name>__"
                // which is at a specific struct offset. Use the DataLayout to get the
                // exact byte offset of the base sub-object field.
                uint32_t field_offset = 0;
                auto agg_st = agg->get_struct_type();
                if (agg_st) {
                    llvm::Type* agg_llvm_type = _context->get_llvm_type(agg_st);
                    if (agg_llvm_type && agg_llvm_type->isStructTy()) {
                        auto* struct_ty = llvm::cast<llvm::StructType>(agg_llvm_type);
                        auto* sl = data_layout.getStructLayout(struct_ty);
                        // The base sub-object field is typically at index 1 (after vptr).
                        // For single inheritance with a class/interface, field 0 is __vptr__
                        // and field 1 is the base sub-object.
                        // For structs (non-class), field 0 is the base sub-object directly.
                        // We find the correct field by matching the type.
                        auto base_st = base_agg->get_struct_type();
                        llvm::Type* base_llvm = base_st ? _context->get_llvm_type(base_st) : nullptr;
                        unsigned num_fields = struct_ty->getNumElements();
                        for (unsigned fi = 0; fi < num_fields; ++fi) {
                            if (struct_ty->getElementType(fi) == base_llvm) {
                                field_offset = (uint32_t)sl->getElementOffset(fi);
                                break;
                            }
                        }
                    }
                }

                uint32_t base_offset = cumulative_offset + field_offset;

                std::string base_ti_name = mangler::mangle_rtti(base_agg->get_name());
                auto* base_ti = mod.getNamedGlobal(base_ti_name);
                if (!base_ti) {
                    base_ti = new llvm::GlobalVariable(
                        mod, ptr_ty, /*isConstant=*/true,
                        llvm::GlobalValue::LinkOnceODRLinkage,
                        llvm::ConstantPointerNull::get(ptr_ty),
                        base_ti_name);
                }
                chain_data.push_back({base_ti, base_offset});
                add_bases(base_agg, base_offset); // Recursive
            }
        };
        add_bases(thrown_agg, 0);
    }

    // Build the LLVM constant array from chain_data
    std::vector<llvm::Constant*> chain_entries;
    for (auto& entry : chain_data) {
        chain_entries.push_back(llvm::ConstantStruct::get(chain_entry_ty,
            {entry.ti, llvm::ConstantInt::get(i32_type, entry.offset)}));
    }
    // Null terminator
    chain_entries.push_back(llvm::ConstantStruct::get(chain_entry_ty,
        {llvm::ConstantPointerNull::get(ptr_ty), llvm::ConstantInt::get(i32_type, 0)}));

    // Create a constant array for the chain
    auto* chain_arr_ty = llvm::ArrayType::get(chain_entry_ty, chain_entries.size());
    auto* chain_initializer = llvm::ConstantArray::get(chain_arr_ty, chain_entries);
    std::string chain_global_name = typeinfo_name + "_chain";
    auto* chain_arr_gv = mod.getNamedGlobal(chain_global_name);
    if (!chain_arr_gv) {
        chain_arr_gv = new llvm::GlobalVariable(
            mod, chain_arr_ty, /*isConstant=*/true,
            llvm::GlobalValue::LinkOnceODRLinkage,
            chain_initializer,
            chain_global_name);
    }

    // 5. Store the chain pointer in _k_thrown_typeinfo_chain. This global holds
    //    a pointer to the null-terminated typeinfo array for the current exception.
    auto* ti_chain_global = mod.getNamedGlobal("_k_thrown_typeinfo_chain");
    if (!ti_chain_global) {
        ti_chain_global = new llvm::GlobalVariable(
            mod, ptr_ty, /*isConstant=*/false,
            llvm::GlobalValue::ExternalLinkage,
            llvm::ConstantPointerNull::get(ptr_ty),
            "_k_thrown_typeinfo_chain");
    }
    _builder->CreateStore(chain_arr_gv, ti_chain_global);

    // Also store the primary typeinfo for backward compatibility
    auto* ti_global = mod.getNamedGlobal("_k_thrown_typeinfo");
    if (!ti_global) {
        ti_global = new llvm::GlobalVariable(
            mod, ptr_ty, /*isConstant=*/false,
            llvm::GlobalValue::ExternalLinkage,
            llvm::ConstantPointerNull::get(ptr_ty),
            "_k_thrown_typeinfo");
    }
    _builder->CreateStore(typeinfo_gv, ti_global);

    // 5. __cxa_throw(void* thrown_exception, void* tinfo, void (*dest)(void*))
    //    The destructor pointer is null for now (no cleanup needed for plain structs).
    auto cxa_throw = mod.getOrInsertFunction("__cxa_throw",
        llvm::FunctionType::get(void_ty, {ptr_ty, ptr_ty, ptr_ty}, false));
    auto* current_func = _builder->GetInsertBlock()->getParent();

    if (!_landing_pad_stack.empty()) {
        // Inside a try-catch: use invoke so exception unwinds to the landing pad
        auto* after_throw = llvm::BasicBlock::Create(llvm_ctx, "after_throw", current_func);
        auto* invoke_inst = _builder->CreateInvoke(
            cxa_throw, after_throw, _landing_pad_stack.top().lpad_bb,
            {exc_mem, typeinfo_gv, llvm::ConstantPointerNull::get(ptr_ty)});
        invoke_inst->setDoesNotReturn();
        // after_throw is unreachable but needed for LLVM well-formedness
        _builder->SetInsertPoint(after_throw);
        _builder->CreateUnreachable();
        // Create a continuation block for any code after the throw statement
        auto* cont_bb = llvm::BasicBlock::Create(llvm_ctx, "post_throw", current_func);
        _builder->SetInsertPoint(cont_bb);
    } else {
        // Not inside try-catch: plain call (exception unwinds past this frame)
        auto* call = _builder->CreateCall(cxa_throw,
            {exc_mem, typeinfo_gv, llvm::ConstantPointerNull::get(ptr_ty)});
        call->setDoesNotReturn();
        _builder->CreateUnreachable();
        // Create a new basic block for any code following the throw (unreachable)
        auto* after_throw = llvm::BasicBlock::Create(llvm_ctx, "after_throw", current_func);
        _builder->SetInsertPoint(after_throw);
    }
}

//
// Try-catch statement
//

void symbol_resolver::visit_try_catch_statement(try_catch_statement& stmt)
{
    if(auto body = stmt.get_try_body()) {
        body->accept(*this);
    }
    for(auto& clause : stmt.get_catch_clauses()) {
        if(auto var = clause->get_exception_var()) {
            var->accept(*this);
        }
        if(auto body = clause->get_body()) {
            body->accept(*this);
        }
    }
    if(auto body = stmt.get_finally_body()) {
        body->accept(*this);
    }
}

void type_reference_resolver::visit_try_catch_statement(try_catch_statement& stmt)
{
    // Collect the caught types from catch clauses BEFORE visiting the try body,
    // so that throw/call checks inside the body can see them.
    try_catch_scope scope;
    for(auto& clause : stmt.get_catch_clauses()) {
        if(auto var = clause->get_exception_var()) {
            // Only resolve the type — skip full variable init validation
            // (catch variables are bound by the runtime, not user init expressions)
            lex::opt_any_lexeme var_lexeme;
            if (auto ast_vd = var->get_ast_variable_decl()) {
                var_lexeme = lex::any_lexeme{ast_vd->name};
            }
            resolve_variable_type(*var, var_lexeme);
            if (auto vt = var->get_type()) {
                scope.caught_types.push_back(vt);
            }
        }
    }
    _try_catch_stack.push_back(std::move(scope));

    // Visit the try body (throw/call checks will consult the stack)
    if(auto body = stmt.get_try_body()) {
        body->accept(*this);
    }

    _try_catch_stack.pop_back();

    // Visit catch clause bodies (which are outside the try scope)
    for(auto& clause : stmt.get_catch_clauses()) {
        if(auto body = clause->get_body()) {
            // Push catch scope so that bare 'throw;' inside the body can be validated
            catch_scope cs;
            if (auto var = clause->get_exception_var()) {
                cs.caught_type = var->get_type();
            }
            _catch_clause_stack.push_back(std::move(cs));
            body->accept(*this);
            _catch_clause_stack.pop_back();
        }
    }

    // Visit finally body
    if(auto body = stmt.get_finally_body()) {
        body->accept(*this);
    }
}

void declaration_generator::visit_try_catch_statement(try_catch_statement& stmt) {
    // Nothing to do here
}

void implementation_generator::visit_try_catch_statement(try_catch_statement& stmt) {
    auto& llvm_ctx = _context->llvm_context();
    auto& mod = _context->module();
    auto* func = _builder->GetInsertBlock()->getParent();
    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
    auto* i32_ty = llvm::Type::getInt32Ty(llvm_ctx);

    // Set the personality function for C++ exception handling
    if (!func->hasPersonalityFn()) {
        auto personality = mod.getOrInsertFunction("__gxx_personality_v0",
            llvm::FunctionType::get(i32_ty, true));
        func->setPersonalityFn(llvm::cast<llvm::Constant>(personality.getCallee()));
    }

    // Create basic blocks
    auto* try_bb = llvm::BasicBlock::Create(llvm_ctx, "try", func);
    auto* lpad_bb = llvm::BasicBlock::Create(llvm_ctx, "lpad", func);
    auto* dispatch_bb = llvm::BasicBlock::Create(llvm_ctx, "exc_dispatch", func);
    auto* end_bb = llvm::BasicBlock::Create(llvm_ctx, "try_end", func);

    // Create catch handler blocks
    const auto& clauses = stmt.get_catch_clauses();
    std::vector<llvm::BasicBlock*> catch_bbs;
    for (size_t i = 0; i < clauses.size(); ++i) {
        catch_bbs.push_back(llvm::BasicBlock::Create(llvm_ctx,
            "catch_" + std::to_string(i), func));
    }
    auto* catch_fallthrough_bb = llvm::BasicBlock::Create(llvm_ctx, "catch_unmatched", func);

    // Create an alloca to store the exception pointer (accessible from dispatch_bb
    // whether we arrive from lpad_bb or from an inner catch fallthrough).
    auto* entry_bb = &func->getEntryBlock();
    llvm::IRBuilder<> alloca_builder(entry_bb, entry_bb->begin());
    auto* exc_ptr_alloca = alloca_builder.CreateAlloca(ptr_ty, nullptr, "exc_ptr_slot");

    // Create an alloca to hold the matched base offset (written by dispatch, read by catch handler).
    auto* offset_alloca = alloca_builder.CreateAlloca(i32_ty, nullptr, "catch_offset_slot");

    // Branch to try body
    _builder->CreateBr(try_bb);
    _builder->SetInsertPoint(try_bb);

    // Save the outer context (if any) for nested unwinding
    eh_landing_context* outer_ctx = _landing_pad_stack.empty() ? nullptr : &_landing_pad_stack.top();

    // Push landing pad context so function calls within the try body use invoke
    _landing_pad_stack.push({lpad_bb, dispatch_bb, exc_ptr_alloca});

    // Helper lambda: emit the finally body at the current insert point (inlined copy).
    auto finally_body = stmt.get_finally_body();
    auto emit_finally = [&]() {
        if (finally_body) {
            finally_body->accept(*this);
        }
    };

    // Push finally context so return/break/continue inside the try body emit the finally block
    if (finally_body) {
        _finally_stack.push({finally_body, false});
    }

    // Generate the try body
    if (auto body = stmt.get_try_body()) {
        body->accept(*this);
    }

    // Pop finally context for the try body
    if (finally_body) {
        _finally_stack.pop();
    }

    // Pop landing pad
    _landing_pad_stack.pop();

    // If we reach end of try body normally, emit finally then branch to end
    if (!_builder->GetInsertBlock()->getTerminator()) {
        emit_finally();
        if (!_builder->GetInsertBlock()->getTerminator()) {
            _builder->CreateBr(end_bb);
        }
    }

    // Landing pad block — entered when an exception is thrown
    _builder->SetInsertPoint(lpad_bb);

    // Build the list of typeinfo globals for each catch clause
    std::vector<llvm::Constant*> typeinfos;
    for (auto& clause : clauses) {
        auto var = clause->get_exception_var();
        if (!var) {
            typeinfos.push_back(nullptr);
            continue;
        }
        auto var_type = var->get_type();
        // Peel addresser wrappers to get the underlying struct type
        auto inner_type = var_type;
        while (inner_type && inner_type->get_subtype() &&
               (type::is_pointer(inner_type) || type::is_reference(inner_type) ||
                type::is_const(inner_type))) {
            inner_type = inner_type->get_subtype();
        }

        std::string typeinfo_name;
        if (auto st = std::dynamic_pointer_cast<struct_type>(inner_type)) {
            if (auto agg = st->get_struct()) {
                typeinfo_name = mangler::mangle_rtti(agg->get_name());
            } else {
                typeinfo_name = "_KTI_" + st->name();
            }
        } else {
            typeinfo_name = "_KTI_" + (inner_type ? inner_type->to_string() : "unknown");
        }

        auto* ti_gv = mod.getNamedGlobal(typeinfo_name);
        if (!ti_gv) {
            ti_gv = new llvm::GlobalVariable(
                mod, ptr_ty, /*isConstant=*/true,
                llvm::GlobalValue::LinkOnceODRLinkage,
                llvm::ConstantPointerNull::get(ptr_ty),
                typeinfo_name);
        }
        typeinfos.push_back(ti_gv);
    }

    // Create the landingpad instruction with catch-all clause.
    auto* lpad_type = llvm::StructType::get(llvm_ctx, {ptr_ty, i32_ty});
    auto* lpad = _builder->CreateLandingPad(lpad_type, 1, "lpad_val");
    lpad->addClause(llvm::ConstantPointerNull::get(ptr_ty));

    // Extract exception pointer and store to the shared alloca
    auto* exc_ptr_from_lpad = _builder->CreateExtractValue(lpad, 0, "exc_ptr");
    _builder->CreateStore(exc_ptr_from_lpad, exc_ptr_alloca);
    _builder->CreateBr(dispatch_bb);

    // Dispatch block — compare thrown typeinfo against catch clause typeinfos.
    // This block can be reached from lpad_bb (normal unwinding) or from an inner
    // catch fallthrough (nested try-catch propagation).
    _builder->SetInsertPoint(dispatch_bb);

    // Read the typeinfo chain of the thrown exception from the global variable.
    // The chain is a null-terminated array of typeinfo pointers [self, base1, ..., null].
    auto* ti_chain_global = mod.getNamedGlobal("_k_thrown_typeinfo_chain");
    if (!ti_chain_global) {
        ti_chain_global = new llvm::GlobalVariable(
            mod, ptr_ty, /*isConstant=*/false,
            llvm::GlobalValue::ExternalLinkage,
            llvm::ConstantPointerNull::get(ptr_ty),
            "_k_thrown_typeinfo_chain");
    }
    auto* thrown_chain_ptr = _builder->CreateLoad(ptr_ty, ti_chain_global, "thrown_chain");

    // Initialize the offset alloca to 0
    _builder->CreateStore(llvm::ConstantInt::get(i32_ty, 0), offset_alloca);

    // The chain entry struct type: { ptr typeinfo, i32 offset }
    auto* chain_entry_struct_ty = llvm::StructType::get(llvm_ctx, {ptr_ty, i32_ty});

    // Dispatch: for each catch clause, iterate the thrown type's chain and check
    // if the catch clause's typeinfo matches any entry (supports base class catching).
    if (clauses.empty()) {
        // No catch clauses (try-finally only): go directly to fallthrough
        _builder->CreateBr(catch_fallthrough_bb);
    }
    for (size_t i = 0; i < clauses.size(); ++i) {
        if (!typeinfos[i]) {
            // Catch-all (no typeinfo): always matches, offset 0
            _builder->CreateStore(llvm::ConstantInt::get(i32_ty, 0), offset_alloca);
            _builder->CreateBr(catch_bbs[i]);
            break;
        }

        // Generate a loop that walks the typeinfo chain:
        //   for (entry* p = chain; p->ti != null; p++) { if (p->ti == catch_ti) { offset = p->offset; goto catch; } }
        auto* prev_bb = _builder->GetInsertBlock();
        auto* loop_bb = llvm::BasicBlock::Create(llvm_ctx,
            "catch_check_loop_" + std::to_string(i), func);
        auto* match_bb = catch_bbs[i];
        auto* no_match_bb = (i == clauses.size() - 1)
            ? catch_fallthrough_bb
            : llvm::BasicBlock::Create(llvm_ctx, "catch_test_" + std::to_string(i+1), func);

        _builder->CreateBr(loop_bb);
        _builder->SetInsertPoint(loop_bb);

        // PHI node for the current entry pointer in the chain
        auto* phi_ptr = _builder->CreatePHI(ptr_ty, 2, "chain_ptr_" + std::to_string(i));
        phi_ptr->addIncoming(thrown_chain_ptr, prev_bb);

        // Load the typeinfo field (index 0) of the current entry
        auto* ti_field_ptr = _builder->CreateStructGEP(chain_entry_struct_ty, phi_ptr, 0, "entry_ti_ptr");
        auto* entry_ti = _builder->CreateLoad(ptr_ty, ti_field_ptr, "chain_entry_ti");

        // Check if null (end of chain → no match)
        auto* is_null = _builder->CreateICmpEQ(entry_ti,
            llvm::ConstantPointerNull::get(ptr_ty), "chain_end");
        auto* check_match_bb = llvm::BasicBlock::Create(llvm_ctx,
            "catch_cmp_" + std::to_string(i), func);
        _builder->CreateCondBr(is_null, no_match_bb, check_match_bb);

        // Compare with catch typeinfo
        _builder->SetInsertPoint(check_match_bb);
        auto* match = _builder->CreateICmpEQ(entry_ti, typeinfos[i],
            "catch_match_" + std::to_string(i));
        auto* next_iter_bb = llvm::BasicBlock::Create(llvm_ctx,
            "catch_next_" + std::to_string(i), func);

        // On match: store the offset and branch to catch handler
        auto* store_offset_bb = llvm::BasicBlock::Create(llvm_ctx,
            "catch_store_offset_" + std::to_string(i), func);
        _builder->CreateCondBr(match, store_offset_bb, next_iter_bb);

        _builder->SetInsertPoint(store_offset_bb);
        auto* offset_field_ptr = _builder->CreateStructGEP(chain_entry_struct_ty, phi_ptr, 1, "entry_offset_ptr");
        auto* offset_val = _builder->CreateLoad(i32_ty, offset_field_ptr, "entry_offset");
        _builder->CreateStore(offset_val, offset_alloca);
        _builder->CreateBr(match_bb);

        // Advance pointer to next entry and loop
        _builder->SetInsertPoint(next_iter_bb);
        auto* next_ptr = _builder->CreateGEP(chain_entry_struct_ty, phi_ptr,
            {llvm::ConstantInt::get(i32_ty, 1)}, "chain_next");
        phi_ptr->addIncoming(next_ptr, next_iter_bb);
        _builder->CreateBr(loop_bb);

        // Continue from no_match_bb for the next clause
        if (i < clauses.size() - 1) {
            _builder->SetInsertPoint(no_match_bb);
        }
    }

    // Generate catch handler bodies
    // Declare __cxa_begin_catch and __cxa_end_catch
    auto cxa_begin = mod.getOrInsertFunction("__cxa_begin_catch",
        llvm::FunctionType::get(ptr_ty, {ptr_ty}, false));
    auto cxa_end = mod.getOrInsertFunction("__cxa_end_catch",
        llvm::FunctionType::get(llvm::Type::getVoidTy(llvm_ctx), {}, false));

    for (size_t i = 0; i < clauses.size(); ++i) {
        _builder->SetInsertPoint(catch_bbs[i]);

        // Load the exception pointer from the shared alloca
        auto* exc_ptr = _builder->CreateLoad(ptr_ty, exc_ptr_alloca, "exc_ptr");

        // Begin catch — returns pointer to the exception object (start of thrown type)
        auto* caught_ptr = _builder->CreateCall(cxa_begin, {exc_ptr}, "caught");

        // Adjust the pointer by the matched base offset to point to the correct
        // base sub-object for the catch variable's declared type.
        auto* matched_offset = _builder->CreateLoad(i32_ty, offset_alloca, "matched_offset");
        auto* adjusted_ptr = _builder->CreateGEP(
            llvm::Type::getInt8Ty(llvm_ctx), caught_ptr, {matched_offset}, "adjusted_exc_ptr");

        // Bind the exception variable — create its alloca and store the adjusted pointer
        auto var = clauses[i]->get_exception_var();
        if (var) {
            // Create alloca for the catch variable (reference type — stored as pointer at ABI level)
            auto var_type = var->get_type();
            llvm::Type* llvm_var_type = _context->get_llvm_type(var_type);
            llvm::IRBuilder<> var_alloca_builder(entry_bb, entry_bb->begin());
            auto* alloca = var_alloca_builder.CreateAlloca(llvm_var_type, nullptr,
                var->get_short_name() + "_exc");
            // Register the variable in the context
            _context->_variables[var] = alloca;
            // Store the adjusted pointer (pointing to the correct base sub-object)
            _builder->CreateStore(adjusted_ptr, alloca);
        }

        // Generate catch body
        // Push finally context (in_catch=true) so return/break/continue inside the catch
        // body will emit __cxa_end_catch + finally before exiting.
        if (finally_body) {
            _finally_stack.push({finally_body, true});
        }

        if (auto body = clauses[i]->get_body()) {
            body->accept(*this);
        }

        if (finally_body) {
            _finally_stack.pop();
        }

        // End catch (only on normal path — early exit paths handle this via _finally_stack)
        if (!_builder->GetInsertBlock()->getTerminator()) {
            _builder->CreateCall(cxa_end);
        }

        // Emit finally body after catch handler, then branch to end
        if (!_builder->GetInsertBlock()->getTerminator()) {
            emit_finally();
            if (!_builder->GetInsertBlock()->getTerminator()) {
                _builder->CreateBr(end_bb);
            }
        }
    }

    // Catch fallthrough — no match, emit finally then propagate to outer handler or resume unwinding
    _builder->SetInsertPoint(catch_fallthrough_bb);

    // Emit finally body before propagating unmatched exception
    emit_finally();

    if (outer_ctx) {
        // Nested try-catch in the same function: store the exception pointer into the
        // outer handler's alloca and branch directly to its dispatch block.
        // This avoids re-throwing and stays entirely within the function's CFG.
        auto* exc_ptr = _builder->CreateLoad(ptr_ty, exc_ptr_alloca, "exc_ptr_prop");
        _builder->CreateStore(exc_ptr, outer_ctx->exc_ptr_alloca);
        _builder->CreateBr(outer_ctx->dispatch_bb);
    } else {
        // Top-level try-catch: use resume to propagate out of the current function.
        auto* exc_ptr = _builder->CreateLoad(ptr_ty, exc_ptr_alloca, "exc_ptr_resume");
        auto* lpad_type_res = llvm::StructType::get(llvm_ctx, {ptr_ty, i32_ty});
        auto* resume_val = llvm::UndefValue::get(lpad_type_res);
        auto* resume_val2 = _builder->CreateInsertValue(resume_val, exc_ptr, 0);
        auto* resume_val3 = _builder->CreateInsertValue(resume_val2,
            llvm::ConstantInt::get(i32_ty, 0), 1);
        _builder->CreateResume(resume_val3);
    }

    // Continue after try-catch
    _builder->SetInsertPoint(end_bb);
}

//
// Catch clause
//

void symbol_resolver::visit_catch_clause(catch_clause& clause)
{
    if(auto var = clause.get_exception_var()) {
        var->accept(*this);
    }
    if(auto body = clause.get_body()) {
        body->accept(*this);
    }
}

void type_reference_resolver::visit_catch_clause(catch_clause& clause)
{
    if(auto var = clause.get_exception_var()) {
        // Only resolve the type of the catch variable — do NOT run full variable init
        // validation (validate_reference_variable would fail because catch variables
        // have no user-provided init expression; they are bound by the runtime).
        lex::opt_any_lexeme var_lexeme;
        if (auto ast_vd = var->get_ast_variable_decl()) {
            var_lexeme = lex::any_lexeme{ast_vd->name};
        }
        resolve_variable_type(*var, var_lexeme);

        // Validate that the catch parameter uses a reference addresser (&)
        auto var_type = var->get_type();
        if (var_type) {
            if (!type::is_reference(var_type)) {
                throw_error(static_cast<unsigned int>(k::diag::exception_diag::ERR_CATCH_MUST_BE_REFERENCE),
                            var_lexeme,
                            "Catch parameter '{}' must use a reference addresser (&), not a pointer or other addresser",
                            {var->get_short_name()});
            }

            // Validate that the caught type derives from ::k::Throwable
            auto caught_agg = get_exception_aggregate(var_type);
            if (caught_agg) {
                std::shared_ptr<aggregate> throwable_class;
                auto root_ns = _unit.get_root_namespace();
                if (root_ns) {
                    auto k_ns = root_ns->get_child_namespace("k");
                    if (k_ns) {
                        throwable_class = k_ns->get_aggregate("Throwable");
                    }
                }
                if (!throwable_class) {
                    k::name thr_name(false, {"k", "Throwable"});
                    throwable_class = _unit.get_or_create_imported_aggregate(thr_name, _context);
                }
                if (throwable_class) {
                    if (caught_agg != throwable_class && !caught_agg->is_derived_from(throwable_class)) {
                        throw_error(static_cast<unsigned int>(k::diag::exception_diag::ERR_CATCH_NOT_EXCEPTION_TYPE),
                                    var_lexeme,
                                    "Catch parameter type '{}' does not derive from ::k::Throwable",
                                    {caught_agg->get_short_name()});
                    }
                }
            }
        }
    }
    if(auto body = clause.get_body()) {
        // Push catch scope so that bare 'throw;' inside the body can be validated
        catch_scope cs;
        if (auto var = clause.get_exception_var()) {
            cs.caught_type = var->get_type();
        }
        _catch_clause_stack.push_back(std::move(cs));
        body->accept(*this);
        _catch_clause_stack.pop_back();
    }
}

void declaration_generator::visit_catch_clause(catch_clause& clause) {
    // Nothing to do here
}

void implementation_generator::visit_catch_clause(catch_clause& clause) {
    // TODO: implement catch clause codegen
}

//
// Exception contract checking helpers
//

std::shared_ptr<aggregate> type_reference_resolver::get_exception_aggregate(const std::shared_ptr<type>& t)
{
    if (!t) return nullptr;
    // Peel addresser wrappers (pointer, reference, const, link, view, owner, drain)
    auto inner = t;
    while (inner && inner->get_subtype() &&
           (type::is_pointer(inner) || type::is_reference(inner) ||
            type::is_const(inner) || type::is_link(inner) ||
            type::is_view(inner) || type::is_owner(inner))) {
        inner = inner->get_subtype();
    }
    if (auto st = std::dynamic_pointer_cast<struct_type>(inner)) {
        return st->get_struct();
    }
    return nullptr;
}

bool type_reference_resolver::is_exception_type_covered(
    const std::shared_ptr<aggregate>& thrown_agg,
    const std::vector<std::shared_ptr<type>>& declared_types)
{
    if (!thrown_agg) return false;
    for (const auto& decl_type : declared_types) {
        auto decl_agg = get_exception_aggregate(decl_type);
        if (!decl_agg) continue;
        // Exact match
        if (thrown_agg == decl_agg) return true;
        // Thrown type derives from declared type (caught by base class handler)
        if (thrown_agg->is_derived_from(decl_agg)) return true;
    }
    return false;
}

bool type_reference_resolver::is_exception_caught_by_try_catch(
    const std::shared_ptr<aggregate>& thrown_agg) const
{
    // Check all enclosing try-catch scopes (innermost first)
    for (auto it = _try_catch_stack.rbegin(); it != _try_catch_stack.rend(); ++it) {
        if (is_exception_type_covered(thrown_agg, it->caught_types)) {
            return true;
        }
    }
    return false;
}

void type_reference_resolver::check_throw_contract(
    const std::shared_ptr<type>& thrown_type,
    const lex::opt_any_lexeme& lexeme)
{
    if (_function_stack.empty()) return;
    auto& current_func = _function_stack.back();
    if (!current_func) return;

    // Only enforce the contract if the current function has a throws clause
    if (!current_func->has_throws_spec()) return;

    auto thrown_agg = get_exception_aggregate(thrown_type);
    if (!thrown_agg) return; // Non-struct throw — cannot check further

    // FatalError-derived types are unchecked — they don't need throws declarations
    {
        std::shared_ptr<aggregate> fatal_class;
        auto root_ns = _unit.get_root_namespace();
        if (root_ns) {
            auto k_ns = root_ns->get_child_namespace("k");
            if (k_ns) fatal_class = k_ns->get_aggregate("FatalError");
        }
        if (!fatal_class) {
            k::name fe_name(false, {"k", "FatalError"});
            fatal_class = _unit.get_or_create_imported_aggregate(fe_name, _context);
        }
        if (fatal_class && (thrown_agg == fatal_class || thrown_agg->is_derived_from(fatal_class))) {
            return; // Unchecked — no contract enforcement
        }
    }

    // Check if caught by an enclosing try-catch
    if (is_exception_caught_by_try_catch(thrown_agg)) return;

    // Check if declared in the function's throws spec
    if (is_exception_type_covered(thrown_agg, current_func->get_throws_spec())) return;

    // Not declared and not caught — error
    throw_error(static_cast<unsigned int>(k::diag::exception_diag::ERR_THROW_UNDECLARED_EXCEPTION),
                lexeme,
                "Function '{}' throws type '{}' which is not declared in its throws clause",
                {current_func->get_short_name(), thrown_agg->get_short_name()});
}

void type_reference_resolver::check_call_contract(
    const function& called_func,
    const lex::opt_any_lexeme& lexeme)
{
    if (_function_stack.empty()) return;
    auto& current_func = _function_stack.back();
    if (!current_func) return;

    // Only check if the called function has a throws spec
    if (!called_func.has_throws_spec()) return;

    // For each exception type declared by the called function, check if it's handled
    for (const auto& exc_type : called_func.get_throws_spec()) {
        auto exc_agg = get_exception_aggregate(exc_type);
        if (!exc_agg) continue;

        // Caught by enclosing try-catch?
        if (is_exception_caught_by_try_catch(exc_agg)) continue;

        // Propagated via the current function's throws clause?
        if (current_func->has_throws_spec() &&
            is_exception_type_covered(exc_agg, current_func->get_throws_spec())) continue;

        // Not handled — error
        throw_error(static_cast<unsigned int>(k::diag::exception_diag::ERR_UNCAUGHT_EXCEPTION),
                    lexeme,
                    "Call to '{}' may throw '{}' which is neither caught nor declared in the throws clause of '{}'",
                    {called_func.get_short_name(), exc_agg->get_short_name(), current_func->get_short_name()});
    }
}

//
// If-then-else
//

void symbol_resolver::visit_if_else_statement(if_else_statement& stmt)
{
    if(stmt.has_cond_var()) {
        for(auto& var : stmt.get_cond_vars()) {
            var->accept(*this);
        }
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
        // Resolve all condition variables (type + init)
        for(auto& var : stmt.get_cond_vars()) {
            var->accept(*this);
        }

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
        } else if(stmt.is_multi_var_softfail()) {
            // Multi-var soft-fail form: each addressor variable is null-checked.
            // Non-addressor variables are allowed (they just don't get null-checked).
            // No explicit bool-castability check needed — null-check is implicit.
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
                    else if(std::dynamic_pointer_cast<enum_type>(var_type)) can_cast = true;
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
        for(auto& var : stmt.get_cond_vars()) {
            var->accept(*this);
        }
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
        if (st_type->get_struct()) {
            auto dtor = st_type->get_struct()->get_destructor();
            if (!dtor) return;
            auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
            if (dtor_it == _context->_functions.end()) return;
            _builder->CreateCall(dtor_it->second, {alloca});
        } else {
            // Union with non-trivial alternatives
            auto udef = find_union_for_struct_type(_unit, st_type);
            if (udef) emit_union_cleanup(alloca, *udef);
        }
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
    bool is_multi_softfail = stmt.is_multi_var_softfail();
    // Single-var soft-fail: single addressor var without test expression
    bool is_single_softfail = has_cond_var && !has_cond_var_with_test && !is_multi_softfail;

    // Step 1: Create then/else/merge basic blocks BEFORE evaluating the condition,
    // so that _null_failure_bb can reference them during condition codegen.
    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* then_block = llvm::BasicBlock::Create(**_context, "if-then", func);
    llvm::BasicBlock* else_block = has_else ? llvm::BasicBlock::Create(**_context, "if-else") : nullptr;
    llvm::BasicBlock* cont_block = llvm::BasicBlock::Create(**_context, "if-continue");

    // The destination for soft-fail branches (else or continue)
    llvm::BasicBlock* fail_dest = has_else ? else_block : cont_block;

    auto* saved_null_failure_bb = _null_failure_bb;
    auto* saved_union_failure_bb = _union_failure_bb;

    if(is_multi_softfail) {
        // =====================================================================
        // Multi-var soft-fail form: if(var1; var2; ...)
        // Each addressor var gets a null-check after init. On null, cleanup
        // previously-initialized vars in reverse, then jump to else/cont.
        // For ref/link: _null_failure_bb handles the soft-fail during init.
        // For ptr/view/owner: explicit null-check after init.
        // Non-addressor vars: no null-check, always succeed.
        // =====================================================================
        auto& vars = stmt.get_cond_vars();

        for(size_t i = 0; i < vars.size(); ++i) {
            auto& var = vars[i];

            // Create a cleanup trampoline for failure at this variable.
            // It cleans up vars 0..i-1 (already initialized) in reverse, then jumps to fail_dest.
            // For ref/link, _null_failure_bb is set to this trampoline so the
            // existing soft-fail mechanism in visit_variable_statement uses it.
            llvm::BasicBlock* trampoline = nullptr;
            if(i > 0) {
                trampoline = llvm::BasicBlock::Create(**_context, "if-softfail-cleanup-" + std::to_string(i));
                // Build the trampoline: cleanup vars [i-1 .. 0] then branch to fail_dest
                // We'll fill it in after emitting the var, but create it now for _null_failure_bb
            }

            // Determine if this variable type is an addressor
            auto var_type = var->get_type();
            bool is_addressor = type::is_any_indirection(var_type);
            bool is_ref_or_link = std::dynamic_pointer_cast<reference_type>(var_type) != nullptr
                               || std::dynamic_pointer_cast<link_type>(var_type) != nullptr;

            // Set _null_failure_bb for ref/link soft-fail during init
            if(is_ref_or_link) {
                _null_failure_bb = trampoline ? trampoline : fail_dest;
            } else {
                // For non-ref/link, don't set soft-fail — null assignment would be fatal
                // (which is correct for ptr/view/owner: we'll check explicitly after init)
                _null_failure_bb = saved_null_failure_bb;
            }

            // Union alternative access mismatch in condition variable initializers
            // should soft-fail like null-addressor checks in if-let mode.
            _union_failure_bb = trampoline ? trampoline : fail_dest;

            // Emit the variable declaration
            var->accept(*this);

            // For ref/link: if we reach here, init succeeded (soft-fail would have jumped away)
            // For ptr/view/owner: emit explicit null-check
            if(is_addressor && !is_ref_or_link) {
                auto var_it = _context->_variables.find(var);
                if(var_it != _context->_variables.end()) {
                    llvm::AllocaInst* alloca = var_it->second;
                    auto& llvm_ctx = _builder->getContext();
                    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
                    llvm::Value* loaded = _builder->CreateLoad(ptr_ty, alloca, "softfail_null_check");
                    auto* is_null = _builder->CreateICmpEQ(loaded,
                        llvm::ConstantPointerNull::get(ptr_ty), "softfail_is_null");

                    llvm::BasicBlock* ok_bb = llvm::BasicBlock::Create(**_context, "softfail-ok-" + std::to_string(i), func);

                    // On null, jump to trampoline (or fail_dest if first var)
                    llvm::BasicBlock* null_target = trampoline ? trampoline : fail_dest;

                    // But we also need to cleanup THIS var (var i) if it's an owner with non-null before the check
                    // Actually: the var IS null if we branch here, so no cleanup of THIS var needed.
                    // But we need to cleanup vars 0..i-1. The trampoline handles 0..i-1.
                    // For var i which is null, no cleanup needed (it's null).
                    // However we need a trampoline that also cleans up var i if it was already
                    // stored but is not null... Actually no: if is_null is true, the value IS null,
                    // so no cleanup needed for this var. Just cleanup 0..i-1.
                    // But for owner types, the alloca was already stored with null, which is fine —
                    // emit_cond_var_cleanup checks for null before freeing owners.
                    // Actually, let's just route to a trampoline that cleans i..0 including this var
                    // since cleanup of a null owner/ptr is a no-op anyway.

                    // Create a trampoline for failure AT this var (cleanup vars 0..i in reverse)
                    llvm::BasicBlock* this_fail_bb = llvm::BasicBlock::Create(**_context, "if-softfail-at-" + std::to_string(i));
                    _builder->CreateCondBr(is_null, this_fail_bb, ok_bb);

                    // Fill the this_fail_bb: cleanup vars [i..0] then jump to fail_dest
                    func->insert(func->end(), this_fail_bb);
                    _builder->SetInsertPoint(this_fail_bb);
                    for(int j = (int)i; j >= 0; --j) {
                        emit_cond_var_cleanup(vars[j]);
                    }
                    _builder->CreateBr(fail_dest);

                    _builder->SetInsertPoint(ok_bb);
                }
            }

            // Fill the trampoline (if any): cleanup vars 0..i-1 then jump to fail destination.
            // This is used by ref/link null-softfail and by union discriminant softfail.
            if(trampoline) {
                auto* saved_bb = _builder->GetInsertBlock();
                func->insert(func->end(), trampoline);
                _builder->SetInsertPoint(trampoline);
                for(int j = (int)i - 1; j >= 0; --j) {
                    emit_cond_var_cleanup(vars[j]);
                }
                _builder->CreateBr(fail_dest);
                _builder->SetInsertPoint(saved_bb);
            }
        }

        // All vars initialized successfully → go to then
        _null_failure_bb = saved_null_failure_bb;
        _union_failure_bb = saved_union_failure_bb;
        _builder->CreateBr(then_block);

    } else if(has_cond_var_with_test) {
        // =====================================================================
        // if(vars; test) form — hard-fail, test expression determines branch
        // =====================================================================
        // No soft-fail for any var
        for(auto& var : stmt.get_cond_vars()) {
            var->accept(*this);
        }
        _null_failure_bb = saved_null_failure_bb;
        _union_failure_bb = saved_union_failure_bb;

        _value = nullptr;
        stmt.get_test_expr()->accept(*this);
        auto test_value = _value;
        _value = nullptr;

        emit_expression_temporaries_cleanup();

        if(has_else) {
            _builder->CreateCondBr(test_value, then_block, else_block);
        } else {
            _builder->CreateCondBr(test_value, then_block, cont_block);
        }

    } else if(is_single_softfail) {
        // =====================================================================
        // Classic if-let: single var, no test expression
        // =====================================================================
        _null_failure_bb = fail_dest;
        _union_failure_bb = fail_dest;

        stmt.get_cond_var()->accept(*this);

        auto var_type = stmt.get_cond_var()->get_type();
        bool is_ref_or_link = std::dynamic_pointer_cast<reference_type>(var_type) != nullptr
                           || std::dynamic_pointer_cast<link_type>(var_type) != nullptr;

        if(is_ref_or_link) {
            // Soft-fail mechanism handled null → else. If we're here, success → then.
            _null_failure_bb = saved_null_failure_bb;
            _union_failure_bb = saved_union_failure_bb;
            _builder->CreateBr(then_block);
        } else if(std::dynamic_pointer_cast<pointer_type>(var_type)
               || std::dynamic_pointer_cast<owner_type>(var_type)
               || std::dynamic_pointer_cast<view_type>(var_type)) {
            // Pointer/owner/view: explicit null-check → soft-fail
            _null_failure_bb = saved_null_failure_bb;
            _union_failure_bb = saved_union_failure_bb;
            auto var_it = _context->_variables.find(stmt.get_cond_var());
            llvm::AllocaInst* alloca = var_it->second;
            auto& llvm_ctx = _builder->getContext();
            auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
            llvm::Value* loaded = _builder->CreateLoad(ptr_ty, alloca, "if_cond_ptr_load");
            auto* test_value = _builder->CreateICmpNE(loaded,
                llvm::ConstantPointerNull::get(ptr_ty), "if_cond_ne_null");

            emit_expression_temporaries_cleanup();

            if(has_else) {
                _builder->CreateCondBr(test_value, then_block, else_block);
            } else {
                _builder->CreateCondBr(test_value, then_block, cont_block);
            }
        } else {
            _null_failure_bb = saved_null_failure_bb;
            _union_failure_bb = saved_union_failure_bb;

            // Generate bool cast from the variable value (primitive, aggregate, etc.)
            auto var_it = _context->_variables.find(stmt.get_cond_var());
            llvm::AllocaInst* alloca = var_it->second;

            llvm::Value* test_value = nullptr;
            if(auto prim_type = std::dynamic_pointer_cast<primitive_type>(var_type)) {
                llvm::Type* llvm_t = _context->get_llvm_type(var_type);
                llvm::Value* loaded = _builder->CreateLoad(llvm_t, alloca, "if_cond_load");
                if(prim_type->is_float()) {
                    test_value = _builder->CreateFCmpONE(loaded,
                        llvm::ConstantFP::get(llvm_t, 0.0), "if_cond_ne_zero");
                } else {
                    test_value = _builder->CreateICmpNE(loaded,
                        llvm::ConstantInt::get(llvm_t, 0), "if_cond_ne_zero");
                }
            } else if(auto en_type = std::dynamic_pointer_cast<enum_type>(var_type)) {
                llvm::Type* llvm_t = _context->get_llvm_type(var_type);
                llvm::Value* loaded = _builder->CreateLoad(llvm_t, alloca, "if_cond_enum_load");
                test_value = _builder->CreateICmpNE(loaded,
                    llvm::ConstantInt::get(llvm_t, 0), "if_cond_enum_ne_zero");
            } else if(auto st_type = std::dynamic_pointer_cast<struct_type>(var_type)) {
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
                test_value = _builder->getTrue();
            }

            emit_expression_temporaries_cleanup();

            if(has_else) {
                _builder->CreateCondBr(test_value, then_block, else_block);
            } else {
                _builder->CreateCondBr(test_value, then_block, cont_block);
            }
        }
    } else {
        // =====================================================================
        // Classic form: if(expr) — no cond vars
        // =====================================================================
        _null_failure_bb = fail_dest;
        _union_failure_bb = saved_union_failure_bb;

        _value = nullptr;
        stmt.get_test_expr()->accept(*this);
        auto test_value = _value;
        _value = nullptr;

        _null_failure_bb = saved_null_failure_bb;
        _union_failure_bb = saved_union_failure_bb;

        emit_expression_temporaries_cleanup();

        if(has_else) {
            _builder->CreateCondBr(test_value, then_block, else_block);
        } else {
            _builder->CreateCondBr(test_value, then_block, cont_block);
        }
    }

    // Step 6: Visit then-block, emit cleanup for cond vars, emit branch to merge
    _builder->SetInsertPoint(then_block);
    stmt.get_then_stmt()->accept(*this);
    if(has_cond_var) {
        // Cleanup in reverse declaration order
        auto& vars = stmt.get_cond_vars();
        for(auto it = vars.rbegin(); it != vars.rend(); ++it) {
            emit_cond_var_cleanup(*it);
        }
    }
    _builder->CreateBr(cont_block);

    // Step 7: Visit else-block (if present), emit cleanup for cond vars, emit branch to merge
    if(has_else) {
        func->insert(func->end(), else_block);
        _builder->SetInsertPoint(else_block);
        stmt.get_else_stmt()->accept(*this);
        // For soft-fail forms (single or multi without test): no cleanup in else.
        // Variables don't exist on the else path (option 3a).
        // For if(vars; test) form: all variables always exist, cleanup in reverse order.
        if(has_cond_var) {
            if(has_cond_var_with_test) {
                auto& vars = stmt.get_cond_vars();
                for(auto it = vars.rbegin(); it != vars.rend(); ++it) {
                    emit_cond_var_cleanup(*it);
                }
            }
            // else: soft-fail form — no cleanup in else (cleanup done in trampolines
            // or variable was never fully initialized)
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
    _loop_finally_depth.push(_finally_stack.size());

    stmt.get_nested_stmt()->accept(*this);

    // Pop loop exit context
    _loop_finally_depth.pop();
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
    _loop_finally_depth.push(_finally_stack.size());

    stmt.get_nested_stmt()->accept(*this);

    // Pop loop context
    _loop_finally_depth.pop();
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

void implementation_generator::emit_union_cleanup(llvm::AllocaInst* alloca, union_type_def& udef) {
    // Layout: { i32 discriminant, [N x i8] storage }
    auto* union_llvm_type = udef.get_struct_type()->get_llvm_type();
    auto* disc_ptr = _builder->CreateStructGEP(union_llvm_type, alloca, 0, "union_dtor_disc_ptr");
    auto* disc_val = _builder->CreateLoad(llvm::Type::getInt32Ty(**_context), disc_ptr, "union_dtor_disc");

    auto* cur_fn = _builder->GetInsertBlock()->getParent();
    auto* merge_bb = llvm::BasicBlock::Create(**_context, "union_dtor_done", cur_fn);

    // Count how many alternatives in the FULL CHAIN actually need destruction
    std::vector<std::pair<size_t, std::shared_ptr<function>>> alt_dtors;
    for (const auto* alt_ptr : udef.all_alternatives_ptrs()) {
        if (auto st = std::dynamic_pointer_cast<struct_type>(alt_ptr->resolved_type)) {
            if (auto agg = st->get_struct()) {
                if (auto dtor = agg->get_destructor()) {
                    auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
                    if (dtor_it != _context->_functions.end()) {
                        alt_dtors.emplace_back(alt_ptr->index, dtor);
                    }
                }
            }
        }
    }

    if (alt_dtors.empty()) {
        // No alternatives actually have destructors — just branch to merge
        _builder->CreateBr(merge_bb);
        _builder->SetInsertPoint(merge_bb);
        return;
    }

    auto* switch_inst = _builder->CreateSwitch(disc_val, merge_bb, alt_dtors.size());

    for (auto& [alt_idx, dtor] : alt_dtors) {
        auto* case_bb = llvm::BasicBlock::Create(**_context,
            "union_dtor_alt" + std::to_string(alt_idx), cur_fn);
        switch_inst->addCase(
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(**_context), alt_idx),
            case_bb);

        _builder->SetInsertPoint(case_bb);
        // GEP to storage, bitcast to alternative struct pointer, call destructor
        auto* storage_ptr = _builder->CreateStructGEP(union_llvm_type, alloca, 1, "union_dtor_storage");
        auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
        _builder->CreateCall(dtor_it->second, {storage_ptr});
        _builder->CreateBr(merge_bb);
    }

    _builder->SetInsertPoint(merge_bb);
}


void implementation_generator::emit_union_cleanup_on_reassign(llvm::Value* union_base, union_type_def& udef, size_t new_alt_idx) {
    // Destroy the currently-active alternative (if it has a destructor and it
    // differs from the new one being assigned).
    auto* union_llvm_type = udef.get_struct_type()->get_llvm_type();
    auto* disc_ptr = _builder->CreateStructGEP(union_llvm_type, union_base, 0, "union_reassign_disc_ptr");
    auto* disc_val = _builder->CreateLoad(llvm::Type::getInt32Ty(**_context), disc_ptr, "union_reassign_disc");

    // Collect alternatives in the FULL CHAIN that have destructors and are NOT the new alternative
    std::vector<std::pair<size_t, std::shared_ptr<function>>> alt_dtors;
    for (const auto* alt_ptr : udef.all_alternatives_ptrs()) {
        if (alt_ptr->index == new_alt_idx) continue;
        if (auto st = std::dynamic_pointer_cast<struct_type>(alt_ptr->resolved_type)) {
            if (auto agg = st->get_struct()) {
                if (auto dtor = agg->get_destructor()) {
                    auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
                    if (dtor_it != _context->_functions.end()) {
                        alt_dtors.emplace_back(alt_ptr->index, dtor);
                    }
                }
            }
        }
    }

    if (alt_dtors.empty()) {
        // No alternatives with destructors to clean up — nothing to do
        return;
    }

    auto* cur_fn = _builder->GetInsertBlock()->getParent();
    auto* merge_bb = llvm::BasicBlock::Create(**_context, "union_reassign_done", cur_fn);

    // If the discriminant already matches the new target, skip destruction
    auto* already_new = _builder->CreateICmpEQ(disc_val,
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(**_context), new_alt_idx), "union_already_new");
    auto* need_cleanup_bb = llvm::BasicBlock::Create(**_context, "union_reassign_cleanup", cur_fn);
    _builder->CreateCondBr(already_new, merge_bb, need_cleanup_bb);

    _builder->SetInsertPoint(need_cleanup_bb);
    auto* switch_inst = _builder->CreateSwitch(disc_val, merge_bb, alt_dtors.size());

    for (auto& [alt_idx, dtor] : alt_dtors) {
        auto* case_bb = llvm::BasicBlock::Create(**_context,
            "union_reassign_dtor_alt" + std::to_string(alt_idx), cur_fn);
        switch_inst->addCase(
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(**_context), alt_idx),
            case_bb);

        _builder->SetInsertPoint(case_bb);
        auto* storage_ptr = _builder->CreateStructGEP(union_llvm_type, union_base, 1, "union_reassign_storage");
        auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
        _builder->CreateCall(dtor_it->second, {storage_ptr});
        _builder->CreateBr(merge_bb);
    }

    _builder->SetInsertPoint(merge_bb);
}

} // namespace k::model::gen
