#include <iostream>

#include <string_view>
#include <vector>

#include "logger.hpp"
#include "parser.hpp"
#include "ast_dump.hpp"
#include "unit.hpp"
#include "ast_unit_visitor.hpp"
#include "unit_dump.hpp"
#include "symbol_type_resolver.hpp"
#include  "unit_llvm_ir_gen.hpp"

using namespace k;

k::log::logger logger;


#if 1
int main() {
    std::cout << "Hello, World!" << std::endl;

#if 0
    std::string source = R"SRC(
    module titi;
    import io;
    import net;
    /* Hello */

    ploc(a: int, b: int) : int {
        if(a > b)
            return a;
        else {
            return b;
        }
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
        fibo(i: unsigned short) : unsigned long {
            if(i==0) return 1;
            else if(i==1) return 1;
            return fibo(i-1) + fibo(i-2);
        }
    )SRC";
#endif

#if 0
    std::string source = R"SRC(
        test(a: int, b: float) : bool {
            return a > b;
        }
    )SRC";
#endif

    try {

        k::parse::parser parser(logger, source);
        std::shared_ptr<k::parse::ast::unit> ast_unit = parser.parse_unit();

        k::parse::dump::ast_dump_visitor visit(std::cout);
        std::cout << "#" << std::endl << "# Parsing" << std::endl << "#" << std::endl;
        visit.visit_unit(*ast_unit);

        k::unit::dump::unit_dump unit_dump(std::cout);

        k::unit::unit unit;
        parse::ast_unit_visitor::visit(logger, *ast_unit, unit);
        std::cout << "#" << std::endl << "# Unit construction" << std::endl << "#" << std::endl;
        unit_dump.dump(unit);

        k::unit::symbol_type_resolver resolver(logger, unit);
        resolver.resolve();
        std::cout << "#" << std::endl << "# Resolution" << std::endl << "#" << std::endl;
        unit_dump.dump(unit);

        k::unit::gen::unit_llvm_ir_gen gen(logger, unit);
        std::cout << "#" << std::endl << "# LLVM Module" << std::endl << "#" << std::endl;
        unit.accept(gen);
        gen.verify();
        gen.dump();

        std::cout << "#" << std::endl << "# LLVM Optimize Module" << std::endl << "#" << std::endl;
        gen.optimize_functions();
        gen.verify();
        gen.dump();

        logger.print();

        auto jit = gen.to_jit();
        if (!jit) {
            std::cerr << "JIT instantiation error." << std::endl;
            return -1;
        }

#if 1
        auto fibo = jit.get()->lookup_symbol < unsigned short(*) (unsigned long long) > ("fibo");
        std::cout << "Test : fibo(0) = " << (fibo(2)) << std::endl;
#endif

#if 0
        auto ploc = jit.get()->lookup_symbol < float(*)
        (float) > ("ploc");
        std::cout << "Test : ploc(1.2) = " << (ploc(1.2)) << std::endl;
#endif


#if 0
        int (*test)(int, int) = (int(*)(int, int)) jit.get()->lookup_symbol<int(*)(int, int)>("test");

        std::cout << "Test : test(0,0) = " << (test(0, 0)) << std::endl;
        std::cout << "Test : test(1,2) = " << (test(1, 2)) << std::endl;

        int (*sum)(int, int) = (int(*)(int, int)) jit.get()->lookup_symbol<int(*)(int, int)>("sum");

        std::cout << "Test : sum(0,0) = " << (sum(0, 0)) << std::endl;
        std::cout << "Test : sum(1,2) = " << (sum(1, 2)) << std::endl;
#endif

    } catch(...) {
    }
    logger.print();

    return 0;
}
#endif
