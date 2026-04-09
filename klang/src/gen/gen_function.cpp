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
// gen_function.cpp — Code generation for K language functions (including constructor/destructors).
//
// This file contains all visitor method overrides and helper functions
// related to the 'function' and derivated feature:

#include "resolvers.hpp"
#include "generators.hpp"
#include "gen_helpers.hpp"

#include "../model/expressions.hpp"
#include "../model/statements.hpp"
#include "../model/imported.hpp"
#include "../model/mangler.hpp"
#include "../parse/ast.hpp"

#include <llvm/IR/Verifier.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>

#include <algorithm>
#include <cctype>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include "../errors.hpp"


namespace k::model::gen {



void emit_vptr_store(llvm::IRBuilder<>& builder,
                     klass& st,
                     llvm::Value* this_ptr,
                     std::shared_ptr<context> ctx) {
    if (!st.has_vtable()) return;

    auto vt = st.get_vtable();
    if (!vt->llvm_global) return;

    auto struct_llvm_type = st.get_struct_type()->get_llvm_type();
    if (!struct_llvm_type) return;

    llvm::LLVMContext& llvm_ctx = **ctx;
    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    // Set the primary vptr (field 0) of the current object to point to this class's vtable.
    llvm::Value* vptr_addr = builder.CreateStructGEP(struct_llvm_type, this_ptr, 0, "__vptr_addr__");
    builder.CreateStore(vt->llvm_global, vptr_addr);

    // For classes with base sub-objects that have vtables, also update the base sub-object's
    // vptr to point to this class's vtable (K uses single virtual inheritance: one vtable per
    // complete object).  We walk all direct base sub-objects recursively.
    // Note: interfaces are class-like (they have vtables) even though is_class() returns false.
    // imported_aggregate variants are covered via is_class() overrides.
    auto is_class_like = [](const aggregate& a) {
        return a.is_class() || std::dynamic_pointer_cast<const interface>(a.shared_as<const element>()) != nullptr;
    };

    // Helper: look up the secondary vtable for `target` in `st`'s secondary vtable list.
    // Also searches the LLVM module for secondary vtable globals for imported bases
    // (named "__vtable_<ConcreteClass>_for_<Base>__" by the declaration pass).
    auto find_secondary_vtable = [&](const aggregate& target) -> llvm::GlobalVariable* {
        // First: check local secondary vtables (for local klass/interface bases)
        for (auto& [base_agg, sec_vt] : st.get_secondary_vtables()) {
            if (base_agg.get() == &target && sec_vt && sec_vt->llvm_global) {
                return sec_vt->llvm_global;
            }
        }
        // Second: check the LLVM module for secondary vtable globals created by the
        // declaration pass for imported bases.
        // Name convention: mangler::mangle_vtable(st) + "_for_" + target.get_short_name()
        std::string sec_name = mangler::mangle_vtable(st.get_name()) + "_for_" + target.get_short_name();
        return ctx->module().getNamedGlobal(sec_name);
    };

    std::function<void(aggregate& base_st, llvm::Value* base_ptr)> update_base_vptrs;
    update_base_vptrs = [&](aggregate& base_st, llvm::Value* base_ptr) {
        if (!is_class_like(base_st) || !base_st.has_vtable()) return;
        auto base_llvm_type = base_st.get_struct_type()->get_llvm_type();
        if (!base_llvm_type) return;

        // Determine which vtable to write: prefer the secondary vtable for this
        // base type as registered in the most-derived class (`st`).  Fall back to
        // the primary vtable only if no secondary spec exists.
        llvm::GlobalVariable* vtable_to_write = find_secondary_vtable(base_st);
        if (!vtable_to_write) vtable_to_write = vt->llvm_global;

        // Overwrite the base's vptr (field 0)
        llvm::Value* base_vptr_addr = builder.CreateStructGEP(base_llvm_type, base_ptr, 0,
                                                               base_st.get_short_name() + "_vptr_addr");
        builder.CreateStore(vtable_to_write, base_vptr_addr);

        // Recurse into base's embedded non-virtual sub-objects
        for (auto& bs : base_st.get_bases()) {
            if (!bs.base || !is_class_like(*bs.base) || bs.is_virtual) continue;
            std::string subobj_name = "__base_" + bs.sanitised_name() + "__";
            auto field_opt = base_st.get_struct_type()->get_member(subobj_name);
            if (!field_opt) continue;
            llvm::Value* sub_ptr = builder.CreateStructGEP(base_llvm_type, base_ptr,
                (unsigned)field_opt->index, subobj_name + "_ptr");
            update_base_vptrs(*bs.base, sub_ptr);
        }
    };

    for (auto& bs : st.get_bases()) {
        if (!bs.base || !is_class_like(*bs.base)) continue;
        std::string subobj_name = bs.is_virtual
            ? "__vbase_" + bs.sanitised_name() + "__"
            : "__base_" + bs.sanitised_name() + "__";
        auto field_opt = st.get_struct_type()->get_member(subobj_name);
        if (!field_opt) continue;
        llvm::Value* sub_ptr = builder.CreateStructGEP(struct_llvm_type, this_ptr,
            (unsigned)field_opt->index, subobj_name + "_ptr");
        update_base_vptrs(*bs.base, sub_ptr);
    }

    // ── Update vptrs of transitively-collected virtual bases ─────────────────
    {
        auto kl_ptr = dynamic_cast<klass*>(&st);
        if (kl_ptr) {
            auto vbases = st.get_all_virtual_base_structs();
            for (auto& vbase : vbases) {
                if (!is_class_like(*vbase)) continue;
                std::string vbase_field_name = "__vbase_" + vbase->get_short_name() + "__";
                auto vbase_field = st.get_struct_type()->get_member(vbase_field_name);
                if (!vbase_field) continue;  // This class is not the collector

                llvm::Value* vbase_ptr = builder.CreateStructGEP(
                    struct_llvm_type, this_ptr,
                    (unsigned)vbase_field->index,
                    "vbase_" + vbase->get_short_name() + "_for_vptr");
                update_base_vptrs(*vbase, vbase_ptr);
            }
        }
    }
}

//
// Function parameter
//
void symbol_resolver::visit_parameter(parameter& param) {
    visit_named_element(param);

    if(auto expr = param.get_init_expr()) {
        expr->accept(*this);
    }
    if(auto expr = param.get_default_expr()) {
        expr->accept(*this);
    }

    // ── Resolve annotation instances and validate @Target for parameters ──
    if (!param.get_annotations().empty()) {
        lex::opt_any_lexeme param_lexeme;
        if (auto ast_ps = param.get_ast_parameter_spec()) {
            if (ast_ps->name) param_lexeme = lex::any_lexeme{*ast_ps->name};
        }
        resolve_and_validate_annotations(param, param, param.get_short_name(), param_lexeme, "PARAMETER");
    }
}

void signature_resolver::visit_parameter(parameter& param) {
    if (auto var_type = param.get_type(); !type::is_resolved(var_type)) {
        // Handle unresolved_function_ref_type (function pointer/pin/link type)
        if (auto ufrt = std::dynamic_pointer_cast<unresolved_function_ref_type>(var_type)) {
            // Function ref type resolution requires scope lookup and function matching.
            // Leave it for the full type_reference_resolver pass.
            return;
        }

        std::shared_ptr<type> res_type = _context->resolve_type(var_type);
        if (!type::is_resolved(res_type)) {
            // Fallback for composite types wrapping an imported aggregate
            // (e.g. reference_type(unresolved("ns::Type"))).
            // Peel wrappers, resolve the inner aggregate from imports, then re-wrap.
            enum class WrapKind { Ref, Ptr, Link, View, Const, Owner, Drain };
            std::vector<WrapKind> wrappers;
            auto inner = var_type;
            while (inner && !std::dynamic_pointer_cast<unresolved_type>(inner)) {
                if      (type::is_reference(inner))  wrappers.push_back(WrapKind::Ref);
                else if (type::is_pointer(inner))    wrappers.push_back(WrapKind::Ptr);
                else if (type::is_link(inner))       wrappers.push_back(WrapKind::Link);
                else if (type::is_view(inner))     wrappers.push_back(WrapKind::View);
                else if (type::is_const(inner))      wrappers.push_back(WrapKind::Const);
                else if (type::is_owner(inner))      wrappers.push_back(WrapKind::Owner);
                else if (type::is_drain(inner))      wrappers.push_back(WrapKind::Drain);
                else break;
                inner = inner->get_subtype();
            }
            if (auto unres = std::dynamic_pointer_cast<unresolved_type>(inner);
                unres && !unres->type_id().empty())
            {
                if (auto imp_agg = _unit.get_or_create_imported_aggregate(unres->type_id(), _context)) {
                    res_type = imp_agg->get_struct_type();
                    for (auto it = wrappers.rbegin(); it != wrappers.rend(); ++it) {
                        switch (*it) {
                            case WrapKind::Ref:   res_type = res_type->get_reference(); break;
                            case WrapKind::Ptr:   res_type = res_type->get_pointer();   break;
                            case WrapKind::Link:  res_type = res_type->get_link();      break;
                            case WrapKind::View:   res_type = res_type->get_view();    break;
                            case WrapKind::Const: res_type = res_type->get_const();     break;
                            case WrapKind::Owner: res_type = res_type->get_owner();     break;
                            case WrapKind::Drain: res_type = res_type->get_drain();     break;
                        }
                    }
                }
            }
        }
        if (type::is_resolved(res_type)) {
            param.set_type(res_type);
        }
        // If still unresolved, silently leave it — the full type_reference_resolver
        // pass will either resolve it or emit the proper error.
    }
    // Do NOT process init/default expressions — those need expression visitors
    // which are only available in the full type_reference_resolver pass.
}

void type_reference_resolver::visit_parameter(parameter& param) {

    if (auto var_type = param.get_type(); !type::is_resolved(var_type)) {
        // Extract lexeme from AST parameter_spec for error reporting
        lex::opt_any_lexeme param_lexeme;
        if (auto ast_ps = param.get_ast_parameter_spec(); ast_ps && ast_ps->name) {
            param_lexeme = lex::any_lexeme{*ast_ps->name};
        }
        // Handle unresolved_function_ref_type (function pointer/pin/link type)
        if (auto ufrt = std::dynamic_pointer_cast<unresolved_function_ref_type>(var_type)) {
            auto resolved = resolve_function_ref_type(ufrt, param);
            if (resolved && type::is_resolved(resolved)) {
                param.set_type(resolved);
            } else {
                throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_PARAM_VOID_NOT_ALLOWED), param_lexeme,
                    "Cannot resolve function reference type for parameter '{}'",
                    {param.get_short_name()});
            }
        } else {
        std::shared_ptr<type> res_type = _context->resolve_type(var_type);
        if (!type::is_resolved(res_type)) {
            // Fallback for composite types wrapping an imported aggregate
            // (e.g. reference_type(unresolved("ns::Type"))).
            // Peel wrappers, resolve the inner aggregate from imports, then re-wrap.
            enum class WrapKind { Ref, Ptr, Link, View, Const, Owner, Drain };
            std::vector<WrapKind> wrappers;
            auto inner = var_type;
            while (inner && !std::dynamic_pointer_cast<unresolved_type>(inner)) {
                if      (type::is_reference(inner))  wrappers.push_back(WrapKind::Ref);
                else if (type::is_pointer(inner))    wrappers.push_back(WrapKind::Ptr);
                else if (type::is_link(inner))       wrappers.push_back(WrapKind::Link);
                else if (type::is_view(inner))     wrappers.push_back(WrapKind::View);
                else if (type::is_const(inner))      wrappers.push_back(WrapKind::Const);
                else if (type::is_owner(inner))      wrappers.push_back(WrapKind::Owner);
                else if (type::is_drain(inner))      wrappers.push_back(WrapKind::Drain);
                else break;
                inner = inner->get_subtype();
            }
            if (auto unres = std::dynamic_pointer_cast<unresolved_type>(inner);
                unres && !unres->type_id().empty())
            {
                if (auto imp_agg = _unit.get_or_create_imported_aggregate(unres->type_id(), _context)) {
                    res_type = imp_agg->get_struct_type();
                    for (auto it = wrappers.rbegin(); it != wrappers.rend(); ++it) {
                        switch (*it) {
                            case WrapKind::Ref:   res_type = res_type->get_reference(); break;
                            case WrapKind::Ptr:   res_type = res_type->get_pointer();   break;
                            case WrapKind::Link:  res_type = res_type->get_link();      break;
                            case WrapKind::View:   res_type = res_type->get_view();    break;
                            case WrapKind::Const: res_type = res_type->get_const();     break;
                            case WrapKind::Owner: res_type = res_type->get_owner();     break;
                            case WrapKind::Drain: res_type = res_type->get_drain();     break;
                        }
                    }
                }
            }
        }
        if (!type::is_resolved(res_type)) {
            throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_PARAM_VOID_NOT_ALLOWED), param_lexeme,
                "Cannot resolve type for parameter '{}': the type name is unknown",
                {param.get_short_name()});
        }
        param.set_type(res_type);
        } // end else (not unresolved_function_ref_type)
    }

    if(auto expr = param.get_init_expr()) {
        expr->accept(*this);
    }

    if(auto expr = param.get_default_expr()) {
        expr->accept(*this);
        // Adapt type of default expression to parameter type
        auto cast = adapt_type(expr, param.get_type());
        if(cast && cast != expr) {
            param.set_default_expr(cast);
        }
    }
}

