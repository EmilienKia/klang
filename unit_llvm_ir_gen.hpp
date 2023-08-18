//
// Created by emilien on 01/06/23.
//

#ifndef KLANG_UNIT_LLVM_IR_GEN_HPP
#define KLANG_UNIT_LLVM_IR_GEN_HPP

#include <llvm/ADT/STLExtras.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/LegacyPassManager.h>


#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"



#include "unit.hpp"


namespace k::unit::gen {

class unit_llvm_jit;


class unit_llvm_ir_gen : public default_element_visitor {
protected:
    unit& _unit;

    std::unique_ptr<llvm::LLVMContext> _context;
    std::unique_ptr<llvm::IRBuilder<>> _builder;
    std::unique_ptr<llvm::Module> _module;

    llvm::Value* _value;

    std::map<std::shared_ptr<global_variable_definition>, llvm::GlobalVariable*> _global_vars;
    std::map<std::shared_ptr<function>, llvm::Function*> _functions;
    std::map<std::shared_ptr<parameter>, llvm::Argument*> _parameters;
    std::map<std::shared_ptr<parameter>, llvm::AllocaInst*> _parameter_variables;
    std::map<std::shared_ptr<variable_statement>, llvm::AllocaInst*> _variables;

public:
    unit_llvm_ir_gen(unit& unit);

    void visit_unit(unit &) override;

    void visit_namespace(ns &) override;
    void visit_function(function &) override;
    void visit_global_variable_definition(global_variable_definition &) override;

    void visit_block(block&) override;
    void visit_return_statement(return_statement&) override;
    void visit_expression_statement(expression_statement&) override;
    void visit_variable_statement(variable_statement&) override;

    void visit_value_expression(value_expression&) override;
    void visit_variable_expression(variable_expression&) override;

    std::pair<llvm::Value*,llvm::Value*> process_binary_expression(binary_expression&);
    void visit_addition_expression(addition_expression&) override;
    void visit_substraction_expression(substraction_expression&) override;
    void visit_multiplication_expression(multiplication_expression&) override;
    void visit_division_expression(division_expression&) override;
    void visit_modulo_expression(modulo_expression&) override;

    void create_assignement(std::shared_ptr<expression> expr, llvm::Value* value);
    void visit_assignation_expression(assignation_expression&) override;
    void visit_addition_assignation_expression(additition_assignation_expression&) override;
    void visit_substraction_assignation_expression(substraction_assignation_expression&) override;
    void visit_multiplication_assignation_expression(multiplication_assignation_expression&) override;
    void visit_division_assignation_expression(division_assignation_expression&) override;
    void visit_modulo_assignation_expression(modulo_assignation_expression&) override;

    void dump();
    void verify();
    void optimize_functions();

    llvm::Expected<std::unique_ptr<unit_llvm_jit>> to_jit();
};



class unit_llvm_jit {
private:
    std::unique_ptr<llvm::orc::ExecutionSession> _ES;
    llvm::DataLayout _DL;
    llvm::orc::MangleAndInterner _Mangle;
    llvm::orc::RTDyldObjectLinkingLayer _ObjectLayer;
    llvm::orc::IRCompileLayer _CompileLayer;
    llvm::orc::JITDylib &_MainJD;

public:

    unit_llvm_jit(unit_llvm_jit&&) = default;

    unit_llvm_jit(std::unique_ptr<llvm::orc::ExecutionSession> ES, llvm::orc::JITTargetMachineBuilder JTMB, llvm::DataLayout DL) :
            _ES(std::move(ES)),
            _DL(std::move(DL)),
            _Mangle(*this->_ES, this->_DL),
            _ObjectLayer(*this->_ES, []() { return std::make_unique<llvm::SectionMemoryManager>(); }),
            _CompileLayer(*this->_ES, _ObjectLayer, std::make_unique<llvm::orc::ConcurrentIRCompiler>(std::move(JTMB))),
            _MainJD(this->_ES->createBareJITDylib("<main>")) {
        _MainJD.addGenerator(llvm::cantFail(llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(DL.getGlobalPrefix())));
        if (JTMB.getTargetTriple().isOSBinFormatCOFF()) {
            _ObjectLayer.setOverrideObjectFlagsWithResponsibilityFlags(true);
            _ObjectLayer.setAutoClaimResponsibilityForObjectSymbols(true);
        }
    }

    ~unit_llvm_jit() {
        if (auto Err = _ES->endSession())
            _ES->reportError(std::move(Err));
    }

    static llvm::Expected<std::unique_ptr<unit_llvm_jit>> Create() {
        auto EPC = llvm::orc::SelfExecutorProcessControl::Create();
        if (!EPC)
            return EPC.takeError();

        auto ES = std::make_unique<llvm::orc::ExecutionSession>(std::move(*EPC));

        llvm::orc::JITTargetMachineBuilder JTMB(ES->getExecutorProcessControl().getTargetTriple());

        auto DL = JTMB.getDefaultDataLayoutForTarget();
        if (!DL)
            return DL.takeError();

        return std::make_unique<unit_llvm_jit>(std::move(ES), std::move(JTMB), std::move(*DL));
    }

    const llvm::DataLayout &getDataLayout() const { return _DL; }

    llvm::orc::JITDylib &getMainJITDylib() { return _MainJD; }

    llvm::Error addModule(llvm::orc::ThreadSafeModule TSM, llvm::orc::ResourceTrackerSP RT = nullptr) {
        if (!RT)
            RT = _MainJD.getDefaultResourceTracker();
        return _CompileLayer.add(RT, std::move(TSM));
    }

    llvm::Expected<llvm::JITEvaluatedSymbol> lookup(llvm::StringRef Name) {
        return _ES->lookup({&_MainJD}, _Mangle(Name.str()));
    }


    template<typename T>
    T lookup_symbol(llvm::StringRef Name) {
        return (T) _ES->lookup({&_MainJD}, _Mangle(Name.str()))->getAddress();
    }


};



} // k::unit::gen

#endif //KLANG_UNIT_LLVM_IR_GEN_HPP
