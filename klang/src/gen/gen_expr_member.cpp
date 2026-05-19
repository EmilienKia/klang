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
// member_of_object, member_of_pointer, pm, subscript

void symbol_resolver::visit_member_of_expression(member_of_expression& expr) {
    // Explicitly only resolve sub expression.
    // Symbol can only be resolved afterward, cause it will depend on the type of subexpression.
    visit_unary_expression(expr);
}

/**
 * Resolve a member-of-object expression (expr.member): field access, method lookup,
 * unified call syntax, or enum member access.
 *
 * Steps:
 *   1. Resolve the sub-expression (LHS) and determine its struct type.
 *   2. Look up the member name in the struct (fields, methods, inherited members).
 *   3. For fields: set type as reference to field type, record field index.
 *   4. For methods: record the function target for later call resolution.
 *   5. For unified-call syntax: check free functions with first param = ref to struct.
 *   6. For enum members: resolve enum entry on an enum-typed sub-expression.
 */
void type_reference_resolver::visit_member_of_object_expression(member_of_object_expression& expr) {
    expr.sub_expr()->accept(*this);
    auto type = expr.sub_expr()->get_type();

    // Handle vbptr path: sub_expr was cast to pointer<VirtualBase> by the type resolver (this visit)
    // on a previous recursive call. Accept pointer type and resolve the member type.
    if (type::is_pointer(type)) {
        auto ptr_type = std::dynamic_pointer_cast<pointer_type>(type);
        auto bare_subtype = type::remove_const(ptr_type->get_pointed_type());
        if (auto struct_subtype = std::dynamic_pointer_cast<struct_type>(bare_subtype)) {
            const auto& member_name = expr.symbol();
            const std::string& name_str = member_name.get_name().to_string();
            if (auto field = struct_subtype->get_member(name_str)) {
                auto field_type = field->field_type.lock();
                expr.set_type(field_type->get_reference());
            }
            // For method: leave type unset, handled by function_invocation_expression
        }
        return;
    }

    if(!type::is_reference(type) && !type::is_drain(type) && !type::is_owner(type)) {
        // ── Bare struct rvalue (e.g. function return value) ──────────────────
        // Allow member access on struct-typed rvalues (temporaries).
        // The implementation generator will materialize the temporary into an alloca.
        if (type::is_struct(type)) {
            auto bare_subtype = type::remove_const(type);
            bool is_const_access = type::is_const(type);
            if (auto struct_subtype = std::dynamic_pointer_cast<struct_type>(bare_subtype)) {
                const auto& member_name = expr.symbol();
                const k::name& sym_name = member_name.get_name();
                const std::string& name_str = sym_name.to_string();
                std::string simple_name = sym_name.size() > 1 ? sym_name.back() : name_str;

                // Look up the member in the struct type (field or method)
                if (auto field = struct_subtype->get_member(simple_name)) {
                    auto field_type = field->field_type.lock();
                    if (is_const_access) {
                        field_type = type::remove_const(field_type)->get_const();
                    }
                    expr.set_type(field_type->get_reference());
                }
                // For method: leave type unset, handled by function_invocation_expression
            }
            return;
        }
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_MEMBER_NOT_FOUND_ON_OBJECT), expr.first_lexeme(),
            "Cannot access a member on a non-reference expression: "
            "the '.' operator requires the left-hand side to be a reference to a struct, "
            "but the left-hand side has type '{}'",
            {type ? type->to_string() : "?"});
    }
    auto subtype = type->get_subtype();
    // Detect if we are accessing through a const reference (ref<const S> or ref<S> where S is const struct)
    bool is_const_access = type::is_const(subtype);
    // Strip const to get the actual struct_type for member lookup
    auto bare_subtype = type::remove_const(subtype);

    // A variable of type owner<T> is used through a symbol expression of type
    // ref<owner<T>>. For member access with '.', unwrap the intermediate owner
    // layer so lookup runs against T itself.
    if (auto owner_subtype = std::dynamic_pointer_cast<owner_type>(bare_subtype)) {
        bare_subtype = type::remove_const(owner_subtype->get_subtype());
    }

    // ── Handle unsized-array fields: T[] is canonically ref<array<T>>, so a
    //    const T[] field appears as const(ref(array(T))). After unwrapping the
    //    outer ref + const, bare_subtype may be another ref<array<T>>.
    //    Unwrap that inner reference to reach the array_type for virtual member checks.
    if (auto inner_ref = std::dynamic_pointer_cast<reference_type>(bare_subtype)) {
        auto inner_sub = type::remove_const(inner_ref->get_subtype());
        if (std::dynamic_pointer_cast<array_type>(inner_sub)) {
            bare_subtype = inner_sub;
        }
    }

    // ── Virtual member: array.size ──────────────────────────────────────────
    // Arrays (sized or unsized) expose a virtual read-only member "size"
    // that returns the element count (unsigned int, stored in LLVM struct field 0).
    if (auto arr_subtype = std::dynamic_pointer_cast<array_type>(bare_subtype)) {
        const std::string& name_str = expr.symbol().get_name().to_string();
        if (name_str == "size") {
            // "size" is an unsigned int value (not a reference — it is loaded, not addressable)
            expr.set_type(_context->from_type(primitive_type::UNSIGNED_INT));
            return;
        }
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_MEMBER_NOT_FOUND_ON_TYPE), expr.first_lexeme(),
            "Arrays have no member named '{}'; only 'size' is available",
            {name_str});
    }

    if(auto struct_subtype = std::dynamic_pointer_cast<struct_type>(bare_subtype)) {
        const auto& member_name = expr.symbol();
        const k::name& sym_name = member_name.get_name();
        const std::string& name_str = sym_name.to_string();

        // ── Union alternative access ──
        // If the struct_type has no owning aggregate, it's a union type.
        // Look up the alternative by name and set the expression type to a reference
        // to that alternative's type.
        if (!struct_subtype->get_struct()) {
            // Find the union_type_def for this struct_type
            std::shared_ptr<union_type_def> union_def;
            auto root_ns = _unit.get_root_namespace();
            if (root_ns) {
                for (auto& [uname, udef] : root_ns->unions()) {
                    if (udef->get_struct_type() == struct_subtype) {
                        union_def = udef;
                        break;
                    }
                }
            }
            if (!union_def) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_MEMBER_NOT_FOUND_ON_TYPE), expr.first_lexeme(),
                    "Cannot resolve union type for member access '{}'",
                    {name_str});
                return;
            }
            auto* alt = union_def->get_alternative_by_name(name_str);
            if (!alt) {
                throw_error(static_cast<unsigned int>(k::diag::union_diag::ERR_UNION_MEMBER_NOT_FOUND), expr.first_lexeme(),
                    "No alternative named '{}' in union '{}'",
                    {name_str, union_def->get_short_name()});
                return;
            }
            // The member access result type is a reference to the alternative's type
            // (const reference if the alternative is const-qualified)
            auto result_type = alt->resolved_type;
            if (alt->is_const) {
                result_type = result_type->get_const();
            }
            expr.set_type(result_type->get_reference());
            return;
        }

        // ── Detect qualified member access (e.g. d.A::v where sym_name has parts ["A", "v"]) ──
        // If the name has more than one part, the last part is the member name and
        // the preceding parts form the qualifier (base class name).
        bool is_qualified = sym_name.size() > 1;
        std::string simple_name = is_qualified ? sym_name.back() : name_str;
        std::string qualifier_name;
        if (is_qualified) {
            // Build qualifier from all parts except the last
            for (size_t i = 0; i + 1 < sym_name.size(); ++i) {
                if (i > 0) qualifier_name += "::";
                qualifier_name += sym_name[i];
            }
        }

        // ── Helper: search a struct and its bases for a named field or function,
        //    returning (struct_type*, field) or (struct_type*, nullptr=function).
        //    Returns empty vector if not found, multiple items if ambiguous.
        //    'via_virtual' is true when the path to the member crossed at least one virtual link.
        struct MemberHit {
            std::shared_ptr<struct_type> in_struct_type;
            std::optional<struct_type::field> field;
            bool is_function = false;
            bool via_virtual = false; ///< true if the path to this member crosses a virtual base link
        };

        // Track visited structs to deduplicate diamond paths.
        // When traversing via a virtual base path, all transitively-visited structs
        // are deduplicated (even non-virtual ones), because the virtual base already
        // ensures a single shared copy.
        std::unordered_set<const aggregate*> visited_virtual_bases;

        std::function<std::vector<MemberHit>(const std::shared_ptr<struct_type>&, const std::string&, visibility, bool, bool)> search_member;
        search_member = [&](const std::shared_ptr<struct_type>& stype, const std::string& mname,
                             visibility inherit_vis, bool /*top_level*/, bool via_virt) -> std::vector<MemberHit> {
            std::vector<MemberHit> hits;
            // Check direct field
            if (auto field = stype->get_member(mname)) {
                hits.push_back({stype, field, false, via_virt});
                return hits;
            }
            // Check direct method
            if (auto st = stype->get_struct()) {
                if (st->get_function(mname)) {
                    hits.push_back({stype, std::nullopt, true, via_virt});
                    return hits;
                }
                // Search bases
                for (auto& bs : st->get_bases()) {
                    if (!bs.base || !bs.base->get_struct_type()) continue;
                    visibility eff_vis = (inherit_vis == PRIVATE || bs.vis == PRIVATE) ? PRIVATE :
                                         (inherit_vis == PROTECTED || bs.vis == PROTECTED) ? PROTECTED :
                                         PUBLIC;
                    bool next_via_virt = via_virt || bs.is_virtual;
                    // Deduplicate: if this base is virtual (explicitly), or if we're already
                    // traversing through a virtual-base path, track visited structs to avoid
                    // finding the same member multiple times (diamond disambiguation).
                    if (bs.is_virtual || via_virt) {
                        if (visited_virtual_bases.count(bs.base.get())) continue;
                        visited_virtual_bases.insert(bs.base.get());
                    }
                    auto sub_hits = search_member(bs.base->get_struct_type(), mname, eff_vis, false, next_via_virt);
                    hits.insert(hits.end(), sub_hits.begin(), sub_hits.end());
                }
            }
            return hits;
        };

        auto hits = search_member(struct_subtype, simple_name, PUBLIC, true, false);
        if (hits.size() > 1) {
            std::cerr << "[DEBUG] Ambiguous: " << hits.size() << " hits for '" << simple_name << "' in '" << struct_subtype->name() << "'\n" << std::flush;
        }

        // If qualified, filter hits to only those in the named base
        if (is_qualified && !hits.empty()) {
            std::vector<MemberHit> filtered;
            for (auto& h : hits) {
                auto h_agg = h.in_struct_type->get_struct();
                if (h_agg && h_agg->get_short_name() == qualifier_name) {
                    filtered.push_back(h);
                }
            }
            if (filtered.empty()) {
                // If this is a function callee (e.g. this.Base::method()), defer
                // to function_invocation_expression which handles qualified calls.
                auto parent_expr = expr.get_parent_expression();
                if (auto parent_invoc = std::dynamic_pointer_cast<function_invocation_expression>(parent_expr)) {
                    if (parent_invoc->callee_expr().get() == &expr) {
                        return; // defer to function_invocation_expression
                    }
                }
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_MEMBER_NOT_FOUND_ON_TYPE), expr.first_lexeme(),
                    "No member named '{}' in struct '{}' or any of its bases",
                    {name_str, struct_subtype->name()});
                return;
            }
            hits = std::move(filtered);
        }

        if (hits.empty()) {
            // If this member_of_object_expression is the callee of a function_invocation_expression,
            // the name may be a free function callable via unified-call syntax (e.g. pt.sum() where
            // sum(p: point&) is a free function). Let visit_function_invocation_expression handle it.
            auto parent_expr = expr.get_parent_expression();
            bool is_function_callee = false;
            if (auto parent_invoc = std::dynamic_pointer_cast<function_invocation_expression>(parent_expr)) {
                is_function_callee = (parent_invoc->callee_expr().get() == &expr);
            }
            if (is_function_callee) return; // defer to function_invocation_expression
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_MEMBER_NOT_FOUND_ON_TYPE), expr.first_lexeme(),
                "No member named '{}' in struct '{}' or any of its bases",
                {name_str, struct_subtype->name()});
        }

        if (hits.size() > 1) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_MEMBER_DEREF_NOT_POINTER), expr.first_lexeme(),
                "Ambiguous access to member '{}' in struct '{}': "
                "the member is found in multiple base classes; use Base::member to disambiguate",
                {name_str, struct_subtype->name()});
        }

        auto& hit = hits[0];

        // Check visibility of the accessed member
        if (auto st_model = hit.in_struct_type->get_struct()) {
            auto mv = st_model->get_variable(simple_name);
            if (auto member_var = std::dynamic_pointer_cast<member_variable_definition>(mv)) {
                auto vis = member_var->get_visibility();
                if (vis != PUBLIC) {
                    if (!scope_lookup::is_struct_member_accessible(vis, *st_model, st_model, _function_stack)) {
                        if (vis != PROTECTED || !scope_lookup::is_friend_of(*st_model, _function_stack, _unit)) {
                            throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_CTOR_VISIBILITY_MISMATCH), expr.first_lexeme(),
                                "{} member variable '{}' of struct '{}' is not accessible here; "
                                "it can only be accessed from member functions of '{}'{}",
                                {vis == PROTECTED ? "protected" : "private",
                                 member_var->get_short_name(), st_model->get_short_name(), st_model->get_short_name(),
                                 vis == PROTECTED ? " or its subclasses or friends" : ""});
                        }
                    }
                }
            }
        }

        if (!hit.is_function && hit.field.has_value()) {
            auto field_type = hit.field->field_type.lock();
            // If accessing through a const reference, the field is also const.
            if (is_const_access) {
                field_type = type::remove_const(field_type)->get_const();
            }
            expr.set_type(field_type->get_reference());
            // If the field is in a base, wrap sub_expr in a cast_expression to base ref
            // so that implementation_generator will compute the correct GEP offset.
            if (hit.in_struct_type != struct_subtype) {
                if (hit.via_virtual) {
                    // Virtual base access: find the direct path to the virtual base.
                    // The most-derived class has a __vbase_X__ embedded sub-object;
                    // an intermediate class has a __vbptr_X__ pointer field.
                    auto vbase_st = hit.in_struct_type->get_struct();
                    std::string vbase_short_name = vbase_st ? vbase_st->get_short_name() : "";
                    std::string vbase_field_name = "__vbase_" + vbase_short_name + "__";
                    std::string vbptr_field_name = "__vbptr_" + vbase_short_name + "__";

                    if (struct_subtype->get_member(vbase_field_name)) {
                        // Most-derived class: has the actual __vbase_X__ embedded sub-object.
                        // Upcast to that sub-object via GEP (no load needed).
                        auto base_ref_type = is_const_access
                            ? hit.in_struct_type->get_const()->get_reference()
                            : hit.in_struct_type->get_reference();
                        auto upcast = cast_expression::make_shared(expr.sub_expr(), base_ref_type);
                        upcast->set_type(base_ref_type);
                        expr.sub_expr() = upcast;
                    } else if (struct_subtype->get_member(vbptr_field_name)) {
                        // Intermediate class: has a __vbptr_X__ pointer.
                        // Use ref<A> (not A*) — the cast implementation will load the vbptr.
                        auto base_ref_type = is_const_access
                            ? hit.in_struct_type->get_const()->get_reference()
                            : hit.in_struct_type->get_reference();
                        auto vbptr_cast = cast_expression::make_shared(expr.sub_expr(), base_ref_type);
                        vbptr_cast->set_type(base_ref_type);
                        expr.sub_expr() = vbptr_cast;
                    } else {
                        // Fallback: the member is in a base X that is not directly a virtual base
                        // of struct_subtype, but is reachable through a virtual base path.
                        // Example: D->B(virtual)->A(non-virtual): x is in A, path is D.__vbase_B__.__base_A__.x
                        // Find one direct base of D that contains or leads to hit.in_struct_type.
                        bool found = false;
                        std::string base_A_name = "__base_" + vbase_short_name + "__";
                        for (auto& bs2 : struct_subtype->get_struct()->get_bases()) {
                            if (!bs2.base) continue;
                            auto base_sub = bs2.base->get_struct_type();
                            if (!base_sub) continue;
                            bool this_base_has_path = base_sub->get_member(vbase_field_name)
                                || base_sub->get_member(vbptr_field_name)
                                || base_sub->get_member(base_A_name);
                            if (!this_base_has_path) continue;
                            // Found a direct base of D that can reach hit.in_struct_type
                            // Build the cast: D → (via vbptr/vbase) bs2.base → (via __base_X__) hit.in_struct_type
                            auto inter_ref_type = is_const_access
                                ? base_sub->get_const()->get_reference()
                                : base_sub->get_reference();
                            auto inter_upcast = cast_expression::make_shared(expr.sub_expr(), inter_ref_type);
                            inter_upcast->set_type(inter_ref_type);
                            auto vbase_ref_type = is_const_access
                                ? hit.in_struct_type->get_const()->get_reference()
                                : hit.in_struct_type->get_reference();
                            auto vbase_upcast = cast_expression::make_shared(inter_upcast, vbase_ref_type);
                            vbase_upcast->set_type(vbase_ref_type);
                            expr.sub_expr() = vbase_upcast;
                            found = true;
                            break;
                        }
                        if (!found) {
                            // Simple fallback: treat as regular cast
                            auto base_ref_type = is_const_access
                                ? hit.in_struct_type->get_const()->get_reference()
                                : hit.in_struct_type->get_reference();
                            auto upcast = cast_expression::make_shared(expr.sub_expr(), base_ref_type);
                            upcast->set_type(base_ref_type);
                            expr.sub_expr() = upcast;
                        }
                    }
                } else {
                    // Non-virtual base: normal GEP-based upcast
                    auto base_ref_type = is_const_access
                        ? hit.in_struct_type->get_const()->get_reference()
                        : hit.in_struct_type->get_reference();
                    auto upcast = cast_expression::make_shared(expr.sub_expr(), base_ref_type);
                    upcast->set_type(base_ref_type);
                    expr.sub_expr() = upcast;
                }
            }
        } else if (hit.is_function) {
            // Member function: update the sub_expr type to point to the struct that owns the method.
            // This is needed so that implementation_generator finds the method in the correct struct,
            // and so that the 'this' pointer is correctly adjusted via upcast if needed.
            if (hit.in_struct_type != struct_subtype) {
                if (hit.via_virtual) {
                    // Virtual base: function is in a virtual base. Same path-finding as field access.
                    auto vbase_st = hit.in_struct_type->get_struct();
                    std::string vbase_short_name = vbase_st ? vbase_st->get_short_name() : "";
                    std::string vbase_field_name = "__vbase_" + vbase_short_name + "__";
                    std::string vbptr_field_name = "__vbptr_" + vbase_short_name + "__";

                    // Step 1: Resolve the sub-expression (LHS) and determine its struct type
                    // Step 2: Look up the member name in the struct (fields, methods, inherited members)
                    if (struct_subtype->get_member(vbase_field_name)) {
                        auto base_ref_type = is_const_access
                            ? hit.in_struct_type->get_const()->get_reference()
                            : hit.in_struct_type->get_reference();
                        auto upcast = cast_expression::make_shared(expr.sub_expr(), base_ref_type);
                        upcast->set_type(base_ref_type);
                        expr.sub_expr() = upcast;
                    } else if (struct_subtype->get_member(vbptr_field_name)) {
                        // Intermediate class has __vbptr_X__: load vbptr.
                        // Use ref<A> type (not A*) so function invocations accept it as reference.
                        auto base_ref_type = is_const_access
                            ? hit.in_struct_type->get_const()->get_reference()
                            : hit.in_struct_type->get_reference();
                        auto vbptr_cast = cast_expression::make_shared(expr.sub_expr(), base_ref_type);
                        vbptr_cast->set_type(base_ref_type);
                        expr.sub_expr() = vbptr_cast;
                    } else {
                        // Fallback: member function in a transitively reachable base
                        // (e.g., D->B(virtual)->A(non-virtual): method is in A)
                        std::string base_A_name2 = "__base_" + vbase_short_name + "__";
                        bool found2 = false;
                        for (auto& bs : struct_subtype->get_struct()->get_bases()) {
                            if (!bs.base) continue;
                            auto base_sub = bs.base->get_struct_type();
                            if (!base_sub) continue;
                            if (base_sub->get_member(vbase_field_name) || base_sub->get_member(vbptr_field_name) || base_sub->get_member(base_A_name2)) {
                                auto inter_ref_type = is_const_access
                                    ? base_sub->get_const()->get_reference()
                                    : base_sub->get_reference();
                                auto inter_upcast = cast_expression::make_shared(expr.sub_expr(), inter_ref_type);
                                inter_upcast->set_type(inter_ref_type);
                                auto vbase_ref_type = is_const_access
                                    ? hit.in_struct_type->get_const()->get_reference()
                                    : hit.in_struct_type->get_reference();
                                auto vbase_upcast = cast_expression::make_shared(inter_upcast, vbase_ref_type);
                                vbase_upcast->set_type(vbase_ref_type);
                                expr.sub_expr() = vbase_upcast;
                                found2 = true;
                                break;
                            }
                        }
                        if (!found2) {
                            // Simple fallback
                            auto base_ref_type = is_const_access
                                ? hit.in_struct_type->get_const()->get_reference()
                                : hit.in_struct_type->get_reference();
                            auto upcast = cast_expression::make_shared(expr.sub_expr(), base_ref_type);
                            upcast->set_type(base_ref_type);
                            expr.sub_expr() = upcast;
                        }
                    }
                } else {
                    auto base_ref_type = is_const_access
                        ? hit.in_struct_type->get_const()->get_reference()
                        : hit.in_struct_type->get_reference();
                    auto upcast = cast_expression::make_shared(expr.sub_expr(), base_ref_type);
                    upcast->set_type(base_ref_type);
                    expr.sub_expr() = upcast;
                }
            }
            // Step 5: For unified-call syntax: check free functions with first param = ref to struct
            // Type of member_of_object_expression for functions is the struct ref (for 'this')
            // — leave expr type unset; function_invocation_expression will handle it.
        }
    } else {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_MEMBER_ACCESS_ON_RVALUE), expr.first_lexeme(),
            "The '.' operator can only be applied to a reference to a struct type, "
            "but the left-hand side is a reference to '{}' which is not a struct",
            {bare_subtype ? bare_subtype->to_string() : "?"});
    }
}

