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
#include "../model/constant_evaluator.hpp"
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
// new, delete, owner_move, array_init, designated_struct_init

void symbol_resolver::visit_delete_expression(delete_expression& expr) {
    if (expr.sub_expr()) expr.sub_expr()->accept(*this);
}

void type_reference_resolver::visit_delete_expression(delete_expression& expr) {
    if (expr.sub_expr()) {
        expr.sub_expr()->accept(*this);
    }
    // The sub-expression must be an owner (directly or via reference-to-owner)
    auto sub = expr.sub_expr();
    if (!sub) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DELETE_EXPECT_EXPR), expr.first_lexeme(), "'delete' requires an expression");
    }
    auto sub_type = sub->get_type();
    // Unwrap reference-to-owner if needed
    if (type::is_reference(sub_type)) {
        auto ref = std::dynamic_pointer_cast<reference_type>(sub_type);
        sub_type = ref->get_subtype();
    }
    if (!type::is_owner(sub_type)) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DELETE_NOT_OWNER), expr.first_lexeme(),
            "'delete' can only be applied to an owner ('!') type, got '{}'",
            {sub->get_type() ? sub->get_type()->to_string() : "<null>"});
    }
    // Result type is void
    expr.set_type(nullptr); // void
}

//
// Owner move expression
//

void symbol_resolver::visit_owner_move_expression(owner_move_expression& expr) {
    if (expr.sub_expr()) expr.sub_expr()->accept(*this);
}

void type_reference_resolver::visit_owner_move_expression(owner_move_expression& expr) {
    if (expr.sub_expr()) expr.sub_expr()->accept(*this);
    auto src_type = expr.sub_expr() ? expr.sub_expr()->get_type() : nullptr;
    if (!src_type) return;
    // Source must be ref<owner<T>> → result type is owner<T>
    if (type::is_reference(src_type)) {
        auto inner = type::remove_const(std::dynamic_pointer_cast<reference_type>(src_type)->get_subtype());
        if (type::is_owner(inner)) {
            expr.set_type(inner);
            return;
        }
        if (auto ct = std::dynamic_pointer_cast<callable_type>(inner)) {
            if (ct->is_owner()) {
                expr.set_type(inner);
                return;
            }
        }
    }
    // If already owner<T> or owned callable (e.g., wrapping a new_expression rvalue), pass through
    if (type::is_owner(src_type)) {
        expr.set_type(src_type);
        return;
    }
    if (auto ct = std::dynamic_pointer_cast<callable_type>(src_type)) {
        if (ct->is_owner()) {
            expr.set_type(src_type);
            return;
        }
    }
}

void implementation_generator::visit_owner_move_expression(owner_move_expression& expr) {
    _value = nullptr;
    if (expr.sub_expr()) expr.sub_expr()->accept(*this);
    if (!_value) return;

    auto& llvm_ctx = _builder->getContext();
    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    auto src_type = expr.sub_expr() ? expr.sub_expr()->get_type() : nullptr;
    if (src_type && type::is_reference(src_type)) {
        auto inner = type::remove_const(std::dynamic_pointer_cast<reference_type>(src_type)->get_subtype());
        if (auto ct = std::dynamic_pointer_cast<callable_type>(inner)) {
            if (ct->is_owner()) {
                auto* callable_llvm_type = _context->get_or_create_callable_llvm_type();
                llvm::Value* alloca_ptr = _value;
                _value = _builder->CreateLoad(callable_llvm_type, alloca_ptr, "own_callable_move_val");
                _builder->CreateStore(llvm::ConstantAggregateZero::get(callable_llvm_type), alloca_ptr);
                return;
            }
        }
        // Source is ref<owner<T>>: _value is the alloca (pointer to owner slot)
        llvm::Value* alloca_ptr = _value;
        // Load the raw owner pointer from the alloca
        _value = _builder->CreateLoad(ptr_ty, alloca_ptr, "own_move_val");
        // Null out the source alloca (ownership transferred)
        _builder->CreateStore(llvm::ConstantPointerNull::get(ptr_ty), alloca_ptr);
    }
    // else: _value is already the raw owner pointer (e.g., from new_expression)
}

//
// Array init expression
//

void symbol_resolver::visit_array_init_expression(array_init_expression& expr) {
    if (expr.constructed_symbol()) expr.constructed_symbol()->accept(*this);
    if (expr.is_uniform()) {
        for (auto& a : expr.uniform_ctor_args()) {
            if (a) a->accept(*this);
        }
    } else {
        for (size_t i = 0; i < expr.size(); ++i) {
            if (auto e = expr.element(i)) e->accept(*this);
        }
    }
}

void symbol_resolver::visit_designated_struct_init_expression(designated_struct_init_expression& expr) {
    if (expr.constructed_symbol()) expr.constructed_symbol()->accept(*this);
    for (auto& m : expr.members_mutable()) {
        if (m.value) m.value->accept(*this);
        for (auto& a : m.args) {
            if (a) a->accept(*this);
        }
    }
}

/**
 * Resolve an array brace-initialization expression ({elem1, elem2, ...}).
 *
 * Steps:
 *   1. Determine the expected element type from the parent variable's array type.
 *   2. Resolve each element expression via visitor.
 *   3. Adapt each element's type to match the expected element type.
 *   4. Validate element count against the array size.
 */
