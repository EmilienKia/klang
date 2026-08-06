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
#include "resolvers.hpp"
#include "generators.hpp"
#include "gen_helpers.hpp"
#include "../model/expressions.hpp"
#include "../model/statements.hpp"
#include "../model/operators.hpp"
#include "../model/mangler.hpp"
#include "../model/imported.hpp"
#include "../model/template.hpp"
#include "../model/template_instantiator.hpp"
#include "../parse/ast.hpp"
#include "../../../libkdi/src/kdi_aggregates.hpp"
#include "llvm/Support/raw_os_ostream.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Intrinsics.h>
#include <unordered_set>
#include "../errors.hpp"
namespace k::model::gen {
// constructor_invocation, temporary_construction

void type_reference_resolver::visit_constructor_invocation_expression(constructor_invocation_expression& expr) {
    // Just accept arguments,
    // the rest of resolution will be done (just after) in variable definition caller
    for(auto& arg : expr.arguments()) {
        arg->accept(*this);
    }

    // This expression is always returning the reference to the constructed object
    // so type is set to the reference of the constructed symbol type.
    auto var_def = expr.constructed_symbol()->get_variable_def();
    if(!var_def) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F009), expr.first_lexeme(),
            "Internal error: constructor invocation expression does not refer to a variable definition; "
            "the constructed symbol must be a variable — this indicates a compiler bug");
    }
    expr.set_type(var_def->get_type()->get_reference());

    // Check if constructor is explicitly needed
    auto var_type = var_def->get_type();
    if (!var_type) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F00A), expr.first_lexeme(),
            "Internal error: constructor invocation refers to a variable '{}' with no type; "
            "the type must be resolved before constructor invocation can be typed",
            {var_def->get_fq_name()});
    }
    if (type::is_primitive(var_type)) {
        if (!expr.empty()) {
            auto cast = adapt_type(expr.argument(0), var_type);
            if (cast && cast != expr.argument(0)) {
                expr.assign_argument(0, cast);
            }
        }
    } else if (auto et = std::dynamic_pointer_cast<enum_type>(var_type)) {
        // Enum type construction:
        //   MonEnum(entree)   — enum entry by name (resolved relative to the enum)
        //   MonEnum(3)        — construction from numeric literal
        //   MonEnum           — default construction (uses default entry)
        auto en = et->get_enumeration();
        if (!en) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F017), expr.first_lexeme(),
                "Internal error: enum_type has no associated enumeration model object");
        }
        if (expr.empty()) {
            // Default construction: use the default entry value
            auto def_entry = en->get_default_entry();
            auto val = value_expression::from_value(static_cast<long long>(def_entry.value));
            val->set_type(et);
            expr.arguments({val});
        } else if (expr.size() == 1) {
            auto arg = expr.argument(0);
            // Check if the argument is an unresolved symbol (enum entry name)
            auto sym = std::dynamic_pointer_cast<symbol_expression>(arg);
            if (sym && !sym->is_resolved()) {
                // Try to resolve as an enum entry name.
                // The symbol may be a qualified name like Policy::RUNTIME — use only the
                // last component as the entry name.
                auto entry_name = sym->get_name().back();
                auto entry = en->get_entry_by_name(entry_name);
                if (entry.has_value()) {
                    auto val = value_expression::from_value(static_cast<long long>(entry->value));
                    val->set_type(et);
                    expr.assign_argument(0, val);
                } else {
                    throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_OVERLOAD_ARG_TYPE_MISMATCH), expr.first_lexeme(),
                        "Enum '{}' has no entry named '{}'",
                        {en->get_short_name(), entry_name});
                }
            } else {
                // Argument is an already-resolved expression (e.g. numeric literal, variable, qualified enum entry)
                // Visit it to resolve types if needed
                arg->accept(*this);
                auto cast = adapt_type(expr.argument(0), et);
                if (cast && cast != expr.argument(0)) {
                    expr.assign_argument(0, cast);
                }
            }
        } else {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_OVERLOAD_RETURN_TYPE_MISMATCH), expr.first_lexeme(),
                "Enum constructor takes at most one argument, but {} were provided",
                {std::to_string(expr.size())});
        }
    } else if (type::is_owner(var_type)) {
        // Owner type member init: _buf(buf) — move the owner pointer.
        // Adapt the argument type if needed (e.g. ref<owner> → owner).
        if (!expr.empty()) {
            auto cast = adapt_type(expr.argument(0), var_type);
            if (cast && cast != expr.argument(0)) {
                expr.assign_argument(0, cast);
            }
        }
    } else if (type::is_drain(var_type)) {
        // Drain type init: adapt argument to drain type.
        if (!expr.empty()) {
            auto cast = adapt_type(expr.argument(0), var_type);
            if (cast && cast != expr.argument(0)) {
                expr.assign_argument(0, cast);
            }
        }
    } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(var_type)) {
        auto st = st_type->get_struct();
        std::vector<std::shared_ptr<expression>> ctor_args = expr.arguments();

        // Union types: no constructors; default construction sets discriminant = 0.
        // The implementation_generator handles the actual code emission.
        if (!st) {
            // This is a union type (struct_type with no owning aggregate).
            // Mark the constructor invocation as null (no real constructor to call).
            expr.set_constructor(nullptr);
            expr.arguments(ctor_args);
            return;
        }

        // For non-static inner structs: auto-inject __parent__ if constructing from the outer struct context
        if (st && st->is_inner()) {
            auto outer_struct = st->get_enclosing_structure();
            auto var_elem = dynamic_cast<const element*>(var_def.get());
            bool in_outer_method = false;
            if (var_elem) {
                auto enclosing_func = var_elem->ancestor<function>();
                if (enclosing_func && enclosing_func->is_member() && !enclosing_func->is_static()) {
                    if (enclosing_func->get_owner() == outer_struct) in_outer_method = true;
                }
            }
            if (in_outer_method) {
                bool needs_inject = true;
                if (!st->constructors().empty()) {
                    if (ctor_args.size() == st->constructors()[0]->parameters().size()) needs_inject = false;
                }
                if (needs_inject) {
                    auto this_sym = symbol_expression::from_identifier(k::name("this"));
                    auto enclosing_func = var_elem->ancestor<function>();
                    if (enclosing_func && enclosing_func->get_this_parameter()) {
                        this_sym->set_target(std::const_pointer_cast<parameter>(enclosing_func->get_this_parameter()));
                        this_sym->set_type(enclosing_func->get_this_parameter()->get_type());
                    }
                    ctor_args.insert(ctor_args.begin(), this_sym);
                    expr.arguments(ctor_args);
                }
            }
        }

        // ── Direct struct copy: if single arg has the same struct type (by value or by ref),
        //    allow direct aggregate copy without a constructor.
        if (ctor_args.size() == 1) {
            auto arg_type = ctor_args[0]->get_type();
            // Aliases are nominal only: a copy from an alias of the same struct
            // is still a direct struct copy.
            auto arg_type_nc = type::canonical(type::remove_const(arg_type));
            bool is_direct_copy = false;
            bool is_lvalue_copy = false; // source is a ref<struct> (lvalue)
            // Check bare struct type (rvalue from function return or temporary)
            if (arg_type_nc == st_type) {
                is_direct_copy = true;
            }
            // Check ref<struct> (lvalue variable)
            if (!is_direct_copy && type::is_reference(arg_type_nc)) {
                auto ref_sub = type::canonical(type::remove_const(std::dynamic_pointer_cast<reference_type>(arg_type_nc)->get_subtype()));
                if (ref_sub == st_type) {
                    is_direct_copy = true;
                    is_lvalue_copy = true;
                }
            }
            if (is_direct_copy) {
                // For lvalue copies: reject non-trivial structs that have no copy constructor.
                if (is_lvalue_copy && st) {
                    bool has_cc    = st->get_copy_constructor() != nullptr;
                    // Check for a resource-referencing field (owner/pointer/link/view,
                    // recursively through nested struct members) — such a field makes
                    // shallow copy unsafe even without a direct owner-typed field.
                    if (!has_cc && aggregate_type_has_resource_field(st_type)) {
                        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::ERR_TYPE_NOT_COPYABLE),
                            expr.first_lexeme(),
                            "Type '{}' is not copyable: variable initialisation from a lvalue requires a copy constructor",
                            {st_type->to_string()});
                    }
                }
                // Direct copy: null constructor signals aggregate store in impl_gen
                expr.set_constructor(nullptr);
                expr.arguments(ctor_args);
                return;
            }
        }

        auto [best_constructor, adapted_args] = get_best_matching_constructor(st->constructors(), ctor_args);
        if (!best_constructor) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_MEMBER_NO_MATCH), expr.first_lexeme(),
                "No matching constructor found for member initialisation of type '{}': "
                "none of the available constructors can be called with the provided arguments",
                {st_type->to_string()});
        }
        // Cannot instantiate an abstract class directly.
        // Skip this check for synthetic base sub-object fields (e.g. __base_X__ / __vbase_X__)
        // which are compiler-generated during base class construction.
        if (st->is_abstract()) {
            auto var_name = var_def->get_short_name();
            bool is_subobject_init = (var_name.rfind("__base_", 0) == 0)
                                  || (var_name.rfind("__vbase_", 0) == 0);
            if (!is_subobject_init) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_MEMBER_FUNC_NO_MATCH), expr.first_lexeme(),
                    "Cannot instantiate abstract class '{}'; abstract classes cannot be directly instantiated",
                    {st->get_short_name()});
            }
        }
        // Check constructor visibility
        check_constructor_visibility(*best_constructor, expr);
        // Check exception contract for throwing constructors
        check_call_contract(*best_constructor, expr.first_lexeme());
        expr.set_constructor(best_constructor);
        expr.arguments(adapted_args);
    }
}