//
// Function
//

void symbol_resolver::visit_function(function& fn) {
    // Skip template definitions — they are not instantiated yet.
    if (fn.is_template()) {
        trace("[symbol_resolver::visit_function] skipping template '{}'", {fn.get_short_name()});
        return;
    }
    trace("[symbol_resolver::visit_function] '{}'", {fn.get_short_name()});
    visit_named_element(fn);

    if (fn.is_member() && !fn.is_static()) {
        fn.create_this_parameter();
    }

    for(auto param : fn.parameters()) {
        param->accept(*this);
    }

    // ── Resolve annotation instances and validate @Target ─────────────────
    if (!fn.get_annotations().empty()) {
        lex::opt_any_lexeme fn_lexeme;
        if (auto ast_fd = fn.get_ast_function_decl()) fn_lexeme = lex::any_lexeme{ast_fd->name};

        // Constructors use "CONSTRUCTOR" element kind; all other functions use "FUNCTION".
        std::string element_kind = "FUNCTION";
        if (std::dynamic_pointer_cast<constructor>(fn.shared_as<function>())) {
            element_kind = "CONSTRUCTOR";
        }
        resolve_and_validate_annotations(fn, fn, fn.get_short_name(), fn_lexeme, element_kind);

        // ── Warn about RUNTIME annotations on non-public functions ────────
        // Non-public functions have no RTTI, so RUNTIME annotations won't be
        // accessible at runtime.  Public constructors now have Constructor RTTI,
        // so they are included in the has_rtti check.
        bool has_rtti = (fn.get_visibility() == PUBLIC)
                        && !std::dynamic_pointer_cast<destructor>(fn.shared_as<function>())
                        && !std::dynamic_pointer_cast<static_constructor>(fn.shared_as<function>())
                        && !std::dynamic_pointer_cast<static_destructor>(fn.shared_as<function>());
        if (!has_rtti) {
            for (auto& ann_inst : fn.get_annotations()) {
                if (!ann_inst.resolved_type) continue;
                if (!ann_inst.resolved_type->is_source_retention()) {
                    std::string reason;
                    if (fn.get_visibility() != PUBLIC) {
                        reason = "non-public functions have no RTTI";
                    } else {
                        reason = "destructors have no RTTI";
                    }
                    warn(static_cast<unsigned int>(k::diag::symbol_diag::WARN_UNUSED_PRIVATE_CTOR), fn_lexeme,
                        "RUNTIME annotation '@{}' on '{}' will not be accessible at runtime; {}",
                        {ann_inst.raw_name, fn.get_short_name(), reason});
                }
            }
        }

        // ── Process @k::ffi::Extern annotation ───────────────────────────
        for (auto& ann_inst : fn.get_annotations()) {
            if (!ann_inst.resolved_type) continue;
            std::string fq = ann_inst.resolved_type->get_fq_name();
            if (fq != "k::ffi::Extern" && fq != "::k::ffi::Extern") continue;

            // Helper: extract a string literal from an AST expression.
            // Returns empty optional if the expression is null/null literal/not a string literal.
            auto extract_string = [](const std::shared_ptr<k::parse::ast::expression>& expr)
                    -> std::optional<std::string> {
                if (!expr) return std::nullopt;
                if (auto lit = std::dynamic_pointer_cast<k::parse::ast::literal_expr>(expr)) {
                    // null literal → nullopt
                    if (std::holds_alternative<lex::null>(lit->literal)) return std::nullopt;
                    if (auto* s = lit->literal.get_if<lex::string>()) {
                        return std::get<std::string>(s->value());
                    }
                }
                return std::nullopt;
            };

            // Extract parameters from the annotation AST node
            std::optional<std::string> language;
            std::optional<std::string> library;
            std::optional<std::string> symbol;

            if (ann_inst.ast_node->has_parens) {
                // Positional form: @Extern("C") or @Extern("C", "lib", "sym")
                auto& args = ann_inst.ast_node->args;
                if (args.size() >= 1) language = extract_string(args[0]);
                if (args.size() >= 2) library  = extract_string(args[1]);
                if (args.size() >= 3) symbol   = extract_string(args[2]);
            } else if (ann_inst.ast_node->brace_init && ann_inst.ast_node->brace_init->is_designated) {
                // Designated form: @Extern{.language="C", .symbol="custom"}
                for (auto& elem : ann_inst.ast_node->brace_init->elements) {
                    auto des = std::dynamic_pointer_cast<k::parse::ast::designated_init_element>(elem);
                    if (!des) continue;
                    std::string member{des->member_name.content};
                    if (member == "language") language = extract_string(des->value);
                    else if (member == "library") library = extract_string(des->value);
                    else if (member == "symbol") symbol = extract_string(des->value);
                }
            }

            // Validate language parameter (mandatory, case-insensitive, only "C" supported)
            if (!language.has_value() || language->empty()) {
                throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_ANNOTATION_MISMATCH), fn_lexeme,
                    "@ffi::Extern on '{}': 'language' parameter is required and must not be empty",
                    {fn.get_short_name()});
            }
            std::string lang_lower = language.value();
            std::transform(lang_lower.begin(), lang_lower.end(), lang_lower.begin(),
                [](unsigned char c) { return std::tolower(c); });
            if (lang_lower != "c") {
                throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_ANNOTATION_MISMATCH), fn_lexeme,
                    "@ffi::Extern on '{}': unsupported language '{}'; only \"C\" is currently supported",
                    {fn.get_short_name(), language.value()});
            }

            // Warn if 'library' is specified (not yet used for C FFI)
            if (library.has_value()) {
                warn(static_cast<unsigned int>(k::diag::function_diag::WARN_FUNC_BODY_IGNORED), fn_lexeme,
                    "@ffi::Extern on '{}': 'library' parameter is not yet used for language \"C\" and will be ignored",
                    {fn.get_short_name()});
            }

            // Validate: non-static member methods cannot be @Extern
            if (fn.is_member() && !fn.is_static()) {
                throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_ABSTRACT_HAS_BODY), fn_lexeme,
                    "@ffi::Extern on '{}': only global or static functions can be declared as FFI extern; "
                    "non-static member functions are not supported",
                    {fn.get_short_name()});
            }

            // Validate: @Extern function must not have a body
            if (fn.get_ast_function_decl() && fn.get_ast_function_decl()->content) {
                throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_ANNOTATION_MISMATCH), fn_lexeme,
                    "@ffi::Extern function '{}' must not have a body; "
                    "remove the body or the @ffi::Extern annotation",
                    {fn.get_short_name()});
            }

            // Validate: @Extern + abstract is not allowed
            if (fn.is_abstract_func()) {
                throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_ANNOTATION_MISMATCH), fn_lexeme,
                    "Function '{}' cannot be both @ffi::Extern and abstract",
                    {fn.get_short_name()});
            }

            // Determine the C symbol name
            std::string c_symbol = symbol.value_or(fn.get_short_name());
            fn.set_extern_c_symbol(c_symbol);
            // Re-compute the mangled name now that _is_extern and _extern_c_symbol are set.
            fn.update_mangled_name();

            // Only one @Extern annotation makes sense — stop after the first one
            break;
        }
    }

    // ── Process @k::ffi::CString on parameters ───────────────────────────
    // Validate that each @CString-annotated parameter belongs to an @Extern("C")
    // function and that its K type is an addresser (ref, ptr, view, link or owner)
    // to char.  Drain (#) triggers a warning and is treated as reference.
    // unsigned char triggers a warning.  Any other addressed type is rejected.
    for (auto& param : fn.parameters()) {
        for (auto& ann_inst : param->get_annotations()) {
            if (!ann_inst.resolved_type) continue;
            std::string fq = ann_inst.resolved_type->get_fq_name();
            if (fq != "k::ffi::CString" && fq != "::k::ffi::CString") continue;

            lex::opt_any_lexeme param_lexeme;
            if (auto ast_ps = param->get_ast_parameter_spec()) {
                if (ast_ps->name) param_lexeme = lex::any_lexeme{*ast_ps->name};
            }

            // 1. The owning function must be @Extern("C")
            if (!fn.is_extern()) {
                throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_PARAM_TYPE_UNRESOLVED), param_lexeme,
                    "@ffi::CString on parameter '{}' of function '{}': "
                    "@ffi::CString is only valid on parameters of @ffi::Extern(\"C\") functions",
                    {param->get_short_name(), fn.get_short_name()});
            }

            // 2. Validate the parameter type
            auto ptype = param->get_type();
            if (!ptype || !type::is_resolved(ptype)) break; // unresolved — skip silently

            // 2a. Strip const wrappers
            auto inner = ptype;
            while (type::is_const(inner)) inner = inner->get_subtype();

            // 2b. Check for drain — warn and peel
            if (type::is_drain(inner)) {
                warn(static_cast<unsigned int>(k::diag::function_diag::WARN_PARAM_DRAIN_NON_STRUCT), param_lexeme,
                    "@ffi::CString on parameter '{}': drain indirection (#) is not meaningful "
                    "for C FFI; treating as reference",
                    {param->get_short_name()});
                inner = inner->get_subtype();
            }
            // 2c. Check for an addresser (reference, pointer, view, link, owner)
            else if (type::is_reference(inner) || type::is_pointer(inner)
                  || type::is_view(inner) || type::is_link(inner)
                  || type::is_owner(inner)) {
                inner = inner->get_subtype();
            } else {
                throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_PARAM_DRAIN_MUST_BE_LAST), param_lexeme,
                    "@ffi::CString on parameter '{}': the parameter type must be an addresser "
                    "(reference, pointer, view, link or owner) to char, but got '{}'",
                    {param->get_short_name(), ptype->to_string()});
            }

            // 2d. Strip const again (for `const char&` patterns)
            while (type::is_const(inner)) inner = inner->get_subtype();

            // 2e. Leaf type must be char (or unsigned char with warning)
            auto prim = std::dynamic_pointer_cast<primitive_type>(inner);
            if (!prim) {
                throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_PARAM_DEFAULT_TYPE_MISMATCH), param_lexeme,
                    "@ffi::CString on parameter '{}': the addressed type must be char, "
                    "but got '{}'",
                    {param->get_short_name(), inner->to_string()});
            } else if (prim->get_type() == primitive_type::BYTE) {
                // BYTE == UNSIGNED_CHAR
                warn(static_cast<unsigned int>(k::diag::function_diag::WARN_PARAM_DEFAULT_NARROWING), param_lexeme,
                    "@ffi::CString on parameter '{}': unsigned char will be treated "
                    "as char for C FFI",
                    {param->get_short_name()});
            } else if (prim->get_type() != primitive_type::CHAR) {
                throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_PARAM_DEFAULT_TYPE_MISMATCH), param_lexeme,
                    "@ffi::CString on parameter '{}': the addressed type must be char, "
                    "but got '{}'",
                    {param->get_short_name(), inner->to_string()});
            }

            // 3. Mark the parameter
            param->set_ffi_cstring(true);

            // Only process the first @CString occurrence per parameter
            break;
        }
    }

    // Resolve redirect target if this is a redirected function
    if (fn.is_redirected()) {
        auto target_name = fn.get_redirect_target_name();
        auto result = resolve_symbol(fn, target_name);
        if (auto* target_fn = std::get_if<std::shared_ptr<function>>(&result)) {
            fn.set_redirect_target(*target_fn);
        } else {
            lex::opt_any_lexeme fn_lexeme;
            if (auto ast_fd = fn.get_ast_function_decl()) fn_lexeme = lex::any_lexeme{ast_fd->name};
            throw_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_FUNC_RETURN_UNRESOLVED), fn_lexeme,
                "Function redirector '{}' targets '{}', which could not be resolved to a function",
                {fn.get_short_name(), target_name.to_string()});
        }
        // No body to visit for a redirect
        return;
    }

    _function_stack.push_back(fn.shared_as<function>());
    if(auto block = fn.get_block()) {
        visit_block(*block);
    }
    _function_stack.pop_back();
}

