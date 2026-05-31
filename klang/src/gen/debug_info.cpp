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

#include "../model/statements.hpp"

#include <filesystem>
#include <vector>

#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Module.h>

namespace k::model::gen {

namespace {

struct resolved_debug_location {
    llvm::DIFile* file = nullptr;
    unsigned int line = 1;
    unsigned int col = 1;
};

resolved_debug_location get_debug_location(debug_info_emitter& emitter,
                                           k::compiler& compiler,
                                           const lex::opt_any_lexeme& lexeme,
                                           llvm::DIFile* fallback_file) {
    resolved_debug_location result{};
    result.file = fallback_file;

    if (lexeme.has_value()) {
        if (auto loc = compiler.get_source_location(*lexeme)) {
            result.file = emitter.get_or_create_file(loc->file_path);
            result.line = loc->line;
            result.col = loc->col;
        }
    }

    if (!result.file) {
        result.file = fallback_file;
    }

    return result;
}

unsigned long long get_type_size_bits(const llvm::DataLayout& data_layout, llvm::Type* llvm_type) {
    if (!llvm_type || !llvm_type->isSized()) {
        return 0;
    }
    return data_layout.getTypeSizeInBits(llvm_type);
}

unsigned int get_type_align_bits(const llvm::DataLayout& data_layout, llvm::Type* llvm_type) {
    if (!llvm_type || !llvm_type->isSized()) {
        return 0;
    }
    return static_cast<unsigned int>(data_layout.getABITypeAlign(llvm_type).value() * 8U);
}

lex::opt_any_lexeme get_parameter_lexeme(parameter& param) {
    if (auto ast_param = param.get_ast_parameter_spec(); ast_param && ast_param->name) {
        return lex::any_lexeme{*ast_param->name};
    }
    return std::nullopt;
}

lex::opt_any_lexeme get_variable_lexeme(variable_statement& var) {
    if (auto ast_var = var.get_ast_variable_decl()) {
        return lex::any_lexeme{ast_var->name};
    }
    return std::nullopt;
}

} // namespace

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

llvm::DIType* debug_info_emitter::get_or_create_type(const std::shared_ptr<type>& type_info) {
    if (!_builder) {
        return nullptr;
    }

    if (!type_info) {
        return nullptr;
    }

    if (auto it = _types.find(type_info.get()); it != _types.end()) {
        return it->second;
    }

    auto& module = _context->module();
    const auto& data_layout = module.getDataLayout();
    llvm::Type* llvm_type = _context->get_llvm_type(type_info);

    llvm::DIType* result = nullptr;

    if (auto const_type_info = std::dynamic_pointer_cast<const_type>(type_info)) {
        auto* inner = get_or_create_type(const_type_info->get_inner_type());
        result = _builder->createQualifiedType(llvm::dwarf::DW_TAG_const_type, inner);
    } else if (auto primitive = std::dynamic_pointer_cast<primitive_type>(type_info)) {
        unsigned int encoding = llvm::dwarf::DW_ATE_signed;
        if (primitive->is_boolean()) {
            encoding = llvm::dwarf::DW_ATE_boolean;
        } else if (primitive->is_float()) {
            encoding = llvm::dwarf::DW_ATE_float;
        } else if (primitive->get_type() == primitive_type::CHAR || primitive->get_type() == primitive_type::BYTE) {
            encoding = primitive->is_unsigned() ? llvm::dwarf::DW_ATE_unsigned_char : llvm::dwarf::DW_ATE_signed_char;
        } else if (primitive->is_unsigned()) {
            encoding = llvm::dwarf::DW_ATE_unsigned;
        }

        result = _builder->createBasicType(
            primitive->to_string(),
            primitive->type_size(),
            encoding);
    } else if (auto enum_type_info = std::dynamic_pointer_cast<enum_type>(type_info)) {
        if (enum_type_info->is_object_backed()) {
            result = _builder->createUnspecifiedType(enum_type_info->to_string());
        } else {
            auto underlying = enum_type_info->get_underlying_type();
            auto size_bits = underlying ? underlying->type_size() : get_type_size_bits(data_layout, llvm_type);
            auto align_bits = get_type_align_bits(data_layout, llvm_type);
            auto elements = _builder->getOrCreateArray({});
            result = _builder->createEnumerationType(
                _compile_unit ? static_cast<llvm::DIScope*>(_compile_unit) : static_cast<llvm::DIScope*>(_fallback_file),
                enum_type_info->to_string(),
                _fallback_file,
                0,
                size_bits,
                align_bits,
                elements,
                underlying ? get_or_create_type(underlying) : nullptr);
        }
    } else if (type::is_any_indirection(type_info)) {
        auto* pointee = get_or_create_type(type_info->get_subtype());
        auto* ptr_type = llvm::PointerType::get(_context->llvm_context(), 0);
        result = _builder->createPointerType(
            pointee,
            data_layout.getTypeSizeInBits(ptr_type),
            0,
            std::nullopt,
            type_info->to_string());
    } else if (auto struct_type_info = std::dynamic_pointer_cast<struct_type>(type_info)) {
        result = _builder->createStructType(
            _compile_unit ? static_cast<llvm::DIScope*>(_compile_unit) : static_cast<llvm::DIScope*>(_fallback_file),
            struct_type_info->name(),
            _fallback_file,
            0,
            get_type_size_bits(data_layout, llvm_type),
            get_type_align_bits(data_layout, llvm_type),
            llvm::DINode::FlagZero,
            nullptr,
            _builder->getOrCreateArray({}));
    } else {
        result = _builder->createUnspecifiedType(type_info->to_string());
    }

    _types[type_info.get()] = result;
    return result;
}

llvm::DIScope* debug_info_emitter::attach_function_debug_scope(function& fn, llvm::Function* llvm_fn) {
    if (!_builder || !_compile_unit || !llvm_fn) {
        return nullptr;
    }

    lex::opt_any_lexeme fn_lexeme;
    if (auto ast_fn = fn.get_ast_function_decl()) {
        fn_lexeme = lex::any_lexeme{ast_fn->name};
    }

    auto loc = get_debug_location(*this, _compiler, fn_lexeme, _fallback_file);

    std::vector<llvm::Metadata*> debug_signature;
    debug_signature.push_back(fn.has_return_type() ? get_or_create_type(fn.get_return_type()) : nullptr);
    for (const auto& param : fn.parameters()) {
        debug_signature.push_back(get_or_create_type(param->get_type()));
    }

    auto* fn_type = _builder->createSubroutineType(_builder->getOrCreateTypeArray(debug_signature));
    auto* sp = _builder->createFunction(
        loc.file ? static_cast<llvm::DIScope*>(loc.file) : static_cast<llvm::DIScope*>(_compile_unit),
        fn.get_short_name(),
        fn.get_mangled_name(),
        loc.file ? loc.file : _fallback_file,
        loc.line,
        fn_type,
        loc.line,
        llvm::DINode::FlagPrototyped,
        llvm::DISubprogram::SPFlagDefinition);

    llvm_fn->setSubprogram(sp);
    return sp;
}

llvm::DIScope* debug_info_emitter::create_lexical_block(llvm::DIScope* parent_scope,
                                                        const lex::opt_any_lexeme& lexeme) {
    if (!_builder || !parent_scope) {
        return parent_scope;
    }

    auto loc = get_debug_location(*this, _compiler, lexeme, _fallback_file);
    return _builder->createLexicalBlock(parent_scope, loc.file ? loc.file : _fallback_file, loc.line, loc.col);
}

void debug_info_emitter::declare_parameter(llvm::IRBuilder<>& builder,
                                           parameter& param,
                                           llvm::Value* storage,
                                           unsigned int arg_index,
                                           llvm::DIScope* scope) {
    if (!_builder || !scope || !storage || !storage->getType()->isPointerTy()) {
        return;
    }

    auto lexeme = get_parameter_lexeme(param);
    auto loc = get_debug_location(*this, _compiler, lexeme, _fallback_file);
    auto* variable = _builder->createParameterVariable(
        scope,
        param.get_short_name(),
        arg_index,
        loc.file ? loc.file : _fallback_file,
        loc.line,
        get_or_create_type(param.get_type()),
        true);

    auto* expression = _builder->createExpression();
    auto* debug_loc = llvm::DILocation::get(_context->llvm_context(), loc.line, loc.col, scope);
    if (builder.GetInsertPoint() != builder.GetInsertBlock()->end()) {
        _builder->insertDeclare(storage, variable, expression, debug_loc, &*builder.GetInsertPoint());
    } else {
        _builder->insertDeclare(storage, variable, expression, debug_loc, builder.GetInsertBlock());
    }
}

void debug_info_emitter::declare_local_variable(llvm::IRBuilder<>& builder,
                                                variable_statement& var,
                                                llvm::Value* storage,
                                                llvm::DIScope* scope) {
    if (!_builder || !scope || !storage || !storage->getType()->isPointerTy()) {
        return;
    }

    auto lexeme = get_variable_lexeme(var);
    auto loc = get_debug_location(*this, _compiler, lexeme, _fallback_file);
    auto* variable = _builder->createAutoVariable(
        scope,
        var.get_short_name(),
        loc.file ? loc.file : _fallback_file,
        loc.line,
        get_or_create_type(var.get_type()),
        true);

    auto* expression = _builder->createExpression();
    auto* debug_loc = llvm::DILocation::get(_context->llvm_context(), loc.line, loc.col, scope);
    if (builder.GetInsertPoint() != builder.GetInsertBlock()->end()) {
        _builder->insertDeclare(storage, variable, expression, debug_loc, &*builder.GetInsertPoint());
    } else {
        _builder->insertDeclare(storage, variable, expression, debug_loc, builder.GetInsertBlock());
    }
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




