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

#include <filesystem>
#include <iostream>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar/GVN.h>

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/args.h>

#include "common/process.hpp"
#include "gen/resolvers.hpp"
#include "gen/generators.hpp"
#include "parse/ast_dump.hpp"
#include "model/model_builder.hpp"
#include "model/model_dump.hpp"

namespace k {

bool compiler::_compiler_class_init = false;

void compiler::initialize() {
    if (!_compiler_class_init) {
        llvm::InitializeAllTargetInfos();
        llvm::InitializeAllTargets();
        llvm::InitializeAllTargetMCs();
        llvm::InitializeAllAsmParsers();
        llvm::InitializeAllAsmPrinters();
        _compiler_class_init = true;
    }
}

compiler::compiler(llvm::TargetMachine* target):
    _context(k::model::context::create()),
    _model_unit(k::model::unit::create(_context)),
    _target(target)
{
}

std::shared_ptr<compiler> compiler::create(llvm::TargetMachine* target_machine) {
    initialize();

    if (target_machine == nullptr) {
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
        target_machine = target->createTargetMachine(
                target_triple, cpu,
                features,
                target_options,
                reloc_model);
    }
    return std::shared_ptr<compiler>{new compiler(target_machine)};
}

std::vector<std::shared_ptr<model::element>> compiler::find_elements(const name& name) const {
    std::vector<std::shared_ptr<model::element>> results;

    if (!_model_unit || name.empty()) {
        return results;
    }

    auto root_ns = _model_unit->get_root_namespace();
    if (!root_ns) {
        return results;
    }

    if (name.has_root_prefix()) {
        // Has root prefix : absolute search
        if(name.start_with(root_ns->get_name())) {
            // Explicitly look at the ns content
            auto search_name = name.without_front(root_ns->get_name().size()).without_root_prefix();
            if (!search_name.empty()) { // Cannot retrieve namespaces
               find_elements_from(search_name, root_ns,results);
            }
        }
        // TODO Look at imported modules
    } else {
        // No root prefix : relative search
        // 1. Look at members of root namespace
        find_elements_from(name, root_ns, results);
        // 2. Look at the root namespace with explicit path
        if(name.start_with(root_ns->get_name())) {
            // TODO look at the intermediate ns names of the root ns
            // Explicitly look at the ns content
            auto search_name = name.without_front(root_ns->get_name().size());
            if (!search_name.empty()) { // Cannot retrieve namespaces
                find_elements_from(search_name, root_ns,results);
            }
        }
        // 3. Look at imported modules
        // TODO Look at imported modules
    }
    return results;
}

void compiler::find_elements_from(const name& name, const std::shared_ptr<model::element>& element, std::vector<std::shared_ptr<model::element>>& res) const {
    auto [front, rest] = name.pop_front();
    bool is_leaf = rest.empty();

    if (is_leaf) {
        if (auto var_holder = std::dynamic_pointer_cast<model::variable_holder>(element)) {
            // Only resolve global or static variables ...
            if (auto var = std::dynamic_pointer_cast<model::global_variable_definition>(var_holder->get_variable(front))) {
                res.push_back(std::dynamic_pointer_cast<model::element>(var));
            }
        }
        if (auto fn_holder = std::dynamic_pointer_cast<model::function_holder>(element)) {
            // ... and functions
            if (auto fn = fn_holder->get_function(front)) {
                res.push_back(std::dynamic_pointer_cast<model::element>(fn));
            }
        }
        if (auto st_holder = std::dynamic_pointer_cast<model::structure_holder>(element)) {
            // ... and structures
            if (auto st = st_holder->get_structure(front)) {
                res.push_back(std::dynamic_pointer_cast<model::element>(st));
            }
        }
    } else {
        if (auto st_holder = std::dynamic_pointer_cast<model::structure_holder>(element)) {
            if (auto st = st_holder->get_structure(front)) {
                // Recurse structures to find functions or static variables
                find_elements_from(rest, std::dynamic_pointer_cast<model::element>(st), res);
            }
        }
        if (auto ns = std::dynamic_pointer_cast<model::ns>(element)) {
            if (auto subns = ns->get_child_namespace(front)) {
                // Recurse sub namespaces
                find_elements_from(rest, std::dynamic_pointer_cast<model::element>(subns), res);
            }
        }
    }
}

std::string compiler::get_element_mangled_name(const name& name) const {
    std::vector<std::shared_ptr<k::model::named_element>> filtered;
    for (const auto& elem : find_elements(name)) {
        if (std::dynamic_pointer_cast<k::model::global_variable_definition>(elem) || std::dynamic_pointer_cast<k::model::function>(elem)) {
            filtered.emplace_back(std::dynamic_pointer_cast<k::model::named_element>(elem));
        }
    }
    if (filtered.empty()) {
        throw std::runtime_error("No matching element found");
    } else if (filtered.size() > 1) {
        throw std::runtime_error("Too many elements found");
    } else {
        return filtered.front()->get_mangled_name();
    }
}

void compiler::parse_source(const std::string_view& path, const std::string_view& src, bool optimize, bool dump) {
    // TODO : what to do if _source, _ast_unit and so on are already filled (by previous call)
    _source.path = path;
    _source.content = src;
    try {
        k::parse::parser parser(*this);
        parser.parse(_source);
        _ast_unit = parser.parse_unit();

        if(dump) {
            std::cout << "#" << std::endl << "# Parsing" << std::endl << "#" << std::endl;
            k::parse::dump::ast_dump_visitor visit(std::cout);
            visit.visit_unit(*_ast_unit);
        }

        if(dump) {
            std::cout << "#" << std::endl << "# Unit construction" << std::endl << "#" << std::endl;
        }
        k::model::model_builder::visit(*this, _context, *_ast_unit, *_model_unit);

        if(dump) {
            k::model::dump::unit_dump unit_dump(std::cout);
            unit_dump.dump(*_model_unit);
        }

        k::model::gen::symbol_resolver var_resolver(*this, _context, *_model_unit);
        if(dump) {
            std::cout << "#" << std::endl << "# Variable resolution" << std::endl << "#" << std::endl;
        }
        var_resolver.resolve();

        if(dump) {
            k::model::dump::unit_dump unit_dump(std::cout);
            unit_dump.dump(*_model_unit);
        }

        _context->resolve_types();

        k::model::gen::type_reference_resolver type_ref_resolver(*this, _context, *_model_unit);
        type_ref_resolver.resolve();

        if(dump) {
            k::model::dump::unit_dump unit_dump(std::cout);
            std::cout << "#" << std::endl << "# Type resolution" << std::endl << "#" << std::endl;
            unit_dump.dump(*_model_unit);
        }

        process_generation(optimize, dump);
    } catch (std::exception e) {
        std::cerr << "Exception : " << e.what() << std::endl;
    }
}

bool compiler::has_main_method() const {
    return get_unit()!=nullptr && get_unit()->has_main_method();
}

void compiler::process_generation(bool optimize, bool dump) {

    _context->init_module(_model_unit->get_unit_name());

    if (_target) {
        _context->module().setDataLayout(_target->createDataLayout());
        _context->module().setTargetTriple(_target->getTargetTriple().getTriple());
    }

    if(dump) {
        std::cout << "#" << std::endl << "# Generate declarations in LLVM module" << std::endl << "#" << std::endl;
    }
    k::model::gen::declaration_generator gen_decl(*this, _context, *_model_unit);

    _model_unit->accept(gen_decl);

    if(dump) {
        std::cout << "#" << std::endl << "# Generate implementation in LLVM module" << std::endl << "#" << std::endl;
    }
    k::model::gen::implementation_generator gen_impl(*this, _context, *_model_unit);

    _model_unit->accept(gen_impl);
    verify_gen_code();
    if(dump) {
        dump_gen_code();
    }

    if (optimize) {
        if(dump) {
            std::cout << "#" << std::endl << "# Optimize LLVM module" << std::endl << "#" << std::endl;
        }
        optimize_gen_code();
        verify_gen_code();
        if(dump) {
            dump_gen_code();
        }
    }
}

void compiler::dump_gen_code() {
    _context->module().print(llvm::outs(), nullptr);
}

bool compiler::verify_gen_code() {
    // TODO Better log check errors
    return !llvm::verifyModule(_context->module(), &llvm::outs());
}

void compiler::optimize_gen_code() {
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

std::unique_ptr<k::model::gen::jit> compiler::to_jit(bool init_runtime) {

    auto jit = model::gen::jit::create(shared_from_this());
    if (!jit) {
        std::cerr << "Error instantiating jit engine." << std::endl;
        return nullptr;
    }
    std::unique_ptr<llvm::Module> module = std::move(_context->_module);
    std::unique_ptr<llvm::LLVMContext> context = _context->move_llvm_context();
    jit->add_module(llvm::orc::ThreadSafeModule(std::move(module), std::move(context)));

    if (init_runtime) {
        jit->initialize_runtime();
    }

    return jit;
}

bool compiler::gen_object_file(const std::string& output_file) {
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

    pass.run(_context->module());
    dest.flush();
    return true;

}

bool compiler::gen_executable(const std::string& output_file) {
    if (!has_main_method()) {
        std::cerr << "Cannot generate executable : no main function." << std::endl;
        return false;
    }

    std::filesystem::path output_path(output_file.empty() ? get_unit()->get_unit_name().to_string() : output_file);

    auto object_path = std::filesystem::temp_directory_path() / (output_path.filename().generic_string() + ".o");

    std::cout << "Generating object: " << object_path << std::endl;

    gen_object_file(object_path);

    std::cout << "Generating executable: " << output_path << std::endl;

    auto exec_res = tools::lookup_run_process("clang", {"-pie", "-o", output_path, object_path});

    std::filesystem::remove(object_path);

    if (!exec_res.out.empty()) {
        std::cout << exec_res.out << std::endl;
    }
    if (!exec_res.err.empty()) {
        std::cerr << exec_res.err << std::endl;
    }
    return exec_res.exit_code == 0;
}

char_coord compiler::coordinates_from_pos(const k::char_pos& coord) const {
    return _source.get_coordinates(coord);
}

std::pair<char_coord,char_coord> compiler::coordinates_from_lex(const lex::lexeme& lex) const {
    if (lex.content.empty()) {
        return {char_coord::INVALID(), char_coord::INVALID()};
    } else {
        return {_source.get_coordinates({&lex.content.front()}), _source.get_coordinates({&lex.content.back()})};
    }
}

static const char* criticality_str[] = {
    "Info   ",
    "Warning",
    "Error  "
};

void compiler::log_message(k::log::log_entry::CRITICALITY criticality, unsigned int code, const std::string_view& message, const std::vector<std::string>& args) {
    static constexpr auto FORMAT = "{}: {} {:0>5X} : {}\n";
    if(args.size()>0) {
        fmt::dynamic_format_arg_store<fmt::format_context> store;
        for(const auto& arg : args) {
            store.push_back(arg);
        }
        std::string msg = fmt::vformat(message, store);
        fmt::print(FORMAT, _source.path, criticality_str[criticality], code,  msg);
    } else {
        fmt::print(FORMAT, _source.path, criticality_str[criticality], code, message);
    }
}

void compiler::log_message(k::log::log_entry::CRITICALITY criticality, unsigned int code, char_coord pos, const std::string_view& message, const std::vector<std::string>& args) {
    if (pos) {
        static constexpr auto FORMAT = "{}:{}:{}: {} {:0>5X} : {}\n";
        if(args.size()>0) {
            fmt::dynamic_format_arg_store<fmt::format_context> store;
            for(const auto& arg : args) {
                store.push_back(arg);
            }
            std::string msg = fmt::vformat(message, store);
            fmt::print(FORMAT, _source.path, pos.line, pos.col, criticality_str[criticality], code,  msg);
        } else {
            fmt::print(FORMAT, _source.path, pos.line, pos.col, criticality_str[criticality], code, message);
        }
    } else {
        log_message(criticality, code, pos, message, args);
    }
}

void compiler::log_message(k::log::log_entry::CRITICALITY criticality, const std::string_view& message, const std::vector<std::string>& args) {
    static constexpr auto FORMAT = "{}: {} : {}\n";
    if(args.size()>0) {
        fmt::dynamic_format_arg_store<fmt::format_context> store;
        for(const auto& arg : args) {
            store.push_back(arg);
        }
        std::string msg = fmt::vformat(message, store);
        fmt::print(FORMAT, _source.path, criticality_str[criticality],  msg);
    } else {
        fmt::print(FORMAT, _source.path, criticality_str[criticality], message);
    }
}

void compiler::log_message(k::log::log_entry::CRITICALITY criticality, char_coord pos, const std::string_view& message, const std::vector<std::string>& args) {
    if (pos) {
        static constexpr auto FORMAT = "{}:{}:{}: {} : {}\n";
        if(args.size()>0) {
            fmt::dynamic_format_arg_store<fmt::format_context> store;
            for(const auto& arg : args) {
                store.push_back(arg);
            }
            std::string msg = fmt::vformat(message, store);
            fmt::print(FORMAT, _source.path, pos.line, pos.col, criticality_str[criticality], msg);
        } else {
            fmt::print(FORMAT, _source.path, pos.line, pos.col, criticality_str[criticality], message);
        }
    } else {
        log_message(criticality, pos, message, args);
    }
}

void compiler::log_source_line(unsigned int line, unsigned int col) {
    auto txt = _source.get_line(line);
    fmt::print("{:>5d} | {}", line, txt);
    fmt::print("      | {}^", std::string(col, ' ') );
    if (txt.empty() || (txt.back()!='\r' && txt.back()!='\n')) {
        fmt::print("\n");
    }
}

void compiler::log_source_line(unsigned int line, unsigned int start, unsigned int end) {
    if (end<start) {
        log_source_line(line, end, start);
    } else {
        auto txt = _source.get_line(line);
        fmt::print("{:>5d} | {}", line, txt);
        if (start == end) {
            fmt::print("      | {}^", std::string(start, ' ') );
        } else {
            fmt::print("      | {}^{}", std::string(start, ' '), std::string(end-start-1, '~') );
        }
        if (txt.empty() || (txt.back()!='\r' && txt.back()!='\n')) {
            fmt::print("\n");
        }
    }
}

void compiler::log_source_lines(unsigned int line_start, unsigned int start, unsigned int line_end, unsigned int end) {
    if (line_end<line_start) {
        log_source_lines(line_end, end, line_start, start);
    } else {
        auto line1 = _source.get_line(line_start);
        fmt::print("{:>5d} | {}", line_start, line1);
        fmt::print("      | {}^{}", std::string(start, ' '), std::string(line1.size()-start-1, '~') );
        if (line_end > line_start + 1) {
            fmt::print("  ... |");
        }
        if (line1.empty() || (line1.back()!='\r' && line1.back()!='\n')) {
            fmt::print("\n");
        }
        auto line2 = _source.get_line(line_end);
        fmt::print("{:>5d} | {}", line_end, line2);
        if (end==0) {
            fmt::print("      | ^");
        } else {
            fmt::print("      | {}^", std::string(end-1, '~') );
        }
        if (line2.empty() || (line2.back()!='\r' && line2.back()!='\n')) {
            fmt::print("\n");
        }
    }
}

void compiler::log_source_line(char_coord pos) {
    log_source_line(pos.line, pos.col);
}

void compiler::log_source_line(char_coord start, char_coord end) {
    if (start.line==end.line) {
        log_source_line(start.line, start.col, end.col);
    } else {
        log_source_lines(start.line, start.col, end.line, end.col);
    }
}




void compiler::do_log(k::log::log_entry::CRITICALITY criticality, unsigned int code, const std::string_view& message, const std::vector<std::string>& args) {
    log_message(criticality, code, message, {});
}

void compiler::do_log(k::log::log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& pos, const std::string_view& message, const std::vector<std::string>& args) {
    char_coord coord = coordinates_from_pos(pos);
    log_message(criticality, code, coord, message, args);
    log_source_line(coord);
}

void compiler::do_log(k::log::log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& start, const k::char_pos& end, const std::string_view& message, const std::vector<std::string>& args) {
    char_coord coord1 = coordinates_from_pos(start);
    char_coord coord2 = coordinates_from_pos(end);
    log_message(criticality, code, coord1, message, args);
    log_source_line(coord1, coord2);
}

void compiler::do_log(k::log::log_entry::CRITICALITY criticality, unsigned int code, const k::char_pos& start, const k::char_pos& end, const k::char_pos& pos, const std::string_view& message, const std::vector<std::string>& args) {
    char_coord coord1 = coordinates_from_pos(start);
    char_coord coord2 = coordinates_from_pos(end);
    char_coord coord3 = coordinates_from_pos(pos);
    log_message(criticality, code, coord3, message, args);
    log_source_line(coord1, coord2);
}


void compiler::do_log(k::log::log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& pos, const std::string_view& message, const std::vector<std::string>& args) {
    if (!pos.content.empty()) {
        do_log(criticality, code, char_pos{&pos.content.front()}, char_pos{&pos.content.back()}, message, args);
    } else {
        do_log(criticality, code, message, args);
    }
}

void compiler::do_log(k::log::log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const std::string_view& message, const std::vector<std::string>& args) {
    if (!start.content.empty() && !end.content.empty()) {
        do_log(criticality, code, char_pos{&start.content.front()}, char_pos{&end.content.back()}, message, args);
    } else if (!start.content.empty() /* && end.content.empty()*/) {
        do_log(criticality, code, char_pos{&start.content.front()}, char_pos{&start.content.back()}, message, args);
    } else if (/*start.content.empty() && */ !end.content.empty()) {
        do_log(criticality, code, char_pos{&end.content.front()}, char_pos{&start.content.back()}, message, args);
    } else {
        do_log(criticality, code, message, args);
    }
}

void compiler::do_log(k::log::log_entry::CRITICALITY criticality, unsigned int code, const k::lex::lexeme& start, const k::lex::lexeme& end, const k::lex::lexeme& pos, const std::string_view& message, const std::vector<std::string>& args) {
    if (!pos.content.empty()) {
        if (!start.content.empty() && !end.content.empty()) {
            do_log(criticality, code, char_pos{&start.content.front()}, char_pos{&end.content.back()}, char_pos{&pos.content.front()}, message, args);
        } else if (!start.content.empty() /* && end.content.empty()*/) {
            do_log(criticality, code, char_pos{&start.content.front()}, char_pos{&start.content.back()}, char_pos{&pos.content.front()}, message, args);
        } else if (/*start.content.empty() && */ !end.content.empty()) {
            do_log(criticality, code, char_pos{&end.content.front()}, char_pos{&start.content.back()}, char_pos{&pos.content.front()}, message, args);
        } else {
            do_log(criticality, code, char_pos{&pos.content.front()}, message, args);
        }
    } else {
        do_log(criticality, code, start, end, message, args);
    }
}

void compiler::print_logs() {
    // In the new architecture, logs are printed in real-time by do_log().
    // Nothing to do here.
}


} // k