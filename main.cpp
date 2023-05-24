#include <iostream>

#include <string_view>
#include <vector>

#include "parser.hpp"
#include "ast_dump.hpp"
#include "unit.hpp"
#include "ast_unit_visitor.hpp"
#include "unit_dump.hpp"

using namespace k;

#if 1
int main() {
    std::cout << "Hello, World!" << std::endl;

    std::string source = R"SRC(
    module titi;
    import io;
    import net;
    /* Hello */

    test(titi: int, toto: int) : int {
        titi + toto;
        return titi + toto;
    }

    namespace titi {
        protected:
        static const plic : long = 0;
        public :
        sum(a : int, b : int) : int {
            res : int;
            {
                res = a + b * 2;
            }
            return res;
        }

    }
    )SRC";

    k::parse::parser parser(source);
    k::parse::ast::unit ast_unit = parser.parse_unit();

    k::parse::dump::ast_dump_visitor visit(std::cout);
    visit.visit_unit(ast_unit);

    k::unit::unit unit;
    parse::ast_unit_visitor::visit(ast_unit, unit);
    k::unit::dump::unit_dump unit_dump(std::cout);
    unit_dump.dump(unit);

    return 0;
}
#endif

#if 0
int main() {
    std::cout << "Hello, World!" << std::endl;

    unit::unit unit;

    auto rootns = unit.get_root_namespace();
    auto add_int = rootns->define_function("add_int");

    add_int->return_type(unit::unresolved_type::from_string("int32"));
    add_int->append_parameter("a", unit::unresolved_type::from_string("int32"));
    add_int->append_parameter("b", unit::unresolved_type::from_string("int32"));

    add_int->get_block()->append_expression_statement(
            unit::addition_expression::make_shared(
                    unit::unresolved_variable_expression::from_string("a"),
                    unit::addition_expression::make_shared(
                        unit::unresolved_variable_expression::from_string("b"),
                        unit::value_expression::from_value(4)
                    )
            )
    );

    return 0;
}
#endif