//
// Temporary construction expression (type_reference_resolver)
//
void type_reference_resolver::visit_temporary_construction_expression(temporary_construction_expression& expr) {
    // Step 1: Resolve all argument expressions
    for (size_t i = 0; i < expr.arguments().size(); ++i) {
        _replacement_expr = nullptr;
        expr.arguments()[i]->accept(*this);
        if (_replacement_expr) {
            expr.assign_argument(i, _replacement_expr);
            _replacement_expr = nullptr;
        }
    }

    // Handle primitive type materialization (rvalue-to-reference binding via adapt_type).
    // When adapt_type creates a temporary_construction_expression for a primitive (e.g. int),
    // no constructor resolution is needed — just confirm the ref<T> result type.
    if (auto prim_type = std::dynamic_pointer_cast<primitive_type>(expr.constructed_type())) {
        // Result type is ref<primitive>: the alloca will be used as a reference.
        expr.set_type(prim_type->get_reference());
        return;
    }

    auto st_type = std::dynamic_pointer_cast<struct_type>(expr.constructed_type());
    if (!st_type) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_NEW_EXPECT_STRUCT_OR_PRIM), expr.first_lexeme(),
            "Temporary construction expression requires a struct type, but '{}' was provided",
            {expr.constructed_type() ? expr.constructed_type()->to_string() : "?"});
    }

    // Union temporary construction (struct_type with no owning aggregate):
    // allow U() and U(x) where x matches one alternative.
    if (!st_type->get_struct()) {
        if (expr.empty()) {
            expr.set_type(st_type->get_reference());
            return;
        }
        if (expr.size() != 1) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_NEW_CTOR_ARG_MISMATCH), expr.first_lexeme(),
                "Union temporary construction of '{}' accepts at most one argument, but {} were provided",
                {st_type->to_string(), std::to_string(expr.size())});
        }

        auto union_def = find_union_by_struct_type(_unit.get_root_namespace(), st_type);
        if (!union_def) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F02C), expr.first_lexeme(),
                "Internal error: union temporary '{}' has no union definition metadata",
                {st_type->to_string()});
        }

        auto src = expr.argument(0);
        std::shared_ptr<expression> best_expr;
        cast_weight best_weight = CAST_IMPOSSIBLE;
        for (const auto* alt : union_def->all_alternatives_ptrs()) {
            if (!alt || !alt->resolved_type) continue;
            auto w = compute_cast_weight(src, alt->resolved_type);
            if (w == CAST_IMPOSSIBLE) continue;
            auto adapted = (w == CAST_NONE) ? src : adapt_type(src, alt->resolved_type);
            if (!adapted) continue;
            if (!best_expr || w < best_weight) {
                best_expr = adapted;
                best_weight = w;
            }
        }
        if (!best_expr) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_NEW_CTOR_ARG_MISMATCH), expr.first_lexeme(),
                "No union alternative in '{}' can be constructed from argument type '{}'",
                {st_type->to_string(), src && src->get_type() ? src->get_type()->to_string() : "?"});
        }

        if (best_expr != src) {
            expr.assign_argument(0, best_expr);
        }
        expr.set_type(st_type->get_reference());
        return;
    }

    auto st = st_type->get_struct();

    // Check not abstract
    if (st->is_abstract()) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_MEMBER_FUNC_NO_MATCH), expr.first_lexeme(),
            "Cannot instantiate abstract class '{}' as a temporary; abstract classes cannot be directly instantiated",
            {st->get_short_name()});
    }

    // Step 2: Resolve constructor
    auto constructors = st->constructors();
    if (constructors.empty() && expr.empty()) {
        // No constructors and no arguments: zero-init (default construction)
        expr.set_type(st_type->get_reference());
        return;
    }

    if (constructors.empty() && expr.size() == 1) {
        // Direct copy from a single argument (struct copy)
        expr.set_type(st_type->get_reference());
        return;
    }

    if (constructors.empty()) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_NEW_CTOR_ARG_MISMATCH), expr.first_lexeme(),
            "Struct '{}' has no constructors, but {} arguments were provided for temporary construction",
            {st->get_short_name(), std::to_string(expr.size())});
    }

    auto [best_constructor, adapted_args] = get_best_matching_constructor(constructors, expr.arguments());
    if (!best_constructor) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_NEW_CTOR_ARG_MISMATCH), expr.first_lexeme(),
            "No matching constructor found for temporary construction of '{}' with {} argument(s)",
            {st->get_short_name(), std::to_string(expr.size())});
    }

    check_constructor_visibility(*best_constructor, expr);
    // Check exception contract for throwing constructors
    check_call_contract(*best_constructor, expr.first_lexeme());
    expr.set_constructor(best_constructor);
    expr.assign_arguments(adapted_args);
    // The temporary is an alloca → its type is a reference to the struct
    expr.set_type(st_type->get_reference());
}

