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
// Expression
//
void expression::accept(element_visitor& visitor) {
    visitor.visit_expression(*this);
}

//
// Value expression
//

value_expression::value_expression(const k::lex::any_literal& literal) :
expression(type_from_literal(literal)),
_literal(literal)
{
}

std::shared_ptr<type> value_expression::type_from_literal(const k::lex::any_literal& literal)
{
    if(std::holds_alternative<lex::integer>(literal)) {
        auto lit = literal.get<lex::integer>();
        switch(lit.size) {
            case k::lex::BYTE:
                return primitive_type::from_type(lit.unsigned_num ? primitive_type::BYTE : primitive_type::CHAR);
            case k::lex::SHORT:
                return primitive_type::from_type(lit.unsigned_num ? primitive_type::UNSIGNED_SHORT : primitive_type::SHORT);
            case k::lex::INT:
                return primitive_type::from_type(lit.unsigned_num ? primitive_type::UNSIGNED_INT : primitive_type::INT);
            case k::lex::LONG:
                return primitive_type::from_type(lit.unsigned_num ? primitive_type::UNSIGNED_LONG : primitive_type::LONG);
            default:
                // TODO Add (unsigned) long long and bigint
                return {};
        }
    } else if(std::holds_alternative<lex::character>(literal)) {
        return primitive_type::from_type(primitive_type::CHAR);
    } else if(std::holds_alternative<lex::boolean>(literal)) {
        return primitive_type::from_type(primitive_type::BOOL);
    } else {
        // TODO handle other literal types
        return nullptr;
    }
}

void value_expression::accept(element_visitor& visitor)
{
    visitor.visit_value_expression(*this);
}

std::shared_ptr<value_expression> value_expression::from_literal(const k::lex::any_literal& literal)
{
    return std::shared_ptr<value_expression>(new value_expression(literal));
}

//
// Symbol expression
//

symbol_expression::symbol_expression(const name& name):
    _name(name)
{}

symbol_expression::symbol_expression(const std::shared_ptr<variable_definition>& var):
    _name(var->get_name()),
    _symbol(var)
{}

symbol_expression::symbol_expression(const std::shared_ptr<function>& func):
    _name(func->name()),
    _symbol(func)
{}


void symbol_expression::accept(element_visitor& visitor) {
        visitor.visit_symbol_expression(*this);
}

std::shared_ptr<symbol_expression> symbol_expression::from_string(const std::string& name)
{
    return std::shared_ptr<symbol_expression>(new symbol_expression(name));
}

std::shared_ptr<symbol_expression> symbol_expression::from_identifier(const name& name)
{
    return std::shared_ptr<symbol_expression>(new symbol_expression(name));
}

void symbol_expression::resolve(std::shared_ptr<variable_definition> var) {
    _symbol = var;
    _type = var->get_type();
}

void symbol_expression::resolve(std::shared_ptr<function> func) {
    _symbol = func;
    // TODO Add function prototype to type
}

//
// Unary expression
//
void unary_expression::accept(element_visitor& visitor) {
    visitor.visit_unary_expression(*this);
}

//
// Binary expression
//
void binary_expression::accept(element_visitor& visitor) {
    visitor.visit_binary_expression(*this);
}

//
// Arithmetic binary expression
//
void arithmetic_binary_expression::accept(element_visitor& visitor) {
    visitor.visit_arithmetic_binary_expression(*this);
}

//
// Addition expression
//
void addition_expression::accept(element_visitor& visitor) {
    visitor.visit_addition_expression(*this);
}

//
// Substraction expression
//
void substraction_expression::accept(element_visitor& visitor) {
    visitor.visit_substraction_expression(*this);
}

//
// Multiplication expression
//
void multiplication_expression::accept(element_visitor& visitor) {
    visitor.visit_multiplication_expression(*this);
}

//
// Division expression
//
void division_expression::accept(element_visitor& visitor) {
    visitor.visit_division_expression(*this);
}

//
// Modulo expression
//
void modulo_expression::accept(element_visitor& visitor) {
    visitor.visit_modulo_expression(*this);
}

//
// Bitwise AND expression
//
void bitwise_and_expression::accept(element_visitor& visitor) {
    visitor.visit_bitwise_and_expression(*this);
}

//
// Bitwise OR expression
//
void bitwise_or_expression::accept(element_visitor& visitor) {
    visitor.visit_bitwise_or_expression(*this);
}

//
// Bitwise XOR expression
//
void bitwise_xor_expression::accept(element_visitor& visitor) {
    visitor.visit_bitwise_xor_expression(*this);
}

//
// Bitwise left shift expression
//
void left_shift_expression::accept(element_visitor& visitor) {
    visitor.visit_left_shift_expression(*this);
}

