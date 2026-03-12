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
// Note: Last gen_unit log number: 0x0035
//
#include "resolvers.hpp"
#include "generators.hpp"

#include "../model/imported.hpp"
#include "../model/expressions.hpp"

#include <llvm/IR/Verifier.h>
#include <llvm/IR/GlobalAlias.h>

#include <queue>
#include <unordered_map>

namespace k::model::gen {

//
// Named element
//

void symbol_resolver::visit_named_element(named_element& named) {
    // Assign fully qualified name if not already assigned, and compute the mangled name accordingly
    if (named.get_fq_name().empty()) {
        if (named.get_short_name().empty()) {
            // TODO correctly handle unnamed elements
        } else {
            auto elem = dynamic_cast<element*>(&named);
            if (elem) {
                named.assign_name(elem->ancestor<named_element>()->get_name().with_back(named.get_short_name()));
            }
        }
    }
}

//
// Unit
// Note : Global constructor and destructor method objects are always created but generated and registered only if needed.
// But Global main method is
//

void symbol_resolver::visit_unit(unit& unit)
{
    // ── Pre-pass: resolve base names for diamond detection ──────────────────────
    // We need to call compute_virtual_bases() BEFORE visit_namespace() so that
    // is_virtual is set correctly before sub-objects are injected.
    // This pre-pass only sets base_spec::base pointers (name resolution),
    // without performing any injection or other work.
    {
        std::function<void(const std::vector<std::shared_ptr<element>>&)> prepass;
        prepass = [&](const std::vector<std::shared_ptr<element>>& children) {
            for (auto& child : children) {
                if (auto st = std::dynamic_pointer_cast<aggregate>(child)) {
                    // Resolve nested aggregates first (depth-first, matches visit_structure order)
                    prepass(st->get_children());
                    // Resolve base names for this aggregate
                    for (auto& bs : st->get_bases_mutable()) {
                        if (!bs.base) { // not yet resolved
                            auto base_st = scope_lookup::lookup_structure(st->shared_as<element>(), bs.raw_name);
                            if (base_st) bs.base = base_st;
                            // Errors (not found, final, cross-type) will be caught properly in visit_structure
                        }
                    }
                } else if (auto nspace = std::dynamic_pointer_cast<ns>(child)) {
                    prepass(nspace->get_children());
                }
            }
        };
        prepass(_unit.get_root_namespace()->get_children());

        // Now compute virtual bases (diamond detection) on the fully pre-resolved graph
        std::vector<std::shared_ptr<aggregate>> all_structs;
        std::function<void(const std::vector<std::shared_ptr<element>>&)> collect;
        collect = [&](const std::vector<std::shared_ptr<element>>& children) {
            for (auto& child : children) {
                if (auto agg = std::dynamic_pointer_cast<aggregate>(child)) {
                    all_structs.push_back(agg);
                    collect(agg->get_children());
                } else if (auto nspace = std::dynamic_pointer_cast<ns>(child)) {
                    collect(nspace->get_children());
                }
            }
        };
        collect(_unit.get_root_namespace()->get_children());
        klass::compute_virtual_bases(all_structs);
    }

    visit_namespace(*_unit.get_root_namespace());

    visit_global_constructor_function(_unit.get_global_constructor_function());
    visit_global_destructor_function(_unit.get_global_destructor_function());
}

void type_reference_resolver::visit_unit(unit& unit)
{
    visit_namespace(*_unit.get_root_namespace());

    // Compute unified initialization/finalization order over all static constructors
    // and global variables, resolving cross-dependencies.
    init_order_resolver order_resolver(_log, _context, _unit);
    order_resolver.resolve();

    visit_global_constructor_function(_unit.get_global_constructor_function());
    visit_global_destructor_function(_unit.get_global_destructor_function());

    if (auto func = unit.get_root_namespace()->get_function("main")) {
        if (auto main_func = unit.generate_main_function(func)) {
            visit_global_main_function(*main_func);
        }
    }
}

void declaration_generator::visit_unit(unit &unit) {
    visit_namespace(*_unit.get_root_namespace());

    visit_global_constructor_function(_unit.get_global_constructor_function());
    visit_global_destructor_function(_unit.get_global_destructor_function());

    if (unit._global_main_func) {
        visit_global_main_function(*unit._global_main_func);
    }

    // ── Emit LLVM declarations for all imported entities ──────────────────
    // Strategy: if the entity has a non-empty llvm_def we parse it directly
    // via context::declare_llvm_function_from_def — this guarantees ABI
    // fidelity regardless of how the K type system maps to LLVM types.
    // Fallback to the old visit_function() path when llvm_def is absent.

    auto declare_imported_fn = [&](const std::shared_ptr<function>& fn,
                                   const std::string& llvm_def,
                                   const std::string& mangled) {
        if (_context->lookup_llvm_function(fn)) return; // already declared
        if (!llvm_def.empty() && !mangled.empty()) {
            auto* llvm_fn = _context->declare_llvm_function_from_def(llvm_def, mangled);
            if (llvm_fn) {
                _context->_functions[fn] = llvm_fn;
                return;
            }
        }
        // Fallback: derive signature from K types (old path)
        fn->accept(*this);
    };

    // imported_function
    for (const auto& [mangled, fn] : unit.get_imported_functions()) {
        const auto* kfn = fn->get_kdi_function();
        declare_imported_fn(fn,
            kfn ? kfn->llvm_def : "",
            kfn ? kfn->mangled_name : mangled);
    }

    // imported_aggregate: constructors, destructor, methods
    for (const auto& [fq, agg] : unit.get_imported_aggregates()) {
        const auto* kdi_agg = agg->get_kdi_aggregate();

        // Constructors
        for (size_t i = 0; i < agg->constructors().size(); ++i) {
            const auto& ic = agg->constructors()[i];
            std::string def, mng;
            if (kdi_agg && i < kdi_agg->constructors.size()) {
                def = kdi_agg->constructors[i].llvm_def;
                mng = kdi_agg->constructors[i].mangled_name;
            }
            declare_imported_fn(ic, def, mng);
        }
        // Destructor
        if (auto id = agg->get_destructor()) {
            std::string def, mng;
            if (kdi_agg && kdi_agg->destructor.has_value()) {
                def = kdi_agg->destructor->llvm_def;
                mng = kdi_agg->destructor->mangled_name;
            }
            declare_imported_fn(id, def, mng);
        }
        // Methods
        size_t method_idx = 0;
        for (const auto& child : agg->get_children()) {
            if (auto im = std::dynamic_pointer_cast<imported_method>(child)) {
                const kdi::kdi_method* km = im->get_kdi_method();
                declare_imported_fn(im,
                    km ? km->llvm_def : "",
                    km ? km->mangled_name : "");
                ++method_idx;
            }
        }
    }

    // imported_variable — emit ExternalLinkage GlobalVariable with no initialiser
    for (const auto& [mangled, var] : unit.get_imported_variables()) {
        if (!var || !var->get_type()) continue;
        auto llvm_type = _context->get_llvm_type(var->get_type());
        if (!llvm_type) continue;
        if (_context->_module->getGlobalVariable(var->get_mangled_name())) continue;
        auto* gv = new llvm::GlobalVariable(
            *_context->_module,
            llvm_type,
            /*isConstant=*/false,
            llvm::GlobalValue::ExternalLinkage,
            /*Initializer=*/nullptr,
            var->get_mangled_name());
        _context->_global_vars.insert({var, gv});
    }

    // ── Emit GlobalAlias for redirected functions ─────────────────────────
    // After all normal function declarations are emitted, create LLVM
    // GlobalAlias entries for redirected functions pointing to their
    // resolved targets.
    emit_redirect_aliases(*_unit.get_root_namespace());
}

void declaration_generator::emit_redirect_aliases(ns& nspc) {
    for (auto& child : nspc.get_children()) {
        if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            emit_redirect_alias(*fn);
        } else if (auto agg = std::dynamic_pointer_cast<aggregate>(child)) {
            emit_redirect_aliases_from_aggregate(*agg);
        } else if (auto child_ns = std::dynamic_pointer_cast<ns>(child)) {
            emit_redirect_aliases(*child_ns);
        }
    }
}

