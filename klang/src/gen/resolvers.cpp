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
// Note: Last resolver log number: 0x30004
//

#include "resolvers.hpp"

namespace k::model::gen {


//
// Exceptions
//
resolution_error::resolution_error(const std::string &arg) :
        runtime_error(arg)
{}

resolution_error::resolution_error(const char *string) :
        runtime_error(string)
{}

//
// Symbol resolver
//

void symbol_resolver::resolve()
{
    visit_unit(_unit);
}

std::variant<std::monostate, std::shared_ptr<variable_definition>, std::shared_ptr<function>> // TODO Add traversal direction flag
symbol_resolver::resolve_symbol(const element& elem, const name& name) {

    // Specifically look at the "this" symbol (non-static function specific parameter)
    if (name.size() == 1 && name.to_string() == "this") {
        auto func = elem.ancestor<function>();
        while (func) {
            if (func->is_member() && func->get_this_parameter()) {
                return std::const_pointer_cast<parameter>(func->get_this_parameter());
            }
            func = func->ancestor<function>();
        }
        // TODO throw exception Symbol 'this' used outside a non-static member function.
        std::clog << "Symbol 'this' used outside a non-static member function." << std::endl;
        return std::monostate{};
    }

    if (name.has_root_prefix()) {
        // TODO if name has root prefix, look at the unit directly.
        std::clog << "Try to resolve symbol with root prefix: " << name.to_string() << std::endl;
    } else if (name.empty()) {
        // Invalid name, must have at least one part
        return std::monostate{};
    } else if(name.size() > 1) {
        // Qualified name

        // Look at structures
        if (auto st_holder = dynamic_cast<const structure_holder*>(&elem)) {
            if (auto st = st_holder->get_structure(name.front())) {
                if (auto res = resolve_symbol(*st, name.without_front()); res.index()!=0) {
                    return res;
                }
            }
        }

        // Look at namespace
        if (auto nspc = dynamic_cast<const ns*>(&elem)) {
            if (auto child = nspc->get_child_namespace(name.front())) {
                if (auto res = resolve_symbol(*child, name.without_front()); res.index()!=0) {
                    return res;
                }
            }
        }

    } else /*(name.size() == 1)*/ {
        // Simple name, try to resolve it directly

        // Look at a variable
        if (auto var_holder = dynamic_cast<const variable_holder*>(&elem)) {
            if (auto def = var_holder->get_variable(name)) {
                return def;
            }
        }

        // Look at a function
        if (auto func_holder = dynamic_cast<const function_holder*>(&elem)) {
            if (auto func = func_holder->lookup_function(name.to_string())) {
                return func;
            }
        }

        // TODO: Workaround, remove it when function will be a (parameter) variable_holder
        if (auto blck = dynamic_cast<const block*>(&elem)) {
            if (auto func = blck->get_direct_function()) {
                if (auto param = func->get_parameter(name.to_string())) {
                    return std::const_pointer_cast<parameter>(param);
                }
            }
        }
    }

    if (auto parent_elem = elem.parent<element>()) {
        // Try to find the symbol in the parent element context
        return resolve_symbol(*parent_elem, name);
    } else {
        // No parent element, cannot resolve symbol here
        return std::monostate{};
    }
}

std::shared_ptr<expression> symbol_resolver::adapt_reference_load_value(const std::shared_ptr<expression>& expr) {
    auto type = expr->get_type();

    if(!expr || !type::is_resolved(type)) {
        // Arguments must not be null, expr must have a type and this must be resolved.
        return nullptr;
    }

    if(type::is_reference(type)) {
        auto deref = load_value_expression::make_shared(expr);
        deref->set_type(type->get_subtype());
        return deref;
    } else {
        return expr;
    }
}


std::shared_ptr<expression> symbol_resolver::adapt_type(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type) {
    if(!expr || !type::is_resolved(type) || !type::is_resolved(expr->get_type())) {
        // Arguments must not be null, expr must have a type and types (expr and target) must be resolved.
        return nullptr;
    }

    auto type_src = expr->get_type();

    if(type::is_pointer(type_src)) {
        if(type::is_pointer(type)) {
            if (type == type_src) {
                // Pointers to same type, return the expression
                return expr;
            } else {
                // Pointers to different types
                // TODO verify casting
                return {};
            }
        } else {
            // Error : Source is a pointer, and asked to be cast to an object.
            return {};
        }
    }

    if(type::is_double_reference(type_src)) {
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        auto deref = load_value_expression::make_shared(expr);
        deref->set_type(ref_src->get_subtype());
        expr = deref;
        type_src = ref_src->get_subtype();
    }

    if(type::is_reference(type_src)) {
        if(type::is_reference(type)) {
            if (type == type_src) {
                // Reference to same type, return the expression
                return expr;
            } else {
                // Reference to different types
                // TODO verify casting
                return {};
            }
        }
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        if(ref_src->get_subtype() == type) {
            return adapt_reference_load_value(expr);
        }
    }

    auto prim_src = std::dynamic_pointer_cast<primitive_type>(expr->get_type());
    auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(type);

    if(!prim_src || !prim_tgt) {
        // Support only primitive types for now.
        // TODO support not-primitive type casting
        return {};
    }

    if(prim_src && prim_tgt && *prim_src==*prim_tgt) {
        // Trivially agree for same types
        return expr;
    }

    auto cast = cast_expression::make_shared(expr, prim_tgt);
    cast->set_type(prim_tgt);
    return cast;
}

//
// Type resolver
//

void type_reference_resolver::resolve()
{
    visit_unit(_unit);
}

void type_reference_resolver::visit_variable_definition(variable_definition& var)
{
    if(!type::is_resolved(var.get_type())) {
        auto unres_type = std::dynamic_pointer_cast<unresolved_type>(var.get_type());
        if(!unres_type) {
            // TODO throw an exception
            std::cerr << "Error: global variable definition has an unresolvable type." << std::endl;
        }
        auto type = _context->from_string(unres_type->type_id());
        if(!type || !type::is_resolved(type)) {
            // TODO throw an exception
            std::cerr << "Error: global variable definition has an unresolvable type." << std::endl;
        } else {
            var.set_type(type);
        }
    }

    // Resolve init expressions if any
    auto init_expr = var.get_init_expr();
    if (init_expr) {
        init_expr->accept(*this);
    }

    auto var_type = var.get_type();

    if (type::is_primitive(var_type)) {
        // Primitive type supports only one init expression, always try to cast it, if any.
        if (!init_expr || init_expr->empty()) {
            // If no explicit initialization, let's have 0-filled initialization:
        } else if (init_expr->size() > 1) {
            // TODO throw an exception
            std::cerr << "Error: global variable of primitive type can only have one initialization expression" << std::endl;
        } else if (auto expr = init_expr->argument(0)) {

            // Align init expr type to variable type
            auto cast = adapt_type(expr, var_type);
            if(!cast) {
                // TODO throw_error(0x0004, var.get_ast_for_stmt()->for_kw, "For test expression type must be convertible to bool");
            } else if(cast != expr) {
                // Casted, assign casted expression as return expr.
                init_expr->assign_argument(0, cast);
            } else {
                // Compatible type, no need to cast.
            }

        } else {
            // TODO throw an exception
            std::cerr << "Error: global variable of primitive type has empty initialization expression" << std::endl;
        }

    } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(var.get_type())) {
        // Structure, try to find the right constructor
        auto [best_constructor, adapted_args] = get_best_matching_constructor(st_type->get_struct()->constructors(), init_expr ? init_expr->arguments() : std::vector<std::shared_ptr<expression>>{});
        if (!best_constructor) {
            // TODO throw an exception
            std::cerr << "Error: no matching constructor found for global variable initialization" << std::endl;
        }
        var.set_var_constructor(best_constructor);
        if (init_expr) {
            init_expr->set_constructor(best_constructor);
            init_expr->arguments(adapted_args);
        }

    } else {
        // Unsupported construction for other types for now
        // TODO Support construction for other types (ref, array, etc.)
    }
}

