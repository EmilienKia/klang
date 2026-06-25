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
// value_expression, symbol_expression, and helpers
// (shared helpers moved to gen_helpers.hpp)



//
// Value expression
//

void symbol_resolver::visit_value_expression(value_expression& expr)
{
}

void type_reference_resolver::visit_value_expression(value_expression& expr)
{
    if (expr.is_literal()) {
        auto type = _context->from_literal(expr.any_literal());
        expr.set_type(type);
    } else {
        // Non-literal value expression (e.g. template value parameter substitution).
        // Infer type from the k::value_type variant.
        auto& val = expr.get_value();
        std::shared_ptr<type> inferred = std::visit([this](auto&& v) -> std::shared_ptr<type> {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool>)           return _context->from_type(primitive_type::BOOL);
            else if constexpr (std::is_same_v<T, char>)      return _context->from_type(primitive_type::CHAR);
            else if constexpr (std::is_same_v<T, unsigned char>)  return _context->from_type(primitive_type::UNSIGNED_BYTE);
            else if constexpr (std::is_same_v<T, short>)     return _context->from_type(primitive_type::SHORT);
            else if constexpr (std::is_same_v<T, unsigned short>) return _context->from_type(primitive_type::UNSIGNED_SHORT);
            else if constexpr (std::is_same_v<T, int>)       return _context->from_type(primitive_type::INT);
            else if constexpr (std::is_same_v<T, unsigned int>)   return _context->from_type(primitive_type::UNSIGNED_INT);
            else if constexpr (std::is_same_v<T, long>)      return _context->from_type(primitive_type::LONG);
            else if constexpr (std::is_same_v<T, unsigned long>)  return _context->from_type(primitive_type::UNSIGNED_LONG);
            else if constexpr (std::is_same_v<T, long long>)     return _context->from_type(primitive_type::LONG_LONG);
            else if constexpr (std::is_same_v<T, unsigned long long>) return _context->from_type(primitive_type::UNSIGNED_LONG_LONG);
            else if constexpr (std::is_same_v<T, float>)     return _context->from_type(primitive_type::FLOAT);
            else if constexpr (std::is_same_v<T, double>)    return _context->from_type(primitive_type::DOUBLE);
            else return {};
        }, val);
        expr.set_type(inferred);
    }
}

llvm::Constant* implementation_generator::get_llvm_constant_from_value_expr(const value_expression& expr) const {
    if(expr.is_literal()) {
        return _context->get_llvm_constant_from_literal(expr.any_literal());
    } else {
        return _context->get_llvm_constant_from_value(expr.get_value());
    }
}

void implementation_generator::visit_value_expression(value_expression &expr) {
    _value = get_llvm_constant_from_value_expr(expr);
}

//
// Symbol expression:
//
// Symbol expression could be found in two cases:
// - In the symbol part (right hand) of a member-of expression, like b in "a.b".
//   In this case, the symbol is supposed to be already resolved when visiting the member-of expression.
//   It must not be resolved again here.
// - As a standalone symbol expression, like a or b in "a + b".
//   In this case, the symbol must be resolved here.
//   It could be only:
//   - a non-member variable: local variable, parameter, global variable.
//      - the returned value is the reference (address) of the variable.
//      - the return value type is always a reference to the variable type.
//   - a non-member function: function name used as a function pointer. It returns the reference of the function.
//      - the return value is always a function reference address.
//      - the return value type is always a function reference type.
//