void signature_resolver::visit_function(function& fn) {
    // Skip template definitions — they are not instantiated yet.
    if (fn.is_template()) return;

    // Resolve this-parameter type
    if (fn.is_member() && !fn.is_static()) {
        if (auto this_param = fn.get_this_parameter()) {
            this_param->accept(*this);
        }
    }

    // Resolve regular parameter types
    for (auto param : fn.parameters()) {
        param->accept(*this);
    }

    // Resolve return type if still unresolved
    if (fn.get_return_type() && !type::is_resolved(fn.get_return_type())) {
        auto resolved = _context->resolve_type(fn.get_return_type());
        if (resolved && type::is_resolved(resolved)) {
            fn.set_return_type(resolved);
        }
    }

    // Do NOT visit the function body — that is the job of type_reference_resolver.
}

void type_reference_resolver::visit_function(function& fn) {
    // Skip template definitions — they are not instantiated yet.
    if (fn.is_template()) {
        trace("[type_reference_resolver::visit_function] skipping template '{}'", {fn.get_short_name()});
        return;
    }
    trace("[type_reference_resolver::visit_function] '{}'", {fn.get_short_name()});

    if (fn.is_member() && !fn.is_static()) {
        fn.get_this_parameter()->accept(*this);
    }

    for(auto param : fn.parameters()) {
        param->accept(*this);
    }

    // Resolve return type if still unresolved (e.g. member functions returning StructType&)
    if (fn.get_return_type() && !type::is_resolved(fn.get_return_type())) {
        auto resolved = _context->resolve_type(fn.get_return_type());
        if (resolved && type::is_resolved(resolved)) {
            fn.set_return_type(resolved);
        }
    }

    // Redirected functions have no body to visit
    if (fn.is_redirected()) {
        return;
    }

    _function_stack.push_back(fn.shared_as<function>());
    if(auto block = fn.get_block()) {
        visit_block(*block);
    }
    _function_stack.pop_back();
}