/**
 * Generate LLVM IR for a constructor invocation expression.
 *
 * Steps:
 *   1. Evaluate all argument expressions.
 *   2. Determine the constructor's LLVM function.
 *   3. Allocate the target struct on the stack (or use provided destination).
 *   4. Call the constructor with 'this' pointer + adapted arguments.
 *   5. For inner classes: pass __parent__ pointer as first constructor arg.
 *   6. Handle copy-construction (direct aggregate copy if no constructor).
 */
void implementation_generator::visit_constructor_invocation_expression(constructor_invocation_expression& expr) {
    // Step 1: Evaluate all argument expressions
    auto var_def = expr.constructed_symbol()->get_variable_def();
    if (!var_def) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F02C), expr.first_lexeme(),
            "Internal error: constructor invocation expression does not refer to a variable definition; "
            "this indicates a compiler bug — the constructed symbol must be a variable");
    }

    // Aliases have no representation of their own: construct on the canonical type.
    auto var_type = type::canonical(var_def->get_type());

    llvm::Value* object_ref = nullptr;
    _value = nullptr;
    expr.constructed_symbol()->accept(*this);
    object_ref = _value;
    _value = nullptr;

    if(!object_ref) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F02D), expr.first_lexeme(),
            "Internal error: failed to obtain an LLVM reference for the object being constructed ('{}'); "
            "the variable must have been allocated before constructor code generation",
            {var_def->get_fq_name()});
    }

    if (auto prim_type = std::dynamic_pointer_cast<primitive_type>(var_type)) {
        llvm::Value* value = nullptr;
        if (!expr.empty()) {
            auto first_arg = expr.argument(0);
            if (auto value_expr = std::dynamic_pointer_cast<value_expression>(first_arg)) {
                if (!std::dynamic_pointer_cast<global_variable_definition>(value_expr)) {
                    llvm::Constant* constant = _context->get_llvm_constant_from_value_expression(*value_expr);
                    if (constant == nullptr) {
                        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F02E), expr.first_lexeme(),
                            "Internal error: failed to generate an LLVM constant from a literal value expression "
                            "during primitive constructor invocation for variable '{}'; "
                            "this indicates a compiler bug",
                            {var_def->get_fq_name()});
                    } else {
                        value = constant;
                    }
                }
            }
            if (value == nullptr) {
                _value = nullptr;
                first_arg->accept(*this);
                if (!_value) {
                    throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F02F), expr.first_lexeme(),
                        "Internal error: failed to generate an LLVM value for the initialisation argument "
                        "of variable '{}'; the argument expression produced no result",
                        {var_def->get_fq_name()});
                } else {
                    value = _value;
                }
            }
        }
        if (value != nullptr) {
            _builder->CreateStore(value, object_ref);
        }
    } else if (auto et = std::dynamic_pointer_cast<enum_type>(var_type)) {
        // Enum type: treat like a primitive — store the integer value.
        llvm::Value* value = nullptr;
        if (!expr.empty()) {
            auto first_arg = expr.argument(0);
            _value = nullptr;
            first_arg->accept(*this);
            if (!_value) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F054), expr.first_lexeme(),
                    "Internal error: failed to generate an LLVM value for enum initialisation "
                    "of variable '{}'; the argument expression produced no result",
                    {var_def->get_fq_name()});
            }
            value = _value;
        } else {
            // Default construction: use default value initializer
            value = et->generate_default_value_initializer();
        }
        if (value != nullptr) {
            _builder->CreateStore(value, object_ref);
        }
    } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(var_type)) {
        auto st = st_type->get_struct();
        auto function = expr.get_constructor();

        // ── Union type (struct_type with no aggregate): zero-init ──
        if (!st) {
            // Union construction
            auto* llvm_struct_ty = _context->get_llvm_type(st_type);
            if (!llvm_struct_ty) {
                _value = object_ref;
                return;
            }

            if (expr.empty()) {
                // Default construction: zero-initialize the entire struct (discriminant=0, storage=0)
                auto* zero_val = llvm::ConstantAggregateZero::get(llvm_struct_ty);
                _builder->CreateStore(zero_val, object_ref);
            } else {
                // Typed construction: find matching alternative by type and store the value
                // First, zero-init the storage
                auto* zero_val = llvm::ConstantAggregateZero::get(llvm_struct_ty);
                _builder->CreateStore(zero_val, object_ref);

                // Evaluate the argument
                _value = nullptr;
                expr.argument(0)->accept(*this);
                llvm::Value* init_val = _value;

                if (init_val) {
                    // Find the matching alternative by type
                    // For now, match by the first alternative with a compatible LLVM type
                    std::shared_ptr<union_type_def> union_def;
                    auto root_ns = _unit.get_root_namespace();
                    if (root_ns) {
                        union_def = find_union_by_struct_type(root_ns, st_type);
                    }

                    uint32_t disc_value = 0;
                    if (union_def) {
                        auto arg_type = expr.argument(0)->get_type();
                        // Strip reference if the argument is a reference (lvalue)
                        if (type::is_reference(arg_type)) {
                            arg_type = arg_type->get_subtype();
                        }
                        // Search the FULL inheritance chain for a matching alternative
                        for (const auto* alt_ptr : union_def->all_alternatives_ptrs()) {
                            if (alt_ptr->resolved_type == arg_type ||
                                alt_ptr->resolved_type == type::remove_const(arg_type)) {
                                disc_value = static_cast<uint32_t>(alt_ptr->index);
                                break;
                            }
                        }
                    }

                    // Store discriminant
                    auto* disc_ptr = _builder->CreateStructGEP(llvm_struct_ty, object_ref, 0, "union_disc");
                    _builder->CreateStore(
                        llvm::ConstantInt::get(llvm::Type::getInt32Ty(_builder->getContext()), disc_value),
                        disc_ptr);

                    // Store value in storage
                    auto* storage_ptr = _builder->CreateStructGEP(llvm_struct_ty, object_ref, 1, "union_storage");
                    // If init_val is a pointer (reference to value), load the value first
                    if (type::is_reference(expr.argument(0)->get_type())) {
                        // Look up the alternative by global index to get its LLVM type
                        if (union_def) {
                            const auto* alt_ptr = union_def->get_alternative_by_global_index(disc_value);
                            if (alt_ptr && alt_ptr->resolved_type) {
                                auto* val_type = alt_ptr->resolved_type->get_llvm_type();
                                if (val_type) {
                                    init_val = _builder->CreateLoad(val_type, init_val, "init_load");
                                }
                            }
                        }
                    }
                    _builder->CreateStore(init_val, storage_ptr);
                }
            }
            _value = object_ref;
            return;
        }

        // ── Direct struct copy (no constructor): use emit_value_copy_or_move ──
        // Variable initialisation `x : T = expr;` where the resolver selected a direct
        // aggregate copy (no constructor). For rvalue temporaries, emit_value_copy_or_move
        // performs a move (memcpy + cancel cleanup). For lvalue references, it calls the
        // copy constructor (if any) or falls back to memcpy for trivially copyable types.
        if (!function && expr.size() == 1) {
            _value = nullptr;
            expr.argument(0)->accept(*this);
            if (_value && _value != object_ref) {
                auto arg_type = expr.argument(0)->get_type();
                bool src_is_pointer = type::is_reference(arg_type) || _value->getType()->isPointerTy();
                if (src_is_pointer) {
                    // Source is addressable (alloca, reference): use emit_value_copy_or_move.
                    // This handles lvalue copy-ctor calls and prvalue move (cancel cleanup).
                    emit_value_copy_or_move(object_ref, _value, st_type, /*destroy_dest_first=*/false,
                        expr.first_lexeme(), "variable initialisation");
                } else {
                    // Source is an immediate aggregate value (e.g. template aggregate constant):
                    // store directly without going through memcpy.
                    _builder->CreateStore(_value, object_ref);
                }
            }
            // If _value == object_ref the inner expression (e.g. an sret call whose
            // destination was pre-wired to object_ref) already wrote into the slot,
            // so no further action is required.
            _value = object_ref;
            return;
        }

        // The outer _sret_destination (if any) is meant for the object being constructed
        // by THIS constructor invocation (already bound to object_ref above) — it must
        // not leak into evaluation of the constructor's OWN arguments. Otherwise a nested
        // sret-returning argument call (e.g. `g : OptionalConstRef<int> = get();` where
        // get() returns a *different* struct type OptionalRef<int>, requiring a converting
        // constructor OptionalConstRef(other: const OptionalRef<int>&)) would wrongly write
        // its result directly into object_ref, aliasing 'other' with 'this' at the call
        // site — the constructor then zero-initializes 'this' on entry before reading
        // 'other', wiping out the very value it was supposed to convert.
        std::vector<llvm::Value*> args;
        args.push_back(object_ref);
        llvm::Value* saved_sret_destination_for_ctor_args = _sret_destination;
        _sret_destination = nullptr;
        for(auto arg : expr.arguments()) {
            _value = nullptr;
            arg->accept(*this);
            if(!_value) {
                _sret_destination = saved_sret_destination_for_ctor_args;
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F030), expr.first_lexeme(),
                    "Internal error: a constructor argument for type '{}' produced no LLVM value; "
                    "this indicates a code-generation bug",
                    {st_type->to_string()});
            }
            args.push_back(_value);
        }
        _sret_destination = saved_sret_destination_for_ctor_args;
        auto it = _context->_functions.find(function);
        if(it==_context->_functions.end()) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F031), expr.first_lexeme(),
                "Internal error: LLVM declaration not found for constructor of type '{}'; "
                "the declaration pass must be run before the implementation pass",
                {st_type->to_string()});
        }
        llvm::Function* llvm_func = it->second;
        if(!llvm_func) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F032), expr.first_lexeme(),
                "Internal error: LLVM constructor function object is null for type '{}'; "
                "this indicates a compiler bug in the declaration pass",
                {st_type->to_string()});
        }
        _value = create_call_or_invoke(llvm_func->getFunctionType(), llvm_func, args);

    } else if (type::is_owner(var_type)) {
        // Owner type member init: _buf(buf) — move the owner pointer.
        // Load the pointer from the source (parameter alloca), store into the member,
        // then null out the source so the exit-param cleanup doesn't free it.
        if (!expr.empty()) {
            _value = nullptr;
            expr.argument(0)->accept(*this);
            if (_value) {
                auto arg_type = expr.argument(0)->get_type();
                llvm::Value* ptr_val = _value;
                // If arg is ref<owner<T>>, load the owner pointer from the alloca
                if (arg_type && type::is_reference(arg_type)) {
                    auto inner = std::dynamic_pointer_cast<reference_type>(arg_type)->get_subtype();
                    inner = type::remove_const(inner);
                    if (type::is_owner(inner)) {
                        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
                        // _value is the alloca of the source owner variable
                        llvm::Value* src_alloca = _value;
                        ptr_val = _builder->CreateLoad(ptr_ty, src_alloca, "owner_move_load");
                        // Store into the destination member
                        _builder->CreateStore(ptr_val, object_ref);
                        // Null out the source to prevent double-free
                        _builder->CreateStore(
                            llvm::ConstantPointerNull::get(llvm::PointerType::get(_builder->getContext(), 0)),
                            src_alloca);
                    } else {
                        _builder->CreateStore(ptr_val, object_ref);
                    }
                } else {
                    // Direct owner value (e.g. from a move expression)
                    _builder->CreateStore(ptr_val, object_ref);
                }
            }
        }
        _value = object_ref;

    } else if (auto ptr_var_type = std::dynamic_pointer_cast<pointer_type>(var_type)) {
        // Pointer (*) variable: store the address of the pointed-to object.
        // For ref<indirection> source, load the indirection value; for ref<struct>, the alloca address IS the pointer.
        if (!expr.empty()) {
            _value = nullptr;
            expr.argument(0)->accept(*this);
            if (_value) {
                auto arg_type = expr.argument(0)->get_type();
                if (arg_type && type::is_reference(arg_type)) {
                    auto inner = std::dynamic_pointer_cast<reference_type>(arg_type)->get_subtype();
                    // Only load if inner is an indirection (ptr/link/pin); for a plain struct the alloca IS the pointer.
                    if (type::is_any_indirection(inner)) {
                        _value = _builder->CreateLoad(_context->get_llvm_type(inner), _value, "ptr_init_load");
                    }
                }
                _builder->CreateStore(_value, object_ref);
            }
        }
        _value = object_ref;

    } else if (type::is_link(var_type) || type::is_view(var_type)) {
        // Link (+) or view (?) variable: store the address of the linked object.
        if (!expr.empty()) {
            _value = nullptr;
            expr.argument(0)->accept(*this);
            if (_value) {
                // If the argument type is ref<link/pin/ptr<T>>, load the stored indirection value.
                // If the argument type is ref<struct_T> (a direct object reference), the alloca
                // address IS the link value — no load needed.
                auto arg_type = expr.argument(0)->get_type();
                if (arg_type && type::is_reference(arg_type)) {
                    auto inner = std::dynamic_pointer_cast<reference_type>(arg_type)->get_subtype();
                    if (type::is_any_indirection(inner)) {
                        // ref<link/pin/ptr<T>>: load the stored pointer value
                        _value = _builder->CreateLoad(_context->get_llvm_type(inner), _value, "ind_init_load");
                    }
                    // else: ref<struct T> — _value is already the address of the object (= the link)
                }
                if (type::is_link(var_type)) {
                    // Non-null required: emit null-check if source is nullable.
                    // If the argument is a cast_expression (e.g. upcast Derived→Base), pierce through to
                    // the original source type to check its nullability.
                    auto effective_type = arg_type;
                    if (effective_type && type::is_reference(effective_type)) {
                        effective_type = std::dynamic_pointer_cast<reference_type>(effective_type)->get_subtype();
                    }
                    // Pierce cast_expression to find the real source nullability
                    if (auto cast_arg = std::dynamic_pointer_cast<cast_expression>(expr.argument(0))) {
                        auto inner_type = cast_arg->sub_expr()->get_type();
                        if (inner_type && type::is_reference(inner_type)) {
                            inner_type = std::dynamic_pointer_cast<reference_type>(inner_type)->get_subtype();
                        }
                        if (inner_type && type::is_nullable_indirection(inner_type)) {
                            effective_type = inner_type;
                        }
                    }
                    if (effective_type && type::is_nullable_indirection(effective_type)) {
                        set_debug_location(expr.first_lexeme());
                        auto* fatal = get_or_declare_fatal_null_function("__k_fatal_null_assignation");
                        emit_null_check(_value, fatal, "link_ctor");
                    }
                }
                _builder->CreateStore(_value, object_ref);
            }
        }
        _value = object_ref;

    } else if (auto ref_type = std::dynamic_pointer_cast<reference_type>(var_type)) {
        // Reference variable — could be a plain ref (int&) or an array ref (int[N]&).
        // In both cases we need the raw alloca, not the loaded pointer.

        llvm::Value* alloca_ptr = nullptr;
        if (auto local_var = std::dynamic_pointer_cast<variable_statement>(var_def)) {
            auto it = _context->_variables.find(local_var);
            if (it != _context->_variables.end()) alloca_ptr = it->second;
        } else if (auto global_var = std::dynamic_pointer_cast<global_variable_definition>(var_def)) {
            auto it = _context->_global_vars.find(global_var);
            if (it != _context->_global_vars.end()) alloca_ptr = it->second;
        } else if (std::dynamic_pointer_cast<member_variable_definition>(var_def)) {
            // Member variable of a struct/annotation: storage is a GEP into 'this',
            // already computed as object_ref.
            alloca_ptr = object_ref;
        }

        if (!alloca_ptr) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F034), expr.first_lexeme(),
                "Internal error: could not obtain the storage location for reference variable '{}'; "
                "the variable must have been allocated before constructor code generation",
                {var_def->get_fq_name()});
        }

        if (!expr.empty()) {
            auto first_arg = expr.argument(0);
            _value = nullptr;
            first_arg->accept(*this);
            if (!_value) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F035), expr.first_lexeme(),
                    "Internal error: reference variable '{}' initialisation argument produced no LLVM value; "
                    "this indicates a code-generation bug",
                    {var_def->get_fq_name()});
            }

            auto ref_sub = ref_type->get_subtype();
            if (type::is_sized_array(ref_sub)) {
                // int[N]& : copy-initialise.
                // alloca_ptr is the alloca of the reference variable — it holds a POINTER (ptr),
                // not the struct itself.  We must:
                //   1. Allocate a fresh { i32, [N x T] } struct in the entry block.
                //   2. Copy elements from the source into that fresh struct.
                //   3. Store the address of the fresh struct into alloca_ptr.
                auto dest_arr = std::dynamic_pointer_cast<sized_array_type>(ref_sub);
                auto src_ptr  = _value; // ptr to source struct { i32, [M x T] }

                // Step 2: Determine the constructor's LLVM function
                auto* struct_llvm    = dest_arr->get_llvm_struct_type();
                auto* data_arr_llvm  = dest_arr->get_llvm_data_array_type();
                auto* elem_llvm      = _context->get_llvm_type(dest_arr->get_subtype());
                auto  dest_n         = static_cast<uint64_t>(dest_arr->get_size());

                // Step 3: Allocate the target struct on the stack (or use provided destination)
                // Allocate the destination struct in the entry block
                auto* fn = _builder->GetInsertBlock()->getParent();
                llvm::IRBuilder<> entry_build(&fn->getEntryBlock(), fn->getEntryBlock().begin());
                llvm::AllocaInst* dest_struct_alloca = entry_build.CreateAlloca(
                    struct_llvm, nullptr, var_def->get_short_name() + "_arr_storage");

                // Step 4: Call the constructor with 'this' pointer + adapted arguments
                // Zero-fill destination struct, then set its size field
                _builder->CreateStore(llvm::ConstantAggregateZero::get(struct_llvm), dest_struct_alloca);
                llvm::Value* dest_size_field = _builder->CreateStructGEP(struct_llvm, dest_struct_alloca,
                    sized_array_type::FIELD_SIZE, "dest_size_fld");
                _builder->CreateStore(
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(_builder->getContext()), dest_n, false),
                    dest_size_field);

                // Step 5: For inner classes: pass __parent__ pointer as first constructor arg
                // Determine the source struct type (may differ in array size from dest_arr)
                // We read the source capacity from field 0.
                // Use the source's struct type by reading src_n from field 0.
                // Since field 0 is always i32 at offset 0, we can use dest_arr's struct type for GEP.
                llvm::Value* src_size_field = _builder->CreateStructGEP(struct_llvm, src_ptr,
                    sized_array_type::FIELD_SIZE, "src_size_fld");
                llvm::Value* src_n = _builder->CreateLoad(
                    llvm::Type::getInt32Ty(_builder->getContext()), src_size_field, "src_n");

                // Data pointers
                llvm::Value* src_data_ptr  = _builder->CreateStructGEP(struct_llvm, src_ptr,
                    sized_array_type::FIELD_DATA, "src_data_ptr");
                llvm::Value* dest_data_ptr = _builder->CreateStructGEP(struct_llvm, dest_struct_alloca,
                    sized_array_type::FIELD_DATA, "dest_data_ptr");

                // copy_n = min(dest_n, src_n)
                auto* i32_t = llvm::Type::getInt32Ty(_builder->getContext());
                auto* dest_n_val = llvm::ConstantInt::get(i32_t, dest_n, false);
                llvm::Value* copy_n_val = _builder->CreateSelect(
                    _builder->CreateICmpULT(src_n, dest_n_val), src_n, dest_n_val, "copy_n");

                // Step 6: Handle copy-construction (direct aggregate copy if no constructor)
                // --- copy loop ---
                auto* copy_entry_bb = _builder->GetInsertBlock();
                auto* copy_loop_bb  = llvm::BasicBlock::Create(_builder->getContext(), "arr_ref_copy_loop", fn);
                auto* copy_done_bb  = llvm::BasicBlock::Create(_builder->getContext(), "arr_ref_copy_done", fn);
                auto* zero_loop_bb  = llvm::BasicBlock::Create(_builder->getContext(), "arr_ref_zero_loop", fn);
                auto* init_done_bb  = llvm::BasicBlock::Create(_builder->getContext(), "arr_ref_init_done", fn);

                _builder->CreateCondBr(
                    _builder->CreateICmpUGT(copy_n_val, llvm::ConstantInt::get(i32_t, 0, false)),
                    copy_loop_bb, copy_done_bb);

                _builder->SetInsertPoint(copy_loop_bb);
                auto* copy_idx = _builder->CreatePHI(i32_t, 2, "copy_idx");
                copy_idx->addIncoming(llvm::ConstantInt::get(i32_t, 0, false), copy_entry_bb);

                llvm::Value* s_elem = _builder->CreateGEP(data_arr_llvm, src_data_ptr,
                    {_builder->getInt32(0), copy_idx}, "s_elem");
                llvm::Value* d_elem = _builder->CreateGEP(data_arr_llvm, dest_data_ptr,
                    {_builder->getInt32(0), copy_idx}, "d_elem");
                _builder->CreateStore(_builder->CreateLoad(elem_llvm, s_elem, "ev"), d_elem);

                auto* copy_next = _builder->CreateAdd(copy_idx,
                    llvm::ConstantInt::get(i32_t, 1, false), "copy_next");
                copy_idx->addIncoming(copy_next, copy_loop_bb);
                _builder->CreateCondBr(
                    _builder->CreateICmpULT(copy_next, copy_n_val),
                    copy_loop_bb, copy_done_bb);

                _builder->SetInsertPoint(copy_done_bb);
                _builder->CreateCondBr(
                    _builder->CreateICmpULT(copy_n_val, dest_n_val),
                    zero_loop_bb, init_done_bb);

                _builder->SetInsertPoint(zero_loop_bb);
                auto* zero_idx = _builder->CreatePHI(i32_t, 2, "zero_idx");
                zero_idx->addIncoming(copy_n_val, copy_done_bb);

                llvm::Value* z_elem = _builder->CreateGEP(data_arr_llvm, dest_data_ptr,
                    {_builder->getInt32(0), zero_idx}, "z_elem");
                _builder->CreateStore(llvm::Constant::getNullValue(elem_llvm), z_elem);

                auto* zero_next = _builder->CreateAdd(zero_idx,
                    llvm::ConstantInt::get(i32_t, 1, false), "zero_next");
                zero_idx->addIncoming(zero_next, zero_loop_bb);
                _builder->CreateCondBr(
                    _builder->CreateICmpULT(zero_next, dest_n_val),
                    zero_loop_bb, init_done_bb);

                _builder->SetInsertPoint(init_done_bb);

                // Store the address of our local struct into the reference variable's alloca
                _builder->CreateStore(dest_struct_alloca, alloca_ptr);
                object_ref = alloca_ptr;
            } else {
                // Plain reference (int&, struct&, etc.) — store the address of the referent
                _builder->CreateStore(_value, alloca_ptr);
                object_ref = alloca_ptr;
            }
        } else {
            // No initialisation argument for a reference-type variable.
            // For member variables (e.g. in annotation default constructors), store null.
            if (std::dynamic_pointer_cast<member_variable_definition>(var_def)) {
                auto* null_ptr = llvm::ConstantPointerNull::get(
                    llvm::PointerType::get(_builder->getContext(), 0));
                _builder->CreateStore(null_ptr, alloca_ptr);
                object_ref = alloca_ptr;
            } else {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F036), expr.first_lexeme(),
                    "Internal error: reference variable '{}' has no initialisation argument; "
                    "the resolver should have rejected this earlier",
                    {var_def->get_fq_name()});
            }
        }

    } else if (type::is_drain(var_type)) {
        // Drain variable — store the drain address into the alloca.
        // Similar to reference init: get the raw alloca and store the address.
        llvm::Value* alloca_ptr = nullptr;
        if (auto local_var = std::dynamic_pointer_cast<variable_statement>(var_def)) {
            auto it = _context->_variables.find(local_var);
            if (it != _context->_variables.end()) alloca_ptr = it->second;
        } else if (auto global_var = std::dynamic_pointer_cast<global_variable_definition>(var_def)) {
            auto it = _context->_global_vars.find(global_var);
            if (it != _context->_global_vars.end()) alloca_ptr = it->second;
        }
        if (!alloca_ptr) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F034), expr.first_lexeme(),
                "Internal error: could not obtain the storage location for drain variable '{}'; "
                "the variable must have been allocated before constructor code generation",
                {var_def->get_fq_name()});
        }
        if (!expr.empty()) {
            _value = nullptr;
            expr.argument(0)->accept(*this);
            if (_value) {
                _builder->CreateStore(_value, alloca_ptr);
            }
        }
        object_ref = alloca_ptr;

    } else if (auto sized_arr_type = std::dynamic_pointer_cast<sized_array_type>(var_type)) {
        // Sized array value variable: int[N]
        // No explicit init — zero-initialise the entire struct, then set the count field.
        auto* struct_llvm = sized_arr_type->get_llvm_struct_type();
        if (!struct_llvm) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F037), expr.first_lexeme(),
                "Internal error: sized array variable '{}' has no LLVM struct type",
                {var_def->get_fq_name()});
        }
        // Zero-fill the entire struct { i32, [N x T] }
        _builder->CreateStore(llvm::ConstantAggregateZero::get(struct_llvm), object_ref);
        // Then write the element count N into field 0
        llvm::Value* size_field_ptr = _builder->CreateStructGEP(struct_llvm, object_ref,
            sized_array_type::FIELD_SIZE, "arr_size_ptr");
        _builder->CreateStore(
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(_builder->getContext()),
                sized_arr_type->get_size(), false),
            size_field_ptr);
        // Field 1 (data) is now zeroed — primitives are ready.
        // TODO: call default constructors for struct element types.
    } else if (type::is_function_reference(var_type)) {
        // Function-reference variable (*(T), ^(T), ~(T)): store the function pointer value.
        // The variable is an alloca of type ptr (opaque pointer).
        // After visiting the argument, _value is already the raw function pointer (ptr):
        // - if the arg is a symbol_expression resolving to a function, impl_gen sets _value=llvm_func directly.
        // - if the arg is a symbol_expression resolving to a function_reference_type variable,
        //   our modified visit_symbol_expression already loads the value from the alloca.
        if (!expr.empty()) {
            _value = nullptr;
            expr.argument(0)->accept(*this);
            if (_value) {
                _builder->CreateStore(_value, object_ref);
            }
        }
        _value = object_ref;

    } else {
        // TODO This is probably a non-primitive primary type, so direct construction will be done
    }

    // The result of a constructor invocation is the reference to the constructed object
    _value = object_ref;
}