void type_reference_resolver::visit_array_init_expression(array_init_expression& expr) {
    if (expr.is_uniform()) {
        // ── Uniform array init: var : T(args)[N]; ──
        // Resolve uniform ctor args
        for (auto& a : expr._uniform_ctor_args) {
            if (a) a->accept(*this);
        }

        auto var_def = expr.constructed_symbol() ? expr.constructed_symbol()->get_variable_def() : nullptr;
        if (!var_def) return;
        auto var_type = var_def->get_type();
        if (!type::is_sized_array(var_type)) return;

        auto arr_type = std::dynamic_pointer_cast<sized_array_type>(var_type);
        auto elem_type = arr_type->get_subtype();
        size_t arr_size = arr_type->get_size();

        // Update stored array size
        expr._array_size = arr_size;

        // Check for abstract types
        if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
            auto struct_model = st_type->get_struct();
            if (struct_model && struct_model->is_abstract()) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_ALLOC_NOT_POINTER), expr.first_lexeme(),
                    "Cannot create uniform array of abstract class '{}'",
                    {struct_model->get_short_name()});
                return;
            }
        }

        // Resolve the constructor / type-check for the uniform args
        if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
            auto struct_model = st_type->get_struct();
            if (!struct_model) return;
            auto [best_ctor, adapted_args] = get_best_matching_constructor(
                struct_model->constructors(), expr._uniform_ctor_args);
            if (!best_ctor) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_ALLOC_NOT_ARRAY), expr.first_lexeme(),
                    "No matching constructor for uniform array init of type '{}'",
                    {st_type->to_string()});
                return;
            }
            expr._uniform_constructor = best_ctor;
            expr.set_uniform_ctor_args(adapted_args);
        } else if (type::is_primitive(elem_type)) {
            // Primitive: must have exactly one arg convertible to the element type
            if (expr._uniform_ctor_args.size() > 1) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_ALLOC_TYPE_MISMATCH), expr.first_lexeme(),
                    "Uniform array init for primitive type '{}' expects at most one argument, got {}",
                    {elem_type->to_string(), std::to_string(expr._uniform_ctor_args.size())});
                return;
            }
            if (!expr._uniform_ctor_args.empty() && expr._uniform_ctor_args[0]) {
                auto cast = adapt_type(expr._uniform_ctor_args[0], elem_type);
                if (!cast) {
                    throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_ALLOC_TYPE_MISMATCH), expr.first_lexeme(),
                        "Cannot convert uniform init value to primitive element type '{}'",
                        {elem_type->to_string()});
                    return;
                }
                if (cast != expr._uniform_ctor_args[0]) {
                    expr.assign_uniform_ctor_arg(0, cast);
                }
            }
        }

        // Evaluate uniform constant array
        if (arr_type && elem_type && !expr._uniform_ctor_args.empty() && expr._uniform_ctor_args[0] && expr._uniform_ctor_args[0]->is_constant()) {
            auto cast_elem = constant_evaluator::cast_to_type(expr._uniform_ctor_args[0]->get_constant_value(), elem_type);
            if (cast_elem) {
                std::vector<constant_value> const_elements(arr_size, *cast_elem);
                auto av = std::make_shared<array_value>(arr_type, std::move(const_elements));
                expr.set_constant_value(constant_value(av));
            }
        }
        return;
    }

    // Resolve sub-expressions
    for (size_t i = 0; i < expr.size(); ++i) {
        if (auto e = expr.element(i)) e->accept(*this);
    }

    // Step 1: Determine expected array/element types.
    auto var_def = expr.constructed_symbol() ? expr.constructed_symbol()->get_variable_def() : nullptr;
    std::shared_ptr<sized_array_type> arr_type;
    std::shared_ptr<type> elem_type;

    if (expr.is_temporary()) {
        auto resolved_elem = resolve_type_by_name(expr.temporary_type_name(), expr);
        if (!resolved_elem) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_INIT_NOT_STRUCT), expr.first_lexeme(),
                "Unknown type '{}' in temporary array initializer",
                {expr.temporary_type_name().to_string()});
        }
        auto elem_nc = type::remove_const(resolved_elem);
        if (type::is_reference(elem_nc)) elem_nc = elem_nc->get_subtype();
        elem_type = elem_nc;
        auto arr_unsized = elem_type ? elem_type->get_array() : nullptr;
        if (!arr_unsized) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_INIT_NOT_STRUCT), expr.first_lexeme(),
                "Type '{}' cannot be used as temporary array element type",
                {expr.temporary_type_name().to_string()});
        }
        arr_type = arr_unsized->with_size(expr.size());
        expr._array_size = expr.size();
        expr.set_type(arr_type->get_reference());
    } else {
        if (!var_def) return;
        auto var_type = var_def->get_type();
        if (!type::is_sized_array(var_type)) return;
        arr_type = std::dynamic_pointer_cast<sized_array_type>(var_type);
        elem_type = arr_type->get_subtype();
    }

    // Validate element count
    size_t arr_size = arr_type->get_size();
    size_t init_count = expr.size();

    if (init_count > arr_size) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_INIT_NOT_STRUCT), expr.first_lexeme(),
            "Array initializer list has {} elements, but the array '{}' has size {}: too many initializers",
            {std::to_string(init_count), var_def ? var_def->get_fq_name() : "<temporary>",
             std::to_string(arr_size)});
        return;
    }
    if (init_count < arr_size && init_count > 0) {
        warn(static_cast<unsigned int>(k::diag::type_diag::WARN_DESIG_INIT_PARTIAL),
            "Array initializer list has {} elements, but the array '{}' has size {}: "
            "remaining {} elements will be default-initialized",
            {std::to_string(init_count), var_def ? var_def->get_fq_name() : "<temporary>", std::to_string(arr_size),
             std::to_string(arr_size - init_count)});
    }

    // Type-check and adapt each element
    if (type::is_primitive(elem_type)) {
        for (size_t i = 0; i < init_count; ++i) {
            auto e = expr.element(i);
            if (!e) continue; // default-init slot
            auto cast = adapt_type(e, elem_type);
            if (!cast) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_INIT_MEMBER_NOT_FOUND), expr.first_lexeme(),
                    "Cannot convert array element {} to type '{}' for array '{}'",
                    {std::to_string(i), elem_type->to_string(),
                     var_def ? var_def->get_fq_name() : "<temporary>"});
            } else if (cast != e) {
                expr.assign_element(i, cast);
            }
        }
    } else if (type::is_any_indirection(elem_type)) {
        // Indirection element types (link, pointer, view, owner):
        // each init expression must be adaptable to the element indirection type.
        for (size_t i = 0; i < init_count; ++i) {
            auto e = expr.element(i);
            if (!e) continue;
            auto cast = adapt_type(e, elem_type);
            if (!cast) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_INIT_MEMBER_NOT_FOUND), expr.first_lexeme(),
                    "Cannot convert array element {} to indirection type '{}' for array '{}'",
                    {std::to_string(i), elem_type->to_string(),
                     var_def ? var_def->get_fq_name() : "<temporary>"});
            } else if (cast != e) {
                expr.assign_element(i, cast);
            }
        }
    } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
        auto struct_model = st_type->get_struct();
        for (size_t i = 0; i < init_count; ++i) {
            auto e = expr.element(i);
            if (!e) continue; // default-init slot, will use default ctor

            // Direct struct value/reference of the same element type (e.g. Point{...} temporary)
            // can be used without constructor-call syntax.
            if (auto e_type = e->get_type()) {
                auto e_nc = type::remove_const(e_type);
                if (type::is_reference(e_nc)) e_nc = e_nc->get_subtype();
                if (auto e_st = std::dynamic_pointer_cast<struct_type>(e_nc)) {
                    if (e_st == st_type) {
                        auto cast = adapt_type(e, elem_type);
                        if (cast && cast != e) {
                            expr.assign_element(i, cast);
                        }
                        continue;
                    }
                }
            }

            // Step 2: Resolve each element expression via visitor
            // Check if element is a function invocation (explicit constructor call)
            // The model_builder creates function_invocation_expression for Name(args...) patterns
            auto func_inv = std::dynamic_pointer_cast<function_invocation_expression>(e);
            if (func_inv) {
                // Explicit constructor call — resolve constructor
                std::vector<std::shared_ptr<expression>> ctor_args;
                for (auto& arg : func_inv->arguments()) {
                    ctor_args.push_back(arg);
                }
                // Step 3: Adapt each element's type to match the expected element type
                auto [best_ctor, adapted_args] = get_best_matching_constructor(struct_model->constructors(), ctor_args);
                if (!best_ctor) {
                    throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_INIT_TYPE_MISMATCH), expr.first_lexeme(),
                        // Step 4: Validate element count against the array size
                        "No matching constructor for array element {} of type '{}'",
                        {std::to_string(i), st_type->to_string()});
                }
                // Store ctor info in the expression for later use by codegen
                // For now, adapt the arguments
                func_inv->assign_arguments(adapted_args);
            } else {
                // Implicit single-param constructor
                std::vector<std::shared_ptr<expression>> ctor_args = {e};
                auto [best_ctor, adapted_args] = get_best_matching_constructor(struct_model->constructors(), ctor_args);
                if (!best_ctor) {
                    throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_INIT_CTOR_MISMATCH), expr.first_lexeme(),
                        "No matching single-parameter constructor for array element {} of type '{}' "
                        "with argument type '{}'",
                        {std::to_string(i), st_type->to_string(),
                         e->get_type() ? e->get_type()->to_string() : "?"});
                }
                if (!adapted_args.empty() && adapted_args[0] != e) {
                    expr.assign_element(i, adapted_args[0]);
                }
             }
        }
    }

    // Evaluate constant array value if all elements are constant
    if (arr_type && elem_type) {
        std::vector<constant_value> const_elements;
        bool all_const = true;
        auto default_elem = constant_evaluator::default_value_for_type(elem_type);

        for (size_t i = 0; i < arr_size; ++i) {
            if (i < init_count && expr.element(i)) {
                if (expr.element(i)->is_constant()) {
                    auto cast_elem = constant_evaluator::cast_to_type(expr.element(i)->get_constant_value(), elem_type);
                    if (cast_elem) {
                        const_elements.push_back(*cast_elem);
                    } else {
                        const_elements.push_back(expr.element(i)->get_constant_value());
                    }
                } else {
                    all_const = false;
                    break;
                }
            } else {
                if (default_elem) {
                    const_elements.push_back(*default_elem);
                } else {
                    all_const = false;
                    break;
                }
            }
        }
        if (all_const) {
            auto av = std::make_shared<array_value>(arr_type, std::move(const_elements));
            expr.set_constant_value(constant_value(av));
        }
    }
}

/**
 * Resolve a designated struct initialization expression ({.field1 = val1, .field2 = val2}).
 *
 * Steps:
 *   1. Determine the target struct type from the parent variable.
 *   2. For each designator: look up the field in the struct, resolve the value expression.
 *   3. Adapt each value's type to match the field's type.
 *   4. Validate that all designators refer to valid fields.
 */