type_reference_resolver::cast_weight
type_reference_resolver::compute_cast_weight(const std::shared_ptr<expression>& expr, const std::shared_ptr<k::model::type>& tgt) {
    if (!expr || !type::is_resolved(tgt) || !type::is_resolved(expr->get_type())) {
        return CAST_IMPOSSIBLE;
    }

    auto type_src = expr->get_type();

    // --- Pointer cases ---
    if (type::is_pointer(type_src)) {
        if (type::is_pointer(tgt)) {
            // Pointed types must be identical for now, no pointer conversions supported yet.
            return (type_src == tgt) ? CAST_NONE : CAST_IMPOSSIBLE;
        }
        return CAST_IMPOSSIBLE;
    }

    // --- Double reference: unwrap one level ---
    std::shared_ptr<k::model::type> effective_src = type_src;
    if (type::is_double_reference(type_src)) {
        effective_src = std::dynamic_pointer_cast<reference_type>(type_src)->get_subtype();
    }

    // --- Reference cases ---
    if (type::is_reference(effective_src)) {
        auto ref_src = std::dynamic_pointer_cast<reference_type>(effective_src);
        if (type::is_reference(tgt)) {
            return (effective_src == tgt) ? CAST_NONE : CAST_IMPOSSIBLE;
        }
        // ref -> value: need a load
        auto sub = ref_src->get_subtype();
        if (sub == tgt) {
            return CAST_REF_CONV;
        }
        // ref -> different primitive: load + cast
        auto prim_sub = std::dynamic_pointer_cast<primitive_type>(sub);
        auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(tgt);
        if (prim_sub && prim_tgt) {
            if (*prim_sub == *prim_tgt) return CAST_REF_CONV;
            // Widening: same category, target is larger or same
            if (prim_sub->is_integer() && prim_tgt->is_integer() &&
                prim_sub->is_unsigned() == prim_tgt->is_unsigned() &&
                prim_tgt->type_size() >= prim_sub->type_size()) {
                return CAST_WIDENING;
            }
            if (prim_sub->is_float() && prim_tgt->is_float() &&
                prim_tgt->type_size() >= prim_sub->type_size()) {
                return CAST_WIDENING;
            }
            // Everything else between primitives is narrowing
            return CAST_NARROWING;
        }
        return CAST_IMPOSSIBLE;
    }

    // --- Both primitive ---
    auto prim_src = std::dynamic_pointer_cast<primitive_type>(effective_src);
    auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(tgt);
    if (prim_src && prim_tgt) {
        if (*prim_src == *prim_tgt) return CAST_NONE;
        // Widening: integers same signedness, target wider or equal; or float widening
        if (prim_src->is_integer() && prim_tgt->is_integer() &&
            prim_src->is_unsigned() == prim_tgt->is_unsigned() &&
            prim_tgt->type_size() >= prim_src->type_size()) {
            return CAST_WIDENING;
        }
        if (prim_src->is_float() && prim_tgt->is_float() &&
            prim_tgt->type_size() >= prim_src->type_size()) {
            return CAST_WIDENING;
        }
        // All other primitive conversions (int<->float, narrowing int, bool<->int, etc.)
        return CAST_NARROWING;
    }

    // --- Struct construction via single-arg constructor ---
    if (auto st_tgt = std::dynamic_pointer_cast<struct_type>(tgt)) {
        auto st = st_tgt->get_struct();
        if (st) {
            for (auto& ctor : st->constructors()) {
                if (ctor->parameters().size() == 1) {
                    auto param_type = ctor->parameters()[0]->get_type();
                    // Recursively check if expr can be passed to the constructor parameter
                    auto sub_weight = compute_cast_weight(expr, param_type);
                    if (sub_weight != CAST_IMPOSSIBLE) {
                        return CAST_CONSTRUCT;
                    }
                }
            }
        }
    }

    return CAST_IMPOSSIBLE;
}

