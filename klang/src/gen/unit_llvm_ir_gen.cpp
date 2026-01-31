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

#include "unit_llvm_ir_gen.hpp"

#include "../model/context.hpp"

#include <llvm/IR/Verifier.h>

#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"

#include "llvm/Target/TargetMachine.h"

namespace k {
class compiler;
}

namespace k::model::gen {

//
// Exceptions
//
generation_error::generation_error(const std::string &arg) :
        runtime_error(arg)
{}

generation_error::generation_error(const char *string) :
        runtime_error(string)
{}

//
// LLVM model generator
//

unit_llvm_ir_gen::unit_llvm_ir_gen(k::log::logger& logger, std::shared_ptr<context> context, unit& unit):
lexeme_logger(logger, 0x40000),
_context(context),
_unit(unit)
{
    _builder = std::make_unique<llvm::IRBuilder<>>(**_context);
    context->init_module(unit.get_unit_name());
}

llvm::Module& unit_llvm_ir_gen::get_module() {
    return _context->module();
}

void unit_llvm_ir_gen::dump() {
    _context->module().print(llvm::outs(), nullptr);
}

void unit_llvm_ir_gen::verify() {
    llvm::verifyModule(_context->module(), &llvm::outs());
}


void unit_llvm_ir_gen::optimize_functions() {
    // TODO switch to new pass manager
    std::shared_ptr<llvm::legacy::FunctionPassManager> passes;

    // Initialize Function pass manager
    passes = std::make_shared<llvm::legacy::FunctionPassManager>(&_context->module());
    // Do simple "peephole" optimizations and bit-twiddling options.
    passes->add(llvm::createInstructionCombiningPass());
    // Re-associate expressions.
    passes->add(llvm::createReassociatePass());
    // Eliminate Common SubExpressions.
    passes->add(llvm::createGVNPass());
    // Eliminate chains of dead computations.
    passes->add(llvm::createDeadCodeEliminationPass());
    // Simplify the control flow graph (deleting unreachable blocks, etc).
    passes->add(llvm::createCFGSimplificationPass());

    passes->doInitialization();

    for(auto& func : _context->module()) {
        passes->run(func);
    }
}

//
// LLVM JIT
//

unit_llvm_jit::unit_llvm_jit(std::shared_ptr<compiler> compiler) :
        _compiler(compiler),
        _lljit(llvm::cantFail(llvm::orc::LLJITBuilder().create(), "Cannot instantiate JIT stack")),
        _main_dynlib(_lljit->getMainJITDylib())
 {
    _main_dynlib.addGenerator(llvm::cantFail(llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(_lljit->getDataLayout().getGlobalPrefix())));
}

unit_llvm_jit::~unit_llvm_jit() {
    finalize_runtime();
}

std::unique_ptr<unit_llvm_jit> unit_llvm_jit::create(std::shared_ptr<compiler> compiler) {
    return std::unique_ptr<unit_llvm_jit>(new unit_llvm_jit(compiler));
}

void unit_llvm_jit::add_module(llvm::orc::ThreadSafeModule module) {
    if (_lljit->addIRModule(std::move(module))) {
        std::cerr << "Cannot register module in JIT instance." << std::endl;
    }
}

llvm::Expected<llvm::orc::ExecutorAddr> unit_llvm_jit::lookup_symbol_address(const std::string& name) {
    return _lljit->lookup(_main_dynlib, llvm::StringRef( (name.starts_with("_K") ? name : _compiler->get_element_mangled_name(name)) ));
}

void unit_llvm_jit::initialize_runtime() {
    switch (_state) {
        case DEFAULT:
            if(_lljit->initialize(_main_dynlib)) {
                std::cerr << "Error during JIT module initialization." << std::endl;
            }
            _state = INITIALIZED;
            break;
        case INITIALIZED:
            std::clog << "Initialize JIT module again is useless." << std::endl;
            break;
        case FINALIZED:
            std::cerr << "Cannot initialize JIT module after finalization." << std::endl;
            break;
    }
}

void unit_llvm_jit::finalize_runtime() {
    switch (_state) {
        case DEFAULT:
            std::cerr << "Cannot finalize JIT module before initialization." << std::endl;
            break;
        case INITIALIZED:
            if(_lljit->deinitialize(_main_dynlib)) {
                std::cerr << "Error during JIT module finalization." << std::endl;
            }
            _state = FINALIZED;
            break;
        case FINALIZED:
            std::clog << "Finalize JIT module again is useless." << std::endl;
            break;
    }
}

} // k::model::gen