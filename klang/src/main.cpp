/*
 * K Language compiler
 *
 * Copyright 2023 Emilien Kia
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
#include <iostream>

#include <string_view>
#include <vector>

#include "common/logger.hpp"
#include "parse/parser.hpp"
#include "parse/ast_dump.hpp"
#include "unit/unit.hpp"
#include "unit/ast_unit_visitor.hpp"
#include "unit/unit_dump.hpp"
#include "gen/symbol_type_resolver.hpp"
#include  "gen/unit_llvm_ir_gen.hpp"

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
        sum(i : short) : int {
            r : int;
            r = 0;
            for(n: short = 0; n<i; n+=1) {
                r += n;
            }
            return r;
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
        auto cumul = jit.get()->lookup_symbol < int(*) (int) > ("sum");
        std::cout << "Test : cumul(0) = " << (cumul(0)) << std::endl;
        std::cout << "Test : cumul(1) = " << (cumul(1)) << std::endl;
        std::cout << "Test : cumul(2) = " << (cumul(2)) << std::endl;
        std::cout << "Test : cumul(3) = " << (cumul(3)) << std::endl;
        std::cout << "Test : cumul(4) = " << (cumul(4)) << std::endl;
        std::cout << "Test : cumul(5) = " << (cumul(5)) << std::endl;
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