//
// Bitwise right shift expression
//
void right_shift_expression::accept(element_visitor& visitor) {
    visitor.visit_right_shift_expression(*this);
}

//
// Assignation expression
//
void assignation_expression::accept(element_visitor& visitor) {
    visitor.visit_assignation_expression(*this);
}

//
// Simple assignation expression
//
void simple_assignation_expression::accept(element_visitor& visitor) {
    visitor.visit_simple_assignation_expression(*this);
}


//
// Addition assignation expression
//
void additition_assignation_expression::accept(element_visitor& visitor) {
    visitor.visit_addition_assignation_expression(*this);
}

//
// Substraction assignation expression
//
void substraction_assignation_expression::accept(element_visitor& visitor) {
    visitor.visit_substraction_assignation_expression(*this);
}

//
// Multiplication assignation expression
//
void multiplication_assignation_expression::accept(element_visitor& visitor) {
    visitor.visit_multiplication_assignation_expression(*this);
}

//
// Division assignation expression
//
void division_assignation_expression::accept(element_visitor& visitor) {
    visitor.visit_division_assignation_expression(*this);
}

//
// Modulo assignation expression
//
void modulo_assignation_expression::accept(element_visitor& visitor) {
    visitor.visit_modulo_assignation_expression(*this);
}

//
// Bitwise AND assignation expression
//
void bitwise_and_assignation_expression::accept(element_visitor& visitor) {
    visitor.visit_bitwise_and_assignation_expression(*this);
}

//
// Bitwise OR assignation expression
//
void bitwise_or_assignation_expression::accept(element_visitor& visitor) {
    visitor.visit_bitwise_or_assignation_expression(*this);
}

//
// Bitwise XOR assignation expression
//
void bitwise_xor_assignation_expression::accept(element_visitor& visitor) {
    visitor.visit_bitwise_xor_assignation_expression(*this);
}

//
// Bitwise left shift assignation expression
//
void left_shift_assignation_expression::accept(element_visitor& visitor) {
    visitor.visit_left_shift_assignation_expression(*this);
}

//
// Bitwise right shift assignation expression
//
void right_shift_assignation_expression::accept(element_visitor& visitor) {
    visitor.visit_right_shift_assignation_expression(*this);
}

//
// Modulo assignation expression
//
void cast_expression::accept(element_visitor& visitor) {
    visitor.visit_cast_expression(*this);
}

//
// Function invocation expression
//
void function_invocation_expression::accept(element_visitor& visitor) {
    visitor.visit_function_invocation_expression(*this);
}

//
// Statement
//

void statement::accept(element_visitor& visitor) {
    visitor.visit_statement(*this);
}

//
// Return statement
//
void return_statement::accept(element_visitor& visitor) {
    visitor.visit_return_statement(*this);
}

//
// Expression statement
//
void expression_statement::accept(element_visitor& visitor) {
    visitor.visit_expression_statement(*this);
}

//
// Variable statement
//
void variable_statement::accept(element_visitor& visitor) {
    visitor.visit_variable_statement(*this);
}

//
// Block
//

void block::accept(element_visitor& visitor) {
    visitor.visit_block(*this);
}

std::shared_ptr<block> block::for_function(std::shared_ptr<function> func)
{
    return std::shared_ptr<block>(new block(func)); //std::from_type<block>(func);
}

std::shared_ptr<block> block::for_block(std::shared_ptr<block> parent)
{
    return std::shared_ptr<block>(new block(parent)); //std::from_type<block>(parent);
}

std::shared_ptr<return_statement> block::append_return_statement()
{
    std::shared_ptr<return_statement> stmt{ new return_statement(std::dynamic_pointer_cast<block>(shared_from_this())) };
    _statements.push_back(stmt);
    return stmt;
}

std::shared_ptr<expression_statement> block::append_expression_statement()
{
    auto stmt = expression_statement::make_shared(std::dynamic_pointer_cast<block>(shared_from_this()));
    _statements.push_back(stmt);
    return stmt;
}