void declaration_generator::emit_redirect_aliases_from_aggregate(aggregate& agg) {
    for (auto& fn : agg.functions()) {
        emit_redirect_alias(*fn);
    }
    // Recurse into nested aggregates
    for (auto& [name, nested] : agg.aggregates()) {
        emit_redirect_aliases_from_aggregate(*nested);
    }
}

void declaration_generator::emit_redirect_alias(function& fn) {
    if (!fn.is_redirected()) return;
    auto target = fn.get_redirect_target();
    if (!target) return;

    // Find the LLVM function for the target
    auto target_it = _context->_functions.find(target);
    if (target_it == _context->_functions.end()) {
        // Target not yet declared — shouldn't happen after full declaration pass
        return;
    }
    llvm::Function* target_llvm = target_it->second;

    // Create GlobalAlias with the redirector's mangled name pointing to the target
    auto* alias = llvm::GlobalAlias::create(
        target_llvm->getValueType(),
        target_llvm->getAddressSpace(),
        llvm::GlobalValue::ExternalLinkage,
        fn.get_mangled_name(),
        target_llvm,
        _context->_module.get());

    // Register in the context so the alias can be found by symbol name
    _context->_functions.insert({fn.shared_as<k::model::function>(), target_llvm});
    (void)alias; // alias is owned by the module
}

