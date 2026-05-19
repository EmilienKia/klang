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

#include "model.hpp"
#include "context.hpp"
#include "expressions.hpp"
#include "model_visitor.hpp"
#include "mangler.hpp"
#include "tools/kdi_type_converter.hpp"

#include "../parse/ast.hpp"
#include "../common/tools.hpp"

#include <kdi.hpp>  // kdi::kdi_file, kdi::kdi_namespace, kdi::kdi_function, …

#include <queue>
#include <unordered_set>


namespace k::model {


static name to_name(const parse::ast::qualified_identifier &ident) {
    std::vector<std::string> idents;
    for (const auto &id: ident.names) {
        idents.emplace_back(id.content);
    }
    return {ident.has_root_prefix(), idents};
}

//
// Base model element
//
std::shared_ptr<context> element::get_context() {
    std::shared_ptr<element> current = shared_as<element>();
    std::shared_ptr<element> parent = _parent;
    while(parent) {
        current = parent;
        parent = parent->_parent;
    }
    if (auto root = current->shared_as<unit>()) {
        return root->_context;
    }
    return {};
}

//
// Bases of named element
//

void named_element::update_names() {
    _short_name = _name.back();
    if (_name.has_root_prefix()) {
        _fq_name = _name.to_string();
        update_mangled_name();
    } else {
        _fq_name.clear();
        _mangled_name.clear();
    }

}


//
// Abstract variable holder
//

std::shared_ptr<variable_definition> variable_holder::append_variable(const std::string& name, bool is_static) {
    if (_vars.contains(name)) {
        // TODO throw exception : var is already defined.
    }
    std::shared_ptr<variable_definition> var = do_create_variable(name, is_static);
    _vars[name] = var;
    on_variable_defined(var);
    return var;
}

std::shared_ptr<variable_definition> variable_holder::get_variable(const std::string& name) const {
    // TODO add type checking
    auto it = _vars.find(name);
    if (it != _vars.end()) {
        return it->second;
    } else {
        return {};
    }
}


variable_holder::variable_map_t::const_iterator variable_holder::variable_begin() const {
    return _vars.begin();
}

variable_holder::variable_map_t::const_iterator variable_holder::variable_end() const {
    return _vars.end();
}


//
// Abstract function holder
//

std::shared_ptr<function> function_holder::define_function(const std::string &name, bool is_static) {
    std::shared_ptr<function> func = do_create_function(name, is_static);
    _functions.push_back(func);
    on_function_defined(func);
    return func;
}

std::shared_ptr<function> function_holder::get_function(const std::string &name) const {
    // TODO add prototype checking
    for (auto func: _functions) {
        if (func->get_short_name() == name) {
            return func;
        }
    }
    return nullptr;
}

std::vector<std::shared_ptr<function>> function_holder::get_functions(const std::string &name) const {
    std::vector<std::shared_ptr<function>> res;
    for (auto& func: _functions) {
        if (func->get_short_name() == name) {
            res.push_back(func);
        }
    }
    return res;
}


//
// Abstract aggregate holder
//

std::shared_ptr<aggregate> aggregate_holder::define_aggregate(const std::string &name, bool is_class) {
    if (is_class) {
        return define_class(name);
    } else {
        return define_structure(name);
    }
}

std::shared_ptr<structure> aggregate_holder::define_structure(const std::string &name) {
    std::shared_ptr<structure> st = do_create_structure(name);
    _structs.insert({name, st});
    on_aggregate_defined(st);
    return st;
}

std::shared_ptr<klass> aggregate_holder::define_class(const std::string &name) {
    std::shared_ptr<klass> kl = do_create_class(name);
    _structs.insert({name, kl});
    on_aggregate_defined(kl);
    return kl;
}

std::shared_ptr<interface> aggregate_holder::define_interface(const std::string &name) {
    std::shared_ptr<interface> iface = do_create_interface(name);
    _structs.insert({name, iface});
    on_aggregate_defined(iface);
    return iface;
}

std::shared_ptr<annotation_type> aggregate_holder::define_annotation(const std::string &name) {
    std::shared_ptr<annotation_type> ann = do_create_annotation(name);
    _structs.insert({name, ann});
    on_aggregate_defined(ann);
    return ann;
}

std::shared_ptr<aggregate> aggregate_holder::get_aggregate(const std::string &name) const {
    auto it = _structs.find(name);
    if (it != _structs.end()) {
        return it->second;
    } else {
        return {};
    }
}

std::shared_ptr<structure> aggregate_holder::get_structure(const std::string &name) const {
    auto it = _structs.find(name);
    if (it != _structs.end()) {
        return std::dynamic_pointer_cast<structure>(it->second);
    } else {
        return {};
    }
}


//
// Enum holder
//

std::shared_ptr<enumeration> enum_holder::define_enum(const std::string &name) {
    std::shared_ptr<enumeration> en = do_create_enum(name);
    _enums.insert({name, en});
    on_enum_defined(en);
    return en;
}

std::shared_ptr<enumeration> enum_holder::get_enum(const std::string &name) const {
    auto it = _enums.find(name);
    if (it != _enums.end()) {
        return it->second;
    } else {
        return {};
    }
}


//
// Enumeration
//

std::shared_ptr<enumeration> enumeration::make_shared(std::shared_ptr<element> parent, const std::string& name) {
    auto en = std::shared_ptr<enumeration>(new enumeration(parent));
    en->assign_name(name);
    return en;
}

void enumeration::update_mangled_name() {
    // Use the same mangling convention as struct types
    _mangled_name = std::to_string(_short_name.size()) + _short_name;
}

void enumeration::accept(model_visitor& visitor) {
    visitor.visit_enumeration(*this);
}


//
// Union holder
//

std::shared_ptr<union_type_def> union_holder::define_union(const std::string &name) {
    std::shared_ptr<union_type_def> un = do_create_union(name);
    _unions.insert({name, un});
    on_union_defined(un);
    return un;
}

std::shared_ptr<union_type_def> union_holder::get_union(const std::string &name) const {
    auto it = _unions.find(name);
    if (it != _unions.end()) {
        return it->second;
    } else {
        return {};
    }
}


//
// Union type definition
//

std::shared_ptr<union_type_def> union_type_def::make_shared(std::shared_ptr<element> parent, const std::string& name) {
    auto un = std::shared_ptr<union_type_def>(new union_type_def(parent));
    un->assign_name(name);
    return un;
}

void union_type_def::update_mangled_name() {
    if (!_name.has_root_prefix()) {
        _mangled_name = "";
        return;
    }
    _mangled_name = mangler::mangle_structure(_name);
}

void union_type_def::accept(model_visitor& visitor) {
    visitor.visit_union(*this);
}


//
// Variable definition
//

void variable_definition::init(const std::string &name, const std::shared_ptr<type> &type) {
    assign_name(name);
    _type = type;
}

std::shared_ptr<type> variable_definition::get_type() const {
    return _type;
}

std::shared_ptr<expression> variable_definition::get_init_expr() const {
    return _init_expr;
}

variable_definition& variable_definition::set_type(std::shared_ptr<type> type) {
    _type = type;
    return *this;
}

variable_definition& variable_definition::set_init_expr(std::shared_ptr<expression> init_expr) {
    _init_expr = init_expr;
    if (_init_expr) {
        if (auto self = dynamic_cast<element*>(this)) {
            _init_expr->set_parent(self->shared_as<element>());
        }
    }
    return *this;
}

variable_definition& variable_definition::set_init_expr(std::shared_ptr<constructor_invocation_expression> init_expr) {
    return set_init_expr(std::static_pointer_cast<expression>(init_expr));
}


//
// NS element
//
#if WITH_NS_ELEMENT
void ns_element::accept(model_visitor &visitor) {
    visitor.visit_ns_element(*this);
}
#endif // WITH_NS_ELEMENT

//
// Parameter
//

parameter::parameter(std::shared_ptr<function> func, size_t pos) :
        element(func),
        _function(std::move(func)), _pos(pos) {
}

std::shared_ptr<parameter> parameter::make_shared(std::shared_ptr<function> func, size_t pos) {
    return std::shared_ptr<parameter>(new parameter(std::move(func), pos));
}

std::shared_ptr<parameter> parameter::make_shared(std::shared_ptr<function> func, const std::string &name, size_t pos) {
    auto param = std::shared_ptr<parameter>(new parameter(std::move(func), pos));
    param->init(name);
    return param;
}

std::shared_ptr<parameter> parameter::make_shared(std::shared_ptr<function> func, const std::string &name, const std::shared_ptr<type> &type, size_t pos) {
    auto param = std::shared_ptr<parameter>(new parameter(std::move(func), pos));
    param->init(name, type);
    return param;
}

void parameter::update_mangled_name() {
    // Parameter is not mangled cause not exported
}

void parameter::accept(model_visitor& visitor) {
    visitor.visit_parameter(*this);
}

//
// Function
//

std::shared_ptr<function> function::make_shared(std::shared_ptr<element> parent, const std::string& name, bool is_static) {
    // Is static is only supported for aggregate members (not global methods)
    is_static = std::dynamic_pointer_cast<aggregate>(parent)!=nullptr ? is_static : false;
    auto fn = std::shared_ptr<function>(new function(std::move(parent), is_static));
    fn->assign_name(name);
    return fn;
}

void function::update_mangled_name() {
    if (_is_extern) {
        // Extern functions use their explicit C symbol, or the short name as fallback.
        _mangled_name = _extern_c_symbol.value_or(get_short_name());
    } else {
        _mangled_name = mangler(get_context()).mangle_function(*this);
    }
}

void function::create_this_parameter() {
    if (is_member() && !_this_param) {
        auto struct_type = get_owner()->get_struct_type();
        std::shared_ptr<type> this_type;
        if (_is_const_member) {
            // const member function: this is ref<const T>
            this_type = struct_type->get_const()->get_reference();
        } else {
            // mutable member function: this is ref<T>
            this_type = struct_type->get_reference();
        }
        _this_param = parameter::make_shared(shared_as<function>(), "this", this_type, -1);
        _this_param->set_parent(shared_from_this());
    }
}

void function::accept(model_visitor &visitor) {
    visitor.visit_function(*this);
}

void function::set_block(const std::shared_ptr<block>& block) {
    _block = block;
    _block->set_as_parent(shared_as<function>());
}

std::shared_ptr<block> function::get_block() {
    if (!_block) {
        _block = std::make_shared<block>(shared_as<function>());
        _block->set_as_parent(shared_as<function>());
    }
    return _block;
}

std::shared_ptr<block> function::get_existing_block() {
    return _block;
}

std::shared_ptr<const block> function::get_existing_block() const {
    return _block;
}

bool function::is_member() const {
    return std::dynamic_pointer_cast<const aggregate>(parent<element>()) != nullptr;
}

std::shared_ptr<const aggregate> function::function::get_owner() const {
    return std::dynamic_pointer_cast<const aggregate>(parent<element>());
}

std::shared_ptr<aggregate> function::function::get_owner() {
    return std::dynamic_pointer_cast<aggregate>(parent<element>());
}

void function::set_return_type(std::shared_ptr<type> return_type) {
    _return_type = return_type;
}

std::shared_ptr<variable_definition> function::append_variable(const std::string& name, bool is_static) {
    // DO NOT USE METHOD, USE append_parameter instead
    // TODO throw exception
    std::cerr << "Error: function::append_variable is not supported, use append_parameter instead." << std::endl;
    return nullptr;
}

std::shared_ptr<variable_definition> function::do_create_variable(const std::string &name, bool is_static) {
    if (is_static) {
        std::clog << "A function parameter cannot be declared static : " << get_fq_name() << "::" << name << ", ignore it" << std::endl;
    }
    return _parameters.emplace_back(parameter::make_shared(shared_as<function>(), name, _parameters.size()));
}

void function::on_variable_defined(std::shared_ptr<variable_definition>) {
    // Do nothing : parameter already pushed on the list.
}

std::shared_ptr<parameter> function::append_parameter(const std::string &name, std::shared_ptr<type> type) {
    auto param = _parameters.emplace_back(parameter::make_shared(shared_as<function>(), name, type, _parameters.size()));
    _vars[name] = param;
    return param;
}

std::shared_ptr<parameter> function::insert_parameter(const std::string &name, std::shared_ptr<type> type, size_t pos) {
    if (pos >= _parameters.size()) {
        size_t idx = _parameters.size();
        while (idx < pos) {
            _parameters.emplace_back(parameter::make_shared(shared_as<function>(), idx));
            idx = _parameters.size();
        }
        auto param = _parameters.emplace_back(parameter::make_shared(shared_as<function>(), name, type, idx));
        _vars[name] = param;
        return param;
    } else {
        auto res = _parameters.emplace(_parameters.begin() + pos, parameter::make_shared(shared_as<function>(), name, type, pos));
        _vars[name] = *res;
        auto it = res;
        while (++it != _parameters.end()) {
            it->get()->_pos++;
        }
        return *res;
    }
}

std::shared_ptr<parameter> function::get_parameter(size_t index) {
    if (index < _parameters.size()) {
        return _parameters.at(index);
    } else {
        size_t idx = _parameters.size();
        while (idx <= index) {
            _parameters.emplace_back(new parameter(shared_as<function>(), idx));
        }
        return _parameters.back();
    }
}

std::shared_ptr<const parameter> function::get_parameter(size_t index) const {
    if (index < _parameters.size()) {
        return _parameters.at(index);
    } else {
        return nullptr;
    }
}

std::shared_ptr<parameter> function::get_parameter(const std::string &name) {
    for (auto param: _parameters) {
        if (param->get_short_name() == name) {
            return param;
        }
    }
    return {};
}

std::shared_ptr<const parameter> function::get_parameter(const std::string &name) const {
    for (auto param: _parameters) {
        if (param->get_short_name() == name) {
            return param;
        }
    }
    return {};
}

//
// Constructor
//

void constructor::accept(model_visitor& visitor) {
    visitor.visit_constructor(*this);
}

void constructor::update_mangled_name() {
    _mangled_name = mangler(get_context()).mangle_constructor(*this);
}

std::shared_ptr<constructor> constructor::make_shared(std::shared_ptr<aggregate> parent) {
    auto fn = std::shared_ptr<constructor>(new constructor(parent));
    fn->assign_name(parent->get_short_name());
    return fn;
}

//
// Destructor
//

void destructor::accept(model_visitor& visitor) {
    visitor.visit_destructor(*this);
}

void destructor::update_mangled_name() {
    _mangled_name = mangler(get_context()).mangle_destructor(*this);
}

std::shared_ptr<destructor> destructor::make_shared(std::shared_ptr<aggregate> parent) {
    auto fn = std::shared_ptr<destructor>(new destructor(parent));
    fn->assign_name("~" + parent->get_short_name());
    return fn;
}

//
// Static constructor
//

void static_constructor::accept(model_visitor& visitor) {
    visitor.visit_static_constructor(*this);
}

void static_constructor::update_mangled_name() {
    _mangled_name = mangler(get_context()).mangle_static_constructor(*this);
}

std::shared_ptr<static_constructor> static_constructor::make_shared(std::shared_ptr<aggregate> parent) {
    auto fn = std::shared_ptr<static_constructor>(new static_constructor(parent));
    fn->assign_name(parent->get_short_name());
    return fn;
}

//
// Static destructor
//

void static_destructor::accept(model_visitor& visitor) {
    visitor.visit_static_destructor(*this);
}

void static_destructor::update_mangled_name() {
    _mangled_name = mangler(get_context()).mangle_static_destructor(*this);
}

std::shared_ptr<static_destructor> static_destructor::make_shared(std::shared_ptr<aggregate> parent) {
    auto fn = std::shared_ptr<static_destructor>(new static_destructor(parent));
    fn->assign_name("~" + parent->get_short_name());
    return fn;
}

//
// Global tool function
//

void global_tool_function::accept(model_visitor &visitor) {
    visitor.visit_global_tool_function(*this);
}

void global_tool_function::update_mangled_name() {
    // No mangle for this special functions
    _mangled_name = get_short_name();
}

void global_tool_function::add_global_variable_definition(const std::shared_ptr<global_variable_definition>& gv) {
    _global_vars.push_back(gv);
}

void global_tool_function::add_static_constructor(const std::shared_ptr<static_constructor>& sctor) {
    _static_ctors.push_back(sctor);
}

void global_tool_function::add_static_function(const std::shared_ptr<function>& func) {
    if (auto sctor = std::dynamic_pointer_cast<static_constructor>(func)) {
        add_static_constructor(sctor);
    }
    // static_destructor registration is handled separately via destructor global_tool_function
}

std::vector<std::shared_ptr<global_variable_definition>> global_tool_function::get_sorted_global_variables() const {
    std::vector<std::shared_ptr<global_variable_definition>> res;
    for (auto& item : _ordered_items) {
        if (auto gv = std::get_if<std::shared_ptr<global_variable_definition>>(&item)) {
            res.push_back(*gv);
        }
    }
    return res;
}

std::vector<std::shared_ptr<function>> global_tool_function::get_static_functions() const {
    std::vector<std::shared_ptr<function>> res;
    for (auto& item : _ordered_items) {
        if (auto sc = std::get_if<std::shared_ptr<static_constructor>>(&item)) {
            res.push_back(*sc);
        }
    }

    return res;
}

//
// Global constructor function
//
global_constructor_function::global_constructor_function(std::shared_ptr<element> parent) :
global_tool_function(parent)
{}


void global_constructor_function::accept(model_visitor &visitor) {
    visitor.visit_global_constructor_function(*this);
}

//
// Global destructor function
//
global_destructor_function::global_destructor_function(std::shared_ptr<element> parent) :
global_tool_function(parent)
{}

void global_destructor_function::accept(model_visitor &visitor) {
    visitor.visit_global_destructor_function(*this);
}

//
// Global main function
//
global_main_function::global_main_function(std::shared_ptr<element> parent, std::shared_ptr<function> real_main_func) :
function(parent),
_real_main_func(real_main_func)
{
}

void global_main_function::update_mangled_name() {
    // No mangle for this special functions
    _mangled_name = get_short_name(); // Must be "main"
}

void global_main_function::accept(model_visitor& visitor) {
    visitor.visit_function(*this);
}

//
// Member variable definition
//
member_variable_definition::member_variable_definition(std::shared_ptr<aggregate> st) :
        element(st){}

std::shared_ptr<member_variable_definition> member_variable_definition::make_shared(std::shared_ptr<aggregate> st, const std::string &name) {
    auto var_def =  std::shared_ptr<member_variable_definition>(new member_variable_definition(std::move(st)));
    var_def->init(name);
    return var_def;
}

void member_variable_definition::update_mangled_name() {
    // TODO Implement mangling scheme
}

void member_variable_definition::accept(model_visitor &visitor) {
    visitor.visit_member_variable_definition(*this);
}


//
// Structure
//

// (make_shared and accept defined below, together with aggregate)

//
// Klass
//

// (make_shared and accept defined below, together with aggregate)

//
// Aggregate
//

void aggregate::update_mangled_name() {
    if (!_name.has_root_prefix()) {
        _mangled_name = "";
        return;
    }
    if (has_tpl_args()) {
        _mangled_name = mangler(get_context()).mangle_structure(*this);
    } else {
        _mangled_name = mangler::mangle_structure(_name);
    }
}

void aggregate::accept(model_visitor& visitor) {
    visitor.visit_aggregate(*this);
}

//
// Structure
//

std::shared_ptr<structure> structure::make_shared(std::shared_ptr<element> parent, const std::string &name) {
    auto st = std::shared_ptr<structure>(new structure(std::move(parent)));
    st->assign_name(name);
    return st;
}

void structure::accept(model_visitor& visitor) {
    visitor.visit_structure(*this);
}

//
// Annotation type
//

std::shared_ptr<annotation_type> annotation_type::make_shared(std::shared_ptr<element> parent, const std::string &name) {
    auto ann = std::shared_ptr<annotation_type>(new annotation_type(std::move(parent)));
    ann->assign_name(name);
    return ann;
}

void annotation_type::accept(model_visitor& visitor) {
    visitor.visit_annotation_type(*this);
}

bool annotation_type::is_source_retention() const {
    for (auto& meta : get_annotations()) {
        if (!meta.resolved_type) continue;
        std::string meta_fqn = meta.resolved_type->get_fq_name();
        if (meta_fqn != "k::annotations::Retention"
            && meta_fqn != "::k::annotations::Retention"
            && meta.raw_name != "Retention") continue;

        // Examine the AST to find the Policy value
        if (!meta.ast_node) continue;
        auto* ast = meta.ast_node.get();

        // Helper: check if an expression is Policy::SOURCE
        auto is_source_expr = [](const k::parse::ast::expr_ptr& expr) -> bool {
            if (auto ident = std::dynamic_pointer_cast<k::parse::ast::identifier_expr>(expr)) {
                if (!ident->qident.names.empty()) {
                    std::string last{ident->qident.names.back().content};
                    return last == "SOURCE";
                }
            }
            return false;
        };

        // @Retention(Policy::SOURCE) — positional arg
        if (ast->has_parens && !ast->args.empty()) {
            if (is_source_expr(ast->args[0])) return true;
        }
        // @Retention{.policy = Policy::SOURCE} or @Retention{.policy(Policy::SOURCE)}
        else if (ast->brace_init && ast->brace_init->is_designated) {
            for (auto& elem : ast->brace_init->elements) {
                auto desig = std::dynamic_pointer_cast<k::parse::ast::designated_init_element>(elem);
                if (!desig) continue;
                std::string name{desig->member_name.content};
                if (name != "policy") continue;
                if (desig->is_call_form && !desig->args.empty()) {
                    if (is_source_expr(desig->args[0])) return true;
                } else if (desig->value) {
                    if (is_source_expr(desig->value)) return true;
                }
            }
        }
        // @Retention{Policy::SOURCE} — positional brace-init
        else if (ast->brace_init && !ast->brace_init->is_designated) {
            if (!ast->brace_init->elements.empty()) {
                if (is_source_expr(ast->brace_init->elements[0])) return true;
            }
        }
    }
    return false;
}

//
// Klass
//

std::shared_ptr<klass> klass::make_shared(std::shared_ptr<element> parent, const std::string &name) {
    auto kl = std::shared_ptr<klass>(new klass(std::move(parent)));
    kl->assign_name(name);
    return kl;
}

void klass::accept(model_visitor& visitor) {
    visitor.visit_klass(*this);
}

//
// Interface
//

std::shared_ptr<interface> interface::make_shared(std::shared_ptr<element> parent, const std::string &name) {
    auto iface = std::shared_ptr<interface>(new interface(std::move(parent)));
    iface->assign_name(name);
    return iface;
}

void interface::accept(model_visitor& visitor) {
    visitor.visit_interface(*this);
}

std::shared_ptr<function> aggregate::define_function(const std::string &name, bool is_static) {
    if (name == get_short_name()) {
        if (is_static) {
            // Static constructor (class initializer)
            if (_static_constructor) {
                std::cerr << "Error: aggregate " << get_short_name() << " already has a static constructor." << std::endl;
                return _static_constructor;
            }
            auto sctor = static_constructor::make_shared(shared_as<aggregate>());
            if (sctor) {
                _static_constructor = sctor;
                _children.push_back(sctor);
            }
            return sctor;
        } else {
            auto construct = constructor::make_shared(shared_as<aggregate>());
            if (construct) {
                _constructors.push_back(construct);
                _children.push_back(construct);
            }
            return construct;
        }
    } else if (name == "~" + get_short_name()) {
        if (is_static) {
            // Static destructor (class finalizer)
            if (_static_destructor) {
                std::cerr << "Error: aggregate " << get_short_name() << " already has a static destructor." << std::endl;
                return _static_destructor;
            }
            auto sdtor = static_destructor::make_shared(shared_as<aggregate>());
            if (sdtor) {
                _static_destructor = sdtor;
                _children.push_back(sdtor);
            }
            return sdtor;
        } else {
            if (_destructor) {
                std::cerr << "Error: aggregate " << get_short_name() << " already has a destructor." << std::endl;
                return _destructor;
            }
            auto dtor = destructor::make_shared(shared_as<aggregate>());
            if (dtor) {
                _destructor = dtor;
                _children.push_back(dtor);
            }
            return dtor;
        }
    } else {
        return function_holder::define_function(name, is_static);
    }
}

std::shared_ptr<function> aggregate::do_create_function(const std::string &name, bool is_static) {
    std::shared_ptr<aggregate> this_agg = shared_as<aggregate>();
    return std::shared_ptr<function>{function::make_shared(this_agg, name, is_static)};
}

void aggregate::on_function_defined(std::shared_ptr<function> func) {
    _children.push_back(func);
}

std::shared_ptr<variable_definition> aggregate::do_create_variable(const std::string &name, bool is_static) {
    if (is_static) {
        return std::shared_ptr<variable_definition>(global_variable_definition::make_shared(shared_as<aggregate>(), name));
    } else {
        return std::shared_ptr<variable_definition>(member_variable_definition::make_shared(shared_as<aggregate>(), name));
    }
}

void aggregate::on_variable_defined(std::shared_ptr<variable_definition> var) {
    if(std::dynamic_pointer_cast<member_variable_definition>(var) != nullptr || std::dynamic_pointer_cast<global_variable_definition>(var) != nullptr ) {
        _children.push_back(std::dynamic_pointer_cast<element>(var));
    } else {
        std::cerr << "Try to register an unsupported type of variable as member of aggregate" << std::endl;
    }
}

std::shared_ptr<structure> aggregate::do_create_structure(const std::string &name) {
    return structure::make_shared(shared_as<aggregate>(), name);
}

std::shared_ptr<klass> aggregate::do_create_class(const std::string &name) {
    return klass::make_shared(shared_as<aggregate>(), name);
}

std::shared_ptr<interface> aggregate::do_create_interface(const std::string &name) {
    return interface::make_shared(shared_as<aggregate>(), name);
}

std::shared_ptr<annotation_type> aggregate::do_create_annotation(const std::string &name) {
    return annotation_type::make_shared(shared_as<aggregate>(), name);
}

void aggregate::on_aggregate_defined(std::shared_ptr<aggregate> agg) {
    _children.push_back(agg);
}

std::shared_ptr<enumeration> aggregate::do_create_enum(const std::string &name) {
    return enumeration::make_shared(shared_as<aggregate>(), name);
}

void aggregate::on_enum_defined(std::shared_ptr<enumeration> en) {
    _children.push_back(en);
}

bool aggregate::is_derived_from(const std::shared_ptr<aggregate>& base_st) const {
    for (auto& bs : _bases) {
        if (!bs.base) continue;
        if (bs.base == base_st) return true;
        if (bs.base->is_derived_from(base_st)) return true;
    }
    return false;
}

std::vector<base_spec> aggregate::get_all_bases() const {
    std::vector<base_spec> result;
    for (auto& bs : _bases) {
        if (!bs.base) continue;
        result.push_back(bs);
        auto sub_bases = bs.base->get_all_bases();
        result.insert(result.end(), sub_bases.begin(), sub_bases.end());
    }
    return result;
}

std::vector<std::shared_ptr<aggregate>> aggregate::get_all_virtual_base_structs() const {
    std::vector<std::shared_ptr<aggregate>> result;
    std::unordered_set<const aggregate*> seen;
    std::queue<const aggregate*> q;
    q.push(this);
    while (!q.empty()) {
        const aggregate* cur = q.front(); q.pop();
        for (auto& bs : cur->get_bases()) {
            if (!bs.base) continue;
            if (bs.is_virtual) {
                if (!seen.count(bs.base.get())) {
                    seen.insert(bs.base.get());
                    result.push_back(bs.base);
                }
                q.push(bs.base.get());
            } else {
                q.push(bs.base.get());
            }
        }
    }
    return result;
}

void klass::compute_virtual_bases(const std::vector<std::shared_ptr<aggregate>>& all_aggregates) {
    // Step 1: count how many times each aggregate appears in the full base graph
    // of each class. Aggregates reached more than once are diamond bases.
    std::function<void(const aggregate*, std::unordered_map<const aggregate*, int>&)> count_class_bases;
    count_class_bases = [&](const aggregate* cur, std::unordered_map<const aggregate*, int>& counts) {
        for (auto& bs : cur->get_bases()) {
            if (!bs.base || !bs.base->is_class()) continue;
            counts[bs.base.get()]++;
            count_class_bases(bs.base.get(), counts);
        }
    };

    // Collect all diamond-base pairs: (intermediate, diamond_base)
    // so we can mark intermediate→diamond_base as virtual in the intermediate class.
    // Maps intermediate aggregate → set of diamond bases it should treat as virtual.
    std::unordered_map<const aggregate*, std::unordered_set<const aggregate*>> needs_virtual;

    for (auto& agg : all_aggregates) {
        if (!agg || !agg->is_class()) continue;

        std::unordered_map<const aggregate*, int> base_count;
        count_class_bases(agg.get(), base_count);

        std::unordered_set<const aggregate*> diamond_bases;
        for (auto& [base_ptr, count] : base_count) {
            if (count > 1) diamond_bases.insert(base_ptr);
        }

        if (diamond_bases.empty()) continue;

        // For every node in the base graph of agg that has a diamond_base as a direct base,
        // mark that edge as virtual. This includes the direct bases of agg itself (D→B, D→C)
        // AND the intermediate bases (B→A, C→A).
        std::function<void(aggregate*)> mark_virtual_edges;
        mark_virtual_edges = [&](aggregate* cur) {
            for (auto& bs : cur->get_bases_mutable()) {
                if (!bs.base || !bs.base->is_class()) continue;
                if (diamond_bases.count(bs.base.get())) {
                    // This edge (cur→bs.base) leads directly to a diamond base: mark virtual.
                    bs.is_virtual = true;
                    needs_virtual[cur].insert(bs.base.get());
                } else {
                    // This edge leads to an intermediate. Recurse to mark deeper edges.
                    mark_virtual_edges(bs.base.get());
                }
            }
        };
        mark_virtual_edges(agg.get());
    }
}

bool klass::has_abstract_vtable_slots() const {
    if (!_vtable) return false;
    for (auto& entry : _vtable->entries) {
        if (entry.func && entry.func->is_abstract_func()) return true;
    }
    return false;
}

std::shared_ptr<constructor> aggregate::get_copy_constructor() const {
    for (auto& ctor : _constructors) {
        if (ctor->is_copy_constructor()) return ctor;
        if (ctor->get_parameter_size() == 1) {
            auto p0 = ctor->get_parameter(0);
            if (p0) {
                auto pt = p0->get_type();
                if (type::is_const(pt)) pt = type::remove_const(pt);
                if (auto ref = std::dynamic_pointer_cast<reference_type>(pt)) {
                    auto sub = ref->get_referenced_type();
                    if (type::is_const(sub)) sub = type::remove_const(sub);
                    if (auto st = std::dynamic_pointer_cast<struct_type>(sub)) {
                        if (st->get_struct() && st->get_struct().get() == this) {
                            return ctor;
                        }
                    }
                    if (auto unres = std::dynamic_pointer_cast<unresolved_type>(sub)) {
                        if (unres->type_id().to_string() == get_short_name()) {
                            return ctor;
                        }
                    }
                }
            }
        }
    }
    return nullptr;
}

//
// Global variable definition
//
global_variable_definition::global_variable_definition(std::shared_ptr<variable_holder> parent) :
        element(std::dynamic_pointer_cast<element>(parent)) {}

std::shared_ptr<global_variable_definition> global_variable_definition::make_shared(std::shared_ptr<variable_holder> parent, const std::string& name) {
    auto var_def = std::shared_ptr<global_variable_definition>(new global_variable_definition(std::move(parent)));
    var_def->init(name);
    return var_def;
}

void global_variable_definition::update_mangled_name() {
    _mangled_name = _name.has_root_prefix() ? mangler::mangle_global_variable(_name) : "";
}


void global_variable_definition::accept(model_visitor &visitor) {
    visitor.visit_global_variable_definition(*this);
}

//
// Namespace
//

std::shared_ptr<ns> ns::make_shared(std::shared_ptr<element> parent, const std::string &name) {
    auto nspace = std::shared_ptr<ns>(new ns(parent));
    nspace->assign_name(name);
    return nspace;
}

void ns::update_mangled_name() {
    // Useless but for information
    _mangled_name = _name.has_root_prefix() ? mangler::mangle_namespace(_name) : "";
}

void ns::accept(model_visitor &visitor) {
    visitor.visit_namespace(*this);
}

std::shared_ptr<ns> ns::get_child_namespace(const std::string &child_name) {
    auto it = _ns.find(child_name);
    std::shared_ptr<ns> namesp;
    if (it == _ns.end()) {
        namesp = std::shared_ptr<ns>(ns::make_shared(shared_as<ns>(), child_name));
        _ns.insert({child_name, namesp});
        _children.push_back(namesp);
    } else {
        namesp = it->second;
    }
    return namesp;
}

std::shared_ptr<const ns> ns::get_child_namespace(const std::string &child_name) const {
    auto it = _ns.find(child_name);
    if (it != _ns.end()) {
        return it->second;
    } else {
        return {};
    }
}

std::shared_ptr<variable_definition> ns::do_create_variable(const std::string &name, bool is_static) {
    if (is_static) {
        std::clog << "A global variable cannot be declared static : " << get_fq_name() << "::" << name << ", ignore it" << std::endl;
    }
    return std::shared_ptr<variable_definition>(global_variable_definition::make_shared(std::dynamic_pointer_cast<ns>(shared_from_this()), name));
}

void ns::on_variable_defined(std::shared_ptr<variable_definition> var) {
    if(auto v = std::dynamic_pointer_cast<global_variable_definition>(var)) {
        _children.push_back(v);
    }
}

std::shared_ptr<function> ns::do_create_function(const std::string &name, bool is_static) {
    if (is_static) {
        std::clog << "A global function cannot be declared static : " << get_fq_name() << "::" << name << ", ignore it" << std::endl;
    }
    std::shared_ptr<ns> this_ns = shared_as<ns>();
    return std::shared_ptr<function>{function::make_shared(this_ns, name)};
}

void ns::on_function_defined(std::shared_ptr<function> func) {
    _children.push_back(func);
}

std::shared_ptr<structure> ns::do_create_structure(const std::string &name) {
    return std::shared_ptr<structure>(structure::make_shared(std::dynamic_pointer_cast<ns>(shared_from_this()), name));
}

std::shared_ptr<klass> ns::do_create_class(const std::string &name) {
    return std::shared_ptr<klass>(klass::make_shared(std::dynamic_pointer_cast<ns>(shared_from_this()), name));
}

std::shared_ptr<interface> ns::do_create_interface(const std::string &name) {
    return std::shared_ptr<interface>(interface::make_shared(std::dynamic_pointer_cast<ns>(shared_from_this()), name));
}

std::shared_ptr<annotation_type> ns::do_create_annotation(const std::string &name) {
    return std::shared_ptr<annotation_type>(annotation_type::make_shared(std::dynamic_pointer_cast<ns>(shared_from_this()), name));
}

void ns::on_aggregate_defined(std::shared_ptr<aggregate> agg) {
    _children.push_back(agg);
}

std::shared_ptr<enumeration> ns::do_create_enum(const std::string &name) {
    return enumeration::make_shared(std::dynamic_pointer_cast<ns>(shared_from_this()), name);
}

void ns::on_enum_defined(std::shared_ptr<enumeration> en) {
    _children.push_back(en);
}

std::shared_ptr<union_type_def> ns::do_create_union(const std::string &name) {
    return union_type_def::make_shared(std::dynamic_pointer_cast<ns>(shared_from_this()), name);
}

void ns::on_union_defined(std::shared_ptr<union_type_def> un) {
    _children.push_back(un);
}



//
// Unit
//

std::shared_ptr<unit> unit::create(std::shared_ptr<context> context) {
    return std::shared_ptr<unit>(new unit(context));
}

unit::unit(std::shared_ptr<context> context):
element(nullptr),
_context(context)
{
}

void unit::accept(model_visitor &visitor) {
    visitor.visit_unit(*this);
}

void unit::set_unit_name(const name& unit_name) {
    _unit_name = unit_name.without_root_prefix();
    get_root_namespace()->assign_name(unit_name.with_root_prefix());
}

std::shared_ptr<ns> unit::get_root_namespace() {
    if(!_root_ns) {
        _root_ns = ns::make_shared(shared_as<unit>(), "");
        _global_constructor_func = std::shared_ptr<global_constructor_function>( new global_constructor_function(_root_ns) );
        _global_constructor_func->assign_name(name(true, "__K_global_init"));
        _global_destructor_func = std::shared_ptr<global_destructor_function>( new global_destructor_function(_root_ns) );
        _global_destructor_func->assign_name(name(true, "__K_global_finit"));
    }
    return _root_ns;
}

std::shared_ptr<global_main_function> unit::generate_main_function(std::shared_ptr<function> func) {
    _global_main_func = std::shared_ptr<global_main_function>(new global_main_function(shared_as<element>(), func));
    return _global_main_func;
}

void unit::add_import(const k::name& module_name) {
    // Avoid duplicates
    for (auto& imp : _imported_modules) {
        if (imp.module_name == module_name) return;
    }
    _imported_modules.push_back(imported_module{module_name});
}

imported_module* unit::find_import(const k::name& module_name) {
    for (auto& imp : _imported_modules) {
        if (imp.module_name == module_name) return &imp;
    }
    return nullptr;
}

const imported_module* unit::find_import(const k::name& module_name) const {
    for (const auto& imp : _imported_modules) {
        if (imp.module_name == module_name) return &imp;
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Implementations of imported_* model nodes and unit::find_imported_* /
// unit::get_or_create_imported_* have been moved to imported.cpp.
// ─────────────────────────────────────────────────────────────────────────────


} // namespace k::model
