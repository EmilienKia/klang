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

unit_llvm_jit::unit_llvm_jit(std::shared_ptr<compiler> compiler, std::unique_ptr<llvm::orc::ExecutionSession> session, llvm::orc::JITTargetMachineBuilder jtmb, llvm::DataLayout layout) :
        _compiler(compiler),
        _session(std::move(session)),
        _layout(std::move(layout)),
        _mangle(*this->_session, this->_layout),
        _object_layer(*this->_session, []() { return std::make_unique<llvm::SectionMemoryManager>(); }),
        _compile_layer(*this->_session, _object_layer, std::make_unique<llvm::orc::ConcurrentIRCompiler>(std::move(jtmb))),
        _main_dynlib(this->_session->createBareJITDylib("<main>")) {
    _main_dynlib.addGenerator(llvm::cantFail(llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(layout.getGlobalPrefix())));
    if (jtmb.getTargetTriple().isOSBinFormatCOFF()) {
        _object_layer.setOverrideObjectFlagsWithResponsibilityFlags(true);
        _object_layer.setAutoClaimResponsibilityForObjectSymbols(true);
    }
}

unit_llvm_jit::~unit_llvm_jit() {
    if (auto Err = _session->endSession())
        _session->reportError(std::move(Err));
}

std::unique_ptr<unit_llvm_jit> unit_llvm_jit::create(std::shared_ptr<compiler> compiler) {
    auto epc = llvm::orc::SelfExecutorProcessControl::Create();
    if (!epc) {
        // TODO throw an exception.
        std::cerr << "Failed to instantiate ORC SelfExecutorProcessControl" << std::endl;
        return nullptr;
    }

    auto session = std::make_unique<llvm::orc::ExecutionSession>(std::move(*epc));

    llvm::orc::JITTargetMachineBuilder jtmb(session->getExecutorProcessControl().getTargetTriple());

    auto layout = jtmb.getDefaultDataLayoutForTarget();
    if (!layout) {
        // TODO throw an exception.
        std::cerr << "Failed to retrieve default data layout for current target." << std::endl;
        return nullptr;
    }

    return std::unique_ptr<unit_llvm_jit>(new unit_llvm_jit(compiler, std::move(session), std::move(jtmb), std::move(*layout)));
}

void unit_llvm_jit::add_module(llvm::orc::ThreadSafeModule module, llvm::orc::ResourceTrackerSP res_tracker) {
    if (!res_tracker)
        res_tracker = _main_dynlib.getDefaultResourceTracker();
    if(llvm::Error err = _compile_layer.add(res_tracker, std::move(module))) {
        std::cerr << "Failed to register module to jit." << std::endl;
    }
}

llvm::Expected<llvm::orc::ExecutorSymbolDef> unit_llvm_jit::lookup_symbol_address(const std::string& name) {
    return _session->lookup(
                llvm::ArrayRef<llvm::orc::JITDylib*>{&_main_dynlib},
                _mangle(llvm::StringRef( (name.starts_with("_K") ? name : _compiler->get_element_mangled_name(name)) ))
            );
}

} // k::model::gen