void declaration_generator::visit_function(function &function) {
    // Skip template definitions — they are not instantiated yet.
    if (function.is_template()) {
        trace("[declaration_generator::visit_function] skipping template '{}'", {function.get_short_name()});
        return;
    }
    trace("[declaration_generator::visit_function] '{}'", {function.get_short_name()});
    // Deleted constructors must never be called; do not emit any LLVM declaration for them.
    if (function.is_deleted()) {
        return;
    }
    // Abstract functions have no body and must not be materialized (similar to deleted).
    if (function.is_abstract_func()) {
        return;
    }

    // Redirected functions: skip normal declaration, alias is created after all declarations
    if (function.is_redirected()) {
        return;
    }

    // Extern functions: emit a bare LLVM declaration with C linkage (no K mangling).
    // The symbol name is determined by the @k::ffi::Extern annotation, resolved
    // at link time from an external library.
    if (function.is_extern()) {
        std::vector<llvm::Type*> param_types;
        for (const auto& param : function.parameters()) {
            auto ptype = _context->get_llvm_type(param->get_type());
            if (!ptype) return;
            param_types.push_back(ptype);
        }
        llvm::Type* ret_type = nullptr;
        if (const auto& ret = function.get_return_type()) {
            ret_type = _context->get_llvm_type(ret);
        } else {
            ret_type = llvm::Type::getVoidTy(**_context);
        }
        llvm::FunctionType* func_type = llvm::FunctionType::get(ret_type, param_types, false);
        // Use the mangled name — for extern functions this is the C symbol name.
        llvm::Function* func = llvm::Function::Create(
            func_type, llvm::Function::ExternalLinkage,
            function.get_mangled_name(), *_context->_module);
        _context->_functions.insert({function.shared_as<k::model::function>(), func});
        return;
    }

    // Deferred body-check: if the function has no body and is not abstract, extern,
    // deleted, redirected, or defaulted, it is an error. This catches functions that
    // bypassed the model_builder body check because they carried annotations but those
    // annotations did not mark the function as extern.
    if (function.get_ast_function_decl() && !function.get_ast_function_decl()->content
        && !function.is_abstract_func() && !function.is_extern()
        && !function.is_deleted() && !function.is_redirected()
        && !function.is_defaulted()) {
        lex::opt_any_lexeme fn_lexeme;
        if (auto ast_fd = function.get_ast_function_decl()) fn_lexeme = lex::any_lexeme{ast_fd->name};
        throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_INTERFACE_NOT_IMPLEMENTED), fn_lexeme,
            "Function '{}' has no body; a function body is required unless the function is abstract, "
            "declared inside an interface, or annotated with @ffi::Extern",
            {function.get_fq_name()});
    }

    // Parameter types:
    std::vector<llvm::Type*> param_types;
    if (function.is_member()  && !function.is_static()) {
        // First parameter is the 'this' pointer
        auto this_param = function.get_this_parameter();
        if (!this_param || !this_param->get_type()) {
            // Cannot emit declaration without a valid 'this' parameter type.
            // This can happen for imported methods whose struct type is not yet resolved.
            return;
        }
        param_types.push_back(_context->get_llvm_type(this_param->get_type()));
    }
    for(const auto& param : function.parameters()) {
        auto ptype = _context->get_llvm_type(param->get_type());
        if (!ptype) {
            // Skip declaration if any parameter type is unresolved.
            return;
        }
        param_types.push_back(ptype);
    }

    // Return type, if any:
    llvm::Type* ret_type = nullptr;
    bool use_sret = false;
    if(const auto& ret = function.get_return_type()) {
        if (needs_sret_return(ret)) {
            // sret ABI: prepend a pointer parameter for the return value, actual return is void
            param_types.insert(param_types.begin(), llvm::PointerType::get(**_context, 0));
            ret_type = llvm::Type::getVoidTy(**_context);
            use_sret = true;
        } else {
            ret_type = _context->get_llvm_type(ret);
        }
    } else {
        ret_type = llvm::Type::getVoidTy(**_context);
    }

    // create the function:
    llvm::FunctionType *func_type = llvm::FunctionType::get(ret_type, param_types, false);
    llvm::Function *func = llvm::Function::Create(func_type, llvm::Function::ExternalLinkage, function.get_mangled_name(), *_context->_module);

    if (use_sret) {
        // Mark the first parameter with the StructRet attribute
        func->addParamAttr(0, llvm::Attribute::get(**_context, llvm::Attribute::StructRet,
            _context->get_llvm_type(function.get_return_type())));
    }

    _context->_functions.insert({function.shared_as<k::model::function>(), func});

    // Declare content (only if there is a block — defaulted constructors may have no body yet)
    if (auto blck = function.get_block()) {
        blck->accept(*this);
    }
}

/**
 * Generate LLVM IR for a function body.
 *
 * Steps:
 *   1. Resolve the LLVM Function from the context.
 *   2. Create entry basic block and reset per-function state.
 *   3. Handle sret (structure-return) ABI: add sret parameter.
 *   4. Perform NRVO analysis (Named Return Value Optimization).
 *   5. Allocate parameters and store incoming values.
 *   6. For constructors: emit pre-block IR (zero-init, parent ptr, copy ctor).
 *   7. Visit the function body block.
 *   8. For constructors: emit post-block IR (vptr stores, virtual base init).
 *   9. For destructors: emit cleanup (member/base destructor calls).
 *   10. Emit function epilogue (return, cleanup, dead instruction elimination).
 */