void symbol_resolver::visit_symbol_expression(symbol_expression& symbol)
{
    auto found_symbol = resolve_symbol(symbol);
    if (std::holds_alternative<std::shared_ptr<variable_definition>>(found_symbol)) {
        auto var_def = std::get<std::shared_ptr<variable_definition>>(found_symbol);
        symbol.set_target(var_def);
        // Check visibility of member and global variables at the access site
        check_variable_visibility(*var_def, symbol);
    } else if (std::holds_alternative<std::shared_ptr<function>>(found_symbol)) {
        auto func =  std::get<std::shared_ptr<function>>(found_symbol);
        symbol.set_target(func);
    } else {
        // Symbol not found as variable/function.
        // Try special trailing-keyword resolution: EnumName::entryName,
        // AnnotationName::annotation, etc.
        bool resolved_as_special = false;
        const auto& sym_name = symbol.get_name();

        if (sym_name.size() >= 2 && !sym_name.has_root_prefix()) {
            const std::string& last_part = sym_name.back();

            // ── AnnotationName::annotation → RTTI descriptor ────────────
            if (last_part == "annotation") {
                std::vector<std::string> ann_parts(sym_name.parts().begin(),
                                                     sym_name.parts().end() - 1);
                k::name ann_name{false, std::move(ann_parts)};
                std::shared_ptr<aggregate> found_ann;

                // Try local scope first (single-part name)
                if (ann_name.size() == 1) {
                    auto agg = scope_lookup::lookup_structure(
                        symbol.shared_as<element>(), ann_name.front());
                    if (agg && agg->is_annotation()) found_ann = agg;
                }
                // Try imported aggregates
                if (!found_ann) {
                    if (auto imp_agg = _unit.get_or_create_imported_aggregate(ann_name, _context)) {
                        if (imp_agg->is_annotation()) found_ann = imp_agg;
                    }
                }

                if (found_ann) {
                    symbol.set_target(symbol_expression::annotation_type_rtti_target{found_ann});
                    resolved_as_special = true;
                }
            }

            // ── EnumName::entryName → enum entry ────────────────────────
            if (!resolved_as_special) {
                const std::string& entry_name = last_part;
                std::shared_ptr<enumeration> found_enum;

                if (sym_name.size() == 2) {
                    // Simple: EnumName::entryName — walk up the scope chain
                    for (auto current = symbol.shared_as<element>(); current; current = current->parent<element>()) {
                        if (auto eh = std::dynamic_pointer_cast<enum_holder>(current)) {
                            if (auto en = eh->get_enum(sym_name.front())) {
                                found_enum = en;
                                break;
                            }
                        }
                    }
                }

                // UnionName::Kind::entryName — look for union Kind enum
                if (!found_enum && sym_name.size() == 3 && sym_name[1] == "Kind") {
                    const std::string& union_name = sym_name.front();
                    for (auto current = symbol.shared_as<element>(); current; current = current->parent<element>()) {
                        if (auto uh = std::dynamic_pointer_cast<union_holder>(current)) {
                            if (auto un = uh->get_union(union_name)) {
                                found_enum = un->get_kind_enum();
                                break;
                            }
                        }
                    }
                }

                // If not found locally, try imported enums (works for both 2-part and N-part names)
                if (!found_enum) {
                    std::vector<std::string> enum_parts(sym_name.parts().begin(),
                                                         sym_name.parts().end() - 1);
                    k::name enum_name{false, std::move(enum_parts)};
                    found_enum = _unit.get_or_create_imported_enum(enum_name, _context);
                }

                if (found_enum) {
                    auto entry = found_enum->get_entry_by_name(entry_name);
                    if (entry.has_value()) {
                        size_t idx = 0;
                        for (auto& e : found_enum->entries()) {
                            if (e.name == entry_name) break;
                            idx++;
                        }
                        symbol.set_target(symbol_expression::enum_entry_target{found_enum, idx});
                        resolved_as_special = true;
                    } else {
                        throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_ANNOTATION_MISMATCH), symbol.first_lexeme(),
                            "Enum '{}' has no entry named '{}'",
                            {found_enum->get_short_name(), entry_name});
                    }
                }
            }
        }

        if (!resolved_as_special) {
            // If this symbol is the callee of a function invocation, defer resolution to
            // type_reference_resolver which handles unified call syntax and member lookups.
            // Also defer if the symbol is an argument of a constructor_invocation_expression:
            // the name may be an enum entry that can only be resolved once the target type is known.
            // Otherwise throw immediately, since there is nothing further that can resolve it.
            auto parent_expr = symbol.get_parent_expression();
            bool is_function_callee = false;
            if (auto parent_invoc = std::dynamic_pointer_cast<function_invocation_expression>(parent_expr)) {
                is_function_callee = (parent_invoc->callee_expr().get() == &symbol);
            }
            bool is_ctor_arg = std::dynamic_pointer_cast<constructor_invocation_expression>(parent_expr) != nullptr;
            if (!is_function_callee && !is_ctor_arg) {
                throw_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_UNRESOLVED_IDENTIFIER), symbol.first_lexeme(),
                    "Undefined symbol '{}': no variable, parameter or function with this name is visible in the current scope",
                    {symbol.get_name().to_string()});
            }
            // else: leave unresolved; type_reference_resolver will report the error if still not found
        }
    }
}