void implementation_generator::visit_unit(unit &unit) {
    visit_namespace(*_unit.get_root_namespace());

    visit_global_constructor_function(_unit.get_global_constructor_function());
    visit_global_destructor_function(_unit.get_global_destructor_function());

    if (unit._global_main_func) {
        visit_global_main_function(*unit._global_main_func);
    }
}

//
// Namespace
//

void symbol_resolver::visit_namespace(ns& ns)
{
    if (ns.get_fq_name().empty()) {
        if (ns.is_root()) {
            // Root namespace
            // Should not happen, supposed to be handled at model construction level
            if (ns.get_name().empty()) {
                throw_internal_error(0x0001, std::nullopt,
                    "Internal error: root namespace has no name at code generation stage; "
                    "this should not happen and indicates a compiler bug");
            } else {
                ns.assign_name(ns.get_name().with_root_prefix());
            }
        } else {
            if (ns.get_short_name().empty()) {
                // TODO correctly handle unnamed namespaces
            } else {
                ns.assign_name(ns.parent<model::ns>()->get_name().with_back(ns.get_short_name()));
            }
        }
    }

    for(auto& child : ns.get_children()) {
        child->accept(*this);
    }

}

void type_reference_resolver::visit_namespace(ns& ns)
{
    for(auto& child : ns.get_children()) {
        child->accept(*this);
    }
    // After all children are resolved, check for overload collisions among free functions.
    check_overload_collisions(ns);
}

void declaration_generator::visit_namespace(ns &ns) {
    for(auto child : ns.get_children()) {
        child->accept(*this);
    }
}

void implementation_generator::visit_namespace(ns &ns) {
    for(auto child : ns.get_children()) {
        child->accept(*this);
    }
}

//
// Member variable definition
//
void symbol_resolver::visit_member_variable_definition(member_variable_definition& var) {
    visit_named_element(var);
    // No symbol resolution today, because only primitive types are supported today.
    // TODO Add complex member resolution.
    // TODO visit the initialization expression if any
}

void type_reference_resolver::visit_member_variable_definition(member_variable_definition& var) {
    // __parent__ field is already assigned a resolved pointer type by symbol_resolver; skip.
    if (var.get_short_name() == "__parent__") return;
    // Do nothing for now
    // Everything is done at structure level
}

void declaration_generator::visit_member_variable_definition(member_variable_definition&) {
    // Do nothing for now
    // Everything is done at structure level
}

void implementation_generator::visit_member_variable_definition(member_variable_definition&) {
    // Do nothing for now
    // Everything is done at structure level
}


//
// Global variable definition
//

void symbol_resolver::visit_global_variable_definition(global_variable_definition& var)
{
    visit_named_element(var);

    if (auto expr = var.get_init_expr()) {
        expr->accept(*this);
    }
}

void type_reference_resolver::visit_global_variable_definition(global_variable_definition& var)
{
    visit_variable_definition(var);

    // Unconditionnally register global variable to global constructor for now, because we need to be sure it is registered before any possible use in other variable initialization expression.
    // TODO Add registering condition for trivial primitive initialization
    var.ancestor<unit>()->get_global_constructor_function().add_global_variable_definition(var.shared_as<global_variable_definition>());

    // If the variable's type is a struct with a destructor, also register it for global destruction.
    if (auto st_type = std::dynamic_pointer_cast<struct_type>(var.get_type())) {
        if (st_type->get_struct() && st_type->get_struct()->get_destructor()) {
            var.ancestor<unit>()->get_global_destructor_function().add_global_variable_definition(var.shared_as<global_variable_definition>());
        }
    }
}

