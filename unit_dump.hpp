//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#ifndef KLANG_UNIT_DUMP_HPP
#define KLANG_UNIT_DUMP_HPP

#include "unit.hpp"

namespace k::unit::dump {

template<typename OSTM>
class unit_dump {
    OSTM& _stm;
    size_t off = 0;
public:

    static void dump(OSTM& stm, const k::unit::unit& unit) {
        k::parse::dump::ast_dump_visitor visit(stm);
        dump(unit);
    }

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
    void dump(k::unit::unit& unit) {
        dump_unit(unit);
    }

    void dump_unit(k::unit::unit& unit) {
        // Module name:
        prefix() << "unit: " << unit.get_unit_name().to_string() << std::endl;

        // TODO Imports

        dump_namespace(*unit.get_root_namespace());
    }

    void dump_ns_element(std::shared_ptr<ns_element> elem) {
        if(auto ns = std::dynamic_pointer_cast<k::unit::ns>(elem)) {
            dump_namespace(*ns);
        } else if(auto func = std::dynamic_pointer_cast<k::unit::function>(elem)) {
            dump_function(*func);
        } else if(auto var = std::dynamic_pointer_cast<k::unit::variable_definition>(elem)) {
            dump_variable_definition(*var);
        } else {
            // Unsupported ns element
        }
    }

    void dump_namespace(k::unit::ns& ns) {
        prefix() << "namespace '" << ns.get_name() << "' {" << std::endl;
        {
            auto pf = prefix_inc();
            for(auto& child : ns.get_children()) {
                dump_ns_element(child);
            }
        }
        prefix() << "} // " << ns.get_name() << std::endl;
    }

    void dump_function(k::unit::function& func) {
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
        dump_block(*func.get_block());
    }

    void  dump_variable_definition(k::unit::variable_definition& var) {
        prefix() << "variable '" << var.get_name() << "' : ";
        dump_type(*var.get_type());
        if(auto init = var.get_init_expr()) {
            _stm << " = ";
            // TODO dump init expression
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

    void dump_statement(statement& stmt) {
        if(auto blk = dynamic_cast<block*>(&stmt)) {
            dump_block(*blk);
        } else if(auto var = dynamic_cast<variable_definition*>(&stmt)) {
            dump_variable_definition(*var);
        } else if(auto ret = dynamic_cast<return_statement*>(&stmt)) {
            dump_return_statement(*ret);
        } else if(auto expr = dynamic_cast<expression_statement*>(&stmt)) {
            dump_expression_statement(*expr);
        } else {
            prefix() << "<<unsupported statement type>>" << std::endl;
            // Unsupported statement
        }
    }

    void dump_return_statement(return_statement& stmt) {
        prefix() << "return ";
        if(auto expr = stmt.get_expression()) {
            dump_expression(*expr);
        }
        _stm << ";" << std::endl;
    }

    void dump_block(block& blk) {
        prefix() << "{" << std::endl;
        {
            auto pf = prefix_inc();
            for (auto &child: blk.get_statements()) {
                dump_statement(*child);
            }
        }
        prefix() << "}" << std::endl;
    }

    void dump_expression_statement(expression_statement& stmt) {
        prefix();
        if(auto expr = stmt.get_expression()) {
            dump_expression(*expr);
        }
        _stm << ";" << std::endl;
    }

    void dump_expression(expression& expr) {
        if(auto e = dynamic_cast<variable_expression*>(&expr)) {
            dump_variable_expression(*e);
        } else if(auto e = dynamic_cast<value_expression*>(&expr)) {
            dump_value_expression(*e);
        } else if(auto e = dynamic_cast<addition_expression*>(&expr)) {
            dump_addition(*e);
        } else if(auto e = dynamic_cast<substraction_expression*>(&expr)) {
            dump_substraction(*e);
        } else if(auto e = dynamic_cast<multiplication_expression*>(&expr)) {
            dump_multiplication(*e);
        } else if(auto e = dynamic_cast<division_expression*>(&expr)) {
            dump_division(*e);
        } else if(auto e = dynamic_cast<modulo_expression*>(&expr)) {
            dump_modulo(*e);
        } else if(auto e = dynamic_cast<assignation_expression*>(&expr)) {
            dump_assignation(*e);
        } else if(auto e = dynamic_cast<additition_assignation_expression*>(&expr)) {
            dump_addition_assignation(*e);
        } else if(auto e = dynamic_cast<substraction_assignation_expression*>(&expr)) {
            dump_substraction_assignation(*e);
        } else if(auto e = dynamic_cast<multiplication_assignation_expression*>(&expr)) {
            dump_multiplication_assignation(*e);
        } else if(auto e = dynamic_cast<division_assignation_expression*>(&expr)) {
            dump_division_assignation(*e);
        } else if(auto e = dynamic_cast<modulo_assignation_expression*>(&expr)) {
            dump_modulo_assignation(*e);
        } else {
            _stm << "<<unknown-expr>>";
        }
    }

    void dump_variable_expression(variable_expression& expr) {
        if(expr.is_resolved()) {
            _stm << "<<var-expr:" << expr.get_variable_def()->get_name() << ">>";
        } else {
            _stm << "<<unresolved-var-expr:" << expr.get_var_name().to_string() << ">>";
        }
    }

    void dump_value_expression(value_expression& expr) {
        if(expr.is_litteral()) {
            _stm << "<<lit-value-expr:" << expr.get_litteral().content << ">>";
        } else {
            _stm << "<<val-value-expr:TODO" << ">>";
            // TODO
        }
    }

    void dump_addition(addition_expression& expr) {
        dump_expression(*expr.left());
        _stm << " + ";
        dump_expression(*expr.right());
    }

    void dump_substraction(substraction_expression& expr) {
        dump_expression(*expr.left());
        _stm << " - ";
        dump_expression(*expr.right());
    }

    void dump_multiplication(multiplication_expression& expr) {
        dump_expression(*expr.left());
        _stm << " * ";
        dump_expression(*expr.right());
    }

    void dump_division(division_expression& expr) {
        dump_expression(*expr.left());
        _stm << " / ";
        dump_expression(*expr.right());
    }

    void dump_modulo(modulo_expression& expr) {
        dump_expression(*expr.left());
        _stm << " % ";
        dump_expression(*expr.right());
    }

    void dump_assignation(assignation_expression& expr) {
        dump_expression(*expr.left());
        _stm << " = ";
        dump_expression(*expr.right());
    }

    void dump_addition_assignation(additition_assignation_expression& expr) {
        dump_expression(*expr.left());
        _stm << " += ";
        dump_expression(*expr.right());
    }

    void dump_substraction_assignation(substraction_assignation_expression& expr) {
        dump_expression(*expr.left());
        _stm << " -= ";
        dump_expression(*expr.right());
    }

    void dump_multiplication_assignation(multiplication_assignation_expression& expr) {
        dump_expression(*expr.left());
        _stm << " *= ";
        dump_expression(*expr.right());
    }

    void dump_division_assignation(division_assignation_expression& expr) {
        dump_expression(*expr.left());
        _stm << " /= ";
        dump_expression(*expr.right());
    }

    void dump_modulo_assignation(modulo_assignation_expression& expr) {
        dump_expression(*expr.left());
        _stm << " %= ";
        dump_expression(*expr.right());
    }
};


} // namespace k::unit::dump
#endif //KLANG_UNIT_DUMP_HPP