void type_reference_resolver::visit_designated_struct_init_expression(designated_struct_init_expression& expr) {
    // Resolve sub-expressions in each member initializer
    for (auto& m : expr.members_mutable()) {
        if (m.value) m.value->accept(*this);
        for (auto& a : m.args) {
            if (a) a->accept(*this);
        }
    }

    // Step 1: Determine the target struct type.
    // For temporaries, resolve from the type name.
    // For top-level designated inits, derive from the constructed variable's type.
    // For nested designated inits (no constructed_symbol), _target_aggregate is pre-set by the parent.
    std::shared_ptr<struct_type> st_type;
    std::shared_ptr<aggregate> target_struct = expr._target_aggregate;

    if (!target_struct) {
        if (expr.is_temporary()) {
            // Temporary designated init: resolve type by name
            auto resolved_type = resolve_type_by_name(expr.type_name(), expr);
            if (!resolved_type) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_STRUCT_NOT_FOUND), expr.first_lexeme(),
                    "Unknown type '{}' in temporary designated initializer",
                    {expr.type_name()});
                return;
            }
            auto resolved_nc = type::remove_const(resolved_type);
            if (type::is_reference(resolved_nc))
                resolved_nc = resolved_nc->get_subtype();
            st_type = std::dynamic_pointer_cast<struct_type>(resolved_nc);
            if (!st_type || !st_type->get_struct()) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_STRUCT_NOT_FOUND), expr.first_lexeme(),
                    "Designated initializer can only be used with struct types, but '{}' is not a struct",
                    {expr.type_name()});
                return;
            }
            target_struct = st_type->get_struct();

            // Check for abstract classes
            if (target_struct->is_abstract()) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_NEW_EXPECT_STRUCT_OR_PRIM), expr.first_lexeme(),
                    "Cannot create temporary of abstract type '{}'",
                    {target_struct->get_short_name()});
                return;
            }

            expr._target_aggregate = target_struct;
            // Set the expression type to a reference to the struct type
            expr.set_type(st_type->get_reference());
        } else {
            auto var_def = expr.constructed_symbol() ? expr.constructed_symbol()->get_variable_def() : nullptr;
            if (!var_def) return;
            auto var_type = var_def->get_type();

            // Unwrap reference if needed
            if (type::is_reference(var_type)) {
                var_type = std::dynamic_pointer_cast<reference_type>(var_type)->get_subtype();
            }

            st_type = std::dynamic_pointer_cast<struct_type>(var_type);
            if (!st_type) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_STRUCT_NOT_FOUND), expr.first_lexeme(),
                    "Designated initializer can only be used with struct types, but '{}' has type '{}'",
                    {var_def->get_fq_name(), var_type ? var_type->to_string() : "?"});
                return;
            }
            target_struct = st_type->get_struct();
            if (!target_struct) return;

            // Only valid for structs, not classes with virtual inheritance
            if (target_struct->is_class() && target_struct->has_virtual_bases()) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_STRUCT_TOO_FEW_ARGS), expr.first_lexeme(),
                    "Designated initializer cannot be used with class '{}' which has virtual bases",
                    {target_struct->get_short_name()});
                return;
            }

            // Store the resolved aggregate in the expression
            expr._target_aggregate = target_struct;
        }
    } else {
        // Nested designated init: _target_aggregate already set
        st_type = target_struct->get_struct_type();
    }

    // Step 3: Adapt each value's type to match the field's type
    // Collect all accessible member variables from the struct and its bases
    // Map: member_name -> (member_var, owning_aggregate)
    struct member_info {
        std::shared_ptr<member_variable_definition> var;
        std::shared_ptr<aggregate> owner;
    };
    std::map<std::string, std::vector<member_info>> all_members;

    // Helper: skip synthetic members (__base_X__, __vbptr_X__, __vbase_X__, __parent__)
    auto is_synthetic = [](const std::string& name) {
        return name.size() >= 4 && name[0] == '_' && name[1] == '_'
            && name[name.size()-1] == '_' && name[name.size()-2] == '_';
    };

    // Gather members from the struct itself
    for (auto& [name, var] : target_struct->variables()) {
        auto mem = std::dynamic_pointer_cast<member_variable_definition>(var);
        if (!mem) continue;
        if (is_synthetic(name)) continue;
        all_members[name].push_back({mem, target_struct});
    }

    // Gather inherited members from base classes
    auto all_bases = target_struct->get_all_bases();
    for (auto& base : all_bases) {
        if (!base.base) continue;
        for (auto& [name, var] : base.base->variables()) {
            auto mem = std::dynamic_pointer_cast<member_variable_definition>(var);
            if (!mem) continue;
            if (is_synthetic(name)) continue;
            all_members[name].push_back({mem, base.base});
        }
    }

    // Step 4: Validate that all designators refer to valid fields
    // Validate and resolve each designated member
    std::set<std::string> seen_members;
    for (auto& m : expr.members_mutable()) {
        std::string full_name = m.qualifier.empty()
            ? m.member_name
            : m.qualifier + "::" + m.member_name;

        // Check for duplicate designators
        if (seen_members.count(full_name)) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_STRUCT_FIELD_NOT_FOUND), expr.first_lexeme(),
                "Duplicate designated initializer for member '.{}'",
                {full_name});
            continue;
        }
        seen_members.insert(full_name);

        // Find the member
        auto it = all_members.find(m.member_name);
        if (it == all_members.end()) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_STRUCT_DUPLICATE_FIELD), expr.first_lexeme(),
                "No member '{}' in struct '{}' for designated initializer",
                {m.member_name, target_struct->get_short_name()});
            continue;
        }

        auto& candidates = it->second;

        // Resolve ambiguity using qualifier if needed
        std::shared_ptr<member_variable_definition> resolved_mem;
        std::shared_ptr<aggregate> resolved_owner;
        if (candidates.size() > 1 && m.qualifier.empty()) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_STRUCT_TOO_MANY_ARGS), expr.first_lexeme(),
                "Ambiguous member '{}' in struct '{}': "
                "use a qualified name (e.g. '.Base::{}') to disambiguate",
                {m.member_name, target_struct->get_short_name(), m.member_name});
            continue;
        } else if (!m.qualifier.empty()) {
            // Find the candidate matching the qualifier
            bool found = false;
            for (auto& cand : candidates) {
                if (cand.owner->get_short_name() == m.qualifier) {
                    resolved_mem = cand.var;
                    resolved_owner = cand.owner;
                    found = true;
                    break;
                }
            }
            if (!found) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_STRUCT_EXTRA_POSITIONAL), expr.first_lexeme(),
                    "No member '{}::{}' found in struct '{}'",
                    {m.qualifier, m.member_name, target_struct->get_short_name()});
                continue;
            }
        } else {
            resolved_mem = candidates[0].var;
            resolved_owner = candidates[0].owner;
        }

        // Check accessibility
        if (resolved_mem->get_visibility() == PRIVATE || resolved_mem->get_visibility() == PROTECTED) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_STRUCT_FIELD_TYPE_MISMATCH), expr.first_lexeme(),
                "Member '{}' is {} in struct '{}' and cannot be used in a designated initializer",
                {full_name,
                 resolved_mem->get_visibility() == PRIVATE ? "private" : "protected",
                 target_struct->get_short_name()});
            continue;
        }

        m.resolved_member = resolved_mem;
        m.resolved_owner = resolved_owner;

        // Type-check the initializer value
        auto member_type = resolved_mem->get_type();
        if (m.is_call_form) {
            // Constructor form: .member(args...)
            if (auto mem_st_type = std::dynamic_pointer_cast<struct_type>(member_type)) {
                auto mem_struct = mem_st_type->get_struct();
                if (mem_struct) {
                    auto [best_ctor, adapted_args] = get_best_matching_constructor(
                        mem_struct->constructors(), m.args);
                    if (!best_ctor) {
                        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_STRUCT_CTOR_ARG_MISMATCH), expr.first_lexeme(),
                            "No matching constructor for member '{}' of type '{}'",
                            {full_name, mem_st_type->to_string()});
                    } else {
                        m.resolved_constructor = best_ctor;
                        m.args = adapted_args;
                    }
                }
            } else {
                // Primitive or indirection: constructor form is like a single-arg init
                if (m.args.size() != 1) {
                    throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_STRUCT_CTOR_NOT_FOUND), expr.first_lexeme(),
                        "Constructor form for non-aggregate member '{}' of type '{}' expects exactly one argument",
                        {full_name, member_type ? member_type->to_string() : "?"});
                } else if (m.args[0]) {
                    auto cast = adapt_type(m.args[0], member_type);
                    if (!cast) {
                        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_STRUCT_CTOR_AMBIGUOUS), expr.first_lexeme(),
                            "Cannot convert argument to type '{}' for member '{}'",
                            {member_type->to_string(), full_name});
                    } else if (cast != m.args[0]) {
                        m.args[0] = cast;
                    }
                }
            }
        } else {
            // Assignment form: .member = expr
            if (m.value) {
                // Check if value is a brace_init_list (nested designated init) — handled as sub-struct
                if (auto desig_sub = std::dynamic_pointer_cast<designated_struct_init_expression>(m.value)) {
                    // Pre-set target aggregate from the member type for nested resolution
                    if (auto mem_st_type = std::dynamic_pointer_cast<struct_type>(member_type)) {
                        desig_sub->_target_aggregate = mem_st_type->get_struct();
                    }
                    // Resolve recursively
                    desig_sub->accept(*this);
                } else {
                    auto cast = adapt_type(m.value, member_type);
                    if (!cast) {
                        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DESIG_STRUCT_NO_DEFAULT_CTOR), expr.first_lexeme(),
                            "Cannot convert initializer value to type '{}' for member '{}'",
                            {member_type ? member_type->to_string() : "?", full_name});
                    } else if (cast != m.value) {
                        m.value = cast;
                    }
                }
            }
        }
    }

    // Evaluate constant struct value if target is a struct and all members are constant
    if (target_struct && !target_struct->is_class()) {
        std::map<std::string, constant_value> field_values;
        bool all_const = true;
        for (const auto& m : expr.members()) {
            if (m.is_call_form) {
                if (m.args.size() == 1 && m.args[0] && m.args[0]->is_constant()) {
                    field_values[m.member_name] = m.args[0]->get_constant_value();
                } else {
                    all_const = false;
                    break;
                }
            } else {
                if (m.value && m.value->is_constant()) {
                    field_values[m.member_name] = m.value->get_constant_value();
                } else {
                    all_const = false;
                    break;
                }
            }
        }
        if (all_const) {
            auto res = constant_evaluator::eval_struct_init(target_struct, field_values);
            if (res) {
                expr.set_constant_value(*res);
            }
        }
    }
}

