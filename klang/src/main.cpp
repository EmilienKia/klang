/*
 * K Language compiler
 *
 * Copyright 2023-2024 Emilien Kia
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
#include "model/model.hpp"
#include "model/model_builder.hpp"
#include "model/model_dump.hpp"
#include "gen/symbol_type_resolver.hpp"
#include  "gen/unit_llvm_ir_gen.hpp"

using namespace k;

k::log::logger logger;


#if 1
int main() {
    std::cout << "Hello, World!" << std::endl;

#if 1
    std::string source = R"SRC(

        a : int;

        assign(var: int*, val: int) : int {
            *var = val;
            return *var;
        }

        test() : int {
            return assign(&a, 4);
        }
    )SRC";
#endif

#if 0
    std::string source = R"SRC(

        a : int;

        assign(var: int&, val: int) : int {
            var = val;
            return var;
        }

        test() : int {
            return assign(a, 4);
        }
    )SRC";
#endif

    try {

        k::parse::parser parser(logger, source);
        std::shared_ptr<k::parse::ast::unit> ast_unit = parser.parse_unit();

        k::parse::dump::ast_dump_visitor visit(std::cout);
        std::cout << "#" << std::endl << "# Parsing" << std::endl << "#" << std::endl;
        visit.visit_unit(*ast_unit);

        k::model::dump::unit_dump unit_dump(std::cout);

        k::model::unit unit;
        k::model::model_builder::visit(logger, *ast_unit, unit);
        std::cout << "#" << std::endl << "# Unit construction" << std::endl << "#" << std::endl;
        unit_dump.dump(unit);

        k::model::gen::symbol_type_resolver resolver(logger, unit);
        resolver.resolve();
        std::cout << "#" << std::endl << "# Resolution" << std::endl << "#" << std::endl;
        unit_dump.dump(unit);

        k::model::gen::unit_llvm_ir_gen gen(logger, unit);
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

        auto assign = jit.get()->lookup_symbol< int(*)(int&, int) >("assign");
        int var = 0;
        std::cout << "Var:" << var << "(=0)" << std::endl;
        std::cout << "Assign (&var, 1):" << std::endl;
        int res = assign(var, 1);
        std::cout << "Res:" << res << "(=1)" << std::endl;
        std::cout << "Var:" << var << "(=1)" << std::endl;
#endif


    } catch(...) {
    }
    logger.print();

    return 0;
}
#endif