/**
 * Resolve a symbol expression: determine its target (variable, function, enum entry)
 * and set its type.
 *
 * Steps:
 *   1. Handle enum-qualified names (MyEnum::entry): resolve enum type and entry value.
 *   2. For variables: set type as reference to the variable's type.
 *   3. For functions: set type as function_reference_type.
 *   4. Check visibility of the resolved symbol.
 */
void type_reference_resolver::visit_symbol_expression(symbol_expression& symbol)
{
    // Step 1: Handle enum-qualified names (MyEnum::entry): resolve enum type and entry value
    if(!symbol.is_resolved()) {
        // Allow unresolved symbols that are arguments of a constructor_invocation_expression.
        // These may be enum entry names that will be resolved during visit_constructor_invocation_expression.
        auto parent = symbol.get_parent_expression();
        if (std::dynamic_pointer_cast<constructor_invocation_expression>(parent)) {
            return; // defer to visit_constructor_invocation_expression
        }

        // Late resolution for "UnionName::Kind::entryName" symbols that were not resolved
        // by symbol_resolver because they lived inside a template definition (which symbol_resolver
        // skips).  When the template is instantiated during type_reference_resolver, the nested
        // union's Kind enum may not have been synthesised yet.  We synthesise it here on demand
        // and resolve the symbol before falling through to the normal enum-entry type assignment.
        const auto& sym_name = symbol.get_name();
        if (sym_name.size() == 3 && !sym_name.has_root_prefix() && sym_name[1] == "Kind") {
            const std::string& union_name = sym_name.front();
            const std::string& entry_name = sym_name.back();
            // Walk up the element parent chain looking for a union_holder that owns the union.
            for (auto cur = symbol.shared_as<element>(); cur; cur = cur->parent<element>()) {
                if (auto* uh = dynamic_cast<union_holder*>(cur.get())) {
                    if (auto un = uh->get_union(union_name)) {
                        // Ensure the Kind enum is synthesised (idempotent call).
                        un->synthesize_kind_enum();
                        if (auto kind_enum = un->get_kind_enum()) {
                            // Create and register the enum_type if it doesn't exist yet.
                            if (!kind_enum->get_enum_type()) {
                                auto uint_type = _context->from_type(primitive_type::UNSIGNED_INT);
                                kind_enum->set_underlying_type(uint_type);
                                auto et = std::shared_ptr<enum_type>(new enum_type(kind_enum, uint_type));
                                kind_enum->set_enum_type(et);
                                std::string fq = kind_enum->get_fq_name();
                                if (!fq.empty()) _context->add_enum(fq, et);
                            }
                            // Resolve the symbol to the matching enum entry.
                            size_t idx = 0;
                            bool found_entry = false;
                            for (auto& e : kind_enum->entries()) {
                                if (e.name == entry_name) { found_entry = true; break; }
                                ++idx;
                            }
                            if (found_entry) {
                                symbol.set_target(symbol_expression::enum_entry_target{kind_enum, idx});
                            }
                        }
                        break;
                    }
                }
            }
        }

        // Late resolution for the 'this' keyword inside template-instantiated method
        // bodies.  symbol_resolver skips template definitions, and the lightweight
        // body-symbol pass run by template_instantiator cannot resolve 'this' because
        // the synthesised this-parameter is created only after that pass.  By the time
        // type resolution reaches the instantiated body the this-parameter exists, so
        // bind the symbol to the enclosing non-static member function's this-parameter.
        if (!symbol.is_resolved()) {
            const auto& nm = symbol.get_name();
            if (nm.size() == 1 && !nm.has_root_prefix() && nm.front() == "this") {
                for (auto func = symbol.ancestor<function>(); func; func = func->ancestor<function>()) {
                    if (func->is_member() && func->get_this_parameter()) {
                        symbol.set_target(std::const_pointer_cast<parameter>(func->get_this_parameter()));
                        break;
                    }
                }
            }
        }

        if (!symbol.is_resolved()) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F003), symbol.first_lexeme(),
                "Internal error: symbol '{}' reached type-resolution phase without being resolved; "
                "symbol resolution must be run before type resolution",
                {symbol.get_name().to_string()});
        }
    }
    if (symbol.is_variable_def()) {
        auto var_def = symbol.get_variable_def();
        auto var_type = var_def->get_type();
        // If the variable is declared const (flag), propagate const-ness through the type system.
        // For primitive/struct types: wrap in const_type → "const int", "const MyStruct".
        // For indirection types (link, pointer, view): apply const to the pointed-at subtype
        // → "link(const int)", "pointer(const int)", "view(const int)".
        // References are immutable by design; const on a reference applies to its subtype too.
        // This ensures that "const x : int~" and "x : const int~" are truly identical.
        if (var_def->is_const() && !type::is_const(var_type)) {
            if (type::is_any_indirection(var_type)) {
                // Apply const to the subtype of the indirection.
                auto sub = var_type->get_subtype();
                auto const_sub = sub->get_const();
                // Reconstruct the same indirection kind with const subtype.
                if (type::is_link(var_type)) {
                    var_type = const_sub->get_link();
                } else if (type::is_pointer(var_type)) {
                    var_type = const_sub->get_pointer();
                } else if (type::is_view(var_type)) {
                    var_type = const_sub->get_view();
                } else { // reference
                    var_type = const_sub->get_reference();
                }
            } else {
                var_type = var_type->get_const();
            }
        }
        // Step 2: For variables: set type as reference to the variable's type
        // Variable symbol will always be a reference to the variable type.
        if (type::is_reference(var_type) || type::is_drain(var_type)) {
            // Variable is already a reference/drain (indirection), so symbol type is the variable type.
            symbol.set_type(var_type);
        } else {
            // Variable is not a reference, so symbol type is a reference to the variable type.
            symbol.set_type(var_type->get_reference());
        }
    } else if (symbol.is_function()) {
        // Step 4: Check visibility of the resolved symbol
        // A symbol resolved to a function (without call parentheses) yields the address
        // of the function.  The type is a function_reference_type with ref_kind::link
        // (non-null, immutable — same semantics as a reference in K).
        // It can be freely assigned to a pointer (*), pin (^) or link (+) variable.
        auto func = symbol.get_function();
        if (func) {
            // Build the function_reference_type for this function.
            function_reference_type_builder builder(_context);
            builder.ref_kind(function_reference_type::ref_kind::link);
            // Return type
            auto ret_type = func->get_return_type();
            if (ret_type) builder.return_type(ret_type);
            // Parameter types
            for (size_t i = 0; i < func->get_parameter_size(); ++i) {
                auto p = func->get_parameter(i);
                if (p && p->get_type()) {
                    builder.append_parameter_type(p->get_type());
                }
            }
            // Owner struct (member function)
            auto owner_st = func->parent<aggregate>();
            if (owner_st && !func->is_static()) {
                auto owner_struct = std::dynamic_pointer_cast<structure>(owner_st);
                if (owner_struct) builder.member_of(owner_struct);
            }
            auto fn_ref_type = builder.build();
            // Keep fn_ref_type alive for the duration of type resolution.
            // Without this, fn_ref_type is a temporary local; the only other strong
            // reference is fn_ref_type->reference (the cached ref_type), which does NOT
            // own fn_ref_type.  When fn_ref_type expires, reference_type::subtype (a
            // weak_ptr) becomes dangling, causing is_resolved() to crash.
            _ephemeral_types.push_back(fn_ref_type);
            // The symbol gives a reference to the function ref type (non-null, like K's ~).
            // Wrap in a reference so that it can be assigned to any indirection kind.
            symbol.set_type(fn_ref_type->get_reference());
        }
    } else if (symbol.is_enum_entry()) {
        // Enum entry: the type is the enum_type itself (not a reference — it's an rvalue constant).
        auto& target = symbol.get_enum_entry();
        auto en = target.enum_def;
        if (en) {
            // Lazily create the enum_type if it doesn't exist yet (can happen for
            // union Kind enums resolved during symbol_resolver before aggregate_type_resolver).
            if (!en->get_enum_type()) {
                auto uint_type = _context->from_type(primitive_type::UNSIGNED_INT);
                en->set_underlying_type(uint_type);
                auto et = std::shared_ptr<enum_type>(new enum_type(en, uint_type));
                en->set_enum_type(et);
                std::string fq = en->get_fq_name();
                if (!fq.empty()) _context->add_enum(fq, et);
            }
            symbol.set_type(en->get_enum_type());
        }
    } else if (symbol.is_annotation_type_rtti()) {
        // AnnotationName::annotation → const k::AnnotationType&
        // Look up the ::k::AnnotationType class from libk and produce a const reference to it.
        k::name ann_type_name{false, {"k", "AnnotationType"}};
        auto ann_type_agg = _unit.get_or_create_imported_aggregate(ann_type_name, _context);
        if (ann_type_agg && ann_type_agg->get_struct_type()) {
            auto st = ann_type_agg->get_struct_type()->get_const()->get_reference();
            symbol.set_type(st);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_OVERLOAD_ARG_TYPE_MISMATCH), symbol.first_lexeme(),
                "Cannot resolve '::annotation' descriptor: the 'k' standard library must be imported "
                "to use annotation type RTTI (requires ::k::AnnotationType)",
                {});
        }
    }
    // (symbol type resolution complete)
}