std::pair<std::shared_ptr<constructor>/*best_constructor*/, std::vector<std::shared_ptr<expression>>/*adapted_args*/>
type_reference_resolver::get_best_matching_constructor(const std::vector<std::shared_ptr<constructor>>& constructors, const std::vector<std::shared_ptr<expression>>& args) {
    const size_t arg_count = args.size();

    // --- Step 1: filter by arity ---
    std::vector<std::shared_ptr<constructor>> arity_matched;
    for (auto& ctor : constructors) {
        if (ctor->parameters().size() == arg_count) {
            arity_matched.push_back(ctor);
        }
    }

    if (arity_matched.empty()) {
        std::cerr << "Error: no constructor found with " << arg_count << " argument(s).";
        if (!constructors.empty()) {
            std::cerr << " Available constructors have " ;
            bool first = true;
            for (auto& ctor : constructors) {
                if (!first) std::cerr << ", ";
                std::cerr << ctor->parameters().size() << " parameter(s)";
                first = false;
            }
        }
        std::cerr << "." << std::endl;
        return {nullptr, {}};
    }

    // --- Step 2: compute per-candidate score (max of per-param weights) ---
    struct Candidate {
        std::shared_ptr<constructor> ctor;
        std::vector<std::shared_ptr<expression>> adapted_args;
        cast_weight score; // worst (max) cast weight across all parameters
    };

    // Candidates that fail because of at least one IMPOSSIBLE cast
    struct FailedCandidate {
        std::shared_ptr<constructor> ctor;
        std::vector<size_t> failed_param_indices; // 0-based indices with IMPOSSIBLE weight
    };

    std::vector<Candidate> valid_candidates;
    std::vector<FailedCandidate> failed_candidates;

    for (auto& ctor : arity_matched) {
        cast_weight max_weight = CAST_NONE;
        bool has_impossible = false;
        std::vector<size_t> failed_indices;
        std::vector<std::shared_ptr<expression>> adapted_args;

        for (size_t i = 0; i < arg_count; ++i) {
            auto param_type = ctor->parameters()[i]->get_type();
            cast_weight w = compute_cast_weight(args[i], param_type);
            if (w == CAST_IMPOSSIBLE) {
                has_impossible = true;
                failed_indices.push_back(i);
            } else {
                if (w > max_weight) max_weight = w;
                auto adapted = adapt_type(args[i], param_type);
                adapted_args.push_back(adapted ? adapted : args[i]);
            }
        }

        if (has_impossible) {
            failed_candidates.push_back({ctor, std::move(failed_indices)});
        } else {
            valid_candidates.push_back({ctor, std::move(adapted_args), max_weight});
        }
    }

    // --- Step 3: no valid candidates ---
    if (valid_candidates.empty()) {
        std::cerr << "Error: no viable constructor found (impossible implicit cast(s)). Candidates:" << std::endl;
        for (auto& fc : failed_candidates) {
            std::cerr << "  constructor(";
            bool first = true;
            for (auto& param : fc.ctor->parameters()) {
                if (!first) std::cerr << ", ";
                if (auto pt = param->get_type()) std::cerr << pt->to_string();
                else std::cerr << "?";
                first = false;
            }
            std::cerr << ") — impossible cast for argument index(es):";
            for (size_t idx : fc.failed_param_indices) std::cerr << " " << idx;
            std::cerr << std::endl;
        }
        return {nullptr, {}};
    }

    // --- Step 4: find minimum score ---
    cast_weight best_score = CAST_IMPOSSIBLE;
    for (auto& cand : valid_candidates) {
        if (cand.score < best_score) best_score = cand.score;
    }

    // Perfect match short-circuit (score == CAST_NONE)
    if (best_score == CAST_NONE) {
        for (auto& cand : valid_candidates) {
            if (cand.score == CAST_NONE) {
                return {cand.ctor, cand.adapted_args};
            }
        }
    }

    // --- Step 5: collect all candidates with best score ---
    std::vector<Candidate*> best_candidates;
    for (auto& cand : valid_candidates) {
        if (cand.score == best_score) {
            best_candidates.push_back(&cand);
        }
    }

    // --- Step 6: ambiguity check ---
    if (best_candidates.size() > 1) {
        std::cerr << "Error: ambiguous constructor call (cast score=" << best_score << "). Equally viable candidates:" << std::endl;
        for (auto* cand : best_candidates) {
            std::cerr << "  constructor(";
            bool first = true;
            for (auto& param : cand->ctor->parameters()) {
                if (!first) std::cerr << ", ";
                if (auto pt = param->get_type()) std::cerr << pt->to_string();
                else std::cerr << "?";
                first = false;
            }
            std::cerr << ")" << std::endl;
        }
        return {nullptr, {}};
    }

    // --- Step 7: unique best candidate ---
    return {best_candidates[0]->ctor, best_candidates[0]->adapted_args};
}