/**
 * Generate LLVM IR for array brace-initialization: alloca + element stores.
 *
 * Steps:
 *   1. Allocate the array on the stack.
 *   2. For each element: evaluate expression, GEP to array slot, store value.
 *   3. Zero-fill remaining elements if fewer initializers than array size.
 */
void implementation_generator::visit_array_init_expression(array_init_expression& expr) {
    std::shared_ptr<sized_array_type> arr_type;
    llvm::Value* arr_alloca = nullptr;

    if (expr.is_temporary()) {
        auto expr_type = type::remove_const(expr.get_type());
        if (type::is_reference(expr_type)) {
            expr_type = expr_type->get_subtype();
        }
        arr_type = std::dynamic_pointer_cast<sized_array_type>(expr_type);
        if (!arr_type) return;

        llvm::Type* llvm_arr_ty = _context->get_llvm_type(arr_type);
        llvm::Function* current_fn = _builder->GetInsertBlock()->getParent();
        llvm::IRBuilder<> entry_builder(&current_fn->getEntryBlock(),
                                        current_fn->getEntryBlock().begin());
        arr_alloca = entry_builder.CreateAlloca(llvm_arr_ty, nullptr, "tmp_arr");
    } else {
        auto var_def = expr.constructed_symbol() ? expr.constructed_symbol()->get_variable_def() : nullptr;
        if (!var_def) return;

        auto var_type = var_def->get_type();
        arr_type = std::dynamic_pointer_cast<sized_array_type>(var_type);
        if (!arr_type) return;

        // Get the alloca for the array variable
        _value = nullptr;
        expr.constructed_symbol()->accept(*this);
        arr_alloca = _value;
        _value = nullptr;
        if (!arr_alloca) return;
    }

    auto* struct_llvm = arr_type->get_llvm_struct_type();
    auto elem_type = arr_type->get_subtype();
    llvm::Type* llvm_elem_type = _context->get_llvm_type(elem_type);
    size_t arr_size = arr_type->get_size();

    // Zero-init the entire array struct first
    _builder->CreateStore(llvm::ConstantAggregateZero::get(struct_llvm), arr_alloca);

    // Write the element count into field 0
    llvm::Value* size_ptr = _builder->CreateStructGEP(struct_llvm, arr_alloca,
        sized_array_type::FIELD_SIZE, "arr_size");
    _builder->CreateStore(
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(_builder->getContext()),
            arr_size, false),
        size_ptr);

    // Step 1: Allocate the array on the stack
    // Get pointer to the data array (field 1)
    llvm::Value* data_ptr = _builder->CreateStructGEP(struct_llvm, arr_alloca,
        sized_array_type::FIELD_DATA, "arr_data");
    auto* llvm_arr_type = arr_type->get_llvm_data_array_type();

    // Step 2: For each element: evaluate expression, GEP to array slot, store value
    if (expr.is_uniform()) {
        // ── Uniform mode: initialize all elements with the same ctor args ──
        for (size_t i = 0; i < arr_size; ++i) {
            llvm::Value* elem_ptr = _builder->CreateConstInBoundsGEP2_32(
                llvm_arr_type, data_ptr, 0, i, "uarr_elem_" + std::to_string(i));

            // Step 3: Zero-fill remaining elements if fewer initializers than array size
            if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
                auto ctor = expr.uniform_constructor();
                if (ctor) {
                    auto ctor_it = _context->_functions.find(ctor->shared_as<function>());
                    if (ctor_it != _context->_functions.end()) {
                        std::vector<llvm::Value*> args;
                        args.push_back(elem_ptr);
                        for (auto& arg : expr.uniform_ctor_args()) {
                            _value = nullptr;
                            arg->accept(*this);
                            if (_value) args.push_back(_value);
                        }
                        create_call_or_invoke(ctor_it->second->getFunctionType(), ctor_it->second, args, "");
                    }
                }
            } else if (type::is_primitive(elem_type)) {
                if (!expr.uniform_ctor_args().empty() && expr.uniform_ctor_args()[0]) {
                    _value = nullptr;
                    expr.uniform_ctor_args()[0]->accept(*this);
                    if (_value) _builder->CreateStore(_value, elem_ptr);
                }
                // else: already zero-inited
            }
        }
    } else {
        // ── Per-element mode (existing behavior) ──
        // Initialize each element
        for (size_t i = 0; i < expr.size() && i < arr_size; ++i) {
            auto elem_expr = expr.element(i);
            if (!elem_expr) continue; // default-init (already zeroed)

            llvm::Value* elem_ptr = _builder->CreateConstInBoundsGEP2_32(
                llvm_arr_type, data_ptr, 0, i, "arr_elem_" + std::to_string(i));

            if (type::is_primitive(elem_type) || type::is_any_indirection(elem_type)) {
                _value = nullptr;
                elem_expr->accept(*this);
                if (_value) {
                    _builder->CreateStore(_value, elem_ptr);
                }
            } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
                // For struct elements, we need to call the constructor
                _value = nullptr;
                elem_expr->accept(*this);
                if (_value) {
                    _builder->CreateStore(_value, elem_ptr);
                }
            }
        }
    }
    if (expr.is_temporary()) {
        auto* temp_alloca = llvm::dyn_cast<llvm::AllocaInst>(arr_alloca);
        if (temp_alloca) {
            auto elem_type = arr_type->get_subtype();
            if (std::dynamic_pointer_cast<struct_type>(elem_type) || std::dynamic_pointer_cast<owner_type>(elem_type)) {
                _expression_temporaries.push_back({temp_alloca, nullptr, arr_type});
            }
        }
    }

    _value = arr_alloca;
}

/**
 * Generate LLVM IR for designated struct initialization: alloca + field stores.
 *
 * Steps:
 *   1. Allocate the struct on the stack, zero-initialize.
 *   2. For each designator: evaluate the value expression, GEP to the field, store.
 *   3. Call the default constructor if the struct has one and not all fields are designated.
 */