//
// Member of object expression
// If the member is a field, return the address of the field within the struct.
// If the member is a function, return the address of the object onto which the function will be called (the future this ref).
//
/**
 * Generate LLVM IR for member-of-object expression (field GEP, method address).
 *
 * Steps:
 *   1. Visit sub-expression to get the object value.
 *   2. If LHS is a reference: unwrap to get the object pointer.
 *   3. GEP to the member field using the recorded field index.
 *   4. Handle nested struct-in-struct member chains.
 */
void implementation_generator::visit_member_of_object_expression(member_of_object_expression& expr) {
    _value = nullptr;
    expr.sub_expr()->accept(*this);

    auto type = expr.sub_expr()->get_type(); // Is a reference or (for vbptr path) a pointer

    // Step 1: Visit sub-expression to get the object value
    // Handle vbptr path: sub_expr was cast to pointer<VirtualBase> by the type resolver
    if (type::is_pointer(type)) {
        auto ptr_type = std::dynamic_pointer_cast<pointer_type>(type);
        auto bare_subtype = type::remove_const(ptr_type->get_pointed_type());
        if (auto struct_subtype = std::dynamic_pointer_cast<struct_type>(bare_subtype)) {
            const auto& member_name = expr.symbol();
            if (auto field = struct_subtype->get_member(member_name.get_name()); field) {
                _value = _builder->CreateStructGEP(bare_subtype->get_llvm_type(), _value, field->index);
            }
            // For method calls: leave _value as the A* pointer (treated as A ref at LLVM level)
        }
        return;
    }

    // ── Bare struct rvalue (e.g. function return materialized into alloca) ──
    // _value is already a pointer (alloca) from struct return materialization.
    if (type::is_struct(type)) {
        auto bare_subtype = type::remove_const(type);
        if (auto struct_subtype = std::dynamic_pointer_cast<struct_type>(bare_subtype)) {
            const auto& member_name = expr.symbol();
            const k::name& sym_name = member_name.get_name();
            std::string simple_name = sym_name.size() > 1 ? sym_name.back() : sym_name.to_string();
            if (auto field = struct_subtype->get_member(simple_name); field) {
                _value = _builder->CreateStructGEP(bare_subtype->get_llvm_type(), _value, field->index);
            } else if (auto method = struct_subtype->get_struct()->get_function(simple_name)) {
                // Leave _value as the struct alloca pointer (will be used as 'this')
            } else {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F024), expr.first_lexeme(),
                    "Internal error: struct '{}' has no member named '{}' during code generation; "
                    "the model is inconsistent — type resolution should have caught this earlier",
                    {struct_subtype->name(), simple_name});
            }
        }
        return;
    }

    // Strip const from the subtype to get the bare struct_type for GEP/method lookup.
    auto bare_subtype = type::remove_const(type->get_subtype());

    // Member access on an owner variable comes through a ref<owner<T>> symbol.
    // Load the owned object pointer from the owner slot, then continue as if we
    // were operating on T directly.
    if (auto owner_subtype = std::dynamic_pointer_cast<owner_type>(bare_subtype)) {
        _value = _builder->CreateLoad(
            llvm::PointerType::get(_builder->getContext(), 0), _value, "owner_obj_load");
        bare_subtype = type::remove_const(owner_subtype->get_subtype());
    }

    // Step 2: If LHS is a reference: unwrap to get the object pointer
    // ── Handle unsized-array fields: const T[] = const(ref(array(T))).
    //    After stripping const, bare_subtype may be ref<array<T>>.
    //    Unwrap the inner reference (with an extra load) to reach the array struct.
    bool arr_needs_inner_ref_load = false;
    if (auto inner_ref = std::dynamic_pointer_cast<reference_type>(bare_subtype)) {
        auto inner_sub = type::remove_const(inner_ref->get_subtype());
        if (std::dynamic_pointer_cast<array_type>(inner_sub)) {
            bare_subtype = inner_sub;
            arr_needs_inner_ref_load = true;
        }
    }

    // ── Virtual member: array.size ──────────────────────────────────────────
    if (auto arr_subtype = std::dynamic_pointer_cast<array_type>(bare_subtype)) {
        const std::string& name_str = expr.symbol().get_name().to_string();
        if (name_str == "size") {
            // For unsized array fields (double-reference), load the inner pointer first.
            if (arr_needs_inner_ref_load) {
                _value = _builder->CreateLoad(
                    llvm::PointerType::get(_builder->getContext(), 0), _value, "arr_inner_ref_load");
            }
            // _value is a pointer to the array struct { i32, [N x T] }.
            // GEP into field 0 (size), then load.
            auto* struct_ty = arr_subtype->get_llvm_struct_type();
            auto* size_ptr = _builder->CreateStructGEP(struct_ty, _value, array_type::FIELD_SIZE, "arr_size_ptr");
            _value = _builder->CreateLoad(llvm::Type::getInt32Ty(_builder->getContext()), size_ptr, "arr_size");
            return;
        }
    }

    // Step 3: GEP to the member field using the recorded field index
    if(auto struct_subtype = std::dynamic_pointer_cast<struct_type>(bare_subtype)) {
        const auto& member_name =  expr.symbol();
        // For qualified names like A::v, use only the last part (the field name)
        const k::name& sym_name = member_name.get_name();
        std::string simple_name = sym_name.size() > 1 ? sym_name.back() : sym_name.to_string();

        // ── Union alternative access ──
        // If the struct_type has no owning aggregate, it's a union type.
        // GEP to the storage field (index 1) and bitcast to the alternative's type pointer.
        if (!struct_subtype->get_struct()) {
            // Find the union_type_def
            std::shared_ptr<union_type_def> union_def;
            auto root_ns = _unit.get_root_namespace();
            if (root_ns) {
                for (auto& [uname, udef] : root_ns->unions()) {
                    if (udef->get_struct_type() == struct_subtype) {
                        union_def = udef;
                        break;
                    }
                }
            }
            if (!union_def) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F024), expr.first_lexeme(),
                    "Internal error: cannot find union definition for codegen of member '{}'",
                    {simple_name});
                return;
            }
            auto* alt = union_def->get_alternative_by_name(simple_name);
            if (!alt || !alt->resolved_type) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F024), expr.first_lexeme(),
                    "Internal error: union '{}' has no alternative named '{}' during codegen",
                    {union_def->get_short_name(), simple_name});
                return;
            }
            // _value is a pointer to the union struct { i32, [N x i8] }
            auto* union_llvm_type = struct_subtype->get_llvm_type();
            // ── Runtime discriminant check (read-only accesses) ──
            // Skip the check when this access is the LHS of an assignment (we're about
            // to switch the active alternative — the discriminant will be updated after).
            if (!_skip_union_disc_check) {
                auto* disc_ptr = _builder->CreateStructGEP(union_llvm_type, _value, 0, "union_rd_disc_ptr");
                auto* disc_val = _builder->CreateLoad(llvm::Type::getInt32Ty(_builder->getContext()), disc_ptr, "union_rd_disc");
                auto* expected = llvm::ConstantInt::get(llvm::Type::getInt32Ty(_builder->getContext()), alt->index);
                auto* cmp = _builder->CreateICmpNE(disc_val, expected, "union_disc_cmp");

                auto* cur_fn = _builder->GetInsertBlock()->getParent();
                auto* fail_bb = llvm::BasicBlock::Create(_builder->getContext(), "union_access_fail", cur_fn);
                auto* ok_bb = llvm::BasicBlock::Create(_builder->getContext(), "union_access_ok", cur_fn);
                _builder->CreateCondBr(cmp, fail_bb, ok_bb);

                // Fail branch: call trap and unreachable
                _builder->SetInsertPoint(fail_bb);
                auto* trap_fn = llvm::Intrinsic::getDeclaration(&_context->module(), llvm::Intrinsic::trap);
                _builder->CreateCall(trap_fn);
                _builder->CreateUnreachable();

                // OK branch: continue with member access
                _builder->SetInsertPoint(ok_bb);
            }
            // GEP to the storage field (index 1)
            auto* storage_ptr = _builder->CreateStructGEP(union_llvm_type, _value, 1, "union_storage");
            // The storage pointer is i8*; cast it to a pointer to the alternative's type
            // In LLVM opaque pointers mode, no bitcast needed — just use the pointer directly
            _value = storage_ptr;
            return;
        }

        // Step 4: Handle nested struct-in-struct member chains
        if(auto field = struct_subtype->get_member(simple_name); field) {
            _value = _builder->CreateStructGEP(bare_subtype->get_llvm_type(), _value, field->index);
        } else if(auto method = struct_subtype->get_struct()->get_function(simple_name)) {
            // Note return the already-assigned address of the struct onto which the function is applied to
        } else {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F024), expr.first_lexeme(),
                "Internal error: struct '{}' has no member named '{}' during code generation; "
                "the model is inconsistent — type resolution should have caught this earlier",
                {struct_subtype->name(), simple_name});
        }
    } else {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F025), expr.first_lexeme(),
            "Internal error: the '.' operator is applied to a non-struct type during code generation; "
            "the operand type is '{}' — type resolution should have caught this earlier",
            {type && type->get_subtype() ? type->get_subtype()->to_string() : "?"});
    }
}

