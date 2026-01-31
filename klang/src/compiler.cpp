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
    _context(k::model::context::create()),
    _model_unit(k::model::unit::create(_context)),
    _target(target)
{
}

std::shared_ptr<compiler> compiler::create(llvm::TargetMachine* target) {
    return std::shared_ptr<compiler>{new compiler(target)};
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

void compiler::parse_source(const std::string_view& src, bool optimize, bool dump) {
    // TODO : what to do if _source, _ast_unit and so on are already filled (by previous call)
    _source = src;
    try {
        k::parse::parser parser(_log);
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
        k::model::model_builder::visit(_log, _context, *_ast_unit, *_model_unit);

        if(dump) {
            k::model::dump::unit_dump unit_dump(std::cout);
            unit_dump.dump(*_model_unit);
        }

        k::model::gen::symbol_resolver var_resolver(_log, _context, *_model_unit);
        if(dump) {
            std::cout << "#" << std::endl << "# Variable resolution" << std::endl << "#" << std::endl;
        }
        var_resolver.resolve();

        if(dump) {
            k::model::dump::unit_dump unit_dump(std::cout);
            unit_dump.dump(*_model_unit);
        }

        _context->resolve_types();

        k::model::gen::type_reference_resolver type_ref_resolver(_log, _context, *_model_unit);
        type_ref_resolver.resolve();

        if(dump) {
            k::model::dump::unit_dump unit_dump(std::cout);
            std::cout << "#" << std::endl << "# Type resolution" << std::endl << "#" << std::endl;
            unit_dump.dump(*_model_unit);
        }

        process_gen(optimize, dump);
    } catch (std::exception e) {
        std::cerr << "Exception : " << e.what() << std::endl;
    }
}

void compiler::process_gen(bool optimize, bool dump) {

    auto gen = std::make_unique<k::model::gen::unit_llvm_ir_gen>(_log, _context, *_model_unit);

    if (_target) {
        gen->get_module().setDataLayout(_target->createDataLayout());
        gen->get_module().setTargetTriple(_target->getTargetTriple().getTriple());
    }

    if(dump) {
        std::cout << "#" << std::endl << "# LLVM Module" << std::endl << "#" << std::endl;
    }
    _model_unit->accept(*gen);
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

std::unique_ptr<k::model::gen::unit_llvm_jit> compiler::to_jit(bool init_runtime) {
    if (!_gen) {
        process_gen();
    }
    if (_gen) {
        auto jit = model::gen::unit_llvm_jit::create(shared_from_this());
        if (!jit) {
            std::cerr << "Error instantiating jit engine." << std::endl;
            return nullptr;
        }
        jit->add_module(llvm::orc::ThreadSafeModule(std::move(_context->_module), _context->move_llvm_context()));
        _gen.reset();

        if (init_runtime) {
            jit->initialize_runtime();
        }

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