void declaration_generator::visit_global_variable_definition(global_variable_definition& var) {
    auto type = var.get_type();
    llvm::Type *llvm_type = _context->get_llvm_type(type);
    if (!llvm_type) return; // type not yet resolved or unsupported

    auto variable = new llvm::GlobalVariable(*_context->_module, llvm_type, false, llvm::GlobalValue::ExternalLinkage, nullptr, var.get_mangled_name());
    _context->_global_vars.insert({var.shared_as<global_variable_definition>(), variable});
}


void implementation_generator::visit_global_variable_definition(global_variable_definition& var) {
    auto type = var.get_type();
    llvm::Type *llvm_type = _context->get_llvm_type(type);

    // Generate initialization
    llvm::Constant* constInitValue = nullptr;

    auto init_expr_ctor = std::dynamic_pointer_cast<constructor_invocation_expression>(var.get_init_expr());
    if (type::is_primitive(var.get_type()) && init_expr_ctor && init_expr_ctor->size() == 1) {
        if (auto value = std::dynamic_pointer_cast<value_expression>(init_expr_ctor->argument(0))) {
            // Constant init expression
            if (auto constant = get_llvm_constant_from_value_expr(*value)) {
                // TODO Implement type conversion
                constInitValue = constant;
            }
        }
    }

    // For sized arrays with brace init, try to build a static constant initializer
    if (!constInitValue && type::is_sized_array(var.get_type())) {
        auto arr_init = std::dynamic_pointer_cast<array_init_expression>(var.get_init_expr());
        auto arr_type = std::dynamic_pointer_cast<sized_array_type>(var.get_type());
        if (arr_type) {
            auto elem_type = arr_type->get_subtype();
            auto* struct_llvm = arr_type->get_llvm_struct_type();
            auto* arr_data_type = arr_type->get_llvm_data_array_type();
            llvm::Type* llvm_elem_type = _context->get_llvm_type(elem_type);
            size_t arr_size = arr_type->get_size();

            bool all_constant = true;
            std::vector<llvm::Constant*> elem_constants;

            if (arr_init) {
                for (size_t i = 0; i < arr_size; ++i) {
                    auto elem = (i < arr_init->size()) ? arr_init->element(i) : nullptr;
                    if (!elem) {
                        // Default-init = zero
                        elem_constants.push_back(llvm::Constant::getNullValue(llvm_elem_type));
                    } else if (auto value = std::dynamic_pointer_cast<value_expression>(elem)) {
                        auto c = get_llvm_constant_from_value_expr(*value);
                        if (c) {
                            elem_constants.push_back(c);
                        } else {
                            all_constant = false;
                            break;
                        }
                    } else {
                        all_constant = false;
                        break;
                    }
                }
            }

            if (all_constant && elem_constants.size() == arr_size) {
                auto* size_const = llvm::ConstantInt::get(
                    llvm::Type::getInt32Ty(_context->llvm_context()), arr_size, false);
                auto* arr_const = llvm::ConstantArray::get(arr_data_type, elem_constants);
                llvm::Constant* struct_fields[] = {size_const, arr_const};
                constInitValue = llvm::ConstantStruct::get(struct_llvm, struct_fields);
            }
            // else: fall through to default zero init; dynamic init handled by global ctor
        }
    }

    if (!constInitValue) {
        // If no explicit initialization, or complex initialization, lets have 0-filled initialization:
        constInitValue = type->generate_default_value_initializer();
    }

    // Final fallback: if the type is an opaque pointer (e.g. function_reference_type),
    // use ConstantPointerNull; otherwise zeroinitializer.
    if (!constInitValue && llvm_type) {
        if (auto* ptr_ty = llvm::dyn_cast<llvm::PointerType>(llvm_type)) {
            constInitValue = llvm::ConstantPointerNull::get(ptr_ty);
        } else {
            constInitValue = llvm::Constant::getNullValue(llvm_type);
        }
    }

    auto variable_it = _context->_global_vars.find(var.shared_as<global_variable_definition>());
    if (variable_it == _context->_global_vars.end()) {
        // Not declared yet, should not append, but let's create it lazily anyway
        auto variable = new llvm::GlobalVariable(*_context->_module, llvm_type, false, llvm::GlobalValue::ExternalLinkage, constInitValue, var.get_mangled_name());
        _context->_global_vars.insert({var.shared_as<global_variable_definition>(), variable});
    } else {
        // Already declared, just add initializer
        variable_it->second->setInitializer(constInitValue);
    }
}

} // namespace k::model::gen
