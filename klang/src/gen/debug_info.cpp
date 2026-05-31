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

#include "debug_info.hpp"

#include <filesystem>

#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Module.h>

namespace k::model::gen {

void debug_info_emitter::initialize(const std::string& unit_name) {
    if (!is_enabled()) {
        return;
    }

    auto& mod = _context->module();
    _builder = std::make_unique<llvm::DIBuilder>(mod);

    const auto opts = _compiler.get_debug_info_options();

    mod.addModuleFlag(llvm::Module::Warning, "Debug Info Version", llvm::DEBUG_METADATA_VERSION);
    mod.addModuleFlag(llvm::Module::Warning, "Dwarf Version", opts.dwarf_version);

    // Use module identifier as fallback source file for generated/implicit code.
    std::string fallback_name = unit_name.empty() ? std::string("<module>") : unit_name + ".k";
    _fallback_file = _builder->createFile(fallback_name, ".");

    const auto emission_kind = opts.line_tables_only
        ? llvm::DICompileUnit::LineTablesOnly
        : llvm::DICompileUnit::FullDebug;

    _compile_unit = _builder->createCompileUnit(
        llvm::dwarf::DW_LANG_C_plus_plus,
        _fallback_file,
        "klangc",
        false,
        "",
        0,
        "",
        emission_kind);
}

void debug_info_emitter::finalize() {
    if (_builder) {
        _builder->finalize();
    }
}

llvm::DIFile* debug_info_emitter::get_or_create_file(const std::string& path) {
    if (!_builder) {
        return nullptr;
    }
    if (path.empty()) {
        return _fallback_file;
    }

    if (auto it = _files.find(path); it != _files.end()) {
        return it->second;
    }

    std::filesystem::path fs_path(path);
    std::string file_name = fs_path.filename().string();
    std::string dir_name = fs_path.parent_path().string();
    if (file_name.empty()) {
        file_name = path;
        dir_name = ".";
    } else if (dir_name.empty()) {
        dir_name = ".";
    }

    auto* file = _builder->createFile(file_name, dir_name);
    _files[path] = file;
    return file;
}

llvm::DIScope* debug_info_emitter::attach_function_debug_scope(function& fn, llvm::Function* llvm_fn) {
    if (!_builder || !_compile_unit || !llvm_fn) {
        return nullptr;
    }

    unsigned int line = 1;
    llvm::DIFile* file = _fallback_file;

    if (auto ast_fn = fn.get_ast_function_decl()) {
        if (auto loc = _compiler.get_source_location(lex::any_lexeme{ast_fn->name})) {
            line = loc->line;
            file = get_or_create_file(loc->file_path);
        }
    }

    auto* fn_type = _builder->createSubroutineType(_builder->getOrCreateTypeArray({}));
    auto* sp = _builder->createFunction(
        file ? static_cast<llvm::DIScope*>(file) : static_cast<llvm::DIScope*>(_compile_unit),
        fn.get_short_name(),
        fn.get_mangled_name(),
        file ? file : _fallback_file,
        line,
        fn_type,
        line,
        llvm::DINode::FlagPrototyped,
        llvm::DISubprogram::SPFlagDefinition);

    llvm_fn->setSubprogram(sp);
    return sp;
}

void debug_info_emitter::set_current_debug_location(llvm::IRBuilder<>& builder,
                                                    const lex::opt_any_lexeme& lexeme,
                                                    llvm::DIScope* scope) const {
    if (!_builder || !scope) {
        return;
    }

    unsigned int line = 1;
    unsigned int col = 1;

    if (lexeme.has_value()) {
        if (auto loc = _compiler.get_source_location(*lexeme)) {
            line = loc->line;
            col = loc->col;
        }
    }

    builder.SetCurrentDebugLocation(llvm::DILocation::get(_context->llvm_context(), line, col, scope));
}

} // namespace k::model::gen