std::shared_ptr<expression> type_reference_resolver::adapt_reference_load_value(const std::shared_ptr<expression>& expr) {
    auto type = expr->get_type();

    if(!expr || !type::is_resolved(type)) {
        // Arguments must not be null, expr must have a type and this must be resolved.
        return nullptr;
    }

    if(type::is_reference(type)) {
        auto deref = load_value_expression::make_shared(expr);
        deref->set_type(type->get_subtype());
        return deref;
    } else {
        return expr;
    }
}

std::shared_ptr<expression> type_reference_resolver::adapt_type(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type) {
    if(!expr || !type::is_resolved(type) || !type::is_resolved(expr->get_type())) {
        // Arguments must not be null, expr must have a type and types (expr and target) must be resolved.
        return nullptr;
    }

    auto type_src = expr->get_type();

    if(type::is_pointer(type_src)) {
        if(type::is_pointer(type)) {
            if (type == type_src) {
                // Pointers to same type, return the expression
                return expr;
            } else {
                // Pointers to different types
                // TODO verify casting
                return {};
            }
        } else {
            // Error : Source is a pointer, and asked to be cast to an object.
            return {};
        }
    }

    if(type::is_double_reference(type_src)) {
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        auto deref = load_value_expression::make_shared(expr);
        deref->set_type(ref_src->get_subtype());
        expr = deref;
        type_src = ref_src->get_subtype();
    }

    if(type::is_reference(type_src)) {
        if(type::is_reference(type)) {
            if (type == type_src) {
                // Reference to same type, return the expression
                return expr;
            } else {
                // Reference to different types
                // TODO verify casting
                return {};
            }
        }
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        auto ref_subtype = ref_src->get_subtype();
        if(ref_subtype == type) {
            // ref<T> -> T : simple load
            return adapt_reference_load_value(expr);
        }
        // ref<primA> -> primB : load first, then cast between primitives
        auto prim_sub = std::dynamic_pointer_cast<primitive_type>(ref_subtype);
        auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(type);
        if (prim_sub && prim_tgt) {
            // Load the reference to get the value
            auto loaded = adapt_reference_load_value(expr);
            if (!loaded) return {};
            if (*prim_sub == *prim_tgt) return loaded;
            // Cast the loaded value to the target primitive type
            auto cast = cast_expression::make_shared(loaded, prim_tgt);
            cast->set_type(prim_tgt);
            return cast;
        }
        return {};
    }

    auto prim_src = std::dynamic_pointer_cast<primitive_type>(expr->get_type());
    auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(type);

    if(!prim_src || !prim_tgt) {
        // Support only primitive types for now.
        // TODO support not-primitive type casting
        return {};
    }

    if(*prim_src==*prim_tgt) {
        // Trivially agree for same types
        return expr;
    }

    auto cast = cast_expression::make_shared(expr, prim_tgt);
    cast->set_type(prim_tgt);
    return cast;
}



} // k::model::gen
