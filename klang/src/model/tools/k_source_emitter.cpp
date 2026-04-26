/*
 * K Language compiler
 *
 * Copyright 2026 Emilien Kia
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

#include "k_source_emitter.hpp"

#include "../model.hpp"
#include "../type.hpp"
#include "../template.hpp"
#include "../expressions.hpp"
#include "../operators.hpp"
#include "../statements.hpp"

#include <cassert>
#include <variant>

namespace k::model {

// ═════════════════════════════════════════════════════════════════════════════
// Helpers
// ═════════════════════════════════════════════════════════════════════════════

void k_source_emitter::reset() {
    _os.str("");
    _os.clear();
}

void k_source_emitter::indent(int level) {
    for (int i = 0; i < level; ++i) _os << '\t';
}

std::string k_source_emitter::fq_name_for_source(const std::string& fq) {
    // Strip leading "::" if present
    if (fq.size() >= 2 && fq[0] == ':' && fq[1] == ':')
        return fq.substr(2);
    return fq;
}

const char* k_source_emitter::aggregate_keyword(const aggregate& agg) {
    if (dynamic_cast<const interface*>(&agg)) return "interface";
    if (agg.is_class()) return "class";
    return "struct";
}

// ═════════════════════════════════════════════════════════════════════════════
// Type emission
// ═════════════════════════════════════════════════════════════════════════════

void k_source_emitter::emit_type(const std::shared_ptr<type>& t) {
    if (!t) {
        _os << "void";
        return;
    }

    // const qualifier
    if (auto ct = std::dynamic_pointer_cast<const_type>(t)) {
        _os << "const ";
        emit_type(ct->get_inner_type());
        return;
    }

    // Primitive types — use their K names directly
    if (auto pt = std::dynamic_pointer_cast<primitive_type>(t)) {
        _os << pt->to_string();
        return;
    }

    // Unresolved type — template parameter placeholders stay as bare names,
    // resolved unresolved types follow through to the resolved type.
    if (auto ut = std::dynamic_pointer_cast<unresolved_type>(t)) {
        if (ut->is_template_param_placeholder()) {
            // Template parameter: emit bare name (e.g. "T")
            _os << ut->type_id().to_string();
            return;
        }
        if (ut->is_resolved()) {
            emit_type(ut->get_resolved());
            return;
        }
        // Check alias map for using-aliased types
        auto name_str = ut->type_id().to_string();
        if (auto it = _alias_map.find(name_str); it != _alias_map.end()) {
            _os << it->second;
            return;
        }
        // Truly unresolved — emit the raw name as a fallback
        _os << name_str;
        return;
    }

    // Reference type (&)
    if (auto rt = std::dynamic_pointer_cast<reference_type>(t)) {
        emit_type(rt->get_subtype());
        _os << "&";
        return;
    }

    // Pointer type (*)
    if (auto pt = std::dynamic_pointer_cast<pointer_type>(t)) {
        emit_type(pt->get_subtype());
        _os << "*";
        return;
    }

    // Link type (+)
    if (auto lt = std::dynamic_pointer_cast<link_type>(t)) {
        emit_type(lt->get_subtype());
        _os << "+";
        return;
    }

    // View type (?)
    if (auto vt = std::dynamic_pointer_cast<view_type>(t)) {
        emit_type(vt->get_subtype());
        _os << "?";
        return;
    }

    // Owner type (!)
    if (auto ot = std::dynamic_pointer_cast<owner_type>(t)) {
        emit_type(ot->get_subtype());
        _os << "!";
        return;
    }

    // Drain type (#)
    if (auto dt = std::dynamic_pointer_cast<drain_type>(t)) {
        emit_type(dt->get_subtype());
        _os << "#";
        return;
    }

    // Sized array type
    if (auto sat = std::dynamic_pointer_cast<sized_array_type>(t)) {
        emit_type(sat->get_subtype());
        _os << "[" << sat->get_size() << "]";
        return;
    }

    // Unsized array type
    if (auto at = std::dynamic_pointer_cast<array_type>(t)) {
        emit_type(at->get_subtype());
        _os << "[]";
        return;
    }

    // Struct type — use FQ name of the underlying aggregate
    if (auto st = std::dynamic_pointer_cast<struct_type>(t)) {
        if (auto agg = st->get_struct()) {
            _os << fq_name_for_source(agg->get_fq_name());
        } else {
            _os << st->name();
        }
        return;
    }

    // Enum type — use FQ name of the underlying enumeration
    if (auto et = std::dynamic_pointer_cast<enum_type>(t)) {
        if (auto en = et->get_enumeration()) {
            _os << fq_name_for_source(en->get_fq_name());
        } else {
            _os << et->to_string();
        }
        return;
    }

    // Function reference type
    if (auto frt = std::dynamic_pointer_cast<function_reference_type>(t)) {
        _os << "(";
        auto& params = frt->get_parameter_types();
        for (size_t i = 0; i < params.size(); ++i) {
            if (i > 0) _os << ", ";
            emit_type(params[i]);
        }
        _os << ")";
        switch (frt->get_ref_kind()) {
            case function_reference_type::ref_kind::pointer: _os << "*"; break;
            case function_reference_type::ref_kind::view:    _os << "?"; break;
            case function_reference_type::ref_kind::link:    _os << "+"; break;
        }
        _os << " -> ";
        emit_type(frt->get_return_type());
        return;
    }

    // Null type
    if (std::dynamic_pointer_cast<null_type>(t)) {
        _os << "null";
        return;
    }

    // Fallback
    _os << "/* unknown type */";
}