void implementation_generator::visit_function(function &function) {
    // Skip template definitions — they are not instantiated yet.
    if (function.is_template()) {
        trace("[implementation_generator::visit_function] skipping template '{}'", {function.get_short_name()});
        return;
    }
    trace("[implementation_generator::visit_function] '{}'", {function.get_short_name()});
    // Deleted functions have no LLVM declaration and must never be implemented.
    if (function.is_deleted()) {
        return;
    }
    // Abstract functions have no body and must not be materialized.
    if (function.is_abstract_func()) {
        return;
    }
    // Extern functions have no body in this module (resolved at link time).
    if (function.is_extern()) {
        return;
    }
    // Redirected functions are handled via GlobalAlias in declaration pass.
    if (function.is_redirected()) {
        return;
    }

    // Step 1: Resolve the LLVM Function from the context
    auto func_it = _context->_functions.find(function.shared_as<k::model::function>());
    if (func_it==_context->_functions.end()) {
        lex::opt_any_lexeme fn_lexeme;
        if (auto ast_fd = function.get_ast_function_decl()) fn_lexeme = lex::any_lexeme{ast_fd->name};
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F01B), fn_lexeme,
            "Internal error: LLVM function declaration not found for '{}'; "
            "the declaration pass must be run before the implementation pass",
            {function.get_fq_name()});
    }

    llvm::Function* func = func_it->second;

    // Step 2: Create entry basic block and reset per-function state
    // create the function content:
    llvm::BasicBlock *block = llvm::BasicBlock::Create(**_context, "entry", func);
    _builder->SetInsertPoint(block);

    // Reset per-function state
    _retval_alloca = nullptr;
    _sret_ptr = nullptr;
    _nrvo_candidate = nullptr;
    _sret_destination = nullptr;
    _expression_temporaries.clear();
    while (!_cleanup_blocks.empty()) _cleanup_blocks.pop();
    while (!_cleanup_vars_stack.empty()) _cleanup_vars_stack.pop();
    while (!_owner_params_stack.empty()) _owner_params_stack.pop();
    while (!_struct_params_stack.empty()) _struct_params_stack.pop();

    // Step 3: Handle sret (structure-return) ABI: add sret parameter
    // Determine if this function uses sret ABI
    const bool use_sret = function.has_return_type() && needs_sret_return(function.get_return_type());

    if (use_sret) {
        debug("[implementation_generator::visit_function] '{}' uses sret ABI", {function.get_short_name()});
        // Capture the sret argument (first LLVM argument, before 'this' or explicit params)
        auto arg_it_sret = func->arg_begin();
        llvm::Argument* sret_arg = &*(arg_it_sret);
        sret_arg->setName("sret");
        _sret_ptr = sret_arg;

        // Step 4: Perform NRVO analysis (Named Return Value Optimization)
        // Named return variable: guaranteed NRVO — skip heuristic scan
        if (function.has_named_return_var()) {
            _nrvo_candidate = function.get_named_return_var();
        } else {
        // NRVO analysis: scan all return statements in the function body.
        // If every return returns the same named local variable, it's an NRVO candidate.
        auto blk = function.get_block();
        if (blk) {
            std::shared_ptr<variable_statement> nrvo_var;
            bool nrvo_eligible = true;
            std::function<void(const k::model::block&)> scan_returns;
            scan_returns = [&](const k::model::block& b) {
                for (auto& stmt : b.get_statements()) {
                    if (auto ret = std::dynamic_pointer_cast<return_statement>(stmt)) {
                        if (auto expr = ret->get_expression()) {
                            // Check if expression is a symbol_expression referring to a local variable_statement
                            auto sym = std::dynamic_pointer_cast<symbol_expression>(expr);
                            if (!sym) {
                                // Could be wrapped in a load_value_expression or cast
                                if (auto lv = std::dynamic_pointer_cast<load_value_expression>(expr))
                                    sym = std::dynamic_pointer_cast<symbol_expression>(lv->sub_expr());
                            }
                            if (sym && sym->is_variable_def()) {
                                auto var_def = sym->get_variable_def();
                                auto var_stmt = std::dynamic_pointer_cast<variable_statement>(var_def);
                                if (var_stmt && type::is_struct(var_stmt->get_type())) {
                                    if (!nrvo_var) {
                                        nrvo_var = var_stmt;
                                    } else if (nrvo_var != var_stmt) {
                                        nrvo_eligible = false; // different variables in different returns
                                    }
                                } else {
                                    nrvo_eligible = false;
                                }
                            } else {
                                nrvo_eligible = false;
                            }
                        }
                    }
                    // Recurse into nested blocks (if-else, while, for bodies)
                    if (auto sub_block = std::dynamic_pointer_cast<k::model::block>(stmt)) {
                        scan_returns(*sub_block);
                    }
                    if (auto if_stmt = std::dynamic_pointer_cast<if_else_statement>(stmt)) {
                        if (auto then_b = std::dynamic_pointer_cast<k::model::block>(if_stmt->get_then_stmt()))
                            scan_returns(*then_b);
                        if (auto else_b = std::dynamic_pointer_cast<k::model::block>(if_stmt->get_else_stmt()))
                            scan_returns(*else_b);
                    }
                    if (auto while_stmt = std::dynamic_pointer_cast<while_statement>(stmt)) {
                        if (auto body = std::dynamic_pointer_cast<k::model::block>(while_stmt->get_nested_stmt()))
                            scan_returns(*body);
                    }
                    if (auto for_stmt = std::dynamic_pointer_cast<for_statement>(stmt)) {
                        if (auto body = std::dynamic_pointer_cast<k::model::block>(for_stmt->get_nested_stmt()))
                            scan_returns(*body);
                    }
                }
            };
            scan_returns(*blk);
            if (nrvo_eligible && nrvo_var) {
                _nrvo_candidate = nrvo_var;
            }
        }
        } // end else (heuristic NRVO when no named return var)
    }

    if (_nrvo_candidate) {
        debug("[implementation_generator::visit_function] '{}' NRVO candidate selected", {function.get_short_name()});
    }

    // If function has a non-void return type AND is NOT sret, pre-create an alloca for
    // the return value so that destructor calls can happen before the actual ret instruction.
    if (function.has_return_type() && !use_sret) {
        llvm::IRBuilder<> alloca_builder(&func->getEntryBlock(), func->getEntryBlock().begin());
        _retval_alloca = alloca_builder.CreateAlloca(
            _context->get_llvm_type(function.get_return_type()), nullptr, "retval");
    }

    // Capture arguments — for sret functions, first LLVM arg is sret, so skip it
    auto arg_it = func->arg_begin();
    if (use_sret) ++arg_it; // skip sret argument
    if (function.is_member() && !function.is_static()) {
        // First parameter is the 'this' pointer
        llvm::Argument *arg = &*(arg_it++);
        arg->setName("this");
        // Create dedicated local storage for "this" argument
        llvm::AllocaInst* alloca = _builder->CreateAlloca(llvm::PointerType::get(_context->llvm_context(), 0), nullptr, "this");
        _context->_function_this_variables.insert({function.shared_as<model::function>(), alloca});
        _context->_parameter_variables.insert({function.get_this_parameter(), alloca});
        // Read "this" param value and store it in dedicated local var
        _builder->CreateStore(arg, alloca);
    }
    for(const auto& param : function.parameters()) {
        // Iterate to get all explicit parameters
        llvm::Argument *arg = &*(arg_it++);
        arg->setName(param->get_short_name());
        // Create dedicated local storage for argument
        llvm::AllocaInst* alloca = _builder->CreateAlloca(_context->get_llvm_type(param->get_type()), nullptr, param->get_short_name());
        _context->_parameter_variables.insert({param, alloca});
        // Read param value and store it in dedicated local var
        _builder->CreateStore(arg, alloca);
    }

    // Step 5: Allocate parameters and store incoming values
    // Collect owner-typed parameters for end-of-function cleanup (destroy + free on scope exit).
    // This enables RAII semantics for owner parameters: when a function receives an owner,
    // it takes ownership, and the object is destroyed when the function returns.
    {
        std::vector<std::shared_ptr<parameter>> owner_params;
        for (const auto& param : function.parameters()) {
            if (type::is_owner(param->get_type())) {
                owner_params.push_back(param);
            }
        }
        if (!owner_params.empty()) {
            _owner_params_stack.push(owner_params);
        }
    }

    // Collect struct-typed by-value parameters that have destructors.
    // These need destructor calls at function exit (they are copies owned by the callee).
    {
        std::vector<std::shared_ptr<parameter>> struct_params;
        for (const auto& param : function.parameters()) {
            auto pt = param->get_type();
            // Skip reference, pointer, link, view, owner types — only plain struct by value
            if (type::is_reference(pt) || type::is_pointer(pt) || type::is_link(pt)
                || type::is_view(pt) || type::is_owner(pt)) continue;
            if (auto st_type = std::dynamic_pointer_cast<struct_type>(pt)) {
                if (st_type->get_struct() && st_type->get_struct()->get_destructor()) {
                    struct_params.push_back(param);
                }
            }
        }
        if (!struct_params.empty()) {
            _struct_params_stack.push(struct_params);
        }
    }

    // Step 6: For constructors: emit pre-block IR (zero-init, parent ptr, copy ctor)
    // Constructor pre-block: zero-init, parent pointer, copy ctor, standalone vbase init
    if (emit_constructor_pre_block(function, func)) {
        return; // Function fully handled (e.g. generated copy constructor)
    }

    // Compiler-generated copy assignment operator
    if (emit_copy_assignment_operator(function, func)) {
        return; // Function fully handled
    }

    // Step 7: Visit the function body block
    // Produce content
    function.get_block()->accept(*this);

    // Step 8: For constructors: emit post-block IR (vptr stores, virtual base init)
    // Constructor post-block: vptr stores + virtual base pointer initialization
    emit_constructor_post_block(function);

    // Step 9: For destructors: emit cleanup (member/base destructor calls)
    // Destructor: member and base destructor calls
    emit_destructor_cleanup(function);

    // Step 10: Emit function epilogue (return, cleanup, dead instruction elimination)
    // Epilogue: param cleanup, return, NRVO, optimization, verification
    emit_function_return_epilogue(function, func, use_sret);
}

// ── visit_function extracted helpers ──────────────────────────────────────────

/**
 * Emit constructor pre-block IR: zero-init, parent pointer store, generated copy
 * constructor memberwise copy, standalone virtual base initialization.
 *
 * Steps:
 *   1. Zero-initialize the struct via memset(this, 0, sizeof(struct)).
 *   2. Store __parent__ pointer for inner (non-static nested) structs.
 *   3. For generated copy constructors: emit memberwise copy and return true.
 *   4. For virtual base initialization: store virtual base sub-object pointers.
 *
 * @return true if the function was fully handled (e.g. generated copy ctor).
 */
