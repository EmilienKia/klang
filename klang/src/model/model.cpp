/*
 * K Language compiler
 *
 * Copyright 2023 Emilien Kia
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
// NS element
//
void ns_element::accept(model_visitor &visitor) {
    visitor.visit_ns_element(*this);
}

//
// Parameter
//

parameter::parameter(std::shared_ptr<function> func, size_t pos) :
        _function(std::move(func)), _pos(pos) {
}

parameter::parameter(std::shared_ptr<function> func, const std::string &name, const std::shared_ptr<type> &type,
                     size_t pos) :
        variable_definition(name, type), _function(std::move(func)), _pos(pos) {
}

//
// Function
//

function::function(std::shared_ptr<ns> ns, const std::string &name) :
        ns_element(ns->get_unit(), ns),
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
        _block = std::make_shared<block>();
        _block->set_as_parent(shared_as<function>());
    }
    return _block;
}

void function::return_type(std::shared_ptr<type> return_type) {
    _return_type = return_type;
}

std::shared_ptr<parameter> function::append_parameter(const std::string &name, std::shared_ptr<type> type) {
    return _parameters.emplace_back(new parameter(shared_as<function>(), name, type, _parameters.size()));
}

std::shared_ptr<parameter> function::insert_parameter(const std::string &name, std::shared_ptr<type> type, size_t pos) {
    if (pos >= _parameters.size()) {
        size_t idx = _parameters.size();
        while (idx < pos) {
            _parameters.emplace_back(new parameter(shared_as<function>(), idx));
        }
        return _parameters.emplace_back(new parameter(shared_as<function>(), name, type, idx));
    } else {
        auto res = _parameters.emplace(_parameters.begin() + pos,
                                       new parameter(shared_as<function>(), name, type, pos));
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
// Global variable definition
//
global_variable_definition::global_variable_definition(std::shared_ptr<ns> ns) :
        ns_element(ns->get_unit(), ns) {}

global_variable_definition::global_variable_definition(std::shared_ptr<ns> ns, const std::string &name) :
        ns_element(ns->get_unit(), ns), variable_definition(name) {}

void global_variable_definition::accept(model_visitor &visitor) {
    visitor.visit_global_variable_definition(*this);
}

//
// Namespace
//
ns::ns(unit &unit, std::shared_ptr<ns> parent, const std::string &name) :
        ns_element(unit, std::move(parent)),
        _name(name) {
}

void ns::accept(model_visitor &visitor) {
    visitor.visit_namespace(*this);
}

std::shared_ptr<ns> ns::create(unit &unit, std::shared_ptr<ns> parent, const std::string &name) {
    return std::shared_ptr<ns>(new ns(unit, parent, name));
}

std::shared_ptr<ns> ns::get_child_namespace(const std::string &child_name) {
    auto it = _ns.find(child_name);
    std::shared_ptr<ns> namesp;
    if (it == _ns.end()) {
        namesp = std::shared_ptr<ns>(new ns(_unit, shared_as<ns>(), child_name));
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

std::shared_ptr<function> ns::define_function(const std::string &name) {
    std::shared_ptr<ns> this_ns = shared_as<ns>();
    std::shared_ptr<function> func{new function(this_ns, name)};
    _children.push_back(func);
    return func;
}

std::shared_ptr<function> ns::get_function(const std::string &name) {
    // TODO add prototype checking
    for (auto child: _children) {
        if (auto func = std::dynamic_pointer_cast<function>(child)) {
            if (func->name() == name) {
                return func;
            }
        }
    }
    return nullptr;
}

std::shared_ptr<function> ns::lookup_function(const std::string &name) {
    // TODO add prototype checking
    if (auto func = get_function(name)) {
        return func;
    }

    if (auto ns = parent_ns()) {
        // If has a parent namespace, look at it
        return ns->lookup_function(name);
    }

    return nullptr;
}


std::shared_ptr<variable_definition> ns::append_variable(const std::string &name) {
    if (_vars.contains(name)) {
        // TODO throw exception : var is already defined.
    }
    std::shared_ptr<global_variable_definition> var{
            new global_variable_definition(std::dynamic_pointer_cast<ns>(shared_from_this()), name)};
    _vars[name] = var;
    _children.push_back(var);
    return var;
}

std::shared_ptr<variable_definition> ns::get_variable(const std::string &name) {
    // TODO add type checking
    auto it = _vars.find(name);
    if (it != _vars.end()) {
        return it->second;
    } else {
        return {};
    }
}

std::shared_ptr<variable_definition> ns::lookup_variable(const std::string &name) {
    // TODO add type checking
    // TODO add qualified name lookup
    if (auto var = get_variable(name)) {
        return var;
    }

    if (auto ns = parent_ns()) {
        // If has a parent namespace, look at it
        return ns->lookup_variable(name);
    }

    return {};
}

//
// Unit
//

unit::unit() :
        _root_ns(ns::create(*this, nullptr, "")) {
}

void unit::accept(model_visitor &visitor) {
    visitor.visit_unit(*this);
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