// ═════════════════════════════════════════════════════════════════════════════
// Template parameter clause
// ═════════════════════════════════════════════════════════════════════════════

void k_source_emitter::emit_template_clause(const tpl_info& ti) {
    _os << "template<";
    for (size_t i = 0; i < ti.params.size(); ++i) {
        if (i > 0) _os << ", ";
        auto& p = ti.params[i];
        if (p.is_value_param()) {
            emit_type(p.value_type);
            _os << " " << p.name;
            if (p.default_value.has_value()) {
                _os << " = ";
                emit_value(*p.default_value);
            }
        } else {
            // Type parameter
            switch (p.kind) {
                case template_param_kind::TYPENAME:  _os << "typename "; break;
                case template_param_kind::STRUCT:    _os << "struct ";   break;
                case template_param_kind::CLASS:     _os << "class ";    break;
                case template_param_kind::INTERFACE: _os << "interface "; break;
                default: break;
            }
            _os << p.name;
            if (p.constraint_type) {
                _os << " : ";
                emit_type(p.constraint_type);
            }
            if (p.default_type) {
                _os << " = ";
                emit_type(p.default_type);
            }
        }
    }
    _os << ">\n";
}

// ═════════════════════════════════════════════════════════════════════════════
// Value emission (for compile-time constants)
// ═════════════════════════════════════════════════════════════════════════════

void k_source_emitter::emit_value(const k::value_type& val) {
    std::visit([this](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, std::nullptr_t>) {
            _os << "0";
        } else if constexpr (std::is_same_v<T, bool>) {
            _os << (v ? "true" : "false");
        } else if constexpr (std::is_same_v<T, char>) {
            _os << "'" << v << "'";
        } else if constexpr (std::is_same_v<T, std::string>) {
            _os << "\"" << v << "\"";
        } else {
            _os << v;
        }
    }, val);
}

// ═════════════════════════════════════════════════════════════════════════════
// Expression emission
// ═════════════════════════════════════════════════════════════════════════════