/**
 * Generate LLVM IR for a symbol expression (variable load, parameter access, function address).
 *
 * Steps:
 *   1. Variables/parameters: load alloca or global, GEP for member variables.
 *   2. Function symbols: get the LLVM function pointer.
 *   3. Member function pointers: create a {funcptr, adjustment} pair.
 *   4. Handle 'this' parameter specially.
 */
void implementation_generator::visit_symbol_expression(symbol_expression &symbol) {
    if(symbol.is_variable_def()) {
        auto var_def = symbol.get_variable_def();
        llvm::Value* ptr = nullptr;
        std::string name;

        // Handle source of symbol
        if (auto param = std::dynamic_pointer_cast<parameter>(var_def)) {
            ptr =  _context->_parameter_variables[param];
            name = param->get_short_name();
        } else if (auto global_var = std::dynamic_pointer_cast<global_variable_definition>(var_def)) {
            ptr = _context->_global_vars[global_var];
            name = global_var->get_short_name();
        } else if (auto local_var = std::dynamic_pointer_cast<variable_statement>(var_def)) {
            ptr = _context->_variables[local_var];
            name = local_var->get_short_name();
        } else if (auto member_var = std::dynamic_pointer_cast<member_variable_definition>(var_def)) {
            name = member_var->get_short_name();

            // Get 'this' pointer
            llvm::Value* this_value_ref = nullptr;
            auto enclosing_stmt = symbol.find_statement();
            auto func = enclosing_stmt
                ? std::dynamic_pointer_cast<function>(enclosing_stmt->get_function())
                : nullptr;
            if(!func) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F01B), symbol.first_lexeme(),
                    "Internal error: cannot find enclosing function context for member variable '{}' access; "
                    "member variables can only be accessed from inside a method",
                    {member_var->get_fq_name()});
            }
            this_value_ref = _context->_function_this_variables[func];
            if (!this_value_ref) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F01C), symbol.first_lexeme(),
                    "Internal error: no 'this' pointer found in function '{}' for member variable '{}' access; "
                    "the function may be static or have no associated struct instance",
                    {func->get_fq_name(), member_var->get_fq_name()});
            }

            // Get member variable — potentially from an ancestor struct via __parent__ chain
            if(_struct_stack.empty()) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F01D), symbol.first_lexeme(),
                    "Internal error: no struct context on the code-generation stack when accessing member variable '{}'; "
                    "member access code generation must be performed inside a struct method",
                    {name});
            }

            // Determine if the member belongs to the current struct or an ancestor struct.
            // Build the chain of structs from the current (innermost) up to the owning struct.
            auto member_owner = member_var->parent<aggregate>();
            auto current_struct = _struct_stack.top();

            // Walk the __parent__ chain to reach the owning struct
            auto current_struct_type = current_struct->get_struct_type();
            llvm::Value* this_ptr = _builder->CreateLoad(
                    _context->get_llvm_type(current_struct_type->get_reference()),
                    this_value_ref,
                    "this_ref"
            );

            // Navigate __parent__ pointers until we reach the struct that owns member_var
            auto walk_struct = current_struct;
            while (walk_struct && walk_struct != member_owner) {
                if (walk_struct->is_inner()) {
                    // GEP to __parent__ field (always index 0 for inner structs)
                    auto walk_st_type = walk_struct->get_struct_type();
                    this_ptr = _builder->CreateStructGEP(
                            _context->get_llvm_type(walk_st_type),
                            this_ptr,
                            0, // __parent__ is always field 0
                            "parent_field_ptr"
                    );
                    // Load the parent reference (stored as opaque pointer, same as 'this')
                    auto outer_struct = walk_struct->get_enclosing_structure();
                    auto outer_ref_type = outer_struct->get_struct_type()->get_reference();
                    this_ptr = _builder->CreateLoad(
                            _context->get_llvm_type(outer_ref_type),
                            this_ptr,
                            "parent_ref"
                    );
                    walk_struct = outer_struct;
                } else {
                    // Not an inner struct: try to reach member_owner via base-class chain (__base_X__)
                    bool found_base_path = false;
                    std::function<bool(std::shared_ptr<aggregate>, llvm::Value*)> walk_bases;
                    walk_bases = [&](std::shared_ptr<aggregate> cur_agg, llvm::Value* cur_ptr) -> bool {
                        for (auto& bs : cur_agg->get_bases()) {
                            if (!bs.base || bs.is_virtual) continue;
                            std::string field_name = "__base_" + bs.sanitised_name() + "__";
                            auto cur_st = cur_agg->get_struct_type();
                            auto field = cur_st->get_member(field_name);
                            if (!field) continue;
                            llvm::Value* base_ptr = _builder->CreateStructGEP(
                                    _context->get_llvm_type(cur_st),
                                    cur_ptr,
                                    (unsigned)field->index,
                                    "base_" + bs.sanitised_name() + "_ptr"
                            );
                            bool same_owner = (bs.base.get() == member_owner.get());
                            if (!same_owner && bs.base && member_owner) {
                                same_owner = (bs.base->get_fq_name() == member_owner->get_fq_name());
                            }
                            if (same_owner) {
                                this_ptr = base_ptr;
                                walk_struct = bs.base;
                                return true;
                            }
                            if (walk_bases(bs.base, base_ptr)) return true;
                        }
                        return false;
                    };
                    found_base_path = walk_bases(walk_struct->shared_as<aggregate>(), this_ptr);
                    if (!found_base_path) {
                        // Imported/synthesized concrete aggregates can lose explicit base metadata
                        // while still carrying lowered __base_<name>__ fields in the LLVM layout.
                        // Try a direct jump to the expected base field before failing.
                        auto cur_st = walk_struct ? walk_struct->get_struct_type() : nullptr;
                        if (cur_st && member_owner) {
                            auto try_base_field = [&](const std::string& field_name) -> bool {
                                for (auto it = cur_st->fields_begin(); it != cur_st->fields_end(); ++it) {
                                    if (it->name != field_name) {
                                        continue;
                                    }
                                    this_ptr = _builder->CreateStructGEP(
                                        _context->get_llvm_type(cur_st),
                                        this_ptr,
                                        (unsigned)it->index,
                                        "base_fallback_ptr"
                                    );
                                    walk_struct = member_owner;
                                    return true;
                                }
                                return false;
                            };

                            if (try_base_field("__base_" + member_owner->get_short_name() + "__")) {
                                continue;
                            }

                            // Build sanitized namespace prefix from fq name using '_' separators.
                            std::string owner_fq_sanitized = member_owner->get_fq_name();
                            if (owner_fq_sanitized.rfind("::", 0) == 0) {
                                owner_fq_sanitized.erase(0, 2);
                            }
                            std::size_t ns_sep_pos = 0;
                            while ((ns_sep_pos = owner_fq_sanitized.find("::", ns_sep_pos)) != std::string::npos) {
                                owner_fq_sanitized.replace(ns_sep_pos, 2, "_");
                                ns_sep_pos += 1;
                            }
                            if (try_base_field("__base_" + owner_fq_sanitized + "__")) {
                                continue;
                            }

                            // Imported concrete types can expose base slots with a fully-qualified
                            // sanitized prefix (e.g. __base_k_io_FilterInputStream__byte__).
                            // Match by owner short-name suffix when exact synthesized names differ.
                            const std::string owner_suffix = member_owner->get_short_name() + "__";
                            std::optional<struct_type::field> suffix_match;
                            for (auto it = cur_st->fields_begin(); it != cur_st->fields_end(); ++it) {
                                if (it->name.rfind("__base_", 0) != 0) {
                                    continue;
                                }
                                if (!it->name.ends_with(owner_suffix)) {
                                    continue;
                                }
                                if (suffix_match) {
                                    // Ambiguous fallback: keep deterministic behavior and report error below.
                                    suffix_match.reset();
                                    break;
                                }
                                suffix_match = *it;
                            }
                            if (suffix_match) {
                                this_ptr = _builder->CreateStructGEP(
                                    _context->get_llvm_type(cur_st),
                                    this_ptr,
                                    (unsigned)suffix_match->index,
                                    "base_suffix_fallback_ptr"
                                );
                                walk_struct = member_owner;
                                continue;
                            }
                        }

                        // Defensive fallback: imported concrete specializations may carry
                        // owner metadata that does not align with the actual lowered
                        // layout chain. If the current struct already has the field,
                        // stop here instead of failing hard.
                        cur_st = walk_struct ? walk_struct->get_struct_type() : nullptr;
                        if (cur_st && cur_st->get_member(name)) {
                            member_owner = walk_struct;
                            continue;
                        }

                        std::string lowered_fields;
                        if (cur_st) {
                            bool first = true;
                            for (auto it = cur_st->fields_begin(); it != cur_st->fields_end(); ++it) {
                                if (!first) lowered_fields += ", ";
                                lowered_fields += it->name;
                                first = false;
                            }
                        }
                        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F01E), symbol.first_lexeme(),
                            "Internal error: could not reach owning struct '{}' for member '{}' via __parent__ or __base__ chain; "
                            "the struct hierarchy is inconsistent (current struct='{}', lowered fields=[{}])",
                            {member_owner ? member_owner->get_short_name() : "?", name,
                             walk_struct ? walk_struct->get_short_name() : "?", lowered_fields});
                    }
                }
            }

            if (!walk_struct) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F01F), symbol.first_lexeme(),
                    "Internal error: could not find owning struct for member variable '{}' in __parent__ chain",
                    {name});
            }

            auto struct_type = walk_struct->get_struct_type();
            if(struct_type) {
                if(auto field = struct_type->get_member(name); field) {
                    ptr = _builder->CreateStructGEP(
                            _context->get_llvm_type(struct_type),
                            this_ptr,
                            (unsigned)field->index,
                            "this_" + walk_struct->get_short_name() + "_" + name + "_ptr"
                    );
                } else {
                    throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F01E), symbol.first_lexeme(),
                        "Internal error: struct '{}' has no member named '{}'; "
                        "the model is inconsistent — the member was not found during code generation",
                        {struct_type->name(), name});
                }
            } else { // TODO add here the method resolution
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F01F), symbol.first_lexeme(),
                    "Internal error: struct has no LLVM type information when accessing member '{}'; "
                    "the declaration pass must be run before the implementation pass",
                    {name});
            }

        // Step 1: Variables/parameters: load alloca or global, GEP for member variables
        } else {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F020), symbol.first_lexeme(),
                "Internal error: unsupported variable definition kind encountered while generating code for symbol '{}'; "
                "only parameters, global variables, local variables and member variables are supported",
                {var_def->get_fq_name()});
        }

        // Handle type of symbol
        auto var_type = var_def->get_type();
        llvm::Type* type = _context->get_llvm_type(var_type);
        if (!type) {
            auto sym_type = symbol.get_type();
            if (sym_type && (type::is_reference(sym_type) || type::is_drain(sym_type))) {
                sym_type = sym_type->get_subtype();
            }
            if (sym_type) {
                type = _context->get_llvm_type(sym_type);
                if (type) {
                    var_type = sym_type;
                }
            }
        }
        if (!type && var_type &&
            (type::is_pointer(var_type) || type::is_link(var_type) || type::is_view(var_type) || type::is_owner(var_type))) {
            // Pointer-like values keep an ABI-level opaque pointer representation
            // even when the pointee model type is unresolved.
            type = llvm::PointerType::get(**_context, 0);
        }

        if(ptr && type) {
            if (type::is_reference(var_type) || type::is_drain(var_type)) {
                // Type is a reference/drain (indirection), so value is loaded from the alloca
                _value = _builder->CreateLoad(type, ptr, name + "_ref");
            } else {
                // Value of a symbol (as a reference) is always its address.
                // This includes function_reference_type variables: the caller that needs
                // the actual function pointer (e.g. indirect call) must load from this address.
                _value = ptr;
            }
        } else if (ptr && !type) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F020), symbol.first_lexeme(),
                "Internal error: cannot map variable '{}' type '{}' to LLVM while generating symbol access",
                {var_def->get_fq_name(), var_type ? var_type->to_string() : "<null>"});
        }

    } else if (symbol.is_function()) {
        auto func = symbol.get_function();

        // Step 2: Function symbols: get the LLVM function pointer
        // Find the function definition
        auto it = _context->_functions.find(func);
        if(it==_context->_functions.end()) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F021), symbol.first_lexeme(),
                "Internal error: LLVM declaration not found for function '{}'; "
                "the declaration pass must be run before the implementation pass",
                {func ? func->get_fq_name() : "<null>"});
        }
        llvm::Function* llvm_func = it->second;
        if(!llvm_func) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F022), symbol.first_lexeme(),
                "Internal error: LLVM function object is null for '{}'; "
                "this indicates a compiler bug in the declaration pass",
                {func ? func->get_fq_name() : "<null>"});
        }
        _value = llvm_func;
    } else if (symbol.is_enum_entry()) {
        // Enum entry: generate a constant integer value with the enum's underlying LLVM type.
        auto& target = symbol.get_enum_entry();
        auto en = target.enum_def;
        auto& entry = en->entries()[target.entry_index];
        auto et = en->get_enum_type();
        llvm::Type* llvm_ty = (et && et->get_llvm_type())
            ? et->get_llvm_type()
            : llvm::Type::getInt32Ty(_builder->getContext());
        _value = llvm::ConstantInt::get(llvm_ty, static_cast<uint64_t>(entry.value), /*isSigned=*/entry.value < 0);
    } else if (symbol.is_annotation_type_rtti()) {
        // AnnotationName::annotation → pointer to the RTTI global (AnnotationType instance).
        auto& target = symbol.get_annotation_type_rtti();
        auto ann = target.ann_type;
        std::string rtti_name = mangler::mangle_rtti(ann->get_name());

        // Step 3: Member function pointers: create a {funcptr, adjustment} pair
        // Try to find the RTTI global in the current module (defined locally or already declared).
        llvm::GlobalVariable* rtti_gv = _context->module().getNamedGlobal(rtti_name);

        // Step 4: Handle 'this' parameter specially
        // If not present, declare it as an external global (the annotation was imported).
        if (!rtti_gv) {
            llvm::Type* ptr_ty = llvm::PointerType::get(**_context, 0);
            rtti_gv = new llvm::GlobalVariable(
                _context->module(), ptr_ty,
                /*isConstant=*/true,
                llvm::GlobalValue::ExternalLinkage,
                nullptr, rtti_name);
        }
        // The expression value is the address of the RTTI global (used as a const reference).
        _value = rtti_gv;
    }
    // TODO Support other types of symbols, not only variables and functions
}

//
// Unary expression
//


} // namespace k::model::gen