void implementation_generator::visit_designated_struct_init_expression(designated_struct_init_expression& expr) {
    // Step 1: Allocate the struct on the stack, zero-initialize
    auto target_struct = expr.target_aggregate();
    if (!target_struct) return;

    std::shared_ptr<struct_type> st_type;
    llvm::Value* struct_alloca = nullptr;

    if (expr.is_temporary()) {
        // Temporary designated init: create a stack alloca
        st_type = target_struct->get_struct_type();
        if (!st_type) return;
        llvm::Type* llvm_struct_ty = _context->get_llvm_type(st_type);

        llvm::Function* current_fn = _builder->GetInsertBlock()->getParent();
        llvm::IRBuilder<> entry_builder(&current_fn->getEntryBlock(),
                                         current_fn->getEntryBlock().begin());
        struct_alloca = entry_builder.CreateAlloca(llvm_struct_ty, nullptr, "tmp.desig");

        // Zero-init
        auto* llvm_type = st_type->get_llvm_type();
        if (llvm_type) {
            _builder->CreateStore(llvm::ConstantAggregateZero::get(llvm_type), struct_alloca);
        }
    } else if (expr.constructed_symbol()) {
        // Top-level designated init: get alloca from variable
        auto var_def = expr.constructed_symbol()->get_variable_def();
        if (!var_def) return;
        st_type = std::dynamic_pointer_cast<struct_type>(var_def->get_type());
        if (!st_type) return;

        _value = nullptr;
        expr.constructed_symbol()->accept(*this);
        struct_alloca = _value;
        _value = nullptr;
        if (!struct_alloca) return;

        // Zero-init the entire struct first (default initialization for all members)
        auto* llvm_type = st_type->get_llvm_type();
        if (llvm_type) {
            _builder->CreateStore(llvm::ConstantAggregateZero::get(llvm_type), struct_alloca);
        }
    } else {
        // Nested designated init: _value is the pre-computed mem_ptr from outer caller
        struct_alloca = _value;
        if (!struct_alloca) return;
        st_type = target_struct->get_struct_type();
        if (!st_type) return;
        _value = nullptr;

        // Zero-init the nested struct sub-object
        auto* llvm_type = st_type->get_llvm_type();
        if (llvm_type) {
            _builder->CreateStore(llvm::ConstantAggregateZero::get(llvm_type), struct_alloca);
        }
    }

    // Step 2: For each designator: evaluate the value expression, GEP to the field, store
    // ── Helper: DFS to find GEP path from a source aggregate to a target aggregate
    //    through __base_X__ sub-objects. Returns the sequence of (struct_type, field_index)
    //    pairs to GEP through.
    struct GepStep {
        llvm::Type* llvm_type;
        unsigned field_index;
        std::string name;
    };
    std::function<bool(aggregate*, struct_type*, aggregate*, std::vector<GepStep>&)> find_base_path;
    find_base_path = [&](aggregate* cur_agg, struct_type* cur_st, aggregate* target_agg,
                          std::vector<GepStep>& path) -> bool {
        if (cur_agg == target_agg) return true;
        for (auto& bs : cur_agg->get_bases()) {
            if (!bs.base || bs.is_virtual) continue;
            std::string field_name = "__base_" + bs.sanitised_name() + "__";
            auto field = cur_st->get_member(field_name);
            if (!field) continue;
            auto base_st = bs.base->get_struct_type();
            if (!base_st) continue;
            path.push_back(GepStep{cur_st->get_llvm_type(), (unsigned)field->index, field_name});
            if (find_base_path(bs.base.get(), base_st.get(), target_agg, path)) {
                return true;
            }
            path.pop_back();
        }
        return false;
    };

    // ── Helper: Get a pointer to a member, navigating through base sub-objects if needed.
    //    Returns nullptr if the path cannot be found.
    auto get_member_ptr = [&](const std::string& member_name,
                               aggregate* member_owner,
                               const std::string& label_prefix) -> llvm::Value* {
        if (member_owner == target_struct.get()) {
            // Direct member of the target struct
            auto field = st_type->get_member(member_name);
            if (!field) return nullptr;
            return _builder->CreateStructGEP(st_type->get_llvm_type(), struct_alloca, field->index,
                                              label_prefix + member_name);
        }
        // Inherited member: navigate __base_X__ chain
        std::vector<GepStep> path;
        if (!find_base_path(target_struct.get(), st_type.get(), member_owner, path)) {
            return nullptr;
        }
        // Walk the GEP path to reach the base sub-object
        llvm::Value* ptr = struct_alloca;
        for (auto& step : path) {
            ptr = _builder->CreateStructGEP(step.llvm_type, ptr, step.field_index,
                                             label_prefix + step.name);
        }
        // Now GEP to the field within the base sub-object
        auto owner_st_type = member_owner->get_struct_type();
        if (!owner_st_type) return nullptr;
        auto field = owner_st_type->get_member(member_name);
        if (!field) return nullptr;
        return _builder->CreateStructGEP(owner_st_type->get_llvm_type(), ptr, field->index,
                                          label_prefix + member_name);
    };

    // Build set of designated member keys for quick lookup
    // Use "qualifier::name" as key to handle qualified members correctly
    std::set<std::string> designated_keys;
    for (auto& m : expr.members()) {
        std::string key = m.qualifier.empty() ? m.member_name : m.qualifier + "::" + m.member_name;
        designated_keys.insert(key);
    }

    // ── Helper: call default constructor for an aggregate-typed member
    auto call_default_ctor = [&](aggregate* mem_struct, llvm::Value* mem_ptr) {
        for (auto& ctor : mem_struct->constructors()) {
            if (ctor->parameters().empty() ||
                (ctor->parameters().size() == 1 /* this */)) {
                auto ctor_it = _context->_functions.find(ctor->shared_as<function>());
                if (ctor_it != _context->_functions.end()) {
                    create_call_or_invoke(ctor_it->second->getFunctionType(), ctor_it->second, {mem_ptr}, "");
                }
                break;
            }
        }
    };

    // Helper: skip synthetic members (__base_X__, __vbptr_X__, __vbase_X__, __parent__)
    auto is_synthetic = [](const std::string& name) {
        return name.size() >= 4 && name[0] == '_' && name[1] == '_'
            && name[name.size()-1] == '_' && name[name.size()-2] == '_';
    };

    // Step 3: Call the default constructor if the struct has one and not all fields are designated
    // For each member in the struct (and its bases) that has a default constructor
    // and is NOT designated, call its default constructor.
    // Process direct members first
    for (auto& [name, var] : target_struct->variables()) {
        auto mem = std::dynamic_pointer_cast<member_variable_definition>(var);
        if (!mem) continue;
        if (is_synthetic(name)) continue;
        if (designated_keys.count(name)) continue;

        auto mem_type = mem->get_type();
        if (auto mem_st_type = std::dynamic_pointer_cast<struct_type>(mem_type)) {
            auto mem_struct = mem_st_type->get_struct();
            if (mem_struct) {
                auto field = st_type->get_member(name);
                if (field) {
                    llvm::Value* mem_ptr = _builder->CreateStructGEP(
                        st_type->get_llvm_type(), struct_alloca, field->index,
                        "desig_default_" + name);
                    call_default_ctor(mem_struct.get(), mem_ptr);
                }
            }
        }
    }

    // Process inherited members from all bases
    auto all_bases = target_struct->get_all_bases();
    for (auto& base : all_bases) {
        if (!base.base) continue;
        for (auto& [name, var] : base.base->variables()) {
            auto mem = std::dynamic_pointer_cast<member_variable_definition>(var);
            if (!mem) continue;
            if (is_synthetic(name)) continue;
            // Check if this member is designated (with or without qualifier)
            bool is_designated = designated_keys.count(name)
                || designated_keys.count(base.base->get_short_name() + "::" + name);
            if (is_designated) continue;

            auto mem_type = mem->get_type();
            if (auto mem_st_type = std::dynamic_pointer_cast<struct_type>(mem_type)) {
                auto mem_struct = mem_st_type->get_struct();
                if (mem_struct) {
                    llvm::Value* mem_ptr = get_member_ptr(name, base.base.get(), "desig_default_");
                    if (mem_ptr) {
                        call_default_ctor(mem_struct.get(), mem_ptr);
                    }
                }
            }
        }
    }

    // Now initialize each designated member
    for (auto& m : expr.members()) {
        auto resolved_mem = m.resolved_member;
        if (!resolved_mem) continue;

        // Determine the owning aggregate for this member
        aggregate* owner = m.resolved_owner ? m.resolved_owner.get() : target_struct.get();

        llvm::Value* mem_ptr = get_member_ptr(m.member_name, owner, "desig_");
        if (!mem_ptr) continue;

        auto mem_type = resolved_mem->get_type();

        if (m.is_call_form) {
            // Constructor form: .member(args...)
            if (m.resolved_constructor) {
                auto ctor_it = _context->_functions.find(m.resolved_constructor->shared_as<function>());
                if (ctor_it != _context->_functions.end()) {
                    std::vector<llvm::Value*> args;
                    args.push_back(mem_ptr); // 'this' pointer
                    for (auto& a : m.args) {
                        _value = nullptr;
                        if (a) a->accept(*this);
                        if (_value) args.push_back(_value);
                    }
                    create_call_or_invoke(ctor_it->second->getFunctionType(), ctor_it->second, args, "");
                }
            } else if (type::is_primitive(mem_type) && !m.args.empty() && m.args[0]) {
                _value = nullptr;
                m.args[0]->accept(*this);
                if (_value) _builder->CreateStore(_value, mem_ptr);
            }
        } else {
            // Assignment form: .member = expr
            if (m.value) {
                if (auto desig_sub = std::dynamic_pointer_cast<designated_struct_init_expression>(m.value)) {
                    // Nested designated init — pass mem_ptr as the alloca via _value
                    _value = mem_ptr;
                    desig_sub->accept(*this);
                } else {
                    _value = nullptr;
                    m.value->accept(*this);
                    if (_value) _builder->CreateStore(_value, mem_ptr);
                }
            }
        }
    }

    // For temporaries, register destructor cleanup at full-expression boundary
    if (expr.is_temporary()) {
        auto dtor = target_struct->get_destructor();
        if (dtor) {
            auto dtor_fn = dtor->shared_as<k::model::function>();
            auto dtor_it = _context->_functions.find(dtor_fn);
            if (dtor_it != _context->_functions.end()) {
                auto* temp_alloca_inst = llvm::cast<llvm::AllocaInst>(struct_alloca);
                _expression_temporaries.push_back({temp_alloca_inst, dtor_it->second, nullptr});
            }
        }
    }

    _value = struct_alloca;
}