void k_source_emitter::emit_expression(const std::shared_ptr<expression>& expr) {
    if (!expr) return;

    // Value expression (literal or scalar)
    if (auto ve = std::dynamic_pointer_cast<value_expression>(expr)) {
        if (ve->is_literal()) {
            _os << ve->get_literal().content;
        } else {
            emit_value(ve->get_value());
        }
        return;
    }

    // Symbol expression
    if (auto se = std::dynamic_pointer_cast<symbol_expression>(expr)) {
        if (se->is_variable_def()) {
            auto var = se->get_variable_def();
            _os << var->get_short_name();
        } else if (se->is_function()) {
            auto func = se->get_function();
            // For functions in template bodies, use the short name
            _os << func->get_short_name();
        } else if (se->is_enum_entry()) {
            auto& target = se->get_enum_entry();
            auto en = target.enum_def;
            _os << fq_name_for_source(en->get_fq_name())
                << "::" << en->entries()[target.entry_index].name;
        } else {
            // Unresolved: emit raw name
            _os << se->get_name().to_string();
        }
        return;
    }

    // Function invocation
    if (auto fie = std::dynamic_pointer_cast<function_invocation_expression>(expr)) {
        emit_expression(fie->callee_expr());
        _os << "(";
        for (size_t i = 0; i < fie->arguments().size(); ++i) {
            if (i > 0) _os << ", ";
            emit_expression(fie->arguments()[i]);
        }
        _os << ")";
        return;
    }

    // Constructor invocation
    if (auto cie = std::dynamic_pointer_cast<constructor_invocation_expression>(expr)) {
        if (cie->constructed_symbol()) {
            emit_expression(cie->constructed_symbol());
        }
        _os << "(";
        for (size_t i = 0; i < cie->arguments().size(); ++i) {
            if (i > 0) _os << ", ";
            emit_expression(cie->arguments()[i]);
        }
        _os << ")";
        return;
    }

    // Temporary construction
    if (auto tce = std::dynamic_pointer_cast<temporary_construction_expression>(expr)) {
        emit_type(tce->constructed_type());
        _os << "(";
        for (size_t i = 0; i < tce->arguments().size(); ++i) {
            if (i > 0) _os << ", ";
            emit_expression(tce->arguments()[i]);
        }
        _os << ")";
        return;
    }

    // Cast expression
    if (auto ce = std::dynamic_pointer_cast<cast_expression>(expr)) {
        emit_expression(ce->sub_expr());
        _os << " as ";
        emit_type(ce->get_cast_type());
        return;
    }

    // Load value (transparent — emit inner)
    if (auto lve = std::dynamic_pointer_cast<load_value_expression>(expr)) {
        emit_expression(lve->sub_expr());
        return;
    }

    // Address-of
    if (auto aoe = std::dynamic_pointer_cast<address_of_expression>(expr)) {
        _os << "&";
        emit_expression(aoe->sub_expr());
        return;
    }

    // Drain
    if (auto de = std::dynamic_pointer_cast<drain_expression>(expr)) {
        _os << "#";
        emit_expression(de->sub_expr());
        return;
    }

    // Dereference
    if (auto dre = std::dynamic_pointer_cast<dereference_expression>(expr)) {
        _os << "*";
        emit_expression(dre->sub_expr());
        return;
    }

    // Member of object (a.b)
    if (auto moe = std::dynamic_pointer_cast<member_of_object_expression>(expr)) {
        emit_expression(moe->sub_expr());
        _os << ".";
        // Emit the symbol part
        auto& sym = moe->symbol();
        if (sym.is_variable_def()) {
            _os << sym.get_variable_def()->get_short_name();
        } else if (sym.is_function()) {
            _os << sym.get_function()->get_short_name();
        } else {
            _os << sym.get_name().to_string();
        }
        return;
    }

    // Member of pointer (a->b)
    if (auto mpe = std::dynamic_pointer_cast<member_of_pointer_expression>(expr)) {
        emit_expression(mpe->sub_expr());
        _os << "->";
        auto& sym = mpe->symbol();
        if (sym.is_variable_def()) {
            _os << sym.get_variable_def()->get_short_name();
        } else if (sym.is_function()) {
            _os << sym.get_function()->get_short_name();
        } else {
            _os << sym.get_name().to_string();
        }
        return;
    }

    // Subscript (a[b])
    if (auto se = std::dynamic_pointer_cast<subscript_expression>(expr)) {
        emit_expression(se->left());
        _os << "[";
        emit_expression(se->right());
        _os << "]";
        return;
    }

    // ── Binary operators ─────────────────────────────────────────────────

    // We need to check specific binary types in order.
    // Assignments first, then arithmetic, then comparison, then logical.

    // Simple assignation
    if (auto e = std::dynamic_pointer_cast<simple_assignation_expression>(expr)) {
        emit_expression(e->left()); _os << " = "; emit_expression(e->right()); return;
    }
    if (auto e = std::dynamic_pointer_cast<additition_assignation_expression>(expr)) {
        emit_expression(e->left()); _os << " += "; emit_expression(e->right()); return;
    }
    if (auto e = std::dynamic_pointer_cast<substraction_assignation_expression>(expr)) {
        emit_expression(e->left()); _os << " -= "; emit_expression(e->right()); return;
    }
    if (auto e = std::dynamic_pointer_cast<multiplication_assignation_expression>(expr)) {
        emit_expression(e->left()); _os << " *= "; emit_expression(e->right()); return;
    }
    if (auto e = std::dynamic_pointer_cast<division_assignation_expression>(expr)) {
        emit_expression(e->left()); _os << " /= "; emit_expression(e->right()); return;
    }
    if (auto e = std::dynamic_pointer_cast<modulo_assignation_expression>(expr)) {
        emit_expression(e->left()); _os << " %= "; emit_expression(e->right()); return;
    }
    if (auto e = std::dynamic_pointer_cast<bitwise_and_assignation_expression>(expr)) {
        emit_expression(e->left()); _os << " &= "; emit_expression(e->right()); return;
    }
    if (auto e = std::dynamic_pointer_cast<bitwise_or_assignation_expression>(expr)) {
        emit_expression(e->left()); _os << " |= "; emit_expression(e->right()); return;
    }
    if (auto e = std::dynamic_pointer_cast<bitwise_xor_assignation_expression>(expr)) {
        emit_expression(e->left()); _os << " ^= "; emit_expression(e->right()); return;
    }
    if (auto e = std::dynamic_pointer_cast<left_shift_assignation_expression>(expr)) {
        emit_expression(e->left()); _os << " <<= "; emit_expression(e->right()); return;
    }
    if (auto e = std::dynamic_pointer_cast<right_shift_assignation_expression>(expr)) {
        emit_expression(e->left()); _os << " >>= "; emit_expression(e->right()); return;
    }

    // Arithmetic binary
    if (auto e = std::dynamic_pointer_cast<addition_expression>(expr)) {
        _os << "("; emit_expression(e->left()); _os << " + "; emit_expression(e->right()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<substraction_expression>(expr)) {
        _os << "("; emit_expression(e->left()); _os << " - "; emit_expression(e->right()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<multiplication_expression>(expr)) {
        _os << "("; emit_expression(e->left()); _os << " * "; emit_expression(e->right()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<division_expression>(expr)) {
        _os << "("; emit_expression(e->left()); _os << " / "; emit_expression(e->right()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<modulo_expression>(expr)) {
        _os << "("; emit_expression(e->left()); _os << " % "; emit_expression(e->right()); _os << ")"; return;
    }

    // Bitwise binary
    if (auto e = std::dynamic_pointer_cast<bitwise_and_expression>(expr)) {
        _os << "("; emit_expression(e->left()); _os << " & "; emit_expression(e->right()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<bitwise_or_expression>(expr)) {
        _os << "("; emit_expression(e->left()); _os << " | "; emit_expression(e->right()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<bitwise_xor_expression>(expr)) {
        _os << "("; emit_expression(e->left()); _os << " ^ "; emit_expression(e->right()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<left_shift_expression>(expr)) {
        _os << "("; emit_expression(e->left()); _os << " << "; emit_expression(e->right()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<right_shift_expression>(expr)) {
        _os << "("; emit_expression(e->left()); _os << " >> "; emit_expression(e->right()); _os << ")"; return;
    }

    // Comparison
    if (auto e = std::dynamic_pointer_cast<equal_expression>(expr)) {
        _os << "("; emit_expression(e->left()); _os << " == "; emit_expression(e->right()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<different_expression>(expr)) {
        _os << "("; emit_expression(e->left()); _os << " != "; emit_expression(e->right()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<lesser_expression>(expr)) {
        _os << "("; emit_expression(e->left()); _os << " < "; emit_expression(e->right()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<greater_expression>(expr)) {
        _os << "("; emit_expression(e->left()); _os << " > "; emit_expression(e->right()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<lesser_equal_expression>(expr)) {
        _os << "("; emit_expression(e->left()); _os << " <= "; emit_expression(e->right()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<greater_equal_expression>(expr)) {
        _os << "("; emit_expression(e->left()); _os << " >= "; emit_expression(e->right()); _os << ")"; return;
    }

    // Logical binary
    if (auto e = std::dynamic_pointer_cast<logical_and_expression>(expr)) {
        _os << "("; emit_expression(e->left()); _os << " && "; emit_expression(e->right()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<logical_or_expression>(expr)) {
        _os << "("; emit_expression(e->left()); _os << " || "; emit_expression(e->right()); _os << ")"; return;
    }

    // ── Unary operators ──────────────────────────────────────────────────

    if (auto e = std::dynamic_pointer_cast<unary_plus_expression>(expr)) {
        _os << "+("; emit_expression(e->sub_expr()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<unary_minus_expression>(expr)) {
        _os << "-("; emit_expression(e->sub_expr()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<bitwise_not_expression>(expr)) {
        _os << "~("; emit_expression(e->sub_expr()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<logical_not_expression>(expr)) {
        _os << "!("; emit_expression(e->sub_expr()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<prefix_increment_expression>(expr)) {
        _os << "++("; emit_expression(e->sub_expr()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<prefix_decrement_expression>(expr)) {
        _os << "--("; emit_expression(e->sub_expr()); _os << ")"; return;
    }
    if (auto e = std::dynamic_pointer_cast<postfix_increment_expression>(expr)) {
        _os << "("; emit_expression(e->sub_expr()); _os << ")++"; return;
    }
    if (auto e = std::dynamic_pointer_cast<postfix_decrement_expression>(expr)) {
        _os << "("; emit_expression(e->sub_expr()); _os << ")--"; return;
    }

    // New expression
    if (auto ne = std::dynamic_pointer_cast<new_expression>(expr)) {
        _os << "new ";
        emit_type(ne->allocated_type());
        if (ne->is_array()) {
            _os << "[";
            if (ne->array_size_expr()) emit_expression(ne->array_size_expr());
            _os << "]";
        } else {
            _os << "(";
            for (size_t i = 0; i < ne->arguments().size(); ++i) {
                if (i > 0) _os << ", ";
                emit_expression(ne->arguments()[i]);
            }
            _os << ")";
        }
        return;
    }

    // Delete expression
    if (auto de = std::dynamic_pointer_cast<delete_expression>(expr)) {
        _os << "delete ";
        emit_expression(de->sub_expr());
        return;
    }

    // Owner move expression (transparent)
    if (auto ome = std::dynamic_pointer_cast<owner_move_expression>(expr)) {
        emit_expression(ome->sub_expr());
        return;
    }

    // PM expression (.*  or ->*)
    if (auto pm = std::dynamic_pointer_cast<pm_expression>(expr)) {
        emit_expression(pm->left());
        _os << (pm->is_arrow() ? "->*" : ".*");
        emit_expression(pm->right());
        return;
    }

    // Fallback: unknown expression
    _os << "/* <unknown-expr> */";
}

// ═════════════════════════════════════════════════════════════════════════════
// Statement emission
// ═════════════════════════════════════════════════════════════════════════════

void k_source_emitter::emit_statement(const std::shared_ptr<statement>& stmt, int ind) {
    if (!stmt) return;

    // Block
    if (auto blk = std::dynamic_pointer_cast<block>(stmt)) {
        emit_block(*blk, ind);
        return;
    }

    // Variable statement
    if (auto vs = std::dynamic_pointer_cast<variable_statement>(stmt)) {
        indent(ind);
        if (vs->is_const()) {
            _os << "const ";
        }
        _os << vs->get_short_name() << " : ";
        emit_type(vs->get_type());
        if (auto init = vs->get_init_expr()) {
            // Check if it's a constructor_invocation_expression (don't emit as "= ctor(...)")
            if (auto cie = std::dynamic_pointer_cast<constructor_invocation_expression>(init)) {
                if (!cie->arguments().empty()) {
                    _os << "(";
                    for (size_t i = 0; i < cie->arguments().size(); ++i) {
                        if (i > 0) _os << ", ";
                        emit_expression(cie->arguments()[i]);
                    }
                    _os << ")";
                }
            } else {
                _os << " = ";
                emit_expression(init);
            }
        }
        _os << ";\n";
        return;
    }

    // Return statement
    if (auto rs = std::dynamic_pointer_cast<return_statement>(stmt)) {
        indent(ind);
        _os << "return";
        if (auto e = rs->get_expression()) {
            _os << " ";
            emit_expression(e);
        }
        _os << ";\n";
        return;
    }

    // If/else statement
    if (auto ifs = std::dynamic_pointer_cast<if_else_statement>(stmt)) {
        indent(ind);
        _os << "if (";
        emit_expression(ifs->get_test_expr());
        _os << ") ";
        if (auto then_blk = std::dynamic_pointer_cast<block>(ifs->get_then_stmt())) {
            emit_block(*then_blk, ind);
        } else {
            _os << "\n";
            emit_statement(ifs->get_then_stmt(), ind + 1);
        }
        if (auto else_stmt = ifs->get_else_stmt()) {
            indent(ind);
            _os << "else ";
            if (auto else_blk = std::dynamic_pointer_cast<block>(else_stmt)) {
                emit_block(*else_blk, ind);
            } else if (std::dynamic_pointer_cast<if_else_statement>(else_stmt)) {
                // else if: inline
                emit_statement(else_stmt, ind);
            } else {
                _os << "\n";
                emit_statement(else_stmt, ind + 1);
            }
        }
        return;
    }

    // While statement
    if (auto ws = std::dynamic_pointer_cast<while_statement>(stmt)) {
        indent(ind);
        _os << "while (";
        emit_expression(ws->get_test_expr());
        _os << ") ";
        if (auto nested_blk = std::dynamic_pointer_cast<block>(ws->get_nested_stmt())) {
            emit_block(*nested_blk, ind);
        } else {
            _os << "\n";
            emit_statement(ws->get_nested_stmt(), ind + 1);
        }
        return;
    }

    // For statement
    if (auto fs = std::dynamic_pointer_cast<for_statement>(stmt)) {
        indent(ind);
        _os << "for (";
        if (auto& decl = fs->get_decl_stmt()) {
            if (decl->is_const()) _os << "const ";
            _os << decl->get_short_name() << " : ";
            emit_type(decl->get_type());
            if (auto init = decl->get_init_expr()) {
                _os << " = ";
                emit_expression(init);
            }
        }
        _os << "; ";
        if (auto test = fs->get_test_expr()) emit_expression(test);
        _os << "; ";
        if (auto step = fs->get_step_expr()) emit_expression(step);
        _os << ") ";
        if (auto nested_blk = std::dynamic_pointer_cast<block>(fs->get_nested_stmt())) {
            emit_block(*nested_blk, ind);
        } else {
            _os << "\n";
            emit_statement(fs->get_nested_stmt(), ind + 1);
        }
        return;
    }

    // Expression statement
    if (auto es = std::dynamic_pointer_cast<expression_statement>(stmt)) {
        indent(ind);
        emit_expression(es->get_expression());
        _os << ";\n";
        return;
    }

    // Fallback
    indent(ind);
    _os << "/* <unknown-stmt> */\n";
}

void k_source_emitter::emit_block(const block& blk, int ind) {
    _os << "{\n";
    for (auto& stmt : blk.get_statements()) {
        emit_statement(stmt, ind + 1);
    }
    indent(ind);
    _os << "}\n";
}

// ═════════════════════════════════════════════════════════════════════════════
// Parameter list emission
// ═════════════════════════════════════════════════════════════════════════════

void k_source_emitter::emit_parameter_list(const function& fn) {
    _os << "(";
    bool first = true;
    for (auto& param : fn.parameters()) {
        // Skip synthetic parameters
        if (param->get_short_name() == "this" || param->get_short_name() == "__parent__")
            continue;
        if (!first) _os << ", ";
        first = false;
        _os << param->get_short_name();
        if (param->is_varargs()) _os << "...";
        _os << " : ";
        if (param->is_varargs()) {
            // Varargs param type is T[] in the model; emit the element type T
            // because the parser will re-wrap it as T[] when it sees '...'
            auto ptype = param->get_type();
            if (type::is_reference(ptype)) ptype = ptype->get_subtype();
            if (type::is_array(ptype) && ptype->get_subtype())
                emit_type(ptype->get_subtype());
            else
                emit_type(param->get_type());
        } else {
            emit_type(param->get_type());
        }
        // Default value
        if (param->has_default_expr()) {
            _os << " = ";
            emit_expression(param->get_default_expr());
        }
    }
    _os << ")";
}

// ═════════════════════════════════════════════════════════════════════════════
// Member variable emission
// ═════════════════════════════════════════════════════════════════════════════

void k_source_emitter::emit_member_variable(const member_variable_definition& var) {
    switch (var.get_visibility()) {
        case PUBLIC:    _os << "public ";    break;
        case PROTECTED: _os << "protected "; break;
        case PRIVATE:   _os << "private ";   break;
        default: break;
    }
    if (type::is_const(std::const_pointer_cast<type>(var.get_type()))) {
        _os << "const ";
    }
    _os << var.get_short_name() << " : ";
    emit_type(std::const_pointer_cast<type>(var.get_type()));
    if (auto init = var.get_init_expr()) {
        // Skip compiler-generated default constructor invocations (empty args)
        if (auto cie = std::dynamic_pointer_cast<constructor_invocation_expression>(init)) {
            if (!cie->arguments().empty()) {
                _os << "(";
                for (size_t i = 0; i < cie->arguments().size(); ++i) {
                    if (i > 0) _os << ", ";
                    emit_expression(cie->arguments()[i]);
                }
                _os << ")";
            }
        } else {
            _os << " = ";
            emit_expression(init);
        }
    }
    _os << ";\n";
}

// ═════════════════════════════════════════════════════════════════════════════
// Function/method emission
// ═════════════════════════════════════════════════════════════════════════════

void k_source_emitter::emit_function(const function& fn) {
    // Visibility
    switch (fn.get_visibility()) {
        case PUBLIC:    _os << "public ";    break;
        case PROTECTED: _os << "protected "; break;
        case PRIVATE:   _os << "private ";   break;
        default: break;
    }

    if (fn.is_static()) _os << "static ";
    // Note: do NOT emit 'virtual' — in K, class methods are virtual by default
    // and 'virtual' is not a valid K specifier keyword.
    if (fn.is_abstract_func()) _os << "abstract ";

    _os << fn.get_short_name();
    emit_parameter_list(fn);

    if (fn.is_const_member()) _os << " const";

    if (fn.has_return_type()) {
        _os << " : ";
        emit_type(std::const_pointer_cast<type>(fn.get_return_type()));
    }

    if (fn.is_abstract_func()) {
        _os << ";\n";
    } else if (fn.is_override_specifier()) {
        _os << " override";
        if (const_cast<function&>(fn).get_block()) {
            _os << " ";
            emit_block(*const_cast<function&>(fn).get_block(), 1);
        } else {
            _os << ";\n";
        }
    } else if (fn.is_final_func()) {
        _os << " final";
        if (const_cast<function&>(fn).get_block()) {
            _os << " ";
            emit_block(*const_cast<function&>(fn).get_block(), 1);
        } else {
            _os << ";\n";
        }
    } else if (const_cast<function&>(fn).get_block()) {
        _os << " ";
        emit_block(*const_cast<function&>(fn).get_block(), 1);
    } else {
        _os << ";\n";
    }
}

void k_source_emitter::emit_constructor(const constructor& ctor) {
    switch (ctor.get_visibility()) {
        case PUBLIC:    _os << "public ";    break;
        case PROTECTED: _os << "protected "; break;
        case PRIVATE:   _os << "private ";   break;
        default: break;
    }

    // The constructor name is the aggregate name — we get it from the parent aggregate
    if (auto owner = const_cast<constructor&>(ctor).get_owner()) {
        _os << owner->get_short_name();
    }
    emit_parameter_list(ctor);

    // Member initializer list
    if (!ctor.member_inits().empty()) {
        _os << " : ";
        bool first = true;
        for (auto& mi : ctor.member_inits()) {
            if (!first) _os << ", ";
            first = false;
            _os << mi.member_name << "(";
            for (size_t i = 0; i < mi.args.size(); ++i) {
                if (i > 0) _os << ", ";
                emit_expression(mi.args[i]);
            }
            _os << ")";
        }
    }

    if (ctor.is_defaulted()) {
        _os << " -> default;\n";
    } else if (ctor.is_deleted()) {
        _os << " -> delete;\n";
    } else if (const_cast<constructor&>(ctor).get_block()) {
        _os << " ";
        emit_block(*const_cast<constructor&>(ctor).get_block(), 1);
    } else {
        _os << ";\n";
    }
}

void k_source_emitter::emit_destructor(const destructor& dtor) {
    _os << "~";
    if (auto owner = const_cast<destructor&>(dtor).get_owner()) {
        _os << owner->get_short_name();
    }
    _os << "()";
    if (const_cast<destructor&>(dtor).get_block()) {
        _os << " ";
        emit_block(*const_cast<destructor&>(dtor).get_block(), 1);
    } else {
        _os << ";\n";
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Top-level entry points
// ═════════════════════════════════════════════════════════════════════════════

std::string k_source_emitter::emit_template_aggregate(const aggregate& agg) {
    reset();

    auto* ti = agg.get_tpl_info();
    if (!ti) return {};

    // Template clause
    emit_template_clause(*ti);

    // Aggregate keyword + name
    _os << aggregate_keyword(agg) << " " << agg.get_short_name();

    // Qualifiers
    if (agg.is_abstract()) _os << " abstract";
    if (agg.is_final()) _os << " final";
    if (agg.is_const_struct()) _os << " const";

    // Bases
    emit_bases(agg);

    // Body
    _os << " {\n";

    // Member variables and methods (from children)
    for (auto& child : agg.get_children()) {
        if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(child)) {
            // Skip synthetic fields
            if (mv->get_short_name() == "__parent__" ||
                mv->get_short_name().find("__vptr") == 0 ||
                mv->get_short_name().find("__base_") == 0 ||
                mv->get_short_name().find("__vbptr_") == 0 ||
                mv->get_short_name().find("__vbase_") == 0)
                continue;
            _os << "\t";
            emit_member_variable(*mv);
        } else if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            // Skip compiler-generated, static ctor/dtor, and
            // regular constructors/destructors (emitted separately below).
            if (fn->is_compiler_generated()) continue;
            if (std::dynamic_pointer_cast<static_constructor>(child)) continue;
            if (std::dynamic_pointer_cast<static_destructor>(child)) continue;
            if (std::dynamic_pointer_cast<constructor>(child)) continue;
            if (std::dynamic_pointer_cast<destructor>(child)) continue;
            _os << "\t";
            emit_function(*fn);
        } else if (auto gv = std::dynamic_pointer_cast<global_variable_definition>(child)) {
            // Static member variables
            _os << "\tstatic " << gv->get_short_name() << " : ";
            emit_type(std::const_pointer_cast<type>(gv->get_type()));
            if (auto init = gv->get_init_expr()) {
                _os << " = ";
                emit_expression(init);
            }
            _os << ";\n";
        } else if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
            // Nested aggregates — skip for now (template export typically
            // does not contain nested type definitions)
        } else if (auto en = std::dynamic_pointer_cast<enumeration>(child)) {
            // Nested enumerations — skip for now
        }
    }

    // Constructors
    for (auto& ctor : agg.constructors()) {
        if (!ctor || ctor->is_compiler_generated()) continue;
        _os << "\t";
        emit_constructor(*ctor);
    }

    // Destructor
    if (auto dtor = agg.get_destructor()) {
        if (!dtor->is_compiler_generated()) {
            _os << "\t";
            emit_destructor(*dtor);
        }
    }

    _os << "}\n";
    return _os.str();
}

std::string k_source_emitter::emit_template_function(const function& fn) {
    reset();

    auto* ti = fn.get_tpl_info();
    if (!ti) return {};

    // Template clause
    emit_template_clause(*ti);

    // Function name + parameters + return type + body
    _os << fn.get_short_name();
    emit_parameter_list(fn);

    if (fn.has_return_type()) {
        _os << " : ";
        emit_type(std::const_pointer_cast<type>(fn.get_return_type()));
    }

    if (const_cast<function&>(fn).get_block()) {
        _os << " ";
        emit_block(*const_cast<function&>(fn).get_block(), 0);
    } else {
        _os << ";\n";
    }

    return _os.str();
}

// ═════════════════════════════════════════════════════════════════════════════
// Base class list emission
// ═════════════════════════════════════════════════════════════════════════════

void k_source_emitter::emit_bases(const aggregate& agg) {
    if (!agg.has_bases()) return;

    // Collect bases, skipping the implicit k::Object base for classes
    // (it will be re-injected by the compiler on re-parse).
    std::vector<const base_spec*> explicit_bases;
    for (auto& bs : agg.get_bases()) {
        // Check resolved base pointer
        if (bs.base && bs.base->get_short_name() == "Object") {
            auto parent_ns = bs.base->parent<ns>();
            if (parent_ns && parent_ns->get_short_name() == "k")
                continue;  // Skip implicit k::Object base
        }
        // Check unresolved base name (for template definitions where bases
        // haven't been resolved yet)
        if (!bs.base && bs.raw_name == "Object" && agg.is_class())
            continue;  // Skip implicit k::Object base (unresolved)
        explicit_bases.push_back(&bs);
    }
    if (explicit_bases.empty()) return;

    _os << " : ";
    bool first = true;
    for (auto* bs : explicit_bases) {
        if (!first) _os << ", ";
        first = false;
        if (bs->vis == PROTECTED) _os << "protected ";
        if (bs->base) {
            _os << fq_name_for_source(bs->base->get_fq_name());
        } else {
            // Base not resolved — check alias map
            if (auto it = _alias_map.find(bs->raw_name); it != _alias_map.end()) {
                _os << it->second;
            } else {
                _os << bs->raw_name;
            }
        }
    }
}

} // namespace k::model






















