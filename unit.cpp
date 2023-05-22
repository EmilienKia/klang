//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>
//

#include "unit.hpp"

#include "tools.hpp"

namespace k::unit {


static name to_name(const parse::ast::qualified_identifier& ident) {
    std::vector<std::string> idents;
    for(const auto& id : ident.names) {
        idents.push_back(id.content);
    }
    return {ident.has_root_prefix(), idents};
}

//
// Unresolved type
//

std::shared_ptr<type> unresolved_type::from_string(const std::string& type_name)
{
    return std::shared_ptr<type>{new unresolved_type(name(type_name))};
}

std::shared_ptr<type> unresolved_type::from_identifier(const name& type_id)
{
    return std::shared_ptr<type>{new unresolved_type(type_id)};
}

std::shared_ptr<type> unresolved_type::from_type_specifier(const k::parse::ast::type_specifier& type_spec)
{
    return std::shared_ptr<type>{new unresolved_type(to_name(type_spec.name))};
}

//
// Value expression
//

std::shared_ptr<value_expression> value_expression::from_literal(const k::lex::any_literal& literal)
{
    return std::shared_ptr<value_expression>(new value_expression(literal));
}

//
// Unresolved variable expression
//
std::shared_ptr<variable_expression> unresolved_variable_expression::from_string(const std::string& type_name)
{
    return std::shared_ptr<variable_expression>(new unresolved_variable_expression(name(type_name)));
}

std::shared_ptr<variable_expression> unresolved_variable_expression::from_identifier(const name& type_id)
{
    return std::shared_ptr<variable_expression>(new unresolved_variable_expression(type_id));
}

//
// Block
//

std::shared_ptr<block> block::for_function(std::shared_ptr<function> func)
{
    return std::shared_ptr<block>(new block(func)); //std::make_shared<block>(func);
}

std::shared_ptr<block> block::for_block(std::shared_ptr<block> parent)
{
    return std::shared_ptr<block>(new block(parent)); //std::make_shared<block>(parent);
}

std::shared_ptr<return_statement> block::append_return_statement()
{
    std::shared_ptr<return_statement> stmt{ new return_statement(std::dynamic_pointer_cast<block>(shared_from_this())) };
    _statements.push_back(stmt);
    return stmt;
}

std::shared_ptr<expression_statement> block::append_expression_statement()
{
    std::shared_ptr<expression_statement> stmt{ new expression_statement(std::dynamic_pointer_cast<block>(shared_from_this())) };
    _statements.push_back(stmt);
    return stmt;
}

std::shared_ptr<expression_statement> block::append_expression_statement(const std::shared_ptr<expression>& expr)
{
    if(expr->get_statement()) {
        // Err: an expressions must not be attached yet.
        // TODO add throwing expression.
    }

    std::shared_ptr<expression_statement> stmt{ new expression_statement(std::dynamic_pointer_cast<block>(shared_from_this()), expr) };
    _statements.push_back(stmt);
    return stmt;
}

std::shared_ptr<block> block::append_block_statement()
{
    std::shared_ptr<k::unit::block> blk{new k::unit::block(std::dynamic_pointer_cast<k::unit::block>(shared_from_this()))};
    _statements.push_back(blk);
    return blk;
}


std::shared_ptr<variable_definition> block::append_variable(const std::string& name)
{
    if(_vars.contains(name)) {
        // TODO throw exception : var is already defined.
    }

    std::shared_ptr<variable_statement> var { new variable_statement(std::dynamic_pointer_cast<block>(shared_from_this()), name) };
    _vars[name] = var;
    _statements.push_back(var);
    return var;
}


//
// Parameter
//

parameter::parameter(size_t pos) :
    _pos(pos)
{
}

parameter::parameter(const std::string &name, const std::shared_ptr<type> &type, size_t pos) :
    _name(name), _type(type), _pos(pos)
{
}



//
// Function
//

function::function(std::shared_ptr<ns> ns, const std::string& name) :
        ns_element(ns->get_unit(), ns),
        _name(name)
{
}

std::shared_ptr<block> function::get_block() {
    if(!_block) {
        _block = block::for_function(shared_from_this());
    }
    return _block;
}

void function::return_type(std::shared_ptr<type> return_type)
{
    _return_type = return_type;
}

std::shared_ptr<parameter> function::append_parameter(const std::string& name, std::shared_ptr<type> type)
{
    return _parameters.emplace_back(new parameter(name, type, _parameters.size()));
}

std::shared_ptr<parameter> function::insert_parameter(const std::string& name, std::shared_ptr<type> type, size_t pos)
{
    if(pos >= _parameters.size()) {
        size_t idx = _parameters.size();
        while(idx<pos) {
            _parameters.emplace_back(new parameter(idx));
        }
        return _parameters.emplace_back(new parameter(name, type, idx));
    } else {
        auto res = _parameters.emplace(_parameters.begin()+pos, new parameter(name, type, pos));
        auto it = res;
        while(++it != _parameters.end()) {
            it->get()->_pos++;
        }
        return *res;
    }
}

std::shared_ptr<parameter> function::get_parameter(size_t index)
{
    if(index<_parameters.size()) {
        return _parameters.at(index);
    } else {
        size_t idx = _parameters.size();
        while(idx<=index) {
            _parameters.emplace_back(new parameter(idx));
        }
        return _parameters.back();
    }
}

std::shared_ptr<const parameter> function::get_parameter(size_t index)const
{
    if(index<_parameters.size()) {
        return _parameters.at(index);
    } else {
        return nullptr;
    }
}

//
// Global variable definition
//
global_variable_definition::global_variable_definition(std::shared_ptr<ns> ns) :
        ns_element(ns->get_unit(), ns) {}

global_variable_definition::global_variable_definition(std::shared_ptr<ns> ns, const std::string &name) :
        ns_element(ns->get_unit(), ns), variable_definition(name) {}


//
// Namespace
//
ns::ns(unit& unit, std::shared_ptr<ns> parent, const std::string& name) :
        ns_element(unit, std::move(parent)),
        _name(name)
{
}

std::shared_ptr<ns> ns::create(unit& unit, std::shared_ptr<ns> parent, const std::string& name) {
    return std::shared_ptr<ns>(new ns(unit, parent, name));
}

std::shared_ptr<ns> ns::get_child_namespace(const std::string& child_name)
{
    auto it = _ns.find(child_name);
    std::shared_ptr<ns> namesp;
    if(it==_ns.end()) {
        namesp = std::shared_ptr<ns>(new ns(_unit, shared_from_this(), child_name));
        _ns.insert({child_name, namesp});
        _children.push_back(namesp);
    } else {
        namesp = it->second;
    }
    return namesp;
}

std::shared_ptr<const ns> ns::get_child_namespace(const std::string& child_name)const
{
    auto it = _ns.find(child_name);
    if ( it!=_ns.end() ) {
        return it->second;
    } else {
        return {};
    }
}

std::shared_ptr<function> ns::define_function(const std::string& name)
{
    std::shared_ptr<ns> this_ns = this->shared_from_this();
    std::shared_ptr<function> func {new function(this_ns, name)};
    _children.push_back(func);
    return func;
}

std::shared_ptr<variable_definition> ns::append_variable(const std::string &name)
{
    if(_vars.contains(name)) {
        // TODO throw exception : var is already defined.
    }
    std::shared_ptr<global_variable_definition> var { new global_variable_definition(std::dynamic_pointer_cast<ns>(shared_from_this()), name) };
    _vars[name] = var;
    _children.push_back(var);
    return var;
}

//
// Unit
//

unit::unit() :
    _root_ns(ns::create(*this, nullptr, ""))
{
}

std::shared_ptr<ns> unit::find_namespace(std::string_view name) {

    // TODO

    return std::shared_ptr<ns>();
}


std::shared_ptr<const ns> unit::find_namespace(std::string_view name) const
{
    // TODO

    return {};
}

//void unit::add_import(const std::string &import_name) {
//}


} // k::unit
