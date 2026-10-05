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

#include <dlfcn.h>
#include <string>
#include <stdexcept>

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
    if (_application_jit_host_leave) {
        _application_jit_host_leave();
        _application_jit_host_leave = nullptr;
    }
}

std::unique_ptr<jit> jit::create(std::shared_ptr<compiler> compiler) {
    return std::unique_ptr<jit>(new jit(compiler));
}

void jit::add_module(llvm::orc::ThreadSafeModule module) {
    if (!_application_jit_host_leave && _compiler->has_main_method()) {
        bool requires_application_activation = false;
        module.withModuleDo([&](llvm::Module& mod) {
            requires_application_activation =
                mod.getFunction("__k_application_activate") != nullptr;
        });

        using host_enter_fn = int (*)();
        using host_leave_fn = void (*)();
        using activation_error_fn = void (*)(int);
        auto host_enter = reinterpret_cast<host_enter_fn>(
            dlsym(RTLD_DEFAULT, "__k_application_jit_host_enter"));
        auto host_leave = reinterpret_cast<host_leave_fn>(
            dlsym(RTLD_DEFAULT, "__k_application_jit_host_leave"));
        if (host_enter || host_leave) {
            if (!host_enter || !host_leave) {
                throw std::runtime_error(
                    "Cannot register JIT application host: the libk singleton host ABI is incomplete");
            }

            const int status = host_enter();
            if (status != 0) {
                auto activation_error = reinterpret_cast<activation_error_fn>(
                    dlsym(RTLD_DEFAULT, "__k_application_activation_error"));
                if (activation_error) activation_error(status);
                throw std::runtime_error(
                    "Cannot register JIT application host: libk rejected host activation with status " +
                    std::to_string(status));
            }
            _application_jit_host_leave = host_leave;
        } else if (requires_application_activation) {
            throw std::runtime_error(
                "Cannot register JIT application host: this module requires the libk singleton ABI");
        }
    }

    // Replace the default abort() uncaught-exception handler of the generated
    // main guard with the host callback, so that an uncaught exception does not
    // kill the host process.
    module.withModuleDo([](llvm::Module& mod) {
        auto* handler_gv = mod.getNamedGlobal(k_main_uncaught_handler_global);
        if (!handler_gv) return;
        auto& llvm_ctx = mod.getContext();
        auto* int_ptr_ty = mod.getDataLayout().getIntPtrType(llvm_ctx);
        auto* addr = llvm::ConstantInt::get(int_ptr_ty,
            reinterpret_cast<uintptr_t>(&jit::uncaught_exception_handler));
        handler_gv->setInitializer(
            llvm::ConstantExpr::getIntToPtr(addr, llvm::PointerType::get(llvm_ctx, 0)));
    });

    if (auto err = _lljit->addIRModule(std::move(module))) {
        std::cerr << "Cannot register module in JIT instance: " << llvm::toString(std::move(err)) << std::endl;
    }
}

thread_local jit::run_result* jit::_current_run = nullptr;

void jit::uncaught_exception_handler(void* /*exception*/) {
    if (_current_run) {
        _current_run->uncaught_exception = true;
    }
}

std::optional<jit::run_result> jit::run_main(int argc, char** argv) {
    auto main_fn = lookup_main_entry_symbol<int(*)(int, char**)>();
    if (!main_fn) {
        return std::nullopt;
    }

    run_result result;
    auto* previous_run = _current_run;
    _current_run = &result;
    result.status = main_fn(argc, argv);
    _current_run = previous_run;
    return result;
}

llvm::Expected<llvm::orc::ExecutorAddr> jit::lookup_symbol_address(const std::string& name) {
    return _lljit->lookup(_main_dynlib, llvm::StringRef( (name.starts_with("_K") ? name : _compiler->get_element_mangled_name(name)) ));
}

llvm::Expected<llvm::orc::ExecutorAddr> jit::lookup_main_entry_symbol_address() {
    // Without this check, the lookup would fall through to the host process own
    // 'main' symbol (the process symbol generator is registered on the main dylib).
    if (!_compiler->has_main_method()) {
        return llvm::make_error<llvm::StringError>("No main() entry point in the JIT-compiled program",
                                                   llvm::inconvertibleErrorCode());
    }
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
            {
                typedef void (*tls_exit_fn_t)();
                auto fn = (tls_exit_fn_t)dlsym(RTLD_DEFAULT, "__k_tls_thread_exit");
                if (fn) fn();
            }
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