bool implementation_generator::emit_constructor_pre_block(function& function, llvm::Function* func) {
    auto ctor = function.shared_as<constructor>();
    if (!ctor) return false;

    debug("[implementation_generator::emit_constructor_pre_block] constructor for '{}'", {function.get_fq_name()});

    // For constructor, start by initializing all members
    auto this_param_it = _context->_function_this_variables.find(function.shared_as<model::function>());
    auto this_param = this_param_it->second;
    auto st = ctor->get_owner();
    auto type = st->get_struct_type()->get_llvm_type();
    auto zero_init = llvm::ConstantAggregateZero::get(type);
    auto this_ptr = _builder->CreateLoad(st->get_struct_type()->get_reference()->get_llvm_type(), this_param);
    _builder->CreateStore(zero_init, this_ptr);

    // NOTE: vptr initialization (emit_vptr_store) is deferred to AFTER the block
    // (i.e., after base constructors run) so that the most-derived class's vtable
    // pointer wins. See emit_constructor_post_block.

    // For non-static inner struct constructors: store the __parent__ parameter
    // (first explicit parameter, type Outer&) into the __parent__ field (LLVM struct field index 0).
    if (st->is_inner()) {
        auto parent_param_model = ctor->get_parameter("__parent__");
        if (parent_param_model) {
            auto parent_param_alloca_it = _context->_parameter_variables.find(
                std::const_pointer_cast<parameter>(parent_param_model));
            if (parent_param_alloca_it != _context->_parameter_variables.end()) {
                auto parent_param_alloca = parent_param_alloca_it->second;
                // Load the outer struct pointer (ref = opaque ptr at LLVM level)
                auto outer_ref_llvm_type = _context->get_llvm_type(
                    st->get_enclosing_structure()->get_struct_type()->get_reference());
                auto parent_ptr_val = _builder->CreateLoad(outer_ref_llvm_type, parent_param_alloca, "parent_ref_val");
                // GEP to __parent__ field (field index 0)
                auto parent_field_ptr = _builder->CreateStructGEP(
                    _context->get_llvm_type(st->get_struct_type()),
                    this_ptr,
                    0,
                    "this_parent_field_ptr"
                );
                _builder->CreateStore(parent_ptr_val, parent_field_ptr);
            }
        }
    }

    // ── Generated copy constructor: emit memberwise copy at IR level ──────
    if (ctor->is_copy_constructor() && ctor->is_compiler_generated()) {
        // Load the 'other' parameter (first explicit param, type Struct&)
        auto other_param = ctor->get_parameter("other");
        if (other_param) {
            auto other_alloca_it = _context->_parameter_variables.find(
                std::const_pointer_cast<parameter>(other_param));
            if (other_alloca_it != _context->_parameter_variables.end()) {
                auto other_ref_type = _context->get_llvm_type(st->get_struct_type()->get_reference());
                auto other_ptr = _builder->CreateLoad(other_ref_type, other_alloca_it->second, "other_ref");
                auto st_llvm_type = _context->get_llvm_type(st->get_struct_type());

                // Copy each field by field index using GEP + memcpy approach:
                // We use a simple aggregate load/store (only valid for simple types).
                // For structs with nested struct members, we'd need to call their copy ctors —
                // but since we only generate this for trivially-copyable cases,
                // a bitwise copy (memcpy semantics) is correct.
                // Use llvm.memcpy intrinsic: copy sizeof(Struct) bytes from other to this.
                auto& dl = _context->_module->getDataLayout();
                uint64_t size = dl.getTypeAllocSize(st_llvm_type);
                _builder->CreateMemCpy(
                    this_ptr, llvm::MaybeAlign(),
                    other_ptr, llvm::MaybeAlign(),
                    _builder->getInt64(size)
                );
            }
        }
        // No user block to visit for a generated copy constructor — return immediately.
        // Add terminator and finalize.
        _builder->CreateRetVoid();
        optimize_function_dead_inst_elimination(*func);
        llvm::verifyFunction(*func);
        return true; // fully handled
    }

    // ── Standalone virtual base initialization (BEFORE block) ─────────────
    // For classes that directly declare "virtual B" (have __vbptr_X__ but no __vbase_X__
    // in their struct), create a stack alloca for each virtual base sub-object and
    // set the vbptr to it. This handles the standalone case (e.g. `B b;`).
    // When B is a sub-object of D, D's post-block code will overwrite these vbptrs
    // with the address of D's embedded __vbase_X__.
    // We place the allocas at the top of the entry block (before zero-init of 'this')
    // using a separate builder pointing to the entry block start.
    {
        auto vbases = st->get_all_virtual_base_structs();
        for (auto& vbase : vbases) {
            std::string vbase_name = "__vbase_" + vbase->get_short_name() + "__";
            std::string vbptr_name = "__vbptr_" + vbase->get_short_name() + "__";

            // Only handle classes that have __vbptr_X__ but NOT __vbase_X__
            auto vbptr_field = st->get_struct_type()->get_member(vbptr_name);
            auto vbase_field_opt = st->get_struct_type()->get_member(vbase_name);
            if (!vbptr_field || vbase_field_opt) continue;

            // Create an alloca for the virtual base at the function entry
            auto vbase_llvm_type = _context->get_llvm_type(vbase->get_struct_type());
            if (!vbase_llvm_type) continue;

            // Use a separate builder at the entry block for the alloca
            llvm::IRBuilder<> alloca_builder(&func->getEntryBlock(), func->getEntryBlock().begin());
            llvm::AllocaInst* vbase_alloca = alloca_builder.CreateAlloca(
                vbase_llvm_type, nullptr, "vbase_" + vbase->get_short_name() + "_standalone");

            // Step 1: Zero-initialize the struct via memset(this, 0, sizeof(struct))
            // Zero-initialize the alloca
            _builder->CreateStore(llvm::ConstantAggregateZero::get(vbase_llvm_type), vbase_alloca);

            // Step 2: Store __parent__ pointer for inner (non-static nested) structs
            // Store its address into the __vbptr_X__ field of 'this'
            auto st_llvm_type = _context->get_llvm_type(st->get_struct_type());
            llvm::Value* vbptr_addr = _builder->CreateStructGEP(
                st_llvm_type, this_ptr, (unsigned)vbptr_field->index,
                "vbptr_" + vbase->get_short_name() + "_standalone_addr");
            _builder->CreateStore(vbase_alloca, vbptr_addr);

            // Step 3: For generated copy constructors: emit memberwise copy and return true
            // Call the virtual base's default constructor on the alloca
            // (this handles A() : x(10) {} etc.)
            if (auto vbase_ctor_list = &vbase->constructors(); !vbase_ctor_list->empty()) {
                // Find the default (0-arg) constructor
                for (auto& vbase_ctor : *vbase_ctor_list) {
                    if (vbase_ctor->get_parameter_size() == 0 && !vbase_ctor->is_copy_constructor()) {
                        auto vbase_ctor_it = _context->_functions.find(vbase_ctor->shared_as<k::model::function>());
                        if (vbase_ctor_it != _context->_functions.end()) {
                            _builder->CreateCall(vbase_ctor_it->second, {vbase_alloca});
                        }
                        break;
                    }
                }
            }

            // Step 4: For virtual base initialization: store virtual base sub-object pointers
            // Store the alloca in context for potential use by constructor_invocation_expression
            _context->_vbase_standalone_allocas[st->shared_as<aggregate>()][vbase->get_short_name()] = vbase_alloca;
        }
    }

    return false; // not fully handled, continue with block visit
}

/**
 * Emit compiler-generated copy assignment operator (operator=) memberwise copy.
 *
 * Steps:
 *   1. Verify this is a generated copy-assignment operator.
 *   2. Load 'this' and 'other' parameters.
 *   3. For each member variable: GEP to field, load from other, store to this.
 *   4. Return this pointer.
 *
 * @return true if the function was fully handled.
 */
bool implementation_generator::emit_copy_assignment_operator(function& function, llvm::Function* func) {
    // Step 1: Verify this is a generated copy-assignment operator
    // ── Compiler-generated copy assignment operator: emit memberwise copy ──
    if (!(function.is_compiler_generated() && function.is_operator()
        && function.get_short_name() == "__operator_aS_"
        && function.is_member() && !function.is_static())) {
        return false;
    }

    auto st = function.parent<aggregate>();
    if (!st) return false;

    auto this_param_it = _context->_function_this_variables.find(function.shared_as<model::function>());
    if (this_param_it == _context->_function_this_variables.end()) return false;

    // Step 2: Load 'this' and 'other' parameters
    auto this_ptr = _builder->CreateLoad(
        st->get_struct_type()->get_reference()->get_llvm_type(),
        this_param_it->second, "this_ptr");
    auto other_param = function.get_parameter("other");
    if (other_param) {
        // Step 3: For each member variable: GEP to field, load from other, store to this
        auto other_alloca_it = _context->_parameter_variables.find(
            std::const_pointer_cast<parameter>(other_param));
        if (other_alloca_it != _context->_parameter_variables.end()) {
            auto other_ref_type = _context->get_llvm_type(st->get_struct_type()->get_reference());
            auto other_ptr = _builder->CreateLoad(other_ref_type, other_alloca_it->second, "other_ref");
            auto st_llvm_type = _context->get_llvm_type(st->get_struct_type());
            // Memberwise copy using memcpy
            auto& dl = _context->_module->getDataLayout();
            uint64_t size = dl.getTypeAllocSize(st_llvm_type);
            _builder->CreateMemCpy(
                this_ptr, llvm::MaybeAlign(),
                other_ptr, llvm::MaybeAlign(),
                _builder->getInt64(size)
            );
        }
    }
    // Return this (ref to struct)
    _builder->CreateRet(this_ptr);
    optimize_function_dead_inst_elimination(*func);
    llvm::verifyFunction(*func);
    return true; // fully handled
}

