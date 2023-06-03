//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#ifndef KLANG_UNIT_DUMP_HPP
#define KLANG_UNIT_DUMP_HPP

#include "unit.hpp"

namespace k::unit::dump {

template<typename OSTM>
class unit_dump  : public default_element_visitor {
    OSTM& _stm;
    size_t off = 0;
public:

    unit_dump(OSTM& stm) : _stm(stm) {}

protected:
    void inc() {
        off++;
    }

    void dec() {
        off--;
    }

    OSTM& prefix() {
        for(size_t n=0; n<off; ++n) {
            _stm << '\t';
        }
        return _stm;
    }

    class prefix_inc;

    friend class prefix_inc_holder;

    class prefix_inc_holder {
        unit_dump* _dumper = nullptr;
    protected:
        prefix_inc_holder(unit_dump* visit) : _dumper(visit) { _dumper->inc(); }
        friend class unit_dump;
    public:
        prefix_inc_holder() = delete;
        prefix_inc_holder(const prefix_inc& other) = delete;
        prefix_inc_holder(prefix_inc&& other) : _dumper(other._visitor) { other._visitor = nullptr;}
        ~prefix_inc_holder() {
            if(_dumper != nullptr) {
                _dumper->dec();
                _dumper = nullptr;
            }
        }
    };

    prefix_inc_holder prefix_inc() {
        return prefix_inc_holder(this);
    }

public:
    void dump(unit& unit) {
        visit_unit(unit);
    }

    void visit_unit(unit& unit) override {
        // Module name:
        prefix() << "unit: " << unit.get_unit_name().to_string() << std::endl;

        // TODO Imports
        unit.get_root_namespace()->accept(*this);
    }

    void visit_ns_element(ns_element& elem) override {
        prefix() << "<<unknown ns element>>" << std::endl;
    }

    void visit_namespace(ns& ns) override {
        prefix() << "namespace '" << ns.get_name() << "' {" << std::endl;
        {
            auto pf = prefix_inc();
            for(auto& child : ns.get_children()) {
                child->accept(*this);
            }
        }
        prefix() << "} // " << ns.get_name() << std::endl;
    }

    void visit_function(function& func) override {
        prefix() << "function '" << func.name() << "' (";
        for(size_t idx = 0; idx<func.parameters().size(); idx++) {
            if(idx!=0) {
                _stm << ", ";
            }
            auto param = func.parameters()[idx];
            _stm << param->get_name() << " : ";
            dump_type(*param->get_type());
        }
        _stm << ") : ";
        dump_type(*func.return_type());
        _stm << std::endl;
        func.get_block()->accept(*this);
    }

    void visit_global_variable_definition(global_variable_definition& var) override {
        visit_variable_definition(var);
    }

    void visit_variable_definition(k::unit::variable_definition& var) {
        prefix() << "variable '" << var.get_name() << "' : ";
        dump_type(*var.get_type());
        if(auto init = var.get_init_expr()) {
            _stm << " = ";
            // TODO dump init expression
            init->accept(*this);
        }
        _stm << std::endl;
    }

    void dump_type(k::unit::type& type) {
        if(auto t = dynamic_cast<primitive_type*>(&type)) {
            dump_primitive_type(*t);
        } else if(auto t = dynamic_cast<unresolved_type*>(&type)) {
            dump_unresolved_type(*t);
        //} else if(auto t = dynamic_cast<unresolved_type*>(&type)) {
        //    dump_unresolved_type(*t);
        } else {
            _stm << "<<unknown-type>>";
        }
    }

    void dump_primitive_type(primitive_type& type) {
        _stm << "<<prim-type:" << type.to_string() << ">>";
    }

    void dump_unresolved_type(unresolved_type& type) {
        _stm << "<<unresolved:" << type.type_id().to_string() << ">>";
    }

    void visit_statement(statement& stmt) override {
        prefix() << "<<unsupported statement type>>" << std::endl;
    }

    void visit_return_statement(return_statement& stmt) override {
        prefix() << "return ";
        if(auto expr = stmt.get_expression()) {
            expr->accept(*this);
        }
        _stm << ";" << std::endl;
    }

    void visit_block(block& blk) override {
        prefix() << "{" << std::endl;
        {
            auto pf = prefix_inc();
            for (auto &child: blk.get_statements()) {
                child->accept(*this);
            }
        }
        prefix() << "}" << std::endl;
    }

    void visit_expression_statement(expression_statement& stmt) override {
        prefix();
        if(auto expr = stmt.get_expression()) {
            expr->accept(*this);
        }
        _stm << ";" << std::endl;
    }

    void visit_expression(expression& expr) {
        _stm << "<<unknown-expr>>";
    }

    void visit_variable_expression(variable_expression& expr) override {
        if(expr.is_resolved()) {
            _stm << "<<var-expr:" << expr.get_variable_def()->get_name() << ">>";
        } else {
            _stm << "<<unresolved-var-expr:" << expr.get_var_name().to_string() << ">>";
        }
    }

    void visit_value_expression(value_expression& expr) override {
        if(expr.is_literal()) {
            _stm << "<<lit-value-expr:" << expr.get_literal().content << ">>";
        } else {
            _stm << "<<val-value-expr:TODO" << ">>";
            // TODO
        }
    }

    void visit_addition_expression(addition_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " + ";
        expr.right()->accept(*this);
    }

    void visit_substraction_expression(substraction_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " - ";
        expr.right()->accept(*this);
    }

    void visit_multiplication_expression(multiplication_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " * ";
        expr.right()->accept(*this);
    }

    void visit_division_expression(division_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " / ";
        expr.right()->accept(*this);
    }

    void visit_modulo_expression(modulo_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " % ";
        expr.right()->accept(*this);
    }

    void visit_assignation_expression(assignation_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " = ";
        expr.right()->accept(*this);
    }

    void visit_addition_assignation_expression(additition_assignation_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " += ";
        expr.right()->accept(*this);
    }

    void visit_substraction_assignation_expression(substraction_assignation_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " -= ";
        expr.right()->accept(*this);
    }

    void visit_multiplication_assignation_expression(multiplication_assignation_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " *= ";
        expr.right()->accept(*this);
    }

    void visit_division_assignation_expression(division_assignation_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " /= ";
        expr.right()->accept(*this);
    }

    void visit_modulo_assignation_expression(modulo_assignation_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " %= ";
        expr.right()->accept(*this);
    }
};


} // namespace k::unit::dump
#endif //KLANG_UNIT_DUMP_HPP
