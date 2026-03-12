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
// Note: Last model_builder log number: 0x2002D
//

#include "model_builder.hpp"
#include "operators.hpp"

#include "../common/common.hpp"
#include <random>
#include <sstream>
#include <iomanip>

namespace k::model {

    static std::string gen_random_unsigned_id() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint16_t> dis(0, 0xFFFF);
        std::ostringstream oss;
        oss << std::hex << std::setw(4) << std::setfill('0') << dis(gen);
        return oss.str();
    }

    void model_builder::visit(k::log::logger& logger, std::shared_ptr<k::model::context> context, k::parse::ast::unit& src, k::model::unit& unit) {
        model_builder visitor(logger, context, unit);
        visitor.visit_unit(src);
    }

    void model_builder::visit_unit(parse::ast::unit &unit) {
        // Push root ns context
        stack<ns_context> push(_contexts, _unit.get_root_namespace());

        super::visit_unit(unit);

        if (_unit.get_unit_name().empty()) {
            // If no module name defined, set a default one
            _unit.set_unit_name(k::name("anon"+gen_random_unsigned_id()));
        }
    }

    void model_builder::visit_module_name(parse::ast::module_name &name) {
        if(name.qname) {
            _unit.set_unit_name(name.qname->to_name());
        }
    }

    void model_builder::visit_import(parse::ast::import& imp) {
        // Register the import name in the unit (resolved later by kdi_importer)
        if(imp.qname) {
            _unit.add_import(imp.qname->to_name());
        }
    }

    void model_builder::visit_identified_type_specifier(parse::ast::identified_type_specifier &) {

    }

    void model_builder::visit_parameter_specifier(parse::ast::parameter_spec &) {

    }

    void model_builder::visit_const_type_specifier(parse::ast::const_type_specifier &) {
        // Type resolution is handled by context::from_type_specifier; no model action needed here.
    }

    void model_builder::visit_function_ref_type_specifier(parse::ast::function_ref_type_specifier &) {
        // Type resolution is handled by context::from_type_specifier; no model action needed here.
    }

    void model_builder::visit_qualified_identifier(parse::ast::qualified_identifier &) {

    }

    void model_builder::visit_keyword_type_specifier(parse::ast::keyword_type_specifier &) {

    }

    void model_builder::visit_visibility_decl(parse::ast::visibility_decl &visibility) {
        auto scope = current_context<visibility_context>();
        if(!scope) {
            throw_error(0x0001, visibility.scope, "Visibility specifier '{}' is only allowed inside a namespace or a structure body, not at the current scope", {std::string{visibility.scope.content}});
        }

        switch(visibility.scope.type) {
            case lex::keyword::PUBLIC:
                scope->visibility = model::PUBLIC;
                break;
            case lex::keyword::PROTECTED:
                scope->visibility = model::PROTECTED;
                break;
            case lex::keyword::PRIVATE:
                scope->visibility = model::PRIVATE;
                break;
            default:
                throw_error(0x0002, visibility.scope, "'{}' is not a valid visibility keyword; expected 'public', 'protected' or 'private'", {std::string{visibility.scope.content}});
                break;
        }
    }

    void model_builder::visit_namespace_decl(parse::ast::namespace_decl &ns) {
        auto parent_ns = current_context_content<model::ns>();
        std::shared_ptr<k::model::ns> namesp = parent_ns->get_child_namespace(std::string{ns.name->content});

        // Push namespace context
        stack<ns_context> push(_contexts, namesp);

        super::visit_namespace_decl(ns);
    }

    void model_builder::visit_aggregate_decl(parse::ast::aggregate_decl& st) {
        std::shared_ptr<model::aggregate_holder> parent_scope = current_context_content<model::aggregate_holder>();
        if(!parent_scope){
            throw_error(0x0003, st.kw_aggregate_type, "Structure '{}' cannot be declared here; structures are only allowed at namespace or structure scope", {std::string{st.name.content}});
        }

        // Determine if this is a class (keyword 'class'), interface (keyword 'interface'), or a struct (keyword 'struct')
        bool is_class = (st.kw_aggregate_type.type == lex::keyword::CLASS);
        bool is_interface = (st.kw_aggregate_type.type == lex::keyword::INTERFACE);

        std::shared_ptr<model::aggregate> agg;
        if (is_class) {
            agg = parent_scope->define_class(std::string{st.name.content});
        } else if (is_interface) {
            agg = parent_scope->define_interface(std::string{st.name.content});
        } else {
            agg = parent_scope->define_structure(std::string{st.name.content});
        }

        // Detect if declared inside an outer aggregate
        bool is_static_nested = lex::keyword::has(st.specifiers, lex::keyword::STATIC);
        agg->set_static_nested(is_static_nested);

        // Detect if the final specifier is present
        bool is_final = lex::keyword::has(st.specifiers, lex::keyword::FINAL);
        agg->set_final(is_final);

        // Detect if the abstract specifier is present (only valid on classes and interfaces, not structs)
        if (lex::keyword::has(st.specifiers, lex::keyword::ABSTRACT)) {
            if (!is_class && !is_interface) {
                throw_error(0x0023, st.kw_aggregate_type,
                    "Specifier 'abstract' is not allowed on struct '{}'; 'abstract' is only valid on classes and interfaces",
                    {std::string{st.name.content}});
            }
            if (is_interface) {
                // 'abstract' is redundant on an interface: interfaces are implicitly abstract
                logger_relay::warn(with_flag(0x002A), lex::any_lexeme{st.kw_aggregate_type},
                    "Specifier 'abstract' is redundant on interface '{}'; interfaces are implicitly abstract",
                    {std::string{st.name.content}});
            }
            agg->set_abstract(true);
        }

        // Interfaces are always abstract, regardless of whether the specifier was written
        if (is_interface) {
            agg->set_abstract(true);
        }

        // Detect if the const specifier is present
        bool is_const_struct = lex::keyword::has(st.specifiers, lex::keyword::CONST);
        agg->set_const_struct(is_const_struct);

        // Resolve visibility: per-element specifier takes precedence over group visibility
        model::visibility vis = model::PUBLIC; // default
        if (auto vctx = current_context<visibility_context>()) {
            if (vctx->visibility != model::DEFAULT) {
                vis = vctx->visibility;
            }
        }
        if (lex::keyword::has(st.specifiers, lex::keyword::PUBLIC)) {
            vis = model::PUBLIC;
        } else if (lex::keyword::has(st.specifiers, lex::keyword::PROTECTED)) {
            vis = model::PROTECTED;
        } else if (lex::keyword::has(st.specifiers, lex::keyword::PRIVATE)) {
            vis = model::PRIVATE;
        }
        agg->set_visibility(vis);

        // Register base-class clause entries (raw names, will be resolved later by symbol_resolver)
        for (auto& base_entry : st.bases) {
            // Default inheritance visibility: public for struct, protected for class/interface
            model::visibility base_vis = (is_class || is_interface) ? model::PROTECTED : model::PUBLIC;
            if (base_entry.visibility_kw.has_value()) {
                switch (base_entry.visibility_kw->type) {
                    case lex::keyword::PUBLIC:    base_vis = model::PUBLIC;    break;
                    case lex::keyword::PROTECTED: base_vis = model::PROTECTED; break;
                    case lex::keyword::PRIVATE:
                        // Private inheritance is forbidden for classes and interfaces
                        if (is_class || is_interface) {
                            throw_error(0x0022, st.kw_aggregate_type,
                                "Class/interface '{}' cannot use private inheritance; private inheritance is not supported in K language; use public or protected inheritance",
                                {std::string{st.name.content}});
                        }
                        base_vis = model::PRIVATE;
                        break;
                    default: break;
                }
            }
            agg->add_base(base_entry.qualified_name, base_vis);
        }

        // Push aggregate context
        stack<struct_context> push(_contexts, agg);

        // Set initial default visibility for the aggregate context
        if (auto vctx = current_context<visibility_context>()) {
            vctx->visibility = model::DEFAULT;
        }

        default_ast_visitor::visit_aggregate_decl(st);
    }

    void model_builder::visit_variable_decl(parse::ast::variable_decl &decl) {
        std::shared_ptr<model::variable_holder> parent_scope = current_context_content<model::variable_holder>();
        if(!parent_scope){
            throw_error(0x0004, decl.name, "Variable '{}' cannot be declared here; variable declarations are not allowed in the current context", {std::string{decl.name.content}});
        }

        bool is_static = lex::keyword::has(decl.specifiers, lex::keyword::STATIC);
        bool is_const  = lex::keyword::has(decl.specifiers, lex::keyword::CONST);
        std::shared_ptr<model::variable_definition> var = parent_scope->append_variable(std::string{decl.name.content}, is_static);
        auto var_type = _context->from_type_specifier(*decl.type);
        // Normalize: if the type itself is const-qualified (e.g. "var : const T"),
        // strip the const from the type and promote it to the is_const flag.
        // This makes "const var : T", "var : const T", and "const var : const T" semantically identical.
        if (type::is_const(var_type)) {
            var_type = type::remove_const(var_type);
            is_const = true;
        }
        var->set_type(var_type);
        var->set_const(is_const);

        // Resolve visibility for namespace/struct-level variables (global or member)
        // Local variables (inside functions/blocks) do not have visibility.
        if (auto vctx = current_context<visibility_context>()) {
            // Default visibility depends on context:
            // - For class member variables: PROTECTED by default
            // - For struct member variables: PUBLIC by default
            // - For namespace variables: PUBLIC by default
            model::visibility vis = model::PUBLIC; // default for ns/struct
            if (vctx->visibility != model::DEFAULT) {
                vis = vctx->visibility;
            } else {
                // Check if we are inside a class (PROTECTED default) vs struct (PUBLIC default)
                if (auto owner_agg = current_context_content<model::aggregate>()) {
                    if (owner_agg->is_class()) {
                        vis = model::PROTECTED;
                    }
                }
            }
            // Per-element specifier overrides group visibility
            if (lex::keyword::has(decl.specifiers, lex::keyword::PUBLIC)) {
                vis = model::PUBLIC;
            } else if (lex::keyword::has(decl.specifiers, lex::keyword::PROTECTED)) {
                vis = model::PROTECTED;
            } else if (lex::keyword::has(decl.specifiers, lex::keyword::PRIVATE)) {
                vis = model::PRIVATE;
            }
            if (auto gv = std::dynamic_pointer_cast<model::global_variable_definition>(var)) {
                gv->set_visibility(vis);
            } else if (auto mv = std::dynamic_pointer_cast<model::member_variable_definition>(var)) {
                mv->set_visibility(vis);
            }
        }

        if(decl.init) {
            std::vector<std::shared_ptr<model::expression>> args;
            if (decl.is_brace_init) {
                // Brace initializer list: { expr, expr, ... }
                auto brace_list = std::dynamic_pointer_cast<parse::ast::brace_init_list>(decl.init);
                if (!brace_list) {
                    throw_error(0x0062, decl.name, "Internal error: brace init flag set but init is not a brace_init_list");
                }

                // If the type is an unsized array reference (T[]&, from T[] without a size),
                // re-create it as a sized array using the brace list element count.
                if (type::is_reference(var_type)) {
                    auto ref_type = std::dynamic_pointer_cast<reference_type>(var_type);
                    auto inner = ref_type ? ref_type->get_subtype() : nullptr;
                    if (type::is_array(inner) && !type::is_sized_array(inner)) {
                        auto unsized = std::dynamic_pointer_cast<array_type>(inner);
                        auto elem_type = unsized->get_subtype();
                        auto sized = elem_type->get_array(brace_list->elements.size());
                        var->set_type(sized);
                        var_type = sized;
                    }
                }

                // Build model element expressions from the AST
                std::vector<std::shared_ptr<model::expression>> elements;
                for (auto& elem_ast : brace_list->elements) {
                    if (elem_ast) {
                        _expr.reset();
                        elem_ast->visit(*this);
                        elements.push_back(_expr);
                        _expr.reset();
                    } else {
                        elements.push_back(nullptr); // default-init slot
                    }
                }
                var->set_init_expr(model::array_init_expression::make_shared(var, elements));
            } else if (type::is_owner(var_type)
                || type::is_pointer(var_type)
                || type::is_link(var_type)
                || type::is_pinned(var_type)) {
                // Owner/pointer/link/pin variable: the init expression is an address-valued
                // expression (new_expression, symbol, &expr, null …).
                // Store it directly — no constructor_invocation_expression wrapper.
                _expr.reset();
                decl.init->visit(*this);
                if (_expr) {
                    var->set_init_expr(_expr);
                    _expr.reset();
                }
            } else if (decl.is_constructor) {
                _expr.reset();
                if(auto list = std::dynamic_pointer_cast<parse::ast::expr_list_expr>(decl.init)) {
                    for(auto arg : list->exprs()) {
                        arg->visit(*this);
                        args.push_back(_expr);
                        _expr = nullptr;
                    }
                } else if(decl.init) {
                    decl.init->visit(*this);
                    args.push_back(_expr);
                }
                var->set_init_expr(model::constructor_invocation_expression::make_shared(var, args));
            } else {
                _expr.reset();
                decl.init->visit(*this);
                args.push_back(_expr);
                var->set_init_expr(model::constructor_invocation_expression::make_shared(var, args));
            }
        } else if (!type::is_owner(var_type)
                   && !type::is_pointer(var_type)
                   && !type::is_link(var_type)
                   && !type::is_pinned(var_type)) {
            // Non-indirection with no initializer: use empty constructor_invocation.
            var->set_init_expr(model::constructor_invocation_expression::make_shared(var, {}));
        }
    }

    void model_builder::visit_function_decl(parse::ast::function_decl & func) {
        auto parent_scope = current_context_content<function_holder>();
        if(!parent_scope) {
            throw_error(0x0005, func.name, "Function '{}' cannot be declared here; function declarations are only allowed at namespace or structure scope", {std::string{func.name.content}});
        }

        bool is_static = lex::keyword::has(func.specifiers, lex::keyword::STATIC);
        bool is_const_member = lex::keyword::has(func.specifiers, lex::keyword::CONST);

        // For destructor, prefix the name with ~ to match structure::define_function detection
        std::string func_name = func.is_destructor
            ? "~" + std::string{func.name.content}
            : std::string{func.name.content};

        std::shared_ptr<model::function> function = parent_scope->define_function(func_name, is_static);

        // const member is only meaningful for non-static member functions (not constructors/destructors/static)
        if (is_const_member) {
            if (is_static) {
                throw_error(0x0009, func.name,
                    "Function '{}' cannot be both 'const' and 'static': "
                    "'const' on a member function qualifies the implicit 'this' parameter as const, "
                    "which is meaningless for a static function that has no 'this' parameter",
                    {func_name});
            }
            if (func.is_destructor) {
                throw_error(0x000A, func.name,
                    "Destructor '~{}' cannot be declared 'const': "
                    "destructors always operate on mutable objects",
                    {std::string{func.name.content}});
            }
            if (std::dynamic_pointer_cast<model::constructor>(function)) {
                throw_error(0x000B, func.name,
                    "Constructor '{}' cannot be declared 'const': "
                    "constructors always operate on mutable objects being initialised",
                    {func_name});
            }
            function->set_const_member(true);
        }

        // Propagate aliasing specifier (-> default / -> delete / -> target)
        if(func.aliasing_spec == parse::ast::function_decl::aliasing_spec_t::REDIRECT) {
            // Function redirect: -> qualifiedId ;
            if(func.content) {
                throw_error(0x0022, func.name,
                    "Function redirector '{}' must not have a body; the body is provided by the target function",
                    {func_name});
            }
            if(!func.member_inits.empty()) {
                throw_error(0x0023, func.name,
                    "Function redirector '{}' must not have a member initializer list",
                    {func_name});
            }
            function->set_aliasing(model::function::function_aliasing::REDIRECT);
            function->set_redirect_target_name(func.redirect_target->to_name());
        } else if(func.aliasing_spec != parse::ast::function_decl::aliasing_spec_t::NONE) {
            // Validated at parse time: only non-static constructors may carry DEFAULT/DELETE.
            // Here we double-check at model level and emit a clearer error if the context is wrong.
            if(!std::dynamic_pointer_cast<constructor>(function)) {
                throw_error(0x0021, func.name,
                    "'-> default' / '-> delete' is only allowed on non-static constructors; "
                    "function '{}' is not a non-static constructor",
                    {func_name});
            }
            auto aliasing = (func.aliasing_spec == parse::ast::function_decl::aliasing_spec_t::DEFAULT)
                ? model::function::function_aliasing::DEFAULT
                : model::function::function_aliasing::DELETE;
            function->set_aliasing(aliasing);
            // A defaulted constructor is compiler-generated
            if(aliasing == model::function::function_aliasing::DEFAULT) {
                function->set_compiler_generated(true);
            }
        }

        // Propagate 'final' specifier for functions
        bool is_final_func = lex::keyword::has(func.specifiers, lex::keyword::FINAL);
        function->set_final_func(is_final_func);

        // Propagate 'abstract' specifier for functions
        if (lex::keyword::has(func.specifiers, lex::keyword::ABSTRACT)) {
            // abstract is only valid on non-static, non-private, non-final member functions of classes/interfaces
            if (is_static) {
                throw_error(0x0024, func.name,
                    "Function '{}' cannot be both 'abstract' and 'static': abstract functions require virtual dispatch",
                    {func_name});
            }
            if (is_final_func) {
                throw_error(0x0025, func.name,
                    "Function '{}' cannot be both 'abstract' and 'final': a final function is already defined",
                    {func_name});
            }
            if (func.content) {
                throw_error(0x0026, func.name,
                    "Abstract function '{}' must not have a body; remove the body or remove the 'abstract' specifier",
                    {func_name});
            }
            // Check that we are inside a class or interface, not a struct
            if (auto owner_agg = current_context_content<model::aggregate>()) {
                if (!owner_agg->is_class() && !std::dynamic_pointer_cast<model::interface>(owner_agg)) {
                    throw_error(0x0027, func.name,
                        "Abstract function '{}' is only allowed inside a class or interface, not a struct",
                        {func_name});
                }
                // Warn if 'abstract' is redundant (inside an interface)
                if (std::dynamic_pointer_cast<model::interface>(owner_agg)) {
                    logger_relay::warn(with_flag(0x002B), lex::any_lexeme{func.name},
                        "Specifier 'abstract' is redundant on function '{}' inside interface '{}'; interface member functions are implicitly abstract",
                        {func_name, owner_agg->get_short_name()});
                }
            } else {
                throw_error(0x0028, func.name,
                    "Abstract function '{}' can only be declared as a member of a class or interface",
                    {func_name});
            }
            // Check visibility after it has been resolved
            // (visibility is resolved later in this function; we re-check after)
            function->set_abstract_func(true);
        }

        // Implicitly mark member functions inside an interface as abstract
        // (non-static, non-ctor/dtor, non-final functions that have no body)
        if (!lex::keyword::has(func.specifiers, lex::keyword::ABSTRACT)) {
            if (auto owner_iface = std::dynamic_pointer_cast<model::interface>(current_context_content<model::aggregate>())) {
                if (!is_static
                    && !func.is_destructor
                    && !std::dynamic_pointer_cast<model::constructor>(function)
                    && !std::dynamic_pointer_cast<model::destructor>(function)
                    && !is_final_func) {
                    if (func.content) {
                        // Interface member functions must not have a body
                        throw_error(0x002D, func.name,
                            "Function '{}' inside interface '{}' must not have a body; interface member functions are implicitly abstract",
                            {func_name, owner_iface->get_short_name()});
                    }
                    function->set_abstract_func(true);
                }
            }
        }

        // Resolve visibility for namespace/struct-level functions
        if (auto vctx = current_context<visibility_context>()) {
            model::visibility vis = model::PUBLIC; // default for ns/struct (PUBLIC for both struct and class functions)
            if (vctx->visibility != model::DEFAULT) {
                vis = vctx->visibility;
            }
            // Per-element specifier overrides group visibility
            if (lex::keyword::has(func.specifiers, lex::keyword::PUBLIC)) {
                vis = model::PUBLIC;
            } else if (lex::keyword::has(func.specifiers, lex::keyword::PROTECTED)) {
                vis = model::PROTECTED;
            } else if (lex::keyword::has(func.specifiers, lex::keyword::PRIVATE)) {
                vis = model::PRIVATE;
            }
            function->set_visibility(vis);
        }

        // Post-visibility check: abstract cannot be private (private functions are never virtual)
        if (function->is_abstract_func() && function->get_visibility() == model::PRIVATE) {
            throw_error(0x0029, func.name,
                "Function '{}' cannot be both 'abstract' and 'private': abstract functions must be publicly or protectedly accessible for overriding",
                {func_name});
        }

        // Push function context
        stack<func_context> push(_contexts, function);

        // TODO add function specs

        if(func.type) {
            if(std::dynamic_pointer_cast<constructor>(function)) {
                throw_error(0x0006, func.name, "Constructor '{}' must not have a return type; constructors implicitly return an instance of their owning type", {func_name});
            } else if(std::dynamic_pointer_cast<destructor>(function)) {
                throw_error(0x0007, func.name, "Destructor '~{}' must not have a return type; destructors do not return a value", {std::string{func.name.content}});
            } else if(std::dynamic_pointer_cast<static_constructor>(function)) {
                throw_error(0x001C, func.name, "Static constructor '{}' must not have a return type; static constructors are void by definition", {func_name});
            } else if(std::dynamic_pointer_cast<static_destructor>(function)) {
                throw_error(0x001D, func.name, "Static destructor '~{}' must not have a return type; static destructors are void by definition", {std::string{func.name.content}});
            } else {
                function->set_return_type(_context->from_type_specifier(*func.type));
            }
        }

        if(func.is_destructor && !func.params.empty()) {
            throw_error(0x0008, func.name, "Destructor '~{}' must not have parameters; destructors take no arguments", {std::string{func.name.content}});
        }

        // Static constructor must have no parameters
        if(std::dynamic_pointer_cast<static_constructor>(function) && !func.params.empty()) {
            throw_error(0x001E, func.name, "Static constructor '{}' must not have parameters; static constructors take no arguments", {func_name});
        }

        // Static destructor must have no parameters
        if(std::dynamic_pointer_cast<static_destructor>(function) && !func.params.empty()) {
            throw_error(0x001F, func.name, "Static destructor '~{}' must not have parameters; static destructors take no arguments", {std::string{func.name.content}});
        }

        for(auto param : func.params) {
            bool param_is_const = lex::keyword::has(param->specifiers, lex::keyword::CONST);
            auto param_type = _context->from_type_specifier(*(param->type));
            // Normalize: if the type itself is const-qualified (e.g. "n : const T"),
            // strip the const from the type and promote it to param_is_const.
            // Makes "const n : T", "n : const T" and "const n : const T" semantically identical.
            if (type::is_const(param_type)) {
                param_type = type::remove_const(param_type);
                param_is_const = true;
            }
            std::shared_ptr<model::parameter> parameter = function->append_parameter(std::string{param->name->content}, param_type);
            parameter->set_const(param_is_const);
            // Build default expression if present
            if(param->default_expr) {
                _expr.reset();
                param->default_expr->visit(*this);
                if(_expr) {
                    parameter->set_default_expr(_expr);
                    _expr.reset();
                }
            }
        }


        if(auto ctor = std::dynamic_pointer_cast<constructor>(function)) {
            for(auto& ast_mi : func.member_inits) {
                std::vector<std::shared_ptr<model::expression>> init_args;
                for(auto& ast_arg : ast_mi.args) {
                    _expr.reset();
                    ast_arg->visit(*this);
                    if(_expr) {
                        init_args.push_back(_expr);
                        _expr.reset();
                    }
                }
                ctor->add_member_init(std::string{ast_mi.name.content}, std::move(init_args));
            }
        } else if (auto sctor = std::dynamic_pointer_cast<static_constructor>(function)) {
            // For a static constructor, the mem-init list declares dependency ordering:
            // `static S() : A(), gvar() {}` means S must be initialized after A and gvar.
            // No initialization arguments are accepted; we validate and record the names only.
            for (auto& ast_mi : func.member_inits) {
                if (!ast_mi.args.empty()) {
                    throw_error(0x0020, func.name,
                        "Static constructor dependency '{}' must not have arguments; "
                        "the mem-init list of a static constructor declares ordering dependencies only, not initializers",
                        {std::string{ast_mi.name.content}});
                }
                sctor->add_static_dep(std::string{ast_mi.name.content});
            }
        }

        if(func.content) {
            visit_block_statement(*func.content);
            if(auto block = std::dynamic_pointer_cast<model::block>(_stmt)) {
                function->set_block(block);
            }
        } else if (!function->is_abstract_func()
                   && func.aliasing_spec == parse::ast::function_decl::aliasing_spec_t::NONE) {
            // A non-abstract function with no body is only valid inside an interface
            // (where it is implicitly abstract) or when using '-> default'/'-> delete'/'-> target'.
            throw_error(0x002C, func.name,
                "Function '{}' has no body; a function body is required unless the function is abstract or declared inside an interface",
                {func_name});
        }
    }

    void model_builder::visit_block_statement(parse::ast::block_statement &block_stmt) {
        auto parent_scope = current_context_content<element>(); // Could be a function or a block
        if(!parent_scope) {
            throw_error(0x000A, block_stmt.open_brace, "Unexpected block '{{...}}': a block statement can only appear inside a function or another block, not at the current scope");
        }

        std::shared_ptr<model::block> block = std::make_shared<model::block>(parent_scope);

        // Push function context
        stack<block_context> push(_contexts, block);

        // Visit all children statements
        for(auto& stmt : block_stmt.statements) {
            _stmt.reset();
            stmt->visit(*this);
            if(_stmt) {
                block->append_statement(_stmt);
                _stmt.reset();
            }
        }

        _stmt = block;
    }

    void model_builder::visit_return_statement(parse::ast::return_statement &stmt) {
        auto parent_scope = current_context_content<statement>();
        if(!parent_scope) {
            throw_error(0x000B, stmt.ret, "'return' statement cannot appear here; it must be inside a function body");
        }

        std::shared_ptr<model::return_statement> ret_stmt = std::make_shared<model::return_statement>(parent_scope, stmt.shared_as<parse::ast::return_statement>());

        // Push function context
        stack<return_context> push(_contexts, ret_stmt);

        _expr.reset();
        if(stmt.expr) {
            stmt.expr->visit(*this);
        }
        if(_expr) {
            ret_stmt->set_expression(_expr);
            _expr.reset();
        }

        _stmt = ret_stmt;
    }

    void model_builder::visit_if_else_statement(parse::ast::if_else_statement &stmt) {
        auto parent_scope = current_context_content<statement>();
        if(!parent_scope) {
            throw_error(0x000C, stmt.if_kw, "'if' statement cannot appear here; it must be inside a function or block body");
        }

        std::shared_ptr<model::if_else_statement> if_else_stmt = std::make_shared<model::if_else_statement>(parent_scope, stmt.shared_as<parse::ast::if_else_statement>());

        // Push function context
        stack<if_else_context> push(_contexts, if_else_stmt);

        // Test expression
        _expr.reset();
        if(stmt.test_expr) {
            stmt.test_expr->visit(*this);
        } /* else process absence in next if */
        if(_expr) {
            if_else_stmt->set_test_expr(_expr);
            _expr.reset();
        } else {
            // Test expression is mandatory
            throw_error(0x000D, stmt.if_kw, "'if' statement requires a condition expression between the parentheses: 'if (condition) ...'");
        }

        // Then statement
        _stmt.reset();
        if(stmt.then_stmt) {
            stmt.then_stmt->visit(*this);
        } /* else process absence in next if */
        if(_stmt) {
            if_else_stmt->set_then_stmt(_stmt);
            _stmt.reset();
        } else {
            // Then statement is mandatory
            throw_error(0x000E, stmt.if_kw, "'if' statement requires a body: 'if (condition) {{ ... }}'");
        }

        // Else statement
        _stmt.reset();
        if(stmt.else_stmt) {
            stmt.else_stmt->visit(*this);
            if(_stmt) {
                if_else_stmt->set_else_stmt(_stmt);
                _stmt.reset();
            } else {
                // Error in processing else statement
                throw_error(0x000F, *stmt.else_kw, "'else' clause is present but its body could not be built; check that the else body is a valid statement or block");
            }
        } /* else else statement is not mandatory */

        _stmt = if_else_stmt;
    }

    void model_builder::visit_while_statement(parse::ast::while_statement &stmt) {
        auto parent_scope = current_context_content<statement>();
        if(!parent_scope) {
            throw_error(0x0010, stmt.while_kw, "'while' statement cannot appear here; it must be inside a function or block body");
        }

        auto while_stmt = std::make_shared<model::while_statement>(parent_scope, stmt.shared_as<parse::ast::while_statement>());

        // Push function context
        stack<while_context> push(_contexts, while_stmt);

        // Test expression
        _expr.reset();
        if(stmt.test_expr) {
            stmt.test_expr->visit(*this);
        } /* else process absence in next if */
        if(_expr) {
            while_stmt->set_test_expr(_expr);
            _expr.reset();
        } else {
            // Test expression is mandatory
            throw_error(0x0011, stmt.while_kw, "'while' statement requires a condition expression between the parentheses: 'while (condition) ...'");
        }

        // Nested statement
        _stmt.reset();
        if(stmt.nested_stmt) {
            stmt.nested_stmt->visit(*this);
        } /* else process absence in next if */
        if(_stmt) {
            while_stmt->set_nested_stmt(_stmt);
            _stmt.reset();
        } else {
            // Nested statement is mandatory
            throw_error(0x0012, stmt.while_kw, "'while' statement requires a body: 'while (condition) {{ ... }}'");
        }

        _stmt = while_stmt;
    }

    void model_builder::visit_for_statement(parse::ast::for_statement &stmt) {
        auto parent_scope = current_context_content<statement>();
        if(!parent_scope) {
            throw_error(0x0013, stmt.for_kw, "'for' statement cannot appear here; it must be inside a function or block body");
        }

        auto for_stmt = std::make_shared<model::for_statement>(parent_scope, stmt.shared_as<parse::ast::for_statement>());

        // Push function context
        stack<for_context> push(_contexts, for_stmt);

        // Variable decl
        _stmt.reset();
        if(stmt.decl_expr) {
            stmt.decl_expr->visit(*this);
            // Varable supposed to be already registered.
        }
        _stmt.reset();

        // Test expression
        _expr.reset();
        if(stmt.test_expr) {
            stmt.test_expr->visit(*this);
            if(_expr) {
                for_stmt->set_test_expr(_expr);
                _expr.reset();
            } else {
                // Test expression failed
                throw_error(0x0014, stmt.first_semicolon_kw, "Failed to build the condition expression of the 'for' statement; check the expression between the two semicolons: 'for (init; condition; step)'");
            }
        }
        _expr.reset();

        // Step expression
        _expr.reset();
        if(stmt.step_expr) {
            stmt.step_expr->visit(*this);
            if(_expr) {
                for_stmt->set_step_expr(_expr);
                _expr.reset();
            } else {
                // Step expression failed
                throw_error(0x0015, stmt.second_semicolon_kw, "Failed to build the step expression of the 'for' statement; check the expression after the second semicolon: 'for (init; condition; step)'");
            }
        }
        _expr.reset();

        // Nested statement
        _stmt.reset();
        if(stmt.nested_stmt) {
            stmt.nested_stmt->visit(*this);
        } /* else process absence in next if */
        if(_stmt) {
            for_stmt->set_nested_stmt(_stmt);
            _stmt.reset();
        } else {
            // Nested statement is mandatory
            throw_error(0x0016, stmt.for_kw, "'for' statement requires a body: 'for (init; condition; step) {{ ... }}'");
        }

        _stmt = for_stmt;
    }

    void model_builder::visit_expression_statement(parse::ast::expression_statement &stmt) {
        auto parent_scope = current_context_content<statement>();
        if(!parent_scope) {
            // Use the opt_ref_any_lexeme overload (no direct token available on expression_statement)
            throw_error(0x0017, lex::opt_ref_any_lexeme{}, "Expression statement cannot appear here; expression statements are only allowed inside a function or block body");
        }

        std::shared_ptr<model::expression_statement> expr = std::make_shared<model::expression_statement>(parent_scope, stmt.shared_as<parse::ast::expression_statement>());

        // Push function context
        stack<expr_stmt_context> push(_contexts, expr);

        _expr.reset();
        if(stmt.expr) {
            stmt.expr->visit(*this);
        }
        if(_expr) {
            expr->set_expression(_expr);
            _expr.reset();
        }

        _stmt = expr;
    }

    void model_builder::visit_literal_expr(parse::ast::literal_expr &expr) {
        _expr = model::value_expression::from_literal(expr.literal);
    }

    void model_builder::visit_keyword_expr(parse::ast::keyword_expr &expr) {
        // Note: Must not happen
    }

    void model_builder::visit_this_expr(parse::ast::keyword_expr &expr) {
        _expr = model::symbol_expression::from_identifier(name("this"));
    }

    void model_builder::visit_expr_list_expr(parse::ast::expr_list_expr &) {

    }

    void model_builder::visit_conditional_expr(parse::ast::conditional_expr &) {

    }

    void model_builder::visit_binary_operator_expr(parse::ast::binary_operator_expr & expr) {

        expr.lexpr()->visit(*this);
        std::shared_ptr<model::expression> lexpr = _expr;
        expr.rexpr()->visit(*this);
        std::shared_ptr<model::expression> rexpr = _expr;

        switch(expr.op.type) {
            case lex::operator_::PLUS:
                _expr = model::addition_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::MINUS:
                _expr = model::substraction_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::STAR:
                _expr = model::multiplication_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::SLASH:
                _expr = model::division_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::PERCENT:
                _expr = model::modulo_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::AMPERSAND:
                _expr = model::bitwise_and_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::PIPE:
                _expr = model::bitwise_or_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::CARET:
                _expr = model::bitwise_xor_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_CHEVRON_OPEN:
                _expr = model::left_shift_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_CHEVRON_CLOSE:
                _expr = model::right_shift_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::EQUAL:
                _expr = model::simple_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::PLUS_EQUAL:
                _expr = model::additition_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::MINUS_EQUAL:
                _expr = model::substraction_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::STAR_EQUAL:
                _expr = model::multiplication_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::SLASH_EQUAL:
                _expr = model::division_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::PERCENT_EQUAL:
                _expr = model::modulo_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::AMPERSAND_EQUAL:
                _expr = model::bitwise_and_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::PIPE_EQUAL:
                _expr = model::bitwise_or_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::CARET_EQUAL:
                _expr = model::bitwise_xor_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_CHEVRON_OPEN_EQUAL:
                _expr = model::left_shift_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_CHEVRON_CLOSE_EQUAL:
                _expr = model::right_shift_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_AMPERSAND:
                _expr = model::logical_and_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_PIPE:
                _expr = model::logical_or_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_EQUAL:
                _expr = model::equal_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOT_STAR:
                _expr = model::pm_expression::make_shared(lexpr, rexpr, false);
                break;
            case lex::operator_::ARROW_STAR:
                _expr = model::pm_expression::make_shared(lexpr, rexpr, true);
                break;
            case lex::operator_::EXCLAMATION_MARK_EQUAL:
                _expr = model::different_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::CHEVRON_OPEN:
                _expr = model::lesser_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::CHEVRON_CLOSE:
                _expr = model::greater_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::CHEVRON_OPEN_EQUAL:
                _expr = model::lesser_equal_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::CHEVRON_CLOSE_EQUAL:
                _expr = model::greater_equal_expression::make_shared(lexpr, rexpr);
                break;
            default: // TODO other operations
                throw_error(0x0018, expr.op, "Binary operator '{}' is not supported", {std::string{expr.op.content}});
                break;
        }
    }

    void model_builder::visit_cast_expr(parse::ast::cast_expr& expr) {
        _expr = nullptr;
        expr.expr()->visit(*this);
        _expr = model::cast_expression::make_shared(_expr, _context->from_type_specifier(*expr.type));
    }

    void model_builder::visit_unary_prefix_expr(parse::ast::unary_prefix_expr& expr) {
        _expr = nullptr;
        expr.expr()->visit(*this);
        auto sub = _expr;

        std::shared_ptr<model::unary_expression> unary;
        switch(expr.op.type) {
            case lex::operator_::PLUS:
                unary = model::unary_plus_expression::make_shared(sub);
                break;
            case lex::operator_::MINUS:
                unary = model::unary_minus_expression::make_shared(sub);
                break;
            case lex::operator_::TILDE:
                unary = model::bitwise_not_expression::make_shared(sub);
                break;
            case lex::operator_::EXCLAMATION_MARK:
                unary = model::logical_not_expression::make_shared(sub);
                break;
            case lex::operator_::AMPERSAND:
                unary = model::address_of_expression::make_shared(sub);
                break;
            case lex::operator_::STAR:
                unary = model::dereference_expression::make_shared(sub);
                break;
            case lex::operator_::DOUBLE_PLUS:
                unary = model::prefix_increment_expression::make_shared(sub);
                break;
            case lex::operator_::DOUBLE_MINUS:
                unary = model::prefix_decrement_expression::make_shared(sub);
                break;
            default:
                throw_error(0x0019, expr.op, "Unary prefix operator '{}' is not supported", {std::string{expr.op.content}});
                break;
        }
        unary->set_ast_unary_expr(expr.shared_as<parse::ast::unary_prefix_expr>());
        _expr = unary;
    }

    void model_builder::visit_unary_postfix_expr(parse::ast::unary_postfix_expr &expr) {
        _expr = nullptr;
        expr.expr()->visit(*this);
        auto sub = _expr;

        std::shared_ptr<model::unary_expression> unary;
        switch(expr.op.type) {
            case lex::operator_::DOUBLE_PLUS:
                unary = model::postfix_increment_expression::make_shared(sub);
                break;
            case lex::operator_::DOUBLE_MINUS:
                unary = model::postfix_decrement_expression::make_shared(sub);
                break;
            default:
                throw_error(0x001C, expr.op, "Unary postfix operator '{}' is not supported", {std::string{expr.op.content}});
                break;
        }
        unary->set_ast_unary_expr(expr.shared_as<parse::ast::unary_postfix_expr>());
        _expr = unary;
    }

    void model_builder::visit_bracket_postifx_expr(parse::ast::bracket_postifx_expr &expr) {
        expr.lexpr()->visit(*this);
        std::shared_ptr<model::expression> lexpr = _expr;
        expr.rexpr()->visit(*this);
        std::shared_ptr<model::expression> rexpr = _expr;
        _expr = model::subscript_expression::make_shared(lexpr, rexpr);
    }

    void model_builder::visit_parenthesis_postifx_expr(parse::ast::parenthesis_postifx_expr &expr) {
        expr.lexpr()->visit(*this);
        std::shared_ptr<model::expression> callee = _expr;

        _expr = nullptr;
        std::vector<std::shared_ptr<model::expression>> args;
        if(auto list = std::dynamic_pointer_cast<parse::ast::expr_list_expr>(expr.rexpr())) {
            for(auto arg : list->exprs()) {
                arg->visit(*this);
                args.push_back(_expr);
                _expr = nullptr;
            }
        } else if(expr.rexpr()) {
            expr.rexpr()->visit(*this);
            args.push_back(_expr);
        }

        _expr = model::function_invocation_expression::make_shared(callee, args);
    }

    void model_builder::visit_member_access_postfix_expr(parse::ast::member_access_postfix_expr &expr) {
        expr.expr()->visit(*this);
        std::shared_ptr<model::expression> callee = _expr;

        _expr = nullptr;
        expr.ident_expr->visit(*this);
        std::shared_ptr<model::symbol_expression> member = std::dynamic_pointer_cast<symbol_expression>(_expr);
        if(!member) {
            throw_error(0x001A, expr.op, "The right-hand side of '{}' must be a plain identifier (e.g. 'obj.field'), not a complex expression", {std::string{expr.op.content}});
        }

        switch (expr.op.type) {
            case lex::operator_::DOT:
                _expr = model::member_of_object_expression::make_shared(callee, member);
                break;
            case lex::operator_::ARROW:
                _expr = model::member_of_pointer_expression::make_shared(callee, member);
                break;
            default:
                throw_error(0x001B, expr.op, "Member access operator '{}' is not supported; expected '.' to access a member of an object, or '->' to access a member through a pointer", {std::string{expr.op.content}});
                break;
        }
    }

    void model_builder::visit_identifier_expr(parse::ast::identifier_expr &expr) {
        bool has_prefix = expr.qident.initial_doublecolon.has_value();
        std::vector<std::string> idents;
        for(auto ident : expr.qident.names){
            idents.emplace_back(ident.content);
        }
        _expr = model::symbol_expression::from_identifier(name(has_prefix, std::move(idents)));
    }

    void model_builder::visit_new_expr(parse::ast::new_expr& expr) {
        // Resolve the allocated type from the AST type specifier
        auto alloc_type = _context->from_type_specifier(*expr.type);

        if (expr.is_array) {
            // ── Array form: new T[N]{e0, e1, ...} ──

            // Build array size expression (may be nullptr for inferred size)
            std::shared_ptr<model::expression> size_expr;
            if (expr.array_size_expr) {
                _expr = nullptr;
                expr.array_size_expr->visit(*this);
                size_expr = _expr;
            }

            // Build per-element initializer expressions
            std::vector<std::shared_ptr<model::expression>> init_elements;
            if (expr.brace_init) {
                for (auto& elem : expr.brace_init->elements) {
                    if (elem) {
                        _expr = nullptr;
                        elem->visit(*this);
                        init_elements.push_back(_expr);
                    } else {
                        init_elements.push_back(nullptr); // empty slot → default init
                    }
                }
            }

            _expr = model::new_expression::make_array_shared(alloc_type, size_expr, init_elements, expr.brace_init != nullptr);
        } else {
            // ── Single-object form: new T(args...) ──

            // Build argument expressions.
            // parse_expression() may return an expr_list_expr for multi-arg lists ("3, 4").
            // Flatten those so that new_expression always holds individual argument expressions.
            std::vector<std::shared_ptr<model::expression>> args;
            for (auto& arg : expr.args) {
                if (auto list = std::dynamic_pointer_cast<parse::ast::expr_list_expr>(arg)) {
                    for (auto& sub : list->exprs()) {
                        _expr = nullptr;
                        sub->visit(*this);
                        args.push_back(_expr);
                    }
                } else {
                    _expr = nullptr;
                    arg->visit(*this);
                    args.push_back(_expr);
                }
            }

            _expr = model::new_expression::make_shared(alloc_type, args);
        }
    }

    void model_builder::visit_delete_expr(parse::ast::delete_expr& expr) {
        _expr = nullptr;
        expr.expr()->visit(*this);
        auto target = _expr;
        _expr = model::delete_expression::make_shared(target);
    }

    void model_builder::visit_comma_expr(parse::ast::expr_list_expr &) {

    }


} // k::parse

