/*
 * K Language compiler
 *
 * Copyright 2023-2026 Emilien Kia
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

#include "generators.hpp"

#include "debug_info.hpp"

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
// LLVM model declaration generator
//

declaration_generator::declaration_generator(k::log::logger& logger, k::compiler& compiler, std::shared_ptr<context> context, unit& unit):
k::log::logger_relay(logger),
_compiler(compiler),
_context(context),
_unit(unit)
{
    _builder = std::make_unique<llvm::IRBuilder<>>(**_context);
}

llvm::Module& declaration_generator::get_module() {
    return _context->module();
}

void declaration_generator::generate() {
    _unit.accept(*this);
}



//
// LLVM model implementation generator
//

implementation_generator::implementation_generator(k::log::logger& logger, k::compiler& compiler, std::shared_ptr<context> context, unit& unit):
k::log::logger_relay(logger),
_compiler(compiler),
_context(context),
_unit(unit)
{
    _builder = std::make_unique<llvm::IRBuilder<>>(**_context);
    _debug_info = std::make_unique<debug_info_emitter>(_compiler, _context);
    _debug_info->initialize(_unit.get_unit_name().to_string());
}

llvm::Module& implementation_generator::get_module() {
    return _context->module();
}

void implementation_generator::generate() {
    _unit.accept(*this);
}

void implementation_generator::finalize_debug_info() {
    if (_debug_info) {
        _debug_info->finalize();
    }
}

void implementation_generator::set_debug_location(const lex::opt_any_lexeme& lexeme) {
    if (_debug_info) {
        _debug_info->set_current_debug_location(*_builder, lexeme, _current_debug_scope);
    }
}

void implementation_generator::begin_function_debug_scope(function& function, llvm::Function* llvm_func) {
    if (!_debug_info) {
        return;
    }
    _current_debug_scope = _debug_info->attach_function_debug_scope(function, llvm_func);

    // Seed a location early so every emitted instruction has at least a function-level line.
    lex::opt_any_lexeme function_lexeme;
    if (auto ast_func = function.get_ast_function_decl()) {
        function_lexeme = lex::any_lexeme{ast_func->name};
    }
    set_debug_location(function_lexeme);
}

void implementation_generator::end_function_debug_scope() {
    _current_debug_scope = nullptr;
    _builder->SetCurrentDebugLocation(llvm::DebugLoc());
}

//
// LLVM JIT
//

jit::jit(std::shared_ptr<compiler> compiler) :
        _compiler(compiler),
        _lljit(llvm::cantFail(llvm::orc::LLJITBuilder().create(), "Cannot instantiate JIT stack")),
        _main_dynlib(_lljit->getMainJITDylib())
 {
    _main_dynlib.addGenerator(llvm::cantFail(llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(_lljit->getDataLayout().getGlobalPrefix())));
}

jit::~jit() {
    finalize_runtime();
}

std::unique_ptr<jit> jit::create(std::shared_ptr<compiler> compiler) {
    return std::unique_ptr<jit>(new jit(compiler));
}

void jit::add_module(llvm::orc::ThreadSafeModule module) {
    if (_lljit->addIRModule(std::move(module))) {
        std::cerr << "Cannot register module in JIT instance." << std::endl;
    }
}

llvm::Expected<llvm::orc::ExecutorAddr> jit::lookup_symbol_address(const std::string& name) {
    return _lljit->lookup(_main_dynlib, llvm::StringRef( (name.starts_with("_K") ? name : _compiler->get_element_mangled_name(name)) ));
}

llvm::Expected<llvm::orc::ExecutorAddr> jit::lookup_main_entry_symbol_address() {
    return _lljit->lookup(_main_dynlib, llvm::StringRef("main"));
}

void jit::initialize_runtime() {
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

void jit::finalize_runtime() {
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