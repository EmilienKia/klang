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

//
// Extracted helpers for type_reference_resolver::visit_variable_definition.
// Contains:
//   - var_init_context methods
//   - resolve_variable_type          (phase 1: type resolution)
//   - validate_*_variable helpers    (phase 2: per-type-category init validation)
//   - extract_indirection_subtype / check_and_insert_inheritance_cast utilities
//

#include "resolvers.hpp"

#include "../model/imported.hpp"
#include "../model/statements.hpp"
#include "../model/expressions.hpp"
#include "../parse/ast.hpp"

namespace k::model::gen {


// ── var_init_context methods ──────────────────────────────────────────────────

std::shared_ptr<expression>
type_reference_resolver::var_init_context::get_single_init_arg() const {
    if (init_expr && !init_expr->empty()) return init_expr->argument(0);
    if (!init_expr && init_expr_base) return init_expr_base;
    return nullptr;
}

bool type_reference_resolver::var_init_context::has_single_init_arg() const {
    return get_single_init_arg() != nullptr;
}

void type_reference_resolver::var_init_context::assign_single_init_arg(std::shared_ptr<expression> new_arg) {
    if (init_expr && !init_expr->empty()) {
        init_expr->assign_argument(0, new_arg);
    } else {
        // direct-stored: update in-place
        var.set_init_expr(new_arg);
    }
}


// ── Shared utility helpers ────────────────────────────────────────────────────

std::shared_ptr<type>
type_reference_resolver::extract_indirection_subtype(const std::shared_ptr<type>& arg_type) {
    auto effective = arg_type;
    if (auto ref_t = std::dynamic_pointer_cast<reference_type>(arg_type)) {
        effective = ref_t->get_subtype();
    }
    if (auto lnk_t = std::dynamic_pointer_cast<link_type>(effective)) {
        return lnk_t->get_linked_type();
    } else if (auto ptr_t = std::dynamic_pointer_cast<pointer_type>(effective)) {
        return ptr_t->get_pointed_type();
    } else if (auto view_t = std::dynamic_pointer_cast<view_type>(effective)) {
        return view_t->get_viewed_type();
    } else if (auto own_t = std::dynamic_pointer_cast<owner_type>(effective)) {
        return own_t->get_owned_type();
    } else if (auto ref_t2 = std::dynamic_pointer_cast<reference_type>(effective)) {
        return ref_t2->get_subtype();
    }
    return nullptr;
}

bool type_reference_resolver::check_and_insert_inheritance_cast(
    const std::shared_ptr<type>& src_sub_nc,
    const std::shared_ptr<type>& tgt_sub_nc,
    const std::shared_ptr<expression>& arg,
    const std::shared_ptr<type>& target_type,
    std::function<void(std::shared_ptr<expression>)> assign_arg,
    bool null_is_fatal)
{
    if (type::are_equal(src_sub_nc, tgt_sub_nc)) return true;

    auto src_st = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
    auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);

    // Static upcast: Derived -> Base
    if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
        src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
        auto upcast = cast_expression::make_shared(arg, target_type);
        upcast->set_type(target_type);
        assign_arg(upcast);
        return true;
    }

    // Dynamic downcast: Base -> Derived (requires RTTI)
    if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
        tgt_st->get_struct()->is_derived_from(src_st->get_struct()) &&
        tgt_st->get_struct()->has_rtti()) {
        auto dc = cast_expression::make_shared(arg, target_type, null_is_fatal);
        assign_arg(dc);
        return true;
    }

    return false;  // incompatible
}


// ── resolve_variable_type (phase 1) ──────────────────────────────────────────

