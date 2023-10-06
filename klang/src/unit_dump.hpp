//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#ifndef KLANG_UNIT_DUMP_HPP
#define KLANG_UNIT_DUMP_HPP

#include "unit.hpp"

#include <typeinfo>

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
        prefix() << "<<unknown-stmt:" << typeid(stmt).name() << ">>" << std::endl;
    }

    void visit_variable_statement(variable_statement& stmt) override {
        visit_variable_definition(stmt);
    }

    void visit_return_statement(return_statement& stmt) override {
        prefix() << "return ";
        if(auto expr = stmt.get_expression()) {
            expr->accept(*this);
        }
        _stm << ";" << std::endl;
    }

    void visit_if_else_statement(if_else_statement& stmt) override {
        prefix() << "if ( ";
        if(auto test_expr = stmt.get_test_expr()) {
            test_expr->accept(*this);
        }
        _stm << " ) " << std::endl;
        {
            auto pf = prefix_inc();
            stmt.get_then_stmt()->accept(*this);
        }
        if(auto else_stmt = stmt.get_else_stmt()) {
            prefix() << "else" << std::endl;
            auto pf = prefix_inc();
            else_stmt->accept(*this);
        }
    }

    void visit_while_statement(while_statement& stmt) override {
        prefix() << "while ( ";
        if(auto test_expr = stmt.get_test_expr()) {
            test_expr->accept(*this);
        }
        _stm << " ) " << std::endl;
        auto pf = prefix_inc();
        stmt.get_nested_stmt()->accept(*this);
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
        _stm << "<<unknown-expr:" << typeid(expr).name() << ">>";
    }

    void visit_symbol_expression(symbol_expression& expr) override {
        // TODO support other symbol types, not only variables
        if(expr.is_variable_def()) {
            _stm << "<<symbol-var-expr:" << expr.get_variable_def()->get_name() << ">>";
        } else if (expr.is_function()) {
            _stm << "<<symbol-func-expr:" << expr.get_function()->name() << ">>";
        } else {
            _stm << "<<unresolved-symbol-expr:" << expr.get_name().to_string() << ">>";
        }
    }

    void visit_value_expression(value_expression& expr) override {
        if(expr.is_literal()) {
            _stm << "<<value-expr-lit:" << expr.get_literal().content << ">>";
        } else {
            _stm << "<<value-expr-val:TODO" << ">>";
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

    void visit_bitwise_and_expression(bitwise_and_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " & ";
        expr.right()->accept(*this);
    }

    void visit_bitwise_or_expression(bitwise_or_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " | ";
        expr.right()->accept(*this);
    }

    void visit_bitwise_xor_expression(bitwise_xor_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " ^ ";
        expr.right()->accept(*this);
    }

    void visit_left_shift_expression(left_shift_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " << ";
        expr.right()->accept(*this);
    }

    void visit_right_shift_expression(right_shift_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " >> ";
        expr.right()->accept(*this);
    }

    void visit_simple_assignation_expression(simple_assignation_expression& expr) override {
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

    void visit_bitwise_and_assignation_expression(bitwise_and_assignation_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " &= ";
        expr.right()->accept(*this);
    }

    void visit_bitwise_or_assignation_expression(bitwise_or_assignation_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " |= ";
        expr.right()->accept(*this);
    }

    void visit_bitwise_xor_assignation_expression(bitwise_xor_assignation_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " ^= ";
        expr.right()->accept(*this);
    }

    void visit_left_shift_assignation_expression(left_shift_assignation_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " <<= ";
        expr.right()->accept(*this);
    }

    void visit_right_shift_assignation_expression(right_shift_assignation_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " >>= ";
        expr.right()->accept(*this);
    }

    void visit_unary_plus_expression(unary_plus_expression& expr) override {
        _stm << " + ";
        expr.sub_expr()->accept(*this);
    }

    void visit_unary_minus_expression(unary_minus_expression& expr) override {
        _stm << " - ";
        expr.sub_expr()->accept(*this);
    }

    void visit_bitwise_not_expression(bitwise_not_expression& expr) override {
        _stm << " ~ ";
        expr.sub_expr()->accept(*this);
    }

    void visit_logical_and_expression(logical_and_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " && ";
        expr.right()->accept(*this);
    }

    void visit_logical_or_expression(logical_or_expression& expr) override {
        expr.left()->accept(*this);
        _stm << " || ";
        expr.right()->accept(*this);
    }

    void visit_logical_not_expression(logical_not_expression& expr) override {
        _stm << " ! ";
        expr.sub_expr()->accept(*this);
    }

    void visit_equal_expression(equal_expression& expr) {
        expr.left()->accept(*this);
        _stm << " == ";
        expr.right()->accept(*this);
    }

    void visit_different_expression(different_expression& expr) {
        expr.left()->accept(*this);
        _stm << " != ";
        expr.right()->accept(*this);
    }

    void visit_lesser_expression(lesser_expression& expr) {
        expr.left()->accept(*this);
        _stm << " < ";
        expr.right()->accept(*this);
    }

    void visit_greater_expression(greater_expression& expr) {
        expr.left()->accept(*this);
        _stm << " > ";
        expr.right()->accept(*this);
    }

    void visit_lesser_equal_expression(lesser_equal_expression& expr) {
        expr.left()->accept(*this);
        _stm << " <= ";
        expr.right()->accept(*this);
    }

    void visit_greater_equal_expression(greater_equal_expression& expr) {
        expr.left()->accept(*this);
        _stm << " >= ";
        expr.right()->accept(*this);
    }

    void visit_function_invocation_expression(function_invocation_expression &expr) override {
        expr.callee_expr()->accept(*this);
        _stm << "(";
        for(size_t i=0; i<expr.arguments().size(); ++i) {
            if(i>0) {
                _stm << " , ";
            }
            expr.arguments().at(i)->accept(*this);
        }
        _stm << ")";
    }

    void visit_cast_expression(cast_expression& expr) override {
        _stm << "(cast:";
        dump_type(*expr.get_cast_type());
        _stm << ":";
        expr.sub_expr()->accept(*this);
        _stm << ")";
    }
};


} // namespace k::unit::dump
#endif //KLANG_UNIT_DUMP_HPP