//
// Member of pointer expression (->)
// Acts as (*expr).member. Supported LHS: pointer (*), link (+), view (?).
//
/**
 * Resolve a member-of-pointer expression (expr->member): dereference pointer/link/view
 * then access member. Supported LHS: pointer (*), link (~), view (^).
 *
 * Steps:
 *   1. Resolve the sub-expression (LHS) and determine the pointed struct type.
 *   2. For link/view/pointer: unwrap indirection to get the struct type.
 *   3. Look up the member name in the struct (fields and methods).
 *   4. Set type and record field index or function target.
 */
void type_reference_resolver::visit_member_of_pointer_expression(member_of_pointer_expression& expr) {
    expr.sub_expr()->accept(*this);
    auto type = expr.sub_expr()->get_type();

    // Unwrap ref-to-indirection
    if (auto ref_type = std::dynamic_pointer_cast<reference_type>(type)) {
        type = ref_type->get_subtype();
    }

    std::shared_ptr<k::model::type> pointed_type;
    if (auto ptr_t = std::dynamic_pointer_cast<pointer_type>(type)) {
        pointed_type = ptr_t->get_pointed_type();
    } else if (auto lnk_t = std::dynamic_pointer_cast<link_type>(type)) {
        pointed_type = lnk_t->get_linked_type();
    } else if (auto view_t = std::dynamic_pointer_cast<view_type>(type)) {
        pointed_type = view_t->get_viewed_type();
    } else if (auto own_t = std::dynamic_pointer_cast<owner_type>(type)) {
        // Allow -> on owner: syntactic sugar for (*owner).member
        pointed_type = own_t->get_owned_type();
    } else {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_OVERLOAD_CALL_NO_MATCH), expr.first_lexeme(),
            "The '->' operator requires a pointer (*), link (+), view (?) or owner (!) on the LHS, "
            "but got '{}'", {type ? type->to_string() : "?"});
    }

    // ── Virtual member: array->size ─────────────────────────────────────────
    // pointed_type is array_type directly for sized arrays (e.g. int[3]*),
    // or reference<array_type> for unsized arrays (e.g. int[]* since int[] = int[]&).
    auto arr_pointed = pointed_type;
    if (auto ref_wrap = std::dynamic_pointer_cast<reference_type>(arr_pointed)) {
        arr_pointed = ref_wrap->get_subtype();
    }
    // Strip const for structural checks (const is enforced separately)
    arr_pointed = type::remove_const(arr_pointed);
    if (auto arr_subtype = std::dynamic_pointer_cast<array_type>(arr_pointed)) {
        const std::string& name_str = expr.symbol().get_name().to_string();
        if (name_str == "size") {
            expr.set_type(_context->from_type(primitive_type::UNSIGNED_INT));
            return;
        }
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_MEMBER_NOT_FOUND_ON_TYPE), expr.first_lexeme(),
            "Arrays have no member named '{}'; only 'size' is available",
            {name_str});
    }

    // Step 1: Resolve the sub-expression (LHS) and determine the pointed struct type
    auto pointed_nc = type::remove_const(pointed_type);
    auto struct_subtype = std::dynamic_pointer_cast<struct_type>(pointed_nc);
    if (!struct_subtype) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::ERR_GEN_FUNC_OVERLOAD_AMBIGUOUS), expr.first_lexeme(),
            "The '->' operator requires a pointer to a struct or array, "
            "but the pointed-to type is '{}'",
            {pointed_type ? pointed_type->to_string() : "?"});
    }

    // Step 2: For link/view/pointer: unwrap indirection to get the struct type
    const auto& member_name = expr.symbol();
    const std::string& name_str = member_name.get_name().to_string();
    // Step 3: Look up the member name in the struct (fields and methods)
    if (auto field = struct_subtype->get_member(name_str)) {
        // Check visibility of the accessed field
        if (auto st_model = struct_subtype->get_struct()) {
            if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(st_model->get_variable(name_str))) {
                auto vis = mv->get_visibility();
                if (vis != PUBLIC) {
                    if (!scope_lookup::is_struct_member_accessible(vis, *st_model, st_model, _function_stack)) {
                        if (vis != PROTECTED || !scope_lookup::is_friend_of(*st_model, _function_stack, _unit)) {
                            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_OVERLOAD_RETURN_TYPE_MISMATCH), expr.first_lexeme(),
                                "{} member variable '{}' of struct '{}' is not accessible here via '->'; "
                                "it can only be accessed from member functions of '{}'{}",
                                {vis == PROTECTED ? "protected" : "private",
                                 mv->get_short_name(), st_model->get_short_name(), st_model->get_short_name(),
                                 vis == PROTECTED ? " or its subclasses or friends" : ""});
                        }
                    }
                }
            }
        }
        auto field_type = field->field_type.lock();
        expr.set_type(field_type ? field_type->get_reference() : nullptr);
    } else if (struct_subtype->get_struct() && struct_subtype->get_struct()->get_function(name_str)) {
        // Check visibility of the accessed method
        if (auto st_model = struct_subtype->get_struct()) {
            if (auto fn = st_model->get_function(name_str)) {
                auto vis = fn->get_visibility();
                if (vis != PUBLIC) {
                    if (!scope_lookup::is_struct_member_accessible(vis, *st_model, st_model, _function_stack)) {
                        if (vis != PROTECTED || !scope_lookup::is_friend_of(*st_model, _function_stack, _unit)) {
                            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_OVERLOAD_CONST_MISMATCH), expr.first_lexeme(),
                                "{} member function '{}' of struct '{}' is not accessible here via '->'; "
                                "it can only be called from member functions of '{}'{}",
                                {vis == PROTECTED ? "protected" : "private",
                                 fn->get_short_name(), st_model->get_short_name(), st_model->get_short_name(),
                                 vis == PROTECTED ? " or its subclasses or friends" : ""});
                        }
                    }
                }
            }
        }
        expr.set_type(pointed_type->get_reference());
    } else {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_OVERLOAD_ARG_TYPE_MISMATCH), expr.first_lexeme(),
            "Struct '{}' has no member named '{}'",
            {struct_subtype->name(), name_str});
    }
}