void type_reference_resolver::resolve_variable_type(
    variable_definition& var, const lex::opt_any_lexeme& var_lexeme)
{
    if (type::is_resolved(var.get_type())) return;

    // Handle unresolved_function_ref_type (function pointer/pin/link type)
    if (auto ufrt = std::dynamic_pointer_cast<unresolved_function_ref_type>(var.get_type())) {
        const element* var_elem = dynamic_cast<const element*>(&var);
        std::shared_ptr<type> resolved;
        if (var_elem) {
            resolved = resolve_function_ref_type(ufrt, *var_elem);
        } else {
            // Fallback: no scope context, resolve without name lookup (free functions only)
            resolved = resolve_function_ref_type(ufrt, _unit);
        }
        if (resolved && type::is_resolved(resolved)) {
            var.set_type(resolved);
        } else {
            throw_internal_error(0x0002, var_lexeme,
                "Internal error: cannot resolve function reference type for variable '{}'",
                {var.get_fq_name()});
        }
        return;
    }

    // owner<UnresolvedType> — resolve the inner type then rebuild the owner wrapper
    if (auto own_type = std::dynamic_pointer_cast<owner_type>(var.get_type())) {
        auto inner = own_type->get_subtype();
        if (!type::is_resolved(inner)) {
            const element* var_elem = dynamic_cast<const element*>(&var);
            auto resolved_inner = resolve_inner_type(inner, var_elem);
            if (resolved_inner && type::is_resolved(resolved_inner)) {
                // Unsized arrays are canonicalised to ref<array<T>> by resolve_type,
                // but inside an owner we want owner(array(T)), not owner(ref<array<T>>).
                resolved_inner = strip_ref_array(resolved_inner);
                var.set_type(resolved_inner->get_owner());
            } else {
                throw_error(0x0005, var_lexeme,
                    "Unknown inner type for owner variable '{}': cannot resolve '{}'",
                    {var.get_fq_name(), inner ? inner->to_string() : "?"});
            }
        }
        return;
    }

    auto unres_type = std::dynamic_pointer_cast<unresolved_type>(var.get_type());
    if (!unres_type) {
        // The type is not resolved but is not a plain unresolved_type either.
        // It may be a pointer/link/pin/reference wrapping an unresolved inner type
        // (e.g. Point*, Point~, Point^).
        const element* var_elem = dynamic_cast<const element*>(&var);

        std::shared_ptr<type> resolved;
        if (type::is_pointer(var.get_type())) {
            auto inner = resolve_inner_type(var.get_type()->get_subtype(), var_elem);
            if (inner && type::is_resolved(inner)) resolved = strip_ref_array(inner)->get_pointer();
        } else if (type::is_link(var.get_type())) {
            auto inner = resolve_inner_type(var.get_type()->get_subtype(), var_elem);
            if (inner && type::is_resolved(inner)) resolved = strip_ref_array(inner)->get_link();
        } else if (type::is_view(var.get_type())) {
            auto inner = resolve_inner_type(var.get_type()->get_subtype(), var_elem);
            if (inner && type::is_resolved(inner)) resolved = strip_ref_array(inner)->get_view();
        } else if (type::is_reference(var.get_type())) {
            auto inner = resolve_inner_type(var.get_type()->get_subtype(), var_elem);
            if (inner && type::is_resolved(inner)) resolved = inner->get_reference();
        } else if (type::is_drain(var.get_type())) {
            auto inner = resolve_inner_type(var.get_type()->get_subtype(), var_elem);
            if (inner && type::is_resolved(inner)) resolved = inner->get_drain();
        } else {
            // Fallback: delegate to context->resolve_type
            resolved = _context->resolve_type(var.get_type());
        }

        if (resolved && type::is_resolved(resolved)) {
            var.set_type(resolved);
        } else {
            throw_internal_error(0x0001, var_lexeme,
                "Internal error: variable '{}' has an unresolvable type that is not an unresolved_type instance; "
                "this indicates a compiler bug",
                {var.get_fq_name()});
        }
        return;
    }

    // Plain unresolved_type: qualified name resolution
    std::shared_ptr<type> resolved;
    if (unres_type->type_id().has_root_prefix()) {
        resolved = resolve_type_from_root(unres_type->type_id().without_root_prefix());
    } else {
        // Walk up from unit root namespace for global variables
        auto root_ns = _unit.get_root_namespace();
        if (root_ns) {
            resolved = resolve_type_by_name(unres_type->type_id(), *root_ns);
        }
    }
    if (!resolved || !type::is_resolved(resolved)) {
        // Fall back to context->from_string (handles primitive types by string)
        resolved = _context->from_string(unres_type->type_id());
    }
    if (!resolved || !type::is_resolved(resolved)) {
        // Fall back to imported aggregates
        auto imported_agg = _unit.get_or_create_imported_aggregate(
            unres_type->type_id(), _context);
        if (imported_agg && imported_agg->get_struct_type()) {
            resolved = imported_agg->get_struct_type();
        }
    }
    if (!resolved || !type::is_resolved(resolved)) {
        // Fall back to imported enums
        auto imported_en = _unit.get_or_create_imported_enum(
            unres_type->type_id(), _context);
        if (imported_en && imported_en->get_enum_type()) {
            resolved = imported_en->get_enum_type();
        }
    }
    if (!resolved || !type::is_resolved(resolved)) {
        throw_error(0x0005, var_lexeme,
            "Unknown type '{}' for variable '{}': no type with this name could be found in scope",
            {unres_type->type_id().to_string(), var.get_fq_name()});
    } else {
        var.set_type(resolved);
    }
}