/**
 * Emit post-block constructor IR: vptr stores and virtual base pointer initialization.
 *
 * Steps:
 *   1. Store vptr(s) into the most-derived object (after base ctors, so most-derived wins).
 *   2. For each virtual base sub-object in the derived class: store virtual base
 *      pointers across all sub-objects that reference the same virtual base.
 */
void implementation_generator::emit_constructor_post_block(function& function) {
    // Step 1: Store vptr(s) into the most-derived object (after base ctors, so most-derived wins)
    // ── Class vptr initialization (after base ctors, so our vtable wins) ─────
    // This must be done AFTER the block (which calls base constructors that also
    // set their own vptrs). By setting the vptr last, we ensure the most-derived
    // class's vtable pointer is what remains in the object.
    auto ctor = function.shared_as<constructor>();
    if (!ctor) return;

    auto st = ctor->get_owner();
    if (auto kl = std::dynamic_pointer_cast<klass>(st)) {
        if (kl->has_vtable()) {
            auto this_param_it = _context->_function_this_variables.find(function.shared_as<model::function>());
            if (this_param_it != _context->_function_this_variables.end()) {
                auto this_ptr = _builder->CreateLoad(
                    kl->get_struct_type()->get_reference()->get_llvm_type(),
                    this_param_it->second, "this_for_vptr");
                emit_vptr_store(*_builder, *kl, this_ptr, _context);
            }
        }
    }

    // Step 2: For each virtual base sub-object in the derived class
    // ── Virtual base pointer initialization ───────────────────────────────
    // For each transitively-declared virtual base, find the __vbase_X__ sub-object
    // in the most-derived class (this class) and write its address into all
    // __vbptr_X__ fields found in all non-virtual base sub-objects.
    if (!st) return;

    auto vbases = st->get_all_virtual_base_structs();
    if (vbases.empty()) return;

    auto this_param_it2 = _context->_function_this_variables.find(function.shared_as<model::function>());
    if (this_param_it2 == _context->_function_this_variables.end()) return;

    auto this_ptr = _builder->CreateLoad(
        st->get_struct_type()->get_reference()->get_llvm_type(),
        this_param_it2->second, "this_for_vbptr");
    auto st_llvm_type = _context->get_llvm_type(st->get_struct_type());

    for (auto& vbase : vbases) {
        std::string vbase_field_name = "__vbase_" + vbase->get_short_name() + "__";
        auto vbase_field = st->get_struct_type()->get_member(vbase_field_name);
        if (!vbase_field) continue;

        // Get address of the virtual base sub-object
        llvm::Value* vbase_ptr = _builder->CreateStructGEP(
            st_llvm_type, this_ptr, (unsigned)vbase_field->index,
            "vbase_" + vbase->get_short_name() + "_addr");

        std::string vbptr_field_name = "__vbptr_" + vbase->get_short_name() + "__";

        // Walk all base sub-objects (both embedded struct-bases and vbase class-bases)
        // and set their __vbptr_X__ to point to vbase_ptr.
        std::function<void(aggregate&, llvm::Value*)> set_vbptrs;
        set_vbptrs = [&](aggregate& base_st, llvm::Value* base_sub_ptr) {
            auto base_llvm_type = _context->get_llvm_type(base_st.get_struct_type());
            if (!base_llvm_type) return;

            // Check if this base_st has a direct __vbptr_X__ field
            auto vbptr_field = base_st.get_struct_type()->get_member(vbptr_field_name);
            if (vbptr_field) {
                // Store the vbase address into this vbptr field
                llvm::Value* vbptr_addr = _builder->CreateStructGEP(
                    base_llvm_type, base_sub_ptr,
                    (unsigned)vbptr_field->index,
                    "vbptr_" + vbase->get_short_name() + "_addr");
                _builder->CreateStore(vbase_ptr, vbptr_addr);
            }

            // Recurse into embedded (non-virtual) sub-bases
            for (auto& bs : base_st.get_bases()) {
                if (!bs.base) continue;
                if (!bs.is_virtual) {
                    // Non-virtual base: embedded as __base_X__
                    std::string sub_name = "__base_" + bs.sanitised_name() + "__";
                    auto sub_field = base_st.get_struct_type()->get_member(sub_name);
                    if (!sub_field) continue;
                    llvm::Value* sub_ptr = _builder->CreateStructGEP(
                        base_llvm_type, base_sub_ptr,
                        (unsigned)sub_field->index,
                        "sub_" + bs.sanitised_name() + "_ptr");
                    set_vbptrs(*bs.base, sub_ptr);
                } else {
                    // Virtual base: stored as __vbase_X__ in the containing object
                    std::string vbase_sub_name = "__vbase_" + bs.sanitised_name() + "__";
                    auto vbase_sub_field = base_st.get_struct_type()->get_member(vbase_sub_name);
                    if (vbase_sub_field) {
                        llvm::Value* vbase_sub_ptr = _builder->CreateStructGEP(
                            base_llvm_type, base_sub_ptr,
                            (unsigned)vbase_sub_field->index,
                            "vbase_" + bs.sanitised_name() + "_sub_ptr");
                        set_vbptrs(*bs.base, vbase_sub_ptr);
                    }
                }
            }
        };

        // Walk direct bases of 'st' to find and update __vbptr_X__
        for (auto& bs : st->get_bases()) {
            if (!bs.base) continue;
            if (!bs.is_virtual) {
                // Non-virtual base: embedded as __base_X__
                std::string sub_name = "__base_" + bs.sanitised_name() + "__";
                auto sub_field = st->get_struct_type()->get_member(sub_name);
                if (!sub_field) continue;
                llvm::Value* sub_ptr = _builder->CreateStructGEP(
                    st_llvm_type, this_ptr, (unsigned)sub_field->index,
                    "sub_" + bs.sanitised_name() + "_ptr");
                set_vbptrs(*bs.base, sub_ptr);
            } else {
                // Virtual base: stored as __vbase_X__ in st
                std::string vbase_sub_name = "__vbase_" + bs.sanitised_name() + "__";
                auto vbase_sub_field = st->get_struct_type()->get_member(vbase_sub_name);
                if (vbase_sub_field) {
                    llvm::Value* vbase_sub_ptr = _builder->CreateStructGEP(
                        st_llvm_type, this_ptr, (unsigned)vbase_sub_field->index,
                        "vbase_" + bs.sanitised_name() + "_sub_ptr");
                    set_vbptrs(*bs.base, vbase_sub_ptr);
                }
            }
        }
        // Also check if 'st' itself directly holds vbptr (direct virtual base)
        auto direct_vbptr = st->get_struct_type()->get_member(vbptr_field_name);
        if (direct_vbptr) {
            llvm::Value* vbptr_addr = _builder->CreateStructGEP(
                st_llvm_type, this_ptr, (unsigned)direct_vbptr->index,
                "direct_vbptr_" + vbase->get_short_name() + "_addr");
            _builder->CreateStore(vbase_ptr, vbptr_addr);
        }
    }
}

/**
 * Emit destructor cleanup IR: member and base destructor calls.
 *
 * Steps:
 *   1. Call destructors for struct-typed member variables (reverse declaration order).
 *   2. Call destructors for owner-typed member variables (free heap memory).
 *   3. Call base class destructors (reverse base-declaration order).
 *   4. For the most-derived class: call virtual base sub-object destructors.
 */