/**
 * Generate LLVM IR for member-of-pointer expression: load pointer, GEP to field.
 *
 * Steps:
 *   1. Visit sub-expression to get the pointer/link/view value.
 *   2. Load the pointer value (dereference indirection).
 *   3. GEP to the member field.
 */
void implementation_generator::visit_member_of_pointer_expression(member_of_pointer_expression& expr) {
    _value = nullptr;
    expr.sub_expr()->accept(*this);

    auto sub_type = expr.sub_expr()->get_type();
    std::shared_ptr<k::model::type> inner_type;
    if (auto ref_type = std::dynamic_pointer_cast<reference_type>(sub_type)) {
        inner_type = ref_type->get_subtype();
        _value = _builder->CreateLoad(_context->get_llvm_type(inner_type), _value, "arrow_load");
    } else {
        inner_type = sub_type;
    }

    // Null-check for nullable indirections (pointer, pin, owner)
    if (std::dynamic_pointer_cast<pointer_type>(inner_type) ||
        std::dynamic_pointer_cast<view_type>(inner_type) ||
        std::dynamic_pointer_cast<owner_type>(inner_type)) {
        auto* fatal = get_or_declare_fatal_null_function("__k_fatal_null_dereference");
        emit_null_check(_value, fatal, "arrow");
    }

    // Step 1: Visit sub-expression to get the pointer/link/view value
    std::shared_ptr<k::model::type> pointed_type;
    if (auto ptr_t = std::dynamic_pointer_cast<pointer_type>(inner_type)) pointed_type = ptr_t->get_pointed_type();
    else if (auto lnk_t = std::dynamic_pointer_cast<link_type>(inner_type)) pointed_type = lnk_t->get_linked_type();
    else if (auto view_t = std::dynamic_pointer_cast<view_type>(inner_type)) pointed_type = view_t->get_viewed_type();
    else if (auto own_t = std::dynamic_pointer_cast<owner_type>(inner_type)) pointed_type = own_t->get_owned_type();
    if (!pointed_type) return;

    // Step 2: Load the pointer value (dereference indirection)
    // ── Virtual member: array->size ─────────────────────────────────────────
    // For unsized arrays (int[] = int[]&), pointed_type is reference<array_type>.
    // We must unwrap the reference and add an extra load to follow the double
    // indirection (ptr → ref → array struct).
    auto arr_pointed = pointed_type;
    bool arr_needs_ref_load = false;
    if (auto ref_wrap = std::dynamic_pointer_cast<reference_type>(arr_pointed)) {
        arr_pointed = ref_wrap->get_subtype();
        arr_needs_ref_load = true;
    }
    arr_pointed = type::remove_const(arr_pointed);
    if (auto arr_subtype = std::dynamic_pointer_cast<array_type>(arr_pointed)) {
        const std::string& name_str = expr.symbol().get_name().to_string();
        if (name_str == "size") {
            if (arr_needs_ref_load) {
                // Extra load: dereference the reference to get the actual array struct pointer
                _value = _builder->CreateLoad(
                    llvm::PointerType::get(_builder->getContext(), 0), _value, "arr_ref_load");
            }
            auto* struct_ty = arr_subtype->get_llvm_struct_type();
            auto* size_ptr = _builder->CreateStructGEP(struct_ty, _value, array_type::FIELD_SIZE, "arr_size_ptr");
            _value = _builder->CreateLoad(llvm::Type::getInt32Ty(_builder->getContext()), size_ptr, "arr_size");
            return;
        }
    }

    // Step 3: GEP to the member field
    auto pointed_nc = type::remove_const(pointed_type);
    auto struct_subtype = std::dynamic_pointer_cast<struct_type>(pointed_nc);
    if (!struct_subtype) return;
    const auto& member_name = expr.symbol();
    if (auto field = struct_subtype->get_member(member_name.get_name())) {
        _value = _builder->CreateStructGEP(
            _context->get_llvm_type(pointed_nc), _value,
            (unsigned)field->index, member_name.get_name().to_string() + "_ptr");
    }
    // For method: _value is already the struct ptr (this)
}

