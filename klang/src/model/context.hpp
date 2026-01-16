/*
 * K Language compiler
 *
 * Copyright 2024 Emilien Kia
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

#ifndef KLANG_CONTEXT_HPP
#define KLANG_CONTEXT_HPP

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "../lex/lexer.hpp"
#include "../parse/ast.hpp"
#include "../parse/parser.hpp"
#include "../common/common.hpp"
#include "type.hpp"


#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>


namespace k::model {

namespace gen {
    class unit_llvm_ir_gen;
}


class context {
protected:
    std::unique_ptr<llvm::LLVMContext> _context;

    std::map<primitive_type::PRIMITIVE_TYPE, std::shared_ptr<primitive_type>> _primitive_types;

    std::map<std::string, std::shared_ptr<struct_type>> _struct_types;

    std::vector<std::shared_ptr<unresolved_type>> _unresolved;

    context();

public:

    static std::shared_ptr<context> create();

    inline llvm::LLVMContext& llvm_context() {return *_context.get();}
    inline llvm::LLVMContext& operator *() {return *_context.get();}

    std::shared_ptr<primitive_type> from_type(primitive_type::PRIMITIVE_TYPE type);

    std::shared_ptr<type> from_string(const std::string& type_name);
    std::shared_ptr<type> from_keyword(const lex::keyword& kw, bool is_unsigned = false);
    std::shared_ptr<type> from_type_specifier(const k::parse::ast::type_specifier& type_spec);
    std::shared_ptr<type> from_literal(const k::lex::any_literal &literal);

    void add_struct(std::shared_ptr<struct_type> st_type);

    llvm::Type* get_llvm_type(const std::shared_ptr<type>& type);

    void resolve_types();

protected:

    std::shared_ptr<unresolved_type> create_unresolved(const name& type_id);
    std::shared_ptr<unresolved_type> create_unresolved(name&& type_id);


    friend class gen::unit_llvm_ir_gen;
    std::unique_ptr<llvm::LLVMContext> move_llvm_context();

private:
    void init();
    void init_primitive_types();
};




} // namespace k::model
#endif //KLANG_CONTEXT_HPP