void implementation_generator::emit_destructor_cleanup(function& function) {
    // ── For destructors: call member and base destructors ──────────────────
    auto dtor = function.shared_as<destructor>();
    if (!dtor) return;

    auto st = dtor->get_owner();
    std::clog << "[DEBUG] visit_function destructor for " << (st ? st->get_short_name() : "null") << std::endl;
    if (!st) return;

    auto this_param_it = _context->_function_this_variables.find(function.shared_as<model::function>());
    if (this_param_it == _context->_function_this_variables.end()) return;

    auto this_param = this_param_it->second;
    auto this_ptr = _builder->CreateLoad(
        st->get_struct_type()->get_reference()->get_llvm_type(),
        this_param, "this_ptr");

    // Step 1: Call destructors for struct-typed member variables (reverse declaration order)
    // ── Member struct destructor calls (reverse declaration order) ──
    // Collect member variables that have a destructor (own members, not base subobjs)
    std::vector<std::pair<std::shared_ptr<member_variable_definition>, unsigned>> dtor_members;
    for (auto& var_entry : st->variables()) {
        if (auto var = std::dynamic_pointer_cast<member_variable_definition>(var_entry.second)) {
            if (var->get_short_name() == "__parent__") continue;
            if (var->get_short_name().rfind("__base_", 0) == 0) continue;
            if (var->get_short_name().rfind("__vbptr_", 0) == 0) continue;
            if (var->get_short_name().rfind("__vbase_", 0) == 0) continue;
            if (var->get_short_name().rfind("__vptr", 0) == 0) continue;
            if (auto m_st_type = std::dynamic_pointer_cast<struct_type>(var->get_type())) {
                if (m_st_type->get_struct() && m_st_type->get_struct()->get_destructor()) {
                    auto field_opt = st->get_struct_type()->get_member(var->get_short_name());
                    if (field_opt) {
                        dtor_members.push_back({var, (unsigned)field_opt->index});
                    }
                }
            }
        }
    }
    // Call member destructors in reverse declaration order
    for (auto it = dtor_members.rbegin(); it != dtor_members.rend(); ++it) {
        auto& [var, field_idx] = *it;
        std::clog << "[DEBUG] Emitting member dtor call for " << var->get_short_name()
                  << " (field_idx=" << field_idx << ") in " << st->get_short_name() << std::endl;
        auto m_st_type = std::dynamic_pointer_cast<struct_type>(var->get_type());
        auto m_dtor = m_st_type->get_struct()->get_destructor();
        auto m_dtor_it = _context->_functions.find(m_dtor->shared_as<k::model::function>());
        if (m_dtor_it == _context->_functions.end()) continue;
        auto member_ptr = _builder->CreateStructGEP(
            _context->get_llvm_type(st->get_struct_type()),
            this_ptr, field_idx,
            "member_" + var->get_short_name() + "_ptr");
        _builder->CreateCall(m_dtor_it->second, {member_ptr});
    }

    // Step 2: Call destructors for owner-typed member variables (free heap memory)
    // ── Base destructors in reverse base-declaration order ──
    if (st->has_bases() || st->has_virtual_bases()) {
        const auto& bases = st->get_bases();
        // Iterate in reverse base-declaration order
        // Skip virtual bases (handled separately via vbases below)
        for (auto bit = bases.rbegin(); bit != bases.rend(); ++bit) {
            auto& bs = *bit;
            if (!bs.base) continue;
            if (bs.is_virtual) continue; // virtual bases handled below
            auto base_dtor = bs.base->get_destructor();
            if (!base_dtor) continue;
            auto dtor_it = _context->_functions.find(base_dtor->shared_as<k::model::function>());
            if (dtor_it == _context->_functions.end()) continue;

            // GEP to base subobject field
            std::string subobj_name = "__base_" + bs.sanitised_name() + "__";
            auto base_field = st->get_struct_type()->get_member(subobj_name);
            if (!base_field) continue;

            auto base_ptr = _builder->CreateStructGEP(
                _context->get_llvm_type(st->get_struct_type()),
                this_ptr,
                (unsigned)base_field->index,
                "base_" + bs.sanitised_name() + "_ptr"
            );
            _builder->CreateCall(dtor_it->second, {base_ptr});
        }

        // Step 3: Call base class destructors (reverse base-declaration order)
        // ── Virtual base sub-object destructors (most-derived class only) ──
        // Only the class that owns __vbase_X__ should call X's destructor.
        // Collect virtual bases transitively
        auto vbases = st->get_all_virtual_base_structs();
        for (auto it = vbases.rbegin(); it != vbases.rend(); ++it) {
            auto& vbase = *it;
            std::string vbase_field_name = "__vbase_" + vbase->get_short_name() + "__";
            auto vbase_field = st->get_struct_type()->get_member(vbase_field_name);
            if (!vbase_field) continue; // not in this class's layout (not the collector)

            // Step 4: For the most-derived class: call virtual base sub-object destructors
            auto vbase_dtor = vbase->get_destructor();
            if (!vbase_dtor) continue;
            auto dtor_it = _context->_functions.find(vbase_dtor->shared_as<k::model::function>());
            if (dtor_it == _context->_functions.end()) continue;

            auto vbase_ptr = _builder->CreateStructGEP(
                _context->get_llvm_type(st->get_struct_type()),
                this_ptr,
                (unsigned)vbase_field->index,
                "vbase_" + vbase->get_short_name() + "_ptr"
            );
            _builder->CreateCall(dtor_it->second, {vbase_ptr});
        }
        // NOTE: Classes with only __vbptr_X__ (not __vbase_X__) do NOT call virtual
        // base destructors — that is the responsibility of the most-derived class (the
        // one owning __vbase_X__). This matches C++ sub-object destructor semantics.
    }
}

/**
 * Emit function return epilogue: cleanup and return instruction.
 *
 * Steps:
 *   1. Emit owner parameter cleanup (null the caller's owner after move).
 *   2. Emit struct parameter destructor calls.
 *   3. Emit return instruction (ret void, ret value, or ret through sret pointer).
 *   4. NRVO: replace NRVO candidate alloca with sret pointer.
 *   5. Dead instruction elimination pass.
 *   6. LLVM function verification.
 */
void implementation_generator::emit_function_return_epilogue(function& function, llvm::Function* func, bool use_sret) {
    // Step 1: Emit owner parameter cleanup (null the caller's owner after move)
    // Force adding a terminator as last instruction guard (will be eliminated if unreachable).
    // Before that, emit cleanup for owner-typed parameters (fall-through exit path).
    // For functions with an explicit return statement, this code is unreachable and will be
    // removed by optimize_function_dead_inst_elimination.
    if (!_owner_params_stack.empty()) {
        auto params = _owner_params_stack.top();
        _owner_params_stack.pop();
        for (auto it = params.rbegin(); it != params.rend(); ++it) {
            auto& param = *it;
            auto own_type = std::dynamic_pointer_cast<owner_type>(param->get_type());
            if (!own_type) continue;
            auto param_it = _context->_parameter_variables.find(param);
            if (param_it == _context->_parameter_variables.end()) continue;
            llvm::AllocaInst* alloca = param_it->second;
            emit_owner_cleanup_if_nonnull(_builder.get(), get_module(), _context->_functions,
                alloca, own_type->get_owned_type(), "exit_param");
        }
    }

    // Step 2: Emit struct parameter destructor calls
    // Clean up struct-typed by-value parameters at fall-through exit
    if (!_struct_params_stack.empty()) {
        auto params = _struct_params_stack.top();
        _struct_params_stack.pop();
        for (auto it = params.rbegin(); it != params.rend(); ++it) {
            auto& param = *it;
            auto st_type = std::dynamic_pointer_cast<struct_type>(param->get_type());
            if (!st_type || !st_type->get_struct() || !st_type->get_struct()->get_destructor()) continue;
            auto dtor = st_type->get_struct()->get_destructor();
            auto dtor_it = _context->_functions.find(dtor->shared_as<k::model::function>());
            if (dtor_it == _context->_functions.end()) continue;
            auto param_it = _context->_parameter_variables.find(param);
            if (param_it == _context->_parameter_variables.end()) continue;
            _builder->CreateCall(dtor_it->second, {param_it->second});
        }
    }

    // Step 3: Emit return instruction (ret void, ret value, or ret through sret pointer)
    if (function.has_return_type() && !use_sret) {
        // Named return variable (non-sret): load and return it at fall-through
        if (function.has_named_return_var()) {
            auto nrv = function.get_named_return_var();
            auto var_it = _context->_variables.find(nrv);
            if (var_it != _context->_variables.end()) {
                llvm::Type* ret_type = _context->get_llvm_type(function.get_return_type());
                llvm::Value* loaded = _builder->CreateLoad(ret_type, var_it->second, "named_ret_load");
                _builder->CreateRet(loaded);
            } else {
                llvm::Type* ret_type = _context->get_llvm_type(function.get_return_type());
                _builder->CreateRet(llvm::UndefValue::get(ret_type));
            }
        } else {
            llvm::Type* ret_type = _context->get_llvm_type(function.get_return_type());
            _builder->CreateRet(llvm::UndefValue::get(ret_type));
        }
    } else {
        _builder->CreateRetVoid();
    }

    // Step 4: NRVO: replace NRVO candidate alloca with sret pointer
    // NRVO: replace the NRVO candidate's alloca with _sret_ptr in all IR uses.
    // During code generation, the alloca was used normally (constructor writes into it,
    // symbol references return it, etc.). Now that all IR is generated, we swap it out
    // so the constructor writes directly into the caller's destination — zero-copy NRVO.
    if (_nrvo_candidate && _sret_ptr) {
        auto it = _context->_variables.find(_nrvo_candidate);
        if (it != _context->_variables.end()) {
            llvm::AllocaInst* nrvo_alloca = it->second;
            nrvo_alloca->replaceAllUsesWith(_sret_ptr);
            nrvo_alloca->eraseFromParent();
            _context->_variables.erase(it);
        }
    }

    // Step 5: Dead instruction elimination pass
    // Pre-optimize function
    optimize_function_dead_inst_elimination(*func);

    // Step 6: LLVM function verification
    // Verify function
    llvm::verifyFunction(*func);
}

void implementation_generator::optimize_function_dead_inst_elimination(llvm::Function& func) {
    for(auto& block : func) {
        llvm::BasicBlock *bb;
        // Find first terminator instruction
        auto term = std::find_if(block.begin(), block.end(), [](auto& inst)->bool{return inst.isTerminator();});
        if(term!=block.end()) {
            if(++term!=block.end()) {
                block.erase(term, block.end());
            }
        }
    }
}

//
// Constructor
//


} // namespace k::model::gen