//
// PM expression (.* and ->*)
//

/**
 * Resolve a pointer-to-member expression (.* or ->*).
 *
 * Steps:
 *   1. Resolve LHS (object or pointer to object) and RHS (member function pointer).
 *   2. Validate that the RHS is a member_function_reference_type.
 *   3. Set result type to the return type of the member function reference.
 */
void type_reference_resolver::visit_pm_expression(pm_expression& expr) {
    // Resolve both sub-expressions
    expr.left()->accept(*this);
    expr.right()->accept(*this);

    // ── LHS: get the struct type ──────────────────────────────────────────────
    auto obj_type = expr.left()->get_type();

    // Unwrap ref if needed
    if (auto ref = std::dynamic_pointer_cast<reference_type>(obj_type)) {
        obj_type = ref->get_subtype();
    }

    // For ->*, unwrap the pointer/link/pin layer
    if (expr.is_arrow()) {
        if (auto ptr = std::dynamic_pointer_cast<pointer_type>(obj_type)) {
            obj_type = ptr->get_pointed_type();
        } else if (auto lnk = std::dynamic_pointer_cast<link_type>(obj_type)) {
            obj_type = lnk->get_linked_type();
        } else if (auto view_var = std::dynamic_pointer_cast<view_type>(obj_type)) {
            obj_type = view_var->get_viewed_type();
        } else {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_PM_EXPR_BAD_TYPE), expr.first_lexeme(),
                "The '->*' operator requires a pointer (*), link (+) or view (?) on the LHS, "
                "but got '{}'", {obj_type ? obj_type->to_string() : "?"});
        }
    }

    auto struct_t = std::dynamic_pointer_cast<struct_type>(obj_type);
    if (!struct_t) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_PM_EXPR_NOT_MEMBER_PTR), expr.first_lexeme(),
            "The '{}' operator requires a struct on the LHS, but got '{}'",
            {expr.is_arrow() ? "->*" : ".*", obj_type ? obj_type->to_string() : "?"});
    }

    // Step 1: Resolve LHS (object or pointer to object) and RHS (member function pointer)
    // ── RHS: must be a member_function_reference_type ────────────────────────
    auto mfp_type = expr.right()->get_type();
    // Unwrap ref wrapper if present
    if (auto ref = std::dynamic_pointer_cast<reference_type>(mfp_type)) {
        mfp_type = ref->get_subtype();
    }
    auto mfrt = std::dynamic_pointer_cast<member_function_reference_type>(mfp_type);
    if (!mfrt) {
        // Also accept plain function_reference_type (for free-function pointers used in pm context)
        if (!std::dynamic_pointer_cast<function_reference_type>(mfp_type)) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_PM_EXPR_INCOMPATIBLE), expr.first_lexeme(),
                "The '{}' operator requires a member function reference type on the RHS, "
                "but got '{}'",
                {expr.is_arrow() ? "->*" : ".*", mfp_type ? mfp_type->to_string() : "?"});
        }
    }

    // Step 2: Validate that the RHS is a member_function_reference_type
    // Step 3: Set result type to the return type of the member function reference
    // ── Result type: return type of the member function reference ────────────
    auto frt = std::dynamic_pointer_cast<function_reference_type>(mfp_type);
    expr.set_type(frt ? frt->get_return_type() : nullptr);
}

