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

#include "compiler.hpp"
#include "common/logger.hpp"
#include "parse/parser.hpp"
#include "parse/ast_dump.hpp"
#include "model/model.hpp"
#include "model/model_builder.hpp"
#include "model/model_dump.hpp"
#include "gen/resolvers.hpp"
#include  "gen/unit_llvm_ir_gen.hpp"

using namespace k;

k::log::logger logger;

std::unique_ptr<k::model::gen::unit_llvm_jit> gen(std::string_view src, bool optimize = true, bool dump = true) {
    k::compiler comp;
    comp.compile(src, false, dump);
    return comp.to_jit();
}



#if 1
int main() {
    std::cout << "Hello, World!" << std::endl;

    std::string source = R"SRC(

        struct plop {
            a : int;
            b: int;
            add() : int {
                return a + b;
            }
        }

        test_add() : int {
            q : plop;
            q.a = 10;
            q.b = 32;
            return q.add();
        }

        another_test() : int {
            return test_add() + 5;
        }

        glop : plop;
        test_glop() : int {
            glop.a = 10;
            glop.b = 32;
            return glop.add();
        }

        test() : int {
            p : plop;
            p.a = 10;
            p.b = p.a + 20;
            glop.a = 5;
            glop.b = p.a + 7;
            p.b += 12;
            return p.add();
        }

    )SRC";

    struct plop {
        int a;
        int b;
    };

    try {
        auto jit = gen(source, true, true);
        if (!jit) {
            std::cerr << "JIT instantiation error." << std::endl;
            return -1;
        }

        auto test_add = jit->lookup_symbol < int(*)() > ("test_add");
        auto res_add = test_add();
        std::cout << "test_add() = " << res_add << std::endl;

        auto test = jit->lookup_symbol < int(*)() > ("test");
        auto res = test();
        std::cout << "test() = " << res << std::endl;

        auto glop = jit->lookup_symbol < plop* > ("glop");
        std::cout << "glop->a = " << glop->a << std::endl;
        std::cout << "glop->b = " << glop->b << std::endl;

        auto test_glop = jit->lookup_symbol < int(*)() > ("test_glop");
        auto res_glop = test_glop();
        std::cout << "test_glop() = " << res_glop << std::endl;
        std::cout << "glop->a = " << glop->a << std::endl;
        std::cout << "glop->b = " << glop->b << std::endl;

        auto another_test = jit->lookup_symbol < int(*)() > ("another_test");
        auto another_res = another_test();
        std::cout << "another_test() = " << another_res << std::endl;


    } catch(k::model::gen::resolution_error err) {
        std::cerr << "Resolution error: " << err.what() << std::endl;
    } catch(k::model::gen::generation_error err) {
        std::cerr << "Generation error: " << err.what() << std::endl;
    } catch(std::exception err) {
        std::cerr << "Other exception: " << err.what() << std::endl;
    } catch(...) {
        std::cerr << "Error" << std::endl;
    }
    logger.print();

    return 0;
}
#endif
