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

#include "compiler.hpp"

#include <iostream>

#include "gen/resolvers.hpp"
#include "gen/unit_llvm_ir_gen.hpp"
#include "parse/ast_dump.hpp"
#include "model/model_builder.hpp"
#include "model/model_dump.hpp"

namespace k {

compiler::compiler(llvm::TargetMachine* target):
    _parser(_log),
    _context(k::model::context::create()),
    _target(target)
{}

void compiler::compile(std::string_view src, bool optimize, bool dump) {
    try {
        _parser.parse(src);
        _ast_unit = _parser.parse_unit();

        if(dump) {
            std::cout << "#" << std::endl << "# Parsing" << std::endl << "#" << std::endl;
            k::parse::dump::ast_dump_visitor visit(std::cout);
            visit.visit_unit(*_ast_unit);
        }

        _unit = k::model::unit::create(_context);
        if(dump) {
            std::cout << "#" << std::endl << "# Unit construction" << std::endl << "#" << std::endl;
        }
        k::model::model_builder::visit(_log, _context, *_ast_unit, *_unit);

        if(dump) {
            k::model::dump::unit_dump unit_dump(std::cout);
            unit_dump.dump(*_unit);
        }

        k::model::gen::symbol_resolver var_resolver(_log, _context, *_unit);
        if(dump) {
            std::cout << "#" << std::endl << "# Variable resolution" << std::endl << "#" << std::endl;
        }
        var_resolver.resolve();

        if(dump) {
            k::model::dump::unit_dump unit_dump(std::cout);
            unit_dump.dump(*_unit);
        }

        _context->resolve_types();

        k::model::gen::type_reference_resolver type_ref_resolver(_log, _context, *_unit);
        type_ref_resolver.resolve();

        if(dump) {
            k::model::dump::unit_dump unit_dump(std::cout);
            std::cout << "#" << std::endl << "# Type resolution" << std::endl << "#" << std::endl;
            unit_dump.dump(*_unit);
        }

        process_gen(optimize, dump);
    } catch (std::exception e) {
        std::cerr << "Exception : " << e.what() << std::endl;
    }
}

void compiler::process_gen(bool optimize, bool dump) {

    auto gen = std::make_unique<k::model::gen::unit_llvm_ir_gen>(_log, _context, *_unit);

    if (_target) {
        gen->get_module().setDataLayout(_target->createDataLayout());
        gen->get_module().setTargetTriple(_target->getTargetTriple().getTriple());
    }

    if(dump) {
        std::cout << "#" << std::endl << "# LLVM Module" << std::endl << "#" << std::endl;
    }
    _unit->accept(*gen);
    gen->verify();

    if(dump) {
        gen->dump();
    }

    if (optimize) {
        if(dump) {
            std::cout << "#" << std::endl << "# LLVM Optimize Module" << std::endl << "#" << std::endl;
        }
        gen->optimize_functions();
        gen->verify();
        if(dump) {
            gen->dump();
        }
    }

    _gen = std::move(gen);
}

std::unique_ptr<k::model::gen::unit_llvm_jit> compiler::to_jit() {
    if (!_gen) {
        process_gen();
    }
    if (_gen) {
        auto jit = _gen->to_jit();
        _gen.reset();
        return jit;
    } else {
        std::cerr << "Error : Failed to generate code for JIT." << std::endl;
        return nullptr;
    }
}

bool compiler::gen_object_file(const std::string& output_file) {
    if (!_gen) {
        process_gen();
    }
    if (_gen) {
        std::error_code EC;
        llvm::raw_fd_ostream dest(output_file, EC, llvm::sys::fs::OF_None);
        if (EC) {
            llvm::errs() << "Could not open file: " << EC.message();
            return false;
        }

        llvm::legacy::PassManager pass;
        auto FileType = llvm::CodeGenFileType::ObjectFile;

        if (_target->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
            llvm::errs() << "TargetMachine can't emit a file of this type";
            return false;
        }

        pass.run(_gen->get_module());
        dest.flush();
        return true;

    } else {
        std::cerr << "Error : Failed to generate code for object file." << std::endl;
        return false;
    }
}


} // k