void implementation_generator::visit_pm_expression(pm_expression& expr) {
    // pm_expression is used as the callee of a function_invocation_expression.
    // The invocation handler calls this to get the (this_ptr, fn_ptr) pair it needs.
    // Here we just produce the function pointer value; the object pointer is obtained
    // separately by the caller via expr.left().
    //
    // However, if visit_pm_expression is reached standalone (e.g. result unused),
    // produce nullptr.
    _value = nullptr;

    // Evaluate the RHS (member function pointer variable)
    expr.right()->accept(*this);
    auto fn_alloca = _value;
    if (!fn_alloca) return;

    // Load the actual function pointer from the variable alloca
    auto mfp_type = expr.right()->get_type();
    if (auto ref = std::dynamic_pointer_cast<reference_type>(mfp_type)) {
        mfp_type = ref->get_subtype();
    }
    if (auto frt = std::dynamic_pointer_cast<function_reference_type>(mfp_type)) {
        auto* llvm_fn_type = frt->get_llvm_type();
        if (llvm_fn_type) {
            _value = _builder->CreateLoad(llvm_fn_type, fn_alloca, "mfp_load");
        }
    }
}

//
// Subscript expression
//

/**
 * Resolve a subscript expression (expr[index]): array/pointer element access or
 * operator[] overload.
 *
 * Steps:
 *   1. Resolve LHS and RHS sub-expressions.
 *   2. If LHS is a struct type: look for operator[] overload.
 *   3. If LHS is an array (sized or unsized): validate index type, set element type.
 *   4. If LHS is a pointer/link/view/owner: perform pointer arithmetic + deref.
 *   5. Set result type as reference to element type.
 */