/**
 * Generate LLVM IR for a new expression: heap allocation + constructor call.
 *
 * Steps:
 *   1. Compute allocation size (struct size or array element size * count).
 *   2. Call malloc or the allocator function.
 *   3. Cast raw pointer to the target type.
 *   4. For structs: call the resolved constructor on the allocated object.
 *   5. For arrays: zero-initialize or element-wise construct.
 *   6. Store vptr(s) for polymorphic classes.
 *   7. Set _value to the owner<T> result.
 */
void implementation_generator::visit_new_expression(new_expression& expr) {
    auto alloc_type = expr.allocated_type();
    if (!alloc_type) { _value = nullptr; return; }

    auto& llvm_ctx = _builder->getContext();
    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    // Ensure malloc is declared
    llvm::Module& mod = get_module();
    llvm::Function* malloc_fn = mod.getFunction("malloc");
    if (!malloc_fn) {
        auto* malloc_type = llvm::FunctionType::get(
            ptr_ty,
            {llvm::Type::getInt64Ty(llvm_ctx)},
            false);
        malloc_fn = llvm::Function::Create(
            malloc_type, llvm::Function::ExternalLinkage, "malloc", mod);
    }

    if (expr.is_array() && expr.is_dynamic_size() && !expr.is_uniform_array()) {
        // ── Dynamic array form: new T[expr] ──
        // Size is a runtime value. No brace initializers. All elements default-initialized.
        auto elem_type = alloc_type;
        llvm::Type* llvm_elem_type = _context->get_llvm_type(elem_type);

        // Get the unsized array_type from the expression type (owner<array_type>)
        auto own_type = std::dynamic_pointer_cast<owner_type>(expr.get_type());
        auto unsized_arr_type = own_type
            ? std::dynamic_pointer_cast<array_type>(own_type->get_owned_type())
            : nullptr;
        if (!unsized_arr_type) { _value = nullptr; return; }

        auto* struct_llvm = unsized_arr_type->get_llvm_struct_type();
        auto* llvm_arr_type = unsized_arr_type->get_llvm_data_array_type();
        if (!struct_llvm || !llvm_arr_type) { _value = nullptr; return; }

        // Evaluate the size expression → i32 runtime value
        _value = nullptr;
        expr.array_size_expr()->accept(*this);
        llvm::Value* n_val = _value; // i32
        if (!n_val) { _value = nullptr; return; }

        // Compute allocation size: header_size + sizeof(T) * n
        // header_size = offset of field 1 in { i32, [0 x T] }
        auto* i64_ty = llvm::Type::getInt64Ty(llvm_ctx);
        auto* i32_ty = llvm::Type::getInt32Ty(llvm_ctx);
        uint64_t header_size = mod.getDataLayout().getStructLayout(struct_llvm)->getElementOffset(array_type::FIELD_DATA);
        uint64_t elem_size = mod.getDataLayout().getTypeAllocSize(llvm_elem_type);

        llvm::Value* n_i64 = _builder->CreateZExt(n_val, i64_ty, "n_i64");
        llvm::Value* data_bytes = _builder->CreateMul(
            n_i64,
            llvm::ConstantInt::get(i64_ty, elem_size),
            "data_bytes");
        llvm::Value* alloc_size = _builder->CreateAdd(
            data_bytes,
            llvm::ConstantInt::get(i64_ty, header_size),
            "alloc_size");

        // malloc
        llvm::Value* raw_ptr = _builder->CreateCall(
            malloc_fn->getFunctionType(), malloc_fn, {alloc_size}, "new_dynarr_raw");

        // Null-check: throw OutOfMemory if allocation failed
        emit_alloc_null_check(raw_ptr, "new_dynarr");

        // memset to zero
        _builder->CreateMemSet(raw_ptr,
            llvm::ConstantInt::get(llvm::Type::getInt8Ty(llvm_ctx), 0),
            alloc_size, llvm::MaybeAlign(1));

        // Store the element count in field 0
        llvm::Value* size_ptr = _builder->CreateStructGEP(struct_llvm, raw_ptr,
            array_type::FIELD_SIZE, "dynarr_size");
        _builder->CreateStore(n_val, size_ptr);

        // Get pointer to the data area (field 1)
        llvm::Value* data_ptr = _builder->CreateStructGEP(struct_llvm, raw_ptr,
            array_type::FIELD_DATA, "dynarr_data");

        // Step 1: Compute allocation size (struct size or array element size * count)
        // For struct element types with a constructor, emit an IR loop to call
        // the default constructor on each element.
        if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
            auto default_ctor = (!expr.element_constructors().empty())
                ? expr.element_constructors()[0] : nullptr;
            if (default_ctor) {
                auto ctor_it = _context->_functions.find(default_ctor->shared_as<function>());
                if (ctor_it != _context->_functions.end()) {
                    // Emit IR loop: for (i = 0; i < n; ++i) ctor(&data[i])
                    auto* fn = _builder->GetInsertBlock()->getParent();
                    auto* loop_header = llvm::BasicBlock::Create(llvm_ctx, "dynarr_init_hdr", fn);
                    auto* loop_body   = llvm::BasicBlock::Create(llvm_ctx, "dynarr_init_body", fn);
                    auto* loop_end    = llvm::BasicBlock::Create(llvm_ctx, "dynarr_init_end", fn);

                    _builder->CreateBr(loop_header);

                    // Loop header: %i = phi [0, entry], [%i_next, body]; if i < n goto body else end
                    _builder->SetInsertPoint(loop_header);
                    auto* entry_bb = loop_header->getSinglePredecessor();
                    llvm::PHINode* i_phi = _builder->CreatePHI(i32_ty, 2, "dynarr_i");
                    i_phi->addIncoming(llvm::ConstantInt::get(i32_ty, 0), entry_bb);
                    llvm::Value* cmp = _builder->CreateICmpULT(i_phi, n_val, "dynarr_cmp");
                    _builder->CreateCondBr(cmp, loop_body, loop_end);

                    // Loop body: GEP to element, call ctor, increment
                    _builder->SetInsertPoint(loop_body);
                    llvm::Value* indices[] = {llvm::ConstantInt::get(i32_ty, 0), i_phi};
                    llvm::Value* elem_ptr = _builder->CreateGEP(
                        llvm_arr_type, data_ptr, indices, "dynarr_elem");
                    create_call_or_invoke(ctor_it->second->getFunctionType(), ctor_it->second, {elem_ptr}, "");
                    // After invoke, builder may be in invoke_cont block
                    auto* latch_bb = _builder->GetInsertBlock();
                    llvm::Value* i_next = _builder->CreateAdd(
                        i_phi, llvm::ConstantInt::get(i32_ty, 1), "dynarr_i_next");
                    i_phi->addIncoming(i_next, latch_bb);
                    _builder->CreateBr(loop_header);

                    _builder->SetInsertPoint(loop_end);
                }
            }
        }
        // For primitive types: memset already zero-initialized everything.

        _value = raw_ptr;
        return;
    }

    if (expr.is_uniform_array()) {
        // ── Uniform array form: new T(args)[N] ──
        auto elem_type = alloc_type;
        llvm::Type* llvm_elem_type = _context->get_llvm_type(elem_type);

        if (expr.is_dynamic_size()) {
            // ── Dynamic uniform array: new T(args)[expr] ──
            auto own_type = std::dynamic_pointer_cast<owner_type>(expr.get_type());
            auto unsized_arr_type = own_type
                ? std::dynamic_pointer_cast<array_type>(own_type->get_owned_type())
                : nullptr;
            if (!unsized_arr_type) { _value = nullptr; return; }

            auto* struct_llvm = unsized_arr_type->get_llvm_struct_type();
            auto* llvm_arr_type = unsized_arr_type->get_llvm_data_array_type();
            if (!struct_llvm || !llvm_arr_type) { _value = nullptr; return; }

            // Evaluate the size expression → i32 runtime value
            _value = nullptr;
            expr.array_size_expr()->accept(*this);
            llvm::Value* n_val = _value;
            if (!n_val) { _value = nullptr; return; }

            // Compute allocation size: header_size + sizeof(T) * n
            auto* i64_ty = llvm::Type::getInt64Ty(llvm_ctx);
            auto* i32_ty = llvm::Type::getInt32Ty(llvm_ctx);
            uint64_t header_size = mod.getDataLayout().getStructLayout(struct_llvm)->getElementOffset(array_type::FIELD_DATA);
            uint64_t elem_size = mod.getDataLayout().getTypeAllocSize(llvm_elem_type);

            llvm::Value* n_i64 = _builder->CreateZExt(n_val, i64_ty, "n_i64");
            llvm::Value* data_bytes = _builder->CreateMul(
                n_i64,
                llvm::ConstantInt::get(i64_ty, elem_size),
                "data_bytes");
            llvm::Value* alloc_size = _builder->CreateAdd(
                data_bytes,
                llvm::ConstantInt::get(i64_ty, header_size),
                "alloc_size");

            // Step 2: Call malloc or the allocator function
            // malloc
            llvm::Value* raw_ptr = _builder->CreateCall(
                malloc_fn->getFunctionType(), malloc_fn, {alloc_size}, "new_uarr_raw");

            // Null-check: throw OutOfMemory if allocation failed
            emit_alloc_null_check(raw_ptr, "new_uarr_dyn");

            // memset to zero
            _builder->CreateMemSet(raw_ptr,
                llvm::ConstantInt::get(llvm::Type::getInt8Ty(llvm_ctx), 0),
                alloc_size, llvm::MaybeAlign(1));

            // Store the element count in field 0
            llvm::Value* size_ptr = _builder->CreateStructGEP(struct_llvm, raw_ptr,
                array_type::FIELD_SIZE, "uarr_size");
            _builder->CreateStore(n_val, size_ptr);

            // Get pointer to the data area (field 1)
            llvm::Value* data_ptr = _builder->CreateStructGEP(struct_llvm, raw_ptr,
                array_type::FIELD_DATA, "uarr_data");

            // Emit IR loop: for (i = 0; i < n; ++i) init element i with ctor args
            auto* fn = _builder->GetInsertBlock()->getParent();
            auto* loop_header = llvm::BasicBlock::Create(llvm_ctx, "uarr_init_hdr", fn);
            auto* loop_body   = llvm::BasicBlock::Create(llvm_ctx, "uarr_init_body", fn);
            auto* loop_end    = llvm::BasicBlock::Create(llvm_ctx, "uarr_init_end", fn);

            _builder->CreateBr(loop_header);

            _builder->SetInsertPoint(loop_header);
            auto* entry_bb = loop_header->getSinglePredecessor();
            llvm::PHINode* i_phi = _builder->CreatePHI(i32_ty, 2, "uarr_i");
            i_phi->addIncoming(llvm::ConstantInt::get(i32_ty, 0), entry_bb);
            llvm::Value* cmp = _builder->CreateICmpULT(i_phi, n_val, "uarr_cmp");
            _builder->CreateCondBr(cmp, loop_body, loop_end);

            _builder->SetInsertPoint(loop_body);
            llvm::Value* indices[] = {llvm::ConstantInt::get(i32_ty, 0), i_phi};
            llvm::Value* elem_ptr = _builder->CreateGEP(
                llvm_arr_type, data_ptr, indices, "uarr_elem");

            if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
                auto ctor = expr.uniform_constructor();
                if (ctor) {
                    auto ctor_it = _context->_functions.find(ctor->shared_as<function>());
                    if (ctor_it != _context->_functions.end()) {
                        std::vector<llvm::Value*> args;
                        args.push_back(elem_ptr);
                        for (auto& arg : expr.uniform_ctor_args()) {
                            _value = nullptr;
                            arg->accept(*this);
                            if (_value) args.push_back(_value);
                        }
                        create_call_or_invoke(ctor_it->second->getFunctionType(), ctor_it->second, args, "");
                    }
                }
            } else if (type::is_primitive(elem_type)) {
                if (!expr.uniform_ctor_args().empty() && expr.uniform_ctor_args()[0]) {
                    _value = nullptr;
                    expr.uniform_ctor_args()[0]->accept(*this);
                    if (_value) _builder->CreateStore(_value, elem_ptr);
                }
            }

            llvm::Value* i_next = _builder->CreateAdd(
                i_phi, llvm::ConstantInt::get(i32_ty, 1), "uarr_i_next");
            i_phi->addIncoming(i_next, _builder->GetInsertBlock());
            _builder->CreateBr(loop_header);

            _builder->SetInsertPoint(loop_end);
            _value = raw_ptr;
            return;
        } else {
            // ── Static uniform array: new T(args)[N] ──
            size_t arr_size = expr.array_size();

            // Step 3: Cast raw pointer to the target type
            auto own_type = std::dynamic_pointer_cast<owner_type>(expr.get_type());
            auto sized_arr_type = own_type
                ? std::dynamic_pointer_cast<sized_array_type>(own_type->get_owned_type())
                : nullptr;
            if (!sized_arr_type) { _value = nullptr; return; }

            auto* struct_llvm = sized_arr_type->get_llvm_struct_type();
            if (!struct_llvm) { _value = nullptr; return; }

            // malloc(sizeof(struct { i32, [N x T] }))
            auto* size_val = llvm::ConstantInt::get(
                llvm::Type::getInt64Ty(llvm_ctx),
                mod.getDataLayout().getTypeAllocSize(struct_llvm));
            llvm::Value* raw_ptr = _builder->CreateCall(
                malloc_fn->getFunctionType(), malloc_fn, {size_val}, "new_uarr_raw");

            // Null-check: throw OutOfMemory if allocation failed
            emit_alloc_null_check(raw_ptr, "new_uarr_static");

            // Zero-init the entire struct
            auto* zero_init = llvm::ConstantAggregateZero::get(struct_llvm);
            _builder->CreateStore(zero_init, raw_ptr);

            // Store the element count in field 0
            llvm::Value* size_ptr2 = _builder->CreateStructGEP(struct_llvm, raw_ptr,
                sized_array_type::FIELD_SIZE, "uarr_size");
            _builder->CreateStore(
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx), arr_size, false),
                size_ptr2);

            // Get pointer to the data array (field 1)
            llvm::Value* data_ptr = _builder->CreateStructGEP(struct_llvm, raw_ptr,
                sized_array_type::FIELD_DATA, "uarr_data");
            auto* llvm_arr_type2 = sized_arr_type->get_llvm_data_array_type();

            // Initialize each element
            for (size_t i = 0; i < arr_size; ++i) {
                llvm::Value* elem_ptr = _builder->CreateConstInBoundsGEP2_32(
                    llvm_arr_type2, data_ptr, 0, i, "uarr_elem_" + std::to_string(i));

                if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
                    auto ctor = expr.uniform_constructor();
                    if (ctor) {
                        auto ctor_it = _context->_functions.find(ctor->shared_as<function>());
                        if (ctor_it != _context->_functions.end()) {
                            std::vector<llvm::Value*> args;
                            args.push_back(elem_ptr);
                            for (auto& arg : expr.uniform_ctor_args()) {
                                _value = nullptr;
                                arg->accept(*this);
                                if (_value) args.push_back(_value);
                            }
                            create_call_or_invoke(ctor_it->second->getFunctionType(), ctor_it->second, args, "");
                        }
                    }
                } else if (type::is_primitive(elem_type)) {
                    if (!expr.uniform_ctor_args().empty() && expr.uniform_ctor_args()[0]) {
                        _value = nullptr;
                        expr.uniform_ctor_args()[0]->accept(*this);
                        if (_value) _builder->CreateStore(_value, elem_ptr);
                    }
                }
            }

            _value = raw_ptr;
            return;
        }
    }

    if (expr.is_array()) {
        // ── Static array form: new T[N]{e0, e1, ...} ──
        size_t arr_size = expr.array_size();
        auto elem_type = alloc_type;

        // Get the sized_array_type from the expression type (owner<sized_array_type>)
        auto own_type = std::dynamic_pointer_cast<owner_type>(expr.get_type());
        auto sized_arr_type = own_type
            ? std::dynamic_pointer_cast<sized_array_type>(own_type->get_owned_type())
            : nullptr;
        if (!sized_arr_type) { _value = nullptr; return; }

        // Get LLVM types
        auto* struct_llvm = sized_arr_type->get_llvm_struct_type();
        if (!struct_llvm) { _value = nullptr; return; }
        llvm::Type* llvm_elem_type = _context->get_llvm_type(elem_type);

        // malloc(sizeof(struct { i32, [N x T] }))
        auto* size_val = llvm::ConstantInt::get(
            llvm::Type::getInt64Ty(llvm_ctx),
            mod.getDataLayout().getTypeAllocSize(struct_llvm));
        llvm::Value* raw_ptr = _builder->CreateCall(
            malloc_fn->getFunctionType(), malloc_fn, {size_val}, "new_arr_raw");

        // Null-check: throw OutOfMemory if allocation failed
        emit_alloc_null_check(raw_ptr, "new_arr");

        // Zero-init the entire struct
        auto* zero_init = llvm::ConstantAggregateZero::get(struct_llvm);
        _builder->CreateStore(zero_init, raw_ptr);

        // Store the element count in field 0
        llvm::Value* size_ptr = _builder->CreateStructGEP(struct_llvm, raw_ptr,
            sized_array_type::FIELD_SIZE, "arr_size");
        _builder->CreateStore(
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx), arr_size, false),
            size_ptr);

        // Get pointer to the data array (field 1)
        llvm::Value* data_ptr = _builder->CreateStructGEP(struct_llvm, raw_ptr,
            sized_array_type::FIELD_DATA, "arr_data");
        auto* llvm_arr_type = sized_arr_type->get_llvm_data_array_type();

        // Initialize each element
        size_t init_count = expr.array_init_elements().size();
        for (size_t i = 0; i < arr_size; ++i) {
            llvm::Value* elem_ptr = _builder->CreateConstInBoundsGEP2_32(
                llvm_arr_type, data_ptr, 0, i, "arr_elem_" + std::to_string(i));

            std::shared_ptr<expression> elem_expr = (i < init_count) ? expr.array_init_elements()[i] : nullptr;

            if (type::is_primitive(elem_type)) {
                if (elem_expr) {
                    _value = nullptr;
                    elem_expr->accept(*this);
                    if (_value) _builder->CreateStore(_value, elem_ptr);
                }
                // else: already zero-inited
            } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
                auto ctor = (i < expr.element_constructors().size())
                    ? expr.element_constructors()[i] : nullptr;
                if (ctor) {
                    auto ctor_it = _context->_functions.find(ctor->shared_as<function>());
                    if (ctor_it != _context->_functions.end()) {
                        std::vector<llvm::Value*> args;
                        args.push_back(elem_ptr); // 'this' pointer for the element

                        if (elem_expr) {
                            auto func_inv = std::dynamic_pointer_cast<function_invocation_expression>(elem_expr);
                            if (func_inv) {
                                // Explicit constructor call: pass all arguments
                                for (auto& arg : func_inv->arguments()) {
                                    _value = nullptr;
                                    arg->accept(*this);
                                    if (_value) args.push_back(_value);
                                }
                            } else {
                                // Implicit single-param constructor
                                _value = nullptr;
                                elem_expr->accept(*this);
                                if (_value) args.push_back(_value);
                            }
                        }
                        // else: default constructor (no extra args)

                        create_call_or_invoke(ctor_it->second->getFunctionType(), ctor_it->second, args, "");
                    }
                }
            }
        }

        _value = raw_ptr;
        return;
    }

    // ── Single-object form (unchanged) ──

    llvm::Type* llvm_type = _context->get_llvm_type(alloc_type);
    if (!llvm_type) { _value = nullptr; return; }

    auto* size_val = llvm::ConstantInt::get(
        llvm::Type::getInt64Ty(llvm_ctx),
        mod.getDataLayout().getTypeAllocSize(llvm_type));
    std::vector<llvm::Value*> malloc_args = {size_val};
    llvm::Value* raw_ptr = _builder->CreateCall(
        malloc_fn->getFunctionType(), malloc_fn, malloc_args, "new_raw");

    // Null-check: throw OutOfMemory if allocation failed
    emit_alloc_null_check(raw_ptr, "new_single");

    // Step 4: For structs: call the resolved constructor on the allocated object
    // Call constructor if struct
    if (auto st_type = std::dynamic_pointer_cast<struct_type>(alloc_type)) {
        auto ctor = expr.get_constructor();
        if (ctor) {
            auto ctor_it = _context->_functions.find(ctor->shared_as<function>());
            if (ctor_it != _context->_functions.end()) {
                std::vector<llvm::Value*> args;
                args.push_back(raw_ptr);
                for (auto& arg : expr.arguments()) {
                    _value = nullptr;
                    arg->accept(*this);
                    if (_value) args.push_back(_value);
                }
                create_call_or_invoke(ctor_it->second->getFunctionType(), ctor_it->second, args, "");
            }
        }
    } else if (type::is_primitive(alloc_type)) {
        // Store the argument value if provided
        if (!expr.arguments().empty()) {
            _value = nullptr;
            expr.arguments()[0]->accept(*this);
            if (_value) _builder->CreateStore(_value, raw_ptr);
        } else {
            // Zero-init
            _builder->CreateStore(llvm::Constant::getNullValue(llvm_type), raw_ptr);
        }
    }

    // Step 5: For arrays: zero-initialize or element-wise construct
    _value = raw_ptr;
}


