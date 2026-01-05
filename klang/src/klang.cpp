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
#include <iostream>
#include <fstream>
#include <sstream>

#include <boost/program_options.hpp>

#include "config.h"

#include "common/logger.hpp"
#include "parse/parser.hpp"
#include "parse/ast_dump.hpp"
#include "model/model.hpp"
#include "model/model_builder.hpp"
#include "model/model_dump.hpp"
#include "gen/symbol_type_resolver.hpp"
#include "gen/unit_llvm_ir_gen.hpp"

#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"

namespace po = boost::program_options;
using namespace k;

k::log::logger logger;


static std::string read_text_file_content(const std::string& path) {
    std::ostringstream ss;
    std::ifstream input_file(path);
    if (!input_file.is_open()) {
        std::cerr << "Could not open the file '" << path << "'" << std::endl;
        exit(EXIT_FAILURE);
    }
    ss << input_file.rdbuf();
    return ss.str();
}


int main(int argc, const char** argv) {

    std::string output_file;
    std::vector<std::string> input_files;
    std::string target_triple;

    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();

    po::options_description cli_gobal_options("Global options");
    cli_gobal_options.add_options()
            ("help,h", "Display this information.")
            ("version,v", "Display version information.")
            ("output,o", po::value<std::string>(&output_file), "Place the output into <arg> file.")
            ("input-file", po::value<std::vector<std::string>>(&input_files), "input file")
            ;

    po::options_description cli_target_options("Target options");
    cli_target_options.add_options()
            ("print-effective-triple", "Print the effective target triple")
            ("print-target-triple", "Print the normalized target triple")
            ("print-targets", "Print the registered targets")
            ("target", po::value<std::string>(&target_triple), "Generate code for the given target")
            ;

    po::options_description cmdline_options;
    cmdline_options.add(cli_gobal_options).add(cli_target_options);

    po::positional_options_description p;
    p.add("input-file", -1);

    po::variables_map vm;
    auto parser = po::command_line_parser(argc, argv)
            .options(cmdline_options)
            .positional(p)
            .allow_unregistered()
            .run();
    po::store(parser, vm);
    po::notify(vm);

    std::vector<std::string> unrecognized = po::collect_unrecognized(parser.options, po::include_positional);
    if(!unrecognized.empty()) {
        std::cout << "Unrecognized option : " << unrecognized[0] << std::endl << std::endl;
    }

    if(vm.count("help") || !unrecognized.empty()) {
        std::cout << "Usage: klangc [options] input-file..." << std::endl;
        std::cout << cmdline_options << std::endl;
        return 1;
    }

    if(!vm.count("target")) {
        target_triple = llvm::sys::getDefaultTargetTriple();
    }

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


    if(vm.count("version")) {
        std::cout << "klangc - K lang compiler " << PROJECT_VER << std::endl;
        std::cout << "Target: " << target_machine->getTargetTriple().getTriple() << std::endl;
        return 2;
    }

    if(vm.count("print-targets")) {
        llvm::TargetRegistry::printRegisteredTargetsForVersion(llvm::outs());
        // TODO make it prettier.
        //for(auto target : llvm::TargetRegistry::targets()) {
        //    std::cout << target.getName() << " - " << target.getBackendName() << " : " << target.getShortDescription() << std::endl;
        //}
        return 3;
    }

    if(vm.count("print-target-triple") || vm.count("print-effective-triple")) {
        std::cout << "Target: " << target_machine->getTargetTriple().getTriple() << std::endl;
        return 4;
    }



    if(input_files.empty()) {
        std::cerr << "No input file." << std::endl;
        return -1;
    }

    if(input_files.empty() > 1) {
        std::cout << "klangc is supporting only one input file yet. Additional files will be ignored." << std::endl;
    }

    std::string source = read_text_file_content(input_files[0]);

    try {

        k::parse::parser parser(logger, source);
        std::shared_ptr<k::parse::ast::unit> ast_unit = parser.parse_unit();

        k::parse::dump::ast_dump_visitor visit(std::cout);
        std::cout << "#" << std::endl << "# Parsing" << std::endl << "#" << std::endl;
        visit.visit_unit(*ast_unit);

        k::model::dump::unit_dump unit_dump(std::cout);

        auto context = k::model::context::create();
        auto unit = k::model::unit::create(context);
        k::model::model_builder::visit(logger, context, *ast_unit, *unit);
        std::cout << "#" << std::endl << "# Unit construction" << std::endl << "#" << std::endl;
        unit_dump.dump(*unit);

        k::model::gen::symbol_type_resolver resolver(logger, context, *unit);
        resolver.resolve();
        std::cout << "#" << std::endl << "# Resolution" << std::endl << "#" << std::endl;
        unit_dump.dump(*unit);

        k::model::gen::unit_llvm_ir_gen gen(logger, context, *unit);
        gen.get_module().setDataLayout(target_machine->createDataLayout());
        gen.get_module().setTargetTriple(target_machine->getTargetTriple().getTriple());
        std::cout << "#" << std::endl << "# LLVM Module" << std::endl << "#" << std::endl;
        unit->accept(gen);
        gen.verify();
        gen.dump();

        std::cout << "#" << std::endl << "# LLVM Optimize Module" << std::endl << "#" << std::endl;
        gen.optimize_functions();
        gen.verify();
        gen.dump();



        std::error_code EC;
        llvm::raw_fd_ostream dest(output_file, EC, llvm::sys::fs::OF_None);
        if (EC) {
            llvm::errs() << "Could not open file: " << EC.message();
            return 1;
        }

        llvm::legacy::PassManager pass;
        auto FileType = llvm::CodeGenFileType::ObjectFile;

        if (target_machine->addPassesToEmitFile(pass, dest, nullptr, FileType)) {
            llvm::errs() << "TargetMachine can't emit a file of this type";
            return 1;
        }

        pass.run(gen.get_module());
        dest.flush();


    } catch(...) {
    }
    logger.print();

    return 0;
}
