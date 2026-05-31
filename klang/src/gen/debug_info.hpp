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

#ifndef KLANG_GEN_DEBUG_INFO_HPP
#define KLANG_GEN_DEBUG_INFO_HPP

#include <memory>
#include <string>
#include <unordered_map>

#include <llvm/IR/DIBuilder.h>
#include <llvm/IR/DebugInfoMetadata.h>
#include <llvm/IR/IRBuilder.h>

#include "../compiler.hpp"
#include "../model/context.hpp"
#include "../model/model.hpp"

namespace k::model::gen {

/**
 * Thin wrapper around LLVM DIBuilder for klang-generated DWARF metadata.
 *
 * Bootstrap scope:
 * - compile unit + files
 * - function subprogram metadata
 * - instruction debug locations from lexeme coordinates
 */
class debug_info_emitter {
protected:
    k::compiler& _compiler;
    std::shared_ptr<context> _context;

    std::unique_ptr<llvm::DIBuilder> _builder;
    llvm::DICompileUnit* _compile_unit = nullptr;
    llvm::DIFile* _fallback_file = nullptr;

    std::unordered_map<std::string, llvm::DIFile*> _files;
    std::unordered_map<const type*, llvm::DIType*> _types;

public:
    debug_info_emitter(k::compiler& compiler, std::shared_ptr<context> context)
        : _compiler(compiler), _context(std::move(context)) {}

    bool is_enabled() const { return _compiler.get_debug_info_options().enabled; }

    void initialize(const std::string& unit_name);
    void finalize();

    llvm::DIFile* get_or_create_file(const std::string& path);
    llvm::DIType* get_or_create_type(const std::shared_ptr<type>& type_info);

    /**
     * Create and attach function debug scope metadata to an LLVM function.
     * Returns the created scope or nullptr when debug is disabled.
     */
    llvm::DIScope* attach_function_debug_scope(function& fn, llvm::Function* llvm_fn);

    /** Create a nested lexical scope for a statement block. */
    llvm::DIScope* create_lexical_block(llvm::DIScope* parent_scope,
                                        const lex::opt_any_lexeme& lexeme);

    /** Emit debug metadata for a function parameter stored in local storage. */
    void declare_parameter(llvm::IRBuilder<>& builder,
                           parameter& param,
                           llvm::Value* storage,
                           unsigned int arg_index,
                           llvm::DIScope* scope);

    /** Emit debug metadata for a local variable stored in local storage. */
    void declare_local_variable(llvm::IRBuilder<>& builder,
                                variable_statement& var,
                                llvm::Value* storage,
                                llvm::DIScope* scope);

    /** Set current IRBuilder debug location from a lexical token. */
    void set_current_debug_location(llvm::IRBuilder<>& builder,
                                    const lex::opt_any_lexeme& lexeme,
                                    llvm::DIScope* scope) const;
};

} // namespace k::model::gen

#endif // KLANG_GEN_DEBUG_INFO_HPP



