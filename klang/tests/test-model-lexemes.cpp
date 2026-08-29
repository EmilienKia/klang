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

#include <catch2/catch_all.hpp>

#include "helpers.hpp"
#include "../src/model/model.hpp"
#include "../src/model/model_aggregate.hpp"
#include "../src/model/model_union.hpp"
#include "../src/model/model_enum.hpp"
#include "../src/model/model_alias.hpp"
#include "../src/model/statements.hpp"
#include "../src/model/expressions.hpp"
#include "../src/model/operators.hpp"
#include "../src/lex/lexemes.hpp"

using namespace k::model;

TEST_CASE("Model lexemes: aggregate declarations (struct, class, interface)", "[model][lexemes]") {
    auto comp = compile_model(R"SRC(
        module model_lexemes_01;

        struct MyStruct {
            member_var : int;
        }

        class MyClass {
            field : int;
        }

        interface MyInterface {
            foo() : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);
    REQUIRE(comp->get_unit() != nullptr);

    auto ns = comp->get_unit()->get_root_namespace();
    REQUIRE(ns != nullptr);

    // Struct
    auto st = ns->get_structure("MyStruct");
    REQUIRE(st != nullptr);
    auto st_interest = st->get_interest_lexeme();
    REQUIRE(st_interest.has_value());
    CHECK(k::lex::is<k::lex::identifier>(*st_interest));
    CHECK(k::lex::as_lexeme(st_interest).content == "MyStruct");

    auto st_first = st->get_first_lexeme();
    REQUIRE(st_first.has_value());
    CHECK(k::lex::as_lexeme(st_first).content == "struct");

    auto st_last = st->get_last_lexeme();
    REQUIRE(st_last.has_value());
    CHECK(k::lex::as_lexeme(st_last).content == "}");

    // struct_type delegates
    auto st_type = st->get_struct_type();
    REQUIRE(st_type != nullptr);
    auto st_type_interest = st_type->get_interest_lexeme();
    REQUIRE(st_type_interest.has_value());
    CHECK(k::lex::as_lexeme(st_type_interest).content == "MyStruct");

    // Member variable
    auto mv = st->get_variable("member_var");
    REQUIRE(mv != nullptr);
    auto mv_elem = std::dynamic_pointer_cast<element>(mv);
    REQUIRE(mv_elem != nullptr);
    auto mv_interest = mv_elem->get_interest_lexeme();
    REQUIRE(mv_interest.has_value());
    CHECK(k::lex::as_lexeme(mv_interest).content == "member_var");

    // Class
    auto kl = ns->get_aggregate("MyClass");
    REQUIRE(kl != nullptr);
    auto kl_interest = kl->get_interest_lexeme();
    REQUIRE(kl_interest.has_value());
    CHECK(k::lex::as_lexeme(kl_interest).content == "MyClass");
    auto kl_first = kl->get_first_lexeme();
    REQUIRE(kl_first.has_value());
    CHECK(k::lex::as_lexeme(kl_first).content == "class");

    // Interface
    auto iface = ns->get_aggregate("MyInterface");
    REQUIRE(iface != nullptr);
    auto iface_interest = iface->get_interest_lexeme();
    REQUIRE(iface_interest.has_value());
    CHECK(k::lex::as_lexeme(iface_interest).content == "MyInterface");
}

TEST_CASE("Model lexemes: union, enum, alias/typedef", "[model][lexemes]") {
    auto comp = compile_model(R"SRC(
        module model_lexemes_02;

        union MyUnion {
            first : int;
            second : float;
        }

        enum MyEnum {
            A;
            B;
        }

        typedef MyInt : int;
        alias OtherInt : int;
    )SRC");
    REQUIRE(comp != nullptr);
    REQUIRE(comp->get_unit() != nullptr);

    auto ns = comp->get_unit()->get_root_namespace();
    REQUIRE(ns != nullptr);

    // Union
    auto un = ns->get_union("MyUnion");
    REQUIRE(un != nullptr);
    auto un_interest = un->get_interest_lexeme();
    REQUIRE(un_interest.has_value());
    CHECK(k::lex::as_lexeme(un_interest).content == "MyUnion");
    auto un_first = un->get_first_lexeme();
    REQUIRE(un_first.has_value());
    CHECK(k::lex::as_lexeme(un_first).content == "union");

    // Enum
    auto en = ns->get_enum("MyEnum");
    REQUIRE(en != nullptr);
    auto en_interest = en->get_interest_lexeme();
    REQUIRE(en_interest.has_value());
    CHECK(k::lex::as_lexeme(en_interest).content == "MyEnum");
    auto en_first = en->get_first_lexeme();
    REQUIRE(en_first.has_value());
    CHECK(k::lex::as_lexeme(en_first).content == "enum");

    auto en_type = en->get_enum_type();
    if (en_type) {
        auto en_type_interest = en_type->get_interest_lexeme();
        REQUIRE(en_type_interest.has_value());
        CHECK(k::lex::as_lexeme(en_type_interest).content == "MyEnum");
    }

    // Typedef
    auto td = ns->get_alias("MyInt");
    REQUIRE(td != nullptr);
    auto td_interest = td->get_interest_lexeme();
    REQUIRE(td_interest.has_value());
    CHECK(k::lex::as_lexeme(td_interest).content == "MyInt");

    // Alias
    auto al = ns->get_alias("OtherInt");
    REQUIRE(al != nullptr);
    auto al_interest = al->get_interest_lexeme();
    REQUIRE(al_interest.has_value());
    CHECK(k::lex::as_lexeme(al_interest).content == "OtherInt");
}

TEST_CASE("Model lexemes: function, parameters, and local variables", "[model][lexemes]") {
    auto comp = compile_model(R"SRC(
        module model_lexemes_03;

        compute(first_param : int, second_param : int) : int {
            local_var : int = first_param + second_param;
            return local_var;
        }
    )SRC");
    REQUIRE(comp != nullptr);
    REQUIRE(comp->get_unit() != nullptr);

    auto ns = comp->get_unit()->get_root_namespace();
    REQUIRE(ns != nullptr);

    auto fn = ns->get_function("compute");
    REQUIRE(fn != nullptr);
    auto fn_interest = fn->get_interest_lexeme();
    REQUIRE(fn_interest.has_value());
    CHECK(k::lex::as_lexeme(fn_interest).content == "compute");

    // Parameters
    REQUIRE(fn->get_parameter_size() == 2);
    auto p0 = fn->get_parameter(0);
    REQUIRE(p0 != nullptr);
    auto p0_interest = p0->get_interest_lexeme();
    REQUIRE(p0_interest.has_value());
    CHECK(k::lex::as_lexeme(p0_interest).content == "first_param");

    auto p1 = fn->get_parameter(1);
    REQUIRE(p1 != nullptr);
    auto p1_interest = p1->get_interest_lexeme();
    REQUIRE(p1_interest.has_value());
    CHECK(k::lex::as_lexeme(p1_interest).content == "second_param");

    // Function body statements
    auto blk = fn->get_existing_block();
    REQUIRE(blk != nullptr);
    REQUIRE(blk->get_statements().size() >= 2);

    // Variable statement (local_var)
    auto vs = std::dynamic_pointer_cast<variable_statement>(blk->get_statements()[0]);
    REQUIRE(vs != nullptr);
    auto vs_interest = vs->get_interest_lexeme();
    REQUIRE(vs_interest.has_value());
    CHECK(k::lex::as_lexeme(vs_interest).content == "local_var");

    // Return statement
    auto rs = std::dynamic_pointer_cast<return_statement>(blk->get_statements()[1]);
    REQUIRE(rs != nullptr);
    auto rs_interest = rs->get_interest_lexeme();
    REQUIRE(rs_interest.has_value());
    CHECK(k::lex::as_lexeme(rs_interest).content == "return");
}

TEST_CASE("Model lexemes: expressions and operators", "[model][lexemes]") {
    auto comp = compile_model(R"SRC(
        module model_lexemes_04;

        test_ops(a : int, b : int) : int {
            c : int = a + b;
            d : int = -c;
            e : int = (a > b) ? a : b;
            return e;
        }
    )SRC");
    REQUIRE(comp != nullptr);
    REQUIRE(comp->get_unit() != nullptr);

    auto ns = comp->get_unit()->get_root_namespace();
    REQUIRE(ns != nullptr);

    auto fn = ns->get_function("test_ops");
    REQUIRE(fn != nullptr);
    auto blk = fn->get_existing_block();
    REQUIRE(blk != nullptr);

    // First statement: c : int = a + b;
    auto vs1 = std::dynamic_pointer_cast<variable_statement>(blk->get_statements()[0]);
    REQUIRE(vs1 != nullptr);
    auto init1 = vs1->get_init_expr();
    REQUIRE(init1 != nullptr);
    // Interest of binary addition is the '+' operator
    auto add_interest = init1->get_interest_lexeme();
    REQUIRE(add_interest.has_value());
    CHECK(k::lex::as_lexeme(add_interest).content == "+");
    CHECK(k::lex::is<k::lex::operator_>(*add_interest));

    // First/last of addition
    auto add_first = init1->get_first_lexeme();
    REQUIRE(add_first.has_value());
    CHECK(k::lex::as_lexeme(add_first).content == "a");
    auto add_last = init1->get_last_lexeme();
    REQUIRE(add_last.has_value());
    CHECK(k::lex::as_lexeme(add_last).content == "b");

    // Second statement: d : int = -c;
    auto vs2 = std::dynamic_pointer_cast<variable_statement>(blk->get_statements()[1]);
    REQUIRE(vs2 != nullptr);
    auto init2 = vs2->get_init_expr();
    REQUIRE(init2 != nullptr);
    // Interest of unary minus is '-'
    auto minus_interest = init2->get_interest_lexeme();
    REQUIRE(minus_interest.has_value());
    CHECK(k::lex::as_lexeme(minus_interest).content == "-");

    // Third statement: e : int = (a > b) ? a : b;
    auto vs3 = std::dynamic_pointer_cast<variable_statement>(blk->get_statements()[2]);
    REQUIRE(vs3 != nullptr);
    auto init3 = vs3->get_init_expr();
    REQUIRE(init3 != nullptr);
    auto ternary_interest = init3->get_interest_lexeme();
    REQUIRE(ternary_interest.has_value());
    auto ternary_first = init3->get_first_lexeme();
    REQUIRE(ternary_first.has_value());
    auto ternary_last = init3->get_last_lexeme();
    REQUIRE(ternary_last.has_value());
    CHECK(k::lex::as_lexeme(ternary_last).content == "b");
}

TEST_CASE("Model lexemes: control statements (if, while, for, foreach, throw, try-catch)", "[model][lexemes]") {
    auto comp = compile_model(R"SRC(
        module model_lexemes_05;

        namespace k {
            class Throwable {}
            class Exception : public Throwable {}
        }

        class MyError : public k::Exception {}

        test_control(x : int, arr : int[5]) : void {
            if (x > 0) {
                x = x - 1;
            }
            while (x < 10) {
                x = x + 1;
            }
            for (i : int = 0; i < 5; ++i) {
                x = x + i;
            }
            for (elem : int = arr) {
                x = x + elem;
            }
            try {
                err : MyError;
                throw err;
            } catch (e : MyError&) {
                x = 0;
            }
        }
    )SRC");
    REQUIRE(comp != nullptr);
    REQUIRE(comp->get_unit() != nullptr);

    auto ns = comp->get_unit()->get_root_namespace();
    REQUIRE(ns != nullptr);

    auto fn = ns->get_function("test_control");
    REQUIRE(fn != nullptr);
    auto blk = fn->get_existing_block();
    REQUIRE(blk != nullptr);
    REQUIRE(blk->get_statements().size() >= 5);

    // If statement
    auto if_stmt = std::dynamic_pointer_cast<if_else_statement>(blk->get_statements()[0]);
    REQUIRE(if_stmt != nullptr);
    auto if_interest = if_stmt->get_interest_lexeme();
    REQUIRE(if_interest.has_value());
    CHECK(k::lex::as_lexeme(if_interest).content == "if");

    // While statement
    auto while_stmt = std::dynamic_pointer_cast<while_statement>(blk->get_statements()[1]);
    REQUIRE(while_stmt != nullptr);
    auto while_interest = while_stmt->get_interest_lexeme();
    REQUIRE(while_interest.has_value());
    CHECK(k::lex::as_lexeme(while_interest).content == "while");

    // For statement
    auto for_stmt = std::dynamic_pointer_cast<for_statement>(blk->get_statements()[2]);
    REQUIRE(for_stmt != nullptr);
    auto for_interest = for_stmt->get_interest_lexeme();
    REQUIRE(for_interest.has_value());
    CHECK(k::lex::as_lexeme(for_interest).content == "for");

    // Foreach statement
    auto foreach_stmt = std::dynamic_pointer_cast<foreach_statement>(blk->get_statements()[3]);
    REQUIRE(foreach_stmt != nullptr);
    auto foreach_interest = foreach_stmt->get_interest_lexeme();
    REQUIRE(foreach_interest.has_value());
    CHECK(k::lex::as_lexeme(foreach_interest).content == "for");

    // Try-catch statement
    auto try_stmt = std::dynamic_pointer_cast<try_catch_statement>(blk->get_statements()[4]);
    REQUIRE(try_stmt != nullptr);
    auto try_interest = try_stmt->get_interest_lexeme();
    REQUIRE(try_interest.has_value());
    CHECK(k::lex::as_lexeme(try_interest).content == "try");

    // Try body contains throw statement
    auto try_body = try_stmt->get_try_body();
    REQUIRE(try_body != nullptr);
    REQUIRE(try_body->get_statements().size() >= 2);
    auto throw_stmt = std::dynamic_pointer_cast<throw_statement>(try_body->get_statements()[1]);
    REQUIRE(throw_stmt != nullptr);
    auto throw_interest = throw_stmt->get_interest_lexeme();
    REQUIRE(throw_interest.has_value());
    CHECK(k::lex::as_lexeme(throw_interest).content == "throw");

    // Catch clause
    REQUIRE(!try_stmt->get_catch_clauses().empty());
    auto catch_clause_ptr = try_stmt->get_catch_clauses()[0];
    REQUIRE(catch_clause_ptr != nullptr);
    auto catch_interest = catch_clause_ptr->get_interest_lexeme();
    REQUIRE(catch_interest.has_value());
    CHECK(k::lex::as_lexeme(catch_interest).content == "e");
}