void implementation_generator::visit_delete_expression(delete_expression& expr) {
    auto sub = expr.sub_expr();
    if (!sub) { _value = nullptr; return; }

    auto sub_type = sub->get_type();

    // Determine the owner type and the alloca holding it
    std::shared_ptr<owner_type> own_type;
    llvm::Value* owner_alloca = nullptr; // alloca of the owner variable (opaque ptr to owner slot)

    bool is_ref_to_owner = false;
    if (type::is_reference(sub_type)) {
        auto ref = std::dynamic_pointer_cast<reference_type>(sub_type);
        own_type = std::dynamic_pointer_cast<owner_type>(ref->get_subtype());
        is_ref_to_owner = (own_type != nullptr);
    } else {
        own_type = std::dynamic_pointer_cast<owner_type>(sub_type);
    }

    if (!own_type) { _value = nullptr; return; }
    auto alloc_type = own_type->get_owned_type();

    // Get the alloca (address of the owner slot)
    _value = nullptr;
    // We need the address (reference), not the value — so we evaluate the sub_expr
    // as an lvalue (address). For a symbol_expression this gives us the alloca directly.
    // Note: for both direct owner and reference-to-owner, accept() on a
    // symbol_expression produces the alloca (address of the owner slot).
    sub->accept(*this);
    owner_alloca = _value;

    if (!owner_alloca) { _value = nullptr; return; }

    emit_owner_cleanup_if_nonnull(_builder.get(), get_module(), _context->_functions,
        owner_alloca, alloc_type, "delete");
    _value = nullptr;
}


} // namespace k::model::gen