// ── Per-type-category validation helpers ──────────────────────────────────────

void type_reference_resolver::validate_primitive_variable(var_init_context& ctx) {
    if (!ctx.init_expr || ctx.init_expr->empty()) {
        // If no explicit initialization, let's have 0-filled initialization:
    } else if (ctx.init_expr->size() > 1) {
        throw_error(0x0006, ctx.var_lexeme,
            "Variable '{}' of primitive type '{}' can only be initialised with a single expression, "
            "but {} were provided",
            {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?",
             std::to_string(ctx.init_expr->size())});
    } else if (auto expr = ctx.init_expr->argument(0)) {
        // Align init expr type to variable type
        auto cast = adapt_type(expr, ctx.var_type);
        if (!cast) {
            // TODO throw_error(0x0004, ...)
        } else if (cast != expr) {
            // Casted, assign casted expression as return expr.
            ctx.init_expr->assign_argument(0, cast);
        } else {
            // Compatible type, no need to cast.
        }
    } else {
        throw_internal_error(0x0002, ctx.var_lexeme,
            "Variable '{}' of primitive type '{}' has an empty initialisation expression list; "
            "this is an internal inconsistency",
            {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?"});
    }
}


void type_reference_resolver::validate_struct_variable(var_init_context& ctx) {
    auto st_type = std::dynamic_pointer_cast<struct_type>(ctx.var.get_type());

    // Check for designated struct init first
    auto desig_init = std::dynamic_pointer_cast<designated_struct_init_expression>(ctx.init_expr_base);
    if (desig_init) {
        // Designated struct init — resolve handled in visit_designated_struct_init_expression
        desig_init->accept(*this);
        return;
    }

    // Structure, try to find the right constructor
    auto struct_model = st_type->get_struct();
    std::vector<std::shared_ptr<expression>> ctor_args =
        ctx.init_expr ? ctx.init_expr->arguments() : std::vector<std::shared_ptr<expression>>{};

    // For non-static inner structs: the constructor's first parameter is __parent__.
    // If we are inside a method of the direct enclosing struct, auto-prepend 'this'.
    if (struct_model && struct_model->is_inner()) {
        auto outer_struct = struct_model->get_enclosing_structure();
        auto var_elem = dynamic_cast<const element*>(&ctx.var);
        bool in_outer_method = false;
        if (var_elem) {
            auto enclosing_func = var_elem->ancestor<function>();
            if (enclosing_func && enclosing_func->is_member() && !enclosing_func->is_static()) {
                auto owner_st = enclosing_func->get_owner();
                if (owner_st == outer_struct) {
                    in_outer_method = true;
                }
            }
        }
        if (in_outer_method) {
            auto this_sym = symbol_expression::from_identifier(k::name("this"));
            auto func_elem = dynamic_cast<const element*>(&ctx.var);
            if (func_elem) {
                auto enclosing_func = func_elem->ancestor<function>();
                if (enclosing_func && enclosing_func->get_this_parameter()) {
                    this_sym->set_target(std::const_pointer_cast<parameter>(enclosing_func->get_this_parameter()));
                    this_sym->set_type(enclosing_func->get_this_parameter()->get_type());
                }
            }
            bool needs_inject = true;
            if (!struct_model->constructors().empty()) {
                size_t n_params = struct_model->constructors()[0]->parameters().size();
                if (ctor_args.size() == n_params) needs_inject = false;
            }
            if (needs_inject) {
                ctor_args.insert(ctor_args.begin(), this_sym);
                if (ctx.init_expr) {
                    ctx.init_expr->arguments(ctor_args);
                }
            }
        }
    }

    // Direct struct copy: if single arg has the same struct type (by value or by ref),
    // allow direct aggregate copy without a constructor.
    bool handled_as_direct_copy = false;
    if (ctor_args.size() == 1) {
        auto arg_type = ctor_args[0]->get_type();
        auto arg_type_nc = type::remove_const(arg_type);
        bool is_direct_copy = false;
        // Check bare struct type (rvalue from function return)
        if (arg_type_nc == st_type) {
            is_direct_copy = true;
        }
        // Check ref<struct> (lvalue variable)
        if (!is_direct_copy && type::is_reference(arg_type_nc)) {
            auto ref_sub = type::remove_const(std::dynamic_pointer_cast<reference_type>(arg_type_nc)->get_subtype());
            if (ref_sub == st_type) {
                is_direct_copy = true;
            }
        }
        if (is_direct_copy) {
            // Direct copy: null constructor signals aggregate store in impl_gen
            ctx.var.set_var_constructor(nullptr);
            if (ctx.init_expr) {
                ctx.init_expr->set_constructor(nullptr);
                ctx.init_expr->arguments(ctor_args);
            }
            handled_as_direct_copy = true;
        }
    }

    if (!handled_as_direct_copy) {
        auto [best_constructor, adapted_args] = get_best_matching_constructor(struct_model->constructors(), ctor_args);
        if (!best_constructor) {
            throw_error(0x0008, ctx.var_lexeme,
                "No matching constructor found for variable '{}' of type '{}': "
                "none of the available constructors can be called with the provided arguments",
                {ctx.var.get_fq_name(), st_type->to_string()});
        } else {
            // Check constructor visibility from the variable's declaration site
            if (auto var_elem = dynamic_cast<const element*>(&ctx.var)) {
                check_constructor_visibility(*best_constructor, *var_elem);
            }
            ctx.var.set_var_constructor(best_constructor);
            if (ctx.init_expr) {
                ctx.init_expr->set_constructor(best_constructor);
                ctx.init_expr->arguments(adapted_args);
            }
        }
    }
}


void type_reference_resolver::validate_reference_variable(var_init_context& ctx) {
    auto ref_var_type = std::dynamic_pointer_cast<reference_type>(ctx.var.get_type());
    auto ref_sub = ref_var_type->get_subtype();

    // ------------------------------------------------------------------
    // Case A: ref to sized array, i.e.  int[N]&
    // ------------------------------------------------------------------
    if (type::is_sized_array(ref_sub)) {
        auto dest_arr = std::dynamic_pointer_cast<sized_array_type>(ref_sub);
        if (!ctx.init_expr || ctx.init_expr->empty()) {
            throw_error(0x4101, ctx.var_lexeme,
                "Array reference variable '{}' of type '{}' must be initialised at its declaration; "
                "an array reference cannot be left unbound",
                {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?"});
            return;
        }
        if (ctx.init_expr->size() > 1) {
            throw_error(0x4102, ctx.var_lexeme,
                "Array reference variable '{}' of type '{}' must be initialised with exactly one "
                "expression, but {} were provided",
                {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?",
                 std::to_string(ctx.init_expr->size())});
            return;
        }
        auto arg = ctx.init_expr->argument(0);
        auto arg_type = arg ? arg->get_type() : nullptr;
        // Initialiser must be a reference to a sized array of the same element type.
        if (!arg_type || !type::is_reference(arg_type)) {
            throw_error(0x4104, ctx.var_lexeme,
                "Array reference variable '{}' of type '{}' must be initialised with an array "
                "reference (lvalue), but the initialiser has type '{}' which is not a reference",
                {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?",
                 arg_type ? arg_type->to_string() : "?"});
            return;
        }
        auto arg_ref = std::dynamic_pointer_cast<reference_type>(arg_type);
        auto arg_sub = arg_ref->get_subtype();
        auto src_arr = std::dynamic_pointer_cast<sized_array_type>(arg_sub);
        if (!type::is_sized_array(arg_sub)) {
            throw_error(0x4105, ctx.var_lexeme,
                "Array reference variable '{}' of type '{}' can only be initialised from another "
                "array reference, but the initialiser refers to type '{}' which is not a sized array",
                {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?",
                 arg_sub ? arg_sub->to_string() : "?"});
            return;
        }
        // Element types must match exactly.
        if (!type::are_equal(dest_arr->get_subtype(), src_arr->get_subtype())) {
            throw_error(0x4106, ctx.var_lexeme,
                "Array reference variable '{}' of type '{}' cannot be initialised from an array of "
                "type '{}': element types must match exactly",
                {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?",
                 arg_type ? arg_type->to_string() : "?"});
            return;
        }
        // Validation OK
        return;
    }

    // ------------------------------------------------------------------
    // Case B: plain reference (non-array), e.g.  int&
    // ------------------------------------------------------------------

    // 1. Initialization is mandatory
    if (!ctx.init_expr || ctx.init_expr->empty()) {
        throw_error(0x4001, ctx.var_lexeme,
            "Reference variable '{}' of type '{}' must be initialised at its declaration: "
            "a reference is an alias for an existing object and cannot be left unbound",
            {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?"});
        return;
    }

    // 2. Only one initializer expression is allowed
    if (ctx.init_expr->size() > 1) {
        throw_error(0x4002, ctx.var_lexeme,
            "Reference variable '{}' of type '{}' must be initialised with exactly one expression, "
            "but {} were provided",
            {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?",
             std::to_string(ctx.init_expr->size())});
        return;
    }

    auto arg = ctx.init_expr->argument(0);
    if (!arg) {
        throw_internal_error(0x4003, ctx.var_lexeme,
            "Reference variable '{}': initialisation argument is null; "
            "this is an internal compiler inconsistency",
            {ctx.var.get_fq_name()});
        return;
    }

    auto arg_type = arg->get_type();

    // 3. Initialization must be a reference (lvalue), not a bare value
    if (!type::is_reference(arg_type)) {
        throw_error(0x4004, ctx.var_lexeme,
            "Reference variable '{}' of type '{}' must be initialised with a reference (an addressable "
            "object), but the initialiser has type '{}' which is not a reference; "
            "you cannot bind a reference to a temporary or rvalue",
            {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?",
             arg_type ? arg_type->to_string() : "?"});
        return;
    }

    // 4. Type compatibility check
    auto arg_ref = std::dynamic_pointer_cast<reference_type>(arg_type);
    auto arg_sub = arg_ref ? arg_ref->get_subtype() : nullptr;
    auto var_sub = ref_var_type->get_subtype();

    if (!arg_sub || !var_sub) {
        throw_error(0x4005, ctx.var_lexeme,
            "Reference variable '{}' of type '{}' cannot be bound to an expression of type '{}': "
            "the referenced type must match exactly",
            {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?",
             arg_type ? arg_type->to_string() : "?"});
        return;
    }

    if (!type::are_equal(arg_sub, var_sub)) {
        bool ok = check_and_insert_inheritance_cast(
            arg_sub, var_sub, arg, ctx.var_type,
            [&](std::shared_ptr<expression> e) { ctx.init_expr->assign_argument(0, e); },
            /*null_is_fatal=*/true);
        if (!ok) {
            throw_error(0x4005, ctx.var_lexeme,
                "Reference variable '{}' of type '{}' cannot be bound to an expression of type '{}': "
                "the referenced type must match exactly",
                {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?",
                 arg_type ? arg_type->to_string() : "?"});
            return;
        }
    }
}


void type_reference_resolver::validate_pointer_variable(var_init_context& ctx) {
    // Pointer variable (*): validate const-compatibility of initializer and type compatibility.
    if (!ctx.has_single_init_arg()) return;
    auto arg = ctx.get_single_init_arg();
    if (!arg) return;

    // Null literal is always compatible with any pointer type — skip type checks.
    bool is_null_init = type::is_null(arg->get_type());
    if (!is_null_init) {
        if (auto ve = std::dynamic_pointer_cast<value_expression>(arg)) {
            is_null_init = std::holds_alternative<std::nullptr_t>(ve->get_value())
                           || (ve->is_literal() && std::holds_alternative<lex::null>(ve->any_literal()));
        }
    }
    if (is_null_init) return;

    auto arg_type = arg->get_type();
    auto effective_arg = arg_type;
    if (type::is_reference(arg_type)) {
        effective_arg = std::dynamic_pointer_cast<reference_type>(arg_type)->get_subtype();
    }
    // Also accept owner as source
    if (auto own_t = std::dynamic_pointer_cast<owner_type>(effective_arg)) {
        effective_arg = own_t->get_owned_type()->get_pointer();
    }
    auto tgt_ptr = std::dynamic_pointer_cast<pointer_type>(ctx.var.get_type());
    std::shared_ptr<type> src_sub;
    if (auto src_ptr = std::dynamic_pointer_cast<pointer_type>(effective_arg)) {
        src_sub = src_ptr->get_subtype();
    } else if (auto src_lnk = std::dynamic_pointer_cast<link_type>(effective_arg)) {
        src_sub = src_lnk->get_linked_type();
    } else if (auto src_view = std::dynamic_pointer_cast<view_type>(effective_arg)) {
        src_sub = src_view->get_viewed_type();
    } else if (auto src_own = std::dynamic_pointer_cast<owner_type>(effective_arg)) {
        src_sub = src_own->get_owned_type();
    }
    if (!tgt_ptr || !src_sub) return;

    auto tgt_sub = tgt_ptr->get_subtype();
    if (type::is_const(src_sub) && !type::is_const(tgt_sub)) {
        throw_error(0x0081, ctx.var_lexeme,
            "Cannot initialise a pointer-to-mutable ('{}') from a pointer-to-const ('{}'): "
            "this would allow modification of a const object through the mutable pointer",
            {ctx.var.get_type()->to_string(), arg_type ? arg_type->to_string() : "?"});
    }
    auto src_sub_nc = type::remove_const(src_sub);
    auto tgt_sub_nc = type::remove_const(tgt_sub);
    if (!type::are_equal(src_sub_nc, tgt_sub_nc)) {
        bool ok = check_and_insert_inheritance_cast(
            src_sub_nc, tgt_sub_nc, arg, ctx.var.get_type(),
            [&](std::shared_ptr<expression> e) { ctx.assign_single_init_arg(e); },
            /*null_is_fatal=*/false);
        if (!ok) {
            throw_error(0x4700, ctx.var_lexeme,
                "Pointer variable '{}' of type '{}' cannot be initialised from an expression of type '{}': "
                "the pointed types are incompatible (no inheritance relationship)",
                {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?",
                 arg_type ? arg_type->to_string() : "?"});
            return;
        }
    }
}


void type_reference_resolver::validate_link_variable(var_init_context& ctx) {
    // Link variable (~): validate const-compatibility of initializer (for rebind semantics).
    auto link_var_type = std::dynamic_pointer_cast<link_type>(ctx.var.get_type());

    if (!ctx.has_single_init_arg()) {
        throw_error(0x4501, ctx.var_lexeme,
            "Link variable '{}' of type '{}' must be initialised at its declaration: "
            "a link is non-null and cannot be left unbound",
            {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?"});
        return;
    }
    auto arg = ctx.get_single_init_arg();
    if (!arg) {
        throw_internal_error(0x4503, ctx.var_lexeme,
            "Link variable '{}': initialisation argument is null; "
            "this is an internal compiler inconsistency",
            {ctx.var.get_fq_name()});
        return;
    }
    auto arg_type = arg->get_type();
    // The initialiser must provide an address: reference, link, view, pointer or owner.
    if (!type::is_any_indirection(arg_type) && !type::is_owner(arg_type)) {
        throw_error(0x4504, ctx.var_lexeme,
            "Link variable '{}' of type '{}' must be initialised with an addressable expression "
            "(reference, link, view, pointer or owner), but the initialiser has type '{}' "
            "which is not an indirection type",
            {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?",
             arg_type ? arg_type->to_string() : "?"});
        return;
    }
    // Const-compatibility: link-to-mutable cannot be init from link/pointer/ref-to-const
    {
        auto link_sub = link_var_type->get_linked_type();
        std::shared_ptr<type> src_pointed_type = extract_indirection_subtype(arg_type);
        if (!src_pointed_type && type::is_const(arg_type)) {
            src_pointed_type = arg_type;
        }
        if (src_pointed_type && type::is_const(src_pointed_type) && !type::is_const(link_sub)) {
            throw_error(0x0082, ctx.var_lexeme,
                "Cannot initialise link-to-mutable ('{}') from a const source (type '{}'): "
                "this would allow modification of a const object",
                {ctx.var.get_type()->to_string(), arg_type ? arg_type->to_string() : "?"});
        }
    }
    // If initialising from a nullable indirection (view, pointer or owner), emit a warning:
    if (type::is_nullable_indirection(arg_type) || type::is_owner(arg_type)) {
        auto diag = k::log::diagnostic::make_warning(with_flag(0x4505),
            "Link variable '{}' of type '{}' is being initialised from a nullable source "
            "(type '{}'): a runtime null-check will be inserted",
            {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?",
             arg_type ? arg_type->to_string() : "?"});
        logger_relay::report(diag);
    }
    // Type compatibility
    {
        auto link_sub_nc = type::remove_const(link_var_type->get_linked_type());
        auto src_pointed_nc = extract_indirection_subtype(arg_type);
        if (src_pointed_nc) src_pointed_nc = type::remove_const(src_pointed_nc);

        if (src_pointed_nc && !type::are_equal(src_pointed_nc, link_sub_nc)) {
            bool ok = check_and_insert_inheritance_cast(
                src_pointed_nc, link_sub_nc, arg, ctx.var_type,
                [&](std::shared_ptr<expression> e) { ctx.assign_single_init_arg(e); },
                /*null_is_fatal=*/true);
            if (!ok) {
                throw_error(0x4506, ctx.var_lexeme,
                    "Link variable '{}' of type '{}' cannot be bound to an expression of type '{}': "
                    "the linked types are incompatible (no inheritance relationship)",
                    {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?",
                     arg_type ? arg_type->to_string() : "?"});
                return;
            }
        }
    }
}


void type_reference_resolver::validate_view_variable(var_init_context& ctx) {
    // Pinned variable (^): immutable (not rebindable after init), nullable.
    // Must be initialised at declaration; initialiser can be any indirection, owner or null.
    if (!ctx.has_single_init_arg()) {
        throw_error(0x4601, ctx.var_lexeme,
            "Pinned variable '{}' of type '{}' must be initialised at its declaration: "
            "a view indirection cannot be left unbound",
            {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?"});
        return;
    }
    auto arg = ctx.get_single_init_arg();
    if (!arg) {
        throw_internal_error(0x4603, ctx.var_lexeme,
            "Pinned variable '{}': initialisation argument is null; "
            "this is an internal compiler inconsistency",
            {ctx.var.get_fq_name()});
        return;
    }
    auto arg_type = arg->get_type();
    bool is_null_init = type::is_null(arg_type);
    if (!is_null_init) {
        if (auto ve = std::dynamic_pointer_cast<value_expression>(arg)) {
            is_null_init = std::holds_alternative<std::nullptr_t>(ve->get_value());
        }
    }
    if (!is_null_init && !type::is_any_indirection(arg_type) && !type::is_owner(arg_type)) {
        throw_error(0x4604, ctx.var_lexeme,
            "View variable '{}' of type '{}' must be initialised with an addressable expression "
            "(reference, link, view, pointer, owner or null), but the initialiser has type '{}' "
            "which is not an indirection type",
            {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?",
             arg_type ? arg_type->to_string() : "?"});
        return;
    }
    if (!is_null_init && (type::is_any_indirection(arg_type) || type::is_owner(arg_type))) {
        auto view_var_type = std::dynamic_pointer_cast<view_type>(ctx.var.get_type());
        auto view_sub_nc = type::remove_const(view_var_type->get_viewed_type());

        auto src_pointed_nc = extract_indirection_subtype(arg_type);
        if (src_pointed_nc) src_pointed_nc = type::remove_const(src_pointed_nc);

        if (src_pointed_nc && !type::are_equal(src_pointed_nc, view_sub_nc)) {
            bool ok = check_and_insert_inheritance_cast(
                src_pointed_nc, view_sub_nc, arg, ctx.var_type,
                [&](std::shared_ptr<expression> e) { ctx.assign_single_init_arg(e); },
                /*null_is_fatal=*/false);
            if (!ok) {
                throw_error(0x4605, ctx.var_lexeme,
                    "View variable '{}' of type '{}' cannot be bound to an expression of type '{}': "
                    "the view types are incompatible (no inheritance relationship)",
                    {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?",
                     arg_type ? arg_type->to_string() : "?"});
                return;
            }
        }
    }
}


void type_reference_resolver::validate_owner_variable(var_init_context& ctx) {
    // Owner variable (!): owns a heap-allocated object.
    if (!ctx.has_single_init_arg()) return;
    auto arg = ctx.get_single_init_arg();
    if (!arg) return;

    auto arg_type = arg->get_type();
    // Accept: new_expression (owner<T>), null literal, or ref<owner<T>> / owner<compatible_T>
    bool is_null_init = type::is_null(arg_type);
    if (!is_null_init) {
        if (auto ve = std::dynamic_pointer_cast<value_expression>(arg)) {
            is_null_init = std::holds_alternative<std::nullptr_t>(ve->get_value());
        }
    }
    if (is_null_init) return;

    // Unwrap ref<owner<T>> to owner<T> for type checks
    auto effective_arg_type = arg_type;
    bool is_ref_owner = false;
    if (type::is_reference(arg_type)) {
        auto inner = std::dynamic_pointer_cast<reference_type>(arg_type)->get_subtype();
        if (type::is_owner(inner)) {
            effective_arg_type = inner;
            is_ref_owner = true;
        }
    }
    if (!type::is_owner(effective_arg_type)) {
        throw_error(0x4802, ctx.var_lexeme,
            "Owner variable '{}' of type '{}' must be initialised with a 'new' expression, "
            "another owner variable, or null, but the initialiser has type '{}' which is not "
            "an owner type",
            {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?",
             arg_type ? arg_type->to_string() : "?"});
        return;
    }
    // Check owned-type compatibility
    auto own_var = std::dynamic_pointer_cast<owner_type>(ctx.var_type);
    auto own_arg = std::dynamic_pointer_cast<owner_type>(effective_arg_type);
    if (own_var && own_arg) {
        auto var_sub = type::remove_const(own_var->get_owned_type());
        auto arg_sub = type::remove_const(own_arg->get_owned_type());
        if (!type::are_equal(var_sub, arg_sub)) {
            // Allow static upcast for polymorphic types
            auto src_st = std::dynamic_pointer_cast<struct_type>(arg_sub);
            auto tgt_st = std::dynamic_pointer_cast<struct_type>(var_sub);
            bool is_upcast = src_st && tgt_st &&
                src_st->get_struct() && tgt_st->get_struct() &&
                src_st->get_struct()->is_derived_from(tgt_st->get_struct());
            if (!is_upcast) {
                throw_error(0x4803, ctx.var_lexeme,
                    "Owner variable '{}' of type '{}' cannot be initialised from "
                    "an owner of incompatible type '{}'",
                    {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?",
                     arg_type ? arg_type->to_string() : "?"});
                return;
            }
        }
    }
    // If the source is ref<owner<T>>, wrap it in owner_move_expression
    // (load + null source = transfer ownership)
    if (is_ref_owner) {
        auto move = owner_move_expression::make_shared(arg);
        move->set_type(effective_arg_type);
        ctx.assign_single_init_arg(move);
    }
}


void type_reference_resolver::validate_sized_array_variable(var_init_context& ctx) {
    // Sized array variable: int[N]
    auto arr_init = std::dynamic_pointer_cast<array_init_expression>(ctx.init_expr_base);
    if (arr_init) {
        // Array brace init — resolve handled in visit_array_init_expression
        arr_init->accept(*this);
    } else if (ctx.init_expr && !ctx.init_expr->empty()) {
        // Non-brace-init explicit initializer is not supported
        throw_error(0x4201, ctx.var_lexeme,
            "Array variable '{}' of type '{}' cannot have an explicit initialiser at declaration; "
            "use brace initialization syntax: arr : T[N] {{elem1, elem2, ...}}",
            {ctx.var.get_fq_name(), ctx.var_type ? ctx.var_type->to_string() : "?"});
        return;
    }
    // No initializer = zero-init (always valid for any element type).
}


} // namespace k::model::gen

