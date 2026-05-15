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

#include "config.h"
#include "common/path_lookup_file_resolver.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_set>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar/GVN.h>

#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/LegacyPassManager.h>

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/args.h>

#include "common/process.hpp"
#include "gen/resolvers.hpp"
#include "gen/generators.hpp"
#include "parse/ast_dump.hpp"
#include "model/model_builder.hpp"
#include "model/context.hpp"
#include "model/model_dump.hpp"
#include "model/tools/kdi_exporter.hpp"
#include "model/tools/kdi_importer.hpp"
#include "common/path_lookup_file_resolver.hpp"

#include <kdi.hpp>
#include "errors.hpp"
#include "common/target_init.hpp"

namespace k {

bool compiler::_compiler_class_init = false;

void compiler::initialize() {
    if (!_compiler_class_init) {
        k::initialize_llvm_targets();
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
        if (auto st_holder = std::dynamic_pointer_cast<model::aggregate_holder>(element)) {
            // ... and aggregates
            if (auto agg = st_holder->get_aggregate(front)) {
                res.push_back(std::dynamic_pointer_cast<model::element>(agg));
            }
        }
    } else {
        if (auto st_holder = std::dynamic_pointer_cast<model::aggregate_holder>(element)) {
            if (auto agg = st_holder->get_aggregate(front)) {
                // Recurse aggregates to find functions or static variables
                find_elements_from(rest, std::dynamic_pointer_cast<model::element>(agg), res);
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
    std::vector<std::pair<std::string, std::string>> sources;
    sources.emplace_back(std::string(path), std::string(src));
    parse_sources(std::move(sources), optimize, dump);
}

void compiler::parse_sources(std::vector<std::pair<std::string, std::string>> sources,
                              bool optimize, bool dump,
                              const std::string& forced_module_name) {
    // ── Phase 0 — Load all sources into _sources with a single reserve ─────
    trace("[compiler::parse_sources] Phase 0 — loading {} source file(s)", {std::to_string(sources.size())});
    assert(!_sources_locked && "Cannot add sources after lexing/parsing has started");
    _sources.clear();
    _sources.reserve(sources.size());
    for (auto& [path, content] : sources) {
        _sources.emplace_back(std::string_view(path), std::string_view(content));
        debug("[compiler::parse_sources] loaded source '{}' ({} bytes)", {path, std::to_string(content.size())});
    }
    _sources_locked = true;

    try {
        // ── Phase 1 — Pre-lookup: discover the module name ─────────────────
        trace("[compiler::parse_sources] Phase 1 — module name discovery");
        k::name resolved_unit_name;
        bool found_module_decl = false;
        size_t module_decl_file_idx = 0; // index of (first) file with module decl

        if (!forced_module_name.empty()) {
            // CLI --module-name overrides everything
            resolved_unit_name = k::name::from(forced_module_name);
            found_module_decl = true;
            debug("[compiler::parse_sources] forced module name: '{}'", {forced_module_name});
        } else {
            // Scan each source for a module declaration
            for (size_t i = 0; i < _sources.size(); ++i) {
                // We need a temporary copy of the source for lookup because
                // the real lexing pass will re-lex the source from scratch.
                k::source tmp_src(_sources[i].path, _sources[i].content);
                auto mod_name = k::parse::lookup_module_name(tmp_src, *this);
                if (mod_name) {
                    if (!found_module_decl) {
                        resolved_unit_name = *mod_name;
                        found_module_decl = true;
                        module_decl_file_idx = i;
                    } else if (!(*mod_name == resolved_unit_name)) {
                        // Conflicting module declarations
                        auto diag = k::log::diagnostic::make_error(
                            static_cast<unsigned int>(k::diag::compiler_diag::ERR_CONFLICTING_MODULE_DECL),
                            "Conflicting module declarations: '{}' vs '{}'",
                            {resolved_unit_name.to_string(), mod_name->to_string()});
                        report(diag);
                        _has_compilation_error = true;
                        throw k::log::compiler_error(std::move(diag));
                    }
                    // else same name — OK, ignore duplicate
                }
            }
            if (!found_module_decl) {
                // No module declaration in any file — warning, generate random name
                auto diag = k::log::diagnostic::make_warning(
                    static_cast<unsigned int>(k::diag::compiler_diag::WARN_NO_MODULE_DECL),
                    "No module declaration found in any source file; generating a random unit name");
                report(diag);
            }
        }

        if (found_module_decl) {
            debug("[compiler::parse_sources] resolved module name: '{}'", {resolved_unit_name.to_string()});
        }

        // ── Phase 2 — Full parse of each source, merge into single AST ─────
        trace("[compiler::parse_sources] Phase 2 — parsing and AST merge");
        // Parse files that declare the module first so the unit name / root
        // namespace is established before other files are processed.
        // Build a parsing order: file with module decl first (if any), then the rest.
        std::vector<size_t> parse_order;
        parse_order.reserve(_sources.size());
        if (found_module_decl && !forced_module_name.empty()) {
            // CLI override: no specific ordering needed
            for (size_t i = 0; i < _sources.size(); ++i)
                parse_order.push_back(i);
        } else if (found_module_decl) {
            parse_order.push_back(module_decl_file_idx);
            for (size_t i = 0; i < _sources.size(); ++i)
                if (i != module_decl_file_idx) parse_order.push_back(i);
        } else {
            for (size_t i = 0; i < _sources.size(); ++i)
                parse_order.push_back(i);
        }

        _ast_unit = std::make_shared<k::parse::ast::unit>();

        // Keep per-file AST units alive so that their lexeme string_views stay valid
        std::vector<std::shared_ptr<k::parse::ast::unit>> per_file_asts;
        per_file_asts.reserve(_sources.size());

        for (size_t idx : parse_order) {
            trace("[compiler::parse_sources] parsing source '{}'", {_sources[idx].path});
            k::parse::parser parser(*this);
            parser.parse(_sources[idx]);
            auto file_ast = parser.parse_unit();
            per_file_asts.push_back(file_ast);

            // Merge module_name
            if (file_ast->module_name && !_ast_unit->module_name) {
                _ast_unit->module_name = file_ast->module_name;
            }
            // Merge imports
            for (auto& imp : file_ast->imports) {
                _ast_unit->imports.push_back(imp);
            }
            // Merge declarations
            for (auto& decl : file_ast->declarations) {
                _ast_unit->declarations.push_back(decl);
            }
        }

        if(dump) {
            std::cout << "#" << std::endl << "# Parsing" << std::endl << "#" << std::endl;
            k::parse::dump::ast_dump_visitor visit(std::cout);
            visit.visit_unit(*_ast_unit);
        }

        // ── Phase 3 — Model building ───────────────────────────────────────
        trace("[compiler::parse_sources] Phase 3 — model building");
        if(dump) {
            std::cout << "#" << std::endl << "# Unit construction" << std::endl << "#" << std::endl;
        }

        // If we have a forced module name from the CLI, strip the module_name
        // from the merged AST so that model_builder::visit_module_name() does
        // not overwrite it, then pre-set the unit name.
        if (!forced_module_name.empty()) {
            _ast_unit->module_name.reset();
            _model_unit->set_unit_name(resolved_unit_name);
        }
        // Otherwise model_builder::visit_module_name will pick it up from the AST.

        k::model::model_builder::visit(*this, _context, *_ast_unit, *_model_unit);

        // ── Implicit import of base standard library (module "k") ───────
        // Every K module automatically imports "k" unless:
        //   - it IS "k" itself (bootstrap: stdlib cannot import itself)
        //   - the user already wrote "import k;" (add_import deduplicates)
        {
            const auto unit_name = _model_unit->get_unit_name().to_string();
            if (unit_name != "k") {
                _model_unit->add_import(k::name("k"));
                // Mark as implicit so it won't trigger unused-import warnings
                if (auto* imp = _model_unit->find_import(k::name("k"))) {
                    imp->implicit = true;
                }
            }
        }

        // ── Import resolution ──────────────────────────────────────────────
        trace("[compiler::parse_sources] import resolution");
        // Build a default resolver (current dir) if none was set by the caller.
        if (!_file_resolver) {
            auto r = std::make_shared<k::path_lookup_file_resolver>();
            r->add_search_dir(std::filesystem::current_path());
            _file_resolver = r;
        }
        k::model::kdi_importer importer(*_model_unit, *_file_resolver, *this,
                                        _enforce_ns_collision);
        importer.import_all();
        // Phase B: eagerly materialise all imported symbols (aggregates, functions,
        // variables) before any resolver pass runs.  This ensures that every
        // imported type is registered in the context before type resolution begins.
        // To switch to lazy loading in the future, simply remove this call.
        importer.materialise_all(_context);

        if(dump) {
            k::model::dump::unit_dump unit_dump(std::cout);
            unit_dump.dump(*_model_unit);
        }

        k::model::gen::symbol_resolver var_resolver(*this, _context, *_model_unit);
        trace("[compiler::parse_sources] symbol resolution");
        if(dump) {
            std::cout << "#" << std::endl << "# Variable resolution" << std::endl << "#" << std::endl;
        }
        var_resolver.resolve();

        if(dump) {
            k::model::dump::unit_dump unit_dump(std::cout);
            unit_dump.dump(*_model_unit);
        }

        _context->resolve_types();

        trace("[compiler::parse_sources] generic constraint validation");
        k::model::gen::generic_constraint_validator generic_validator(*this, _context, *_model_unit);
        generic_validator.validate();

        trace("[compiler::parse_sources] aggregate type resolution");
        k::model::gen::aggregate_type_resolver agg_type_resolver(*this, _context, *_model_unit);
        agg_type_resolver.resolve();

        // Re-resolve types for any template instantiations that were created
        // during aggregate type resolution (their LLVM struct types need to be built).
        _context->resolve_types();

        trace("[compiler::parse_sources] model materialization");
        k::model::gen::model_materializer materializer(*this, _context, *_model_unit);
        materializer.materialize();

        trace("[compiler::parse_sources] type reference resolution");
        k::model::gen::type_reference_resolver type_ref_resolver(*this, _context, *_model_unit);
        type_ref_resolver.resolve();

        if(dump) {
            k::model::dump::unit_dump unit_dump(std::cout);
            std::cout << "#" << std::endl << "# Type resolution" << std::endl << "#" << std::endl;
            unit_dump.dump(*_model_unit);
        }

        // ── Phase C — unused-import check ─────────────────────────────────
        trace("[compiler::parse_sources] unused-import check");
        // Must run after all resolver passes so that the 'used' flags are set.
        importer.check_unused_imports();

        process_generation(optimize, dump);
    } catch (k::model::context_resolution_error& e) {
        report(e.get_diagnostic());
        _has_compilation_error = true;
        throw;
    } catch (k::log::compiler_error&) {
        // Diagnostic already reported via logger_relay::report() before the throw.
        // Mark compilation as failed, then propagate to the caller.
        _has_compilation_error = true;
        throw;
    } catch (std::exception& e) {
        auto diag = k::log::diagnostic::make_fatal(
            static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F001),
            "Unexpected compiler exception: {0}",
            {e.what()}
        );
        report(diag);
        _has_compilation_error = true;
        throw k::log::compiler_error(std::move(diag));
    }
}

bool compiler::has_main_method() const {
    return get_unit()!=nullptr && get_unit()->has_main_method();
}

void compiler::process_generation(bool optimize, bool dump) {

    trace("[compiler::process_generation] initializing LLVM module");
    _context->init_module(_model_unit->get_unit_name());

    if (_target) {
        _context->module().setDataLayout(_target->createDataLayout());
        _context->module().setTargetTriple(_target->getTargetTriple().getTriple());
    }

    if(dump) {
        std::cout << "#" << std::endl << "# Generate declarations in LLVM module" << std::endl << "#" << std::endl;
    }
    trace("[compiler::process_generation] declaration generation");
    k::model::gen::declaration_generator gen_decl(*this, _context, *_model_unit);

    _model_unit->accept(gen_decl);

    if(dump) {
        std::cout << "#" << std::endl << "# Generate implementation in LLVM module" << std::endl << "#" << std::endl;
    }
    trace("[compiler::process_generation] implementation generation");
    k::model::gen::implementation_generator gen_impl(*this, _context, *_model_unit);

    _model_unit->accept(gen_impl);
    verify_gen_code();
    if(dump) {
        dump_gen_code();
    }
    if (_ir_output_options.emit_raw_ir) {
        emit_ir(_ir_output_options.raw_ir_file);
    }

    if (optimize) {
        trace("[compiler::process_generation] optimization");
        if(dump) {
            std::cout << "#" << std::endl << "# Optimize LLVM module" << std::endl << "#" << std::endl;
        }
        optimize_gen_code();
        verify_gen_code();
        if(dump) {
            dump_gen_code();
        }
        if (_ir_output_options.emit_opt_ir) {
            emit_ir(_ir_output_options.opt_ir_file);
        }
    }
}

void compiler::dump_gen_code() {
    _context->module().print(llvm::outs(), nullptr);
}

void compiler::set_ir_output_options(const IrOutputOptions& opts) {
    _ir_output_options = opts;
    // Providing a file path implicitly enables the corresponding flag
    if (!opts.raw_ir_file.empty()) {
        _ir_output_options.emit_raw_ir = true;
    }
    if (!opts.opt_ir_file.empty()) {
        _ir_output_options.emit_opt_ir = true;
    }
}

void compiler::set_file_resolver(std::shared_ptr<k::file_resolver> resolver) {
    _file_resolver = std::move(resolver);
}

void compiler::set_enforce_ns_collision(bool enforce) {
    _enforce_ns_collision = enforce;
}

void compiler::set_extra_object_files(std::vector<std::string> paths) {
    _extra_object_files = std::move(paths);
}

void compiler::set_log_level(log::diagnostic::severity level) {
    _log_level = level;
}

void compiler::set_log_file(const std::string& path) {
    if (path.empty()) {
        _log_stream = nullptr;
        _log_file_stream.reset();
    } else if (path == "stderr") {
        _log_stream = &std::cerr;
        _log_file_stream.reset();
    } else {
        _log_file_stream = std::make_unique<std::ofstream>(path, std::ios::out | std::ios::trunc);
        if (!_log_file_stream->is_open()) {
            std::cerr << "Warning: cannot open log file '" << path << "', falling back to stdout." << std::endl;
            _log_file_stream.reset();
            _log_stream = nullptr;
        } else {
            _log_stream = _log_file_stream.get();
        }
    }
}

void compiler::emit_ir(const std::string& filepath) {
    if (filepath.empty()) {
        _context->module().print(llvm::outs(), nullptr);
    } else {
        std::error_code ec;
        llvm::raw_fd_ostream os(filepath, ec, llvm::sys::fs::OF_Text);
        if (ec) {
            llvm::errs() << "Could not open IR output file '" << filepath << "': " << ec.message() << "\n";
            return;
        }
        _context->module().print(os, nullptr);
    }
}

void compiler::resolve_ir_filenames(const std::string& output_file) {
    if (output_file.empty()) {
        return;
    }
    std::filesystem::path base(output_file);
    // Remove any existing extension to build a clean stem
    std::filesystem::path stem = base.parent_path() / base.stem();

    if (_ir_output_options.emit_raw_ir && _ir_output_options.raw_ir_file.empty()) {
        _ir_output_options.raw_ir_file = stem.string() + ".raw.ll";
    }
    if (_ir_output_options.emit_opt_ir && _ir_output_options.opt_ir_file.empty()) {
        _ir_output_options.opt_ir_file = stem.string() + ".opt.ll";
    }
}

bool compiler::verify_gen_code() {
    std::string errors;
    llvm::raw_string_ostream err_stream(errors);
    bool has_errors = llvm::verifyModule(_context->module(), &err_stream);
    if (has_errors) {
        std::cerr << "LLVM module verification errors:\n" << errors << std::endl;
    }
    return !has_errors;
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

    if (_has_compilation_error) {
        return nullptr;
    }

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
    resolve_ir_filenames(output_file);
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


} // k
