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
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>

#include "../src/common/logger.hpp"
#include "../src/parse/parser.hpp"
#include "../src/parse/ast_dump.hpp"
#include "../src/model/model.hpp"
#include "../src/model/model_builder.hpp"
#include "../src/model/model_dump.hpp"
#include "../src/gen/resolvers.hpp"
#include "../src/gen/generators.hpp"
#include "../src/compiler.hpp"
#include "../src/common/process.hpp"


bool compile_text(const std::string_view& source, const std::string& out_file) {
    std::string target_triple = llvm::sys::getDefaultTargetTriple();
    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(target_triple, error);
    if(!target) {
        std::cerr << "Problem to find target: " << error << std::endl;
    }

    std::string cpu = "generic";
    std::string features = "";

    llvm::TargetOptions target_options;
    std::optional<llvm::Reloc::Model> reloc_model;
    auto target_machine = target->createTargetMachine(
            target_triple, cpu,
            features,
            target_options,
            reloc_model);

    auto compiler = k::compiler::create(target_machine);
    compiler->parse_source(source, true, false);

    return compiler->gen_executable(out_file);
}

k::tools::exec_result build_and_exec(const std::string_view& src) {
    char* out_file = std::tmpnam(nullptr);
    if (out_file==nullptr) {
        throw std::runtime_error("Could not create temporary output file for building test");
    }
    if (!compile_text(src, out_file)) {
        std::filesystem::remove(out_file);
        throw std::runtime_error("Error building source code");
    }

    auto res = k::tools::run_process(out_file, {});
    std::filesystem::remove(out_file);
    return res;
}


TEST_CASE( "Fibonacci 8", "[gen]" ) {
    auto res = build_and_exec(R"SRC(
        module fibo;

        fibo(i: unsigned short) : unsigned int {
            if(i==0) return 1;
            else if(i==1) return 1;
            return fibo(i-1) + fibo(i-2);
        }

        main() : int {
            return fibo(8);
        }
    )SRC");

    REQUIRE( res.exit_code == 34 );

}
