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
#include "model/model_dump.hpp"
#include "model/tools/kdi_exporter.hpp"
#include "model/tools/kdi_importer.hpp"
#include "common/path_lookup_file_resolver.hpp"

#include <kdi.hpp>

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
    assert(!_sources_locked && "Cannot add sources after lexing/parsing has started");
    _sources.clear();
    _sources.reserve(sources.size());
    for (auto& [path, content] : sources) {
        _sources.emplace_back(std::string_view(path), std::string_view(content));
    }
    _sources_locked = true;

    try {
        // ── Phase 1 — Pre-lookup: discover the module name ─────────────────
        k::name resolved_unit_name;
        bool found_module_decl = false;
        size_t module_decl_file_idx = 0; // index of (first) file with module decl

        if (!forced_module_name.empty()) {
            // CLI --module-name overrides everything
            resolved_unit_name = k::name::from(forced_module_name);
            found_module_decl = true;
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
                            0x0001,
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
                    0x0002,
                    "No module declaration found in any source file; generating a random unit name");
                report(diag);
            }
        }

        // ── Phase 2 — Full parse of each source, merge into single AST ─────
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
        if(dump) {
            std::cout << "#" << std::endl << "# Variable resolution" << std::endl << "#" << std::endl;
        }
        var_resolver.resolve();

        if(dump) {
            k::model::dump::unit_dump unit_dump(std::cout);
            unit_dump.dump(*_model_unit);
        }

        _context->resolve_types();

        k::model::gen::aggregate_type_resolver agg_type_resolver(*this, _context, *_model_unit);
        agg_type_resolver.resolve();

        k::model::gen::model_materializer materializer(*this, _context, *_model_unit);
        materializer.materialize();

        k::model::gen::type_reference_resolver type_ref_resolver(*this, _context, *_model_unit);
        type_ref_resolver.resolve();

        if(dump) {
            k::model::dump::unit_dump unit_dump(std::cout);
            std::cout << "#" << std::endl << "# Type resolution" << std::endl << "#" << std::endl;
            unit_dump.dump(*_model_unit);
        }

        // ── Phase C — unused-import check ─────────────────────────────────
        // Must run after all resolver passes so that the 'used' flags are set.
        importer.check_unused_imports();

        process_generation(optimize, dump);
    } catch (k::log::compiler_error&) {
        // Diagnostic already reported via logger_relay::report() before the throw.
        // Mark compilation as failed, then propagate to the caller.
        _has_compilation_error = true;
        throw;
    } catch (std::exception& e) {
        std::cerr << "Unexpected exception : " << e.what() << std::endl;
        _has_compilation_error = true;
        throw;
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
    if (_ir_output_options.emit_raw_ir) {
        emit_ir(_ir_output_options.raw_ir_file);
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

bool compiler::gen_executable(const std::string& output_file) {
    if (!has_main_method()) {
        std::cerr << "Cannot generate executable : no main function." << std::endl;
        return false;
    }

    std::filesystem::path output_path(
        output_file.empty() ? unit_name_to_lib_base(get_unit()->get_unit_name().to_string())
                            : output_file);
    resolve_ir_filenames(output_path.string());

    auto object_path = std::filesystem::temp_directory_path() / (output_path.filename().generic_string() + ".o");

    std::cout << "Generating object: " << object_path << std::endl;
    gen_object_file(object_path);

    std::cout << "Generating executable: " << output_path << std::endl;
    tools::exec_result exec_res;
    try {
        std::vector<std::string> clang_args = {"-pie", "-o", output_path.string(), object_path.string()};
        // Append -L/-l flags for used imports
        auto import_args = build_import_link_args();
        clang_args.insert(clang_args.end(), import_args.begin(), import_args.end());
        exec_res = tools::lookup_run_process("clang", clang_args);
    } catch (const tools::tool_not_found& e) {
        std::cerr << "Error: " << e.what() << " (needed to link executable)" << std::endl;
        std::filesystem::remove(object_path);
        return false;
    }

    std::filesystem::remove(object_path);

    if (!exec_res.out.empty()) std::cout << exec_res.out << std::endl;
    if (!exec_res.err.empty()) std::cerr << exec_res.err << std::endl;
    return exec_res.exit_code == 0;
}

// ---------------------------------------------------------------------------
// Library name utilities
// ---------------------------------------------------------------------------

std::string compiler::unit_name_to_lib_base(const std::string& unit_name) {
    std::string base = unit_name;
    size_t pos = 0;
    while ((pos = base.find("::", pos)) != std::string::npos) {
        base.replace(pos, 2, ".");
        pos += 1;
    }
    return base;
}

std::string compiler::get_lib_base_name() const {
    return unit_name_to_lib_base(get_unit()->get_unit_name().to_string());
}

// ---------------------------------------------------------------------------
// Import link args
// ---------------------------------------------------------------------------

std::vector<std::string> compiler::build_import_link_args() const {
    std::vector<std::string> args;

    if (!_model_unit) return args;

    // ── -L<dir> flags from the file resolver search paths ─────────────────
    if (auto plr = std::dynamic_pointer_cast<const path_lookup_file_resolver>(_file_resolver)) {
        for (const auto& dir : plr->get_lib_search_dirs()) {
            args.push_back("-L" + dir.string());
        }
    }

    // ── -l<base> for each used direct import ──────────────────────────────
    // Track added lib bases to avoid duplicates (direct + transitive may overlap)
    std::unordered_set<std::string> added_libs;

    // Always link the base standard library (libk) unless we ARE building it
    // or it was not resolved (e.g. during bootstrap / tests without stdlib).
    {
        const auto unit_name = _model_unit->get_unit_name().to_string();
        if (unit_name != "k") {
            auto* k_import = _model_unit->find_import(k::name("k"));
            if (k_import && k_import->kdi) {
                added_libs.insert("k");
                args.push_back("-lk");
            }
        }
    }

    for (const auto& imp : _model_unit->get_imports()) {
        if (!imp.used) continue;
        if (!imp.kdi)  continue;
        const std::string& lib_base = imp.kdi->header.lib_base;
        if (!lib_base.empty() && added_libs.insert(lib_base).second) {
            args.push_back("-l" + lib_base);
        }
    }

    // ── -l<base> for transitive dependencies of used imports ──────────────
    // When lib A depends on lib B, and an executable imports A, the linker
    // needs -lB as well because libA.so has a DT_NEEDED on libB.so and the
    // linker must resolve its undefined symbols at link time.
    for (const auto& tkdi : _model_unit->get_transitive_kdis()) {
        if (!tkdi) continue;
        const std::string& lib_base = tkdi->header.lib_base;
        if (!lib_base.empty() && added_libs.insert(lib_base).second) {
            args.push_back("-l" + lib_base);
        }
    }

    return args;
}

bool compiler::gen_kdi(const std::string& lib_path) {
    // --no-emit-kdi: silently skip KDI generation
    if (_ir_output_options.no_emit_kdi) return true;

    if (!_model_unit) {
        std::cerr << "gen_kdi: no compiled unit available." << std::endl;
        return false;
    }

    // Derive .kdi path: same stem as lib_path, extension = ".kdi"
    std::filesystem::path kdi_path(lib_path);
    kdi_path.replace_extension(".kdi");

    const std::string ver = "klangc-" + std::string(PROJECT_VER);

    kdi::kdi_file kdi_file = k::model::build_kdi(*_context, *_model_unit, lib_path, ver);

    if (!kdi::kdi_write_cbor_file(kdi_file, kdi_path.string())) {
        std::cerr << "gen_kdi: failed to write '" << kdi_path << "'" << std::endl;
        return false;
    }
    std::cout << "Generated KDI: " << kdi_path << std::endl;

    // Optionally also write the JSON equivalent (.kdi.json)
    if (_ir_output_options.emit_kdi_json) {
        std::string json_path = kdi_path.string() + ".json";
        if (!kdi::kdi_write_json_file(kdi_file, json_path)) {
            std::cerr << "gen_kdi: failed to write JSON '" << json_path << "'" << std::endl;
        } else {
            std::cout << "Generated KDI JSON: " << json_path << std::endl;
        }
    }
    return true;
}

bool compiler::gen_shared_library(const std::string& output_file) {
    std::filesystem::path output_path(
        output_file.empty() ? "lib" + get_lib_base_name() + ".so"
                            : output_file);
    resolve_ir_filenames(output_path.string());

    auto object_path = std::filesystem::temp_directory_path() / (output_path.filename().generic_string() + ".o");

    std::cout << "Generating object: " << object_path << std::endl;
    gen_object_file(object_path);

    std::cout << "Generating shared library: " << output_path << std::endl;
    tools::exec_result exec_res;
    try {
        std::vector<std::string> clang_args = {"-shared", "-fPIC", "-o", output_path.string(), object_path.string()};
        auto import_args = build_import_link_args();
        clang_args.insert(clang_args.end(), import_args.begin(), import_args.end());
        exec_res = tools::lookup_run_process("clang", clang_args);
    } catch (const tools::tool_not_found& e) {
        std::cerr << "Error: " << e.what() << " (needed to link shared library)" << std::endl;
        std::filesystem::remove(object_path);
        return false;
    }

    std::filesystem::remove(object_path);

    if (!exec_res.out.empty()) std::cout << exec_res.out << std::endl;
    if (!exec_res.err.empty()) std::cerr << exec_res.err << std::endl;
    if (exec_res.exit_code == 0) gen_kdi(output_path.string());
    return exec_res.exit_code == 0;
}

bool compiler::gen_static_library(const std::string& output_file) {
    std::filesystem::path output_path(
        output_file.empty() ? "lib" + get_lib_base_name() + ".a"
                            : output_file);
    resolve_ir_filenames(output_path.string());

    auto object_path = std::filesystem::temp_directory_path() / (output_path.filename().generic_string() + ".o");

    std::cout << "Generating object: " << object_path << std::endl;
    gen_object_file(object_path);

    std::cout << "Generating static library: " << output_path << std::endl;
    // ar rcs: create archive, add index, be silent on missing files
    tools::exec_result exec_res;
    try {
        exec_res = tools::lookup_run_process("ar", {"rcs", output_path.string(), object_path.string()});
    } catch (const tools::tool_not_found& e) {
        std::cerr << "Error: " << e.what() << " (needed to create static library)" << std::endl;
        std::filesystem::remove(object_path);
        return false;
    }

    std::filesystem::remove(object_path);

    if (!exec_res.out.empty()) std::cout << exec_res.out << std::endl;
    if (!exec_res.err.empty()) std::cerr << exec_res.err << std::endl;
    if (exec_res.exit_code == 0) gen_kdi(output_path.string());
    return exec_res.exit_code == 0;
}

bool compiler::gen_libraries(const std::string& shared_out, const std::string& static_out) {
    // Determine output paths up front
    const std::string base = get_lib_base_name();
    std::filesystem::path so_path(shared_out.empty() ? "lib" + base + ".so" : shared_out);
    std::filesystem::path  a_path(static_out.empty() ? "lib" + base + ".a"  : static_out);

    resolve_ir_filenames(so_path.string());

    // Generate the object file once
    auto object_path = std::filesystem::temp_directory_path() / ("lib" + base + ".o");
    std::cout << "Generating object: " << object_path << std::endl;
    if (!gen_object_file(object_path)) {
        return false;
    }

    bool ok = true;

    // Shared library
    std::cout << "Generating shared library: " << so_path << std::endl;
    tools::exec_result so_res;
    try {
        std::vector<std::string> clang_args = {"-shared", "-fPIC", "-o", so_path.string(), object_path.string()};
        auto import_args = build_import_link_args();
        clang_args.insert(clang_args.end(), import_args.begin(), import_args.end());
        so_res = tools::lookup_run_process("clang", clang_args);
    } catch (const tools::tool_not_found& e) {
        std::cerr << "Error: " << e.what() << " (needed to link shared library)" << std::endl;
        std::filesystem::remove(object_path);
        return false;
    }
    if (!so_res.out.empty()) std::cout << so_res.out << std::endl;
    if (!so_res.err.empty()) std::cerr << so_res.err << std::endl;
    ok &= (so_res.exit_code == 0);

    // Static library
    std::cout << "Generating static library: " << a_path << std::endl;
    tools::exec_result ar_res;
    try {
        ar_res = tools::lookup_run_process("ar", {"rcs", a_path.string(), object_path.string()});
    } catch (const tools::tool_not_found& e) {
        std::cerr << "Error: " << e.what() << " (needed to create static library)" << std::endl;
        std::filesystem::remove(object_path);
        return false;
    }
    if (!ar_res.out.empty()) std::cout << ar_res.out << std::endl;
    if (!ar_res.err.empty()) std::cerr << ar_res.err << std::endl;
    ok &= (ar_res.exit_code == 0);

    std::filesystem::remove(object_path);
    // Generate KDI once (keyed on the .so path; same content for both libs)
    if (ok) gen_kdi(so_path.string());
    return ok;
}

const source* compiler::source_for_position(const char* ptr) const {
    if (!ptr) return nullptr;
    for (const auto& src : _sources) {
        const char* begin = src.content.data();
        const char* end   = begin + src.content.size();
        if (ptr >= begin && ptr <= end) {
            return &src;
        }
    }
    return nullptr;
}

char_coord compiler::coordinates_from_pos(const k::char_pos& coord) const {
    if (auto* src = source_for_position(coord.pos)) {
        return src->get_coordinates(coord);
    }
    return char_coord::INVALID();
}

std::pair<char_coord,char_coord> compiler::coordinates_from_lex(const lex::lexeme& lex) const {
    if (lex.content.empty()) {
        return {char_coord::INVALID(), char_coord::INVALID()};
    }
    if (auto* src = source_for_position(&lex.content.front())) {
        return {src->get_coordinates({&lex.content.front()}), src->get_coordinates({&lex.content.back()})};
    }
    return {char_coord::INVALID(), char_coord::INVALID()};
}

static const char* severity_str[] = {
    "Info   ",
    "Warning",
    "Error  ",
    "Fatal  "
};

void compiler::report(const k::log::diagnostic& diag) {
    const unsigned int code = diag.code;
    const auto sev = (int)diag.level;
    const char* sev_str = severity_str[sev < 4 ? sev : 2];

    // Resolve source location from the primary lexeme (pos), then range (start/end).
    // The resolved source file and coordinates are returned together.
    struct located {
        const source* src = nullptr;
        char_coord c1 = char_coord::INVALID();
        char_coord c2 = char_coord::INVALID();
    };

    auto lex_to_located = [&](const k::lex::any_lexeme& lex) -> located {
        return std::visit([&](const auto& l) -> located {
            using T = std::decay_t<decltype(l)>;
            if constexpr (std::is_base_of_v<k::lex::lexeme, T>) {
                if (!l.content.empty()) {
                    auto* s = source_for_position(&l.content.front());
                    if (s) {
                        return { s,
                                 s->get_coordinates({&l.content.front()}),
                                 s->get_coordinates({&l.content.back()}) };
                    }
                }
            }
            return {};
        }, lex);
    };

    // Format message
    std::string formatted = diag.message;
    if (!diag.args.empty()) {
        fmt::dynamic_format_arg_store<fmt::format_context> store;
        for(const auto& arg : diag.args) store.push_back(arg);
        try { formatted = fmt::vformat(diag.message, store); } catch(...) {}
    }

    // Determine primary display coord and source file
    located primary;
    if (diag.pos) {
        primary = lex_to_located(*diag.pos);
    } else if (diag.start) {
        primary = lex_to_located(*diag.start);
    }

    const std::string& diag_path = primary.src ? primary.src->path : (_sources.empty() ? "" : _sources.front().path);

    // Print main message
    if (primary.c1) {
        fmt::print("{}:{}:{}: {} {:0>5X} : {}\n",
            diag_path, primary.c1.line, primary.c1.col,
            sev_str, code, formatted);
    } else {
        fmt::print("{}: {} {:0>5X} : {}\n",
            diag_path, sev_str, code, formatted);
    }

    // Print source excerpt
    auto log_excerpt = [&](const located& loc_start, const located& loc_end) {
        if (!loc_start.src) return;
        if (loc_start.c1 && loc_end.c2) {
            log_source_line(*loc_start.src, loc_start.c1, loc_end.c2);
        } else if (loc_start.c1) {
            log_source_line(*loc_start.src, loc_start.c1);
        }
    };

    if (diag.start && diag.end) {
        auto ls = lex_to_located(*diag.start);
        auto le = lex_to_located(*diag.end);
        log_excerpt(ls, le);
    } else if (diag.pos) {
        auto lp = lex_to_located(*diag.pos);
        if (lp.src && lp.c1 && lp.c2) log_source_line(*lp.src, lp.c1, lp.c2);
        else if (lp.src && lp.c1)      log_source_line(*lp.src, lp.c1);
    } else if (diag.start) {
        auto ls = lex_to_located(*diag.start);
        if (ls.src && ls.c1) log_source_line(*ls.src, ls.c1, ls.c2);
    }

    // Print notes
    for (const auto& note : diag.notes) {
        std::string note_msg = note.message;
        if (!note.args.empty()) {
            fmt::dynamic_format_arg_store<fmt::format_context> store;
            for(const auto& arg : note.args) store.push_back(arg);
            try { note_msg = fmt::vformat(note.message, store); } catch(...) {}
        }
        if (note.pos) {
            auto nl = lex_to_located(*note.pos);
            const std::string& note_path = nl.src ? nl.src->path : diag_path;
            if (nl.c1) {
                fmt::print("{}:{}:{}: note: {}\n", note_path, nl.c1.line, nl.c1.col, note_msg);
                if (nl.src) log_source_line(*nl.src, nl.c1);
            } else {
                fmt::print("{}: note: {}\n", note_path, note_msg);
            }
        } else {
            fmt::print("{}: note: {}\n", diag_path, note_msg);
        }
    }
}

void compiler::print_logs() {
    // Logs are printed in real-time by report(). Nothing to do here.
}


void compiler::log_source_line(const source& src, unsigned int line, unsigned int col) {
    auto txt = src.get_line(line);
    fmt::print("{:>5d} | {}", line, txt);
    fmt::print("      | {}^", std::string(col, ' ') );
    if (txt.empty() || (txt.back()!='\r' && txt.back()!='\n')) {
        fmt::print("\n");
    }
}

void compiler::log_source_line(const source& src, unsigned int line, unsigned int start, unsigned int end) {
    if (end<start) {
        log_source_line(src, line, end, start);
    } else {
        auto txt = src.get_line(line);
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

void compiler::log_source_lines(const source& src, unsigned int line_start, unsigned int start, unsigned int line_end, unsigned int end) {
    if (line_end<line_start) {
        log_source_lines(src, line_end, end, line_start, start);
    } else {
        auto line1 = src.get_line(line_start);
        fmt::print("{:>5d} | {}", line_start, line1);
        fmt::print("      | {}^{}", std::string(start, ' '), std::string(line1.size()-start-1, '~') );
        if (line_end > line_start + 1) {
            fmt::print("  ... |");
        }
        if (line1.empty() || (line1.back()!='\r' && line1.back()!='\n')) {
            fmt::print("\n");
        }
        auto line2 = src.get_line(line_end);
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

void compiler::log_source_line(const source& src, char_coord pos) {
    log_source_line(src, pos.line, pos.col);
}

void compiler::log_source_line(const source& src, char_coord start, char_coord end) {
    if (start.line==end.line) {
        log_source_line(src, start.line, start.col, end.col);
    } else {
        log_source_lines(src, start.line, start.col, end.line, end.col);
    }
}




} // k