void type_reference_resolver::visit_subscript_expression(subscript_expression& expr) {
    // Step 1: Resolve LHS and RHS sub-expressions
    visit_binary_expression(expr);

    // Step 2: If LHS is a struct type: look for operator[] overload
    auto left = expr.left();
    auto right = expr.right();

    auto left_type = left->get_type();

//  TODO dereference for double references

    // Dereference if needed
    if(!type::is_reference(left_type)) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_POINTER_MEMBER_NOT_FOUND), expr.first_lexeme(),
            "Subscript operator '[]' requires a reference to an array as left operand, "
            "but the left operand has type '{}' which is not a reference",
            {left_type ? left_type->to_string() : "?"});
    }
    if(type::is_double_reference(left_type)) {
        // Deref first ref
        left_type = left_type->get_subtype();
    }
    auto inner_type = std::dynamic_pointer_cast<reference_type>(left_type)->get_subtype();

    // Detect constness before stripping const qualifier
    bool is_const_left = type::is_const(inner_type);
    // Strip const qualifier for type checks
    auto bare_inner = type::remove_const(inner_type);

    // ── Operator[] overload for aggregate (struct/class/interface) ──
    // Only triggered when the inner type is directly a struct type (not through
    // an indirection like pointer/owner/link/view — those remain array indexing).
    if (type::is_struct(bare_inner)) {
        auto st_type = std::dynamic_pointer_cast<struct_type>(bare_inner);
        if (st_type) {
            auto agg = st_type->get_struct();
            if (agg) {
                auto [op_func, adapted_right] = resolve_binary_operator_overload(expr, agg, left, right, is_const_left);
                if (op_func) {
                    // Store the resolved operator function on the expression
                    expr.set_operator_func(op_func);
                    // Apply the adapted right operand (implicit cast if needed)
                    if (adapted_right && adapted_right != right) {
                        expr.assign_right(adapted_right);
                    }
                    // Set the expression type to the return type of the operator function
                    if (op_func->has_return_type()) {
                        expr.set_type(op_func->get_return_type());
                    } else {
                        expr.set_type(bare_inner);
                    }
                    // Compute dispatch info for virtual calls
                    if (op_func->is_member()) {
                        auto di = compute_operator_dispatch_info(op_func, left_type);
                        expr.set_operator_dispatch_info(std::move(di));
                    } else {
                        virtual_dispatch_info di;
                        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                        expr.set_operator_dispatch_info(std::move(di));
                    }
                    return;
                }
                // No operator[] found on this aggregate — fall through to array path
                // (the aggregate might wrap an array, or this will error below)
            }
        }
    }

    // ── Array subscript path (original logic) ──
    // Unwrap any indirection type (owner, pointer, link, view) to reach the inner array
    auto arr_type_inner = bare_inner;
    if (type::is_owner(arr_type_inner)) {
        arr_type_inner = std::dynamic_pointer_cast<owner_type>(arr_type_inner)->get_owned_type();
    } else if (type::is_pointer(arr_type_inner)) {
        arr_type_inner = std::dynamic_pointer_cast<pointer_type>(arr_type_inner)->get_pointed_type();
    } else if (type::is_link(arr_type_inner)) {
        arr_type_inner = std::dynamic_pointer_cast<link_type>(arr_type_inner)->get_linked_type();
    } else if (type::is_view(arr_type_inner)) {
        arr_type_inner = std::dynamic_pointer_cast<view_type>(arr_type_inner)->get_viewed_type();
    }

    // Step 3: If LHS is an array (sized or unsized): validate index type, set element type
    // Unsized arrays (e.g. char[]) are canonicalized to ref<array<T>>.
    // After unwrapping an indirection such as pointer<ref<array<T>>> we
    // may still have a reference wrapper — strip it to reach the array.
    if (type::is_reference(arr_type_inner)) {
        arr_type_inner = std::dynamic_pointer_cast<reference_type>(arr_type_inner)->get_subtype();
    }

    // Step 4: If LHS is a pointer/link/view/owner: perform pointer arithmetic + deref
    // Strip any remaining const wrapper (e.g. pointer<const<array<T>>> → const<array<T>>)
    arr_type_inner = type::remove_const(arr_type_inner);

    // Step 5: Set result type as reference to element type
    if(!type::is_array(arr_type_inner)) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_SUBSCRIPT_NOT_ARRAY), expr.first_lexeme(),
            "Subscript operator '[]' can only be applied to an array type, "
            "but the dereferenced left operand has type '{}' which is not an array",
            {arr_type_inner ? arr_type_inner->to_string() : "?"});
    }
    auto arr_type = std::dynamic_pointer_cast<array_type>(arr_type_inner);
    expr.set_type(arr_type->get_subtype()->get_reference());

    // Check the right hand can be cast to unsigned integer
    // TODO adapt to the really right index type.
    // TODO is array really indexed by uint ?
    auto adapted_right = adapt_type(right, _context->from_type(primitive_type::UNSIGNED_INT));
    if(!adapted_right) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_SUBSCRIPT_INDEX_TYPE), expr.first_lexeme(),
            "Subscript index expression cannot be implicitly converted to an unsigned integer index type; "
            "the index operand has type '{}' — use an explicit cast if needed",
            {right->get_type() ? right->get_type()->to_string() : "?"});
    } else if(adapted_right!=right) {
        right = adapted_right;
        expr.assign_right(right);
    }
}

