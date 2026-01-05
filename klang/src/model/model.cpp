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

#include "../common/tools.hpp"


namespace k::model {


static name to_name(const parse::ast::qualified_identifier &ident) {
    std::vector<std::string> idents;
    for (const auto &id: ident.names) {
        idents.push_back(id.content);
    }
    return {ident.has_root_prefix(), idents};
}

//
// Base model element
//
std::shared_ptr<context> element::get_context() {
    return _context ? _context : ( _parent ? _parent->get_context() : nullptr);
}


//
// Abstract variable holder
//

std::shared_ptr<variable_definition> variable_holder::append_variable(const std::string& name) {
    if (_vars.contains(name)) {
        // TODO throw exception : var is already defined.
    }
    std::shared_ptr<variable_definition> var = do_create_variable(name);
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

std::shared_ptr<variable_definition> variable_holder::lookup_variable(const std::string& name) const {
    // TODO add type checking
    // TODO add qualified name lookup
    if (auto var = get_variable(name)) {
        return var;
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

std::shared_ptr<function> function_holder::define_function(const std::string &name) {
    std::shared_ptr<function> func = do_create_function(name);
    _functions.push_back(func);
    on_function_defined(func);
    return func;
}

std::shared_ptr<function> function_holder::get_function(const std::string &name) const {
    // TODO add prototype checking
    for (auto func: _functions) {
        if (func->name() == name) {
            return func;
        }
    }
    return nullptr;
}

std::shared_ptr<function> function_holder::lookup_function(const std::string &name) const {
    // TODO add prototype checking
    if (auto func = get_function(name)) {
        return func;
    } else {
        return nullptr;
    }
}

//
// Abstract structure holder
//

std::shared_ptr<structure> structure_holder::define_structure(const std::string &name) {
    std::shared_ptr<structure> st = do_create_structure(name);
    _structs.insert({name, st});
    on_structure_defined(st);
    return st;
}

std::shared_ptr<structure> structure_holder::get_structure(const std::string &name) {
    auto it = _structs.find(name);
    if (it != _structs.end()) {
        return it->second;
    } else {
        return {};
    }
}

std::shared_ptr<structure> structure_holder::lookup_structure(const std::string &name) {
    if (auto st = get_structure(name)) {
        return st;
    } else {
        return nullptr;
    }
}


//
// Variable definition
//

const std::string& variable_definition::get_name() const {
    return _name;
}

std::shared_ptr<type> variable_definition::get_type() const {
    return _type;
}

std::shared_ptr<expression> variable_definition::get_init_expr() const {
    return _expression;
}

variable_definition& variable_definition::set_type(std::shared_ptr<type> type) {
    _type = type;
    return *this;
}

variable_definition& variable_definition::set_init_expr(std::shared_ptr<expression> init_expr) {
    _expression = init_expr;
    return *this;
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

parameter::parameter(std::shared_ptr<function> func, const std::string &name, size_t pos) :
        element(func),
        variable_definition(name), _function(std::move(func)), _pos(pos) {
}


parameter::parameter(std::shared_ptr<function> func, const std::string &name, const std::shared_ptr<type> &type,
                     size_t pos) :
        element(func),
        variable_definition(name, type), _function(std::move(func)), _pos(pos) {
}

void parameter::accept(model_visitor& visitor) {
    visitor.visit_parameter(*this);
}

//
// Function
//

function::function(std::shared_ptr<element> parent, const std::string &name) :
        element(parent),
        _name(name) {
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
        _block = std::make_shared<block>(_context, shared_as<function>());
        _block->set_as_parent(shared_as<function>());
    }
    return _block;
}

bool function::is_member() const {
    return std::dynamic_pointer_cast<const structure>(parent<element>()) != nullptr;
}

std::shared_ptr<const structure> function::function::get_owner() const {
    return std::dynamic_pointer_cast<const structure>(parent<element>());
}

std::shared_ptr<structure> function::function::get_owner() {
    return std::dynamic_pointer_cast<structure>(parent<element>());
}

void function::return_type(std::shared_ptr<type> return_type) {
    _return_type = return_type;
}

std::shared_ptr<variable_definition> function::append_variable(const std::string& name) {
    // DO NOT USE METHOD, USE append_parameter instead
    // TODO throw exception
    std::cerr << "Error: function::append_variable is not supported, use append_parameter instead." << std::endl;
    return nullptr;
}

std::shared_ptr<variable_definition> function::do_create_variable(const std::string &name) {
    return _parameters.emplace_back(new parameter(shared_as<function>(), name, _parameters.size()));
}

void function::on_variable_defined(std::shared_ptr<variable_definition>) {
    // Do nothing : parameter already pushed on the list.
}

std::shared_ptr<parameter> function::append_parameter(const std::string &name, std::shared_ptr<type> type) {
    auto param = _parameters.emplace_back(new parameter(shared_as<function>(), name, type, _parameters.size()));
    _vars[name] = param;
    return param;
}

std::shared_ptr<parameter> function::insert_parameter(const std::string &name, std::shared_ptr<type> type, size_t pos) {
    if (pos >= _parameters.size()) {
        size_t idx = _parameters.size();
        while (idx < pos) {
            _parameters.emplace_back(new parameter(shared_as<function>(), idx));
            idx = _parameters.size();
        }
        auto param = _parameters.emplace_back(new parameter(shared_as<function>(), name, type, idx));
        _vars[name] = param;
        return param;
    } else {
        auto res = _parameters.emplace(_parameters.begin() + pos,
                                       new parameter(shared_as<function>(), name, type, pos));
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
        if (param->get_name() == name) {
            return param;
        }
    }
    return {};
}

std::shared_ptr<const parameter> function::get_parameter(const std::string &name) const {
    for (auto param: _parameters) {
        if (param->get_name() == name) {
            return param;
        }
    }
    return {};
}


//
// Member variable definition
//
member_variable_definition::member_variable_definition(std::shared_ptr<structure> st) :
        element(st) {}

member_variable_definition::member_variable_definition(std::shared_ptr<structure> st, const std::string &name) :
        element(st), variable_definition(name) {}

void member_variable_definition::accept(model_visitor &visitor) {
    visitor.visit_member_variable_definition(*this);
}


//
// Structure
//

structure::structure(std::shared_ptr<element> parent, const std::string& name) :
    element(parent),
    _name(name) {
}

void structure::accept(model_visitor& visitor) {
    visitor.visit_structure(*this);
}


std::shared_ptr<function> structure::do_create_function(const std::string &name) {
    std::shared_ptr<structure> this_st = shared_as<structure>();
    return std::shared_ptr<function>{new function(this_st, name)};
}

void structure::on_function_defined(std::shared_ptr<function> func) {
    _children.push_back(func);
}

std::shared_ptr<function> structure::lookup_function(const std::string& name) const {
    // TODO add prototype checking
    if(auto func = function_holder::lookup_function(name)){
        return func;
    } else if (auto fh = ancestor<function_holder>()) {
        // If has a parent namespace, look at it
        return fh->lookup_function(name);
    } else {
        return nullptr;
    }
}


std::shared_ptr<variable_definition> structure::do_create_variable(const std::string &name) {
    return std::shared_ptr<variable_definition>(new member_variable_definition(shared_as<structure>(), name));
}

void structure::on_variable_defined(std::shared_ptr<variable_definition> var) {
    if(auto v = std::dynamic_pointer_cast<member_variable_definition>(var)) {
        _children.push_back(v);
    }
}


std::shared_ptr<variable_definition> structure::lookup_variable(const std::string& name) const {
    // TODO add type checking
    // TODO add qualified name lookup
    if(auto var = variable_holder::lookup_variable(name)){
        return var;
    } else if (auto vh = ancestor<variable_holder>()) {
        return vh->lookup_variable(name);
    } else {
        return {};
    }
}

//
// Global variable definition
//
global_variable_definition::global_variable_definition(std::shared_ptr<ns> ns) :
        element(ns) {}

global_variable_definition::global_variable_definition(std::shared_ptr<ns> ns, const std::string &name) :
        element(ns), variable_definition(name) {}

void global_variable_definition::accept(model_visitor &visitor) {
    visitor.visit_global_variable_definition(*this);
}

//
// Namespace
//
ns::ns(std::shared_ptr<element> parent, const std::string &name) :
        element(parent),
        _name(name) {
}

void ns::accept(model_visitor &visitor) {
    visitor.visit_namespace(*this);
}

std::shared_ptr<ns> ns::create(std::shared_ptr<element> parent, const std::string &name) {
    return std::shared_ptr<ns>(new ns(parent, name));
}

std::shared_ptr<ns> ns::get_child_namespace(const std::string &child_name) {
    auto it = _ns.find(child_name);
    std::shared_ptr<ns> namesp;
    if (it == _ns.end()) {
        namesp = std::shared_ptr<ns>(new ns(shared_as<ns>(), child_name));
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

std::shared_ptr<variable_definition> ns::do_create_variable(const std::string &name) {
    return std::shared_ptr<variable_definition>(new global_variable_definition(std::dynamic_pointer_cast<ns>(shared_from_this()), name));
}

void ns::on_variable_defined(std::shared_ptr<variable_definition> var) {
    if(auto v = std::dynamic_pointer_cast<global_variable_definition>(var)) {
        _children.push_back(v);
    }
}

std::shared_ptr<function> ns::do_create_function(const std::string &name) {
    std::shared_ptr<ns> this_ns = shared_as<ns>();
    return std::shared_ptr<function>{new function(this_ns, name)};
}

void ns::on_function_defined(std::shared_ptr<function> func) {
    _children.push_back(func);
}

std::shared_ptr<structure> ns::do_create_structure(const std::string &name) {
    return std::shared_ptr<structure>(new structure(std::dynamic_pointer_cast<ns>(shared_from_this()), name));
}

void ns::on_structure_defined(std::shared_ptr<structure> st) {
    _children.push_back(st);
}


std::shared_ptr<function> ns::lookup_function(const std::string &name) const {
    // TODO add prototype checking
    if(auto func = function_holder::lookup_function(name)){
        return func;
    } else if (auto fh = ancestor<function_holder>()) {
        // If has a parent namespace, look at it
        return fh->lookup_function(name);
    } else {
        return nullptr;
    }
}

std::shared_ptr<variable_definition> ns::lookup_variable(const std::string &name) const {
    // TODO add type checking
    // TODO add qualified name lookup
    if(auto var = variable_holder::lookup_variable(name)){
        return var;
    } else if (auto vh = ancestor<variable_holder>()) {
        // If has a parent namespace, look at it
        return vh->lookup_variable(name);
    } else {
        return {};
    }
}

std::shared_ptr<structure> ns::lookup_structure(const std::string& name) {
    // TODO add qualified name lookup
    if(auto st = structure_holder::lookup_structure(name)){
        return st;
    } else if (auto sh = ancestor<structure_holder>()) {
        // If has a parent, look at it
        return sh->lookup_structure(name);
    } else {
        return {};
    }
}


//
// Unit
//

std::shared_ptr<unit> unit::create(std::shared_ptr<context> context) {
    return std::shared_ptr<unit>(new unit(context));
}

unit::unit(std::shared_ptr<context> context):
element(context)
{
//    _root_ns = ns::create(shared_as<unit>(), "");
}

void unit::accept(model_visitor &visitor) {
    visitor.visit_unit(*this);
}

std::shared_ptr<ns> unit::get_root_namespace() {
    if(!_root_ns) {
        _root_ns = ns::create(shared_as<unit>(), "");
    }
    return _root_ns;
}

std::shared_ptr<ns> unit::find_namespace(std::string_view name) {

    // TODO

    return std::shared_ptr<ns>();
}


std::shared_ptr<const ns> unit::find_namespace(std::string_view name) const {
    // TODO

    return {};
}

//void model::add_import(const std::string &import_name) {
//}


} // namespace k::model
