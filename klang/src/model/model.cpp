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

#include "../common/tools.hpp"


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
// Abstract structure holder
//

std::shared_ptr<structure> structure_holder::define_structure(const std::string &name) {
    std::shared_ptr<structure> st = do_create_structure(name);
    _structs.insert({name, st});
    on_structure_defined(st);
    return st;
}

std::shared_ptr<structure> structure_holder::get_structure(const std::string &name) const {
    auto it = _structs.find(name);
    if (it != _structs.end()) {
        return it->second;
    } else {
        return {};
    }
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

std::shared_ptr<constructor_invocation_expression> variable_definition::get_init_expr() const {
    return _init_expr;
}


variable_definition& variable_definition::set_type(std::shared_ptr<type> type) {
    _type = type;
    return *this;
}

variable_definition& variable_definition::set_init_expr(std::shared_ptr<constructor_invocation_expression> init_expr) {
    _init_expr = init_expr;
    if (_init_expr) {
        if (auto self = dynamic_cast<element*>(this)) {
            _init_expr->set_parent(self->shared_as<element>());
        }
    }
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
    // Is static is only supported for structure members (not global methods)
    is_static = std::dynamic_pointer_cast<structure>(parent)!=nullptr ? is_static : false;
    auto fn = std::shared_ptr<function>(new function(std::move(parent), is_static));
    fn->assign_name(name);
    return fn;
}

void function::update_mangled_name() {
    _mangled_name = mangler(get_context()).mangle_function(*this);
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

bool function::is_member() const {
    return std::dynamic_pointer_cast<const structure>(parent<element>()) != nullptr;
}

std::shared_ptr<const structure> function::function::get_owner() const {
    return std::dynamic_pointer_cast<const structure>(parent<element>());
}

std::shared_ptr<structure> function::function::get_owner() {
    return std::dynamic_pointer_cast<structure>(parent<element>());
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

std::shared_ptr<constructor> constructor::make_shared(std::shared_ptr<structure> parent) {
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

std::shared_ptr<destructor> destructor::make_shared(std::shared_ptr<structure> parent) {
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

std::shared_ptr<static_constructor> static_constructor::make_shared(std::shared_ptr<structure> parent) {
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

std::shared_ptr<static_destructor> static_destructor::make_shared(std::shared_ptr<structure> parent) {
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
member_variable_definition::member_variable_definition(std::shared_ptr<structure> st) :
        element(st){}

std::shared_ptr<member_variable_definition> member_variable_definition::make_shared(std::shared_ptr<structure> st, const std::string &name) {
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

std::shared_ptr<structure> structure::make_shared(std::shared_ptr<element> parent, const std::string &name) {
    auto st = std::shared_ptr<structure>(new structure(std::move(parent)));
    st->assign_name(name);
    return st;
}

void structure::update_mangled_name() {
    // Useless but for information
    _mangled_name = _name.has_root_prefix() ? mangler::mangle_structure(_name) : "";
}

void structure::accept(model_visitor& visitor) {
    visitor.visit_structure(*this);
}

std::shared_ptr<function> structure::define_function(const std::string &name, bool is_static) {
    if (name == get_short_name()) {
        if (is_static) {
            // Static constructor (class initializer)
            if (_static_constructor) {
                std::cerr << "Error: structure " << get_short_name() << " already has a static constructor." << std::endl;
                return _static_constructor;
            }
            auto sctor = static_constructor::make_shared(shared_as<structure>());
            if (sctor) {
                _static_constructor = sctor;
                _children.push_back(sctor);
            }
            return sctor;
        } else {
            auto construct = constructor::make_shared(shared_as<structure>());
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
                std::cerr << "Error: structure " << get_short_name() << " already has a static destructor." << std::endl;
                return _static_destructor;
            }
            auto sdtor = static_destructor::make_shared(shared_as<structure>());
            if (sdtor) {
                _static_destructor = sdtor;
                _children.push_back(sdtor);
            }
            return sdtor;
        } else {
            if (_destructor) {
                // TODO throw error: only one destructor allowed
                std::cerr << "Error: structure " << get_short_name() << " already has a destructor." << std::endl;
                return _destructor;
            }
            auto dtor = destructor::make_shared(shared_as<structure>());
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

std::shared_ptr<function> structure::do_create_function(const std::string &name, bool is_static) {
    std::shared_ptr<structure> this_st = shared_as<structure>();
    return std::shared_ptr<function>{function::make_shared(this_st, name, is_static)};
}

void structure::on_function_defined(std::shared_ptr<function> func) {
    _children.push_back(func);
}


std::shared_ptr<variable_definition> structure::do_create_variable(const std::string &name, bool is_static) {
    if (is_static) {
        return std::shared_ptr<variable_definition>(global_variable_definition::make_shared(shared_as<structure>(), name));
    } else {
        return std::shared_ptr<variable_definition>(member_variable_definition::make_shared(shared_as<structure>(), name));
    }
}

void structure::on_variable_defined(std::shared_ptr<variable_definition> var) {
    if(std::dynamic_pointer_cast<member_variable_definition>(var) != nullptr || std::dynamic_pointer_cast<global_variable_definition>(var) != nullptr ) {
        _children.push_back(std::dynamic_pointer_cast<element>(var));
    } else {
        std::cerr << "Try to register an unsupported type of variable as member of struct" << std::endl;
    }
}

std::shared_ptr<structure> structure::do_create_structure(const std::string &name) {
    return structure::make_shared(shared_as<structure>(), name);
}

void structure::on_structure_defined(std::shared_ptr<structure> st) {
    _children.push_back(st);
}

bool structure::is_derived_from(const std::shared_ptr<structure>& base_st) const {
    for (auto& bs : _bases) {
        if (!bs.base) continue;
        if (bs.base == base_st) return true;
        if (bs.base->is_derived_from(base_st)) return true;
    }
    return false;
}

std::vector<base_spec> structure::get_all_bases() const {
    std::vector<base_spec> result;
    for (auto& bs : _bases) {
        if (!bs.base) continue;
        result.push_back(bs);
        auto sub_bases = bs.base->get_all_bases();
        result.insert(result.end(), sub_bases.begin(), sub_bases.end());
    }
    return result;
}

std::shared_ptr<constructor> structure::get_copy_constructor() const {
    for (auto& ctor : _constructors) {
        if (ctor->is_copy_constructor()) return ctor;
        // Detect by signature: single parameter of type ThisStruct& or const ThisStruct&
        if (ctor->get_parameter_size() == 1) {
            auto p0 = ctor->get_parameter(0);
            if (p0) {
                auto pt = p0->get_type();
                // Strip const if present (const T& is also a valid copy ctor signature)
                if (type::is_const(pt)) pt = type::remove_const(pt);
                if (auto ref = std::dynamic_pointer_cast<reference_type>(pt)) {
                    auto sub = ref->get_referenced_type();
                    // Strip const from the referenced type too (const Struct& case)
                    if (type::is_const(sub)) sub = type::remove_const(sub);
                    // Already-resolved struct_type
                    if (auto st = std::dynamic_pointer_cast<struct_type>(sub)) {
                        if (st->get_struct() && st->get_struct().get() == this) {
                            return ctor;
                        }
                    }
                    // Not-yet-resolved unresolved_type: match by simple name
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

void ns::on_structure_defined(std::shared_ptr<structure> st) {
    _children.push_back(st);
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

//void model::add_import(const std::string &import_name) {
//}


} // namespace k::model