std::shared_ptr<expression_statement> block::append_expression_statement(const std::shared_ptr<expression>& expr)
{
    if(expr->get_statement()) {
        // Err: an expressions must not be attached yet.
        // TODO add throwing expression.
    }

    auto stmt = expression_statement::make_shared(std::dynamic_pointer_cast<block>(shared_from_this()), expr);
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

std::shared_ptr<variable_definition> block::get_variable(const std::string& name)
{
    auto it = _vars.find(name);
    if(it != _vars.end()) {
        return it->second;
    } else {
        return {};
    }
}

std::shared_ptr<variable_definition> block::lookup_variable(const std::string& name) {
    // TODO add qualified name lookup
    if(auto var = get_variable(name)) {
        return var;
    }

    if(auto block = get_block()) {
        // Has a parent block, look at it
        return block->lookup_variable(name);
    } else if(auto param = _function->get_parameter(name)){
        // Has a parameter of same name
        return param;
    } else if(auto ns = _function->parent_ns()) {
        // Else base block of a function, look at the enclosing scope (ns)
        return ns->lookup_variable(name);
    }

    return {};
}

//
// NS element
//
void ns_element::accept(element_visitor& visitor) {
    visitor.visit_ns_element(*this);
}

//
// Parameter
//

parameter::parameter(std::shared_ptr<function> func, size_t pos) :
    _function(std::move(func)), _pos(pos)
{
}

parameter::parameter(std::shared_ptr<function> func, const std::string &name, const std::shared_ptr<type> &type, size_t pos) :
    variable_definition(name, type), _function(std::move(func)), _pos(pos)
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

void function::accept(element_visitor& visitor) {
    visitor.visit_function(*this);
}

std::shared_ptr<block> function::get_block() {
    if(!_block) {
        _block = block::for_function(shared_as<function>());
    }
    return _block;
}

void function::return_type(std::shared_ptr<type> return_type)
{
    _return_type = return_type;
}

std::shared_ptr<parameter> function::append_parameter(const std::string& name, std::shared_ptr<type> type)
{
    return _parameters.emplace_back(new parameter(shared_as<function>(), name, type, _parameters.size()));
}

std::shared_ptr<parameter> function::insert_parameter(const std::string& name, std::shared_ptr<type> type, size_t pos)
{
    if(pos >= _parameters.size()) {
        size_t idx = _parameters.size();
        while(idx<pos) {
            _parameters.emplace_back(new parameter(shared_as<function>(), idx));
        }
        return _parameters.emplace_back(new parameter(shared_as<function>(), name, type, idx));
    } else {
        auto res = _parameters.emplace(_parameters.begin()+pos, new parameter(shared_as<function>(), name, type, pos));
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
            _parameters.emplace_back(new parameter(shared_as<function>(), idx));
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

std::shared_ptr<parameter> function::get_parameter(const std::string& name)
{
    for(auto param : _parameters) {
        if(param->get_name()==name) {
            return param;
        }
    }
    return {};
}

std::shared_ptr<const parameter> function::get_parameter(const std::string& name) const
{
    for(auto param : _parameters) {
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

void global_variable_definition::accept(element_visitor& visitor) {
    visitor.visit_global_variable_definition(*this);
}

//
// Namespace
//
ns::ns(unit& unit, std::shared_ptr<ns> parent, const std::string& name) :
        ns_element(unit, std::move(parent)),
        _name(name)
{
}

void ns::accept(element_visitor& visitor) {
    visitor.visit_namespace(*this);
}

std::shared_ptr<ns> ns::create(unit& unit, std::shared_ptr<ns> parent, const std::string& name) {
    return std::shared_ptr<ns>(new ns(unit, parent, name));
}

std::shared_ptr<ns> ns::get_child_namespace(const std::string& child_name)
{
    auto it = _ns.find(child_name);
    std::shared_ptr<ns> namesp;
    if(it==_ns.end()) {
        namesp = std::shared_ptr<ns>(new ns(_unit, shared_as<ns>(), child_name));
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
    std::shared_ptr<ns> this_ns = shared_as<ns>();
    std::shared_ptr<function> func {new function(this_ns, name)};
    _children.push_back(func);
    return func;
}

std::shared_ptr<function> ns::get_function(const std::string& name)
{
    // TODO add prototype checking
    for(auto child : _children) {
        if(auto func = std::dynamic_pointer_cast<function>(child)) {
            if(func->name()==name) {
                return func;
            }
        }
    }
    return nullptr;
}

std::shared_ptr<function> ns::lookup_function(const std::string& name)
{
    // TODO add prototype checking
    if(auto func = get_function(name)) {
        return func;
    }

    if(auto ns = parent_ns() ) {
        // If has a parent namespace, look at it
        return ns->lookup_function(name);
    }

    return nullptr;
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

std::shared_ptr<variable_definition> ns::get_variable(const std::string& name) {
    // TODO add type checking
    auto it = _vars.find(name);
    if(it!=_vars.end()) {
        return it->second;
    } else {
        return {};
    }
}

std::shared_ptr<variable_definition> ns::lookup_variable(const std::string& name) {
    // TODO add type checking
    // TODO add qualified name lookup
    if(auto var = get_variable(name)) {
        return var;
    }

    if(auto ns = parent_ns() ) {
        // If has a parent namespace, look at it
        return ns->lookup_variable(name);
    }

    return {};
}

//
// Unit
//

unit::unit() :
    _root_ns(ns::create(*this, nullptr, ""))
{
}

void unit::accept(element_visitor& visitor) {
    visitor.visit_unit(*this);
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


//
// Default element visitor
//

void default_element_visitor::visit_element(element&) {
}

void default_element_visitor::visit_unit(unit& unit) {
    visit_element(unit);
}

void default_element_visitor::visit_ns_element(ns_element& elem) {
    visit_element(elem);
}

void default_element_visitor::visit_namespace(ns& ns) {
    visit_ns_element(ns);
}

void default_element_visitor::visit_function(function& func) {
    visit_ns_element(func);
}

void default_element_visitor::visit_global_variable_definition(global_variable_definition& def) {
    visit_ns_element(def);
}

void default_element_visitor::visit_statement(statement& stmt) {
    visit_element(stmt);
}

void default_element_visitor::visit_block(block& stmt) {
    visit_statement(stmt);
}

void default_element_visitor::visit_return_statement(return_statement& stmt) {
    visit_statement(stmt);
}

void default_element_visitor::visit_expression_statement(expression_statement& stmt) {
    visit_statement(stmt);
}

void default_element_visitor::visit_variable_statement(variable_statement& stmt) {
    visit_statement(stmt);
}

void default_element_visitor::visit_expression(expression& expr) {
    visit_element(expr);
}

void default_element_visitor::visit_value_expression(value_expression& expr) {
    visit_expression(expr);
}

void default_element_visitor::visit_symbol_expression(symbol_expression& expr) {
    visit_expression(expr);
}

void default_element_visitor::visit_unary_expression(unary_expression& expr) {
    visit_expression(expr);
}

void default_element_visitor::visit_cast_expression(cast_expression& expr) {
    visit_unary_expression(expr);
}

void default_element_visitor::visit_binary_expression(binary_expression& expr) {
    visit_expression(expr);
}

void default_element_visitor::visit_arithmetic_binary_expression(arithmetic_binary_expression& expr) {
    visit_binary_expression(expr);
}

void default_element_visitor::visit_addition_expression(addition_expression& expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_element_visitor::visit_substraction_expression(substraction_expression& expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_element_visitor::visit_multiplication_expression(multiplication_expression& expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_element_visitor::visit_division_expression(division_expression& expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_element_visitor::visit_modulo_expression(modulo_expression& expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_element_visitor::visit_bitwise_and_expression(bitwise_and_expression& expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_element_visitor::visit_bitwise_or_expression(bitwise_or_expression& expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_element_visitor::visit_bitwise_xor_expression(bitwise_xor_expression& expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_element_visitor::visit_left_shift_expression(left_shift_expression& expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_element_visitor::visit_right_shift_expression(right_shift_expression& expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_element_visitor::visit_assignation_expression(assignation_expression& expr) {
    visit_binary_expression(expr);
}

void default_element_visitor::visit_simple_assignation_expression(simple_assignation_expression& expr) {
    visit_assignation_expression(expr);
}

void default_element_visitor::visit_addition_assignation_expression(additition_assignation_expression& expr) {
    visit_assignation_expression(expr);
}

void default_element_visitor::visit_substraction_assignation_expression(substraction_assignation_expression& expr) {
    visit_assignation_expression(expr);
}

void default_element_visitor::visit_multiplication_assignation_expression(multiplication_assignation_expression& expr) {
    visit_assignation_expression(expr);
}

void default_element_visitor::visit_division_assignation_expression(division_assignation_expression& expr) {
    visit_assignation_expression(expr);
}

void default_element_visitor::visit_modulo_assignation_expression(modulo_assignation_expression& expr) {
    visit_assignation_expression(expr);
}

void default_element_visitor::visit_bitwise_and_assignation_expression(bitwise_and_assignation_expression& expr) {
    visit_assignation_expression(expr);
}

void default_element_visitor::visit_bitwise_or_assignation_expression(bitwise_or_assignation_expression& expr) {
    visit_assignation_expression(expr);
}

void default_element_visitor::visit_bitwise_xor_assignation_expression(bitwise_xor_assignation_expression& expr) {
    visit_assignation_expression(expr);
}

void default_element_visitor::visit_left_shift_assignation_expression(left_shift_assignation_expression& expr) {
    visit_assignation_expression(expr);
}

void default_element_visitor::visit_right_shift_assignation_expression(right_shift_assignation_expression& expr) {
    visit_assignation_expression(expr);
}

void default_element_visitor::visit_function_invocation_expression(function_invocation_expression& expr) {
    visit_expression(expr);
}


} // k::unit