/**
 * Generate LLVM IR for subscript expression: GEP + load for arrays/pointers,
 * or operator[] call for structs.
 *
 * Steps:
 *   1. If operator overload: delegate to generate_binary_operator_overload.
 *   2. For arrays: compute GEP with index into the array data.
 *   3. For pointers/links/views: load pointer, then GEP with index.
 *   4. Result is a pointer to the element (reference semantics).
 */
void implementation_generator::visit_subscript_expression(subscript_expression& expr) {
    // Step 1: If operator overload: delegate to generate_binary_operator_overload
    if (expr.has_operator_overload()) {
        if (generate_binary_operator_overload(expr)) return;
    }

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        _value = nullptr;
        return;
    }

    auto left_type = expr.left()->get_type();

    // Dereference if double ref (ref<ref<array>>)
    if(type::is_double_reference(left_type)) {
        left_type = left_type->get_subtype();
        left = _builder->CreateLoad(_context->get_llvm_type(left_type), left, "arr_ref");
    }

    // At this point left_type is ref<X> where X can be:
    //   - sized_array_type or array_type (direct array)
    //   - owner_type, pointer_type, link_type, view_type wrapping an array
    //   - const_type wrapping any of the above
    auto arr_type_inner = left_type->get_subtype();

    // Strip const qualifier before checking for indirection / array
    arr_type_inner = type::remove_const(arr_type_inner);

    // Handle any indirection wrapping an array: load the raw pointer, then treat it as ptr to array struct
    // All indirection types (owner, pointer, link, view) are opaque pointers in LLVM IR.
    if (auto own_type = std::dynamic_pointer_cast<owner_type>(arr_type_inner)) {
        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
        left = _builder->CreateLoad(ptr_ty, left, "own_arr_ptr");
        arr_type_inner = own_type->get_owned_type();
    } else if (auto ptr_type = std::dynamic_pointer_cast<pointer_type>(arr_type_inner)) {
        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
        left = _builder->CreateLoad(ptr_ty, left, "ptr_arr_ptr");
        arr_type_inner = ptr_type->get_pointed_type();
    } else if (auto lnk_type = std::dynamic_pointer_cast<link_type>(arr_type_inner)) {
        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
        left = _builder->CreateLoad(ptr_ty, left, "lnk_arr_ptr");
        arr_type_inner = lnk_type->get_linked_type();
    } else if (auto view_type_var = std::dynamic_pointer_cast<view_type>(arr_type_inner)) {
        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
        left = _builder->CreateLoad(ptr_ty, left, "view_arr_ptr");
        arr_type_inner = view_type_var->get_viewed_type();
    }

    // Unsized arrays (e.g. char[]) are canonicalized to ref<array<T>>.
    // After unwrapping an indirection such as pointer<ref<array<T>>> we
    // may still have a reference wrapper — strip it and load the actual
    // array struct pointer through the reference.
    if (auto ref_inner = std::dynamic_pointer_cast<reference_type>(arr_type_inner)) {
        arr_type_inner = ref_inner->get_subtype();
        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
        left = _builder->CreateLoad(ptr_ty, left, "ref_arr_ptr");
    }

    // Strip any remaining const wrapper (e.g. pointer<const<array<T>>> → const<array<T>>)
    arr_type_inner = type::remove_const(arr_type_inner);

    // Dereference index if needed
    auto right_type = expr.right()->get_type();
    if(type::is_reference(right_type)) {
        right_type = std::dynamic_pointer_cast<reference_type>(right_type)->get_subtype();
        right = _builder->CreateLoad(_context->get_llvm_type(right_type), right, "idx");
    }

    if (auto sized_arr = std::dynamic_pointer_cast<sized_array_type>(arr_type_inner)) {
        // Layout: { i32, [N x T] }
        // GEP: struct_ptr -> field 1 (data array) -> element index
        auto* struct_llvm = sized_arr->get_llvm_struct_type();
        auto* data_arr_llvm = sized_arr->get_llvm_data_array_type();
        if (!struct_llvm || !data_arr_llvm) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F026), expr.first_lexeme(),
                "Internal error: sized array has no LLVM struct type during subscript code generation");
        }

        // ── Runtime bounds check ─────────────────────────────────────────
        // Load the element count from field 0 and verify index < count.
        llvm::Value* count_ptr = _builder->CreateStructGEP(struct_llvm, left,
            sized_array_type::FIELD_SIZE, "arr_count_ptr");
        llvm::Value* count_val = _builder->CreateLoad(
            _builder->getInt32Ty(), count_ptr, "arr_count");
        emit_array_bounds_check(_builder.get(), get_module(), right, count_val, "subscript");

        // Two-step GEP: first into the struct field 1, then into the array element
        llvm::Value* field_data_ptr = _builder->CreateStructGEP(struct_llvm, left,
            sized_array_type::FIELD_DATA, "arr_data_ptr");
        llvm::Value* indices[] = {_builder->getInt32(0), right};
        _value = _builder->CreateGEP(data_arr_llvm, field_data_ptr, indices, "elem_ptr");
    } else if (auto unsized_arr = std::dynamic_pointer_cast<array_type>(arr_type_inner)) {
        // Unsized (dynamic) array: layout { i32, [0 x T] } — same GEP pattern as sized.
        auto* struct_llvm = unsized_arr->get_llvm_struct_type();
        auto* data_arr_llvm = unsized_arr->get_llvm_data_array_type();
        if (!struct_llvm || !data_arr_llvm) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F026), expr.first_lexeme(),
                "Internal error: unsized array has no LLVM struct type during subscript code generation");
        }

        // Runtime bounds check: load count from field 0, verify index < count
        llvm::Value* count_ptr = _builder->CreateStructGEP(struct_llvm, left,
            array_type::FIELD_SIZE, "dynarr_count_ptr");
        llvm::Value* count_val = _builder->CreateLoad(
            _builder->getInt32Ty(), count_ptr, "dynarr_count");
        emit_array_bounds_check(_builder.get(), get_module(), right, count_val, "subscript");

        // Step 2: For arrays: compute GEP with index into the array data
        // Step 3: For pointers/links/views: load pointer, then GEP with index
        // GEP into field 1 (data), then index into the [0 x T] trailing array
        llvm::Value* field_data_ptr = _builder->CreateStructGEP(struct_llvm, left,
            array_type::FIELD_DATA, "dynarr_data_ptr");
        llvm::Value* indices[] = {_builder->getInt32(0), right};
        _value = _builder->CreateGEP(data_arr_llvm, field_data_ptr, indices, "elem_ptr");
    }
}

//
// Function invocation expression
//


} // namespace k::model::gen
