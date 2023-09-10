#include <iostream>

#include <string_view>
#include <vector>

#include "parser.hpp"
#include "ast_dump.hpp"
#include "unit.hpp"
#include "ast_unit_visitor.hpp"
#include "unit_dump.hpp"
#include "symbol_type_resolver.hpp"
#include  "unit_llvm_ir_gen.hpp"

using namespace k;

#if 1
int main() {
    std::cout << "Hello, World!" << std::endl;

#if 0
    std::string source = R"SRC(
    module titi;
    import io;
    import net;
    /* Hello */

    ploc(p: int) : int {
        c : char;
        c = p + 1;
        return p * 2 + c;
    }

    test(titi: int, toto: long) : long {
        return titi + toto + ploc(toto);
    }

    namespace titi {
        protected:
        static const plic : int = 0;
        public :
        sum(a : int, b : int) : int {
            res : int;
            {
                res = a + b * 2;
                plic = res / 2;
                res += 3;
            }
            return res;
        }
    }
    )SRC";
#endif

#if 1
    std::string source = R"SRC(
    ploc(a:int, b: int) : bool {
        return a >= b;
    }
    )SRC";
#endif


    k::parse::parser parser(source);
    k::parse::ast::unit ast_unit = parser.parse_unit();

    k::parse::dump::ast_dump_visitor visit(std::cout);
    std::cout << "#" << std::endl << "# Parsing" << std::endl << "#" << std::endl;
    visit.visit_unit(ast_unit);

    k::unit::dump::unit_dump unit_dump(std::cout);

    k::unit::unit unit;
    parse::ast_unit_visitor::visit(ast_unit, unit);
    std::cout << "#" << std::endl << "# Unit construction" << std::endl << "#" << std::endl;
    unit_dump.dump(unit);

    k::unit::symbol_type_resolver resolver(unit);
    resolver.resolve();
    std::cout << "#" << std::endl << "# Resolution" << std::endl << "#" << std::endl;
    unit_dump.dump(unit);

    k::unit::gen::unit_llvm_ir_gen gen(unit);
    std::cout << "#" << std::endl << "# LLVM Module" << std::endl << "#" << std::endl;
    unit.accept(gen);
    gen.verify();
    gen.dump();

    std::cout << "#" << std::endl << "# LLVM Optimize Module" << std::endl << "#" << std::endl;
    gen.optimize_functions();
    gen.verify();
    gen.dump();

    auto jit = gen.to_jit();
    if(!jit){
        std::cerr << "JIT instantiation error." << std::endl;
        return -1;
    }

#if 0
    int (*test)(int, int) = (int(*)(int, int)) jit.get()->lookup_symbol<int(*)(int, int)>("test");

    std::cout << "Test : test(0,0) = " << (test(0, 0)) << std::endl;
    std::cout << "Test : test(1,2) = " << (test(1, 2)) << std::endl;

    int (*sum)(int, int) = (int(*)(int, int)) jit.get()->lookup_symbol<int(*)(int, int)>("sum");

    std::cout << "Test : sum(0,0) = " << (sum(0, 0)) << std::endl;
    std::cout << "Test : sum(1,2) = " << (sum(1, 2)) << std::endl;
#endif

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