//
// Temporary construction expression (implementation_generator)
//
void implementation_generator::visit_temporary_construction_expression(temporary_construction_expression& expr) {
    // ── Primitive type materialization: rvalue-to-reference binding ─────────────
    // When passed a primitive value as a reference parameter, adapt_type creates a
    // temporary_construction_expression<primitive_type>. Codegen: alloca + store.
    if (auto prim_type = std::dynamic_pointer_cast<primitive_type>(expr.constructed_type())) {
        llvm::Type* llvm_ty = _context->get_llvm_type(prim_type);
        llvm::Function* current_fn = _builder->GetInsertBlock()->getParent();
        llvm::IRBuilder<> entry_builder(&current_fn->getEntryBlock(),
                                        current_fn->getEntryBlock().begin());
        auto* temp_alloca = entry_builder.CreateAlloca(llvm_ty, nullptr, "tmp_prim_ref");
        if (expr.size() == 1) {
            _value = nullptr;
            expr.argument(0)->accept(*this);
            if (_value) {
                _builder->CreateStore(_value, temp_alloca);
            }
        }
        _value = temp_alloca;
        return;
    }

    auto st_type = std::dynamic_pointer_cast<struct_type>(expr.constructed_type());
    if (!st_type) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F02C), expr.first_lexeme(),
            "Internal error: temporary construction expression has no resolved struct type");
    }

    auto st = st_type->get_struct();
    llvm::Type* llvm_struct_ty = _context->get_llvm_type(st_type);

    // Check if an sret destination has been provided (e.g., by ternary operator or variable_statement).
    // If so, use it WITHOUT consuming it (so it remains available for other uses).
    // Otherwise create a new temporary alloca.
    llvm::Value* temp_alloca;
    if (_sret_destination) {
        temp_alloca = _sret_destination;
    } else {
        // Create a stack alloca in the entry block for the temporary
        llvm::Function* current_fn = _builder->GetInsertBlock()->getParent();
        llvm::IRBuilder<> entry_builder(&current_fn->getEntryBlock(),
                                         current_fn->getEntryBlock().begin());
        temp_alloca = entry_builder.CreateAlloca(llvm_struct_ty, nullptr, "tmp_ctor");
    }

    // Union temporary construction (struct_type with no owning aggregate).
    if (!st) {
        auto* union_llvm_ty = llvm::dyn_cast<llvm::StructType>(llvm_struct_ty);
        if (!union_llvm_ty) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F02C), expr.first_lexeme(),
                "Internal error: union temporary '{}' has no LLVM struct type",
                {st_type->to_string()});
        }

        // Default union temporary: discriminant/storage zeroed.
        _builder->CreateStore(llvm::ConstantAggregateZero::get(union_llvm_ty), temp_alloca);

        if (expr.size() == 1) {
            auto union_def = find_union_by_struct_type(_unit.get_root_namespace(), st_type);
            if (!union_def) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F02C), expr.first_lexeme(),
                    "Internal error: union temporary '{}' has no union definition metadata",
                    {st_type->to_string()});
            }

            _value = nullptr;
            expr.argument(0)->accept(*this);
            llvm::Value* init_val = _value;
            if (!init_val) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F030), expr.first_lexeme(),
                    "Internal error: union temporary '{}' argument produced no LLVM value",
                    {st_type->to_string()});
            }

            auto arg_type = expr.argument(0)->get_type();
            bool arg_is_ref = type::is_reference(arg_type) || type::is_drain(arg_type);
            auto arg_nc = type::remove_const(arg_type);
            if (type::is_reference(arg_nc) || type::is_drain(arg_nc)) {
                arg_nc = type::remove_const(arg_nc->get_subtype());
            }

            const union_alternative* selected_alt = nullptr;
            for (const auto* alt : union_def->all_alternatives_ptrs()) {
                if (!alt || !alt->resolved_type) continue;
                if (type::remove_const(alt->resolved_type) == arg_nc) {
                    selected_alt = alt;
                    break;
                }
            }
            if (!selected_alt || !selected_alt->resolved_type) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F02C), expr.first_lexeme(),
                    "Internal error: no union alternative of '{}' matches argument type '{}'",
                    {st_type->to_string(), arg_type ? arg_type->to_string() : "?"});
            }

            auto* disc_ptr = _builder->CreateStructGEP(union_llvm_ty, temp_alloca, 0, "union_disc");
            _builder->CreateStore(
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(_builder->getContext()),
                    static_cast<uint32_t>(selected_alt->index)),
                disc_ptr);

            auto* storage_ptr = _builder->CreateStructGEP(union_llvm_ty, temp_alloca, 1, "union_storage");
            llvm::Type* alt_llvm_type = _context->get_llvm_type(selected_alt->resolved_type);
            if (arg_is_ref || init_val->getType()->isPointerTy()) {
                init_val = _builder->CreateLoad(alt_llvm_type, init_val, "union_tmp_init_load");
            }
            auto* typed_storage_ptr = _builder->CreateBitCast(storage_ptr, alt_llvm_type->getPointerTo(),
                "union_storage_typed");
            _builder->CreateStore(init_val, typed_storage_ptr);
        } else if (!expr.empty()) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_NEW_CTOR_ARG_MISMATCH), expr.first_lexeme(),
                "Union temporary construction of '{}' accepts at most one argument, but {} were provided",
                {st_type->to_string(), std::to_string(expr.size())});
        }

        _value = temp_alloca;
        return;
    }

    auto ctor = expr.get_constructor();

    if (!ctor && expr.empty()) {
        // No constructor and no arguments: zero-init
        _builder->CreateStore(llvm::ConstantAggregateZero::get(llvm_struct_ty), temp_alloca);
    } else if (!ctor && expr.size() == 1) {
        // Direct struct copy/move from a single argument.
        _value = nullptr;
        expr.argument(0)->accept(*this);
        if (_value) {
            if (_value->getType()->isPointerTy()) {
                // Move the argument into the freshly materialised temporary when it
                // is a prvalue temporary (transfers ownership + cancels the source's
                // destruction), or copy it otherwise.  A plain load+store aggregate
                // copy would shallow-duplicate any owned buffer (e.g. Vector<T>) and,
                // since both temporaries are registered for destruction, cause a
                // double free.
                emit_value_copy_or_move(temp_alloca, _value, st_type,
                                        /*destroy_dest_first=*/false,
                                        expr.first_lexeme(), "temporary construction");
            } else {
                // Already a loaded scalar/aggregate value: store directly.
                _builder->CreateStore(_value, temp_alloca);
            }
        }
    } else if (ctor) {
        // Constructor call: evaluate arguments and call the constructor.
        // `temp_alloca` may alias the outer `_sret_destination` (reused above without
        // consuming it) — it must not leak into evaluation of the constructor's OWN
        // arguments, or a nested sret-returning argument call of a *different* struct
        // type would wrongly write its result directly into temp_alloca, aliasing
        // 'other' with 'this' at the call site (see visit_constructor_invocation_expression
        // for the same fix and full rationale).
        std::vector<llvm::Value*> args;
        args.push_back(temp_alloca); // 'this' pointer
        llvm::Value* saved_sret_destination_for_ctor_args = _sret_destination;
        _sret_destination = nullptr;
        for (auto& arg : expr.arguments()) {
            _value = nullptr;
            arg->accept(*this);
            if (!_value) {
                _sret_destination = saved_sret_destination_for_ctor_args;
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F030), expr.first_lexeme(),
                    "Internal error: a constructor argument for temporary '{}' produced no LLVM value",
                    {st_type->to_string()});
            }
            args.push_back(_value);
        }
        _sret_destination = saved_sret_destination_for_ctor_args;
        auto ctor_fn = ctor->shared_as<k::model::function>();
        auto it = _context->_functions.find(ctor_fn);
        if (it == _context->_functions.end()) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F031), expr.first_lexeme(),
                "Internal error: LLVM declaration not found for constructor of type '{}'",
                {st_type->to_string()});
        }
        llvm::Function* llvm_ctor = it->second;
        if (!llvm_ctor) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F032), expr.first_lexeme(),
                "Internal error: LLVM constructor function object is null for type '{}'",
                {st_type->to_string()});
        }
        create_call_or_invoke(llvm_ctor->getFunctionType(), llvm_ctor, args, "");
    }

    // Register the temporary for destructor cleanup at full-expression boundary,
    // but ONLY if this alloca is NOT from an external _sret_destination (those are managed by the caller).
    if (!_sret_destination || temp_alloca != _sret_destination) {
        auto dtor = st->get_destructor();
        if (dtor) {
            auto dtor_fn = dtor->shared_as<k::model::function>();
            auto dtor_it = _context->_functions.find(dtor_fn);
            if (dtor_it != _context->_functions.end()) {
                if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(temp_alloca)) {
                    _expression_temporaries.push_back({alloca, dtor_it->second, nullptr});
                }
            }
        }
    }

    // The result is the alloca pointer (like a reference to the temporary)
    _value = temp_alloca;
}

//
// Cast expression
//

/**
 * Resolve a cast expression: validate source-to-target type compatibility.
 *
 * Steps:
 *   1. Resolve the sub-expression and the target type.
 *   2. Check for casting operator overload on the source struct type.
 *   3. Validate primitive casts (widening, narrowing, int↔float).
 *   4. Validate pointer/link/view/owner casts (same indirection kind or cross-kind).
 *   5. Validate struct upcast (static) and downcast (dynamic, requires RTTI).
 *   6. Set the result type to the cast target type.
 */

} // namespace k::model::gen
