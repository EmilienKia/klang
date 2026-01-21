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
#ifndef KLANG_COMPILER_HPP
#define KLANG_COMPILER_HPP
#include <string_view>

#include "common/logger.hpp"
#include "parse/parser.hpp"

namespace llvm {
class TargetMachine;
}

namespace k {
namespace model {
namespace gen {
class unit_llvm_ir_gen;
class unit_llvm_jit;
}

class unit;
class context;
}

class compiler {
protected:
    k::log::logger _log;
    k::parse::parser _parser;
    std::shared_ptr<k::parse::ast::unit> _ast_unit;
    std::shared_ptr<model::context> _context;
    std::shared_ptr<model::unit> _unit;

    std::unique_ptr<model::gen::unit_llvm_ir_gen> _gen;

    llvm::TargetMachine* _target;

    void process_gen(bool optimize = true, bool dump = true);

public:
    compiler(llvm::TargetMachine* target = nullptr);

    void compile(std::string_view src, bool optimize = true, bool dump = true);

    std::unique_ptr<k::model::gen::unit_llvm_jit> to_jit();

    bool gen_object_file(const std::string& output_file);
};

} // k
#endif //KLANG_COMPILER_HPP